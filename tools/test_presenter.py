#!/usr/bin/env python3
"""Exercise the production dirty-region presenter against a full-frame oracle.

No ESP or LVGL headers are used: only the invalidation call is stubbed. The
presenter itself is extracted verbatim from catdesk_ui.c on every invocation.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


PREFIX = r'''
#include "catdesk_render.h"
#include "catdesk_couple.h"
#include "catdesk_cats.h"
#include "catdesk_layout.h"
#include "catdesk_room.h"
#include "catdesk_welcome.h"
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PHYSICAL_WIDTH = 800, PHYSICAL_HEIGHT = 480 };
typedef struct { int32_t x1, y1, x2, y2; } lv_area_t;
typedef int lv_obj_t;
static cd_model_t s_model, s_presented;
static bool s_presented_once;
static cd_welcome_t s_welcome={.phase=CD_WELCOME_DONE}, s_welcome_presented;
static bool s_welcome_presented_once;
static uint16_t *s_background, *s_frame, *s_physical;
static uint16_t s_red[32], s_green[64], s_blue[32];
static unsigned s_brightness_level;
static uint32_t s_frames;
static lv_obj_t s_image_value;
static lv_obj_t *s_image = &s_image_value;
static bool invalidated;
static lv_area_t invalidations[6];
static unsigned invalidation_calls, frame_invalidations;

static void lv_obj_invalidate_area(lv_obj_t *image, const lv_area_t *area)
{
    assert(image == s_image);
    assert(area->x1 >= 0 && area->x2 < PHYSICAL_WIDTH);
    assert(area->y1 >= 0 && area->y2 < PHYSICAL_HEIGHT);
    assert(area->x1 <= area->x2 && area->y1 <= area->y2);
    assert(area->x1 % 2 == 0 && area->y1 % 2 == 0);
    assert(area->x2 % 2 == 1 && area->y2 % 2 == 1);
    assert(frame_invalidations < 6);
    invalidated = true;
    invalidations[frame_invalidations++] = *area;
    ++invalidation_calls;
}

'''


SUFFIX = r'''
static uint16_t *reference_frame, *previous_expected;
static unsigned checked_frames;
static const int64_t OCT_6 = INT64_C(1791216000);
static const int64_t OCT_6_LAST_SECOND = INT64_C(1791302399);
static const int64_t OCT_7 = INT64_C(1791302400);

static uint16_t reference_dim(uint16_t pixel, unsigned brightness)
{
    unsigned level = 35 + brightness * 65 / 100;
    unsigned r = ((pixel >> 11) & 31) * level / 100;
    unsigned g = ((pixel >> 5) & 63) * level / 100;
    unsigned b = (pixel & 31) * level / 100;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void fail_pixel(const char *scenario, int x, int y, uint16_t expected, uint16_t actual)
{
    fprintf(stderr, "Presenter mismatch frame=%u scenario=%s physical=(%d,%d) logical=(%d,%d) expected=%04x actual=%04x popup=%d day=%d phase=%d brightness=%u\n",
        checked_frames, scenario, x, y, x/2, y/2, expected, actual,
        s_model.popup, s_model.day, s_model.celebration_phase, s_model.brightness);
    for (unsigned i=0; i<CD_CAT_COUNT; ++i) {
        fprintf(stderr, "cat%u x=%d state=%d frame=%u heart=%" PRIu32 " bubble=%" PRIu32 "\n",
            i, s_model.cats[i].x, s_model.cats[i].state, s_model.cats[i].frame,
            s_model.cats[i].heart_ms, s_model.cats[i].bubble_ms);
    }
    exit(1);
}

static void check_frame(const char *scenario)
{
    invalidated = false;
    frame_invalidations = 0;
    present_frame();
    cd_render_frame(reference_frame, s_background, &s_model);
    if(cd_welcome_active(&s_welcome))cd_welcome_render(reference_frame,s_background,&s_model,&s_welcome);
    for (int y=0; y<CD_HEIGHT; ++y) {
        for (int x=0; x<CD_WIDTH; ++x) {
            size_t logical = (size_t)y * CD_WIDTH + x;
            size_t physical = (size_t)y * 2 * PHYSICAL_WIDTH + x * 2;
            uint16_t expected = reference_dim(reference_frame[logical], s_model.brightness);
            if (s_physical[physical] != expected)
                fail_pixel(scenario, x*2, y*2, expected, s_physical[physical]);
            if (s_physical[physical+1] != expected)
                fail_pixel(scenario, x*2+1, y*2, expected, s_physical[physical+1]);
            if (s_physical[physical+PHYSICAL_WIDTH] != expected)
                fail_pixel(scenario, x*2, y*2+1, expected, s_physical[physical+PHYSICAL_WIDTH]);
            if (s_physical[physical+PHYSICAL_WIDTH+1] != expected)
                fail_pixel(scenario, x*2+1, y*2+1, expected, s_physical[physical+PHYSICAL_WIDTH+1]);
            if (previous_expected[logical] != expected) {
                bool covered=false;
                for(unsigned i=0;i<frame_invalidations;i++){
                    const lv_area_t *a=&invalidations[i];
                    if(x*2>=a->x1 && x*2+1<=a->x2 && y*2>=a->y1 && y*2+1<=a->y2)covered=true;
                }
                if (!covered) {
                    fprintf(stderr, "Dirty bounds miss frame=%u scenario=%s logical=(%d,%d)\n",
                        checked_frames, scenario, x, y);
                    exit(1);
                }
            }
            previous_expected[logical] = expected;
        }
    }
    ++checked_frames;
}

static void test_transitions(void)
{
    cd_model_init(&s_model, OCT_6);
    check_frame("initial full frame");
    check_frame("identical frame");
    assert(!invalidated);
    for (unsigned brightness=20; brightness<=100; ++brightness) {
        s_model.brightness = (uint8_t)brightness;
        check_frame("brightness sweep");
    }
    s_model.brightness = 85;
    check_frame("brightness restored");

    /* A transport transition changes the real Mac screen outside the
       regular cat, clock and popup redraw boxes. Check both directions
       explicitly; the old static screen hid this omission. */
    for (unsigned popup=0; popup<2; ++popup) {
        s_model.popup = popup ? CD_POPUP_FOCUS : CD_POPUP_NONE;
        s_model.mac_connected = false;
        check_frame("Mac screen disconnected setup");
        s_model.mac_connected = true;
        check_frame("Mac screen lights with and without modal");
        s_model.mac_connected = false;
        check_frame("Mac screen dims with and without modal");
    }
    s_model.popup = CD_POPUP_NONE;
    check_frame("Mac lighting transition modal cleanup");

    s_model.popup = CD_POPUP_FOCUS;
    cd_ui_pointer(-1,-1,false);
    check_frame("normal selected focus button");
    cd_ui_pointer(160,151,true);
    check_frame("render-only button pressed prepaint");
    cd_ui_visual_tick(159);
    check_frame("pressed pointer remains before expiry");
    cd_ui_visual_tick(1);
    check_frame("pressed pointer expiry clears same open modal");
    cd_ui_pointer(160,151,true);
    check_frame("closing action pressed prepaint");
    cd_ui_press(&s_model,160,151);
    assert(s_model.popup == CD_POPUP_NONE && s_model.focus_running);
    cd_model_tick(&s_model,80);
    cd_ui_visual_tick(80);
    check_frame("closing button after physical feedback hold");
    cd_ui_pointer(-1,-1,false);
    s_model.focus_running = false;
    s_model.focus_remaining_ms = CD_FOCUS_DURATION_MS;
    check_frame("pointer and focus feedback cleanup");

    for (int popup=CD_POPUP_ANNIVERSARY; popup<=CD_POPUP_SETTINGS; ++popup) {
        s_model.popup = (cd_popup_t)popup;
        check_frame("open each popup");
        cd_model_tick(&s_model, 800);
        cd_model_tap_cat(&s_model, CD_CAT_BLACK);
        check_frame("cat animates behind popup");
        s_model.popup = CD_POPUP_NONE;
        check_frame("close each popup");
    }
    s_model.popup = CD_POPUP_TODO;
    while (s_model.todo_count < CD_TODO_CAPACITY) {
        cd_model_add_todo(&s_model);
        check_frame("add todo");
    }
    for (unsigned i=0; i<CD_TODO_CAPACITY; ++i) {
        cd_model_toggle_todo(&s_model, i);
        check_frame("check todo");
    }
    s_model.popup = CD_POPUP_NONE;
    check_frame("close populated todo");

    static const unsigned counts[] = {4, 2, 6, 4, 6};
    for (unsigned cat=0; cat<CD_CAT_COUNT; ++cat) {
        for (int edge=0; edge<2; ++edge) {
            s_model.cats[cat].x = edge ? 320 : 40;
            s_model.cats[cat].direction = edge ? -1 : 1;
            for (unsigned state=0; state<=CD_CAT_TAP; ++state) {
                s_model.cats[cat].state = (cd_cat_state_t)state;
                for (unsigned frame=0; frame<counts[state]; ++frame) {
                    s_model.cats[cat].frame = (uint8_t)frame;
                    s_model.cats[cat].heart_ms = frame & 1u ? 800 : 0;
                    s_model.cats[cat].bubble_ms = frame & 1u ? 1800 : 0;
                    check_frame("every sprite frame at floor edges");
                }
            }
        }
    }

    cd_model_init(&s_model, OCT_6_LAST_SECOND);
    s_model.popup = CD_POPUP_MAC;
    check_frame("midnight with Mac popup");
    cd_model_tick(&s_model, 1000);
    check_frame("anniversary frame while popup open");
    s_model.popup = CD_POPUP_NONE;
    check_frame("popup close at midnight");
    for (unsigned i=0; i<120; ++i) {
        cd_model_tick(&s_model, 80);
        check_frame("anniversary approach hearts and cleanup");
    }
    s_model.popup = CD_POPUP_FOCUS;
    cd_model_start_focus(&s_model);
    check_frame("focus starts and popup closes");
    cd_model_tick(&s_model, CD_FOCUS_DURATION_MS-1);
    check_frame("focus last millisecond");
    cd_model_tick(&s_model, 1);
    check_frame("focus completed notice");
    cd_model_tick(&s_model, 6000);
    check_frame("focus notice removed");
    s_model.focus_remaining_ms = CD_FOCUS_DURATION_MS;
    check_frame("focus badge removed after reset");
}

static void test_mixed_frames(unsigned count)
{
    cd_model_init(&s_model, OCT_6);
    check_frame("mixed simulation setup");
    for (unsigned i=0; i<count; ++i) {
        cd_model_tick(&s_model, i%13 == 0 ? 1000 : 80);
        cd_ui_visual_tick(i%13 == 0 ? 1000 : 80);
        if (i%37 == 0) s_model.popup = (cd_popup_t)((i/37)%7);
        if (i%41 == 0) cd_model_tap_cat(&s_model, (cd_cat_kind_t)((i/41)%2));
        if (i%79 == 0) {
            for (unsigned tap=0; tap<5; ++tap) cd_model_tap_cat(&s_model, CD_CAT_BLACK);
        }
        if (i%83 == 0) cd_model_toggle_todo(&s_model, (i/83)%CD_TODO_CAPACITY);
        if (i%113 == 0) cd_model_add_todo(&s_model);
        if (i%211 == 0) s_model.brightness = (uint8_t)(20+(i/211)%81);
        if (i%127 == 0) {
            s_model.mac_connected = (i/127)%2 != 0;
            s_model.mac_charging = (i/127)%3 != 0;
            s_model.mac_battery = (uint8_t)((i/127)%101);
            s_model.mac_cpu = (uint8_t)((i/31)%101);
            s_model.mac_memory = (uint8_t)((i/7)%101);
        }
        if (i%347 == 0) s_model.clock_synced = !s_model.clock_synced;
        if (i%503 == 0) cd_model_set_time(&s_model, s_model.epoch_utc+3601);
        if (i%977 == 0) cd_model_replay_anniversary(&s_model);
        if (i%997 == 0) {
            s_model.focus_remaining_ms = 1200;
            s_model.focus_running = true;
        }
        if (i%1999 == 0) {
            s_model.focus_remaining_ms = CD_FOCUS_DURATION_MS;
            s_model.focus_running = false;
        }
        if (i == 900) cd_model_set_time(&s_model, OCT_6_LAST_SECOND);
        if (i == 1200) cd_model_set_time(&s_model, OCT_7);
        if (i == 4200) cd_model_set_time(&s_model, INT64_C(1709222399)); /* Leap Feb 29. */
        if (i == 6400) cd_model_set_time(&s_model, INT64_C(1791388799)); /* Leave anniversary. */
        check_frame("mixed animation and UI changes");
    }
}

static void test_ambient_regions(void)
{
    cd_model_init(&s_model,OCT_6);
    s_model.clock_synced=true;
    cd_ui_pointer(-1,-1,false);
    check_frame("ambient fixed room setup");
    const cd_model_t before=s_model;
    unsigned window_frames=0,moth_frames=0;
    for(unsigned tick=0;tick<750;tick++){
        cd_room_pet_pose_t old[CD_CAT_COUNT];
        for(unsigned i=0;i<CD_CAT_COUNT;i++)old[i]=cd_room_pet_pose(&s_model,i,cd_ui_life_ms());
        cd_ui_visual_tick(80);
        check_frame("one minute of independent art effects");
        for(unsigned i=0;i<frame_invalidations;i++){
            const lv_area_t *a=&invalidations[i];
            if(a->x1>=100 && a->x2<=201 && a->y1>=54 && a->y2<=109)++window_frames;
            else if(a->x1>=280 && a->x2<=313 && a->y1>=204 && a->y2<=239)++moth_frames;
            else {
                bool pet_area=false;
                for(unsigned kind=0;kind<CD_CAT_COUNT;kind++){
                    cd_room_pet_pose_t now=cd_room_pet_pose(&s_model,kind,cd_ui_life_ms());
                    int x1=now.x<old[kind].x?now.x:old[kind].x;
                    int x2=now.x>old[kind].x?now.x:old[kind].x;
                    int y1=now.y<old[kind].y?now.y:old[kind].y;
                    int y2=now.y>old[kind].y?now.y:old[kind].y;
                    if(a->x1>=(x1-4)*2 && a->x2<=(x2+44)*2+1 &&
                       a->y1>=(y1-32)*2 && a->y2<=(y2+41)*2+1)pet_area=true;
                }
                assert(pet_area);
            }
        }
    }
    assert(window_frames>0 && window_frames<150 && moth_frames>0 && moth_frames<100);
    assert(memcmp(&s_model,&before,sizeof(s_model))==0);
    /* Catch-up arithmetic and host wall-clock corrections must not turn
       art into a model mutation or restart its independent animation loop. */
    cd_ui_visual_tick(UINT32_MAX);
    check_frame("ambient large delta catch-up");
    for(int hour=1;hour<=3;hour++){
        cd_model_set_time(&s_model,OCT_6+hour*3600);
        check_frame("host clock correction leaves ambient phase alone");
        assert(!invalidated);
    }
    s_model.celebration_active=true;
    check_frame("cutscene hides ambient visitors without trails");
    s_model.celebration_active=false;
    check_frame("ambient room resumes after cutscene");
}

static void test_room_cycle(void)
{
    cd_model_init(&s_model,OCT_6+12*3600);
    s_model.clock_synced=true;
    cd_ui_visual_tick((300000u-cd_ui_life_ms())%300000u);
    check_frame("five-minute room setup");
    for(unsigned tick=0;tick<3750;tick++){
        cd_model_tick(&s_model,80);
        cd_ui_visual_tick(80);
        if(tick==1250)cd_model_set_time(&s_model,OCT_6+17*3600);
        if(tick==2500)cd_model_set_time(&s_model,OCT_6+20*3600);
        if(tick==3100)s_model.clock_synced=false;
        check_frame("full room life with day/dusk/night corrections");
    }
    assert(cd_ui_life_ms()==0);
}

static void couple_frame(uint32_t delta,const char *scenario)
{
    cd_model_tick(&s_model,delta);cd_ui_visual_tick(delta);
    cd_ui_couple_tick(&s_model,delta,!cd_welcome_active(&s_welcome));
    check_frame(scenario);
}

static bool couple_moved(const cd_person_t old[2])
{
    const cd_person_t *now=cd_ui_couple_state()->people;
    return old[0].x!=now[0].x || old[0].y!=now[0].y ||
           old[1].x!=now[1].x || old[1].y!=now[1].y;
}

static void test_couple_regions(void)
{
    s_welcome.phase=CD_WELCOME_DONE;
    cd_model_init(&s_model,OCT_6+12*3600);s_model.clock_synced=true;
    cd_ui_pointer(-1,-1,false);cd_ui_couple_init(17);
    cd_ui_visual_tick((400000u-cd_ui_life_ms())%300000u);
    assert(cd_ui_couple_visual_key(&s_model)==CD_COUPLE_ROOM_BIT);
    for(unsigned i=0;i<2;i++){
        cd_person_view_t p=cd_ui_couple_person(&s_model,i);
        assert(p.x==(i?208:172) && p.y==(i?143:139) && p.pose==CD_PERSON_FRONT);
    }
    check_frame("two people visible immediately, no added photo");
    cd_ui_press(&s_model,284,46);
    assert(s_model.popup==CD_POPUP_NONE);
    check_frame("removed photo coordinates remain normal background");
    cd_person_t frozen[2];memcpy(frozen,cd_ui_couple_state()->people,sizeof(frozen));
    for(unsigned tick=0;tick<10;tick++){
        couple_frame(80,"ordinary background touch briefly pauses wandering");
        assert(memcmp(frozen,cd_ui_couple_state()->people,sizeof(frozen))==0);
    }

    unsigned moving_frames=0,waiting_frames=0;
    for(unsigned tick=0;tick<4500;tick++){
        cd_person_t old[2];memcpy(old,cd_ui_couple_state()->people,sizeof(old));
        couple_frame(80,"six minutes of walking poses, positions and exact trail restoration");
        if(couple_moved(old))moving_frames++;else waiting_frames++;
        if(tick==700){
            cd_ui_press(&s_model,315,105);
            assert(s_model.popup==CD_POPUP_FOCUS);
            check_frame("original focus popup while people freeze");
            memcpy(frozen,cd_ui_couple_state()->people,sizeof(frozen));
            for(unsigned wait=0;wait<30;wait++){
                couple_frame(80,"modal covers stationary people without trails");
                assert(memcmp(frozen,cd_ui_couple_state()->people,sizeof(frozen))==0);
            }
            cd_ui_press(&s_model,160,151);
            assert(s_model.popup==CD_POPUP_NONE && s_model.focus_running);
            check_frame("original focus action closes modal and keeps people still");
            for(unsigned wait=0;wait<30;wait++){
                couple_frame(80,"running focus freezes walking without changing original focus UI");
                assert(memcmp(frozen,cd_ui_couple_state()->people,sizeof(frozen))==0);
            }
            s_model.focus_running=false;s_model.focus_remaining_ms=CD_FOCUS_DURATION_MS;
            check_frame("focus ends, room pair resumes after interaction quiet time");
        }
        if(tick==2100){
            s_model.notice_ms=6000;
            memcpy(frozen,cd_ui_couple_state()->people,sizeof(frozen));
            for(unsigned wait=0;wait<30;wait++){
                couple_frame(80,"notice overlays stationary room pair");
                assert(memcmp(frozen,cd_ui_couple_state()->people,sizeof(frozen))==0);
            }
            s_model.notice_ms=0;check_frame("notice removed from room without person trails");
        }
    }
    assert(moving_frames>150 && waiting_frames>150);
    memcpy(frozen,cd_ui_couple_state()->people,sizeof(frozen));
    cd_ui_couple_tick(&s_model,UINT32_MAX,false);
    check_frame("welcome gate rejects large walking catch-up");
    assert(memcmp(frozen,cd_ui_couple_state()->people,sizeof(frozen))==0);
    bool resumed=false;
    for(unsigned tick=0;tick<100;tick++){
        couple_frame(80,"ordinary room resumes walking after frozen interval");
        if(couple_moved(frozen))resumed=true;
    }
    assert(resumed);

    /* Skip the twenty-minute wait in a fixture only. The production
     * chance draw and same seated rendering path are still exercised. */
    cd_ui_visual_tick((400000u-cd_ui_life_ms())%300000u);
    cd_ui_pointer(-1,-1,false);cd_ui_couple_init(2);
    cd_ui_couple_tick(&s_model,1200000,true);
    assert(cd_ui_couple_visual_key(&s_model)&CD_COUPLE_SOFA_BIT);
    for(unsigned i=0;i<2;i++){
        cd_person_view_t p=cd_ui_couple_person(&s_model,i);
        assert(p.x==(i?88:71) && p.y==109 && p.pose==CD_PERSON_SIT);
    }
    check_frame("same pair sits on sofa, prior floor silhouettes are restored");
    memcpy(frozen,cd_ui_couple_state()->people,sizeof(frozen));
    while(cd_ui_couple_state()->sofa_ms>80){
        couple_frame(80,"two static sofa guests with no duplicate floor people");
        assert(memcmp(frozen,cd_ui_couple_state()->people,sizeof(frozen))==0);
    }
    couple_frame(80,"sofa expiry restores ordinary wandering pair");
    assert(!(cd_ui_couple_visual_key(&s_model)&CD_COUPLE_SOFA_BIT));
    cd_ui_visual_tick((400000u-cd_ui_life_ms())%300000u);
    cd_ui_couple_init(2);cd_ui_couple_tick(&s_model,1200000,true);
    assert(cd_ui_couple_state()->sofa_ms);
    check_frame("sofa second appearance fixture");
    cd_ui_press(&s_model,315,105);
    check_frame("key operation cancels sofa and opens original focus menu");
    assert(!cd_ui_couple_state()->sofa_ms && s_model.popup==CD_POPUP_FOCUS);
    s_model.popup=CD_POPUP_NONE;
    cd_ui_couple_init(2);cd_model_replay_anniversary(&s_model);
    check_frame("anniversary hides ordinary pair during original cat approach");
    bool finale_seen=false;
    for(unsigned tick=0;tick<160;tick++){
        couple_frame(80,"original anniversary, appended still and wandering pair restoration");
        uint32_t key=cd_ui_couple_visual_key(&s_model);
        if(key&CD_COUPLE_FINALE_BIT){
            assert(!(key&CD_COUPLE_ROOM_BIT));finale_seen=true;
        }
    }
    assert(finale_seen && !s_model.celebration_active && !cd_ui_couple_state()->finale_ms);
    assert(cd_ui_couple_visual_key(&s_model)&CD_COUPLE_ROOM_BIT);
    /* Same software-only path on the actual anniversary date, including
       modal suppression during the added still and replay cancellation. */
    cd_ui_couple_init(2);cd_model_set_time(&s_model,OCT_7);
    for(unsigned tick=0;tick<150;tick++){
        if(tick==110)s_model.popup=CD_POPUP_WEATHER;
        if(tick==120)s_model.popup=CD_POPUP_NONE;
        couple_frame(80,"real-date branch and interrupted finale restore pair");
    }
    cd_ui_couple_init(1);
}

int main(int argc, char **argv)
{
    unsigned count = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 10000;
    size_t logical_bytes = CD_WIDTH*CD_HEIGHT*sizeof(uint16_t);
    s_background = malloc(logical_bytes);
    s_frame = malloc(logical_bytes);
    s_physical = calloc(PHYSICAL_WIDTH*PHYSICAL_HEIGHT, sizeof(uint16_t));
    reference_frame = malloc(logical_bytes);
    previous_expected = calloc(CD_WIDTH*CD_HEIGHT, sizeof(uint16_t));
    assert(s_background && s_frame && s_physical && reference_frame && previous_expected);
    cd_render_background(s_background);
    test_transitions();
    test_mixed_frames(count);
    test_ambient_regions();
    test_room_cycle();
    test_couple_regions();
    /* Exercise the actual new presenter with date corrections, stationary
       waiting, brightness, all wipe tiles and the first room refresh. */
    for(unsigned variant=0;variant<2;variant++) {
        cd_model_init(&s_model,variant?OCT_7:OCT_6);
        cd_welcome_init(&s_welcome,&s_model);
        s_welcome_presented_once=false;
        check_frame("welcome initial night");
        for(unsigned tick=0;tick<110;tick++) {
            cd_model_tick(&s_model,100);cd_ui_visual_tick(100);
            if(tick==45)cd_model_set_time(&s_model,variant?OCT_6:OCT_7);
            if(tick==51)s_model.brightness=20;
            if(tick==60)assert(cd_welcome_press(&s_welcome));
            bool active=cd_welcome_active(&s_welcome);
            (void)cd_welcome_tick(&s_welcome,100,&s_model);
            cd_ui_couple_tick(&s_model,100,!cd_welcome_active(&s_welcome));
            if(active && !cd_welcome_active(&s_welcome))s_presented_once=false;
            check_frame("welcome animation wait touch doorway wipe home");
        }
    }
    printf("CatDesk presenter: %u frames, every 800x480 pixel and dirty rectangle checked; %u invalidations passed.\n",
        checked_frames, invalidation_calls);
    free(s_background); free(s_frame); free(s_physical);
    free(reference_frame); free(previous_expected);
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=10_000)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    if args.frames < 1:
        parser.error("--frames must be positive")
    root = Path(__file__).resolve().parent.parent
    component = root / "firmware/components/catdesk_ui"
    source = (component / "catdesk_ui.c").read_text()
    start = source.index("static void prepare_brightness")
    end = source.index("static void serial_task", start)
    production_presenter = source[start:end]
    with tempfile.TemporaryDirectory(prefix="catdesk-presenter-") as directory:
        work = Path(directory)
        generated = work / "presenter_test.c"
        binary = work / "presenter_test"
        generated.write_text(PREFIX + production_presenter + SUFFIX)
        command = [
            args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
            "-I" + str(component / "include"), str(generated),
            *(str(component / name) for name in (
                "catdesk_model.c", "catdesk_render.c", "catdesk_couple.c", "catdesk_room.c", "catdesk_welcome.c", "catdesk_cats.c", "catdesk_font.c", "pixel_ui.c"
            )), "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary), str(args.frames)], check=True)


if __name__ == "__main__":
    main()
