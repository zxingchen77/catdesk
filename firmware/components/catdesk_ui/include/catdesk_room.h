#pragma once

#include "catdesk_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CD_ROOM_ACTIVITY_NONE = 0,
    CD_ROOM_ACTIVITY_SOFA,
    CD_ROOM_ACTIVITY_BOX,
    CD_ROOM_ACTIVITY_YARN,
    CD_ROOM_ACTIVITY_WINDOW,
    CD_ROOM_ACTIVITY_MAC,
    CD_ROOM_ACTIVITY_SNIFF,
    CD_ROOM_ACTIVITY_CHASE,
    CD_ROOM_ACTIVITY_CUDDLE
} cd_room_activity_t;

/* Coordinates are the upper-left of the existing 40 x 40 sprite. Paws are
 * at y + 36. Only the presentation changes; the simulation stays intact. */
typedef struct {
    int x;
    int y;
    int state;
    int frame;
    int direction;
    cd_room_activity_t activity;
} cd_room_pet_pose_t;

/* life_ms is monotonic boot time, independent of the host wall clock. The
 * choreography repeats every 300000 ms. Celebration uses the exact native
 * pose; ordinary approaches remain projected during room activities and
 * use their native pose during NONE slots. Petting uses native state/frame
 * at projected x/y.
 * This function neither allocates nor changes the model. */
cd_room_pet_pose_t cd_room_pet_pose(const cd_model_t *model, unsigned kind,
                                   uint32_t life_ms);

#ifdef __cplusplus
}
#endif
