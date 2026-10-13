#include "catdesk_couple.h"
#include "catdesk_render.h"
#include "pixel_theme.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t pixels[CD_WIDTH*CD_HEIGHT], expected[CD_WIDTH*CD_HEIGHT];

static cd_model_t ordinary(void)
{
    cd_model_t m;
    cd_model_init(&m,1791283320);
    return m;
}

static void test_sprites(void)
{
    for(unsigned person=0;person<2;person++)for(int pose=0;pose<=CD_PERSON_WALK_B;pose++){
        memset(pixels,0,sizeof(pixels));
        cd_couple_draw_person(pixels,100,100,person,(cd_person_pose_t)pose,false);
        uint16_t palette[16];unsigned count=0;
        for(int y=0;y<CD_HEIGHT;y++)for(int x=0;x<CD_WIDTH;x++){
            uint16_t c=pixels[y*CD_WIDTH+x];
            if(!c)continue;
            assert(x>=100 && x<116 && y>=100 && y<124);
            unsigned color=0;while(color<count && palette[color]!=c)color++;
            if(color==count)palette[count++]=c;
            /* Every external silhouette edge has a single dark pixel. */
            if(!pixels[(y-1)*CD_WIDTH+x] || !pixels[(y+1)*CD_WIDTH+x] ||
               !pixels[y*CD_WIDTH+x-1] || !pixels[y*CD_WIDTH+x+1])
                assert(c==cd_color(PT_OUTLINE));
        }
        assert(count==9);
        cd_couple_draw_person(pixels,140,100,person,(cd_person_pose_t)pose,true);
        for(int y=0;y<24;y++)for(int x=0;x<16;x++){
            uint16_t mirrored=pixels[(100+y)*CD_WIDTH+100+15-x];
            assert(pixels[(100+y)*CD_WIDTH+140+x]==mirrored);
        }
        cd_couple_draw_person(pixels,-8,-12,person,(cd_person_pose_t)pose,false);
        cd_couple_draw_person(pixels,392,228,person,(cd_person_pose_t)pose,false);
    }
    /* Two tiny leg frames must actually differ, rather than two copies
     * of a stationary pose masquerading as a walk cycle. */
    for(unsigned person=0;person<2;person++){
        memset(pixels,0,sizeof(pixels));memset(expected,0,sizeof(expected));
        cd_couple_draw_person(pixels,100,100,person,CD_PERSON_WALK_A,false);
        cd_couple_draw_person(expected,100,100,person,CD_PERSON_WALK_B,false);
        assert(memcmp(pixels,expected,sizeof(pixels))!=0);
    }
}

static uint32_t trigger_sofa(cd_couple_t *s,const cd_model_t *m)
{
    for(unsigned attempt=0;attempt<96 && !s->sofa_ms;attempt++){
        s->wait_ms=0;
        cd_couple_tick(s,m,1,100000,true,false);
    }
    assert(s->sofa_ms>=8000u && s->sofa_ms<=15000u);
    return s->sofa_ms;
}

static void test_visitors(void)
{
    cd_model_t m=ordinary(),before=m;
    cd_couple_t s;
    cd_couple_init(&s,2);
    cd_couple_tick(&s,&m,1199999,100000,true,false);
    assert(s.sofa_ms==0 && s.wait_ms==1);
    uint32_t duration=trigger_sofa(&s,&m);
    assert(s.wait_ms==1200000);
    assert((cd_couple_visual_key(&s,&m)&CD_COUPLE_SOFA_BIT) &&
           (cd_couple_visual_key(&s,&m)&CD_COUPLE_ROOM_BIT));
    for(unsigned i=0;i<2;i++){
        cd_person_view_t v=cd_couple_person(&s,&m,i);
        assert(v.x==(i?88:71) && v.y==109 && v.pose==CD_PERSON_SIT);
    }
    memset(pixels,0,sizeof(pixels));memset(expected,0,sizeof(expected));
    cd_couple_draw_room(pixels,&s,&m);cd_couple_draw_sofa(expected);
    assert(memcmp(pixels,expected,sizeof(pixels))==0); /* Two seated people, no floor copies. */
    cd_person_t seated[2];memcpy(seated,s.people,sizeof(seated));
    cd_couple_tick(&s,&m,duration-1,100000+duration-1,true,false);
    assert(s.sofa_ms==1 && memcmp(seated,s.people,sizeof(seated))==0);
    cd_couple_tick(&s,&m,1,100000+duration,true,false);
    assert(!s.sofa_ms && s.wait_ms>1180000);
    assert(cd_couple_visual_key(&s,&m)&CD_COUPLE_ROOM_BIT);
    assert(memcmp(&m,&before,sizeof(m))==0);

    for(unsigned gate=0;gate<15;gate++){
        m=ordinary();cd_couple_init(&s,2);s.wait_ms=0;
        bool home=true,contact=false;uint32_t life=100000;
        if(gate<6)m.popup=(cd_popup_t)(gate+1);
        if(gate==6)m.focus_running=true;
        if(gate==7)m.notice_ms=100;
        if(gate==8)m.celebration_active=true;
        if(gate==9)home=false;
        if(gate==10)contact=true;
        if(gate==11)s.quiet_ms=500;
        if(gate==12)life=30000;
        if(gate==13)life=64999;
        if(gate==14)life=270001;
        cd_couple_tick(&s,&m,1,life,home,contact);
        assert(!s.sofa_ms && s.wait_ms>=120000 && s.wait_ms<=240000);
    }
    /* Starting at the latest allowed opportunity still holds for 8-15s. */
    m=ordinary();cd_couple_init(&s,2);
    for(unsigned attempt=0;attempt<96 && !s.sofa_ms;attempt++){
        s.wait_ms=0;cd_couple_tick(&s,&m,1,270000,true,false);
    }
    duration=s.sofa_ms;assert(duration>=8000 && duration<=15000);
    cd_couple_tick(&s,&m,duration-1,270000+duration-1,true,false);
    assert(s.sofa_ms==1);
    cd_couple_touch(&s);assert(!s.sofa_ms && s.quiet_ms==30000);
    s.sofa_ms=12000;m.popup=CD_POPUP_TODO;
    assert(!(cd_couple_visual_key(&s,&m)&CD_COUPLE_SOFA_BIT));
    cd_couple_tick(&s,&m,80,100000,true,false);assert(!s.sofa_ms);

    /* 24h at 80ms: gate-aware sparse events, bounded full durations, no
     * bursts, independent PRNG and no model/persistence mutation. */
    unsigned appearances=0;uint32_t previous=0,last=0;
    m=ordinary();before=m;cd_couple_init(&s,17);
    for(uint32_t now=80;now<=86400000u;now+=80){
        cd_couple_tick(&s,&m,80,now,true,false);
        if(s.sofa_ms && !previous){
            assert(now>=1200000u && now-last>=1200000u);
            assert(s.sofa_ms>=8000u && s.sofa_ms<=15000u);
            assert(cd_couple_visual_key(&s,&m)&CD_COUPLE_ROOM_BIT);
            last=now;appearances++;
        }
        previous=s.sofa_ms;
    }
    assert(appearances>5 && appearances<72);
    assert(memcmp(&m,&before,sizeof(m))==0);
    printf("Couple visitor sample: %u appearances in 24h; >=20min spacing.\n",appearances);
    cd_couple_tick(&s,&m,UINT32_MAX,0,false,false);
    assert(!s.sofa_ms && !s.finale_ms && s.wait_ms>=120000);
}

static void test_finale(void)
{
    cd_model_t m=ordinary();cd_couple_t s;
    cd_couple_init(&s,0);
    cd_model_replay_anniversary(&m);
    unsigned elapsed=0;
    while(m.celebration_active){
        cd_model_tick(&m,50);cd_couple_tick(&s,&m,50,elapsed,true,false);
        elapsed+=50;assert(elapsed<15000);
    }
    assert(s.finale_ms==2500);
    assert(cd_couple_visual_key(&s,&m)&CD_COUPLE_FINALE_BIT);
    assert(!(cd_couple_visual_key(&s,&m)&(CD_COUPLE_ROOM_BIT|CD_COUPLE_SOFA_BIT)));
    for(unsigned i=0;i<2;i++){
        cd_person_view_t v=cd_couple_person(&s,&m,i);
        assert(v.x==(i?202:183) && v.y==178 && v.pose==CD_PERSON_FRONT);
    }
    cd_model_t before=m;
    cd_person_t frozen[2];memcpy(frozen,s.people,sizeof(frozen));
    cd_couple_tick(&s,&m,2499,elapsed+2499,true,false);
    assert(s.finale_ms==1 && memcmp(frozen,s.people,sizeof(frozen))==0);
    cd_couple_tick(&s,&m,1,elapsed+2500,true,false);
    assert(!s.finale_ms && !s.hearts_seen);
    assert(cd_couple_visual_key(&s,&m)&CD_COUPLE_ROOM_BIT);
    assert(memcmp(&m,&before,sizeof(m))==0);
    cd_couple_tick(&s,&m,50,elapsed+2550,true,false);assert(!s.finale_ms);

    for(unsigned gate=0;gate<4;gate++){
        cd_couple_init(&s,3);m=ordinary();
        m.celebration_active=true;m.celebration_phase=CD_CELEBRATION_HEARTS;
        cd_couple_tick(&s,&m,50,0,true,false);
        m.celebration_active=false;
        if(gate==0)m.popup=CD_POPUP_SETTINGS;
        if(gate==1)m.local_day++;
        if(gate==2)m.notice_ms=100;
        cd_couple_tick(&s,&m,50,0,gate!=3,false);
        assert(!s.finale_ms && !s.hearts_seen);
        m.popup=CD_POPUP_NONE;m.notice_ms=0;
        cd_couple_tick(&s,&m,50,0,true,false);assert(!s.finale_ms);
    }
    cd_couple_init(&s,2);m=ordinary();s.finale_ms=2500;
    cd_model_replay_anniversary(&m);
    cd_couple_tick(&s,&m,1,0,true,false);assert(!s.finale_ms);
}

static void check_people(const cd_couple_t *s)
{
    for(unsigned i=0;i<2;i++){
        const cd_person_t *p=&s->people[i];
        assert(p->x>=150 && p->x<=240 && p->y>=128 && p->y<=146);
        assert(p->target_x>=150 && p->target_x<=240 && p->target_y>=128 && p->target_y<=146);
        assert(p->direction==1 || p->direction==-1);
    }
    assert(abs(s->people[0].x-s->people[1].x)>=16);
}

static void test_walking(void)
{
    cd_model_t m=ordinary(),before=m;cd_couple_t s;
    cd_couple_init(&s,17);check_people(&s);
    assert(s.people[0].x==172 && s.people[0].y==139);
    assert(s.people[1].x==208 && s.people[1].y==143);
    assert(cd_couple_visual_key(&s,&m)==CD_COUPLE_ROOM_BIT);
    memset(pixels,0,sizeof(pixels));memset(expected,0,sizeof(expected));
    cd_couple_draw_room(pixels,&s,&m);
    for(unsigned i=0;i<2;i++){
        cd_person_view_t v=cd_couple_person(&s,&m,i);
        assert(v.x==s.people[i].x && v.y==s.people[i].y);
        cd_couple_draw_person(expected,v.x,v.y,i,v.pose,v.direction<0 && v.pose!=CD_PERSON_FRONT);
    }
    unsigned drawn=0;
    for(unsigned i=0;i<CD_WIDTH*CD_HEIGHT;i++)if(expected[i]){
        assert(pixels[i]==expected[i]);drawn++;
    }
    assert(drawn>250); /* Both silhouettes are visible immediately. */

    unsigned moved[2]={0},paused[2]={0},walk_frames[2]={0};
    for(uint32_t now=80;now<=360000u;now+=80){
        cd_person_t old[2];memcpy(old,s.people,sizeof(old));
        cd_couple_tick(&s,&m,80,now,true,false);
        check_people(&s);
        for(unsigned i=0;i<2;i++){
            cd_person_t *p=&s.people[i];
            int dx=abs(p->x-old[i].x),dy=abs(p->y-old[i].y);
            int distance=dx>dy?dx:dy;
            assert(distance<=1); /* No furniture-jumping or catch-up teleport. */
            if(distance)moved[i]++;else paused[i]++;
            if(distance && p->x==p->target_x && p->y==p->target_y)
                assert(p->wait_ms>=1800u && p->wait_ms<=6000u);
            if(p->target_x!=old[i].target_x || p->target_y!=old[i].target_y){
                assert(abs(p->target_x-old[i].x)<=24 && abs(p->target_y-old[i].y)<=5);
            }
            cd_person_view_t v=cd_couple_person(&s,&m,i);
            assert(v.x==p->x && v.y==p->y);
            if(v.pose==CD_PERSON_WALK_A || v.pose==CD_PERSON_WALK_B)walk_frames[i]++;
        }
    }
    for(unsigned i=0;i<2;i++)assert(moved[i]>30 && paused[i]>30 && walk_frames[i]>30);
    assert(memcmp(&m,&before,sizeof(m))==0);

    /* The pause gates must also reject a huge elapsed interval without
     * moving off-screen or banking a burst for the next interactive frame. */
    for(unsigned gate=0;gate<13;gate++){
        cd_couple_init(&s,11);m=ordinary();
        cd_couple_tick(&s,&m,6000,100000,true,false);
        bool home=true,contact=false;
        if(gate<6)m.popup=(cd_popup_t)(gate+1);
        if(gate==6)m.focus_running=true;
        if(gate==7)m.notice_ms=100;
        if(gate==8)m.celebration_active=true;
        if(gate==9)home=false;
        if(gate==10)contact=true;
        if(gate==11)s.walk_quiet_ms=500;
        if(gate==12)s.sofa_ms=12000;
        cd_person_t frozen[2];memcpy(frozen,s.people,sizeof(frozen));
        cd_couple_tick(&s,&m,80,100000,home,contact);
        assert(memcmp(frozen,s.people,sizeof(frozen))==0);
        /* Persistent gates stay frozen even with UINT32_MAX; a timer-only
         * gate legitimately expires, so test that separately below. */
        if(gate!=11 && gate!=12){
            cd_couple_tick(&s,&m,UINT32_MAX,100000,home,contact);
            assert(memcmp(frozen,s.people,sizeof(frozen))==0);
        }
    }
    m=ordinary();cd_couple_init(&s,11);cd_couple_touch(&s);
    assert(s.walk_quiet_ms>0);
    cd_person_t frozen[2];memcpy(frozen,s.people,sizeof(frozen));
    cd_couple_tick(&s,&m,1,100000,true,false);
    assert(memcmp(frozen,s.people,sizeof(frozen))==0);
    cd_couple_tick(&s,&m,UINT32_MAX,0,false,false);check_people(&s);
    assert(memcmp(frozen,s.people,sizeof(frozen))==0);
    cd_couple_tick(&s,&m,80,100000,true,false);check_people(&s);
    memcpy(frozen,s.people,sizeof(frozen));
    cd_couple_tick(&s,&m,UINT32_MAX,100000,true,false);check_people(&s);
    for(unsigned i=0;i<2;i++){
        assert(abs(s.people[i].x-frozen[i].x)<=8);
        assert(abs(s.people[i].y-frozen[i].y)<=8);
    }

    /* The removed photo's old coordinates have no custom tap behavior:
     * they remain ordinary background, while original key controls work. */
    m=ordinary();before=m;cd_ui_couple_init(1);
    cd_ui_press(&m,284,46);
    assert(m.popup==CD_POPUP_NONE && memcmp(&before,&m,sizeof(m))==0);
    assert(cd_ui_couple_visual_key(&m)==CD_COUPLE_ROOM_BIT);
    cd_ui_press(&m,315,105);assert(m.popup==CD_POPUP_FOCUS);
}

int main(void)
{
    test_sprites();test_visitors();test_finale();test_walking();
    puts("CatDesk couple: ten 9-color outlined sprites, visible room pair, bounded walking and pauses, no model mutation, sofa rarity/gates, 2.5s finale and removed photo tap passed.");
    return 0;
}
