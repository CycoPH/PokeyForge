# Changelog

All notable changes to **PokeyForge** are tracked in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

## [1.3.1] - 2026-06-02

### Fixed
- **Scratchy / "too fast" playback in tap mode** (the F12 audio path
  introduced in 1.3.0). The patched `sa_pokey.dll` ships with the same
  `Pokey_Process` contract as upstream Altirra and Raster Music Tracker
  (RMT): `Advance(sndn / 2)` ticks the POKEY scheduler, where `sndn` is
  expected to be the **stereo 8-bit** byte count (i.e. `sample_pair * 2`),
  so the engine advances exactly `sample_pair` ticks per call - matching
  the configured playback rate in real time. RMT uses `CHANNELS = 2`
  and matches this contract; **PokeyForge runs mono 8-bit**, so we were
  passing `sndn = sample_count` and only ticking
  `sample_count / 2` per call. The engine ran at *half* real time, the
  audio tap under-fed by ~2x, and intermediate workarounds (a Generate
  fill loop in `Audio::Callback`) over-corrected and made playback ~2x
  *too fast*.

  Fix: `Pokey::Process` now wraps the DLL call with an internal
  `thread_local` 2x scratch buffer and passes `sndn = numSamples * 2`,
  matching what RMT does. The engine ticks `numSamples` per call - real
  time at 44.1 kHz - and the existing fixed-rate decimator in
  `Audio::Callback` (`kStep = 63337 / 44100 ≈ 1.4357` tap samples per
  host sample) drains the tap buffer at the correct pace with no
  silence patches and no time compression. The Generate fill loop
  introduced earlier as a band-aid is gone; one `Generate(want_frames)`
  per callback is now exactly enough.

  Verified against `D:\Projects\Atari8bit\Altirra-4.40-src` (the MFC
  Altirra build that supplied the original `sa_pokey.dll` to RMT) -
  its `rmtinterface.cpp` is identical to AltirraSDL's for everything
  except the PokeyForge `SetAudioTap` / `SetMute` /
  `GetAnalysisAbiVersion` exports we added, so the contract is the
  same regardless of which Altirra source the DLL was built from. Tap
  and native (F12) modes now sound pitch- and time-identical.

### Documentation
- CHANGELOG note above documenting the bug, the diagnosis (mono
  vs stereo `sndn` semantics in the Altirra DLL), and the fix.

## [1.3.0] - 2026-06-02

### Added
- **JetBrains Mono font via SDL3_ttf.** Replaces SDL3's 8×8 debug-text
  glyphs with a properly hinted monospace font (ptsize=13). Sharper at
  every panel; the per-glyph width is unchanged so existing geometry
  still snaps to the same column grid. New runtime dependencies:
  `SDL3_ttf.dll` (1.9 MB) and `JetBrainsMono-Regular.ttf` (267 KB),
  both bundled by `release.ps1`.
- **Bank navigation loads the focused slot.** `Ctrl+Left/Right/Up/Down`
  on the bank steps the cursor and, when the new slot is occupied,
  loads that instrument into the editor panels - same effect as
  left-clicking the slot. Empty slots are still skipped over without
  touching what's currently loaded, so you can walk across gaps
  without losing an in-flight edit. Audition via `Ctrl+letter` is
  unchanged (no load). Tab cycling still just moves the cursor.
- **Mono/Stereo toggle in the volume-envelope popup.** Top-right corner
  of the big vol popup, mirrors the toggle on the Envelope panel
  header. Lets you flip the working instrument between mono and
  stereo without leaving the popup.
- **Stereo copy buttons in the vol popup.** `↑` / `↓` buttons between
  the VolR and VolL sections copy the entire envelope between
  channels in one undoable operation. Only visible in stereo mode.
- **Goto-column drag handle in the vol popup.** A draggable
  `Goto: N` strip at the bottom of the popup sets `PAR_ENV_GOTO`
  live - drop the thumb to commit one undo entry for the whole drag.

### Changed
- **Mono is the default load mode.** New / loaded / bank-slot
  instruments come up in mono regardless of the stored ATA encoding;
  use the `Mono` / `Stereo` toggle to switch when needed. Previously
  the encoding bit drove the mode, which surprised users who didn't
  realise they'd loaded a stereo instrument.
- **Bidirectional VolL ↔ VolR mirror in mono.** Any change to a
  volume cell in mono mode now propagates to the other channel,
  regardless of how the change arrived: hex digit typing (`0-9 A-F`),
  `+` / `-` nudges, `Shift+Up`/`Down`, scroll-wheel nudges, envelope
  grid drag-paint, or vol-popup drag-paint. Previously only the vol
  popup's drag-paint mirrored VolL → VolR, so editing VolR cells via
  the keyboard left the two channels out of sync.
- **Mono/Stereo toggle labels** now read `Mono` / `Stereo` (were `MN`
  / `ST`). Wider button (`GlyphW × 6 + 8 px`) accommodates the full
  word; the hit-test and mini-vol-strip extents follow the new width.
- **Vol popup shows both VolR and VolL sections in mono.** Previously
  mono mode collapsed the popup to one section; both are now drawn
  regardless of stereo state so the popup geometry stays stable
  through a Mono/Stereo flip.
- **Envelope header layout.** The `[EDITING - Tab to switch]` hint
  used to live next to the section title, on the left side, where
  its width overlapped the mini-volume graph. Moved next to the
  Mono/Stereo toggle on the right; the title stays a short
  `Envelope` and the underline always spans the full header width
  whether or not the panel is being edited.
- **Mini vol graph clipping.** Long envelopes used to draw vol bars
  on top of the toggle button (and now the EDITING hint). The strip
  now clips at the right-hand controls' left edge - baseline, bars,
  and the goto marker all stop short.

### Fixed
- **Volume popup hit detection** for stereo mode (popup stereo
  param, 16-zone value map, mono Y-offset reconciled with the
  always-both-sections layout from session 3).
- **Envelope panel section title underline** is back at the full
  header width when editing - previously the underline was
  shortened (because the title temporarily included the editing
  hint) and visually disconnected from the rest of the header.

## [1.2.0] - 2026-06-01

### Added

**Audio-feature extraction is back.** The v1.1 "audio features disabled"
known limitation is solved. A patched `sa_pokey.dll` now exposes the
Altirra audio mixer's internal float-sample stream so PokeyForge can
fingerprint instruments at the engine's native ~64 kHz mix rate without
going through `Pokey_Process`'s memset-to-silence buffer. The full
12,791-instrument bundled library re-analyses in ~9 seconds on a desktop
CPU — over 250× realtime.

- **`Pokey_SetAudioTap` / `Pokey_SetMute` / `Pokey_GetAnalysisAbiVersion`**
  added to `sa_pokey.dll`. PokeyForge installs a tap pointing at the
  mixer's internal stream; the existing RMT plugin contract
  (`Pokey_Process`, `Pokey_SoundInit`, `Pokey_PutByte`, etc.) is
  unchanged so the DLL still works as a drop-in for vanilla RMT.
- **Vendored AltirraSDL source under `extras/sa_pokey-src/`.** ~3 MB
  of the minimal upstream slice (`AltirraRMTPOKEY`, `ATAudio`, `ATCore`,
  `system`, public headers, build .props), plus a one-button
  `build.ps1` that rebuilds the patched DLL from a clean checkout and
  drops it into `runtime/`. The full upstream patch is also kept as
  `patches/rmtinterface.cpp.patch` for audit. Build outputs are
  `.gitignore`d. See `extras/sa_pokey-src/README.md` for license and
  refresh notes.
- **Smoke-test CLI** — `PokeyForge.exe --smoke-tap <out.raw>` drives
  the engine through a known square-wave configuration and dumps the
  captured float samples to disk. Used to verify the tap is wired
  correctly without needing the GUI.
- **Audio path rewritten.** Playback now drains the audio tap into
  PokeyForge's SDL3 output stream as **explicit stereo** (mono samples
  duplicated to both channels), and **mutes the DLL's own native audio
  device**. Eliminates the only-on-one-speaker regression some users
  saw with the new DLL and gives PokeyForge full control of the audio
  output.
- **Cluster view UX overhaul.**
  - Cluster headers start **collapsed** (previously expanded). Click
    a header to toggle just that cluster; the cluster containing the
    current selection auto-expands.
  - **Hover-tooltip on truncated cluster headers** — full label
    wraps to two lines inside the tree pane.
  - **Generated cluster names.** Numbered clusters now carry a
    descriptive suffix derived from member files:
    `Cluster 3 - Bass + Pad (dark, sustained) (24)`. Names are
    computed at view-build time from majority categories and the
    same adjective vocabulary as the per-file tags (bright / dark /
    loud / quiet / percussive / sustained / animated / noisy).
- **`k_override` persisted in `analysis.json`.** The k-means cluster
  count you set with `Ctrl+]` / `Ctrl+[` now survives across launches.
  Default bumped from `0` (auto, capped at 12) to **`24`**, which
  produces useful sub-groups on multi-thousand-instrument libraries.
- **6-page F1 help** (was 4 in v1.1). Pages: Keybindings, Categories &
  analysis, Search syntax, **Clusters** (expanded with cluster naming
  + adjective table), **Spectral signature panel** (now a dedicated
  page with typical-signature shapes for Kick / HiHat / Pad / Bass /
  Sweep / Bell), **Editor** (new — covers F6 edit mode in depth:
  panels, cursor movement, hex compose, audition while editing,
  Ctrl+Z/Y/S semantics). **Mouse wheel** now pages through the help
  when the cursor is over the panel.
- **Bank slot right-click menu** gains an **Analyse** action and an
  always-visible cluster-info section at the bottom:
  - **Analyse** renders the slot's current ATA through the engine,
    classifies it against the library's existing cluster centroids,
    and shows `Cluster N - <label>` + `Members: X` inline (menu
    stays open).
  - Empty / never-analysed slots show **None**. Result is cached
    per-slot and keyed on ATA hash, so subsequent menu opens are
    instant — unless the slot was edited / imported / pasted, in
    which case the cache auto-invalidates back to None.
- **Bank-wide Analyse button** on the bank title row (left of `EDIT`).
  One click runs the per-slot analysis loop across every used slot;
  notice bar reports `Bank Analyse: N / M slots clustered` when done.
- **Cluster fingerprints persist with the bank.** `Bank::Slot` now
  carries `cluster_info` + `cluster_hash` fields. `manifest.txt`
  gains two trailing TSV columns (`cluster_info`, `cluster_hash`);
  newlines in `cluster_info` are escaped as `\n` so the format stays
  one-line-per-slot. Both columns are optional - bank folders
  written by older PokeyForge or never analysed simply omit them.
  Reload preserves cluster info even across PokeyForge restarts.
- **One-popup-at-a-time + ESC closes popups.** Opening the bank menu
  while the tree menu is open closes the tree menu first (and vice
  versa). ESC dismisses any open right-click popup before falling
  through to the editor's exit / playback silence.
- **Bank Ctrl shortcuts gated by Bank EDIT mode.** `Ctrl+C/X/V`
  (already gated in v1.1) and now `Ctrl+Y` / `Ctrl+S` only fire when
  the bank EDIT toggle is on. Outside EDIT mode they fall through to
  `Ctrl+letter` audition so you can no longer accidentally trigger
  Export / Redo when you meant to play the `S` or `Y` chromatic note.
  `Ctrl+Z` (Undo) stays universal.

### Changed
- **Classifier bumped to v7.** Analysis cache version is `7`; existing
  v6 caches auto-regenerate on load. v6 caches' all-zero audio rows
  are replaced with real spectral data; tags `bright`, `dark`, `loud`,
  `quiet`, `animated` become populated; k-means clustering produces
  real groups instead of dumping every file into `(unclustered)`.
- **Pokey emulator mix rate documented** as ~63920 Hz NTSC / 63337 Hz
  PAL (`cps / 28`) — this is the rate audio features are computed at,
  not 44.1 kHz as in the v1.1 code (which never worked).
- **Bank EDIT toggle notice** now reads
  `Ctrl+C/X/V/Y/S edit, Ctrl+key still moves cursor` so the wider
  shortcut gate is discoverable from the UI.

### Fixed
- **Cluster view rows can be individually toggled.** v1.1 had the
  collapse-all routine but no per-header click dispatcher in the
  Cluster view — clicking a header did nothing. Routed properly now.

### Documentation
- New `extras/sa_pokey-src/README.md` covering rebuild, license
  (GPLv2 + RMT exception for the Altirra plugins, zlib for `system/`),
  and how to refresh against a newer upstream.
- Readme's cluster section now documents the `k_override` default of
  24, the persistence in `analysis.json`, the descriptive cluster
  labels, and the hover-tooltip behaviour.
- Removed the v1.1 "Known limitations: audio features disabled"
  section; everything described there now works.

## [1.1.0] - 2026-05-31

### Added
- **Directory scrollbar** on the right edge of the tree pane. Drag the thumb
  to free-scroll; click the track above/below the thumb to page by ~one
  screen. Selection auto-snap only kicks in when the selected node actually
  changes, so dragging doesn't fight you.
- **Auto-expand selection's parent.** Stepping past the bottom (or top) of a
  category in *By Category* view — or across a collapsed folder boundary in
  *Folders* view — now opens the receiving group so the highlighted
  instrument is always visible.
- **Bank slot right-click context menu** with hover highlighting and four
  actions:
  - **New** — fresh blank instrument in the slot, loaded as current, edit
    cursor parked on the name field.
  - **Clear** — empty the slot (with confirm). Clears the engine's
    instrument page so a stray key press can't replay the stale POKEY data.
  - **Export RTI** — write the slot's stored ATA blob to a `.rti` file.
  - **Import RTI** — load a `.rti` from disk straight into the slot (with
    confirm if it was occupied).
  - Clear and Export are greyed out on empty slots and become true no-ops
    when clicked.
- **Right-click on a directory entry** adds that instrument to the bank,
  without disturbing the currently-loaded instrument or any in-flight edits.
- **ATA-blob deduplication on add.** Both `+` and right-click compare the
  instrument's sound info (ATA bytes, ignoring name/source path) against
  every used slot. A match moves the bank cursor to the existing slot
  instead of creating a duplicate.
- **First-launch analysis is automatic.** When a library has no cached
  `analysis.json` (first time you open it, or after deleting the cache),
  PokeyForge now runs analysis at startup, on F4 (Switch Library), and on
  drag-drop opens, with a "*Analysing instruments...*" splash so the wait
  is explained. Subsequent launches read the cached file as before. F7
  ('Analyse') is now an explicit re-run.
- **F1 help is now paged.** Four pages — Keybindings, Categories &
  analysis, Search syntax (with worked `@tag` examples), Clusters &
  spectral view. **Left / Right** cycles pages; **Esc / F1** closes.
- **Analysis progress counter.** The "Analysing instruments" splash now
  reports the current "N / M (%)" file count instead of staying static,
  and re-pumps the OS event queue so the window doesn't appear frozen
  on large libraries.
- **Instrument header now shows analysis metadata.** A second text row
  in the panel header lists the effective category, confidence,
  manual-override flag, and parameter-derived sub-tags for the
  currently-loaded file. Hidden when there's no analysis data yet.
- **Search by tag** via the existing search bar. Type `@vibrato` to
  filter to instruments tagged vibrato; combine with commas
  (`@vibrato,highfreq`) for an AND match. Plain queries still do
  case-insensitive substring matching on filenames.
- **CSV export** alongside `analysis.json`. Every analysis run also
  writes `analysis_report.csv` with one row per file - importable into
  Excel / pandas for offline library slicing.
- **Tree right-click menu now offers "Override category..."** which
  opens a category-picker popup listing every category (colour-coded)
  plus a "Clear override (auto)" row. Faster than the Ctrl+R cycle for
  jumping to a specific category. The tree menu also gains a "Clear
  override" row when the file already has one.
- **Library-wide clear** of all manual overrides via `Ctrl+Shift+R`.
- **`analysis.json` is now fully portable.** The previous absolute
  `"library"` field is gone; the file only stores instrument paths
  relative to its own location. You can move (or rename, or zip up)
  an instrument folder and its cached analysis travels with it - no
  re-analysis required.
- **Analysis cache is versioned.** `analysis.json` files now carry an
  integer `"version"` field that matches `Analysis::kAnalysisVersion` in
  the code. On launch, libraries with a missing or mismatched version are
  auto-re-analysed against the current classifier - so bumping the
  version in the source is enough to invalidate every user's cache.
- **Classifier v4 - 16-bucket category set and richer signals.** Added
  **Chord** (4+ distinct note-table values), **Glide** (monotonic
  note-table drift / portamento), and refined **Arp** to "trill / short
  pattern" (2-3 distinct values). Falling note-table pitch strengthens
  Kick confidence. Per-file **confidence score** records how many
  signals voted for the winner (0-5); low-confidence rows are dimmed
  in the tree.
- **Sub-tags as orthogonal labels.** Per-file `tags` bitmask captures
  cross-cutting descriptors that don't fit "one category per
  instrument": `vibrato`, `highfreq`, `ascending`, `descending`.
  Stored as a comma-separated string in `analysis.json`.
- **Clusters view-tab** (or press **F10**) — groups the tree by k-means
  cluster id alongside the existing Folder / Category views. In this
  release the audio features that feed clustering are disabled (see
  Known limitations) so every file lands in the "(unclustered)"
  group; the view-tab itself is in place for future re-enable.
- **Per-category colour coding** in the directory tree. Each row is
  tinted by its effective category (manual override beats automatic);
  low-confidence rows dim toward the panel background so misfiles
  stand out.
- **Manual category override.** **Ctrl+R** on a directory row cycles
  the current file's category through every value (and back to "use
  auto"); the override is shown with an "M" marker in the tree and
  persists across rebuilds via the new `manual` field in
  `analysis.json`.
- **Post-analysis report.** F7 and the auto-run path now print a
  per-category breakdown to stdout (also surfaced via the notice bar)
  - useful for spotting libraries where the classifier is dumping
  too many files into Other.
- **Expanded analysis classifier (v2).** Single Percussion bucket is now
  split into **Kick / Snare / HiHat / Perc**; **Arp / Chord** (note-table
  variance), **Bell** (POKEY 15 kHz / 1.79 MHz high-frequency tonal),
  **Swept FX** (filter envelope movement), and **Lead (vibrato)**
  (PAR_VIBRATO > 0) are new top-level buckets. Decision tree now reads 11
  signals instead of 5 (added: distortion transient, AUDCTL high-freq
  bits, note-table variation, vibrato, filter-envelope sweep, filename
  hints). Filename tokens (`kick`, `snare`, `bd`, `hat`, `bass`, `lead`,
  `pad`, `arp`, `bell`, `fx`, ...) act as a tie-breaker when the
  parameter heuristics return Other. Existing `analysis.json` caches
  using the old "Percussion" name are still loadable — files mapped to a
  category name that no longer exists simply revert to "(unanalysed)"
  until you re-run F7.
- **Pre-picker splash.** When startup has no remembered library and no
  command-line argument, a "*Looking for instrument folder - please choose
  one...*" overlay is shown behind the native folder picker.
- **Mouse wheel nudges the focused edit field by ±1** when in Edit mode and
  the cursor is outside the directory pane. Inside the directory pane the
  wheel still steps the instrument selection (3 per notch).
- **Hover highlighting** on the bank context menu, the Yes/No confirm
  dialog, and the unsaved-edits Save/Discard/Cancel prompt.
- **Bank-slot context menu (UI)** — directory-pane tab buttons reorganised:
  Folders is a tall left-anchored button, *By Category* is a wider
  centre-anchored button, and *All* / *No dupes* are right-aligned so the
  rightmost button's edge sits flush with the panel border. All button
  labels are now centred inside their buttons; directory tree row text is
  vertically centred inside each row.
- **Bank panel widened** so the 8 cells per row consume the full panel
  width minus 4 px on each side; the click hit-test follows the new
  geometry.
- **Bank slot label** stays on one line and gets truncated with a trailing
  `~` if it doesn't fit (was previously wrapping onto a second line).

### Changed
- **Driver location lookup.** `LocateDriverObx` now prefers
  `SDL_GetBasePath()` (the directory containing `PokeyForge.exe`) over the
  current working directory, so launching from the Start menu, a shortcut,
  or VS with a non-default working directory still finds the bundled
  `rmt_driver_v2.obx`.
- **Switching instruments fully resets playback.** `LoadCurrent` and
  `LoadBankSlot` now silence the engine, drop the last-played note marker,
  and exit Edit mode before swapping in the new instrument.
- **Stack footprint reduced.** `Atari::m_memory` (64 KB) and
  `Audio::m_scratch` (16 KB) moved off the stack onto the heap, eliminating
  the C6262 "main uses ~94 KB of stack" warning.
- **`Bank::IndexOfAta`** added for sound-info deduplication; the
  `IndexOfPath` add-side check was removed because `IndexOfAta` subsumes it.

### Fixed
- **Bank slot label clipping** — the per-cell label now respects the cell
  width and never spills onto a second row.
- **Right-click previously toggled binary fields only.** It now also
  handles bank-slot menus and directory-row "add to bank" before falling
  back to the field-toggle behaviour.

### Documentation
- Readme: documented the **Analyse** categoriser's decision tree
  (channel-join → noise+envelope-length → fast-fade → loop+sustain →
  pure-tone), the right-click bank menu, the directory scrollbar, ATA-blob
  dedup, and the new mouse behaviours.
- Significant inline comment additions across `Directory`, `Bank`, `Gui`,
  and `main.cpp` to explain non-obvious behaviour (auto-expand, dedup,
  context menu geometry, scrollbar drag, etc.).

### Known limitations
- **Audio-rendered features are disabled in this release.** Categories,
  confidence, parameter-derived tags (`vibrato`, `highfreq`, `ascending`,
  `descending`), manual overrides, and search-by-tag all work; the
  audio-feature numbers (RMS profile, ZCR, spectral centroid / rolloff
  / flux) and the audio-driven sub-tags (`bright`, `dark`, `loud`,
  `quiet`, `animated`) are computed as zero. Consequence: the **Spectral
  signature panel** in Cluster view shows the "(no audio analysis)"
  placeholder for every file, **k-means clustering** lumps every file
  into the "(unclustered)" group, and the instrument header doesn't
  show the `Cen / RMS / ZCR / Flux` readout. The classifier core (the
  v2/v4 parametric decision tree) is unaffected. Root cause: the
  Altirra POKEY DLL would not render audio while the analysis pass was
  in flight - every `Generate` call returned 0x80 silence regardless of
  thread, call size, or engine state. The FFT and feature helpers
  remain in `Analysis.cpp` so a future fix can wire them back without
  re-deriving anything. `kAnalysisVersion = 6` invalidates any v5
  caches that contain stale all-zero audio rows.

## [1.0.0] - 2026-05-28

Initial public release.

### Added

**Browse**
- Recursive folder scan of every `.RTI` under a library directory.
- Multiple list views: **Folders**, **Category**, **All**, and **No duplicates**.
- Type-to-search filter bar.
- Automatic categorisation (bass / lead / percussion / noise-FX / pad) and
  duplicate detection across the library.
- Drag-and-drop a folder or a single `.RTI` onto the window to load it.

**Audition**
- Chromatic playback across `a–z` / `0–9` keys with arrow-key / mouse-wheel
  navigation, PAL/NTSC clock toggle, and ±octave transposition.
- `Esc` silences without exiting.
- Live master oscilloscope showing channel-1 waveform.
- Sample any bank slot in place with `Ctrl` + an audition key.

**Inspect & edit**
- All 12 parameters, 8 AUDCTL flags, envelope columns, note table, and the
  instrument name editable in place on a single screen.
- Hex/decimal entry, `Shift+↑/↓` nudge, right-click bit toggles, with live
  re-audition of every change.
- Full undo / redo (`Ctrl+Z` / `Ctrl+Y`).
- Export an edited instrument as a new standalone `.RTI` (`Ctrl+S`).

**Bank**
- 64-slot bank with copy / cut / paste / reorder in EDIT mode.
- Save as a set of individual `.RTI` files **and** a single RMT-compatible
  `.RMT` module plus a text manifest.
- Bank-slot operations: `Ctrl+Ins` (set), `Ctrl+Del` (clear),
  `Ctrl+a-z/0-9` (sample).

**Session**
- Last library, last bank, and last instrument persisted in
  `pokeyforge.json` next to the exe.

**Engine**
- Plays through an emulated 6502 + Altirra-grade POKEY core running the RMT
  tracker driver, so output matches Atari hardware and RMT.

**Polish**
- App icon, splash screen, in-app About panel, F1 keybindings overlay, and
  friendly error dialogs for fatal startup failures.

[1.1.0]: https://github.com/REPLACE_OWNER/PokeyForge/releases/tag/v1.1.0
[1.0.0]: https://github.com/REPLACE_OWNER/PokeyForge/releases/tag/v1.0.0
