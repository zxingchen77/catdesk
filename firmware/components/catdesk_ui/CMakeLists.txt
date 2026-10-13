idf_component_register(
    SRCS "catdesk_ui.c" "catdesk_model.c" "catdesk_render.c" "catdesk_couple.c" "catdesk_room.c" "catdesk_welcome.c" "catdesk_cats.c" "catdesk_font.c" "catdesk_link.c" "pixel_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES catdesk_hal lvgl esp_lvgl_adapter nvs_flash esp_timer esp_driver_uart)
string(TIMESTAMP CATDESK_BUILD_EPOCH "%s" UTC)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CATDESK_BUILD_EPOCH=${CATDESK_BUILD_EPOCH}LL)
