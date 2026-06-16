# PokeyForge — Technical Reference

This document describes how PokeyForge is built and how it produces sound. For the
user-facing guide, see [Readme.md](./Readme.md).

---

## 1. Overview

PokeyForge is a C++17 / SDL3 Windows application that plays Atari RMT `.RTI`
instrument files. Rather than re-implement RMT's playback logic, it reuses
RMT's actual runtime stack:

- a **6502 CPU emulator** (`sa_c6502.dll`) running the **RMT tracker driver**
  (a 6502 program, `rmt_driver_v2.obx`),
- which writes POKEY registers consumed by a **POKEY emulator**
  (`sa_pokey.dll`),
- whose PCM output is streamed to **SDL3 audio**.

Because the audio path is byte-identical to RMT's, instruments sound exactly as
they do in the tracker.

### Data flow

```
 SDL3 keyboard / mouse / window events
            │
            ▼
   main.cpp event loop (App)
            │  load .RTI, trigger notes, manage bank
            ▼
        RmtEngine
   ┌────────┴───────────────────────────────────────┐
   │ write instrument blob to RAM $4000+slot*256     │
   │ JSR RMT_ATA_SETNOTEINSTR / RMT_ATA_SETVOLUME    │
   │                                                 │
   │ per audio frame (50/60 Hz):                     │
   │   JSR RMT_P3        (driver advances state)     │
   │   JSR RMT_SETPOKEY  (driver writes $D200..$D208)│
   │   copy $D200..$D208 -> Pokey::PutByte            │
   │   Pokey::Process    -> N PCM samples            │
   └────────┬───────────────────────────────────────┘
            │ (driver runs on sa_c6502.dll over a 64 KB RAM array)
            ▼
   SDL3 audio stream (U8 mono 44100 Hz) -> speakers
```

The 6502 emulator and POKEY emulator are loaded at runtime with
`LoadLibrary`/`GetProcAddress`; they are **not** link-time dependencies.

---

## 2. Source layout

All sources live in `src/`. Modules are deliberately small and single-purpose.

| File | Responsibility |
|------|----------------|
| `Types.h` | `byte`, `MemoryAddress` aliases. Intentionally avoids redefining Win32 `BYTE`/`WORD`/`DWORD` (those come from `<windows.h>` where needed). |
| `RmtAddresses.h` | RMT driver jump-table entry-point addresses and the instrument base address. |
| `InstrumentTypes.h` | `TInstrument` struct and `PAR_*` / envelope constants, ported verbatim from RMT (MFC stripped). |
| `Notes.h` | Note-range constant (`NOTESNUM = 61`). |
| `C6502.{h,cpp}` | Loader/wrapper for `sa_c6502.dll`. Exposes `Init(byte* mem)`, `JSR(...)`, `DeInit()`. |
| `Pokey.{h,cpp}` | Loader/wrapper for `sa_pokey.dll`. Exposes `InitDll`, `SoundInit`, `PutByte`, `Process`. |
| `Atari.{h,cpp}` | Owns the 64 KB memory array, clock/frame-rate maths, and a `JSR` helper bound to the CPU emulator. |
| `DriverBinaries.{h,cpp}` | Parses an Atari multi-segment binary (`.obx`) and loads it into RAM. |
| `RtiFile.{h,cpp}` | Parses `.RTI` files; decodes the ATA blob into a `TInstrument` (`AtaToInstr` v0/v1 ports) and re-encodes it (`InstrumentToAta`, the `InstrToAta` port used by the editor). |
| `Editor.{h,cpp}` | Edit-mode cursor model + value logic: panel focus, cell cursor, hex/increment value entry, name text-field editing, and `Describe()` (the field readout for the edit bar). |
| `RmtEngine.{h,cpp}` | The playback engine: instrument loading, note on/off, per-frame driver stepping, sample generation. Thread-safe via a mutex. |
| `Audio.{h,cpp}` | SDL3 audio device + pull callback that drains `RmtEngine::Generate`. |
| `Directory.{h,cpp}` | Recursive `.RTI` scan, tree model (expand/collapse), folder/category view modes, duplicate hiding, and analysis-aware navigation (display `Rows()` + visible `NavFiles()`). |
| `Analysis.{h,cpp}` | Hashes each instrument's ATA blob to detect sonic duplicates, classifies it into a category, applies results to the `Directory`, and reads/writes `analysis.json`. |
| `Bank.{h,cpp}` | 64-slot instrument bank; saves `.RTI` files + manifest, exports `.RMT`, and loads from either. |
| `Config.{h,cpp}` | Reads/writes `pokeyforge.json` (last library, last bank, last file index) next to the exe. Minimal hand-rolled JSON. |
| `Keyboard.{h,cpp}` | Maps SDL keycodes to chromatic semitone offsets. |
| `Gui.{h,cpp}` | Renderer for all panels, command bar, edit bar, help and save-prompt overlays (uses `SDL_RenderDebugText`). Also provides the bank-grid mouse hit-test and draws the edit-cursor highlights. |
| `main.cpp` | Window/renderer/engine/audio setup, config load/save, native dialogs, event loop, action dispatch. |

---

## 3. The `.RTI` file format

A `.RTI` file stores a single instrument:

| Offset | Size | Meaning |
|-------:|-----:|---------|
| 0 | 3 | ASCII `"RTI"` |
| 3 | 1 | Version byte. Only `0` and `1` are supported. |
| 4 | 33 | Instrument name (space-padded, terminating zero in byte 32). |
| 37 | 1 | Length of the ATA blob, in bytes. |
| 38 | N | The **ATA blob**: the instrument in raw Atari memory layout. |

The ATA blob is the same byte layout the RMT driver expects in Atari RAM.
`RtiFile::ToInstrument` decodes it for display via the ported `AtaToInstr`
(version 1) and `AtaToInstrV0` (legacy) routines. The blob's internal layout:

- byte 0: note-table end offset (`tableLen + 12`)
- byte 1: note-table goto offset (`tableGoto + 12`)
- byte 2: envelope end pointer
- byte 3: envelope goto pointer
- byte 4: table type/mode/speed (bit 7 type, bit 6 mode, bits 0–5 speed)
- byte 5: AUDCTL bit field
- byte 6: volume fade-out
- byte 7: volume min (high nibble)
- byte 8: delay
- byte 9: vibrato (bits 0–1)
- byte 10: frequency shift
- byte 11: unused
- byte 12…: note table, then 3-byte envelope columns

`Bank::SaveTo` writes the same structure back out, so saved files round-trip
through RMT.

---

## 4. The driver binary (`.obx`)

`rmt_driver_v2.obx` is an Atari executable in the standard "multi-segment
binary" format:

```
[ optional FFFF marker ]
segment: <start lo,hi> <end lo,hi> <(end-start+1) bytes> 
segment: ...
```

`DriverBinaries::LoadIntoAtari` walks the segments and `memcpy`s each into the
64 KB RAM array at its declared address. `v2` corresponds to RMT's
`UNPATCHED_WITH_TUNING` driver variant. The driver loads to `$3400`.

### Driver entry points

Defined in `RmtAddresses.h`. The first five are a stable jump table at the
driver's base; the latter three are fixed routine addresses.

| Symbol | Address | Purpose |
|--------|--------:|---------|
| `RMT_INIT` | `$3400` | Initialise the driver/song state. Called once after load with `A=0, X=0, Y=$3F`. |
| `RMT_PLAY` | `$3403` | Full play step (unused by PokeyForge). |
| `RMT_P3` | `$3406` | One processing step — advances envelopes, note tables, vibrato, fades. |
| `RMT_SILENCE` | `$3409` | Silence all channels. |
| `RMT_SETPOKEY` | `$340C` | Write the driver's POKEY shadow values into `$D200..$D208`. |
| `RMT_ATA_SETNOTEINSTR` | `$3D00` | Set note + instrument for a track. `A=note, X=track, Y=instrument`. |
| `RMT_ATA_SETVOLUME` | `$3E00` | Set a track's volume. `A=volume, X=track`. |
| `RMT_ATA_INSTROFF` | `$3E80` | Turn an instrument off. |

Instruments live at `$4000 + slot*256`; PokeyForge only uses slot 0 for live
audition.

---

## 5. CPU and POKEY ABIs

### `sa_c6502.dll`

```c
void C6502_Initialise(BYTE* memory);                       // bind 64 KB RAM
int  C6502_JSR(WORD* adr, BYTE* a, BYTE* x, BYTE* y, int* cycles);
void C6502_About(char** name, char** author, char** desc);
```

`C6502_JSR` performs a `JSR` to `*adr`, runs until `RTS` (or the `cycles`
budget is exhausted), and writes back the registers and remaining cycles.
PokeyForge's `Atari::JSR` supplies a one-frame cycle budget
(`clock / frameRate`).

### `sa_pokey.dll`

```c
void Pokey_Initialise(int*, char**);                       // called with (0,0)
void Pokey_SoundInit(DWORD clockHz, WORD samplesPerSec, BYTE channels);
void Pokey_Process(BYTE* buffer, const WORD numSamples);   // U8 PCM out
void Pokey_PutByte(WORD addr, BYTE value);                 // register write
void Pokey_About(char**, char**, char**);
```

PokeyForge runs **mono** (`channels = 1`). Each frame it copies registers
`0..7` (AUDF1/AUDC1…AUDF4/AUDC4 from `$D200..$D207`) and register `8`
(AUDCTL from `$D208`) via `Pokey_PutByte`, then calls `Pokey_Process` for that
frame's worth of samples.

### Clock constants

| Region | Clock (Hz) | Frame rate | Cycles/frame |
|--------|-----------:|-----------:|-------------:|
| PAL | 1,773,447 | 50 | 35,468 |
| NTSC | 1,789,773 | 60 | 29,829 |

Toggling region (`F5`) re-runs `Pokey_SoundInit` with the new clock and
re-initialises the driver.

---

## 6. The playback engine (`RmtEngine`)

`RmtEngine` is the heart of PokeyForge. Key methods:

- `Init(obx, ntsc, samplesPerSec)` — loads the CPU + POKEY DLLs, loads the
  driver, runs `RMT_INIT`.
- `LoadInstrumentSlot(slot, blob, len)` — zeroes the 256-byte slot at
  `$4000+slot*256` and copies the ATA blob in.
- `NoteOn(track, note, instr, vol)` — `JSR RMT_ATA_SETNOTEINSTR` then
  `JSR RMT_ATA_SETVOLUME`.
- `NoteOff(track)` — sets the track volume to 0.
- `Silence()` — `JSR RMT_SILENCE`.
- `Generate(buffer, numSamples)` — the per-frame loop: for each emulated VBI
  frame it runs `RMT_P3`, `RMT_SETPOKEY`, syncs POKEY registers, and asks the
  POKEY emulator for `samplesPerFrame` samples, until `numSamples` is filled.

### Threading

`Generate` runs on SDL's **audio thread**; `NoteOn`, `LoadInstrumentSlot`,
`SetNTSC`, etc. run on the **main thread**. All of these take a single
`std::mutex`, because they all touch the shared 64 KB RAM array and the CPU/
POKEY emulator state. Audio frames are short, so contention is negligible.

---

## 7. Audio output (`Audio`)

`Audio::Open` opens the default playback device with
`SDL_OpenAudioDeviceStream` configured as `SDL_AUDIO_U8`, 1 channel, 44100 Hz,
and registers a pull callback. When SDL needs more data, the callback asks
`RmtEngine::Generate` to fill a scratch buffer and pushes it with
`SDL_PutAudioStreamData`. If the engine can't produce (e.g. during teardown)
the callback emits `0x80` (U8 silence) to avoid underrun glitches.

---

## 8. Directory model (`Directory`)

`Scan(root)` recursively walks the folder with
`std::filesystem::recursive_directory_iterator` semantics (implemented as a
manual recursion so ordering is controlled): within each folder, subfolders
come first (alphabetical), then `.RTI` files (alphabetical).

The model stores a flat `vector<Node>`; node 0 is the root. Each file node also
carries analysis fields (`category`, `is_duplicate`). Three derived lists are
rebuilt by `RebuildViews()`:

- `AllFiles()` — every `.RTI` node, depth-first (used by analysis; never filtered).
- `Rows()` — the display rows for the tree pane. In **Folder** view these are the
  expand-aware folder/file nodes; in **Category** view they are category header
  rows followed by the files in each category. Duplicate files are omitted when
  hide-duplicates is on.
- `NavFiles()` — the visible files in navigation order (`←/→`), honouring the
  current view mode and duplicate hiding.

`SetViewMode`, `SetHideDuplicates`, `SetFilter`, `ToggleExpanded`, and
`SetFileAnalysis` (+ `RebuildViews`) drive the lists. A non-empty `SetFilter`
collapses both lists to a flat, alphabetical set of files whose name contains
the (case-insensitive) query, ignoring folders/categories. Navigation
(`NextFile`, `PrevFile`, `StepFiles`, `SetCurrentFileIndex`, `SelectByNode`)
operates on the `NavFiles` index and preserves the selected node across rebuilds
when possible. The tree scroll (`m_tree_scroll`, in `Gui`) is persistent and
only moves when the selection would leave the viewport, so clicking a row
doesn't make the list jump.

---

## 8a. Analysis (`Analysis` + `analysis.json`)

`Analysis::Run(dir, libraryRoot, writeJson)`:

1. For each file in `dir.AllFiles()`, loads the `.RTI`, computes a 64-bit FNV-1a
   hash of its **ATA blob** (the sound definition — independent of filename and
   instrument name), and classifies it with `Classify()`.
2. **Duplicates**: the first file seen for a given hash is the keeper; later
   files with the same hash are flagged `is_duplicate` with `duplicate_of` set
   to the keeper's path.
3. Applies the results to the directory (`SetFileAnalysis` + `SetCategoryNames`)
   and rebuilds its views.
4. Writes `analysis.json` into the library folder.

`Classify()` is a heuristic over the decoded `TInstrument`: channel-join → Bass;
short + fast-fade + noise → Percussion; noise distortion → Noise/FX; short +
fast-fade → Percussion; looping envelope without fade → Pad; pure-tone (`0x0A`)
→ Lead; else Other.

`analysis.json` is a small JSON object: `version`, `library`, and an
`instruments` array with one object per file (`path` relative to the library,
`name`, `hash`, `len`, `category`, `duplicate_of`). It is written one instrument
per line so `LoadAndApply` can parse it with a simple per-line key scan (no JSON
library). On startup, if `analysis.json` is present it is applied and duplicates
are hidden by default.

---

## 9. Bank model (`Bank`)

A fixed `std::array<Slot, 64>`. Each used slot keeps the source path, name,
RTI version, a copy of the ATA blob, and a `dirty` flag (added/edited since the
last save to disk — rendered orange; cleared by `MarkAllClean()` after `F2`).
`SetSlot` / `AddWorking` store a raw working ATA blob (used by `Ctrl+Ins` and
when committing edits).

**`SaveTo(outdir)`** — individual `.RTI` files:

1. Creates `outdir`.
2. For each used slot, writes `NN_<sanitised name>.rti` in the exact RMT RTI
   layout (§3).
3. Writes `manifest.txt` mapping `slot → name → source path`.

Filename sanitisation replaces characters illegal on Windows and trims trailing
dots/spaces.

**`SaveRmt(path)`** — single `.RMT` module (see §9a for the format). Two
paths, picked by whether the bank was loaded from a `.rmt` source:

- **Round-trip path** (`m_rmt_source.valid == true`): rebuilds the source
  module's first block with current bank slots substituted into the
  instrument-pointer table. Preserves the original song, tracks, channel
  count, header speeds, song name, and goto target. Instrument count grows
  to fit any added slots; never shrinks below the source count, so the
  song's instrument-index references in track data stay valid (removed
  slots get a zero pointer — RMT silences those notes). Instrument names
  follow the user's current bank-slot names.
- **Silent-loop fallback** (`m_rmt_source.valid == false`): hand-built
  module with 4 channels, one 64-line empty pause track, a one-line song
  looping forever, and song name `"PokeyForge Bank"`. Used when the bank
  was built from scratch or loaded from a manifest folder.

ATA blobs are emitted directly into the instrument data area, so no
re-encoding is needed.

**`LoadFromManifest(folder_or_manifest)`** — clears the bank, reads
`manifest.txt`, and reloads each referenced `.RTI` (falling back to the
`NN_*.rti` file beside the manifest if the recorded source path is gone).
Leaves `m_rmt_source.valid` cleared, so a subsequent `SaveRmt` takes the
silent-loop path.

**`LoadFromRmt(path)`** — clears the bank, parses the RMT module's first
block, walks the instrument-pointer table, and copies each instrument's
ATA blob into the matching slot. Blob length is recovered from the blob's
own header (`blob[2] + 3`, the envelope-end pointer plus the final 3-byte
column). Names come from the module's second (names) block.

In addition, captures `RmtModuleMeta` for round-trip on save: header
config bytes 3–7 (channels, track length, song/instrument speeds, format
version), the song name, each track's event blob (sliced using a sorted
boundary set of `{ track pointers, ptrSongData, blockEnd, instrument
pointers }`), and the song-data byte stream up through its first 254-goto
record. Stored on `m_rmt_source`; reset to defaults by `Clear()`. Known
limitations:

- Tracks are sliced contiguously; over-reads into trailing zeros are
  silently terminated by RMT's track interpreter.
- The song scan stops at the first 254-goto row. Multi-section modules
  with additional sub-songs lose everything past that point; a warning
  is logged to stderr.
- Validation failures (out-of-range pointers, track-table size
  disagreement, missing 254-goto) leave `m_rmt_source.valid = false`,
  so the bank still loads its instruments but SaveRmt falls back to
  silent-loop.

The UI keeps a **bank cursor** (selected slot). The mouse sets it via
`Gui::BankSlotAtLogical` (logical-coordinate hit-test that shares geometry with
the renderer); `Tab`/`Shift+Tab` and `Ctrl`+arrows move it (`Ctrl`+Left/Right by
one, `Ctrl`+Up/Down by a row of 8). `Ctrl`-modified keys act on the selected
slot:

- `Ctrl + a-z/0-9` → `App::PlayBankSlot` loads the slot's blob into a dedicated
  engine instrument slot (`kSampleSlot = 1`, distinct from the current
  instrument at `kInstrSlot = 0`) and triggers a note, so sampling a bank slot
  doesn't disturb the loaded instrument.
- `Ctrl+Ins` → `App::RequestInsertIntoCursor`; overwrites the selected slot with
  the working instrument, first showing a Yes/No confirm if the slot is occupied.
- `Ctrl+Del` → `App::RequestRemoveCursor`; clears the selected slot after a
  confirm.

The confirm is a generic Yes/No modal (`App::AskConfirm` + `Gui::DrawConfirm`)
that captures a `std::function` action and swallows keyboard/mouse until
resolved.

Clicking a filled slot loads it as the current instrument (`App::LoadBankSlot`),
decoding its blob via an in-memory `.RTI` wrapper.

---

## 9a. The `.RMT` module format (export/import)

A `.RMT` file is **two Atari binary blocks** (§4). PokeyForge's silent-loop
export targets `$4000`; the round-trip export preserves whatever load
address the source module used.

**Block 1 — the module.** 16-byte header:

| Offset | Bytes | Meaning |
|-------:|------:|---------|
| 0 | 3 | `"RMT"` |
| 3 | 1 | channel count: `'4'` for mono, `'8'` for stereo |
| 4 | 1 | track length (PokeyForge silent path uses 64; round-trip preserves source) |
| 5 | 1 | song speed (silent: 6; round-trip: from source) |
| 6 | 1 | instrument speed (silent: 1; round-trip: from source) |
| 7 | 1 | RMT format version (1) |
| 8–9 | 2 | pointer to instrument table |
| 10–11 | 2 | pointer to track low-byte table |
| 12–13 | 2 | pointer to track high-byte table |
| 14–15 | 2 | pointer to song data |

**Silent-loop layout** (used when no source `.rmt` was captured): the
instrument-pointer table (`numInstruments × 2`, little-endian; `0` for
empty slots), the track tables, the instrument ATA blobs, one empty track
(`{62, 64}` = a 64-beat pause), and a song of one line referencing track 0
on all channels followed by a `254`-goto looping to line 0. The result is a
valid, silent, looping module whose only real payload is the instruments.

**Round-trip layout** (used when `m_rmt_source.valid == true`): header
copied from source (bytes 3–7), instrument-pointer table sized to
`max(source_count, current_highest_used+1)`, tracks_lo / tracks_hi tables
sized to `source.num_tracks`, then the captured song data verbatim (with
its trailing 254-goto target re-pointed at the new song base), then each
captured track's event blob (pointers recorded back into tracks_lo/hi),
then each used slot's current ATA blob (pointers recorded into the
instrument table).

**Block 2 — names.** The song name (silent: `"PokeyForge Bank"`;
round-trip: from source) followed by each used instrument's name
(zero-terminated, in slot order — names follow user edits in both paths).

Import (`LoadFromRmt`) reverses this: it reads block 1 into a 64K image,
derives `numInstruments` from `(trackLoPtr − instrPtr) / 2`, and extracts
each non-zero instrument pointer's blob. See §9's `LoadFromRmt` description
for the metadata it also captures for round-trip.

---

## 9b. Config (`pokeyforge.json`)

A flat JSON object written next to the executable (path from
`SDL_GetBasePath()`):

```json
{
  "library": "C:\\path\\to\\instruments",
  "last_bank": "C:\\path\\to\\drums.rmt",
  "last_file": 12
}
```

Hand-rolled serialisation escapes `\`, `"`, newline, and tab; the parser is a
tolerant key-scan tailored to this fixed schema (no general JSON library). On
boot with no command-line argument, `library` + `last_file` restore the previous
session; `Config::Save()` is called on exit and whenever the library or bank
changes.

---

## 9c. Editing (`Editor` + `App`)

Edit mode is an explicit toggle (`F6`, `App::editor.active`). Browse-mode keys
are unchanged; editing only happens when active.

- **Cursor model** (`Editor`): a focused `Panel` (Params / Envelope / NoteTable
  / Name) with a per-panel cell cursor. `Tab` cycles panels; arrows move the
  cell; `InputHex` types values (a `m_typing_fresh` flag makes the first digit
  replace and subsequent digits shift, so two-digit fields compose); `Increment`
  nudges ±1 with per-field clamping (e.g. distortion stays even). The editable
  parameter list and per-envelope-row ranges live in `Editor.cpp`.
- **Name editing**: the Name panel is a variable-length text field over the
  fixed 33-byte buffer. `InsertChar` inserts at the caret (shifting right),
  `Backspace`/`DeleteForward` remove and shift left, `NameLength` trims trailing
  spaces. `name_pos` is an insertion point in `[0, length]`. These keys
  auto-repeat (excluded from the repeat filter while editing).
- **Working copy + live audio**: edits mutate `App::current_instr`; `ApplyEdit`
  re-encodes it to `working_ata` via `RtiFile::InstrumentToAta` and reloads
  engine slot 0, so changes are audible immediately. `Ctrl+key` auditions the
  edited instrument; `Space` re-triggers the last note.
- **Persistence**: originals are never written. `modified` (header "MODIFIED")
  tracks unsaved edits; committing to the bank (`+`, `Ctrl+Ins`, or the navigate
  -away prompt's "Keep") stores the working blob into a slot and marks it dirty
  (orange). Disk writes happen only through the bank save (`F2`).
- **Cursor**: the Parameters panel uses a 2D screen-layout map
  (`ParamCellPos`/`ParamCellIdx` in `Editor.cpp`) so arrows follow the visible
  grid (two 6-row columns + the 4×2 AUDCTL block). `Shift+↑/↓` nudge the value;
  plain arrows move the cursor.
- **Undo / redo**: each applied edit pushes the prior `TInstrument` onto
  `undo_stack` and clears `redo_stack`. `Ctrl+Z` moves a snapshot to
  `redo_stack`; `Ctrl+Y` moves it back. Both cleared on load.
- **Export**: `Ctrl+S` writes the working instrument to a new `.RTI`
  (`RtiFile::WriteFile`) via a Save dialog — originals are never overwritten.
- **Bank clipboard**: a bank **EDIT** toggle (button in the bank panel) gates
  `Ctrl+C/X/V` — when on they copy/cut/paste a `Bank::Slot` between slots
  (cut+paste = reorder); when off they fall through and play the slot.
  `last_bank` from `pokeyforge.json` is auto-reloaded on startup.
- **Navigate-away guard**: `App::GuardedNav` wraps file/library/bank-load
  navigation; if `modified`, it defers the action and shows the Save-prompt
  modal (`K`/`D`/`C` → Keep-to-bank / Discard / Cancel).

The `Editor::Describe(TInstrument)` call returns the panel name, field label,
value, range, and help text for the focused cell, which the GUI renders in the
edit bar.

---

## 10. GUI (`Gui`)

Pure immediate-mode rendering with SDL's primitives plus `SDL_RenderDebugText`
(the built-in 8×8 font), so there is **no font/asset dependency**. The renderer
draws at a logical 1280×720 surface; `SDL_SetRenderLogicalPresentation` with
`LETTERBOX` lets the window resize and go fullscreen (`F11`) without changing
the layout code.

`Gui::Render` takes a const `GuiState` snapshot each frame (pointers to the
`Directory`, `Bank`, current `RtiFile`, decoded `TInstrument`, plus octave/
clock/help flags, the bank cursor, the library path, and any transient notice)
— the GUI never mutates application state.

A top-right **master scope** (`DrawMasterScope`) draws channel 1's waveform
synthesized from the POKEY registers (`RmtEngine::SnapshotPokey`). A **search
bar** strip (`DrawSearchBar`, `PointInSearchBar`) sits below the directory tree
in the left column. The modals (`DrawAbout`, `DrawSavePrompt`, `DrawConfirm`)
have **clickable buttons** with matching hit-tests (`SavePromptButtonAt`,
`ConfirmButtonAt`) as well as keyboard shortcuts; a `DrawBusy` popup is shown
before the blocking Analyse. Click hit-tests (`MenuAtLogical`, `DirTabAtLogical`,
`TreeRowAtLogical`, `PointInBankEdit`) cover the menu bar, the directory tabs,
tree rows, and the bank EDIT toggle. Files dropped on the window
(`SDL_EVENT_DROP_FILE`) open as a library (folder) or load + select (a `.RTI`).

Panels: a top **menu bar** (clickable Save / Load / Library / Analyse / About / Help buttons +
`[EDIT]` / `MODIFIED` / clock / octave / bank status), directory tree (with
scroll-to-current and name truncation), instrument header (working name +
source filename + name caret), parameters + AUDCTL, envelope grid, note table,
bank grid (tiles labelled `NN-name`; green = saved, orange = dirty, red border =
current, yellow outline = selected), the command bar, the edit bar, and the
F1-help / save-prompt / confirm overlays (dimmed, alpha-blended modals).

The 8 AUDCTL flags render as a 4×2 grid of wide toggle boxes so the labels
(e.g. `1.79MHz3`) fit. **Right-clicking** any binary field calls
`Editor::ToggleBinary` to flip it 0↔1 without typing (AUDCTL flags, table
type/mode, envelope filter/portamento).

While editing, the focused panel gets a bright white double `FocusFrame` and a
`[EDITING]` title; the focused cell gets a bold red `CellCursor` (translucent
wash + double border). `DrawEditBar` shows the `Editor::Describe` readout and the
control hints.

Bank-grid geometry lives in one `ComputeBankGeom()` helper used by both the
renderer and `BankSlotAtLogical`, so the drawn tiles and the mouse hit-test can
never drift apart. `MenuAtLogical` and `EditFieldAtLogical` likewise mirror the
draw geometry of the menu bar and instrument panels for click hit-testing. Mouse
events are mapped from window space to the logical 1280×720 space with
`SDL_ConvertEventToRenderCoordinates` before hit-testing; left-click selects /
loads / edits, right-click toggles a binary field.

Native file/folder dialogs (`SDL_ShowSaveFileDialog`, `SDL_ShowOpenFileDialog`,
`SDL_ShowOpenFolderDialog`) are run synchronously in `main.cpp`: each is fired
with a callback, then a small event-pump loop spins until the callback reports a
result or cancellation.

---

## 11. Building

**Toolchain:** Visual Studio 2022 (v143 toolset), x64, C++17.

```
MSBuild PokeyForge.sln /p:Configuration=Debug   /p:Platform=x64
MSBuild PokeyForge.sln /p:Configuration=Release /p:Platform=x64
```

(or open `PokeyForge.sln` in the IDE).

The project links as a **Windows subsystem** app (no console window). The entry
point comes from `<SDL3/SDL_main.h>` (included in `main.cpp`), which provides
`WinMain` and calls our `main()`. Because there is no console, fatal startup
failures (missing DLL/`.obx`, bad library, engine/audio init failure) are
reported via `SDL_ShowSimpleMessageBox` (the `Fatal` helper) rather than
`stderr`. `app.rc` compiles `PokeyForge.ico` into the exe as the application icon;
the window icon is generated procedurally at runtime (`MakeAppIcon`). A loading
splash (`DrawSplash`) is shown during startup.

### Dependencies

- **SDL3** (3.4.8) — headers/libs vendored at `lib/SDL3-3.4.8/`. The project
  references `lib/SDL3-3.4.8/include` and links `lib/SDL3-3.4.8/lib/x64/SDL3.lib`.
- **Runtime assets** in `runtime/`, copied next to the executable by a
  post-build step:
  - `SDL3.dll`
  - `sa_pokey.dll`, `sa_c6502.dll` (Altirra emulation cores)
  - `rmt_driver_v2.obx` (RMT tracker driver, UNPATCHED_WITH_TUNING)

Output goes to `build/<Configuration>/`. The post-build `xcopy` ensures the
DLLs and `.obx` sit beside `PokeyForge.exe` so it runs in place.

---

## 12. Known limitations / future work

- **Master scope is channel 1 only**, synthesized from the live POKEY registers
  (the DLL only exposes mixed PCM). It shows the audition voice's waveform.
- **Mono, 4-channel.** Stereo (dual POKEY, 8-channel) is not wired up; the ATA
  decoder already has the stereo branch but the engine forces mono. The `.RMT`
  export is likewise 4-channel.
- **Two audition slots, monophonic.** Engine slot 0 holds the current
  instrument and slot 1 is used for bank-slot sampling; playback is still one
  note at a time (no chords).
- **`.RMT` export is a hand-built module.** It is byte-laid-out to match RMT's
  format (header, instrument table, one empty track, looping song, names block)
  but is not produced by RMT's own `MakeModule`; it should be verified against
  RMT on format changes.
- **Driver version is fixed** to `v2`. Other RMT driver variants exist in the
  RMT tree but aren't selectable at runtime.
- **Windows-only**, by design — it depends on the Altirra DLLs.
