#include "C6502.h"

#include <windows.h>
#include <cstdio>

namespace {

using SA_C6502_Initialise_PROC = void (*)(BYTE*);
using SA_C6502_JSR_PROC        = int  (*)(WORD*, BYTE*, BYTE*, BYTE*, int*);
using SA_C6502_About_PROC      = void (*)(char**, char**, char**);

HINSTANCE g_dll = nullptr;
SA_C6502_Initialise_PROC g_init = nullptr;
SA_C6502_JSR_PROC        g_jsr  = nullptr;
SA_C6502_About_PROC      g_about = nullptr;
char g_about_text[512] = "sa_c6502 not loaded";

} // anonymous namespace

namespace C6502 {

bool Init(byte* memory)
{
    if (g_dll) DeInit();

    g_dll = LoadLibraryA("sa_c6502.dll");
    if (!g_dll) {
        std::fprintf(stderr, "C6502::Init: LoadLibrary('sa_c6502.dll') failed\n");
        return false;
    }

    g_init  = (SA_C6502_Initialise_PROC)GetProcAddress(g_dll, "C6502_Initialise");
    g_jsr   = (SA_C6502_JSR_PROC)       GetProcAddress(g_dll, "C6502_JSR");
    g_about = (SA_C6502_About_PROC)     GetProcAddress(g_dll, "C6502_About");

    if (!g_init || !g_jsr) {
        std::fprintf(stderr, "C6502::Init: sa_c6502.dll missing required exports\n");
        DeInit();
        return false;
    }

    if (g_about) {
        char* n = nullptr; char* a = nullptr; char* d = nullptr;
        g_about(&n, &a, &d);
        std::snprintf(g_about_text, sizeof(g_about_text), "%s / %s / %s",
                      n ? n : "?", a ? a : "?", d ? d : "?");
    }

    g_init(memory);
    return true;
}

void DeInit()
{
    if (g_dll) FreeLibrary(g_dll);
    g_dll = nullptr;
    g_init = nullptr;
    g_jsr = nullptr;
    g_about = nullptr;
}

void JSR(Address& adr, Register& a, Register& x, Register& y, CycleCount& cycles)
{
    if (g_jsr) g_jsr(&adr, &a, &x, &y, &cycles);
}

const char* About() { return g_about_text; }

} // namespace C6502
