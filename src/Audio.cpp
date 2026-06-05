#include "Audio.h"

#include "Pokey.h"
#include "RmtEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

Audio::Audio() : m_scratch(16384, 0)
{
    m_tap_buf.reserve(8192);
}

Audio::~Audio() { Close(); }

bool Audio::Open(RmtEngine& engine, int samplesPerSec)
{
    Close();
    m_engine = &engine;
    m_samples_per_sec = samplesPerSec;
    m_channels = 2;

    SDL_AudioSpec spec{};
    spec.format   = SDL_AUDIO_U8;
    spec.channels = m_channels;       // explicit stereo - some Windows
                                      // configurations don't auto-upmix a
                                      // mono SDL stream to both speakers
    spec.freq     = samplesPerSec;

    m_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec,
        &Audio::CallbackThunk,
        this);

    if (!m_stream) {
        std::fprintf(stderr, "Audio::Open: SDL_OpenAudioDeviceStream failed: %s\n",
                     SDL_GetError());
        return false;
    }

    // If the patched DLL is loaded AND tap mode is preferred, take over
    // audio: silence the DLL's own native audio device and capture real
    // samples through the tap so the SDL stream below has something to
    // play. On a legacy DLL or with prefer_tap=false the DLL's own
    // audio device handles playback; we still feed SDL (the Pokey_Process
    // buffer is silent on current Altirra builds), which is the legacy
    // RMT behaviour.
    if (Pokey::HasAnalysisAbi() && m_prefer_tap) {
        Pokey::SetMute(true);
        Pokey::SetAudioTap(&Audio::TapThunk, this);
        m_using_tap = true;
    } else {
        if (Pokey::HasAnalysisAbi()) {
            Pokey::SetMute(false);
            Pokey::SetAudioTap(nullptr, nullptr);
        }
        m_using_tap = false;
    }

    if (!SDL_ResumeAudioStreamDevice(m_stream)) {
        std::fprintf(stderr, "Audio::Open: SDL_ResumeAudioStreamDevice failed: %s\n",
                     SDL_GetError());
        Close();
        return false;
    }
    return true;
}

void Audio::Close()
{
    // Uninstall the tap BEFORE the SDL stream goes away so a late callback
    // from the audio thread can't observe a half-torn-down Audio.
    if (m_using_tap) {
        Pokey::SetAudioTap(nullptr, nullptr);
        Pokey::SetMute(false);
        m_using_tap = false;
    }
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(m_tap_mutex);
        m_tap_buf.clear();
    }
    m_engine = nullptr;
}

void Audio::Pause()
{
    m_paused.store(true, std::memory_order_release);
    if (m_stream) SDL_PauseAudioStreamDevice(m_stream);
    // Release the tap so Analysis::Run can install its own without
    // racing against the SDL callback. Audio::Resume() puts ours back.
    if (m_using_tap) Pokey::SetAudioTap(nullptr, nullptr);
}

void Audio::Resume()
{
    if (m_stream) SDL_FlushAudioStream(m_stream);
    if (m_using_tap) Pokey::SetAudioTap(&Audio::TapThunk, this);
    // Anything that piled up during analysis would play out-of-context
    // on the first frame back - drop it and reset the read phase so
    // playback starts clean at the next callback.
    {
        std::lock_guard<std::mutex> lock(m_tap_mutex);
        m_tap_buf.clear();
        m_resample_phase = 0.0;
    }
    m_paused.store(false, std::memory_order_release);
    if (m_stream) SDL_ResumeAudioStreamDevice(m_stream);
}

void Audio::SetGain(float gain)
{
    if (m_stream) SDL_SetAudioStreamGain(m_stream, gain);
}

void Audio::SetUseAudioTap(bool prefer_tap)
{
    m_prefer_tap = prefer_tap;
    if (!Pokey::HasAnalysisAbi()) {
        // Legacy DLL: there is no choice to make, only the DLL's own
        // audio device exists. Keep m_using_tap=false.
        m_using_tap = false;
        return;
    }
    if (prefer_tap && !m_using_tap) {
        Pokey::SetMute(true);
        Pokey::SetAudioTap(&Audio::TapThunk, this);
        m_using_tap = true;
        std::lock_guard<std::mutex> lock(m_tap_mutex);
        m_tap_buf.clear();
        m_resample_phase = 0.0;
    } else if (!prefer_tap && m_using_tap) {
        Pokey::SetAudioTap(nullptr, nullptr);
        Pokey::SetMute(false);
        m_using_tap = false;
        // Any captured-but-unplayed samples are now noise; drop them so
        // the next callback doesn't briefly mix stale audio in.
        std::lock_guard<std::mutex> lock(m_tap_mutex);
        m_tap_buf.clear();
        m_resample_phase = 0.0;
    }
}

void Audio::DropPendingTapAudio()
{
    std::lock_guard<std::mutex> lock(m_tap_mutex);
    m_tap_buf.clear();
}

void SDLCALL Audio::CallbackThunk(void* userdata,
                                  SDL_AudioStream* stream,
                                  int additional_amount,
                                  int /*total_amount*/)
{
    auto* self = static_cast<Audio*>(userdata);
    if (self) self->Callback(stream, additional_amount);
}

void __cdecl Audio::TapThunk(const float* left, const float* /*right*/,
                             std::uint32_t count, std::uint32_t /*ts*/,
                             void* user)
{
    auto* self = static_cast<Audio*>(user);
    if (self && left && count) self->TapPush(left, count);
}

void Audio::TapPush(const float* left, std::uint32_t count)
{
    std::lock_guard<std::mutex> lock(m_tap_mutex);
    m_tap_buf.insert(m_tap_buf.end(), left, left + count);
    // Cap the queue at ~250 ms of mix-rate samples to bound memory if the
    // engine over-produces relative to host-rate consumption (it does -
    // ~2.37 tap samples per host sample empirically vs the 1.4357 we
    // consume for correct pitch). Drop the oldest half when over the cap
    // and reset the read phase so the consumer doesn't dereference a
    // freed position.
    constexpr size_t kMaxQueue = 16384;
    if (m_tap_buf.size() > kMaxQueue) {
        m_tap_buf.erase(m_tap_buf.begin(),
                        m_tap_buf.begin() + (m_tap_buf.size() - kMaxQueue / 2));
        m_resample_phase = 0.0;
    }
}

void Audio::Callback(SDL_AudioStream* stream, int additional_amount)
{
    if (!m_engine) return;
    if (m_paused.load(std::memory_order_acquire)) return;

    // additional_amount is bytes. Each stereo uint8 frame = 2 bytes.
    int bytes_per_frame = m_channels;
    int frames_needed   = additional_amount / bytes_per_frame;

    // Generate() at most this many host samples per inner iteration so the
    // scratch buffer stays sized in bytes.
    int max_frames_per_chunk = (int)m_scratch.size() / bytes_per_frame;

    // Engine-to-host resample ratio (PAL: ~1.4357 tap samples per host
    // sample). Pre-computed here so the tap-mode fill loop and the
    // resample stage agree on the target buffer level.
    constexpr double kEngineRate_local = 1773447.0 / 28.0;
    const double     kStep_local       = kEngineRate_local / (double)m_samples_per_sec;

    while (frames_needed > 0) {
        int want_frames = std::min(frames_needed, max_frames_per_chunk);

        // Run the engine for `want_frames` mono samples. On the patched DLL
        // the buffer comes back as 0x80 silence (by design) - we only call
        // Generate to tick the POKEY scheduler so the audio tap fires.
        // On a legacy DLL the buffer holds the real mono audio.
        //
        // Pokey::Process passes sndn = 2 * numSamples to the DLL so each
        // Generate(want_frames) advances the engine by exactly want_frames
        // host samples of emulated time - matching real-time at the
        // configured playback rate. With that fix one Generate per
        // callback is the right amount; no fill loop needed (the old
        // fill loop tried to compensate for half-rate engine advance and
        // ended up over-driving 2-3x, making everything play too fast).
        int produced = m_engine->Generate(m_scratch.data(), want_frames);
        if (produced <= 0) {
            // Engine couldn't produce: feed silence so SDL doesn't underrun.
            std::vector<byte> sil((size_t)want_frames * bytes_per_frame, 0x80);
            SDL_PutAudioStreamData(stream, sil.data(), (int)sil.size());
            frames_needed -= want_frames;
            continue;
        }

        std::vector<byte> stereo((size_t)produced * bytes_per_frame);

        if (m_using_tap) {
            // Resample the captured float stream into the host's U8 stereo
            // output at the FIXED ratio engine_rate / host_rate. The
            // previous code used ratio = snapshot.size() / produced,
            // which compressed all of the engine's over-production into
            // the per-callback host window and played ~1.6x too fast /
            // an octave-ish too high. Keeping a fractional read phase
            // across callbacks locks the playback to the engine's true
            // emit rate; any surplus tap samples accumulate in the
            // queue and are drained at the correct cadence (or dropped
            // at the queue cap, see TapPush).
            //
            // Engine rate hardcoded to PAL (1773447 / 28) because
            // Pokey_SoundInit in the patched DLL force-sets the
            // reference clock to PAL regardless of what we pass in.
            constexpr double kEngineRate = 1773447.0 / 28.0;  // ~63337 Hz
            const double     kHostRate   = (double)m_samples_per_sec;
            const double     kStep       = kEngineRate / kHostRate;   // ~1.4357

            std::lock_guard<std::mutex> lock(m_tap_mutex);
            const int sz = (int)m_tap_buf.size();
            int last_consumed = -1;

            for (int i = 0; i < produced; ++i) {
                int src = (int)m_resample_phase;
                if (src < 0 || src >= sz) {
                    // Buffer underrun - engine hasn't produced enough yet.
                    // Push silence; phase stays put so we resume cleanly.
                    stereo[(size_t)i * 2 + 0] = 0x80;
                    stereo[(size_t)i * 2 + 1] = 0x80;
                    continue;
                }
                float v = m_tap_buf[src];
                if (v < -1.0f) v = -1.0f;
                if (v >  1.0f) v =  1.0f;
                byte b = (byte)std::lround(128.0f + v * 127.0f);
                stereo[(size_t)i * 2 + 0] = b;
                stereo[(size_t)i * 2 + 1] = b;
                m_resample_phase += kStep;
                last_consumed = src;
            }

            // Discard the samples we've fully consumed and rebase the phase
            // so we don't unbounded-grow the vector head. The queue cap in
            // TapPush handles the over-production case where this drain
            // doesn't keep up.
            int consumed = (int)m_resample_phase;
            if (consumed >= sz) {
                m_tap_buf.clear();
                m_resample_phase = 0.0;
            } else if (consumed > 0) {
                m_tap_buf.erase(m_tap_buf.begin(), m_tap_buf.begin() + consumed);
                m_resample_phase -= (double)consumed;
            }
            (void)last_consumed;
        } else {
            // Legacy path: m_scratch holds Pokey_Process's byte output.
            // Duplicate each mono sample to L+R so the audio is the same
            // shape regardless of which DLL is loaded.
            for (int i = 0; i < produced; ++i) {
                byte b = m_scratch[i];
                stereo[(size_t)i * 2 + 0] = b;
                stereo[(size_t)i * 2 + 1] = b;
            }
        }

        SDL_PutAudioStreamData(stream, stereo.data(), (int)stereo.size());
        frames_needed -= produced;
    }
}
