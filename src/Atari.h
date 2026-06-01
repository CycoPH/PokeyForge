#pragma once

#include "Types.h"
#include "C6502.h"

#include <memory>

// 64 KB Atari memory + helpers for JSR-ing into 6502 code.
// MFC-free port of RMT's CAtari/Atari.h, scoped to PokeyForge's needs.

class Atari {
public:
    static constexpr int MEMORY_SIZE = 0x10000;

    // True Atari clock frequencies.
    static constexpr int FREQ_17_NTSC = 1789773;
    static constexpr int FREQ_17_PAL  = 1773447;

    Atari();

    // Loads sa_c6502.dll and binds it to our memory. Returns false on failure.
    bool Init(bool ntsc);
    void DeInit();

    void ClearMemory();

    bool IsNTSC() const { return m_ntsc; }
    void SetNTSC(bool ntsc) { m_ntsc = ntsc; }

    int  ClockFrequency() const { return m_ntsc ? FREQ_17_NTSC : FREQ_17_PAL; }
    int  FrameRateHz()    const { return m_ntsc ? 60 : 50; }
    int  CyclesPerFrame() const { return ClockFrequency() / FrameRateHz(); }

    byte  GetByteAt(MemoryAddress address) const     { return m_memory[address]; }
    void  SetByteAt(MemoryAddress address, byte v)   { m_memory[address] = v; }
    byte* GetMemoryAt(MemoryAddress address)         { return &m_memory[address]; }

    // JSR with our standard cycle budget.
    void JSR(MemoryAddress addr, byte& a, byte& x, byte& y);

private:
    // Heap-allocated so the 64 KB doesn't sit in main's stack frame.
    std::unique_ptr<byte[]> m_memory;
    bool  m_ntsc = false;
    bool  m_cpu_loaded = false;
};
