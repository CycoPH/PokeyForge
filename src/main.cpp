// PokeyForge — RMT .RTI instrument auditioner / editor / bank builder.

#include "Analysis.h"
#include "Audio.h"
#include "Bank.h"
#include "Config.h"
#include "Directory.h"
#include "Editor.h"
#include "Gui.h"
#include "TextRenderer.h"
#include "Keyboard.h"
#include "Pokey.h"
#include "RmtEngine.h"
#include "RtiFile.h"
#include "Version.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // provides the Windows entry point (no console)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>   // AttachConsole for headless --analyse mode
#endif

namespace fs = std::filesystem;

namespace {

constexpr int  kSampleRate  = 44100;
constexpr int  kBaseTrack   = 0;
constexpr int  kInstrSlot   = 0;   // engine RAM slot for the current instrument
constexpr int  kSampleSlot  = 1;   // engine RAM slot for sampling a bank slot
constexpr int  kPlayVolume  = 15;
constexpr int  kMinNote     = 0;
constexpr int  kMaxNote     = 60;
constexpr int  kPageSize    = 10;
constexpr int  kLogicalW    = 1280;
constexpr int  kLogicalH    = 720;

// Build the window icon procedurally: a dark rounded panel with a cyan
// waveform (matches PokeyForge.ico used for the .exe).
SDL_Surface* MakeAppIcon()
{
    const int N = 64;
    SDL_Surface* s = SDL_CreateSurface(N, N, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    Uint8* base = static_cast<Uint8*>(s->pixels);
    int pitch = s->pitch;
    auto put = [&](int x, int y, Uint8 R, Uint8 G, Uint8 B, Uint8 A) {
        if (x < 0 || y < 0 || x >= N || y >= N) return;
        Uint8* p = base + y * pitch + x * 4;
        p[0] = R; p[1] = G; p[2] = B; p[3] = A;
    };
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            bool border = (x < 3 || y < 3 || x >= N - 3 || y >= N - 3);
            if (border) put(x, y, 80, 140, 220, 255);
            else        put(x, y, 20, 24, 32, 255);
        }
    // Two-cycle waveform across the middle.
    for (int x = 8; x < N - 8; ++x) {
        double t = (double)(x - 8) / (N - 16);
        double v = std::sin(t * 2.0 * 3.14159265 * 2.0);
        int y = (int)(N / 2 - v * (N / 4));
        for (int d = -1; d <= 1; ++d) put(x, y + d, 90, 210, 250, 255);
    }
    return s;
}

// A small centred "busy" popup, presented before a blocking operation so the
// user knows the app is working (the window is frozen while the op runs).
void DrawBusy(SDL_Renderer* r, const char* msg)
{
    SDL_SetRenderDrawColor(r, 16, 18, 24, 255);
    SDL_RenderClear(r);
    int pw = 520, ph = 120;
    int px = (kLogicalW - pw) / 2, py = (kLogicalH - ph) / 2;
    SDL_SetRenderDrawColor(r, 30, 34, 44, 255);
    SDL_FRect p{ (float)px, (float)py, (float)pw, (float)ph };
    SDL_RenderFillRect(r, &p);
    SDL_SetRenderDrawColor(r, 235, 150, 40, 255);
    SDL_RenderRect(r, &p);
    SDL_SetRenderScale(r, 2.0f, 2.0f);
    SDL_SetRenderDrawColor(r, 255, 220, 90, 255);
    SDL_RenderDebugText(r, (px + 24) / 2.0f, (py + 24) / 2.0f, "Please wait...");
    SDL_SetRenderScale(r, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(r, 200, 200, 210, 255);
    SDL_RenderDebugText(r, (float)(px + 24), (float)(py + 72), msg);
    SDL_RenderPresent(r);
}

// Loading splash drawn during startup. `status` is the current step.
void DrawSplash(SDL_Renderer* r, const char* status)
{
    SDL_SetRenderDrawColor(r, 16, 18, 24, 255);
    SDL_RenderClear(r);

    int pw = 560, ph = 220;
    int px = (kLogicalW - pw) / 2;
    int py = (kLogicalH - ph) / 2;

    SDL_SetRenderDrawColor(r, 28, 32, 40, 255);
    SDL_FRect panel{ (float)px, (float)py, (float)pw, (float)ph };
    SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawColor(r, 80, 140, 220, 255);
    SDL_RenderRect(r, &panel);

    // A decorative waveform across the panel.
    SDL_SetRenderDrawColor(r, 60, 120, 200, 255);
    int wy = py + 150, ww = pw - 80, wx = px + 40;
    float prevx = (float)wx, prevy = (float)wy;
    for (int i = 1; i <= ww; ++i) {
        double t = (double)i / ww;
        double v = std::sin(t * 2.0 * 3.14159265 * 3.0);
        float cy = (float)(wy - v * 22.0);
        SDL_RenderLine(r, prevx, prevy, (float)(wx + i), cy);
        prevx = (float)(wx + i); prevy = cy;
    }

    // Big title via render scale.
    SDL_SetRenderScale(r, 4.0f, 4.0f);
    SDL_SetRenderDrawColor(r, 255, 220, 90, 255);
    SDL_RenderDebugText(r, (px + 40) / 4.0f, (py + 40) / 4.0f, "PokeyForge");
    SDL_SetRenderScale(r, 1.0f, 1.0f);

    SDL_SetRenderDrawColor(r, 200, 200, 210, 255);
    SDL_RenderDebugText(r, (float)(px + 42), (float)(py + 96), "RMT .RTI instrument auditioner");
    SDL_SetRenderDrawColor(r, 235, 150, 40, 255);
    SDL_RenderDebugText(r, (float)(px + 42), (float)(py + 116), "written by RetroCoder");
    SDL_SetRenderDrawColor(r, 130, 200, 240, 255);
    SDL_RenderDebugText(r, (float)(px + 42), (float)(py + ph - 24), status ? status : "Loading...");

    SDL_RenderPresent(r);
}

// Show a fatal error to the user (there is no console in the Windows build).
void Fatal(SDL_Window* w, const std::string& msg)
{
    std::fprintf(stderr, "%s\n", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PokeyForge - Error", msg.c_str(), w);
}

std::string LocateDriverObx()
{
    // Prefer the directory the .exe lives in so launching from the Start menu,
    // a shortcut, or a different cwd still finds the bundled driver. Falls
    // back to ../../runtime for running uncopied development builds, and to
    // the cwd as a last resort.
    std::vector<fs::path> roots;
    const char* base = SDL_GetBasePath();
    if (base && *base) roots.emplace_back(base);
#ifdef _WIN32
    // SDL_GetBasePath returns null when SDL hasn't been initialised yet (the
    // --analyse path runs before SDL_Init). Fall back to GetModuleFileName so
    // headless analysis still finds the .obx beside the .exe.
    {
        char exe[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            fs::path p(exe);
            roots.push_back(p.parent_path());
        }
    }
#endif
    roots.push_back(fs::current_path());

    for (const auto& root : roots) {
        fs::path here = root / "rmt_driver_v2.obx";
        if (fs::exists(here)) return here.string();
        fs::path dev = root / ".." / ".." / "runtime" / "rmt_driver_v2.obx";
        std::error_code ec;
        if (fs::exists(dev, ec)) return fs::weakly_canonical(dev, ec).string();
    }
    return std::string{};
}

// Synchronous SDL dialogs. Each pumps events while the native dialog is open
// and returns the chosen path (empty if cancelled / failed).
struct DlgResult { bool done = false; bool cancelled = false; std::string path; };

void SDLCALL DialogCallback(void* userdata, const char* const* filelist, int /*filter*/)
{
    auto* r = static_cast<DlgResult*>(userdata);
    if (filelist == nullptr || filelist[0] == nullptr) {
        r->cancelled = true;
    } else {
        r->path = filelist[0];
    }
    r->done = true;
}

std::string PumpDialog(DlgResult& r)
{
    while (!r.done) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) { r.cancelled = true; r.done = true; }
        }
        SDL_Delay(10);
    }
    return r.cancelled ? std::string{} : r.path;
}

std::string PickFolderModal(SDL_Window* window, const char* start = nullptr)
{
    DlgResult r;
    SDL_ShowOpenFolderDialog(&DialogCallback, &r, window, start, false);
    return PumpDialog(r);
}

std::string SaveFileModal(SDL_Window* window, const SDL_DialogFileFilter* filters, int nfilters, const char* start = nullptr)
{
    DlgResult r;
    SDL_ShowSaveFileDialog(&DialogCallback, &r, window, filters, nfilters, start);
    return PumpDialog(r);
}

std::string OpenFileModal(SDL_Window* window, const SDL_DialogFileFilter* filters, int nfilters, const char* start = nullptr)
{
    DlgResult r;
    SDL_ShowOpenFileDialog(&DialogCallback, &r, window, filters, nfilters, start, false);
    return PumpDialog(r);
}

// Central composition root. Owns all the major subsystems (engine, audio,
// directory model, bank, editor) plus the transient UI state (modals, search
// bar, edit mode, mouse pos, bank context menu) and exposes a handful of
// verb methods (LoadCurrent, AddToBank, NewSlot, ...) that the event loop
// dispatches keys/clicks to. The Gui never sees the App directly — main()
// snapshots the relevant fields into a GuiState per frame.
//
// The "current" instrument has two possible sources (current_source):
//   Source::Directory - loaded from the directory tree; current_rti is the
//                       on-disk file and current_bank_slot is -1.
//   Source::Bank      - loaded from a bank slot; current_bank_slot is that
//                       slot's index. current_rti is empty.
// In both cases, current_instr holds the live, possibly-edited working
// copy and modified=true means it has unsaved changes (which trips the
// Save/Discard/Cancel prompt on navigation via GuardedNav).
struct App {
    RmtEngine    engine;
    Audio        audio;
    Directory    dir;
    Bank         bank;
    RtiFile      current_rti;
    TInstrument  current_instr{};
    bool         current_instr_valid = false;
    int          octave_shift = 0;
    bool         ntsc = false;
    int          last_note_played = -1;
    bool         fullscreen = false;
    bool         show_help = false;
    int          help_page = 0;       // current page of the F1 help overlay
    bool         show_about = false;

    SDL_Window*   window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Gui*         gui = nullptr;
    Config       config;
    std::string  library_path;
    std::string  last_bank_path;
    int          bank_cursor = 0;          // selected bank slot
    std::string  notice;                   // transient status message
    Uint64       notice_until = 0;         // ticks (ms) when notice expires

    // --- Phase 2 editing state ---
    Editor       editor;
    std::vector<byte> working_ata;         // re-encoded edited instrument
    bool         modified = false;         // working copy has unsaved edits
    int          last_play_semitone = 0;   // for Space re-audition in edit mode
    std::vector<TInstrument> undo_stack;   // editor undo snapshots
    std::vector<TInstrument> redo_stack;   // editor redo snapshots

    enum class Source { Directory, Bank };
    Source       current_source = Source::Directory;
    int          current_bank_slot = -1;

    // Bank clipboard (copy/cut/paste between slots).
    Bank::Slot   clipboard;
    bool         clipboard_full = false;
    bool         bank_edit = false;        // when on, Ctrl+C/X/V move slots

    // Search bar.
    bool         search_active = false;
    std::string  search_query;

    // Save / Discard / Cancel prompt before losing edits.
    bool         show_prompt = false;
    std::function<void()> pending_nav;

    // Generic Yes/No confirmation (e.g. overwrite/delete a bank slot).
    bool         show_confirm = false;
    std::string  confirm_msg;
    std::function<void()> confirm_action;

    // Bank-slot right-click context menu.
    bool         bank_menu_open = false;
    int          bank_menu_slot = -1;
    int          bank_menu_x = 0;
    int          bank_menu_y = 0;
    // What the bank menu currently displays in its bottom info section.
    // Pulled from the slot's cached cluster_info on menu open; rewritten
    // when the user clicks "Analyse". Empty string is rendered as "None".
    // The actual per-slot cache lives on Bank::Slot (cluster_info /
    // cluster_hash) so it serialises through manifest.txt with the bank.
    std::string  bank_menu_cluster_info;

    // Envelope horizontal drag-paint state.
    // The user holds the left mouse button down and drags across envelope cells;
    // every cell in the same row gets stamped with the value of the first cell
    // that was clicked.  Binary rows (Filt / Port) toggle on click and then
    // paint the toggled value.  A single undo entry is pushed for the whole
    // drag on mouse-up.
    bool         drag_paint_active   = false;
    int          drag_paint_row      = 0;
    int          drag_paint_value    = 0;
    int          drag_paint_last_col = -1;
    bool         drag_paint_changed  = false;
    TInstrument  drag_paint_start{};

    // Volume popup bar-drag state.  A drag in the popup is collapsed into a
    // single undo entry (same as envelope drag-paint).
    bool         vol_drag_active  = false;
    bool         vol_drag_changed = false;
    TInstrument  vol_drag_start{};

    // Whether the current instrument is treated as stereo (independent VolR/VolL)
    // or mono (VolR is a mirror of VolL).  Auto-detected on load; toggled by the
    // stereo button in the envelope section header.
    bool         instr_stereo = false;

    // Goto-handle drag in the vol popup.
    bool         vol_goto_drag_active = false;
    TInstrument  vol_goto_drag_start{};

    // Directory-tree right-click context menu.
    bool         tree_menu_open = false;
    int          tree_menu_node = -1;
    int          tree_menu_x = 0;
    int          tree_menu_y = 0;

    // Category picker, chained from the tree menu's "Override..." action.
    bool         cat_picker_open = false;
    int          cat_picker_node = -1;
    int          cat_picker_x = 0;
    int          cat_picker_y = 0;

    // Latest mouse position in logical (1280x720) coords. Used by the GUI
    // for hover highlights (currently the bank context menu).
    int          mouse_x = -1;
    int          mouse_y = -1;

    // Manual k-means cluster count override for the next analysis run.
    // 0 = use the automatic choice (ceil(sqrt(N/2)) clamped to [3,12]).
    // K-means cluster count for the next analysis (0 = auto). Default is
    // Analysis::kDefaultKOverride (24, tuned for multi-thousand-instrument
    // libraries). Persisted across runs via analysis.json's "k_override"
    // field and restored by LoadOrRunAnalysis when a cache loads.
    int          k_clusters_override = Analysis::kDefaultKOverride;

    void AskConfirm(const std::string& msg, std::function<void()> action)
    {
        confirm_msg = msg;
        confirm_action = std::move(action);
        show_confirm = true;
    }

    void ResolveConfirm(bool yes)
    {
        show_confirm = false;
        auto act = confirm_action;
        confirm_action = nullptr;
        if (yes && act) act();
    }

    void SetNotice(const std::string& msg)
    {
        notice = msg;
        notice_until = SDL_GetTicks() + 4000;
        std::printf("%s\n", msg.c_str());
    }

    void SaveConfig()
    {
        config.library   = library_path;
        config.last_bank = last_bank_path;
        config.last_file = dir.CurrentFileIndex();
        config.Save();
    }

    bool LoadCurrent()
    {
        int node = dir.CurrentNodeIndex();
        if (node < 0) {
            current_rti = RtiFile{};
            current_instr_valid = false;
            return false;
        }
        // Stop any note still ringing on the outgoing instrument before
        // overwriting the slot, so the audition cuts cleanly on each switch.
        // Also drop out of edit mode: the cursor/panel state belongs to the
        // outgoing instrument and would be confusing on the new one.
        engine.Silence();
        last_note_played = -1;
        editor.SetActive(false);
        const auto& n = dir.At(node);
        if (!current_rti.LoadFromFile(n.path.c_str())) {
            std::fprintf(stderr, "Failed to load %s\n", n.path.c_str());
            current_instr_valid = false;
            return false;
        }
        engine.LoadInstrumentSlot(kInstrSlot,
                                  current_rti.AtaBlob().data(),
                                  current_rti.AtaBlob().size());
        instr_stereo = false;
        current_instr_valid = current_rti.ToInstrument(current_instr, instr_stereo);
        working_ata    = current_rti.AtaBlob();
        modified       = false;
        current_source = Source::Directory;
        current_bank_slot = -1;
        undo_stack.clear(); redo_stack.clear();
        editor.Clamp(current_instr);

        int total = (int)dir.AllFiles().size();
        int pos   = dir.CurrentFileIndex() + 1;
        std::printf("[%d/%d] %s  (\"%s\", v%d, %zu ATA bytes)\n",
                    pos, total, n.path.c_str(),
                    current_rti.Name().c_str(),
                    current_rti.Version(),
                    current_rti.AtaBlob().size());
        return true;
    }

    // Load a filled bank slot as the current instrument (audition + edit).
    bool LoadBankSlot(int slot)
    {
        if (slot < 0 || slot >= Bank::SLOT_COUNT) return false;
        const Bank::Slot& s = bank.At(slot);
        if (!s.used) return false;

        engine.Silence();
        last_note_played = -1;
        editor.SetActive(false);
        engine.LoadInstrumentSlot(kInstrSlot, s.ata.data(), s.ata.size());
        instr_stereo = false;
        current_instr_valid = DecodeAta(s.ata, current_instr, instr_stereo);
        // The decoder blanks the name (the ATA blob has none); restore it from
        // the bank slot so the header and later commits keep the real name.
        if (current_instr_valid) {
            std::memset(current_instr.name, ' ', INSTRUMENT_NAME_MAX_LEN);
            size_t nm = std::min<size_t>(s.name.size(), INSTRUMENT_NAME_MAX_LEN);
            std::memcpy(current_instr.name, s.name.data(), nm);
            current_instr.name[INSTRUMENT_NAME_MAX_LEN] = '\0';
        }
        working_ata       = s.ata;
        modified          = false;
        current_source    = Source::Bank;
        current_bank_slot = slot;
        bank_cursor       = slot;
        current_rti = RtiFile{};   // GUI header falls back to instrument name
        undo_stack.clear(); redo_stack.clear();
        editor.Clamp(current_instr);
        SetNotice("Bank slot " + std::to_string(slot) + ": '" + s.name + "' loaded");
        return true;
    }

    // Decode an ATA blob into a TInstrument by wrapping it in an in-memory
    // .RTI image and reusing the existing parser.
    static bool DecodeAta(const std::vector<byte>& ata, TInstrument& out, bool stereo = false)
    {
        std::vector<byte> img;
        img.reserve(4 + 33 + 1 + ata.size());
        img.push_back('R'); img.push_back('T'); img.push_back('I'); img.push_back(1);
        for (int i = 0; i < 33; ++i) img.push_back(' ');
        img.push_back((byte)std::min<size_t>(ata.size(), 255));
        img.insert(img.end(), ata.begin(), ata.end());
        RtiFile tmp;
        if (!tmp.LoadFromMemory(img.data(), img.size())) return false;
        return tmp.ToInstrument(out, stereo);
    }

    // Inspect an ATA blob and return true when any envelope step has a different
    // high nibble (VolR) and low nibble (VolL), indicating stereo content.
    static bool DetectStereoFromAta(const std::vector<byte>& ata)
    {
        // ATA layout: [env_start_idx] [goto_idx] [env_end_idx] [step0] [step1] ...
        // Each step is 3 bytes: [VolByte] [DistByte] [CmdByte]
        if (ata.size() < 4) return false;
        int env_start = (int)(unsigned char)ata[0];
        int env_end   = (int)(unsigned char)ata[2];
        for (int p = env_start; p <= env_end && p + 2 < (int)ata.size(); p += 3) {
            unsigned char b = (unsigned char)ata[p];
            if ((b >> 4) != (b & 0x0F)) return true;
        }
        return false;
    }

    void PlayNote(int semitone)
    {
        last_play_semitone = semitone;
        // No instrument loaded (e.g. after clearing the current bank slot):
        // ignore key presses so the engine's last-loaded data can't play.
        if (!current_instr_valid) return;
        int note = Keyboard::BASE_NOTE + semitone + octave_shift;
        note = std::clamp(note, kMinNote, kMaxNote);
        engine.NoteOn(kBaseTrack, note, kInstrSlot, kPlayVolume);
        last_note_played = note;
    }

    void RetriggerNote()
    {
        if (!current_instr_valid) return;
        int note = Keyboard::BASE_NOTE + last_play_semitone + octave_shift;
        note = std::clamp(note, kMinNote, kMaxNote);
        engine.NoteOn(kBaseTrack, note, kInstrSlot, kPlayVolume);
        last_note_played = note;
    }

    // Sample (play) the currently-selected bank slot at the given pitch,
    // using a dedicated engine slot so the current instrument stays loaded.
    void PlayBankSlot(int semitone)
    {
        if (bank_cursor < 0 || bank_cursor >= Bank::SLOT_COUNT) return;
        const Bank::Slot& s = bank.At(bank_cursor);
        if (!s.used || s.ata.empty()) {
            SetNotice("Slot " + std::to_string(bank_cursor) + " is empty");
            return;
        }
        engine.LoadInstrumentSlot(kSampleSlot, s.ata.data(), s.ata.size());
        int note = Keyboard::BASE_NOTE + semitone + octave_shift;
        note = std::clamp(note, kMinNote, kMaxNote);
        engine.NoteOn(kBaseTrack, note, kSampleSlot, kPlayVolume);
        last_play_semitone = semitone;
        last_note_played = note;
    }

    // Copy the current instrument (working copy) into a bank slot, overriding
    // whatever is there. Leaves the panel instrument unchanged.
    void InsertCurrentIntoSlot(int slot)
    {
        if (!current_instr_valid) { SetNotice("No instrument to insert"); return; }
        if (slot < 0 || slot >= Bank::SLOT_COUNT) return;
        working_ata = RtiFile::InstrumentToAta(current_instr, instr_stereo);
        bank.SetSlot(slot, TrimName(), working_ata,
                     current_rti.Valid() ? current_rti.Path() : "");
        bank_cursor = slot;
        SetNotice("Put '" + TrimName() + "' into slot " + std::to_string(slot) +
                  " (" + std::to_string(bank.UsedCount()) + "/64)");
    }

    // Ctrl+Ins: insert into the selected slot, confirming first if occupied.
    void RequestInsertIntoCursor()
    {
        if (!current_instr_valid) { SetNotice("No instrument to insert"); return; }
        int slot = bank_cursor;
        if (slot >= 0 && slot < Bank::SLOT_COUNT && bank.At(slot).used) {
            AskConfirm("Overwrite slot " + std::to_string(slot) + " ('" +
                       bank.At(slot).name + "') with '" + TrimName() + "'?",
                       [this, slot]() { InsertCurrentIntoSlot(slot); });
        } else {
            InsertCurrentIntoSlot(slot);
        }
    }

    // Ctrl+Del: remove the selected slot, confirming first if occupied.
    void RequestRemoveCursor()
    {
        int slot = bank_cursor;
        if (slot < 0 || slot >= Bank::SLOT_COUNT || !bank.At(slot).used) {
            SetNotice("Slot " + std::to_string(slot) + " is empty");
            return;
        }
        AskConfirm("Delete slot " + std::to_string(slot) + " ('" +
                   bank.At(slot).name + "')?",
                   [this]() { RemoveBankCursorSlot(); });
    }

    std::string TrimName() const
    {
        std::string n(current_instr.name);
        while (!n.empty() && (n.back() == ' ' || n.back() == '\0')) n.pop_back();
        return n;
    }

    // Re-encode the edited instrument and push it live to the engine.
    void ApplyEdit()
    {
        if (!current_instr_valid) return;
        working_ata = RtiFile::InstrumentToAta(current_instr, instr_stereo);
        engine.LoadInstrumentSlot(kInstrSlot, working_ata.data(), working_ata.size());
        modified = true;
        if (current_source == Source::Bank && current_bank_slot >= 0) {
            bank.SetSlot(current_bank_slot, TrimName(), working_ata,
                         current_rti.Valid() ? current_rti.Path() : "");
        }
    }

    // Commit the working copy into the bank (the "Keep" choice).
    bool CommitToBank()
    {
        if (!current_instr_valid) return false;
        working_ata = RtiFile::InstrumentToAta(current_instr, instr_stereo);
        std::string name = TrimName();   // working name (reflects edits)
        std::string path = current_rti.Valid() ? current_rti.Path() : "";

        if (current_source == Source::Bank && current_bank_slot >= 0) {
            bank.SetSlot(current_bank_slot, name, working_ata, path);
        } else {
            int slot = bank.AddWorking(name, working_ata, path);
            if (slot < 0) { SetNotice("Bank full (64/64)."); return false; }
            current_source = Source::Bank;
            current_bank_slot = slot;
            bank_cursor = slot;
        }
        modified = false;
        SetNotice("Kept in bank slot " + std::to_string(current_bank_slot) +
                  " (" + std::to_string(bank.UsedCount()) + "/64)");
        return true;
    }

    // Navigation guarded by the unsaved-edits prompt.
    void GuardedNav(std::function<void()> nav)
    {
        if (modified) {
            pending_nav = std::move(nav);
            show_prompt = true;
        } else {
            nav();
        }
    }

    void ResolvePrompt(char choice) // 'k' keep, 'd' discard, 'c' cancel
    {
        if (choice == 'c') { show_prompt = false; pending_nav = nullptr; return; }
        if (choice == 'k') CommitToBank();
        modified = false;
        show_prompt = false;
        auto nav = pending_nav;
        pending_nav = nullptr;
        if (nav) nav();
    }

    void StepFiles(int delta)
    {
        if (delta == 0) return;
        GuardedNav([this, delta]() {
            dir.StepFiles(delta);
            LoadCurrent();
        });
    }

    void JumpToFile(int idx)
    {
        GuardedNav([this, idx]() {
            dir.SetCurrentFileIndex(idx);
            LoadCurrent();
        });
    }

    // Clicking an instrument field jumps the editor cursor there (turning on
    // Edit mode if needed).
    void EnterEditField(Editor::Panel panel, int a, int b)
    {
        if (!current_instr_valid) return;
        bool was_active = editor.active;
        editor.SetActive(true);
        editor.panel = panel;
        switch (panel) {
            case Editor::Panel::Params:    editor.param_idx = a; break;
            case Editor::Panel::Envelope:  editor.env_col = a; editor.env_row = b; break;
            case Editor::Panel::NoteTable: editor.tbl_idx = a; break;
            case Editor::Panel::Name:      editor.name_pos = a; break;
        }
        editor.Clamp(current_instr);
        if (!was_active) SetNotice("Edit mode ON");
    }

    // Push the pre-edit state for undo (keeps the stack bounded).
    void PushUndo(const TInstrument& before)
    {
        undo_stack.push_back(before);
        if (undo_stack.size() > 128) undo_stack.erase(undo_stack.begin());
    }

    // In mono mode, VolL and VolR share one value per envelope column.
    // After an edit that touched exactly one of the two channels we
    // propagate the new value to the other so the two stay locked
    // together regardless of how the change came in (hex digit, +/-
    // nudge, scroll wheel, drag-paint, etc). Columns where both changed
    // (the unusual case where the same edit hit both rows) are left
    // alone - they were presumably already in sync.
    void MirrorVolMonoAfterEdit(const TInstrument& before)
    {
        if (instr_stereo) return;
        for (int c = 0; c < ENVELOPE_MAX_COLUMNS; ++c) {
            int l_before = before.envelope[c][VOLUMEL];
            int l_after  = current_instr.envelope[c][VOLUMEL];
            int r_before = before.envelope[c][VOLUMER];
            int r_after  = current_instr.envelope[c][VOLUMER];
            bool l_changed = (l_after != l_before);
            bool r_changed = (r_after != r_before);
            if (l_changed && !r_changed)
                current_instr.envelope[c][VOLUMER] = l_after;
            else if (r_changed && !l_changed)
                current_instr.envelope[c][VOLUMEL] = r_after;
        }
    }

    // Run an editor action; if it changed the instrument, record undo + apply.
    template <typename Fn>
    void DoEdit(Fn&& fn)
    {
        TInstrument before = current_instr;
        if (fn()) {
            MirrorVolMonoAfterEdit(before);
            PushUndo(before);
            redo_stack.clear();
            ApplyEdit();
        }
    }

    // Push the current working instrument live to the engine + bank slot.
    void ReencodeLive()
    {
        working_ata = RtiFile::InstrumentToAta(current_instr, instr_stereo);
        engine.LoadInstrumentSlot(kInstrSlot, working_ata.data(), working_ata.size());
        if (current_source == Source::Bank && current_bank_slot >= 0)
            bank.SetSlot(current_bank_slot, TrimName(), working_ata,
                         current_rti.Valid() ? current_rti.Path() : "");
        editor.Clamp(current_instr);
    }

    void ToggleStereo()
    {
        instr_stereo = !instr_stereo;
        ReencodeLive();
        SetNotice(instr_stereo ? "Stereo mode" : "Mono mode");
    }

    void ToggleField()         { DoEdit([&]{ return editor.ToggleBinary(current_instr); }); }

    // Cycle a small-range param (maxv ≤ 3, not binary) forward by one step.
    void CycleField()
    {
        if (!editor.active || editor.panel != Editor::Panel::Params) return;
        int pi, maxv; bool flag;
        Editor::ParamCell(editor.param_idx, pi, maxv, flag);
        if (maxv < 2 || maxv > 3) return;   // binary handled by ToggleField; >3 = use keyboard
        DoEdit([&]{
            int nv = (current_instr.parameters[pi] + 1) % (maxv + 1);
            if (nv == current_instr.parameters[pi]) return false;
            current_instr.parameters[pi] = nv;
            return true;
        });
    }

    // Begin drag-paint on an envelope cell.  Binary rows (Filt / Port) are
    // toggled on click; the toggled value becomes the paint stamp.
    // In mono mode, dragging a vol cell mirrors to the other vol row at
    // the same column so VolL/VolR stay locked together. Called from the
    // env drag-paint hot path - kept inline-cheap; the no-op branches
    // dominate (drag-paint on FILTER/PORTAMENTO/DIST etc. never touches
    // the vol rows).
    void MirrorVolMonoCell(int col, int row)
    {
        if (instr_stereo) return;
        if (col < 0 || col >= ENVELOPE_MAX_COLUMNS) return;
        if (row == VOLUMEL)
            current_instr.envelope[col][VOLUMER] = current_instr.envelope[col][VOLUMEL];
        else if (row == VOLUMER)
            current_instr.envelope[col][VOLUMEL] = current_instr.envelope[col][VOLUMER];
    }

    void BeginEnvDragPaint(int col, int row)
    {
        if (!current_instr_valid) return;
        drag_paint_row      = row;
        drag_paint_last_col = col;
        drag_paint_changed  = false;
        drag_paint_start    = current_instr;
        drag_paint_active   = true;
        EnterEditField(Editor::Panel::Envelope, col, row);
        // Binary rows toggle the clicked cell first; the new value becomes the stamp.
        bool is_binary = (row == FILTER || row == PORTAMENTO);
        if (is_binary) {
            int old_val = current_instr.envelope[col][row];
            int new_val = old_val ? 0 : 1;
            current_instr.envelope[col][row] = new_val;
            MirrorVolMonoCell(col, row);
            drag_paint_value = new_val;
            drag_paint_changed = true;
            ApplyEdit();
        } else {
            drag_paint_value = current_instr.envelope[col][row];
        }
    }

    // Continue painting during mouse motion (same row, left button held).
    void ContinueEnvDragPaint(int col, int row)
    {
        if (!drag_paint_active || row != drag_paint_row) return;
        if (col == drag_paint_last_col) return;
        int env_len = current_instr.parameters[PAR_ENV_LENGTH];
        if (col < 0 || col > env_len) return;
        drag_paint_last_col = col;
        if (current_instr.envelope[col][row] == drag_paint_value) return;
        current_instr.envelope[col][row] = drag_paint_value;
        MirrorVolMonoCell(col, row);
        drag_paint_changed = true;
        ApplyEdit();
        editor.env_col = col;
        editor.env_row = row;
    }

    // End drag-paint; push a single undo entry for the whole stroke.
    void EndEnvDragPaint()
    {
        if (!drag_paint_active) return;
        drag_paint_active = false;
        if (drag_paint_changed) {
            PushUndo(drag_paint_start);
            redo_stack.clear();
        }
    }

    // Volume popup bar-drag: set a specific envelope cell value from mouse position.
    void BeginVolDrag(int col, int row, int value)
    {
        if (!current_instr_valid) return;
        vol_drag_start   = current_instr;
        vol_drag_active  = true;
        vol_drag_changed = false;
        int env_len = current_instr.parameters[PAR_ENV_LENGTH];
        if (col < 0 || col > env_len || row < 0 || row >= ENVROWS) return;
        current_instr.envelope[col][row] = std::clamp(value, 0, 15);
        if (!instr_stereo) {
            if (row == VOLUMEL)
                current_instr.envelope[col][VOLUMER] = current_instr.envelope[col][VOLUMEL];
            else
                current_instr.envelope[col][VOLUMEL] = current_instr.envelope[col][VOLUMER];
        }
        vol_drag_changed = true;
        ApplyEdit();
        editor.env_col = col;
        editor.env_row = row;
    }

    void ContinueVolDrag(int col, int row, int value)
    {
        if (!vol_drag_active || !current_instr_valid) return;
        int env_len = current_instr.parameters[PAR_ENV_LENGTH];
        if (col < 0 || col > env_len || row < 0 || row >= ENVROWS) return;
        int v = std::clamp(value, 0, 15);
        if (current_instr.envelope[col][row] == v) return;
        current_instr.envelope[col][row] = v;
        if (!instr_stereo) {
            if (row == VOLUMEL)
                current_instr.envelope[col][VOLUMER] = current_instr.envelope[col][VOLUMEL];
            else
                current_instr.envelope[col][VOLUMEL] = current_instr.envelope[col][VOLUMER];
        }
        vol_drag_changed = true;
        ApplyEdit();
        editor.env_col = col;
        editor.env_row = row;
    }

    void EndVolDrag()
    {
        if (!vol_drag_active) return;
        vol_drag_active = false;
        if (vol_drag_changed) {
            PushUndo(vol_drag_start);
            redo_stack.clear();
        }
    }

    // Apply a new goto column value, clamped to valid range.
    void ApplyGotoCol(int col)
    {
        int env_len = current_instr.parameters[PAR_ENV_LENGTH];
        col = std::clamp(col, 0, env_len);
        if (current_instr.parameters[PAR_ENV_GOTO] == col) return;
        current_instr.parameters[PAR_ENV_GOTO] = col;
        ApplyEdit();
    }

    void BeginGotoDrag(int col)
    {
        if (!current_instr_valid) return;
        vol_goto_drag_start  = current_instr;
        vol_goto_drag_active = true;
        ApplyGotoCol(col);
    }

    void ContinueGotoDrag(int col)
    {
        if (!vol_goto_drag_active || !current_instr_valid) return;
        ApplyGotoCol(col);
    }

    void EndGotoDrag()
    {
        if (!vol_goto_drag_active) return;
        vol_goto_drag_active = false;
        if (current_instr.parameters[PAR_ENV_GOTO] !=
            vol_goto_drag_start.parameters[PAR_ENV_GOTO]) {
            PushUndo(vol_goto_drag_start);
            redo_stack.clear();
        }
    }

    // Copy all VolL values to VolR (^ button: VolL -> VolR).
    void CopyVolLToVolR()
    {
        if (!current_instr_valid) return;
        TInstrument before = current_instr;
        int env_len = current_instr.parameters[PAR_ENV_LENGTH];
        bool changed = false;
        for (int c = 0; c <= env_len; ++c) {
            byte v = current_instr.envelope[c][VOLUMEL];
            if (current_instr.envelope[c][VOLUMER] != v) {
                current_instr.envelope[c][VOLUMER] = v;
                changed = true;
            }
        }
        if (changed) { PushUndo(before); redo_stack.clear(); ApplyEdit(); }
    }

    // Copy all VolR values to VolL (v button: VolR -> VolL).
    void CopyVolRToVolL()
    {
        if (!current_instr_valid) return;
        TInstrument before = current_instr;
        int env_len = current_instr.parameters[PAR_ENV_LENGTH];
        bool changed = false;
        for (int c = 0; c <= env_len; ++c) {
            byte v = current_instr.envelope[c][VOLUMER];
            if (current_instr.envelope[c][VOLUMEL] != v) {
                current_instr.envelope[c][VOLUMEL] = v;
                changed = true;
            }
        }
        if (changed) { PushUndo(before); redo_stack.clear(); ApplyEdit(); }
    }

    // Reload from disk and discard all unsaved edits + both stacks.
    void RevertFromDisk()
    {
        if (current_source == Source::Bank) {
            // Bank slots have no on-disk source to revert to; re-decode ATA.
            if (current_bank_slot >= 0 && bank.At(current_bank_slot).used) {
                TInstrument reverted{};
                const auto& slot = bank.At(current_bank_slot);
                if (DecodeAta(slot.ata, reverted)) {
                    current_instr = reverted;
                    current_instr_valid = true;
                }
            }
        } else {
            // Directory source: reload from disk.
            RtiFile fresh;
            if (fresh.LoadFromFile(current_rti.Path().c_str())) {
                current_rti = fresh;
                instr_stereo = false;
                current_instr_valid = current_rti.ToInstrument(current_instr, instr_stereo);
            }
        }
        undo_stack.clear();
        redo_stack.clear();
        modified = false;
        if (current_instr_valid) {
            working_ata = RtiFile::InstrumentToAta(current_instr, instr_stereo);
            engine.LoadInstrumentSlot(kInstrSlot, working_ata.data(), working_ata.size());
        }
        editor.Clamp(current_instr);
        SetNotice("Reverted to saved");
    }
    void EditHex(int nibble)   { DoEdit([&]{ return editor.InputHex(nibble, current_instr); }); }
    void EditNudge(int delta)  { DoEdit([&]{ return editor.Increment(delta, current_instr); }); }
    void EditChar(char c)      { DoEdit([&]{ return editor.InsertChar(c, current_instr); }); }
    void EditBackspace()       { DoEdit([&]{ return editor.Backspace(current_instr); }); }
    void EditDeleteForward()   { DoEdit([&]{ return editor.DeleteForward(current_instr); }); }

    void Undo()
    {
        if (undo_stack.empty()) { SetNotice("Nothing to undo"); return; }
        redo_stack.push_back(current_instr);
        current_instr = undo_stack.back();
        undo_stack.pop_back();
        ReencodeLive();
        modified = !undo_stack.empty();
        SetNotice("Undo");
    }

    void Redo()
    {
        if (redo_stack.empty()) { SetNotice("Nothing to redo"); return; }
        undo_stack.push_back(current_instr);
        current_instr = redo_stack.back();
        redo_stack.pop_back();
        ReencodeLive();
        modified = true;
        SetNotice("Redo");
    }

    // --- Bank clipboard (copy / cut / paste between slots) ---
    void CopySlot()
    {
        if (bank_cursor < 0 || !bank.At(bank_cursor).used) { SetNotice("Slot is empty"); return; }
        clipboard = bank.At(bank_cursor);
        clipboard_full = true;
        SetNotice("Copied slot " + std::to_string(bank_cursor) + " ('" + clipboard.name + "')");
    }
    void CutSlot()
    {
        if (bank_cursor < 0 || !bank.At(bank_cursor).used) { SetNotice("Slot is empty"); return; }
        clipboard = bank.At(bank_cursor);
        clipboard_full = true;
        std::string nm = clipboard.name;
        bank.Remove(bank_cursor);
        SetNotice("Cut slot " + std::to_string(bank_cursor) + " ('" + nm + "')");
    }
    void PasteSlot()
    {
        if (!clipboard_full) { SetNotice("Clipboard empty"); return; }
        bank.SetSlot(bank_cursor, clipboard.name, clipboard.ata, clipboard.source_path);
        SetNotice("Pasted into slot " + std::to_string(bank_cursor) + " ('" + clipboard.name + "')");
    }

    void ToggleParentFolder()
    {
        int node = dir.CurrentNodeIndex();
        if (node < 0) return;
        int parent = dir.At(node).parent;
        if (parent < 0) return;
        dir.ToggleExpanded(parent);
        std::printf("Folder %s -> %s\n",
                    dir.At(parent).path.c_str(),
                    dir.At(parent).expanded ? "expanded" : "collapsed");
    }

    void AddToBank()
    {
        if (!current_instr_valid) return;
        // Editing a bank slot: '+' just commits the edit back in place.
        if (current_source == Source::Bank) { CommitToBank(); return; }

        // Sound-identical instruments are deduplicated by ATA blob (names and
        // source paths are ignored). If the same sound is already in a slot,
        // move the highlight to that slot instead of creating a duplicate.
        std::vector<byte> ata = RtiFile::InstrumentToAta(current_instr, instr_stereo);
        int existing = bank.IndexOfAta(ata);
        if (existing >= 0) {
            bank_cursor = existing;
            SetNotice("Already in bank: slot " + std::to_string(existing) +
                      " ('" + bank.At(existing).name + "')");
            return;
        }
        CommitToBank();
    }

    // Right-click on a directory file: add that file to the bank without
    // disturbing the currently-loaded instrument or its edit state. If the
    // same sound is already in the bank, move the highlight to it instead.
    void AddFileNodeToBank(int node)
    {
        if (node < 0 || node >= dir.NodeCount()) return;
        const auto& n = dir.At(node);
        if (n.type != Directory::NodeType::File) return;

        RtiFile rti;
        if (!rti.LoadFromFile(n.path.c_str())) {
            SetNotice("Failed to load " + fs::path(n.path).filename().string());
            return;
        }
        int existing = bank.IndexOfAta(rti.AtaBlob());
        if (existing >= 0) {
            bank_cursor = existing;
            SetNotice("Already in bank: slot " + std::to_string(existing) +
                      " ('" + bank.At(existing).name + "')");
            return;
        }
        int slot = bank.Add(rti);
        if (slot < 0) { SetNotice("Bank full (64/64)."); return; }
        bank_cursor = slot;   // move highlight to the freshly-added slot
        SetNotice("Added '" + rti.Name() + "' to slot " + std::to_string(slot) +
                  " (" + std::to_string(bank.UsedCount()) + "/64)");
    }

    void RemoveFromBank()
    {
        if (current_source == Source::Bank && current_bank_slot >= 0) {
            std::string nm = bank.At(current_bank_slot).name;
            bank.Remove(current_bank_slot);
            current_source = Source::Directory;
            current_bank_slot = -1;
            SetNotice("Removed '" + nm + "' (" + std::to_string(bank.UsedCount()) + "/64)");
            return;
        }
        if (current_rti.Valid() && bank.RemoveByPath(current_rti.Path())) {
            SetNotice("Removed '" + current_rti.Name() + "' (" +
                      std::to_string(bank.UsedCount()) + "/64)");
        } else {
            SetNotice("Not in bank.");
        }
    }

    void MoveBankCursor(int delta)
    {
        bank_cursor = (bank_cursor + delta + Bank::SLOT_COUNT) % Bank::SLOT_COUNT;
    }

    // Move the bank cursor AND - when the new slot is occupied - load that
    // instrument into the editor panels (same effect as left-clicking a
    // filled slot). Empty slots just move the cursor without touching
    // what's currently loaded, so the user can walk over gaps without
    // losing their in-flight edit. Bound to Ctrl+Left/Right/Up/Down so
    // arrow stepping doubles as a "navigate + audition" gesture; the
    // Ctrl+letter audition path still plays without loading.
    void MoveBankCursorAndLoad(int delta)
    {
        MoveBankCursor(delta);
        if (bank_cursor < 0 || bank_cursor >= Bank::SLOT_COUNT) return;
        if (!bank.At(bank_cursor).used) return;
        int slot = bank_cursor;
        GuardedNav([this, slot]() { LoadBankSlot(slot); });
    }

    void RemoveBankCursorSlot()
    {
        if (bank_cursor < 0 || bank_cursor >= Bank::SLOT_COUNT) return;
        if (!bank.At(bank_cursor).used) { SetNotice("Slot is empty."); return; }
        std::string name = bank.At(bank_cursor).name;
        bank.Remove(bank_cursor);
        SetNotice("Removed slot " + std::to_string(bank_cursor) + " ('" + name +
                  "')  (" + std::to_string(bank.UsedCount()) + "/64)");
    }

    void SaveBankDialog()
    {
        if (bank.UsedCount() == 0) { SetNotice("Bank is empty - nothing to save."); return; }
        static const SDL_DialogFileFilter filters[] = {
            { "RMT module", "rmt" },
        };
        std::string path = SaveFileModal(window, filters, 1,
                                         library_path.empty() ? nullptr : library_path.c_str());
        if (path.empty()) { SetNotice("Save cancelled."); return; }

        // Ensure a .rmt extension.
        fs::path rmt(path);
        if (rmt.extension() != ".rmt") rmt += ".rmt";

        // Write the single-file .rmt plus a sibling <stem>_rti folder of .RTI.
        bool rmt_ok = bank.SaveRmt(rmt.string());
        fs::path rti_dir = rmt.parent_path() / (rmt.stem().string() + "_rti");
        int n = bank.SaveTo(rti_dir.string());

        last_bank_path = rmt.string();
        SaveConfig();

        if (rmt_ok && n >= 0) {
            bank.MarkAllClean();   // everything is now on disk -> green
            SetNotice("Saved " + rmt.filename().string() + " + " +
                      std::to_string(n) + " .rti in " + rti_dir.filename().string() + "/");
        } else {
            SetNotice("Save failed.");
        }
    }

    // ---------- Bank-slot context-menu actions ----------

    // Close every right-click popup (bank menu, tree menu, category picker).
    // The Open* helpers below call this first so only one popup is ever on
    // screen at a time. ESC also routes through here when a popup is open.
    void CloseAllPopups()
    {
        bank_menu_open   = false;
        bank_menu_cluster_info.clear();
        tree_menu_open   = false;
        cat_picker_open  = false;
    }
    bool AnyPopupOpen() const
    {
        return bank_menu_open || tree_menu_open || cat_picker_open;
    }

    void OpenBankSlotMenu(int slot, int x, int y)
    {
        if (slot < 0 || slot >= Bank::SLOT_COUNT) return;
        CloseAllPopups();
        bank_menu_open = true;
        bank_menu_slot = slot;
        bank_menu_x    = x;
        bank_menu_y    = y;
        bank_menu_cluster_info = CachedBankClusterInfo(slot);
    }

    void CloseBankSlotMenu()
    {
        bank_menu_open = false;
        bank_menu_cluster_info.clear();
    }

    // Look up the cluster info to display for a slot. Returns the cached
    // string when the slot is still occupied AND the cached hash matches
    // the slot's current ATA (so an Import/Paste/edit since the last
    // Analyse correctly returns nothing). Empty string is what the menu
    // renders as "None".
    std::string CachedBankClusterInfo(int slot) const
    {
        if (slot < 0 || slot >= Bank::SLOT_COUNT) return std::string{};
        const Bank::Slot& s = bank.At(slot);
        if (!s.used) return std::string{};
        if (s.cluster_info.empty()) return std::string{};
        // Hash mismatch = slot was mutated since the cluster info was
        // computed (Import/Paste/editor save changed the ATA). Treat as
        // stale so the menu shows "None" and prompts a re-Analyse.
        if (Analysis::HashAta(s.ata) != s.cluster_hash) return std::string{};
        return s.cluster_info;
    }

    // Open the directory-tree right-click menu anchored at (x,y) for the
    // given file node.
    void OpenTreeMenu(int node, int x, int y)
    {
        if (node < 0 || node >= dir.NodeCount()) return;
        if (dir.At(node).type != Directory::NodeType::File) return;
        CloseAllPopups();
        tree_menu_open = true;
        tree_menu_node = node;
        tree_menu_x    = x;
        tree_menu_y    = y;
    }

    void CloseTreeMenu() { tree_menu_open = false; }

    void OpenCategoryPicker(int node, int x, int y)
    {
        if (node < 0 || node >= dir.NodeCount()) return;
        CloseAllPopups();
        cat_picker_open = true;
        cat_picker_node = node;
        cat_picker_x    = x;
        cat_picker_y    = y;
    }
    void CloseCategoryPicker() { cat_picker_open = false; }

    // Render the bank slot's instrument through the engine and assign it
    // to the nearest existing library cluster. Returns a multi-line string
    // suitable for the bank menu's bottom info area:
    //   "Cluster 5 - Bass + Pad (dark, sustained)"
    //   "Members: 24"
    // On any failure (no engine, no patched DLL, no clustered library, no
    // valid audio render) returns a one-line explanatory message.
    std::string FindClusterForBankSlot(int slot)
    {
        if (slot < 0 || slot >= Bank::SLOT_COUNT) return "Invalid slot";
        const Bank::Slot& s = bank.At(slot);
        if (!s.used)                   return "Empty slot";
        if (!Pokey::HasAnalysisAbi())  return "sa_pokey.dll: no analysis ABI";
        const int ncl = dir.ClusterCount();
        if (ncl <= 0)                  return "No clusters - run F7 first";

        // Pause SDL audio so the tap doesn't race against the audio thread,
        // render the slot's instrument, restore audio.
        audio.Pause();
        Analysis::Features feats = Analysis::ExtractFeaturesOneShot(engine, s.ata);
        audio.Resume();
        if (!feats.valid)              return "Audio render failed";

        // Build per-cluster centroids from the library's clustered files.
        // Centroid distance is computed in the same normalised feature
        // space used by Analysis::FillVec (Hz axes divided by 22050) so
        // it lines up with how Analysis::Run did the original clustering.
        std::vector<std::array<float, 8>> centroids(ncl, std::array<float, 8>{});
        std::vector<int> counts(ncl, 0);
        for (int node : dir.AllFiles()) {
            const auto& n = dir.At(node);
            if (!n.audio_valid || n.cluster_id < 0 || n.cluster_id >= ncl) continue;
            centroids[n.cluster_id][0] += n.audio[0];
            centroids[n.cluster_id][1] += n.audio[1];
            centroids[n.cluster_id][2] += n.audio[2];
            centroids[n.cluster_id][3] += n.audio[3];
            centroids[n.cluster_id][4] += n.audio[4];
            centroids[n.cluster_id][5] += n.audio[5] / 22050.0f;
            centroids[n.cluster_id][6] += n.audio[6] / 22050.0f;
            centroids[n.cluster_id][7] += n.audio[7];
            counts[n.cluster_id]++;
        }
        int populated = 0;
        for (int c = 0; c < ncl; ++c) {
            if (counts[c] == 0) continue;
            for (int d = 0; d < 8; ++d) centroids[c][d] /= (float)counts[c];
            ++populated;
        }
        if (populated == 0)            return "No clustered library files";

        float q[8] = {
            feats.rms_early, feats.rms_mid, feats.rms_late,
            feats.zcr, feats.peak_pos,
            feats.centroid / 22050.0f, feats.rolloff / 22050.0f,
            feats.flux,
        };

        int   best_cl   = -1;
        float best_dist = 1e30f;
        for (int c = 0; c < ncl; ++c) {
            if (counts[c] == 0) continue;
            float d2 = 0.0f;
            for (int d = 0; d < 8; ++d) {
                float diff = q[d] - centroids[c][d];
                d2 += diff * diff;
            }
            if (d2 < best_dist) { best_dist = d2; best_cl = c; }
        }
        if (best_cl < 0)               return "No matching cluster found";

        // Reuse the same descriptive-label helper the cluster headers use,
        // so the bank menu's label format matches what's shown in the tree.
        std::vector<int> members;
        members.reserve((size_t)counts[best_cl]);
        for (int node : dir.AllFiles()) {
            if (dir.At(node).cluster_id == best_cl) members.push_back(node);
        }
        std::string label = dir.ClusterCharacterLabel(members);

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "Cluster %d - %s\nMembers: %d",
            best_cl + 1, label.empty() ? "(no descriptor)" : label.c_str(),
            counts[best_cl]);
        return std::string(buf);
    }

    // Analyse every used slot in the bank and store the result on each
    // Bank::Slot (which serialises through manifest.txt). Triggered by the
    // "Analyse" button on the bank panel. Updates the notice bar with a
    // progress count while it runs and a final summary when done.
    void AnalyseAllBankSlots()
    {
        if (!Pokey::HasAnalysisAbi()) {
            SetNotice("Bank Analyse: sa_pokey.dll has no analysis ABI");
            return;
        }
        if (dir.ClusterCount() <= 0) {
            SetNotice("Bank Analyse: no clusters - run F7 first");
            return;
        }
        int used = bank.UsedCount();
        if (used == 0) {
            SetNotice("Bank Analyse: bank is empty");
            return;
        }

        int done = 0, failed = 0;
        for (int slot = 0; slot < Bank::SLOT_COUNT; ++slot) {
            if (!bank.At(slot).used) continue;
            std::string info = FindClusterForBankSlot(slot);
            // FindClusterForBankSlot returns "Cluster N - ..." on success;
            // anything else (e.g. "Audio render failed") is an error. We
            // still cache it so the user can see what went wrong on a
            // per-slot Analyse, but count it toward the failure tally.
            if (info.rfind("Cluster ", 0) == 0) ++done;
            else                                 ++failed;
            bank.SetClusterInfo(slot, info,
                Analysis::HashAta(bank.At(slot).ata));
        }
        char buf[128];
        if (failed == 0) {
            std::snprintf(buf, sizeof(buf),
                "Bank Analyse: %d / %d slots clustered", done, used);
        } else {
            std::snprintf(buf, sizeof(buf),
                "Bank Analyse: %d ok, %d failed (out of %d)",
                done, failed, used);
        }
        SetNotice(buf);
    }

    // Adjust the manual k-means cluster count for the next analysis run.
    // Negative values clamp to 0 (which means "automatic" inside Analysis).
    void StepClusterCount(int delta)
    {
        int next = k_clusters_override + delta;
        if (next < 0)  next = 0;
        if (next > 24) next = 24;
        k_clusters_override = next;
        if (k_clusters_override == 0) {
            SetNotice("Cluster count: auto (re-run F7 to apply)");
        } else {
            SetNotice("Cluster count: " + std::to_string(k_clusters_override) +
                      " (re-run F7 to apply)");
        }
    }

    // Ctrl+Shift+R: clear every per-file manual override across the whole
    // library. No confirm dialog - the action is fully reversible by
    // setting overrides again.
    void ClearAllOverrides()
    {
        int count = 0;
        for (int f : dir.AllFiles()) {
            if (dir.GetFileManualCategory(f) >= 0) ++count;
        }
        if (count == 0) {
            SetNotice("No manual overrides to clear.");
            return;
        }
        dir.ClearAllManualOverrides();
        SetNotice("Cleared " + std::to_string(count) + " manual override(s)");
    }

    // Build a brand-new TInstrument with sensible defaults so it's audible
    // straight away. Used by NewSlot.
    static void InitBlankInstrument(TInstrument& ins)
    {
        ins = TInstrument{};
        // Name: "New" padded with spaces to the fixed width.
        std::memset(ins.name, ' ', INSTRUMENT_NAME_MAX_LEN);
        ins.name[INSTRUMENT_NAME_MAX_LEN] = '\0';
        const char* nm = "New";
        for (int i = 0; nm[i] != '\0' && i < INSTRUMENT_NAME_MAX_LEN; ++i)
            ins.name[i] = nm[i];
        // Single-step envelope at full volume with a pulse waveform so the
        // user hears something immediately and can edit from there.
        ins.parameters[PAR_TBL_SPEED] = 1;
        ins.envelope[0][VOLUMER]    = 15;
        ins.envelope[0][VOLUMEL]    = 15;
        ins.envelope[0][DISTORTION] = 10;   // pulse
    }

    // Menu: New -> blank instrument in slot, cursor in the name field.
    void NewSlot(int slot)
    {
        if (slot < 0 || slot >= Bank::SLOT_COUNT) return;
        GuardedNav([this, slot]() {
            TInstrument blank{};
            InitBlankInstrument(blank);
            std::vector<byte> ata = RtiFile::InstrumentToAta(blank, /*stereo=*/false);
            bank.SetSlot(slot, "New", ata, /*source=*/"");
            bank_cursor = slot;
            LoadBankSlot(slot);
            // Drop straight into name-edit so the user can type a real name.
            EnterEditField(Editor::Panel::Name, 0, 0);
            SetNotice("New instrument in slot " + std::to_string(slot));
        });
    }

    // Menu: Clear -> empty this slot (with confirm if it's occupied).
    void ClearSlot(int slot)
    {
        if (slot < 0 || slot >= Bank::SLOT_COUNT) return;
        if (!bank.At(slot).used) {
            SetNotice("Slot " + std::to_string(slot) + " is already empty");
            return;
        }
        AskConfirm("Clear slot " + std::to_string(slot) + " ('" +
                   bank.At(slot).name + "')?",
                   [this, slot]() {
            bank.Remove(slot);
            if (current_source == Source::Bank && current_bank_slot == slot) {
                current_bank_slot = -1;
                current_source = Source::Directory;
                current_instr_valid = false;
                current_rti = RtiFile{};
                editor.SetActive(false);
                engine.Silence();
                // Wipe the engine's instrument slot so a stray NoteOn can't
                // resurrect the cleared instrument's POKEY shadow data.
                std::vector<byte> blank(256, 0);
                engine.LoadInstrumentSlot(kInstrSlot, blank.data(), blank.size());
            }
            SetNotice("Slot " + std::to_string(slot) + " cleared");
        });
    }

    // Menu: Export RTI -> save the slot's stored ATA blob as a .rti file.
    void ExportSlot(int slot)
    {
        if (slot < 0 || slot >= Bank::SLOT_COUNT) return;
        const Bank::Slot& sl = bank.At(slot);
        if (!sl.used) {
            SetNotice("Slot " + std::to_string(slot) + " is empty");
            return;
        }
        static const SDL_DialogFileFilter filters[] = { { "RMT instrument", "rti" } };
        std::string path = SaveFileModal(window, filters, 1,
                                         library_path.empty() ? nullptr : library_path.c_str());
        if (path.empty()) { SetNotice("Export cancelled."); return; }
        fs::path rti(path);
        if (rti.extension() != ".rti") rti += ".rti";
        if (RtiFile::WriteFile(rti.string(), sl.name, sl.ata))
            SetNotice("Exported " + rti.filename().string());
        else
            SetNotice("Export failed.");
    }

    // Menu: Import RTI -> load a .rti file directly into this slot.
    void ImportSlot(int slot)
    {
        if (slot < 0 || slot >= Bank::SLOT_COUNT) return;
        static const SDL_DialogFileFilter filters[] = { { "RMT instrument", "rti" } };
        std::string path = OpenFileModal(window, filters, 1,
                                         library_path.empty() ? nullptr : library_path.c_str());
        if (path.empty()) { SetNotice("Import cancelled."); return; }

        RtiFile rti;
        if (!rti.LoadFromFile(path.c_str())) {
            SetNotice("Failed to load " + fs::path(path).filename().string());
            return;
        }

        auto apply = [this, slot, rti]() {
            bank.AddAt(slot, rti);
            bank_cursor = slot;
            LoadBankSlot(slot);
            SetNotice("Imported into slot " + std::to_string(slot));
        };
        if (bank.At(slot).used) {
            AskConfirm("Overwrite slot " + std::to_string(slot) + " ('" +
                       bank.At(slot).name + "') with '" + rti.Name() + "'?", apply);
        } else {
            apply();
        }
    }

    // Ctrl+S: export the current (edited) instrument as a .RTI under a new name.
    void ExportCurrentRti()
    {
        if (!current_instr_valid) { SetNotice("No instrument to export."); return; }
        std::vector<byte> ata = RtiFile::InstrumentToAta(current_instr, instr_stereo);
        std::string name = TrimName();

        static const SDL_DialogFileFilter filters[] = { { "RMT instrument", "rti" } };
        std::string path = SaveFileModal(window, filters, 1,
                                         library_path.empty() ? nullptr : library_path.c_str());
        if (path.empty()) { SetNotice("Export cancelled."); return; }
        fs::path rti(path);
        if (rti.extension() != ".rti") rti += ".rti";

        if (RtiFile::WriteFile(rti.string(), name, ata))
            SetNotice("Exported " + rti.filename().string());
        else
            SetNotice("Export failed.");
    }

    // Open a folder as the library (optionally selecting one file in it).
    // Used by drag-and-drop. Returns false on failure.
    bool OpenLibraryPath(const std::string& folder, const std::string& selectFile)
    {
        if (!dir.Scan(folder)) { SetNotice("Cannot open: " + folder); return false; }
        library_path = folder;
        LoadOrRunAnalysis();
        if (!selectFile.empty()) {
            std::error_code ec;
            fs::path want = fs::absolute(selectFile, ec);
            for (int node : dir.AllFiles())
                if (fs::path(dir.At(node).path) == want) { dir.SelectByNode(node); break; }
        } else {
            dir.SetCurrentFileIndex(0);
        }
        LoadCurrent();
        SaveConfig();
        SetNotice("Library: " + folder + "  (" +
                  std::to_string(dir.AllFiles().size()) + " files)");
        return true;
    }

    // Load a bank from a path (.rmt or a manifest/folder). Returns count, -1
    // on error. Used by F3 and by auto-reload on startup.
    int LoadBankFromPath(const std::string& path)
    {
        fs::path p(path);
        if (p.extension() == ".rmt") return bank.LoadFromRmt(path);
        return bank.LoadFromManifest(path);
    }

    void LoadBankDialog()
    {
        static const SDL_DialogFileFilter filters[] = {
            { "Bank (RMT or manifest)", "rmt;txt" },
            { "RMT module", "rmt" },
            { "Bank manifest", "txt" },
        };
        std::string path = OpenFileModal(window, filters, 3,
                                         library_path.empty() ? nullptr : library_path.c_str());
        if (path.empty()) { SetNotice("Load cancelled."); return; }

        int n = LoadBankFromPath(path);
        if (n < 0) { SetNotice("Bank load failed."); return; }
        bank_cursor = 0;
        last_bank_path = path;
        SaveConfig();
        SetNotice("Loaded bank: " + std::to_string(n) + " instruments from " +
                  fs::path(path).filename().string());
    }

    // --- Search ---
    void BeginSearch()
    {
        search_active = true;
        SetNotice("Search: type to filter, Enter to keep, Esc to clear");
    }
    void UpdateSearch()
    {
        dir.SetFilter(search_query);
        LoadCurrent();
    }
    void EndSearch(bool clear)
    {
        search_active = false;
        if (clear) { search_query.clear(); dir.SetFilter(""); LoadCurrent(); }
    }

    bool SwitchLibrary()
    {
        std::string folder = PickFolderModal(window,
                                             library_path.empty() ? nullptr : library_path.c_str());
        if (folder.empty()) { SetNotice("Switch library cancelled."); return false; }
        if (!dir.Scan(folder)) { SetNotice("Cannot scan: " + folder); return false; }
        library_path = folder;
        // Load cached analysis for the new library, or auto-run if missing -
        // same behaviour as startup so the user never browses an unanalysed
        // library by accident.
        LoadOrRunAnalysis();
        dir.SetCurrentFileIndex(0);
        LoadCurrent();
        SaveConfig();
        SetNotice("Library: " + folder + "  (" +
                  std::to_string(dir.AllFiles().size()) + " files)");
        return true;
    }

    int BankSlotFromMouse(float lx, float ly)
    {
        return gui ? gui->BankSlotAtLogical((int)lx, (int)ly) : -1;
    }

    // Apply the cached analysis.json for the current library if present;
    // otherwise run a fresh analysis (showing a splash so the user knows
    // what the wait is for) and cache it. Always defaults to hiding
    // duplicates after a successful analysis - this is what the curated
    // browsing list shows on launch / library switch.
    //
    // Called from startup, SwitchLibrary, and OpenLibraryPath so every
    // library the user opens ends up analysed; F7 ('Analyse') re-runs
    // analysis explicitly on the same library.
    void LoadOrRunAnalysis()
    {
        int loaded_k = -1;
        if (Analysis::LoadAndApply(dir, library_path, &loaded_k)) {
            dir.SetHideDuplicates(true);
            if (loaded_k >= 0) k_clusters_override = loaded_k;
            std::printf("Loaded analysis.json (k_override = %d)\n",
                        k_clusters_override);
            return;
        }
        if (dir.AllFiles().empty()) return;   // empty library, nothing to do
        // Kill any currently-ringing note before analysis takes over.
        engine.Silence();
        last_note_played = -1;
        Analysis::Options opts;
        opts.engine     = &engine;
        opts.k_override = k_clusters_override;
        opts.progress   = &App::AnalysisProgressThunk;
        opts.progress_ud = this;
        // Pause SDL audio so Analysis::Run can own the engine + audio tap
        // without racing against our playback thread. Resume puts both back.
        audio.Pause();
        Analysis::Summary sum = Analysis::Run(dir, library_path,
                                              /*writeJson=*/true, opts);
        audio.Resume();
        if (sum.ok) {
            dir.SetHideDuplicates(true);
            PrintAnalysisReport(sum);
            SetNotice("Analysed " + std::to_string(sum.total) + " instruments, " +
                      std::to_string(sum.duplicates) + " duplicates, " +
                      std::to_string(sum.clusters) + " clusters");
        }
    }

    // F7: re-run analysis on the current library (classify + find duplicates,
    // hide dupes, rewrite analysis.json). Equivalent to forcing the auto-run
    // path of LoadOrRunAnalysis even when a cache is present.
    void AnalyseLibrary()
    {
        engine.Silence();
        last_note_played = -1;
        Analysis::Options opts;
        opts.engine     = &engine;
        opts.k_override = k_clusters_override;
        opts.progress   = &App::AnalysisProgressThunk;
        opts.progress_ud = this;
        audio.Pause();
        Analysis::Summary sum = Analysis::Run(dir, library_path,
                                              /*writeJson=*/true, opts);
        audio.Resume();
        if (!sum.ok) { SetNotice("Analysis failed."); return; }
        dir.SetHideDuplicates(true);
        LoadCurrent();   // restores the engine's instrument slot to the live one
        PrintAnalysisReport(sum);
        SetNotice("Analysed " + std::to_string(sum.total) + " instruments, " +
                  std::to_string(sum.duplicates) + " duplicates, " +
                  std::to_string(sum.clusters) + " clusters. Saved analysis.json");
    }

    // Thunk that bridges the C-style Analysis progress callback back to
    // an App method. Redraws the splash with the current "N / M" count
    // and pumps SDL events so the window doesn't appear hung on big
    // libraries.
    static void AnalysisProgressThunk(int current, int total, void* userdata)
    {
        auto* app = static_cast<App*>(userdata);
        if (!app || !app->renderer) return;
        char msg[160];
        int pct = total > 0 ? (current * 100) / total : 0;
        std::snprintf(msg, sizeof(msg),
            "Analysing instruments %d / %d  (%d%%)", current, total, pct);
        DrawSplash(app->renderer, msg);
        // Pump the OS event queue so the window stays responsive (move /
        // close events get processed); we drop the events because the main
        // loop owns input.
        SDL_Event drop;
        while (SDL_PollEvent(&drop)) { /* discard */ }
    }

    // Dump a per-category count breakdown to stdout after analysis. Quick
    // way to spot a library that's heavy on one category or to notice that
    // "Other" has too many entries (i.e. the classifier is giving up).
    void PrintAnalysisReport(const Analysis::Summary& sum)
    {
        int counts[(int)Analysis::Category::COUNT] = { 0 };
        int total_files = 0;
        int low_conf    = 0;
        for (int f : dir.AllFiles()) {
            int c = dir.EffectiveCategory(f);
            if (c >= 0 && c < (int)Analysis::Category::COUNT) counts[c]++;
            if (dir.At(f).confidence == 1) ++low_conf;
            ++total_files;
        }
        std::printf("--- Analysis report ----------------------------------\n");
        std::printf("  Library: %s\n", library_path.c_str());
        std::printf("  Files:   %d   (duplicates: %d, clusters: %d)\n",
                    total_files, sum.duplicates, sum.clusters);
        std::printf("  Low-confidence rows: %d\n", low_conf);
        for (int i = 0; i < (int)Analysis::Category::COUNT; ++i) {
            if (counts[i] == 0) continue;
            std::printf("    %-18s %d\n",
                        Analysis::Name((Analysis::Category)i), counts[i]);
        }
        std::printf("------------------------------------------------------\n");
    }

    // F8: toggle the directory tree between folder view and group-by-category.
    void ToggleGrouping()
    {
        bool cat = dir.GetViewMode() == Directory::ViewMode::Category;
        dir.SetViewMode(cat ? Directory::ViewMode::Folder
                            : Directory::ViewMode::Category);
        LoadCurrent();
        SetNotice(cat ? "View: folders" : "View: grouped by category");
    }

    // F10: toggle Cluster view. Falls back to Folder view if the current
    // library has no clusters (analysis didn't run, or k=0).
    void ToggleClusterView()
    {
        if (dir.GetViewMode() == Directory::ViewMode::Cluster) {
            dir.SetViewMode(Directory::ViewMode::Folder);
            LoadCurrent();
            SetNotice("View: folders");
            return;
        }
        if (dir.ClusterCount() <= 0) {
            SetNotice("No clusters yet - run F7 Analyse first.");
            return;
        }
        dir.SetViewMode(Directory::ViewMode::Cluster);
        LoadCurrent();
        SetNotice("View: grouped by cluster (" +
                  std::to_string(dir.ClusterCount()) + " clusters)");
    }

    // Ctrl+R: cycle the current file's manual category override through
    // every Analysis::Category value, ending at "use auto" (-1). The
    // tree's "M" marker shows when an override is set; saving analysis
    // again persists it.
    void CycleManualCategory()
    {
        int node = dir.CurrentNodeIndex();
        if (node < 0) return;
        int cur = dir.GetFileManualCategory(node);
        int next = cur + 1;
        if (next >= (int)Analysis::Category::COUNT) next = -1;   // wrap to auto
        dir.SetFileManualCategory(node, next);
        if (next < 0) {
            SetNotice("Reclassify: cleared override (using auto)");
        } else {
            SetNotice(std::string("Reclassify: ") + Analysis::Name((Analysis::Category)next));
        }
    }

    // F9: show/hide duplicates again.
    void ToggleHideDuplicates()
    {
        dir.SetHideDuplicates(!dir.HideDuplicates());
        LoadCurrent();
        SetNotice(dir.HideDuplicates() ? "Duplicates hidden" : "Duplicates shown");
    }

    // Adjust octave_shift by `delta` semitones (mouse wheel uses ±12 per
    // notch to match the [ / ] keys), clamped to the same ±24 range.
    void AdjustOctave(int delta)
    {
        int next = std::clamp(octave_shift + delta, -24, +24);
        if (next == octave_shift) return;
        octave_shift = next;
        SetNotice("Octave shift " + std::to_string(octave_shift / 12) +
                  " (" + std::to_string(octave_shift) + " semitones)");
    }

    void TogglePalNtsc()
    {
        ntsc = !ntsc;
        engine.SetNTSC(ntsc);
        if (current_rti.Valid()) {
            engine.LoadInstrumentSlot(kInstrSlot,
                                      current_rti.AtaBlob().data(),
                                      current_rti.AtaBlob().size());
        }
        std::printf("Clock: %s (frame %d Hz)\n",
                    ntsc ? "NTSC" : "PAL", ntsc ? 60 : 50);
    }

    void Silence()
    {
        engine.Silence();
        last_note_played = -1;
        std::printf("Silenced.\n");
    }

    void ToggleFullscreen(SDL_Window* window)
    {
        fullscreen = !fullscreen;
        SDL_SetWindowFullscreen(window, fullscreen);
        std::printf("Fullscreen: %s\n", fullscreen ? "ON" : "OFF");
    }
};

bool IsNavKey(SDL_Keycode k)
{
    switch (k) {
        case SDLK_LEFT: case SDLK_RIGHT:
        case SDLK_UP:   case SDLK_DOWN:
        case SDLK_PAGEUP: case SDLK_PAGEDOWN:
        case SDLK_HOME: case SDLK_END:
            return true;
        default: return false;
    }
}

// Hex nibble for a keycode, or -1. Accepts 0-9 and a-f.
int HexNibble(SDL_Keycode k)
{
    if (k >= SDLK_0 && k <= SDLK_9) return (int)(k - SDLK_0);
    if (k >= SDLK_A && k <= SDLK_F) return 10 + (int)(k - SDLK_A);
    return -1;
}

// Printable character for name editing, or 0. Honors Shift for letters.
char NameChar(SDL_Keycode k, bool shift)
{
    if (k >= SDLK_A && k <= SDLK_Z) {
        char c = (char)('a' + (k - SDLK_A));
        return shift ? (char)(c - 32) : c;
    }
    if (k >= SDLK_0 && k <= SDLK_9) return (char)('0' + (k - SDLK_0));
    if (k == SDLK_SPACE) return ' ';
    if (k == SDLK_MINUS) return '-';
    if (k == SDLK_PERIOD) return '.';
    return 0;
}

} // anonymous namespace

// Headless CLI mode: scan a folder, run analysis, write analysis.json
// (and analysis_report.csv) next to the instruments, exit. Used by
// release.ps1 to pre-analyse the bundled instruments folder so the user
// doesn't have to wait for first-launch analysis. No SDL window, no
// audio device, no engine - the analysis pass is parametric-only in
// this release anyway, so all we need is the .RTI parser and the
// Directory + Analysis classes.
#ifdef _WIN32
// Re-attach Windows-subsystem app to the parent console so printf/fprintf
// appear in CMD/PowerShell. No-op when launched without a parent console.
static void AttachParentConsole()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
}
#else
static void AttachParentConsole() {}
#endif

// --smoke-tap <out.raw>
//
// Drives the patched sa_pokey.dll end-to-end without any GUI/SDL. Mutes
// the native audio, installs an audio tap, writes a square-wave to POKEY
// channel 1, runs Pokey_Process for ~500 ms, and dumps the captured float
// samples to <out.raw>. Used to verify that the audio-tap path produces
// non-silent samples *before* re-enabling the full audio-feature pipeline
// in Analysis.cpp. Returns 0 on success.
struct SmokeTapBuffer {
    std::vector<float> samples;
    std::uint32_t      total = 0;
};

static void __cdecl SmokeTapCallback(const float* left, const float* /*right*/,
                                     std::uint32_t count, std::uint32_t /*ts*/,
                                     void* user)
{
    auto* buf = static_cast<SmokeTapBuffer*>(user);
    if (!buf || !left || !count) return;
    buf->samples.insert(buf->samples.end(), left, left + count);
    buf->total += count;
}

static int RunSmokeTap(const std::string& out_path)
{
    AttachParentConsole();

    if (!Pokey::InitDll()) {
        std::fprintf(stderr, "smoke-tap: failed to load sa_pokey.dll\n");
        return 2;
    }
    if (!Pokey::HasAnalysisAbi()) {
        std::fprintf(stderr,
            "smoke-tap: sa_pokey.dll has no analysis ABI exports.\n"
            "  -> The DLL next to PokeyForge.exe is the original RMT build.\n"
            "  -> Replace it with the patched build from AltirraSDL.\n");
        Pokey::DeInitDll();
        return 3;
    }
    std::printf("smoke-tap: %s\n", Pokey::About());
    std::printf("smoke-tap: analysis ABI v%d.%d\n",
                (Pokey::AnalysisAbiVersion() >> 16) & 0xFFFF,
                Pokey::AnalysisAbiVersion() & 0xFFFF);

    // 44100 Hz host rate is unused by the tap (tap fires at the engine's
    // native ~64 kHz), but Pokey_SoundInit needs *some* rate for its
    // cycles-per-tick math inside Advance().
    constexpr std::uint32_t kClockNTSC = 1789773;
    Pokey::SoundInit(kClockNTSC, 44100, 1);

    Pokey::SetMute(true);

    SmokeTapBuffer cap;
    Pokey::SetAudioTap(&SmokeTapCallback, &cap);

    // Square-wave on channel 1: AUDF1 = 0x40, AUDC1 = volume 8 + distortion
    // 0xA0 (pure tone with poly bypass), AUDCTL = 0. That's ~280 Hz, plenty
    // loud, no envelope -> stays on for the full window.
    Pokey::PutByte(0x00, 0x40);   // AUDF1
    Pokey::PutByte(0x01, 0xA8);   // AUDC1 = pure + vol 8
    Pokey::PutByte(0x08, 0x00);   // AUDCTL

    // 500 ms at 44.1 kHz host rate = 22050 host samples. Drive them in
    // 1024-sample chunks so Advance() loops with realistic granularity.
    std::vector<std::uint8_t> throwaway(1024);
    const int total_host = 22050;
    int produced_host = 0;
    while (produced_host < total_host) {
        int want = std::min<int>(1024, total_host - produced_host);
        Pokey::Process(throwaway.data(), (std::uint16_t)want);
        produced_host += want;
    }

    Pokey::SetAudioTap(nullptr, nullptr);
    Pokey::SetMute(false);

    if (cap.samples.empty()) {
        std::fprintf(stderr, "smoke-tap: tap never fired (0 samples captured)\n");
        Pokey::DeInitDll();
        return 4;
    }

    // Summary stats so success/failure is visible without inspecting the file.
    float mn =  1e9f, mx = -1e9f, sum = 0.0f, sqsum = 0.0f;
    for (float v : cap.samples) {
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
        sqsum += v * v;
    }
    const float mean = sum / (float)cap.samples.size();
    const float rms  = std::sqrt(sqsum / (float)cap.samples.size());
    std::printf("smoke-tap: captured %u samples (~%.1f ms @ engine rate)\n",
                cap.total, 1000.0f * (float)cap.total / 63920.0f);
    std::printf("smoke-tap: min=%.4f  max=%.4f  mean=%.4f  rms=%.4f\n",
                mn, mx, mean, rms);

    FILE* fp = std::fopen(out_path.c_str(), "wb");
    if (!fp) {
        std::fprintf(stderr, "smoke-tap: cannot open %s for writing\n", out_path.c_str());
        Pokey::DeInitDll();
        return 5;
    }
    std::fwrite(cap.samples.data(), sizeof(float), cap.samples.size(), fp);
    std::fclose(fp);
    std::printf("smoke-tap: wrote %s (%zu bytes, mono float32 @ ~63920 Hz)\n",
                out_path.c_str(), cap.samples.size() * sizeof(float));

    Pokey::DeInitDll();
    return (rms > 1e-6f) ? 0 : 6;
}

int RunHeadlessAnalyse(const std::string& folder)
{
#ifdef _WIN32
    AttachParentConsole();
#endif
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        std::fprintf(stderr, "PokeyForge --analyse: not a directory: %s\n",
                     folder.c_str());
        return 2;
    }
    Directory dir;
    if (!dir.Scan(folder)) {
        std::fprintf(stderr, "PokeyForge --analyse: scan failed: %s\n",
                     folder.c_str());
        return 3;
    }
    if (dir.AllFiles().empty()) {
        std::fprintf(stderr, "PokeyForge --analyse: no .RTI files under %s\n",
                     folder.c_str());
        return 4;
    }
    std::printf("PokeyForge --analyse: %d .RTI files in %s\n",
                (int)dir.AllFiles().size(), folder.c_str());

    // Bring up a real RmtEngine so Analysis::Run can render audio features
    // through the patched sa_pokey.dll. Without the engine the analysis
    // falls back to parametric-only signals, leaving every features.valid
    // == false in analysis.json.
    Analysis::Options opts;
    RmtEngine engine;
    std::string obx = LocateDriverObx();
    bool engine_up = false;
    if (obx.empty()) {
        std::fprintf(stderr,
            "PokeyForge --analyse: rmt_driver_v2.obx not found next to the .exe.\n"
            "  -> falling back to parametric-only analysis (no audio features)\n");
    } else if (!engine.Init(obx.c_str(), /*ntsc=*/true, kSampleRate)) {
        std::fprintf(stderr,
            "PokeyForge --analyse: engine init failed (sa_pokey.dll or driver load).\n"
            "  -> falling back to parametric-only analysis\n");
    } else if (!Pokey::HasAnalysisAbi()) {
        std::fprintf(stderr,
            "PokeyForge --analyse: sa_pokey.dll has no analysis ABI exports.\n"
            "  -> falling back to parametric-only analysis. Rebuild sa_pokey.dll\n"
            "     from the AltirraSDL patch to enable audio features.\n");
        engine.DeInit();
    } else {
        std::printf("PokeyForge --analyse: audio tap available (ABI v%d.%d)\n",
                    (Pokey::AnalysisAbiVersion() >> 16) & 0xFFFF,
                    Pokey::AnalysisAbiVersion() & 0xFFFF);
        opts.engine = &engine;
        engine_up = true;
    }

    Analysis::Summary sum = Analysis::Run(dir, folder, /*writeJson=*/true, opts);

    if (engine_up) engine.DeInit();

    if (!sum.ok) {
        std::fprintf(stderr, "PokeyForge --analyse: analysis failed\n");
        return 5;
    }
    std::printf("PokeyForge --analyse: %d instruments, %d duplicates, %d clusters\n",
                sum.total, sum.duplicates, sum.clusters);
    return 0;
}

int main(int argc, char* argv[])
{
    // Headless analysis: PokeyForge.exe --analyse <folder>. Returns 0 on
    // success, non-zero on failure. Bypasses all SDL / GUI startup.
    if (argc >= 3 && std::strcmp(argv[1], "--analyse") == 0) {
        return RunHeadlessAnalyse(argv[2]);
    }

    // Audio-tap smoke test: PokeyForge.exe --smoke-tap <out.raw>. Used to
    // verify the patched sa_pokey.dll captures non-silent samples through
    // the analysis tap. Bypasses SDL/GUI entirely.
    if (argc >= 3 && std::strcmp(argv[1], "--smoke-tap") == 0) {
        return RunSmokeTap(argv[2]);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        Fatal(nullptr, std::string("SDL could not start:\n") + SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("PokeyForge " POKEYFORGE_VERSION, kLogicalW, kLogicalH,
                                          SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!window || !renderer) {
        Fatal(window, std::string("Could not create the window:\n") + SDL_GetError());
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window)   SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Render everything at logical 1280x720; SDL letterboxes for fullscreen
    // / resized windows. Lets the GUI layout stay fixed.
    SDL_SetRenderLogicalPresentation(renderer, kLogicalW, kLogicalH,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    // Window icon (the .exe icon comes from app.rc / PokeyForge.ico).
    if (SDL_Surface* icon = MakeAppIcon()) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }

    DrawSplash(renderer, "Starting up...");

    // Resolve the root folder, in priority order:
    //   1. command-line argument (folder or .rti file)
    //   2. last library remembered in playrti.json
    //   3. folder picker
    std::error_code ec;
    fs::path root;
    fs::path initial_file;
    int restore_file_index = -1;

    Config config;
    config.Load();

    if (argc >= 2) {
        fs::path arg = argv[1];
        if (fs::is_directory(arg, ec)) {
            root = arg;
        } else if (fs::is_regular_file(arg, ec)) {
            root = arg.parent_path();
            initial_file = arg;
        } else {
            Fatal(window, std::string("Path not found:\n") + argv[1]);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    } else if (!config.library.empty() && fs::is_directory(config.library, ec)) {
        root = config.library;
        restore_file_index = config.last_file;
    } else {
        // No remembered library and no command-line argument: tell the user
        // what's about to happen, then bring up the folder picker. The
        // splash stays behind the native dialog so the screen isn't blank
        // while they're choosing.
        DrawSplash(renderer, "Looking for instrument folder - please choose one...");
        std::string picked = PickFolderModal(window);
        if (picked.empty()) {
            std::fprintf(stderr, "No folder selected; exiting.\n");
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 0;
        }
        root = picked;
    }

    std::string obx = LocateDriverObx();
    if (obx.empty()) {
        Fatal(window, "rmt_driver_v2.obx was not found next to PokeyForge.exe.\n"
                      "Make sure the runtime files (DLLs + .obx) are present.");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    DrawSplash(renderer, "Loading sound driver...");
    App app;
    if (!app.engine.Init(obx.c_str(), app.ntsc, kSampleRate)) {
        Fatal(window, "Could not initialise the sound engine.\n"
                      "sa_c6502.dll / sa_pokey.dll may be missing or incompatible.");
        return 1;
    }
    if (!app.audio.Open(app.engine, kSampleRate)) {
        Fatal(window, std::string("Could not open the audio device:\n") + SDL_GetError());
        return 1;
    }
    DrawSplash(renderer, "Scanning instrument library...");
    if (!app.dir.Scan(root.string())) {
        Fatal(window, "Could not read the instrument folder:\n" + root.string());
        return 1;
    }
    if (app.dir.AllFiles().empty()) {
        std::fprintf(stderr, "No .RTI files found under: %s\n", root.string().c_str());
    }

    app.window       = window;
    app.renderer     = renderer;
    app.config       = config;
    app.library_path = root.string();
    app.last_bank_path = config.last_bank;

    // Apply cached analysis.json if present, or auto-run a fresh analysis
    // (with a splash) when the library has no cache yet. Always defaults to
    // hiding duplicates so the curated list shows on launch.
    DrawSplash(renderer, "Loading analysis...");
    app.LoadOrRunAnalysis();

    // Auto-reload the last bank if it still exists.
    if (!app.last_bank_path.empty() && fs::exists(app.last_bank_path, ec)) {
        DrawSplash(renderer, "Loading last bank...");
        int n = app.LoadBankFromPath(app.last_bank_path);
        if (n > 0) std::printf("Reloaded bank: %d instruments\n", n);
    }

    if (!initial_file.empty()) {
        fs::path want = fs::absolute(initial_file, ec);
        for (int node : app.dir.AllFiles()) {
            if (fs::path(app.dir.At(node).path) == want) {
                app.dir.SelectByNode(node);
                break;
            }
        }
    } else if (restore_file_index > 0) {
        app.dir.SetCurrentFileIndex(restore_file_index);
    }
    app.LoadCurrent();

    // Initialise the TTF text renderer (JetBrains Mono, next to the .exe).
    TextRenderer text_renderer;
    {
        std::string font_path = std::string(SDL_GetBasePath()) + "JetBrainsMono-Regular.ttf";
        if (!text_renderer.Init(renderer, font_path.c_str(), 13)) {
            SDL_Log("TTF init failed; falling back to debug font");
        }
    }

    Gui gui;
    gui.SetTextRenderer(text_renderer.Ok() ? &text_renderer : nullptr);
    app.gui = &gui;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) { running = false; continue; }

            // Drag-and-drop a folder (open as library) or a .rti (open its
            // folder and select it).
            if (event.type == SDL_EVENT_DROP_FILE) {
                if (app.show_prompt || app.show_confirm) continue;
                std::string p = event.drop.data ? event.drop.data : "";
                std::error_code dec;
                if (fs::is_directory(p, dec)) {
                    app.GuardedNav([&app, p]() { app.OpenLibraryPath(p, ""); });
                } else if (fs::is_regular_file(p, dec)) {
                    std::string folder = fs::path(p).parent_path().string();
                    app.GuardedNav([&app, folder, p]() { app.OpenLibraryPath(folder, p); });
                }
                continue;
            }

            // About popup: any click dismisses it.
            if (app.show_about && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                app.show_about = false;
                continue;
            }
            // Help overlay: click the Prev / Next paging buttons to change
            // page; click anywhere outside the panel to dismiss; clicks
            // INSIDE the panel are ignored so the user can browse without
            // accidentally closing the overlay.
            if (app.show_help && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                SDL_ConvertEventToRenderCoordinates(renderer, &event);
                int mx = (int)event.button.x;
                int my = (int)event.button.y;
                int dir = gui.HelpPageButtonAtLogical(mx, my);
                if (dir != 0) {
                    app.help_page = (app.help_page + dir + Gui::kHelpPageCount)
                                    % Gui::kHelpPageCount;
                } else if (!gui.PointInHelpPanel(mx, my)) {
                    app.show_help = false;
                }
                continue;
            }

            // Mouse wheel: by default steps the instrument selection (up =
            // previous). When a field is being edited and the cursor is
            // outside the directory pane, the wheel nudges the focused
            // numeric field by +/- 1 instead (wheel up = increase).
            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                if (app.show_prompt || app.show_confirm || app.show_about) continue;
                int notches = (event.wheel.y > 0) ? -1 : (event.wheel.y < 0) ? 1 : 0;
                if (!notches) continue;
                SDL_ConvertEventToRenderCoordinates(renderer, &event);
                int mx = (int)event.wheel.mouse_x;
                int my = (int)event.wheel.mouse_y;
                // Help overlay: wheel pages through (up = previous, down =
                // next, wraps). Checked before any other wheel target so the
                // help is always navigable regardless of what's underneath.
                if (app.show_help && gui.PointInHelpPanel(mx, my)) {
                    app.help_page = (app.help_page + notches + Gui::kHelpPageCount)
                                    % Gui::kHelpPageCount;
                    continue;
                }
                // Mouse over the "Oct: +N" indicator: wheel changes octave
                // (one octave per notch, same as the [ / ] keys). Checked
                // first so the indicator is always responsive, even in
                // Edit mode.
                if (gui.PointInOctaveIndicator(mx, my)) {
                    app.AdjustOctave(-notches * 12);  // wheel up -> +12
                    continue;
                }
                // Wheel over the edit area: auto-enter edit mode on the
                // hovered field (if any) and nudge it.  If no cell is directly
                // under the cursor but the editor is already active, nudge the
                // currently focused field.  Wheel in the tree pane always steps
                // the instrument list regardless of edit-mode state.
                if (!gui.PointInTreePane(mx, my) && app.current_instr_valid) {
                    Gui::EditHit h = gui.EditFieldAtLogical(mx, my, app.current_instr);
                    if (h.hit) {
                        app.EnterEditField(h.panel, h.a, h.b);
                        app.EditNudge(-notches);
                    } else if (app.editor.active) {
                        app.EditNudge(-notches);
                    } else {
                        app.StepFiles(notches * 3);
                    }
                } else {
                    app.StepFiles(notches * 3); // 3 files per notch = quick
                }
                continue;
            }

            // Mouse motion: always record the latest logical position (used
            // by the GUI for hover highlights). While the tree scrollbar is
            // being dragged, also forward the y coordinate to the drag.
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                SDL_ConvertEventToRenderCoordinates(renderer, &event);
                int mmx = (int)event.motion.x;
                int mmy = (int)event.motion.y;
                app.mouse_x = mmx;
                app.mouse_y = mmy;
                if (gui.TreeScrollDragging()) {
                    gui.UpdateTreeScrollDrag(mmy);
                }
                // Drag-paint: continue painting envelope cells while LMB held.
                if (app.drag_paint_active &&
                    (event.motion.state & SDL_BUTTON_LMASK)) {
                    Gui::EditHit h = gui.EditFieldAtLogical(
                        mmx, mmy, app.current_instr);
                    if (h.hit && h.panel == Editor::Panel::Envelope) {
                        app.ContinueEnvDragPaint(h.a, h.b);
                    }
                }
                // Vol-popup bar-drag: continue setting values while LMB held.
                if (gui.VolPopupOpen() && app.vol_drag_active &&
                    (event.motion.state & SDL_BUTTON_LMASK) &&
                    app.current_instr_valid) {
                    Gui::VolPopupHit vh = gui.VolPopupCellAt(mmx, mmy, app.current_instr, app.instr_stereo);
                    if (vh.hit) app.ContinueVolDrag(vh.col, vh.row, vh.value);
                }
                // Goto handle drag: update PAR_ENV_GOTO while LMB held.
                if (gui.VolPopupOpen() && gui.VolGotoDragging() &&
                    (event.motion.state & SDL_BUTTON_LMASK) &&
                    app.current_instr_valid) {
                    app.ContinueGotoDrag(gui.VolGotoColAt(mmx, app.current_instr));
                }
                continue;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                event.button.button == SDL_BUTTON_LEFT) {
                if (gui.TreeScrollDragging()) gui.EndTreeScrollDrag();
                app.EndEnvDragPaint();
                app.EndVolDrag();
                app.EndGotoDrag();
                gui.EndVolGotoDrag();
                continue;
            }

            // Right-click:
            //   - on a directory file row -> add that file to the bank
            //     (same effect as '+', but for the row under the cursor,
            //     without changing the current instrument)
            //   - on a bank slot           -> open the context menu
            //   - anywhere else            -> toggle the binary instrument
            //     field under the cursor.
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_RIGHT) {
                if (app.show_prompt || app.show_confirm || app.show_about) continue;
                SDL_ConvertEventToRenderCoordinates(renderer, &event);
                int mx = (int)event.button.x;
                int my = (int)event.button.y;

                int trow = gui.TreeRowAtLogical(mx, my);
                if (trow >= 0 && trow < (int)app.dir.Rows().size()) {
                    const auto& row = app.dir.Rows()[trow];
                    if (!row.is_header &&
                        app.dir.At(row.node).type == Directory::NodeType::File) {
                        app.OpenTreeMenu(row.node, mx, my);
                    }
                    continue;
                }

                int slot = gui.BankSlotAtLogical(mx, my);
                if (slot >= 0) {
                    app.OpenBankSlotMenu(slot, mx, my);
                    continue;
                }
                if (!app.current_instr_valid) continue;
                Gui::EditHit h = gui.EditFieldAtLogical(mx, my, app.current_instr);
                if (h.hit) {
                    app.EnterEditField(h.panel, h.a, h.b);
                    app.ToggleField();
                }
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT) {
                SDL_ConvertEventToRenderCoordinates(renderer, &event);

                // Bank-slot context menu: any click closes it. A click on a
                // menu item also triggers its action. The menu is closed
                // before invoking the action because some actions open a
                // confirm dialog which must not stack with the menu.
                // Exception: "Show cluster" computes a cluster fingerprint
                // and pins it to the menu's bottom, keeping the menu open
                // so the user can read it.
                if (app.bank_menu_open) {
                    int mx = (int)event.button.x;
                    int my = (int)event.button.y;
                    Gui::BankMenuItem mi = Gui::BankMenuItem::None;
                    if (gui.PointInBankMenu(mx, my, app.bank_menu_x, app.bank_menu_y,
                                            app.bank_menu_cluster_info)) {
                        mi = gui.BankMenuItemAtLogical(
                            mx, my, app.bank_menu_x, app.bank_menu_y,
                            app.bank_menu_cluster_info);
                    }
                    int slot = app.bank_menu_slot;
                    bool slot_used = (slot >= 0 && slot < Bank::SLOT_COUNT &&
                                      app.bank.At(slot).used);
                    if (mi == Gui::BankMenuItem::Analyse) {
                        if (slot_used) {
                            std::string info = app.FindClusterForBankSlot(slot);
                            // Cache on the slot itself so manifest.txt
                            // persists the result with the bank. Hash is
                            // the current ATA so an Import/Paste/editor
                            // save (which all replace .ata) auto-stales
                            // the cache without us tracking every site.
                            app.bank.SetClusterInfo(slot, info,
                                Analysis::HashAta(app.bank.At(slot).ata));
                            app.bank_menu_cluster_info = info;
                        }
                        // Menu stays open so the user can read the result.
                        continue;
                    }
                    app.CloseBankSlotMenu();
                    switch (mi) {
                        case Gui::BankMenuItem::New:    app.NewSlot(slot); break;
                        case Gui::BankMenuItem::Clear:  if (slot_used) app.ClearSlot(slot);  break;
                        case Gui::BankMenuItem::Export: if (slot_used) app.ExportSlot(slot); break;
                        case Gui::BankMenuItem::Import: app.ImportSlot(slot); break;
                        case Gui::BankMenuItem::Analyse: break;  // handled above
                        case Gui::BankMenuItem::None:   break; // click outside = dismiss
                    }
                    continue;
                }

                // Directory-tree context menu (right-clicked an instrument
                // file row). Same dismiss-on-any-click rule as the bank
                // menu; clicking on an item also fires its action.
                if (app.tree_menu_open) {
                    int mx = (int)event.button.x;
                    int my = (int)event.button.y;
                    bool has_ov = (app.tree_menu_node >= 0 &&
                        app.dir.GetFileManualCategory(app.tree_menu_node) >= 0);
                    Gui::TreeMenuItem mi = Gui::TreeMenuItem::None;
                    if (gui.PointInTreeMenu(mx, my, app.tree_menu_x, app.tree_menu_y,
                                            has_ov)) {
                        mi = gui.TreeMenuItemAtLogical(
                            mx, my, app.tree_menu_x, app.tree_menu_y, has_ov);
                    }
                    int node = app.tree_menu_node;
                    app.CloseTreeMenu();
                    switch (mi) {
                        case Gui::TreeMenuItem::AddToBank:
                            app.AddFileNodeToBank(node);
                            break;
                        case Gui::TreeMenuItem::OverrideSubmenu:
                            app.OpenCategoryPicker(node, mx, my);
                            break;
                        case Gui::TreeMenuItem::ClearOverride:
                            app.dir.SetFileManualCategory(node, -1);
                            app.SetNotice("Cleared override (using auto)");
                            break;
                        case Gui::TreeMenuItem::None:
                            break; // click outside = dismiss
                    }
                    continue;
                }

                // Category picker popup, chained from "Override category".
                if (app.cat_picker_open) {
                    int mx = (int)event.button.x;
                    int my = (int)event.button.y;
                    int picked = -2;   // -2 = outside, -1 = clear, 0..N = category
                    if (gui.PointInCategoryPicker(mx, my,
                            app.cat_picker_x, app.cat_picker_y)) {
                        picked = gui.CategoryPickerItemAtLogical(
                            mx, my, app.cat_picker_x, app.cat_picker_y);
                    }
                    int node = app.cat_picker_node;
                    app.CloseCategoryPicker();
                    if (picked == -1) {
                        app.dir.SetFileManualCategory(node, -1);
                        app.SetNotice("Cleared override (using auto)");
                    } else if (picked >= 0) {
                        app.dir.SetFileManualCategory(node, picked);
                        app.SetNotice(std::string("Override: ") +
                                      Analysis::Name((Analysis::Category)picked));
                    }
                    continue;
                }

                // Unsaved-edits prompt: its buttons are clickable.
                if (app.show_prompt) {
                    char b = gui.SavePromptButtonAt((int)event.button.x,
                                                    (int)event.button.y);
                    if (b) app.ResolvePrompt(b);
                    continue;
                }
                // Yes/No confirm: its buttons are clickable too.
                if (app.show_confirm) {
                    char b = gui.ConfirmButtonAt((int)event.button.x,
                                                 (int)event.button.y);
                    if (b == 'y') app.ResolveConfirm(true);
                    else if (b == 'n') app.ResolveConfirm(false);
                    continue;
                }

                // Volume popup: if open, either start a bar-drag inside it or
                // close it on click-outside.  This check runs before all other
                // instrument-area handlers so the popup is always modal.
                if (gui.VolPopupOpen()) {
                    int mx = (int)event.button.x;
                    int my = (int)event.button.y;
                    if (!gui.PointInVolPopup(mx, my)) {
                        gui.CloseVolPopup();
                        app.EndVolDrag();
                        app.EndGotoDrag();
                        gui.EndVolGotoDrag();
                    } else if (app.current_instr_valid) {
                        // Mono/Stereo toggle button (top-right of popup).
                        if (gui.PointInVolPopupStereoToggle(mx, my)) {
                            app.ToggleStereo();
                        // Check goto handle strip first (non-overlapping with bars).
                        } else if (gui.PointInVolGotoHandle(mx, my, app.current_instr)) {
                            gui.BeginVolGotoDrag();
                            app.BeginGotoDrag(gui.VolGotoColAt(mx, app.current_instr));
                        } else {
                            // Check copy buttons (stereo only).
                            char cbtn = gui.VolCopyButtonAt(mx, my);
                            if      (cbtn == 'L') app.CopyVolLToVolR();
                            else if (cbtn == 'R') app.CopyVolRToVolL();
                            else {
                                Gui::VolPopupHit vh = gui.VolPopupCellAt(mx, my, app.current_instr, app.instr_stereo);
                                if (vh.hit) app.BeginVolDrag(vh.col, vh.row, vh.value);
                            }
                        }
                    }
                    continue;
                }

                // Menu bar (top) takes priority.
                Gui::MenuAction ma = gui.MenuAtLogical((int)event.button.x,
                                                       (int)event.button.y);
                if (ma != Gui::MenuAction::None) {
                    switch (ma) {
                        case Gui::MenuAction::Save:    app.SaveBankDialog(); break;
                        case Gui::MenuAction::Load:    app.GuardedNav([&app]() { app.LoadBankDialog(); }); break;
                        case Gui::MenuAction::Library: app.GuardedNav([&app]() { app.SwitchLibrary(); }); break;
                        case Gui::MenuAction::Analyse: app.AnalyseLibrary(); break;
                        case Gui::MenuAction::About:   app.show_about = !app.show_about; break;
                        case Gui::MenuAction::Help:
                            app.show_help = !app.show_help;
                            if (app.show_help) app.help_page = 0;
                            break;
                        default: break;
                    }
                    continue;
                }

                // Directory view tabs (left pane) switch view / dedupe.
                Gui::DirTab dt = gui.DirTabAtLogical((int)event.button.x,
                                                     (int)event.button.y);
                if (dt != Gui::DirTab::None) {
                    switch (dt) {
                        case Gui::DirTab::Folders:   app.dir.SetViewMode(Directory::ViewMode::Folder); break;
                        case Gui::DirTab::Category:  app.dir.SetViewMode(Directory::ViewMode::Category); break;
                        case Gui::DirTab::Cluster:   app.dir.SetViewMode(Directory::ViewMode::Cluster); break;
                        case Gui::DirTab::ShowAll:   app.dir.SetHideDuplicates(false); break;
                        case Gui::DirTab::HideDupes: app.dir.SetHideDuplicates(true); break;
                        default: break;
                    }
                    app.LoadCurrent();
                    continue;
                }

                // Click the search bar to focus it.
                if (gui.PointInSearchBar((int)event.button.x, (int)event.button.y)) {
                    app.BeginSearch();
                    continue;
                }

                // Click the bank EDIT toggle button.
                if (gui.PointInBankEdit((int)event.button.x, (int)event.button.y)) {
                    app.bank_edit = !app.bank_edit;
                    app.SetNotice(app.bank_edit
                                  ? "Bank EDIT on: Ctrl+C/X/V/Y/S edit, Ctrl+key still moves cursor"
                                  : "Bank EDIT off: Ctrl+key plays the slot");
                    continue;
                }

                // Click the bank "Analyse" button (left of EDIT). Fingerprints
                // every used slot against the library's cluster centroids
                // and stores the result on each Bank::Slot.
                if (gui.PointInBankAnalyse((int)event.button.x, (int)event.button.y)) {
                    app.AnalyseAllBankSlots();
                    continue;
                }

                // Tree scrollbar: clicking the thumb starts a drag; clicking
                // the track above/below pages the view. Handled before the
                // tree-row hit-test since the scrollbar sits inside the tree
                // column.
                char sb = gui.TreeScrollbarHit((int)event.button.x, (int)event.button.y);
                if (sb == 't') {
                    gui.BeginTreeScrollDrag((int)event.button.y);
                    continue;
                }
                if (sb == 'a') { gui.PageTreeScroll(-1); continue; }
                if (sb == 'b') { gui.PageTreeScroll(+1); continue; }

                // Click a tree row: header collapses its category or cluster
                // (depending on which grouped view is active), folder
                // expands/collapses, file selects + loads it.
                int trow = gui.TreeRowAtLogical((int)event.button.x, (int)event.button.y);
                if (trow >= 0 && trow < (int)app.dir.Rows().size()) {
                    const auto& row = app.dir.Rows()[trow];
                    if (row.is_header) {
                        if (app.dir.GetViewMode() == Directory::ViewMode::Cluster)
                            app.dir.ToggleClusterCollapsed(row.header_cat);
                        else
                            app.dir.ToggleCategoryCollapsed(row.header_cat);
                    } else if (app.dir.At(row.node).type == Directory::NodeType::Folder) {
                        app.dir.ToggleExpanded(row.node);
                    } else {
                        int node = row.node;
                        app.GuardedNav([&app, node]() {
                            app.dir.SelectByNode(node);
                            app.LoadCurrent();
                        });
                    }
                    continue;
                }

                int slot = app.BankSlotFromMouse(event.button.x, event.button.y);
                if (slot >= 0) {
                    app.bank_cursor = slot;
                    if (app.bank.At(slot).used) {
                        app.GuardedNav([&app, slot]() { app.LoadBankSlot(slot); });
                    }
                } else {
                    // Check instrument header buttons (Undo / Redo / Revert)
                    // before the general field hit-test.
                    char hbtn = gui.InstrHeaderButtonAt(
                        (int)event.button.x, (int)event.button.y);
                    if (hbtn == 'u') { app.Undo(); }
                    else if (hbtn == 'r') { app.Redo(); }
                    else if (hbtn == 'v') { app.RevertFromDisk(); }
                    else if (gui.PointInStereoToggle((int)event.button.x, (int)event.button.y)) {
                        // Click the mono/stereo toggle button (checked before vol graph
                        // because the two hit-areas share the same Y band).
                        app.ToggleStereo();
                    } else if (app.current_instr_valid &&
                             gui.PointInVolGraph((int)event.button.x, (int)event.button.y)) {
                        // Click the mini vol graph strip → open popup editor.
                        gui.OpenVolPopup();
                    } else {
                        // Click on an instrument field -> edit it.
                        // Envelope cells: begin drag-paint (binary rows toggle first).
                        // Other binary fields (AUDCTL flags, Tbl Type/Mode): toggle
                        //   immediately on left-click.
                        // Small-range params (maxv 2-3, e.g. Vibrato): cycle on click.
                        Gui::EditHit h = gui.EditFieldAtLogical(
                            (int)event.button.x, (int)event.button.y, app.current_instr);
                        if (h.hit) {
                            if (h.panel == Editor::Panel::Envelope) {
                                app.BeginEnvDragPaint(h.a, h.b);
                            } else {
                                app.EnterEditField(h.panel, h.a, h.b);
                                app.CycleField();   // no-op if maxv < 2 or > 3
                                app.ToggleField();  // no-op if not binary (maxv != 1)
                            }
                        }
                    }
                }
                continue;
            }

            if (event.type != SDL_EVENT_KEY_DOWN) continue;

            SDL_Keycode k = event.key.key;
            // Allow auto-repeat for navigation and, while editing, for the
            // delete keys so holding them keeps removing characters.
            bool repeatable = IsNavKey(k) || k == SDLK_BACKSPACE ||
                              (app.editor.active && k == SDLK_DELETE);
            if (event.key.repeat && !repeatable) continue;

            // --- Unsaved-edits prompt swallows all other input ---
            if (app.show_prompt) {
                if      (k == SDLK_K || k == SDLK_RETURN) app.ResolvePrompt('k');
                else if (k == SDLK_D)                     app.ResolvePrompt('d');
                else if (k == SDLK_C || k == SDLK_ESCAPE) app.ResolvePrompt('c');
                continue;
            }

            // --- Yes/No confirmation swallows all other input ---
            if (app.show_confirm) {
                if      (k == SDLK_Y || k == SDLK_RETURN) app.ResolveConfirm(true);
                else if (k == SDLK_N || k == SDLK_ESCAPE) app.ResolveConfirm(false);
                continue;
            }

            // --- About popup: any key closes it ---
            if (app.show_about) { app.show_about = false; continue; }

            // --- Volume popup: ESC closes it ---
            if (gui.VolPopupOpen()) {
                if (k == SDLK_ESCAPE) {
                    gui.CloseVolPopup();
                    app.EndVolDrag();
                    app.EndGotoDrag();
                    gui.EndVolGotoDrag();
                }
                continue;
            }

            // --- Search bar capture ---
            if (app.search_active) {
                bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                if (k == SDLK_ESCAPE)      { app.EndSearch(/*clear=*/true); }
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { app.EndSearch(false); }
                else if (k == SDLK_BACKSPACE) {
                    if (!app.search_query.empty()) app.search_query.pop_back();
                    app.UpdateSearch();
                }
                else if (k == SDLK_LEFT  || k == SDLK_UP)   app.StepFiles(-1);
                else if (k == SDLK_RIGHT || k == SDLK_DOWN) app.StepFiles(+1);
                else {
                    char c = NameChar(k, shift);
                    if (c) { app.search_query.push_back(c); app.UpdateSearch(); }
                }
                continue;
            }

            // Start searching with '/' (browse mode only).
            if (k == SDLK_SLASH && !app.editor.active) { app.BeginSearch(); continue; }

            // --- Global keys that work in both modes ---
            if (k == SDLK_F1)  {
                app.show_help = !app.show_help;
                if (app.show_help) app.help_page = 0;
                continue;
            }
            // Help is modal while open: left/right page, Esc closes, every
            // other key is swallowed so the user can browse the overlay
            // without accidentally triggering note playback / navigation.
            if (app.show_help) {
                if (k == SDLK_LEFT) {
                    app.help_page = (app.help_page + Gui::kHelpPageCount - 1)
                                     % Gui::kHelpPageCount;
                } else if (k == SDLK_RIGHT) {
                    app.help_page = (app.help_page + 1) % Gui::kHelpPageCount;
                } else if (k == SDLK_ESCAPE) {
                    app.show_help = false;
                }
                continue;
            }

            // ESC over an open right-click popup closes the popup instead of
            // falling through to "Silence playback / close help". Handled
            // before the editor and the global ESC default so a popup always
            // wins. CloseAllPopups() is harmless when multiple are open at
            // once (we generally enforce one-at-a-time on Open).
            if (k == SDLK_ESCAPE && app.AnyPopupOpen()) {
                app.CloseAllPopups();
                continue;
            }
            if (k == SDLK_F11) { app.ToggleFullscreen(window); continue; }
            if (k == SDLK_F6)  {
                app.editor.Toggle();
                app.SetNotice(app.editor.active ? "Edit mode ON" : "Edit mode OFF (browsing)");
                continue;
            }
            if (k == SDLK_F7)  { app.AnalyseLibrary();      continue; }
            if (k == SDLK_F8)  { app.ToggleGrouping();      continue; }
            if (k == SDLK_F9)  { app.ToggleHideDuplicates(); continue; }
            if (k == SDLK_F10) { app.ToggleClusterView();    continue; }
            // Ctrl+R reclassifies the current instrument (cycles its manual
            // override through the categories). Quick way to fix a misfiled
            // file without leaving the keyboard. Ctrl+Shift+R clears every
            // override library-wide.
            {
                bool ctrl  = (SDL_GetModState() & SDL_KMOD_CTRL)  != 0;
                bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                if (ctrl && shift && k == SDLK_R) { app.ClearAllOverrides(); continue; }
                if (ctrl && k == SDLK_R)          { app.CycleManualCategory(); continue; }
                // Ctrl+] / Ctrl+[ step the k-means cluster-count override
                // for the next analysis run (0 = auto).
                if (ctrl && k == SDLK_RIGHTBRACKET) { app.StepClusterCount(+1); continue; }
                if (ctrl && k == SDLK_LEFTBRACKET)  { app.StepClusterCount(-1); continue; }
            }

            if (app.editor.active) {
                // ---------- EDIT MODE ----------
                bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                bool ctrl  = (SDL_GetModState() & SDL_KMOD_CTRL)  != 0;
                bool name_panel = (app.editor.panel == Editor::Panel::Name);

                // Ctrl bank verbs (selected slot) + audition the edited
                // instrument with Ctrl+a..z/0..9.
                if (ctrl) {
                    if (k == SDLK_Z) { app.Undo(); continue; }
                    // C / X / V / Y / S are bank-edit verbs only. Outside
                    // bank EDIT mode they fall through to the audition path
                    // (Ctrl+letter plays the keyboard note), so the user
                    // can't accidentally save / redo / cut a slot by
                    // pressing letters that are also chromatic note keys.
                    if (app.bank_edit) {
                        if (k == SDLK_C) { app.CopySlot(); continue; }
                        if (k == SDLK_X) { app.CutSlot(); continue; }
                        if (k == SDLK_V) { app.PasteSlot(); continue; }
                        if (k == SDLK_Y) { app.Redo(); continue; }
                        if (k == SDLK_S) { app.ExportCurrentRti(); continue; }
                    }
                    if (k == SDLK_LEFT)  { app.MoveBankCursorAndLoad(-1); continue; }
                    if (k == SDLK_RIGHT) { app.MoveBankCursorAndLoad(+1); continue; }
                    if (k == SDLK_UP)    { app.MoveBankCursorAndLoad(-8); continue; }
                    if (k == SDLK_DOWN)  { app.MoveBankCursorAndLoad(+8); continue; }
                    if (k == SDLK_DELETE) { app.RequestRemoveCursor(); continue; }
                    if (k == SDLK_INSERT) { app.RequestInsertIntoCursor(); continue; }
                    int off = Keyboard::NoteOffset(k);
                    if (off >= 0) { app.PlayNote(off); continue; }
                }

                switch (k) {
                    case SDLK_ESCAPE: app.editor.SetActive(false);
                                      app.SetNotice("Edit mode OFF (browsing)"); break;
                    case SDLK_TAB:    app.editor.NextPanel(shift ? -1 : +1); break;
                    case SDLK_LEFT:   app.editor.Move(-1, 0, app.current_instr); break;
                    case SDLK_RIGHT:  app.editor.Move(+1, 0, app.current_instr); break;
                    // Shift+Up/Down nudge the value; plain Up/Down move the cursor.
                    case SDLK_UP:     if (shift) app.EditNudge(+1);
                                      else app.editor.Move(0, -1, app.current_instr); break;
                    case SDLK_DOWN:   if (shift) app.EditNudge(-1);
                                      else app.editor.Move(0, +1, app.current_instr); break;
                    case SDLK_SPACE:
                        if (name_panel) app.EditChar(' '); else app.RetriggerNote();
                        break;
                    case SDLK_BACKSPACE: app.EditBackspace(); break;
                    case SDLK_DELETE:    app.EditDeleteForward(); break;
                    case SDLK_EQUALS:
                    case SDLK_PLUS:
                    case SDLK_KP_PLUS:  app.EditNudge(+1); break;
                    case SDLK_KP_MINUS: app.EditNudge(-1); break;
                    case SDLK_MINUS:
                        if (name_panel) app.EditChar('-'); else app.EditNudge(-1);
                        break;
                    default:
                        if (name_panel) {
                            char c = NameChar(k, shift);
                            if (c) app.EditChar(c);
                        } else {
                            int nib = HexNibble(k);
                            if (nib >= 0) app.EditHex(nib);
                        }
                        break;
                }
                continue;
            }

            // ---------- BROWSE MODE ----------
            // Ctrl bank verbs operate on the selected bank slot.
            if (SDL_GetModState() & SDL_KMOD_CTRL) {
                if (k == SDLK_Z) { app.Undo(); continue; }
                // C / X / V / Y / S are bank-edit verbs only. Outside bank
                // EDIT mode they fall through and play the selected slot,
                // so Ctrl+S can't accidentally export when the user meant
                // to audition the 'S' note.
                if (app.bank_edit) {
                    if (k == SDLK_C) { app.CopySlot(); continue; }
                    if (k == SDLK_X) { app.CutSlot(); continue; }
                    if (k == SDLK_V) { app.PasteSlot(); continue; }
                    if (k == SDLK_Y) { app.Redo(); continue; }
                    if (k == SDLK_S) { app.ExportCurrentRti(); continue; }
                }
                if (k == SDLK_LEFT)  { app.MoveBankCursorAndLoad(-1); continue; }
                if (k == SDLK_RIGHT) { app.MoveBankCursorAndLoad(+1); continue; }
                if (k == SDLK_UP)    { app.MoveBankCursorAndLoad(-8); continue; }
                if (k == SDLK_DOWN)  { app.MoveBankCursorAndLoad(+8); continue; }
                if (k == SDLK_DELETE) { app.RequestRemoveCursor(); continue; }
                if (k == SDLK_INSERT) { app.RequestInsertIntoCursor(); continue; }
                int off = Keyboard::NoteOffset(k);
                if (off >= 0) { app.PlayBankSlot(off); continue; }
            }

            switch (k) {
                case SDLK_ESCAPE:
                    if (app.show_help) app.show_help = false;
                    else               app.Silence();
                    break;

                case SDLK_LEFT:
                case SDLK_UP:        app.StepFiles(-1); break;
                case SDLK_RIGHT:
                case SDLK_DOWN:      app.StepFiles(+1); break;
                case SDLK_PAGEUP:    app.StepFiles(-kPageSize); break;
                case SDLK_PAGEDOWN:  app.StepFiles(+kPageSize); break;
                case SDLK_HOME:      app.JumpToFile(0); break;
                case SDLK_END:       app.JumpToFile(app.dir.NavCount() - 1); break;

                case SDLK_RETURN:
                case SDLK_KP_ENTER:  app.ToggleParentFolder(); break;

                case SDLK_TAB:
                    app.MoveBankCursor((SDL_GetModState() & SDL_KMOD_SHIFT) ? -1 : +1);
                    break;

                case SDLK_LEFTBRACKET:
                    app.octave_shift = std::max(-24, app.octave_shift - 12);
                    std::printf("Octave shift = %+d semitones\n", app.octave_shift); break;
                case SDLK_RIGHTBRACKET:
                    app.octave_shift = std::min(+24, app.octave_shift + 12);
                    std::printf("Octave shift = %+d semitones\n", app.octave_shift); break;

                case SDLK_EQUALS:
                case SDLK_PLUS:
                case SDLK_KP_PLUS:   app.AddToBank(); break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS:  app.RemoveFromBank(); break;

                case SDLK_F2:        app.SaveBankDialog(); break;
                case SDLK_F3:        app.GuardedNav([&app]() { app.LoadBankDialog(); }); break;
                case SDLK_F4:        app.GuardedNav([&app]() { app.SwitchLibrary(); }); break;
                case SDLK_F5:        app.TogglePalNtsc(); break;

                default: {
                    int off = Keyboard::NoteOffset(k);
                    if (off >= 0) app.PlayNote(off);
                } break;
            }
        }
        SDL_SetRenderDrawColor(renderer, 16, 18, 24, 255);
        SDL_RenderClear(renderer);

        GuiState gs;
        gs.dir              = &app.dir;
        gs.bank             = &app.bank;
        gs.rti              = &app.current_rti;
        gs.instrument       = app.current_instr_valid ? &app.current_instr : nullptr;
        gs.octave_shift     = app.octave_shift;
        gs.ntsc             = app.ntsc;
        gs.last_note_played = app.last_note_played;
        gs.show_help        = app.show_help;
        gs.help_page        = app.help_page;
        gs.bank_cursor      = app.bank_cursor;
        gs.library_path     = app.library_path;
        gs.editor           = &app.editor;
        gs.modified         = app.modified;
        gs.show_prompt      = app.show_prompt;
        gs.show_confirm     = app.show_confirm;
        gs.confirm_msg      = app.confirm_msg;
        gs.show_about       = app.show_about;
        gs.search_active    = app.search_active;
        gs.search_query     = app.search_query;
        gs.bank_edit        = app.bank_edit;
        gs.bank_menu_open   = app.bank_menu_open;
        gs.bank_menu_slot   = app.bank_menu_slot;
        gs.bank_menu_x      = app.bank_menu_x;
        gs.bank_menu_y      = app.bank_menu_y;
        gs.bank_menu_cluster_info = app.bank_menu_cluster_info;
        gs.tree_menu_open   = app.tree_menu_open;
        gs.tree_menu_node   = app.tree_menu_node;
        gs.tree_menu_x      = app.tree_menu_x;
        gs.tree_menu_y      = app.tree_menu_y;
        gs.tree_menu_has_override = (app.tree_menu_node >= 0 &&
            app.dir.GetFileManualCategory(app.tree_menu_node) >= 0);
        gs.cat_picker_open  = app.cat_picker_open;
        gs.cat_picker_node  = app.cat_picker_node;
        gs.cat_picker_x     = app.cat_picker_x;
        gs.cat_picker_y     = app.cat_picker_y;
        gs.mouse_x          = app.mouse_x;
        gs.mouse_y          = app.mouse_y;
        gs.instr_stereo     = app.instr_stereo;
        gs.undo_depth       = (int)app.undo_stack.size();
        gs.redo_depth       = (int)app.redo_stack.size();
        app.engine.SnapshotPokey(gs.pokey);
        gs.current_bank_slot = (app.current_source == App::Source::Bank)
                                   ? app.current_bank_slot : -1;
        if (SDL_GetTicks() < app.notice_until) gs.notice = app.notice;
        gui.Render(renderer, gs);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    app.SaveConfig();
    app.audio.Close();
    app.engine.DeInit();
    text_renderer.Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
