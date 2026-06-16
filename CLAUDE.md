# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

**Toolchain:** Visual Studio 2022 (v143), x64, C++17. SDL3 is vendored under `lib/`.

```powershell
# Release (typical)
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" PokeyForge.sln /p:Configuration=Release /p:Platform=x64 /m /nologo

# Debug
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" PokeyForge.sln /p:Configuration=Debug /p:Platform=x64 /m /nologo
```

Output lands in `build\Release\` or `build\Debug\`. A post-build step copies the runtime assets (`SDL3.dll`, `sa_pokey.dll`, `sa_c6502.dll`, `rmt_driver_v2.obx`, `SDL3_ttf.dll`, `JetBrainsMono-Regular.ttf`) next to the exe, so it runs in place immediately after build.

There are no unit tests and no linter step.

## Architecture

PokeyForge is a **single-window, immediate-mode GUI** app. All application state lives in the `App` struct in `main.cpp`. There are no dynamic polymorphism or plugin patterns — every subsystem is a concrete struct/class owned by `App`.

### The rendering model

`Gui` is **stateless and read-only** with respect to app data. Every frame, `main.cpp` builds a `GuiState` snapshot (pointers + value copies) and calls `Gui::Render`. The GUI never mutates `App`. Hit-tests (`MenuAtLogical`, `TreeRowAtLogical`, `BankSlotAtLogical`, etc.) are public methods on `Gui` that mirror the draw geometry exactly — one `ComputeXxxGeom()` helper is shared by both the draw path and the hit-test path so they can't drift.

Mouse events are converted from window space to the fixed **1280×720 logical canvas** with `SDL_ConvertEventToRenderCoordinates` before any hit-test.

### The mutation path

**All instrument edits must go through `App::DoEdit(fn)`** — never mutate `current_instr` directly.

```
DoEdit(fn):
  saves before-state onto undo_stack
  calls fn(current_instr)
  clears redo_stack
  calls ApplyEdit()

ApplyEdit():
  RtiFile::InstrumentToAta → working_ata blob
  RmtEngine::LoadInstrumentSlot(kInstrSlot, blob)   // live audio update
```

Undo/redo stacks hold `TInstrument` snapshots (max 128 each). `Ctrl+Z` / `Ctrl+Y` swap snapshots between stacks and call `ApplyEdit`.

### Audio pipeline

```
SDL audio thread
  Audio::Callback
    RmtEngine::Generate(scratch, want_frames)
      [for each VBI frame]
        JSR RMT_P3       (advance envelopes, note tables, vibrato)
        JSR RMT_SETPOKEY (write POKEY shadow → $D200..$D208)
        Pokey::PutByte × 9
        Pokey::Process → PCM samples
    SDL_PutAudioStreamData
```

`RmtEngine` is protected by a single `std::mutex` — all main-thread calls (`NoteOn`, `LoadInstrumentSlot`, `SetNTSC`) take it; the audio callback's `Generate` also takes it. Contention is negligible because audio frames are short.

**TAP vs NATIVE audio (F12 toggle):** The patched `sa_pokey.dll` exposes `Pokey_SetAudioTap` / `Pokey_SetMute`. In TAP mode PokeyForge mutes the DLL's native audio device and drains the engine's internal float stream through a fixed-rate resampler (`kStep ≈ 1.4357` tap samples per host sample, PAL engine rate / 44100 Hz). In NATIVE mode the DLL's own DirectSound/WASAPI device plays back. Both paths are set up in `Audio::Open` / `Audio::SetUseAudioTap`.

**Important DLL contract:** `Pokey::Process` passes `sndn = numSamples * 2` to the DLL (not `numSamples`) because the DLL does `Advance(sndn/2)` expecting stereo byte count. Passing mono byte count halves the engine rate and causes pitch/tempo errors.

### Key domain types

| Type | Where | Role |
|------|-------|------|
| `TInstrument` | `InstrumentTypes.h` | In-memory instrument: `name[33]`, `parameters[24]`, `envelope[48][8]`, `noteTable[32]`. Ported verbatim from RMT. |
| `PAR_*` constants | `InstrumentTypes.h` | Parameter indices into `TInstrument::parameters`. |
| `GuiState` | `Gui.h` | Const snapshot passed to `Gui::Render` each frame. |
| `Editor` | `Editor.h` | Cursor model: focused panel, cell position, hex-compose state, name caret. |
| `HelpRow` / `HelpPage` / `kHelpPages[]` | `Gui.cpp` (anonymous namespace) | 15-page F1 help content — add pages here and update `kHelpPageCount` in `Gui.h`. |

### Directory / analysis / bank

- `Directory` owns the flat `vector<Node>` tree. Three derived lists are rebuilt by `RebuildViews()`: `AllFiles()` (for analysis), `Rows()` (display), `NavFiles()` (arrow-key navigation). Never sort or filter the raw node list — go through the view rebuild.
- `Analysis::Run` hashes each ATA blob (FNV-1a 64-bit), classifies with `Classify()`, writes `analysis.json`. Cache is versioned (`kAnalysisVersion`); bump it when the classifier changes to force a re-analysis on next launch.
- `Bank` is a fixed `array<Slot, 64>`. Instrument slot 0 (`kInstrSlot`) is used for live audition; slot 1 (`kSampleSlot`) is used for bank-slot sampling via `Ctrl+letter`, so they don't interfere.

### Edit mode guard

`Ctrl+Z/C/X/V/Y/S` are **gated by the bank EDIT toggle** (`app.bank_edit`) in browse mode. Outside EDIT mode those keys fall through to `Ctrl+letter` audition so they can't accidentally fire undo/save/redo when the user means to play a note. The F6 editor's own Ctrl+Z still runs Undo unconditionally when the editor is active (different code path).

### Adding a help page

1. Add a `const HelpRow kHelpPageN[]` array in the anonymous namespace in `Gui.cpp` (follow the existing pattern).
2. Add an entry to `kHelpPages[]`.
3. Increment `kHelpPageCount` in `Gui.h` — the `static_assert` will catch a mismatch.
4. If the new page belongs to an existing topic section, update the `kHelpTopics[]` target pages so the topic shortcut buttons still jump to the right page.

## File layout (non-obvious relationships)

- `src/RmtAddresses.h` — driver entry-point addresses; change only if the `.obx` is rebuilt.
- `src/InstrumentTypes.h` — `TInstrument` must stay byte-compatible with RMT's struct; do not reorder or resize fields.
- `runtime/` — DLLs and `.obx` copied by post-build; not in `src/`.
- `extras/sa_pokey-src/` — full AltirraSDL source for the patched `sa_pokey.dll` with `build.ps1` to rebuild it. See its `README.md` for the patch details and license.
- `lib/SDL3-3.4.8/` and `lib/SDL3_ttf-3.2.2/` — vendored SDL headers and import libs.
- `screenshots/` — README images; `envelope_editor.png` is a placeholder (not yet captured).
