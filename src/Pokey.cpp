#include "Pokey.h"

#include <windows.h>
#include <cstdio>

namespace {

using Pokey_Initialise_PROC = void (*)(int*, char**);
using Pokey_SoundInit_PROC  = void (*)(DWORD, WORD, BYTE);
using Pokey_Process_PROC    = void (*)(BYTE*, const WORD);
using Pokey_PutByte_PROC    = void (*)(WORD, BYTE);
using Pokey_About_PROC      = void (*)(char**, char**, char**);

HINSTANCE g_dll = nullptr;
Pokey_Initialise_PROC g_init = nullptr;
Pokey_SoundInit_PROC  g_sound_init = nullptr;
Pokey_Process_PROC    g_process = nullptr;
Pokey_PutByte_PROC    g_put_byte = nullptr;
Pokey_About_PROC      g_about = nullptr;
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
    if (g_process) g_process((BYTE*)buffer, (WORD)numSamples);
}

const char* About() { return g_about_text; }

} // namespace Pokey
