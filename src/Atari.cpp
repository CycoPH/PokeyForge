#include "Atari.h"

#include <cstring>

Atari::Atari()
{
    ClearMemory();
}

bool Atari::Init(bool ntsc)
{
    m_ntsc = ntsc;
    ClearMemory();
    m_cpu_loaded = C6502::Init(m_memory);
    return m_cpu_loaded;
}

void Atari::DeInit()
{
    if (m_cpu_loaded) {
        C6502::DeInit();
        m_cpu_loaded = false;
    }
}

void Atari::ClearMemory()
{
    std::memset(m_memory, 0, sizeof(m_memory));
}

void Atari::JSR(MemoryAddress addr, byte& a, byte& x, byte& y)
{
    C6502::Address adr_local = addr;
    C6502::CycleCount cycles = CyclesPerFrame();
    C6502::JSR(adr_local, a, x, y, cycles);
}
