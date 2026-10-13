/* Host check: cc -std=c11 -Wall -Wextra -Werror -pedantic \
 *   -fsanitize=address,undefined -Ifirmware/components/catdesk_ui/include \
 *   tools/test_room.c firmware/components/catdesk_ui/catdesk_room.c \
 *   firmware/components/catdesk_ui/catdesk_model.c -o /tmp/catdesk-test-room
 */
#include "catdesk_room.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int distance(int a, int b) { return a > b ? a - b : b - a; }

static void assert_native(cd_room_pet_pose_t pose, const cd_cat_t *cat)
{
    assert(pose.activity == CD_ROOM_ACTIVITY_NONE);
    assert(pose.x == cat->x && pose.y == cat->y);
    assert(pose.state == (int)cat->state && pose.frame == cat->frame);
    assert(pose.direction == cat->direction);
}

static void assert_valid(cd_room_pet_pose_t pose)
{
    static const int count[] = {4, 2, 6, 4, 6};
    assert(pose.x >= 40 && pose.x <= 320);
    assert(pose.y >= 22 && pose.y <= 184);
    assert(pose.x + 40 <= 400 && pose.y + 40 <= 240);
    assert(pose.state >= CD_CAT_IDLE && pose.state <= CD_CAT_TAP);
    assert(pose.frame >= 0 && pose.frame < count[pose.state]);
    assert(pose.direction == 1 || pose.direction == -1);
    assert(pose.activity >= CD_ROOM_ACTIVITY_NONE
           && pose.activity <= CD_ROOM_ACTIVITY_CUDDLE);
}

static void test_schedule(cd_model_t *model)
{
    static const struct {
        uint32_t time;
        unsigned kind;
        cd_room_activity_t activity;
        int x, y, state;
    } fixtures[] = {
        {45000, CD_CAT_ORANGE, CD_ROOM_ACTIVITY_SOFA, 77, 94, CD_CAT_SLEEP},
        {75000, CD_CAT_BLACK, CD_ROOM_ACTIVITY_BOX, 56, 120, CD_CAT_IDLE},
        {105000, CD_CAT_ORANGE, CD_ROOM_ACTIVITY_YARN, 313, 184, CD_CAT_TAP},
        {135000, CD_CAT_BLACK, CD_ROOM_ACTIVITY_WINDOW, 54, 22, CD_CAT_IDLE},
        {165000, CD_CAT_ORANGE, CD_ROOM_ACTIVITY_MAC, 268, 79, CD_CAT_IDLE},
        {195000, CD_CAT_ORANGE, CD_ROOM_ACTIVITY_SNIFF, 166, 170, CD_CAT_IDLE},
        {195000, CD_CAT_BLACK, CD_ROOM_ACTIVITY_SNIFF, 188, 170, CD_CAT_IDLE},
        {230000, CD_CAT_ORANGE, CD_ROOM_ACTIVITY_CHASE, 292, 170, CD_CAT_WALK},
        {230000, CD_CAT_BLACK, CD_ROOM_ACTIVITY_CHASE, 260, 178, CD_CAT_WALK},
        {270000, CD_CAT_ORANGE, CD_ROOM_ACTIVITY_CUDDLE, 166, 170, CD_CAT_SLEEP},
        {270000, CD_CAT_BLACK, CD_ROOM_ACTIVITY_CUDDLE, 186, 170, CD_CAT_SLEEP}
    };
    for (unsigned i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i) {
        cd_room_pet_pose_t pose = cd_room_pet_pose(model, fixtures[i].kind,
                                                 fixtures[i].time);
        assert(pose.activity == fixtures[i].activity);
        assert(pose.x == fixtures[i].x && pose.y == fixtures[i].y);
        assert(pose.state == fixtures[i].state);
    }
    for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
        assert_native(cd_room_pet_pose(model, kind, 0), &model->cats[kind]);
        assert_native(cd_room_pet_pose(model, kind, 29999), &model->cats[kind]);
        assert_native(cd_room_pet_pose(model, kind, 290000), &model->cats[kind]);
        assert_native(cd_room_pet_pose(model, kind, 299999), &model->cats[kind]);
    }
    assert_native(cd_room_pet_pose(model, CD_CAT_BLACK, 45000), &model->cats[1]);
    assert_native(cd_room_pet_pose(model, CD_CAT_ORANGE, 75000), &model->cats[0]);
    assert_native(cd_room_pet_pose(model, CD_CAT_BLACK, 105000), &model->cats[1]);
    assert_native(cd_room_pet_pose(model, CD_CAT_ORANGE, 135000), &model->cats[0]);
    assert_native(cd_room_pet_pose(model, CD_CAT_BLACK, 165000), &model->cats[1]);
}

static void test_paths(const cd_model_t *model)
{
    /* Every millisecond of the loop, including the chase reversals and
     * cross-activity boundaries, moves by at most one presentation pixel.
     * This checks actual continuity rather than only the slot endpoints. */
    for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
        cd_room_pet_pose_t previous = cd_room_pet_pose(model, kind, 0);
        for (uint32_t time = 1; time <= 300000u; ++time) {
            cd_room_pet_pose_t pose = cd_room_pet_pose(model, kind, time);
            assert_valid(pose);
            if (distance(previous.x, pose.x) > 1 || distance(previous.y, pose.y) > 1)
                fprintf(stderr, "Cat %u at %u: (%d,%d) -> (%d,%d)\n",
                        kind, time, previous.x, previous.y, pose.x, pose.y);
            assert(distance(previous.x, pose.x) <= 1);
            assert(distance(previous.y, pose.y) <= 1);
            previous = pose;
        }
    }
}

static void test_priority(cd_model_t *model)
{
    for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
        uint32_t time = kind == CD_CAT_ORANGE ? 45000u : 135000u;
        cd_room_pet_pose_t before = cd_room_pet_pose(model, kind, time);
        cd_model_t copy = *model;
        cd_model_tap_cat(&copy, (cd_cat_kind_t)kind);
        cd_room_pet_pose_t tapped = cd_room_pet_pose(&copy, kind, time);
        assert(tapped.x == before.x && tapped.y == before.y);
        assert(tapped.activity == before.activity);
        assert(tapped.state == CD_CAT_TAP && tapped.frame == 0);

        copy.cats[kind].state = CD_CAT_BLINK;
        copy.cats[kind].frame = 1;
        copy.cats[kind].bubble_ms = 1800;
        cd_room_pet_pose_t bubble = cd_room_pet_pose(&copy, kind, time);
        assert(bubble.x == before.x && bubble.y == before.y);
        assert(bubble.state == CD_CAT_BLINK && bubble.frame == 1);
        copy.cats[kind].bubble_ms = 0;
        copy.cats[kind].heart_ms = 800;
        cd_room_pet_pose_t heart = cd_room_pet_pose(&copy, kind, time);
        assert(heart.x == before.x && heart.y == before.y);
        assert(heart.state == CD_CAT_BLINK && heart.frame == 1);

        copy.celebration_active = true;
        assert_native(cd_room_pet_pose(&copy, kind, time), &copy.cats[kind]);
        copy.celebration_active = false;
        copy.approach_active = true;
        cd_room_pet_pose_t approach = cd_room_pet_pose(&copy, kind, time);
        assert(approach.x == before.x && approach.y == before.y);
        assert(approach.activity == before.activity);
        assert_native(cd_room_pet_pose(&copy, kind, 10000), &copy.cats[kind]);
    }
}

static void test_live_model(void)
{
    cd_model_t model;
    cd_model_init(&model, 1791216000);
    cd_room_pet_pose_t previous[CD_CAT_COUNT];
    for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind)
        previous[kind] = cd_room_pet_pose(&model, kind, 0);
    bool was_approaching = false;
    unsigned approach_starts = 0, approach_ends = 0;
    unsigned seen_activities = 0;
    for (uint32_t time = 50; time <= 300000u; time += 50) {
        cd_model_tick(&model, 50);
        assert(!model.celebration_active);
        if (model.approach_active != was_approaching) {
            if (model.approach_active) ++approach_starts;
            else ++approach_ends;
            was_approaching = model.approach_active;
        }
        cd_model_t unchanged = model;
        for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
            cd_room_pet_pose_t pose = cd_room_pet_pose(&model, kind, time);
            assert_valid(pose);
            seen_activities |= 1u << pose.activity;
            if (distance(previous[kind].x, pose.x) > 4
                || distance(previous[kind].y, pose.y) > 4)
                fprintf(stderr, "Live cat %u at %u: (%d,%d) -> (%d,%d), approach=%d\n",
                        kind, time, previous[kind].x, previous[kind].y,
                        pose.x, pose.y, model.approach_active);
            assert(distance(previous[kind].x, pose.x) <= 4);
            assert(distance(previous[kind].y, pose.y) <= 4);
            previous[kind] = pose;
        }
        assert(memcmp(&model, &unchanged, sizeof(model)) == 0);
    }
    /* The original simulation ran its natural approach repeatedly, while
     * every furniture/pair slot remained visible and paths stayed smooth. */
    assert(approach_starts >= 4 && approach_ends >= 4);
    assert(seen_activities == (1u << (CD_ROOM_ACTIVITY_CUDDLE + 1u)) - 1u);
}

static void test_pure_and_long_time(cd_model_t *model)
{
    cd_model_t original = *model;
    const uint32_t times[] = {300000, 600000, UINT32_MAX, UINT32_MAX - 1u,
                              1000000000u, 4294800000u};
    for (unsigned i = 0; i < sizeof(times) / sizeof(times[0]); ++i) {
        for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
            cd_room_pet_pose_t actual = cd_room_pet_pose(model, kind, times[i]);
            cd_room_pet_pose_t wrapped = cd_room_pet_pose(model, kind,
                                                         times[i] % 300000u);
            assert(actual.x == wrapped.x && actual.y == wrapped.y);
            assert(actual.state == wrapped.state && actual.frame == wrapped.frame);
            assert(actual.direction == wrapped.direction);
            assert(actual.activity == wrapped.activity);
            assert_valid(actual);
        }
    }
    assert(memcmp(model, &original, sizeof(original)) == 0);
    assert(cd_room_pet_pose(NULL, 0, UINT32_MAX).activity == CD_ROOM_ACTIVITY_NONE);
    assert(cd_room_pet_pose(model, CD_CAT_COUNT, UINT32_MAX).activity == CD_ROOM_ACTIVITY_NONE);
}

int main(void)
{
    cd_model_t model;
    cd_model_init(&model, 1791216000); /* October 6; anniversary not running. */
    assert(!model.celebration_active && !model.approach_active);
    test_schedule(&model);
    test_paths(&model);
    test_priority(&model);
    test_live_model();
    test_pure_and_long_time(&model);
    puts("CatDesk room: paths, bounds, reactions, celebration and pure wrapping passed.");
    return 0;
}
