/* CatDesk hardware adapter based on Waveshare's official RGB/GT911 sequence. */
#include "catdesk_hal.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_check.h"

static const char *TAG = "CATDESK_HAL";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_ch422g_ctrl;
static i2c_master_dev_handle_t s_ch422g_io;

static esp_err_t add_ch422g_devices(void)
{
    if (s_ch422g_ctrl && s_ch422g_io) return ESP_OK;
    const i2c_device_config_t ctrl_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x24,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    const i2c_device_config_t io_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x38,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &ctrl_cfg, &s_ch422g_ctrl), TAG, "add CH422G control");
    return i2c_master_bus_add_device(s_i2c_bus, &io_cfg, &s_ch422g_io);
}

static esp_err_t i2c_init(void)
{
    if (s_i2c_bus) return ESP_OK;
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "init I2C");
    return add_ch422g_devices();
}

static esp_err_t waveshare_touch_reset(void)
{
    ESP_RETURN_ON_ERROR(i2c_init(), TAG, "init I2C for touch reset");
    const uint8_t expander_output = 0x01;
    const uint8_t touch_high = 0x2C;
    const uint8_t touch_low = 0x2E;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_ch422g_ctrl, &expander_output, 1, I2C_MASTER_TIMEOUT_MS), TAG, "init CH422G");
    ESP_RETURN_ON_ERROR(gpio_set_direction(EXAMPLE_TOUCH_RESET_GPIO, GPIO_MODE_OUTPUT), TAG, "configure touch reset");
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_ch422g_io, &touch_high, 1, I2C_MASTER_TIMEOUT_MS), TAG, "release touch reset");
    esp_rom_delay_us(100 * 1000);
    gpio_set_level(EXAMPLE_TOUCH_RESET_GPIO, 0);
    esp_rom_delay_us(100 * 1000);
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_ch422g_io, &touch_low, 1, I2C_MASTER_TIMEOUT_MS), TAG, "select GT911 address");
    esp_rom_delay_us(200 * 1000);
    return ESP_OK;
}

esp_err_t waveshare_rgb_lcd_backlight_on(void)
{
    ESP_RETURN_ON_ERROR(i2c_init(), TAG, "init I2C for backlight");
    const uint8_t backlight_on = 0x1E;
    return i2c_master_transmit(s_ch422g_io, &backlight_on, 1, I2C_MASTER_TIMEOUT_MS);
}

esp_err_t waveshare_esp32_s3_rgb_lcd_init(esp_lv_adapter_tear_avoid_mode_t tear_mode,
                                          esp_lv_adapter_rotation_t rotation,
                                          esp_lcd_panel_handle_t *panel_handle,
                                          esp_lcd_touch_handle_t *touch_handle)
{
    if (!panel_handle || !touch_handle) return ESP_ERR_INVALID_ARG;
    *panel_handle = NULL;
    *touch_handle = NULL;

    const uint8_t num_fbs = esp_lv_adapter_get_required_frame_buffer_count(tear_mode, rotation);
    const esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            .h_res = EXAMPLE_LCD_H_RES,
            .v_res = EXAMPLE_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = 1,
        },
        .data_width = EXAMPLE_RGB_DATA_WIDTH,
        .bits_per_pixel = EXAMPLE_RGB_BIT_PER_PIXEL,
        .num_fbs = num_fbs,
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC,
        .vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC,
        .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE,
        .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK,
        .disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP,
        .data_gpio_nums = {
            EXAMPLE_LCD_IO_RGB_DATA0, EXAMPLE_LCD_IO_RGB_DATA1, EXAMPLE_LCD_IO_RGB_DATA2, EXAMPLE_LCD_IO_RGB_DATA3,
            EXAMPLE_LCD_IO_RGB_DATA4, EXAMPLE_LCD_IO_RGB_DATA5, EXAMPLE_LCD_IO_RGB_DATA6, EXAMPLE_LCD_IO_RGB_DATA7,
            EXAMPLE_LCD_IO_RGB_DATA8, EXAMPLE_LCD_IO_RGB_DATA9, EXAMPLE_LCD_IO_RGB_DATA10, EXAMPLE_LCD_IO_RGB_DATA11,
            EXAMPLE_LCD_IO_RGB_DATA12, EXAMPLE_LCD_IO_RGB_DATA13, EXAMPLE_LCD_IO_RGB_DATA14, EXAMPLE_LCD_IO_RGB_DATA15,
        },
        .flags.fb_in_psram = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, panel_handle), TAG, "create RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel_handle), TAG, "init RGB panel");

    ESP_RETURN_ON_ERROR(waveshare_touch_reset(), TAG, "reset GT911");
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(s_i2c_bus, &tp_io_cfg, &tp_io), TAG, "create touch IO");
    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels.reset = 0,
        .levels.interrupt = 0,
        .flags.swap_xy = 0,
        .flags.mirror_x = 0,
        .flags.mirror_y = 0,
    };
    return esp_lcd_touch_new_i2c_gt911(tp_io, &touch_cfg, touch_handle);
}
