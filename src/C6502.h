#pragma once

// Thin wrapper for sa_c6502.dll (Altirra's standalone 6502 core by
// Avery Lee / phaeron). MFC-free port of RMT's C6502.h.

#include "Types.h"

namespace C6502 {

using Address      = std::uint16_t;
using Register     = std::uint8_t;
using CycleCount   = int;
using ClockFreq    = int;

// Loads sa_c6502.dll from the exe directory and hands it a pointer to the
// 64 KB Atari memory array. Returns false if the DLL or any required
// proc isn't available.
bool Init(byte* memory);

void DeInit();

// Run 6502 code starting at *adr via JSR. Registers and remaining cycle
// budget are updated in place. The cycles parameter is the maximum number
// of cycles to run before forcing return.
void JSR(Address& adr, Register& a, Register& x, Register& y, CycleCount& cycles);

// "About" text (set after Init).
const char* About();

} // namespace C6502
