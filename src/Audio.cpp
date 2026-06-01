#include "Audio.h"

#include "RmtEngine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

Audio::Audio() : m_scratch(16384, 0) {}

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

void Audio::Pause()
{
    m_paused.store(true, std::memory_order_release);
    if (m_stream) SDL_PauseAudioStreamDevice(m_stream);
}

void Audio::Resume()
{
    if (m_stream) SDL_FlushAudioStream(m_stream);
    m_paused.store(false, std::memory_order_release);
    if (m_stream) SDL_ResumeAudioStreamDevice(m_stream);
}

void Audio::SetGain(float gain)
{
    if (m_stream) SDL_SetAudioStreamGain(m_stream, gain);
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
    if (m_paused.load(std::memory_order_acquire)) return;

    while (additional_amount > 0) {
        int want = std::min<int>(additional_amount, (int)m_scratch.size());
        int produced = m_engine->Generate(m_scratch.data(), want);
        if (produced <= 0) {
            // Engine couldn't produce: feed silence so SDL doesn't underrun.
            std::memset(m_scratch.data(), 0x80, (size_t)want); // 0x80 = silence
            produced = want;
        }
        SDL_PutAudioStreamData(stream, m_scratch.data(), produced);
        additional_amount -= produced;
    }
}
