#!/usr/bin/env python3
"""Check the candidate room through its actual renderer and touch routing.

Compiles a temporary host driver with ASan/UBSan. No serial devices, Helper,
ESP-IDF build directories, release binaries, or installed services are used.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


DRIVER = r'''
#include "catdesk_render.h"
#include "catdesk_couple.h"
#include "catdesk_room.h"
#include "catdesk_cats.h"
#include "catdesk_layout.h"
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { LIFE_CYCLE = 300000, PIXELS = CD_WIDTH * CD_HEIGHT };
static uint16_t background[PIXELS], frame[PIXELS], sprite[PIXELS], saved[PIXELS];
static unsigned rendered, touch_checks, prop_checks;
static const int64_t OCT_6 = INT64_C(1791216000);
static const int64_t OCT_7 = INT64_C(1791302400);

static void life_at(uint32_t desired)
{
    desired %= LIFE_CYCLE;
    const uint32_t current = cd_ui_life_ms();
    assert(current < LIFE_CYCLE);
    cd_ui_visual_tick((desired + LIFE_CYCLE - current) % LIFE_CYCLE);
    assert(cd_ui_life_ms() == desired);
}

static uint64_t hash_region(const uint16_t *pixels, int x, int y, int w, int h)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    assert(x >= 0 && y >= 0 && x + w <= CD_WIDTH && y + h <= CD_HEIGHT);
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col) {
            hash ^= pixels[row * CD_WIDTH + col];
            hash *= UINT64_C(1099511628211);
        }
    return hash;
}

static void render_pure(const cd_model_t *model)
{
    unsigned char previous[sizeof(*model)];
    memcpy(previous, model, sizeof(*model));
    const uint64_t bg_hash = hash_region(background, 0, 0, CD_WIDTH, CD_HEIGHT);
    cd_render_frame(frame, background, model);
    assert(memcmp(previous, model, sizeof(*model)) == 0);
    assert(hash_region(background, 0, 0, CD_WIDTH, CD_HEIGHT) == bg_hash);
    ++rendered;
}

static cd_room_pet_pose_t pose_pure(const cd_model_t *model, unsigned kind)
{
    unsigned char previous[sizeof(*model)];
    memcpy(previous, model, sizeof(*model));
    cd_room_pet_pose_t pose = cd_room_pet_pose(model, kind, cd_ui_life_ms());
    assert(memcmp(previous, model, sizeof(*model)) == 0);
    assert(pose.x >= 0 && pose.x + CD_CAT_WIDTH <= CD_WIDTH);
    assert(pose.y >= 0 && pose.y + CD_CAT_HEIGHT <= CD_HEIGHT);
    assert(pose.state >= CD_CAT_IDLE && pose.state <= CD_CAT_TAP);
    static const unsigned lengths[] = {4, 2, 6, 4, 6};
    assert(pose.frame >= 0 && (unsigned)pose.frame < lengths[pose.state]);
    assert(pose.direction == -1 || pose.direction == 1);
    assert(pose.activity >= CD_ROOM_ACTIVITY_NONE && pose.activity <= CD_ROOM_ACTIVITY_CUDDLE);
    return pose;
}

static bool same_pose(cd_room_pet_pose_t a, cd_room_pet_pose_t b)
{
    return a.x == b.x && a.y == b.y && a.state == b.state && a.frame == b.frame
        && a.direction == b.direction && a.activity == b.activity;
}

static void draw_pose(cd_room_pet_pose_t pose, unsigned kind)
{
    memset(sprite, 0, sizeof(sprite));
    int jump = 0;
    if (kind == CD_CAT_ORANGE && pose.state == CD_CAT_TAP) {
        const int jumps[] = {0, 0, 8, 13, 6, 0};
        jump = jumps[pose.frame % 6];
    }
    catdesk_draw_cat(sprite, CD_WIDTH, pose.x, pose.y - jump, (int)kind,
                     pose.state, pose.frame, pose.direction < 0);
}

static unsigned visible_sprite_pixels(cd_room_pet_pose_t pose, unsigned kind)
{
    draw_pose(pose, kind);
    unsigned visible = 0;
    for (unsigned i = 0; i < PIXELS; ++i)
        if (sprite[i] && sprite[i] == frame[i]) ++visible;
    /* A cat partially inside the box may have its lower body occluded. */
    if (visible < 24) {
        fprintf(stderr, "Invisible projected cat: life=%" PRIu32 " kind=%u activity=%d x=%d y=%d state=%d frame=%d matched=%u\n",
                cd_ui_life_ms(), kind, pose.activity, pose.x, pose.y, pose.state, pose.frame, visible);
        assert(visible >= 24);
    }
    return visible;
}

static void test_periods(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6);
    life_at(0);
    uint64_t sky[3], lamp[3];
    const unsigned hours[] = {12, 18, 23};
    const cd_room_period_t periods[] = {CD_ROOM_DAY, CD_ROOM_DUSK, CD_ROOM_NIGHT};
    for (unsigned i = 0; i < 3; ++i) {
        cd_model_set_time(&model, OCT_6 + hours[i] * 3600);
        model.clock_synced = true;
        assert(cd_room_period(&model) == periods[i]);
        render_pure(&model);
        sky[i] = hash_region(frame, CD_WINDOW_X + 5, CD_WINDOW_Y + 5, 46, 24);
        lamp[i] = hash_region(frame, 110, 108, 36, 44);
    }
    assert(sky[0] != sky[1] && sky[1] != sky[2] && sky[0] != sky[2]);
    assert(lamp[0] != lamp[1] && lamp[0] != lamp[2]);
    for (unsigned hour = 0; hour < 24; ++hour) {
        cd_model_set_time(&model, OCT_6 + hour * 3600);
        model.clock_synced = true;
        const cd_room_period_t expected = hour >= 6 && hour <= 16 ? CD_ROOM_DAY
            : hour >= 17 && hour <= 19 ? CD_ROOM_DUSK : CD_ROOM_NIGHT;
        assert(cd_room_period(&model) == expected);
        render_pure(&model);
        model.clock_synced = false;
        assert(cd_room_period(&model) == CD_ROOM_DUSK);
        render_pure(&model);
        assert(hash_region(frame, CD_WINDOW_X + 5, CD_WINDOW_Y + 5, 46, 24) == sky[1]);
    }
    const unsigned transitions[] = {6, 17, 20};
    for (unsigned i = 0; i < 3; ++i) {
        cd_model_set_time(&model, OCT_6 + transitions[i] * 3600 - 1);
        model.clock_synced = true;
        const cd_room_period_t before = cd_room_period(&model);
        render_pure(&model);
        cd_model_tick(&model, 1000);
        assert(cd_room_period(&model) != before);
        render_pure(&model);
    }
}

static void test_five_minute_renderer(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6 + 12 * 3600);
    model.clock_synced = true;
    unsigned seen = 0, elevated = 0;
    uint64_t first = 0;
    bool changed = false;
    for (uint32_t ms = 0; ms <= LIFE_CYCLE; ms += 250) {
        life_at(ms);
        render_pure(&model);
        const uint64_t current = hash_region(frame, 0, 0, CD_WIDTH, CD_HEIGHT);
        if (ms == 0) first = current;
        else if (current != first) changed = true;
        for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
            const cd_room_pet_pose_t pose = pose_pure(&model, kind);
            seen |= 1u << pose.activity;
            if (pose.y < model.cats[kind].y) ++elevated;
            (void)visible_sprite_pixels(pose, kind);
            const cd_room_pet_pose_t wrapped = cd_room_pet_pose(&model, kind, ms + LIFE_CYCLE);
            assert(same_pose(pose, wrapped));
        }
    }
    assert(changed && elevated > 100);
    assert(seen == (1u << (CD_ROOM_ACTIVITY_CUDDLE + 1)) - 1u);
    /* Rendering one complete loop cannot alter date, cats, timers, or data. */
    assert(model.epoch_utc == OCT_6 + 12 * 3600 && model.popup == CD_POPUP_NONE);
    assert(model.cats[0].x == 112 && model.cats[1].x == 248);
}

static void test_visible_taps(const cd_model_t *model, unsigned kind)
{
    const cd_room_pet_pose_t before = pose_pure(model, kind);
    draw_pose(before, kind);
    cd_model_t expected = *model;
    cd_model_tap_cat(&expected, (cd_cat_kind_t)kind);
    for (int y = before.y; y < before.y + CD_CAT_HEIGHT; ++y)
        for (int x = before.x; x < before.x + CD_CAT_WIDTH; ++x) {
            const unsigned index = (unsigned)y * CD_WIDTH + (unsigned)x;
            if (!sprite[index] || sprite[index] != frame[index]) continue;
            cd_model_t tapped = *model;
            cd_ui_press(&tapped, x, y);
            if (memcmp(&tapped, &expected, sizeof(tapped)) != 0) continue;
            const cd_room_pet_pose_t reaction = pose_pure(&tapped, kind);
            assert(reaction.x == before.x && reaction.y == before.y);
            assert(reaction.state == CD_CAT_TAP && reaction.frame == 0);
            render_pure(&tapped);
            (void)visible_sprite_pixels(reaction, kind);
            ++touch_checks;
            return;
        }
    fprintf(stderr, "No visible touch route: life=%" PRIu32 " kind=%u activity=%d x=%d y=%d\n",
            cd_ui_life_ms(), kind, before.activity, before.x, before.y);
    assert(false);
}

static bool visible_cat_at(const cd_model_t *model, int x, int y)
{
    const unsigned index = (unsigned)y * CD_WIDTH + (unsigned)x;
    for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
        draw_pose(pose_pure(model, kind), kind);
        if (sprite[index] && sprite[index] == frame[index]) return true;
    }
    return false;
}

static void test_prop_access(const cd_model_t *model, cd_popup_t popup,
                             int left, int top, int width, int height)
{
    for (int y = top; y < top + height; ++y)
        for (int x = left; x < left + width; ++x) {
            if (visible_cat_at(model, x, y)) continue;
            cd_model_t pressed = *model;
            cd_ui_press(&pressed, x, y);
            if (pressed.popup == popup) {
                /* Opening furniture changes only the popup, not core cats. */
                pressed.popup = model->popup;
                assert(memcmp(&pressed, model, sizeof(pressed)) == 0);
                ++prop_checks;
                return;
            }
        }
    fprintf(stderr, "Prop inaccessible outside cat: life=%" PRIu32 " popup=%d\n",
            cd_ui_life_ms(), popup);
    assert(false);
}

static void test_projected_input(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6 + 12 * 3600);
    model.clock_synced = true;
    for (uint32_t ms = 0; ms < LIFE_CYCLE; ms += 5000) {
        life_at(ms);
        for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
            render_pure(&model);
            test_visible_taps(&model, kind);
        }
        render_pure(&model);
        test_prop_access(&model, CD_POPUP_MAC, CD_MAC_X, CD_MAC_Y, CD_MAC_W, CD_MAC_H);
        test_prop_access(&model, CD_POPUP_WEATHER, CD_WINDOW_X, CD_WINDOW_Y, CD_WINDOW_W, CD_WINDOW_H);
        test_prop_access(&model, CD_POPUP_TODO, CD_BOOK_X, CD_BOOK_Y, CD_BOOK_W, CD_BOOK_H);
        test_prop_access(&model, CD_POPUP_FOCUS, CD_TOMATO_X, CD_TOMATO_Y, CD_TOMATO_W, CD_TOMATO_H);
        test_prop_access(&model, CD_POPUP_ANNIVERSARY, CD_FRAME_X, CD_FRAME_Y, CD_FRAME_W, CD_FRAME_H);
        test_prop_access(&model, CD_POPUP_SETTINGS, CD_GEAR_X, CD_GEAR_Y, 13, 13);
    }
}

static void test_running_model(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6 + 12 * 3600);
    model.clock_synced = true;
    life_at(0);
    unsigned activity_seen = 0, native_approaches = 0;
    for (unsigned step = 0; step < LIFE_CYCLE / 250; ++step) {
        cd_model_tick(&model, 250);
        cd_ui_visual_tick(250);
        render_pure(&model);
        if (model.approach_active) ++native_approaches;
        for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
            const cd_room_pet_pose_t pose = pose_pure(&model, kind);
            activity_seen |= 1u << pose.activity;
            (void)visible_sprite_pixels(pose, kind);
            if (model.approach_active && pose.activity == CD_ROOM_ACTIVITY_NONE) {
                assert(pose.x == model.cats[kind].x && pose.y == model.cats[kind].y);
            }
        }
    }
    assert(model.epoch_utc == OCT_6 + 12 * 3600 + 300);
    assert(cd_ui_life_ms() == 0 && native_approaches > 0);
    assert(activity_seen == (1u << (CD_ROOM_ACTIVITY_CUDDLE + 1)) - 1u);
}

static void test_celebration_priority(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_7);
    model.clock_synced = true;
    for (unsigned phase = 0; phase < 2; ++phase) {
        if (phase == 1) {
            cd_model_tick(&model, 3000);
            assert(model.celebration_phase == CD_CELEBRATION_HEARTS);
        }
        assert(model.celebration_active);
        life_at(0);
        render_pure(&model);
        memcpy(saved, frame, sizeof(saved));
        for (uint32_t ms = 0; ms < LIFE_CYCLE; ms += 3000) {
            life_at(ms);
            render_pure(&model);
            assert(memcmp(saved, frame, sizeof(saved)) == 0);
            for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
                const cd_room_pet_pose_t pose = pose_pure(&model, kind);
                const cd_cat_t *native = &model.cats[kind];
                assert(pose.x == native->x && pose.y == native->y);
                assert(pose.state == (int)native->state && pose.frame == native->frame);
                assert(pose.direction == native->direction && pose.activity == CD_ROOM_ACTIVITY_NONE);
            }
        }
    }
}

static void test_monotonic_clock(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6 + 12 * 3600);
    life_at(90001);
    const uint32_t life = cd_ui_life_ms();
    const cd_room_pet_pose_t poses[] = {pose_pure(&model, 0), pose_pure(&model, 1)};
    for (unsigned hour = 0; hour < 24; ++hour) {
        cd_model_set_time(&model, OCT_6 + hour * 3600);
        assert(cd_ui_life_ms() == life);
        for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind)
            assert(same_pose(poses[kind], pose_pure(&model, kind)));
    }
    const uint32_t deltas[] = {4000, 200000, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(deltas) / sizeof(deltas[0]); ++i) {
        const uint32_t previous = cd_ui_life_ms();
        cd_ui_visual_tick(deltas[i]);
        assert(cd_ui_life_ms() == ((uint64_t)previous + deltas[i]) % LIFE_CYCLE);
    }
}

int main(void)
{
    cd_render_background(background);
    cd_ui_pointer(-100, -100, false);
    test_periods();
    test_five_minute_renderer();
    test_projected_input();
    test_running_model();
    test_celebration_priority();
    test_monotonic_clock();
    printf("CatDesk room integration: %u real frames, 300-second choreography, all24 synced/unsynced hours, projected cats visible/tappable (%u), all6 props accessible (%u), immutable model/background, anniversary priority and monotonic clock PASS.\n",
           rendered, touch_checks, prop_checks);
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    component = root / "firmware/components/catdesk_ui"
    with tempfile.TemporaryDirectory(prefix="catdesk-room-integration-") as directory:
        work = Path(directory)
        source = work / "room_integration.c"
        binary = work / "room_integration"
        source.write_text(DRIVER)
        command = [
            args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
            "-I" + str(component / "include"), str(source),
            *(str(component / name) for name in (
                "catdesk_model.c", "catdesk_room.c", "catdesk_render.c", "catdesk_couple.c",
                "catdesk_cats.c", "catdesk_font.c", "pixel_ui.c",
            )), "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
