#include "RmtEngine.h"

#include "DriverBinaries.h"
#include "Pokey.h"
#include "RmtAddresses.h"

#include <algorithm>
#include <cstring>

RmtEngine::RmtEngine() = default;

RmtEngine::~RmtEngine() { DeInit(); }

bool RmtEngine::Init(const char* obx_path, bool ntsc, int samplesPerSec)
{
    DeInit();
    m_obx_path = obx_path ? obx_path : "";
    m_samples_per_sec = samplesPerSec;

    if (!m_atari.Init(ntsc)) return false;
    if (!Pokey::InitDll()) {
        m_atari.DeInit();
        return false;
    }
    Pokey::SoundInit(m_atari.ClockFrequency(), (std::uint16_t)samplesPerSec, 1);

    if (!ReinitDriver()) {
        DeInit();
        return false;
    }
    m_initialised = true;
    return true;
}

void RmtEngine::DeInit()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialised) {
        byte a = 0, x = 0, y = 0;
        MemoryAddress adr = RMT_SILENCE;
        m_atari.JSR(adr, a, x, y);
    }
    Pokey::DeInitDll();
    m_atari.DeInit();
    m_initialised = false;
}

void RmtEngine::SetNTSC(bool ntsc)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_atari.IsNTSC() == ntsc) return;
    m_atari.SetNTSC(ntsc);
    Pokey::SoundInit(m_atari.ClockFrequency(), (std::uint16_t)m_samples_per_sec, 1);
    // Re-load the driver and re-init RMT to reset clock-dependent state.
    ReinitDriver();
}

bool RmtEngine::IsNTSC() const { return m_atari.IsNTSC(); }

bool RmtEngine::ReinitDriver()
{
    if (!DriverBinaries::LoadIntoAtari(m_atari, m_obx_path.c_str())) return false;

    byte a = 0, x = 0x00, y = 0x3f;
    MemoryAddress adr = RMT_INIT;
    m_atari.JSR(adr, a, x, y);
    return true;
}

void RmtEngine::LoadInstrumentSlot(int slot, const byte* ata_blob, size_t len)
{
    if (slot < 0 || slot >= 64 || !ata_blob || len == 0) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    MemoryAddress dest = INSTRUMENTS_BASE + (MemoryAddress)slot * INSTRUMENT_SLOT_SIZE;
    byte* mem = m_atari.GetMemoryAt(dest);
    // Zero the whole 256-byte slot before writing, so leftover bytes from
    // any prior instrument can't be interpreted as a longer table/envelope.
    std::memset(mem, 0, INSTRUMENT_SLOT_SIZE);
    size_t copy_len = std::min<size_t>(len, INSTRUMENT_SLOT_SIZE);
    std::memcpy(mem, ata_blob, copy_len);
}

void RmtEngine::NoteOn(int track, int note, int instr, int vol)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    {
        byte a = (byte)note, x = (byte)track, y = (byte)instr;
        MemoryAddress adr = RMT_ATA_SETNOTEINSTR;
        m_atari.JSR(adr, a, x, y);
    }
    {
        byte a = (byte)vol, x = (byte)track, y = 0;
        MemoryAddress adr = RMT_ATA_SETVOLUME;
        m_atari.JSR(adr, a, x, y);
    }
}

void RmtEngine::NoteOff(int track)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Turn the instrument off on this track. Just setting volume to 0 is not
    // enough — the instrument's envelope re-drives the volume each frame, so
    // the note keeps sounding. RMT_ATA_INSTROFF actually stops it.
    byte a = 0, x = (byte)track, y = 0;
    MemoryAddress adr = RMT_ATA_INSTROFF;
    m_atari.JSR(adr, a, x, y);
    m_atari.SetByteAt((MemoryAddress)(0xD200 + track * 2 + 1), 0); // zero AUDCx
}

void RmtEngine::Silence()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Mirror RMT's InstrumentTurnOff: SILENCE alone does not stop an
    // instrument whose envelope is actively driving the volume. Turn off the
    // instrument on every track and zero each channel's AUDC (control)
    // register so nothing keeps ringing.
    for (int t = 0; t < 4; ++t) {
        byte a = 0, x = (byte)t, y = 0;
        MemoryAddress adr = RMT_ATA_INSTROFF;
        m_atari.JSR(adr, a, x, y);
        m_atari.SetByteAt((MemoryAddress)(0xD200 + t * 2 + 1), 0); // AUDCx = 0
    }

    byte a = 0, x = 0, y = 0;
    MemoryAddress adr = RMT_SILENCE;
    m_atari.JSR(adr, a, x, y);

    // Flush the zeroed registers straight to POKEY so the current audio
    // buffer goes quiet immediately, not just on the next frame.
    CopyAtariMemoryToPokey();
}

void RmtEngine::SnapshotPokey(byte out9[9])
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < 9; ++i) out9[i] = m_atari.GetByteAt((MemoryAddress)(0xD200 + i));
}

void RmtEngine::CopyAtariMemoryToPokey()
{
    // Mono: write registers 0..7 (AUDF1/AUDC1..AUDF4/AUDC4) from
    // $D200..$D207, then AUDCTL (register 8) from $D208.
    for (int i = 0; i < 8; ++i) {
        Pokey::PutByte((std::uint16_t)i, m_atari.GetByteAt((MemoryAddress)(0xD200 + i)));
    }
    Pokey::PutByte(0x08, m_atari.GetByteAt(0xD208));
}

int RmtEngine::Generate(byte* buffer, int numSamples)
{
    if (!m_initialised || !buffer || numSamples <= 0) return 0;

    std::lock_guard<std::mutex> lock(m_mutex);

    int samples_per_frame = SamplesPerFrame();
    if (samples_per_frame <= 0) return 0;

    int produced = 0;
    while (produced < numSamples) {
        // Drive one Atari VBI frame: process + setpokey.
        {
            byte a = 0, x = 0, y = 0;
            MemoryAddress adr = RMT_P3;
            m_atari.JSR(adr, a, x, y);
        }
        {
            byte a = 0, x = 0, y = 0;
            MemoryAddress adr = RMT_SETPOKEY;
            m_atari.JSR(adr, a, x, y);
        }
        CopyAtariMemoryToPokey();

        int want = std::min(samples_per_frame, numSamples - produced);
        Pokey::Process(buffer + produced, (std::uint16_t)want);
        produced += want;
    }
    return produced;
}
