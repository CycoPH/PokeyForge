#pragma once

#include "Types.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

class RmtEngine;

// SDL3 audio output bound to an RmtEngine. Opens the default playback device
// at 8-bit unsigned stereo 44100 Hz and pulls samples from the engine when
// SDL needs more data.
//
// Audio path on the patched sa_pokey.dll:
//   - On Open(), Audio installs a Pokey audio tap and mutes the DLL's
//     native audio device. The DLL's Pokey_Process always memsets its
//     output buffer to 0x80, so we have to capture the real audio off
//     the engine's internal mixer through the tap. The tap fires from
//     inside Generate() with raw POKEY mix-rate (~64 kHz) float samples;
//     Callback() drains and resamples them into the SDL stream.
//   - Stereo is forced (channels = 2) and each tap sample is duplicated
//     to both L/R, because some Windows audio setups don't reliably
//     up-mix a mono SDL stream to both speakers.
//
// Audio path on the unpatched (legacy) sa_pokey.dll:
//   - No tap exports, so Audio drives Generate() and pushes the byte
//     buffer Pokey_Process produced. With the legacy DLL this used to
//     be real samples; with newer Avery Lee builds it's 0x80 silence and
//     the user only hears the DLL's own native audio device. Stereo
//     duplication still happens at the SDL push so an upstream change
//     can't break L/R routing.

class Audio {
public:
    Audio();
    ~Audio();

    bool Open(RmtEngine& engine, int samplesPerSec = 44100);
    void Close();

    // Suspend / resume the audio stream's callback. Used while the analysis
    // pass renders instruments offline through the same engine - pausing
    // here lets Analysis::Run drive the engine + tap with no interleaving
    // from the SDL audio thread.
    void Pause();
    void Resume();

    // Set the audio output gain (1.0 = full volume, 0.0 = muted). General
    // utility; not currently used by the analysis path.
    void SetGain(float gain);

    int SamplesPerSec() const { return m_samples_per_sec; }

    // True when audio is being captured off the DLL's internal mixer
    // via the analysis-ABI audio tap. False on legacy DLLs - the legacy
    // path falls back to whatever Pokey_Process writes into the byte
    // buffer (which is silence on a current Avery Lee build).
    bool UsingTap() const { return m_using_tap; }

    // Discard any tap samples queued while playback was paused. Call this
    // right before Resume() to keep the first frame after un-pause from
    // playing the stale audio that piled up during analysis.
    void DropPendingTapAudio();

private:
    static void SDLCALL CallbackThunk(void* userdata,
                                      SDL_AudioStream* stream,
                                      int additional_amount,
                                      int total_amount);
    void Callback(SDL_AudioStream* stream, int additional_amount);

    static void __cdecl TapThunk(const float* left, const float* right,
                                 std::uint32_t count, std::uint32_t timestamp,
                                 void* user);
    void TapPush(const float* left, std::uint32_t count);

    RmtEngine*        m_engine = nullptr;
    SDL_AudioStream*  m_stream = nullptr;
    int               m_samples_per_sec = 44100;
    int               m_channels = 2;
    // Set by Pause(), checked from the SDL audio thread's callback. When
    // true the callback bails immediately without touching the engine -
    // this is the source of truth for "stop playing" because
    // SDL_PauseAudioStreamDevice doesn't actually stop the callback from
    // firing (it only stops the device from consuming the queued data),
    // so the engine could otherwise still get called during analysis.
    std::atomic<bool> m_paused { false };
    bool m_using_tap = false;
    // Float samples captured from the Pokey audio tap, mix-rate (~64 kHz).
    // Callback() drains this into the SDL stream. Guarded because the tap
    // fires from whichever thread is driving the engine, which is *usually*
    // this SDL audio thread but could be the main thread during analysis.
    std::mutex        m_tap_mutex;
    std::vector<float> m_tap_buf;
    // 16 KB scratch buffer; heap-allocated to keep it off main's stack.
    std::vector<byte> m_scratch;
};
