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

    // If the patched DLL is loaded, take over audio: silence the DLL's
    // own native audio device and capture real samples through the tap so
    // the SDL stream below has something to play. On a legacy DLL these
    // calls are no-ops (HasAnalysisAbi() == false) and the callback falls
    // back to whatever Pokey_Process writes into its byte buffer.
    if (Pokey::HasAnalysisAbi()) {
        Pokey::SetMute(true);
        Pokey::SetAudioTap(&Audio::TapThunk, this);
        m_using_tap = true;
    } else {
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
    // on the first frame back - drop it.
    {
        std::lock_guard<std::mutex> lock(m_tap_mutex);
        m_tap_buf.clear();
    }
    m_paused.store(false, std::memory_order_release);
    if (m_stream) SDL_ResumeAudioStreamDevice(m_stream);
}

void Audio::SetGain(float gain)
{
    if (m_stream) SDL_SetAudioStreamGain(m_stream, gain);
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
    // SDL device stops pulling (paused, hung, etc). Keep the tail because
    // it's the freshest audio - dropping the head loses old samples that
    // would have played first but are no longer urgent.
    constexpr size_t kMaxQueue = 16384;
    if (m_tap_buf.size() > kMaxQueue) {
        m_tap_buf.erase(m_tap_buf.begin(),
                        m_tap_buf.begin() + (m_tap_buf.size() - kMaxQueue / 2));
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

    while (frames_needed > 0) {
        int want_frames = std::min(frames_needed, max_frames_per_chunk);

        // Run the engine for `want_frames` mono samples. On the patched DLL
        // the buffer comes back as 0x80 silence (by design) - we only call
        // Generate to tick the POKEY scheduler so the audio tap fires.
        // On a legacy DLL the buffer holds the real mono audio.
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
            // Snapshot the tap queue under the lock, then resample it
            // outside. Linear-rate nearest-neighbour decimation - audio
            // is already at engine mix rate (~63 kHz); going to 44.1 kHz
            // is a tiny down-sample, no audible artifacts.
            std::vector<float> snapshot;
            {
                std::lock_guard<std::mutex> lock(m_tap_mutex);
                snapshot.swap(m_tap_buf);
            }
            if (snapshot.empty()) {
                std::memset(stereo.data(), 0x80, stereo.size());
            } else {
                const float ratio = (float)snapshot.size() / (float)produced;
                for (int i = 0; i < produced; ++i) {
                    int src = (int)(i * ratio);
                    if (src < 0)                       src = 0;
                    if (src >= (int)snapshot.size())   src = (int)snapshot.size() - 1;
                    float v = snapshot[src];
                    if (v < -1.0f) v = -1.0f;
                    if (v >  1.0f) v =  1.0f;
                    byte b = (byte)std::lround(128.0f + v * 127.0f);
                    stereo[(size_t)i * 2 + 0] = b;
                    stereo[(size_t)i * 2 + 1] = b;
                }
            }
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
