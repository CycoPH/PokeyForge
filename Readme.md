# PokeyForge

**Audition, inspect, and curate Atari RMT instrument files.**

PokeyForge is a standalone Windows tool for working with `.RTI` files — the
instrument format used by [Raster Music Tracker (RMT)](https://en.wikipedia.org/wiki/Raster_Music_Tracker)
on the Atari 8-bit. It lets you:

- **Browse** a whole folder tree of `.RTI` files at once.
- **Play** any instrument across the keyboard at different pitches, so you can
  hear what it actually sounds like without opening each one in RMT.
- **Inspect** an instrument's internals — its parameters, AUDCTL flags,
  envelope, and note table — laid out on a single screen.
- **Edit** an instrument in place: move a cursor through any field, type new
  values, rename it, and hear the change immediately.
- **Collect** the ones you like into a 64-slot *bank*, audition any slot, then
  **save** it as both a set of individual `.RTI` files and a single `.RMT`
  module that RMT can open.
- **Reload** a saved bank later, **switch** between instrument libraries, and
  pick up where you left off — PokeyForge remembers your last library and the last
  instrument you were on.

![PokeyForge](screenshots/main.png)

The sound is produced by the exact same emulation RMT itself uses (the Altirra
POKEY and 6502 cores plus the RMT tracker driver), so what you hear in PokeyForge
is what you'll hear in RMT.

> **How edits are saved:** PokeyForge never overwrites your original `.RTI` files.
> Edits live in a working copy; to keep them, put the instrument in the bank
> (`+` or `Ctrl+Ins`) and save the bank (`F2`). Anything modified-but-unsaved is
> marked **orange** so you can see what still needs saving.

---

## Features

**Authentic sound**
- Plays instruments through the **real RMT signal chain** — an emulated 6502
  running the RMT tracker driver into an Altirra-grade POKEY core — so the
  output matches Atari hardware and RMT itself.
- **PAL/NTSC** clock toggle; **±octave** transposition.
- Live **master oscilloscope** showing the channel-1 waveform as it plays.

**Browse a whole library at once**
- Recursive folder scan of every `.RTI` under a directory.
- Multiple list views: **Folders**, **Category**, **All**, and **No duplicates**.
- **Type-to-search** filter bar to jump to an instrument by name.
- Automatic **categorisation** (bass / lead / percussion / noise-FX / pad) and
  **duplicate detection** across the library.
- **Drag-and-drop** a folder or a single `.RTI` onto the window to load it.

**Audition**
- Play any instrument chromatically across the keyboard (`a–z`, `0–9`).
- Browse with the arrow keys / mouse wheel; `Esc` silences without exiting.
- Sample any **bank slot** in place with `Ctrl` + an audition key.

**Inspect & edit**
- Every instrument laid out on one screen: **parameters**, **AUDCTL** flags,
  **envelope**, and **note table**.
- Move a cursor through any field and edit with **hex/decimal entry**,
  **Shift+↑/↓ nudge**, or right-click **bit toggles** — and hear the change
  immediately.
- **Rename** instruments inline.
- Full **undo / redo** (`Ctrl+Z` / `Ctrl+Y`).
- **Export** an edited instrument as a new standalone `.RTI` (`Ctrl+S`).

**Build a bank**
- Curate a **64-slot bank**; copy / cut / paste / reorder slots in EDIT mode.
- Save the bank as a set of individual `.RTI` files **and** a single
  RMT-compatible **`.RMT`** module (plus a manifest).
- **Reload** a saved bank, **switch libraries** on the fly, and resume where you
  left off — the last library, bank, and instrument are remembered between runs.

**Standalone & self-contained**
- Single Windows `.exe`, no installer and no MFC — built in **C++17 with SDL3**.
- App icon, splash screen, in-app **About** and **F1 keybindings** overlay, and
  friendly error dialogs.

---

## Getting started

### Launch with a folder

```
PokeyForge.exe "C:\path\to\your\instruments"
```

PokeyForge scans that folder **and all its subfolders** for `.RTI` files and opens
ready to play the first one.

### Launch with no folder

```
PokeyForge.exe
```

If you've used PokeyForge before, it reopens the **last library** you browsed and
jumps back to the **last instrument** you were on. If there's no remembered
library (first run, or the saved folder no longer exists), a folder-picker
dialog appears — choose the folder containing your `.RTI` files. Cancelling the
dialog on first run exits the program.

You can change libraries any time with `F4` (see *Switching libraries* below).

### Launch with a single file

```
PokeyForge.exe "C:\path\to\bass1.rti"
```

PokeyForge loads the file's parent folder and jumps straight to that file.

---

## The screen at a glance

PokeyForge shows everything on one screen — there are no separate tabs or windows
to switch between. As you move through the directory, every panel updates to
reflect the currently selected instrument.

```
+-----------------------------------------------------------------------+
| [Save F2][Load F3][Library F4][Help F1]   Clock:PAL Oct:+0 Bank:03/64  |  <- Menu bar / header
+--------------------+--------------------------------------------------+
|  Directory         |  Instrument: "bass1" v1 (NN ATA bytes) file:b... |
|                    |--------------------------------------------------|
|  v drums/          |  Parameters         AUDCTL [15kHz][HPF1]...      |
|    B kick.rti      |  Tbl Len : 04  ...                               |
|      snare.rti     |--------------------------------------------------|
|  > leads/          |  Envelope                                        |
|    bass1.rti  <--  |   VolR  [F][F][E][C]...                          |
|    bass2.rti       |   VolL  [F][F][E][C]...                          |
|                    |--------------------------------------------------|
|                    |  Note table   [00][04][08]...                    |
|                    |--------------------------------------------------|
|  12 / 87 files     |  Bank  [00-kick][01-bass][02-..][  ]...          |
+--------------------+--------------------------------------------------+
| F2 Save F3 Load F4 Library | Ctrl+key sample  Ctrl+Ins set  Ctrl+Del  |  <- Command bar
+-----------------------------------------------------------------------+
```

When you turn on **Edit mode** (`F6`), the focused panel gets a bright white
frame and an **edit bar** appears above the command bar, naming the field under
the red cell cursor, its value and range, and what it does.

**Panels:**

| Panel | What it shows |
|-------|---------------|
| **Menu bar / header** | Clickable buttons — **Save (F2)**, **Load (F3)**, **Library (F4)**, **Analyse (F7)**, **About**, **Help (F1)** — plus status (PAL/NTSC clock, octave shift, bank fill count, `[EDIT]` / `MODIFIED`), and a small **live scope** in the top-right corner showing channel 1's waveform as it plays. |
| **Search bar** (bottom of the right column) | Type-to-filter the instrument list by name (press `/` or click it). |
| **Directory** (left) | The folder tree. `>` is a collapsed folder, `v` is expanded. The highlighted row is the instrument you're auditioning. A `B` next to a file means it's in your bank. Long names are shortened with a trailing `~`; the full filename always appears in the instrument header. |
| **Instrument header** | The selected instrument's internal name, RTI version, ATA byte size, and the source **filename** (handy when the tree truncates it). |
| **Parameters** | The instrument's 12 main parameters (table length/goto/speed/type/mode, envelope length/goto, volume fade/min, delay, vibrato, frequency shift) plus the 8 AUDCTL flag toggles. |
| **Envelope** | The 8-row envelope grid (volume left/right, filter, command, distortion, portamento, X, Y) across each step. Brighter cells = higher values. A red marker shows the envelope's loop (goto) point. |
| **Note table** | The instrument's note/frequency table; the loop (goto) step is highlighted. |
| **Bank** | Your 64 collected instruments as an 8×8 grid, each tile labelled `NN-name` (e.g. `00-kick`). Saved slots are green; **orange** slots are added/edited but not yet saved to disk; the slot holding the current instrument has a red border; the **selected** slot (for Ctrl operations) has a yellow outline. |
| **Command bar** (bottom) | Quick reminders of the file/bank actions (`F2 Save`, `F3 Load`, `F4 Library`, the `Ctrl` bank verbs), the current library path, and transient confirmations like *"Saved drums.rmt + 12 .rti"*. |
| **Edit bar** (when editing) | Appears above the command bar in Edit mode: names the focused panel and field, shows its current value + range, and a one-line description of what the field does, plus the editing controls. |

---

## How to use it

### Hearing an instrument

Press any letter or number key to play the **currently selected** instrument
at a pitch:

- `a`–`z` then `0`–`9` form one long chromatic scale — 36 semitones in total,
  from low to high. `a` is the lowest, `9` is the highest.
- Use `[` and `]` to shift the whole keyboard down or up by an octave, so you
  can hear the same instrument in a very low or very high register.

Notes play one at a time. Each instrument plays out according to its own
envelope and fade — you don't need to release the key. Press `Esc` at any time
to cut the sound.

### Moving through the directory

- `←` / `→` or `↑` / `↓` — step to the previous / next instrument. **Hold the
  key** to scroll quickly.
- **Mouse wheel** — fly through the list (3 instruments per notch).
- **Click** any instrument in the tree to select it; click a folder to
  expand/collapse it, or a category header to collapse it.
- `PageUp` / `PageDown` — jump 10 files at a time.
- `Home` / `End` — jump to the first / last file.
- `/` — **search**: start typing to filter the list to instruments whose name
  contains what you type. Arrows still move through the matches; `Enter` keeps
  the filter, `Esc` clears it.
- `Enter` — collapse or expand the folder that the current file lives in, to
  tidy up the tree as you work.

The directory list always wraps around — going past the last file brings you
back to the first (via the arrows) — and every panel refreshes as you move.

### Building a bank

The bank is your shortlist of favourite instruments — up to 64 of them.

**Selecting a slot:** click a bank tile with the mouse, or press `Tab` /
`Shift+Tab` to move the yellow selection cursor. The selected slot is the target
of the `Ctrl` operations below. Clicking a *filled* slot also loads it as the
current instrument (so you can edit it).

**Adding:**

- `+` (or `=`) — add the current instrument to the next free bank slot.
- `Ctrl+Ins` — copy the current instrument into the **selected** slot,
  overwriting whatever was there.

**Auditioning a slot:**

- `Ctrl` + an audition key (`a`–`z` / `0`–`9`) — play the **selected** bank
  slot at that pitch. Plain keys (no `Ctrl`) still play the current instrument,
  so you can A/B the two.

**Removing:**

- `-` — remove the instrument you're currently auditioning from the bank.
- `Ctrl+Del` — remove the **selected** slot. (Plain `Delete` does nothing to the
  bank, to avoid accidental deletions.)

Added or edited slots show **orange** until you save the bank, then turn green.

**Saving:**

- `F2` — opens a standard Windows *Save As* dialog. Type a name and choose a
  location.

PokeyForge then writes **two** things at that location:

- `<name>.rmt` — a single RMT module containing all your bank instruments (with
  a silent placeholder song). Open it directly in RMT; the instruments populate
  the instrument table.
- `<name>_rti/` — a folder of one `.RTI` per filled slot (`00_kick.rti`,
  `01_bass1.rti`, …) plus a `manifest.txt`. Load these individually in RMT with
  *File ▸ Load Instrument*.

**Reordering:**

- `Ctrl+C` copies the selected slot, `Ctrl+X` cuts it, `Ctrl+V` pastes into the
  selected slot. Cut + paste moves an instrument to a different slot.

**Loading:**

- `F3` — opens a file picker. Choose either a `.rmt` file (PokeyForge reads its
  instrument table) or a saved bank's `manifest.txt` (PokeyForge re-reads the
  individual `.RTI` files). The bank is replaced with what you load.
- The **last bank you saved or loaded is reopened automatically** on the next
  launch (remembered in `pokeyforge.json`).

### Editing instruments

Press `F6` to toggle **Edit mode** (the header shows `[EDIT]`). Browse mode is
unchanged; nothing is editable until you turn this on.

In Edit mode:

- The **focused panel** (Parameters, Envelope, Note table, or Name) gets a
  bright white frame, and a red cursor marks the exact cell you're changing. The
  **edit bar** at the bottom names the field, its value and range, and what it
  does.
- `Tab` / `Shift+Tab` — move between the four panels.
- Arrow keys — move the cell cursor.
- `0`–`9`, `A`–`F` — type a value (two-digit fields compose); `+` / `-` nudge by
  one.
- **Right-click** any binary field — an AUDCTL flag, the table type/mode, or an
  envelope filter/portamento cell — to flip it between 0 and 1 instantly,
  without typing. (Left-click any field to put the cursor there.)
- In the **Name** panel, type to insert characters, `Backspace` / `Delete` to
  remove (hold to repeat), arrows to move the caret.
- `Ctrl` + an audition key — hear the instrument you're editing at that pitch;
  `Space` re-triggers the last note. Changes are applied and audible live.
- `Ctrl+Ins` drops the edited instrument into the selected bank slot;
  `Ctrl+Del` clears the selected slot.

Edits never touch your original `.RTI` files. The header shows **MODIFIED**
while a working copy has unsaved changes. `Ctrl+Z` / `Ctrl+Y` undo / redo your
edits. If you navigate to another instrument with unsaved edits, PokeyForge asks
**Keep in bank / Discard / Cancel** (click the buttons or press `K` / `D` / `C`)
— "Keep" stores the edit into the bank, where it stays orange until you save the
bank with `F2`.

To save an edited instrument on its own (not via the bank), press `Ctrl+S` to
**export it as a new `.RTI`** — a Save dialog lets you choose the name and
location. The original file is left untouched.

### Switching libraries

`F4` opens a folder picker so you can point PokeyForge at a different folder of
`.RTI` files without restarting. The tree, file list, and panels refresh to the
new library, and the choice is remembered for next launch. (Your bank is left
untouched when you switch.)

### Analysing & organising

`F7` (or the **Analyse** menu button) scans every `.RTI` in the library and:

- **Finds duplicates** — two instruments are duplicates if their sound
  definition is byte-identical, *regardless of filename or instrument name*.
  The first one (alphabetically) is kept; the rest are **hidden** from the
  browse list and tree. Your files are never deleted — `F9` shows/hides the
  duplicates again at any time.
- **Categorises** each instrument (Bass / Lead / Percussion / Noise-FX / Pad /
  Other) from its parameters and envelope. `F8` toggles the directory pane
  between the normal **folder view** and a **grouped-by-category** view.

The results are cached in `analysis.json` in the library folder, so the next
time you open that library the categories and de-duplication are restored
automatically. The classification is heuristic — treat the categories as a
helpful rough grouping, not a strict taxonomy.

### PAL vs NTSC

`F5` toggles between PAL (50 Hz, European Atari) and NTSC (60 Hz, US Atari).
This subtly changes the pitch and the speed at which envelopes advance — match
it to the system your music targets. PokeyForge starts in PAL.

### Display

- `F11` — toggle fullscreen. The layout scales to fit and stays correct at any
  size.
- `F1` — show or hide the on-screen keybindings. While it's open, `F1` or `Esc`
  closes it.

### Quitting

Close the window (the title-bar close button or `Alt+F4`). `Esc` does **not**
quit — it silences playback so you can stop a long sound without leaving the
app.

---

## Keybindings

**Browsing & auditioning**

| Key | Action |
|-----|--------|
| `a`–`z`, `0`–`9` | Play the current instrument at chromatic pitches (low → high) |
| `[` / `]` | Octave shift down / up |
| `←` / `→` or `↑` / `↓` | Previous / next instrument (hold to repeat) |
| Mouse wheel | Move the selection quickly (3 instruments per notch) |
| Click a tree row | Select that instrument (or expand a folder / collapse a category) |
| `PageUp` / `PageDown` | Jump back / forward 10 instruments |
| `Home` / `End` | First / last instrument |
| `/` | Search — filter the list by name (type, `Enter` keeps it, `Esc` clears) |
| `Enter` | Collapse / expand the current file's folder |
| `Esc` | Silence playback (or close the help overlay) |

**Bank**

| Key | Action |
|-----|--------|
| Mouse click | Select a bank slot (filled slot also loads it) |
| `Tab` / `Shift+Tab` | Move the bank selection cursor forward / back |
| `Ctrl` + `←` / `→` | Move the bank cursor by one slot |
| `Ctrl` + `↑` / `↓` | Move the bank cursor by a row (±8) |
| `+` (or `=`) | Add the current instrument to the next free slot |
| `-` | Remove the current instrument from the bank |
| `Ctrl` + `a`–`z`/`0`–`9` | Sample (play) the selected bank slot |
| `Ctrl+Ins` | Copy the current instrument into the selected slot (confirm if occupied) |
| `Ctrl+Del` | Delete the selected slot (confirm) |
| **EDIT** button (bank panel) | Toggle bank-edit: when on, `Ctrl+C/X/V` move slots; when off they play |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | (EDIT on) copy / cut / paste a bank slot (cut + paste = move/reorder) |

**Editing** (`F6` to toggle Edit mode)

| Key | Action |
|-----|--------|
| `F6` | Toggle Edit mode |
| `Tab` / `Shift+Tab` | Move between Parameters / Envelope / Note table / Name |
| Arrows | Move the cell cursor (follows the on-screen grid layout) |
| `0`–`9`, `A`–`F` | Type a value into the cell |
| `+` / `-` or `Shift+↑` / `Shift+↓` | Nudge the value up / down |
| Type / `Backspace` / `Delete` | Edit the name (Name panel; hold to repeat) |
| Left-click a field | Put the edit cursor on that field (enters Edit mode) |
| Right-click a field | Toggle a binary field (AUDCTL flag, type/mode, filter) |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo the last edit |
| `Ctrl+S` | Export the current instrument as a new `.RTI` file |
| `Ctrl` + key | Audition the instrument being edited; `Space` replays |

**Files & display**

| Key | Action |
|-----|--------|
| `F1` | Toggle on-screen help |
| `F2` | Save the bank (`.rmt` + `.rti` folder) via Save dialog |
| `F3` | Load a bank (`.rmt` file or saved bank folder) |
| `F4` | Switch to a different instrument library folder |
| `F5` | Toggle PAL / NTSC |
| `F7` | Analyse: classify instruments + hide duplicates (writes `analysis.json`) |
| `F8` | Toggle folder view / group-by-category |
| `F9` | Show / hide duplicate instruments |
| `F11` | Toggle fullscreen |
| **About** button | Show the about / credits popup (any key or click closes it) |
| Drag & drop | Drop a folder (open as library) or a `.RTI` (open its folder + select) onto the window |
| Close window | Quit |

---

## Requirements

- 64-bit Windows.
- The program ships with everything it needs next to the executable: the SDL3
  library, the POKEY and 6502 emulation libraries, and the RMT tracker driver.
  Don't separate `PokeyForge.exe` from those files.
- On first run PokeyForge writes a small `pokeyforge.json` next to the executable to
  remember your last library, last bank, and last instrument. Deleting it just
  resets those memories; it's safe to remove.
- Running **Analyse** (`F7`) writes `analysis.json` into the *library* folder
  (the categories + duplicate list for that folder). It's also safe to delete;
  it just gets regenerated next time you analyse.

---

## Building from source

**Toolchain:** Visual Studio 2022 (v143 toolset), x64, C++17. SDL3 (3.4.8) is
vendored in the repo under `lib/`, so there's nothing extra to install.

```
MSBuild PokeyForge.sln /p:Configuration=Release /p:Platform=x64
```

(or open `PokeyForge.sln` in the IDE and build). The produced executable is
`PokeyForge.exe`.

Output lands in `build\Release\`, and a post-build step copies the runtime
assets (`SDL3.dll`, `sa_pokey.dll`, `sa_c6502.dll`, `rmt_driver_v2.obx`) next to
the exe so it runs in place. See [`tech.md`](tech.md) for the full architecture
and internals.

---

## Roadmap

- **Done:** browse, play, inspect, and build banks; save banks as `.RMT` +
  `.RTI` files; reload banks; switch libraries; remember your place between
  sessions; in-place editing of parameters, envelope, note table, and name with
  live audition and **undo/redo**; bank slot sampling and Ctrl bank operations;
  search, auto-categorisation, and duplicate detection.
- **Possible future work:** stereo (8-channel) playback, multi-voice/chord
  audition, and selectable RMT driver versions.

---

## Credits

PokeyForge reuses the audio core from Raster Music Tracker (by Raster / Radek
Štěrba, with later work by VinsCool and others) and the standalone POKEY and
6502 emulators (`sa_pokey.dll`, `sa_c6502.dll`) from Avery Lee's Altirra. PokeyForge
itself is an independent front-end built on top of those components.

---

## License

The PokeyForge application code (everything under `src/`) is released under the
**MIT License** — see [`LICENSE`](LICENSE).
Copyright © 2026 RetroCoder.

The bundled runtime components are **not** covered by that license and remain
under their respective authors' terms:

- **SDL3** — zlib license (Sam Lantinga / the SDL contributors).
- **POKEY & 6502 cores** (`sa_pokey.dll`, `sa_c6502.dll`) and the **RMT tracker
  driver** (`rmt_driver_v2.obx`) originate from Altirra and Raster Music Tracker
  respectively; consult those projects for their licensing. They are
  redistributed here for convenience as PokeyForge depends on them at runtime.

If you intend to redistribute PokeyForge, review the upstream licenses for these
third-party components.
