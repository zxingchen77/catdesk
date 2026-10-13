/* Host integration and framebuffer-boundary checks. Run from the repo root:
 * cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
 *   -fno-omit-frame-pointer -g -Ifirmware/components/catdesk_ui/include \
 *   tools/test_ui.c firmware/components/catdesk_ui/catdesk_model.c \
 *   firmware/components/catdesk_ui/catdesk_render.c \
 *   firmware/components/catdesk_ui/catdesk_cats.c \
 *   firmware/components/catdesk_ui/catdesk_font.c \
 *   firmware/components/catdesk_ui/pixel_ui.c -o /tmp/catdesk-test-ui
 * /tmp/catdesk-test-ui
 */
#include "catdesk_render.h"
#include "catdesk_font.h"
#include "catdesk_layout.h"
#include "pixel_theme.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Exactly-sized arrays let ASan catch writes beyond all four frame edges. */
static uint16_t background[CD_WIDTH * CD_HEIGHT];
static uint16_t framebuffer[CD_WIDTH * CD_HEIGHT];
static const int64_t OCT_6_MIDNIGHT = 1791216000;
static const int64_t OCT_7_MIDNIGHT = 1791302400;
static unsigned render_count;

static uint64_t image_hash(const uint16_t *pixels)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (unsigned i = 0; i < CD_WIDTH * CD_HEIGHT; ++i) {
        hash ^= pixels[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void render(const cd_model_t *model)
{
    unsigned char previous_model[sizeof(*model)];
    memcpy(previous_model, model, sizeof(*model));
    cd_render_frame(framebuffer, background, model);
    assert(memcmp(previous_model, model, sizeof(*model)) == 0);
    ++render_count;
}

static uint64_t region_hash(int x, int y, int width, int height)
{
    assert(x >= 0 && y >= 0 && x+width <= CD_WIDTH && y+height <= CD_HEIGHT);
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int row=y; row<y+height; ++row) {
        for (int col=x; col<x+width; ++col) {
            hash ^= framebuffer[row*CD_WIDTH+col];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static void assert_palette_budget(void)
{
    unsigned char seen[65536] = {0};
    unsigned colors = 0;
    for (unsigned i=0; i<CD_WIDTH*CD_HEIGHT; ++i) {
        unsigned color = framebuffer[i];
        if (!seen[color]) { seen[color] = 1; ++colors; }
    }
    assert(colors <= 32); /* Theme, decor and both cats share 31 colors. */
}

static void test_font_metrics(void)
{
    assert(cd_text_width("25:00",3) == 87);
    assert(cd_text_width("CatDesk",1) == 41);
    assert(cd_tiny_width("2024.10.07") == 35);
    assert(cd_tiny_width("2024.10.07") <= CD_FRAME_W-6);
    /* The declared interior width must be clear parchment, not a nested
       border that swallows the final date digit at furniture scale. */
    int date_x = CD_FRAME_X+CD_FRAME_W/2-cd_tiny_width("2024.10.07")/2;
    for (int y=CD_FRAME_Y+30; y<CD_FRAME_Y+35; ++y) {
        for (int x=date_x; x<date_x+35; ++x) {
            uint16_t paper = background[y*CD_WIDTH+x];
            assert(paper == cd_color(PT_PAPER) || paper == cd_color(PT_PAPER_LIGHT) ||
                   paper == cd_color(PT_PAPER_DARK));
        }
    }
    assert(cd_tiny_width("730 DAYS") == 29);
    assert(cd_tiny_width(".") == 1 && cd_tiny_width("0") == 3);
    memset(framebuffer,0,sizeof(framebuffer));
    cd_tiny_text(framebuffer,CD_WIDTH,0,0,"2024.10.07",0x1234);
    unsigned marked = 0;
    for (int y=0; y<CD_HEIGHT; ++y) {
        for (int x=0; x<CD_WIDTH; ++x) {
            if (framebuffer[y*CD_WIDTH+x]) {
                assert(x < 35 && y < 5 && framebuffer[y*CD_WIDTH+x] == 0x1234);
                ++marked;
            }
        }
    }
    assert(marked > 30);
    cd_tiny_text(framebuffer,CD_WIDTH,-2,-2,"2024.10.07",0x1234);
    cd_tiny_text(framebuffer,CD_WIDTH,399,239,"2024.10.07",0x1234);
    cd_tiny_text(framebuffer,CD_WIDTH,0,0,"\xE4\xB8",0x1234);
    assert(cd_tiny_width("\xE4\xB8") == 7);
}

/* The art-only proof ignores every popup, clock, text notice and timer.
 * It still uses the same real room, NPC sprites and laptop painter. */
static void test_textless_world(void)
{
    cd_model_t model;
    cd_model_init(&model,OCT_6_MIDNIGHT);
    cd_render_world(framebuffer,background,&model);
    const uint64_t world = image_hash(framebuffer);
    assert_palette_budget();
    model.popup = CD_POPUP_SETTINGS;
    model.focus_running = true;
    model.focus_remaining_ms = 1;
    model.notice_ms = 6000;
    model.anniversary_today = true;
    model.hour = 0;
    model.minute = 59;
    model.elapsed_days = 730;
    model.cats[0].bubble_ms = 1800;
    cd_render_world(framebuffer,background,&model);
    assert(image_hash(framebuffer) == world);
    model.mac_connected = true;
    cd_render_world(framebuffer,background,&model);
    assert(image_hash(framebuffer) != world);
    assert_palette_budget();
}

/* Actual visual changes must follow the existing UI actions. These checks
 * deliberately inspect the affected text/screen rather than unrelated cat
 * movement, and keep all existing hit areas and model transitions intact. */
static void test_visual_regions(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6_MIDNIGHT);
    render(&model);
    assert_palette_budget();
    const uint64_t disconnected_screen = region_hash(CD_MAC_X,CD_MAC_Y,CD_MAC_W,CD_MAC_H);
    model.mac_connected = true;
    render(&model);
    assert(region_hash(CD_MAC_X,CD_MAC_Y,CD_MAC_W,CD_MAC_H) != disconnected_screen);
    assert_palette_budget();
    model.mac_connected = false;
    render(&model);
    assert(region_hash(CD_MAC_X,CD_MAC_Y,CD_MAC_W,CD_MAC_H) == disconnected_screen);

    cd_ui_press(&model,265, 106);
    for (unsigned count=model.todo_count; count<CD_TODO_CAPACITY; ++count)
        cd_ui_press(&model,266,172);
    assert(model.todo_count == CD_TODO_CAPACITY);
    for (unsigned row=0; row<CD_TODO_CAPACITY; ++row) {
        render(&model);
        uint64_t label = region_hash(127,78+(int)row*18,151,16);
        cd_ui_press(&model,140,84+(int)row*18);
        render(&model);
        assert(region_hash(127,78+(int)row*18,151,16) != label);
    }
    assert(model.todo_checked == 31);
    assert_palette_budget();
    cd_ui_press(&model,310,220);

    cd_ui_press(&model,315, 105);
    cd_ui_press(&model,160,151);
    cd_model_tick(&model,61000);
    cd_ui_press(&model,315, 105);
    render(&model);
    const uint64_t pause_label = region_hash(127,140,75,26);
    cd_ui_press(&model,160,151);
    render(&model);
    assert(region_hash(127,140,75,26) != pause_label);
    assert_palette_budget();
    cd_ui_press(&model,160,151);
    cd_model_tick(&model,model.focus_remaining_ms);
    render(&model);
    const uint64_t complete_notice = region_hash(139,115,122,23);
    cd_model_tick(&model,6000);
    render(&model);
    assert(region_hash(139,115,122,23) != complete_notice);

    cd_model_init(&model,OCT_6_MIDNIGHT);
    model.mac_connected = true;
    cd_ui_press(&model,291, 103);
    for (unsigned endpoint=0; endpoint<2; ++endpoint) {
        model.mac_battery = model.mac_cpu = model.mac_memory = (uint8_t)(endpoint ? 100 : 0);
        model.mac_charging = endpoint != 0;
        render(&model);
        assert_palette_budget();
    }
    for (int popup=CD_POPUP_ANNIVERSARY; popup<=CD_POPUP_SETTINGS; ++popup) {
        model.popup = (cd_popup_t)popup;
        render(&model);
        assert_palette_budget();
    }
}

static void test_pointer_feedback(void)
{
    cd_model_t model;
    static const struct { cd_popup_t popup; int x, y; } controls[] = {
        {CD_POPUP_ANNIVERSARY,200,167}, {CD_POPUP_TODO,266,172},
        {CD_POPUP_FOCUS,160,151}, {CD_POPUP_FOCUS,237,151},
        {CD_POPUP_ANNIVERSARY,303,64}, {CD_POPUP_TODO,287,62},
        {CD_POPUP_FOCUS,270,73}, {CD_POPUP_SETTINGS,273,83},
        {CD_POPUP_SETTINGS,256,150},
        {CD_POPUP_WEATHER,276,63}, {CD_POPUP_MAC,276,60}
    };
    for (unsigned i=0; i<sizeof(controls)/sizeof(controls[0]); ++i) {
        cd_model_init(&model,OCT_6_MIDNIGHT);
        model.popup = controls[i].popup;
        cd_ui_pointer(-1,-1,false);
        unsigned char original[sizeof(model)];
        memcpy(original,&model,sizeof(model));
        assert(cd_ui_pointer_button(&model,controls[i].x,controls[i].y));
        render(&model);
        uint64_t normal_frame = image_hash(framebuffer);
        cd_ui_pointer(controls[i].x,controls[i].y,true);
        render(&model);
        assert(image_hash(framebuffer) != normal_frame);
        assert(memcmp(original,&model,sizeof(model)) == 0);
        cd_ui_pointer(controls[i].x,controls[i].y,false);
        render(&model);
        assert(image_hash(framebuffer) == normal_frame);
    }
    cd_model_init(&model,OCT_6_MIDNIGHT);
    cd_ui_pointer(-1,-1,false);
    cd_ui_press(&model,315, 105);
    render(&model);
    const uint64_t normal = region_hash(127,140,75,26);
    unsigned char before[sizeof(model)];
    memcpy(before,&model,sizeof(model));
    assert(cd_ui_pointer_button(&model,160,151));
    assert(!cd_ui_pointer_button(&model,200,100));
    cd_ui_pointer(160,151,true);
    assert(cd_ui_has_pointer());
    render(&model);
    const uint64_t pressed = region_hash(127,140,75,26);
    assert(pressed != normal);
    assert(memcmp(before,&model,sizeof(model)) == 0);
    cd_ui_visual_tick(159);
    assert(cd_ui_has_pointer());
    render(&model);
    assert(region_hash(127,140,75,26) == pressed);
    cd_ui_visual_tick(1);
    assert(!cd_ui_has_pointer());
    render(&model);
    assert(region_hash(127,140,75,26) == normal);
    assert(memcmp(before,&model,sizeof(model)) == 0);

    cd_ui_pointer(160,151,true);
    cd_ui_pointer(160,151,false); /* Release/cancel clears feedback immediately. */
    assert(!cd_ui_has_pointer());
    render(&model);
    assert(region_hash(127,140,75,26) == normal);
    cd_ui_pointer(160,151,true);
    render(&model); /* Same prepaint order used by the production touch callback. */
    cd_ui_press(&model,160,151);
    assert(model.popup == CD_POPUP_NONE && model.focus_running);
    cd_ui_visual_tick(160);
    render(&model);
    assert(!cd_ui_has_pointer());
    cd_ui_pointer(-1,-1,false);
}

static void assert_model_bounds(const cd_model_t *model)
{
    static const unsigned frame_counts[] = {4, 2, 6, 4, 6};
    assert((unsigned)model->popup <= CD_POPUP_SETTINGS);
    assert(model->brightness >= 20 && model->brightness <= 100);
    assert(model->todo_count <= CD_TODO_CAPACITY);
    assert((model->todo_checked >> model->todo_count) == 0);
    assert(model->focus_remaining_ms <= CD_FOCUS_DURATION_MS);
    for (unsigned i = 0; i < CD_CAT_COUNT; ++i) {
        const cd_cat_t *cat = &model->cats[i];
        assert(cat->x >= 40 && cat->x <= 320);
        assert(cat->y == 170);
        assert(cat->target_x >= 40 && cat->target_x <= 320);
        assert(cat->direction == 1 || cat->direction == -1);
        assert((unsigned)cat->state <= CD_CAT_TAP);
        assert(cat->frame < frame_counts[cat->state]);
    }
}

static void test_render_states(void)
{
    static const unsigned frame_counts[] = {4, 2, 6, 4, 6};
    cd_model_t model;
    cd_model_init(&model, OCT_6_MIDNIGHT);
    render(&model);
    const uint64_t home_hash = image_hash(framebuffer);
    uint64_t popup_hashes[CD_POPUP_SETTINGS + 1];
    popup_hashes[0] = home_hash;
    for (int popup = CD_POPUP_ANNIVERSARY; popup <= CD_POPUP_SETTINGS; ++popup) {
        model.popup = (cd_popup_t)popup;
        render(&model);
        popup_hashes[popup] = image_hash(framebuffer);
        for (int previous = 0; previous < popup; ++previous) {
            assert(popup_hashes[popup] != popup_hashes[previous]);
        }
    }

    /* Both ends of the permitted floor range, every frame, both directions,
       and speech/heart overlays. An idle render alone misses these paths. */
    model.popup = CD_POPUP_NONE;
    for (int kind = CD_CAT_ORANGE; kind <= CD_CAT_BLACK; ++kind) {
        for (int edge = 0; edge < 2; ++edge) {
            model.cats[kind].x = (int16_t)(edge ? 320 : 40);
            for (int facing = -1; facing <= 1; facing += 2) {
                model.cats[kind].direction = (int8_t)facing;
                for (int state = CD_CAT_IDLE; state <= CD_CAT_TAP; ++state) {
                    model.cats[kind].state = (cd_cat_state_t)state;
                    for (unsigned frame = 0; frame < frame_counts[state]; ++frame) {
                        model.cats[kind].frame = (uint8_t)frame;
                        model.cats[kind].heart_ms = frame & 1u ? 5000 : 200;
                        model.cats[kind].bubble_ms = frame & 1u ? 0 : 1000;
                        render(&model);
                    }
                }
            }
        }
    }
    cd_model_init(&model, OCT_7_MIDNIGHT);
    model.celebration_phase = CD_CELEBRATION_HEARTS;
    model.celebration_ms = 5000;
    model.notice_ms = 6000;
    model.todo_count = CD_TODO_CAPACITY;
    model.todo_checked = 31;
    model.mac_connected = true;
    for (int popup = CD_POPUP_NONE; popup <= CD_POPUP_SETTINGS; ++popup) {
        model.popup = (cd_popup_t)popup;
        for (int charging = 0; charging < 2; ++charging) {
            model.mac_charging = charging != 0;
            model.brightness = (uint8_t)(charging ? 100 : 20);
            render(&model);
        }
    }
    model.popup = CD_POPUP_FOCUS;
    model.focus_remaining_ms = 0;
    render(&model);
    model.focus_running = true;
    model.focus_remaining_ms = 1;
    render(&model);
}

static void test_navigation(void)
{
    const struct { int x, y; cd_popup_t popup; } targets[] = {
        {75, 42, CD_POPUP_WEATHER}, {64, 50, CD_POPUP_WEATHER},
        {210, 42, CD_POPUP_ANNIVERSARY}, {265, 106, CD_POPUP_TODO},
        {291, 103, CD_POPUP_MAC}, {315, 105, CD_POPUP_FOCUS},
        {373, 23, CD_POPUP_SETTINGS}
    };
    cd_model_t model;
    for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        cd_model_init(&model, OCT_6_MIDNIGHT);
        cd_ui_press(&model, targets[i].x, targets[i].y);
        assert(model.popup == targets[i].popup);
        assert(model.cats[0].state != CD_CAT_TAP && model.cats[1].state != CD_CAT_TAP);
        render(&model);
        /* First outside tap dismisses the modal without activating the scene. */
        cd_ui_press(&model, 310, 220);
        assert(model.popup == CD_POPUP_NONE);
        assert(model.cats[0].state != CD_CAT_TAP && model.cats[1].state != CD_CAT_TAP);
    }

    cd_model_init(&model, OCT_6_MIDNIGHT);
    for (int kind = CD_CAT_ORANGE; kind <= CD_CAT_BLACK; ++kind) {
        cd_ui_press(&model, model.cats[kind].x + 20, model.cats[kind].y + 20);
        assert(model.popup == CD_POPUP_NONE);
        assert(model.cats[kind].state == CD_CAT_TAP);
        assert(model.cats[kind].tap_count == 1);
    }
    cd_model_init(&model, OCT_6_MIDNIGHT);
    const int misses[][2] = {{-1, -1}, {400, 240}, {399, 239}, {180, 180}, {0, 80}};
    for (unsigned i = 0; i < sizeof(misses) / sizeof(misses[0]); ++i) {
        const cd_model_t before = model;
        cd_ui_press(&model, misses[i][0], misses[i][1]);
        assert(memcmp(&model, &before, sizeof(model)) == 0);
    }

    const struct { cd_popup_t popup; int x, y; } closes[] = {
        {CD_POPUP_ANNIVERSARY, 303, 64}, {CD_POPUP_TODO, 287, 62},
        {CD_POPUP_FOCUS, 270, 73}, {CD_POPUP_SETTINGS, 273, 83},
        {CD_POPUP_SETTINGS,256,150},
        {CD_POPUP_WEATHER, 276, 63}, {CD_POPUP_MAC, 276, 60}
    };
    for (unsigned i = 0; i < sizeof(closes) / sizeof(closes[0]); ++i) {
        model.popup = closes[i].popup;
        cd_ui_press(&model, closes[i].x, closes[i].y);
        assert(model.popup == CD_POPUP_NONE);
    }

    /* A modal consumes cat taps and all non-control taps in its content area. */
    cd_model_init(&model, OCT_6_MIDNIGHT);
    model.popup = CD_POPUP_WEATHER;
    cd_ui_press(&model, 132, 185);
    assert(model.popup == CD_POPUP_WEATHER);
    assert(model.cats[0].state == CD_CAT_IDLE);
    cd_ui_press(&model, 285, 190);
    assert(model.popup == CD_POPUP_WEATHER);
}

static void test_todo(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6_MIDNIGHT);
    cd_ui_press(&model, 265, 106);
    cd_ui_press(&model, 117, 85);
    assert(model.todo_checked == 1);
    cd_ui_press(&model, 245, 85); /* Entire row, not only the checkbox. */
    assert(model.todo_checked == 0);
    cd_ui_press(&model, 117, 103);
    assert(model.todo_checked == 2);
    cd_ui_press(&model, 266, 172);
    assert(model.todo_count == 4 && model.todo_checked == 2);
    cd_ui_press(&model, 266, 172);
    assert(model.todo_count == 5 && model.todo_checked == 2);
    cd_ui_press(&model, 266, 172);
    assert(model.todo_count == 5);
    cd_ui_press(&model, 120, 157);
    assert(model.todo_checked == 18);
    cd_ui_press(&model, 120, 76); /* Header/row boundary must not toggle. */
    assert(model.todo_checked == 18);
    render(&model);
}

static void test_focus(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6_MIDNIGHT);
    cd_ui_press(&model, 315, 105);
    cd_ui_press(&model, 160, 151);
    assert(model.popup == CD_POPUP_NONE && model.focus_running);
    assert(model.focus_remaining_ms == CD_FOCUS_DURATION_MS);
    cd_model_tick(&model, 1234);
    const uint32_t remaining = model.focus_remaining_ms;
    cd_ui_press(&model, 329, 14); /* The active wall timer is also tappable. */
    assert(model.popup == CD_POPUP_FOCUS);
    cd_ui_press(&model, 270, 73); /* Closing a popup doesn't pause the timer. */
    assert(model.popup == CD_POPUP_NONE && model.focus_running);
    cd_model_tick(&model, 1000);
    assert(model.focus_remaining_ms == remaining - 1000);
    cd_ui_press(&model, 315, 105);
    cd_ui_press(&model, 160, 151);
    assert(!model.focus_running && model.popup == CD_POPUP_FOCUS);
    const uint32_t paused = model.focus_remaining_ms;
    cd_model_tick(&model, 3000);
    assert(model.focus_remaining_ms == paused);
    cd_ui_press(&model, 160, 151); /* Resume must preserve the paused countdown. */
    assert(model.focus_running && model.popup == CD_POPUP_NONE);
    assert(model.focus_remaining_ms == paused);
    cd_ui_press(&model, 315, 105);
    cd_ui_press(&model, 237, 151);
    assert(!model.focus_running && model.focus_remaining_ms == CD_FOCUS_DURATION_MS);
    assert(model.popup == CD_POPUP_FOCUS);
    render(&model);
    cd_ui_press(&model, 160, 151);
    cd_model_tick(&model, CD_FOCUS_DURATION_MS);
    assert(!model.focus_running && model.focus_remaining_ms == 0 && model.notice_ms != 0);
    cd_ui_press(&model, 315, 105);
    cd_ui_press(&model, 160, 151); /* A completed timer starts a new full session. */
    assert(model.focus_running && model.focus_remaining_ms == CD_FOCUS_DURATION_MS);
}

static void test_settings(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6_MIDNIGHT);
    cd_ui_press(&model, 373, 23);
    cd_ui_press(&model, 120, 137);
    assert(model.brightness == 20);
    cd_ui_drag(&model, 280, 137);
    assert(model.brightness == 100);
    cd_ui_drag(&model, 200, 137);
    assert(model.brightness == 60);
    cd_ui_drag(&model, 130, 137);
    assert(model.brightness == 20);
    cd_ui_drag(&model, 270, 137);
    assert(model.brightness == 100);
    const int misses[][2] = {{119, 137}, {281, 137}, {200, 118}, {200, 160}, {-500, 137}, {500, 137}};
    for (unsigned i = 0; i < sizeof(misses) / sizeof(misses[0]); ++i) {
        cd_ui_drag(&model, misses[i][0], misses[i][1]);
        assert(model.brightness == 100);
    }
    cd_ui_press(&model, 10, 10);
    assert(model.popup == CD_POPUP_NONE);
    cd_ui_drag(&model, 130, 137);
    assert(model.brightness == 100);
    cd_ui_press(&model,373,23);
    assert(model.popup == CD_POPUP_SETTINGS);
    cd_ui_press(&model,256,150); /* Bottom CLOSE must not adjust the slider beneath its hit area. */
    assert(model.popup == CD_POPUP_NONE && model.brightness == 100);
}

static void test_anniversary_replay(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_7_MIDNIGHT);
    cd_model_tick(&model, 10000);
    assert(!model.celebration_active);
    /* On the anniversary, the real room frame starts the event directly. */
    cd_ui_press(&model, 210, 42);
    assert(model.popup == CD_POPUP_NONE && model.celebration_active);
    assert(model.celebration_phase == CD_CELEBRATION_APPROACH && model.celebration_preview);
    cd_model_tick(&model, 100);
    for (unsigned i = 0; i < 160 && model.celebration_phase != CD_CELEBRATION_HEARTS; ++i) {
        cd_model_tick(&model, 50);
        render(&model);
    }
    assert(model.celebration_phase == CD_CELEBRATION_HEARTS);
    assert(model.cats[0].x == 166 && model.cats[1].x == 198);
    assert(model.cats[0].heart_ms != 0 && model.cats[1].heart_ms != 0);
    cd_model_tick(&model, 5000);
    assert(!model.celebration_active);
    cd_model_init(&model,OCT_6_MIDNIGHT);
    cd_ui_press(&model,210,42);
    assert(model.popup == CD_POPUP_ANNIVERSARY);
    cd_ui_press(&model,200,167); /* Other dates retain the detail-menu replay action. */
    assert(model.popup == CD_POPUP_NONE && model.celebration_active && model.celebration_preview);
}

static uint32_t next_random(uint32_t *seed)
{
    *seed ^= *seed << 13;
    *seed ^= *seed >> 17;
    *seed ^= *seed << 5;
    return *seed;
}

static void test_random_hour(void)
{
    cd_model_t model;
    /* Cross midnight, including the automatic anniversary animation. */
    cd_model_init(&model, OCT_7_MIDNIGHT - 1800);
    uint32_t seed = UINT32_C(0x4c415443);
    uint32_t elapsed = 0;
    unsigned steps = 0;
    unsigned seen_popup = 0;
    unsigned seen_cat_state = 0;
    while (elapsed < 3600000) {
        uint32_t delta = next_random(&seed) % 999 + 1;
        if (delta > 3600000 - elapsed) delta = 3600000 - elapsed;
        cd_model_tick(&model, delta);
        elapsed += delta;
        if (steps % 7 == 0) {
            int x = (int)(next_random(&seed) % 480) - 40;
            int y = (int)(next_random(&seed) % 320) - 40;
            cd_ui_press(&model, x, y);
        }
        if (steps % 11 == 0) {
            int x = (int)(next_random(&seed) % 200) + 100;
            int y = (int)(next_random(&seed) % 100) + 90;
            cd_ui_drag(&model, x, y);
        }
        /* Occasionally aim at a real prop/cat rather than mostly empty wall. */
        if (steps % 29 == 0) {
            static const int targets[][2] = {{75, 42}, {210, 42}, {265, 106},
                {291, 103}, {315, 105}, {373, 23}, {160, 151}, {266, 172}};
            unsigned target = next_random(&seed) % (sizeof(targets) / sizeof(targets[0]));
            cd_ui_press(&model, targets[target][0], targets[target][1]);
        }
        if (steps % 43 == 0 && model.popup == CD_POPUP_NONE) {
            unsigned kind = next_random(&seed) % CD_CAT_COUNT;
            cd_ui_press(&model, model.cats[kind].x + 20, model.cats[kind].y + 20);
        }
        assert_model_bounds(&model);
        seen_popup |= 1u << model.popup;
        for (unsigned i = 0; i < CD_CAT_COUNT; ++i) seen_cat_state |= 1u << model.cats[i].state;
        render(&model);
        ++steps;
    }
    assert(model.epoch_utc == OCT_7_MIDNIGHT + 1800);
    assert(model.clock_fraction_ms == 0);
    assert(seen_popup == 127u);
    assert(seen_cat_state == 31u);
    assert(steps > 6000);
}

int main(void)
{
    cd_render_background(background);
    cd_ui_pointer(-1,-1,false);
    const uint64_t background_hash = image_hash(background);
    test_font_metrics();
    test_textless_world();
    test_render_states();
    test_visual_regions();
    test_pointer_feedback();
    test_navigation();
    test_todo();
    test_focus();
    test_settings();
    test_anniversary_replay();
    test_random_hour();
    assert(image_hash(background) == background_hash);
    printf("CatDesk UI: navigation, modal boundaries, pets, todo, focus, brightness, replay and one-hour randomized run passed (%u rendered frames).\n", render_count);
    return 0;
}
