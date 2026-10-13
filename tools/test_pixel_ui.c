/* Host check: cc -std=c11 -Wall -Wextra -Werror -pedantic \
 *   -fsanitize=address,undefined -Ifirmware/components/catdesk_ui/include \
 *   tools/test_pixel_ui.c firmware/components/catdesk_ui/pixel_ui.c \
 *   firmware/components/catdesk_ui/catdesk_font.c -o /tmp/catdesk-test-pixel-ui
 * Optional first argument writes a component preview as a 400x240 PPM.
 */
#include "pixel_ui.h"
#include "pixel_theme.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 400, HEIGHT = 240, BACKGROUND = 0x1234 };
static uint16_t *pixels, *saved;

static uint16_t color(unsigned rgb)
{
    return (uint16_t)((((rgb >> 19) & 31) << 11) | (((rgb >> 10) & 63) << 5) | ((rgb >> 3) & 31));
}

static void clear(void)
{
    for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) pixels[i] = BACKGROUND;
}

static uint16_t at(int x, int y)
{
    assert(x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT);
    return pixels[y * WIDTH + x];
}

static void assert_bounds(int x1, int y1, int x2, int y2)
{
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            if (x < x1 || x > x2 || y < y1 || y > y2) assert(at(x, y) == BACKGROUND);
        }
    }
}

static void test_window(void)
{
    clear();
    drawPixelWindow(pixels, WIDTH, 80, 49, 240, 145, "我们在一起");
    assert(at(80, 49) == BACKGROUND && at(82, 49) == BACKGROUND);
    assert(at(83, 49) == color(PT_OUTLINE));
    assert(at(80, 100) == color(PT_OUTLINE) && at(81, 100) == color(PT_OUTLINE));
    assert(at(82, 100) == color(PT_WOOD_DARK) && at(83, 100) == color(PT_WOOD));
    assert(at(84, 100) == color(PT_WOOD_LIGHT) && at(85, 100) == color(PT_PAPER_DARK));
    assert(at(86, 100) == color(PT_PAPER));
    assert(at(320, 100) == color(PT_WOOD_DARK) && at(321, 100) == color(PT_WOOD_DARK));
    assert(at(322, 100) == BACKGROUND);
    assert(at(303, 57) == color(PT_OUTLINE)); /* Established close button position. */
    assert(at(84, 53) == color(PT_GOLD) && at(315, 53) == color(PT_GOLD));
    assert(at(84, 189) == color(PT_GOLD) && at(315, 189) == color(PT_GOLD));
    assert(at(90, 52) == color(PT_WOOD_DARK) && at(99, 52) == color(PT_WOOD));
    assert(at(83, 60) == color(PT_WOOD_DARK) && at(83, 66) == color(PT_WOOD));
    assert(at(93, 191) == color(PT_WOOD) && at(100, 191) == color(PT_WOOD_DARK));
    unsigned dark_fibres = 0, light_fibres = 0;
    for (int yy = 80; yy < 181; ++yy) {
        for (int xx = 100; xx < 291; ++xx) {
            dark_fibres += at(xx, yy) == color(PT_PAPER_DARK);
            light_fibres += at(xx, yy) == color(PT_PAPER_LIGHT);
        }
    }
    assert(dark_fibres >= 5 && light_fibres >= 5);
    assert_bounds(80, 49, 321, 195);
    clear();
    drawPixelWindow(pixels, WIDTH, 107, 46, 186, 150, "MacBook");
    unsigned title_pixels = 0;
    for (int y = 58; y <= 71; ++y) {
        for (int x = 119; x <= 201; ++x) if (at(x, y) == color(PT_TEXT_DARK)) ++title_pixels;
    }
    assert(title_pixels > 0);
    assert(at(125, 72) == color(PT_PAPER_DARK)); /* Heading ends before the carved paper rule. */
}

static void test_button(void)
{
    clear();
    drawPixelButton(pixels, WIDTH, 10, 10, 48, 21, NULL, PIXEL_BUTTON_NORMAL);
    assert(at(20, 10) == color(PT_OUTLINE));
    assert(at(20, 20) == color(PT_WOOD));
    assert(at(20, 12) == color(PT_WOOD_LIGHT));
    assert(at(58, 20) == color(PT_WOOD_DARK));
    assert(at(15, 27) == color(PT_WOOD_DARK) && at(21, 27) == color(PT_WOOD));
    assert_bounds(10, 10, 59, 32);
    clear();
    drawPixelButton(pixels, WIDTH, 10, 10, 48, 21, NULL, PIXEL_BUTTON_PRESSED);
    assert(at(20, 10) == BACKGROUND);
    assert(at(20, 11) == color(PT_OUTLINE));
    assert(at(20, 21) == color(PT_WOOD_DARK));
    assert(at(15, 28) == color(PT_OUTLINE) && at(21, 28) == color(PT_WOOD_DARK));
    assert_bounds(10, 11, 59, 32);
    clear();
    drawPixelButton(pixels, WIDTH, 10, 10, 48, 21, NULL, PIXEL_BUTTON_SELECTED);
    assert(at(20, 10) == color(PT_PAPER_LIGHT));
    clear();
    drawPixelButton(pixels, WIDTH, 10, 10, 48, 21, NULL, PIXEL_BUTTON_SELECTED | PIXEL_BUTTON_PRESSED);
    assert(at(20, 11) == color(PT_PAPER_LIGHT));
}

static void test_checkbox(void)
{
    clear();
    drawPixelCheckbox(pixels, WIDTH, 10, 10, 10, false);
    assert(at(10, 10) == color(PT_OUTLINE) && at(11, 10) == color(PT_OUTLINE));
    assert(at(14, 14) == color(PT_PAPER));
    assert_bounds(10, 10, 19, 19);
    drawPixelCheckbox(pixels, WIDTH, 10, 10, 10, true);
    unsigned green_pixels = 0, dark_check = 0, light_check = 0;
    for (int y = 10; y < 20; ++y) {
        for (int x = 10; x < 20; ++x) {
            if (at(x, y) == color(PT_GREEN)) ++green_pixels;
            if (at(x, y) == color(PT_GREEN_DARK)) ++dark_check;
            if (at(x, y) == color(PT_GREEN_LIGHT)) ++light_check;
        }
    }
    assert(green_pixels >= 5 && dark_check >= 5 && light_check >= 3);
    assert(at(10, 14) == color(PT_OUTLINE) && at(19, 14) == color(PT_OUTLINE));
}

static void test_slider(void)
{
    clear();
    drawPixelSlider(pixels, WIDTH, 20, 20, 147, 18, 20, 20, 100);
    assert(at(21, 29) == color(PT_PAPER_LIGHT)); /* Thumb at the minimum endpoint. */
    assert(at(23, 24) == color(PT_OUTLINE));
    assert(at(50, 29) == color(PT_WOOD));
    assert_bounds(20, 20, 166, 37);
    memcpy(saved, pixels, WIDTH * HEIGHT * sizeof(*pixels));
    clear();
    drawPixelSlider(pixels, WIDTH, 20, 20, 147, 18, -300, 20, 100);
    assert(memcmp(saved, pixels, WIDTH * HEIGHT * sizeof(*pixels)) == 0);
    clear();
    drawPixelSlider(pixels, WIDTH, 20, 20, 147, 18, 100, 20, 100);
    assert(at(161, 29) == color(PT_PAPER_LIGHT)); /* Thumb at the maximum endpoint. */
    assert(at(50, 29) == color(PT_GOLD));
    memcpy(saved, pixels, WIDTH * HEIGHT * sizeof(*pixels));
    clear();
    drawPixelSlider(pixels, WIDTH, 20, 20, 147, 18, 999, 20, 100);
    assert(memcmp(saved, pixels, WIDTH * HEIGHT * sizeof(*pixels)) == 0);
    clear();
    drawPixelSlider(pixels, WIDTH, 20, 20, 147, 18, 40, 40, 40);
    assert(at(21, 29) == color(PT_PAPER_LIGHT));
    drawPixelSlider(pixels, WIDTH, 20, 20, 147, 18, 0, INT_MIN, INT_MAX);
}

static void test_sprites(void)
{
    clear();
    drawPixelHeart(pixels, WIDTH, 10, 10, 1, PT_HEART);
    uint16_t heart[49];
    unsigned highlights = 0;
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            heart[y * 7 + x] = at(10 + x, 10 + y);
            if (heart[y * 7 + x] == color(PT_ROSE_LIGHT)) ++highlights;
        }
    }
    assert(highlights == 1 && heart[6 * 7 + 3] == color(PT_OUTLINE));
    assert_bounds(10, 10, 16, 16);
    clear();
    drawPixelHeart(pixels, WIDTH, 10, 10, 2, PT_HEART);
    for (int y = 0; y < 14; ++y) {
        for (int x = 0; x < 14; ++x) assert(at(10 + x, 10 + y) == heart[(y / 2) * 7 + x / 2]);
    }
    clear();
    drawPixelHeartOutline(pixels, WIDTH, 10, 10, 1, PT_HEART);
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            assert(at(10 + x, 10 + y) == (heart[y * 7 + x] == color(PT_OUTLINE) ? color(PT_HEART) : BACKGROUND));
        }
    }
    clear();
    drawPixelSun(pixels, WIDTH, 10, 10, 1);
    uint16_t sun[144];
    unsigned gold = 0, light = 0, outline = 0;
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12; ++x) {
            const uint16_t pixel = at(10 + x, 10 + y);
            sun[y * 12 + x] = pixel;
            assert(pixel == BACKGROUND || pixel == color(PT_OUTLINE) || pixel == color(PT_GOLD) || pixel == color(PT_PAPER_LIGHT));
            gold += pixel == color(PT_GOLD); light += pixel == color(PT_PAPER_LIGHT); outline += pixel == color(PT_OUTLINE);
        }
    }
    assert(gold > 20 && light > 0 && outline > 20);
    assert(at(15, 21) == color(PT_OUTLINE)); /* True 12-row height. */
    assert_bounds(10, 10, 21, 21);
    clear();
    drawPixelSun(pixels, WIDTH, 10, 10, 2);
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 24; ++x) assert(at(10 + x, 10 + y) == sun[(y / 2) * 12 + x / 2]);
    }
}

static void test_number(void)
{
    clear();
    drawPixelNumber(pixels, WIDTH, 20, 20, "25:00", 3, PT_TEXT_DARK, false);
    int first = HEIGHT, last = -1;
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            if (at(x, y) != BACKGROUND) {
                if (y < first) first = y;
                if (y > last) last = y;
            }
        }
    }
    assert(first == 20 && last == 40); /* Numeric ink anchors y exactly. */
    memcpy(saved, pixels, WIDTH * HEIGHT * sizeof(*pixels));
    clear();
    drawPixelNumber(pixels, WIDTH, 20, 20, "25:00", 3, PT_TEXT_DARK, true);
    unsigned shadow_pixels = 0;
    for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
        if (saved[i] == color(PT_TEXT_DARK)) assert(pixels[i] == color(PT_TEXT_DARK));
        if (pixels[i] == color(PT_PAPER_DARK)) ++shadow_pixels;
    }
    assert(shadow_pixels > 0);
    assert_bounds(20, 20, 108, 41);
}

static void test_clipping(void)
{
    for (int y = -30; y <= 250; y += 20) {
        for (int x = -30; x <= 410; x += 20) {
            clear();
            drawPixelWindow(pixels, WIDTH, x, y, 80, 60, "设置");
            drawPixelButton(pixels, WIDTH, x, y, 40, 21, "开始", PIXEL_BUTTON_PRESSED);
            drawPixelCheckbox(pixels, WIDTH, x, y, 10, true);
            drawPixelSlider(pixels, WIDTH, x, y, 147, 18, x, 20, 100);
            drawPixelHeart(pixels, WIDTH, x, y, 2, PT_HEART);
            drawPixelHeartOutline(pixels, WIDTH, x, y, 2, PT_HEART);
            drawPixelSun(pixels, WIDTH, x, y, 2);
            drawPixelNumber(pixels, WIDTH, x, y, "25:00", 3, PT_TEXT_DARK, true);
        }
    }
    drawPixelWindow(pixels, WIDTH, INT_MAX, INT_MAX, INT_MAX, INT_MAX, "设置");
    drawPixelButton(pixels, WIDTH, INT_MAX, INT_MAX, INT_MAX, INT_MAX, "开始", 0);
    drawPixelSlider(pixels, WIDTH, INT_MIN, INT_MIN, INT_MAX, INT_MAX, INT_MAX, INT_MIN, INT_MAX);
    drawPixelHeart(pixels, WIDTH, INT_MIN, INT_MIN, INT_MAX, PT_HEART);
    drawPixelSun(pixels, WIDTH, INT_MIN, INT_MIN, INT_MAX);
    drawPixelCheckbox(pixels, WIDTH, INT_MIN, INT_MIN, INT_MAX, true);
    drawPixelWindow(NULL, WIDTH, 0, 0, 80, 60, "设置");
    drawPixelButton(NULL, WIDTH, 0, 0, 40, 21, "开始", 0);
    drawPixelCheckbox(NULL, WIDTH, 0, 0, 10, true);
    drawPixelSlider(NULL, WIDTH, 0, 0, 147, 18, 85, 20, 100);
    drawPixelSun(NULL, WIDTH, 0, 0, 1);
    drawPixelNumber(NULL, WIDTH, 0, 0, "0", 1, PT_TEXT_DARK, true);
}

static void capture_components(const char *path)
{
    for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) pixels[i] = color(PT_NIGHT);
    drawPixelWindow(pixels, WIDTH, 9, 8, 216, 222, "设置");
    drawPixelButton(pixels, WIDTH, 26, 48, 82, 25, "开始", PIXEL_BUTTON_SELECTED);
    drawPixelButton(pixels, WIDTH, 119, 48, 85, 25, "暂停", PIXEL_BUTTON_PRESSED);
    drawPixelLabel(pixels, WIDTH, 28, 86, "今日待办", PT_TEXT_DARK, 1);
    drawPixelCheckbox(pixels, WIDTH, 29, 109, 12, false);
    drawPixelLabel(pixels, WIDTH, 50, 109, "喝水", PT_TEXT_DARK, 1);
    drawPixelCheckbox(pixels, WIDTH, 115, 109, 12, true);
    drawPixelLabel(pixels, WIDTH, 136, 109, "健身", PT_WOOD_DARK, 1);
    drawPixelLabel(pixels, WIDTH, 28, 139, "亮度", PT_TEXT_DARK, 1);
    drawPixelNumber(pixels, WIDTH, 159, 139, "85%", 2, PT_TEXT_DARK, true);
    drawPixelSlider(pixels, WIDTH, 26, 164, 179, 24, 85, 20, 100);
    drawPixelButton(pixels, WIDTH, 57, 199, 119, 21, "再看一次", PIXEL_BUTTON_NORMAL);
    drawPixelWindow(pixels, WIDTH, 239, 8, 153, 222, "MacBook");
    drawPixelSun(pixels, WIDTH, 254, 45, 2);
    drawPixelLabel(pixels, WIDTH, 286, 48, "晴", PT_TEXT_DARK, 1);
    drawPixelNumber(pixels, WIDTH, 286, 68, "24", 3, PT_TEXT_DARK, true);
    drawPixelHeart(pixels, WIDTH, 259, 104, 2, PT_HEART);
    drawPixelHeartOutline(pixels, WIDTH, 282, 104, 2, PT_HEART);
    drawPixelNumber(pixels, WIDTH, 254, 141, "25:00", 3, PT_TEXT_DARK, true);
    drawPixelLabel(pixels, WIDTH, 255, 177, "专注一下?", PT_TEXT_DARK, 1);
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    fprintf(file, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
        const uint16_t pixel = pixels[i];
        const unsigned char rgb[] = {
            (unsigned char)(((pixel >> 11) & 31) * 255 / 31),
            (unsigned char)(((pixel >> 5) & 63) * 255 / 63),
            (unsigned char)((pixel & 31) * 255 / 31)
        };
        assert(fwrite(rgb, sizeof(rgb), 1, file) == 1);
    }
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    pixels = malloc(WIDTH * HEIGHT * sizeof(*pixels));
    saved = malloc(WIDTH * HEIGHT * sizeof(*saved));
    assert(pixels != NULL && saved != NULL);
    test_window(); test_button(); test_checkbox(); test_slider();
    test_sprites(); test_number(); test_clipping();
    if (argc == 2) capture_components(argv[1]);
    free(pixels); free(saved);
    puts("CatDesk pixel UI: timber grain/rivets, paper fibres, button states, three-shade check, slot slider, sprites, numeric baseline and clipping passed.");
    return 0;
}
