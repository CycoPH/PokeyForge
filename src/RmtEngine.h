#pragma once

#include "Atari.h"
#include "Types.h"

#include <mutex>
#include <string>

// PokeyForge's playback engine. Mirrors the model in RMT's
// AtariTrackerDriver.cpp + PokeyRenderer.cpp, scoped to 4-channel mono.
//
// Each "frame" advances at the Atari VBI rate (50 Hz PAL, 60 Hz NTSC):
//   - Run the RMT driver (RMT_P3 + RMT_SETPOKEY) once
//   - Copy POKEY shadow registers from $D200..$D208 to the POKEY emulator
//   - Generate `samples_per_frame` PCM samples
//
// The audio thread pulls samples through Generate(); the main thread can
// safely call NoteOn/NoteOff/LoadInstrument — Generate() takes a mutex.

class RmtEngine {
public:
    RmtEngine();
    ~RmtEngine();

    // Loads sa_c6502.dll, sa_pokey.dll, and the named driver .obx.
    // samplesPerSec is typically 44100. Returns false on failure.
    bool Init(const char* obx_path, bool ntsc, int samplesPerSec);
    void DeInit();

    // Switch between PAL (false) and NTSC (true) at runtime.
    // Triggers reinitialization of POKEY clock and re-runs RMT_INIT.
    void SetNTSC(bool ntsc);
    bool IsNTSC() const;

    // Copy a raw ATA blob into emulated RAM at $4000 + slot*256.
    // The blob is the byte array produced by RMT's InstrToAta or as
    // stored in a .RTI file body.
    void LoadInstrumentSlot(int slot, const byte* ata_blob, size_t len);

    // Trigger a note on track t, instrument i, at volume v (0..15).
    // Calls RMT_ATA_SETNOTEINSTR then RMT_ATA_SETVOLUME.
    void NoteOn(int track, int note, int instr, int vol);

    // Force a track silent without retriggering.
    void NoteOff(int track);

    // Drop all current notes via the driver's SILENCE entry point.
    void Silence();

    // Pull samples for the audio device. Runs as many emulated frames as
    // needed to cover `numSamples`; pushes mono unsigned 8-bit PCM.
    // Returns the number of samples actually written.
    int Generate(byte* buffer, int numSamples);

    int  SamplesPerSec() const { return m_samples_per_sec; }
    int  SamplesPerFrame() const { return m_samples_per_sec / m_atari.FrameRateHz(); }

    // Copy the 9 POKEY shadow registers ($D200..$D208: AUDF1/AUDC1 .. AUDCTL)
    // for the scope display. Thread-safe.
    void SnapshotPokey(byte out9[9]);

private:
    void CopyAtariMemoryToPokey();
    bool ReinitDriver();

    Atari m_atari;
    std::string m_obx_path;
    int  m_samples_per_sec = 44100;
    bool m_initialised = false;
    std::mutex m_mutex;
};
