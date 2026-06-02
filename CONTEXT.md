# PokeyForge — Domain Language & Project Context

## What This Is
PokeyForge is a Windows C++17/SDL3 desktop tool for authoring and previewing Atari POKEY instruments in the RMT `.RTI` format. It emulates POKEY audio in real-time via `sa_pokey.dll` (Altirra) and provides a structured editor for all instrument parameters.

## Domain Terms

| Term | Meaning |
|------|---------|
| **RTI** | Raster Music Tracker Instrument — a `.RTI` file containing a single POKEY instrument. Binary format: 3-byte magic "RTI", 1-byte version, 33-byte name, 1-byte ATA length, N-byte ATA blob. |
| **ATA blob** | Raw Atari memory layout for the RMT driver. Produced by `RtiFile::InstrumentToAta`. |
| **POKEY** | Atari's Programmable Sound Generator chip. Has 4 channels with independent distortion, volume, frequency, and filter (AUDCTL) control. |
| **RMT** | Raster Music Tracker — the Atari tracker this tool extends. PokeyForge reuses RMT's instrument data model verbatim (`TInstrument`). |
| **TInstrument** | Core in-memory instrument struct: `name[33]`, `parameters[24]`, `envelope[48][8]`, `noteTable[32]`, cursor fields. |
| **Envelope** | A 48-column × 8-row step-sequencer grid. Rows: VolR, VolL, Filter, Command, Distortion, Portamento, X, Y. Column count set by PAR_ENV_LENGTH. |
| **Note Table** | 32-cell array of semitone offsets (Tbl Type = notes) or raw POKEY frequency bytes (Tbl Type = freq). |
| **AUDCTL** | Atari hardware register controlling POKEY channel pairing, filter routing, and clock frequency. 8 flags, stored as PAR_AUDCTL_* parameters. |
| **PAR_*** | Parameter index constants: PAR_TBL_LENGTH(0), PAR_TBL_GOTO(1), PAR_TBL_SPEED(2), PAR_TBL_TYPE(3), PAR_TBL_MODE(4), PAR_ENV_LENGTH(5), PAR_ENV_GOTO(6), PAR_VOL_FADEOUT(7), PAR_VOL_MIN(8), PAR_DELAY(9), PAR_VIBRATO(10), PAR_FREQ_SHIFT(11), PAR_AUDCTL_15KHZ(12)–PAR_AUDCTL_POLY9(19). |
| **Browse mode** | Default application mode. Keys A–Z and 0–9 play notes for auditioning. Mouse wheel scrolls the instrument list. |
| **Edit mode** | Active when F6 is pressed or any instrument field is left-clicked. Keys type hex values; wheel nudges the focused field. Exited with F6 or Escape. |
| **Distortion mode** | POKEY distortion codes (even values 0–14): 0/2=Buzzy, 4/6=Patchy, 8=Bass, A=Pure, C=Buzzy Bass, E=Silent. |
| **kParams[]** | Array of 20 `ParamEntry` structs in `Editor.cpp`: par_index, max_value, is_flag, label, help string. Drives the Params panel layout and input rules. |

## Architecture

- **Gui.h / Gui.cpp** — Stateless renderer + hit-test. All layout constants hardcoded in pixels. 1280×720 logical canvas (SDL_RenderLogicalPresentation letterbox). Tree pane: x=0–320. Main edit area: x=320–1280.
- **Editor.h / Editor.cpp** — Cursor model and value-change logic. Operates on a working `TInstrument` copy. Input methods: `InputHex`, `Increment`, `ToggleBinary`.
- **main.cpp** — App struct owns all subsystems. `DoEdit(fn)` is the single mutation path: saves before-state → calls fn → pushes undo → clears redo → `ApplyEdit()`. `ApplyEdit()` re-encodes `TInstrument` → ATA blob → loads into engine.
- **Undo/Redo** — `undo_stack` / `redo_stack` (max 128 entries each). Ctrl+Z / Ctrl+Y.

## Panels

| Panel | Location | Content |
|-------|----------|---------|
| Name | Header area y=36 | 33-char instrument name with caret |
| Params | y=70, h=116 | 12 named params (2 cols × 6) + 8 AUDCTL flags (4×2 grid) |
| Envelope | y=192, h=200 | Step-sequencer grid, up to 48 columns × 8 rows |
| NoteTable | y=398, h=64 | 32-cell row |

## Conventions

- All pixel mutations go through `DoEdit(fn)` — never mutate `current_instr` directly.
- `EnterEditField(panel, a, b)` is the single entry point for programmatically placing the edit cursor.
- Binary fields: `is_flag=true` or `maxv==1`. Small-range params: `maxv ≤ 3`.
