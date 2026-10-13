/* Host adapter for the actual welcome scene, room renderer and model.
 * Usage: output.ppm normal|anniversary elapsed_ms [touch_ms|-] [--raw]
 * No illustration or animation is duplicated in this adapter.
 */
#include "catdesk_welcome.h"
#include "catdesk_render.h"
#include "catdesk_couple.h"
#include "catdesk_cats.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned number(const char *text, unsigned maximum)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || !end || *end || value > maximum) {
        fprintf(stderr, "Invalid timeline value: %s\n", text);
        exit(1);
    }
    return (unsigned)value;
}

static uint16_t dim(uint16_t color, unsigned percent)
{
    if (percent >= 100) return color;
    unsigned level = 35 + percent * 65 / 100;
    return (uint16_t)(((((color >> 11) & 31) * level / 100) << 11) |
                      ((((color >> 5) & 63) * level / 100) << 5) |
                      ((color & 31) * level / 100));
}

static const char *phase_name(cd_welcome_phase_t phase)
{
    static const char *names[] = {"intro", "waiting", "entering", "wiping", "done"};
    return phase <= CD_WELCOME_DONE ? names[phase] : "invalid";
}

int main(int argc, char **argv)
{
    if (argc < 4 || (strcmp(argv[2], "normal") && strcmp(argv[2], "anniversary"))) {
        fprintf(stderr, "Usage: %s output.ppm normal|anniversary elapsed_ms [touch_ms|-] [--raw]\n", argv[0]);
        return 1;
    }
    unsigned elapsed = number(argv[3], 120000);
    unsigned touch = argc > 4 && strcmp(argv[4], "-") && strcmp(argv[4], "--raw")
                         ? number(argv[4], 120000) : 120001;
    bool raw = false;
    for (int i = 4; i < argc; ++i) raw |= !strcmp(argv[i], "--raw");
    uint16_t *background = calloc(CD_WIDTH * CD_HEIGHT, sizeof(*background));
    uint16_t *pixels = calloc(CD_WIDTH * CD_HEIGHT, sizeof(*pixels));
    if (!background || !pixels) { free(background); free(pixels); return 2; }

    cd_model_t model;
    cd_model_init(&model, !strcmp(argv[2], "anniversary") ? 1791369720 : 1791283320);
    model.clock_synced = true;
    cd_ui_pointer(-1, -1, false);
    cd_ui_couple_init(1);
    cd_welcome_t scene;
    cd_welcome_init(&scene, &model);
    unsigned now = 0;
    bool touched = false, touch_accepted = false;
    while (now < elapsed) {
        if (!touched && now == touch) {
            touch_accepted = cd_welcome_press(&scene);
            touched = true;
        }
        unsigned delta = cd_welcome_active(&scene) ? CD_WELCOME_FRAME_MS : 80u;
        if (delta > elapsed - now) delta = elapsed - now;
        if (!touched && touch > now && touch - now < delta) delta = touch - now;
        /* The firmware keeps business clocks and room life running while the
         * welcome owns presentation; the welcome never alters the model. */
        cd_model_tick(&model, delta);
        cd_ui_visual_tick(delta);
        cd_welcome_tick(&scene, delta, &model);
        cd_ui_couple_tick(&model,delta,!cd_welcome_active(&scene));
        now += delta;
    }
    if (!touched && now == touch) {
        touch_accepted = cd_welcome_press(&scene);
        touched = true;
    }
    cd_render_background(background);
    if (cd_welcome_active(&scene)) cd_welcome_render(pixels, background, &model, &scene);
    else cd_render_frame(pixels, background, &model);
    FILE *file = fopen(argv[1], "wb");
    if (!file) { free(background); free(pixels); return 2; }
    fprintf(file, "P6\n%d %d\n255\n", CD_WIDTH * 2, CD_HEIGHT * 2);
    unsigned char row[CD_WIDTH * 2 * 3];
    for (int y = 0; y < CD_HEIGHT * 2; ++y) {
        for (int x = 0; x < CD_WIDTH * 2; ++x) {
            uint16_t color = pixels[(y / 2) * CD_WIDTH + x / 2];
            if (!raw) color = dim(color, model.brightness);
            row[x * 3] = (unsigned char)(((color >> 11) & 31) * 255 / 31);
            row[x * 3 + 1] = (unsigned char)(((color >> 5) & 63) * 255 / 63);
            row[x * 3 + 2] = (unsigned char)((color & 31) * 255 / 31);
        }
        if (fwrite(row, 1, sizeof(row), file) != sizeof(row)) {
            fclose(file); free(background); free(pixels); return 2;
        }
    }
    if (fclose(file)) { free(background); free(pixels); return 2; }
    cd_welcome_cat_pose_t orange = cd_welcome_cat_pose(&scene, CD_CAT_ORANGE);
    cd_welcome_cat_pose_t black = cd_welcome_cat_pose(&scene, CD_CAT_BLACK);
    fprintf(stderr,
            "{\"date\":\"%04d-%02d-%02d\",\"elapsed_ms\":%u,\"scene_ms\":%u,"
            "\"exit_ms\":%u,\"phase\":\"%s\",\"active\":%s,\"anniversary\":%s,"
            "\"touch_ms\":%u,\"touch_accepted\":%s,\"press_latched\":%s,"
            "\"orange\":{\"x\":%d,\"y\":%d,\"state\":%d,\"frame\":%d,\"direction\":%d,\"visible\":%s},"
            "\"black\":{\"x\":%d,\"y\":%d,\"state\":%d,\"frame\":%d,\"direction\":%d,\"visible\":%s}}\n",
            model.year, model.month, model.day, elapsed, scene.elapsed_ms, scene.exit_ms,
            phase_name(scene.phase), cd_welcome_active(&scene) ? "true" : "false",
            scene.anniversary ? "true" : "false", touch, touch_accepted ? "true" : "false",
            scene.press_latched ? "true" : "false",
            orange.x, orange.y, orange.state, orange.frame, orange.direction, orange.visible ? "true" : "false",
            black.x, black.y, black.state, black.frame, black.direction, black.visible ? "true" : "false");
    free(background);
    free(pixels);
    return 0;
}
