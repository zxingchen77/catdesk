/* Host check: cc -std=c11 -Wall -Wextra -Werror -pedantic \
 *   -Ifirmware/components/catdesk_ui/include tools/test_model.c \
 *   firmware/components/catdesk_ui/catdesk_model.c -o /tmp/catdesk-test-model
 */
#include "catdesk_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const int64_t OCT_6_MIDNIGHT = 1791216000;
static const int64_t OCT_6_LAST_SECOND = 1791302399;
static const int64_t OCT_7_MIDNIGHT = 1791302400;
static const int64_t OCT_8_MIDNIGHT = 1791388800;

static void assert_cat_bounds(const cd_model_t *model)
{
    static const unsigned frame_counts[] = {4, 2, 6, 4, 6};
    for (unsigned i = 0; i < CD_CAT_COUNT; ++i) {
        const cd_cat_t *cat = &model->cats[i];
        assert(cat->x >= 40 && cat->x <= 320);
        assert(cat->y == 170);
        assert(cat->target_x >= 40 && cat->target_x <= 320);
        assert(cat->direction == 1 || cat->direction == -1);
        assert((unsigned)cat->state <= CD_CAT_TAP);
        assert(cat->frame < frame_counts[cat->state]);
        assert(cat->tap_count <= 4);
    }
}

static void test_dates(void)
{
    cd_model_t model;
    cd_model_init(&model, 1728230400); /* 2024-10-07 00:00 China time. */
    assert(model.year == 2024 && model.month == 10 && model.day == 7);
    assert(model.elapsed_days == 0);
    assert(model.days_to_anniversary == 730);
    assert(!model.anniversary_today);

    cd_model_set_time(&model, OCT_6_MIDNIGHT);
    assert(model.year == 2026 && model.month == 10 && model.day == 6);
    assert(model.hour == 0 && model.minute == 0 && model.second == 0);
    assert(model.elapsed_days == 729);
    assert(model.days_to_anniversary == 1);
    assert(!model.celebration_active);

    cd_model_set_time(&model, OCT_6_LAST_SECOND);
    cd_model_tick(&model, 999);
    assert(model.day == 6 && model.hour == 23 && model.second == 59);
    assert(model.clock_fraction_ms == 999);
    cd_model_tick(&model, 1);
    assert(model.day == 7 && model.hour == 0 && model.second == 0);
    assert(model.epoch_utc == OCT_7_MIDNIGHT);
    assert(model.elapsed_days == 730);
    assert(model.days_to_anniversary == 0);
    assert(model.anniversary_today);
    assert(model.celebration_active);
    assert(model.celebration_phase == CD_CELEBRATION_APPROACH);

    cd_model_set_time(&model, OCT_8_MIDNIGHT);
    assert(model.elapsed_days == 731 && !model.anniversary_today);
    assert(model.days_to_anniversary == 0);
    assert(!model.celebration_active && !model.approach_active);

    cd_model_set_time(&model, 1709135999); /* Leap-year Feb 28, 23:59:59. */
    cd_model_tick(&model, 1000);
    assert(model.year == 2024 && model.month == 2 && model.day == 29);
    cd_model_set_time(&model, 1709222399);
    cd_model_tick(&model, 1000);
    assert(model.month == 3 && model.day == 1);
    cd_model_set_time(&model, 1740758399); /* Ordinary Feb 28. */
    cd_model_tick(&model, 1000);
    assert(model.year == 2025 && model.month == 3 && model.day == 1);

    cd_model_set_time(&model, -28801);
    assert(model.year == 1969 && model.month == 12 && model.day == 31);
    assert(model.hour == 23 && model.minute == 59 && model.second == 59);
    assert(model.elapsed_days == 0);
    cd_model_tick(&model, 1000);
    assert(model.year == 1970 && model.month == 1 && model.day == 1);
}

static void test_anniversary(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_7_MIDNIGHT);
    assert(model.celebration_active && model.approach_active);
    assert(model.cats[0].target_x == 166 && model.cats[1].target_x == 198);
    unsigned waited = 0;
    while (model.celebration_phase == CD_CELEBRATION_APPROACH) {
        cd_model_tick(&model, 50);
        waited += 50;
        assert(waited < 5000);
        assert_cat_bounds(&model);
    }
    assert(model.celebration_phase == CD_CELEBRATION_HEARTS);
    assert(model.cats[0].x == 166 && model.cats[1].x == 198);

    assert(model.cats[0].heart_ms != 0 && model.cats[1].heart_ms != 0);
    assert(model.celebration_ms == 5000);
    cd_model_tick(&model, 4999);
    assert(model.celebration_active);
    cd_model_tick(&model, 1);
    assert(!model.celebration_active);
    assert(model.celebration_phase == CD_CELEBRATION_NONE);

    for (unsigned i = 0; i < 200; ++i) {
        cd_model_tick(&model, 50);
        assert(!model.celebration_active);
    }
    /* Repeated synchronisation on the same date must not replay the gift. */
    cd_model_set_time(&model, OCT_7_MIDNIGHT + 100);
    assert(!model.celebration_active);
    model.popup = CD_POPUP_ANNIVERSARY;
    cd_model_tick(&model, 500);
    assert(!model.celebration_active);

    /* Petting during the approach pauses that cat, then resumes the walk. */
    cd_model_init(&model, OCT_7_MIDNIGHT);
    cd_model_tick(&model, 100);
    const int tapped_x = model.cats[0].x;
    cd_model_tap_cat(&model, CD_CAT_ORANGE);
    cd_model_tick(&model, 1000);
    assert(model.cats[0].x == tapped_x && model.cats[0].state == CD_CAT_TAP);
    cd_model_tick(&model, 5000);
    assert(model.celebration_phase == CD_CELEBRATION_HEARTS);
    assert(model.cats[0].x == 166 && model.cats[1].x == 198);

    cd_model_init(&model, OCT_6_MIDNIGHT);
    cd_model_replay_anniversary(&model);
    cd_model_tick(&model, 3000);
    assert(model.celebration_preview && model.celebration_active);
    assert(model.celebration_phase == CD_CELEBRATION_HEARTS);
    cd_model_tick(&model, 5000);
    assert(!model.celebration_active && !model.celebration_preview);

    cd_model_init(&model, OCT_6_LAST_SECOND);
    model.popup = CD_POPUP_WEATHER;
    cd_model_tick(&model, 1000);
    assert(model.anniversary_today && !model.celebration_active);
    cd_model_tick(&model, 10000);
    assert(!model.celebration_active);
    model.popup = CD_POPUP_NONE;
    cd_model_tick(&model, 50);
    assert(model.celebration_active && model.celebration_phase == CD_CELEBRATION_APPROACH);
}

static void test_taps(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6_MIDNIGHT);
    cd_model_tap_cat(&model, CD_CAT_ORANGE);
    assert(model.cats[0].state == CD_CAT_TAP && model.cats[0].frame == 0);
    assert(model.cats[1].state == CD_CAT_IDLE);
    cd_model_tick(&model, 400);
    assert(model.cats[0].state == CD_CAT_TAP && model.cats[0].frame == 2);
    assert(model.cats[0].heart_ms != 0);
    cd_model_tick(&model, 800);
    assert(model.cats[0].state == CD_CAT_IDLE);

    for (unsigned i = 0; i < 5; ++i) {
        cd_model_tap_cat(&model, CD_CAT_BLACK);
        if (i != 4) cd_model_tick(&model, 100);
    }
    assert(model.cats[1].bubble_ms == 1800);
    cd_model_tick(&model, 1750);
    assert(model.cats[1].bubble_ms == 50);
    cd_model_tick(&model, 50);
    assert(model.cats[1].bubble_ms == 0);

    for (unsigned i = 0; i < 5; ++i) {
        cd_model_tap_cat(&model, CD_CAT_BLACK);
        cd_model_tick(&model, 800);
    }
    assert(model.cats[1].bubble_ms == 0); /* Slow clicks aren't a rapid streak. */
    const cd_model_t snapshot = model;
    cd_model_tap_cat(&model, (cd_cat_kind_t)2);
    cd_model_tap_cat(&model, (cd_cat_kind_t)-1);
    assert(memcmp(&snapshot, &model, sizeof(model)) == 0);
}

static void test_focus(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6_MIDNIGHT);
    model.popup = CD_POPUP_FOCUS;
    cd_model_start_focus(&model);
    assert(model.focus_running);
    assert(model.focus_remaining_ms == 1500000);
    assert(model.popup == CD_POPUP_NONE);
    cd_model_tick(&model, 1000);
    assert(model.focus_remaining_ms == 1499000);
    model.popup = CD_POPUP_WEATHER;
    cd_model_tick(&model, 1000);
    assert(model.focus_remaining_ms == 1498000); /* Popup doesn't pause focus. */
    cd_model_start_focus(&model);
    assert(model.focus_remaining_ms == 1498000); /* Start is idempotent. */
    assert(model.popup == CD_POPUP_NONE);
    model.focus_running = false;
    cd_model_tick(&model, 1000);
    assert(model.focus_remaining_ms == 1498000);
    cd_model_start_focus(&model);
    assert(model.focus_running && model.focus_remaining_ms == 1498000);
    cd_model_tick(&model, 1497999);
    assert(model.focus_running && model.focus_remaining_ms == 1);
    cd_model_tick(&model, 1);
    assert(!model.focus_running && model.focus_remaining_ms == 0);
    assert(model.notice_ms == 6000);
    assert(model.cats[0].state == CD_CAT_TAP && model.cats[1].state == CD_CAT_TAP);
    cd_model_tick(&model, 6000);
    assert(model.notice_ms == 0);
    cd_model_start_focus(&model);
    assert(model.focus_running && model.focus_remaining_ms == CD_FOCUS_DURATION_MS);
}

static void test_todos(void)
{
    cd_model_t model;
    cd_model_init(&model, OCT_6_MIDNIGHT);
    assert(model.todo_count == 3 && model.todo_checked == 0);
    assert(model.brightness == 85);
    cd_model_toggle_todo(&model, 1);
    assert(model.todo_checked == 2);
    cd_model_toggle_todo(&model, 1);
    assert(model.todo_checked == 0);
    cd_model_toggle_todo(&model, 3);
    cd_model_toggle_todo(&model, 32);
    assert(model.todo_checked == 0);
    for (unsigned i = 0; i < 10; ++i) cd_model_add_todo(&model);
    assert(model.todo_count == CD_TODO_CAPACITY);
    for (unsigned i = 0; i < CD_TODO_CAPACITY; ++i) {
        assert(model.todo_ids[i] == i);
        cd_model_toggle_todo(&model, i);
    }
    assert(model.todo_checked == 31);
    cd_model_add_todo(&model);
    assert(model.todo_checked == 31 && model.todo_count == 5);
}

static void test_animation(void)
{
    cd_model_t a, b;
    cd_model_init(&a, OCT_6_MIDNIGHT);
    cd_model_init(&b, OCT_6_MIDNIGHT);
    unsigned seen_states = 0;
    bool saw_social_heart = false;
    for (unsigned i = 0; i < 72000; ++i) { /* One hour in device-sized ticks. */
        cd_model_tick(&a, 50);
        cd_model_tick(&b, 50);
        assert_cat_bounds(&a);
        assert(memcmp(&a, &b, sizeof(a)) == 0);
        for (unsigned cat = 0; cat < CD_CAT_COUNT; ++cat) {
            seen_states |= 1u << a.cats[cat].state;
        }
        if (a.cats[0].heart_ms != 0 && a.cats[1].heart_ms != 0) {
            saw_social_heart = true;
        }
    }
    assert((seen_states & 15u) == 15u); /* idle, blink, walk and sleep. */
    assert(saw_social_heart);
    assert(a.hour == 1 && a.minute == 0 && a.second == 0);
    cd_model_tick(&a, UINT32_MAX); /* Large catch-up is bounded and clock-safe. */
    assert_cat_bounds(&a);
    assert(a.epoch_utc == OCT_6_MIDNIGHT + 3600 + UINT32_MAX / 1000);
    assert(a.clock_fraction_ms == UINT32_MAX % 1000);
}

int main(void)
{
    test_dates();
    test_anniversary();
    test_taps();
    test_focus();
    test_todos();
    test_animation();
    puts("CatDesk model: date, anniversary, pets, focus, todo and bounds checks passed.");
    return 0;
}
