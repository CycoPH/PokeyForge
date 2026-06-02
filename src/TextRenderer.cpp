#include "TextRenderer.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

bool TextRenderer::Init(SDL_Renderer* /*renderer*/, const char* font_path, int point_size)
{
    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return false;
    }

    m_font = TTF_OpenFont(font_path, (float)point_size);
    if (!m_font) {
        SDL_Log("TTF_OpenFont(%s, %d) failed: %s", font_path, point_size, SDL_GetError());
        TTF_Quit();
        return false;
    }

    // Measure the advance of a typical ASCII glyph; for a monospaced font
    // every glyph should have the same advance, so 'M' is representative.
    int minx = 0, maxx = 0, miny = 0, maxy = 0, advance = 0;
    TTF_GetGlyphMetrics(m_font, 'M', &minx, &maxx, &miny, &maxy, &advance);
    m_char_w = (advance > 0) ? advance : 8;
    m_line_h = TTF_GetFontHeight(m_font);
    if (m_line_h <= 0) m_line_h = 8;
    return true;
}

void TextRenderer::Quit()
{
    if (m_font) { TTF_CloseFont(m_font); m_font = nullptr; }
    TTF_Quit();
}

void TextRenderer::DrawText(SDL_Renderer* r, SDL_Color c, int x, int y, const char* s)
{
    if (!s || !*s) return;
    if (!m_font) {
        // Fallback to built-in 8×8 debug font.
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDebugText(r, (float)x, (float)y, s);
        return;
    }
    SDL_Surface* surf = TTF_RenderText_Blended(m_font, s, 0, c);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_DestroySurface(surf);
    if (!tex) return;
    float tw = 0.f, th = 0.f;
    SDL_GetTextureSize(tex, &tw, &th);
    SDL_FRect dst{ (float)x, (float)y, tw, th };
    SDL_RenderTexture(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

void TextRenderer::DrawTextF(SDL_Renderer* r, SDL_Color c, int x, int y,
                              const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    DrawText(r, c, x, y, buf);
}

int TextRenderer::MeasureWidth(const char* s) const
{
    if (!s || !*s) return 0;
    if (m_font) {
        int w = 0, h = 0;
        if (TTF_GetStringSize(m_font, s, 0, &w, &h) && w > 0)
            return w;
    }
    return (int)std::strlen(s) * m_char_w;
}
