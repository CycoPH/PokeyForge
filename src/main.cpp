// PokeyForge — RMT .RTI instrument auditioner / editor / bank builder.

#include "Analysis.h"
#include "Audio.h"
#include "Bank.h"
#include "Config.h"
#include "Directory.h"
#include "Editor.h"
#include "Gui.h"
#include "Keyboard.h"
#include "RmtEngine.h"
#include "RtiFile.h"
#include "Version.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // provides the Windows entry point (no console)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

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
    SDL_RenderDebugText(r, (float)(px + 42), (float)(py + 96),
                        "RMT .RTI instrument auditioner");
    SDL_SetRenderDrawColor(r, 235, 150, 40, 255);
    SDL_RenderDebugText(r, (float)(px + 42), (float)(py + 116),
                        "written by RetroCoder");
    SDL_SetRenderDrawColor(r, 130, 200, 240, 255);
    SDL_RenderDebugText(r, (float)(px + 42), (float)(py + ph - 24),
                        status ? status : "Loading...");

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
    fs::path exe_dir = fs::current_path();
    fs::path candidate = exe_dir / "rmt_driver_v2.obx";
    if (fs::exists(candidate)) return candidate.string();
    fs::path runtime = exe_dir / ".." / ".." / "runtime" / "rmt_driver_v2.obx";
    if (fs::exists(runtime)) return runtime.string();
    return std::string{};
}

void PrintHelp()
{
    std::printf(
        "PokeyForge controls\n"
        "  a..z, 0..9       Play current instrument at chromatic pitches\n"
        "  [ / ]            Octave shift down / up\n"
        "  Left / Right     Previous / next .RTI (hold to repeat)\n"
        "  Up / Down        Previous / next .RTI (hold to repeat)\n"
        "  PageUp / PageDn  Jump by %d files\n"
        "  Home / End       First / last file\n"
        "  Enter            Toggle current file's folder (collapse/expand)\n"
        "  + (=)            Add current instrument to bank\n"
        "  -                Remove current instrument from bank\n"
        "  F1               Toggle on-screen help / keybindings\n"
        "  F2               Save bank to ./bank_out/\n"
        "  F5               Toggle PAL / NTSC\n"
        "  F11              Toggle fullscreen\n"
        "  Esc              Silence playback (does not quit)\n"
        "  Close window     Quit\n", kPageSize);
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

std::string SaveFileModal(SDL_Window* window, const SDL_DialogFileFilter* filters,
                          int nfilters, const char* start = nullptr)
{
    DlgResult r;
    SDL_ShowSaveFileDialog(&DialogCallback, &r, window, filters, nfilters, start);
    return PumpDialog(r);
}

std::string OpenFileModal(SDL_Window* window, const SDL_DialogFileFilter* filters,
                          int nfilters, const char* start = nullptr)
{
    DlgResult r;
    SDL_ShowOpenFileDialog(&DialogCallback, &r, window, filters, nfilters, start, false);
    return PumpDialog(r);
}

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
        const auto& n = dir.At(node);
        if (!current_rti.LoadFromFile(n.path.c_str())) {
            std::fprintf(stderr, "Failed to load %s\n", n.path.c_str());
            current_instr_valid = false;
            return false;
        }
        engine.LoadInstrumentSlot(kInstrSlot,
                                  current_rti.AtaBlob().data(),
                                  current_rti.AtaBlob().size());
        current_instr_valid = current_rti.ToInstrument(current_instr, /*stereo=*/false);
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

        engine.LoadInstrumentSlot(kInstrSlot, s.ata.data(), s.ata.size());
        current_instr_valid = DecodeAta(s.ata, current_instr);
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
    static bool DecodeAta(const std::vector<byte>& ata, TInstrument& out)
    {
        std::vector<byte> img;
        img.reserve(4 + 33 + 1 + ata.size());
        img.push_back('R'); img.push_back('T'); img.push_back('I'); img.push_back(1);
        for (int i = 0; i < 33; ++i) img.push_back(' ');
        img.push_back((byte)std::min<size_t>(ata.size(), 255));
        img.insert(img.end(), ata.begin(), ata.end());
        RtiFile tmp;
        if (!tmp.LoadFromMemory(img.data(), img.size())) return false;
        return tmp.ToInstrument(out, /*stereo=*/false);
    }

    void PlayNote(int semitone)
    {
        last_play_semitone = semitone;
        int note = Keyboard::BASE_NOTE + semitone + octave_shift;
        note = std::clamp(note, kMinNote, kMaxNote);
        engine.NoteOn(kBaseTrack, note, kInstrSlot, kPlayVolume);
        last_note_played = note;
    }

    void RetriggerNote()
    {
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
        working_ata = RtiFile::InstrumentToAta(current_instr, /*stereo=*/false);
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
        working_ata = RtiFile::InstrumentToAta(current_instr, /*stereo=*/false);
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
        working_ata = RtiFile::InstrumentToAta(current_instr, false);
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

    // Run an editor action; if it changed the instrument, record undo + apply.
    template <typename Fn>
    void DoEdit(Fn&& fn)
    {
        TInstrument before = current_instr;
        if (fn()) { PushUndo(before); redo_stack.clear(); ApplyEdit(); }
    }

    // Push the current working instrument live to the engine + bank slot.
    void ReencodeLive()
    {
        working_ata = RtiFile::InstrumentToAta(current_instr, /*stereo=*/false);
        engine.LoadInstrumentSlot(kInstrSlot, working_ata.data(), working_ata.size());
        if (current_source == Source::Bank && current_bank_slot >= 0)
            bank.SetSlot(current_bank_slot, TrimName(), working_ata,
                         current_rti.Valid() ? current_rti.Path() : "");
        editor.Clamp(current_instr);
    }

    void ToggleField()         { DoEdit([&]{ return editor.ToggleBinary(current_instr); }); }
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
        // Unmodified directory instrument already present -> no duplicate.
        if (!modified && current_source == Source::Directory &&
            current_rti.Valid() && bank.IndexOfPath(current_rti.Path()) >= 0) {
            SetNotice("Already in bank.");
            return;
        }
        // Commit the working copy (adds a new slot, or updates the bank slot
        // this instrument came from).
        CommitToBank();
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

    // Ctrl+S: export the current (edited) instrument as a .RTI under a new name.
    void ExportCurrentRti()
    {
        if (!current_instr_valid) { SetNotice("No instrument to export."); return; }
        std::vector<byte> ata = RtiFile::InstrumentToAta(current_instr, /*stereo=*/false);
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
        Analysis::LoadAndApply(dir, library_path);
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

    // F7: analyse the library (classify + find duplicates), hide duplicates,
    // and cache the result to analysis.json.
    void AnalyseLibrary()
    {
        if (renderer) DrawBusy(renderer, "Analysing instrument library...");
        Analysis::Summary sum = Analysis::Run(dir, library_path, /*writeJson=*/true);
        if (!sum.ok) { SetNotice("Analysis failed."); return; }
        dir.SetHideDuplicates(true);
        LoadCurrent();
        SetNotice("Analysed " + std::to_string(sum.total) + " instruments, " +
                  std::to_string(sum.duplicates) + " duplicates hidden. Saved analysis.json");
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

    // F9: show/hide duplicates again.
    void ToggleHideDuplicates()
    {
        dir.SetHideDuplicates(!dir.HideDuplicates());
        LoadCurrent();
        SetNotice(dir.HideDuplicates() ? "Duplicates hidden" : "Duplicates shown");
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

int main(int argc, char* argv[])
{
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

    // Apply a cached analysis.json if present (categories + duplicates), and
    // hide duplicates by default so the curated list shows on launch.
    DrawSplash(renderer, "Loading analysis...");
    if (Analysis::LoadAndApply(app.dir, app.library_path)) {
        app.dir.SetHideDuplicates(true);
        std::printf("Loaded analysis.json\n");
    }

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
    PrintHelp();

    Gui gui;
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

            // Any click dismisses the About or Help popup.
            if ((app.show_about || app.show_help) &&
                event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                app.show_about = false;
                app.show_help = false;
                continue;
            }

            // Mouse wheel: step the instrument selection (up = previous).
            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                if (app.show_prompt || app.show_confirm || app.show_about) continue;
                int notches = (event.wheel.y > 0) ? -1 : (event.wheel.y < 0) ? 1 : 0;
                if (notches) app.StepFiles(notches * 3);   // 3 files per notch = quick
                continue;
            }

            // Right-click: toggle a binary instrument field under the cursor.
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_RIGHT) {
                if (app.show_prompt || app.show_confirm || app.show_about) continue;
                if (!app.current_instr_valid) continue;
                SDL_ConvertEventToRenderCoordinates(renderer, &event);
                Gui::EditHit h = gui.EditFieldAtLogical(
                    (int)event.button.x, (int)event.button.y, app.current_instr);
                if (h.hit) {
                    app.EnterEditField(h.panel, h.a, h.b);
                    app.ToggleField();
                }
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT) {
                SDL_ConvertEventToRenderCoordinates(renderer, &event);

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
                        case Gui::MenuAction::Help:    app.show_help = !app.show_help; break;
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
                                  ? "Bank EDIT on: Ctrl+C/X/V move instruments"
                                  : "Bank EDIT off: Ctrl+key plays the slot");
                    continue;
                }

                // Click a tree row: header collapses its category, folder
                // expands/collapses, file selects + loads it.
                int trow = gui.TreeRowAtLogical((int)event.button.x, (int)event.button.y);
                if (trow >= 0 && trow < (int)app.dir.Rows().size()) {
                    const auto& row = app.dir.Rows()[trow];
                    if (row.is_header) {
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
                    // Click on an instrument field -> edit it.
                    Gui::EditHit h = gui.EditFieldAtLogical(
                        (int)event.button.x, (int)event.button.y, app.current_instr);
                    if (h.hit) app.EnterEditField(h.panel, h.a, h.b);
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
            if (k == SDLK_F1)  { app.show_help = !app.show_help; continue; }
            if (k == SDLK_F11) { app.ToggleFullscreen(window); continue; }
            if (k == SDLK_F6)  {
                app.editor.Toggle();
                app.SetNotice(app.editor.active ? "Edit mode ON" : "Edit mode OFF (browsing)");
                continue;
            }
            if (k == SDLK_F7)  { app.AnalyseLibrary();      continue; }
            if (k == SDLK_F8)  { app.ToggleGrouping();      continue; }
            if (k == SDLK_F9)  { app.ToggleHideDuplicates(); continue; }

            if (app.editor.active) {
                // ---------- EDIT MODE ----------
                bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                bool ctrl  = (SDL_GetModState() & SDL_KMOD_CTRL)  != 0;
                bool name_panel = (app.editor.panel == Editor::Panel::Name);

                // Ctrl bank verbs (selected slot) + audition the edited
                // instrument with Ctrl+a..z/0..9.
                if (ctrl) {
                    if (k == SDLK_Z) { app.Undo(); continue; }
                    if (k == SDLK_Y) { app.Redo(); continue; }
                    if (k == SDLK_S) { app.ExportCurrentRti(); continue; }
                    // C/X/V move slots only in bank EDIT mode; otherwise they
                    // fall through and play (audition) like other Ctrl+keys.
                    if (app.bank_edit) {
                        if (k == SDLK_C) { app.CopySlot(); continue; }
                        if (k == SDLK_X) { app.CutSlot(); continue; }
                        if (k == SDLK_V) { app.PasteSlot(); continue; }
                    }
                    if (k == SDLK_LEFT)  { app.MoveBankCursor(-1); continue; }
                    if (k == SDLK_RIGHT) { app.MoveBankCursor(+1); continue; }
                    if (k == SDLK_UP)    { app.MoveBankCursor(-8); continue; }
                    if (k == SDLK_DOWN)  { app.MoveBankCursor(+8); continue; }
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
                if (k == SDLK_Y) { app.Redo(); continue; }
                if (k == SDLK_S) { app.ExportCurrentRti(); continue; }
                // C/X/V move slots only in bank EDIT mode; otherwise they
                // fall through and play the selected slot.
                if (app.bank_edit) {
                    if (k == SDLK_C) { app.CopySlot(); continue; }
                    if (k == SDLK_X) { app.CutSlot(); continue; }
                    if (k == SDLK_V) { app.PasteSlot(); continue; }
                }
                if (k == SDLK_LEFT)  { app.MoveBankCursor(-1); continue; }
                if (k == SDLK_RIGHT) { app.MoveBankCursor(+1); continue; }
                if (k == SDLK_UP)    { app.MoveBankCursor(-8); continue; }
                if (k == SDLK_DOWN)  { app.MoveBankCursor(+8); continue; }
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
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
