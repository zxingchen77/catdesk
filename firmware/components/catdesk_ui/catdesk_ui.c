#include "catdesk_ui.h"
#include "catdesk_hal.h"
#include "catdesk_model.h"
#include "catdesk_render.h"
#include "catdesk_layout.h"
#include "catdesk_room.h"
#include "catdesk_welcome.h"
#include "catdesk_couple.h"
#include "catdesk_link.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

#ifndef CATDESK_BUILD_EPOCH
#define CATDESK_BUILD_EPOCH 1791216000LL
#endif

enum { PHYSICAL_WIDTH = 800, PHYSICAL_HEIGHT = 480 };
static const char *TAG = "CATDESK";
static cd_model_t s_model;
static uint16_t *s_background, *s_frame, *s_physical;
static lv_image_dsc_t s_image_descriptor;
static lv_obj_t *s_image;
static esp_lcd_panel_handle_t s_panel;
static nvs_handle_t s_nvs;
static bool s_nvs_ready, s_settings_dirty;
static int64_t s_last_change_us, s_last_save_us, s_last_tick_us, s_last_log_us;
static uint32_t s_frames, s_render_us;
static uint16_t s_red[32], s_green[64], s_blue[32];
static unsigned s_brightness_level;
static cd_model_t s_presented;
static bool s_presented_once;
static QueueHandle_t s_link_commands;
static int64_t s_last_mac_us;
static int64_t s_visual_hold_until_us;
static bool s_rgb_startup_pending = true;
static cd_welcome_t s_welcome, s_welcome_presented;
static bool s_welcome_presented_once, s_welcome_contact;

static void resync_rgb(void)
{
    if (!s_panel) return;
    /* Flash writes suspend the cache used by RGB bounce buffers. The public
       API restores the scan at the next VSYNC without resetting the device. */
    esp_err_t error = esp_lcd_rgb_panel_restart(s_panel);
    if (error != ESP_OK) ESP_LOGW(TAG, "RGB resync failed: %s", esp_err_to_name(error));
}

static void first_refresh_event(lv_event_t *event)
{
    (void)event;
    if (!s_rgb_startup_pending) return;
    s_rgb_startup_pending = false;
    resync_rgb();
}

static int64_t load_settings(void)
{
    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        /* Never erase a partition when preferences are unavailable. */
        ESP_LOGW(TAG, "NVS unavailable (%s); using defaults", esp_err_to_name(error));
        return 0;
    }
    error = nvs_open("catdesk", NVS_READWRITE, &s_nvs);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed (%s); using defaults", esp_err_to_name(error));
        return 0;
    }
    s_nvs_ready = true;
    uint8_t value;
    if (nvs_get_u8(s_nvs, "brightness", &value) == ESP_OK && value >= 20 && value <= 100)
        s_model.brightness = value;
    if (nvs_get_u8(s_nvs, "todo_count", &value) == ESP_OK && value >= 3 && value <= CD_TODO_CAPACITY)
        s_model.todo_count = value;
    if (nvs_get_u8(s_nvs, "todo_checks", &value) == ESP_OK)
        s_model.todo_checked = value & (uint8_t)((1u << s_model.todo_count) - 1u);
    uint8_t ids[CD_TODO_CAPACITY];
    size_t ids_size = sizeof(ids);
    if (nvs_get_blob(s_nvs, "todo_ids", ids, &ids_size) == ESP_OK && ids_size == sizeof(ids)) {
        bool valid = true;
        for (unsigned i = 0; i < CD_TODO_CAPACITY; ++i) if (ids[i] >= CD_TODO_CAPACITY) valid = false;
        if (valid) memcpy(s_model.todo_ids, ids, sizeof(ids));
    }
    int64_t epoch = 0;
    (void)nvs_get_i64(s_nvs, "epoch", &epoch);
    return epoch;
}

static void save_settings(int64_t now_us)
{
    if (!s_nvs_ready) return;
    esp_err_t error = nvs_set_u8(s_nvs, "brightness", s_model.brightness);
    if (error == ESP_OK) error = nvs_set_u8(s_nvs, "todo_count", s_model.todo_count);
    if (error == ESP_OK) error = nvs_set_u8(s_nvs, "todo_checks", s_model.todo_checked);
    if (error == ESP_OK) error = nvs_set_blob(s_nvs, "todo_ids", s_model.todo_ids, sizeof(s_model.todo_ids));
    if (error == ESP_OK) error = nvs_set_i64(s_nvs, "epoch", s_model.epoch_utc);
    if (error == ESP_OK) error = nvs_commit(s_nvs);
    resync_rgb();
    s_last_save_us = now_us;
    if (error == ESP_OK) s_settings_dirty = false;
    else {
        /* Keep the existing two-second retry delay after a storage failure,
           rather than writing flash and resyncing RGB on every UI tick. */
        s_last_change_us = now_us;
        ESP_LOGW(TAG, "Preferences save failed: %s", esp_err_to_name(error));
    }
}

static void prepare_brightness(unsigned level)
{
    if(level == s_brightness_level) return;
    for(unsigned i=0;i<32;i++) {
        s_red[i]=(uint16_t)((i*level/100)<<11);
        s_blue[i]=(uint16_t)(i*level/100);
    }
    for(unsigned i=0;i<64;i++) s_green[i]=(uint16_t)((i*level/100)<<5);
    s_brightness_level=level;
}

static void expand_area(int x1,int y1,int x2,int y2,lv_area_t *dirty)
{
    if(x1<0)x1=0;
    if(y1<0)y1=0;
    if(x2>=CD_WIDTH)x2=CD_WIDTH-1;
    if(y2>=CD_HEIGHT)y2=CD_HEIGHT-1;
    for (int y = y1; y <= y2; ++y) {
        uint16_t *row = s_physical + y * 2 * PHYSICAL_WIDTH;
        for (int x = x1; x <= x2; ++x) {
            uint16_t source = s_frame[y * CD_WIDTH + x];
            uint16_t pixel = s_red[source>>11] | s_green[(source>>5)&63] | s_blue[source&31];
            int xx = x * 2;
            if (row[xx] == pixel) continue;
            row[xx] = row[xx + 1] = row[PHYSICAL_WIDTH + xx] = row[PHYSICAL_WIDTH + xx + 1] = pixel;
            if (x < dirty->x1) dirty->x1 = x;
            if (x > dirty->x2) dirty->x2 = x;
            if (y < dirty->y1) dirty->y1 = y;
            if (y > dirty->y2) dirty->y2 = y;
        }
    }
}

static void present_ambient_area(int x1,int y1,int x2,int y2)
{
    lv_area_t dirty={.x1=CD_WIDTH,.y1=CD_HEIGHT,.x2=-1,.y2=-1};
    expand_area(x1,y1,x2,y2,&dirty);
    if(dirty.x2<0)return;
    lv_area_t area={.x1=dirty.x1*2,.y1=dirty.y1*2,.x2=dirty.x2*2+1,.y2=dirty.y2*2+1};
    lv_obj_invalidate_area(s_image,&area);
}

static void present_welcome(void)
{
    cd_welcome_rect_t bounds;
    bool brightness_changed=!s_welcome_presented_once || s_model.brightness!=s_presented.brightness;
    if(!cd_welcome_dirty_rect(&s_welcome,brightness_changed?NULL:&s_welcome_presented,&bounds))return;
    cd_welcome_render(s_frame,s_background,&s_model,&s_welcome);
    prepare_brightness(35u+s_model.brightness*65u/100u);
    present_ambient_area(bounds.x,bounds.y,bounds.x+bounds.width-1,bounds.y+bounds.height-1);
    s_welcome_presented=s_welcome;
    s_welcome_presented_once=true;
    s_presented=s_model;
    ++s_frames;
}

static void present_frame(void)
{
    if(cd_welcome_active(&s_welcome)) {
        present_welcome();
        return;
    }
    static uint32_t presented_life_ms;
    static uint32_t presented_couple_key;
    static cd_person_view_t presented_people[2];
    uint32_t life_ms=cd_ui_life_ms();
    uint32_t couple_key=cd_ui_couple_visual_key(&s_model);
    cd_render_frame(s_frame, s_background, &s_model);
    lv_area_t dirty={.x1=CD_WIDTH,.y1=CD_HEIGHT,.x2=-1,.y2=-1};
    prepare_brightness(35u+s_model.brightness*65u/100u);
    if(!s_presented_once || s_model.brightness!=s_presented.brightness ||
       s_model.celebration_active!=s_presented.celebration_active ||
       ((couple_key^presented_couple_key)&CD_COUPLE_FINALE_BIT) ||
       cd_room_period(&s_model)!=cd_room_period(&s_presented)) {
        expand_area(0,0,399,239,&dirty);
    } else {
        /* Only these regions can animate. Keep the static room in place. */
        if(s_model.celebration_active || s_presented.celebration_active || s_model.notice_ms || s_presented.notice_ms)
            expand_area(102,96,302,187,&dirty);
        if(s_model.popup!=CD_POPUP_NONE || s_presented.popup!=CD_POPUP_NONE)
            expand_area(76,42,325,201,&dirty);
        if(s_model.local_day!=s_presented.local_day)expand_area(187,19,234,66,&dirty);
        if(s_model.mac_connected!=s_presented.mac_connected)
            expand_area(CD_MAC_X-4,CD_MAC_Y,CD_MAC_X+27,CD_MAC_Y+20,&dirty);
        if(s_model.minute!=s_presented.minute || s_model.hour!=s_presented.hour || s_model.local_day!=s_presented.local_day)
            expand_area(298,226,399,239,&dirty);
        if(s_model.focus_remaining_ms<CD_FOCUS_DURATION_MS || s_presented.focus_remaining_ms<CD_FOCUS_DURATION_MS || s_model.focus_running || s_presented.focus_running)
            expand_area(CD_FOCUS_HUD_X-1,CD_FOCUS_HUD_Y-1,CD_FOCUS_HUD_X+34,CD_FOCUS_HUD_Y+8,&dirty);
        for(unsigned person=0;person<2;person++){
            cd_person_view_t now=cd_ui_couple_person(&s_model,person);
            cd_person_view_t old=presented_people[person];
            if(((couple_key^presented_couple_key)&CD_COUPLE_ROOM_BIT) ||
               ((couple_key|presented_couple_key)&CD_COUPLE_ROOM_BIT &&
                (now.x!=old.x || now.y!=old.y || now.pose!=old.pose || now.direction!=old.direction))){
                int x1=now.x<old.x?now.x:old.x,x2=now.x>old.x?now.x:old.x;
                int y1=now.y<old.y?now.y:old.y,y2=now.y>old.y?now.y:old.y;
                expand_area(x1,y1,x2+15,y2+24,&dirty);
            }
        }
    }
    if (dirty.x2 >= 0) {
        lv_area_t area = { .x1 = dirty.x1 * 2, .y1 = dirty.y1 * 2, .x2 = dirty.x2 * 2 + 1, .y2 = dirty.y2 * 2 + 1 };
        lv_obj_invalidate_area(s_image, &area);
    }
    /* Each pet has its own small moving region, including its old position.
       Window visitors never merge with a cat on the opposite side of the room. */
    for(unsigned i=0;i<CD_CAT_COUNT;i++){
        cd_room_pet_pose_t now=cd_room_pet_pose(&s_model,i,life_ms);
        cd_room_pet_pose_t old=cd_room_pet_pose(&s_presented,i,presented_life_ms);
        int x1=now.x<old.x?now.x:old.x,x2=now.x>old.x?now.x:old.x;
        int y1=now.y<old.y?now.y:old.y,y2=now.y>old.y?now.y:old.y;
        present_ambient_area(x1-4,y1-32,x2+44,y2+41);
    }
    present_ambient_area(50,27,100,54);
    present_ambient_area(140,102,156,119);
    present_ambient_area(337,220,344,223);
    s_presented=s_model;
    presented_life_ms=life_ms;
    presented_couple_key=couple_key;
    for(unsigned person=0;person<2;person++)presented_people[person]=cd_ui_couple_person(&s_model,person);
    s_presented_once=true;
    ++s_frames;
}

static void serial_task(void *argument)
{
    (void)argument;
    char line[CD_LINK_MAX_LINE]={0};
    size_t used=0;
    bool dropping=false;
    for(;;) {
        uint8_t bytes[64];
        int n=uart_read_bytes(UART_NUM_0,bytes,sizeof(bytes),pdMS_TO_TICKS(100));
        for(int i=0;i<n;i++) {
            if(bytes[i]=='\n') {
                if(!dropping && used) {
                    if(line[used-1]=='\r')--used;
                    line[used]='\0';
                    /* The UI timer owns all model mutations and replies. */
                    (void)xQueueSend(s_link_commands,line,pdMS_TO_TICKS(200));
                }
                used=0;dropping=false;
            } else if(!dropping) {
                if(bytes[i]==0 || bytes[i]>127 || (bytes[i]<32 && bytes[i]!='\r'))dropping=true;
                else if(used<sizeof(line)-1)line[used++]=(char)bytes[i];
                else dropping=true;
            }
        }
    }
}

static void process_serial(int64_t now_us)
{
    if(!s_link_commands)return;
    char line[CD_LINK_MAX_LINE],reply[48];
    for(int i=0;i<8 && xQueueReceive(s_link_commands,line,0)==pdTRUE;i++) {
        bool was_synced=s_model.clock_synced;
        if(!cd_link_parse(&s_model,line,reply,sizeof(reply)))continue;
        if(strncmp(line,"CD1 MAC ",8)==0)s_last_mac_us=now_us;
        if(strncmp(line,"CD1 TIME ",9)==0) {
            struct timeval time_value={.tv_sec=s_model.epoch_utc,.tv_usec=0};
            settimeofday(&time_value,NULL);
            if(!was_synced) {
                s_settings_dirty=true;
                s_last_change_us=now_us;
            }
        }
        printf("%s\n",reply);
        fflush(stdout);
    }
    if(s_last_mac_us && now_us-s_last_mac_us>10000000LL) {
        cd_link_disconnect(&s_model);
        s_last_mac_us=0;
    }
}

static void touch_event(lv_event_t *event)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    int code=lv_event_get_code(event);
    /* A boot gesture belongs entirely to the welcome, even if it remains
       held through the wipe. The room receives only a fresh press. */
    if(cd_welcome_active(&s_welcome) || s_welcome_contact) {
        if(code==LV_EVENT_PRESSED) {
            s_welcome_contact=true;
            if(cd_welcome_press(&s_welcome))
                ESP_LOGI(TAG,"welcome touch accepted x=%d y=%d",point.x/2,point.y/2);
        } else if(code==LV_EVENT_RELEASED || code==LV_EVENT_PRESS_LOST) {
            s_welcome_contact=false;
        }
        return;
    }
    uint8_t brightness = s_model.brightness, count = s_model.todo_count, checked = s_model.todo_checked;
    int x=point.x/2,y=point.y/2;
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        s_visual_hold_until_us=0;
        bool button=cd_ui_pointer_button(&s_model,x,y);
        cd_ui_pointer(x,y,true);
        /* Paint the down-state before the unchanged action takes effect. */
        if(button)present_frame();
        cd_ui_press(&s_model,x,y);
        ESP_LOGI(TAG,"touch x=%d y=%d popup=%d focus=%d brightness=%u",
            x,y,s_model.popup,s_model.focus_running,s_model.brightness);
        /* Keep one visible down-frame after a button closes its menu.
           Model clocks, pets, persistence and USB continue while it displays. */
        if(button && s_model.popup==CD_POPUP_NONE)
            s_visual_hold_until_us=esp_timer_get_time()+80000;
    } else if(lv_event_get_code(event)==LV_EVENT_RELEASED || lv_event_get_code(event)==LV_EVENT_PRESS_LOST) {
        cd_ui_pointer(x,y,false);
    } else cd_ui_drag(&s_model,x,y);
    if (brightness != s_model.brightness || count != s_model.todo_count || checked != s_model.todo_checked) {
        s_settings_dirty = true;
        s_last_change_us = esp_timer_get_time();
    }
}

static void tick_event(lv_timer_t *timer)
{
    (void)timer;
    int64_t now_us = esp_timer_get_time();
    bool welcome_active=cd_welcome_active(&s_welcome);
    uint32_t delta=0;
    uint64_t elapsed_ms = (uint64_t)(now_us - s_last_tick_us) / 1000;
    if (elapsed_ms) {
        delta = elapsed_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_ms;
        s_last_tick_us += (int64_t)delta * 1000;
        cd_model_tick(&s_model, delta);
        cd_ui_visual_tick(delta);
    }
    process_serial(now_us);
    bool welcome_changed=false;
    if(welcome_active) {
        cd_welcome_phase_t before=cd_welcome_phase(&s_welcome);
        welcome_changed=cd_welcome_tick(&s_welcome,delta,&s_model);
        cd_welcome_phase_t after=cd_welcome_phase(&s_welcome);
        if(after!=before) {
            ESP_LOGI(TAG,"welcome phase=%d",(int)after);
            if(after==CD_WELCOME_WAITING)ESP_LOGI(TAG,"welcome waiting for touch");
            if(after==CD_WELCOME_DONE) {
                s_presented_once=false;
                lv_timer_set_period(timer,80);
                ESP_LOGI(TAG,"welcome home; scene stopped");
            }
        }
    }
    uint32_t couple_before=cd_ui_couple_visual_key(&s_model);
    cd_ui_couple_tick(&s_model,delta,!cd_welcome_active(&s_welcome));
    if((couple_before^cd_ui_couple_visual_key(&s_model))&(CD_COUPLE_SOFA_BIT|CD_COUPLE_FINALE_BIT))
        ESP_LOGI(TAG,"couple scene sofa_ms=%u finale_ms=%u",
            (unsigned)cd_ui_couple_state()->sofa_ms,(unsigned)cd_ui_couple_state()->finale_ms);
    int64_t render_start = esp_timer_get_time();
    if((!welcome_active || welcome_changed) && now_us>=s_visual_hold_until_us)present_frame();
    s_render_us = (uint32_t)(esp_timer_get_time() - render_start);
    if (s_nvs_ready && (now_us - s_last_save_us >= 60000000 ||
        (s_settings_dirty && now_us - s_last_change_us >= 2000000))) save_settings(now_us);
    if (now_us - s_last_log_us >= 15000000) {
        s_last_log_us = now_us;
        cd_person_view_t person0=cd_ui_couple_person(&s_model,0),person1=cd_ui_couple_person(&s_model,1);
        bool people_visible=!cd_welcome_active(&s_welcome) &&
            (cd_ui_couple_visual_key(&s_model)&(CD_COUPLE_ROOM_BIT|CD_COUPLE_FINALE_BIT));
        ESP_LOGI(TAG, "alive frames=%" PRIu32 " render_us=%" PRIu32 " free_sram=%u free_psram=%u cst=%04d-%02d-%02d %02d:%02d sync=%d mac=%d anniversary=%d people=%d:(%d,%d,%d)/(%d,%d,%d)",
            s_frames, s_render_us, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), s_model.year,s_model.month,s_model.day,s_model.hour,s_model.minute,
            s_model.clock_synced,s_model.mac_connected,s_model.celebration_phase,people_visible,
            person0.x,person0.y,person0.pose,person1.x,person1.y,person1.pose);
    }
}

void catdesk_ui_start(void)
{
    ESP_LOGI(TAG, "boot");
    ESP_LOGI(TAG, "psram ok");
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(
        ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB,
        ESP_LV_ADAPTER_ROTATE_0, &panel, &touch));
    ESP_LOGI(TAG, "display ok");
    s_panel = panel;
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());
    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    /* RGB was initialized on core 0. Keep CPU rendering on that core so the
       bounce-buffer ISR can preempt it instead of competing for PSRAM. */
    adapter_cfg.task_core_id = 0;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_cfg));
    esp_lv_adapter_display_config_t display_cfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(panel, NULL, 800, 480, ESP_LV_ADAPTER_ROTATE_0);
    display_cfg.profile.use_psram = true;
    /* Use two of the existing panel buffers. Unlike TRIPLE_PARTIAL, DIRECT
       synchronizes dirty regions only, avoiding a nearly full-screen PSRAM
       copy on every cat frame. HAL allocation and RGB timings stay intact. */
    display_cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT;
    lv_display_t *display = esp_lv_adapter_register_display(&display_cfg);
    ESP_LOGI(TAG, "RGB presentation: double-direct, render_core=0");
    if (touch) {
        esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch);
        esp_lv_adapter_register_touch(&touch_cfg);
    }
    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_LOGI(TAG, "touch ok");

    cd_model_init(&s_model, CATDESK_BUILD_EPOCH);
    int64_t saved_epoch = load_settings();
    int64_t boot_epoch = (int64_t)time(NULL);
    if (boot_epoch < CATDESK_BUILD_EPOCH) boot_epoch = CATDESK_BUILD_EPOCH;
    if (boot_epoch < saved_epoch) boot_epoch = saved_epoch;
    cd_model_set_time(&s_model, boot_epoch);
    cd_welcome_init(&s_welcome,&s_model);
    cd_ui_couple_init((uint32_t)esp_timer_get_time());
    size_t logical_bytes = CD_WIDTH * CD_HEIGHT * sizeof(uint16_t);
    size_t physical_bytes = PHYSICAL_WIDTH * PHYSICAL_HEIGHT * sizeof(uint16_t);
    uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    s_background = heap_caps_malloc(logical_bytes, caps);
    s_frame = heap_caps_malloc(logical_bytes, caps);
    s_physical = heap_caps_aligned_alloc(64, physical_bytes, caps);
    if (!s_background || !s_frame || !s_physical) {
        ESP_LOGE(TAG, "Pixel buffers allocation failed; free_psram=%u", (unsigned)heap_caps_get_free_size(caps));
        heap_caps_free(s_background); heap_caps_free(s_frame); heap_caps_free(s_physical);
        return;
    }
    memset(s_physical, 0, physical_bytes);
    cd_render_background(s_background);
    s_link_commands=xQueueCreate(8,CD_LINK_MAX_LINE);
    s_image_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_image_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    s_image_descriptor.header.w = PHYSICAL_WIDTH;
    s_image_descriptor.header.h = PHYSICAL_HEIGHT;
    s_image_descriptor.header.stride = PHYSICAL_WIDTH * sizeof(uint16_t);
    s_image_descriptor.data_size = physical_bytes;
    s_image_descriptor.data = (const uint8_t *)s_physical;
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x342A32), 0);
        lv_obj_set_style_pad_all(scr, 0, 0);
        lv_obj_set_scrollable(scr, false);
        s_image = lv_image_create(scr);
        lv_image_set_src(s_image, &s_image_descriptor);
        lv_image_set_antialias(s_image, false);
        lv_obj_set_pos(s_image, 0, 0);
        lv_obj_set_size(s_image, PHYSICAL_WIDTH, PHYSICAL_HEIGHT);
        lv_obj_set_clickable(s_image, true);
        lv_obj_set_scrollable(s_image, false);
        lv_obj_add_event_cb(s_image, touch_event, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(s_image, touch_event, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(s_image, touch_event, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(s_image, touch_event, LV_EVENT_PRESS_LOST, NULL);
        /* Flush completion must run on the adapter task, never app_main. */
        lv_display_add_event_cb(display, first_refresh_event, LV_EVENT_REFR_READY, NULL);
        present_frame();
        int64_t now_us = esp_timer_get_time();
        s_last_tick_us = s_last_save_us = s_last_log_us = now_us;
        lv_timer_create(tick_event, CD_WELCOME_FRAME_MS, NULL);
        esp_lv_adapter_unlock();
    }
    ESP_LOGI(TAG, "ready %04d-%02d-%02d %02d:%02d:%02d CST days=%" PRId32 " brightness=%u free_sram=%u free_psram=%u",
        s_model.year, s_model.month, s_model.day, s_model.hour, s_model.minute, s_model.second,
        s_model.elapsed_days, s_model.brightness,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    /* Use the established UART console without changing baud, pins or USB mode. */
    esp_err_t serial_error=uart_driver_install(UART_NUM_0,1024,0,0,NULL,0);
    if(serial_error==ESP_OK) {
        if(!s_link_commands || xTaskCreate(serial_task,"catdesk_link",4096,NULL,2,NULL)!=pdPASS)
            ESP_LOGW(TAG,"Helper task unavailable; local room remains active");
    } else ESP_LOGW(TAG,"Helper serial unavailable: %s",esp_err_to_name(serial_error));
}
