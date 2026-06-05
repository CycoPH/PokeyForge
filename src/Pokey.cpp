#include "Pokey.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using Pokey_Initialise_PROC = void (*)(int*, char**);
using Pokey_SoundInit_PROC  = void (*)(DWORD, WORD, BYTE);
using Pokey_Process_PROC    = void (*)(BYTE*, const WORD);
using Pokey_PutByte_PROC    = void (*)(WORD, BYTE);
using Pokey_About_PROC      = void (*)(char**, char**, char**);
using Pokey_GetAbi_PROC     = int  (*)();
using Pokey_SetTap_PROC     = void (*)(Pokey::AudioTapFn, void*);
using Pokey_SetMute_PROC    = void (*)(int);

HINSTANCE g_dll = nullptr;
Pokey_Initialise_PROC g_init = nullptr;
Pokey_SoundInit_PROC  g_sound_init = nullptr;
Pokey_Process_PROC    g_process = nullptr;
Pokey_PutByte_PROC    g_put_byte = nullptr;
Pokey_About_PROC      g_about = nullptr;
Pokey_GetAbi_PROC     g_get_abi = nullptr;
Pokey_SetTap_PROC     g_set_tap = nullptr;
Pokey_SetMute_PROC    g_set_mute = nullptr;
int g_analysis_abi = 0;
char g_about_text[512] = "sa_pokey not loaded";

} // anonymous namespace

namespace Pokey {

bool InitDll()
{
    if (g_dll) DeInitDll();

    g_dll = LoadLibraryA("sa_pokey.dll");
    if (!g_dll) {
        std::fprintf(stderr, "Pokey::InitDll: LoadLibrary('sa_pokey.dll') failed\n");
        return false;
    }

    g_init       = (Pokey_Initialise_PROC)GetProcAddress(g_dll, "Pokey_Initialise");
    g_sound_init = (Pokey_SoundInit_PROC) GetProcAddress(g_dll, "Pokey_SoundInit");
    g_process    = (Pokey_Process_PROC)   GetProcAddress(g_dll, "Pokey_Process");
    g_put_byte   = (Pokey_PutByte_PROC)   GetProcAddress(g_dll, "Pokey_PutByte");
    g_about      = (Pokey_About_PROC)     GetProcAddress(g_dll, "Pokey_About");

    // Optional extensions: present only in PokeyForge-patched builds.
    g_get_abi  = (Pokey_GetAbi_PROC)  GetProcAddress(g_dll, "Pokey_GetAnalysisAbiVersion");
    g_set_tap  = (Pokey_SetTap_PROC)  GetProcAddress(g_dll, "Pokey_SetAudioTap");
    g_set_mute = (Pokey_SetMute_PROC) GetProcAddress(g_dll, "Pokey_SetMute");
    g_analysis_abi = (g_get_abi && g_set_tap && g_set_mute) ? g_get_abi() : 0;

    if (!g_init || !g_sound_init || !g_process || !g_put_byte) {
        std::fprintf(stderr, "Pokey::InitDll: sa_pokey.dll missing required exports\n");
        DeInitDll();
        return false;
    }

    if (g_about) {
        char* n = nullptr; char* a = nullptr; char* d = nullptr;
        g_about(&n, &a, &d);
        std::snprintf(g_about_text, sizeof(g_about_text), "%s / %s / %s",
                      n ? n : "?", a ? a : "?", d ? d : "?");
    }

    g_init(nullptr, nullptr);
    return true;
}

void DeInitDll()
{
    if (g_dll) FreeLibrary(g_dll);
    g_dll = nullptr;
    g_init = nullptr; g_sound_init = nullptr; g_process = nullptr;
    g_put_byte = nullptr; g_about = nullptr;
    g_get_abi = nullptr; g_set_tap = nullptr; g_set_mute = nullptr;
    g_analysis_abi = 0;
}

void SoundInit(std::uint32_t clockHz, std::uint16_t samplesPerSec, std::uint8_t channels)
{
    if (g_sound_init) g_sound_init((DWORD)clockHz, (WORD)samplesPerSec, (BYTE)channels);
}

void PutByte(std::uint16_t addr, std::uint8_t value)
{
    if (g_put_byte) g_put_byte((WORD)addr, (BYTE)value);
}

void Process(std::uint8_t* buffer, std::uint16_t numSamples)
{
    if (!g_process) return;
    // The DLL's Pokey_Process is designed for STEREO 8-bit: sndn = byte
    // count = sample_pair_count * 2, and Advance(sndn / 2) advances by
    // sample_pair_count ticks = real-time at the configured playback
    // rate. RMT itself uses CHANNELS=2 and matches this contract.
    //
    // PokeyForge runs MONO, which would mean sndn = sample_count and
    // Advance(sndn / 2) = sample_count / 2 ticks - HALF real-time. The
    // engine then under-feeds the audio tap by ~2x, and earlier fixes
    // that tried to make up the shortfall (a Generate fill loop) ended
    // up over-driving the engine instead, making playback sound too
    // fast. Compensate here by always passing the DLL twice our mono
    // sample count (using an internal scratch buffer so the DLL's
    // memset doesn't overflow the caller's mono-sized output).
    //
    // The DLL's behavior of memset'ing the buffer to 0x80 silence is
    // unchanged; we throw those bytes away. The caller's `buffer` ends
    // up populated with silence too (we copy back so the contract
    // matches the legacy DLL's memset). The real audio comes off the
    // audio tap (when installed) or the DLL's native audio device.
    static thread_local std::vector<BYTE> stereo_scratch;
    const size_t need = (size_t)numSamples * 2u;
    if (stereo_scratch.size() < need) stereo_scratch.resize(need);
    g_process(stereo_scratch.data(), (WORD)need);
    if (buffer && numSamples > 0)
        std::memcpy(buffer, stereo_scratch.data(), numSamples);
}

const char* About() { return g_about_text; }

bool HasAnalysisAbi()      { return g_analysis_abi != 0; }
int  AnalysisAbiVersion()  { return g_analysis_abi; }

void SetAudioTap(AudioTapFn fn, void* user)
{
    if (g_set_tap) g_set_tap(fn, user);
}

void SetMute(bool mute)
{
    if (g_set_mute) g_set_mute(mute ? 1 : 0);
}

} // namespace Pokey
