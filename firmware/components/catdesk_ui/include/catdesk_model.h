#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The simulation uses the 400 x 240 art grid, independent of LVGL or an OS. */
#define CD_CAT_COUNT 2
#define CD_TODO_CAPACITY 5
#define CD_FOCUS_DURATION_MS (25u * 60u * 1000u)

typedef enum {
    CD_CAT_ORANGE = 0,
    CD_CAT_BLACK = 1
} cd_cat_kind_t;

typedef enum {
    CD_CAT_IDLE = 0,
    CD_CAT_BLINK,
    CD_CAT_WALK,
    CD_CAT_SLEEP,
    CD_CAT_TAP
} cd_cat_state_t;

typedef enum {
    CD_POPUP_NONE = 0,
    CD_POPUP_ANNIVERSARY,
    CD_POPUP_TODO,
    CD_POPUP_FOCUS,
    CD_POPUP_WEATHER,
    CD_POPUP_MAC,
    CD_POPUP_SETTINGS
} cd_popup_t;

typedef enum {
    CD_CELEBRATION_NONE = 0,
    CD_CELEBRATION_APPROACH,
    CD_CELEBRATION_HEARTS
} cd_celebration_phase_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t target_x;
    int8_t direction; /* +1 faces right, -1 faces left. */
    cd_cat_state_t state;
    uint8_t frame;
    uint32_t state_elapsed_ms;
    uint32_t state_remaining_ms;
    uint32_t movement_ms;
    uint32_t heart_ms;
    uint32_t bubble_ms;
    uint32_t tap_window_ms;
    uint8_t tap_count;
    bool tap_heart_shown;
} cd_cat_t;

typedef struct {
    cd_cat_t cats[CD_CAT_COUNT];
    cd_popup_t popup;

    int64_t epoch_utc;
    uint32_t clock_fraction_ms;
    bool clock_synced; /* This boot has received a valid host wall clock. */
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int32_t local_day;
    int32_t elapsed_days;
    int32_t days_to_anniversary;
    bool anniversary_today;

    bool celebration_active;
    bool celebration_preview; /* Explicit replay can run on any date. */
    cd_celebration_phase_t celebration_phase;
    uint32_t celebration_ms; /* Five-second banner timer after cats arrive. */
    int32_t celebrated_local_day;
    bool approach_active;
    uint32_t next_approach_ms;

    bool focus_running;
    uint32_t focus_remaining_ms;
    uint32_t notice_ms;

    uint8_t todo_count;
    uint8_t todo_checked; /* One checkbox bit per visible item. */
    uint8_t todo_ids[CD_TODO_CAPACITY]; /* Renderer owns the Chinese labels. */
    uint8_t brightness; /* Percent, default 85. */

    /* Mock defaults can be replaced by a transport without changing the UI. */
    bool mac_connected;
    bool mac_charging;
    uint8_t mac_battery;
    uint8_t mac_cpu;
    uint8_t mac_memory;

    uint32_t random_state;
    uint32_t animation_fraction_ms;
} cd_model_t;

void cd_model_init(cd_model_t *model, int64_t epoch_utc);
void cd_model_tick(cd_model_t *model, uint32_t delta_ms);
void cd_model_tap_cat(cd_model_t *model, cd_cat_kind_t kind);
void cd_model_set_time(cd_model_t *model, int64_t epoch_utc);
void cd_model_start_focus(cd_model_t *model);
void cd_model_replay_anniversary(cd_model_t *model);
void cd_model_toggle_todo(cd_model_t *model, unsigned index);
void cd_model_add_todo(cd_model_t *model);

#ifdef __cplusplus
}
#endif
