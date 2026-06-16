#include "Bank.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

int Bank::IndexOfAta(const std::vector<byte>& ata) const
{
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (m_slots[i].used && m_slots[i].ata == ata) return i;
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
    // Drop any captured source-RMT metadata so a subsequent SaveRmt falls
    // back to the silent-loop boilerplate. LoadFromRmt repopulates this
    // after calling Clear(); LoadFromManifest leaves it cleared (manifest
    // folders have no source song to preserve).
    m_rmt_source = RmtModuleMeta{};
}

int Bank::UsedCount() const
{
    int n = 0;
    for (const auto& s : m_slots) if (s.used) ++n;
    return n;
}

bool Bank::IsFull() const { return UsedCount() >= SLOT_COUNT; }

void Bank::SetClusterInfo(int slot, const std::string& info, std::uint64_t hash)
{
    if (slot < 0 || slot >= SLOT_COUNT) return;
    m_slots[slot].cluster_info = info;
    m_slots[slot].cluster_hash = hash;
}

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
    if (manifest) {
        manifest << "# PokeyForge bank manifest.\n"
                    "# Columns: slot \\t name \\t source \\t cluster_info \\t cluster_hash\n"
                    "# - cluster_info: multi-line; newlines escaped as '\\n'\n"
                    "# - cluster_hash: hex FNV-1a of the slot's ATA when cluster_info was set\n"
                    "# Both cluster columns are optional - older PokeyForge writes (or banks\n"
                    "# never analysed) omit them, and the loader treats absent columns as\n"
                    "# 'no cached cluster info'.\n";
    }

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
            // Escape \n and \t inside cluster_info so it round-trips through
            // a one-line-per-slot manifest. Only \n is actually used today
            // (FindClusterForBankSlot emits two-line strings) but escaping
            // \t too keeps the format robust.
            std::string esc;
            esc.reserve(s.cluster_info.size());
            for (char c : s.cluster_info) {
                if      (c == '\\') esc += "\\\\";
                else if (c == '\n') esc += "\\n";
                else if (c == '\t') esc += "\\t";
                else                esc += c;
            }
            char hashbuf[24];
            std::snprintf(hashbuf, sizeof(hashbuf), "%016llx",
                          (unsigned long long)s.cluster_hash);
            manifest << i << '\t' << s.name << '\t' << s.source_path
                     << '\t' << esc
                     << '\t' << (s.cluster_info.empty() ? "" : hashbuf)
                     << '\n';
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
    // Round-trip the source module when one was captured at load time,
    // so songs/tracks/header config survive an edit-and-save cycle.
    // Otherwise emit the hand-built silent-loop module (legacy behaviour
    // for banks built from scratch or loaded from a manifest folder).
    if (m_rmt_source.valid) return SaveRmtRoundTrip(rmt_path);
    return SaveRmtSilent(rmt_path);
}

bool Bank::SaveRmtSilent(const std::string& rmt_path) const
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

bool Bank::SaveRmtRoundTrip(const std::string& rmt_path) const
{
    // Rebuild the source module's first block with current bank slots
    // plugged into the instrument-pointer table. Song, tracks, header
    // metadata, and song name all come from m_rmt_source (captured at
    // LoadFromRmt time). The instrument count grows to fit any new bank
    // slots but never shrinks below the source count - the song's
    // instrument-index references in track data must stay valid; removed
    // slots get a zero pointer (RMT silences those notes).
    const int addr      = m_rmt_source.base_addr;
    const int numInstr  = std::max(m_rmt_source.num_instr_source,
                                   HighestUsedPlusOne());
    const int numTracks = m_rmt_source.num_tracks;

    std::vector<byte> mem(0x10000, 0);

    const int ptrInstruments = addr + 16;
    const int ptrTracksLo    = ptrInstruments + numInstr * 2;
    const int ptrTracksHi    = ptrTracksLo + numTracks;
    const int ptrSongData    = ptrTracksHi + numTracks;

    // 1. Song data verbatim. The trailing 4 bytes are the 254-goto
    //    record [254, line, lo, hi]; lo/hi point at the loop target
    //    inside the original song region, so we re-point them at the
    //    new song base + the captured relative offset.
    const int songLen = (int)m_rmt_source.song_data.size();
    if (ptrSongData + songLen > 0x10000) {
        std::fprintf(stderr, "Bank::SaveRmt: module would exceed 64 KiB (song)\n");
        return false;
    }
    std::memcpy(mem.data() + ptrSongData,
                m_rmt_source.song_data.data(), songLen);
    if (songLen >= 4) {
        const int newGotoTarget = ptrSongData + m_rmt_source.song_goto_target_off;
        const int gotoLoOffset  = ptrSongData + songLen - 2;
        PutWord(mem, gotoLoOffset, newGotoTarget);
    }
    int cursor = ptrSongData + songLen;

    // 2. Track event blobs, each track's new pointer recorded in
    //    tracks_lo / tracks_hi. Empty captured tracks write a 0 pointer
    //    (matches LoadFromRmt's handling of zero pointers).
    for (int i = 0; i < numTracks; ++i) {
        const auto& blob = m_rmt_source.tracks[i];
        if (blob.empty()) {
            mem[ptrTracksLo + i] = 0;
            mem[ptrTracksHi + i] = 0;
            continue;
        }
        if (cursor + (int)blob.size() > 0x10000) {
            std::fprintf(stderr, "Bank::SaveRmt: module would exceed 64 KiB (track %d)\n", i);
            return false;
        }
        std::memcpy(mem.data() + cursor, blob.data(), blob.size());
        mem[ptrTracksLo + i] = (byte)(cursor & 0xFF);
        mem[ptrTracksHi + i] = (byte)((cursor >> 8) & 0xFF);
        cursor += (int)blob.size();
    }

    // 3. Instrument ATA blobs. Used slots get a real pointer; unused
    //    slots (including holes the user opened up since load) get 0.
    for (int i = 0; i < numInstr; ++i) {
        const Slot& s = m_slots[i];
        if (s.used && !s.ata.empty()) {
            if (cursor + (int)s.ata.size() > 0x10000) {
                std::fprintf(stderr, "Bank::SaveRmt: module would exceed 64 KiB (instr %d)\n", i);
                return false;
            }
            PutWord(mem, ptrInstruments + i * 2, cursor);
            std::memcpy(mem.data() + cursor, s.ata.data(), s.ata.size());
            cursor += (int)s.ata.size();
        } else {
            PutWord(mem, ptrInstruments + i * 2, 0);
        }
    }

    const int endOfModule = cursor;

    // 4. Header. Bytes 3-7 preserved from source; pointers reflect new
    //    layout. Magic is always "RMT".
    mem[addr + 0] = 'R'; mem[addr + 1] = 'M'; mem[addr + 2] = 'T';
    mem[addr + 3] = m_rmt_source.channels;
    mem[addr + 4] = m_rmt_source.track_len;
    mem[addr + 5] = m_rmt_source.song_speed;
    mem[addr + 6] = m_rmt_source.instr_speed;
    mem[addr + 7] = m_rmt_source.format_ver;
    PutWord(mem, addr + 8,  ptrInstruments);
    PutWord(mem, addr + 10, ptrTracksLo);
    PutWord(mem, addr + 12, ptrTracksHi);
    PutWord(mem, addr + 14, ptrSongData);

    // 5. Names block: source song name (or fallback) then per-used-slot
    //    name in slot order. Empty slots are skipped - matches the
    //    LoadFromRmt parser which assigns names to used slots only.
    int nameAddr = endOfModule;
    const int namesStart = nameAddr;
    const std::string songName =
        m_rmt_source.song_name.empty() ? std::string("PokeyForge Bank")
                                       : m_rmt_source.song_name;
    if (nameAddr + (int)songName.size() + 1 > 0x10000) {
        std::fprintf(stderr, "Bank::SaveRmt: module would exceed 64 KiB (song name)\n");
        return false;
    }
    std::memcpy(mem.data() + nameAddr, songName.c_str(), songName.size() + 1);
    nameAddr += (int)songName.size() + 1;
    for (int i = 0; i < numInstr; ++i) {
        if (!m_slots[i].used) continue;
        const std::string& nm = m_slots[i].name;
        if (nameAddr + (int)nm.size() + 1 > 0x10000) {
            std::fprintf(stderr, "Bank::SaveRmt: module would exceed 64 KiB (instr names)\n");
            return false;
        }
        std::memcpy(mem.data() + nameAddr, nm.c_str(), nm.size() + 1);
        nameAddr += (int)nm.size() + 1;
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
        // Format (current): slot \t name \t source \t cluster_info \t cluster_hash
        // Format (legacy):  slot \t name \t source
        // The two trailing fields are optional; missing or empty values
        // leave the slot's cluster cache unset.
        std::vector<std::string> cols;
        {
            size_t start = 0;
            while (start <= line.size()) {
                size_t t = line.find('\t', start);
                cols.push_back(line.substr(start,
                    (t == std::string::npos ? line.size() : t) - start));
                if (t == std::string::npos) break;
                start = t + 1;
            }
        }
        if (cols.size() < 2) continue;
        int slot = std::atoi(cols[0].c_str());
        std::string src = (cols.size() >= 3) ? cols[2] : std::string{};

        // Un-escape cluster_info written by SaveTo (\\ -> \, \n -> newline,
        // \t -> tab).
        std::string cluster_info;
        if (cols.size() >= 4 && !cols[3].empty()) {
            const std::string& raw = cols[3];
            cluster_info.reserve(raw.size());
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size()) {
                    char n = raw[++i];
                    if      (n == 'n')  cluster_info += '\n';
                    else if (n == 't')  cluster_info += '\t';
                    else if (n == '\\') cluster_info += '\\';
                    else { cluster_info += '\\'; cluster_info += n; }
                } else {
                    cluster_info += raw[i];
                }
            }
        }
        std::uint64_t cluster_hash = 0;
        if (cols.size() >= 5 && !cols[4].empty()) {
            cluster_hash = std::strtoull(cols[4].c_str(), nullptr, 16);
        }

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
        if (ok && slot >= 0 && slot < SLOT_COUNT && AddAt(slot, rti)) {
            // AddAt rebuilds the slot from the .rti, so it stomps any
            // cluster fields the default Slot{} ctor left at empty/0.
            // Restore them from the manifest now that the slot exists.
            SetClusterInfo(slot, cluster_info, cluster_hash);
            ++loaded;
        }
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

    int ptrInstruments = mem[addr + 8]  | (mem[addr + 9]  << 8);
    int ptrTracksLo    = mem[addr + 10] | (mem[addr + 11] << 8);
    int ptrTracksHi    = mem[addr + 12] | (mem[addr + 13] << 8);
    int ptrSongData    = mem[addr + 14] | (mem[addr + 15] << 8);
    // Number of instruments = entries until the track-lo table.
    int numInstr = (ptrTracksLo - ptrInstruments) / 2;
    if (numInstr < 0) numInstr = 0;
    if (numInstr > SLOT_COUNT) numInstr = SLOT_COUNT;

    // Second block: names (song name then instrument names). Capture the
    // song name so SaveRmt can preserve it on round-trip; the previous
    // code skipped past it.
    size_t namesStart = p + blockLen;
    std::string songName;
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
            // Song name comes first.
            while (idx < nlen && nm[idx]) songName += (char)nm[idx++];
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

    // ----- Capture source-module metadata for SaveRmt round-trip ---------
    //
    // Validate the header pointers, slice each track's event blob, and
    // scan the song table for its 254-goto terminator. On any failure
    // we leave m_rmt_source.valid = false (Clear() ran above, so the
    // default value is already in place) and SaveRmt falls back to the
    // legacy silent-loop path - no regression vs the pre-round-trip
    // behaviour.
    const int blockEnd = addr + (int)blockLen;
    auto ptrInBlock = [&](int q) { return q >= addr && q < blockEnd; };

    RmtModuleMeta meta;
    bool meta_ok = true;

    // Minimum requirement for ANY usable metadata (song view + round-trip):
    // pointers must all be inside the loaded block. RMT itself doesn't
    // enforce any ordering between the four pointer slots, so don't reject
    // a file just because the tables aren't in the order PokeyForge would
    // emit. We only need ptrTracksHi - ptrTracksLo (the track-table size)
    // and ptrSongData (the song-list base) to be coherent.
    if (!ptrInBlock(ptrInstruments) || !ptrInBlock(ptrTracksLo) ||
        !ptrInBlock(ptrTracksHi)    || !ptrInBlock(ptrSongData)) {
        meta.fail_reason = "pointer(s) out of block";
        meta_ok = false;
    }

    int numTracks = 0;
    if (meta_ok) {
        const int numTracksA = ptrTracksHi - ptrTracksLo;
        if (numTracksA < 0 || numTracksA > 256) {
            meta.fail_reason = "track-table size out of range (" +
                               std::to_string(numTracksA) + ")";
            meta_ok = false;
        } else {
            numTracks = numTracksA;
        }
    }

    if (meta_ok) {
        // Read the per-track 16-bit pointers.
        std::vector<int> trackPtr(numTracks, 0);
        for (int i = 0; i < numTracks; ++i) {
            trackPtr[i] = mem[ptrTracksLo + i] | (mem[ptrTracksHi + i] << 8);
        }

        // Build the sorted boundary set: every reasonable "next-thing"
        // pointer that could mark the end of a track's event data.
        // Tracks are packed contiguously in RMT-exported modules, so
        // a track's bytes run from its pointer to the next-higher
        // boundary in this set.
        std::vector<int> boundaries;
        boundaries.push_back(ptrSongData);
        boundaries.push_back(blockEnd);
        for (int i = 0; i < numTracks; ++i) {
            if (trackPtr[i] > 0 && ptrInBlock(trackPtr[i]))
                boundaries.push_back(trackPtr[i]);
        }
        for (int i = 0; i < numInstr; ++i) {
            int ip = mem[ptrInstruments + i * 2] |
                     (mem[ptrInstruments + i * 2 + 1] << 8);
            if (ip > 0 && ptrInBlock(ip)) boundaries.push_back(ip);
        }
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                         boundaries.end());

        meta.tracks.resize(numTracks);
        for (int i = 0; i < numTracks; ++i) {
            int start = trackPtr[i];
            if (start <= 0 || !ptrInBlock(start)) {
                // Zero pointer or out-of-range - keep an empty blob.
                continue;
            }
            auto it = std::upper_bound(boundaries.begin(),
                                       boundaries.end(), start);
            int end = (it == boundaries.end()) ? blockEnd : *it;
            if (end <= start) continue;
            meta.tracks[i].assign(mem.begin() + start, mem.begin() + end);
        }

        // Capture the entire song region (ptrSongData..blockEnd). This is
        // what RMT itself uses (CSong::AtaToSong walks the full byte range
        // and handles multiple 254-goto records as sub-song terminators).
        // The previous code stopped at the FIRST 254 which truncated
        // multi-section modules to a single sub-song.
        meta.song_data.assign(mem.begin() + ptrSongData,
                              mem.begin() + blockEnd);

        // For backward-compat with the single-section round-trip path:
        // record the offset of the FIRST goto's target relative to
        // ptrSongData. SaveRmtRoundTrip uses this when re-patching the
        // first goto on save. Multi-section round-trip is best-effort
        // (only the first sub-song's goto is re-pointed).
        meta.song_goto_target_off = 0;
        {
            const int rowLen = (mem[addr + 3] == '8') ? 8 : 4;
            int col = 0;
            int q = ptrSongData;
            while (q + 3 < blockEnd) {
                if (mem[q] == 254 && col == 0) {
                    int gotoTarget = mem[q + 2] | (mem[q + 3] << 8);
                    meta.song_goto_target_off = gotoTarget - ptrSongData;
                    break;
                }
                ++q;
                ++col;
                if (col >= rowLen) col = 0;
            }
        }
    }

    if (meta_ok) {
        meta.valid             = true;
        meta.base_addr         = addr;
        meta.num_instr_source  = numInstr;
        meta.num_tracks        = numTracks;
        meta.channels          = mem[addr + 3];
        meta.track_len         = mem[addr + 4];
        meta.song_speed        = mem[addr + 5];
        meta.instr_speed       = mem[addr + 6];
        meta.format_ver        = mem[addr + 7];
        meta.song_name         = std::move(songName);
        m_rmt_source           = std::move(meta);
    }
    return loaded;
}
