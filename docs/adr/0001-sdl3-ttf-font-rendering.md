# ADR-0001: Replace SDL_RenderDebugText with SDL3_ttf + JetBrains Mono

**Date:** 2026-06-02  
**Status:** Accepted  
**Deciders:** Peter (owner)

---

## Context

PokeyForge renders all text via `SDL_RenderDebugText` — an 8×8 fixed-width ASCII-only debug font built into SDL3. It requires zero additional dependencies and no font file, but it:

- Cannot render Unicode (no arrows, symbols, or box-drawing characters)
- Is fixed at 8×8px and cannot scale cleanly with the 1280×720 logical canvas
- Looks visibly crude relative to modern tool UIs
- Blocks adding icon buttons (undo ↩, redo ↪, revert ↺) that require Unicode glyphs

The trigger for this decision was the addition of Undo/Redo/Revert icon buttons to the instrument panel header. These buttons need recognisable Unicode arrow glyphs. Once SDL3_ttf is a dependency for those buttons, migrating all remaining text calls costs little extra and yields a substantially better UI.

---

## Decision

**Replace all `SDL_RenderDebugText` calls with SDL3_ttf text rendering using JetBrains Mono as the bundled font.**

- **Library:** SDL3_ttf (the official SDL3 TTF extension, same vendor and release cadence as SDL3)
- **Font:** JetBrains Mono — monospaced, OFL-licensed, excellent Unicode symbol coverage, visually appropriate for a music tracker tool
- **Migration strategy:** Icons first (validates the integration), then all remaining text calls

---

## Alternatives Considered

| Option | Why Rejected |
|--------|-------------|
| Use SDL3_ttf only for icon buttons, keep debug font for all other text | Two parallel rendering paths; debug font remains a visual quality ceiling; marginal cost of full migration is low once the dependency is added |
| Use a different Unicode font (e.g. Hack, Fira Mono, Iosevka) | JetBrains Mono is the most widely known developer monospace font, OFL-licensed, ships with good symbol glyphs; other choices are equivalent but less familiar |
| Embed a custom pixel-font bitmap atlas | Eliminates the TTF dependency but requires hand-authoring Unicode coverage; not worthwhile when SDL3_ttf is already maintained and well-tested |
| Use FreeType directly | SDL3_ttf wraps FreeType already; using it directly adds complexity for no gain |

---

## Consequences

**Positive:**
- Full Unicode rendering throughout the app — arrows, symbols, box-drawing available everywhere
- Scalable text; font size can be tuned per-panel (e.g. larger for the name field, smaller for grid cells)
- Enables icon buttons with standard glyphs
- Substantially improves overall UI quality

**Negative / Risks:**
- New runtime dependency: `SDL3_ttf.dll` must be shipped alongside the executable
- Font file (`JetBrainsMono-Regular.ttf` and variants) must be bundled in `runtime/`
- All text-rendering call sites must be migrated — mechanical but non-trivial volume of changes
- Font metrics differ from 8×8 debug font; all hardcoded pixel layouts that assume 8px character width will need adjustment
- TTF rendering has a small startup cost (font loading + atlas generation) vs. zero-cost debug font

**Mitigations:**
- SDL3_ttf is the official SDL extension and follows the same versioning contract as SDL3 itself — low long-term maintenance risk
- JetBrains Mono is a stable, actively maintained open-source font
- Layout adjustments can be made incrementally alongside the text call migration
