#pragma once

#include "InstrumentTypes.h"

#include <string>

// In-place instrument editor cursor + value logic. Operates on a working
// TInstrument copy; the owner re-encodes to an ATA blob after any change.
//
// Browse mode leaves the editor inactive. In Edit mode a cell cursor lives in
// one of four panels; Tab cycles panels, arrows move the cursor, hex digits
// type values (two-digit fields compose), and Up/Down nudge by +/-1.

class Editor {
public:
    enum class Panel { Params, Envelope, NoteTable, Name };

    bool  active = false;
    Panel panel  = Panel::Params;

    // Cursor positions per panel.
    int param_idx = 0;   // index into the editable-parameter list (see .cpp)
    int env_col   = 0;   // 0..PAR_ENV_LENGTH
    int env_row   = 0;   // 0..ENVROWS-1
    int tbl_idx   = 0;   // 0..PAR_TBL_LENGTH
    int name_pos  = 0;   // 0..INSTRUMENT_NAME_MAX_LEN-1

    void Toggle();
    void SetActive(bool on);

    void NextPanel(int dir);                 // Tab / Shift+Tab
    void Move(int dx, int dy, const TInstrument& ins);

    // Returns true if the instrument changed (caller must re-encode).
    bool InputHex(int nibble, TInstrument& ins);
    bool Increment(int delta, TInstrument& ins);

    // Toggle the cell under the cursor if it is a binary (0/1) field
    // (AUDCTL flags, table type/mode, envelope filter/portamento). Returns
    // true if it changed. Used by right-click.
    bool ToggleBinary(TInstrument& ins);

    // Name editing (Name panel only). The name behaves like a text field:
    // a variable-length string (the buffer is space-padded to the fixed
    // width), name_pos is an insertion point in [0, length].
    bool InsertChar(char c, TInstrument& ins);
    bool Backspace(TInstrument& ins);     // delete char before the cursor
    bool DeleteForward(TInstrument& ins); // delete char at the cursor

    static int NameLength(const TInstrument& ins);

    // Human-readable description of the cell the cursor is on, for the
    // on-screen edit readout.
    struct FieldInfo {
        const char* panel = "";   // panel name
        std::string field;        // field label, e.g. "Tbl Len" or "VolL col 3/6"
        int  value = -1;          // current value (-1 = not numeric, e.g. Name)
        int  vmin = 0, vmax = 0;  // valid range
        const char* help = "";    // what the field does
    };
    FieldInfo Describe(const TInstrument& ins) const;

    // How many editable parameter cells exist (named params + AUDCTL flags).
    static int ParamCellCount();

    // Describe the parameter cell at `idx`: the PAR_* index and its max value.
    static void ParamCell(int idx, int& par_index, int& max_value, bool& is_flag);

    // Clamp all cursors to the instrument's current dimensions.
    void Clamp(const TInstrument& ins);

private:
    bool m_typing_fresh = true; // next hex digit replaces rather than shifts
    void ResetTyping() { m_typing_fresh = true; }
};
