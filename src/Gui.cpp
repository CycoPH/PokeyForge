#include "Gui.h"
#include "Analysis.h"
#include "TextRenderer.h"
#include "Version.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

	// File-scope TextRenderer pointer set at the start of Gui::Render().
	// All anonymous-namespace draw helpers route through this so the TTF font
	// is used when available, with SDL_RenderDebugText as fallback.
	static TextRenderer* g_tr = nullptr;

	// 1280x720 layout. All measurements in pixels.
	constexpr int kWinW = 1280;
	constexpr int kWinH = 720;
	constexpr int kHeaderH = 32;
	constexpr int kCmdBarH = 22;
	constexpr int kSearchH = 24;   // search bar above the command bar (main column)
	constexpr int kTreeW = 320;
	constexpr int kMainX = kTreeW;
	constexpr int kMainW = kWinW - kTreeW;

	// Instrument panel layout constants (chained so changing one shifts everything below it).
	// kHeaderH=32 → InstrHdr starts at 34, Parameters at 72, Envelope at 214, NoteTable at 416, Bank at 506.
	constexpr int kInstrHdrY  = kHeaderH + 2;                           // = 34
	constexpr int kInstrHdrH  = 36;
	constexpr int kParamY0    = kInstrHdrY + kInstrHdrH + 2;            // = 72
	constexpr int kParamRowH  = 18;                                      // fits GlyphH≈16
	constexpr int kParamH     = 28 + 6 * kParamRowH + 4;                // = 140
	constexpr int kEnvY0      = kParamY0 + kParamH + 2;                 // = 214
	constexpr int kNoteTableY0 = kEnvY0 + 200 + 2;                      // = 416

	// Bank panel geometry, shared by the renderer and the mouse hit-test so they
	// never drift apart.
	struct BankGeom { int panel_x, panel_y, panel_w, panel_h; int gx, gy, cw, ch, pad; };

	BankGeom ComputeBankGeom()
	{
		int x0 = kMainX + 2;
		int y0 = kNoteTableY0 + 78 + 2;  // just after the NoteTable panel (max height=78)
		int w = kMainW - 4;
		int h = (kWinH - kCmdBarH - 4) - y0;
		constexpr int kCellPad = 2;          // reduced from 3: inner tile = ch-4 >= GlyphH
		int gx = x0 + (4 - kCellPad);
		int gy = y0 + 26;                    // grid starts 26px below panel top (underline at y0+24)
		int cw = (w - 2 * (4 - kCellPad)) / 8;
		int ch = (h - 34) / 8;              // h minus gy-offset(26) minus bottom-margin(8)
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
		if (g_tr && g_tr->Ok()) {
			g_tr->DrawText(r, SDL_Color{c.r, c.g, c.b, c.a}, x, y, s);
		} else {
			SetCol(r, c);
			SDL_RenderDebugText(r, (float)x, (float)y, s);
		}
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

	// Debug font is a fixed 8px per glyph; when TTF is available use its advance.
	int GlyphW() { return (g_tr && g_tr->Ok()) ? g_tr->CharWidth() : 8; }
	// Line height for vertical centering: box_y + (box_h - GlyphH()) / 2
	int GlyphH() { return (g_tr && g_tr->Ok()) ? g_tr->LineHeight() : 8; }
	constexpr int kGlyphW = 8;  // kept for existing layout math that hasn't switched yet

	// Section title with an underline.
	void DrawSectionTitle(SDL_Renderer* r, int x, int y, int w, const char* label)
	{
		DrawText(r, kHighlight, x, y, label);
		SetCol(r, kBorder);
		int lineY = y + GlyphH() + 2;
		SDL_RenderLine(r, static_cast<float>(x), static_cast<float>(lineY), static_cast<float>(x + w), static_cast<float>(lineY));
	}

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
	// Clip the click to the actual visible-rows region. Without this a
	// click inside the spectral-signature panel (Cluster view) - which
	// sits below the rows - would be mapped to a phantom row index and
	// select an instrument the user can't see, possibly past the end of
	// the list entirely.
	const int rows_bottom = m_tree_top + m_tree_visible_rows * m_tree_rowh;
	if (y >= rows_bottom) return -1;
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
	g_tr = m_text_renderer;   // make the TTF renderer available to all helpers
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
	if (m_vol_popup_open)  DrawVolPopup(r, s);
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
	const int gh_cd = GlyphH();
	const int msg_y1 = py + 18 + gh_cd + 10;
	const int msg_y2 = msg_y1 + gh_cd + 4;
	if ((int)msg.size() <= max_chars) {
		DrawText(r, kText, px + 20, msg_y1, msg.c_str());
	}
	else {
		int cut = max_chars;
		while (cut > 0 && msg[cut] != ' ') --cut;
		if (cut == 0) cut = max_chars;
		DrawText(r, kText, px + 20, msg_y1, msg.substr(0, cut).c_str());
		DrawText(r, kText, px + 20, msg_y2, msg.substr(cut + 1).c_str());
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
			bx + (b.w - tw) / 2, by + std::max(0, (kConfirmBtnH - gh_cd) / 2), b.label);
	}
	DrawText(r, kTextDim, px + 320, by + std::max(0, (kConfirmBtnH - gh_cd) / 2), "(Enter = Yes, Esc = No)");

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
		DrawText(r, kHighlight, x - 32 + 4, y + std::max(0, (h - GlyphH()) / 2), nbuf);
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
	// Height is derived from the font so all 3 lines always fit.
	const int gh_eb = GlyphH();
	const int line_gap = gh_eb + 4;
	int h = 6 + 3 * line_gap + 6;   // top_pad + 3 lines + bottom_pad
	int y = kWinH - kCmdBarH - h;
	int x = kMainX + 4;
	int w = kWinW - x - 4;

	FillRect(r, Col{ 40, 36, 22 }, x, y, w, h);   // warm tint = editing
	OutlineRect(r, kOrange, x, y, w, h);

	Editor::FieldInfo fi = s.editor->Describe(*s.instrument);

	// Line 1: where the cursor is and the current value.
	DrawText(r, kHighlight, x + 10, y + 6, "EDITING");
	char line1[200];
	if (fi.value >= 0) {
		// Append distortion short name when in the Distortion envelope row.
		const char* dist_name = "";
		if (std::string(fi.panel) == "ENVELOPE" &&
			fi.field.find("Dist") != std::string::npos) {
			// Short labels indexed by (value >> 1), matching RMT distortion table.
			static const char* kDistShort[8] = {
				"WhiteNoise", "Poly5", "Poly4+5", "16-Bit",
				"Poly17/9" , "Pure" , "BuzzyBass", "GrittyBass"
			};
			dist_name = kDistShort[(std::clamp(fi.value & 0xFE, 0, 14)) >> 1];
		}
		// Append command short name when in the Command envelope row.
		const char* cmd_name = "";
		if (std::string(fi.panel) == "ENVELOPE" &&
			fi.field.find("Cmd") != std::string::npos) {
			static const char* kCmdNames[8] = {
				"Note+XY", "Freq XY", "Note+FreqXY", "Shift Note",
				"Shift FShift", "Portamento", "Filter/Bass", "AUDCTL"
			};
			cmd_name = kCmdNames[std::clamp(fi.value, 0, 7)];
		}
		const char* tag = *dist_name ? dist_name : (*cmd_name ? cmd_name : nullptr);
		if (tag) {
			std::snprintf(line1, sizeof(line1),
				"%s  >  %s   =   %02X (%d) \"%s\"   range %d-%d",
				fi.panel, fi.field.c_str(), fi.value & 0xFF, fi.value,
				tag, fi.vmin, fi.vmax);
		} else {
			std::snprintf(line1, sizeof(line1), "%s  >  %s   =   %02X (%d)   range %d-%d",
				fi.panel, fi.field.c_str(), fi.value & 0xFF, fi.value,
				fi.vmin, fi.vmax);
		}
	}
	else {
		std::snprintf(line1, sizeof(line1), "%s  >  %s", fi.panel, fi.field.c_str());
	}
	DrawText(r, kText, x + 90, y + 6, line1);

	// Line 2: option pills (small-range params) or help text.
	if (fi.options && fi.value >= 0) {
		// Draw labelled pills; active value highlighted.
		int px = x + 10;
		const int pill_h = gh_eb + 4;
		const int line2_y = y + 6 + line_gap;
		const int line3_y = line2_y + pill_h + 4;
		for (int i = 0; fi.options[i]; ++i) {
			bool active = (fi.value == i);
			int pw = (int)std::strlen(fi.options[i]) * kGlyphW + 10;
			FillRect(r, active ? kAccent : Col{ 55, 55, 55 }, px, line2_y, pw, pill_h);
			OutlineRect(r, active ? kHighlight : kBorder, px, line2_y, pw, pill_h);
			DrawText(r, active ? kBg : kText, px + 5, line2_y + std::max(0, (pill_h - gh_eb) / 2), fi.options[i]);
			px += pw + 6;
		}
		// Help text after pills.
		DrawText(r, kTextDim, x + 10, line3_y, fi.help);
	} else {
		// Line 2: what the field does.
		DrawText(r, kTextDim, x + 10, y + 6 + line_gap, fi.help);

		// Line 3: the controls.
		DrawText(r, kFolder, x + 10, y + 6 + line_gap * 2,
			"Tab panel  arrows move  0-9 A-F set  +/- or Shift+Up/Dn nudge  "
			"click toggle  Ctrl+key play  Space replay  F6 exit");
	}
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
	const int gh_pr = GlyphH();
	const int pr_y1 = py + 18 + gh_pr + 10;
	const int pr_y2 = pr_y1 + gh_pr + 4;
	DrawText(r, kText, px + 20, pr_y1,
		"You have edited this instrument. Keep the change in the bank");
	DrawText(r, kText, px + 20, pr_y2, "before switching?");

	int by = py + kPromptH - kPromptBtnH - 14;
	for (const auto& b : kPromptBtns) {
		int bx = px + b.dx;
		bool hovered = (s.mouse_x >= bx && s.mouse_x < bx + b.w &&
			s.mouse_y >= by && s.mouse_y < by + kPromptBtnH);
		FillRect(r, hovered ? kAccent : kPanelDark, bx, by, b.w, kPromptBtnH);
		OutlineRect(r, kAccent, bx, by, b.w, kPromptBtnH);
		int tw = (int)std::char_traits<char>::length(b.label) * kGlyphW;
		DrawText(r, hovered ? kBg : kHighlight,
			bx + (b.w - tw) / 2, by + std::max(0, (kPromptBtnH - gh_pr) / 2), b.label);
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

	const int ty_txt = y + std::max(0, (h - GlyphH()) / 2);
	DrawText(r, kTextDim, x + 6, ty_txt, "Find:");
	int tx = x + 6 + 5 * kGlyphW + 4;
	if (s.search_active || !s.search_query.empty()) {
		std::string q = Truncate(s.search_query, (x + w - tx - kGlyphW) / kGlyphW);
		DrawText(r, kText, tx, ty_txt, q.c_str());
		if (s.search_active) {
			int caret = tx + (int)q.size() * kGlyphW;
			FillRect(r, kCellCursor, caret, y + 3, kGlyphW, h - 6);
		}
	}
	else {
		DrawText(r, kTextDim, tx, ty_txt, "/ to search");
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
	const int kCmdTxtY = y + std::max(0, (kCmdBarH - GlyphH()) / 2);
	if (!s.notice.empty()) {
		DrawText(r, kHighlight, 8, kCmdTxtY, s.notice.c_str());
		return;
	}

	DrawText(r, kTextDim, 8, kCmdTxtY, "F2 Save  F3 Load  F4 Lib  F7 Analyse  F8 Group  F9 Dupes");

	if (!s.library_path.empty()) {
		std::string lib = "Library: " + s.library_path;
		int max_chars = (kWinW - kWinW/2) / kGlyphW;
		// Show the tail of long paths (more informative than the head).
		if ((int)lib.size() > max_chars && max_chars > 4) {
			lib = "Library: ~" + s.library_path.substr(s.library_path.size() - (size_t)(max_chars - 10));
		}
		DrawText(r, kTextDim, (kWinW - kWinW / 2), kCmdTxtY, lib.c_str());
	}
}

void Gui::DrawHeader(SDL_Renderer* r, const GuiState& s)
{
	FillRect(r, kPanel, 0, 0, kWinW, kHeaderH);

	// Clickable menu bar with hover highlight. Matches the accent fill /
	// inverted text scheme used by the bank context menu and the help
	// page-buttons, so a hovered menu item looks the same wherever it
	// appears in the UI.
	const int kMenuTxtY = kMenuY + std::max(0, (kMenuH - GlyphH()) / 2);
	for (const auto& b : kMenu) {
		const bool hovered =
			s.mouse_x >= b.x && s.mouse_x < b.x + b.w &&
			s.mouse_y >= kMenuY && s.mouse_y < kMenuY + kMenuH;
		FillRect(r, hovered ? kAccent : kPanelDark, b.x, kMenuY, b.w, kMenuH);
		OutlineRect(r, hovered ? kAccent : kBorder, b.x, kMenuY, b.w, kMenuH);
		DrawText(r, hovered ? kBg : kHighlight, b.x + 8, kMenuTxtY, b.label);
	}

	// Status on the right (kept left of the scope in the top-right corner).
	const int kHdrTxtY = std::max(0, (kHeaderH - GlyphH()) / 2);
	int used = s.bank ? s.bank->UsedCount() : 0;
	DrawTextF(r, kText, 548, kHdrTxtY, "Clock: %s", s.ntsc ? "NTSC 60Hz" : "PAL 50Hz");
	DrawTextF(r, kText, 690, kHdrTxtY, "Oct: %+d", s.octave_shift);
	DrawTextF(r, kText, 755, kHdrTxtY, "Bank: %02d/64", used);

	// [EDIT] and MODIFIED are pinned to the LEFT of the tone-number
	// readout that hugs the scope's top-left corner (see DrawMasterScope,
	// `#N` at scope_x - 28). With JetBrains Mono ptsize=13 the per-glyph
	// width grew enough that the previous fixed-x=916 MODIFIED label
	// extended past x=972 and overlaid the `#N` readout. Right-align both
	// labels against tone_x_left with explicit pixel gaps so they always
	// sit clear of the scope panel, regardless of the active font width.
	bool editing = s.editor && s.editor->active;
	const int gw = GlyphW();
	const int kScopeW = 274;                            // matches DrawMasterScope
	const int scope_x = kWinW - kScopeW - 6;
	const int tone_x  = scope_x - 28;                   // "#NNN" readout's left edge
	int pack_right = tone_x - 6;                        // 6 px gap to tone readout
	if (s.modified) {
		int w = (int)std::strlen("MODIFIED") * gw;
		int x = pack_right - w;
		DrawText(r, kOrange, x, kHdrTxtY, "MODIFIED");
		pack_right = x - 8;                             // 8 px gap to next label
	}
	if (editing) {
		int w = (int)std::strlen("[EDIT]") * gw;
		int x = pack_right - w;
		DrawText(r, kHighlight, x, kHdrTxtY, "[EDIT]");
	}

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
			int text_y = ty + std::max(0, (t.h - GlyphH()) / 2);
			DrawText(r, active ? kBg : kTextDim, text_x, text_y, t.label);
		}
	}
	SetCol(r, kBorder);
	SDL_RenderLine(r, 4, (float)(kHeaderH + kDirTabRelY + kDirTabH + 2),
		(float)(kTreeW - 4), (float)(kHeaderH + kDirTabRelY + kDirTabH + 2));

	if (!s.dir) return;
	const auto& rows = s.dir->Rows();
	int cur_node = s.dir->CurrentNodeIndex();

	const int kRowH = std::max(20, GlyphH() + 4);  // tall enough for the font + 2px padding
	constexpr int kScrollW = 14;     // scrollbar width (flush with right edge)
	constexpr int kScrollPad = 4;    // gap between rows and scrollbar
	constexpr int kRowRight = kTreeW - kScrollW - kScrollPad; // text right edge
	int top = kHeaderH + 26;
	// Reserve the bottom for the footer line, the search bar and command bar.
	int bottom = kWinH - kCmdBarH - kSearchH - (GlyphH() + 4);
	// In Cluster view we steal a strip from the bottom of the row list to
	// host the spectral signature panel (drawn below). The panel only
	// appears in this view because that's where the user is browsing by
	// similarity and the visual signature is most useful. Height scales
	// with the TTF font so the title + bar chart + label row + the small
	// margins always fit (an 8 px built-in font fits in 72 px, JetBrains
	// Mono at ptsize=13 reports GlyphH() ~ 17 and needs ~95 px).
	bool show_spectral = s.dir && s.dir->GetViewMode() == Directory::ViewMode::Cluster;
	const int kSpectralH = 30 + 2 * GlyphH() + 28;  // title + bars + labels + margins
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
		int text_y = row_y + std::max(0, (kRowH - GlyphH()) / 2);
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
		// Selection and hover highlights (drawn before text so text is on top).
		bool is_sel = (row.node == cur_node);
		bool is_hov = !is_sel &&
			s.mouse_x >= 4 && s.mouse_x < kRowRight &&
			s.mouse_y >= row_y && s.mouse_y < row_y + kRowH;
		if (is_sel) {
			FillRect(r, kAccent, 4, row_y, kRowRight - 4, kRowH);
		} else if (is_hov) {
			FillRect(r, Col{ 40, 44, 56 }, 4, row_y, kRowRight - 4, kRowH);
		}

		if (n.type == Directory::NodeType::Folder) {
			const char* glyph = n.expanded ? "v " : "> ";
			std::string name = Truncate(n.name, std::max(0, avail_chars - 3));
			DrawTextF(r, is_hov ? kText : kFolder, indent, text_y, "%s%s/", glyph, name.c_str());
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
			Col c = is_sel ? kBg : (is_hov ? kText : cat_col);
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
		const int gh = GlyphH();
		// Background + thin divider lines (top / bottom) to set it apart
		// from the tree above and the footer below without a full box.
		FillRect(r, kPanelDark, sx + 4, sy, sw - 8, sh);
		SetCol(r, kBorder);
		SDL_RenderLine(r, (float)(sx + 4), (float)sy,
			(float)(sx + sw - 4), (float)sy);
		SDL_RenderLine(r, (float)(sx + 4), (float)(sy + sh - 1),
			(float)(sx + sw - 4), (float)(sy + sh - 1));
		// Title row: 4 px below the top divider, gh tall.
		const int title_y = sy + 4;
		DrawText(r, kHighlight, sx + 8, title_y, "Audio signature");

		int cur = s.dir ? s.dir->CurrentNodeIndex() : -1;
		bool have = (cur >= 0 && s.dir->At(cur).audio_valid);
		// Bar chart sits between the title and the label row; pad 4 px on
		// each side so neither row visually touches the bars. Label row
		// hugs the bottom divider with 4 px breathing room above the
		// divider and gh of vertical space for the text itself.
		const int chartTop    = title_y + gh + 4;
		const int chartBottom = sy + sh - 4 - gh - 2;   // 2 px gap between bars and labels
		const int chartH      = std::max(8, chartBottom - chartTop);
		const int labels_y    = chartBottom + 2;
		if (!have) {
			DrawText(r, kTextDim, sx + 8, chartTop + (chartH - gh) / 2,
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
				DrawText(r, kTextDim, label_x, labels_y, labels[i]);
			}
		}
	}

	// Footer: position info + filter state, just above the search bar.
	if (s.dir->CurrentFileIndex() >= 0) {
		DrawTextF(r, kTextDim, 8, kWinH - kCmdBarH - kSearchH - GlyphH() - 4, "%d / %d shown%s",
			s.dir->CurrentFileIndex() + 1, s.dir->NavCount(),
			s.dir->HideDuplicates() ? "  (dupes hidden)" : "");
	}

	// Header tooltip: when the cursor is over a category/cluster header
	// whose label was truncated to fit the tree width, show the full label
	// in a small word-wrapped box pinned inside the tree pane. Cluster
	// names with their "Cluster N - Bass + Pad (dark, sustained) (24)"
	// descriptors are the common case; long category names with overrides
	// can also trip it. We wrap rather than scaling because SDL3's debug
	// font is fixed-size - a smaller font would mean bundling SDL_ttf or
	// a bitmap atlas, which is overkill for one tooltip.
	if (s.mouse_x >= 0 && s.mouse_x < kTreeW &&
	    s.mouse_y >= top && s.mouse_y < bottom) {
		int rel = (s.mouse_y - top) / kRowH;
		int idx = scroll + rel;
		if (idx >= 0 && idx < total) {
			const auto& hrow = rows[idx];
			int avail = std::max(0, (kRowRight - 22) / kGlyphW);
			if (hrow.is_header && (int)hrow.label.size() > avail) {
				// Wrap on word boundaries to fit kMaxChars per line.
				constexpr int kMaxChars = 34;
				std::vector<std::string> lines;
				{
					const std::string& s2 = hrow.label;
					std::string cur;
					size_t i = 0;
					while (i < s2.size()) {
						while (i < s2.size() && s2[i] == ' ') ++i;
						if (i >= s2.size()) break;
						size_t j = i;
						while (j < s2.size() && s2[j] != ' ') ++j;
						std::string word = s2.substr(i, j - i);
						while ((int)word.size() > kMaxChars) {
							std::string head = word.substr(0, kMaxChars);
							word = word.substr(kMaxChars);
							if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
							lines.push_back(head);
						}
						if (cur.empty()) cur = word;
						else if ((int)(cur.size() + 1 + word.size()) <= kMaxChars)
							cur += " " + word;
						else { lines.push_back(cur); cur = word; }
						i = j;
					}
					if (!cur.empty()) lines.push_back(cur);
				}
				if (lines.empty()) lines.push_back(hrow.label);

				const int kLineH = GlyphH() + 2;  // dynamic: font height + 2px gap
				constexpr int kPadX = 6;
				constexpr int kPadY = 4;
				int max_w = 0;
				for (const auto& ln : lines) {
					int w = (int)ln.size() * kGlyphW;
					if (w > max_w) max_w = w;
				}
				int box_w = max_w + kPadX * 2;
				int box_h = (int)lines.size() * kLineH + kPadY * 2 - 2;

				// Anchor below the hovered row. Flip above if it would
				// fall outside the row-list area; clamp horizontally so
				// the whole box stays inside the tree pane.
				int row_y = top + (idx - scroll) * kRowH;
				int box_x = 4;
				int box_y = row_y + kRowH + 2;
				if (box_y + box_h > bottom - 2)
					box_y = std::max(top, row_y - box_h - 2);
				if (box_x + box_w > kTreeW - 6) box_x = kTreeW - 6 - box_w;
				if (box_x < 4) box_x = 4;

				FillRect(r, kBg, box_x, box_y, box_w, box_h);
				OutlineRect(r, kAccent, box_x, box_y, box_w, box_h);
				for (int li = 0; li < (int)lines.size(); ++li) {
					DrawText(r, kText, box_x + kPadX,
						box_y + kPadY + li * kLineH, lines[li].c_str());
				}
			}
		}
	}
}

void Gui::DrawInstrumentHeader(SDL_Renderer* r, const GuiState& s)
{
	const int y  = kInstrHdrY;
	const int ph = kInstrHdrH;
	FillRect(r, kPanel, kMainX + 4, y, kMainW - 8, ph);
	FocusFrame(r, s, Editor::Panel::Name, kMainX + 4, y, kMainW - 8, ph);

	bool have_rti = s.rti && s.rti->Valid();
	bool have_instr = s.instrument != nullptr;
	if (!have_rti && !have_instr) {
		DrawText(r, kTextDim, kMainX + 16, y + std::max(0, (ph - GlyphH()) / 2), "No instrument loaded.");
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

	// Top text line y: centred in the top half of the panel.
	const int gh = GlyphH();
	const int line1_y = y + std::max(0, (ph / 2 - gh) / 2);
	const int line2_y = y + ph / 2 + std::max(0, (ph / 2 - gh) / 2);

	// Line 1: "Instrument: name  v1 (NN ATA) file: foo.rti".
	DrawText(r, kHighlight, kMainX + 16, line1_y, "Instrument:");

	int name_x = kMainX + 120;
	DrawTextF(r, kText, name_x, line1_y, "\"%s\"", name.c_str());

	// Name-edit caret.
	if (EditingPanel(s, Editor::Panel::Name)) {
		int caret = name_x + (1 + s.editor->name_pos) * kGlyphW; // +1 for the quote
		FillRect(r, kCellCursor, caret, line1_y - 1, kGlyphW, gh + 2);
		if (s.editor->name_pos < (int)name.size()) {
			char ch[2] = { name[s.editor->name_pos], 0 };
			DrawText(r, kBg, caret, line1_y, ch);
		}
	}

	int info_x = name_x + (int)(name.size() + 4) * kGlyphW;
	if (have_rti) {
		DrawTextF(r, kTextDim, info_x, line1_y, "v%d  (%zu ATA) File",	s.rti->Version(), s.rti->AtaBlob().size());
	}
	else if (s.current_bank_slot >= 0) {
		DrawTextF(r, kTextDim, info_x, line1_y, "(Bank slot %02d)", s.current_bank_slot);
	}

	// Undo / Redo / Revert buttons (right-aligned in the header bar).
	// Single-glyph Unicode icons in compact pills: [↩] [↪] [↺]
	{
		const int kBtnH = gh + 4;
		constexpr int kBtnW = 28, kBtnGap = 4;
		int bx = kMainX + 4 + kMainW - 8 - (kBtnW * 3 + kBtnGap * 2) - 4;
		int by = y + std::max(0, (ph / 2 - kBtnH) / 2);
		bool can_undo = s.undo_depth > 0;
		bool can_redo = s.redo_depth > 0;
		// Arrows confirmed present in JetBrains Mono cmap:
		//   U+21A9 ↩  U+21AA ↪  U+219E ↞  U+21D0 ⇐  U+21D2 ⇒  U+2190 ←  U+2192 →
		// U+21BA ↺ and all circular/arc arrows are NOT in the font.
		static const char* kUndo   = "\xe2\x86\xa9";  // U+21A9 ↩  hook-left
		static const char* kRedo   = "\xe2\x86\xaa";  // U+21AA ↪  hook-right
		static const char* kRevert = "\xe2\x86\x9e";  // U+219E ↞  two-headed left (revert-all)
		auto DrawIconBtn = [&](int bxl, const char* icon, bool enabled) {
			Col bg = enabled ? Col{ 55, 58, 70 } : Col{ 35, 36, 42 };
			Col fg = enabled ? kText : kTextDim;
			FillRect(r, bg, bxl, by, kBtnW, kBtnH);
			OutlineRect(r, enabled ? kBorder : Col{ 45, 46, 52 }, bxl, by, kBtnW, kBtnH);
			int iw = g_tr ? g_tr->MeasureWidth(icon) : kGlyphW;
			DrawText(r, fg, bxl + std::max(0, (kBtnW - iw) / 2), by + std::max(0, (kBtnH - gh) / 2), icon);
		};
		DrawIconBtn(bx,                         kUndo,   can_undo);
		DrawIconBtn(bx + kBtnW + kBtnGap,       kRedo,   can_redo);
		DrawIconBtn(bx + (kBtnW + kBtnGap) * 2, kRevert, have_rti || have_instr);
	}

	// Second line: analysis metadata.
	if (s.dir) {
		int cur = s.dir->CurrentNodeIndex();
		if (cur >= 0) {
			const auto& n = s.dir->At(cur);
			int eff_cat = s.dir->EffectiveCategory(cur);
			if (eff_cat >= 0 || n.tags || n.audio_valid) {
				char buf[256];
				int  pos = 0;
				if (eff_cat >= 0) {
					pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Cat: %s", Analysis::Name((Analysis::Category)eff_cat));
					if (n.manual_category >= 0)
						pos += std::snprintf(buf + pos, sizeof(buf) - pos, " [M]");
					if (n.confidence > 0)
						pos += std::snprintf(buf + pos, sizeof(buf) - pos, " (conf %d)", n.confidence);
					pos += std::snprintf(buf + pos, sizeof(buf) - pos, "   ");
				}
				if (n.tags) {
					pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Tags: %s   ", Analysis::TagsToString(n.tags).c_str());
				}
				if (n.audio_valid) {
					pos += std::snprintf(buf + pos, sizeof(buf) - pos,
						"Cent %.1fk  RMS %.2f  ZCR %.2f  Flux %.2f",
						n.audio[5] / 1000.0f,
						(n.audio[0] + n.audio[1] + n.audio[2]) / 3.0f,
						n.audio[3], n.audio[7]);
				}
				DrawText(r, kTextDim, kMainX + 16, line2_y, buf);
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
	int y0 = kParamY0;
	int w = kMainW - 4;
	int h = kParamH;

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
	const int gh = GlyphH();

	// 12 named params in 2 columns of 6.
	for (int i = 0; i < 12; ++i) {
		int col = i / 6;
		int row = i % 6;
		int x = x0 + 16 + col * 220;
		int ry = y0 + 28 + row * kParamRowH;  // top of the row slot
		DrawTextF(r, kText, x, ry + std::max(0, (kParamRowH - gh) / 2), "%s : %02X",
			kParamLabels[i].name, par[kParamLabels[i].idx] & 0xFF);
	}

	// AUDCTL flags, 8 bits as a 4x2 grid of wide toggles (room for labels).
	int ax = x0 + 460;
	int ay = y0 + 28;
	const int kFlagH = gh + 4;   // button height: just tall enough for the font
	constexpr int kFlagW = 88, kFlagSX = 92;
	const int kFlagSY = kFlagH + 4;   // row stride
	// "AUDCTL" header label row
	DrawText(r, kHighlight, ax, ay + std::max(0, (kFlagSY - gh) / 2), "AUDCTL  (click to toggle)");
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
		int bx = ax + col * kFlagSX, by = ay + kFlagSY + row * kFlagSY;
		bool on = par[idxs[i]] != 0;
		Col cell = on ? kAccent : kBankEmpty;
		FillRect(r, cell, bx, by, kFlagW, kFlagH);
		OutlineRect(r, kBorder, bx, by, kFlagW, kFlagH);
		DrawText(r, on ? kBg : kTextDim, bx + 4, by + std::max(0, (kFlagH - gh) / 2), names[i]);
	}

	// Edit cursor.
	if (EditingPanel(s, Editor::Panel::Params)) {
		int ci = s.editor->param_idx;
		if (ci < 12) {
			int col = ci / 6, row = ci % 6;
			int cx = x0 + 16 + col * 220;
			int cy = y0 + 28 + row * kParamRowH;
			CellCursor(r, cx - 2, cy - 1, 210, kParamRowH);
		}
		else {
			int fi = ci - 12;
			if (fi >= 0 && fi < 8) {
				int col = fi % 4, row = fi / 4;
				CellCursor(r, ax + col * kFlagSX - 1, ay + kFlagSY + row * kFlagSY - 1,
					kFlagW + 2, kFlagH + 2);
			}
		}
	}
}

void Gui::DrawEnvelope(SDL_Renderer* r, const GuiState& s)
{
	int x0 = kMainX + 2;
	int y0 = kEnvY0;
	int w = kMainW - 4;
	int h = 200;

	FillRect(r, kPanelDark, x0, y0, w, h);
	OutlineRect(r, kBorder, x0, y0, w, h);
	FocusFrame(r, s, Editor::Panel::Envelope, x0, y0, w, h);
	// Section title and right-aligned controls. The "[EDITING - Tab to
	// switch]" hint used to live next to the title but its full width
	// overlapped the mini-volume graph (which starts ~10 glyphs into the
	// header strip). Moved next to the Mono/Stereo toggle on the right so
	// the title can stay short ("Envelope") and the centre header stays
	// clear for the volume strip.
	const bool envelope_editing = EditingPanel(s, Editor::Panel::Envelope);
	const char* kEditingHint    = "[EDITING - Tab to switch]";
	const int   kEditingHintW   = (int)std::strlen(kEditingHint) * GlyphW();
	const int   kEditingGap     = 8;       // gap between hint and toggle
	{
		const int kTglW = GlyphW() * 6 + 8;  // 6 chars ("Stereo") + padding
		// The title text itself is always just "Envelope" - the EDITING
		// hint sits next to the toggle on the far right rather than after
		// the title - so the section title's underline only needs to skip
		// the toggle button. Keeping the underline at this fixed width
		// regardless of editing state lets it span all the way under the
		// mini-volume graph and the EDITING hint, instead of stopping
		// right after the word "Envelope".
		DrawSectionTitle(r, x0 + 8, y0 + 6, w - 16 - kTglW - 4, "Envelope");

		// Mono / Stereo toggle button (top-right of section).
		const int bw = kTglW, bh = GlyphH() + 4;
		const int bx = x0 + w - 8 - bw, by = y0 + 4;
		Col btn_bg  = s.instr_stereo ? Col{50, 80, 130} : Col{30, 34, 44};
		Col btn_txt = s.instr_stereo ? kAccent : kTextDim;
		Col btn_brd = s.instr_stereo ? kAccent : kBorder;
		FillRect(r, btn_bg, bx, by, bw, bh);
		OutlineRect(r, btn_brd, bx, by, bw, bh);
		const char* sym = s.instr_stereo ? "Stereo" : "Mono";
		DrawText(r, btn_txt, bx + 4, by + 2, sym);

		// "[EDITING - Tab to switch]" sits immediately left of the toggle
		// button and only appears in edit mode. Same vertical centre line
		// as the toggle so the two read as a single control cluster.
		if (envelope_editing) {
			int hint_x = bx - kEditingGap - kEditingHintW;
			DrawText(r, kHighlight, hint_x, by + 1, kEditingHint);
		}
	}

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

	int grid_y = y0 + 32;
	int cell_h = 18;

	// Mini vol graph: bar chart in the envelope header strip (y0+5 to y0+27).
	// Starts well past the "Envelope" label text so they don't overlap, and
	// stops short of the right-hand controls (toggle button, and the
	// "[EDITING - Tab to switch]" hint when present).
	// Mono: only VolL (cyan). Stereo: VolR (orange) left half, VolL (cyan) right half.
	if (s.instrument) {
		const int kTglW2     = GlyphW() * 6 + 8;  // toggle button width (same as above)
		const int strip_x    = x0 + 8 + GlyphW() * 10; // past "Envelope" text + margin
		const int right_pad  = kTglW2 + 4 +	(envelope_editing ? (kEditingHintW + kEditingGap) : 0);
		const int strip_end  = x0 + w - 8 - right_pad;
		const int strip_top  = y0 + 3;
		const int strip_base = y0 + 25;
		const int strip_h    = strip_base - strip_top;
		const int ncols2    = env_len + 1;
		const int scw       = 12;  // fixed 12 px per column
		for (int c = 0; c < ncols2; ++c) 
		{
			int sx = strip_x + c * scw;
			if (sx + scw > strip_end) break;   // don't spill into the toggle / EDITING area
			int vl = s.instrument->envelope[c][VOLUMEL] & 0xF;
			if (s.instr_stereo) 
			{
				int vr = s.instrument->envelope[c][VOLUMER] & 0xF;
				const int half_w = std::max(1, scw / 2 - 1);
				if (vr > 0) {
					int bh = vr * strip_h / 15;
					FillRect(r, Col{220, 140, 40}, sx, strip_base - bh, half_w, bh);
				}
				if (vl > 0) {
					int bh = vl * strip_h / 15;
					FillRect(r, Col{40, 195, 185}, sx + half_w + 1, strip_base - bh, half_w, bh);
				}
			} else {
				if (vl > 0) {
					int bh = vl * strip_h / 15;
					FillRect(r, Col{40, 195, 185}, sx, strip_base - bh, scw - 1, bh);
				}
			}
		}
		// Baseline + goto marker also stop at strip_end so they don't
		// underline the EDITING hint or the toggle button.
		int strip_w = std::min(ncols2 * scw, strip_end - strip_x);
		FillRect(r, Col{60, 64, 72}, strip_x, strip_base, strip_w, 1);
		int go = par[PAR_ENV_GOTO];
		if (go >= 0 && go <= env_len) {
			int goto_x = strip_x + go * scw + scw - 1;
			if (goto_x + 2 <= strip_end)
				FillRect(r, kBankCurrent, goto_x, y0 + 2, 2, strip_h + 2);
		}
	}

	for (int row = 0; row < ENVROWS; ++row) {
		int rx = x0 + 12;
		int ry = grid_y + row * (cell_h + 2);
		DrawText(r, kTextDim, rx, ry + std::max(0, (cell_h - GlyphH()) / 2), row_names[row]);
		// In mono mode strike through the VolR label to indicate it is inactive.
		if (row == VOLUMER && !s.instr_stereo) {
			int ly = ry + std::max(0, (cell_h - GlyphH()) / 2) + GlyphH() / 2;
			FillRect(r, kTextDim, rx, ly, GlyphW() * 4, 1);
		}

		for (int c = 0; c <= env_len; ++c) {
			int v = s.instrument->envelope[c][row];
			int cx = grid_x + c * cell_w;
			int cy = ry;

			// Colour by value intensity; VolR/VolL get a vertical bar background.
			Col fill;
			if (row == VOLUMER || row == VOLUMEL) {
				// Dark background cell
				fill = Col{ 26, 30, 38 };
				FillRect(r, fill, cx, cy, cell_w - 1, cell_h);
				// Colored bar growing from the bottom
				int bh = v * (cell_h - 2) / 15;
				if (bh > 0) {
					Col bar_col = (row == VOLUMER)
						? Col{200, 120, 30}    // orange for VolR
						: Col{30,  175, 165};  // cyan for VolL
					FillRect(r, bar_col, cx, cy + cell_h - 1 - bh, cell_w - 1, bh);
				}
			} else {
				Uint8 g = (Uint8)std::min(220, 40 + v * 18);
				fill = Col{ kEnvCell.r, g, kEnvCell.b };
				FillRect(r, fill, cx, cy, cell_w - 1, cell_h);
			}
			char buf[4];
			std::snprintf(buf, sizeof(buf), "%X", v & 0xF);
			// VolR/VolL cells: white text on the coloured bar background.
			Col txt_col = (row == VOLUMER || row == VOLUMEL) ? kText : kBg;
			DrawText(r, txt_col, cx + (cell_w >= 12 ? cell_w / 2 - 4 : 1), cy + std::max(0, (cell_h - GlyphH()) / 2), buf);

			// Distortion row: show abbreviated mode name when cells are wide enough
			// and the font is small enough to fit two lines.
			if (row == (int)DISTORTION && cell_w >= 24 && GlyphH() * 2 <= cell_h) {
				static const char* kDistAbbr[] = {
					"Buz","Buz","Pat","Pat",
					"Bas","Bas","His","His",
					"Pur","Pur","BB ","BB ",
					"B2 ","B2 ","---","---"
				};
				DrawText(r, kBg, cx + 1, cy + GlyphH(), kDistAbbr[std::clamp(v, 0, 15)]);
			}
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
	int y0 = kNoteTableY0;
	int w = kMainW - 4;
	// Fixed panel height regardless of notes_mode — no layout bounce when switching.
	bool notes_mode = s.instrument && s.instrument->parameters[PAR_TBL_TYPE] == 0;
	constexpr int h = 78;

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
	int gy = y0 + 30;  // title text at y0+6, underline at y0+24; leave 6px gap below it

	const int gh = GlyphH();
	const int cell_h_note = std::max(20, gh + 4);  // cell height tall enough for font
	DrawText(r, kTextDim, x0 + 12, gy + std::max(0, (cell_h_note - gh) / 2), "Step");
	for (int i = 0; i <= tbl_len && i < NOTE_TABLE_MAX_LEN; ++i) {
		Col c = (i == par[PAR_TBL_GOTO]) ? kHighlight : kEnvCell;
		FillRect(r, c, gx + i * cell_w, gy, cell_w - 1, cell_h_note);
		DrawTextF(r, kBg, gx + i * cell_w + 2, gy + std::max(0, (cell_h_note - gh) / 2), "%02X",
			s.instrument->noteTable[i] & 0xFF);
	}

	// Second row: note names (Notes mode only).
	if (notes_mode) {
		static const char* kNoteNames[] = {
			"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"
		};
		int gy2 = gy + cell_h_note + 2;
		DrawText(r, kTextDim, x0 + 12, gy2 + std::max(0, (cell_h_note - gh) / 2), "Note");
		for (int i = 0; i <= tbl_len && i < NOTE_TABLE_MAX_LEN; ++i) {
			int raw = (int)(int8_t)(Uint8)s.instrument->noteTable[i];
			int abs_semi = 48 + raw;   // 48 = C-4 as semitone 0
			FillRect(r, kPanelDark, gx + i * cell_w, gy2, cell_w - 1, cell_h_note);
			if (abs_semi >= 0 && abs_semi < 96) {
				int oct  = abs_semi / 12;
				int note = abs_semi % 12;
				char nbuf[5];
				std::snprintf(nbuf, sizeof(nbuf), "%s%d", kNoteNames[note], oct);
				DrawText(r, kTextDim, gx + i * cell_w + 1, gy2 + std::max(0, (cell_h_note - gh) / 2), nbuf);
			} else {
				DrawText(r, kTextDim, gx + i * cell_w + 1, gy2 + std::max(0, (cell_h_note - gh) / 2), "???");
			}
		}
	}

	// Edit cursor.
	if (EditingPanel(s, Editor::Panel::NoteTable)) {
		int i = std::clamp(s.editor->tbl_idx, 0, tbl_len);
		CellCursor(r, gx + i * cell_w, gy, cell_w - 1, cell_h_note);
	}
}

char Gui::InstrHeaderButtonAt(int x, int y) const
{
	if (y < kInstrHdrY || y >= kInstrHdrY + kInstrHdrH / 2) return 0;  // top half only
	constexpr int kBtnW = 28, kBtnGap = 4;
	int bx = kMainX + 4 + kMainW - 8 - (kBtnW * 3 + kBtnGap * 2) - 4;
	if (x >= bx && x < bx + kBtnW)                                  return 'u';
	if (x >= bx + kBtnW + kBtnGap && x < bx + kBtnW * 2 + kBtnGap) return 'r';
	if (x >= bx + (kBtnW + kBtnGap) * 2 && x < bx + kBtnW * 3 + kBtnGap * 2) return 'v';
	return 0;
}

// ---------------------------------------------------------------------------
// Volume bar-graph popup
// ---------------------------------------------------------------------------

namespace {
	// Shared geometry used by DrawVolPopup, PointInVolPopup, VolPopupCellAt,
	// PointInVolGotoHandle, VolGotoColAt, and VolCopyButtonAt.
	// Keep these consistent with each other.
	constexpr int kVpW           = 840;
	constexpr int kVpH           = 350;   // expanded for goto strip + copy buttons
	constexpr int kVpBarMaxH     = 110;   // v=15 → 110 px
	constexpr int kVpLabelW      = 40;
	constexpr int kVpBarAreaX    = 8 + kVpLabelW + 8;   // offset from px
	constexpr int kVpBarAreaW    = kVpW - kVpBarAreaX - 8;
	constexpr int kVpVolRSY      = 32;    // VolR section top (offset from py)
	constexpr int kVpVolLSY      = kVpVolRSY + kVpBarMaxH + 40;  // VolL section top; 40px gap for copy buttons
	// VolR baseline = py + kVpVolRSY + kVpBarMaxH (= py + 142)
	// VolL baseline = py + kVpVolLSY + kVpBarMaxH (= py + 292)
	// Copy buttons sit in the stereo gap (py+142 to py+182):
	constexpr int kVpCopyLabelSY = kVpVolRSY + kVpBarMaxH + 2;   // = 144
	constexpr int kVpCopyBtnSY   = kVpVolRSY + kVpBarMaxH + 22;  // = 164
	constexpr int kVpCopyBtnH    = 16;
	constexpr int kVpCopyBtnW    = 24;   // 2 glyphs (8px each) + 8px padding
	// Goto handle strip below VolL:
	constexpr int kVpGotoStripSY = kVpVolLSY + kVpBarMaxH + 10;  // = 302
	constexpr int kVpGotoStripH  = 22;
}

bool Gui::PointInVolPopup(int x, int y) const
{
	int px = (kWinW - kVpW) / 2;
	int py = (kWinH - kVpH) / 2;
	return x >= px && x < px + kVpW && y >= py && y < py + kVpH;
}

bool Gui::PointInVolGraph(int x, int y) const
{
	int x0 = kMainX + 2;
	int y0 = kEnvY0;
	int w  = kMainW - 4;
	const int kTglW  = GlyphW() * 2 + 8;
	const int strip_x   = x0 + 8 + GlyphW() * 10;
	const int strip_end = x0 + w - 8 - kTglW - 4;
	return x >= strip_x && x < strip_end && y >= y0 + 5 && y < y0 + 28;
}

Gui::VolPopupHit Gui::VolPopupCellAt(int x, int y, const TInstrument& ins, bool stereo) const
{
	VolPopupHit r;
	if (!m_vol_popup_open) return r;
	int px = (kWinW - kVpW) / 2;
	int py = (kWinH - kVpH) / 2;
	int env_len = ins.parameters[PAR_ENV_LENGTH];
	int ncols   = std::max(1, env_len + 1);
	int bar_w   = std::clamp(kVpBarAreaW / ncols, 4, 24);
	int bax     = px + kVpBarAreaX;

	// Test one section.  Divides height into 16 equal zones so every value
	// 0-15 has the same sized click target (kVpBarMaxH/16 px each).
	auto trySection = [&](int sy_offset, int row_idx) -> bool {
		int sec_sy = py + sy_offset;
		if (y < sec_sy || y >= sec_sy + kVpBarMaxH) return false;
		int col = (x - bax) / bar_w;
		if (x < bax || col < 0 || col > env_len) return false;
		r.hit   = true;
		r.row   = row_idx;
		r.col   = col;
		r.value = std::clamp(15 - (y - sec_sy) * 16 / kVpBarMaxH, 0, 15);
		return true;
	};

	// Both sections are always drawn at fixed positions; mono sync is
	// handled by the drag callbacks, not here.
	if (trySection(kVpVolRSY, VOLUMER)) return r;
	if (trySection(kVpVolLSY, VOLUMEL)) return r;
	return r;
}

bool Gui::PointInStereoToggle(int x, int y) const
{
	const int kTglW = GlyphW() * 6 + 8;
	const int bh    = GlyphH() + 4;
	const int bx    = kMainX + 2 + (kMainW - 4) - 8 - kTglW;
	const int by    = kEnvY0 + 4;
	return x >= bx && x < bx + kTglW && y >= by && y < by + bh;
}

bool Gui::PointInVolGotoHandle(int x, int y, const TInstrument& ins) const
{
	if (!m_vol_popup_open) return false;
	int px = (kWinW - kVpW) / 2;
	int py = (kWinH - kVpH) / 2;
	int gy = py + kVpGotoStripSY;
	if (y < gy || y >= gy + kVpGotoStripH) return false;
	int bax = px + kVpBarAreaX;
	return x >= bax && x < bax + kVpBarAreaW;
}

int Gui::VolGotoColAt(int x, const TInstrument& ins) const
{
	int px  = (kWinW - kVpW) / 2;
	int bax = px + kVpBarAreaX;
	int env_len = ins.parameters[PAR_ENV_LENGTH];
	int ncols   = std::max(1, env_len + 1);
	int bar_w   = std::clamp(kVpBarAreaW / ncols, 4, 24);
	return std::clamp((x - bax) / bar_w, 0, env_len);
}

char Gui::VolCopyButtonAt(int x, int y) const
{
	if (!m_vol_popup_open) return 0;
	int px  = (kWinW - kVpW) / 2;
	int py  = (kWinH - kVpH) / 2;
	int by  = py + kVpCopyBtnSY;
	int lbx = px + kVpW / 2 - kVpCopyBtnW - 4;
	int rbx = px + kVpW / 2 + 4;
	if (y >= by && y < by + kVpCopyBtnH) {
		if (x >= lbx && x < lbx + kVpCopyBtnW) return 'L';
		if (x >= rbx && x < rbx + kVpCopyBtnW) return 'R';
	}
	return 0;
}

bool Gui::PointInVolPopupStereoToggle(int x, int y) const
{
	if (!m_vol_popup_open) return false;
	int px  = (kWinW - kVpW) / 2;
	int py  = (kWinH - kVpH) / 2;
	int btw = GlyphW() * 6 + 8;
	int bth = GlyphH() + 4;
	int btx = px + kVpW - 8 - btw;
	int bty = py + 8;
	return x >= btx && x < btx + btw && y >= bty && y < bty + bth;
}

void Gui::DrawVolPopup(SDL_Renderer* r, const GuiState& s)
{
	if (!s.instrument) return;

	// Dim background
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	FillRect(r, Col{0, 0, 0, 160}, 0, 0, kWinW, kWinH);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

	int px = (kWinW - kVpW) / 2;
	int py = (kWinH - kVpH) / 2;
	FillRect(r, Col{22, 26, 34}, px, py, kVpW, kVpH);
	OutlineRect(r, kAccent,   px,     py,     kVpW,     kVpH);
	OutlineRect(r, kBorder,   px + 1, py + 1, kVpW - 2, kVpH - 2);

	DrawText(r, kHighlight,  px + 10, py + 10, "Volume Envelope  \xe2\x80\x94  drag bars to draw");
	DrawText(r, kTextDim,    px + 10, py + kVpH - GlyphH() - 8,
		"ESC or click outside to close");

	const TInstrument& ins = *s.instrument;
	int env_len = ins.parameters[PAR_ENV_LENGTH];
	int go_col  = ins.parameters[PAR_ENV_GOTO];
	int ncols   = std::max(1, env_len + 1);
	int bar_w   = std::clamp(kVpBarAreaW / ncols, 4, 24);
	int bax     = px + kVpBarAreaX;

	struct Section {
		const char* label;
		int         row_idx;
		int         sy;
		Col         bar_col;
		Col         bar_hi;
	};

	// Always show both sections; mono mode keeps them in sync via drag callbacks.
	const int n_sections = 2;
	Section sections[2] = {
		{ "VolR", VOLUMER, kVpVolRSY, Col{210, 130, 35},  Col{240, 175, 60}  },
		{ "VolL", VOLUMEL, kVpVolLSY, Col{30,  170, 160}, Col{60,  210, 200} },
	};
	// Mono/Stereo toggle button (top-right corner of popup).
	{
		int btw = GlyphW() * 6 + 8;
		int bth = GlyphH() + 4;
		int btx = px + kVpW - 8 - btw;
		int bty = py + 8;
		Col bt_bg  = s.instr_stereo ? Col{50, 80, 130} : Col{30, 34, 44};
		Col bt_txt = s.instr_stereo ? kAccent : kTextDim;
		Col bt_brd = s.instr_stereo ? kAccent : kBorder;
		FillRect(r, bt_bg, btx, bty, btw, bth);
		OutlineRect(r, bt_brd, btx, bty, btw, bth);
		DrawText(r, bt_txt, btx + 4, bty + 2, s.instr_stereo ? "Stereo" : "Mono");
	}

	for (int si = 0; si < n_sections; ++si) {
		auto& sec    = sections[si];
		int sy       = py + sec.sy;
		int baseline = sy + kVpBarMaxH;

		DrawText(r, kText, px + 8, sy + kVpBarMaxH / 2 - GlyphH() / 2, sec.label);

		// Horizontal gridlines at 0, 4, 8, 12, 15 with hex labels
		for (int v : {0, 4, 8, 12, 15}) {
			int gy = baseline - v * kVpBarMaxH / 15;
			FillRect(r, Col{45, 49, 58}, bax, gy, kVpBarAreaW, 1);
			char buf[4];
			std::snprintf(buf, sizeof(buf), "%X", v);
			DrawText(r, Col{80, 84, 92}, bax - GlyphW() * (int)std::strlen(buf) - 4,
				gy - GlyphH() / 2, buf);
		}

		for (int c = 0; c <= env_len; ++c) {
			int v  = ins.envelope[c][sec.row_idx] & 0xF;
			int bx = bax + c * bar_w;
			int bh = v * kVpBarMaxH / 15;

			FillRect(r, Col{35, 39, 48}, bx, sy, bar_w - 1, kVpBarMaxH);
			if (bh > 0) {
				bool active = s.editor && s.editor->active
					&& s.editor->panel == Editor::Panel::Envelope
					&& s.editor->env_col == c
					&& s.editor->env_row == sec.row_idx;
				FillRect(r, active ? sec.bar_hi : sec.bar_col,
					bx, baseline - bh, bar_w - 1, bh);
			}
			if (s.editor && s.editor->active
				&& s.editor->panel == Editor::Panel::Envelope
				&& s.editor->env_col == c
				&& s.editor->env_row == sec.row_idx) {
				OutlineRect(r, kEditCursor, bx, sy, bar_w - 1, kVpBarMaxH);
			}
			if (c == go_col)
				FillRect(r, kBankCurrent, bx + bar_w - 2, sy, 2, kVpBarMaxH + 4);
		}
	}

	// Copy buttons in the stereo gap (py+142 to py+182) — stereo mode only.
	if (s.instr_stereo) {
		DrawText(r, kTextDim,
			px + kVpW / 2 - GlyphW() * 6,
			py + kVpCopyLabelSY,
			"copy volume");
		int by  = py + kVpCopyBtnSY;
		int lbx = px + kVpW / 2 - kVpCopyBtnW - 4;
		int rbx = px + kVpW / 2 + 4;
		// Left button: ↑ copies VolL up to VolR
		FillRect(r,  Col{40, 55, 80}, lbx, by, kVpCopyBtnW, kVpCopyBtnH);
		OutlineRect(r, kBorder,        lbx, by, kVpCopyBtnW, kVpCopyBtnH);
		DrawText(r, kText, lbx + kVpCopyBtnW / 2 - GlyphW() / 2,
			by + (kVpCopyBtnH - GlyphH()) / 2, "\xe2\x86\x91");
		// Right button: ↓ copies VolR down to VolL
		FillRect(r,  Col{40, 55, 80}, rbx, by, kVpCopyBtnW, kVpCopyBtnH);
		OutlineRect(r, kBorder,        rbx, by, kVpCopyBtnW, kVpCopyBtnH);
		DrawText(r, kText, rbx + kVpCopyBtnW / 2 - GlyphW() / 2,
			by + (kVpCopyBtnH - GlyphH()) / 2, "\xe2\x86\x93");
	}

	// Goto handle strip — draggable red marker below VolL.
	{
		int gy = py + kVpGotoStripSY;
		// Background
		FillRect(r, Col{28, 32, 42}, bax, gy, kVpBarAreaW, kVpGotoStripH);
		// Center rail
		FillRect(r, Col{50, 55, 68}, bax, gy + kVpGotoStripH / 2, kVpBarAreaW, 1);
		// "Goto:N" label in the left margin
		char gbuf[16];
		std::snprintf(gbuf, sizeof(gbuf), "Goto:%d", go_col);
		DrawText(r, kTextDim, px + 8, gy + (kVpGotoStripH - GlyphH()) / 2, gbuf);
		// Draggable thumb at the right edge of goto column
		int hx = bax + go_col * bar_w + bar_w - 1;
		FillRect(r, kBankCurrent, hx - 4, gy + 2, 8, kVpGotoStripH - 4);
	}
}

bool Gui::PointInBankEdit(int x, int y) const
{
	BankGeom g = ComputeBankGeom();
	const int ebh = 20;  // conservative hit area for the EDIT button
	int ebx = g.panel_x + g.panel_w - 70, eby = g.panel_y + 4, ebw = 62;
	return x >= ebx && x < ebx + ebw && y >= eby && y < eby + ebh;
}

bool Gui::PointInBankAnalyse(int x, int y) const
{
	BankGeom g = ComputeBankGeom();
	const int ebh = 20;             // same conservative band as EDIT
	const int abw = 78;
	int ebx = g.panel_x + g.panel_w - 70;
	int abx = ebx - 8 - abw;
	int aby = g.panel_y + 4;
	return x >= abx && x < abx + abw && y >= aby && y < aby + ebh;
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

	// --- Instrument header (Name) ---
	{
		if (y >= kInstrHdrY && y < kInstrHdrY + kInstrHdrH / 2 &&
			x >= kMainX + 4 && x < kMainX + 4 + kMainW - 8) {
			int name_x = kMainX + 120 + kGlyphW; // past the opening quote
			int pos = (x - name_x) / kGlyphW;
			r.hit = true; r.panel = Editor::Panel::Name;
			r.a = std::clamp(pos, 0, Editor::NameLength(ins));
			return r;
		}
	}

	// --- Parameters ---
	{
		int x0 = kMainX + 2, y0 = kParamY0;
		// 12 named params: 2 columns of 6.
		for (int i = 0; i < 12; ++i) {
			int col = i / 6, row = i % 6;
			int cx = x0 + 16 + col * 220;
			int cy = y0 + 28 + row * kParamRowH;
			if (x >= cx - 2 && x < cx + 208 && y >= cy && y < cy + kParamRowH) {
				r.hit = true; r.panel = Editor::Panel::Params; r.a = i; return r;
			}
		}
		// 8 AUDCTL flag boxes (4x2 grid, must match DrawParameters).
		int ax = x0 + 460, ay = y0 + 28;
		const int kFlagH_ht = 22;  // generous hit area (slightly larger than drawn)
		constexpr int kFlagSX_ht = 92, kFlagSY_ht = 22;
		for (int fi = 0; fi < 8; ++fi) {
			int col = fi % 4, row = fi / 4;
			int bx = ax + col * kFlagSX_ht, by = ay + kFlagSY_ht + row * kFlagSY_ht;
			if (x >= bx && x < bx + 88 && y >= by && y < by + kFlagH_ht) {
				r.hit = true; r.panel = Editor::Panel::Params; r.a = 12 + fi; return r;
			}
		}
	}

	// --- Envelope ---
	{
		int x0 = kMainX + 2, y0 = kEnvY0, w = kMainW - 4;
		int env_len = ins.parameters[PAR_ENV_LENGTH];
		int label_w = 44;
		int grid_x = x0 + 8 + label_w + 8;
		int grid_w = w - 24 - label_w;
		int cells = std::max(1, env_len + 1);
		int cell_w = std::max(8, grid_w / std::max(cells, 16));
		if (cell_w > 24) cell_w = 24;
		int grid_y = y0 + 32, cell_h = 18, row_pitch = cell_h + 2;
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

	// --- Note table ---
	{
		int x0 = kMainX + 2, y0 = kNoteTableY0, w = kMainW - 4;
		int tbl_len = ins.parameters[PAR_TBL_LENGTH];
		int label_w = 44;
		int gx = x0 + 8 + label_w + 8;
		int gw = w - 24 - label_w;
		int cell_w = std::max(16, gw / 32);
		if (cell_w > 24) cell_w = 24;
		int gy = y0 + 30;  // must match DrawNoteTable
		int cell_h_ht = std::max(20, 16 + 4);  // matches draw-time cell_h_note
		if (x >= gx && y >= gy && y < gy + cell_h_ht) {
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
	const int gh_b = GlyphH();
	const int ebh = gh_b + 4;
	int ebx = g.panel_x + g.panel_w - 70, eby = g.panel_y + 4, ebw = 62;
	FillRect(r, s.bank_edit ? kBankCurrent : kPanelDark, ebx, eby, ebw, ebh);
	OutlineRect(r, s.bank_edit ? kHighlight : kBorder, ebx, eby, ebw, ebh);
	DrawText(r, s.bank_edit ? kBg : kTextDim, ebx + 14, eby + std::max(0, (ebh - gh_b) / 2), "EDIT");

	// "Analyse" button immediately to the left of EDIT. Click runs
	// App::AnalyseAllBankSlots to fingerprint every used slot in one go;
	// results are cached on each Bank::Slot and persist through
	// manifest.txt when the bank is saved.
	const int abw = 78;
	const int abx = ebx - 8 - abw;
	FillRect(r, kPanelDark, abx, eby, abw, ebh);
	OutlineRect(r, kBorder, abx, eby, abw, ebh);
	{
		const int label_w = (int)std::strlen("Analyse") * GlyphW();
		DrawText(r, kText, abx + std::max(0, (abw - label_w) / 2),
			eby + std::max(0, (ebh - gh_b) / 2), "Analyse");
	}

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
		// Vertically centre text in the inner tile.
		int inner_h = cell_h - 2 * cell_pad;
		int txt_y = cy + cell_pad + std::max(0, (inner_h - GlyphH()) / 2);
		DrawText(r, kBg, cx + cell_pad + 4, txt_y, line.c_str());
	}
}

// ---------- Bank-slot right-click context menu ----------

namespace {
	struct BankMenuGeom {
		int x, y, w, h, item_h, items_top;
		// Cluster info area (only set when cluster_info is non-empty):
		// info_top is the y-coord of the separator line above the text,
		// info_lines lists the wrapped strings (so draw + hit-test agree).
		int info_top = 0;
		std::vector<std::string> info_lines;
	};
	constexpr int kBankMenuW         = 200;
	constexpr int kBankMenuItemH     = 22;
	constexpr int kBankMenuItems     = 5;     // New, Clear, Export, Import, Show cluster
	constexpr int kBankMenuHeaderH   = 20;    // slot title strip at the top
	constexpr int kBankMenuBottomPad = 4;     // breathing room below the last item
	constexpr int kBankMenuInfoPad   = 4;     // breathing room around the info text
	constexpr int kBankMenuInfoLineH = 11;    // 8 px font + 3 px gap
	constexpr int kBankMenuInfoMaxChars = 22; // wrap width inside the menu

	// Word-wrap `s` so each line is at most `max_chars`. Words longer than the
	// max are hard-split. Returns at least one line for any non-empty input.
	std::vector<std::string> WrapForBankMenu(const std::string& s, int max_chars)
	{
		std::vector<std::string> lines;
		if (max_chars <= 0 || s.empty()) return lines;
		std::string cur;
		size_t i = 0;
		while (i < s.size()) {
			while (i < s.size() && s[i] == ' ') ++i;
			if (i >= s.size()) break;
			size_t j = i;
			while (j < s.size() && s[j] != ' ') ++j;
			std::string word = s.substr(i, j - i);
			while ((int)word.size() > max_chars) {
				std::string head = word.substr(0, max_chars);
				word = word.substr(max_chars);
				if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
				lines.push_back(head);
			}
			if (cur.empty()) cur = word;
			else if ((int)(cur.size() + 1 + word.size()) <= max_chars)
				cur += " " + word;
			else { lines.push_back(cur); cur = word; }
			i = j;
		}
		if (!cur.empty()) lines.push_back(cur);
		// Honour explicit '\n' in the source by re-wrapping every line that
		// contains one (the caller can use \n to force a paragraph break).
		std::vector<std::string> out;
		for (const auto& ln : lines) {
			size_t start = 0;
			while (start <= ln.size()) {
				size_t nl = ln.find('\n', start);
				out.push_back(ln.substr(start,
					(nl == std::string::npos ? ln.size() : nl) - start));
				if (nl == std::string::npos) break;
				start = nl + 1;
			}
		}
		return out;
	}

	BankMenuGeom ComputeBankMenuGeom(int anchor_x, int anchor_y,
		const std::string& cluster_info)
	{
		BankMenuGeom g;
		g.w = kBankMenuW;
		g.item_h = kBankMenuItemH;
		int base = kBankMenuHeaderH + kBankMenuItemH * kBankMenuItems + kBankMenuBottomPad;
		// The info area is always drawn (shows "None" when nothing has been
		// analysed yet). An empty string from the caller is treated as
		// "None" so the layout stays consistent across opens.
		std::string body = cluster_info.empty() ? std::string("None") : cluster_info;
		g.info_lines = WrapForBankMenu(body, kBankMenuInfoMaxChars);
		if (g.info_lines.empty()) g.info_lines.push_back("None");
		int info_h = kBankMenuInfoPad
			+ (int)g.info_lines.size() * kBankMenuInfoLineH
			+ kBankMenuInfoPad;
		g.h = base + info_h;
		g.x = anchor_x;
		g.y = anchor_y;
		// Keep the menu fully on-screen even when the click is near an edge.
		if (g.x + g.w > kWinW) g.x = kWinW - g.w;
		if (g.y + g.h > kWinH) g.y = kWinH - g.h;
		if (g.x < 0) g.x = 0;
		if (g.y < 0) g.y = 0;
		g.items_top = g.y + kBankMenuHeaderH;
		g.info_top  = g.items_top + kBankMenuItems * kBankMenuItemH + kBankMenuBottomPad;
		return g;
	}

	const char* kBankMenuLabels[kBankMenuItems] = {
		"New", "Clear", "Export RTI", "Import RTI", "Analyse",
	};
} // namespace

void Gui::DrawBankMenu(SDL_Renderer* r, const GuiState& s)
{
	BankMenuGeom g = ComputeBankMenuGeom(s.bank_menu_x, s.bank_menu_y,
		s.bank_menu_cluster_info);

	FillRect(r, kPanel, g.x, g.y, g.w, g.h);
	OutlineRect(r, kBorder, g.x, g.y, g.w, g.h);

	// Header strip identifying which slot the menu operates on.
	char title[32];
	std::snprintf(title, sizeof(title), "Slot %02d", s.bank_menu_slot);
	DrawText(r, kTextDim, g.x + 8, g.y + std::max(2, (kBankMenuHeaderH - GlyphH()) / 2), title);
	SetCol(r, kBorder);
	SDL_RenderLine(r, (float)(g.x + 2), (float)(g.y + kBankMenuHeaderH - 2),
		(float)(g.x + g.w - 2), (float)(g.y + kBankMenuHeaderH - 2));

	// Clear, Export, and Show cluster need an occupied slot; grey them
	// out when empty so the user can see the option exists but isn't
	// currently applicable.
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
		bool disabled = !slot_used &&
			(i == 1 /*Clear*/ || i == 2 /*Export*/ || i == 4 /*Show cluster*/);
		bool hovered = (i == hover);
		if (hovered) {
			FillRect(r, kAccent, g.x + 1, iy, g.w - 2, g.item_h);
		}
		Col col;
		if (disabled)     col = kBorder;
		else if (hovered) col = kBg;
		else              col = kText;
		DrawText(r, col, g.x + 10, iy + std::max(0, (g.item_h - GlyphH()) / 2), kBankMenuLabels[i]);
	}

	// Cluster-info section. Always drawn - "None" when the slot has never
	// been analysed (or was modified since the last analysis). A horizontal
	// divider sits just above it; lines are word-wrapped to fit the menu
	// width. The text is not clickable - clicking anywhere here dismisses
	// the menu like clicking outside.
	SetCol(r, kBorder);
	SDL_RenderLine(r, (float)(g.x + 6), (float)(g.info_top),
		(float)(g.x + g.w - 6), (float)(g.info_top));
	int ty = g.info_top + kBankMenuInfoPad;
	for (const auto& ln : g.info_lines) {
		DrawText(r, kHighlight, g.x + 10, ty, ln.c_str());
		ty += kBankMenuInfoLineH;
	}
}

Gui::BankMenuItem Gui::BankMenuItemAtLogical(int x, int y, int menu_x, int menu_y,
	const std::string& cluster_info) const
{
	BankMenuGeom g = ComputeBankMenuGeom(menu_x, menu_y, cluster_info);
	if (x < g.x || x >= g.x + g.w) return BankMenuItem::None;
	if (y < g.items_top) return BankMenuItem::None;
	int idx = (y - g.items_top) / g.item_h;
	if (idx < 0 || idx >= kBankMenuItems) return BankMenuItem::None;
	switch (idx) {
		case 0: return BankMenuItem::New;
		case 1: return BankMenuItem::Clear;
		case 2: return BankMenuItem::Export;
		case 3: return BankMenuItem::Import;
		case 4: return BankMenuItem::Analyse;
	}
	return BankMenuItem::None;
}

bool Gui::PointInBankMenu(int x, int y, int menu_x, int menu_y,
	const std::string& cluster_info) const
{
	BankMenuGeom g = ComputeBankMenuGeom(menu_x, menu_y, cluster_info);
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
		DrawText(r, col, g.x + 10, iy + std::max(0, (g.item_h - GlyphH()) / 2),
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
		DrawText(r, col, g.x + 10, iy + std::max(0, (g.item_h - GlyphH()) / 2), label);
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

	// Help is split into many short pages (~18 rows each) so it stays
	// readable at TTF font sizes. Pages are grouped:
	//   1-4   Keybindings (browsing, bank, tree views, edit-mode keys)
	//   5     Categories
	//   6     Tags & confidence
	//   7     Audio features + manual overrides
	//   8     Search syntax
	//   9-10  Clusters (concepts, navigation)
	//   11-12 Spectral signature panel (bars, reading)
	//   13-15 Editor (panels, values, save/undo)

	// ----- Page 1: Browsing & playback. -----
	const HelpRow kHelpPage1[] = {
		{ "Browsing & playback", "" },
		{ "a .. z, 0 .. 9",   "Play the current instrument at chromatic pitches" },
		{ "[  /  ]",          "Octave down / up (or wheel over the 'Oct:' readout)" },
		{ "Left / Right",     "Previous / next .RTI (hold to repeat)" },
		{ "Up / Down",        "Previous / next .RTI (hold to repeat)" },
		{ "Mouse wheel",      "Move the selection quickly (3 at a time)" },
		{ "PageUp / PageDn",  "Jump by 10 files" },
		{ "Home / End",       "First / last file in the current view" },
		{ "Enter",            "Collapse / expand the current file's folder" },
		{ "Esc",              "Silence playback (also closes this help)" },
		{ "", "" },
		{ "Search", "" },
		{ "/",                "Open the search bar (page 8 covers syntax + @tags)" },
		{ "", "" },
		{ "Files", "" },
		{ "Drag & drop",      "Drop a folder or .RTI onto the window to load it" },
		{ "F2 / F3 / F4",     "Save bank / Load bank / Switch library" },
	};

	// ----- Page 2: Keybindings - Bank. -----
	const HelpRow kHelpPage2[] = {
		{ "Bank slot navigation", "" },
		{ "Click slot",       "Select a slot. Filled slots auto-load the instrument" },
		{ "Tab / Shift+Tab",  "Step the bank cursor forward / back (no load)" },
		{ "Ctrl + arrows",    "Step the bank cursor. Filled slots auto-load;" },
		{ "",                 "empty slots step over without touching what's loaded" },
		{ "Ctrl + a-z / 0-9", "Audition the selected slot at that chromatic pitch" },
		{ "", "" },
		{ "Bank build", "" },
		{ "+  (or =)",        "Add current instrument to the bank (dedupes by sound)" },
		{ "-",                "Remove the current instrument from the bank" },
		{ "Ctrl+Ins / Del",   "Copy current into / delete the selected slot" },
		{ "Right-click slot", "Bank menu: New / Clear / Export / Import / Analyse" },
		{ "Analyse button",   "(bank title row, left of EDIT) Fingerprint every" },
		{ "",                 "used slot in one pass; results persist in manifest.txt" },
	};

	// ----- Page 3: Keybindings - Bank EDIT mode + tree views. -----
	const HelpRow kHelpPage3[] = {
		{ "Bank EDIT mode  (button on the bank title row)", "" },
		{ "",  "When the EDIT toggle is on, Ctrl+letter is reserved for edit" },
		{ "",  "verbs instead of slot audition. Outside EDIT, those keys fall" },
		{ "",  "through to audition - so e.g. Ctrl+S can never accidentally" },
		{ "",  "trigger a Save when you meant to play the 'S' note." },
		{ "", "" },
		{ "Ctrl+C / Ctrl+X",  "(EDIT) Copy / Cut the selected slot" },
		{ "Ctrl+V",           "(EDIT) Paste into the selected slot" },
		{ "Ctrl+Y",           "(EDIT) Redo the last instrument edit" },
		{ "Ctrl+S",           "(EDIT) Export current instrument as a new .RTI" },
		{ "Ctrl+Z",           "Undo - always available regardless of EDIT state" },
		{ "", "" },
		{ "Tree views", "" },
		{ "F8 / F9 / F10",    "Categories / Hide duplicates / Clusters toggle" },
		{ "Click tab",        "Folders / Categories / Clusters / Show all / No dupes" },
		{ "",                 "tabs above the tree mirror the F-keys" },
	};

	// ----- Page 4: Keybindings - Editing & misc. -----
	const HelpRow kHelpPage4[] = {
		{ "Editing  (F6 toggles Edit mode - pages 13-15 cover the editor)", "" },
		{ "F6",               "Toggle Edit mode on / off" },
		{ "Click a field",    "Jump the cell cursor onto that parameter / cell / name" },
		{ "Right-click",      "Toggle a binary field (AUDCTL bit / Filt / Port)" },
		{ "Tab / Shift+Tab",  "(edit) Cycle through the four editable panels" },
		{ "Arrow keys",       "(edit) Move the cell cursor within the panel" },
		{ "0-9 A-F",          "(edit) Type a hex value into the cell" },
		{ "+ / -",            "(edit) Nudge the cell by +/- 1" },
		{ "Shift+Up / Down",  "(edit) Same as + / -" },
		{ "Mouse wheel",      "(edit) Nudge the focused field by +/- 1" },
		{ "Ctrl + key",       "(edit) Audition without leaving the cursor" },
		{ "Space",            "(edit) Re-trigger the last audition note" },
		{ "", "" },
		{ "Display", "" },
		{ "F1",               "Toggle this help (arrows / wheel = pages; Esc closes)" },
		{ "F5",               "Toggle PAL / NTSC clock" },
		{ "F7",               "Re-analyse the library (pages 5-7 explain the output)" },
		{ "F11",              "Toggle fullscreen" },
		{ "F12",              "Toggle audio path (TAP vs NATIVE, see About box)" },
	};

	// ----- Page 5: Analysis - Categories & confidence. -----
	const HelpRow kHelpPage5[] = {
		{ "What F7 does", "" },
		{ "F7", "Re-analyse the library: classify every .RTI, find duplicates," },
		{ "",   "render each through the engine for audio features, and cluster" },
		{ "",   "by sonic similarity. Results are cached to analysis.json, which" },
		{ "",   "lives next to your instruments folder and travels with it." },
		{ "", "" },
		{ "Categories  (Cat: ... in the instrument header)", "" },
		{ "",   "Each file is placed in ONE bucket. The 15 categories are:" },
		{ "",   "  Bass / Lead / Lead (vibrato) / Arp / Chord / Glide / Pad" },
		{ "",   "  Bell / Kick / Snare / HiHat / Perc / Swept FX / Noise FX / Other" },
		{ "",   "Directory rows are tinted by category colour so you can see them" },
		{ "",   "at a glance. The selected row still reads white-on-blue." },
		{ "", "" },
		{ "Confidence  (conf N in the header)", "" },
		{ "",   "How many independent signals voted for the category (0-5)." },
		{ "",   "Confidence 1 = a guess. Those rows are dimmed in the tree so you" },
		{ "",   "can spot the analyser's uncertain calls and override them manually." },
		{ "", "" },
		{ "Duplicates", "" },
		{ "",   "Files whose rendered audio is byte-for-byte identical are marked" },
		{ "",   "as duplicates. The 'No dupes' tab hides all but the first copy." },
		{ "",   "F9 toggles the same filter from the keyboard." },
	};

	// ----- Page 6: Analysis - Tags. -----
	const HelpRow kHelpPage6[] = {
		{ "Tags  (orthogonal sub-labels set alongside the category)", "" },
		{ "",   "A file can carry several tags at once. They describe acoustic" },
		{ "",   "properties, not category membership, so a file can be tagged" },
		{ "",   "'loud' and 'bright' regardless of whether it is a Bass or a Lead." },
		{ "", "" },
		{ "Structural tags  (from instrument parameters)", "" },
		{ "vibrato",    "PAR_VIBRATO > 0" },
		{ "highfreq",   "AUDCTL 15 kHz / 1.79 MHz bit is set" },
		{ "ascending",  "note-table values generally rise over the table length" },
		{ "descending", "note-table values generally fall" },
		{ "", "" },
		{ "Audio tags  (from the rendered waveform)", "" },
		{ "bright",    "spectral centroid above 4.5 kHz" },
		{ "dark",      "spectral centroid below 1.5 kHz" },
		{ "loud",      "mean RMS above 0.20" },
		{ "quiet",     "mean RMS below 0.05" },
		{ "animated",  "spectral flux > 0.20  (timbre changes a lot over time)" },
		{ "", "" },
		{ "Using tags in search  (page 8)", "" },
		{ "@bright",       "all bright-centroid instruments" },
		{ "@bright,loud",  "bright AND loud (comma = AND)" },
		{ "@animated",     "FX / sweeps whose timbre moves over time" },
		{ "@dark,quiet",   "dark + quiet (ambient pads, soft basses)" },
		{ "", "" },
		{ "Tags in the cluster view", "" },
		{ "",   "Cluster names are built partly from the tags of their member files." },
		{ "",   "Example: 'Cluster 3 - Bass + Pad (dark, sustained) (24)'" },
		{ "",   "See pages 9-11 for the full cluster naming vocabulary." },
	};

	// ----- Page 7: Analysis - Audio features & manual override. -----
	const HelpRow kHelpPage7[] = {
		{ "Audio features  (shown in the instrument header after F7)", "" },
		{ "Cen",  "Spectral centroid (kHz) - perceived brightness" },
		{ "Rll",  "Spectral 85% roll-off (kHz) - where most energy stops" },
		{ "Atk",  "RMS over the first 1/3 of the rendered window" },
		{ "Mid",  "RMS over the middle 1/3" },
		{ "End",  "RMS over the last 1/3" },
		{ "ZCR",  "Zero-crossing rate (high = noisy / high-frequency content)" },
		{ "Pk",   "Position of the absolute-peak sample (0 = attack, 1 = swell)" },
		{ "Flx",  "Average frame-to-frame spectral change (0 = static timbre)" },
		{ "", "" },
		{ "Manual category override", "" },
		{ "",               "If the analyser mis-classifies a file you can pin its" },
		{ "",               "category manually. The override survives F7 re-analysis." },
		{ "Right-click row","'Override category...' opens a colour-coded picker" },
		{ "Ctrl+R",         "Cycle the current file's manual category (forward)" },
		{ "Ctrl+Shift+R",   "Clear every manual override in the library at once" },
		{ "", "" },
		{ "How overrides are shown", "" },
		{ "",   "Overridden rows display an 'M' marker at the right edge of the" },
		{ "",   "tree row. The instrument header reads 'Cat: <name> [M]'." },
		{ "",   "Overrides are stored in analysis.json alongside the analysis" },
		{ "",   "cache so they travel with the library." },
	};

	// ----- Page 8: Search syntax. -----
	const HelpRow kHelpPage8[] = {
		{ "Opening and closing the search bar", "" },
		{ "/",        "Open the search bar (also from the tree-pane footer area)" },
		{ "Enter",    "Commit the filter and return focus to the tree" },
		{ "Esc",      "Clear the filter and close the search bar" },
		{ "",         "Arrow keys still move the selection while the bar is focused." },
		{ "", "" },
		{ "Substring search  (default)", "" },
		{ "",  "Type anything not starting with '@'. The directory filters to" },
		{ "",  "instruments whose filename contains that text, case-insensitive." },
		{ "", "" },
		{ "Examples", "" },
		{ "kick",      "matches  Bouncy_kick.rti,  kick_low.rti,  ..." },
		{ "vinscool",  "matches every file with 'vinscool' anywhere in its name" },
		{ "", "" },
		{ "Tag search  (@tag)", "" },
		{ "",  "Prefix with '@' to filter by audio tags (page 6) instead of" },
		{ "",  "filename. Combine tags with commas for AND matching - every" },
		{ "",  "listed tag must be set on the file." },
		{ "", "" },
		{ "Examples", "" },
		{ "@bright",      "all bright-centroid instruments" },
		{ "@bright,loud", "bright AND loud" },
		{ "@vibrato",     "every instrument with vibrato set" },
		{ "@animated",    "FX whose timbre moves over time" },
		{ "@ascending",   "everything with a rising note-table pattern" },
		{ "", "" },
		{ "Recognised tag tokens", "" },
		{ "",  "vibrato  bright  dark  loud  quiet  animated  highfreq" },
		{ "",  "ascending  descending" },
	};

	// ----- Page 9: Clusters - concept & how it works. -----
	const HelpRow kHelpPage9[] = {
		{ "What clusters are for", "" },
		{ "",  "Categories tell you WHAT KIND of instrument something is." },
		{ "",  "Clusters tell you WHICH OTHERS it sounds like - based on the" },
		{ "",  "rendered audio, not on parameters or filename. Two files in the" },
		{ "",  "same cluster share an overall sonic character regardless of how" },
		{ "",  "they were named or organised on disk." },
		{ "", "" },
		{ "How clustering works", "" },
		{ "",  "k-means runs over an 8-dimensional feature vector per file:" },
		{ "",  "  rms_early / rms_mid / rms_late  (attack / body / tail shape)" },
		{ "",  "  ZCR  (noisiness)" },
		{ "",  "  peak position  (where the loudest moment falls)" },
		{ "",  "  centroid  (brightness)" },
		{ "",  "  rolloff  (bandwidth)" },
		{ "",  "  flux  (timbre motion)" },
		{ "",  "Values are standardised (zero mean, unit variance per axis) so no" },
		{ "",  "single feature dominates the distance calculation." },
		{ "", "" },
		{ "k-means++ seeding", "" },
		{ "",  "The first centroid is chosen arbitrarily; each subsequent one is" },
		{ "",  "the point farthest from any existing centroid. This is deterministic" },
		{ "",  "- no random restarts, identical output for the same library." },
	};

	// ----- Page 10: Clusters - count, naming & adjectives. -----
	const HelpRow kHelpPage10[] = {
		{ "Cluster count  (k)", "" },
		{ "",                  "Default k = 24 (tuned for large libraries)." },
		{ "Ctrl+]  /  Ctrl+[", "Step k up / down for the next F7 (0 = auto)." },
		{ "",                  "Auto uses ceil(sqrt(N/2)) clamped to [3, 12]." },
		{ "",                  "k is persisted in analysis.json; the notice bar shows" },
		{ "",                  "the current value when you change it." },
		{ "", "" },
		{ "Cluster names", "" },
		{ "",  "k-means produces unlabelled groups. PokeyForge builds a label:" },
		{ "",  "  'Cluster 3 - Bass + Pad (dark, sustained) (24)'" },
		{ "",  "  Bass + Pad   top-1 (and close top-2) member categories" },
		{ "",  "  dark/...     up to 3 adjectives from the cluster's mean features" },
		{ "",  "  24           how many instruments are in the cluster" },
		{ "", "" },
		{ "Adjective vocabulary  (derived from mean feature values)", "" },
		{ "bright",     "mean spectral centroid > 4.5 kHz" },
		{ "dark",       "mean spectral centroid < 1.5 kHz" },
		{ "loud",       "mean RMS > 0.20" },
		{ "quiet",      "mean RMS < 0.05" },
		{ "percussive", "short attack, near-silent tail (rms_early >> rms_late)" },
		{ "sustained",  "flat RMS profile across the whole render window" },
		{ "animated",   "mean spectral flux > 0.40 (timbre keeps shifting)" },
		{ "noisy",      "mean zero-crossing rate > 0.10" },
	};

	// ----- Page 11: Clusters - using the view. -----
	const HelpRow kHelpPage11[] = {
		{ "Enabling the cluster view", "" },
		{ "F10",             "Toggle Clusters view on / off" },
		{ "Clusters tab",    "Click the tab above the tree to switch to Clusters" },
		{ "", "" },
		{ "Navigating the cluster list", "" },
		{ "",            "Headers start COLLAPSED so the whole cluster list fits" },
		{ "",            "in the pane at once. Click a cluster header to expand or" },
		{ "",            "collapse that cluster's instrument list." },
		{ "",            "The cluster that contains the current selection auto-expands" },
		{ "",            "so the highlighted row is never hidden after navigation." },
		{ "", "" },
		{ "Cluster header tooltip", "" },
		{ "Hover header", "The full cluster name appears as a tooltip. Long names" },
		{ "",             "are truncated in the tree pane to fit; the tooltip always" },
		{ "",             "shows the complete label, wrapped to two lines if needed." },
		{ "", "" },
		{ "Row order within a cluster", "" },
		{ "",  "Rows are sorted by spectral centroid ascending - dark sounds" },
		{ "",  "(low centroid) first, bright sounds (high centroid) last." },
		{ "", "" },
		{ "Spectral signature panel  (pages 12-13)", "" },
		{ "",  "An 8-bar fingerprint chart appears below the cluster list when" },
		{ "",  "the cluster view is active. It shows the selected file's cached" },
		{ "",  "audio features. See pages 12-13 for what each bar means." },
	};

	// ----- Page 12: Spectral signature panel - bars. -----
	const HelpRow kHelpPage12[] = {
		{ "What the spectral panel shows", "" },
		{ "",  "An 8-bar bar chart of the currently-selected instrument's cached" },
		{ "",  "audio features. It appears at the bottom of the tree pane, just" },
		{ "",  "above the search bar, but only when the Cluster view is active." },
		{ "",  "Each bar's height is one feature normalised to [0, 1] - it's a" },
		{ "",  "visual fingerprint of the sound." },
		{ "", "" },
		{ "When it's visible", "" },
		{ "",  "Cluster view only (F10 / Clusters tab). Folder and Category views" },
		{ "",  "hide it because their grouping isn't by audio similarity." },
		{ "",  "If you haven't run F7 yet, all bars sit at zero." },
		{ "", "" },
		{ "The eight bars  (left to right)", "" },
		{ "Atk",  "rms_early  - RMS over the first third of the rendered window" },
		{ "Mid",  "rms_mid    - RMS over the middle third" },
		{ "End",  "rms_late   - RMS over the last third" },
		{ "ZCR",  "zero-crossing rate (0..1)  High = noisy / high-frequency" },
		{ "Pk",   "position of the absolute-peak sample within the window" },
		{ "",     "  0 = attack peak,  0.5 = mid,  1 = swell at the very end" },
		{ "Cen",  "spectral centroid in Hz, normalised by 22050" },
		{ "",     "  perceived brightness - higher bar = brighter sound" },
		{ "Rll",  "spectral 85% roll-off in Hz, normalised by 22050" },
		{ "",     "  where most of the energy stops - tracks bandwidth" },
		{ "Flx",  "average frame-to-frame spectral change" },
		{ "",     "  0 = static timbre,  higher = timbre keeps shifting" },
	};

	// ----- Page 13: Spectral signature panel - reading shapes. -----
	const HelpRow kHelpPage13[] = {
		{ "Typical bar shapes  (what to look for)", "" },
		{ "Kick",   "Atk tall, Mid/End decay sharply, Pk near 0, Flx high" },
		{ "Hi-hat", "Atk tall, Mid/End near zero, ZCR very high, Cen high" },
		{ "Pad",    "Atk / Mid / End roughly equal (flat profile), Flx low" },
		{ "Bass",   "Cen + Rll low, RMS bars moderate and roughly equal" },
		{ "Sweep",  "Flx high (timbre moves), Atk/Mid/End at similar height" },
		{ "Bell",   "Cen high, ZCR moderate, Pk roughly centred" },
		{ "", "" },
		{ "Colour and category cross-check", "" },
		{ "",  "Bars are tinted with the file's category colour, matching the" },
		{ "",  "row colour in the tree. If the bar shape doesn't match the tint" },
		{ "",  "(e.g. Pad tint but tall Atk and fast decay), the file is likely" },
		{ "",  "mis-classified. Right-click the tree row and choose" },
		{ "",  "'Override category...' to correct it (page 7 for more detail)." },
		{ "", "" },
		{ "Relationship to cluster membership", "" },
		{ "",  "Two files in the same cluster will tend to have similar bar" },
		{ "",  "shapes. If the shapes look very different, it likely means they" },
		{ "",  "ended up together by accident (outlier clustering). Those are" },
		{ "",  "good candidates to inspect - the cluster view's right-click menu" },
		{ "",  "lets you re-analyse a single slot to refresh its fingerprint." },
	};

	// ----- Page 14: Editor - entering, panels, cursor, values. -----
	const HelpRow kHelpPage14[] = {
		{ "Entering and leaving Edit mode", "" },
		{ "F6",   "Toggle Edit mode on / off (also Esc to leave)" },
		{ "",     "The focused panel gets a white frame; a red cell cursor appears" },
		{ "",     "inside it. An orange dot next to the filename = unsaved changes." },
		{ "", "" },
		{ "The four editable panels", "" },
		{ "Name",       "Instrument name text. Type characters directly." },
		{ "",           "Backspace / Delete remove; arrows move the caret." },
		{ "",           "Maximum 32 characters." },
		{ "Parameters", "Top numeric panel - envelope length, table length, fade," },
		{ "",           "vibrato, AUDCTL flags, etc. Two-digit hex fields compose" },
		{ "",           "as you type (first nibble shifts left, second fills low)." },
		{ "Envelope",   "16-column x 6-row grid: Volume L, Volume R (stereo)," },
		{ "",           "Distortion, AUDCTL mods, Filter / Portamento, and command." },
		{ "",           "Columns 0..PAR_ENV_LENGTH are active." },
		{ "Note table", "Per-step note offsets (arps / chords / glides)." },
		{ "",           "Length is PAR_TBL_LENGTH." },
		{ "", "" },
		{ "Moving the cursor", "" },
		{ "Tab / Shift+Tab", "Cycle through panels  (Name -> Params -> Env -> Tbl)" },
		{ "Arrow keys",      "Move within the current panel" },
		{ "Click",           "Jump cursor to any clicked field" },
		{ "", "" },
		{ "Changing values", "" },
		{ "0-9 A-F",         "Type a hex digit (two-digit fields compose)" },
		{ "+ / -",           "Increment / decrement by 1" },
		{ "Shift+Up / Down", "Same as +/-" },
		{ "Mouse wheel",     "Nudge the focused field by +/- 1" },
		{ "Right-click",     "Toggle a binary field (AUDCTL, Filter, Port flag)" },
	};

	// ----- Page 15: Editor - mono/stereo, audition, undo, save. -----
	const HelpRow kHelpPage15[] = {
		{ "Mono vs Stereo  (toggle in the Envelope panel header)", "" },
		{ "",   "Loaded instruments default to MONO. The 'Mono' / 'Stereo' button" },
		{ "",   "in the Envelope header (and in the vol popup) flips the encoding." },
		{ "",   "In MONO, any change to VolL mirrors to VolR and vice versa" },
		{ "",   "- across hex digits, +/- nudges, scroll-wheel, drag-paint, and" },
		{ "",   "the vol popup. The channels stay locked until you flip to Stereo." },
		{ "",   "In STEREO the channels are independent. The popup's up / down" },
		{ "",   "arrows between sections copy one row over the other (one undo step)." },
		{ "", "" },
		{ "Audition while editing", "" },
		{ "Ctrl + a-z / 0-9", "Play a note without leaving the cursor. Ctrl" },
		{ "",                 "prevents the key being read as a hex digit." },
		{ "Space",            "Re-trigger the last note with the latest changes." },
		{ "", "" },
		{ "Undo and redo", "" },
		{ "Ctrl+Z",  "Undo. Always available; stack resets on instrument load." },
		{ "Ctrl+Y",  "Redo. Requires Bank EDIT mode - outside EDIT, Ctrl+Y" },
		{ "",        "plays the 'Y' note so it can't fire by accident." },
		{ "", "" },
		{ "Saving / exporting", "" },
		{ "Ctrl+S",  "Export the working copy as a new .RTI file." },
		{ "",        "Requires Bank EDIT mode (same reason as Ctrl+Y above)." },
		{ "",        "Defaults to the current library folder. The on-disk source" },
		{ "",        "is never overwritten - every Ctrl+S is effectively Save As." },
		{ "", "" },
		{ "Visual cues at a glance", "" },
		{ "Orange dot",        "Unsaved changes on the working copy" },
		{ "White panel frame", "The panel that has cell focus" },
		{ "Red cell box",      "The cell currently under the cursor" },
		{ "Header subline",    "<panel>  <field>: <value> [range]  <description>" },
	};

	const HelpPage kHelpPages[] = {
		{ "Browsing & playback",    kHelpPage1,  (int)(sizeof(kHelpPage1)  / sizeof(kHelpPage1[0]))  },
		{ "Bank navigation",        kHelpPage2,  (int)(sizeof(kHelpPage2)  / sizeof(kHelpPage2[0]))  },
		{ "Bank EDIT & tree views", kHelpPage3,  (int)(sizeof(kHelpPage3)  / sizeof(kHelpPage3[0]))  },
		{ "Editing & display keys", kHelpPage4,  (int)(sizeof(kHelpPage4)  / sizeof(kHelpPage4[0]))  },
		{ "Categories & analysis",  kHelpPage5,  (int)(sizeof(kHelpPage5)  / sizeof(kHelpPage5[0]))  },
		{ "Tags",                   kHelpPage6,  (int)(sizeof(kHelpPage6)  / sizeof(kHelpPage6[0]))  },
		{ "Audio features",         kHelpPage7,  (int)(sizeof(kHelpPage7)  / sizeof(kHelpPage7[0]))  },
		{ "Search syntax",          kHelpPage8,  (int)(sizeof(kHelpPage8)  / sizeof(kHelpPage8[0]))  },
		{ "Clusters - concept",     kHelpPage9,  (int)(sizeof(kHelpPage9)  / sizeof(kHelpPage9[0]))  },
		{ "Clusters - count/names", kHelpPage10, (int)(sizeof(kHelpPage10) / sizeof(kHelpPage10[0])) },
		{ "Clusters - view",        kHelpPage11, (int)(sizeof(kHelpPage11) / sizeof(kHelpPage11[0])) },
		{ "Spectral panel - bars",  kHelpPage12, (int)(sizeof(kHelpPage12) / sizeof(kHelpPage12[0])) },
		{ "Spectral - shapes",      kHelpPage13, (int)(sizeof(kHelpPage13) / sizeof(kHelpPage13[0])) },
		{ "Editor - panels",        kHelpPage14, (int)(sizeof(kHelpPage14) / sizeof(kHelpPage14[0])) },
		{ "Editor - save/undo",     kHelpPage15, (int)(sizeof(kHelpPage15) / sizeof(kHelpPage15[0])) },
	};
	static_assert((int)(sizeof(kHelpPages) / sizeof(kHelpPages[0])) == Gui::kHelpPageCount,
	              "kHelpPageCount in Gui.h must match the number of kHelpPages entries");

} // namespace

namespace {

	// Shared geometry for the help overlay. Sized to fit in the 1280x720
	// canvas with 16 px breathing room on all sides. The Prev / Next paging
	// buttons live in the bottom-right corner; the renderer and the hit-test
	// both compute their rects through this helper so they can't drift.
	// Topic shortcut buttons: label + target page (0-based).
	struct HelpTopic { const char* label; int page; };
	constexpr HelpTopic kHelpTopics[] = {
		{ "Bindings", 0  },
		{ "Analysis", 4  },
		{ "Search",   7  },
		{ "Clusters", 8  },
		{ "Spectral", 11 },
		{ "Editor",   13 },
	};
	constexpr int kHelpTopicCount = (int)(sizeof(kHelpTopics) / sizeof(kHelpTopics[0]));

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
		// Topic shortcut buttons (same row as prev/next, left-aligned).
		int topic_btn_w;         // uniform width for all topic buttons
		int topic_btn_x0;        // left edge of the first topic button
		int topic_btn_y;         // top edge (same as prev_y)
		int topic_btn_gap;       // horizontal gap between topic buttons
	};
	HelpGeom ComputeHelpGeom()
	{
		// All sizes derive from the active font (8 px built-in or TTF)
		// so the panel scales correctly with JetBrains Mono / SDL3_ttf,
		// which has roughly 11 px glyph width and 17 px line height
		// vs the 8x8 fallback. The previous fixed offsets crammed
		// rows together and clipped the "< Prev" / "Next >" buttons.
		const int gw = GlyphW();
		const int gh = GlyphH();

		HelpGeom g;
		g.w = 900;
		g.h = 700;                       // fits in 720 with 10 px margin top/bottom
		g.x = (kWinW - g.w) / 2;
		g.y = (kWinH - g.h) / 2;

		// Content rows: text line + breathing room. ~50% extra spacing
		// gives a clearly-readable list at TTF sizes without wasting too
		// much vertical real estate.
		g.row_h = gh + (gh / 2);

		// Keys column starts after the left margin; description column is
		// wide enough for ~22 chars of key text at the current font.
		g.content_left  = g.x + 28;
		g.desc_x        = g.x + 28 + (gw * 22) + 12;
		g.content_right = g.x + g.w - 24;

		// Title row at the top: gh-tall text with 18 px top padding plus
		// a 6 px gap before the underline, then 12 px more before content.
		g.content_top = g.y + 18 + gh + 6 + 12;

		// Footer line + paging buttons at the bottom. Buttons need to be
		// tall enough for the font with breathing room; the labels
		// "< Prev" / "Next >" are 6 chars so we size for 8 chars of
		// padding to keep them clearly clickable at any font width.
		g.btn_h = gh + 10;
		g.btn_w = gw * 8 + 16;
		// Footer hint sits at the very bottom of the panel.
		g.footer_y  = g.y + g.h - gh - 10;
		// Buttons stack just above the footer hint with an 8 px gap.
		g.next_y    = g.footer_y - g.btn_h - 8;
		g.prev_y    = g.next_y;
		g.next_x    = g.content_right - g.btn_w;
		g.prev_x    = g.next_x - g.btn_w - 10;

		// Topic buttons: same height/row as prev/next, starting from the
		// content left margin. Width sized for the longest label ("Bindings"
		// = 8 chars) plus padding; gap is 6 px between buttons.
		g.btn_h        = gh + 10;        // already set above, kept for clarity
		g.topic_btn_w  = gw * 8 + 14;
		g.topic_btn_gap = 6;
		g.topic_btn_x0  = g.content_left;
		g.topic_btn_y   = g.next_y;      // same row as prev/next
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
	const int gw = GlyphW();
	const int gh = GlyphH();

	FillRect(r, Col{ 30, 34, 44, 245 }, g.x, g.y, g.w, g.h);
	OutlineRect(r, kAccent, g.x, g.y, g.w, g.h);
	OutlineRect(r, kAccent, g.x + 1, g.y + 1, g.w - 2, g.h - 2);

	// Title + page indicator.
	const int title_y = g.y + 18;
	char title[128];
	std::snprintf(title, sizeof(title), "PokeyForge  -  %s", hp.title);
	DrawText(r, kHighlight, g.x + 20, title_y, title);
	char pageInfo[40];
	std::snprintf(pageInfo, sizeof(pageInfo), "Page %d / %d", page + 1, total_pages);
	int piw = (int)std::strlen(pageInfo) * gw;
	DrawText(r, kTextDim, g.content_right - piw, title_y, pageInfo);

	const int title_divider_y = title_y + gh + 6;
	SetCol(r, kBorder);
	SDL_RenderLine(r, (float)(g.x + 20), (float)title_divider_y,
		(float)(g.content_right), (float)title_divider_y);

	// Content rows. Empty key+desc = blank line; non-empty key with empty
	// desc = section heading. Rows that overflow the available height are
	// silently clipped - kHelpPages are sized to fit at TTF dimensions.
	int y = g.content_top;
	const int footer_divider_y = g.next_y - 8;
	const int max_y = footer_divider_y - g.row_h / 2;
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

	// Footer divider sits 8 px above the paging buttons; the hint text
	// sits at the very bottom of the panel below the buttons.
	SetCol(r, kBorder);
	SDL_RenderLine(r, (float)(g.x + 20), (float)footer_divider_y,
		(float)(g.content_right), (float)footer_divider_y);

	// Clickable Prev / Next buttons. Hover highlight matches the rest of
	// the modal popups (kAccent fill + kBg text on hover). The label is
	// centred using measured pixel widths so it always fits regardless of
	// the active font.
	auto draw_btn = [&](int bx, int by, const char* label) {
		bool hovered = (s.mouse_x >= bx && s.mouse_x < bx + g.btn_w &&
			s.mouse_y >= by && s.mouse_y < by + g.btn_h);
		FillRect(r, hovered ? kAccent : kPanelDark, bx, by, g.btn_w, g.btn_h);
		OutlineRect(r, kAccent, bx, by, g.btn_w, g.btn_h);
		int tw = (int)std::strlen(label) * gw;
		DrawText(r, hovered ? kBg : kHighlight,
			bx + (g.btn_w - tw) / 2,
			by + std::max(0, (g.btn_h - gh) / 2),
			label);
	};
	draw_btn(g.prev_x, g.prev_y, "< Prev");
	draw_btn(g.next_x, g.next_y, "Next >");

	// Topic shortcut buttons. The active topic's button is highlighted with
	// kAccent fill (same as hover) to show which section is current.
	for (int i = 0; i < kHelpTopicCount; ++i) {
		const HelpTopic& t = kHelpTopics[i];
		int bx = g.topic_btn_x0 + i * (g.topic_btn_w + g.topic_btn_gap);
		int by = g.topic_btn_y;
		bool active  = (page >= t.page &&
		               (i + 1 >= kHelpTopicCount || page < kHelpTopics[i + 1].page));
		bool hovered = (s.mouse_x >= bx && s.mouse_x < bx + g.topic_btn_w &&
		                s.mouse_y >= by && s.mouse_y < by + g.btn_h);
		Col fill = (active || hovered) ? kAccent : kPanelDark;
		Col text = (active || hovered) ? kBg      : kTextDim;
		FillRect(r, fill, bx, by, g.topic_btn_w, g.btn_h);
		OutlineRect(r, kAccent, bx, by, g.topic_btn_w, g.btn_h);
		int tw = (int)std::strlen(t.label) * gw;
		DrawText(r, text,
			bx + (g.topic_btn_w - tw) / 2,
			by + std::max(0, (g.btn_h - gh) / 2),
			t.label);
	}

	DrawText(r, kTextDim, g.x + 20, g.footer_y,
		"Left / Right or wheel: page    Esc / F1: close    Click outside: dismiss");

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

int Gui::HelpTopicButtonAtLogical(int x, int y) const
{
	HelpGeom g = ComputeHelpGeom();
	if (y < g.topic_btn_y || y >= g.topic_btn_y + g.btn_h) return -1;
	for (int i = 0; i < kHelpTopicCount; ++i) {
		int bx = g.topic_btn_x0 + i * (g.topic_btn_w + g.topic_btn_gap);
		if (x >= bx && x < bx + g.topic_btn_w)
			return kHelpTopics[i].page;
	}
	return -1;
}

bool Gui::PointInHelpPanel(int x, int y) const
{
	HelpGeom g = ComputeHelpGeom();
	return x >= g.x && x < g.x + g.w && y >= g.y && y < g.y + g.h;
}
