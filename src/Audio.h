#pragma once

#include "Types.h"

#include <SDL3/SDL.h>

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

    int SamplesPerSec() const { return m_samples_per_sec; }

private:
    static void SDLCALL CallbackThunk(void* userdata,
                                      SDL_AudioStream* stream,
                                      int additional_amount,
                                      int total_amount);
    void Callback(SDL_AudioStream* stream, int additional_amount);

    RmtEngine*       m_engine = nullptr;
    SDL_AudioStream* m_stream = nullptr;
    int              m_samples_per_sec = 44100;
    byte             m_scratch[16384]{};
};
