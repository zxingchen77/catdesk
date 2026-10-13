#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bitmap text in logical pixels: Chinese 12x12, ASCII 5x7 centered in
 * the same 12-pixel cell. Advance: Chinese 13, ASCII 6; newline: 14.
 * Transparent rendering, integer nearest-neighbor scale. The framebuffer
 * has stride pixels per row and 240 rows. Clipped on all four sides.
 * Null text/buffer and nonpositive stride/scale are accepted as no-ops.
 */
void cd_text(uint16_t *buf, int stride, int x, int y,
             const char *utf8, uint16_t color, int scale);

/* Maximum rendered line advance, without the final one-pixel gap. */
int cd_text_width(const char *utf8, int scale);

/* Furniture-only micro font, original one-bit 3x5 ASCII. x/y are the ink
 * top-left, line height 6. Digits/letters advance4; '.'/':'/',' advance2.
 * Tiny lowercase is rendered as miniature capitals. Unsupported Unicode
 * becomes '?'. Same transparent rendering and four-side clipping as above.
 * A date such as "2024.10.07" is exactly35 pixels wide.
 */
void cd_tiny_text(uint16_t *buf, int stride, int x, int y,
                  const char *utf8, uint16_t color);
int cd_tiny_width(const char *utf8);

#ifdef __cplusplus
}
#endif
