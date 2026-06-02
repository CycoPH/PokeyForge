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

// ---- PokeyForge audio-capture extensions ---------------------------------
// These wrap Pokey_SetAudioTap / Pokey_SetMute / Pokey_GetAnalysisAbiVersion
// exposed by our patched sa_pokey.dll. They give us access to raw POKEY
// float samples at the engine's native mix rate (~63920 Hz NTSC,
// 63337 Hz PAL) without the device-buffer silencing that Pokey_Process
// performs on its own output buffer. HasAnalysisAbi() returns true only
// when the patched DLL is present; older shipped DLLs will report false
// and the analysis path should fall back to "no audio features".

// Signature mirrors ATRMTPokeyAudioTapFn from rmtinterface.cpp. `right`
// is null in mono, otherwise interleaved-by-channel. `count` is samples
// per channel. `timestamp` is the engine's cycle count (informational).
using AudioTapFn = void (__cdecl *)(const float* left, const float* right,
                                    std::uint32_t count, std::uint32_t timestamp,
                                    void* user);

bool HasAnalysisAbi();
int  AnalysisAbiVersion();

void SetAudioTap(AudioTapFn fn, void* user);
void SetMute(bool mute);

} // namespace Pokey
