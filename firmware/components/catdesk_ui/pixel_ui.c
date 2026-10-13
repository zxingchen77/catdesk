#include "pixel_ui.h"
#include "pixel_theme.h"
#include "catdesk_font.h"

#include <stddef.h>
#include <limits.h>

enum { PIXEL_BUFFER_HEIGHT = 240 };

static uint16_t pixel_color(unsigned rgb)
{
    return (uint16_t)((((rgb >> 19) & 31u) << 11)
        | (((rgb >> 10) & 63u) << 5) | ((rgb >> 3) & 31u));
}

static void pixel_block(uint16_t *pixels, int stride, int64_t x, int64_t y,
                        int width, int height, uint16_t color)
{
    if (pixels == NULL || stride <= 0 || width <= 0 || height <= 0) return;
    int64_t right = x + width;
    int64_t bottom = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > stride) right = stride;
    if (bottom > PIXEL_BUFFER_HEIGHT) bottom = PIXEL_BUFFER_HEIGHT;
    if (x >= right || y >= bottom) return;
    for (int row = (int)y; row < bottom; ++row) {
        for (int col = (int)x; col < right; ++col) {
            pixels[(size_t)row * (size_t)stride + (size_t)col] = color;
        }
    }
}

/* 0: transparent, 1: edge, 2: body, 3: single highlight. Keeping the edge
 * explicit makes the tiny outlined crest identical to the filled sprite. */
static const char heart_pixels[7][8] = {
    "0110110",
    "1221221",
    "1232221",
    "1222221",
    "0122210",
    "0012100",
    "0001000"
};

void drawPixelLabel(uint16_t *pixels, int stride, int x, int y,
                    const char *text, unsigned rgb, int scale)
{
    cd_text(pixels, stride, x, y, text, pixel_color(rgb), scale);
}

void drawPixelHeart(uint16_t *pixels, int stride, int x, int y,
                    int scale, unsigned rgb)
{
    if (pixels == NULL || stride <= 0 || scale <= 0) return;
    const uint16_t colors[] = {
        0, pixel_color(PT_OUTLINE), pixel_color(rgb), pixel_color(PT_ROSE_LIGHT)
    };
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 7; ++col) {
            const unsigned ink = (unsigned)(heart_pixels[row][col] - '0');
            if (ink != 0) pixel_block(pixels, stride, (int64_t)x + (int64_t)col * scale,
                (int64_t)y + (int64_t)row * scale, scale, scale, colors[ink]);
        }
    }
}

void drawPixelHeartOutline(uint16_t *pixels, int stride, int x, int y,
                           int scale, unsigned rgb)
{
    if (pixels == NULL || stride <= 0 || scale <= 0) return;
    const uint16_t stroke = pixel_color(rgb);
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 7; ++col) {
            if (heart_pixels[row][col] == '1') {
                pixel_block(pixels, stride, (int64_t)x + (int64_t)col * scale,
                    (int64_t)y + (int64_t)row * scale, scale, scale, stroke);
            }
        }
    }
}

static void pixel_stepped_fill(uint16_t *pixels, int stride, int64_t x, int64_t y,
                               int width, int height, int corner, unsigned rgb)
{
    if (pixels == NULL || stride <= 0 || width <= 0 || height <= 0) return;
    if (y >= PIXEL_BUFFER_HEIGHT || y + height <= 0 || x >= stride || x + width <= 0) return;
    const int64_t first_row = y < 0 ? -y : 0;
    const int64_t last_row = y + height > PIXEL_BUFFER_HEIGHT ? PIXEL_BUFFER_HEIGHT - y : height;
    const uint16_t color = pixel_color(rgb);
    for (int64_t row = first_row; row < last_row; ++row) {
        const int64_t bottom_distance = (int64_t)height - 1 - row;
        const int64_t edge_distance = row < bottom_distance ? row : bottom_distance;
        const int inset = edge_distance < corner ? corner - (int)edge_distance : 0;
        pixel_block(pixels, stride, x + inset, y + row, width - inset * 2, 1, color);
    }
}

void drawPixelWindow(uint16_t *pixels, int stride, int x, int y,
                     int width, int height, const char *title)
{
    if (pixels == NULL || stride <= 0 || width < 38 || height < 35) return;
    const int64_t left = x, top = y;
    if (left >= stride || top >= PIXEL_BUFFER_HEIGHT
        || left + width + PT_WINDOW_SHADOW <= 0 || top + height + PT_WINDOW_SHADOW <= 0) return;
    pixel_stepped_fill(pixels, stride, left + PT_WINDOW_SHADOW, top + PT_WINDOW_SHADOW,
        width, height, 3, PT_WOOD_DARK);
    /* Six-pixel timber construction: deep edge, broad wood, bevel, paper rim. */
    pixel_stepped_fill(pixels, stride, left, top, width, height, 3, PT_OUTLINE);
    pixel_stepped_fill(pixels, stride, left + 2, top + 2, width - 4, height - 4, 1, PT_WOOD);
    pixel_stepped_fill(pixels, stride, left + 4, top + 4, width - 8, height - 8, 1, PT_WOOD_LIGHT);
    pixel_block(pixels, stride, left + 5, top + 5, width - 10, height - 10, pixel_color(PT_PAPER_DARK));
    pixel_block(pixels, stride, left + 6, top + 6, width - 12, height - 12, pixel_color(PT_PAPER));
    pixel_block(pixels, stride, left + 7, top + 2, width - 14, 1, pixel_color(PT_WOOD_LIGHT));
    pixel_block(pixels, stride, left + 2, top + 7, 1, height - 14, pixel_color(PT_WOOD_DARK));
    pixel_block(pixels, stride, left + width - 4, top + 7, 2, height - 14, pixel_color(PT_WOOD_DARK));
    pixel_block(pixels, stride, left + 7, top + height - 4, width - 14, 2, pixel_color(PT_WOOD_DARK));

    /* Short broken grain belongs to the boards, rather than decorating a flat
       outline. Iterate only visible fragments for large clipped windows. */
    int64_t grain_x = left + 10;
    if (grain_x < 0) grain_x += ((-grain_x + 22) / 23) * 23;
    const int64_t grain_end = left + width - 9 < stride ? left + width - 9 : stride;
    for (; grain_x < grain_end; grain_x += 23) {
        pixel_block(pixels, stride, grain_x, top + 3, 6, 1, pixel_color(PT_WOOD_DARK));
        pixel_block(pixels, stride, grain_x + 3, top + height - 3, 5, 1, pixel_color(PT_WOOD));
    }
    int64_t grain_y = top + 11;
    if (grain_y < 0) grain_y += ((-grain_y + 16) / 17) * 17;
    const int64_t grain_bottom = top + height - 9 < PIXEL_BUFFER_HEIGHT ? top + height - 9 : PIXEL_BUFFER_HEIGHT;
    for (; grain_y < grain_bottom; grain_y += 17) {
        pixel_block(pixels, stride, left + 3, grain_y, 1, 5, pixel_color(PT_WOOD_DARK));
        pixel_block(pixels, stride, left + width - 3, grain_y + 3, 1, 4, pixel_color(PT_WOOD));
    }

    /* Sparse paper fibres use only two discrete parchment shades. */
    int64_t fibre_y = top + 10;
    if (fibre_y < 0) fibre_y += ((-fibre_y + 18) / 19) * 19;
    const int64_t fibre_bottom = top + height - 8 < PIXEL_BUFFER_HEIGHT ? top + height - 8 : PIXEL_BUFFER_HEIGHT;
    for (; fibre_y < fibre_bottom; fibre_y += 19) {
        int64_t fibre_x = left + 11;
        if (fibre_x < 0) fibre_x += ((-fibre_x + 28) / 29) * 29;
        const int64_t fibre_end = left + width - 10 < stride ? left + width - 10 : stride;
        for (; fibre_x < fibre_end; fibre_x += 29) {
            const int64_t seed = (fibre_x - left) / 29 + (fibre_y - top) / 19 * 3;
            if (seed % 4 == 0) pixel_block(pixels, stride, fibre_x, fibre_y, 1, 1, pixel_color(PT_PAPER_DARK));
            if (seed % 5 == 1) pixel_block(pixels, stride, fibre_x + 2, fibre_y + 1, 2, 1, pixel_color(PT_PAPER_LIGHT));
        }
    }

    /* An engraved parchment heading, without a solid operating-system bar. */
    bool ascii_title = title != NULL && *title != '\0';
    if (title != NULL) {
        for (const unsigned char *letter = (const unsigned char *)title; *letter != '\0'; ++letter) {
            if (*letter >= 128) ascii_title = false;
        }
    }
    drawPixelLabel(pixels, stride, x + 13, y + (ascii_title ? 9 : 11), title,
        PT_PAPER_DARK, ascii_title ? 2 : PT_FONT_TITLE);
    drawPixelLabel(pixels, stride, x + 12, y + (ascii_title ? 8 : 10), title,
        PT_TEXT_DARK, ascii_title ? 2 : PT_FONT_TITLE);
    pixel_block(pixels, stride, left + 11, top + 26, width - 22, 1, pixel_color(PT_PAPER_DARK));
    pixel_block(pixels, stride, left + 13, top + 27, width - 26, 1, pixel_color(PT_PAPER_LIGHT));

    /* Four inset brass rivets fasten the visibly separate wooden boards. */
    const int64_t nail_x[] = {left + 3, left + width - 6};
    const int64_t nail_y[] = {top + 3, top + height - 6};
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            pixel_block(pixels, stride, nail_x[i], nail_y[j], 3, 3, pixel_color(PT_OUTLINE));
            pixel_block(pixels, stride, nail_x[i] + 1, nail_y[j] + 1, 1, 1, pixel_color(PT_GOLD));
            pixel_block(pixels, stride, nail_x[i] + 1, nail_y[j], 1, 1, pixel_color(PT_PAPER_LIGHT));
        }
    }

    /* The established 15 x 15 close target stays at x+w-24, y+8. */
    const int64_t close_x = left + width - 24, close_y = top + 8;
    if (close_x >= INT_MIN && close_x <= INT_MAX && close_y >= INT_MIN && close_y <= INT_MAX)
        drawPixelButton(pixels, stride, (int)close_x, (int)close_y, 15, 15, "X", PIXEL_BUTTON_NORMAL);
}

void drawPixelButton(uint16_t *pixels, int stride, int x, int y,
                     int width, int height, const char *label, unsigned flags)
{
    if (pixels == NULL || stride <= 0 || width < 8 || height < 8) return;
    const int64_t left = x, top = y;
    if (left >= stride || top >= PIXEL_BUFFER_HEIGHT
        || left + width + 2 <= 0 || top + height + 2 <= 0) return;
    const bool pressed = (flags & PIXEL_BUTTON_PRESSED) != 0;
    const int64_t face_y = top + (pressed ? 1 : 0);
    const unsigned edge = (flags & PIXEL_BUTTON_SELECTED) ? PT_PAPER_LIGHT : PT_OUTLINE;
    pixel_stepped_fill(pixels, stride, left + 2, top + 2, width, height, 1, PT_WOOD_DARK);
    pixel_stepped_fill(pixels, stride, left, face_y, width, height, 1, edge);
    pixel_stepped_fill(pixels, stride, left + 1, face_y + 1, width - 2, height - 2, 0,
        pressed ? PT_WOOD_DARK : PT_WOOD);
    pixel_block(pixels, stride, left + 3, face_y + 2, width - 6, 1,
        pixel_color(pressed ? PT_WOOD : PT_WOOD_LIGHT));
    pixel_block(pixels, stride, left + 2, face_y + 3, 1, height - 6,
        pixel_color(pressed ? PT_WOOD : PT_WOOD_LIGHT));
    pixel_block(pixels, stride, left + 3, face_y + height - 2, width - 6, 1, pixel_color(PT_WOOD_DARK));
    pixel_block(pixels, stride, left + width - 3, face_y + 3, 1, height - 6, pixel_color(PT_WOOD_DARK));
    int64_t grain = left + 5;
    if (grain < 0) grain += ((-grain + 14) / 15) * 15;
    const int64_t grain_end = left + width - 5 < stride ? left + width - 5 : stride;
    for (; grain < grain_end; grain += 15) {
        pixel_block(pixels, stride, grain, face_y + height - 4, 5, 1,
            pixel_color(pressed ? PT_OUTLINE : PT_WOOD_DARK));
        pixel_block(pixels, stride, grain + 3, face_y + 4, 2, 1,
            pixel_color(pressed ? PT_WOOD : PT_WOOD_LIGHT));
    }
    if (label != NULL && height >= 14) {
        const int64_t text_x = left + width / 2 - cd_text_width(label, 1) / 2;
        const int64_t text_y = face_y + (height - 12) / 2;
        if (text_x >= INT_MIN && text_x < INT_MAX && text_y >= INT_MIN && text_y < INT_MAX) {
            drawPixelLabel(pixels, stride, (int)text_x + 1, (int)text_y + 1, label, PT_OUTLINE, 1);
            drawPixelLabel(pixels, stride, (int)text_x, (int)text_y, label, PT_TEXT_LIGHT, 1);
        }
    }
}

void drawPixelCheckbox(uint16_t *pixels, int stride, int x, int y,
                       int size, bool checked)
{
    if (pixels == NULL || stride <= 0 || size < 10) return;
    const int64_t left = x, top = y;
    pixel_block(pixels, stride, left, top, size, size, pixel_color(PT_OUTLINE));
    pixel_block(pixels, stride, left + 1, top + 1, size - 2, size - 2, pixel_color(PT_PAPER));
    pixel_block(pixels, stride, left + 1, top + 1, size - 2, 1, pixel_color(PT_PAPER_DARK));
    pixel_block(pixels, stride, left + 1, top + 2, 1, size - 3, pixel_color(PT_PAPER_DARK));
    pixel_block(pixels, stride, left + 2, top + size - 2, size - 3, 1, pixel_color(PT_PAPER_LIGHT));
    if (checked) {
        static const char check[8][9] = {
            "00000000", "00000001", "00000012", "11000123",
            "12301230", "01212300", "00123000", "00010000"
        };
        const uint16_t colors[] = {0, pixel_color(PT_GREEN_DARK), pixel_color(PT_GREEN), pixel_color(PT_GREEN_LIGHT)};
        const int unit = size / 10;
        const int64_t check_x = left + (size - unit * 8) / 2;
        const int64_t check_y = top + (size - unit * 8) / 2;
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                const unsigned ink = (unsigned)(check[row][col] - '0');
                if (ink != 0) pixel_block(pixels, stride,
                    check_x + (int64_t)col * unit, check_y + (int64_t)row * unit,
                    unit, unit, colors[ink]);
            }
        }
    }
}

void drawPixelSlider(uint16_t *pixels, int stride, int x, int y,
                     int width, int height, int value, int minimum, int maximum)
{
    if (pixels == NULL || stride <= 0 || width < 14 || height < 7) return;
    const int64_t left = x, top = y, center_y = top + height / 2;
    if (left >= stride || top >= PIXEL_BUFFER_HEIGHT || left + width <= 0 || top + height <= 0) return;
    const int64_t range = (int64_t)maximum - minimum;
    int64_t fraction = (int64_t)value - minimum;
    if (fraction < 0 || range <= 0) fraction = 0;
    if (range > 0 && fraction > range) fraction = range;
    const int64_t thumb_x = left + 3 + (range > 0 ? fraction * (width - 7) / range : 0);
    const int bar_width = width - 14;
    pixel_block(pixels, stride, left + 7, center_y - 3, bar_width, 7, pixel_color(PT_OUTLINE));
    pixel_block(pixels, stride, left + 8, center_y - 2, bar_width - 2, 5, pixel_color(PT_WOOD_DARK));
    pixel_block(pixels, stride, left + 9, center_y + 2, bar_width - 4, 1, pixel_color(PT_WOOD));
    const int64_t filled_end = left + 9 + (range > 0 ? fraction * (width - 18) / range : 0);
    /* Chunky independent slots, rather than a continuous modern progress bar. */
    int64_t slot = left + 9;
    if (slot < 0) slot += ((-slot + 8) / 9) * 9;
    const int64_t end = left + width - 9 < stride ? left + width - 9 : stride;
    for (; slot < end; slot += 9) {
        const bool full = slot + 2 < filled_end;
        const int slot_width = slot + 6 < left + width - 9 ? 6 : (int)(left + width - 9 - slot);
        pixel_block(pixels, stride, slot, center_y - 1, slot_width, 3, pixel_color(full ? PT_GOLD : PT_WOOD));
        pixel_block(pixels, stride, slot, center_y - 1, slot_width, 1, pixel_color(full ? PT_PAPER_LIGHT : PT_WOOD_LIGHT));
    }
    /* Decorative arrows share the unchanged endpoint mapping and hit area. */
    static const char arrow[5][4] = {"001", "011", "121", "011", "001"};
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            const unsigned ink = (unsigned)(arrow[row][col] - '0');
            if (ink != 0) {
                const uint16_t color = pixel_color(ink == 1 ? PT_OUTLINE : PT_GOLD);
                pixel_block(pixels, stride, left + col, center_y - 2 + row, 1, 1, color);
                pixel_block(pixels, stride, left + width - 1 - col, center_y - 2 + row, 1, 1, color);
            }
        }
    }
    const int knob_height = height >= 13 ? 11 : height - 2;
    const int64_t knob_y = center_y - knob_height / 2;
    pixel_stepped_fill(pixels, stride, thumb_x - 3, knob_y, 7, knob_height, 1, PT_OUTLINE);
    pixel_block(pixels, stride, thumb_x - 2, knob_y + 1, 5, knob_height - 2, pixel_color(PT_WOOD_LIGHT));
    pixel_block(pixels, stride, thumb_x - 2, knob_y + 2, 1, knob_height - 4, pixel_color(PT_PAPER_LIGHT));
    pixel_block(pixels, stride, thumb_x, center_y - 2, 1, 5, pixel_color(PT_WOOD_DARK));
    pixel_block(pixels, stride, thumb_x + 1, center_y - 2, 1, 1, pixel_color(PT_GOLD));
}

void drawPixelSun(uint16_t *pixels, int stride, int x, int y, int scale)
{
    if (pixels == NULL || stride <= 0 || scale <= 0) return;
    static const char sun[12][13] = {
        "000001100000", "000012210000", "010012210010", "021122221120",
        "012233332210", "112233332211", "122233322221", "012222222210",
        "021122221120", "010012210010", "000012210000", "000001100000"
    };
    const uint16_t colors[] = {0, pixel_color(PT_OUTLINE), pixel_color(PT_GOLD), pixel_color(PT_PAPER_LIGHT)};
    for (int row = 0; row < 12; ++row) {
        for (int col = 0; col < 12; ++col) {
            const unsigned ink = (unsigned)(sun[row][col] - '0');
            if (ink != 0) pixel_block(pixels, stride, (int64_t)x + (int64_t)col * scale,
                (int64_t)y + (int64_t)row * scale, scale, scale, colors[ink]);
        }
    }
}

void drawPixelNumber(uint16_t *pixels, int stride, int x, int y,
                     const char *text, int scale, unsigned rgb, bool shadow)
{
    if (pixels == NULL || stride <= 0 || scale <= 0 || text == NULL) return;
    const int64_t text_y = (int64_t)y - (int64_t)scale * 2;
    if (text_y < INT_MIN || text_y >= INT_MAX || x >= INT_MAX) return;
    if (shadow) drawPixelLabel(pixels, stride, x + 1, (int)text_y + 1, text, PT_PAPER_DARK, scale);
    drawPixelLabel(pixels, stride, x, (int)text_y, text, rgb, scale);
}
