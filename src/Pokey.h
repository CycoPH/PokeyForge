#pragma once

// Wrapper for sa_pokey.dll (Altirra POKEY emulator). MFC-free port of
// RMT's Pokey.h. We only target the SA_POKEY backend (not apokeysnd).

#include "Types.h"

namespace Pokey {

// Load sa_pokey.dll. Returns false on failure.
bool InitDll();

void DeInitDll();

// Configure sample generation. clockHz is 1789773 (NTSC) or 1773447 (PAL).
// channels is 1 for mono, 2 for stereo (dual POKEY).
void SoundInit(std::uint32_t clockHz, std::uint16_t samplesPerSec, std::uint8_t channels);

// Poke a POKEY register. addr is the low-byte index in $D200..$D20F
// (the wrapper internally uses the full $D2xx address).
void PutByte(std::uint16_t addr, std::uint8_t value);

// Generate `numSamples` PCM samples into buffer. Sample format is
// 8-bit unsigned per the sa_pokey ABI.
void Process(std::uint8_t* buffer, std::uint16_t numSamples);

const char* About();

} // namespace Pokey
