#include "Audio.h"

#include "RmtEngine.h"

#include <algorithm>
#include <cstdio>

Audio::Audio() = default;

Audio::~Audio() { Close(); }

bool Audio::Open(RmtEngine& engine, int samplesPerSec)
{
    Close();
    m_engine = &engine;
    m_samples_per_sec = samplesPerSec;

    SDL_AudioSpec spec{};
    spec.format   = SDL_AUDIO_U8;
    spec.channels = 1;
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
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    m_engine = nullptr;
}

void SDLCALL Audio::CallbackThunk(void* userdata,
                                  SDL_AudioStream* stream,
                                  int additional_amount,
                                  int /*total_amount*/)
{
    auto* self = static_cast<Audio*>(userdata);
    if (self) self->Callback(stream, additional_amount);
}

void Audio::Callback(SDL_AudioStream* stream, int additional_amount)
{
    if (!m_engine) return;

    while (additional_amount > 0) {
        int want = std::min<int>(additional_amount, (int)sizeof(m_scratch));
        int produced = m_engine->Generate(m_scratch, want);
        if (produced <= 0) {
            // Engine couldn't produce: feed silence so SDL doesn't underrun.
            std::memset(m_scratch, 0x80, want); // 0x80 = silence in U8
            produced = want;
        }
        SDL_PutAudioStreamData(stream, m_scratch, produced);
        additional_amount -= produced;
    }
}
