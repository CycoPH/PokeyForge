#include "Bank.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

// Replace characters that would be illegal in a Windows filename, so a
// slot whose instrument name is e.g. "drum/snare?" becomes "drum_snare_".
std::string SanitiseForFilename(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c >= 32 && c < 127 && c != '<' && c != '>' && c != ':' && c != '"' &&
            c != '/' && c != '\\' && c != '|' && c != '?' && c != '*') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "unnamed";
    // Strip trailing dots / spaces (Windows hates those at end of filenames).
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty()) out = "unnamed";
    return out;
}

} // anonymous namespace

Bank::Bank() = default;

int Bank::FirstEmptySlot() const
{
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (!m_slots[i].used) return i;
    }
    return -1;
}

int Bank::IndexOfPath(const std::string& path) const
{
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (m_slots[i].used && m_slots[i].source_path == path) return i;
    }
    return -1;
}

int Bank::Add(const RtiFile& rti)
{
    if (!rti.Valid()) return -1;
    int slot = FirstEmptySlot();
    if (slot < 0) return -1;
    return AddAt(slot, rti) ? slot : -1;
}

bool Bank::AddAt(int slot, const RtiFile& rti)
{
    if (slot < 0 || slot >= SLOT_COUNT) return false;
    if (!rti.Valid()) return false;

    Slot& s = m_slots[slot];
    s.used        = true;
    s.dirty       = true;
    s.source_path = rti.Path();
    s.name        = rti.Name();
    s.version     = rti.Version();
    s.ata         = rti.AtaBlob();
    return true;
}

int Bank::AddWorking(const std::string& name, const std::vector<byte>& ata,
                     const std::string& source_path)
{
    int slot = FirstEmptySlot();
    if (slot < 0) return -1;
    SetSlot(slot, name, ata, source_path);
    return slot;
}

void Bank::SetSlot(int slot, const std::string& name, const std::vector<byte>& ata,
                   const std::string& source_path)
{
    if (slot < 0 || slot >= SLOT_COUNT) return;
    Slot& s = m_slots[slot];
    s.used        = true;
    s.dirty       = true;
    s.source_path = source_path;
    s.name        = name;
    s.version     = 1;
    s.ata         = ata;
}

void Bank::MarkAllClean()
{
    for (auto& s : m_slots) s.dirty = false;
}

bool Bank::HasDirty() const
{
    for (const auto& s : m_slots) if (s.used && s.dirty) return true;
    return false;
}

void Bank::Remove(int slot)
{
    if (slot < 0 || slot >= SLOT_COUNT) return;
    m_slots[slot] = Slot{};
}

bool Bank::RemoveByPath(const std::string& path)
{
    int idx = IndexOfPath(path);
    if (idx < 0) return false;
    Remove(idx);
    return true;
}

void Bank::Clear()
{
    for (auto& s : m_slots) s = Slot{};
}

int Bank::UsedCount() const
{
    int n = 0;
    for (const auto& s : m_slots) if (s.used) ++n;
    return n;
}

bool Bank::IsFull() const { return UsedCount() >= SLOT_COUNT; }

int Bank::HighestUsedPlusOne() const
{
    for (int i = SLOT_COUNT - 1; i >= 0; --i) {
        if (m_slots[i].used) return i + 1;
    }
    return 0;
}

int Bank::SaveTo(const std::string& outdir) const
{
    std::error_code ec;
    fs::create_directories(outdir, ec);
    if (ec) {
        std::fprintf(stderr, "Bank::SaveTo: create_directories failed: %s\n",
                     ec.message().c_str());
        return -1;
    }

    int written = 0;
    std::ofstream manifest(fs::path(outdir) / "manifest.txt", std::ios::trunc);
    if (manifest) manifest << "# PokeyForge bank manifest. slot\tname\tsource\n";

    for (int i = 0; i < SLOT_COUNT; ++i) {
        const Slot& s = m_slots[i];
        if (!s.used) continue;

        char prefix[8];
        std::snprintf(prefix, sizeof(prefix), "%02d_", i);
        std::string fname = std::string(prefix) + SanitiseForFilename(s.name) + ".rti";
        fs::path out = fs::path(outdir) / fname;

        std::ofstream of(out, std::ios::binary | std::ios::trunc);
        if (!of) {
            std::fprintf(stderr, "Bank::SaveTo: cannot create %s\n",
                         out.string().c_str());
            continue;
        }

        // Header: 'R','T','I', version (1)
        char head[4] = { 'R', 'T', 'I', (char)s.version };
        of.write(head, 4);

        // 33-byte name (RMT layout: 32 chars + terminating zero).
        char name_buf[33] = {0};
        std::memset(name_buf, ' ', 32);
        size_t nm = std::min<size_t>(s.name.size(), 32);
        std::memcpy(name_buf, s.name.data(), nm);
        of.write(name_buf, sizeof(name_buf));

        byte len = (byte)std::min<size_t>(s.ata.size(), 0xFFu);
        of.write(reinterpret_cast<const char*>(&len), 1);
        if (len > 0) of.write(reinterpret_cast<const char*>(s.ata.data()), len);

        if (manifest) {
            manifest << i << '\t' << s.name << '\t' << s.source_path << '\n';
        }
        ++written;
    }
    return written;
}

// ---------------------------------------------------------------------------
// RMT module export
// ---------------------------------------------------------------------------

namespace {

void PutWord(std::vector<byte>& mem, int addr, int value)
{
    mem[addr]     = (byte)(value & 0xFF);
    mem[addr + 1] = (byte)((value >> 8) & 0xFF);
}

// Append an Atari binary block (from..to inclusive) to the output stream.
// When `with_header` is true the FFFF marker is emitted first.
void WriteBinaryBlock(std::ofstream& out, const std::vector<byte>& mem,
                      int from, int to, bool with_header)
{
    auto w16 = [&](int v) {
        char lo = (char)(v & 0xFF), hi = (char)((v >> 8) & 0xFF);
        out.write(&lo, 1); out.write(&hi, 1);
    };
    if (with_header) w16(0xFFFF);
    w16(from);
    w16(to);
    out.write(reinterpret_cast<const char*>(mem.data() + from), to - from + 1);
}

} // anonymous namespace

bool Bank::SaveRmt(const std::string& rmt_path) const
{
    const int addr = 0x4000;
    const int numInstr  = HighestUsedPlusOne();
    const int numTracks = 1;

    std::vector<byte> mem(0x10000, 0);

    int ptrInstruments  = addr + 16;
    int ptrTracksLo     = ptrInstruments + numInstr * 2;
    int ptrTracksHi     = ptrTracksLo + numTracks;
    int ptrInstrData    = ptrTracksHi + numTracks;

    // Instrument data + pointer table.
    for (int i = 0; i < numInstr; ++i) {
        const Slot& s = m_slots[i];
        if (s.used && !s.ata.empty()) {
            PutWord(mem, ptrInstruments + i * 2, ptrInstrData);
            std::memcpy(mem.data() + ptrInstrData, s.ata.data(), s.ata.size());
            ptrInstrData += (int)s.ata.size();
        } else {
            PutWord(mem, ptrInstruments + i * 2, 0);
        }
    }

    // One empty track: a single pause spanning the whole (64-line) track.
    int ptrTrackData = ptrInstrData;
    mem[ptrTrackData + 0] = 62;   // pause indicator
    mem[ptrTrackData + 1] = 64;   // 64 beats
    mem[ptrTracksLo] = (byte)(ptrTrackData & 0xFF);
    mem[ptrTracksHi] = (byte)((ptrTrackData >> 8) & 0xFF);
    int trackLen = 2;

    // Song: line 0 plays track 0 on all four channels, then a goto loops
    // back to line 0 -> silent, harmless, valid.
    int ptrSongData = ptrTrackData + trackLen;
    mem[ptrSongData + 0] = 0;
    mem[ptrSongData + 1] = 0;
    mem[ptrSongData + 2] = 0;
    mem[ptrSongData + 3] = 0;
    int goLine = ptrSongData + 4;
    mem[goLine + 0] = 254;                       // go command
    mem[goLine + 1] = 0;                          // jump to song line 0
    PutWord(mem, goLine + 2, ptrSongData);        // goto vector
    int endOfModule = goLine + 4;

    // Header.
    mem[addr + 0] = 'R'; mem[addr + 1] = 'M'; mem[addr + 2] = 'T';
    mem[addr + 3] = '4';                 // 4 channels (mono)
    mem[addr + 4] = 64;                  // track length
    mem[addr + 5] = 6;                   // song speed
    mem[addr + 6] = 1;                   // instrument speed
    mem[addr + 7] = 1;                   // RMT format version
    PutWord(mem, addr + 8,  ptrInstruments);
    PutWord(mem, addr + 10, ptrTracksLo);
    PutWord(mem, addr + 12, ptrTracksHi);
    PutWord(mem, addr + 14, ptrSongData);

    // Names block: song name then each used instrument's name.
    int nameAddr = endOfModule;
    const char* songName = "PokeyForge Bank";
    int sl = (int)std::strlen(songName) + 1;
    std::memcpy(mem.data() + nameAddr, songName, sl);
    int namesStart = nameAddr;
    nameAddr += sl;
    for (int i = 0; i < numInstr; ++i) {
        if (m_slots[i].used) {
            std::string nm = m_slots[i].name;
            int len = (int)nm.size() + 1;
            std::memcpy(mem.data() + nameAddr, nm.c_str(), len);
            nameAddr += len;
        }
    }

    std::ofstream out(rmt_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "Bank::SaveRmt: cannot create %s\n", rmt_path.c_str());
        return false;
    }
    WriteBinaryBlock(out, mem, addr, endOfModule - 1, /*with_header=*/true);
    WriteBinaryBlock(out, mem, namesStart, nameAddr - 1, /*with_header=*/false);
    return true;
}

// ---------------------------------------------------------------------------
// Bank loading
// ---------------------------------------------------------------------------

int Bank::LoadFromManifest(const std::string& folder_or_manifest)
{
    fs::path p(folder_or_manifest);
    fs::path manifest = fs::is_directory(p) ? (p / "manifest.txt") : p;
    fs::path base = manifest.parent_path();

    std::ifstream in(manifest);
    if (!in) {
        std::fprintf(stderr, "Bank::LoadFromManifest: cannot open %s\n",
                     manifest.string().c_str());
        return -1;
    }

    Clear();
    int loaded = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        // Format: slot \t name \t source_path
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        int slot = std::atoi(line.substr(0, t1).c_str());
        size_t t2 = line.find('\t', t1 + 1);
        std::string src = (t2 == std::string::npos) ? "" : line.substr(t2 + 1);

        // Prefer the recorded source path; fall back to the NN_name.rti next
        // to the manifest.
        RtiFile rti;
        bool ok = !src.empty() && rti.LoadFromFile(src.c_str());
        if (!ok) {
            char prefix[8];
            std::snprintf(prefix, sizeof(prefix), "%02d_", slot);
            for (auto& e : fs::directory_iterator(base)) {
                auto fn = e.path().filename().string();
                if (fn.rfind(prefix, 0) == 0) {
                    ok = rti.LoadFromFile(e.path().string().c_str());
                    break;
                }
            }
        }
        if (ok && slot >= 0 && slot < SLOT_COUNT && AddAt(slot, rti)) ++loaded;
    }
    return loaded;
}

int Bank::LoadFromRmt(const std::string& rmt_path)
{
    std::ifstream in(rmt_path, std::ios::binary);
    if (!in) return -1;
    std::vector<byte> file((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());

    // Parse the first Atari binary block into a 64K image.
    std::vector<byte> mem(0x10000, 0);
    size_t p = 0;
    auto rd16 = [&](size_t& i) -> int {
        if (i + 1 >= file.size()) return -1;
        int v = file[i] | (file[i + 1] << 8);
        i += 2;
        return v;
    };

    int from = rd16(p);
    if (from == 0xFFFF) from = rd16(p);
    int to = rd16(p);
    if (from < 0 || to < from || to >= 0x10000) return -1;
    size_t blockLen = (size_t)(to - from + 1);
    if (p + blockLen > file.size()) return -1;
    std::memcpy(mem.data() + from, file.data() + p, blockLen);

    int addr = from;
    if (mem[addr] != 'R' || mem[addr + 1] != 'M' || mem[addr + 2] != 'T') return -1;

    int ptrInstruments = mem[addr + 8] | (mem[addr + 9] << 8);
    // Number of instruments = entries until the track-lo table.
    int ptrTracksLo = mem[addr + 10] | (mem[addr + 11] << 8);
    int numInstr = (ptrTracksLo - ptrInstruments) / 2;
    if (numInstr < 0) numInstr = 0;
    if (numInstr > SLOT_COUNT) numInstr = SLOT_COUNT;

    // Second block: names (song name then instrument names).
    size_t namesStart = p + blockLen;
    std::vector<std::string> names;
    if (namesStart + 4 <= file.size()) {
        size_t q = namesStart;
        int nf = rd16(q);
        if (nf == 0xFFFF) nf = rd16(q);
        int nt = rd16(q);
        if (nf >= 0 && nt >= nf && nt < 0x10000 &&
            q + (size_t)(nt - nf + 1) <= file.size()) {
            const byte* nm = file.data() + q;
            int nlen = nt - nf + 1;
            int idx = 0;
            // Skip song name.
            while (idx < nlen && nm[idx]) ++idx;
            ++idx;
            while (idx < nlen) {
                std::string s;
                while (idx < nlen && nm[idx]) s += (char)nm[idx++];
                ++idx;
                names.push_back(s);
            }
        }
    }

    Clear();
    int loaded = 0;
    int nameCursor = 0;
    for (int i = 0; i < numInstr; ++i) {
        int ptr = mem[ptrInstruments + i * 2] | (mem[ptrInstruments + i * 2 + 1] << 8);
        if (ptr == 0) continue;
        // Instrument blob length is derivable from its own header: the
        // envelope-end pointer (byte 2) + 3 (see RtiFile / InstrToAta).
        int blobLen = mem[ptr + 2] + 3;
        if (ptr + blobLen > 0x10000 || blobLen <= 0) continue;

        Slot& s = m_slots[i];
        s.used    = true;
        s.version = 1;
        s.ata.assign(mem.begin() + ptr, mem.begin() + ptr + blobLen);
        s.name    = (nameCursor < (int)names.size()) ? names[nameCursor]
                                                     : ("instr" + std::to_string(i));
        s.source_path.clear();
        ++nameCursor;
        ++loaded;
    }
    return loaded;
}
