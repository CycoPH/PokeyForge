#include "Editor.h"

#include <algorithm>

namespace {

// Editable parameter cells: the 12 named parameters followed by the 8 AUDCTL
// flag bits. Order matches the GUI's Parameters panel.
struct ParamDesc { int par_index; int max_value; bool is_flag; const char* label; const char* help; };

const ParamDesc kParams[] = {
    { PAR_TBL_LENGTH,  NOTE_TABLE_MAX_LEN - 1,   false, "Tbl Len",  "Note-table length: number of steps (0-31)" },
    { PAR_TBL_GOTO,    NOTE_TABLE_MAX_LEN - 1,   false, "Tbl Goto", "Note-table loop point: step it jumps back to" },
    { PAR_TBL_SPEED,   63,                       false, "Tbl Spd",  "Note-table speed: frames per step (0-63)" },
    { PAR_TBL_TYPE,    1,                        false, "Tbl Type", "Note-table type: 0=notes, 1=frequencies" },
    { PAR_TBL_MODE,    1,                        false, "Tbl Mode", "Note-table mode: 0=set, 1=add to played note" },
    { PAR_ENV_LENGTH,  ENVELOPE_MAX_COLUMNS - 1, false, "Env Len",  "Envelope length: number of columns (0-47)" },
    { PAR_ENV_GOTO,    ENVELOPE_MAX_COLUMNS - 1, false, "Env Goto", "Envelope loop point: column it jumps back to" },
    { PAR_VOL_FADEOUT, 255,                      false, "Vol Fade", "Volume fade-out rate after the note (0-255)" },
    { PAR_VOL_MIN,     15,                       false, "Vol Min",  "Minimum volume floor the fade stops at (0-15)" },
    { PAR_DELAY,       255,                      false, "Delay",    "Delay in frames before the instrument starts (0-255)" },
    { PAR_VIBRATO,     3,                        false, "Vibrato",  "Vibrato depth (0=off .. 3=deep)" },
    { PAR_FREQ_SHIFT,  255,                      false, "FrqShift", "Auto frequency shift / pitch bend amount (0-255)" },
    { PAR_AUDCTL_15KHZ,    1, true, "15kHz",  "AUDCTL: use 15 kHz clock base instead of 64 kHz" },
    { PAR_AUDCTL_HPF_CH2,  1, true, "HPF2",   "AUDCTL: high-pass filter channel 2 with channel 4" },
    { PAR_AUDCTL_HPF_CH1,  1, true, "HPF1",   "AUDCTL: high-pass filter channel 1 with channel 3" },
    { PAR_AUDCTL_JOIN_3_4, 1, true, "Join3-4","AUDCTL: join channels 3+4 into one 16-bit channel" },
    { PAR_AUDCTL_JOIN_1_2, 1, true, "Join1-2","AUDCTL: join channels 1+2 into one 16-bit channel" },
    { PAR_AUDCTL_179_CH3,  1, true, "1.79MHz3","AUDCTL: run channel 3 at 1.79 MHz" },
    { PAR_AUDCTL_179_CH1,  1, true, "1.79MHz1","AUDCTL: run channel 1 at 1.79 MHz" },
    { PAR_AUDCTL_POLY9,    1, true, "Poly9",  "AUDCTL: 9-bit poly noise instead of 17-bit" },
};
constexpr int kNumParams = (int)(sizeof(kParams) / sizeof(kParams[0]));

struct EnvRowDesc { const char* label; const char* help; };
const EnvRowDesc kEnvRows[ENVROWS] = {
    { "VolR", "Right-channel volume for this column (0-15)" },
    { "VolL", "Left-channel volume for this column (0-15)" },
    { "Filt", "Filter on/off for this column (0 or 1)" },
    { "Cmd",  "Effect command for this column (0-7)" },
    { "Dist", "POKEY distortion for this column (even 0..E)" },
    { "Port", "Portamento on/off for this column (0 or 1)" },
    { "X",    "Command parameter X for this column (0-15)" },
    { "Y",    "Command parameter Y for this column (0-15)" },
};

// The Parameters panel is laid out as a visual grid so the arrow keys can
// follow the screen layout:
//   col 0: named params 0..5   (Tbl Len .. Env Len),     6 rows
//   col 1: named params 6..11  (Env Goto .. FrqShift),   6 rows
//   col 2..5: the 4x2 AUDCTL block (2 rows each)
// AUDCTL param order is row-major (fi = row*4 + (col-2)).
void ParamCellPos(int idx, int& col, int& row)
{
    if (idx < 6)        { col = 0; row = idx; }
    else if (idx < 12)  { col = 1; row = idx - 6; }
    else { int fi = idx - 12; col = 2 + (fi % 4); row = fi / 4; }
}

int ParamColRows(int col) { return (col <= 1) ? 6 : 2; }

int ParamCellIdx(int col, int row)
{
    col = std::clamp(col, 0, 5);
    row = std::clamp(row, 0, ParamColRows(col) - 1);
    if (col == 0) return row;
    if (col == 1) return 6 + row;
    return 12 + row * 4 + (col - 2);   // AUDCTL
}

// Per-envelope-row maximum value and whether it must stay even (distortion).
void EnvRowRange(int row, int& maxv, bool& even_only)
{
    even_only = false;
    switch (row) {
        case VOLUMER: case VOLUMEL: maxv = 15; break;
        case FILTER:                maxv = 1;  break;
        case COMMAND:               maxv = 7;  break;
        case DISTORTION:            maxv = 14; even_only = true; break;
        case PORTAMENTO:            maxv = 1;  break;
        case X: case Y:             maxv = 15; break;
        default:                    maxv = 15; break;
    }
}

} // anonymous namespace

int  Editor::ParamCellCount() { return kNumParams; }

void Editor::ParamCell(int idx, int& par_index, int& max_value, bool& is_flag)
{
    idx = std::clamp(idx, 0, kNumParams - 1);
    par_index = kParams[idx].par_index;
    max_value = kParams[idx].max_value;
    is_flag   = kParams[idx].is_flag;
}

void Editor::Toggle() { SetActive(!active); }

void Editor::SetActive(bool on)
{
    active = on;
    ResetTyping();
}

void Editor::NextPanel(int dir)
{
    int n = 4;
    int p = ((int)panel + dir % n + n) % n;
    panel = (Panel)p;
    ResetTyping();
}

void Editor::Clamp(const TInstrument& ins)
{
    param_idx = std::clamp(param_idx, 0, kNumParams - 1);
    int envLen = std::clamp(ins.parameters[PAR_ENV_LENGTH], 0, ENVELOPE_MAX_COLUMNS - 1);
    int tblLen = std::clamp(ins.parameters[PAR_TBL_LENGTH], 0, NOTE_TABLE_MAX_LEN - 1);
    env_col = std::clamp(env_col, 0, envLen);
    env_row = std::clamp(env_row, 0, ENVROWS - 1);
    tbl_idx = std::clamp(tbl_idx, 0, tblLen);
    name_pos = std::clamp(name_pos, 0, NameLength(ins)); // insertion point
}

void Editor::Move(int dx, int dy, const TInstrument& ins)
{
    ResetTyping();
    switch (panel) {
        case Panel::Params: {
            // Follow the on-screen grid: left/right move between columns,
            // up/down move within a column.
            int col, row;
            ParamCellPos(param_idx, col, row);
            col = std::clamp(col + dx, 0, 5);
            row = std::clamp(row + dy, 0, ParamColRows(col) - 1);
            param_idx = ParamCellIdx(col, row);
            break;
        }
        case Panel::Envelope:
            env_col += dx;
            env_row += dy;
            break;
        case Panel::NoteTable:
            tbl_idx += dx + dy * 16;
            break;
        case Panel::Name:
            name_pos += dx;
            break;
    }
    Clamp(ins);
}

bool Editor::InputHex(int nibble, TInstrument& ins)
{
    if (nibble < 0 || nibble > 15) return false;

    auto apply = [&](int cur, int maxv) -> int {
        int v = m_typing_fresh ? nibble : ((cur << 4) | nibble);
        m_typing_fresh = false;
        return std::clamp(v, 0, maxv);
    };

    switch (panel) {
        case Panel::Params: {
            int pi, maxv; bool flag;
            ParamCell(param_idx, pi, maxv, flag);
            int nv = apply(ins.parameters[pi], maxv);
            if (nv == ins.parameters[pi]) return false;
            ins.parameters[pi] = nv;
            Clamp(ins);
            return true;
        }
        case Panel::Envelope: {
            int maxv; bool even; EnvRowRange(env_row, maxv, even);
            int nv = apply(ins.envelope[env_col][env_row], maxv);
            if (even) nv &= 0x0E;
            if (nv == ins.envelope[env_col][env_row]) return false;
            ins.envelope[env_col][env_row] = nv;
            return true;
        }
        case Panel::NoteTable: {
            int nv = apply(ins.noteTable[tbl_idx], 255);
            if (nv == ins.noteTable[tbl_idx]) return false;
            ins.noteTable[tbl_idx] = nv;
            return true;
        }
        case Panel::Name:
            return false; // name uses character input
    }
    return false;
}

bool Editor::Increment(int delta, TInstrument& ins)
{
    ResetTyping();
    switch (panel) {
        case Panel::Params: {
            int pi, maxv; bool flag;
            ParamCell(param_idx, pi, maxv, flag);
            int nv = std::clamp(ins.parameters[pi] + delta, 0, maxv);
            if (nv == ins.parameters[pi]) return false;
            ins.parameters[pi] = nv;
            Clamp(ins);
            return true;
        }
        case Panel::Envelope: {
            int maxv; bool even; EnvRowRange(env_row, maxv, even);
            int step = even ? 2 * delta : delta;
            int nv = std::clamp(ins.envelope[env_col][env_row] + step, 0, maxv);
            if (even) nv &= 0x0E;
            if (nv == ins.envelope[env_col][env_row]) return false;
            ins.envelope[env_col][env_row] = nv;
            return true;
        }
        case Panel::NoteTable: {
            int nv = std::clamp(ins.noteTable[tbl_idx] + delta, 0, 255);
            if (nv == ins.noteTable[tbl_idx]) return false;
            ins.noteTable[tbl_idx] = nv;
            return true;
        }
        case Panel::Name:
            return false;
    }
    return false;
}

int Editor::NameLength(const TInstrument& ins)
{
    int n = INSTRUMENT_NAME_MAX_LEN;
    while (n > 0 && (ins.name[n - 1] == ' ' || ins.name[n - 1] == '\0')) --n;
    return n;
}

bool Editor::ToggleBinary(TInstrument& ins)
{
    ResetTyping();
    switch (panel) {
        case Panel::Params: {
            int pi, maxv; bool flag;
            ParamCell(param_idx, pi, maxv, flag);
            if (maxv != 1) return false;
            ins.parameters[pi] = ins.parameters[pi] ? 0 : 1;
            return true;
        }
        case Panel::Envelope: {
            int maxv; bool even; EnvRowRange(env_row, maxv, even);
            if (maxv != 1) return false;
            int& v = ins.envelope[env_col][env_row];
            v = v ? 0 : 1;
            return true;
        }
        default:
            return false;
    }
}

bool Editor::InsertChar(char c, TInstrument& ins)
{
    if (panel != Panel::Name) return false;
    if (c < 32 || c > 126) return false;
    int len = NameLength(ins);
    if (len >= INSTRUMENT_NAME_MAX_LEN) return false; // buffer full
    name_pos = std::clamp(name_pos, 0, len);

    // Shift the tail right by one, then insert.
    for (int i = len; i > name_pos; --i) ins.name[i] = ins.name[i - 1];
    ins.name[name_pos] = c;
    ++name_pos;

    // Keep the rest space-padded and the buffer terminated.
    for (int i = len + 1; i < INSTRUMENT_NAME_MAX_LEN; ++i) ins.name[i] = ' ';
    ins.name[INSTRUMENT_NAME_MAX_LEN] = '\0';
    return true;
}

bool Editor::Backspace(TInstrument& ins)
{
    if (panel != Panel::Name) return false;
    int len = NameLength(ins);
    name_pos = std::clamp(name_pos, 0, len);
    if (name_pos == 0) return false;

    // Remove the char before the cursor; shift the tail left.
    for (int i = name_pos - 1; i < len - 1; ++i) ins.name[i] = ins.name[i + 1];
    ins.name[len - 1] = ' ';
    --name_pos;
    ins.name[INSTRUMENT_NAME_MAX_LEN] = '\0';
    return true;
}

bool Editor::DeleteForward(TInstrument& ins)
{
    if (panel != Panel::Name) return false;
    int len = NameLength(ins);
    name_pos = std::clamp(name_pos, 0, len);
    if (name_pos >= len) return false; // nothing under/after the cursor

    for (int i = name_pos; i < len - 1; ++i) ins.name[i] = ins.name[i + 1];
    ins.name[len - 1] = ' ';
    ins.name[INSTRUMENT_NAME_MAX_LEN] = '\0';
    return true;
}

Editor::FieldInfo Editor::Describe(const TInstrument& ins) const
{
    FieldInfo fi;
    char buf[64];

    switch (panel) {
        case Panel::Params: {
            int idx = std::clamp(param_idx, 0, kNumParams - 1);
            const ParamDesc& p = kParams[idx];
            fi.panel = "PARAMETERS";
            fi.field = p.label;
            fi.value = ins.parameters[p.par_index];
            fi.vmin  = 0;
            fi.vmax  = p.max_value;
            fi.help  = p.help;
            break;
        }
        case Panel::Envelope: {
            int row = std::clamp(env_row, 0, ENVROWS - 1);
            int envLen = ins.parameters[PAR_ENV_LENGTH];
            int col = std::clamp(env_col, 0, std::max(0, envLen));
            int maxv; bool even; EnvRowRange(row, maxv, even);
            std::snprintf(buf, sizeof(buf), "%s  col %d / %d",
                          kEnvRows[row].label, col, envLen);
            fi.panel = "ENVELOPE";
            fi.field = buf;
            fi.value = ins.envelope[col][row];
            fi.vmin  = 0;
            fi.vmax  = maxv;
            fi.help  = kEnvRows[row].help;
            break;
        }
        case Panel::NoteTable: {
            int tblLen = ins.parameters[PAR_TBL_LENGTH];
            int i = std::clamp(tbl_idx, 0, std::max(0, tblLen));
            std::snprintf(buf, sizeof(buf), "Step %d / %d", i, tblLen);
            fi.panel = "NOTE TABLE";
            fi.field = buf;
            fi.value = ins.noteTable[i];
            fi.vmin  = 0;
            fi.vmax  = 255;
            fi.help  = "Note or frequency at this table step (see Tbl Type)";
            break;
        }
        case Panel::Name: {
            fi.panel = "NAME";
            fi.field = "Instrument name";
            fi.value = -1;
            fi.help  = "Type letters/digits to rename; Backspace deletes";
            break;
        }
    }
    return fi;
}
