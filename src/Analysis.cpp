#include "Analysis.h"

#include "Directory.h"
#include "RmtEngine.h"
#include "RtiFile.h"

#include <SDL3/SDL.h>   // SDL_GetBasePath() for the debug log path

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace Analysis {

const char* Name(Category c)
{
    switch (c) {
        case Category::Bass:        return "Bass";
        case Category::Lead:        return "Lead";
        case Category::LeadVibrato: return "Lead (vibrato)";
        case Category::Arp:         return "Arp";
        case Category::Chord:       return "Chord";
        case Category::Glide:       return "Glide";
        case Category::Pad:         return "Pad";
        case Category::Bell:        return "Bell";
        case Category::Kick:        return "Kick";
        case Category::Snare:       return "Snare";
        case Category::HiHat:       return "HiHat";
        case Category::Perc:        return "Perc";
        case Category::SweptFX:     return "Swept FX";
        case Category::NoiseFX:     return "Noise / FX";
        default:                    return "Other";
    }
}

// ----- Tags ---------------------------------------------------------------

std::string TagsToString(unsigned tags)
{
    if (!tags) return "";
    std::string out;
    auto add = [&](const char* s) {
        if (!out.empty()) out += ',';
        out += s;
    };
    if (tags & Tag_Vibrato)  add("vibrato");
    if (tags & Tag_Bright)   add("bright");
    if (tags & Tag_Dark)     add("dark");
    if (tags & Tag_Loud)     add("loud");
    if (tags & Tag_Quiet)    add("quiet");
    if (tags & Tag_Animated) add("animated");
    if (tags & Tag_HighFreq) add("highfreq");
    if (tags & Tag_ArpAsc)   add("ascending");
    if (tags & Tag_ArpDesc)  add("descending");
    return out;
}

unsigned TagsFromString(const std::string& s)
{
    unsigned t = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        std::string token = s.substr(i, j - i);
        // trim whitespace
        while (!token.empty() && std::isspace((unsigned char)token.front())) token.erase(token.begin());
        while (!token.empty() && std::isspace((unsigned char)token.back()))  token.pop_back();
        if      (token == "vibrato")    t |= Tag_Vibrato;
        else if (token == "bright")     t |= Tag_Bright;
        else if (token == "dark")       t |= Tag_Dark;
        else if (token == "loud")       t |= Tag_Loud;
        else if (token == "quiet")      t |= Tag_Quiet;
        else if (token == "animated")   t |= Tag_Animated;
        else if (token == "highfreq")   t |= Tag_HighFreq;
        else if (token == "ascending")  t |= Tag_ArpAsc;
        else if (token == "descending") t |= Tag_ArpDesc;
        i = j + 1;
    }
    return t;
}

unsigned TagsForInstrument(const TInstrument& ins, const Features* f)
{
    const int* p = ins.parameters;
    unsigned t = 0;
    if (p[PAR_VIBRATO] != 0) t |= Tag_Vibrato;
    if (p[PAR_AUDCTL_15KHZ] || p[PAR_AUDCTL_179_CH1] || p[PAR_AUDCTL_179_CH3])
        t |= Tag_HighFreq;
    // Note-table direction: sign-test the running delta across the used
    // range. A clean ascending / descending pattern picks up both glides
    // and arps.
    int len = p[PAR_TBL_LENGTH];
    int up = 0, down = 0;
    for (int i = 1; i <= len && i < NOTE_TABLE_MAX_LEN; ++i) {
        int a = ins.noteTable[i - 1] & 0xFF;
        int b = ins.noteTable[i]     & 0xFF;
        if (b > a) ++up;
        else if (b < a) ++down;
    }
    if (up >= 2 && down == 0) t |= Tag_ArpAsc;
    if (down >= 2 && up == 0) t |= Tag_ArpDesc;
    if (f && f->valid) {
        float meanRms = (f->rms_early + f->rms_mid + f->rms_late) / 3.0f;
        if (f->centroid > 4500.0f) t |= Tag_Bright;
        if (f->centroid > 0.0f && f->centroid < 1500.0f) t |= Tag_Dark;
        if (meanRms > 0.20f)       t |= Tag_Loud;
        if (meanRms < 0.05f)       t |= Tag_Quiet;
        if (f->flux > 0.20f)       t |= Tag_Animated;
    }
    return t;
}

std::vector<std::string> Names()
{
    std::vector<std::string> v;
    for (int i = 0; i < (int)Category::COUNT; ++i) v.push_back(Name((Category)i));
    return v;
}

std::uint64_t HashAta(const std::vector<byte>& ata)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (byte b : ata) { h ^= b; h *= 1099511628211ULL; }
    return h;
}

namespace {

// ----- Signal extractors used by the classifier ---------------------------

// Note-table character signals. Returns the count of distinct values
// across the used range; the classifier uses this to split Arp (2-3
// distinct - trill / short pattern), Chord (4+ distinct - true multi-note
// arp / chord stab), and Glide (monotonic drift - portamento).
int NoteTableDistinctCount(const TInstrument& ins)
{
    int len = ins.parameters[PAR_TBL_LENGTH];
    if (len < 1) return 1;
    int distinct = 0;
    int seen[256] = { 0 };
    for (int i = 0; i <= len && i < NOTE_TABLE_MAX_LEN; ++i) {
        int v = ins.noteTable[i] & 0xFF;
        if (!seen[v]) { seen[v] = 1; ++distinct; }
    }
    return distinct;
}

// True if the note table moves strictly in one direction across the used
// range (allows brief plateaus). Used to detect glides / portamenti.
bool NoteTableMonotonic(const TInstrument& ins, bool& descends)
{
    int len = ins.parameters[PAR_TBL_LENGTH];
    if (len < 2) { descends = false; return false; }
    int up = 0, down = 0;
    for (int i = 1; i <= len && i < NOTE_TABLE_MAX_LEN; ++i) {
        int a = ins.noteTable[i - 1] & 0xFF;
        int b = ins.noteTable[i]     & 0xFF;
        if (b > a) ++up;
        else if (b < a) ++down;
    }
    descends = (down > up);
    return (up >= 2 && down == 0) || (down >= 2 && up == 0);
}

// True if the filter envelope (FILTER row) is not constant across the used
// envelope columns - i.e. there's a real sweep happening over the lifetime
// of the sound (the canonical "wow" / sci-fi pew effect on POKEY).
bool HasFilterSweep(const TInstrument& ins)
{
    int envLen = ins.parameters[PAR_ENV_LENGTH];
    if (envLen < 2) return false;
    int first = ins.envelope[0][FILTER];
    for (int c = 1; c <= envLen && c < ENVELOPE_MAX_COLUMNS; ++c) {
        if (ins.envelope[c][FILTER] != first) return true;
    }
    return false;
}

// Compute the dominant distortion code (0x00..0x0E) across [first..last]
// envelope columns inclusive. Used to spot transient noise->tone or
// tone->noise transitions inside a single instrument (drum snares hit with
// pulse then ring out as noise, for example).
int DominantDistortionRange(const TInstrument& ins, int first, int last)
{
    int counts[16] = { 0 };
    for (int c = first; c <= last && c < ENVELOPE_MAX_COLUMNS; ++c) {
        int d = ins.envelope[c][DISTORTION] & 0x0E;
        counts[d & 0x0F]++;
    }
    int dom = 0, best = -1;
    for (int d = 0; d < 16; ++d) if (counts[d] > best) { best = counts[d]; dom = d; }
    return dom;
}

inline bool IsNoiseDist(int d) { return d == 0x0 || d == 0x8; }
inline bool IsPureToneDist(int d) { return d == 0x0A; }

// Case-insensitive substring search.
bool ContainsCI(const std::string& haystack, const char* needle)
{
    std::string h; h.reserve(haystack.size());
    for (char c : haystack) h.push_back((char)std::tolower((unsigned char)c));
    std::string n; n.reserve(std::strlen(needle));
    for (const char* p = needle; *p; ++p) n.push_back((char)std::tolower((unsigned char)*p));
    return h.find(n) != std::string::npos;
}

// Best-effort category guess from common naming conventions in user
// libraries. Used as a tie-breaker only when the parameter heuristics would
// otherwise return Other - it never overrides a confident parametric match.
Category CategoryFromFilename(const std::string& fname)
{
    if (fname.empty()) return Category::Other;
    // Strip the directory (basename only) so "leads/bass.rti" matches Bass,
    // not nothing.
    std::string name = fs::path(fname).filename().string();
    // Order matters: more specific tokens before generic ones.
    if (ContainsCI(name, "kick") || ContainsCI(name, "bdrum") ||
        ContainsCI(name, "bass drum") || ContainsCI(name, "bd"))   return Category::Kick;
    if (ContainsCI(name, "snare") || ContainsCI(name, "sdrum") ||
        ContainsCI(name, "sd"))                                    return Category::Snare;
    if (ContainsCI(name, "hat") || ContainsCI(name, "hihat") ||
        ContainsCI(name, " hh") || ContainsCI(name, "cymb") ||
        ContainsCI(name, "shaker"))                                return Category::HiHat;
    if (ContainsCI(name, "perc") || ContainsCI(name, "clap") ||
        ContainsCI(name, "tom") || ContainsCI(name, "wood"))       return Category::Perc;
    if (ContainsCI(name, "bass"))                                  return Category::Bass;
    if (ContainsCI(name, "arp") || ContainsCI(name, "chord"))      return Category::Arp;
    if (ContainsCI(name, "bell") || ContainsCI(name, "mallet") ||
        ContainsCI(name, "chime"))                                 return Category::Bell;
    if (ContainsCI(name, "pad") || ContainsCI(name, "string") ||
        ContainsCI(name, "choir"))                                 return Category::Pad;
    if (ContainsCI(name, "lead") || ContainsCI(name, "solo"))      return Category::Lead;
    if (ContainsCI(name, "sweep") || ContainsCI(name, "riser") ||
        ContainsCI(name, " fx"))                                   return Category::SweptFX;
    if (ContainsCI(name, "noise"))                                 return Category::NoiseFX;
    return Category::Other;
}

// ----- Audio rendering + feature extraction ------------------------------

constexpr int kRenderSampleRate = 44100;
constexpr int kRenderSamples    = 8192;    // ~186 ms at 44.1 kHz
constexpr int kFftSize          = 1024;    // small radix-2 FFT
constexpr int kFftFrames        = 3;       // start / mid / end snapshots
constexpr int kRenderTrack      = 0;
constexpr int kRenderSlot       = 0;
constexpr int kRenderNote       = 24;      // mid-range chromatic note
constexpr int kRenderVolume     = 15;

// In-place iterative radix-2 FFT. N must be a power of 2. Phase convention
// matches numpy.fft.fft (negative exponent). Cheap and self-contained so
// we don't pull in another dependency just for instrument analysis.
void FFT(float* re, float* im, int N)
{
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    // Cooley-Tukey butterflies.
    constexpr float kPi = 3.14159265358979323846f;
    for (int len = 2; len <= N; len <<= 1) {
        float theta = -2.0f * kPi / (float)len;
        float wlre  = std::cos(theta);
        float wlim  = std::sin(theta);
        int half = len >> 1;
        for (int i = 0; i < N; i += len) {
            float wre = 1.0f, wim = 0.0f;
            for (int k = 0; k < half; ++k) {
                int a = i + k;
                int b = a + half;
                float tre = re[b] * wre - im[b] * wim;
                float tim = re[b] * wim + im[b] * wre;
                re[b] = re[a] - tre;
                im[b] = im[a] - tim;
                re[a] = re[a] + tre;
                im[a] = im[a] + tim;
                float new_wre = wre * wlre - wim * wlim;
                wim = wre * wlim + wim * wlre;
                wre = new_wre;
            }
        }
    }
}

// Compute the FFT magnitude spectrum of a window of 8-bit unsigned PCM
// starting at `samples + offset`. POKEY output is centred at 0x80; we
// subtract that to make it bipolar before transforming.
void MagnitudeAt(const byte* samples, int total, int offset,
                 std::vector<float>& mag_out)
{
    mag_out.assign(kFftSize / 2, 0.0f);
    if (offset + kFftSize > total) return;
    static thread_local std::vector<float> re, im;
    re.assign(kFftSize, 0.0f);
    im.assign(kFftSize, 0.0f);
    // Hann window reduces spectral leakage so the centroid isn't dragged
    // by edge discontinuities.
    constexpr float kPi = 3.14159265358979323846f;
    for (int i = 0; i < kFftSize; ++i) {
        float s = ((float)samples[offset + i] - 128.0f) / 128.0f;
        float w = 0.5f - 0.5f * std::cos(2.0f * kPi * i / (kFftSize - 1));
        re[i] = s * w;
    }
    FFT(re.data(), im.data(), kFftSize);
    for (int k = 0; k < kFftSize / 2; ++k) {
        mag_out[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]);
    }
}

float SpectralCentroid(const std::vector<float>& mag)
{
    double num = 0, den = 0;
    for (size_t i = 0; i < mag.size(); ++i) {
        float freq = (float)i * (float)kRenderSampleRate / (float)kFftSize;
        num += freq * mag[i];
        den += mag[i];
    }
    return den > 0.0 ? (float)(num / den) : 0.0f;
}

float SpectralRolloff(const std::vector<float>& mag, float pct = 0.85f)
{
    double total = 0;
    for (float m : mag) total += (double)m * m;
    if (total <= 0.0) return 0.0f;
    double threshold = pct * total;
    double cumulative = 0;
    for (size_t i = 0; i < mag.size(); ++i) {
        cumulative += (double)mag[i] * mag[i];
        if (cumulative >= threshold) {
            return (float)i * (float)kRenderSampleRate / (float)kFftSize;
        }
    }
    return (float)kRenderSampleRate / 2.0f;
}

// L1 distance between two magnitude spectra, normalised by total energy.
float FrameDistance(const std::vector<float>& a, const std::vector<float>& b)
{
    double diff = 0, total = 0;
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        diff  += std::fabs(a[i] - b[i]);
        total += a[i] + b[i];
    }
    return total > 0.0 ? (float)(diff / total) : 0.0f;
}

} // anonymous namespace

namespace {

// Discard-buffer Generate used to advance the RMT driver by a handful of
// VBI frames - lets it "process" a Silence or LoadInstrumentSlot before we
// fire a NoteOn. Without this the driver's RMT_ATA_INSTROFF flag (set by
// Silence) is still active when SETNOTEINSTR runs, the track stays off,
// and the subsequent Generate fills the buffer with silence (0x80) -> all
// features come back zero.
void WarmupFrames(RmtEngine& engine)
{
    byte sink[2048];   // ~46 ms at 44.1k SR (~2 PAL VBI frames)
    engine.Generate(sink, 2048);
}

// (Previously the per-pitch render + feature computation lived here,
// but audio-feature extraction is disabled in Analysis v6 because POKEY
// would not render reliably from inside Analysis::Run. The helper bodies
// above - FFT, MagnitudeAt, SpectralCentroid, SpectralRolloff,
// FrameDistance, WarmupFrames - are kept for a future re-enable.)

} // anonymous namespace

Features ExtractFeatures(RmtEngine& /*engine*/, const std::vector<byte>& /*ata*/)
{
    // Audio-feature extraction is disabled (Analysis v6). Every Pokey
    // render attempted from inside the analysis flow returned 0x80
    // silence regardless of how we routed it; the classifier falls
    // back to parametric-only signals. The unused FFT/RMS helpers in
    // this file are kept in source so a future re-enable doesn't have
    // to re-derive them.
    return Features{};
}

// Heuristic instrument classifier (v3). The decision tree is first-match-
// wins (top-down). Documented in detail in the Readme's "How categorisation
// works" section; the inline comments below name the signal each step is
// reading. `filename` is consulted only as a fall-back when the parameter
// rules would otherwise yield Other. `features` are audio-rendered signals
// (RMS profile, ZCR, peak position, spectral centroid/rolloff/flux); when
// present they confirm or override the parametric guess at a few key
// branches - e.g. a high ZCR forces noise-FX even if the dominant
// distortion looked tonal, and a percussive RMS shape promotes Perc.
Category Classify(const TInstrument& ins, const std::string& filename,
                  const Features* features, int* out_confidence)
{
    // Confidence is just a running count of how many independent signals
    // voted for the eventual winner. It's not a probability - it's a
    // "how loud was the agreement" measure (0..~5). The UI uses it to
    // grey out low-confidence rows.
    int confidence = 0;
    auto give = [&](Category c, int conf) -> Category {
        if (out_confidence) *out_confidence = conf;
        return c;
    };

    const int* p = ins.parameters;
    int envLen = p[PAR_ENV_LENGTH];

    // ---- Pre-compute the signals once --------------------------------
    bool join     = p[PAR_AUDCTL_JOIN_1_2] || p[PAR_AUDCTL_JOIN_3_4];
    bool hi15k    = p[PAR_AUDCTL_15KHZ] != 0;
    bool hi179c1  = p[PAR_AUDCTL_179_CH1] != 0;
    bool hi179c3  = p[PAR_AUDCTL_179_CH3] != 0;
    bool hiFreq   = hi15k || hi179c1 || hi179c3;
    bool vibrato  = p[PAR_VIBRATO] != 0;
    bool loops    = p[PAR_ENV_GOTO] < envLen;
    bool fastFade = p[PAR_VOL_FADEOUT] >= 8;
    int  distinctNotes = NoteTableDistinctCount(ins);
    bool descends     = false;
    bool monotonic    = NoteTableMonotonic(ins, descends);
    bool isChord      = distinctNotes >= 4;
    bool isArp        = !isChord && distinctNotes >= 2 && p[PAR_TBL_LENGTH] >= 2;
    bool isGlide      = monotonic && !isArp && !isChord;
    bool filtSwp  = HasFilterSweep(ins);

    int dom       = DominantDistortionRange(ins, 0, envLen);
    bool noise    = IsNoiseDist(dom);
    bool tone     = IsPureToneDist(dom);

    // Distortion at the very start vs the rest of the envelope - drum
    // transients usually attack with one distortion and "ring out" with
    // another. Computed over the first 1-2 columns vs the remainder.
    int distStart = (envLen > 0)
                    ? (ins.envelope[0][DISTORTION] & 0x0E)
                    : dom;
    int tailFirst = std::min(2, envLen);
    int distTail  = (envLen >= 2)
                    ? DominantDistortionRange(ins, tailFirst, envLen)
                    : distStart;
    bool transient = (distStart != distTail);

    // ---- Audio-rendered signals (optional; only set when features valid) -
    // Each flag is conservative - we only trust the audio when it disagrees
    // strongly with the parametric guess, so a bad render can't flip
    // categories on its own.
    bool fx_valid    = features && features->valid;
    bool fx_noisy    = fx_valid && features->zcr      > 0.40f;  // high zero-crossings = noise
    bool fx_quiet    = fx_valid && features->rms_mid  < 0.03f;  // silent in middle = pluck/percussion
    bool fx_percShape= fx_valid && features->peak_pos < 0.20f &&
                       features->rms_late < features->rms_early * 0.5f;
    bool fx_sustain  = fx_valid && features->rms_late > 0.10f &&
                       features->rms_late > features->rms_early * 0.5f;
    bool fx_bright   = fx_valid && features->centroid > 4500.0f;
    bool fx_animated = fx_valid && features->flux     > 0.20f;

    // ---- Decision tree (first match wins) ---------------------------------
    //
    // 1. Channel-join is the canonical bass signature regardless of anything
    //    else (POKEY pairs channels into a 16-bit-period voice).
    if (join) return give(Category::Bass, 3);

    // 2. Note table characterises the instrument:
    //      4+ distinct values         -> Chord (chord stab / multi-note arp)
    //      2-3 distinct + table len>=2 -> Arp   (trill / short pattern)
    //      monotonic single direction  -> Glide (portamento / slide)
    if (isChord) {
        confidence = 3;
        if (loops || !fastFade) ++confidence;
        return give(Category::Chord, confidence);
    }
    if (isArp) {
        confidence = 2;
        if (!fastFade) ++confidence;
        return give(Category::Arp, confidence);
    }
    if (isGlide) return give(Category::Glide, 2);

    // 3. High-frequency POKEY modes (15 kHz divider, 1.79 MHz clock on
    //    channels 1/3) produce the characteristic bright/bell-like tones.
    //    Catch these before the generic Lead bucket would swallow them.
    if (hiFreq && tone) {
        confidence = 2;
        if (fx_bright) ++confidence;
        return give(Category::Bell, confidence);
    }

    // 4. Drum kit detection. Sound is short (envLen <= ~5) with a fast
    //    fade-out, OR audio says it's clearly percussive (sharp attack with
    //    a quiet tail) - the audio-side branch catches drum sounds that
    //    have unusual envelope lengths.
    if ((envLen <= 5 && fastFade) || fx_percShape) {
        // Snare: starts with a pulse hit then rings out as noise (or vice
        // versa). The transient flag is the cleanest signature.
        if (transient && (IsNoiseDist(distStart) || IsNoiseDist(distTail))) {
            confidence = 2;
            if (fx_percShape) ++confidence;
            return give(Category::Snare, confidence);
        }
        // HiHat: pure noise, very short. Hats and shakers are tiny.
        // High ZCR confirms it when distortion is ambiguous.
        if ((noise || fx_noisy) && envLen <= 2) {
            confidence = 2;
            if (fx_noisy) ++confidence;
            return give(Category::HiHat, confidence);
        }
        // Kick: pulse-led transient that dies on noise. Falling pitch in
        // the note table is the canonical "808 kick" signature - bump
        // confidence when it's there.
        if (!noise && envLen >= 2 && envLen <= 5) {
            confidence = 2;
            if (monotonic && descends) ++confidence;   // pitch sweep down
            if (fx_percShape) ++confidence;
            return give(Category::Kick, confidence);
        }
        return give(Category::Perc, fx_percShape ? 2 : 1);
    }

    // 5. Longer noise-dominant sound that wasn't caught as a drum hit -
    //    riser, sweep, or general FX. Filter-sweeping FX gets its own
    //    bucket so they don't drown the noise / FX listing. fx_animated
    //    (high spectral flux) is a strong indicator of a swept / morphing
    //    sound when the FILTER row didn't move.
    if ((filtSwp || fx_animated) && !loops) {
        confidence = filtSwp && fx_animated ? 3 : 2;
        return give(Category::SweptFX, confidence);
    }
    if (noise || fx_noisy) {
        confidence = noise && fx_noisy ? 3 : 2;
        return give(Category::NoiseFX, confidence);
    }

    // 6. Sustained tonal sound: looping envelope, not fading quickly, OR
    //    audio confirms a sustained tail with non-trivial mid-window RMS.
    if ((loops && !fastFade) || fx_sustain) {
        confidence = (loops && !fastFade && fx_sustain) ? 3 : 2;
        return give(Category::Pad, confidence);
    }

    // 7. Bell-like: bright spectral centroid even if AUDCTL didn't flag
    //    the high-frequency mode. Catches mallet / metallic sounds that
    //    achieve brightness through high-pitched note tables.
    if (fx_bright && tone) return give(Category::Bell, 2);

    // 8. Pure-tone melodic voice. Vibrato separates the two sub-types so
    //    users can pick lead voices that "sing" vs those that hold steady.
    if (tone && vibrato) return give(Category::LeadVibrato, 2);
    if (tone)            return give(Category::Lead, 2);

    // 9. Audio fallback when no parametric rule fired but features are
    //    decisive (very noisy / very percussive / very sustained). These
    //    are inherently low-confidence guesses.
    if (fx_noisy)     return give(Category::NoiseFX, 1);
    if (fx_percShape) return give(Category::Perc, 1);
    if (fx_sustain)   return give(Category::Pad, 1);

    // 10. Filename hint - last resort, only when nothing else fired. Many
    //     libraries follow naming conventions that survive when the
    //     parameter heuristics can't decide.
    Category hint = CategoryFromFilename(filename);
    if (hint != Category::Other) return give(hint, 1);

    return give(Category::Other, 0);
}

namespace {

std::string RelPath(const std::string& abs, const std::string& root)
{
    std::error_code ec;
    fs::path r = fs::relative(fs::path(abs), fs::path(root), ec);
    if (ec || r.empty()) return abs;
    return r.generic_string();
}

std::string JsonEscape(const std::string& s)
{
    std::string o;
    for (char c : s) {
        if (c == '\\' || c == '"') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}

std::string JsonUnescape(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            o += (n == 'n') ? '\n' : n;
        } else o += s[i];
    }
    return o;
}

// Pull a "key":value from one JSON object line. Strings only.
bool Field(const std::string& line, const std::string& key, std::string& out)
{
    std::string needle = "\"" + key + "\"";
    size_t k = line.find(needle);
    if (k == std::string::npos) return false;
    size_t colon = line.find(':', k + needle.size());
    if (colon == std::string::npos) return false;
    size_t q1 = line.find('"', colon + 1);
    if (q1 == std::string::npos) return false;
    std::string raw;
    for (size_t i = q1 + 1; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) { raw += line[i]; raw += line[i + 1]; i++; continue; }
        if (line[i] == '"') break;
        raw += line[i];
    }
    out = JsonUnescape(raw);
    return true;
}

int CategoryIndexFromName(const std::string& n)
{
    auto names = Names();
    for (int i = 0; i < (int)names.size(); ++i) if (names[i] == n) return i;
    return -1;
}

fs::path JsonPath(const std::string& root) { return fs::path(root) / "analysis.json"; }

// ----- K-means over audio features ---------------------------------------
//
// Inputs: one 8-D feature vector per instrument (the Features struct,
// minus the `valid` flag). Output: a cluster id per instrument in [0,k).
// k is chosen as ceil(sqrt(N/2)) clamped to [3, 12] - small enough that
// the UI's tab list stays sane, large enough that big libraries get
// useful sub-groups. Initialised with k-means++ for stability;
// converges in a fixed budget of 50 iterations (typically <10 in
// practice).

constexpr int kFeatureDims = 8;

void FillVec(const Features& f, float v[kFeatureDims])
{
    v[0] = f.rms_early;
    v[1] = f.rms_mid;
    v[2] = f.rms_late;
    v[3] = f.zcr;
    v[4] = f.peak_pos;
    v[5] = f.centroid / 22050.0f;   // normalise Hz to ~[0,1] at 44.1k SR
    v[6] = f.rolloff  / 22050.0f;
    v[7] = f.flux;
}

float SqDist(const float* a, const float* b)
{
    float d = 0;
    for (int i = 0; i < kFeatureDims; ++i) {
        float x = a[i] - b[i];
        d += x * x;
    }
    return d;
}

std::vector<int> RunKMeans(const std::vector<std::array<float, kFeatureDims>>& X,
                           int k, int iterations = 50)
{
    int n = (int)X.size();
    std::vector<int> assign(n, 0);
    if (n == 0 || k <= 0) return assign;
    if (k > n) k = n;

    // Standardise each dimension to unit variance so distances aren't
    // dominated by whichever feature has the widest raw range.
    std::array<float, kFeatureDims> mean{}, stddev{};
    for (const auto& v : X) for (int d = 0; d < kFeatureDims; ++d) mean[d] += v[d];
    for (int d = 0; d < kFeatureDims; ++d) mean[d] /= (float)n;
    for (const auto& v : X) for (int d = 0; d < kFeatureDims; ++d) {
        float diff = v[d] - mean[d];
        stddev[d] += diff * diff;
    }
    for (int d = 0; d < kFeatureDims; ++d)
        stddev[d] = std::sqrt(stddev[d] / (float)n);
    auto normalise = [&](std::array<float, kFeatureDims>& v) {
        for (int d = 0; d < kFeatureDims; ++d) {
            v[d] = stddev[d] > 1e-6f ? (v[d] - mean[d]) / stddev[d] : 0.0f;
        }
    };

    std::vector<std::array<float, kFeatureDims>> Z = X;
    for (auto& v : Z) normalise(v);

    // k-means++ seeding: first centroid is point 0 (deterministic); each
    // subsequent centroid is the point farthest from any existing one.
    std::vector<std::array<float, kFeatureDims>> centroids;
    centroids.push_back(Z[0]);
    for (int c = 1; c < k; ++c) {
        int best_i = -1;
        float best_d = -1;
        for (int i = 0; i < n; ++i) {
            float min_d = 1e30f;
            for (const auto& cc : centroids) {
                float d = SqDist(Z[i].data(), cc.data());
                if (d < min_d) min_d = d;
            }
            if (min_d > best_d) { best_d = min_d; best_i = i; }
        }
        if (best_i < 0) break;
        centroids.push_back(Z[best_i]);
    }
    if ((int)centroids.size() < k) k = (int)centroids.size();

    // Iterate: assign -> recompute -> repeat until no assignment changes
    // or we hit the iteration cap.
    for (int it = 0; it < iterations; ++it) {
        bool any_change = false;
        for (int i = 0; i < n; ++i) {
            int best = 0;
            float best_d = SqDist(Z[i].data(), centroids[0].data());
            for (int c = 1; c < k; ++c) {
                float d = SqDist(Z[i].data(), centroids[c].data());
                if (d < best_d) { best_d = d; best = c; }
            }
            if (assign[i] != best) { assign[i] = best; any_change = true; }
        }
        if (!any_change) break;

        std::vector<std::array<float, kFeatureDims>> sums(k);
        std::vector<int> counts(k, 0);
        for (int i = 0; i < n; ++i) {
            int c = assign[i];
            for (int d = 0; d < kFeatureDims; ++d) sums[c][d] += Z[i][d];
            ++counts[c];
        }
        for (int c = 0; c < k; ++c) {
            if (counts[c] == 0) continue;   // empty cluster - leave centroid
            for (int d = 0; d < kFeatureDims; ++d)
                centroids[c][d] = sums[c][d] / (float)counts[c];
        }
    }
    return assign;
}

int KForN(int n)
{
    if (n <= 6) return 0;   // too few to cluster meaningfully
    int k = (int)std::ceil(std::sqrt((double)n / 2.0));
    if (k < 3)  k = 3;
    if (k > 12) k = 12;
    return k;
}

} // anonymous namespace

Summary Run(Directory& dir, const std::string& libraryRoot, bool writeJson,
            const Options& opts)
{
    RmtEngine* engine = opts.engine;
    Summary sum;

    struct Entry {
        int node;
        std::string rel;
        std::string name;
        std::uint64_t hash;
        int len;
        Category cat;
        int  confidence;
        unsigned tags;
        int  cluster_id;
        bool dup;
        std::string dup_of;
        Features feats;
    };
    std::vector<Entry> entries;
    std::unordered_map<std::uint64_t, int> first_seen; // hash -> entries index

    int total_files = (int)dir.AllFiles().size();
    int processed = 0;
    for (int node : dir.AllFiles()) {
        ++processed;
        if (opts.progress) opts.progress(processed, total_files, opts.progress_ud);

        const auto& n = dir.At(node);
        RtiFile rti;
        if (!rti.LoadFromFile(n.path.c_str()) || !rti.Valid()) continue;

        TInstrument ins{};
        rti.ToInstrument(ins, /*stereo=*/false);

        // Render the instrument and pull audio features if the caller gave
        // us an engine. Caller is responsible for having paused audio so
        // we have exclusive access to the engine's Generate() path.
        Features feats;
        if (engine) feats = ExtractFeatures(*engine, rti.AtaBlob());

        Entry e;
        e.node = node;
        e.rel  = RelPath(n.path, libraryRoot);
        e.name = rti.Name();
        e.hash = HashAta(rti.AtaBlob());
        e.len  = (int)rti.AtaBlob().size();
        e.confidence = 0;
        e.cluster_id = -1;
        e.feats = feats;
        e.cat  = Classify(ins, n.path, feats.valid ? &feats : nullptr,
                          &e.confidence);
        e.tags = TagsForInstrument(ins, feats.valid ? &feats : nullptr);
        e.dup  = false;

        auto it = first_seen.find(e.hash);
        if (it == first_seen.end()) {
            first_seen[e.hash] = (int)entries.size();
        } else {
            e.dup = true;
            e.dup_of = entries[it->second].rel;
            sum.duplicates++;
        }
        entries.push_back(e);
    }

    // Run k-means over the feature vectors of all non-duplicate entries
    // that have valid audio features. Duplicates inherit the cluster id of
    // their representative so they still group correctly when the user
    // toggles "Show all".
    int k = 0;
    if (engine) {
        std::vector<std::array<float, kFeatureDims>> X;
        std::vector<int> idx_for_X;
        for (int i = 0; i < (int)entries.size(); ++i) {
            if (entries[i].dup || !entries[i].feats.valid) continue;
            std::array<float, kFeatureDims> v;
            FillVec(entries[i].feats, v.data());
            X.push_back(v);
            idx_for_X.push_back(i);
        }
        k = opts.k_override > 0 ? std::min(opts.k_override, (int)X.size())
                                : KForN((int)X.size());
        if (k > 0) {
            std::vector<int> assign = RunKMeans(X, k);
            for (size_t i = 0; i < idx_for_X.size(); ++i) {
                entries[idx_for_X[i]].cluster_id = assign[i];
            }
            // Propagate cluster ids from the first-seen entry to its dups.
            for (auto& e : entries) {
                if (!e.dup) continue;
                auto it = first_seen.find(e.hash);
                if (it != first_seen.end()) e.cluster_id = entries[it->second].cluster_id;
            }
        }
    }
    sum.clusters = k;

    // Apply to the directory.
    dir.SetCategoryNames(Names());
    dir.SetClusterCount(k);
    for (const auto& e : entries) {
        dir.SetFileAnalysis(e.node, (int)e.cat, e.dup);
        dir.SetFileExtras(e.node, e.tags, e.confidence, e.cluster_id);
        float audio[8] = {
            e.feats.rms_early, e.feats.rms_mid, e.feats.rms_late,
            e.feats.zcr, e.feats.peak_pos,
            e.feats.centroid, e.feats.rolloff, e.feats.flux
        };
        dir.SetFileFeatures(e.node, audio, e.feats.valid);
    }
    dir.RebuildViews();

    sum.total = (int)entries.size();
    sum.ok = true;

    if (writeJson) {
        std::ofstream out(JsonPath(libraryRoot), std::ios::trunc);
        if (out) {
            // "version" is the classifier version, NOT a file-format version
            // - bumping kAnalysisVersion invalidates older caches on load.
            // No "library" field: instrument paths are stored relative to
            // the directory holding analysis.json, so the whole library
            // folder is free to move anywhere on disk and the cache still
            // resolves correctly.
            out << "{\n  \"version\": " << kAnalysisVersion << ",\n";
            out << "  \"clusters\": " << k << ",\n";
            out << "  \"instruments\": [\n";
            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                char hashbuf[24];
                std::snprintf(hashbuf, sizeof(hashbuf), "%016llx",
                              (unsigned long long)e.hash);
                // 8 audio features collapsed into a single comma-separated
                // string to keep the per-row JSON readable. Empty when no
                // engine was supplied during analysis.
                std::string featStr;
                if (e.feats.valid) {
                    char tmp[160];
                    std::snprintf(tmp, sizeof(tmp),
                        "%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.3f",
                        e.feats.rms_early, e.feats.rms_mid, e.feats.rms_late,
                        e.feats.zcr, e.feats.peak_pos,
                        e.feats.centroid, e.feats.rolloff, e.feats.flux);
                    featStr = tmp;
                }
                out << "    {\"path\": \"" << JsonEscape(e.rel)
                    << "\", \"name\": \"" << JsonEscape(e.name)
                    << "\", \"hash\": \"" << hashbuf
                    << "\", \"len\": " << e.len
                    << ", \"category\": \"" << Name(e.cat)
                    << "\", \"confidence\": " << e.confidence
                    << ", \"cluster\": " << e.cluster_id
                    << ", \"tags\": \"" << JsonEscape(TagsToString(e.tags))
                    << "\", \"features\": \"" << featStr
                    << "\", \"manual\": \"" << JsonEscape(
                        dir.GetFileManualCategory(e.node) >= 0
                            ? Name((Category)dir.GetFileManualCategory(e.node))
                            : "")
                    << "\", \"duplicate_of\": \"" << JsonEscape(e.dup_of)
                    << "\"}" << (i + 1 < entries.size() ? "," : "") << "\n";
            }
            out << "  ]\n}\n";
        }

        // Side-by-side CSV report. Same data, but Excel- / pandas-friendly
        // for users who want to slice their library outside the app.
        std::ofstream csv(fs::path(libraryRoot) / "analysis_report.csv",
                          std::ios::trunc);
        if (csv) {
            csv << "path,category,confidence,cluster,duplicate,tags,"
                   "rms_early,rms_mid,rms_late,zcr,peak_pos,centroid_hz,rolloff_hz,flux\n";
            for (const auto& e : entries) {
                csv << '"';
                for (char c : e.rel) { if (c == '"') csv << '"'; csv << c; }
                csv << "\","
                    << Name(e.cat) << ','
                    << e.confidence << ','
                    << e.cluster_id << ','
                    << (e.dup ? 1 : 0) << ',';
                csv << '"' << TagsToString(e.tags) << "\",";
                if (e.feats.valid) {
                    char tmp[200];
                    std::snprintf(tmp, sizeof(tmp),
                        "%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.3f",
                        e.feats.rms_early, e.feats.rms_mid, e.feats.rms_late,
                        e.feats.zcr, e.feats.peak_pos,
                        e.feats.centroid, e.feats.rolloff, e.feats.flux);
                    csv << tmp;
                } else {
                    csv << ",,,,,,,";
                }
                csv << '\n';
            }
        }
    }
    return sum;
}

// Parse the integer that follows `"version"` in a JSON line. Returns 0 if
// the line doesn't contain the version field or the value isn't numeric -
// 0 is fine as "not the current version" because kAnalysisVersion is always
// >= 1.
int ParseVersionFromLine(const std::string& line)
{
    size_t k = line.find("\"version\"");
    if (k == std::string::npos) return 0;
    size_t colon = line.find(':', k);
    if (colon == std::string::npos) return 0;
    size_t i = colon + 1;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    int v = 0;
    bool got = false;
    while (i < line.size() && std::isdigit((unsigned char)line[i])) {
        v = v * 10 + (line[i] - '0');
        ++i;
        got = true;
    }
    return got ? v : 0;
}

bool LoadAndApply(Directory& dir, const std::string& libraryRoot)
{
    std::ifstream in(JsonPath(libraryRoot));
    if (!in) return false;

    // Slurp the whole file so we can check the version before applying any
    // instrument rows. analysis.json is tiny (one line per instrument)
    // even for huge libraries; the memory cost is negligible.
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(std::move(line));

    // Locate the "version": <int> header. If it's missing or doesn't match
    // the current classifier version, treat the cache as absent so the
    // caller (LoadOrRunAnalysis) auto-reruns analysis on the up-to-date
    // rules.
    int file_version = 0;
    for (const auto& l : lines) {
        file_version = ParseVersionFromLine(l);
        if (file_version != 0) break;
    }
    if (file_version != kAnalysisVersion) {
        std::printf("analysis.json: version %d, expected %d - regenerating\n",
                    file_version, kAnalysisVersion);
        return false;
    }

    // Map relative path -> file node.
    std::unordered_map<std::string, int> by_rel;
    for (int node : dir.AllFiles()) {
        by_rel[RelPath(dir.At(node).path, libraryRoot)] = node;
    }

    // Parse a numeric "clusters": N header (default 0). Same shape as the
    // version parser, just a different key.
    int clusters = 0;
    for (const auto& l : lines) {
        size_t k = l.find("\"clusters\"");
        if (k == std::string::npos) continue;
        size_t colon = l.find(':', k);
        if (colon == std::string::npos) continue;
        size_t i = colon + 1;
        while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) ++i;
        bool got = false;
        while (i < l.size() && std::isdigit((unsigned char)l[i])) {
            clusters = clusters * 10 + (l[i] - '0');
            ++i;
            got = true;
        }
        if (got) break;
    }

    dir.SetCategoryNames(Names());
    dir.SetClusterCount(clusters);
    bool applied = false;
    for (const auto& l : lines) {
        std::string path;
        if (!Field(l, "path", path)) continue; // not an instrument row
        std::string cat, dup_of, tags, manual;
        Field(l, "category",     cat);
        Field(l, "duplicate_of", dup_of);
        Field(l, "tags",         tags);
        Field(l, "manual",       manual);

        // Parse the bare-integer fields ("confidence", "cluster"). Field()
        // only handles strings; for numbers we scan inline.
        auto parse_int = [&](const char* key, int dflt) {
            std::string needle = std::string("\"") + key + "\"";
            size_t k = l.find(needle);
            if (k == std::string::npos) return dflt;
            size_t colon = l.find(':', k);
            if (colon == std::string::npos) return dflt;
            size_t i = colon + 1;
            while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) ++i;
            bool neg = false;
            if (i < l.size() && l[i] == '-') { neg = true; ++i; }
            int v = 0; bool got = false;
            while (i < l.size() && std::isdigit((unsigned char)l[i])) {
                v = v * 10 + (l[i] - '0');
                ++i;
                got = true;
            }
            if (!got) return dflt;
            return neg ? -v : v;
        };
        int confidence = parse_int("confidence", 0);
        int cluster_id = parse_int("cluster", -1);

        // Parse the comma-separated feature string. Empty => no audio
        // analysis was cached for this file.
        std::string featStr;
        Field(l, "features", featStr);
        float audio[8] = { 0 };
        bool  audio_valid = false;
        if (!featStr.empty()) {
            int idx = 0;
            size_t pos = 0;
            while (idx < 8 && pos < featStr.size()) {
                size_t comma = featStr.find(',', pos);
                std::string token = featStr.substr(pos,
                    (comma == std::string::npos ? featStr.size() : comma) - pos);
                try { audio[idx++] = std::stof(token); } catch (...) { audio[idx++] = 0; }
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            audio_valid = (idx >= 8);
        }

        auto it = by_rel.find(path);
        if (it == by_rel.end()) continue;
        int ci = CategoryIndexFromName(cat);
        dir.SetFileAnalysis(it->second, ci, !dup_of.empty());
        dir.SetFileExtras(it->second, TagsFromString(tags), confidence, cluster_id);
        dir.SetFileFeatures(it->second, audio, audio_valid);
        if (!manual.empty()) {
            int mci = CategoryIndexFromName(manual);
            if (mci >= 0) dir.SetFileManualCategory(it->second, mci);
        }
        applied = true;
    }
    if (applied) dir.RebuildViews();
    return applied;
}

} // namespace Analysis
