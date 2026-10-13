#!/usr/bin/env python3
"""Check actual touch/timer callbacks with host input and clock stubs.

The callbacks are extracted verbatim. Renderer, model, sprites, font and pixel
components are real; only the ESP/LVGL boundary is replaced. This verifies
callback ordering and state, not hardware touch latency or display timing.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


PREFIX = r'''
#include "catdesk_render.h"
#include "catdesk_couple.h"
#include "catdesk_layout.h"
#include "catdesk_welcome.h"
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST };
typedef struct { int code; } lv_event_t;
typedef struct { int x, y; } lv_point_t;
typedef int lv_indev_t;
typedef int lv_timer_t;

static const char *TAG = "CATDESK";
static cd_model_t s_model;
static bool s_nvs_ready, s_settings_dirty;
static int64_t s_last_change_us, s_last_save_us, s_last_tick_us, s_last_log_us;
static uint32_t s_frames, s_render_us;
static int64_t s_visual_hold_until_us;
static cd_welcome_t s_welcome;
static bool s_welcome_contact;
static bool s_presented_once;
static unsigned timer_period;

static int64_t fake_now_us;
static bool input_available;
static lv_indev_t input_device;
static lv_point_t input_point;
static unsigned log_calls, serial_calls, save_calls, present_calls;
static int64_t serial_time_us, save_time_us;
static uint16_t background[CD_WIDTH*CD_HEIGHT], frame[CD_WIDTH*CD_HEIGHT];

typedef struct {
    cd_model_t model;
    bool pointer;
    uint64_t hash;
} present_record_t;
static present_record_t records[256];

enum { MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_SPIRAM=4 };
static unsigned heap_caps_get_free_size(unsigned caps) { (void)caps; return 123456u; }
static void host_log(const char *tag,const char *format,...)
{ (void)tag; (void)format; ++log_calls; }
#define ESP_LOGI(tag, ...) host_log(tag,__VA_ARGS__)
static lv_indev_t *lv_indev_get_act(void) { return input_available ? &input_device : NULL; }
static void lv_indev_get_point(lv_indev_t *device, lv_point_t *point)
{ assert(device == &input_device); *point = input_point; }
static int lv_event_get_code(lv_event_t *event) { return event->code; }
static int64_t esp_timer_get_time(void) { return fake_now_us; }
static void lv_timer_set_period(lv_timer_t *timer, uint32_t period)
{ (void)timer; timer_period=period; }
static void process_serial(int64_t now_us) { ++serial_calls; serial_time_us = now_us; }
static void save_settings(int64_t now_us)
{
    ++save_calls; save_time_us = now_us;
    s_last_save_us = now_us; s_settings_dirty = false;
}

static uint64_t render_hash(void)
{
    cd_render_frame(frame, background, &s_model);
    if(cd_welcome_active(&s_welcome))cd_welcome_render(frame,background,&s_model,&s_welcome);
    uint64_t hash = UINT64_C(1469598103934665603);
    for (unsigned i=0; i<CD_WIDTH*CD_HEIGHT; ++i) {
        hash ^= frame[i]; hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void present_frame(void)
{
    assert(present_calls < sizeof(records)/sizeof(records[0]));
    records[present_calls].model = s_model;
    records[present_calls].pointer = cd_ui_has_pointer();
    records[present_calls].hash = render_hash();
    ++present_calls; ++s_frames;
}
'''


SUFFIX = r'''
static void reset(void)
{
    cd_model_init(&s_model, INT64_C(1791216000));
    /* A fresh boot resets both the model and its independent art clock. */
    cd_ui_visual_tick((300000u-cd_ui_life_ms())%300000u);
    cd_ui_pointer(-100, -100, false);
    fake_now_us = INT64_C(10000000);
    s_last_tick_us = s_last_save_us = s_last_log_us = fake_now_us;
    s_last_change_us = s_visual_hold_until_us = 0;
    s_nvs_ready = s_settings_dirty = false;
    s_frames = s_render_us = 0;
    memset(&s_welcome,0,sizeof(s_welcome));
    s_welcome.phase=CD_WELCOME_DONE;
    s_welcome_contact=false;
    timer_period=80;
    input_available = true;
    log_calls = serial_calls = save_calls = present_calls = 0;
    serial_time_us = save_time_us = 0;
    memset(records, 0, sizeof(records));
}

static void send_event(int code, int x, int y)
{
    /* Exercise the actual callback's 800x480 -> 400x240 coordinate mapping. */
    input_point.x = x * 2; input_point.y = y * 2;
    lv_event_t event = {.code = code};
    touch_event(&event);
}

static void advance(unsigned delta_ms)
{
    fake_now_us += (int64_t)delta_ms * 1000;
    tick_event(NULL);
}

static const struct { cd_popup_t popup; int x, y, close_x, close_y; } entrances[] = {
    {CD_POPUP_ANNIVERSARY, CD_FRAME_X+CD_FRAME_W/2, CD_FRAME_Y+CD_FRAME_H/2, 303, 64},
    {CD_POPUP_TODO, CD_BOOK_X+CD_BOOK_W/2, CD_BOOK_Y+CD_BOOK_H/2, 287, 62},
    {CD_POPUP_FOCUS, CD_TOMATO_X+CD_TOMATO_W/2, CD_TOMATO_Y+CD_TOMATO_H/2, 270, 73},
    {CD_POPUP_WEATHER, CD_WINDOW_X+CD_WINDOW_W/2, CD_WINDOW_Y+CD_WINDOW_H/2, 276, 63},
    {CD_POPUP_MAC, CD_MAC_X+CD_MAC_W/2, CD_MAC_Y+CD_MAC_H/2, 276, 60},
    {CD_POPUP_SETTINGS, CD_GEAR_X+8, CD_GEAR_Y+7, 273, 83}
};

static void open_focus(void)
{
    send_event(LV_EVENT_PRESSED, CD_TOMATO_X+CD_TOMATO_W/2, CD_TOMATO_Y+CD_TOMATO_H/2);
}

static void open_frame(void)
{
    send_event(LV_EVENT_PRESSED, CD_FRAME_X+CD_FRAME_W/2, CD_FRAME_Y+CD_FRAME_H/2);
}

static void open_settings(void)
{
    send_event(LV_EVENT_PRESSED, CD_GEAR_X+8, CD_GEAR_Y+7);
}

static void test_open_and_close(void)
{
    for (unsigned i=0; i<sizeof(entrances)/sizeof(entrances[0]); ++i) {
        reset();
        cd_model_t expected = s_model;
        expected.popup = entrances[i].popup;
        send_event(LV_EVENT_PRESSED, entrances[i].x, entrances[i].y);
        assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
        assert(present_calls == 0 && s_visual_hold_until_us == 0);
        send_event(LV_EVENT_RELEASED, entrances[i].x, entrances[i].y);
        assert(!cd_ui_has_pointer());
        const uint64_t up_hash = render_hash();
        const cd_model_t before = s_model;
        send_event(LV_EVENT_PRESSED, entrances[i].close_x, entrances[i].close_y);
        assert(present_calls == 1 && records[0].pointer);
        assert(memcmp(&records[0].model, &before, sizeof(before)) == 0);
        assert(records[0].hash != up_hash);
        assert(s_model.popup == CD_POPUP_NONE);
        assert(s_visual_hold_until_us == fake_now_us + 80000);
        send_event(LV_EVENT_RELEASED, entrances[i].close_x, entrances[i].close_y);
        assert(!cd_ui_has_pointer());
        advance(79);
        assert(present_calls == 1 && serial_calls == 1);
        advance(1);
        assert(present_calls == 2 && records[1].model.popup == CD_POPUP_NONE);
        assert(records[1].hash == render_hash());
    }
}

static void test_start_hold(void)
{
    reset();
    open_focus();
    send_event(LV_EVENT_RELEASED, CD_TOMATO_X+CD_TOMATO_W/2, CD_TOMATO_Y+CD_TOMATO_H/2);
    const cd_model_t before = s_model;
    const uint64_t up_hash = render_hash();
    send_event(LV_EVENT_PRESSED, 160, 151);
    assert(present_calls == 1 && records[0].pointer);
    assert(records[0].model.popup == CD_POPUP_FOCUS && !records[0].model.focus_running);
    assert(memcmp(&records[0].model, &before, sizeof(before)) == 0);
    assert(records[0].hash != up_hash);
    assert(s_model.popup == CD_POPUP_NONE && s_model.focus_running);
    assert(s_visual_hold_until_us == fake_now_us + 80000);
    /* Make persistence due during the visual hold, while clocks keep running. */
    s_nvs_ready = s_settings_dirty = true;
    s_last_change_us = fake_now_us - 2000000;
    advance(40);
    assert(present_calls == 1 && serial_calls == 1 && save_calls == 1);
    assert(serial_time_us == fake_now_us && save_time_us == fake_now_us);
    assert(s_model.focus_remaining_ms == CD_FOCUS_DURATION_MS - 40);
    assert(s_model.clock_fraction_ms == 40 && !s_settings_dirty);
    advance(39);
    assert(present_calls == 1 && serial_calls == 2);
    assert(s_model.focus_remaining_ms == CD_FOCUS_DURATION_MS - 79);
    advance(1);
    assert(present_calls == 2 && serial_calls == 3);
    assert(records[1].model.popup == CD_POPUP_NONE && records[1].model.focus_running);
    assert(records[1].model.focus_remaining_ms == CD_FOCUS_DURATION_MS - 80);
    assert(records[1].hash == render_hash());
    assert(s_model.cats[0].state_elapsed_ms > before.cats[0].state_elapsed_ms);
    advance(80);
    assert(!cd_ui_has_pointer()); /* Missing releases still expire visual state. */
}

static void test_replay_hold(void)
{
    reset();
    open_frame();
    send_event(LV_EVENT_RELEASED, CD_FRAME_X+CD_FRAME_W/2, CD_FRAME_Y+CD_FRAME_H/2);
    const cd_model_t before = s_model;
    cd_model_t expected = s_model;
    cd_model_replay_anniversary(&expected);
    const uint64_t up_hash = render_hash();
    send_event(LV_EVENT_PRESSED, 200, 167);
    assert(present_calls == 1 && records[0].pointer && records[0].hash != up_hash);
    assert(memcmp(&records[0].model, &before, sizeof(before)) == 0);
    assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
    assert(s_model.celebration_active && s_model.celebration_preview);
    assert(s_visual_hold_until_us == fake_now_us + 80000);
    advance(79);
    assert(present_calls == 1 && s_model.celebration_active);
    advance(1);
    assert(present_calls == 2 && records[1].model.celebration_active);
}

static void test_focus_actions(void)
{
    reset();
    s_model.popup = CD_POPUP_FOCUS;
    s_model.focus_running = true;
    s_model.focus_remaining_ms = 60000;
    cd_model_t expected = s_model;
    expected.focus_running = false;
    send_event(LV_EVENT_PRESSED, 160, 151);
    assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
    assert(present_calls == 1 && records[0].model.focus_running);
    assert(s_visual_hold_until_us == 0);
    send_event(LV_EVENT_RELEASED, 160, 151);
    advance(1000);
    assert(s_model.focus_remaining_ms == 60000);
    expected = s_model;
    cd_model_start_focus(&expected);
    send_event(LV_EVENT_PRESSED, 160, 151);
    assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
    assert(records[2].model.popup == CD_POPUP_FOCUS && !records[2].model.focus_running);
    advance(80);
    assert(s_model.focus_remaining_ms == 59920);
    open_focus();
    assert(s_model.popup == CD_POPUP_FOCUS);
    expected = s_model;
    expected.focus_running = false;
    expected.focus_remaining_ms = CD_FOCUS_DURATION_MS;
    send_event(LV_EVENT_PRESSED, 237, 151);
    assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
    assert(s_visual_hold_until_us == 0 && s_model.popup == CD_POPUP_FOCUS);
}

static void test_todo_actions(void)
{
    reset();
    send_event(LV_EVENT_PRESSED, CD_BOOK_X+CD_BOOK_W/2, CD_BOOK_Y+CD_BOOK_H/2);
    cd_model_t expected = s_model;
    cd_model_toggle_todo(&expected, 0);
    send_event(LV_EVENT_PRESSED, 140, 84);
    assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
    assert(present_calls == 0 && s_settings_dirty && s_last_change_us == fake_now_us);
    while (s_model.todo_count < CD_TODO_CAPACITY) {
        expected = s_model;
        cd_model_add_todo(&expected);
        const unsigned before_present = present_calls;
        const unsigned before_count = s_model.todo_count;
        send_event(LV_EVENT_PRESSED, 266, 172);
        assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
        assert(present_calls == before_present + 1 && records[before_present].pointer);
        assert(records[before_present].model.todo_count == before_count);
        assert(s_visual_hold_until_us == 0);
    }
    expected = s_model;
    const unsigned before_present = present_calls;
    send_event(LV_EVENT_PRESSED, 266, 172);
    assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
    assert(present_calls == before_present);
}

static void test_settings_debounce(void)
{
    reset();
    s_nvs_ready = true;
    open_settings();
    send_event(LV_EVENT_PRESSED, 130, 138);
    assert(s_model.brightness == 20 && s_settings_dirty);
    fake_now_us += 100000;
    send_event(LV_EVENT_PRESSING, 270, 138);
    assert(s_model.brightness == 100 && s_last_change_us == fake_now_us);
    const int64_t changed = s_last_change_us;
    send_event(LV_EVENT_RELEASED, 270, 138);
    assert(!cd_ui_has_pointer() && s_last_change_us == changed);
    advance(1999);
    assert(save_calls == 0 && s_settings_dirty);
    advance(1);
    assert(save_calls == 1 && save_time_us == changed + 2000000 && !s_settings_dirty);
    assert(s_model.brightness == 100);
    advance(59999);
    assert(save_calls == 1);
    advance(1);
    assert(save_calls == 2); /* The unchanged clock still saves once per minute. */
}

static void test_new_home_hud(void)
{
    reset();
    s_model.focus_running = true;
    s_model.focus_remaining_ms = 60000;
    cd_model_t expected = s_model;
    expected.popup = CD_POPUP_FOCUS;
    send_event(LV_EVENT_PRESSED, CD_FOCUS_HUD_X+18, CD_FOCUS_HUD_Y+4);
    assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
    assert(present_calls == 0 && s_visual_hold_until_us == 0);
    send_event(LV_EVENT_RELEASED, CD_FOCUS_HUD_X+18, CD_FOCUS_HUD_Y+4);
    assert(!cd_ui_has_pointer());
}

static void test_settings_secondary_close(void)
{
    reset();
    open_settings();
    send_event(LV_EVENT_RELEASED, CD_GEAR_X+8, CD_GEAR_Y+7);
    const cd_model_t before = s_model;
    cd_model_t expected = before;
    expected.popup = CD_POPUP_NONE;
    const uint64_t up_hash = render_hash();
    /* This new visible close button overlaps the old slider drag height. The
       established press action must close it without changing brightness. */
    send_event(LV_EVENT_PRESSED, 256, 159);
    assert(present_calls == 1 && records[0].pointer && records[0].hash != up_hash);
    assert(memcmp(&records[0].model, &before, sizeof(before)) == 0);
    assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
    assert(!s_settings_dirty && s_model.brightness == 85);
    assert(s_visual_hold_until_us == fake_now_us+80000);
    send_event(LV_EVENT_PRESS_LOST, 256, 159);
    assert(!cd_ui_has_pointer());
    advance(79);
    assert(present_calls == 1 && serial_calls == 1);
    advance(1);
    assert(present_calls == 2 && records[1].model.popup == CD_POPUP_NONE);
}

static void test_anniversary_frame_direct_event(void)
{
    reset();
    cd_model_set_time(&s_model, INT64_C(1791302400));
    cd_model_tick(&s_model, 10000);
    assert(s_model.anniversary_today && s_model.popup == CD_POPUP_NONE && !s_model.celebration_active);
    cd_model_t expected = s_model;
    cd_model_replay_anniversary(&expected);
    open_frame();
    assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
    assert(s_model.popup == CD_POPUP_NONE && s_model.celebration_preview);
    assert(present_calls == 0 && s_visual_hold_until_us == 0 && cd_ui_has_pointer());
    advance(80);
    assert(present_calls == 1 && records[0].model.celebration_active);
    assert(records[0].model.popup == CD_POPUP_NONE);
}

static void test_release_and_cat(void)
{
    reset();
    cd_model_t expected = s_model;
    cd_model_tap_cat(&expected, CD_CAT_ORANGE);
    send_event(LV_EVENT_PRESSED, 132, 190);
    assert(memcmp(&s_model, &expected, sizeof(expected)) == 0);
    assert(present_calls == 0 && s_visual_hold_until_us == 0 && cd_ui_has_pointer());
    send_event(LV_EVENT_PRESS_LOST, 132, 190);
    assert(!cd_ui_has_pointer());
    for (unsigned i=0; i<5; ++i) send_event(LV_EVENT_PRESSED, 268, 190);
    assert(s_model.cats[CD_CAT_BLACK].bubble_ms == 1800);
    send_event(LV_EVENT_RELEASED, 268, 190);
    assert(!cd_ui_has_pointer());
    const cd_model_t before = s_model;
    input_available = false;
    open_settings();
    assert(memcmp(&s_model, &before, sizeof(before)) == 0);
    assert(!cd_ui_has_pointer());
    assert(s_render_us == 0); /* Fake clock intentionally has no render duration. */
}

static void test_welcome_callbacks(void)
{
    reset();
    cd_welcome_init(&s_welcome,&s_model);
    timer_period=CD_WELCOME_FRAME_MS;
    const cd_model_t untouched=s_model;
    send_event(LV_EVENT_PRESSED,CD_GEAR_X+8,CD_GEAR_Y+7);
    send_event(LV_EVENT_PRESSING,270,138);
    assert(s_welcome_contact && !s_welcome.press_latched);
    assert(memcmp(&s_model,&untouched,sizeof(s_model))==0);
    send_event(LV_EVENT_PRESS_LOST,270,138);
    assert(!s_welcome_contact);
    for(unsigned i=0;i<40;i++)advance(100);
    assert(s_welcome.phase==CD_WELCOME_WAITING);
    const cd_welcome_cat_pose_t orange=cd_welcome_cat_pose(&s_welcome,0);
    unsigned waiting_present=present_calls;
    for(unsigned i=0;i<120;i++)advance(100);
    assert(s_welcome.phase==CD_WELCOME_WAITING && !s_welcome.press_latched);
    assert(present_calls-waiting_present==12); /* Only 1-second prompt changes. */
    assert(cd_welcome_cat_pose(&s_welcome,0).x==orange.x);
    assert(serial_calls==160); /* Transport and normal model clocks stay alive. */
    assert(s_model.popup==CD_POPUP_NONE && !cd_ui_has_pointer());
    s_nvs_ready=s_settings_dirty=true;
    s_last_change_us=fake_now_us-2000000;
    send_event(LV_EVENT_PRESSED,0,0);
    assert(s_welcome.press_latched && s_welcome.phase==CD_WELCOME_ENTERING);
    for(unsigned i=0;i<17;i++) {
        send_event(LV_EVENT_PRESSED,CD_GEAR_X+8,CD_GEAR_Y+7);
        send_event(LV_EVENT_PRESSING,270,138);
        advance(100);
    }
    assert(s_welcome.phase==CD_WELCOME_DONE && timer_period==80);
    assert(s_model.popup==CD_POPUP_NONE && s_model.brightness==untouched.brightness);
    assert(save_calls==1 && serial_calls==177);
    cd_welcome_t stopped=s_welcome;
    advance(80);
    assert(memcmp(&stopped,&s_welcome,sizeof(stopped))==0);
    send_event(LV_EVENT_PRESSED,CD_GEAR_X+8,CD_GEAR_Y+7);
    send_event(LV_EVENT_PRESSING,270,138);
    assert(s_model.popup==CD_POPUP_NONE && s_welcome_contact);
    send_event(LV_EVENT_RELEASED,270,138);
    assert(!s_welcome_contact);
    open_settings();
    assert(s_model.popup==CD_POPUP_SETTINGS);
}

int main(void)
{
    cd_render_background(background);
    test_open_and_close(); test_start_hold(); test_replay_hold();
    test_focus_actions(); test_todo_actions(); test_settings_debounce();
    test_new_home_hud(); test_settings_secondary_close(); test_anniversary_frame_direct_event();
    test_release_and_cat();
    test_welcome_callbacks();
    puts("CatDesk feedback: original room actions/feedback/persistence plus actual welcome callbacks, early-touch rejection, indefinite waiting, one-shot press, held-contact isolation, 100ms-to-80ms timer switch and stopped scene passed.");
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    component = root / "firmware/components/catdesk_ui"
    source = (component / "catdesk_ui.c").read_text()
    start = source.index("static void touch_event")
    end = source.index("void catdesk_ui_start", start)
    callbacks = source[start:end]
    if "static void tick_event" not in callbacks:
        raise RuntimeError("tick_event is no longer adjacent; update extraction markers")
    with tempfile.TemporaryDirectory(prefix="catdesk-feedback-") as directory:
        work = Path(directory)
        generated, binary = work / "feedback_test.c", work / "feedback_test"
        generated.write_text(PREFIX + callbacks + SUFFIX)
        subprocess.run([
            args.cc, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
            "-I" + str(component / "include"), str(generated),
            *(str(component / name) for name in (
                "catdesk_model.c", "catdesk_render.c", "catdesk_couple.c", "catdesk_room.c", "catdesk_welcome.c", "pixel_ui.c", "catdesk_cats.c", "catdesk_font.c"
            )), "-o", str(binary)
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
