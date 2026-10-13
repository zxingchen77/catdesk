/* Build with the same sources as tools/render_welcome_preview.c and enable
 * -fsanitize=address,undefined. Tests observable state, output and dirty
 * coverage; no animation artwork is recreated here.
 */
#include "catdesk_welcome.h"
#include "catdesk_render.h"
#include "catdesk_cats.h"
#include "pixel_theme.h"
#include "pixel_ui.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

enum { PIXELS = CD_WIDTH * CD_HEIGHT };
static uint16_t background[PIXELS];
static uint16_t previous_pixels[PIXELS];
static uint16_t current_pixels[PIXELS];
static uint16_t home_pixels[PIXELS];
static uint16_t welcome_pixels[PIXELS];
static uint16_t guarded_pixels[PIXELS + 2];

static void init_model(cd_model_t *model)
{
    cd_model_init(model, 1791283320); /* 2026-10-06, Shanghai 19:42. */
    model->clock_synced = true;
    assert(model->month == 10 && model->day == 6);
}

static void render(uint16_t *pixels, const cd_model_t *model, const cd_welcome_t *scene)
{
    cd_model_t model_copy = *model;
    cd_welcome_t scene_copy = *scene;
    if (cd_welcome_active(scene)) cd_welcome_render(pixels, background, model, scene);
    else cd_render_frame(pixels, background, model);
    assert(memcmp(model, &model_copy, sizeof(*model)) == 0);
    assert(memcmp(scene, &scene_copy, sizeof(*scene)) == 0);
}

static unsigned differences(const uint16_t *a, const uint16_t *b)
{
    unsigned changed = 0;
    for (unsigned index = 0; index < PIXELS; ++index) changed += a[index] != b[index];
    return changed;
}

static void assert_pose_equal(cd_welcome_cat_pose_t a, cd_welcome_cat_pose_t b)
{
    assert(a.x == b.x && a.y == b.y && a.state == b.state && a.frame == b.frame);
    assert(a.direction == b.direction && a.visible == b.visible);
}

static unsigned count_shared_hearts(const uint16_t *pixels)
{
    uint16_t sprite[7 * 7] = {0};
    drawPixelHeart(sprite, 7, 0, 0, 1, PT_HEART);
    unsigned count = 0;
    for (int y = 160; y <= 200; ++y) {
        for (int x = 140; x <= 253; ++x) {
            bool matched = true;
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 7; ++col)
                    if (sprite[row * 7 + col] != 0 &&
                        sprite[row * 7 + col] != pixels[(y + row) * CD_WIDTH + x + col])
                        matched = false;
            count += matched;
        }
    }
    return count;
}

static unsigned assert_whole_tiles(const cd_model_t *model, const cd_welcome_t *scene,
                                  const uint16_t *pixels)
{
    cd_welcome_t closed_mask = *scene;
    closed_mask.phase = CD_WELCOME_ENTERING;
    closed_mask.exit_ms = CD_WELCOME_RUN_HOME_MS + CD_WELCOME_DOOR_MS;
    render(welcome_pixels, model, &closed_mask);
    cd_render_frame(home_pixels, background, model);
    unsigned revealed = 0;
    for (int y = 0; y < CD_HEIGHT; y += 16) {
        for (int x = 0; x < CD_WIDTH; x += 16) {
            bool home = true, welcome = true;
            for (int row = y; row < y + 16 && row < CD_HEIGHT; ++row) {
                for (int col = x; col < x + 16 && col < CD_WIDTH; ++col) {
                    unsigned index = (unsigned)(row * CD_WIDTH + col);
                    home &= pixels[index] == home_pixels[index];
                    welcome &= pixels[index] == welcome_pixels[index];
                }
            }
            /* Every whole 16px tile belongs entirely to one native scene.
             * This excludes blend frames, blur and partial tile edges. */
            assert(home || welcome);
            revealed += home && !welcome;
        }
    }
    return revealed;
}

static unsigned assert_dirty(const cd_welcome_t *scene, const cd_welcome_t *previous,
                             const uint16_t *before, const uint16_t *after)
{
    cd_welcome_rect_t rect = {-1, -1, -1, -1};
    bool dirty = cd_welcome_dirty_rect(scene, previous, &rect);
    if (dirty) {
        assert(rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0);
        assert(rect.x + rect.width <= CD_WIDTH && rect.y + rect.height <= CD_HEIGHT);
    }
    unsigned changed = 0;
    for (int y = 0; y < CD_HEIGHT; ++y) {
        for (int x = 0; x < CD_WIDTH; ++x) {
            if (before[y * CD_WIDTH + x] == after[y * CD_WIDTH + x]) continue;
            ++changed;
            if (!(dirty && x >= rect.x && x < rect.x + rect.width &&
                  y >= rect.y && y < rect.y + rect.height)) {
                fprintf(stderr, "Uncovered welcome pixel (%d,%d), phase %d, scene %u, exit %u\n",
                        x, y, scene->phase, scene->elapsed_ms, scene->exit_ms);
                assert(false);
            }
        }
    }
    return changed;
}

static void assert_palette_and_guards(const cd_model_t *model, const cd_welcome_t *scene)
{
    static const unsigned rgb[] = {
        PT_OUTLINE, PT_WOOD_DARK, PT_WOOD, PT_WOOD_LIGHT, PT_PAPER_DARK,
        PT_PAPER, PT_PAPER_LIGHT, PT_WALL, PT_WALL_SHADE, PT_NIGHT,
        PT_NIGHT_BLUE, PT_DUSK, PT_CLOUD, PT_BLUE_LIGHT, PT_GREEN_DARK,
        PT_GREEN, PT_GREEN_LIGHT, PT_RED, PT_ROSE_LIGHT, PT_COZY_PINK,
        PT_COZY_BLUE, PT_GOLD, PT_CAT_ORANGE_OUTLINE, PT_CAT_ORANGE,
        PT_CAT_ORANGE_LIGHT, PT_CAT_ORANGE_SHADOW, PT_CAT_NOSE,
        PT_CAT_BLACK_LIGHT, PT_CAT_BLACK_OUTLINE, PT_CAT_BLACK_SHADOW,
        PT_CAT_EYES
    };
    guarded_pixels[0] = 0xDA7A;
    guarded_pixels[PIXELS + 1] = 0xBEEF;
    memset(guarded_pixels + 1, 0, PIXELS * sizeof(uint16_t));
    render(guarded_pixels + 1, model, scene);
    assert(guarded_pixels[0] == 0xDA7A && guarded_pixels[PIXELS + 1] == 0xBEEF);
    static bool allowed[65536];
    memset(allowed, 0, sizeof(allowed));
    for (unsigned color = 0; color < sizeof(rgb) / sizeof(rgb[0]); ++color)
        allowed[cd_color(rgb[color])] = true;
    if (scene->phase == CD_WELCOME_WIPING || scene->phase == CD_WELCOME_DONE) {
        cd_render_frame(home_pixels, background, model);
        for (unsigned index = 0; index < PIXELS; ++index) allowed[home_pixels[index]] = true;
    }
    for (unsigned index = 1; index <= PIXELS; ++index)
        assert(allowed[guarded_pixels[index]]);
}

static void test_intro_and_wait(void)
{
    cd_model_t model;
    init_model(&model);
    cd_welcome_t scene;
    cd_welcome_init(&scene, &model);
    assert(scene.phase == CD_WELCOME_INTRO && cd_welcome_active(&scene));
    assert(!scene.anniversary && !scene.press_latched);
    assert(!cd_welcome_press(&scene)); /* An early touch cannot bypass the opening. */
    assert(!scene.press_latched && scene.phase == CD_WELCOME_INTRO);
    cd_welcome_rect_t initial;
    assert(cd_welcome_dirty_rect(&scene, NULL, &initial));
    assert(initial.x == 0 && initial.y == 0 && initial.width == 400 && initial.height == 240);
    render(previous_pixels, &model, &scene);
    assert_palette_and_guards(&model, &scene);
    for (unsigned time = 1; time <= CD_WELCOME_INTRO_MS; ++time) {
        cd_welcome_t previous = scene;
        bool dirty = cd_welcome_tick(&scene, 1, &model);
        if (time % CD_WELCOME_FRAME_MS) assert(!dirty);
        if (time % CD_WELCOME_FRAME_MS == 0) {
            render(current_pixels, &model, &scene);
            unsigned changed = assert_dirty(&scene, &previous, previous_pixels, current_pixels);
            if (changed) assert(dirty);
            memcpy(previous_pixels, current_pixels, sizeof(previous_pixels));
            assert_palette_and_guards(&model, &scene);
        }
        if (time < CD_WELCOME_INTRO_MS) assert(scene.phase == CD_WELCOME_INTRO);
    }
    assert(scene.phase == CD_WELCOME_WAITING && !scene.press_latched);
    cd_welcome_cat_pose_t orange = cd_welcome_cat_pose(&scene, CD_CAT_ORANGE);
    cd_welcome_cat_pose_t black = cd_welcome_cat_pose(&scene, CD_CAT_BLACK);
    assert(orange.visible && black.visible);
    assert(orange.x < black.x && orange.direction == 1 && black.direction == -1);
    assert(orange.x >= 0 && black.x + CD_CAT_WIDTH <= CD_WIDTH);
    assert(orange.y >= 0 && orange.y + CD_CAT_HEIGHT <= CD_HEIGHT);
    assert(black.y >= 0 && black.y + CD_CAT_HEIGHT <= CD_HEIGHT);
    render(previous_pixels, &model, &scene);
    unsigned prompt_changes = 0;
    for (unsigned time = 100; time <= 60000; time += 100) {
        cd_welcome_t previous = scene;
        bool dirty = cd_welcome_tick(&scene, 100, &model);
        assert(scene.phase == CD_WELCOME_WAITING && cd_welcome_active(&scene));
        assert(!scene.press_latched);
        render(current_pixels, &model, &scene);
        unsigned changed = assert_dirty(&scene, &previous, previous_pixels, current_pixels);
        assert(dirty == (time % 1000 == 0));
        assert((changed != 0) == (time % 1000 == 0));
        if (changed) {
            ++prompt_changes;
            /* The whole room, title, cats and hearts stay fixed while waiting. */
            assert(memcmp(previous_pixels, current_pixels, CD_WIDTH * 210 * sizeof(uint16_t)) == 0);
        }
        memcpy(previous_pixels, current_pixels, sizeof(previous_pixels));
        cd_welcome_cat_pose_t current_orange = cd_welcome_cat_pose(&scene, CD_CAT_ORANGE);
        cd_welcome_cat_pose_t current_black = cd_welcome_cat_pose(&scene, CD_CAT_BLACK);
        assert_pose_equal(orange, current_orange);
        assert_pose_equal(black, current_black);
    }
    assert(prompt_changes == 60);
}

static cd_welcome_t intro_at(const cd_model_t *model, unsigned time)
{
    cd_welcome_t scene;
    cd_welcome_init(&scene, model);
    cd_welcome_tick(&scene, time, model);
    return scene;
}

static unsigned brightness(uint16_t color)
{
    return ((color >> 11) & 31) * 255u / 31u +
           ((color >> 5) & 63) * 255u / 63u + (color & 31) * 255u / 31u;
}

static void test_opening_choreography(void)
{
    cd_model_t model;
    init_model(&model);
    cd_welcome_t scene = intro_at(&model, 0);
    render(previous_pixels, &model, &scene);
    scene = intro_at(&model, 299);
    render(current_pixels, &model, &scene);
    assert(differences(previous_pixels, current_pixels) == 0);
    scene = intro_at(&model, 300);
    render(current_pixels, &model, &scene);
    assert(differences(previous_pixels, current_pixels) > 3000); /* The original hut arrives after 0.3s. */
    unsigned light = brightness(current_pixels[100 * CD_WIDTH + 125]);
    for (unsigned time = 600; time <= 1000; time += 200) {
        scene = intro_at(&model, time);
        render(current_pixels, &model, &scene);
        unsigned next_light = brightness(current_pixels[100 * CD_WIDTH + 125]);
        assert(next_light > light); /* Warm window steps brighten without blending. */
        light = next_light;
    }
    scene = intro_at(&model, 1199);
    render(previous_pixels, &model, &scene);
    scene = intro_at(&model, 1200);
    render(current_pixels, &model, &scene);
    assert(differences(previous_pixels, current_pixels) > 1000); /* The wood title follows the window light. */
    scene = intro_at(&model, 1499);
    assert(!cd_welcome_cat_pose(&scene, CD_CAT_ORANGE).visible);
    assert(!cd_welcome_cat_pose(&scene, CD_CAT_BLACK).visible);
    scene = intro_at(&model, 1600);
    cd_welcome_cat_pose_t orange = cd_welcome_cat_pose(&scene, CD_CAT_ORANGE);
    cd_welcome_cat_pose_t black = cd_welcome_cat_pose(&scene, CD_CAT_BLACK);
    assert(orange.visible && orange.x < 0 && orange.state == CD_CAT_WALK);
    assert(!black.visible);
    scene = intro_at(&model, 1800);
    cd_welcome_cat_pose_t next_orange = cd_welcome_cat_pose(&scene, CD_CAT_ORANGE);
    black = cd_welcome_cat_pose(&scene, CD_CAT_BLACK);
    assert(next_orange.x > orange.x && black.visible && black.x > 300);
    assert(black.direction == -1 && black.state == CD_CAT_WALK);
    scene = intro_at(&model, 2800);
    orange = cd_welcome_cat_pose(&scene, CD_CAT_ORANGE);
    black = cd_welcome_cat_pose(&scene, CD_CAT_BLACK);
    assert(orange.visible && black.visible && orange.state == CD_CAT_BLINK && black.state == CD_CAT_BLINK);
    render(current_pixels, &model, &scene);
    assert(count_shared_hearts(current_pixels) == 0);
    scene = intro_at(&model, 3000);
    assert_pose_equal((cd_welcome_cat_pose_t){orange.x, orange.y, CD_CAT_IDLE, 1, 1, true},
                      cd_welcome_cat_pose(&scene, CD_CAT_ORANGE));
    render(current_pixels, &model, &scene);
    assert(count_shared_hearts(current_pixels) == 0);
    scene = intro_at(&model, 3200);
    render(current_pixels, &model, &scene);
    assert(count_shared_hearts(current_pixels) == 1);
}

static void test_enter_and_complete(void)
{
    cd_model_t model;
    init_model(&model);
    cd_welcome_t scene;
    cd_welcome_init(&scene, &model);
    cd_welcome_tick(&scene, CD_WELCOME_INTRO_MS, &model);
    render(previous_pixels, &model, &scene);
    cd_welcome_t previous = scene;
    assert(cd_welcome_press(&scene));
    assert(scene.phase == CD_WELCOME_ENTERING && scene.press_latched);
    render(current_pixels, &model, &scene);
    assert_dirty(&scene, &previous, previous_pixels, current_pixels);
    memcpy(previous_pixels, current_pixels, sizeof(previous_pixels));
    cd_welcome_t latched = scene;
    for (unsigned count = 0; count < 50; ++count) assert(!cd_welcome_press(&scene));
    assert(memcmp(&scene, &latched, sizeof(scene)) == 0);
    unsigned end = CD_WELCOME_RUN_HOME_MS + CD_WELCOME_DOOR_MS + CD_WELCOME_WIPE_MS;
    unsigned wipe_frames = 0;
    unsigned previously_revealed = 0;
    for (unsigned time = 100; time <= end; time += 100) {
        previous = scene;
        assert(cd_welcome_tick(&scene, 100, &model));
        assert(!cd_welcome_press(&scene));
        render(current_pixels, &model, &scene);
        assert_dirty(&scene, &previous, previous_pixels, current_pixels);
        memcpy(previous_pixels, current_pixels, sizeof(previous_pixels));
        assert_palette_and_guards(&model, &scene);
        if (time < CD_WELCOME_RUN_HOME_MS + CD_WELCOME_DOOR_MS)
            assert(scene.phase == CD_WELCOME_ENTERING);
        else if (time < end) {
            assert(scene.phase == CD_WELCOME_WIPING);
            ++wipe_frames;
            unsigned revealed = assert_whole_tiles(&model, &scene, current_pixels);
            assert(revealed >= previously_revealed);
            previously_revealed = revealed;
        } else assert(scene.phase == CD_WELCOME_DONE);
    }
    assert(wipe_frames == CD_WELCOME_WIPE_MS / 100);
    assert(!cd_welcome_active(&scene));
    cd_render_frame(home_pixels, background, &model);
    assert(differences(home_pixels, current_pixels) == 0); /* Final wipe exactly matches the existing home. */
    cd_welcome_t completed = scene;
    memset(current_pixels, 0xAF, sizeof(current_pixels));
    memcpy(previous_pixels, current_pixels, sizeof(previous_pixels));
    cd_welcome_render(current_pixels, background, &model, &scene);
    assert(memcmp(previous_pixels, current_pixels, sizeof(current_pixels)) == 0);
    const unsigned deltas[] = {0, 1, 100, 60000, UINT32_MAX};
    for (unsigned index = 0; index < sizeof(deltas) / sizeof(deltas[0]); ++index) {
        assert(!cd_welcome_tick(&scene, deltas[index], &model));
        assert(!cd_welcome_press(&scene));
        assert(memcmp(&scene, &completed, sizeof(scene)) == 0);
        cd_welcome_rect_t rect;
        assert(!cd_welcome_dirty_rect(&scene, &completed, &rect));
    }
    model.day = 7;
    assert(!cd_welcome_tick(&scene, UINT32_MAX, &model));
    assert(memcmp(&scene, &completed, sizeof(scene)) == 0);
}

static void test_dates_and_frame_purity(void)
{
    cd_model_t model;
    init_model(&model);
    const int dates[][2] = {{9, 7}, {10, 6}, {10, 7}, {10, 8}, {11, 7}, {1, 1}};
    for (unsigned index = 0; index < sizeof(dates) / sizeof(dates[0]); ++index) {
        model.month = dates[index][0];
        model.day = dates[index][1];
        model.year = 2027;
        cd_welcome_t scene;
        cd_welcome_init(&scene, &model);
        cd_welcome_tick(&scene, 4000, &model);
        assert(scene.anniversary == (model.month == 10 && model.day == 7));
        render(current_pixels, &model, &scene);
        assert(count_shared_hearts(current_pixels) == (scene.anniversary ? 3u : 1u));
        assert_palette_and_guards(&model, &scene);
    }
    model.month = 10;
    model.day = 6;
    cd_welcome_t scene;
    cd_welcome_init(&scene, &model);
    cd_welcome_tick(&scene, 4000, &model);
    render(previous_pixels, &model, &scene);
    cd_welcome_t previous = scene;
    model.day = 7;
    assert(cd_welcome_tick(&scene, 0, &model));
    assert(scene.anniversary);
    render(current_pixels, &model, &scene);
    assert(assert_dirty(&scene, &previous, previous_pixels, current_pixels) > 0);
    bool extra_line = false, extra_hearts = false;
    for (int y = 0; y < CD_HEIGHT; ++y) {
        for (int x = 0; x < CD_WIDTH; ++x) {
            if (previous_pixels[y * CD_WIDTH + x] == current_pixels[y * CD_WIDTH + x]) continue;
            assert((y >= 112 && y < 134) || (y >= 169 && y < 188));
            extra_line |= y >= 112 && y < 134;
            extra_hearts |= y >= 169 && y < 188;
        }
    }
    assert(extra_line && extra_hearts);
    previous = scene;
    model.day = 8;
    assert(cd_welcome_tick(&scene, 0, &model));
    assert(!scene.anniversary);
    render(home_pixels, &model, &scene);
    assert(assert_dirty(&scene, &previous, current_pixels, home_pixels) > 0);
    assert(differences(previous_pixels, home_pixels) == 0);
}

static void test_arbitrary_deltas(void)
{
    cd_model_t model;
    init_model(&model);
    cd_welcome_t one_step, many_steps;
    cd_welcome_init(&one_step, &model);
    cd_welcome_init(&many_steps, &model);
    for (unsigned time = 0; time < 4000; ++time) cd_welcome_tick(&many_steps, 1, &model);
    cd_welcome_tick(&one_step, 4000, &model);
    assert(memcmp(&one_step, &many_steps, sizeof(one_step)) == 0);
    assert(cd_welcome_press(&one_step) && cd_welcome_press(&many_steps));
    for (unsigned time = 0; time < 1700; ++time) cd_welcome_tick(&many_steps, 1, &model);
    cd_welcome_tick(&one_step, 1700, &model);
    assert(memcmp(&one_step, &many_steps, sizeof(one_step)) == 0);
    assert(one_step.phase == CD_WELCOME_DONE);

    cd_welcome_init(&one_step, &model);
    cd_welcome_tick(&one_step, UINT32_MAX, &model);
    assert(one_step.phase == CD_WELCOME_WAITING && !one_step.press_latched);
    cd_welcome_tick(&one_step, UINT32_MAX, &model);
    assert(one_step.phase == CD_WELCOME_WAITING);
    assert(cd_welcome_press(&one_step));
    cd_welcome_tick(&one_step, UINT32_MAX, &model);
    assert(one_step.phase == CD_WELCOME_DONE && !cd_welcome_active(&one_step));
    assert_palette_and_guards(&model, &one_step);
}

int main(void)
{
    cd_ui_pointer(-1, -1, false);
    cd_render_background(background);
    test_opening_choreography();
    test_intro_and_wait();
    test_enter_and_complete();
    test_dates_and_frame_purity();
    test_arbitrary_deltas();
    puts("CatDesk welcome: 4s opening, indefinite wait, 1s discrete prompt, one touch, 800ms wipe, dates, palette, dirty coverage and frozen completion passed.");
    return 0;
}
