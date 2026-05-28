#include "RtiFile.h"

#include <cstring>
#include <fstream>

namespace {

// Strip trailing spaces / nulls so the GUI doesn't render junk.
std::string TrimRight(const char* p, size_t n)
{
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\0')) --n;
    return std::string(p, n);
}

// Decode the v1 ATA blob. Verbatim port of AtaToInstr from
// IO_Instruments.cpp:443.
bool AtaToInstrV1(const byte* mem, TInstrument& ai, bool stereo)
{
    int noteTableLength = mem[0] - 12;
    int noteTableGoto   = mem[1] - 12;
    int envelopeLength  = (mem[2] - (mem[0] + 1)) / 3;
    int envelopeGoto    = (mem[3] - (mem[0] + 1)) / 3;

    if (noteTableLength >= NOTE_TABLE_MAX_LEN || noteTableGoto > noteTableLength ||
        envelopeLength >= ENVELOPE_MAX_COLUMNS || envelopeGoto > envelopeLength) {
        return false;
    }

    int* par = ai.parameters;
    par[PAR_TBL_LENGTH] = noteTableLength;
    par[PAR_TBL_GOTO]   = noteTableGoto;
    par[PAR_ENV_LENGTH] = envelopeLength;
    par[PAR_ENV_GOTO]   = envelopeGoto;

    par[PAR_TBL_TYPE]  =  mem[4] >> 7;
    par[PAR_TBL_MODE]  = (mem[4] >> 6) & 0x01;
    par[PAR_TBL_SPEED] =  mem[4] & 0x3f;

    par[PAR_AUDCTL_15KHZ]    =  mem[5]       & 0x01;
    par[PAR_AUDCTL_HPF_CH2]  = (mem[5] >> 1) & 0x01;
    par[PAR_AUDCTL_HPF_CH1]  = (mem[5] >> 2) & 0x01;
    par[PAR_AUDCTL_JOIN_3_4] = (mem[5] >> 3) & 0x01;
    par[PAR_AUDCTL_JOIN_1_2] = (mem[5] >> 4) & 0x01;
    par[PAR_AUDCTL_179_CH3]  = (mem[5] >> 5) & 0x01;
    par[PAR_AUDCTL_179_CH1]  = (mem[5] >> 6) & 0x01;
    par[PAR_AUDCTL_POLY9]    = (mem[5] >> 7) & 0x01;

    par[PAR_VOL_FADEOUT] = mem[6];
    par[PAR_VOL_MIN]     = mem[7] >> 4;
    par[PAR_DELAY]       = mem[8];
    par[PAR_VIBRATO]     = mem[9] & 0x03;
    par[PAR_FREQ_SHIFT]  = mem[10];

    for (int i = 0; i <= par[PAR_TBL_LENGTH]; ++i) {
        ai.noteTable[i] = mem[12 + i];
    }

    int p = mem[0] + 1;
    for (int i = 0; i <= par[PAR_ENV_LENGTH]; ++i, p += 3) {
        int* env = ai.envelope[i];
        env[VOLUMER] = stereo ? (mem[p] >> 4) : (mem[p] & 0x0f);
        env[VOLUMEL] =  mem[p] & 0x0f;

        env[FILTER]     =  mem[p + 1] >> 7;
        env[COMMAND]    = (mem[p + 1] >> 4) & 0x07;
        env[DISTORTION] =  mem[p + 1] & 0x0e;
        env[PORTAMENTO] =  mem[p + 1] & 0x01;

        env[X] = mem[p + 2] >> 4;
        env[Y] = mem[p + 2] & 0x0f;
    }
    return true;
}

bool AtaToInstrV0(const byte* ata, TInstrument& ai, bool stereo)
{
    for (int i = 0; i <= 7; ++i) ai.noteTable[i] = ata[i];

    int* par = ai.parameters;
    int len  = par[PAR_ENV_LENGTH] = ata[8] >> 3;
    par[PAR_TBL_LENGTH] = ata[8] & 0x07;
    par[PAR_ENV_GOTO]   = ata[9] >> 3;
    par[PAR_TBL_GOTO]   = ata[9] & 0x07;
    par[PAR_TBL_TYPE]   = ata[10] >> 7;
    par[PAR_TBL_MODE]   = (ata[10] >> 6) & 0x01;
    par[PAR_TBL_SPEED]  = ata[10] & 0x3f;
    par[PAR_VOL_FADEOUT] = ata[11];
    par[PAR_VOL_MIN]    = ata[12] >> 4;

    par[PAR_AUDCTL_15KHZ]    = ata[12] & 0x01;
    par[PAR_AUDCTL_HPF_CH2]  = 0;
    par[PAR_AUDCTL_HPF_CH1]  = 0;
    par[PAR_AUDCTL_JOIN_3_4] = 0;
    par[PAR_AUDCTL_JOIN_1_2] = 0;
    par[PAR_AUDCTL_179_CH3]  = 0;
    par[PAR_AUDCTL_179_CH1]  = 0;
    par[PAR_AUDCTL_POLY9]    = (ata[12] >> 1) & 0x01;

    par[PAR_DELAY]      = ata[13];
    par[PAR_VIBRATO]    = ata[14] & 0x03;
    par[PAR_FREQ_SHIFT] = ata[15];

    int p = 16;
    for (int i = 0; i <= len; ++i, p += 3) {
        int* env = ai.envelope[i];
        env[VOLUMER] = stereo ? (ata[p] >> 4) : (ata[p] & 0x0f);
        env[VOLUMEL] = ata[p] & 0x0f;
        env[FILTER]     = ata[p + 1] >> 7;
        env[COMMAND]    = (ata[p + 1] >> 4) & 0x07;
        env[DISTORTION] = ata[p + 1] & 0x0e;
        env[PORTAMENTO] = ata[p + 1] & 0x01;
        env[X]          = ata[p + 2] >> 4;
        env[Y]          = ata[p + 2] & 0x0f;
    }
    return true;
}

} // anonymous namespace

bool RtiFile::LoadFromFile(const char* path)
{
    m_path = path ? path : "";

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        m_valid = false;
        return false;
    }
    in.seekg(0, std::ios::end);
    auto size = (size_t)in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<byte> buf(size);
    if (size) in.read(reinterpret_cast<char*>(buf.data()), size);

    return LoadFromMemory(buf.data(), buf.size());
}

bool RtiFile::LoadFromMemory(const byte* data, size_t size)
{
    m_valid   = false;
    m_version = -1;
    m_name.clear();
    m_ata.clear();

    if (size < 4 + 33 + 1) return false;
    if (data[0] != 'R' || data[1] != 'T' || data[2] != 'I') return false;

    int version = data[3];
    if (version != 0 && version != 1) return false;

    m_version = version;
    m_name = TrimRight(reinterpret_cast<const char*>(data + 4), 32);

    size_t len = data[4 + 33];
    if (size < 4 + 33 + 1 + len) return false;

    m_ata.assign(data + 4 + 33 + 1, data + 4 + 33 + 1 + len);
    m_valid = true;
    return true;
}

bool RtiFile::WriteFile(const std::string& path, const std::string& name,
                        const std::vector<byte>& ata)
{
    std::ofstream of(path, std::ios::binary | std::ios::trunc);
    if (!of) return false;

    char head[4] = { 'R', 'T', 'I', 1 };
    of.write(head, 4);

    char name_buf[33] = {0};
    std::memset(name_buf, ' ', 32);
    size_t nm = name.size() < 32 ? name.size() : 32;
    std::memcpy(name_buf, name.data(), nm);
    of.write(name_buf, sizeof(name_buf));

    byte len = (byte)(ata.size() < 0xFFu ? ata.size() : 0xFFu);
    of.write(reinterpret_cast<const char*>(&len), 1);
    if (len > 0) of.write(reinterpret_cast<const char*>(ata.data()), len);
    return (bool)of;
}

std::vector<byte> RtiFile::InstrumentToAta(const TInstrument& ai, bool stereo)
{
    const int* par = ai.parameters;
    const int INSTRPAR = 12;

    std::vector<byte> ata(256, 0);

    int tablelast = par[PAR_TBL_LENGTH] + INSTRPAR;
    ata[0] = (byte)tablelast;
    ata[1] = (byte)(par[PAR_TBL_GOTO] + INSTRPAR);
    ata[2] = (byte)(par[PAR_ENV_LENGTH] * 3 + tablelast + 1);
    ata[3] = (byte)(par[PAR_ENV_GOTO] * 3 + tablelast + 1);

    ata[4] = (byte)((par[PAR_TBL_TYPE] << 7)
                  | (par[PAR_TBL_MODE] << 6)
                  | (par[PAR_TBL_SPEED]));

    ata[5] = (byte)( par[PAR_AUDCTL_15KHZ]
                  | (par[PAR_AUDCTL_HPF_CH2]  << 1)
                  | (par[PAR_AUDCTL_HPF_CH1]  << 2)
                  | (par[PAR_AUDCTL_JOIN_3_4] << 3)
                  | (par[PAR_AUDCTL_JOIN_1_2] << 4)
                  | (par[PAR_AUDCTL_179_CH3]  << 5)
                  | (par[PAR_AUDCTL_179_CH1]  << 6)
                  | (par[PAR_AUDCTL_POLY9]    << 7));

    ata[6]  = (byte)par[PAR_VOL_FADEOUT];
    ata[7]  = (byte)(par[PAR_VOL_MIN] << 4);
    ata[8]  = (byte)par[PAR_DELAY];
    ata[9]  = (byte)(par[PAR_VIBRATO] & 0x03);
    ata[10] = (byte)par[PAR_FREQ_SHIFT];
    ata[11] = 0;

    for (int i = 0; i <= par[PAR_TBL_LENGTH]; ++i) {
        ata[INSTRPAR + i] = (byte)ai.noteTable[i];
    }

    int len = par[PAR_ENV_LENGTH];
    for (int i = 0, j = tablelast + 1; i <= len; ++i, j += 3) {
        const int* env = ai.envelope[i];
        ata[j] = (byte)(stereo
            ? ((env[VOLUMER] << 4) | (env[VOLUMEL]))
            : ((env[VOLUMEL] << 4) | (env[VOLUMEL])));
        ata[j + 1] = (byte)((env[FILTER] << 7)
                          | (env[COMMAND] << 4)
                          | (env[DISTORTION])
                          | (env[PORTAMENTO]));
        ata[j + 2] = (byte)((env[X] << 4) | (env[Y]));
    }

    int total = tablelast + 1 + (len + 1) * 3;
    ata.resize(total);
    return ata;
}

bool RtiFile::ToInstrument(TInstrument& out, bool stereo) const
{
    std::memset(&out, 0, sizeof(out));
    out.activeEditSection = InstrumentSection::NONE;
    out.editParameterNr   = -1;
    out.octave            = 2;
    out.volume            = 15;

    size_t nm = m_name.size();
    if (nm > INSTRUMENT_NAME_MAX_LEN) nm = INSTRUMENT_NAME_MAX_LEN;
    std::memcpy(out.name, m_name.data(), nm);

    if (!m_valid || m_ata.empty()) return false;

    if (m_version == 0) return AtaToInstrV0(m_ata.data(), out, stereo);
    return AtaToInstrV1(m_ata.data(), out, stereo);
}
