#include "catdesk_couple.h"
#include "catdesk_render.h"
#include "pixel_theme.h"
#include "pixel_ui.h"
#include "catdesk_cats.h"
#include "catdesk_font.h"
#include <string.h>

#include "catdesk_people.inc"

#define RGB565(c) ((uint16_t)((((c)>>19)&31)<<11 | (((c)>>10)&63)<<5 | (((c)>>3)&31)))
static const uint16_t colors[2][10] = {
    {0, RGB565(PT_OUTLINE), RGB565(PT_NIGHT), RGB565(PT_CAT_BLACK_LIGHT),
     RGB565(PT_PAPER), RGB565(PT_CAT_NOSE), RGB565(PT_BLUE_LIGHT),
     RGB565(PT_NIGHT_BLUE), RGB565(PT_WOOD), RGB565(PT_WOOD_DARK)},
    {0, RGB565(PT_OUTLINE), RGB565(PT_WOOD_DARK), RGB565(PT_WOOD),
     RGB565(PT_PAPER), RGB565(PT_CAT_NOSE), RGB565(PT_PAPER_LIGHT),
     RGB565(PT_PAPER_DARK), RGB565(PT_ROSE_LIGHT), RGB565(PT_RED)}
};
#undef RGB565

static void block(uint16_t *p,int x,int y,int w,int h,unsigned color)
{
    uint16_t c=cd_color(color);
    for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)
        if(xx>=0 && xx<CD_WIDTH && yy>=0 && yy<CD_HEIGHT)p[yy*CD_WIDTH+xx]=c;
}

void cd_couple_draw_person(uint16_t *p,int x,int y,unsigned person,
                           cd_person_pose_t pose,bool mirror)
{
    if(person>1 || pose<CD_PERSON_FRONT || pose>CD_PERSON_WALK_B)return;
    for(int yy=0;yy<CD_PERSON_H;yy++)for(int xx=0;xx<CD_PERSON_W;xx++){
        int source=mirror?CD_PERSON_W-1-xx:xx;
        uint8_t packed=people[person][pose][yy][source/2];
        unsigned index=source%2?packed&15u:packed>>4;
        int px=x+xx,py=y+yy;
        if(index && px>=0 && px<CD_WIDTH && py>=0 && py<CD_HEIGHT)
            p[py*CD_WIDTH+px]=colors[person][index];
    }
}

static uint32_t subtract(uint32_t value,uint32_t delta)
{return value>delta?value-delta:0;}

static uint32_t random_next(cd_couple_t *s)
{
    uint32_t x=s->random_state;
    x^=x<<13;x^=x>>17;x^=x<<5;
    s->random_state=x;
    return x;
}

void cd_couple_init(cd_couple_t *s,uint32_t seed)
{
    memset(s,0,sizeof(*s));
    s->random_state=seed?seed:0x43445043u;
    s->walk_random_state=s->random_state^0x57414C4Bu;
    if(!s->walk_random_state)s->walk_random_state=1;
    s->wait_ms=1200000u; /* Sofa only; floor characters are immediately visible. */
    s->people[0]=(cd_person_t){.wait_ms=600,.x=172,.y=139,.target_x=172,.target_y=139,.direction=1};
    s->people[1]=(cd_person_t){.wait_ms=1600,.x=208,.y=143,.target_x=208,.target_y=143,.direction=-1};
}

void cd_couple_touch(cd_couple_t *s)
{
    s->quiet_ms=30000u;
    s->walk_quiet_ms=1200u;
    s->sofa_ms=0;
}

static int absolute(int value){return value<0?-value:value;}
static uint32_t walk_random_next(cd_couple_t *s)
{
    uint32_t x=s->walk_random_state;
    x^=x<<13;x^=x>>17;x^=x<<5;
    s->walk_random_state=x;
    return x;
}
static int clamp(int value,int low,int high)
{return value<low?low:value>high?high:value;}

static void choose_target(cd_couple_t *s,unsigned person)
{
    cd_person_t *p=&s->people[person];
    const cd_person_t *other=&s->people[1u-person];
    int dx=(int)(walk_random_next(s)%49u)-24;
    if(absolute(dx)<8)dx=dx<0?-12:12;
    int x=clamp(p->x+dx,150,240);
    /* Keep the two silhouettes distinct in the furniture-free corridor.
     * The order never swaps; no pathfinding or collision AI is needed. */
    x=person?clamp(x,other->x+16,240):clamp(x,150,other->x-16);
    int y=clamp(p->y+(int)(walk_random_next(s)%11u)-5,128,146);
    p->target_x=(int16_t)x;p->target_y=(int16_t)y;
    p->direction=x>=p->x?1:-1;
    p->step=0;p->step_ms=0;
    if(x==p->x && y==p->y)p->wait_ms=1000u;
}

static void walk_person(cd_couple_t *s,unsigned person,uint32_t delta)
{
    cd_person_t *p=&s->people[person];
    bool stopped=p->x==p->target_x && p->y==p->target_y;
    if(stopped){
        p->wait_ms=subtract(p->wait_ms,delta);
        if(p->wait_ms)return;
        choose_target(s,person);
    }
    /* At most eight pixels after any scheduling stall; huge deltas cannot
     * overflow timers or create an unbounded animation catch-up loop. */
    uint32_t accumulated=p->step_ms+(delta>880u?880u:delta);
    unsigned steps=accumulated/110u;
    p->step_ms=accumulated%110u;
    for(unsigned i=0;i<steps;i++){
        int x=p->x+(p->target_x>p->x?1:p->target_x<p->x?-1:0);
        int y=p->y+(p->target_y>p->y?1:p->target_y<p->y?-1:0);
        const cd_person_t *other=&s->people[1u-person];
        if(absolute(x-other->x)<16){
            p->target_x=p->x;p->target_y=p->y;p->wait_ms=1000u;
            p->step=0;p->step_ms=0;return;
        }
        p->x=(int16_t)x;p->y=(int16_t)y;p->step=(uint8_t)((p->step+1u)%4u);
        if(p->x==p->target_x && p->y==p->target_y){
            p->wait_ms=1800u+walk_random_next(s)%4201u;
            p->step=0;p->step_ms=0;return;
        }
    }
}

void cd_couple_tick(cd_couple_t *s,const cd_model_t *m,uint32_t delta,
                    uint32_t life_ms,bool home,bool contact)
{
    s->walk_quiet_ms=contact?1200u:subtract(s->walk_quiet_ms,delta);
    s->quiet_ms=contact?30000u:subtract(s->quiet_ms,delta);
    s->wait_ms=subtract(s->wait_ms,delta);
    s->sofa_ms=subtract(s->sofa_ms,delta);
    s->finale_ms=subtract(s->finale_ms,delta);

    /* Append one still after the original five-second cat/scroll scene.
     * A date cancellation, new replay, welcome or modal cannot queue a
     * delayed finale. Cats and all business timers continue unchanged. */
    if(m->celebration_active){
        s->finale_ms=0;
        s->hearts_seen=home && m->popup==CD_POPUP_NONE &&
            m->celebration_phase==CD_CELEBRATION_HEARTS;
        s->heart_day=m->local_day;
    }else if(s->hearts_seen){
        if(home && m->popup==CD_POPUP_NONE && !m->notice_ms && m->local_day==s->heart_day)
            s->finale_ms=2500u;
        s->hearts_seen=false;
    }
    if(!home || m->popup!=CD_POPUP_NONE || m->notice_ms ||
       (s->finale_ms && m->local_day!=s->heart_day))s->finale_ms=0;

    /* Avoid both entry/exit legs of the sofa cat, including the next loop.
     * The latest possible 15s visitor at 270s ends long before 330s. */
    uint32_t room=life_ms%300000u;
    bool safe=home && !contact && !s->quiet_ms && !s->finale_ms &&
        !m->celebration_active && m->popup==CD_POPUP_NONE &&
        !m->focus_running && !m->notice_ms && room>=65000u;
    if(!safe)s->sofa_ms=0;
    bool walk=home && !contact && !s->walk_quiet_ms && !s->sofa_ms &&
        !s->finale_ms && !m->celebration_active && m->popup==CD_POPUP_NONE &&
        !m->focus_running && !m->notice_ms;
    if(walk){walk_person(s,0,delta);walk_person(s,1,delta);}
    if(s->wait_ms || s->sofa_ms)return;

    /* One independent draw per 2-4 minute opportunity, 1/6 probability.
     * Skipped opportunities are discarded; large deltas cannot burst. */
    s->wait_ms=120000u+random_next(s)%120001u;
    if(safe && room<=270000u && random_next(s)%6u==0u){
        s->sofa_ms=8000u+random_next(s)%7001u;
        s->wait_ms=1200000u;
    }
}

uint32_t cd_couple_visual_key(const cd_couple_t *s,const cd_model_t *m)
{
    bool clear=m->popup==CD_POPUP_NONE && !m->notice_ms && !m->celebration_active;
    uint32_t key=clear && !m->focus_running && s->sofa_ms?CD_COUPLE_SOFA_BIT:0u;
    if(clear && s->finale_ms)key|=CD_COUPLE_FINALE_BIT;
    else if(!m->celebration_active)key|=CD_COUPLE_ROOM_BIT;
    return key;
}

cd_person_view_t cd_couple_person(const cd_couple_t *s,const cd_model_t *m,unsigned person)
{
    cd_person_view_t view={0,0,1,CD_PERSON_FRONT};
    if(person>1)return view;
    uint32_t key=cd_couple_visual_key(s,m);
    if(key&CD_COUPLE_FINALE_BIT){
        view.x=person?202:183;view.y=178;return view;
    }
    if(key&CD_COUPLE_SOFA_BIT){
        view.x=person?88:71;view.y=109;view.pose=CD_PERSON_SIT;return view;
    }
    const cd_person_t *p=&s->people[person];
    view.x=p->x;view.y=p->y;view.direction=p->direction;
    bool moving=p->x!=p->target_x || p->y!=p->target_y;
    if(moving)view.pose=p->step<2?CD_PERSON_WALK_A:CD_PERSON_WALK_B;
    return view;
}

void cd_couple_draw_room(uint16_t *pixels,const cd_couple_t *s,const cd_model_t *m)
{
    if(!(cd_couple_visual_key(s,m)&CD_COUPLE_ROOM_BIT))return;
    for(unsigned person=0;person<2;person++){
        cd_person_view_t view=cd_couple_person(s,m,person);
        if(view.pose!=CD_PERSON_SIT)block(pixels,view.x+4,view.y+24,9,1,PT_WOOD_DARK);
        cd_couple_draw_person(pixels,view.x,view.y,person,view.pose,view.direction<0 && view.pose!=CD_PERSON_FRONT);
    }
}

void cd_couple_draw_sofa(uint16_t *p)
{
    cd_couple_draw_person(p,71,109,0,CD_PERSON_SIT,false);
    cd_couple_draw_person(p,88,109,1,CD_PERSON_SIT,false);
}

void cd_couple_draw_finale(uint16_t *p)
{
    /* Same foot line and room perspective as the cats; one small heart.
     * Cats retain their full original sprites, flanking the smaller NPCs. */
    block(p,154,202,25,2,PT_WOOD_DARK);
    block(p,184,202,13,2,PT_WOOD_DARK);
    block(p,203,202,13,2,PT_WOOD_DARK);
    block(p,234,202,25,2,PT_WOOD_DARK);
    catdesk_draw_cat(p,CD_WIDTH,142,166,0,CD_CAT_IDLE,0,false);
    cd_couple_draw_person(p,183,178,0,CD_PERSON_FRONT,false);
    cd_couple_draw_person(p,202,178,1,CD_PERSON_FRONT,false);
    catdesk_draw_cat(p,CD_WIDTH,222,166,1,CD_CAT_IDLE,0,true);
    drawPixelHeart(p,CD_WIDTH,196,169,1,PT_HEART);
    const char *date="2024.10.07";
    drawPixelLabel(p,CD_WIDTH,200-cd_text_width(date,1)/2,211,date,PT_PAPER_LIGHT,1);
}
