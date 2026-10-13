#pragma once

#include "catdesk_model.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The welcome scene owns no model state or framebuffer. Its clock advances
 * at 10 FPS, and stops permanently when the room has been revealed. */
enum {
    CD_WELCOME_FRAME_MS = 100,
    CD_WELCOME_INTRO_MS = 4000,
    CD_WELCOME_RUN_HOME_MS = 600,
    CD_WELCOME_DOOR_MS = 300,
    CD_WELCOME_WIPE_MS = 800
};

typedef enum {
    CD_WELCOME_INTRO = 0,
    CD_WELCOME_WAITING,
    CD_WELCOME_ENTERING,
    CD_WELCOME_WIPING,
    CD_WELCOME_DONE
} cd_welcome_phase_t;

typedef struct {
    uint32_t elapsed_ms;
    uint32_t fraction_ms;
    uint32_t exit_ms;
    cd_welcome_phase_t phase;
    bool anniversary;
    bool press_latched;
} cd_welcome_t;

typedef struct { int x, y, width, height; } cd_welcome_rect_t;
typedef struct {
    int x, y, state, frame, direction;
    bool visible;
} cd_welcome_cat_pose_t;

void cd_welcome_init(cd_welcome_t *scene, const cd_model_t *model);
/* Updates the Oct 7 variant from the model, accepts arbitrary poll deltas,
 * and returns true only when a visible 100 ms frame or phase changed. */
bool cd_welcome_tick(cd_welcome_t *scene, uint32_t delta_ms,
                     const cd_model_t *model);
/* The first press while WAITING is accepted anywhere. Intro presses are
 * ignored, so the completed scene always waits for a fresh touch. Once
 * accepted, subsequent presses cannot restart the doorway sequence. */
bool cd_welcome_press(cd_welcome_t *scene);
bool cd_welcome_active(const cd_welcome_t *scene);
cd_welcome_phase_t cd_welcome_phase(const cd_welcome_t *scene);
cd_welcome_cat_pose_t cd_welcome_cat_pose(const cd_welcome_t *scene,
                                         unsigned kind);

/* Reuses the existing room background. During the wipe it first renders the
 * room into pixels, then paints the welcome only in unrevealed 16 px tiles.
 * No additional full-size framebuffer, image allocation or alpha is used. */
void cd_welcome_render(uint16_t *pixels, const uint16_t *home_background,
                        const cd_model_t *model, const cd_welcome_t *scene);
/* Bounding union of all changed logical pixels, including both old and new
 * cat positions. A NULL previous scene forces the initial full refresh. */
bool cd_welcome_dirty_rect(const cd_welcome_t *scene,
                           const cd_welcome_t *previous,
                           cd_welcome_rect_t *rect);

#ifdef __cplusplus
}
#endif
