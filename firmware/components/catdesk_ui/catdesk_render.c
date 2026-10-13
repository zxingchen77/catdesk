#include "catdesk_render.h"
#include "catdesk_font.h"
#include "catdesk_cats.h"
#include "pixel_theme.h"
#include "pixel_ui.h"
#include "catdesk_layout.h"
#include "catdesk_room.h"
#include "catdesk_couple.h"
#include <stdio.h>
#include <string.h>

/* Everything below is drawn on the original 400 x 240 pixel grid. */
enum { INK=PT_OUTLINE, WOOD=PT_WOOD, LIGHT_WOOD=PT_WOOD_LIGHT, CREAM=PT_PAPER,
       BLUE=PT_NIGHT_BLUE, GREEN=PT_GREEN, WALL=PT_WALL, WALL_SHADE=PT_WALL_SHADE,
       DARK_WOOD=PT_WOOD_DARK, GOLD=PT_GOLD, PINK=PT_HEART, PALE_PINK=PT_ROSE_LIGHT,
       RUG=PT_GREEN_DARK, RUG_LIGHT=PT_GREEN_LIGHT, SKY=PT_NIGHT, SKY_LIGHT=PT_DUSK,
       WHITE=PT_PAPER_LIGHT, LEAF=PT_GREEN_DARK };

uint16_t cd_color(unsigned rgb)
{ return (uint16_t)(((rgb>>19)&31)<<11 | ((rgb>>10)&63)<<5 | ((rgb>>3)&31)); }

static void rect(uint16_t *p,int x,int y,int w,int h,unsigned c)
{
    int x2=x+w,y2=y+h;
    if(x<0)x=0;
    if(y<0)y=0;
    if(x2>CD_WIDTH)x2=CD_WIDTH;
    if(y2>CD_HEIGHT)y2=CD_HEIGHT;
    uint16_t color=cd_color(c);
    for(int j=y;j<y2;j++) for(int i=x;i<x2;i++) p[j*CD_WIDTH+i]=color;
}
static void text(uint16_t *p,int x,int y,const char *s,unsigned c,int scale)
{ drawPixelLabel(p,CD_WIDTH,x,y,s,c,scale); }
static void center(uint16_t *p,int cx,int y,const char *s,unsigned c,int scale)
{ text(p,cx-cd_text_width(s,scale)/2,y,s,c,scale); }
static void outline(uint16_t *p,int x,int y,int w,int h,unsigned fill,unsigned border,int b)
{ rect(p,x,y,w,h,border);rect(p,x+b,y+b,w-2*b,h-2*b,fill); }
static void heart(uint16_t *p,int x,int y,int s,unsigned color)
{
    drawPixelHeart(p,CD_WIDTH,x,y,s,color);
}
static void hud(uint16_t *p,int cx,int y,const char *s,int scale)
{
    int xx=cx-cd_text_width(s,scale)/2;
    drawPixelNumber(p,CD_WIDTH,xx,y,s,scale,INK,true);
}
static int s_pointer_x=-100,s_pointer_y=-100;
static uint32_t s_pointer_ms;
static uint32_t s_ambient_ms;
static uint32_t s_life_ms;
static bool s_pointer_contact;
static cd_couple_t s_couple={
    .random_state=0x43445043u,.walk_random_state=0x14051C08u,.wait_ms=1200000u,
    .people={{.wait_ms=600,.x=172,.y=139,.target_x=172,.target_y=139,.direction=1},
             {.wait_ms=1600,.x=208,.y=143,.target_x=208,.target_y=143,.direction=-1}}
};

void cd_ui_couple_init(uint32_t seed){cd_couple_init(&s_couple,seed);}
void cd_ui_couple_tick(const cd_model_t *m,uint32_t delta,bool home)
{cd_couple_tick(&s_couple,m,delta,s_life_ms,home,s_pointer_contact);}
const cd_couple_t *cd_ui_couple_state(void){return &s_couple;}
uint32_t cd_ui_couple_visual_key(const cd_model_t *m)
{return cd_couple_visual_key(&s_couple,m);}
cd_person_view_t cd_ui_couple_person(const cd_model_t *m,unsigned person)
{return cd_couple_person(&s_couple,m,person);}

cd_room_period_t cd_room_period(const cd_model_t *m)
{
    if(!m->clock_synced)return CD_ROOM_DUSK;
    if(m->hour>=6 && m->hour<17)return CD_ROOM_DAY;
    if(m->hour<20 && m->hour>=17)return CD_ROOM_DUSK;
    return CD_ROOM_NIGHT;
}
uint32_t cd_ui_life_ms(void){return s_life_ms;}

void cd_ui_pointer(int x,int y,bool pressed)
{s_pointer_x=x;s_pointer_y=y;s_pointer_ms=pressed?160:0;s_pointer_contact=pressed;}
void cd_ui_visual_tick(uint32_t delta_ms)
{
    s_pointer_ms=s_pointer_ms>delta_ms?s_pointer_ms-delta_ms:0;
    /* Art only: a monotonic minute loop, independent of USB clock updates. */
    s_ambient_ms=(s_ambient_ms+delta_ms%60000u)%60000u;
    s_life_ms=(s_life_ms+delta_ms%300000u)%300000u;
}
bool cd_ui_has_pointer(void)
{return s_pointer_ms!=0;}

static unsigned button_flags(int x,int y,int w,int h,bool selected)
{
    unsigned flags=selected?PIXEL_BUTTON_SELECTED:PIXEL_BUTTON_NORMAL;
    if(s_pointer_ms && s_pointer_x>=x && s_pointer_x<x+w && s_pointer_y>=y && s_pointer_y<y+h)
        flags|=PIXEL_BUTTON_PRESSED;
    return flags;
}
static void sparkle(uint16_t *p,int x,int y,unsigned c)
{rect(p,x,y-2,1,5,c);rect(p,x-2,y,5,1,c);}
static void gear(uint16_t *p,int x,int y)
{
    rect(p,x+4,y,5,13,INK);rect(p,x,y+4,13,5,INK);rect(p,x+2,y+2,9,9,INK);
    rect(p,x+4,y+4,5,5,CREAM);rect(p,x+5,y+5,3,3,WALL);
}
/* Pixel polygons describe top/front/side planes, never antialiased vectors. */
static void polygon(uint16_t *p,const int points[][2],int count,unsigned color)
{
    int min_y=240,max_y=0;
    for(int i=0;i<count;i++){if(points[i][1]<min_y)min_y=points[i][1];if(points[i][1]>max_y)max_y=points[i][1];}
    if(min_y<0)min_y=0;
    if(max_y>239)max_y=239;
    for(int y=min_y;y<=max_y;y++){
        int nodes[16],n=0;
        for(int i=0,j=count-1;i<count;j=i++){
            int ya=points[i][1],yb=points[j][1];
            if((ya<=y&&yb>y)||(yb<=y&&ya>y))
                nodes[n++]=points[i][0]+(y-ya)*(points[j][0]-points[i][0])/(yb-ya);
        }
        for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)if(nodes[j]<nodes[i]){int t=nodes[i];nodes[i]=nodes[j];nodes[j]=t;}
        for(int i=0;i+1<n;i+=2)rect(p,nodes[i],y,nodes[i+1]-nodes[i]+1,1,color);
    }
}
static void floor_mark(uint16_t *p,int x,int y,int w,int h,unsigned color)
{
    int right=x+w;
    if(x<16)x=16;
    if(right>384)right=384;
    if(right>x)rect(p,x,y,right-x,h,color);
}

static void pot(uint16_t *p,int x,int y)
{
    rect(p,x+4,y+2,1,12,DARK_WOOD);
    const int leaves[][2]={{0,3},{5,0},{7,6},{1,8}};
    for(int i=0;i<4;i++){
        int xx=x+leaves[i][0],yy=y+leaves[i][1];
        rect(p,xx+1,yy,3,1,LEAF);rect(p,xx,yy+1,5,2,LEAF);
        rect(p,xx+1,yy+1,3,1,GREEN);rect(p,xx+2,yy,1,1,RUG_LIGHT);
    }
    outline(p,x+1,y+12,10,3,LIGHT_WOOD,INK,1);
    outline(p,x+2,y+15,8,6,WOOD,INK,1);rect(p,x+3,y+16,2,3,LIGHT_WOOD);
    rect(p,x+8,y+16,1,4,DARK_WOOD);
}

static void small_mac(uint16_t *p,bool connected)
{
    int x=CD_MAC_X,y=CD_MAC_Y;
    outline(p,x,y,23,14,PT_CAT_BLACK_LIGHT,INK,1);
    rect(p,x+1,y+1,21,1,PT_BLUE_LIGHT);
    rect(p,x+3,y+3,17,9,connected?BLUE:PT_NIGHT);
    if(connected){
        rect(p,x+4,y+4,15,1,PT_BLUE_LIGHT);rect(p,x+5,y+6,6,1,CREAM);
        rect(p,x+5,y+9,10,1,PT_BLUE_LIGHT);rect(p,x+18,y+11,1,1,RUG_LIGHT);
    }else rect(p,x+6,y+7,8,1,PT_CAT_BLACK_LIGHT);
    const int base[][2]={{x,y+14},{x+22,y+14},{x+26,y+18},{x-3,y+18}};
    polygon(p,base,4,INK);
    rect(p,x-1,y+15,24,2,PT_CAT_BLACK_LIGHT);rect(p,x,y+15,22,1,PT_BLUE_LIGHT);
    for(int i=3;i<20;i+=3)rect(p,x+i,y+15,1,1,INK);
    rect(p,x+8,y+17,7,1,PT_BLUE_LIGHT);
}

static void cozy_bulb(uint16_t *p,int x,int y)
{
    rect(p,x+1,y-2,1,2,DARK_WOOD);
    rect(p,x+1,y,2,4,INK);rect(p,x,y+1,4,2,INK);
    rect(p,x+1,y+1,2,2,GOLD);rect(p,x+1,y+1,1,2,WHITE);
    rect(p,x+2,y+1,1,1,CREAM);
}

static void cozy_window_vine(uint16_t *p)
{
    /* A tiny hanging terracotta pot to the window's right, x112..126. */
    rect(p,118,15,1,6,DARK_WOOD);
    rect(p,116,19,5,1,INK);
    outline(p,114,21,10,7,WOOD,INK,1);
    rect(p,115,22,8,1,LIGHT_WOOD);
    rect(p,115,23,2,3,LIGHT_WOOD);
    rect(p,122,23,1,3,DARK_WOOD);
    rect(p,118,28,1,22,LEAF);
    rect(p,119,28,1,8,GREEN);
    for(int i=0;i<3;i++) {
        int yy=31+i*8;
        const int left[][2]={{113,yy},{116,yy-1},{118,yy+1},{116,yy+3},{112,yy+2}};
        polygon(p,left,5,INK);
        rect(p,114,yy,3,2,GREEN);rect(p,114,yy,1,1,RUG_LIGHT);
        const int right[][2]={{119,yy+4},{123,yy+2},{126,yy+4},{124,yy+6},{120,yy+6}};
        polygon(p,right,5,INK);
        rect(p,121,yy+4,3,2,GREEN);rect(p,122,yy+3,2,1,RUG_LIGHT);
    }
    rect(p,117,51,3,3,LEAF);rect(p,118,51,1,2,GREEN);
}

static void cozy_wall(uint16_t *p)
{
    /* A slack one-pixel cable wraps outside the 189,23,42,40 keepsake.
       Six warm bulbs, no alpha glow and no animation. */
    const int wire[][4]={
        {146,25,10,1},{155,24,9,1},{163,22,10,1},{172,21,11,1},
        {182,19,10,1},{191,17,12,1},{202,16,13,1},{214,18,10,1},
        {223,20,11,1},{233,23,10,1},{174,22,1,23},{242,24,1,24}
    };
    for(unsigned i=0;i<sizeof(wire)/sizeof(wire[0]);i++)
        rect(p,wire[i][0],wire[i][1],wire[i][2],wire[i][3],DARK_WOOD);
    const int bulbs[][2]={{151,27},{172,31},{194,19},{215,19},{238,26},{241,43}};
    for(unsigned i=0;i<sizeof(bulbs)/sizeof(bulbs[0]);i++)cozy_bulb(p,bulbs[i][0],bulbs[i][1]);

    /* Two small printed memories below the left cable, with paper borders
       and a blank caption strip. Neither overlaps the framed date/HUD. */
    outline(p,143,43,11,14,WHITE,INK,1);
    rect(p,145,45,7,7,PT_COZY_BLUE);
    rect(p,146,46,2,2,CREAM);rect(p,149,46,2,2,CREAM);
    rect(p,146,46,2,1,DARK_WOOD);rect(p,149,46,2,1,INK);
    rect(p,146,49,2,2,PT_COZY_PINK);rect(p,149,49,2,2,BLUE);
    rect(p,145,54,5,1,PT_PAPER_DARK);
    rect(p,147,42,2,2,GOLD);rect(p,147,42,1,1,WHITE);

    outline(p,158,45,11,14,WHITE,INK,1);
    rect(p,160,47,7,7,PT_COZY_BLUE);
    rect(p,164,48,2,2,GOLD);
    rect(p,160,52,7,2,GREEN);
    rect(p,161,50,2,3,LEAF);rect(p,160,51,4,1,LEAF);
    rect(p,160,56,5,1,PT_PAPER_DARK);
    rect(p,162,44,2,2,GOLD);rect(p,162,44,1,1,WHITE);

    /* A folded pink note on the other side, all inside the north wall. */
    outline(p,242,51,12,12,PT_COZY_PINK,INK,1);
    rect(p,243,52,10,1,WHITE);
    rect(p,250,52,3,2,PT_ROSE_LIGHT);rect(p,252,54,1,2,PT_RED);
    heart(p,244,54,1,PINK);
    cozy_window_vine(p);
}

static void cozy_cat_box(uint16_t *p)
{
    /* A 27x19px open cardboard nest between the sofa and existing slippers.
       It has a blue folded pad and hand-pixelled CAT, no interaction target. */
    rect(p,63,162,23,2,DARK_WOOD);
    const int back[][2]={{64,150},{63,145},{78,145},{81,150}};
    polygon(p,back,4,INK);
    rect(p,65,146,12,3,LIGHT_WOOD);rect(p,66,147,10,1,WOOD);
    const int shell[][2]={{63,149},{80,149},{84,153},{83,162},{62,162},{61,154}};
    polygon(p,shell,6,INK);
    outline(p,63,153,20,9,LIGHT_WOOD,INK,1);
    rect(p,64,154,18,1,WOOD);
    rect(p,64,156,2,5,WOOD);rect(p,81,155,1,6,DARK_WOOD);
    rect(p,64,150,16,4,INK);
    rect(p,65,151,14,2,PT_COZY_BLUE);rect(p,66,151,11,1,WHITE);
    const int left[][2]={{63,149},{59,152},{61,157},{65,153}};
    polygon(p,left,4,INK);rect(p,61,152,3,2,WOOD);rect(p,62,151,1,2,LIGHT_WOOD);
    const int right[][2]={{80,149},{86,151},{84,157},{80,153}};
    polygon(p,right,4,INK);rect(p,81,151,3,3,WOOD);rect(p,82,151,2,1,LIGHT_WOOD);
    static const char cat[5][12]={
        "11100100111","10001010010","10001110010","10001010010","11101010010"
    };
    for(int yy=0;yy<5;yy++)for(int xx=0;xx<11;xx++)
        if(cat[yy][xx]=='1')rect(p,67+xx,156+yy,1,1,DARK_WOOD);
}

static void cozy_sofa(uint16_t *p)
{
    /* A folded cream throw follows the left arm and falls down the front.
       Four discrete fabric shades, one-pixel seam and short pixel fringes. */
    const int blanket[][2]={{59,118},{69,118},{74,124},{72,134},{69,140},{59,138}};
    polygon(p,blanket,6,INK);
    const int cloth[][2]={{60,119},{68,119},{73,125},{71,133},{68,139},{60,137}};
    polygon(p,cloth,6,WHITE);
    const int fold[][2]={{65,120},{68,120},{71,125},{69,133},{66,132}};
    polygon(p,fold,5,CREAM);
    rect(p,61,121,1,14,PT_PAPER_DARK);
    rect(p,67,124,1,9,PT_PAPER_DARK);rect(p,68,132,3,1,PT_PAPER_DARK);
    rect(p,62,120,5,1,CREAM);rect(p,61,135,8,1,CREAM);
    for(int xx=60;xx<=68;xx+=2) {
        rect(p,xx,138,1,3,PT_PAPER_DARK);rect(p,xx,138,1,2,CREAM);
    }

    /* Pink heart pillow and a small pale-blue paw cushion on the back seat. */
    const int pink[][2]={{70,115},{79,115},{82,118},{81,126},{69,126},{68,123},{69,118}};
    polygon(p,pink,7,INK);
    rect(p,70,116,10,9,PT_COZY_PINK);rect(p,71,116,7,1,WHITE);
    rect(p,70,124,10,1,PT_ROSE_LIGHT);rect(p,80,119,1,5,PT_ROSE_LIGHT);
    heart(p,72,118,1,PINK);
    const int blue[][2]={{89,116},{98,116},{101,119},{100,126},{89,126},{87,123},{88,118}};
    polygon(p,blue,7,INK);
    rect(p,89,117,10,8,PT_COZY_BLUE);rect(p,90,117,7,1,WHITE);
    rect(p,89,124,10,1,PT_BLUE_LIGHT);rect(p,99,119,1,5,PT_BLUE_LIGHT);
    rect(p,91,120,2,2,WHITE);rect(p,94,119,2,2,WHITE);rect(p,97,120,2,2,WHITE);
    const int paw[][2]={{94,122},{96,122},{98,124},{97,125},{93,125},{92,124}};
    polygon(p,paw,6,WHITE);
    cozy_cat_box(p);
}

static void cozy_desk(uint16_t *p)
{
    /* Two-person keepsake, 10x8, behind the task book. Faces and clothing
     * differ in color, without demanding miniature high-resolution detail. */
    rect(p,265,97,2,7,DARK_WOOD); /* stand, later occluded by task book */
    outline(p,262,90,10,8,LIGHT_WOOD,INK,1);
    rect(p,263,91,8,6,CREAM);
    rect(p,264,92,2,2,WHITE);rect(p,264,92,2,1,PT_CAT_ORANGE_SHADOW);
    rect(p,268,92,2,2,WHITE);rect(p,268,92,2,1,PT_CAT_BLACK_LIGHT);
    rect(p,263,95,4,2,PT_COZY_PINK);rect(p,267,95,4,2,PT_COZY_BLUE);
    rect(p,264,94,1,1,PT_CAT_NOSE);rect(p,268,94,1,1,PT_CAT_NOSE);
    rect(p,263,97,8,1,DARK_WOOD);rect(p,263,91,8,1,GOLD);

    /* A 4x5 mug fits the real gap: book ends x272, Mac begins x279.
     * Handle stops at x278; saucer stops x277 before the Mac keyboard. */
    rect(p,274,108,4,1,CREAM);
    outline(p,274,103,4,5,PT_COZY_BLUE,INK,1);
    rect(p,275,104,2,1,DARK_WOOD);rect(p,275,105,1,2,WHITE);
    rect(p,278,105,1,2,PT_COZY_BLUE);

    /* Thin bud vase: 5x6 base, two tiny flowers in the gap to the tomato.
     * Mac's original screen ends x301, tomato body starts x310; neither is
     * overwritten. Flowers sit above the tomato's own green clock leaf. */
    rect(p,305,94,1,5,GREEN);rect(p,307,93,1,5,LEAF);
    rect(p,304,96,2,1,GREEN);
    outline(p,303,98,5,6,PT_COZY_PINK,INK,1);
    rect(p,304,99,1,3,WHITE);rect(p,306,101,1,2,PALE_PINK);
    rect(p,303,92,1,3,PT_COZY_PINK);rect(p,302,93,3,1,PT_COZY_PINK);
    rect(p,303,93,1,1,GOLD);
    rect(p,307,90,1,3,PT_COZY_BLUE);rect(p,306,91,3,1,PT_COZY_BLUE);
    rect(p,307,91,1,1,WHITE);
}

static void cozy_rug(uint16_t *p)
{
    /* Cream-olive cloth, a pale yellow selvage and one deep grounding edge.
     * Flat embroidery is ground texture; it never blocks the existing cats. */
    outline(p,108,165,188,49,GOLD,INK,1);
    rect(p,110,167,184,45,CREAM);
    rect(p,113,170,178,39,RUG_LIGHT);
    rect(p,114,171,176,1,WHITE);rect(p,114,207,176,1,GREEN);
    rect(p,111,168,182,1,WHITE);rect(p,111,211,182,1,PT_PAPER_DARK);
    for(int x=112;x<293;x+=14){
        rect(p,x,163,2,2,CREAM);rect(p,x+1,214,2,2,PT_PAPER_DARK);
    }
    /* A few uneven, low-contrast woven threads, not a wallpaper repeat. */
    const int threads[][2]={{122,178},{154,175},{186,181},{239,176},{278,188},
                            {133,193},{168,201},{213,194},{245,205},{274,198}};
    for(unsigned i=0;i<sizeof(threads)/sizeof(threads[0]);i++)
        rect(p,threads[i][0],threads[i][1],2+(int)(i%2),1,GREEN);

    /* A few sewn motifs peek between the cats instead of hiding under them. */
    rect(p,203,198,2,6,PT_COZY_PINK);rect(p,201,200,6,2,PT_COZY_PINK);
    rect(p,203,200,2,2,GOLD);rect(p,205,203,2,1,GREEN);
    rect(p,179,178,1,3,CREAM);rect(p,178,179,3,1,CREAM);
    rect(p,179,179,1,1,GOLD);rect(p,180,181,1,2,GREEN);
    rect(p,272,176,1,2,CREAM);rect(p,274,175,1,2,CREAM);
    rect(p,276,176,1,2,CREAM);rect(p,273,179,3,2,CREAM);
    rect(p,274,178,1,1,CREAM);
    sparkle(p,147,179,CREAM);sparkle(p,234,189,GOLD);
}

static void cozy_cat_things(uint16_t *p)
{
    /* Exactly four items grouped beside the rug: two different bowls, yarn,
     * and a little mouse. All solids are below y214; feet remain at y206 and
     * the whole movement lane x40..320 remains unobstructed. No new hit area. */
    const unsigned bowls[]={PT_COZY_PINK,PT_COZY_BLUE};
    for(int i=0;i<2;i++){
        int x=309+i*16,y=215;
        rect(p,x+2,y,8,1,INK);rect(p,x,y+1,12,4,INK);
        rect(p,x+1,y+5,10,1,INK);rect(p,x+3,y+6,6,1,DARK_WOOD);
        rect(p,x+1,y+2,10,3,bowls[i]);rect(p,x+2,y+1,8,1,bowls[i]);
        rect(p,x+3,y+2,6,2,i?BLUE:DARK_WOOD);
        if(i)rect(p,x+3,y+2,3,1,WHITE);
        else {rect(p,x+4,y+2,1,1,GOLD);rect(p,x+7,y+3,1,1,CREAM);}
        rect(p,x+2,y+4,2,1,WHITE);
    }
    /* Six-pixel yarn ball, with one curved loose thread leading inward. */
    rect(p,344,216,5,1,INK);rect(p,343,217,7,4,INK);rect(p,344,221,5,1,INK);
    rect(p,344,217,5,4,PT_COZY_PINK);rect(p,345,217,3,1,WHITE);
    rect(p,344,219,4,1,PT_ROSE_LIGHT);rect(p,347,218,1,3,PT_RED);
    rect(p,339,221,5,1,PT_COZY_PINK);rect(p,337,222,3,1,PT_COZY_PINK);
    /* Tiny fabric mouse tucked just above the yarn: round ear, stitched eye,
     * and a two-step tail. It stays away from the floor's bottom outline. */
    rect(p,350,215,6,1,INK);rect(p,349,216,8,3,INK);rect(p,350,219,6,1,INK);
    rect(p,350,216,6,3,PT_COZY_BLUE);rect(p,351,214,2,2,PT_COZY_PINK);
    rect(p,355,217,1,1,INK);rect(p,357,217,1,1,PT_CAT_NOSE);
    rect(p,347,219,2,1,PT_COZY_BLUE);rect(p,346,220,1,2,PT_COZY_BLUE);
}

void cd_render_background(uint16_t *p)
{
    /* A cutaway game-map room: one continuous floor, world-scale furniture. */
    rect(p,0,0,400,240,PT_NIGHT);
    rect(p,20,13,367,220,PT_CAT_BLACK_OUTLINE);
    rect(p,16,10,368,62,WALL);
    rect(p,16,72,368,156,WOOD);
    /* North wall boards and small plaster imperfections, not a UI backdrop. */
    rect(p,16,10,368,4,INK);rect(p,18,11,364,1,LIGHT_WOOD);
    rect(p,16,14,4,58,DARK_WOOD);rect(p,380,14,4,58,DARK_WOOD);
    rect(p,17,15,1,56,LIGHT_WOOD);rect(p,381,15,1,56,WOOD);
    for(int x=25;x<375;x+=31){
        rect(p,x,15,7,1,WALL_SHADE);
        if(x%2)rect(p,x+11,45,1,2,WALL_SHADE);
        rect(p,x+3,64,2,1,WALL_SHADE);
    }
    rect(p,20,66,360,6,DARK_WOOD);rect(p,20,66,360,1,LIGHT_WOOD);
    for(int x=24;x<380;x+=16){rect(p,x,68,1,3,WOOD);rect(p,x+4,68,6,1,WOOD);}
    /* Staggered 32px oak floor boards, irregular grain and chamfered ends. */
    for(int row=0,y=72;y<228;y+=13,row++){
        rect(p,16,y,368,1,DARK_WOOD);rect(p,16,y+1,368,1,LIGHT_WOOD);
        int offset=(row&1)?16:0;
        for(int x=16-offset;x<384;x+=32){
            floor_mark(p,x,y+1,1,12,DARK_WOOD);
            int grain=4+(row*7+x)%11;
            floor_mark(p,x+5,y+5,grain,1,LIGHT_WOOD);floor_mark(p,x+8,y+8,grain-2,1,DARK_WOOD);
            floor_mark(p,x+23,y+3,3,1,PT_CAT_ORANGE_SHADOW);
            if((row+x)%3==0){floor_mark(p,x+19,y+10,2,1,DARK_WOOD);floor_mark(p,x+21,y+9,1,1,DARK_WOOD);}
        }
    }
    rect(p,16,72,3,156,DARK_WOOD);rect(p,381,72,3,156,DARK_WOOD);
    rect(p,16,226,368,4,INK);rect(p,18,227,364,1,LIGHT_WOOD);
    /* Quiet window to a distant little town, 56x34 rather than a giant widget. */
    int wx=CD_WINDOW_X,wy=CD_WINDOW_Y;
    rect(p,wx+2,wy+2,56,34,WALL_SHADE);
    outline(p,wx,wy,56,34,WOOD,INK,1);rect(p,wx+1,wy+1,54,1,LIGHT_WOOD);
    outline(p,wx+3,wy+3,50,28,DARK_WOOD,LIGHT_WOOD,1);
    rect(p,wx+5,wy+5,46,24,PT_NIGHT);rect(p,wx+5,wy+19,46,10,PT_DUSK);
    rect(p,wx+38,wy+6,5,6,CREAM);rect(p,wx+41,wy+5,3,6,CREAM);
    rect(p,wx+43,wy+5,3,5,PT_NIGHT);
    sparkle(p,wx+15,wy+10,WHITE);rect(p,wx+28,wy+7,1,1,WHITE);
    rect(p,wx+8,wy+20,11,1,PT_CLOUD);rect(p,wx+10,wy+18,7,2,PT_CLOUD);
    for(int i=0;i<4;i++){
        int xx=wx+6+i*11,yy=wy+24-i%2*3;
        rect(p,xx,yy,8,6,BLUE);rect(p,xx+1,yy-1,6,1,BLUE);
        rect(p,xx+2,yy+2,1,1,GOLD);
    }
    rect(p,wx+27,wy+3,2,28,DARK_WOOD);rect(p,wx+27,wy+3,1,28,LIGHT_WOOD);
    rect(p,wx+3,wy+17,50,2,DARK_WOOD);rect(p,wx+3,wy+17,50,1,LIGHT_WOOD);
    rect(p,wx-3,wy-1,62,2,INK);rect(p,wx-2,wy-1,60,1,WOOD);
    for(int x=wx-2;x<=wx+51;x+=53){
        rect(p,x,wy+1,5,28,LEAF);rect(p,x+1,wy+2,2,27,GREEN);
        rect(p,x+1,wy+25,3,1,RUG_LIGHT);rect(p,x,wy+29,5,2,GOLD);
    }
    outline(p,wx-3,wy+32,62,18,WOOD,INK,1);rect(p,wx-2,wy+33,60,2,LIGHT_WOOD);
    rect(p,wx-2,wy+47,60,2,DARK_WOOD);
    pot(p,wx-16,wy+27);
    /* A little personal keepsake with a 36px paper interior. */
    int fx=CD_FRAME_X,fy=CD_FRAME_Y;
    rect(p,fx+19,fy-3,3,1,DARK_WOOD);rect(p,fx+1,fy+2,42,40,WALL_SHADE);
    outline(p,fx,fy,42,40,WOOD,INK,1);rect(p,fx+1,fy+1,40,1,LIGHT_WOOD);
    rect(p,fx+1,fy+2,1,36,LIGHT_WOOD);rect(p,fx+40,fy+2,1,36,DARK_WOOD);
    outline(p,fx+2,fy+3,39,34,CREAM,DARK_WOOD,1);
    rect(p,fx+3,fy+4,37,1,WHITE);
    for(int i=0;i<3;i++){rect(p,fx+6+i*10,fy+2,2,1,GOLD);rect(p,fx+7+i*10,fy+38,3,1,DARK_WOOD);}
    /* Free-standing bookcase: top, front shelves and shaded right side. */
    int bx=315,by=23;
    rect(p,bx+3,by+4,47,54,WALL_SHADE);
    const int shelf_top[][2]={{bx,by+3},{bx+6,by},{bx+46,by},{bx+41,by+5}};
    polygon(p,shelf_top,4,INK);rect(p,bx+5,by+1,39,2,LIGHT_WOOD);
    outline(p,bx,by+4,43,49,WOOD,INK,1);
    rect(p,bx+42,by+5,5,46,DARK_WOOD);rect(p,bx+45,by+4,1,48,INK);
    rect(p,bx+2,by+6,39,43,DARK_WOOD);rect(p,bx+2,by+6,1,43,LIGHT_WOOD);
    const unsigned spines[]={BLUE,PT_RED,GREEN,PT_DUSK,GOLD};
    for(int row=0;row<2;row++){
        for(int i=0;i<6;i++){
            int xx=bx+4+i*6,yy=by+8+row*15+(i*3)%4;
            outline(p,xx,yy,4,12-(i*3)%4,spines[(i+row)%5],INK,1);
            rect(p,xx+1,yy+2,1,2,CREAM);
        }
        rect(p,bx+2,by+21+row*15,39,3,WOOD);rect(p,bx+2,by+21+row*15,39,1,LIGHT_WOOD);
    }
    outline(p,bx+3,by+39,36,10,WOOD,INK,1);rect(p,bx+4,by+40,34,1,LIGHT_WOOD);
    rect(p,bx+18,by+43,5,1,INK);rect(p,bx+19,by+43,3,1,GOLD);
    rect(p,bx+3,by+52,4,3,INK);rect(p,bx+36,by+52,4,3,INK);
    /* Lamplight is a few opaque pixel pools, not a translucent UI effect. */
    const int pool[][2]={{116,126},{123,119},{133,120},{145,142},{137,151},{119,149},{110,140}};
    polygon(p,pool,7,LIGHT_WOOD);
    const int innerpool[][2]={{119,128},{129,124},{139,142},{132,146},{117,143}};
    polygon(p,innerpool,5,PT_CAT_ORANGE_LIGHT);
    /* Grain stays continuous through the light: it is still the same floor. */
    uint16_t outer_light=cd_color(LIGHT_WOOD),inner_light=cd_color(PT_CAT_ORANGE_LIGHT);
    for(int yy=119;yy<152;yy++)for(int xx=110;xx<146;xx++){
        uint16_t c=p[yy*CD_WIDTH+xx];
        if(c!=outer_light && c!=inner_light)continue;
        int row=(yy-72)/13,dy=(yy-72)%13;
        int seam=(xx-16+((row&1)?16:0))%32;
        if(dy==0 || seam==0)p[yy*CD_WIDTH+xx]=cd_color(WOOD);
        else if(dy==5 && seam>=8 && seam<=16)p[yy*CD_WIDTH+xx]=cd_color(LIGHT_WOOD);
    }
    /* Small olive loveseat: raised back, top cushions, front skirt, side planes. */
    int sx=58,sy=111;
    rect(p,sx+4,sy+30,52,5,DARK_WOOD);rect(p,sx+8,sy+33,40,3,DARK_WOOD);
    outline(p,sx+5,sy,46,15,GREEN,INK,1);
    rect(p,sx+7,sy+2,42,2,RUG_LIGHT);rect(p,sx+7,sy+13,42,1,LEAF);
    const int seat[][2]={{sx+5,sy+13},{sx+48,sy+13},{sx+54,sy+23},{sx+2,sy+23}};
    polygon(p,seat,4,INK);rect(p,sx+5,sy+15,44,7,GREEN);rect(p,sx+7,sy+15,40,1,RUG_LIGHT);
    rect(p,sx+27,sy+15,1,7,LEAF);
    outline(p,sx+2,sy+23,52,8,GREEN,INK,1);rect(p,sx+3,sy+24,50,1,RUG_LIGHT);
    rect(p,sx+3,sy+28,50,2,LEAF);
    outline(p,sx,sy+10,7,19,GREEN,INK,1);outline(p,sx+49,sy+10,8,20,LEAF,INK,1);
    rect(p,sx+1,sy+11,5,2,RUG_LIGHT);rect(p,sx+50,sy+11,6,2,GREEN);
    rect(p,sx+3,sy+30,3,4,INK);rect(p,sx+49,sy+30,3,4,INK);
    cozy_sofa(p);
    /* A 17px reading lamp with a shade, spindle and base. */
    int lx=122,ly=108;
    rect(p,lx+7,ly+11,2,25,INK);rect(p,lx+7,ly+12,1,23,GOLD);
    rect(p,lx+4,ly+35,9,2,DARK_WOOD);rect(p,lx+5,ly+34,7,1,LIGHT_WOOD);
    rect(p,lx+4,ly,9,2,INK);rect(p,lx+3,ly+2,11,3,CREAM);
    rect(p,lx+1,ly+5,15,5,CREAM);rect(p,lx,ly+10,17,2,INK);
    rect(p,lx+2,ly+5,2,4,WHITE);rect(p,lx+13,ly+5,2,4,PT_PAPER_DARK);
    /* World-scale writing desk, laid out in three-quarter view. */
    int dx=CD_DESK_X,dy=CD_DESK_Y;
    rect(p,dx+5,dy+28,62,5,DARK_WOOD);
    const int top[][2]={{dx,dy+3},{dx+7,dy-2},{dx+60,dy-2},{dx+64,dy+6},{dx+5,dy+11}};
    polygon(p,top,5,INK);
    const int surface[][2]={{dx+2,dy+3},{dx+8,dy},{dx+58,dy},{dx+61,dy+5},{dx+6,dy+8}};
    polygon(p,surface,5,LIGHT_WOOD);
    rect(p,dx+8,dy+3,45,1,WOOD);rect(p,dx+17,dy+5,25,1,WOOD);
    outline(p,dx+5,dy+11,57,7,WOOD,INK,1);rect(p,dx+6,dy+12,55,1,LIGHT_WOOD);
    rect(p,dx+58,dy+7,5,17,DARK_WOOD);rect(p,dx+62,dy+6,1,18,INK);
    outline(p,dx+9,dy+18,21,7,WOOD,INK,1);rect(p,dx+17,dy+21,5,1,INK);
    rect(p,dx+9,dy+24,3,9,INK);rect(p,dx+10,dy+25,1,6,LIGHT_WOOD);
    rect(p,dx+55,dy+20,3,12,INK);rect(p,dx+56,dy+21,1,9,WOOD);
    cozy_desk(p);
    /* Closed pocket task book and tiny pencil, no app icon label. */
    int nx=CD_BOOK_X,ny=CD_BOOK_Y;
    outline(p,nx,ny,15,12,PT_RED,INK,1);rect(p,nx+2,ny+1,11,1,PALE_PINK);
    rect(p,nx+1,ny+2,2,8,DARK_WOOD);rect(p,nx+4,ny+10,10,1,CREAM);
    rect(p,nx+6,ny+4,4,3,CREAM);rect(p,nx+7,ny+5,2,1,WOOD);
    rect(p,nx+1,ny+12,13,1,INK);rect(p,nx+1,ny+13,3,1,PINK);
    rect(p,nx-4,ny+7,1,8,INK);rect(p,nx-4,ny+7,1,2,GOLD);
    small_mac(p,false);
    int tx=CD_TOMATO_X,ty=CD_TOMATO_Y;
    rect(p,tx+3,ty,5,1,LEAF);rect(p,tx+5,ty-2,1,3,LEAF);
    rect(p,tx+1,ty+2,9,6,INK);rect(p,tx+2,ty+1,7,8,INK);
    rect(p,tx+2,ty+2,7,6,PINK);rect(p,tx+3,ty+2,2,2,PALE_PINK);
    outline(p,tx+4,ty+3,4,4,CREAM,INK,1);
    rect(p,tx+5,ty+4,1,2,INK);
    cozy_rug(p);
    /* Everyday traces: slippers, a folded throw, plant stand and water bowl. */
    outline(p,87,152,5,8,LEAF,INK,1);rect(p,88,153,3,2,GREEN);
    outline(p,94,154,5,8,LEAF,INK,1);rect(p,95,155,3,2,GREEN);
    outline(p,43,149,13,11,WOOD,INK,1);rect(p,44,150,11,2,LIGHT_WOOD);
    rect(p,44,159,2,8,INK);rect(p,53,159,2,8,INK);
    rect(p,43,146,12,4,CREAM);rect(p,44,147,10,1,WHITE);
    pot(p,337,106);
    outline(p,27,132,17,8,WOOD,INK,1);rect(p,29,140,2,10,INK);rect(p,40,140,2,10,INK);
    pot(p,30,111);
    cozy_cat_things(p);
    cozy_wall(p);
}

static void button(uint16_t *p,int x,int y,int w,const char *label,bool selected)
{drawPixelButton(p,CD_WIDTH,x,y,w,PT_BUTTON_HEIGHT,label,button_flags(x,y,w,PT_BUTTON_HEIGHT,selected));}

static void panel(uint16_t *p,int x,int y,int w,int h,const char *title)
{
    drawPixelWindow(p,CD_WIDTH,x,y,w,h,title);
    drawPixelButton(p,CD_WIDTH,x+w-24,y+8,15,15,"X",button_flags(x+w-24,y+8,15,15,false));
}

static void popup_rect(cd_popup_t popup,int *x,int *y,int *w,int *h)
{
    *x=95;*y=48;*w=210;*h=145;
    switch(popup){
    case CD_POPUP_ANNIVERSARY:*x=80;*y=49;*w=240;*h=145;break;
    case CD_POPUP_FOCUS:*x=113;*y=59;*w=174;*h=125;break;
    case CD_POPUP_SETTINGS:*x=110;*y=69;*w=180;*h=105;break;
    case CD_POPUP_WEATHER:*x=107;*y=49;*w=186;*h=145;break;
    case CD_POPUP_MAC:*x=107;*y=46;*w=186;*h=150;break;
    default:break;
    }
}

static void metric_bar(uint16_t *p,int x,int y,unsigned value,bool live)
{
    outline(p,x,y,82,5,PT_PAPER_DARK,DARK_WOOD,1);
    unsigned full=(value*10+99)/100;
    for(unsigned i=0;i<10;i++)
        rect(p,x+2+(int)i*8,y+1,6,3,i<full?(live?GREEN:PT_BLUE_LIGHT):PT_PAPER_DARK);
}

static void open_notebook(uint16_t *p,int x,int y,int w,int h)
{
    /* Two bent paper stacks, a sewn gutter and a real leather cover. */
    rect(p,x+2,y+4,w,h,INK);
    rect(p,x-2,y+6,w+4,h-5,DARK_WOOD);
    rect(p,x+3,y+3,w-6,h-4,PT_RED);
    const int leftpage[][2]={{x+5,y+5},{x+w/2-3,y+9},{x+w/2-3,y+h-7},{x+5,y+h-11}};
    const int rightpage[][2]={{x+w/2+3,y+9},{x+w-5,y+5},{x+w-5,y+h-11},{x+w/2+3,y+h-7}};
    polygon(p,leftpage,4,CREAM);polygon(p,rightpage,4,WHITE);
    rect(p,x+5,y+5,1,h-17,WHITE);rect(p,x+w-6,y+5,1,h-17,PT_PAPER_DARK);
    rect(p,x+w/2-3,y+9,6,h-15,PT_PAPER_DARK);
    rect(p,x+w/2-1,y+10,2,h-17,DARK_WOOD);
    for(int yy=y+16;yy<y+h-8;yy+=13){
        rect(p,x+w/2-4,yy,8,1,DARK_WOOD);rect(p,x+w/2-3,yy+1,6,1,LIGHT_WOOD);
    }
    for(int yy=y+31;yy<y+h-14;yy+=9){
        rect(p,x+11,yy,w/2-21,1,PT_PAPER_DARK);
        rect(p,x+w/2+10,yy,w/2-21,1,PT_PAPER_DARK);
    }
    text(p,x+12,y+10,"今日待办",INK,1);
    cd_tiny_text(p,CD_WIDTH,x+w/2+17,y+18,"NOTES",cd_color(DARK_WOOD));
    drawPixelHeartOutline(p,CD_WIDTH,x+w/2+47,y+39,1,PINK);
    /* An original little pencil cat in the right-hand margin. */
    int cx=x+w/2+31,cy=y+57;
    const int body[][2]={{cx,cy+6},{cx+1,cy},{cx+5,cy+4},{cx+10,cy+4},{cx+14,cy},{cx+15,cy+6},{cx+13,cy+12},{cx+12,cy+22},{cx+3,cy+22},{cx+2,cy+12}};
    polygon(p,body,10,PT_PAPER_DARK);
    rect(p,cx+4,cy+9,2,1,DARK_WOOD);rect(p,cx+10,cy+9,2,1,DARK_WOOD);
    rect(p,cx+7,cy+12,2,1,DARK_WOOD);rect(p,cx+15,cy+18,6,2,PT_PAPER_DARK);
    rect(p,cx+19,cy+14,2,4,PT_PAPER_DARK);
    drawPixelButton(p,CD_WIDTH,x+w-24,y+8,15,15,"X",button_flags(x+w-24,y+8,15,15,false));
}
static void memory_scroll(uint16_t *p,int x,int y,int w,int h)
{
    /* A keepsake letter unrolls in the room; no system-window title bar. */
    rect(p,x+4,y+4,w-2,h,INK);
    outline(p,x+4,y+3,w-8,h-6,CREAM,DARK_WOOD,1);
    rect(p,x+7,y+4,w-14,1,WHITE);
    outline(p,x,y,w,7,PT_PAPER_DARK,INK,1);
    rect(p,x+2,y+1,w-4,2,WHITE);
    outline(p,x,y+h-7,w,7,PT_PAPER_DARK,INK,1);
    rect(p,x+2,y+h-6,w-4,2,CREAM);
    rect(p,x+4,y+8,2,h-16,WHITE);rect(p,x+w-6,y+8,2,h-16,PT_PAPER_DARK);
    center(p,x+w/2,y+13,"我们在一起",INK,1);
    drawPixelButton(p,CD_WIDTH,x+w-24,y+8,15,15,"X",button_flags(x+w-24,y+8,15,15,false));
}

static void render_popup(uint16_t *p,const cd_model_t *m)
{
    if(m->popup==CD_POPUP_NONE)return;
    int x,y,w,h;popup_rect(m->popup,&x,&y,&w,&h);
    char s[80];
    const char *titles[]={"","我们在一起","今日待办","专注一下?","上海","MacBook","OPTIONS"};
    if(m->popup==CD_POPUP_TODO)open_notebook(p,x,y,w,h);
    else if(m->popup==CD_POPUP_ANNIVERSARY)memory_scroll(p,x,y,w,h);
    else panel(p,x,y,w,h,titles[m->popup]);
    switch(m->popup){
    case CD_POPUP_ANNIVERSARY:
        heart(p,116,86,2,PINK);heart(p,269,86,2,PINK);
        snprintf(s,sizeof(s),"%ld",(long)m->elapsed_days);hud(p,200,82,s,PT_FONT_NUMBER);
        center(p,200,105,"DAYS",DARK_WOOD,PT_FONT_SMALL);
        center(p,200,120,"2024.10.07",DARK_WOOD,PT_FONT_SMALL);
        if(m->anniversary_today)center(p,200,136,"两周年快乐",INK,PT_FONT_TITLE);
        else if(m->year==2026 && m->month==10 && m->day==6)center(p,200,136,"距离两周年 1 DAY",INK,PT_FONT_TITLE);
        else center(p,200,136,"为你留一盏灯",INK,PT_FONT_TITLE);
        button(p,159,157,82,"再看一次",true);break;
    case CD_POPUP_TODO:{
        const char *labels[]={"喝水","写作业","健身","读书","给猫加餐"};
        rect(p,x+28,y+30,1,h-46,PALE_PINK);
        for(int i=0;i<m->todo_count;i++){
            int yy=y+33+i*18;
            bool checked=(m->todo_checked&(1<<i))!=0;
            drawPixelCheckbox(p,CD_WIDTH,x+15,yy,10,checked);
            text(p,x+34,yy-1,labels[m->todo_ids[i]%5],checked?PT_PAPER_DARK:INK,PT_FONT_SMALL);
            rect(p,x+30,yy+14,66,1,PT_PAPER_DARK);
        }
        if(m->todo_count<5)button(p,x+w-65,y+h-30,51,"新增",true);
        else text(p,x+15,y+h-23,"5 / 5",DARK_WOOD,PT_FONT_SMALL);
        break;}
    case CD_POPUP_FOCUS:{
        unsigned seconds=(m->focus_remaining_ms+999)/1000;
        rect(p,x+17,y+42,12,2,WOOD);rect(p,x+17,y+62,12,2,WOOD);
        rect(p,x+19,y+44,1,17,INK);rect(p,x+26,y+44,1,17,INK);
        rect(p,x+21,y+45,4,3,GOLD);rect(p,x+22,y+48,2,3,GOLD);
        rect(p,x+23,y+52,1,3,GOLD);rect(p,x+21,y+58,4,3,GOLD);
        snprintf(s,sizeof(s),"%02u:%02u",seconds/60,seconds%60);hud(p,207,y+45,s,PT_FONT_NUMBER);
        button(p,x+16,y+83,68,m->focus_running?"暂停":((m->focus_remaining_ms==CD_FOCUS_DURATION_MS || m->focus_remaining_ms==0)?"开始":"继续"),true);
        button(p,x+93,y+83,65,"重置",false);break;}
    case CD_POPUP_SETTINGS:
        text(p,x+17,y+38,"亮度",INK,PT_FONT_SMALL);
        snprintf(s,sizeof(s),"%u%%",m->brightness);hud(p,x+146,y+37,s,2);
        drawPixelSlider(p,CD_WIDTH,x+17,y+60,147,18,m->brightness,20,100);
        button(p,x+w-53,y+h-25,38,"关闭",false);break;
    case CD_POPUP_WEATHER:
        drawPixelSun(p,CD_WIDTH,x+34,y+36,2);
        text(p,x+69,y+38,"晴",INK,1);
        hud(p,x+122,y+38,"24",3);
        outline(p,x+145,y+39,4,4,CREAM,INK,1);text(p,x+151,y+47,"C",INK,1);
        center(p,x+48,y+83,"今天",INK,1);center(p,x+138,y+83,"明天",INK,1);
        hud(p,x+48,y+103,"24/18",2);hud(p,x+138,y+103,"26/19",2);
        center(p,200,y+129,"示例数据",LEAF,1);break;
    case CD_POPUP_MAC:{
        outline(p,x+17,y+37,6,6,m->mac_connected?GREEN:BLUE,INK,1);
        rect(p,x+19,y+38,2,1,m->mac_connected?RUG_LIGHT:PT_BLUE_LIGHT);
        text(p,x+31,y+35,m->mac_connected?"已连接":"未连接",INK,PT_FONT_SMALL);
        const char *labels[]={"电量","CPU","内存"};
        const unsigned values[]={m->mac_battery,m->mac_cpu,m->mac_memory};
        for(int i=0;i<3;i++){
            int yy=y+58+i*22;
            text(p,x+20,yy,labels[i],DARK_WOOD,PT_FONT_SMALL);
            snprintf(s,sizeof(s),"%u%%",values[i]);
            drawPixelNumber(p,CD_WIDTH,x+169-cd_text_width(s,2),yy-1,s,2,INK,true);
            metric_bar(p,x+87,yy+16,values[i],m->mac_connected);
        }
        center(p,200,y+131,m->mac_connected?(m->mac_charging?"充电中":"使用电池"):"示例数据",LEAF,PT_FONT_SMALL);break;}
    default:break;
    }
}

static void paint_pets(uint16_t *p,const cd_model_t *m,bool labels)
{
    for(int i=0;i<2;i++){
        const cd_cat_t *cat=&m->cats[i];
        cd_room_pet_pose_t pose=cd_room_pet_pose(m,(unsigned)i,s_life_ms);
        /* The same sprite now has a floor, cushion or sill beneath its paws. */
        if(pose.activity!=CD_ROOM_ACTIVITY_WINDOW && pose.activity!=CD_ROOM_ACTIVITY_BOX){
            rect(p,pose.x+12,pose.y+37,17,1,LEAF);
            rect(p,pose.x+14,pose.y+38,13,1,LEAF);
        }
        int jump=0;
        if(i==0 && pose.state==CD_CAT_TAP){const int jumps[]={0,0,8,13,6,0};jump=jumps[pose.frame%6];}
        uint16_t box_front[20*9];
        bool in_box=pose.activity==CD_ROOM_ACTIVITY_BOX && pose.y<133;
        if(in_box)for(int y=0;y<9;y++)
            memcpy(box_front+y*20,p+(153+y)*CD_WIDTH+63,20*sizeof(*p));
        catdesk_draw_cat(p,CD_WIDTH,pose.x,pose.y-jump,i,pose.state,pose.frame,pose.direction<0);
        if(in_box){
            /* The existing cardboard front hides the body as it settles in. */
            for(int y=0;y<9;y++)
                memcpy(p+(153+y)*CD_WIDTH+63,box_front+y*20,20*sizeof(*p));
        }
        if(cat->heart_ms) {
            const int sway[]={0,1,0,-1};
            int phase=(int)(cat->state_elapsed_ms/220)%4;
            int rise=cat->heart_ms<800?(800-(int)cat->heart_ms)/180:0;
            int hx=pose.x+18+sway[phase],hy=pose.y-13-rise*2;
            if(cat->heart_ms<180)drawPixelHeartOutline(p,CD_WIDTH,hx,hy,1,PINK);
            else heart(p,hx,hy,1,PINK);
        }
        if(labels && cat->bubble_ms){
            int bubble_y=pose.y-24;if(bubble_y<2)bubble_y=2;
            outline(p,pose.x+4,bubble_y,32,20,WHITE,INK,1);text(p,pose.x+8,bubble_y+4,"喵?",INK,1);
            rect(p,pose.x+14,bubble_y+20,4,3,INK);rect(p,pose.x+15,bubble_y+20,2,2,WHITE);
        }
        if(labels && pose.state==CD_CAT_SLEEP){text(p,pose.x+30,pose.y+4-(pose.frame%2)*2,"z",CREAM,1);}
        if(pose.activity==CD_ROOM_ACTIVITY_YARN && pose.state==CD_CAT_TAP && cat->state!=CD_CAT_TAP){
            /* A few pixels of loose thread twitch; the toy stays by its bowl. */
            rect(p,337+(pose.frame%2),222,3,1,PT_COZY_PINK);
        }
    }
}

static void paint_ambient(uint16_t *p,const cd_model_t *m)
{
    if(m->celebration_active)return;
    /* A short shooting star stays inside the left window pane, behind its
       wooden crossbars. It never asks the presenter to redraw the room. */
    if(cd_room_period(m)==CD_ROOM_NIGHT && s_ambient_ms>=6000 && s_ambient_ms<6960){
        int step=(int)(s_ambient_ms-6000)/80;
        int x=56+step,y=30+step/2;
        rect(p,x-3,y-2,1,1,PT_BLUE_LIGHT);
        rect(p,x-2,y-1,1,1,CREAM);
        rect(p,x-1,y-1,1,1,WHITE);
        rect(p,x,y,2,1,WHITE);rect(p,x+1,y-1,1,1,GOLD);
    }
    unsigned moth=s_ambient_ms%30000u;
    if(cd_room_period(m)!=CD_ROOM_DAY && moth>=18000 && moth<20400){
        /* One tiny visitor to the reading light, on an occasional short arc. */
        const int dx[]={0,2,4,5,4,2,0,-1};
        const int dy[]={0,-1,-3,-4,-3,-1,1,2};
        int phase=(int)(moth-18000)/120;
        int x=145+dx[phase%8],y=108+dy[phase%8];
        rect(p,x,y,1,3,DARK_WOOD);
        if(phase%2){rect(p,x-1,y+1,1,2,GOLD);rect(p,x+1,y+1,1,2,GOLD);}
        else{rect(p,x-2,y,2,2,CREAM);rect(p,x+1,y,2,2,CREAM);}
        rect(p,x,y,1,1,WHITE);
    }
}
static void dim_room(uint16_t *p)
{
    /* A discrete night palette substitution. No alpha, fade or new shades. */
    /* Constant cases avoid searching twelve runtime palette entries for
       every pixel throughout the night. No lookup buffer is allocated. */
#define ROOM565(c) ((((c)>>19)&31)<<11 | (((c)>>10)&63)<<5 | (((c)>>3)&31))
    for(int i=0;i<CD_WIDTH*CD_HEIGHT;i++){
        switch(p[i]){
        case ROOM565(WOOD):p[i]=ROOM565(DARK_WOOD);break;
        case ROOM565(LIGHT_WOOD):p[i]=ROOM565(WOOD);break;
        case ROOM565(WALL):p[i]=ROOM565(WALL_SHADE);break;
        case ROOM565(CREAM):p[i]=ROOM565(PT_PAPER_DARK);break;
        case ROOM565(WHITE):p[i]=ROOM565(CREAM);break;
        case ROOM565(GREEN):p[i]=ROOM565(LEAF);break;
        case ROOM565(RUG_LIGHT):p[i]=ROOM565(GREEN);break;
        case ROOM565(PT_BLUE_LIGHT):p[i]=ROOM565(BLUE);break;
        case ROOM565(GOLD):p[i]=ROOM565(WOOD);break;
        case ROOM565(PALE_PINK):case ROOM565(PT_COZY_PINK):p[i]=ROOM565(PT_RED);break;
        case ROOM565(PT_COZY_BLUE):p[i]=ROOM565(BLUE);break;
        default:break;
        }
    }
#undef ROOM565
}

static void paint_room_period(uint16_t *p,const cd_model_t *m)
{
    cd_room_period_t period=m->celebration_active ||
        (cd_ui_couple_visual_key(m)&CD_COUPLE_FINALE_BIT)?CD_ROOM_NIGHT:cd_room_period(m);
    if(period==CD_ROOM_NIGHT)dim_room(p);
    int wx=CD_WINDOW_X,wy=CD_WINDOW_Y;
    unsigned sky=period==CD_ROOM_DAY?PT_COZY_BLUE:period==CD_ROOM_DUSK?PT_ROSE_LIGHT:PT_NIGHT;
    unsigned horizon=period==CD_ROOM_DAY?PT_BLUE_LIGHT:PT_DUSK;
    rect(p,wx+5,wy+5,46,24,sky);rect(p,wx+5,wy+19,46,10,horizon);
    if(period==CD_ROOM_NIGHT){
        rect(p,wx+38,wy+6,5,6,CREAM);rect(p,wx+41,wy+5,3,6,CREAM);
        rect(p,wx+43,wy+5,3,5,PT_NIGHT);
        uint32_t stars=m->celebration_active?0:s_ambient_ms;
        sparkle(p,wx+15,wy+10,(stars/1400u)%2?PT_BLUE_LIGHT:WHITE);
        rect(p,wx+28,wy+7,1,1,(stars/2100u)%2?CREAM:WHITE);
    }else{
        int cloud=period==CD_ROOM_DAY?(int)(s_ambient_ms/8000u)%5:0;
        rect(p,wx+8+cloud,wy+10,12,2,CREAM);
        rect(p,wx+10+cloud,wy+8,7,2,WHITE);
        rect(p,wx+39,wy+7,4,4,GOLD);
        if(period==CD_ROOM_DUSK)rect(p,wx+35,wy+17,8,2,GOLD);
    }
    for(int i=0;i<4;i++){
        int xx=wx+6+i*11,yy=wy+24-i%2*3;
        rect(p,xx,yy,8,6,BLUE);rect(p,xx+1,yy-1,6,1,BLUE);
        if(period!=CD_ROOM_DAY)rect(p,xx+2,yy+2,1,1,GOLD);
    }
    /* Repaint the original mullions; moving sky pixels remain behind them. */
    rect(p,wx+27,wy+3,2,28,DARK_WOOD);rect(p,wx+27,wy+3,1,28,LIGHT_WOOD);
    rect(p,wx+3,wy+17,50,2,DARK_WOOD);rect(p,wx+3,wy+17,50,1,LIGHT_WOOD);
    const int bulbs[][2]={{151,27},{172,31},{194,19},{215,19},{238,26},{241,43}};
    for(unsigned i=0;i<sizeof(bulbs)/sizeof(bulbs[0]);i++){
        int x=bulbs[i][0],y=bulbs[i][1];
        if(period==CD_ROOM_DAY){rect(p,x+1,y+1,2,2,PT_PAPER_DARK);rect(p,x+2,y+1,1,1,WOOD);}
        else cozy_bulb(p,x,y);
    }
    if(period==CD_ROOM_DAY){
        /* Remove the warm pool with existing wood colors; no new buffer or alpha. */
        uint16_t outer=cd_color(LIGHT_WOOD),inner=cd_color(PT_CAT_ORANGE_LIGHT),floor=cd_color(WOOD);
        for(int y=119;y<152;y++)for(int x=110;x<146;x++){
            uint16_t *pixel=&p[y*CD_WIDTH+x];
            if(*pixel==outer || *pixel==inner)*pixel=floor;
        }
        rect(p,125,110,11,3,PT_PAPER_DARK);rect(p,123,113,15,5,CREAM);
    }else{
        rect(p,125,110,11,3,CREAM);rect(p,123,113,15,5,CREAM);
        rect(p,124,113,2,4,WHITE);rect(p,135,113,2,4,PT_PAPER_DARK);
    }
}

void cd_render_world(uint16_t *p,const uint16_t *bg,const cd_model_t *m)
{
    memcpy(p,bg,CD_WIDTH*CD_HEIGHT*sizeof(*p));
    paint_room_period(p,m);
    small_mac(p,m->mac_connected);
    paint_ambient(p,m);
    cd_couple_draw_room(p,&s_couple,m);
    if(cd_ui_couple_visual_key(m)&CD_COUPLE_FINALE_BIT)cd_couple_draw_finale(p);
    else paint_pets(p,m,false);
}
void cd_render_frame(uint16_t *p,const uint16_t *bg,const cd_model_t *m)
{
    memcpy(p,bg,CD_WIDTH*CD_HEIGHT*sizeof(*p));
    paint_room_period(p,m);
    paint_ambient(p,m);
    uint32_t couple=cd_ui_couple_visual_key(m);
    cd_couple_draw_room(p,&s_couple,m);
    /* Tiny information is carved into the window ledge and keepsake. */
    drawPixelSun(p,CD_WIDTH,48,60,1);
    cd_tiny_text(p,CD_WIDTH,64,65,"24",cd_color(INK));
    outline(p,73,63,3,3,WALL,INK,1);
    text(p,80,60,"上海",INK,1);
    gear(p,CD_GEAR_X,CD_GEAR_Y);
    char s[80];
    int cx=CD_FRAME_X+CD_FRAME_W/2;
    drawPixelHeartOutline(p,CD_WIDTH,cx-3,CD_FRAME_Y+5,1,PINK);
    if(m->anniversary_today)
        cd_tiny_text(p,CD_WIDTH,cx-cd_tiny_width("2 YEARS")/2,CD_FRAME_Y+19,"2 YEARS",cd_color(INK));
    else if(m->year==2026 && m->month==10 && m->day==6)
        center(p,cx,CD_FRAME_Y+12,"1 DAY",INK,1);
    else{
        snprintf(s,sizeof(s),"%ld",(long)m->elapsed_days);
        center(p,cx,CD_FRAME_Y+10,s,INK,1);
        cd_tiny_text(p,CD_WIDTH,cx-cd_tiny_width("DAYS")/2,CD_FRAME_Y+23,"DAYS",cd_color(DARK_WOOD));
    }
    cd_tiny_text(p,CD_WIDTH,cx-cd_tiny_width("2024.10.07")/2,CD_FRAME_Y+30,"2024.10.07",cd_color(DARK_WOOD));
    small_mac(p,m->mac_connected);
    if(m->focus_running || m->focus_remaining_ms<CD_FOCUS_DURATION_MS){
        unsigned sec=(m->focus_remaining_ms+999)/1000;
        snprintf(s,sizeof(s),"%02u:%02u",sec/60,sec%60);
        cd_tiny_text(p,CD_WIDTH,CD_FOCUS_HUD_X,CD_FOCUS_HUD_Y,s,cd_color(WHITE));
    }
    if(couple&CD_COUPLE_FINALE_BIT)cd_couple_draw_finale(p);
    else paint_pets(p,m,true);
    if(m->celebration_active && m->celebration_phase==CD_CELEBRATION_HEARTS){
        /* A rolled event scroll belongs to the room, with no window controls. */
        rect(p,109,115,11,33,INK);rect(p,110,116,9,30,LIGHT_WOOD);
        rect(p,107,121,3,19,INK);rect(p,108,122,2,17,WOOD);
        rect(p,284,115,11,33,INK);rect(p,285,116,9,30,LIGHT_WOOD);
        rect(p,294,121,3,19,INK);rect(p,294,122,2,17,WOOD);
        rect(p,118,104,167,61,DARK_WOOD);
        rect(p,119,101,162,62,INK);rect(p,116,104,168,55,INK);
        rect(p,120,102,160,59,CREAM);rect(p,117,105,166,53,CREAM);
        rect(p,122,103,156,1,WHITE);rect(p,122,159,156,1,PT_PAPER_DARK);
        rect(p,119,104,3,54,WHITE);rect(p,122,104,1,54,PT_PAPER_DARK);
        rect(p,278,104,3,54,WHITE);rect(p,277,104,1,54,PT_PAPER_DARK);
        center(p,200,111,"两周年快乐",INK,PT_FONT_TITLE);
        center(p,200,128,"2024.10.07",DARK_WOOD,PT_FONT_SMALL);
        rect(p,197,143,6,1,DARK_WOOD);
        center(p,200,145,"2026.10.07",DARK_WOOD,PT_FONT_SMALL);
        const int sway[]={0,1,0,-1};
        for(int i=0;i<5;i++) {
            int frame=(int)((5000-m->celebration_ms)/220+i)%6;
            int hx=158+i*19+sway[frame%4],hy=171-frame*2;
            if(frame==5)drawPixelHeartOutline(p,CD_WIDTH,hx,hy,1,PINK);
            else heart(p,hx,hy,1,PINK);
        }
        sparkle(p,135,116,GOLD);sparkle(p,265,116,GOLD);
        sparkle(p,146,184,GOLD);sparkle(p,249,183,GOLD);
    }
    if(m->notice_ms){outline(p,139,115,122,37,CREAM,INK,2);center(p,200,123,"专注完成",INK,1);center(p,200,139,"辛苦啦",DARK_WOOD,1);}
    render_popup(p,m);
}

static bool inside(int x,int y,int a,int b,int w,int h)
{return x>=a&&x<a+w&&y>=b&&y<b+h;}

bool cd_ui_pointer_button(const cd_model_t *m,int x,int y)
{
    if(m->popup==CD_POPUP_NONE)return false;
    int px,py,w,h;popup_rect(m->popup,&px,&py,&w,&h);
    if(inside(x,y,px+w-24,py+8,15,15))return true;
    switch(m->popup){
    case CD_POPUP_ANNIVERSARY:return inside(x,y,159,157,82,21);
    case CD_POPUP_TODO:return m->todo_count<5 && inside(x,y,px+w-65,py+h-30,51,21);
    case CD_POPUP_FOCUS:return inside(x,y,px+16,py+83,68,21)||inside(x,y,px+93,py+83,65,21);
    case CD_POPUP_SETTINGS:return inside(x,y,px+w-53,py+h-25,38,21);
    default:return false;
    }
}

void cd_ui_drag(cd_model_t *m,int x,int y)
{
    if(m->popup==CD_POPUP_SETTINGS && inside(x,y,120,119,161,41)){
        int v=20+(x-130)*80/140;if(v<20)v=20;if(v>100)v=100;m->brightness=v;
    }
}

static bool room_cat_hit(const cd_room_pet_pose_t *pose,unsigned kind,int x,int y)
{
    if(pose->activity==CD_ROOM_ACTIVITY_NONE)
        return inside(x,y,pose->x-3,pose->y-5,46,46);
    int jump=0;
    if(kind==CD_CAT_ORANGE && pose->state==CD_CAT_TAP){
        const int jumps[]={0,0,8,13,6,0};jump=jumps[pose->frame%6];
    }
    int column_x=x-pose->x,row_y=y-pose->y+jump;
    if(column_x<0 || column_x>=CD_CAT_WIDTH || row_y<0 || row_y>=CD_CAT_HEIGHT)return false;
    if(pose->activity==CD_ROOM_ACTIVITY_BOX && pose->y<133 && inside(x,y,63,153,20,9))return false;
    /* Render one column of the real sprite into 80 bytes. Transparent space
       around a cat sitting on furniture must leave that furniture reachable. */
    uint16_t column[CD_CAT_HEIGHT]={0};
    catdesk_draw_cat(column,1,-column_x,0,(int)kind,pose->state,pose->frame,pose->direction<0);
    return column[row_y]!=0;
}

void cd_ui_press(cd_model_t *m,int x,int y)
{
    cd_couple_touch(&s_couple);
    if(cd_ui_couple_visual_key(m)&CD_COUPLE_FINALE_BIT)return;
    if(m->popup!=CD_POPUP_NONE){
        int px,py,w,h;popup_rect(m->popup,&px,&py,&w,&h);
        if(!inside(x,y,px,py,w,h) || inside(x,y,px+w-29,py+5,24,24)){m->popup=CD_POPUP_NONE;return;}
        switch(m->popup){
        case CD_POPUP_TODO:
            for(int i=0;i<m->todo_count;i++)if(inside(x,y,px+10,py+29+i*18,w-20,18)){cd_model_toggle_todo(m,i);return;}
            if(inside(x,y,px+w-68,py+h-32,56,25))cd_model_add_todo(m);
            break;
        case CD_POPUP_FOCUS:
            if(inside(x,y,px+14,py+80,73,26)){
                if(m->focus_running)m->focus_running=false;
                else{cd_model_start_focus(m);m->popup=CD_POPUP_NONE;}
            }
            if(inside(x,y,px+90,py+80,72,26)){m->focus_running=false;m->focus_remaining_ms=CD_FOCUS_DURATION_MS;}break;
        case CD_POPUP_SETTINGS:
            if(inside(x,y,px+w-53,py+h-25,38,21))m->popup=CD_POPUP_NONE;
            else cd_ui_drag(m,x,y);
            break;
        case CD_POPUP_ANNIVERSARY:
            if(inside(x,y,156,154,88,27)){
                m->popup=CD_POPUP_NONE;cd_model_replay_anniversary(m);
            }break;
        default:break;
        }
        return;
    }
    /* Cats get generous touch targets and take priority on the floor. */
    for(int i=1;i>=0;i--){
        cd_room_pet_pose_t pose=cd_room_pet_pose(m,(unsigned)i,s_life_ms);
        if(room_cat_hit(&pose,(unsigned)i,x,y)){cd_model_tap_cat(m,i);return;}
    }
    if(inside(x,y,358,10,26,24)){m->popup=CD_POPUP_SETTINGS;return;}
    if(inside(x,y,38,16,81,63)){m->popup=CD_POPUP_WEATHER;return;}
    if(inside(x,y,184,19,53,50)){
        if(m->anniversary_today)cd_model_replay_anniversary(m);
        else m->popup=CD_POPUP_ANNIVERSARY;
        return;
    }
    if(inside(x,y,250,88,25,30)){m->popup=CD_POPUP_TODO;return;}
    if(inside(x,y,276,86,29,33)){m->popup=CD_POPUP_MAC;return;}
    if(inside(x,y,306,94,23,25)||inside(x,y,309,7,40,15))m->popup=CD_POPUP_FOCUS;
}
