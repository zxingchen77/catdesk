#pragma once

#include "catdesk_model.h"

enum { CD_PERSON_W=16, CD_PERSON_H=24 };
typedef enum {
    CD_PERSON_FRONT, CD_PERSON_SIDE, CD_PERSON_SIT,
    CD_PERSON_WALK_A, CD_PERSON_WALK_B
} cd_person_pose_t;

/* Original packed 4-bit sprites; existing theme colors only. */
void cd_couple_draw_person(uint16_t *pixels, int x, int y, unsigned person,
                           cd_person_pose_t pose, bool mirror);
enum { CD_COUPLE_SOFA_BIT=1, CD_COUPLE_FINALE_BIT=2, CD_COUPLE_ROOM_BIT=4 };
typedef struct {
    uint32_t wait_ms, step_ms;
    int16_t x, y, target_x, target_y;
    int8_t direction;
    uint8_t step;
} cd_person_t;
typedef struct { int x, y, direction; cd_person_pose_t pose; } cd_person_view_t;
typedef struct {
    uint32_t random_state, wait_ms, quiet_ms, sofa_ms, finale_ms, walk_quiet_ms;
    uint32_t walk_random_state;
    int32_t heart_day;
    bool hearts_seen;
    cd_person_t people[2];
} cd_couple_t;

/* Display-only timers. No model mutation, allocation or persistence. */
void cd_couple_init(cd_couple_t *state, uint32_t seed);
void cd_couple_tick(cd_couple_t *state, const cd_model_t *model,
                    uint32_t delta_ms, uint32_t life_ms, bool home, bool contact);
void cd_couple_touch(cd_couple_t *state);
uint32_t cd_couple_visual_key(const cd_couple_t *state, const cd_model_t *model);
cd_person_view_t cd_couple_person(const cd_couple_t *state, const cd_model_t *model, unsigned person);
void cd_couple_draw_room(uint16_t *pixels, const cd_couple_t *state, const cd_model_t *model);
void cd_couple_draw_sofa(uint16_t *pixels);
void cd_couple_draw_finale(uint16_t *pixels);

/* The existing renderer owns one small instance. */
void cd_ui_couple_init(uint32_t seed);
void cd_ui_couple_tick(const cd_model_t *model, uint32_t delta_ms, bool home);
const cd_couple_t *cd_ui_couple_state(void);
uint32_t cd_ui_couple_visual_key(const cd_model_t *model);
cd_person_view_t cd_ui_couple_person(const cd_model_t *model, unsigned person);
