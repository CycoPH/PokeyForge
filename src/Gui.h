#pragma once

#include "Bank.h"
#include "Directory.h"
#include "Editor.h"
#include "InstrumentTypes.h"
#include "RtiFile.h"

#include <SDL3/SDL.h>

#include <string>

// Read-only renderer for PokeyForge's main screen. Render() takes a
// snapshot-style GuiState (built fresh per frame from the App) and draws:
//   - Directory tree (left column) + view tabs + scrollbar + search bar
//   - Instrument parameters + AUDCTL flags
//   - Envelope grid (8 rows x N columns of coloured cells)
//   - Note table (32 entries)
//   - 8x8 bank slot grid
//   - Floating UI: bank context menu, save prompt, confirm dialog, about,
//     help overlay (drawn in that order so modals always sit on top).
//
// Layout is anchored on a 1280x720 logical canvas; SDL_RenderDebugText (8x8
// built-in font) is used for all text. Hit-tests for clickable areas are
// public methods (MenuAtLogical, DirTabAtLogical, TreeRowAtLogical, ...) so
// the event loop can ask "what is at this pixel?" without duplicating
// layout maths. Where geometry is non-trivial (bank grid, scrollbar, bank
// menu), one helper computes the rects and both the draw and the hit-test
// call it — they can't drift apart that way.
//
// The Gui is otherwise stateless wrt app data; the only persistent fields
// are tree-scroll position + bank-menu drag state, all kept on the Gui
// object itself.

struct GuiState {
    const Directory* dir          = nullptr;
    const Bank*      bank         = nullptr;
    const RtiFile*   rti          = nullptr;
    const TInstrument* instrument = nullptr;
    int   octave_shift   = 0;
    bool  ntsc           = false;
    int   last_note_played = -1;
    bool  show_help      = false;
    int   bank_cursor    = -1;
    std::string library_path;
    std::string notice;

    // Phase 2 editing.
    const Editor* editor = nullptr;  // cursor + active flag
    bool  modified       = false;    // working copy has unsaved edits
    bool  show_prompt    = false;    // unsaved-edits Save/Discard/Cancel modal
    bool  show_confirm   = false;    // generic Yes/No modal
    std::string confirm_msg;
    bool  show_about     = false;    // About info popup
    int   help_page      = 0;        // current page of the F1 help overlay
    bool  search_active  = false;    // search bar focused
    std::string search_query;
    bool  bank_edit      = false;    // bank EDIT mode (Ctrl+C/X/V = move)
    int   current_bank_slot = -1;    // if the current instrument is a bank slot

    // Right-click context menu on a bank slot.
    bool  bank_menu_open = false;
    int   bank_menu_slot = -1;       // target slot for the menu actions
    int   bank_menu_x    = 0;        // anchor (top-left) in logical coords
    int   bank_menu_y    = 0;

    // Right-click context menu on a directory tree file row.
    bool  tree_menu_open = false;
    int   tree_menu_node = -1;       // file node index the menu acts on
    int   tree_menu_x    = 0;
    int   tree_menu_y    = 0;
    bool  tree_menu_has_override = false;   // adds the "Clear override" row

    // Category picker popup (chained from the tree menu's "Override..."
    // action). cat_picker_node is set when the tree menu opens it so the
    // pick lands on the right file regardless of subsequent selection.
    bool  cat_picker_open = false;
    int   cat_picker_node = -1;
    int   cat_picker_x    = 0;
    int   cat_picker_y    = 0;

    // Latest mouse position in logical coords; -1 means off-screen/unknown.
    // Used by DrawBankMenu to draw a hover highlight under the cursor.
    int   mouse_x        = -1;
    int   mouse_y        = -1;

    // Live POKEY shadow registers ($D200..$D208) for the master scope.
    byte  pokey[9] = {0};
};

class Gui {
public:
    // Number of pages in the F1 help overlay (Keybindings, Categories &
    // analysis, Search syntax, Clusters & spectral view). Exposed so the
    // event loop can wrap Left/Right page navigation.
    static constexpr int kHelpPageCount = 4;

    // Help overlay hit-tests: which page button (if any) lies under (x,y)?
    // Returns -1 for the "Prev" arrow, +1 for "Next", 0 for neither.
    // PointInHelpPanel is the dim-outside-dismiss test (clicks inside the
    // panel must NOT close help, otherwise users can't read it without
    // accidentally dismissing it).
    int  HelpPageButtonAtLogical(int x, int y) const;
    bool PointInHelpPanel       (int x, int y) const;

    void Render(SDL_Renderer* renderer, const GuiState& s);

    // Top menu-bar commands.
    enum class MenuAction { None, Save, Load, Library, Analyse, About, Help };

    // Hit-test the menu bar in logical coordinates. Returns the command under
    // (x,y), or MenuAction::None.
    MenuAction MenuAtLogical(int x, int y) const;

    // Directory-pane view tabs.
    enum class DirTab { None, Folders, Category, Cluster, ShowAll, HideDupes };
    DirTab DirTabAtLogical(int x, int y) const;

    // Hit-test a tree row in logical coordinates. Returns the index into
    // Directory::Rows() under (x,y), or -1. Valid after a Render().
    int TreeRowAtLogical(int x, int y) const;

    // Tree scrollbar interaction. TreeScrollbarHit returns 't' for the thumb,
    // 'a' for the track above the thumb, 'b' for the track below it, or 0
    // when (x,y) is outside the scrollbar. Drag methods move m_tree_scroll
    // directly so the tree follows the mouse without changing the selection.
    char TreeScrollbarHit(int x, int y) const;
    void BeginTreeScrollDrag(int y);
    void UpdateTreeScrollDrag(int y);
    void EndTreeScrollDrag();
    bool TreeScrollDragging() const { return m_scroll_dragging; }
    // Scroll by approximately one page; direction = -1 up, +1 down.
    void PageTreeScroll(int direction);

    // True if (x,y) lies anywhere inside the directory pane (the left column
    // containing the view tabs, tree rows, scrollbar, search bar, and footer).
    bool PointInTreePane(int x, int y) const;

    // True if (x,y) lies over the "Oct: +N" indicator in the top header.
    // Used by the event loop to route mouse-wheel scrolls there into an
    // octave change instead of the usual instrument selection step.
    bool PointInOctaveIndicator(int x, int y) const;

    // True if (x,y) is inside the search bar.
    bool PointInSearchBar(int x, int y) const;

    // True if (x,y) is inside the bank EDIT toggle button.
    bool PointInBankEdit(int x, int y) const;

    // Returns 'k'/'d'/'c' for the unsaved-edits prompt button under (x,y), or 0.
    char SavePromptButtonAt(int x, int y) const;

    // Returns 'y'/'n' for the confirm-dialog button under (x,y), or 0.
    char ConfirmButtonAt(int x, int y) const;

    // Hit-test the bank grid in logical (1280x720) coordinates. Returns the
    // slot index under (x,y), or -1 if the point isn't on a bank tile.
    int  BankSlotAtLogical(int x, int y) const;

    // Bank-slot right-click context menu. The menu anchor (top-left in
    // logical coords) is passed in directly so the hit-test can run from
    // the event loop without needing a full GuiState.
    enum class BankMenuItem { None, New, Clear, Export, Import };
    BankMenuItem BankMenuItemAtLogical(int x, int y, int menu_x, int menu_y) const;
    bool         PointInBankMenu      (int x, int y, int menu_x, int menu_y) const;

    // Directory-tree right-click context menu. Same shape as the bank
    // menu (anchor passed in, hit-test usable from the event loop). The
    // item list expands when a manual override is set so the user can
    // clear it directly.
    enum class TreeMenuItem {
        None,
        AddToBank,
        OverrideSubmenu,   // opens the category picker
        ClearOverride
    };
    TreeMenuItem TreeMenuItemAtLogical(int x, int y, int menu_x, int menu_y,
                                       bool has_override) const;
    bool         PointInTreeMenu      (int x, int y, int menu_x, int menu_y,
                                       bool has_override) const;

    // Generic category-picker popup (used by the tree menu's
    // "Override category..." action). Returns the picked category
    // index in [0, Analysis::Category::COUNT), or -1 for the "Clear
    // override" row, or -2 for outside the popup.
    int CategoryPickerItemAtLogical(int x, int y, int menu_x, int menu_y) const;
    bool PointInCategoryPicker      (int x, int y, int menu_x, int menu_y) const;

    // Hit-test the instrument panels in logical coordinates. On a hit, reports
    // which edit panel was clicked and the cursor position within it
    //   Params:    a = param_idx
    //   Envelope:  a = column, b = row
    //   NoteTable: a = step index
    //   Name:      a = caret position
    struct EditHit {
        bool hit = false;
        Editor::Panel panel = Editor::Panel::Params;
        int a = 0;
        int b = 0;
    };
    EditHit EditFieldAtLogical(int x, int y, const TInstrument& ins) const;

private:
    void DrawHeader(SDL_Renderer* r, const GuiState& s);
    void DrawTree  (SDL_Renderer* r, const GuiState& s);
    void DrawInstrumentHeader(SDL_Renderer* r, const GuiState& s);
    void DrawParameters(SDL_Renderer* r, const GuiState& s);
    void DrawEnvelope  (SDL_Renderer* r, const GuiState& s);
    void DrawNoteTable (SDL_Renderer* r, const GuiState& s);
    void DrawBank      (SDL_Renderer* r, const GuiState& s);
    void DrawCommandBar(SDL_Renderer* r, const GuiState& s);
    void DrawEditBar(SDL_Renderer* r, const GuiState& s);
    void DrawSearchBar(SDL_Renderer* r, const GuiState& s);
    void DrawMasterScope(SDL_Renderer* r, const GuiState& s);
    void DrawHelpOverlay(SDL_Renderer* r, const GuiState& s);
    void DrawSavePrompt(SDL_Renderer* r, const GuiState& s);
    void DrawConfirm(SDL_Renderer* r, const GuiState& s);
    void DrawAbout(SDL_Renderer* r, const GuiState& s);
    void DrawBankMenu(SDL_Renderer* r, const GuiState& s);
    void DrawTreeMenu(SDL_Renderer* r, const GuiState& s);
    void DrawCategoryPicker(SDL_Renderer* r, const GuiState& s);

    // Tree geometry captured during DrawTree for click hit-testing.
    int m_tree_top = 0;
    int m_tree_rowh = 16;
    int m_tree_scroll = 0;
    int m_tree_row_count = 0;
    int m_tree_visible_rows = 0;

    // Scrollbar geometry, captured during DrawTree.
    int m_scroll_track_x = 0;
    int m_scroll_track_y = 0;
    int m_scroll_track_w = 0;
    int m_scroll_track_h = 0;
    int m_scroll_thumb_y = 0;
    int m_scroll_thumb_h = 0;

    // Drag state for the scrollbar thumb.
    bool m_scroll_dragging = false;
    int  m_scroll_drag_offset = 0;   // mouse_y - thumb_y at drag start

    // Selection tracking so that view-scroll only auto-snaps when the
    // selected node actually changes, leaving manual scrolling alone.
    int  m_last_cur_node = -2;
    bool m_user_scrolled = false;
};
