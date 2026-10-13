#pragma once
#include "catdesk_model.h"
typedef enum { CD_ROOM_DAY, CD_ROOM_DUSK, CD_ROOM_NIGHT } cd_room_period_t;
/* Shanghai wall-clock periods; an unsynced boot keeps the familiar dusk. */
cd_room_period_t cd_room_period(const cd_model_t *model);
uint32_t cd_ui_life_ms(void);
#define CD_WIDTH 400
#define CD_HEIGHT 240
uint16_t cd_color(unsigned rgb);
void cd_render_background(uint16_t *pixels);
void cd_render_world(uint16_t *pixels, const uint16_t *background, const cd_model_t *model);
void cd_render_frame(uint16_t *pixels, const uint16_t *background, const cd_model_t *model);
void cd_ui_press(cd_model_t *model, int x, int y);
void cd_ui_drag(cd_model_t *model, int x, int y);
/* Render-only feedback; never changes the business or pet state machine. */
void cd_ui_pointer(int x, int y, bool pressed);
void cd_ui_visual_tick(uint32_t delta_ms);
bool cd_ui_has_pointer(void);
bool cd_ui_pointer_button(const cd_model_t *model, int x, int y);
