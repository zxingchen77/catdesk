#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Original pixel components draw directly into a stride x 240 RGB565 buffer.
 * All coordinates and integer scales use the 400 x 240 logical art grid.
 * Colors are RGB24 values from pixel_theme.h. No alpha or antialiasing. */
void drawPixelLabel(uint16_t *pixels, int stride, int x, int y,
                    const char *text, unsigned rgb, int scale);

/* Both variants share the same 7 x 7 silhouette. The filled sprite has a
 * one-pixel deep outline and one highlight. The outlined sprite leaves its
 * interior and exterior untouched, so the room or paper stays visible. */
void drawPixelHeart(uint16_t *pixels, int stride, int x, int y,
                    int scale, unsigned rgb);
void drawPixelHeartOutline(uint16_t *pixels, int stride, int x, int y,
                           int scale, unsigned rgb);

/* Shared parchment RPG menu: stepped corners, six logical timber pixels,
 * a two-pixel opaque shadow, heading and close glyph. Header/close positions
 * match the existing popup hit areas; drawing never changes input behavior. */
void drawPixelWindow(uint16_t *pixels, int stride, int x, int y,
                     int width, int height, const char *title);

enum {
    PIXEL_BUTTON_NORMAL = 0,
    PIXEL_BUTTON_PRESSED = 1,
    PIXEL_BUTTON_SELECTED = 2
};

void drawPixelButton(uint16_t *pixels, int stride, int x, int y,
                     int width, int height, const char *label, unsigned flags);
void drawPixelCheckbox(uint16_t *pixels, int stride, int x, int y,
                       int size, bool checked);
void drawPixelSlider(uint16_t *pixels, int stride, int x, int y,
                     int width, int height, int value, int minimum, int maximum);
void drawPixelSun(uint16_t *pixels, int stride, int x, int y, int scale);
/* x/y anchor the top of numeric ink, rather than the font's text cell. */
void drawPixelNumber(uint16_t *pixels, int stride, int x, int y,
                     const char *text, int scale, unsigned rgb, bool shadow);

#ifdef __cplusplus
}
#endif
