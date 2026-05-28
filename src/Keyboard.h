#pragma once

#include <SDL3/SDL.h>

// Maps SDL keycodes to RMT semitone offsets for PokeyForge's chromatic
// audition ramp.  a..z = offsets 0..25, 0..9 = offsets 26..35.
// Returns -1 if the key isn't part of the ramp.
//
// The caller adds a base note (e.g. 12 ~= C2) and an octave-shift modifier
// before passing the result to RmtEngine::NoteOn.

namespace Keyboard {

// Base RMT note that the 'a' key maps to when octave_shift == 0.
// The RMT driver supports notes 0..60.
static constexpr int BASE_NOTE = 12;

// Number of keys in the chromatic ramp (a-z + 0-9).
static constexpr int RAMP_SIZE = 36;

int NoteOffset(SDL_Keycode key);

} // namespace Keyboard
