#pragma once

#include "Bank.h"
#include "Directory.h"
#include "Editor.h"
#include "InstrumentTypes.h"
#include "RtiFile.h"

#include <SDL3/SDL.h>

#include <string>

// Read-only renderer for PokeyForge's four-panel view:
//   - Directory tree (left)
//   - Instrument parameters + AUDCTL flags
//   - Envelope grid (8 rows x N columns of coloured cells)
//   - Note table (32 entries)
//   - 8x8 bank slot grid
//
// All text is drawn with SDL_RenderDebugText (8x8 built-in font), so this
// module has zero external dependencies.

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
    bool  search_active  = false;    // search bar focused
    std::string search_query;
    bool  bank_edit      = false;    // bank EDIT mode (Ctrl+C/X/V = move)
    int   current_bank_slot = -1;    // if the current instrument is a bank slot

    // Live POKEY shadow registers ($D200..$D208) for the master scope.
    byte  pokey[9] = {0};
};

class Gui {
public:
    void Render(SDL_Renderer* renderer, const GuiState& s);

    // Top menu-bar commands.
    enum class MenuAction { None, Save, Load, Library, Analyse, About, Help };

    // Hit-test the menu bar in logical coordinates. Returns the command under
    // (x,y), or MenuAction::None.
    MenuAction MenuAtLogical(int x, int y) const;

    // Directory-pane view tabs.
    enum class DirTab { None, Folders, Category, ShowAll, HideDupes };
    DirTab DirTabAtLogical(int x, int y) const;

    // Hit-test a tree row in logical coordinates. Returns the index into
    // Directory::Rows() under (x,y), or -1. Valid after a Render().
    int TreeRowAtLogical(int x, int y) const;

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

    // Tree geometry captured during DrawTree for click hit-testing.
    int m_tree_top = 0;
    int m_tree_rowh = 16;
    int m_tree_scroll = 0;
    int m_tree_row_count = 0;
};
