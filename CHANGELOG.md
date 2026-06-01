# Changelog

All notable changes to **PokeyForge** are tracked in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

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
