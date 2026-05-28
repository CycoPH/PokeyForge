#pragma once

#include "InstrumentTypes.h"
#include "Types.h"

#include <string>
#include <vector>

// A parsed .RTI file. The on-disk layout is:
//   4 bytes  : 'R','T','I', version  (only versions 0 and 1 are supported)
//   33 bytes : name, null-terminated
//   1 byte   : ATA blob length
//   N bytes  : ATA blob (raw Atari instrument memory layout)
//
// Ported from RMT IO_Instruments.cpp LoadInstrument(InstrumentIOType::RTI).

class RtiFile {
public:
    bool LoadFromFile(const char* path);
    bool LoadFromMemory(const byte* data, size_t size);

    bool Valid()       const { return m_valid; }
    int  Version()     const { return m_version; }
    const std::string& Path() const { return m_path; }
    const std::string& Name() const { return m_name; }

    const std::vector<byte>& AtaBlob() const { return m_ata; }

    // Decode the ATA blob into the structured TInstrument view used by the
    // GUI. Returns false on malformed data. Safe to call even when Valid()
    // is false (the result is just an empty/cleared instrument).
    bool ToInstrument(TInstrument& out, bool stereo) const;

    // Encode a TInstrument back into an ATA blob (inverse of ToInstrument).
    // Ported from RMT InstrToAta. Used by the editor to re-encode live edits.
    static std::vector<byte> InstrumentToAta(const TInstrument& ai, bool stereo);

    // Write a single .RTI file (RMT layout: "RTI\x01", 33-byte name, length,
    // ATA blob). Returns false on write failure.
    static bool WriteFile(const std::string& path, const std::string& name,
                          const std::vector<byte>& ata);

private:
    bool         m_valid = false;
    int          m_version = -1;
    std::string  m_path;
    std::string  m_name;
    std::vector<byte> m_ata;
};
