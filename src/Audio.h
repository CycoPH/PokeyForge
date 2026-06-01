#pragma once

#include "Types.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <vector>

class RmtEngine;

// SDL3 audio output bound to an RmtEngine. Opens a default playback device
// at 8-bit unsigned mono 44100 Hz, sets up a callback that pulls samples
// from the engine when SDL needs more data.

class Audio {
public:
    Audio();
    ~Audio();

    bool Open(RmtEngine& engine, int samplesPerSec = 44100);
    void Close();

    // Suspend / resume the audio stream's callback. Used while the analysis
    // pass renders instruments offline through the same engine - pausing
    // here lets the main thread call engine.Generate() exclusively with no
    // interleaving from the SDL audio thread.
    void Pause();
    void Resume();

    // Set the audio output gain (1.0 = full volume, 0.0 = muted). General
    // utility; not currently used by the analysis path.
    void SetGain(float gain);

    int SamplesPerSec() const { return m_samples_per_sec; }

private:
    static void SDLCALL CallbackThunk(void* userdata,
                                      SDL_AudioStream* stream,
                                      int additional_amount,
                                      int total_amount);
    void Callback(SDL_AudioStream* stream, int additional_amount);

    RmtEngine*        m_engine = nullptr;
    SDL_AudioStream*  m_stream = nullptr;
    int               m_samples_per_sec = 44100;
    // Set by Pause(), checked from the SDL audio thread's callback. When
    // true the callback bails immediately without touching the engine -
    // this is the source of truth for "stop playing" because
    // SDL_PauseAudioStreamDevice doesn't actually stop the callback from
    // firing (it only stops the device from consuming the queued data),
    // so the engine could otherwise still get called during analysis.
    std::atomic<bool> m_paused { false };
    // 16 KB buffer; heap-allocated to keep it out of main's stack frame.
    std::vector<byte> m_scratch;
};
