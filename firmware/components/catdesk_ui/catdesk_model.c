#include "catdesk_model.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

enum {
    CAT_MIN_X = 40,
    CAT_MAX_X = 320,
    CAT_FLOOR_Y = 170,
    ANIMATION_STEP_MS = 50,
    TAP_DURATION_MS = 1200,
    RAPID_TAP_WINDOW_MS = 750,
    BUBBLE_DURATION_MS = 1800,
    CELEBRATION_DURATION_MS = 5000
};

static uint32_t subtract_ms(uint32_t value, uint32_t delta)
{
    return value > delta ? value - delta : 0;
}

static uint32_t random_next(cd_model_t *model)
{
    uint32_t x = model->random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    model->random_state = x;
    return x;
}

/* Gregorian conversion uses integer arithmetic; day zero is 1970-01-01. */
static int32_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned shifted_month = month > 2 ? month - 3 : month + 9;
    const unsigned doy = (153 * shifted_month + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

static void civil_from_days(int32_t days, int *year, int *month, int *day)
{
    int64_t z = (int64_t)days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    *year = y;
    *month = (int)m;
    *day = (int)d;
}

static void cat_enter(cd_cat_t *cat, cd_cat_state_t state, uint32_t duration)
{
    cat->state = state;
    cat->frame = 0;
    cat->state_elapsed_ms = 0;
    cat->state_remaining_ms = duration;
    cat->movement_ms = 0;
}

static void start_approach(cd_model_t *model, bool anniversary)
{
    model->approach_active = true;
    if (anniversary) {
        model->celebration_active = true;
        model->celebration_phase = CD_CELEBRATION_APPROACH;
        model->celebration_ms = 0;
        model->celebrated_local_day = model->local_day;
    }
    for (unsigned i = 0; i < CD_CAT_COUNT; ++i) {
        cd_cat_t *cat = &model->cats[i];
        cat->target_x = i == CD_CAT_ORANGE ? 166 : 198;
        cat->direction = cat->target_x >= cat->x ? 1 : -1;
        cat_enter(cat, CD_CAT_WALK, 10000);
    }
}

static void refresh_clock(cd_model_t *model)
{
    /* Shanghai time is fixed UTC+8, with no timezone library on the device. */
    int64_t local_seconds = model->epoch_utc + 8 * 60 * 60;
    int64_t days = local_seconds / 86400;
    int64_t seconds = local_seconds % 86400;
    if (seconds < 0) {
        seconds += 86400;
        --days;
    }
    model->local_day = (int32_t)days;
    civil_from_days(model->local_day, &model->year, &model->month, &model->day);
    model->hour = (int)(seconds / 3600);
    model->minute = (int)((seconds % 3600) / 60);
    model->second = (int)(seconds % 60);

    const int32_t start = days_from_civil(2024, 10, 7);
    const int32_t anniversary = days_from_civil(2026, 10, 7);
    model->elapsed_days = model->local_day > start ? model->local_day - start : 0;
    model->days_to_anniversary = model->local_day < anniversary
        ? anniversary - model->local_day : 0;
    model->anniversary_today = model->local_day == anniversary;
    if (!model->anniversary_today && model->celebration_active && !model->celebration_preview) {
        model->celebration_active = false;
        model->celebration_phase = CD_CELEBRATION_NONE;
        model->celebration_ms = 0;
        model->approach_active = false;
    }
    if (model->anniversary_today && model->celebrated_local_day != model->local_day
        && model->popup == CD_POPUP_NONE) {
        model->celebration_preview = false;
        start_approach(model, true);
    }
}

static void next_cat_activity(cd_model_t *model, cd_cat_t *cat)
{
    const uint32_t choice = random_next(model) % 100;
    if (choice < 32) {
        cat_enter(cat, CD_CAT_BLINK, 300);
    } else if (choice < 64) {
        int distance = 16 + (int)(random_next(model) % 53);
        int direction = random_next(model) & 1u ? 1 : -1;
        int target = cat->x + direction * distance;
        if (target < CAT_MIN_X) target = CAT_MIN_X;
        if (target > CAT_MAX_X) target = CAT_MAX_X;
        if (target == cat->x) {
            direction = -direction;
            target = cat->x + direction * distance;
            if (target < CAT_MIN_X) target = CAT_MIN_X;
            if (target > CAT_MAX_X) target = CAT_MAX_X;
        }
        cat->target_x = (int16_t)target;
        cat->direction = target >= cat->x ? 1 : -1;
        distance = target >= cat->x ? target - cat->x : cat->x - target;
        cat_enter(cat, CD_CAT_WALK, (uint32_t)distance * 40 + 400);
    } else if (choice < 78) {
        cat_enter(cat, CD_CAT_SLEEP, 8000 + random_next(model) % 6001);
    } else {
        cat_enter(cat, CD_CAT_IDLE, 2200 + random_next(model) % 4001);
    }
}

static void update_frame(cd_cat_t *cat)
{
    switch (cat->state) {
    case CD_CAT_IDLE: cat->frame = (uint8_t)((cat->state_elapsed_ms / 300) % 4); break;
    case CD_CAT_BLINK: cat->frame = (uint8_t)((cat->state_elapsed_ms / 150) % 2); break;
    case CD_CAT_WALK: cat->frame = (uint8_t)((cat->state_elapsed_ms / 120) % 6); break;
    case CD_CAT_SLEEP: cat->frame = (uint8_t)((cat->state_elapsed_ms / 500) % 4); break;
    case CD_CAT_TAP:
        cat->frame = (uint8_t)(cat->state_elapsed_ms / 200);
        if (cat->frame > 5) cat->frame = 5;
        break;
    }
}

static void move_cat(cd_cat_t *cat, uint32_t delta_ms)
{
    cat->movement_ms += delta_ms;
    while (cat->movement_ms >= 40) {
        cat->movement_ms -= 40;
        if (cat->x < cat->target_x) ++cat->x;
        else if (cat->x > cat->target_x) --cat->x;
    }
    if (cat->x < CAT_MIN_X) cat->x = CAT_MIN_X;
    if (cat->x > CAT_MAX_X) cat->x = CAT_MAX_X;
}

static void animate_cat(cd_model_t *model, cd_cat_t *cat, uint32_t delta_ms)
{
    cat->heart_ms = subtract_ms(cat->heart_ms, delta_ms);
    cat->bubble_ms = subtract_ms(cat->bubble_ms, delta_ms);
    cat->tap_window_ms = subtract_ms(cat->tap_window_ms, delta_ms);
    if (cat->tap_window_ms == 0) cat->tap_count = 0;

    if (model->approach_active && cat->state != CD_CAT_TAP) {
        if (cat->x != cat->target_x) {
            if (cat->state != CD_CAT_WALK) cat_enter(cat, CD_CAT_WALK, 10000);
            move_cat(cat, delta_ms);
            if (cat->x == cat->target_x) cat_enter(cat, CD_CAT_IDLE, 2000);
        }
        cat->state_elapsed_ms += delta_ms;
        update_frame(cat);
        return;
    }

    cat->state_elapsed_ms += delta_ms;
    cat->state_remaining_ms = subtract_ms(cat->state_remaining_ms, delta_ms);
    if (cat->state == CD_CAT_WALK) move_cat(cat, delta_ms);
    if (cat->state == CD_CAT_TAP && cat->state_elapsed_ms >= 400 && !cat->tap_heart_shown) {
        cat->heart_ms = 800;
        cat->tap_heart_shown = true;
    }
    update_frame(cat);

    const bool anniversary_hold = model->celebration_phase == CD_CELEBRATION_HEARTS;
    if (cat->state_remaining_ms == 0) {
        if (cat->state == CD_CAT_TAP || cat->state == CD_CAT_BLINK || anniversary_hold) {
            cat_enter(cat, CD_CAT_IDLE, 2200 + random_next(model) % 2601);
        } else {
            next_cat_activity(model, cat);
        }
    }
}

static void animation_step(cd_model_t *model, uint32_t delta_ms)
{
    if (model->celebration_phase == CD_CELEBRATION_HEARTS) {
        model->celebration_ms = subtract_ms(model->celebration_ms, delta_ms);
        if (model->celebration_ms == 0) {
            model->celebration_active = false;
            model->celebration_preview = false;
            model->celebration_phase = CD_CELEBRATION_NONE;
        }
    }

    for (unsigned i = 0; i < CD_CAT_COUNT; ++i) {
        animate_cat(model, &model->cats[i], delta_ms);
    }

    if (model->approach_active) {
        const bool arrived = model->cats[0].x == model->cats[0].target_x
            && model->cats[1].x == model->cats[1].target_x
            && model->cats[0].state != CD_CAT_TAP
            && model->cats[1].state != CD_CAT_TAP;
        if (arrived) {
            model->approach_active = false;
            model->next_approach_ms = 30000 + random_next(model) % 30001;
            for (unsigned i = 0; i < CD_CAT_COUNT; ++i) {
                cat_enter(&model->cats[i], CD_CAT_IDLE, 5500);
                model->cats[i].heart_ms = model->celebration_active ? 5000 : 1800;
            }
            if (model->celebration_active) {
                model->celebration_phase = CD_CELEBRATION_HEARTS;
                model->celebration_ms = CELEBRATION_DURATION_MS;
            }
        }
    } else if (!model->celebration_active) {
        model->next_approach_ms = subtract_ms(model->next_approach_ms, delta_ms);
        if (model->next_approach_ms == 0) start_approach(model, false);
    }
}

void cd_model_init(cd_model_t *model, int64_t epoch_utc)
{
    if (model == NULL) return;
    memset(model, 0, sizeof(*model));
    model->random_state = 0xc47de5a1u;
    model->celebrated_local_day = INT32_MIN;
    model->next_approach_ms = 35000;
    model->focus_remaining_ms = CD_FOCUS_DURATION_MS;
    model->todo_count = 3;
    for (unsigned i = 0; i < CD_TODO_CAPACITY; ++i) model->todo_ids[i] = (uint8_t)i;
    model->brightness = 85;
    model->mac_battery = 78;
    model->mac_cpu = 12;
    model->mac_memory = 63;
    for (unsigned i = 0; i < CD_CAT_COUNT; ++i) {
        cd_cat_t *cat = &model->cats[i];
        cat->x = i == CD_CAT_ORANGE ? 112 : 248;
        cat->y = CAT_FLOOR_Y;
        cat->target_x = cat->x;
        cat->direction = i == CD_CAT_ORANGE ? 1 : -1;
        cat_enter(cat, CD_CAT_IDLE, i == CD_CAT_ORANGE ? 3200 : 4100);
    }
    cd_model_set_time(model, epoch_utc);
}

void cd_model_set_time(cd_model_t *model, int64_t epoch_utc)
{
    if (model == NULL) return;
    model->epoch_utc = epoch_utc;
    model->clock_fraction_ms = 0;
    refresh_clock(model);
}

static void start_tap(cd_cat_t *cat)
{
    cat_enter(cat, CD_CAT_TAP, TAP_DURATION_MS);
    cat->tap_heart_shown = false;
}

void cd_model_tick(cd_model_t *model, uint32_t delta_ms)
{
    if (model == NULL || delta_ms == 0) return;
    const uint64_t clock_ms = (uint64_t)model->clock_fraction_ms + delta_ms;
    model->epoch_utc += (int64_t)(clock_ms / 1000);
    model->clock_fraction_ms = (uint32_t)(clock_ms % 1000);
    refresh_clock(model);

    model->notice_ms = subtract_ms(model->notice_ms, delta_ms);
    bool focus_finished = false;
    if (model->focus_running) {
        model->focus_remaining_ms = subtract_ms(model->focus_remaining_ms, delta_ms);
        if (model->focus_remaining_ms == 0) {
            model->focus_running = false;
            focus_finished = true;
        }
    }

    /* A suspended device catches up clocks in full, while limiting animation
       work to one minute. Normal 50 ms UI ticks retain every animation frame. */
    const uint32_t animation_delta = delta_ms > 60000 ? 60000 : delta_ms;
    model->animation_fraction_ms += animation_delta;
    while (model->animation_fraction_ms >= ANIMATION_STEP_MS) {
        model->animation_fraction_ms -= ANIMATION_STEP_MS;
        animation_step(model, ANIMATION_STEP_MS);
    }
    if (focus_finished) {
        model->notice_ms = 6000;
        for (unsigned i = 0; i < CD_CAT_COUNT; ++i) start_tap(&model->cats[i]);
    }
}

void cd_model_tap_cat(cd_model_t *model, cd_cat_kind_t kind)
{
    if (model == NULL || (unsigned)kind >= CD_CAT_COUNT) return;
    cd_cat_t *cat = &model->cats[kind];
    cat->tap_count = cat->tap_window_ms != 0 ? (uint8_t)(cat->tap_count + 1) : 1;
    cat->tap_window_ms = RAPID_TAP_WINDOW_MS;
    if (cat->tap_count >= 5) {
        cat->bubble_ms = BUBBLE_DURATION_MS;
        cat->tap_count = 0;
    }
    start_tap(cat);
}

void cd_model_start_focus(cd_model_t *model)
{
    if (model == NULL) return;
    if (!model->focus_running) {
        if (model->focus_remaining_ms == 0) model->focus_remaining_ms = CD_FOCUS_DURATION_MS;
        model->focus_running = true;
        model->notice_ms = 0;
    }
    model->popup = CD_POPUP_NONE;
}

void cd_model_replay_anniversary(cd_model_t *model)
{
    if (model == NULL) return;
    model->popup = CD_POPUP_NONE;
    model->celebration_preview = true;
    start_approach(model, true);
}

void cd_model_toggle_todo(cd_model_t *model, unsigned index)
{
    if (model == NULL || index >= model->todo_count) return;
    model->todo_checked ^= (uint8_t)(1u << index);
}

void cd_model_add_todo(cd_model_t *model)
{
    if (model == NULL || model->todo_count >= CD_TODO_CAPACITY) return;
    const unsigned index = model->todo_count;
    model->todo_ids[index] = (uint8_t)index;
    model->todo_checked &= (uint8_t)~(1u << index);
    ++model->todo_count;
}
