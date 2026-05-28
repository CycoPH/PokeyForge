#include "Gui.h"
#include "Version.h"

#include <algorithm>
#include <cstdio>

namespace {

// 1280x720 layout. All measurements in pixels.
constexpr int kWinW = 1280;
constexpr int kWinH = 720;
constexpr int kHeaderH = 32;
constexpr int kCmdBarH = 22;
constexpr int kSearchH = 24;   // search bar above the command bar (main column)
constexpr int kTreeW = 320;
constexpr int kMainX = kTreeW;
constexpr int kMainW = kWinW - kTreeW;

// Bank panel geometry, shared by the renderer and the mouse hit-test so they
// never drift apart.
struct BankGeom { int panel_x, panel_y, panel_w, panel_h; int gx, gy, cw, ch, pad; };

BankGeom ComputeBankGeom()
{
    int x0 = kMainX + 8;
    int y0 = kHeaderH + 436;
    int w  = kMainW - 16;
    int h  = (kWinH - kCmdBarH - 4) - y0;
    int gx = x0 + 12;
    int gy = y0 + 28;
    int cw = (w - 24) / 8;
    int ch = (h - 36) / 8;
    return BankGeom{ x0, y0, w, h, gx, gy, cw, ch, 3 };
}

// Colours (RGB).
struct Col { Uint8 r, g, b, a = 255; };
constexpr Col kBg          { 16, 18, 24 };
constexpr Col kPanel       { 28, 32, 40 };
constexpr Col kPanelDark   { 22, 26, 32 };
constexpr Col kAccent      { 80, 140, 220 };
constexpr Col kText        { 220, 222, 230 };
constexpr Col kTextDim     { 130, 134, 144 };
constexpr Col kHighlight   { 255, 220, 90 };
constexpr Col kBankFilled  { 60, 180, 90 };
constexpr Col kBankEmpty   { 50, 54, 64 };
constexpr Col kBankCurrent { 220, 90, 80 };
constexpr Col kFolder      { 130, 200, 240 };
constexpr Col kBorder      { 70, 76, 88 };
constexpr Col kEnvCell     { 60, 120, 200 };
constexpr Col kOrange      { 235, 150, 40 };   // modified / unsaved
constexpr Col kEditCursor  { 255, 255, 255 };  // focused-panel frame
constexpr Col kCellCursor  { 235, 40, 40 };    // focused edit cell (red)

void SetCol(SDL_Renderer* r, Col c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

void FillRect(SDL_Renderer* r, Col c, int x, int y, int w, int h)
{
    SetCol(r, c);
    SDL_FRect rect{ (float)x, (float)y, (float)w, (float)h };
    SDL_RenderFillRect(r, &rect);
}

void OutlineRect(SDL_Renderer* r, Col c, int x, int y, int w, int h)
{
    SetCol(r, c);
    SDL_FRect rect{ (float)x, (float)y, (float)w, (float)h };
    SDL_RenderRect(r, &rect);
}

// A bold (3px) cell cursor: a translucent red wash plus a solid red double
// border, so the focused edit cell is impossible to miss.
void CellCursor(SDL_Renderer* r, int x, int y, int w, int h)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SetCol(r, Col{ kCellCursor.r, kCellCursor.g, kCellCursor.b, 90 });
    SDL_FRect wash{ (float)x, (float)y, (float)w, (float)h };
    SDL_RenderFillRect(r, &wash);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    SetCol(r, kCellCursor);
    for (int i = 0; i < 2; ++i) {
        SDL_FRect rect{ (float)(x - i), (float)(y - i),
                        (float)(w + 2 * i), (float)(h + 2 * i) };
        SDL_RenderRect(r, &rect);
    }
}

void DrawText(SDL_Renderer* r, Col c, int x, int y, const char* s)
{
    SetCol(r, c);
    SDL_RenderDebugText(r, (float)x, (float)y, s);
}

// SDL_RenderDebugTextFormat sometimes truncates; build the string ourselves
// to keep the call surface tiny.
template <typename... Args>
void DrawTextF(SDL_Renderer* r, Col c, int x, int y, const char* fmt, Args... args)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), fmt, args...);
    DrawText(r, c, x, y, buf);
}

// Section title with an underline.
void DrawSectionTitle(SDL_Renderer* r, int x, int y, int w, const char* label)
{
    DrawText(r, kHighlight, x, y, label);
    SetCol(r, kBorder);
    SDL_RenderLine(r, (float)x, (float)(y + 12), (float)(x + w), (float)(y + 12));
}

// Debug font is a fixed 8px per glyph.
constexpr int kGlyphW = 8;

bool EditingPanel(const GuiState& s, Editor::Panel p)
{
    return s.editor && s.editor->active && s.editor->panel == p;
}

// Draw a bright double frame + "[EDITING]" title when this panel holds the
// edit cursor, so it's obvious which section the keys affect.
void FocusFrame(SDL_Renderer* r, const GuiState& s, Editor::Panel p,
                int x, int y, int w, int h)
{
    if (!EditingPanel(s, p)) return;
    SetCol(r, kEditCursor);
    SDL_FRect a{ (float)x, (float)y, (float)w, (float)h };
    SDL_RenderRect(r, &a);
    SDL_FRect b{ (float)(x + 1), (float)(y + 1), (float)(w - 2), (float)(h - 2) };
    SDL_RenderRect(r, &b);
}

// Truncate `s` so it fits in `max_chars`, appending '~' when cut.
std::string Truncate(const std::string& s, int max_chars)
{
    if (max_chars <= 0) return std::string{};
    if ((int)s.size() <= max_chars) return s;
    if (max_chars == 1) return "~";
    return s.substr(0, (size_t)max_chars - 1) + "~";
}

// Top menu-bar buttons. Shared by the renderer and the click hit-test so the
// drawn boxes and clickable regions never drift apart.
struct MenuBtn { const char* label; Gui::MenuAction action; int x, w; };
constexpr int kMenuY = 4;
constexpr int kMenuH = 24;
const MenuBtn kMenu[] = {
    { "Save (F2)",    Gui::MenuAction::Save,      6,  84 },
    { "Load (F3)",    Gui::MenuAction::Load,     92,  84 },
    { "Library (F4)", Gui::MenuAction::Library, 178, 106 },
    { "Analyse (F7)", Gui::MenuAction::Analyse, 286, 106 },
    { "About",        Gui::MenuAction::About,   394,  54 },
    { "Help (F1)",    Gui::MenuAction::Help,    450,  82 },
};
constexpr int kMenuCount = (int)(sizeof(kMenu) / sizeof(kMenu[0]));

// Directory-pane view tabs (left pane). y is relative to kHeaderH.
struct DirTabBtn { const char* label; Gui::DirTab tab; int x, w; };
constexpr int kDirTabRelY = 4;
constexpr int kDirTabH = 18;
const DirTabBtn kDirTabs[] = {
    { "Folders",  Gui::DirTab::Folders,     6, 66 },
    { "Category", Gui::DirTab::Category,   76, 76 },
    { "All",      Gui::DirTab::ShowAll,   158, 44 },
    { "No dupes", Gui::DirTab::HideDupes, 208, 78 },
};

} // anonymous namespace

Gui::MenuAction Gui::MenuAtLogical(int x, int y) const
{
    if (y < kMenuY || y >= kMenuY + kMenuH) return MenuAction::None;
    for (const auto& b : kMenu) {
        if (x >= b.x && x < b.x + b.w) return b.action;
    }
    return MenuAction::None;
}

Gui::DirTab Gui::DirTabAtLogical(int x, int y) const
{
    int ty = kHeaderH + kDirTabRelY;
    if (y < ty || y >= ty + kDirTabH) return DirTab::None;
    for (const auto& t : kDirTabs) {
        if (x >= t.x && x < t.x + t.w) return t.tab;
    }
    return DirTab::None;
}

int Gui::TreeRowAtLogical(int x, int y) const
{
    if (x < 0 || x >= kTreeW) return -1;
    if (y < m_tree_top || m_tree_rowh <= 0) return -1;
    int vis = (y - m_tree_top) / m_tree_rowh;
    int idx = m_tree_scroll + vis;
    if (idx < 0 || idx >= m_tree_row_count) return -1;
    return idx;
}

void Gui::Render(SDL_Renderer* r, const GuiState& s)
{
    FillRect(r, kBg, 0, 0, kWinW, kWinH);
    DrawHeader(r, s);
    DrawTree(r, s);
    DrawInstrumentHeader(r, s);
    DrawParameters(r, s);
    DrawEnvelope(r, s);
    DrawNoteTable(r, s);
    DrawBank(r, s);
    DrawSearchBar(r, s);
    if (s.editor && s.editor->active) DrawEditBar(r, s);
    DrawCommandBar(r, s);
    if (s.show_help)    DrawHelpOverlay(r, s);
    if (s.show_prompt)  DrawSavePrompt(r, s);
    if (s.show_confirm) DrawConfirm(r, s);
    if (s.show_about)   DrawAbout(r, s);
}

void Gui::DrawAbout(SDL_Renderer* r, const GuiState& /*s*/)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    FillRect(r, Col{ 0, 0, 0, 190 }, 0, 0, kWinW, kWinH);

    int pw = 600, ph = 320;
    int px = (kWinW - pw) / 2;
    int py = (kWinH - ph) / 2;
    FillRect(r, Col{ 30, 34, 44, 245 }, px, py, pw, ph);
    OutlineRect(r, kAccent, px, py, pw, ph);
    OutlineRect(r, kAccent, px + 1, py + 1, pw - 2, ph - 2);

    // Big title.
    SDL_SetRenderScale(r, 4.0f, 4.0f);
    SetCol(r, kHighlight);
    SDL_RenderDebugText(r, (px + 30) / 4.0f, (py + 26) / 4.0f, "PokeyForge");
    SDL_SetRenderScale(r, 1.0f, 1.0f);

    int tx = px + 30;
    DrawText(r, kAccent, tx, py + 64, "Version " POKEYFORGE_VERSION);
    int y = py + 90;
    DrawText(r, kText,    tx, y,      "RMT .RTI instrument auditioner, editor & bank builder");
    DrawText(r, kOrange,  tx, y + 22, "written by RetroCoder");

    DrawText(r, kHighlight, tx, y + 56, "Credits");
    DrawText(r, kTextDim, tx, y + 74,
             "Audio core: Raster Music Tracker (Raster / R. Sterba, VinsCool)");
    DrawText(r, kTextDim, tx, y + 90,
             "POKEY & 6502 emulation: Altirra (Avery Lee) - sa_pokey / sa_c6502");
    DrawText(r, kTextDim, tx, y + 106,
             "Windowing & audio: SDL3");

    DrawText(r, kTextDim, tx, y + 140,
             "Instruments play through the real RMT tracker driver on an");
    DrawText(r, kTextDim, tx, y + 156,
             "emulated 6502 + POKEY, so they sound exactly as in RMT.");

    DrawText(r, kHighlight, tx, py + ph - 26, "Press any key or click to close.");

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

namespace {
// Shared geometry for the Yes/No confirm dialog and its clickable buttons.
constexpr int kConfirmW = 620, kConfirmH = 150, kConfirmBtnH = 26;
struct ConfirmBtn { char key; const char* label; int dx; int w; };
const ConfirmBtn kConfirmBtns[2] = {
    { 'y', "[Y] Yes", 20,  130 },
    { 'n', "[N] No",  170, 130 },
};
void ConfirmRect(int& px, int& py) {
    px = (kWinW - kConfirmW) / 2;
    py = (kWinH - kConfirmH) / 2;
}
} // anonymous namespace

void Gui::DrawConfirm(SDL_Renderer* r, const GuiState& s)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    FillRect(r, Col{ 0, 0, 0, 180 }, 0, 0, kWinW, kWinH);

    int px, py;
    ConfirmRect(px, py);
    FillRect(r, Col{ 30, 34, 44, 245 }, px, py, kConfirmW, kConfirmH);
    OutlineRect(r, kOrange, px, py, kConfirmW, kConfirmH);

    DrawText(r, kOrange, px + 20, py + 18, "Confirm");
    // Wrap the message across two lines if long.
    std::string msg = s.confirm_msg;
    int max_chars = (kConfirmW - 40) / kGlyphW;
    if ((int)msg.size() <= max_chars) {
        DrawText(r, kText, px + 20, py + 46, msg.c_str());
    } else {
        int cut = max_chars;
        while (cut > 0 && msg[cut] != ' ') --cut;
        if (cut == 0) cut = max_chars;
        DrawText(r, kText, px + 20, py + 44, msg.substr(0, cut).c_str());
        DrawText(r, kText, px + 20, py + 58, msg.substr(cut + 1).c_str());
    }

    int by = py + kConfirmH - kConfirmBtnH - 14;
    for (const auto& b : kConfirmBtns) {
        int bx = px + b.dx;
        FillRect(r, kPanelDark, bx, by, b.w, kConfirmBtnH);
        OutlineRect(r, kAccent, bx, by, b.w, kConfirmBtnH);
        int tw = (int)std::char_traits<char>::length(b.label) * kGlyphW;
        DrawText(r, kHighlight, bx + (b.w - tw) / 2, by + 9, b.label);
    }
    DrawText(r, kTextDim, px + 320, by + 9, "(Enter = Yes, Esc = No)");

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

char Gui::ConfirmButtonAt(int x, int y) const
{
    int px, py;
    ConfirmRect(px, py);
    int by = py + kConfirmH - kConfirmBtnH - 14;
    if (y < by || y >= by + kConfirmBtnH) return 0;
    for (const auto& b : kConfirmBtns) {
        int bx = px + b.dx;
        if (x >= bx && x < bx + b.w) return b.key;
    }
    return 0;
}

void Gui::DrawMasterScope(SDL_Renderer* r, const GuiState& s)
{
    // Compact oscilloscope of the real mixed output, in the header's
    // top-right corner.
    int w  = 274;
    int h  = kHeaderH - 6;
    int x  = kWinW - w - 6;
    int y  = 3;

    FillRect(r, kBg, x, y, w, h);
    OutlineRect(r, kBorder, x, y, w, h);

    int mid = y + h / 2;

    // Faint centre line so the scope reads as a scope even when silent.
    SetCol(r, kBorder);
    SDL_RenderLine(r, (float)(x + 2), (float)mid, (float)(x + w - 2), (float)mid);

    // Show channel 1 only (POKEY voice 0, where the audition note plays),
    // synthesized from its live registers like the old per-channel scope.
    int audf = s.pokey[0];
    int audc = s.pokey[1];
    int vol  = audc & 0x0F;
    bool tone = ((audc & 0xE0) == 0xA0) || ((audc & 0xE0) == 0xC0);

    if (vol == 0) return;   // silent -> centre line only

    int half  = h / 2 - 2;
    int amp   = half * vol / 15;
    int cyc   = std::clamp((256 - audf) / 14 + 1, 1, 22);
    int inner = w - 4;
    int prev_y = mid;
    SetCol(r, kAccent);
    for (int px = 0; px <= inner; ++px) {
        int val;
        if (tone) {
            double phase = (double)px / (double)inner * cyc;
            double frac  = phase - (double)(long)phase;
            val = (frac < 0.5) ? amp : -amp;
        } else {
            unsigned hsh = (unsigned)(px * 2654435761u) ^ (unsigned)(audf * 40503u);
            hsh ^= hsh >> 13;
            val = ((int)(hsh % 201) - 100) * amp / 100;
        }
        int cy = std::clamp(mid - val, y + 1, y + h - 2);
        if (px > 0)
            SDL_RenderLine(r, (float)(x + 2 + px - 1), (float)prev_y,
                           (float)(x + 2 + px), (float)cy);
        prev_y = cy;
    }
}

void Gui::DrawEditBar(SDL_Renderer* r, const GuiState& s)
{
    if (!s.editor || !s.instrument) return;

    // Docked band spanning the main column, just above the command bar.
    int h  = 56;
    int y  = kWinH - kCmdBarH - h;
    int x  = kMainX + 4;
    int w  = kWinW - x - 4;

    FillRect(r, Col{ 40, 36, 22 }, x, y, w, h);   // warm tint = editing
    OutlineRect(r, kOrange, x, y, w, h);

    Editor::FieldInfo fi = s.editor->Describe(*s.instrument);

    // Line 1: where the cursor is and the current value.
    DrawText(r, kHighlight, x + 10, y + 8, "EDITING");
    char line1[160];
    if (fi.value >= 0) {
        std::snprintf(line1, sizeof(line1), "%s  >  %s   =   %02X (%d)   range %d-%d",
                      fi.panel, fi.field.c_str(), fi.value & 0xFF, fi.value,
                      fi.vmin, fi.vmax);
    } else {
        std::snprintf(line1, sizeof(line1), "%s  >  %s", fi.panel, fi.field.c_str());
    }
    DrawText(r, kText, x + 90, y + 8, line1);

    // Line 2: what the field does.
    DrawText(r, kTextDim, x + 10, y + 24, fi.help);

    // Line 3: the controls.
    DrawText(r, kFolder, x + 10, y + 40,
             "Tab panel  arrows move  0-9 A-F set  +/- or Shift+Up/Dn nudge  "
             "right-click toggle  Ctrl+key play  Space replay  F6 exit");
}

namespace {
// Shared geometry for the unsaved-edits prompt and its clickable buttons.
constexpr int kPromptW = 520, kPromptH = 156, kPromptBtnH = 26;
struct PromptBtn { char key; const char* label; int dx; int w; };
const PromptBtn kPromptBtns[3] = {
    { 'k', "[K] Keep in bank", 20,  170 },
    { 'd', "[D] Discard",     200,  130 },
    { 'c', "[C] Cancel",      340,  130 },
};
void PromptRect(int& px, int& py) {
    px = (kWinW - kPromptW) / 2;
    py = (kWinH - kPromptH) / 2;
}
} // anonymous namespace

void Gui::DrawSavePrompt(SDL_Renderer* r, const GuiState& /*s*/)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    FillRect(r, Col{ 0, 0, 0, 180 }, 0, 0, kWinW, kWinH);

    int px, py;
    PromptRect(px, py);
    FillRect(r, Col{ 30, 34, 44, 245 }, px, py, kPromptW, kPromptH);
    OutlineRect(r, kOrange, px, py, kPromptW, kPromptH);

    DrawText(r, kOrange, px + 20, py + 18, "Unsaved instrument edits");
    DrawText(r, kText, px + 20, py + 46,
             "You have edited this instrument. Keep the change in the bank");
    DrawText(r, kText, px + 20, py + 60, "before switching?");

    int by = py + kPromptH - kPromptBtnH - 14;
    for (const auto& b : kPromptBtns) {
        int bx = px + b.dx;
        FillRect(r, kPanelDark, bx, by, b.w, kPromptBtnH);
        OutlineRect(r, kAccent, bx, by, b.w, kPromptBtnH);
        int tw = (int)std::char_traits<char>::length(b.label) * kGlyphW;
        DrawText(r, kHighlight, bx + (b.w - tw) / 2, by + 9, b.label);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

char Gui::SavePromptButtonAt(int x, int y) const
{
    int px, py;
    PromptRect(px, py);
    int by = py + kPromptH - kPromptBtnH - 14;
    if (y < by || y >= by + kPromptBtnH) return 0;
    for (const auto& b : kPromptBtns) {
        int bx = px + b.dx;
        if (x >= bx && x < bx + b.w) return b.key;
    }
    return 0;
}

void Gui::DrawSearchBar(SDL_Renderer* r, const GuiState& s)
{
    // Left column, below the directory tree (filters the instrument list).
    int x = 4;
    int y = kWinH - kCmdBarH - kSearchH;
    int w = kTreeW - 8;
    int h = kSearchH;

    FillRect(r, s.search_active ? Col{ 40, 46, 60 } : kPanelDark, x, y, w, h);
    OutlineRect(r, s.search_active ? kAccent : kBorder, x, y, w, h);

    DrawText(r, kTextDim, x + 6, y + 7, "Find:");
    int tx = x + 6 + 5 * kGlyphW + 4;
    if (s.search_active || !s.search_query.empty()) {
        std::string q = Truncate(s.search_query, (x + w - tx - kGlyphW) / kGlyphW);
        DrawText(r, kText, tx, y + 7, q.c_str());
        if (s.search_active) {
            int caret = tx + (int)q.size() * kGlyphW;
            FillRect(r, kCellCursor, caret, y + 5, kGlyphW, h - 10);
        }
    } else {
        DrawText(r, kTextDim, tx, y + 7, "/ to search");
    }
}

bool Gui::PointInSearchBar(int x, int y) const
{
    int by = kWinH - kCmdBarH - kSearchH;
    return x >= 4 && x < kTreeW - 4 && y >= by && y < by + kSearchH;
}

void Gui::DrawCommandBar(SDL_Renderer* r, const GuiState& s)
{
    int y = kWinH - kCmdBarH;
    FillRect(r, kPanel, 0, y, kWinW, kCmdBarH);
    SetCol(r, kBorder);
    SDL_RenderLine(r, 0, (float)y, (float)kWinW, (float)y);

    // A transient notice (e.g. "Saved drums.rmt + 12 .rti") takes priority;
    // otherwise show the command hints and current library path.
    if (!s.notice.empty()) {
        DrawText(r, kHighlight, 8, y + 7, s.notice.c_str());
        return;
    }

    DrawText(r, kTextDim, 8, y + 7,
             "F2 Save  F3 Load  F4 Lib  F7 Analyse  F8 Group  F9 Dupes");

    if (!s.library_path.empty()) {
        std::string lib = "Library: " + s.library_path;
        int max_chars = (kWinW - 470) / kGlyphW;
        // Show the tail of long paths (more informative than the head).
        if ((int)lib.size() > max_chars && max_chars > 4) {
            lib = "Library: ~" + s.library_path.substr(s.library_path.size() -
                                                       (size_t)(max_chars - 10));
        }
        DrawText(r, kTextDim, 470, y + 7, lib.c_str());
    }
}

void Gui::DrawHeader(SDL_Renderer* r, const GuiState& s)
{
    FillRect(r, kPanel, 0, 0, kWinW, kHeaderH);

    // Clickable menu bar.
    for (const auto& b : kMenu) {
        FillRect(r, kPanelDark, b.x, kMenuY, b.w, kMenuH);
        OutlineRect(r, kBorder, b.x, kMenuY, b.w, kMenuH);
        DrawText(r, kHighlight, b.x + 8, kMenuY + 8, b.label);
    }

    // Status on the right (kept left of the scope in the top-right corner).
    int used = s.bank ? s.bank->UsedCount() : 0;
    DrawTextF(r, kText, 548, 12, "Clock: %s", s.ntsc ? "NTSC 60Hz" : "PAL 50Hz");
    DrawTextF(r, kText, 690, 12, "Oct: %+d", s.octave_shift);
    DrawTextF(r, kText, 760, 12, "Bank: %02d/64", used);

    bool editing = s.editor && s.editor->active;
    if (editing)    DrawText(r, kHighlight, 858, 12, "[EDIT]");
    if (s.modified) DrawText(r, kOrange,    916, 12, "MODIFIED");

    // Master oscilloscope in the top-right corner.
    DrawMasterScope(r, s);
}

void Gui::DrawTree(SDL_Renderer* r, const GuiState& s)
{
    FillRect(r, kPanelDark, 0, kHeaderH, kTreeW, kWinH - kHeaderH);
    OutlineRect(r, kBorder, 0, kHeaderH, kTreeW, kWinH - kHeaderH);

    // View tabs (click to switch; equivalent to F8 / F9).
    if (s.dir) {
        bool category = s.dir->GetViewMode() == Directory::ViewMode::Category;
        bool hide = s.dir->HideDuplicates();
        int ty = kHeaderH + kDirTabRelY;
        for (const auto& t : kDirTabs) {
            bool active = false;
            switch (t.tab) {
                case DirTab::Folders:   active = !category; break;
                case DirTab::Category:  active = category;  break;
                case DirTab::ShowAll:   active = !hide;     break;
                case DirTab::HideDupes: active = hide;      break;
                default: break;
            }
            FillRect(r, active ? kAccent : kPanelDark, t.x, ty, t.w, kDirTabH);
            OutlineRect(r, kBorder, t.x, ty, t.w, kDirTabH);
            DrawText(r, active ? kBg : kTextDim, t.x + 4, ty + 5, t.label);
        }
    }
    SetCol(r, kBorder);
    SDL_RenderLine(r, 4, (float)(kHeaderH + kDirTabRelY + kDirTabH + 2),
                   (float)(kTreeW - 4), (float)(kHeaderH + kDirTabRelY + kDirTabH + 2));

    if (!s.dir) return;
    const auto& rows = s.dir->Rows();
    int cur_node = s.dir->CurrentNodeIndex();

    constexpr int kRowH = 16;
    int top     = kHeaderH + 26;
    // Reserve the bottom for the footer line, the search bar and command bar.
    int bottom  = kWinH - kCmdBarH - kSearchH - 16;
    int visible_rows = (bottom - top) / kRowH;
    if (visible_rows < 1) return;

    // Find the current file's row.
    int cur_pos = -1;
    for (int i = 0; i < (int)rows.size(); ++i) {
        if (!rows[i].is_header && rows[i].node == cur_node) { cur_pos = i; break; }
    }
    // Stable scroll: m_tree_scroll persists between frames and only moves when
    // the current selection would fall off-screen. This keeps the row you
    // click under the cursor (the list no longer jumps on selection).
    int total = (int)rows.size();
    int max_scroll = std::max(0, total - visible_rows);
    if (cur_pos >= 0) {
        if (cur_pos < m_tree_scroll)
            m_tree_scroll = cur_pos;
        else if (cur_pos >= m_tree_scroll + visible_rows)
            m_tree_scroll = cur_pos - visible_rows + 1;
    }
    m_tree_scroll = std::clamp(m_tree_scroll, 0, max_scroll);
    int scroll = m_tree_scroll;
    int end = std::min<int>(scroll + visible_rows, total);

    // Remember geometry so mouse clicks can be mapped back to a row.
    m_tree_top = top; m_tree_rowh = kRowH; m_tree_row_count = total;

    for (int i = scroll; i < end; ++i) {
        const auto& row = rows[i];
        int row_y = top + (i - scroll) * kRowH;
        int indent = row.depth * 12 + 8;
        int avail_chars = (kTreeW - indent - 6) / kGlyphW;

        if (row.is_header) {
            const char* glyph = row.collapsed ? "> " : "v ";
            std::string lbl = std::string(glyph) +
                Truncate(row.label, std::max(0, (kTreeW - 22) / kGlyphW));
            DrawText(r, kHighlight, 6, row_y, lbl.c_str());
            continue;
        }

        const auto& n = s.dir->At(row.node);
        if (row.node == cur_node) {
            FillRect(r, kAccent, 4, row_y - 2, kTreeW - 8, kRowH);
        }

        if (n.type == Directory::NodeType::Folder) {
            const char* glyph = n.expanded ? "v " : "> ";
            std::string name = Truncate(n.name, std::max(0, avail_chars - 3));
            DrawTextF(r, kFolder, indent, row_y, "%s%s/", glyph, name.c_str());
        } else {
            Col c = (row.node == cur_node) ? kBg : kText;
            char dot = ' ';
            if (s.bank && s.bank->IndexOfPath(n.path) >= 0) dot = 'B';
            std::string name = Truncate(n.name, std::max(0, avail_chars - 2));
            DrawTextF(r, c, indent, row_y, "%c %s", dot, name.c_str());
        }
    }

    // Footer: position info + filter state, just above the search bar.
    if (s.dir->CurrentFileIndex() >= 0) {
        DrawTextF(r, kTextDim, 8, kWinH - kCmdBarH - kSearchH - 14, "%d / %d shown%s",
                  s.dir->CurrentFileIndex() + 1, s.dir->NavCount(),
                  s.dir->HideDuplicates() ? "  (dupes hidden)" : "");
    }
}

void Gui::DrawInstrumentHeader(SDL_Renderer* r, const GuiState& s)
{
    int y = kHeaderH + 4;
    FillRect(r, kPanel, kMainX + 4, y, kMainW - 8, 28);
    FocusFrame(r, s, Editor::Panel::Name, kMainX + 4, y, kMainW - 8, 28);

    bool have_rti   = s.rti && s.rti->Valid();
    bool have_instr = s.instrument != nullptr;
    if (!have_rti && !have_instr) {
        DrawText(r, kTextDim, kMainX + 16, y + 10, "No instrument loaded.");
        return;
    }

    // Display name from the working instrument so live name edits show up.
    // Fall back to the .RTI name only when no decoded instrument exists.
    std::string name;
    if (have_instr) {
        name.assign(s.instrument->name, INSTRUMENT_NAME_MAX_LEN);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\0')) name.pop_back();
    } else if (have_rti) {
        name = s.rti->Name();
    }

    DrawText(r, kHighlight, kMainX + 16, y + 10, "Instrument:");

    int name_x = kMainX + 120;
    DrawTextF(r, kText, name_x, y + 10, "\"%s\"", name.c_str());

    // Name-edit caret.
    if (EditingPanel(s, Editor::Panel::Name)) {
        int caret = name_x + (1 + s.editor->name_pos) * kGlyphW; // +1 for the quote
        FillRect(r, kCellCursor, caret, y + 9, kGlyphW, 12);
        if (s.editor->name_pos < (int)name.size()) {
            char ch[2] = { name[s.editor->name_pos], 0 };
            DrawText(r, kBg, caret, y + 10, ch);
        }
    }

    int info_x = name_x + (int)(name.size() + 4) * kGlyphW;
    if (have_rti) {
        std::string file = s.rti->Path();
        auto slash = file.find_last_of("/\\");
        if (slash != std::string::npos) file = file.substr(slash + 1);
        DrawTextF(r, kTextDim, info_x, y + 10, "v%d  (%zu ATA)  file: %s",
                  s.rti->Version(), s.rti->AtaBlob().size(), file.c_str());
    } else if (s.current_bank_slot >= 0) {
        DrawTextF(r, kTextDim, info_x, y + 10, "(bank slot %02d)", s.current_bank_slot);
    }
}

namespace {

struct ParamLabel { int idx; const char* name; };
constexpr ParamLabel kParamLabels[] = {
    { PAR_TBL_LENGTH,  "Tbl Len " }, { PAR_TBL_GOTO,    "Tbl Goto" },
    { PAR_TBL_SPEED,   "Tbl Spd " }, { PAR_TBL_TYPE,    "Tbl Typ " },
    { PAR_TBL_MODE,    "Tbl Mode" }, { PAR_ENV_LENGTH,  "Env Len " },
    { PAR_ENV_GOTO,    "Env Goto" }, { PAR_VOL_FADEOUT, "Vol Fade" },
    { PAR_VOL_MIN,     "Vol Min " }, { PAR_DELAY,       "Delay   " },
    { PAR_VIBRATO,     "Vibrato " }, { PAR_FREQ_SHIFT,  "FrqShift" },
};

} // anonymous namespace

void Gui::DrawParameters(SDL_Renderer* r, const GuiState& s)
{
    int x0 = kMainX + 8;
    int y0 = kHeaderH + 38;
    int w  = kMainW - 16;
    int h  = 116;

    FillRect(r, kPanelDark, x0, y0, w, h);
    OutlineRect(r, kBorder, x0, y0, w, h);
    FocusFrame(r, s, Editor::Panel::Params, x0, y0, w, h);
    DrawSectionTitle(r, x0 + 8, y0 + 6, w - 16,
                     EditingPanel(s, Editor::Panel::Params)
                         ? "Parameters  [EDITING - Tab to switch]" : "Parameters");

    if (!s.instrument) {
        DrawText(r, kTextDim, x0 + 16, y0 + 28, "(no data)");
        return;
    }
    const int* par = s.instrument->parameters;

    // 12 named params in 2 columns of 6.
    for (int i = 0; i < 12; ++i) {
        int col = i / 6;
        int row = i % 6;
        int x = x0 + 16 + col * 220;
        int y = y0 + 26 + row * 14;
        DrawTextF(r, kText, x, y, "%s : %02X",
                  kParamLabels[i].name, par[kParamLabels[i].idx] & 0xFF);
    }

    // AUDCTL flags, 8 bits as a 4x2 grid of wide toggles (room for labels).
    int ax = x0 + 460;
    int ay = y0 + 26;
    constexpr int kFlagW = 88, kFlagH = 18, kFlagSX = 92, kFlagSY = 22;
    DrawText(r, kHighlight, ax, ay, "AUDCTL  (right-click toggles)");
    static const char* names[8] = {
        "15kHz", "HPF 2", "HPF 1", "Join3-4",
        "Join1-2", "1.79MHz3", "1.79MHz1", "Poly9"
    };
    int idxs[8] = {
        PAR_AUDCTL_15KHZ, PAR_AUDCTL_HPF_CH2, PAR_AUDCTL_HPF_CH1, PAR_AUDCTL_JOIN_3_4,
        PAR_AUDCTL_JOIN_1_2, PAR_AUDCTL_179_CH3, PAR_AUDCTL_179_CH1, PAR_AUDCTL_POLY9
    };
    for (int i = 0; i < 8; ++i) {
        int col = i % 4, row = i / 4;
        int bx = ax + col * kFlagSX, by = ay + 14 + row * kFlagSY;
        bool on = par[idxs[i]] != 0;
        Col cell = on ? kAccent : kBankEmpty;
        FillRect(r, cell, bx, by, kFlagW, kFlagH);
        OutlineRect(r, kBorder, bx, by, kFlagW, kFlagH);
        DrawText(r, on ? kBg : kTextDim, bx + 4, by + 5, names[i]);
    }

    // Edit cursor.
    if (EditingPanel(s, Editor::Panel::Params)) {
        int ci = s.editor->param_idx;
        if (ci < 12) {
            int col = ci / 6, row = ci % 6;
            int cx = x0 + 16 + col * 220;
            int cy = y0 + 26 + row * 14;
            CellCursor(r, cx - 2, cy - 2, 210, 13);
        } else {
            int fi = ci - 12;
            if (fi >= 0 && fi < 8) {
                int col = fi % 4, row = fi / 4;
                CellCursor(r, ax + col * kFlagSX - 1, ay + 14 + row * kFlagSY - 1,
                           kFlagW + 1, kFlagH + 1);
            }
        }
    }
}

void Gui::DrawEnvelope(SDL_Renderer* r, const GuiState& s)
{
    int x0 = kMainX + 8;
    int y0 = kHeaderH + 160;
    int w  = kMainW - 16;
    int h  = 200;

    FillRect(r, kPanelDark, x0, y0, w, h);
    OutlineRect(r, kBorder, x0, y0, w, h);
    FocusFrame(r, s, Editor::Panel::Envelope, x0, y0, w, h);
    DrawSectionTitle(r, x0 + 8, y0 + 6, w - 16,
                     EditingPanel(s, Editor::Panel::Envelope)
                         ? "Envelope  [EDITING - Tab to switch]" : "Envelope");

    if (!s.instrument) return;
    const int* par = s.instrument->parameters;
    int env_len = par[PAR_ENV_LENGTH];

    // 8 rows. Each row label ~ 48px, then a grid of N+1 columns.
    static const char* row_names[ENVROWS] = {
        "VolR", "VolL", "Filt", "Cmd ", "Dist", "Port", "X   ", "Y   "
    };
    int label_w = 44;
    int grid_x  = x0 + 8 + label_w + 8;
    int grid_w  = w - 24 - label_w;
    int cells   = std::max(1, env_len + 1);
    int cell_w  = std::max(8, grid_w / std::max(cells, 16));
    if (cell_w > 24) cell_w = 24;

    int grid_y  = y0 + 26;
    int cell_h  = 18;

    for (int row = 0; row < ENVROWS; ++row) {
        int rx = x0 + 12;
        int ry = grid_y + row * (cell_h + 2);
        DrawText(r, kTextDim, rx, ry + 5, row_names[row]);

        for (int c = 0; c <= env_len; ++c) {
            int v = s.instrument->envelope[c][row];
            int cx = grid_x + c * cell_w;
            int cy = ry;

            // Colour by value intensity.
            Uint8 g = (Uint8)std::min(220, 40 + v * 18);
            Col fill{ kEnvCell.r, g, kEnvCell.b };
            FillRect(r, fill, cx, cy, cell_w - 1, cell_h);
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%X", v & 0xF);
            DrawText(r, kBg, cx + (cell_w >= 12 ? cell_w/2 - 4 : 1), cy + 5, buf);
        }
        // Mark the envelope goto column with a thin red bar.
        int g_c = par[PAR_ENV_GOTO];
        if (g_c >= 0 && g_c <= env_len) {
            FillRect(r, kBankCurrent, grid_x + g_c * cell_w + cell_w - 2,
                     ry, 2, cell_h);
        }
    }

    // Edit cursor.
    if (EditingPanel(s, Editor::Panel::Envelope)) {
        int c = std::clamp(s.editor->env_col, 0, env_len);
        int row = std::clamp(s.editor->env_row, 0, ENVROWS - 1);
        int cx = grid_x + c * cell_w;
        int cy = grid_y + row * (cell_h + 2);
        CellCursor(r, cx, cy, cell_w - 1, cell_h);
    }
}

void Gui::DrawNoteTable(SDL_Renderer* r, const GuiState& s)
{
    int x0 = kMainX + 8;
    int y0 = kHeaderH + 366;
    int w  = kMainW - 16;
    int h  = 64;

    FillRect(r, kPanelDark, x0, y0, w, h);
    OutlineRect(r, kBorder, x0, y0, w, h);
    FocusFrame(r, s, Editor::Panel::NoteTable, x0, y0, w, h);
    DrawSectionTitle(r, x0 + 8, y0 + 6, w - 16,
                     EditingPanel(s, Editor::Panel::NoteTable)
                         ? "Note table  [EDITING - Tab to switch]" : "Note table");

    if (!s.instrument) return;
    const int* par = s.instrument->parameters;
    int tbl_len = par[PAR_TBL_LENGTH];

    int label_w = 44;
    int gx = x0 + 8 + label_w + 8;
    int gw = w - 24 - label_w;
    int cell_w = std::max(16, gw / 32);
    if (cell_w > 24) cell_w = 24;
    int gy = y0 + 24;

    DrawText(r, kTextDim, x0 + 12, gy + 5, "Step");
    for (int i = 0; i <= tbl_len && i < NOTE_TABLE_MAX_LEN; ++i) {
        Col c = (i == par[PAR_TBL_GOTO]) ? kHighlight : kEnvCell;
        FillRect(r, c, gx + i * cell_w, gy, cell_w - 1, 22);
        DrawTextF(r, kBg, gx + i * cell_w + 2, gy + 5, "%02X",
                  s.instrument->noteTable[i] & 0xFF);
    }

    // Edit cursor.
    if (EditingPanel(s, Editor::Panel::NoteTable)) {
        int i = std::clamp(s.editor->tbl_idx, 0, tbl_len);
        CellCursor(r, gx + i * cell_w, gy, cell_w - 1, 22);
    }
}

bool Gui::PointInBankEdit(int x, int y) const
{
    BankGeom g = ComputeBankGeom();
    int ebx = g.panel_x + g.panel_w - 70, eby = g.panel_y + 2, ebw = 62, ebh = 16;
    return x >= ebx && x < ebx + ebw && y >= eby && y < eby + ebh;
}

int Gui::BankSlotAtLogical(int x, int y) const
{
    BankGeom g = ComputeBankGeom();
    if (g.cw <= 0 || g.ch <= 0) return -1;
    int col = (x - g.gx) / g.cw;
    int row = (y - g.gy) / g.ch;
    if (col < 0 || col >= 8 || row < 0 || row >= 8) return -1;
    if (x < g.gx || y < g.gy) return -1;
    int slot = row * 8 + col;
    return (slot >= 0 && slot < Bank::SLOT_COUNT) ? slot : -1;
}

Gui::EditHit Gui::EditFieldAtLogical(int x, int y, const TInstrument& ins) const
{
    EditHit r;

    // --- Instrument header (Name) ---  y in [kHeaderH+4, +32]
    {
        int hy = kHeaderH + 4;
        if (y >= hy && y < hy + 28 && x >= kMainX + 4 && x < kMainX + 4 + kMainW - 8) {
            int name_x = kMainX + 120 + kGlyphW; // past the opening quote
            int pos = (x - name_x) / kGlyphW;
            r.hit = true; r.panel = Editor::Panel::Name;
            r.a = std::clamp(pos, 0, Editor::NameLength(ins));
            return r;
        }
    }

    // --- Parameters --- panel at (kMainX+8, kHeaderH+38, kMainW-16, 116)
    {
        int x0 = kMainX + 8, y0 = kHeaderH + 38;
        // 12 named params: 2 columns of 6, cells ~210x13 on a 220x14 grid.
        for (int i = 0; i < 12; ++i) {
            int col = i / 6, row = i % 6;
            int cx = x0 + 16 + col * 220;
            int cy = y0 + 26 + row * 14;
            if (x >= cx - 2 && x < cx + 208 && y >= cy - 2 && y < cy + 11) {
                r.hit = true; r.panel = Editor::Panel::Params; r.a = i; return r;
            }
        }
        // 8 AUDCTL flag boxes (4x2 grid, must match DrawParameters).
        int ax = x0 + 460, ay = y0 + 26;
        for (int fi = 0; fi < 8; ++fi) {
            int col = fi % 4, row = fi / 4;
            int bx = ax + col * 92, by = ay + 14 + row * 22;
            if (x >= bx && x < bx + 88 && y >= by && y < by + 18) {
                r.hit = true; r.panel = Editor::Panel::Params; r.a = 12 + fi; return r;
            }
        }
    }

    // --- Envelope --- panel at (kMainX+8, kHeaderH+160, kMainW-16, 200)
    {
        int x0 = kMainX + 8, y0 = kHeaderH + 160, w = kMainW - 16;
        int env_len = ins.parameters[PAR_ENV_LENGTH];
        int label_w = 44;
        int grid_x = x0 + 8 + label_w + 8;
        int grid_w = w - 24 - label_w;
        int cells = std::max(1, env_len + 1);
        int cell_w = std::max(8, grid_w / std::max(cells, 16));
        if (cell_w > 24) cell_w = 24;
        int grid_y = y0 + 26, cell_h = 18, row_pitch = cell_h + 2;
        if (x >= grid_x && y >= grid_y) {
            int c = (x - grid_x) / cell_w;
            int row = (y - grid_y) / row_pitch;
            int within = (y - grid_y) - row * row_pitch;
            if (c >= 0 && c <= env_len && row >= 0 && row < ENVROWS && within < cell_h) {
                r.hit = true; r.panel = Editor::Panel::Envelope; r.a = c; r.b = row;
                return r;
            }
        }
    }

    // --- Note table --- panel at (kMainX+8, kHeaderH+366, kMainW-16, 64)
    {
        int x0 = kMainX + 8, y0 = kHeaderH + 366, w = kMainW - 16;
        int tbl_len = ins.parameters[PAR_TBL_LENGTH];
        int label_w = 44;
        int gx = x0 + 8 + label_w + 8;
        int gw = w - 24 - label_w;
        int cell_w = std::max(16, gw / 32);
        if (cell_w > 24) cell_w = 24;
        int gy = y0 + 24;
        if (x >= gx && y >= gy && y < gy + 22) {
            int i = (x - gx) / cell_w;
            if (i >= 0 && i <= tbl_len) {
                r.hit = true; r.panel = Editor::Panel::NoteTable; r.a = i; return r;
            }
        }
    }

    return r; // r.hit == false
}

void Gui::DrawBank(SDL_Renderer* r, const GuiState& s)
{
    BankGeom g = ComputeBankGeom();

    FillRect(r, kPanelDark, g.panel_x, g.panel_y, g.panel_w, g.panel_h);
    OutlineRect(r, kBorder, g.panel_x, g.panel_y, g.panel_w, g.panel_h);
    DrawSectionTitle(r, g.panel_x + 8, g.panel_y + 6, g.panel_w - 16,
                     s.bank_edit ? "Bank  EDIT: Ctrl+C/X/V move instruments"
                                 : "Bank  (Ctrl+key plays the selected slot)");

    // EDIT toggle button at the far right of the bank title row.
    int ebx = g.panel_x + g.panel_w - 70, eby = g.panel_y + 2, ebw = 62, ebh = 16;
    FillRect(r, s.bank_edit ? kBankCurrent : kPanelDark, ebx, eby, ebw, ebh);
    OutlineRect(r, s.bank_edit ? kHighlight : kBorder, ebx, eby, ebw, ebh);
    DrawText(r, s.bank_edit ? kBg : kTextDim, ebx + 14, eby + 4, "EDIT");

    if (!s.bank) return;

    int cell_w = g.cw, cell_h = g.ch, cell_pad = g.pad;
    std::string cur_path = s.rti ? s.rti->Path() : std::string{};

    for (int slot = 0; slot < Bank::SLOT_COUNT; ++slot) {
        int col = slot % 8;
        int row = slot / 8;
        int cx = g.gx + col * cell_w;
        int cy = g.gy + row * cell_h;

        const Bank::Slot& sl = s.bank->At(slot);
        Col fill = !sl.used ? kBankEmpty : (sl.dirty ? kOrange : kBankFilled);
        bool is_current = (slot == s.current_bank_slot) ||
                          (sl.used && !cur_path.empty() && sl.source_path == cur_path);
        Col border = is_current ? kBankCurrent : kBorder;

        FillRect(r, fill, cx + cell_pad, cy + cell_pad,
                 cell_w - 2 * cell_pad, cell_h - 2 * cell_pad);
        OutlineRect(r, border, cx + cell_pad, cy + cell_pad,
                    cell_w - 2 * cell_pad, cell_h - 2 * cell_pad);

        // Selection cursor: a bright double outline.
        if (slot == s.bank_cursor) {
            OutlineRect(r, kHighlight, cx + cell_pad - 1, cy + cell_pad - 1,
                        cell_w - 2 * cell_pad + 2, cell_h - 2 * cell_pad + 2);
            OutlineRect(r, kHighlight, cx + cell_pad, cy + cell_pad,
                        cell_w - 2 * cell_pad, cell_h - 2 * cell_pad);
        }

        // Label: "NN-name" (e.g. "00-Test"). Wraps onto a second line if the
        // name is long.
        int chars = std::max(0, (cell_w - 2 * cell_pad - 8) / kGlyphW);
        char numbuf[8];
        std::snprintf(numbuf, sizeof(numbuf), "%02d", slot);
        std::string full = numbuf;
        if (sl.used && !sl.name.empty()) full += "-" + sl.name;

        std::string line1 = full.substr(0, (size_t)std::max(chars, 0));
        DrawText(r, kBg, cx + cell_pad + 4, cy + cell_pad + 4, line1.c_str());
        if ((int)full.size() > chars && chars > 0) {
            std::string line2 = Truncate(full.substr((size_t)chars), chars);
            DrawText(r, kBg, cx + cell_pad + 4, cy + cell_pad + 16, line2.c_str());
        }
    }
}

void Gui::DrawHelpOverlay(SDL_Renderer* r, const GuiState& /*s*/)
{
    struct Row { const char* keys; const char* desc; };
    static const Row rows[] = {
        { "a .. z, 0 .. 9", "Play current instrument at chromatic pitches" },
        { "[  /  ]",        "Octave shift down / up" },
        { "Left / Right",   "Previous / next .RTI (hold to repeat)" },
        { "Up / Down",      "Previous / next .RTI (hold to repeat)" },
        { "Mouse wheel",    "Move selection quickly (3 at a time)" },
        { "PageUp / PageDn","Jump by 10 files" },
        { "Home / End",     "First / last file" },
        { "/",              "Search: filter instruments by name" },
        { "Enter",          "Toggle current file's folder (collapse/expand)" },
        { "+  (or =)",      "Add current instrument to bank" },
        { "-",              "Remove current instrument from bank" },
        { "Click / Tab",    "Select a bank slot (click a filled slot to load it)" },
        { "Ctrl+arrows",    "Move the bank selection cursor (Up/Down = by row)" },
        { "Ctrl+a-z/0-9",   "Sample (play) the selected bank slot" },
        { "Ctrl+Ins",       "Copy current instrument into selected slot (confirm if full)" },
        { "Ctrl+Del",       "Delete the selected bank slot (confirm)" },
        { "Bank EDIT button","Toggle: when on, Ctrl+C/X/V move slots; when off they play" },
        { "Ctrl+C / X / V", "(EDIT on) copy / cut / paste a bank slot = move/reorder" },
        { "Click a field",  "Jump into Edit mode on that parameter / cell / name" },
        { "Right-click",    "Toggle a binary field (AUDCTL flag, type/mode, filter)" },
        { "F6",             "Toggle Edit mode" },
        { "  (edit) Tab",   "Cycle panel: Params/Envelope/Note table/Name" },
        { "  (edit) arrows","Move the cell cursor" },
        { "  (edit) 0-9 A-F","Type a value into the cell" },
        { "  (edit) +/- or Shift+Up/Dn","Nudge the value up / down; Space re-auditions" },
        { "  (edit) Ctrl+key","Hold Ctrl + a-z/0-9 to audition pitches while editing" },
        { "  (edit) Ctrl+Z / Ctrl+Y","Undo / redo the last edit" },
        { "Ctrl+S",         "Export the current instrument as a new .RTI file" },
        { "Drag & drop",    "Drop a folder (open as library) or a .RTI onto the window" },
        { "F1",             "Toggle this help" },
        { "F2",             "Save bank (.rmt + .rti folder)" },
        { "F3",             "Load bank (.rmt or saved folder)" },
        { "F4",             "Switch instrument library folder" },
        { "F5",             "Toggle PAL / NTSC" },
        { "F7",             "Analyse: classify + hide duplicate instruments" },
        { "F8",             "Toggle folder view / group-by-category" },
        { "F9",             "Show / hide duplicate instruments" },
        { "F11",            "Toggle fullscreen" },
        { "Esc",            "Silence playback (or close this help)" },
        { "Close window",   "Quit" },
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));

    // Dim the whole screen.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    FillRect(r, Col{ 0, 0, 0, 180 }, 0, 0, kWinW, kWinH);

    int panel_w = 860;
    int row_h   = 15;
    int panel_h = 74 + n * row_h;   // title + rows + footer; fits 720 tall
    int px = (kWinW - panel_w) / 2;
    int py = (kWinH - panel_h) / 2;

    FillRect(r, Col{ 30, 34, 44, 245 }, px, py, panel_w, panel_h);
    OutlineRect(r, kAccent, px, py, panel_w, panel_h);
    OutlineRect(r, kAccent, px + 1, py + 1, panel_w - 2, panel_h - 2);

    DrawText(r, kHighlight, px + 20, py + 18, "PokeyForge  -  Keybindings");
    SetCol(r, kBorder);
    SDL_RenderLine(r, (float)(px + 20), (float)(py + 34),
                   (float)(px + panel_w - 20), (float)(py + 34));

    int y = py + 48;
    for (int i = 0; i < n; ++i) {
        DrawText(r, kHighlight, px + 28, y, rows[i].keys);
        DrawText(r, kText,      px + 240, y, rows[i].desc);
        y += row_h;
    }

    DrawText(r, kTextDim, px + 20, py + panel_h - 22,
             "Press F1 or Esc to close.");

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}
