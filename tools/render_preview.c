#include "catdesk_render.h"
#include "catdesk_couple.h"
#include "catdesk_room.h"
#include "pixel_ui.h"
#include "pixel_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Host adapter for the actual firmware model and renderer. No art is
 * duplicated here. Usage: output.ppm MODE [elapsed_ms [TAPS]] [--raw]
 * MODE: world, world-connected, home, home-normal, home-connected, home-anniversary, anniversary,
 * anniversary-popup, anniversary-day-popup (component inspection), anniversary-replay,
 * todo, todo-complete,
 * todo-progress, focus, focus-home, focus-running, focus-paused,
 * focus-completed, focus-completed-popup, weather, mac, mac-connected,
 * mac-battery, settings, settings-min, settings-max, components,
 * room-day, room-dusk, room-night, room-life, room-life-inspect,
 * couple-walk, couple-sofa, couple-sofa-inspect, couple-sprites,
 * focus-pressed, todo-pressed, anniversary-pressed, close-pressed.
 * TAPS: none, orange, black, both, rapid, live.
 * The timeline is stepped at the model's real 50 ms animation cadence.
 */
typedef struct { unsigned at_ms; cd_cat_kind_t kind; } tap_event_t;
typedef enum { POST_NONE, POST_ANNIVERSARY_INSPECTION, POST_ANNIVERSARY_REPLAY, POST_FOCUS, POST_PAUSE,
               POST_ROOM_LIFE_INSPECTION } post_action_t;

/* Scenario setup calls the same UI actions and clock APIs as firmware.
 * Mac transport values are applied to the model without opening USB. */
static int setup_scenario(cd_model_t *m,const char *mode,post_action_t *post)
{
    *post=POST_NONE;
    if(!strcmp(mode,"world"))return 1;
    if(!strcmp(mode,"world-connected")){m->mac_connected=true;return 1;}
    if(!strcmp(mode,"components"))return 1;
    if(!strcmp(mode,"couple-sprites") || !strcmp(mode,"couple-walk") ||
       !strcmp(mode,"couple-sofa") || !strcmp(mode,"couple-sofa-inspect"))return 1;
    if(!strcmp(mode,"home"))return 1;
    /* The firmware link sets clock_synced after accepting a Mac clock.
     * These host fixtures apply that same transport-owned flag. */
    if(!strcmp(mode,"room-day")){cd_model_set_time(m,1791259200);m->clock_synced=true;return 1;}
    if(!strcmp(mode,"room-dusk")){cd_model_set_time(m,1791280800);m->clock_synced=true;return 1;}
    if(!strcmp(mode,"room-night") || !strcmp(mode,"room-life") || !strcmp(mode,"room-life-inspect")){
        cd_model_set_time(m,1791295200);
        m->clock_synced=true;
        if(!strcmp(mode,"room-life-inspect"))*post=POST_ROOM_LIFE_INSPECTION;
        return 1;
    }
    if(!strcmp(mode,"home-normal")){cd_model_set_time(m,1791456120);return 1;}
    if(!strcmp(mode,"home-connected")){m->mac_connected=true;return 1;}
    if(!strcmp(mode,"anniversary") || !strcmp(mode,"home-anniversary") || !strcmp(mode,"anniversary-day-popup") || !strcmp(mode,"anniversary-replay")){
        cd_model_set_time(m,1791369720);
        if(!strcmp(mode,"anniversary-day-popup"))*post=POST_ANNIVERSARY_INSPECTION;
        if(!strcmp(mode,"anniversary-replay"))*post=POST_ANNIVERSARY_REPLAY;
        return 1;
    }
    if(!strcmp(mode,"anniversary-popup") || !strcmp(mode,"anniversary-pressed")){cd_ui_press(m,210, 42);return 1;}
    if(!strcmp(mode,"todo") || !strcmp(mode,"todo-pressed") || !strcmp(mode,"todo-complete") || !strcmp(mode,"todo-progress")){
        cd_ui_press(m,265, 106);
        if(!strcmp(mode,"todo-complete") || !strcmp(mode,"todo-progress")){
            for(unsigned count=m->todo_count;count<CD_TODO_CAPACITY;++count)cd_ui_press(m,266,172);
            for(unsigned row=0;row<CD_TODO_CAPACITY;++row)
                if(!strcmp(mode,"todo-complete") || row==0 || row==1 || row==3)
                    cd_ui_press(m,140,84+(int)row*18);
        }
        return 1;
    }
    if(!strcmp(mode,"focus") || !strcmp(mode,"focus-pressed")){cd_ui_press(m,315, 105);return 1;}
    if(!strcmp(mode,"focus-home") || !strcmp(mode,"focus-running") || !strcmp(mode,"focus-paused") ||
       !strcmp(mode,"focus-completed") || !strcmp(mode,"focus-completed-popup")){
        cd_ui_press(m,315, 105);cd_ui_press(m,160,151);
        if(!strcmp(mode,"focus-running"))*post=POST_FOCUS;
        else if(!strcmp(mode,"focus-paused"))*post=POST_PAUSE;
        else if(!strcmp(mode,"focus-completed") || !strcmp(mode,"focus-completed-popup")){
            cd_model_tick(m,CD_FOCUS_DURATION_MS-600);
            if(!strcmp(mode,"focus-completed-popup"))*post=POST_FOCUS;
        }
        return 1;
    }
    if(!strcmp(mode,"weather") || !strcmp(mode,"close-pressed")){cd_ui_press(m,75, 42);return 1;}
    if(!strcmp(mode,"mac") || !strcmp(mode,"mac-connected") || !strcmp(mode,"mac-battery")){
        m->mac_connected=strcmp(mode,"mac")!=0;
        m->mac_charging=!strcmp(mode,"mac-connected");
        cd_ui_press(m,291, 103);return 1;
    }
    if(!strcmp(mode,"settings") || !strcmp(mode,"settings-min") || !strcmp(mode,"settings-max")){
        cd_ui_press(m,373, 23);
        if(!strcmp(mode,"settings-min"))cd_ui_drag(m,130,137);
        else if(!strcmp(mode,"settings-max"))cd_ui_drag(m,270,137);
        return 1;
    }
    return 0;
}

static uint16_t dim(uint16_t c, unsigned pct)
{
    if(pct>=100)return c;
    unsigned level=35+pct*65/100;
    return (uint16_t)(((((c>>11)&31)*level/100)<<11)|
                      ((((c>>5)&63)*level/100)<<5)|((c&31)*level/100));
}

static void trigger_taps(cd_model_t *m,const tap_event_t *events,unsigned count,
                         unsigned *next,unsigned now)
{
    while(*next<count && events[*next].at_ms==now){
        cd_cat_t *cat=&m->cats[events[*next].kind];
        cd_ui_press(m,cat->x+20,cat->y+20);
        ++*next;
    }
}

int main(int argc,char **argv)
{
    if(argc<2){fprintf(stderr,"Usage: %s output.ppm MODE [elapsed_ms [TAPS]] [--raw]\n",argv[0]);return 1;}
    const char *mode=argc>2?argv[2]:"home";
    unsigned elapsed=!strcmp(mode,"anniversary")?4000:0;
    if(argc>3 && strcmp(argv[3],"--raw")){
        char *end=NULL;errno=0;unsigned long value=strtoul(argv[3],&end,10);
        if(errno || !end || *end || value>600000){fprintf(stderr,"Invalid elapsed_ms\n");return 1;}
        elapsed=(unsigned)value;
    }
    const char *taps=argc>4 && strcmp(argv[4],"--raw")?argv[4]:"none";
    int raw=0;for(int i=2;i<argc;i++)if(!strcmp(argv[i],"--raw"))raw=1;
    uint16_t *bg=calloc(CD_WIDTH*CD_HEIGHT,sizeof(*bg)),*p=calloc(CD_WIDTH*CD_HEIGHT,sizeof(*p));
    if(!bg || !p){free(bg);free(p);return 2;}
    cd_model_t m;
    cd_ui_pointer(-1,-1,false);
    cd_model_init(&m,1791283320);
    cd_ui_couple_init(1);
    post_action_t post;
    if(!setup_scenario(&m,mode,&post)){fprintf(stderr,"Unknown mode: %s\n",mode);free(bg);free(p);return 1;}
    tap_event_t events[8];unsigned count=0;
    if(!strcmp(taps,"orange"))events[count++]=(tap_event_t){0,CD_CAT_ORANGE};
    else if(!strcmp(taps,"black"))events[count++]=(tap_event_t){0,CD_CAT_BLACK};
    else if(!strcmp(taps,"both")){
        events[count++]=(tap_event_t){0,CD_CAT_ORANGE};events[count++]=(tap_event_t){0,CD_CAT_BLACK};
    }else if(!strcmp(taps,"rapid")){
        for(unsigned i=0;i<5;i++)events[count++]=(tap_event_t){i*100,CD_CAT_ORANGE};
    }else if(!strcmp(taps,"live")){
        events[count++]=(tap_event_t){480,CD_CAT_ORANGE};
        events[count++]=(tap_event_t){1920,CD_CAT_BLACK};
        for(unsigned i=0;i<5;i++)events[count++]=(tap_event_t){3000+i*120,CD_CAT_ORANGE};
    }else if(strcmp(taps,"none")){fprintf(stderr,"Unknown tap scenario: %s\n",taps);free(bg);free(p);return 1;}
    if(!strcmp(mode,"couple-sofa") || !strcmp(mode,"couple-sofa-inspect")){
        /* Software inspection: skip the quiet 20min wait and select the
         * first successful PRNG opportunity, then run production timers. */
        cd_ui_couple_init(2);cd_ui_visual_tick(100000);
        cd_ui_couple_tick(&m,1200000,true);
    }
    unsigned next=0,now=0;
    trigger_taps(&m,events,count,&next,now);
    while(now<elapsed){
        unsigned delta=elapsed-now;if(delta>50)delta=50;
        if(next<count && events[next].at_ms>now && events[next].at_ms-now<delta)delta=events[next].at_ms-now;
        cd_model_tick(&m,delta);cd_ui_visual_tick(delta);cd_ui_couple_tick(&m,delta,true);now+=delta;trigger_taps(&m,events,count,&next,now);
    }
    /* Oct 7's real frame tap replays the event. The 730-day window below
     * is explicitly an inspection fixture for the shared detail artwork. */
    if(post==POST_ANNIVERSARY_INSPECTION)m.popup=CD_POPUP_ANNIVERSARY;
    /* A schedule inspection suppresses a model's brief natural approach
     * event so every room pose can be compared at its intended timestamp.
     * The room-life animation leaves the real model untouched. */
    if(post==POST_ROOM_LIFE_INSPECTION)m.approach_active=false;
    if(post==POST_ANNIVERSARY_REPLAY){
        cd_ui_press(&m,210,42);cd_model_tick(&m,200);cd_ui_visual_tick(200);
        cd_ui_couple_tick(&m,200,true);
    }
    if(post==POST_FOCUS || post==POST_PAUSE){
        cd_ui_press(&m,315, 105);
        if(post==POST_PAUSE)cd_ui_press(&m,160,151);
    }
    if(!strcmp(mode,"focus-pressed"))cd_ui_pointer(160,151,true);
    else if(!strcmp(mode,"todo-pressed"))cd_ui_pointer(266,172,true);
    else if(!strcmp(mode,"anniversary-pressed"))cd_ui_pointer(200,167,true);
    else if(!strcmp(mode,"close-pressed"))cd_ui_pointer(276,63,true);
    cd_render_background(bg);
    if(!strcmp(mode,"world") || !strcmp(mode,"world-connected"))cd_render_world(p,bg,&m);
    else cd_render_frame(p,bg,&m);
    if(!strcmp(mode,"couple-sprites")){
        for(int pixel=0;pixel<CD_WIDTH*CD_HEIGHT;pixel++)p[pixel]=cd_color(PT_PAPER);
        const char *poses[]={"FRONT","SIDE","SIT","WALK A","WALK B"};
        drawPixelLabel(p,CD_WIDTH,136,20,"CatDesk couple",PT_OUTLINE,1);
        for(unsigned person=0;person<2;person++){
            drawPixelLabel(p,CD_WIDTH,30,65+(int)person*74,person?"GIRL":"BOY",PT_WOOD_DARK,1);
            for(int pose=0;pose<5;pose++){
                cd_couple_draw_person(p,77+pose*62,58+(int)person*74,person,(cd_person_pose_t)pose,false);
                drawPixelLabel(p,CD_WIDTH,70+pose*62,89+(int)person*74,poses[pose],PT_OUTLINE,1);
            }
        }
        drawPixelLabel(p,CD_WIDTH,124,207,"16x24 / 9 COLORS",PT_WOOD_DARK,1);
    }
    if(!strcmp(mode,"components")){
        drawPixelWindow(p,CD_WIDTH,75,44,250,152,"Pixel controls");
        drawPixelButton(p,CD_WIDTH,94,83,66,21,"Normal",PIXEL_BUTTON_NORMAL);
        drawPixelButton(p,CD_WIDTH,167,83,66,21,"Pressed",PIXEL_BUTTON_PRESSED);
        drawPixelButton(p,CD_WIDTH,240,83,66,21,"Selected",PIXEL_BUTTON_SELECTED);
        drawPixelCheckbox(p,CD_WIDTH,97,125,10,false);
        drawPixelLabel(p,CD_WIDTH,114,122,"TODO",PT_TEXT_DARK,1);
        drawPixelCheckbox(p,CD_WIDTH,197,125,10,true);
        drawPixelLabel(p,CD_WIDTH,214,122,"DONE",PT_WOOD_DARK,1);
        drawPixelSlider(p,CD_WIDTH,97,155,206,18,60,20,100);
    }
    FILE *f=fopen(argv[1],"wb");if(!f){free(bg);free(p);return 2;}
    fprintf(f,"P6\n%d %d\n255\n",CD_WIDTH*2,CD_HEIGHT*2);
    unsigned char row[CD_WIDTH*2*3];
    for(int y=0;y<CD_HEIGHT*2;y++){
        for(int x=0;x<CD_WIDTH*2;x++){
            unsigned c=p[(y/2)*CD_WIDTH+x/2];if(!raw)c=dim((uint16_t)c,m.brightness);
            row[x*3]=(unsigned char)(((c>>11)&31)*255/31);
            row[x*3+1]=(unsigned char)(((c>>5)&63)*255/63);
            row[x*3+2]=(unsigned char)((c&31)*255/31);
        }
        if(fwrite(row,1,sizeof(row),f)!=sizeof(row)){fclose(f);free(bg);free(p);return 2;}
    }
    int status=fclose(f);
    cd_room_pet_pose_t orange=cd_room_pet_pose(&m,CD_CAT_ORANGE,cd_ui_life_ms());
    cd_room_pet_pose_t black=cd_room_pet_pose(&m,CD_CAT_BLACK,cd_ui_life_ms());
    uint32_t couple_key=cd_ui_couple_visual_key(&m);
    cd_person_view_t boy=cd_ui_couple_person(&m,0),girl=cd_ui_couple_person(&m,1);
    int people_visible=(couple_key&(CD_COUPLE_ROOM_BIT|CD_COUPLE_FINALE_BIT))!=0;
    fprintf(stderr,"{\"elapsed_ms\":%u,\"orange\":%d,\"black\":%d,\"phase\":%d,\"heart_ms\":%u,\"popup\":%d,\"bubble_ms\":%u,\"brightness\":%u,"
            "\"mac_connected\":%d,\"mac_charging\":%d,\"todo_count\":%u,\"todo_checked\":%u,\"focus_running\":%d,"
            "\"focus_remaining_ms\":%u,\"notice_ms\":%u,\"elapsed_days\":%ld,\"date\":\"%04d-%02d-%02d\",\"pointer_active\":%d,\"inspection_only\":%d,"
            "\"inspection_kind\":\"%s\",\"clock_synced\":%d,\"hour\":%d,\"period\":%d,\"life_ms\":%u,\"approach_active\":%d,"
            "\"rendered_orange\":{\"x\":%d,\"y\":%d,\"state\":%d,\"frame\":%d,\"direction\":%d,\"activity\":%d},"
            "\"rendered_black\":{\"x\":%d,\"y\":%d,\"state\":%d,\"frame\":%d,\"direction\":%d,\"activity\":%d},"
            "\"couple_key\":%u,\"couple_sofa_ms\":%u,\"couple_finale_ms\":%u,"
            "\"people\":[{\"x\":%d,\"y\":%d,\"direction\":%d,\"pose\":%d,\"visible\":%d},"
            "{\"x\":%d,\"y\":%d,\"direction\":%d,\"pose\":%d,\"visible\":%d}]}\n",
            elapsed,m.cats[0].state,m.cats[1].state,m.celebration_phase,m.celebration_ms,m.popup,m.cats[0].bubble_ms,m.brightness,
            m.mac_connected,m.mac_charging,m.todo_count,m.todo_checked,m.focus_running,m.focus_remaining_ms,m.notice_ms,
            (long)m.elapsed_days,m.year,m.month,m.day,cd_ui_has_pointer(),
            post==POST_ANNIVERSARY_INSPECTION || post==POST_ROOM_LIFE_INSPECTION || !strcmp(mode,"couple-sofa-inspect") || !strcmp(mode,"couple-sofa"),
            !strcmp(mode,"couple-sofa-inspect") || !strcmp(mode,"couple-sofa")?"couple-sofa":post==POST_ROOM_LIFE_INSPECTION?"room-life":post==POST_ANNIVERSARY_INSPECTION?"anniversary-detail":"none",
            m.clock_synced,m.hour,cd_room_period(&m),cd_ui_life_ms(),m.approach_active,
            orange.x,orange.y,orange.state,orange.frame,orange.direction,orange.activity,
            black.x,black.y,black.state,black.frame,black.direction,black.activity,
            couple_key,cd_ui_couple_state()->sofa_ms,cd_ui_couple_state()->finale_ms,
            boy.x,boy.y,boy.direction,boy.pose,people_visible,
            girl.x,girl.y,girl.direction,girl.pose,people_visible);
    free(bg);free(p);return status?2:0;
}
