#include "Keyboard.h"

namespace Keyboard {

int NoteOffset(SDL_Keycode key)
{
    if (key >= SDLK_A && key <= SDLK_Z) {
        return (int)(key - SDLK_A);          // 0..25
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        return 26 + (int)(key - SDLK_0);     // 26..35
    }
    return -1;
}

} // namespace Keyboard
