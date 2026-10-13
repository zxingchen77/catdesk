#include "catdesk_room.h"

#include <stddef.h>

enum {
    ROOM_LOOP_MS = 300000,
    WALK_LEG_MS = 5000
};

static cd_room_pet_pose_t native_pose(const cd_cat_t *cat)
{
    cd_room_pet_pose_t pose = {
        cat->x, cat->y, cat->state, cat->frame, cat->direction,
        CD_ROOM_ACTIVITY_NONE
    };
    return pose;
}

/* All interpolation distances come from int16 model coordinates or the
 * fixed room grid, so the multiplication is bounded in a 32-bit int. */
static int interpolate(int from, int to, uint32_t progress)
{
    return from + (to - from) * (int)progress / WALK_LEG_MS;
}

static int animation_frame(int state, uint32_t elapsed)
{
    switch (state) {
    case CD_CAT_BLINK: return (int)((elapsed / 150u) % 2u);
    case CD_CAT_WALK: return (int)((elapsed / 120u) % 6u);
    case CD_CAT_SLEEP: return (int)((elapsed / 500u) % 4u);
    case CD_CAT_TAP: return (int)((elapsed / 200u) % 6u);
    default: return (int)((elapsed / 300u) % 4u);
    }
}

static cd_room_pet_pose_t activity_target(cd_room_activity_t activity,
                                          unsigned kind, uint32_t elapsed)
{
    cd_room_pet_pose_t target = {0, 170, CD_CAT_IDLE, 0, 1, activity};
    switch (activity) {
    case CD_ROOM_ACTIVITY_SOFA:
        target.x = 77;
        target.y = 94; /* Sleep silhouette fits the right sofa cushion. */
        target.state = CD_CAT_SLEEP;
        target.direction = -1;
        break;
    case CD_ROOM_ACTIVITY_BOX:
        target.x = 56;
        target.y = 120; /* Head above the opening; front wall masks paws. */
        target.direction = -1;
        target.state = elapsed % 6000u < 300u ? CD_CAT_BLINK : CD_CAT_IDLE;
        break;
    case CD_ROOM_ACTIVITY_YARN: {
        /* The existing yarn is x343..350, y216..222. The extended paw
         * reaches its thread without changing or redrawing the prop. */
        target.x = 313;
        target.y = 184;
        uint32_t play = elapsed % 3000u;
        target.state = play < 1200u ? CD_CAT_TAP : CD_CAT_IDLE;
        target.frame = animation_frame(target.state, play);
        return target;
    }
    case CD_ROOM_ACTIVITY_WINDOW:
        target.x = 54;
        target.y = 22; /* Paws on y58 sill; weather labels begin at y60. */
        target.direction = -1;
        target.state = elapsed % 6000u < 300u ? CD_CAT_BLINK : CD_CAT_IDLE;
        break;
    case CD_ROOM_ACTIVITY_MAC:
        target.x = 268;
        target.y = 79; /* Paws on desk y115, nose across the Mac display. */
        target.state = elapsed % 6000u < 300u ? CD_CAT_BLINK : CD_CAT_IDLE;
        break;
    case CD_ROOM_ACTIVITY_SNIFF: {
        uint32_t phase = elapsed % 4000u;
        int nudge = (int)(phase < 2000u ? phase : 4000u - phase) / 1000;
        target.x = kind == CD_CAT_ORANGE ? 165 + nudge : 189 - nudge;
        target.direction = kind == CD_CAT_ORANGE ? 1 : -1;
        target.state = phase >= 1800u && phase < 2100u
            ? CD_CAT_BLINK : CD_CAT_IDLE;
        break;
    }
    case CD_ROOM_ACTIVITY_CHASE: {
        /* Two cats share a bounded eight-second out-and-back lane. On
         * reversal the trailing cat becomes the leader, with no jump. */
        uint32_t phase = elapsed % 8000u;
        uint32_t run = phase < 4000u ? phase : 8000u - phase;
        int distance = (int)(run * 164u / 4000u);
        target.x = (kind == CD_CAT_ORANGE ? 128 : 96) + distance;
        /* Separate the lanes while arriving from native positions, which
         * may cross after a natural approach. Neither sprite then fully
         * hides the other during the chase's walk-in or walk-out. */
        target.y = kind == CD_CAT_ORANGE ? 170 : 178;
        target.direction = phase < 4000u ? 1 : -1;
        target.state = CD_CAT_WALK;
        break;
    }
    case CD_ROOM_ACTIVITY_CUDDLE:
        target.x = kind == CD_CAT_ORANGE ? 166 : 186;
        target.direction = kind == CD_CAT_ORANGE ? 1 : -1;
        target.state = CD_CAT_SLEEP;
        break;
    default:
        break;
    }
    target.frame = animation_frame(target.state, elapsed);
    return target;
}

cd_room_pet_pose_t cd_room_pet_pose(const cd_model_t *model, unsigned kind,
                                   uint32_t life_ms)
{
    if (!model || kind >= CD_CAT_COUNT) {
        cd_room_pet_pose_t empty = {0, 0, CD_CAT_IDLE, 0, 1,
                                    CD_ROOM_ACTIVITY_NONE};
        return empty;
    }
    const cd_cat_t *cat = &model->cats[kind];
    cd_room_pet_pose_t native = native_pose(cat);
    if (model->celebration_active) return native;

    uint32_t time = life_ms % ROOM_LOOP_MS;
    cd_room_activity_t activity = CD_ROOM_ACTIVITY_NONE;
    uint32_t start = 0;
    uint32_t duration = 30000;
    if (time >= 30000u && time < 60000u && kind == CD_CAT_ORANGE) {
        activity = CD_ROOM_ACTIVITY_SOFA;
        start = 30000;
    } else if (time >= 60000u && time < 90000u && kind == CD_CAT_BLACK) {
        activity = CD_ROOM_ACTIVITY_BOX;
        start = 60000;
    } else if (time >= 90000u && time < 120000u && kind == CD_CAT_ORANGE) {
        activity = CD_ROOM_ACTIVITY_YARN;
        start = 90000;
    } else if (time >= 120000u && time < 150000u && kind == CD_CAT_BLACK) {
        activity = CD_ROOM_ACTIVITY_WINDOW;
        start = 120000;
    } else if (time >= 150000u && time < 180000u && kind == CD_CAT_ORANGE) {
        activity = CD_ROOM_ACTIVITY_MAC;
        start = 150000;
    } else if (time >= 180000u && time < 210000u) {
        activity = CD_ROOM_ACTIVITY_SNIFF;
        start = 180000;
    } else if (time >= 210000u && time < 250000u) {
        activity = CD_ROOM_ACTIVITY_CHASE;
        start = 210000;
        duration = 40000;
    } else if (time >= 250000u && time < 290000u) {
        activity = CD_ROOM_ACTIVITY_CUDDLE;
        start = 250000;
        duration = 40000;
    }
    if (activity == CD_ROOM_ACTIVITY_NONE) return native;

    uint32_t elapsed = time - start;
    /* Walk toward a fixed arrival point and leave from a fixed departure
     * point. Sampling a moving chase/sniff target throughout either walk
     * would add its motion to the interpolation and cause small jumps. */
    uint32_t target_elapsed = elapsed;
    if (elapsed < WALK_LEG_MS) target_elapsed = WALK_LEG_MS;
    else if (elapsed >= duration - WALK_LEG_MS)
        target_elapsed = duration - WALK_LEG_MS;
    cd_room_pet_pose_t target = activity_target(activity, kind, target_elapsed);
    cd_room_pet_pose_t pose = target;
    if (elapsed < WALK_LEG_MS) {
        pose.x = interpolate(native.x, target.x, elapsed);
        pose.y = interpolate(native.y, target.y, elapsed);
        pose.direction = target.x >= native.x ? 1 : -1;
        pose.state = CD_CAT_WALK;
        pose.frame = animation_frame(CD_CAT_WALK, elapsed);
    } else if (elapsed >= duration - WALK_LEG_MS) {
        uint32_t progress = elapsed - (duration - WALK_LEG_MS);
        pose.x = interpolate(target.x, native.x, progress);
        pose.y = interpolate(target.y, native.y, progress);
        pose.direction = native.x >= target.x ? 1 : -1;
        pose.state = CD_CAT_WALK;
        pose.frame = animation_frame(CD_CAT_WALK, elapsed);
    }

    /* Hearts and the black cat's speech bubble remain anchored to the
     * projected cat. A tap can interrupt any activity without teleporting
     * back to the rug or replacing its existing reaction animation. */
    if (cat->state == CD_CAT_TAP || cat->bubble_ms || cat->heart_ms) {
        pose.state = native.state;
        pose.frame = native.frame;
    }
    return pose;
}
