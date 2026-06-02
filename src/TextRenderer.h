#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

// Thin wrapper around SDL3_ttf that loads a single monospaced font and
// exposes the same DrawText / DrawTextF surface used throughout Gui.cpp.
// The advance width of the chosen font at the chosen point-size is exposed
// as CharWidth() so layout code can replace the old constexpr kGlyphW=8.
//
// Lifecycle: call Init() once after SDL_Init; call Quit() before SDL_Quit.
// The renderer pointer passed to Init must outlive this object.

struct TextRenderer {
    // Initialise TTF and open the font.  Returns false and leaves the object
    // in a "use debug fallback" state if anything fails.
    bool Init(SDL_Renderer* renderer, const char* font_path, int point_size = 13);
    void Quit();

    // Draw text at logical-pixel position (x,y) in the given SDL_Color.
    void DrawText(SDL_Renderer* r, SDL_Color c, int x, int y, const char* s);

    // printf-style variant.
    void DrawTextF(SDL_Renderer* r, SDL_Color c, int x, int y, const char* fmt, ...);

    // Pixel width of the given string.  Uses cached glyph advance for speed.
    int MeasureWidth(const char* s) const;

    // Per-character advance (monospaced, all glyphs the same width).
    int CharWidth() const { return m_char_w; }

    // Total line height (ascender + descender + any internal leading).
    // Use this to vertically centre text inside a box: y = box_y + (box_h - LineHeight()) / 2
    int LineHeight() const { return m_line_h; }

    // True when TTF initialised successfully.
    bool Ok() const { return m_font != nullptr; }

private:
    TTF_Font*    m_font   = nullptr;
    int          m_char_w = 8;   // advance width at the chosen point-size
    int          m_line_h = 8;   // total line height at the chosen point-size
};
