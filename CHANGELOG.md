# Changelog

All notable changes to **PokeyForge** are tracked in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

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

[1.0.0]: https://github.com/REPLACE_OWNER/PokeyForge/releases/tag/v1.0.0
