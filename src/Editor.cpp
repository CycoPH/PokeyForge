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

    // Determine how many nibbles a field with this max value needs.
    // maxv <= 15  → 1 nibble (single digit, 0-9/A-F constrained to range)
    // maxv <= 255 → 2 nibbles (high then low; first digit commits high nibble)
    auto field_nibbles = [](int maxv) { return (maxv <= 15) ? 1 : 2; };

    // Apply one nibble of input to a field.
    // single-nibble: digit must be <= maxv or the input is rejected (returns -1).
    // two-nibble:    first call stores high nibble (partial value); second call
    //                fills low nibble and triggers auto-advance (via advance_out).
    // Returns the new value, or -1 if the digit was rejected.
    auto apply = [&](int cur, int maxv, bool& advance_out) -> int {
        if (field_nibbles(maxv) == 1) {
            if (nibble > maxv) return -1;       // e.g. '5' typed into a 0-3 field
            m_typing_fresh = true;              // reset immediately; field is done
            advance_out = true;
            return nibble;
        } else {
            // Two-nibble hex editor style.
            if (m_typing_fresh) {
                // First nibble: write to the high position, leave low as 0.
                m_typing_fresh = false;
                advance_out = false;
                return std::clamp(nibble << 4, 0, maxv);
            } else {
                // Second nibble: keep the high nibble already set, fill the low.
                m_typing_fresh = true;
                advance_out = true;
                return std::clamp((cur & 0xF0) | nibble, 0, maxv);
            }
        }
    };

    switch (panel) {
        case Panel::Params: {
            int pi, maxv; bool flag;
            ParamCell(param_idx, pi, maxv, flag);
            bool adv = false;
            int nv = apply(ins.parameters[pi], maxv, adv);
            if (nv < 0) return false;           // rejected digit
            bool changed = (nv != ins.parameters[pi]);
            ins.parameters[pi] = nv;
            Clamp(ins);
            if (adv) Move(1, 0, ins);
            return changed || adv;              // adv = state advanced even if value same
        }
        case Panel::Envelope: {
            int maxv; bool even; EnvRowRange(env_row, maxv, even);
            bool adv = false;
            int nv = apply(ins.envelope[env_col][env_row], maxv, adv);
            if (nv < 0) return false;
            if (even) nv &= 0x0E;
            bool changed = (nv != ins.envelope[env_col][env_row]);
            ins.envelope[env_col][env_row] = nv;
            if (adv) Move(1, 0, ins);
            return changed || adv;
        }
        case Panel::NoteTable: {
            bool adv = false;
            int cur = ins.noteTable[tbl_idx] & 0xFF;
            int nv = apply(cur, 255, adv);
            if (nv < 0) return false;
            bool changed = (nv != cur);
            ins.noteTable[tbl_idx] = nv;
            if (adv) Move(1, 0, ins);
            return changed || adv;
        }
        case Panel::Name:
            return false;   // name uses character input
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
            // Populate option pills for small-range (maxv<=3) named params.
            if (idx < 12) {
                // PAR_TBL_TYPE (idx 3, maxv 1)
                if (p.par_index == PAR_TBL_TYPE) {
                    static const char* kOpts[] = { "Notes", "Freqs", nullptr };
                    fi.options = kOpts;
                }
                // PAR_TBL_MODE (idx 4, maxv 1)
                else if (p.par_index == PAR_TBL_MODE) {
                    static const char* kOpts[] = { "Set", "Add", nullptr };
                    fi.options = kOpts;
                }
                // PAR_VIBRATO (idx 10, maxv 3)
                else if (p.par_index == PAR_VIBRATO && p.max_value == 3) {
                    static const char* kOpts[] = { "Off", "Low", "Mid", "Deep", nullptr };
                    fi.options = kOpts;
                }
            }
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
            // For Command/Distortion rows show the description of the current
            // cell value rather than the generic row help.
            if (row == COMMAND) {
                static const char* kCmdHelp[8] = {
                    "Play BASE_NOTE + $XY semitones.",
                    "Play frequency $XY.",
                    "Play BASE_NOTE + frequency $XY.",
                    "Set BASE_NOTE += $XY semitones. Play BASE_NOTE.",
                    "Set FSHIFT += frequency $XY. Play BASE_NOTE.",
                    "Set portamento speed $X, step $Y. Play BASE_NOTE.",
                    "Set FILTER_SHFRQ += $XY. $0Y = BASS16 Distortion. $FF/$01 = Sawtooth inversion (Dist A).",
                    "Set instrument AUDCTL. $FF = VOLUME ONLY mode. $FE/$FD = enable/disable Two-Tone Filter."
                };
                fi.help = kCmdHelp[std::clamp(fi.value, 0, 7)];
            } else if (row == DISTORTION) {
                // Indexed by (value >> 1) — distortion values are always even 0..E.
                static const char* kDistHelp[8] = {
                    "Dist 0: white noise. (AUDC $0v, Poly5+17/9)",
                    "Dist 2: square-ish tones. (AUDC $2v, Poly5)",
                    "Dist 4: no note table yet, Pure Table by default. (AUDC $4v, Poly4+5)",
                    "Dist 6: 16-Bit tones in valid channels; use Cmd 6 to set distortion. (Dist A by default)",
                    "Dist 8: white noise. (AUDC $8v, Poly17/9)",
                    "Dist A: pure tones. CH1+CH3 1.79MHz + AUTOFILTER = Sawtooth. (AUDC $Av)",
                    "Dist C: buzzy bass tones. (AUDC $Cv, Poly4)",
                    "Dist E: gritty bass tones. (AUDC $Cv, Poly4)"
                };
                fi.help = kDistHelp[(std::clamp(fi.value & 0xFE, 0, 14)) >> 1];
            } else {
                fi.help = kEnvRows[row].help;
            }
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
