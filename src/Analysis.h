#pragma once

#include "InstrumentTypes.h"
#include "Types.h"

#include <cstdint>
#include <string>
#include <vector>

class Directory;
class RmtEngine;

// Instrument analysis: hashes each instrument's ATA blob to find sonic
// duplicates and classifies it into a coarse category. Results are applied to
// the Directory (per-file category + duplicate flag) and cached in
// analysis.json in the library folder.
//
// Duplicate detection is a strict byte-compare of the ATA blob (the raw
// instrument definition). Two .RTI files with different filenames but the
// same ATA bytes are considered duplicates regardless of their on-disk
// "name" header field. The FIRST file encountered (depth-first directory
// order, alphabetically within each folder) is the keeper; later matches
// are flagged is_duplicate=true and hidden when "No dupes" is on.
//
// Classification uses the small fixed heuristic implemented in Classify():
//   - PAR_AUDCTL_JOIN_*           -> Bass        (channel pairing)
//   - noise distortion + short    -> Percussion  (short hits)
//   - noise distortion otherwise  -> NoiseFX
//   - short env + fast fade       -> Percussion  (non-noise blips)
//   - looping env, slow fade      -> Pad
//   - dominant pure-tone (0x0A)   -> Lead
//   - else                        -> Other
// See the Readme's "How categorisation works" section for the exact rules
// and the signals each one reads.

namespace Analysis {

// Bump this whenever the classifier's decision tree, signal extractors, or
// category enum changes in a way that would produce different results from
// the same input. analysis.json files are tagged with this version; on load
// a mismatch is treated as "no cache present" and the library is re-analysed
// automatically (see LoadAndApply / LoadOrRunAnalysis).
//
// History:
//   v1 - original 6-bucket tree (Bass/Lead/Percussion/NoiseFX/Pad/Other).
//   v2 - expanded 13-bucket tree (Kick/Snare/HiHat/Perc split, Arp, Bell,
//        SweptFX, LeadVibrato), 11 signals, filename-hint tie-breaker.
//   v3 - audio-rendered features (RMS profile, zero-crossing rate, peak
//        position, spectral centroid/rolloff/flux) read by Classify as
//        additional confirmation signals.
//   v4 - Chord / Glide categories added; falling-pitch confirms Kick;
//        multi-pitch audio rendering (low/mid/high averaged); per-file
//        Tag bitmask, Confidence score, ClusterId + ManualOverride
//        stored in analysis.json.
//   v5 - cached audio features (rms_early/mid/late, zcr, peak_pos,
//        centroid, rolloff, flux) stored per file as a comma-separated
//        "features" string; no absolute "library" field; the file is
//        now fully portable with the instrument folder.
//   v6 - audio-rendered features removed. The Altirra POKEY DLL refuses
//        to render audio while the main thread is in Analysis::Run -
//        every Generate call returned 0x80-silence regardless of how
//        we re-routed it (main vs audio thread, big vs small calls,
//        with/without Silence). The classifier falls back to the v2
//        parametric-only decision tree (categories + confidence + tags
//        derived from parameters); audio-derived sub-tags (bright /
//        dark / loud / quiet / animated) and k-means clustering are
//        disabled. Existing v5 caches with stale all-zero features
//        get auto-regenerated.
constexpr int kAnalysisVersion = 6;

// Expanded category set (v2). The first-match-wins decision tree in
// Classify() walks these top-down; an instrument lands in exactly one.
// The ordering is documented inline in Analysis.cpp (Classify) and
// summarised in the Readme's "How categorisation works" section.
enum class Category {
    Bass = 0,    // POKEY channel join enabled (16-bit period bass voice)
    Lead,        // dominant pure-tone (square / pulse 0x0A) without arp/vibrato
    LeadVibrato, // Lead-shaped envelope with vibrato (PAR_VIBRATO > 0)
    Arp,         // 2-3 distinct note-table values (trill / short pattern)
    Chord,       // 4+ distinct note-table values (chord stab / multi-note arp)
    Glide,       // monotonic note-table drift (portamento / slide)
    Pad,         // looping envelope with a slow / no fade-out
    Bell,        // tonal but uses POKEY 15 kHz / 1.79 MHz high-frequency mode
    Kick,        // short, fast-fade, pulse->noise transient (drum kick)
    Snare,       // short, mixed noise+pulse distortion (drum snare)
    HiHat,       // very short, pure noise, fast fade (hat / shaker / cymbal)
    Perc,        // any other short / fast-fade percussive sound
    SweptFX,     // filter envelope (FILTER column) actually moves over time
    NoiseFX,     // longer noise-dominant sound (sweep, riser, FX)
    Other,       // didn't match any other rule
    COUNT
};

// Orthogonal sub-labels stored as a bitmask alongside the main category.
// A single instrument can carry several tags - they're descriptors, not
// classifications. Stored as a comma-separated string in analysis.json.
enum Tag : unsigned {
    Tag_None     = 0,
    Tag_Vibrato  = 1u << 0,    // PAR_VIBRATO > 0
    Tag_Bright   = 1u << 1,    // audio spectral centroid > 4.5 kHz
    Tag_Dark     = 1u << 2,    // audio spectral centroid < 1.5 kHz
    Tag_Loud     = 1u << 3,    // audio mean RMS > 0.20
    Tag_Quiet    = 1u << 4,    // audio mean RMS < 0.05
    Tag_Animated = 1u << 5,    // audio spectral flux > 0.20
    Tag_HighFreq = 1u << 6,    // AUDCTL 15 kHz / 1.79 MHz bit set
    Tag_ArpAsc   = 1u << 7,    // note table ascends overall
    Tag_ArpDesc  = 1u << 8,    // note table descends overall
};
unsigned TagsForInstrument(const TInstrument& ins, const struct Features* f);
std::string TagsToString(unsigned tags);
unsigned    TagsFromString(const std::string& s);

const char* Name(Category c);
std::vector<std::string> Names();   // category names in index order

// 64-bit FNV-1a hash over the ATA blob (the actual instrument definition).
// Used as a coarse pre-filter for dedup; the canonical equality check is a
// full vector<byte> compare (Bank::IndexOfAta).
std::uint64_t HashAta(const std::vector<byte>& ata);

// Audio-rendered features for one instrument. Populated by ExtractFeatures
// (which renders the instrument through the live engine and computes them
// from the resulting PCM); `valid == false` means rendering was skipped or
// failed and Classify should ignore the rest of the fields.
//
// All RMS values are normalised to [0,1] against the 8-bit PCM full-scale
// range; peak_pos is the position of the absolute-peak sample as a
// fraction of the rendered window (so 0.0 = sound is loudest at the
// attack, 1.0 = loudest at the very end - i.e. a swell). centroid and
// rolloff are in Hz; flux is the average absolute difference between
// consecutive FFT magnitude frames (0 = static timbre, larger = animated).
struct Features {
    bool   valid     = false;
    float  rms_early = 0;   // mean RMS in the first 1/3 of the window
    float  rms_mid   = 0;   // mean RMS in the middle 1/3
    float  rms_late  = 0;   // mean RMS in the last 1/3
    float  zcr       = 0;   // zero-crossing rate (0..1), high = noisy
    float  peak_pos  = 0;   // 0..1 position of the peak amplitude
    float  centroid  = 0;   // spectral centroid in Hz (brightness)
    float  rolloff   = 0;   // 85% energy roll-off in Hz
    float  flux      = 0;   // mean per-frame spectral change (timbre motion)
};

// Audio feature extraction is currently a no-op (see kAnalysisVersion v6
// note above). The signature is preserved for callers; the returned
// Features always has valid == false so Classify falls back to its
// parametric-only path.
Features ExtractFeatures(RmtEngine& engine, const std::vector<byte>& ata);

// Coarse classification from the decoded instrument's parameters/envelope
// (plus, when available, the rendered-audio Features). The decision tree
// is first-match-wins; an instrument lands in exactly one category.
// `filename` is optional - recognised tokens act as a last-resort tie-
// breaker when the parameter heuristics return Other. See Analysis.cpp.
Category Classify(const TInstrument& ins, const std::string& filename = "",
                  const Features* features = nullptr,
                  int* out_confidence = nullptr);

struct Summary {
    int  total      = 0;   // instruments scanned
    int  duplicates = 0;   // marked as duplicates of an earlier file
    int  clusters   = 0;   // k chosen by k-means (0 = clustering skipped)
    bool ok         = false;
};

// Optional progress reporter. Called once per file with (current_index,
// total). Useful for driving an "Analysing... N / M" splash. Free to
// pump SDL events / redraw inside the callback.
using ProgressFn = void(*)(int current, int total, void* userdata);

// Override for k-means cluster count. `k_override > 0` uses that value
// directly; otherwise k is chosen automatically as
// ceil(sqrt(N/2)) clamped to [3, 12].
struct Options {
    RmtEngine*  engine     = nullptr;
    int         k_override = 0;
    ProgressFn  progress   = nullptr;
    void*       progress_ud = nullptr;
};

// Scan every file in `dir`, classify, detect duplicates, apply the results
// to `dir` (category + is_duplicate per file node, plus category names),
// rebuild its views, and optionally write analysis.json into `libraryRoot`.
// `opts.engine` non-null enables audio feature extraction (the caller must
// pause audio playback before calling and resume after); `opts.progress`
// is invoked once per file.
Summary Run(Directory& dir, const std::string& libraryRoot, bool writeJson,
            const Options& opts = Options{});

// Load analysis.json from `libraryRoot` and apply it to `dir` (matching by
// path). Returns true if a usable file was found and applied.
bool LoadAndApply(Directory& dir, const std::string& libraryRoot);

} // namespace Analysis
