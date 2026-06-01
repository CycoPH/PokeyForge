#include "Gui.h"
#include "Analysis.h"
#include "Version.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

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
		int x0 = kMainX + 2;
		int y0 = kHeaderH + 436;
		int w = kMainW - 4;
		int h = (kWinH - kCmdBarH - 4) - y0;
		// Cells leave only 4 px of panel margin on each side. The grid origin
		// pulls in by (4 - cell_pad) so the visible tile (offset by cell_pad
		// inside its cell) lands exactly 4 px from the panel edge.
		constexpr int kCellPad = 3;
		int gx = x0 + (4 - kCellPad);
		int gy = y0 + 28;
		int cw = (w - 2 * (4 - kCellPad)) / 8;
		int ch = (h - 36) / 8;
		return BankGeom{ x0, y0, w, h, gx, gy, cw, ch, kCellPad };
	}

	// Colours (RGB).
	struct Col { Uint8 r, g, b, a = 255; };
	constexpr Col kBg{ 16, 18, 24 };
	constexpr Col kPanel{ 28, 32, 40 };
	constexpr Col kPanelDark{ 22, 26, 32 };
	constexpr Col kAccent{ 80, 140, 220 };
	constexpr Col kText{ 220, 222, 230 };
	constexpr Col kTextDim{ 130, 134, 144 };
	constexpr Col kHighlight{ 255, 220, 90 };
	constexpr Col kBankFilled{ 60, 180, 90 };
	constexpr Col kBankEmpty{ 50, 54, 64 };
	constexpr Col kBankCurrent{ 220, 90, 80 };
	constexpr Col kFolder{ 130, 200, 240 };
	constexpr Col kBorder{ 70, 76, 88 };
	constexpr Col kEnvCell{ 60, 120, 200 };
	constexpr Col kOrange{ 235, 150, 40 };   // modified / unsaved
	constexpr Col kEditCursor{ 255, 255, 255 };  // focused-panel frame
	constexpr Col kCellCursor{ 235, 40, 40 };    // focused edit cell (red)

	// Per-category palette used to colour file rows in the directory tree.
	// Indices match Analysis::Category. "Other" rows fall through to kText.
	// Mapping is deliberately gentle - just enough hue separation that the
	// user can glance at the tree and spot category boundaries without the
	// whole pane turning into a rainbow.
	constexpr Col kCatBass{ 130, 200, 240 };  // cyan
	constexpr Col kCatLead{ 240, 200, 110 };  // amber
	constexpr Col kCatLeadVibrato{ 240, 170,  80 };  // deeper amber
	constexpr Col kCatArp{ 180, 220, 120 };  // green-yellow
	constexpr Col kCatChord{ 130, 220, 130 };  // green
	constexpr Col kCatGlide{ 120, 200, 180 };  // teal
	constexpr Col kCatPad{ 180, 150, 230 };  // soft purple
	constexpr Col kCatBell{ 250, 230, 130 };  // pale yellow
	constexpr Col kCatKick{ 230, 130, 130 };  // pink-red
	constexpr Col kCatSnare{ 230, 160, 120 };  // salmon
	constexpr Col kCatHiHat{ 200, 180, 180 };  // grey-pink
	constexpr Col kCatPerc{ 210, 140, 160 };  // dusty rose
	constexpr Col kCatSweptFX{ 160, 200, 220 };  // light blue-grey
	constexpr Col kCatNoiseFX{ 150, 150, 180 };  // grey-blue

	Col CategoryColour(int cat)
	{
		// Indices follow Analysis::Category. -1 / out-of-range -> default text.
		switch (cat) {
			case 0:  return kCatBass;
			case 1:  return kCatLead;
			case 2:  return kCatLeadVibrato;
			case 3:  return kCatArp;
			case 4:  return kCatChord;
			case 5:  return kCatGlide;
			case 6:  return kCatPad;
			case 7:  return kCatBell;
			case 8:  return kCatKick;
			case 9:  return kCatSnare;
			case 10: return kCatHiHat;
			case 11: return kCatPerc;
			case 12: return kCatSweptFX;
			case 13: return kCatNoiseFX;
			default: return kText;
		}
	}

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
		SDL_RenderLine(r, static_cast<float>(x), static_cast<float>(y + 12), static_cast<float>(x + w), static_cast<float>(y + 12));
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

	// Directory-pane view tabs (left pane). y is relative to kHeaderH so the
	// hit-test and the renderer share the same geometry. Folders is the primary
	// view, so it sits flush in the top-left corner and is taller than the
	// filter buttons. "No dupes" is right-aligned against kTreeW.
	struct DirTabBtn { const char* label; Gui::DirTab tab; int x, y_off, w, h; };
	constexpr int kDirTabRelY = 4;   // default offset (used by the separator line)
	constexpr int kDirTabH = 18;     // default height for the filter buttons
	// Five tabs squeezed into the 320 px panel: three view-mode buttons on the
	// left and two dupe-filter buttons on the right (the "No dupes" right edge
	// stays flush with the panel border at kTreeW). Spacing is tight: 2-4 px
	// gaps between adjacent buttons.
	const DirTabBtn kDirTabs[] = {
		{ "Folders",  Gui::DirTab::Folders,     0, 0, 64, 22 },
		{ "Category", Gui::DirTab::Category,   66, 0, 68, 22 },
		{ "Clusters", Gui::DirTab::Cluster,   136, 0, 68, 22 },
		{ "All",      Gui::DirTab::ShowAll,   208, 0, 30, 22 },
		{ "No dupes", Gui::DirTab::HideDupes, 240, 0, 80, 22 },
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
	for (const auto& t : kDirTabs) {
		int ty = kHeaderH + t.y_off;
		if (y >= ty && y < ty + t.h && x >= t.x && x < t.x + t.w) return t.tab;
	}
	return DirTab::None;
}

int Gui::TreeRowAtLogical(int x, int y) const
{
	if (x < 0 || x >= kTreeW) return -1;
	// The scrollbar overlaps the tree column; clicks there are not rows.
	if (m_scroll_track_w > 0 &&
		x >= m_scroll_track_x && x < m_scroll_track_x + m_scroll_track_w &&
		y >= m_scroll_track_y && y < m_scroll_track_y + m_scroll_track_h) {
		return -1;
	}
	if (y < m_tree_top || m_tree_rowh <= 0) return -1;
	int vis = (y - m_tree_top) / m_tree_rowh;
	int idx = m_tree_scroll + vis;
	if (idx < 0 || idx >= m_tree_row_count) return -1;
	return idx;
}

char Gui::TreeScrollbarHit(int x, int y) const
{
	if (m_scroll_track_w <= 0 || m_scroll_track_h <= 0) return 0;
	if (x < m_scroll_track_x || x >= m_scroll_track_x + m_scroll_track_w) return 0;
	if (y < m_scroll_track_y || y >= m_scroll_track_y + m_scroll_track_h) return 0;
	if (y < m_scroll_thumb_y) return 'a';
	if (y >= m_scroll_thumb_y + m_scroll_thumb_h) return 'b';
	return 't';
}

void Gui::BeginTreeScrollDrag(int y)
{
	m_scroll_dragging = true;
	m_scroll_drag_offset = y - m_scroll_thumb_y;
	m_user_scrolled = true;
}

void Gui::UpdateTreeScrollDrag(int y)
{
	if (!m_scroll_dragging) return;
	int travel = m_scroll_track_h - m_scroll_thumb_h;
	int max_scroll = std::max(0, m_tree_row_count - m_tree_visible_rows);
	if (travel <= 0 || max_scroll <= 0) {
		m_tree_scroll = 0;
		return;
	}
	int new_thumb_y = y - m_scroll_drag_offset;
	int rel = new_thumb_y - m_scroll_track_y;
	rel = std::clamp(rel, 0, travel);
	// Round to nearest to avoid systematic bias as the thumb is dragged.
	int new_scroll = (rel * max_scroll + travel / 2) / travel;
	m_tree_scroll = std::clamp(new_scroll, 0, max_scroll);
	m_user_scrolled = true;
}

void Gui::EndTreeScrollDrag()
{
	m_scroll_dragging = false;
}

void Gui::PageTreeScroll(int direction)
{
	int page = std::max(1, m_tree_visible_rows - 1);
	int max_scroll = std::max(0, m_tree_row_count - m_tree_visible_rows);
	m_tree_scroll = std::clamp(m_tree_scroll + direction * page, 0, max_scroll);
	m_user_scrolled = true;
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
	if (s.bank_menu_open)  DrawBankMenu(r, s);
	if (s.tree_menu_open)  DrawTreeMenu(r, s);
	if (s.cat_picker_open) DrawCategoryPicker(r, s);
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
	DrawText(r, kText, tx, y, "RMT .RTI instrument auditioner, editor & bank builder");
	DrawText(r, kOrange, tx, y + 22, "written by RetroCoder");

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
	}
	else {
		int cut = max_chars;
		while (cut > 0 && msg[cut] != ' ') --cut;
		if (cut == 0) cut = max_chars;
		DrawText(r, kText, px + 20, py + 44, msg.substr(0, cut).c_str());
		DrawText(r, kText, px + 20, py + 58, msg.substr(cut + 1).c_str());
	}

	int by = py + kConfirmH - kConfirmBtnH - 14;
	for (const auto& b : kConfirmBtns) {
		int bx = px + b.dx;
		bool hovered = (s.mouse_x >= bx && s.mouse_x < bx + b.w &&
			s.mouse_y >= by && s.mouse_y < by + kConfirmBtnH);
		FillRect(r, hovered ? kAccent : kPanelDark, bx, by, b.w, kConfirmBtnH);
		OutlineRect(r, kAccent, bx, by, b.w, kConfirmBtnH);
		int tw = (int)std::char_traits<char>::length(b.label) * kGlyphW;
		DrawText(r, hovered ? kBg : kHighlight,
			bx + (b.w - tw) / 2, by + 9, b.label);
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
	int w = 274;
	int h = kHeaderH - 6;
	int x = kWinW - w - 6;
	int y = 3;

	FillRect(r, kBg, x, y, w, h);
	OutlineRect(r, kBorder, x, y, w, h);

	// Note-number readout pinned to the top-left of the scope panel. Shows
	// the value (0..60) actually sent to the RMT driver on the most-recent
	// NoteOn - that's what the kMinNote..kMaxNote-clamped result of
	// (BASE_NOTE + semitone + octave_shift) ends up as. Hidden when no
	// note has been played yet (e.g. just after Silence / load).
	if (s.last_note_played >= 0) {
		char nbuf[16];
		std::snprintf(nbuf, sizeof(nbuf), "#%d", s.last_note_played);
		DrawText(r, kHighlight, x - 32 + 4, y + 10, nbuf);
	}

	int mid = y + h / 2;

	// Faint centre line so the scope reads as a scope even when silent.
	SetCol(r, kBorder);
	SDL_RenderLine(r, (float)(x + 2), (float)mid, (float)(x + w - 2), (float)mid);

	// Show channel 1 only (POKEY voice 0, where the audition note plays),
	// synthesized from its live registers like the old per-channel scope.
	int audf = s.pokey[0];
	int audc = s.pokey[1];
	int vol = audc & 0x0F;
	bool tone = ((audc & 0xE0) == 0xA0) || ((audc & 0xE0) == 0xC0);

	if (vol == 0) return;   // silent -> centre line only

	int half = h / 2 - 2;
	int amp = half * vol / 15;
	int cyc = std::clamp((256 - audf) / 14 + 1, 1, 22);
	int inner = w - 4;
	int prev_y = mid;
	SetCol(r, kAccent);
	for (int px = 0; px <= inner; ++px) {
		int val;
		if (tone) {
			double phase = (double)px / (double)inner * cyc;
			double frac = phase - (double)(long)phase;
			val = (frac < 0.5) ? amp : -amp;
		}
		else {
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
	int h = 56;
	int y = kWinH - kCmdBarH - h;
	int x = kMainX + 4;
	int w = kWinW - x - 4;

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
	}
	else {
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

void Gui::DrawSavePrompt(SDL_Renderer* r, const GuiState& s)
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
		bool hovered = (s.mouse_x >= bx && s.mouse_x < bx + b.w &&
			s.mouse_y >= by && s.mouse_y < by + kPromptBtnH);
		FillRect(r, hovered ? kAccent : kPanelDark, bx, by, b.w, kPromptBtnH);
		OutlineRect(r, kAccent, bx, by, b.w, kPromptBtnH);
		int tw = (int)std::char_traits<char>::length(b.label) * kGlyphW;
		DrawText(r, hovered ? kBg : kHighlight,
			bx + (b.w - tw) / 2, by + 9, b.label);
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
	}
	else {
		DrawText(r, kTextDim, tx, y + 7, "/ to search");
	}
}

bool Gui::PointInSearchBar(int x, int y) const
{
	int by = kWinH - kCmdBarH - kSearchH;
	return x >= 4 && x < kTreeW - 4 && y >= by && y < by + kSearchH;
}

bool Gui::PointInTreePane(int x, int y) const
{
	return x >= 0 && x < kTreeW && y >= kHeaderH && y < kWinH - kCmdBarH;
}

bool Gui::PointInOctaveIndicator(int x, int y) const
{
	// The "Oct: +N" text is drawn at (690, 12) in DrawHeader; widen the
	// hit area slightly so a casual mouse-over works without aiming.
	return x >= 685 && x < 760 && y >= kMenuY && y < kMenuY + kMenuH;
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

	DrawText(r, kTextDim, 8, y + 7, "F2 Save  F3 Load  F4 Lib  F7 Analyse  F8 Group  F9 Dupes");

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
	if (s.modified) DrawText(r, kOrange, 916, 12, "MODIFIED");

	// Master oscilloscope in the top-right corner.
	DrawMasterScope(r, s);
}

void Gui::DrawTree(SDL_Renderer* r, const GuiState& s)
{
	FillRect(r, kPanelDark, 0, kHeaderH, kTreeW, kWinH - kHeaderH);
	OutlineRect(r, kBorder, 0, kHeaderH, kTreeW, kWinH - kHeaderH);

	// View tabs (click to switch; equivalent to F8 / F9 / F10).
	if (s.dir) {
		Directory::ViewMode vm = s.dir->GetViewMode();
		bool is_folder = vm == Directory::ViewMode::Folder;
		bool is_cat = vm == Directory::ViewMode::Category;
		bool is_cluster = vm == Directory::ViewMode::Cluster;
		bool hide = s.dir->HideDuplicates();
		for (const auto& t : kDirTabs) {
			bool active = false;
			switch (t.tab) {
				case DirTab::Folders:   active = is_folder;  break;
				case DirTab::Category:  active = is_cat;     break;
				case DirTab::Cluster:   active = is_cluster; break;
				case DirTab::ShowAll:   active = !hide;      break;
				case DirTab::HideDupes: active = hide;       break;
				default: break;
			}
			int ty = kHeaderH + t.y_off;
			FillRect(r, active ? kAccent : kPanelDark, t.x, ty, t.w, t.h);
			OutlineRect(r, kBorder, t.x, ty, t.w, t.h);
			int label_w = (int)std::strlen(t.label) * kGlyphW;
			int text_x = t.x + (t.w - label_w) / 2;
			int text_y = ty + (t.h - 8) / 2;   // 8 px tall debug font
			DrawText(r, active ? kBg : kTextDim, text_x, text_y, t.label);
		}
	}
	SetCol(r, kBorder);
	SDL_RenderLine(r, 4, (float)(kHeaderH + kDirTabRelY + kDirTabH + 2),
		(float)(kTreeW - 4), (float)(kHeaderH + kDirTabRelY + kDirTabH + 2));

	if (!s.dir) return;
	const auto& rows = s.dir->Rows();
	int cur_node = s.dir->CurrentNodeIndex();

	constexpr int kRowH = 16;
	constexpr int kScrollW = 14;     // scrollbar width (flush with right edge)
	constexpr int kScrollPad = 4;    // gap between rows and scrollbar
	constexpr int kRowRight = kTreeW - kScrollW - kScrollPad; // text right edge
	int top = kHeaderH + 26;
	// Reserve the bottom for the footer line, the search bar and command bar.
	int bottom = kWinH - kCmdBarH - kSearchH - 16;
	// In Cluster view we steal 72 px from the bottom of the row list to
	// host the spectral signature panel (drawn below). The panel only
	// appears in this view because that's where the user is browsing by
	// similarity and the visual signature is most useful.
	bool show_spectral = s.dir && s.dir->GetViewMode() == Directory::ViewMode::Cluster;
	constexpr int kSpectralH = 72;
	int spectral_top = 0;
	if (show_spectral) {
		spectral_top = bottom - kSpectralH;
		bottom = spectral_top - 4;   // small gap above the panel
	}
	int visible_rows = (bottom - top) / kRowH;
	if (visible_rows < 1) return;

	// Find the current file's row.
	int cur_pos = -1;
	for (int i = 0; i < (int)rows.size(); ++i) {
		if (!rows[i].is_header && rows[i].node == cur_node) { cur_pos = i; break; }
	}
	// Auto-snap: keep the selected file visible, but only when the selection
	// actually changes. While the user is scrolling manually (drag or page),
	// m_user_scrolled stays true and we leave m_tree_scroll alone.
	int total = (int)rows.size();
	int max_scroll = std::max(0, total - visible_rows);
	if (cur_node != m_last_cur_node) {
		m_last_cur_node = cur_node;
		m_user_scrolled = false;
	}
	if (cur_pos >= 0 && !m_user_scrolled) {
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
	m_tree_visible_rows = visible_rows;

	for (int i = scroll; i < end; ++i) {
		const auto& row = rows[i];
		int row_y = top + (i - scroll) * kRowH;
		// Centre the 8 px-tall debug font inside the kRowH-pixel row so the
		// text doesn't sit on the top edge of the cell.
		int text_y = row_y + (kRowH - 8) / 2;
		int indent = row.depth * 6 + 8;
		int avail_chars = (kRowRight - indent) / kGlyphW;

		if (row.is_header) {
			const char* glyph = row.collapsed ? "> " : "v ";
			std::string lbl = std::string(glyph) +
				Truncate(row.label, std::max(0, (kRowRight - 22) / kGlyphW));
			DrawText(r, kHighlight, 6, text_y, lbl.c_str());
			continue;
		}

		const auto& n = s.dir->At(row.node);
		if (row.node == cur_node) {
			FillRect(r, kAccent, 4, row_y, kRowRight - 4, kRowH);
		}

		if (n.type == Directory::NodeType::Folder) {
			const char* glyph = n.expanded ? "v " : "> ";
			std::string name = Truncate(n.name, std::max(0, avail_chars - 3));
			DrawTextF(r, kFolder, indent, text_y, "%s%s/", glyph, name.c_str());
		}
		else {
			// File rows are tinted by their effective category (manual
			// override beats automatic). The selected row reads white-on-
			// accent regardless. Low-confidence rows dim a bit so the user
			// can see where the analyser was unsure.
			int eff_cat = s.dir->EffectiveCategory(row.node);
			Col cat_col = CategoryColour(eff_cat);
			if (n.confidence == 1) {
				// Dim by mixing with kPanel - low-confidence rows.
				cat_col.r = (Uint8)((cat_col.r + kPanel.r * 2) / 3);
				cat_col.g = (Uint8)((cat_col.g + kPanel.g * 2) / 3);
				cat_col.b = (Uint8)((cat_col.b + kPanel.b * 2) / 3);
			}
			Col c = (row.node == cur_node) ? kBg : cat_col;
			char dot = ' ';
			if (s.bank && s.bank->IndexOfPath(n.path) >= 0) dot = 'B';
			// Show manual-override marker as 'M' (B takes precedence).
			if (dot == ' ' && n.manual_category >= 0) dot = 'M';
			std::string name = Truncate(n.name, std::max(0, avail_chars - 2));
			DrawTextF(r, c, indent, text_y, "%c %s", dot, name.c_str());
		}
	}

	// Vertical scrollbar on the right edge of the tree pane. No bounding box:
	// a single left edge line separates it from the row text and a bottom
	// line caps it. The thumb size is proportional to the visible fraction,
	// with a sensible minimum so it stays grabbable in very large libraries.
	m_scroll_track_x = kTreeW - kScrollW;
	m_scroll_track_y = top;
	m_scroll_track_w = kScrollW;
	m_scroll_track_h = visible_rows * kRowH;

	SetCol(r, kBorder);
	SDL_RenderLine(r,
		(float)m_scroll_track_x, (float)m_scroll_track_y,
		(float)m_scroll_track_x, (float)(m_scroll_track_y + m_scroll_track_h));
	SDL_RenderLine(r,
		(float)m_scroll_track_x, (float)(m_scroll_track_y + m_scroll_track_h),
		(float)(m_scroll_track_x + m_scroll_track_w),
		(float)(m_scroll_track_y + m_scroll_track_h));

	if (total > 0) {
		constexpr int kMinThumb = 18;
		int thumb_h = std::max(kMinThumb, m_scroll_track_h * visible_rows / total);
		if (thumb_h > m_scroll_track_h) thumb_h = m_scroll_track_h;
		int travel = m_scroll_track_h - thumb_h;
		int thumb_y = (max_scroll > 0)
			? m_scroll_track_y + travel * m_tree_scroll / max_scroll
			: m_scroll_track_y;
		m_scroll_thumb_y = thumb_y;
		m_scroll_thumb_h = thumb_h;
		// Inset by 1px on the left so the thumb doesn't sit on the divider
		// line; clamp height so it doesn't cross the bottom line.
		Col thumb_col = (m_scroll_dragging || total > visible_rows) ? kAccent : kBorder;
		int draw_h = std::min(thumb_h, m_scroll_track_y + m_scroll_track_h - thumb_y);
		FillRect(r, thumb_col, m_scroll_track_x + 1, thumb_y,
			m_scroll_track_w - 1, draw_h);
	}
	else {
		m_scroll_thumb_y = m_scroll_track_y;
		m_scroll_thumb_h = m_scroll_track_h;
	}

	// Spectral signature panel (Cluster view only). Eight vertical bars
	// visualise the current instrument's cached audio features as a quick
	// "what does this sound like" snapshot - the numbers behind the
	// category colour. Bar heights are normalised so each axis maxes out
	// at the panel height; centroid and roll-off are divided by
	// 22050 Hz (Nyquist at 44.1k SR) to map into [0,1].
	if (show_spectral) {
		const int sx = 0;
		const int sy = spectral_top;
		const int sw = kTreeW;
		const int sh = kSpectralH;
		// Background + thin divider lines (top / bottom) to set it apart
		// from the tree above and the footer below without a full box.
		FillRect(r, kPanelDark, sx + 4, sy, sw - 8, sh);
		SetCol(r, kBorder);
		SDL_RenderLine(r, (float)(sx + 4), (float)sy,
			(float)(sx + sw - 4), (float)sy);
		SDL_RenderLine(r, (float)(sx + 4), (float)(sy + sh - 1),
			(float)(sx + sw - 4), (float)(sy + sh - 1));
		DrawText(r, kHighlight, sx + 8, sy + 4, "Audio signature");

		int cur = s.dir ? s.dir->CurrentNodeIndex() : -1;
		bool have = (cur >= 0 && s.dir->At(cur).audio_valid);
		if (!have) {
			DrawText(r, kTextDim, sx + 8, sy + sh / 2,
				"(no audio analysis - press F7)");
		}
		else {
			const auto& n = s.dir->At(cur);
			// Eight bars across (sw - 16) px. Each bar's slot is
			// (sw - 16) / 8 px wide; the bar itself uses the inner ~60% so
			// there's visible spacing.
			const char* labels[8] = {
				"Atk", "Mid", "End", "ZCR", "Pk", "Cen", "Rll", "Flx"
			};
			float feat[8] = {
				n.audio[0], n.audio[1], n.audio[2], n.audio[3],
				n.audio[4],
				std::min(1.0f, n.audio[5] / 22050.0f),
				std::min(1.0f, n.audio[6] / 22050.0f),
				std::min(1.0f, n.audio[7])
			};
			int chartTop = sy + 18;
			int chartBottom = sy + sh - 14;
			int chartH = chartBottom - chartTop;
			int innerW = sw - 16;
			int slotW = innerW / 8;
			int barW = (slotW * 3) / 5;
			// Use the effective-category colour for the bars so the panel
			// doubles as a colour-coded confirmation of the row tint.
			int eff_cat = s.dir->EffectiveCategory(cur);
			Col bar_col = CategoryColour(eff_cat);
			for (int i = 0; i < 8; ++i) {
				float v = std::max(0.0f, std::min(1.0f, feat[i]));
				int bh = (int)(v * (float)chartH);
				int bx = sx + 8 + i * slotW + (slotW - barW) / 2;
				int by = chartBottom - bh;
				FillRect(r, bar_col, bx, by, barW, bh);
				// Floor line so empty bars are still visible as a thin
				// baseline strip - helps spot "zero / unmeasured" features.
				FillRect(r, kBorder, bx, chartBottom - 1, barW, 1);
				int label_x = sx + 8 + i * slotW + (slotW - (int)std::strlen(labels[i]) * kGlyphW) / 2;
				DrawText(r, kTextDim, label_x, chartBottom + 2, labels[i]);
			}
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

	bool have_rti = s.rti && s.rti->Valid();
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
	}
	else if (have_rti) {
		name = s.rti->Name();
	}

	// Top text line: "Instrument: name  v1 (NN ATA) file: foo.rti".
	// Bottom line is reserved for analysis metadata (tags + audio features).
	DrawText(r, kHighlight, kMainX + 16, y + 4, "Instrument:");

	int name_x = kMainX + 120;
	DrawTextF(r, kText, name_x, y + 4, "\"%s\"", name.c_str());

	// Name-edit caret.
	if (EditingPanel(s, Editor::Panel::Name)) {
		int caret = name_x + (1 + s.editor->name_pos) * kGlyphW; // +1 for the quote
		FillRect(r, kCellCursor, caret, y + 3, kGlyphW, 12);
		if (s.editor->name_pos < (int)name.size()) {
			char ch[2] = { name[s.editor->name_pos], 0 };
			DrawText(r, kBg, caret, y + 4, ch);
		}
	}

	int info_x = name_x + (int)(name.size() + 4) * kGlyphW;
	if (have_rti) {
		std::string file = s.rti->Path();
		auto slash = file.find_last_of("/\\");
		if (slash != std::string::npos) file = file.substr(slash + 1);
		DrawTextF(r, kTextDim, info_x, y + 4, "v%d  (%zu ATA)  file: %s",
			s.rti->Version(), s.rti->AtaBlob().size(), file.c_str());
	}
	else if (s.current_bank_slot >= 0) {
		DrawTextF(r, kTextDim, info_x, y + 4, "(bank slot %02d)", s.current_bank_slot);
	}

	// Second line: analysis metadata. Reads the current node's cached
	// category / tags / audio features (populated by Analysis::Run /
	// LoadAndApply). Hidden when no analysis is available so the header
	// doesn't show a half-blank line on freshly-opened libraries.
	if (s.dir) {
		int cur = s.dir->CurrentNodeIndex();
		if (cur >= 0) {
			const auto& n = s.dir->At(cur);
			int eff_cat = s.dir->EffectiveCategory(cur);
			if (eff_cat >= 0 || n.tags || n.audio_valid) {
				char buf[256];
				int  pos = 0;
				if (eff_cat >= 0) {
					pos += std::snprintf(buf + pos, sizeof(buf) - pos,
						"Cat: %s", Analysis::Name((Analysis::Category)eff_cat));
					if (n.manual_category >= 0)
						pos += std::snprintf(buf + pos, sizeof(buf) - pos, " [M]");
					if (n.confidence > 0)
						pos += std::snprintf(buf + pos, sizeof(buf) - pos,
							" (conf %d)", n.confidence);
					pos += std::snprintf(buf + pos, sizeof(buf) - pos, "   ");
				}
				if (n.tags) {
					pos += std::snprintf(buf + pos, sizeof(buf) - pos,
						"Tags: %s   ", Analysis::TagsToString(n.tags).c_str());
				}
				if (n.audio_valid) {
					pos += std::snprintf(buf + pos, sizeof(buf) - pos,
						"Cent %.1fk  RMS %.2f  ZCR %.2f  Flux %.2f",
						n.audio[5] / 1000.0f,
						(n.audio[0] + n.audio[1] + n.audio[2]) / 3.0f,
						n.audio[3], n.audio[7]);
				}
				DrawText(r, kTextDim, kMainX + 16, y + 18, buf);
			}
		}
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
	int x0 = kMainX + 2;
	int y0 = kHeaderH + 38;
	int w = kMainW - 4;
	int h = 116;

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
		}
		else {
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
	int x0 = kMainX + 2;
	int y0 = kHeaderH + 160;
	int w = kMainW - 4;
	int h = 200;

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
	int grid_x = x0 + 8 + label_w + 8;
	int grid_w = w - 24 - label_w;
	int cells = std::max(1, env_len + 1);
	int cell_w = std::max(8, grid_w / std::max(cells, 16));
	if (cell_w > 24) cell_w = 24;

	int grid_y = y0 + 26;
	int cell_h = 18;

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
			DrawText(r, kBg, cx + (cell_w >= 12 ? cell_w / 2 - 4 : 1), cy + 5, buf);
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
	int x0 = kMainX + 2;
	int y0 = kHeaderH + 366;
	int w = kMainW - 4;
	int h = 64;

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
		int x0 = kMainX + 2, y0 = kHeaderH + 38;
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
		int x0 = kMainX + 2, y0 = kHeaderH + 160, w = kMainW - 4;
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
		int x0 = kMainX + 2, y0 = kHeaderH + 366, w = kMainW - 4;
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

		// Label: "NN-name" (e.g. "00-Test"). Truncated with a trailing '~'
		// when it doesn't fit; the label never wraps to a second line.
		int chars = std::max(0, (cell_w - 2 * cell_pad - 8) / kGlyphW);
		char numbuf[8];
		std::snprintf(numbuf, sizeof(numbuf), "%02d", slot);
		std::string full = numbuf;
		if (sl.used && !sl.name.empty()) full += "-" + sl.name;

		std::string line = Truncate(full, chars);
		DrawText(r, kBg, cx + cell_pad + 4, cy + cell_pad + 4, line.c_str());
	}
}

// ---------- Bank-slot right-click context menu ----------

namespace {
	struct BankMenuGeom { int x, y, w, h, item_h, items_top; };
	constexpr int kBankMenuW = 152;
	constexpr int kBankMenuItemH = 22;
	constexpr int kBankMenuItems = 4;
	constexpr int kBankMenuHeaderH = 20;   // slot title strip at the top
	constexpr int kBankMenuBottomPad = 4;    // breathing room below the last item

	BankMenuGeom ComputeBankMenuGeom(int anchor_x, int anchor_y)
	{
		int h = kBankMenuHeaderH + kBankMenuItemH * kBankMenuItems + kBankMenuBottomPad;
		int x = anchor_x;
		int y = anchor_y;
		// Keep the menu fully on-screen even when the click is near an edge.
		if (x + kBankMenuW > kWinW) x = kWinW - kBankMenuW;
		if (y + h > kWinH) y = kWinH - h;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		return BankMenuGeom{ x, y, kBankMenuW, h, kBankMenuItemH, y + kBankMenuHeaderH };
	}

	const char* kBankMenuLabels[kBankMenuItems] = {
		"New", "Clear", "Export RTI", "Import RTI",
	};
} // namespace

void Gui::DrawBankMenu(SDL_Renderer* r, const GuiState& s)
{
	BankMenuGeom g = ComputeBankMenuGeom(s.bank_menu_x, s.bank_menu_y);

	FillRect(r, kPanel, g.x, g.y, g.w, g.h);
	OutlineRect(r, kBorder, g.x, g.y, g.w, g.h);

	// Header strip identifying which slot the menu operates on.
	char title[32];
	std::snprintf(title, sizeof(title), "Slot %02d", s.bank_menu_slot);
	DrawText(r, kTextDim, g.x + 8, g.y + 6, title);
	SetCol(r, kBorder);
	SDL_RenderLine(r, (float)(g.x + 2), (float)(g.y + 18),
		(float)(g.x + g.w - 2), (float)(g.y + 18));

	// Clear and Export need an occupied slot; grey them out when empty so
	// the user can see the option exists but isn't currently applicable.
	bool slot_used = s.bank && s.bank_menu_slot >= 0 &&
		s.bank_menu_slot < Bank::SLOT_COUNT &&
		s.bank->At(s.bank_menu_slot).used;

	// Which item the mouse is currently over (or -1 if outside the rows).
	int hover = -1;
	if (s.mouse_x >= g.x && s.mouse_x < g.x + g.w &&
		s.mouse_y >= g.items_top &&
		s.mouse_y < g.items_top + kBankMenuItems * g.item_h) {
		hover = (s.mouse_y - g.items_top) / g.item_h;
	}

	for (int i = 0; i < kBankMenuItems; ++i) {
		int iy = g.items_top + i * g.item_h;
		bool disabled = !slot_used && (i == 1 /*Clear*/ || i == 2 /*Export*/);
		bool hovered = (i == hover);
		if (hovered) {
			// Subtle accent strip under the hovered row; leaves the menu
			// border untouched at the very edges.
			FillRect(r, kAccent, g.x + 1, iy, g.w - 2, g.item_h);
		}
		Col col;
		if (disabled)     col = kBorder;          // dim = disabled
		else if (hovered) col = kBg;              // dark text on the accent strip
		else              col = kText;
		DrawText(r, col, g.x + 10, iy + (g.item_h - 8) / 2, kBankMenuLabels[i]);
	}
}

Gui::BankMenuItem Gui::BankMenuItemAtLogical(int x, int y, int menu_x, int menu_y) const
{
	BankMenuGeom g = ComputeBankMenuGeom(menu_x, menu_y);
	if (x < g.x || x >= g.x + g.w) return BankMenuItem::None;
	if (y < g.items_top) return BankMenuItem::None;
	int idx = (y - g.items_top) / g.item_h;
	if (idx < 0 || idx >= kBankMenuItems) return BankMenuItem::None;
	switch (idx) {
		case 0: return BankMenuItem::New;
		case 1: return BankMenuItem::Clear;
		case 2: return BankMenuItem::Export;
		case 3: return BankMenuItem::Import;
	}
	return BankMenuItem::None;
}

bool Gui::PointInBankMenu(int x, int y, int menu_x, int menu_y) const
{
	BankMenuGeom g = ComputeBankMenuGeom(menu_x, menu_y);
	return x >= g.x && x < g.x + g.w && y >= g.y && y < g.y + g.h;
}

// ---------- Directory-tree right-click context menu ----------
//
// Same layout idiom as the bank menu (single anchor passed in, padded
// rows, hover-row highlight) but no slot header and a much shorter item
// list. Currently the only entry is "Add to bank" so the menu is one
// row tall plus the standard top/bottom padding.

namespace {
	struct TreeMenuGeom { int x, y, w, h, item_h, items_top, items; };
	constexpr int kTreeMenuW = 168;
	constexpr int kTreeMenuItemH = 22;
	constexpr int kTreeMenuTopPad = 4;
	constexpr int kTreeMenuBottomPad = 4;

	// Two base items + an optional "Clear override" row when the file already
	// has a manual override.
	int TreeMenuItemCount(bool has_override) { return has_override ? 3 : 2; }

	TreeMenuGeom ComputeTreeMenuGeom(int anchor_x, int anchor_y, bool has_override)
	{
		int items = TreeMenuItemCount(has_override);
		int h = kTreeMenuTopPad + kTreeMenuItemH * items + kTreeMenuBottomPad;
		int x = anchor_x;
		int y = anchor_y;
		if (x + kTreeMenuW > kWinW) x = kWinW - kTreeMenuW;
		if (y + h > kWinH) y = kWinH - h;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		return TreeMenuGeom{ x, y, kTreeMenuW, h, kTreeMenuItemH,
							 y + kTreeMenuTopPad, items };
	}

	const char* TreeMenuLabel(int i, bool has_override)
	{
		if (i == 0) return "Add to bank";
		if (i == 1) return "Override category " "\x1A";   // arrow glyph hint
		if (i == 2 && has_override) return "Clear override";
		return "";
	}

	// ---- Category picker (chained from "Override category...") -------------
	struct CatPickerGeom { int x, y, w, h, item_h, items_top, items; };
	constexpr int kCatPickerW = 180;
	constexpr int kCatPickerItemH = 20;
	constexpr int kCatPickerTopPad = 4;
	constexpr int kCatPickerBottomPad = 4;
	// One row per category + a final "Clear override" row.
	int CatPickerItemCount() { return (int)Analysis::Category::COUNT + 1; }

	CatPickerGeom ComputeCatPickerGeom(int anchor_x, int anchor_y)
	{
		int items = CatPickerItemCount();
		int h = kCatPickerTopPad + kCatPickerItemH * items + kCatPickerBottomPad;
		int x = anchor_x;
		int y = anchor_y;
		if (x + kCatPickerW > kWinW) x = kWinW - kCatPickerW;
		if (y + h > kWinH) y = kWinH - h;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		return CatPickerGeom{ x, y, kCatPickerW, h, kCatPickerItemH,
							  y + kCatPickerTopPad, items };
	}
} // namespace

void Gui::DrawTreeMenu(SDL_Renderer* r, const GuiState& s)
{
	bool has_ov = s.tree_menu_has_override;
	TreeMenuGeom g = ComputeTreeMenuGeom(s.tree_menu_x, s.tree_menu_y, has_ov);

	FillRect(r, kPanel, g.x, g.y, g.w, g.h);
	OutlineRect(r, kBorder, g.x, g.y, g.w, g.h);

	int hover = -1;
	if (s.mouse_x >= g.x && s.mouse_x < g.x + g.w &&
		s.mouse_y >= g.items_top &&
		s.mouse_y < g.items_top + g.items * g.item_h) {
		hover = (s.mouse_y - g.items_top) / g.item_h;
	}

	for (int i = 0; i < g.items; ++i) {
		int iy = g.items_top + i * g.item_h;
		bool hovered = (i == hover);
		if (hovered) FillRect(r, kAccent, g.x + 1, iy, g.w - 2, g.item_h);
		Col col = hovered ? kBg : kText;
		DrawText(r, col, g.x + 10, iy + (g.item_h - 8) / 2,
			TreeMenuLabel(i, has_ov));
	}
}

Gui::TreeMenuItem Gui::TreeMenuItemAtLogical(int x, int y, int menu_x, int menu_y,
	bool has_override) const
{
	TreeMenuGeom g = ComputeTreeMenuGeom(menu_x, menu_y, has_override);
	if (x < g.x || x >= g.x + g.w) return TreeMenuItem::None;
	if (y < g.items_top) return TreeMenuItem::None;
	int idx = (y - g.items_top) / g.item_h;
	if (idx < 0 || idx >= g.items) return TreeMenuItem::None;
	switch (idx) {
		case 0: return TreeMenuItem::AddToBank;
		case 1: return TreeMenuItem::OverrideSubmenu;
		case 2: return has_override ? TreeMenuItem::ClearOverride : TreeMenuItem::None;
	}
	return TreeMenuItem::None;
}

bool Gui::PointInTreeMenu(int x, int y, int menu_x, int menu_y,
	bool has_override) const
{
	TreeMenuGeom g = ComputeTreeMenuGeom(menu_x, menu_y, has_override);
	return x >= g.x && x < g.x + g.w && y >= g.y && y < g.y + g.h;
}

// ---- Category picker ----------------------------------------------------

void Gui::DrawCategoryPicker(SDL_Renderer* r, const GuiState& s)
{
	CatPickerGeom g = ComputeCatPickerGeom(s.cat_picker_x, s.cat_picker_y);

	FillRect(r, kPanel, g.x, g.y, g.w, g.h);
	OutlineRect(r, kBorder, g.x, g.y, g.w, g.h);

	int hover = -1;
	if (s.mouse_x >= g.x && s.mouse_x < g.x + g.w &&
		s.mouse_y >= g.items_top &&
		s.mouse_y < g.items_top + g.items * g.item_h) {
		hover = (s.mouse_y - g.items_top) / g.item_h;
	}

	int nCat = (int)Analysis::Category::COUNT;
	for (int i = 0; i < g.items; ++i) {
		int iy = g.items_top + i * g.item_h;
		bool hovered = (i == hover);
		if (hovered) FillRect(r, kAccent, g.x + 1, iy, g.w - 2, g.item_h);
		const char* label;
		if (i < nCat) label = Analysis::Name((Analysis::Category)i);
		else          label = "Clear override (auto)";
		Col col;
		if (hovered)             col = kBg;
		else if (i < nCat)       col = CategoryColour(i);
		else                     col = kTextDim;
		DrawText(r, col, g.x + 10, iy + (g.item_h - 8) / 2, label);
	}
}

int Gui::CategoryPickerItemAtLogical(int x, int y, int menu_x, int menu_y) const
{
	CatPickerGeom g = ComputeCatPickerGeom(menu_x, menu_y);
	if (x < g.x || x >= g.x + g.w) return -2;
	if (y < g.items_top) return -2;
	int idx = (y - g.items_top) / g.item_h;
	if (idx < 0 || idx >= g.items) return -2;
	int nCat = (int)Analysis::Category::COUNT;
	return (idx < nCat) ? idx : -1;   // -1 == "clear override" row
}

bool Gui::PointInCategoryPicker(int x, int y, int menu_x, int menu_y) const
{
	CatPickerGeom g = ComputeCatPickerGeom(menu_x, menu_y);
	return x >= g.x && x < g.x + g.w && y >= g.y && y < g.y + g.h;
}

namespace {

	// One page of help content. `title` is the page name; `rows` is a
	// (keys/heading, description) list. If `desc` is empty the row is treated
	// as a section heading drawn in highlight; if `keys` is empty too the row
	// is just blank vertical space.
	struct HelpRow { const char* keys; const char* desc; };
	struct HelpPage { const char* title; const HelpRow* rows; int count; };

	// ----- Page 1: Keybindings (the original single-page content). -----
	const HelpRow kHelpPage1[] = {
		{ "Browsing & playback", "" },
		{ "a .. z, 0 .. 9", "Play current instrument at chromatic pitches" },
		{ "[  /  ]",        "Octave shift down / up" },
		{ "Left / Right",   "Previous / next .RTI (hold to repeat)" },
		{ "Up / Down",      "Previous / next .RTI (hold to repeat)" },
		{ "Mouse wheel",    "Move selection quickly (3 at a time)" },
		{ "PageUp / PageDn","Jump by 10 files" },
		{ "Home / End",     "First / last file" },
		{ "/",              "Open the search bar (see page 3)" },
		{ "Enter",          "Toggle current file's folder (collapse/expand)" },
		{ "Esc",            "Silence playback (or close this help)" },
		{ "", "" },
		{ "Bank", "" },
		{ "+  (or =)",      "Add current instrument to bank (dedupes by sound)" },
		{ "-",              "Remove current instrument from bank" },
		{ "Click / Tab",    "Select a bank slot (click filled slot loads it)" },
		{ "Ctrl+arrows",    "Move the bank selection cursor" },
		{ "Ctrl+a-z/0-9",   "Sample (play) the selected bank slot" },
		{ "Ctrl+Ins / Del", "Copy current into / delete selected slot (confirm)" },
		{ "Right-click slot","Open the bank context menu (New/Clear/Export/Import)" },
		{ "", "" },
		{ "Editing  (F6 toggles Edit mode)", "" },
		{ "Click a field",  "Jump the edit cursor onto that parameter / cell / name" },
		{ "Right-click",    "Toggle a binary field (AUDCTL / type / filter)" },
		{ "Tab / arrows",   "(edit) cycle panel / move cell cursor" },
		{ "0-9 A-F",        "(edit) type a value; +/- or Shift+Up/Dn to nudge" },
		{ "Ctrl+key",       "(edit) audition while editing; Space re-triggers" },
		{ "Ctrl+Z / Ctrl+Y","Undo / redo" },
		{ "Ctrl+S",         "Export the current instrument as a new .RTI" },
		{ "", "" },
		{ "Files & display", "" },
		{ "Drag & drop",    "Drop a folder or .RTI onto the window to load" },
		{ "F1",             "Toggle this help (Left/Right = pages, Esc = close)" },
		{ "F2 / F3 / F4",   "Save bank / Load bank / Switch library" },
		{ "F5",             "Toggle PAL / NTSC" },
		{ "F11",            "Toggle fullscreen" },
	};

	// ----- Page 2: Categories, tags, confidence, colours, overrides. -----
	const HelpRow kHelpPage2[] = {
		{ "What analysis does", "" },
		{ "F7", "Re-analyse the library: classify every .RTI, find duplicates," },
		{ "",   "render each through the engine for audio features, and cluster" },
		{ "",   "by sonic similarity. Cached to analysis.json (portable - lives" },
		{ "",   "next to your instruments folder)." },
		{ "", "" },
		{ "Categories  (Cat: ... in the header)", "" },
		{ "",   "Each file is placed in ONE bucket. The 15 categories are:" },
		{ "",   "Bass / Lead / Lead (vibrato) / Arp / Chord / Glide / Pad / Bell /" },
		{ "",   "Kick / Snare / HiHat / Perc / Swept FX / Noise FX / Other." },
		{ "",   "Directory rows are tinted by category so you can see them at a" },
		{ "",   "glance. Selection still reads white-on-blue." },
		{ "", "" },
		{ "Confidence  (conf N)", "" },
		{ "",   "How many independent signals voted for the category (0-5)." },
		{ "",   "1 = a guess - those rows are dimmed in the tree so you can spot" },
		{ "",   "the analyser's weak calls and override them." },
		{ "", "" },
		{ "Tags  (orthogonal sub-labels)", "" },
		{ "",   "A file can carry several tags alongside its category:" },
		{ "",   "  vibrato     PAR_VIBRATO > 0" },
		{ "",   "  bright/dark spectral centroid above 4.5k / below 1.5k Hz" },
		{ "",   "  loud/quiet  mean RMS above 0.20 / below 0.05" },
		{ "",   "  animated    spectral flux > 0.20 (timbre moves over time)" },
		{ "",   "  highfreq    AUDCTL 15kHz / 1.79MHz bit set" },
		{ "",   "  ascending / descending   note-table direction" },
		{ "", "" },
		{ "Audio features  (Cen / Rll / Atk / Mid / End / ZCR / Pk / Flx)", "" },
		{ "",   "Cen / Rll  spectral centroid + 85% roll-off (kHz; brightness)" },
		{ "",   "Atk/Mid/End  RMS in the first/middle/last third of the render" },
		{ "",   "ZCR  zero-crossing rate (high = noisy)" },
		{ "",   "Pk   peak-amplitude position (0 = attack, 1 = swell)" },
		{ "",   "Flx  spectral flux (timbre motion frame-to-frame)" },
		{ "", "" },
		{ "Manual override", "" },
		{ "Right-click row", "'Override category...' opens a colour-coded picker" },
		{ "Ctrl+R",         "Cycle the current file's manual category" },
		{ "Ctrl+Shift+R",   "Clear every manual override in the library" },
		{ "",               "Overridden rows show an 'M' marker; the header reads" },
		{ "",               "'Cat: <name> [M]'. Persisted in analysis.json." },
	};

	// ----- Page 3: Search syntax + examples. -----
	const HelpRow kHelpPage3[] = {
		{ "Opening the search bar", "" },
		{ "/", "Start searching. Type to filter, Enter to keep the filter," },
		{ "",  "Esc to clear and return to the full list. Arrow keys still move" },
		{ "",  "between matches while the search bar is focused." },
		{ "", "" },
		{ "Substring search  (default)", "" },
		{ "",  "Type anything that doesn't start with '@' and the directory" },
		{ "",  "filters to instruments whose filename contains that text," },
		{ "",  "case-insensitive." },
		{ "Examples", "" },
		{ "kick",        "matches  Bouncy_kick.rti,  kick_low.rti,  ..." },
		{ "lead pwm",    "(no match - the space is literal text in the filename)" },
		{ "vinscool",    "matches every file with 'vinscool' anywhere in its name" },
		{ "", "" },
		{ "Tag search  (@tag)", "" },
		{ "",  "Prefix the query with '@' to filter by audio tags (see page 2)" },
		{ "",  "instead of filenames. Combine tags with commas for AND match -" },
		{ "",  "every listed tag must be set on the file." },
		{ "Examples", "" },
		{ "@bright",         "all bright-centroid instruments" },
		{ "@bright,loud",    "bright AND loud" },
		{ "@vibrato",        "every Lead with vibrato (and any other vibrato sound)" },
		{ "@animated",       "FX whose timbre moves over time" },
		{ "@dark,quiet",     "dark + quiet (ambient pads, soft basses)" },
		{ "@ascending",      "everything with a rising note-table pattern" },
		{ "", "" },
		{ "Recognised tag tokens", "" },
		{ "",  "vibrato  bright  dark  loud  quiet  animated  highfreq" },
		{ "",  "ascending  descending" },
	};

	// ----- Page 4: Clusters & spectral panel. -----
	const HelpRow kHelpPage4[] = {
		{ "What clusters are for", "" },
		{ "",  "Categories tell you WHAT KIND of instrument something is. Clusters" },
		{ "",  "tell you WHICH OTHERS it sounds like - based on the rendered audio," },
		{ "",  "not on parameters or filename. Two files in the same cluster share" },
		{ "",  "an overall sonic character." },
		{ "", "" },
		{ "How clustering works", "" },
		{ "",  "k-means runs over an 8-dimensional feature vector per file (RMS" },
		{ "",  "profile + ZCR + peak position + centroid + rolloff + flux). The" },
		{ "",  "values are standardised first so no single dimension dominates." },
		{ "",  "k-means++ seeding makes the result deterministic." },
		{ "",  "k = ceil(sqrt(N/2)) clamped to [3, 12] by default." },
		{ "", "" },
		{ "Switching to cluster view", "" },
		{ "F10",         "Toggle Clusters view (or click the 'Clusters' tab)" },
		{ "",            "Clusters are expanded by default - scroll straight through" },
		{ "",            "and you'll hear how similar the adjacent rows are." },
		{ "",            "Within each cluster, rows are sorted by spectral centroid" },
		{ "",            "(brightness) ascending - dark sounds first, bright last." },
		{ "Ctrl+]  /  Ctrl+[", "Step the k-means cluster count for the next F7" },
		{ "",                  "(0 = automatic). The notice bar shows the active value." },
		{ "", "" },
		{ "Spectral signature panel", "" },
		{ "",  "In Clusters view, a small bar chart appears at the bottom of the" },
		{ "",  "tree above the search bar. Each of the 8 bars is one audio feature" },
		{ "",  "of the currently-selected instrument, height = normalised value." },
		{ "",  "Bars are tinted with the file's category colour, so the panel" },
		{ "",  "doubles as a colour-coded confirmation of the row tint." },
		{ "Bars (left -> right)", "" },
		{ "",  "Atk / Mid / End  RMS profile (early / mid / late thirds)" },
		{ "",  "ZCR  zero-crossing rate (noisiness)" },
		{ "",  "Pk   peak-amplitude position (0 = attack, 1 = swell)" },
		{ "",  "Cen  spectral centroid (brightness, Hz / 22050)" },
		{ "",  "Rll  spectral 85% roll-off (Hz / 22050)" },
		{ "",  "Flx  spectral flux (how much the timbre changes)" },
	};

	const HelpPage kHelpPages[] = {
		{ "Keybindings",            kHelpPage1, (int)(sizeof(kHelpPage1) / sizeof(kHelpPage1[0])) },
		{ "Categories & analysis",  kHelpPage2, (int)(sizeof(kHelpPage2) / sizeof(kHelpPage2[0])) },
		{ "Search syntax",          kHelpPage3, (int)(sizeof(kHelpPage3) / sizeof(kHelpPage3[0])) },
		{ "Clusters & spectral view", kHelpPage4, (int)(sizeof(kHelpPage4) / sizeof(kHelpPage4[0])) },
	};

} // namespace

namespace {

	// Shared geometry for the help overlay. Sized to fit in the 1280x720
	// canvas with 16 px breathing room on all sides. The Prev / Next paging
	// buttons live in the bottom-right corner; the renderer and the hit-test
	// both compute their rects through this helper so they can't drift.
	struct HelpGeom {
		int x, y, w, h;          // outer panel
		int row_h;
		int content_top, content_left, content_right;
		int desc_x;              // x-coord of the description column
		int footer_y;            // baseline of the footer hint line
		// Bottom-right paging buttons.
		int btn_w, btn_h;
		int prev_x, prev_y;
		int next_x, next_y;
	};
	HelpGeom ComputeHelpGeom()
	{
		HelpGeom g;
		g.w = 880;
		g.h = 688;                  // fits in 720 with 16 px margin top/bottom
		g.x = (kWinW - g.w) / 2;
		g.y = (kWinH - g.h) / 2;
		g.row_h = 13;               // 8 px font + 5 px spacing
		g.content_top = g.y + 48;
		g.content_left = g.x + 28;
		g.desc_x = g.x + 260;
		g.content_right = g.x + g.w - 20;
		g.footer_y = g.y + g.h - 22;
		g.btn_w = 64;
		g.btn_h = 18;
		g.next_x = g.x + g.w - 20 - g.btn_w;
		g.prev_x = g.next_x - g.btn_w - 8;
		g.next_y = g.footer_y - 4;
		g.prev_y = g.next_y;
		return g;
	}

} // namespace

void Gui::DrawHelpOverlay(SDL_Renderer* r, const GuiState& s)
{
	// Dim the whole screen.
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	FillRect(r, Col{ 0, 0, 0, 180 }, 0, 0, kWinW, kWinH);

	HelpGeom g = ComputeHelpGeom();
	int total_pages = kHelpPageCount;
	int page = std::clamp(s.help_page, 0, total_pages - 1);
	const HelpPage& hp = kHelpPages[page];

	FillRect(r, Col{ 30, 34, 44, 245 }, g.x, g.y, g.w, g.h);
	OutlineRect(r, kAccent, g.x, g.y, g.w, g.h);
	OutlineRect(r, kAccent, g.x + 1, g.y + 1, g.w - 2, g.h - 2);

	// Title + page indicator.
	char title[128];
	std::snprintf(title, sizeof(title), "PokeyForge  -  %s", hp.title);
	DrawText(r, kHighlight, g.x + 20, g.y + 18, title);
	char pageInfo[40];
	std::snprintf(pageInfo, sizeof(pageInfo), "Page %d / %d", page + 1, total_pages);
	int piw = (int)std::strlen(pageInfo) * kGlyphW;
	DrawText(r, kTextDim, g.content_right - piw, g.y + 18, pageInfo);

	SetCol(r, kBorder);
	SDL_RenderLine(r, (float)(g.x + 20), (float)(g.y + 34),
		(float)(g.content_right), (float)(g.y + 34));

	// Content rows. Empty key+desc = blank line; non-empty key with empty
	// desc = section heading. Rows that overflow the available height are
	// silently clipped (shouldn't happen with the current page sizes).
	int y = g.content_top;
	int max_y = g.footer_y - 18;
	for (int i = 0; i < hp.count && y + g.row_h <= max_y; ++i) {
		const HelpRow& row = hp.rows[i];
		bool has_key = row.keys && row.keys[0];
		bool has_desc = row.desc && row.desc[0];
		if (!has_key && !has_desc) { y += g.row_h; continue; }
		if (has_key && !has_desc) {
			DrawText(r, kOrange, g.x + 20, y, row.keys);
		}
		else {
			DrawText(r, kHighlight, g.content_left, y, has_key ? row.keys : "");
			DrawText(r, kText, g.desc_x, y, row.desc);
		}
		y += g.row_h;
	}

	// Footer divider + hint text on the left, paging buttons on the right.
	SetCol(r, kBorder);
	SDL_RenderLine(r, (float)(g.x + 20), (float)(g.footer_y - 6),
		(float)(g.content_right), (float)(g.footer_y - 6));
	DrawText(r, kTextDim, g.x + 20, g.footer_y,
		"Left / Right  page    Esc / F1  close    click outside  dismiss");

	// Clickable Prev / Next buttons. Hover highlight matches the rest of
	// the modal popups (kAccent fill + kBg text on hover).
	auto draw_btn = [&](int bx, int by, const char* label) {
		bool hovered = (s.mouse_x >= bx && s.mouse_x < bx + g.btn_w &&
			s.mouse_y >= by && s.mouse_y < by + g.btn_h);
		FillRect(r, hovered ? kAccent : kPanelDark, bx, by, g.btn_w, g.btn_h);
		OutlineRect(r, kAccent, bx, by, g.btn_w, g.btn_h);
		int tw = (int)std::strlen(label) * kGlyphW;
		DrawText(r, hovered ? kBg : kHighlight,
			bx + (g.btn_w - tw) / 2, by + (g.btn_h - 8) / 2, label);
	};
	draw_btn(g.prev_x, g.prev_y, "< Prev");
	draw_btn(g.next_x, g.next_y, "Next >");

	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

int Gui::HelpPageButtonAtLogical(int x, int y) const
{
	HelpGeom g = ComputeHelpGeom();
	if (y < g.prev_y || y >= g.prev_y + g.btn_h) return 0;
	if (x >= g.prev_x && x < g.prev_x + g.btn_w) return -1;
	if (x >= g.next_x && x < g.next_x + g.btn_w) return +1;
	return 0;
}

bool Gui::PointInHelpPanel(int x, int y) const
{
	HelpGeom g = ComputeHelpGeom();
	return x >= g.x && x < g.x + g.w && y >= g.y && y < g.y + g.h;
}
