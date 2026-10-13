#include "catdesk_welcome.h"

#include "catdesk_cats.h"
#include "catdesk_font.h"
#include "catdesk_render.h"
#include "pixel_theme.h"
#include "pixel_ui.h"
#include <stddef.h>
#include <string.h>

enum {
    HUT_MS = 300,
    WINDOW_MS = 600,
    TITLE_MS = 1200,
    ORANGE_MS = 1500,
    BLACK_MS = 1700,
    ARRIVAL_MS = 2800,
    HEART_MS = 3200,
    CAT_RUN_MS = 1100,
    CAT_Y = 164,
    ORANGE_X = 145,
    BLACK_X = 215,
    TILE_SIZE = 16,
    TILE_STEPS = 21,
    SPRITE_STRIDE = 40
};

/* All shared sprites fit this 3.2 KiB reusable surface. The welcome scene
 * never allocates another 400 x 240 buffer. UI rendering has one owner. */
static uint16_t s_sprite[SPRITE_STRIDE * SPRITE_STRIDE];

typedef struct {
    uint16_t *pixels;
    const cd_welcome_t *scene;
} painter_t;

static bool anniversary(const cd_model_t *model)
{
    return model != NULL && model->month == 10 && model->day == 7;
}

static unsigned window_level(const cd_welcome_t *scene)
{
    if (scene->elapsed_ms < WINDOW_MS) return 0;
    unsigned level = 1u + (scene->elapsed_ms - WINDOW_MS) / 200u;
    return level > 3u ? 3u : level;
}

static bool has_hut(const cd_welcome_t *scene)
{
    return scene->elapsed_ms >= HUT_MS;
}

static bool has_title(const cd_welcome_t *scene)
{
    return scene->elapsed_ms >= TITLE_MS &&
        (scene->phase == CD_WELCOME_INTRO || scene->phase == CD_WELCOME_WAITING);
}

static bool has_hearts(const cd_welcome_t *scene)
{
    return scene->elapsed_ms >= HEART_MS &&
        (scene->phase == CD_WELCOME_INTRO || scene->phase == CD_WELCOME_WAITING);
}

static bool prompt_bright(const cd_welcome_t *scene)
{
    return ((scene->elapsed_ms - CD_WELCOME_INTRO_MS) / 1000u) % 2u == 0;
}

static bool door_open(const cd_welcome_t *scene)
{
    return scene->phase == CD_WELCOME_WIPING ||
        (scene->phase == CD_WELCOME_ENTERING && scene->exit_ms >= CD_WELCOME_RUN_HOME_MS);
}

static int interpolate(int from, int to, uint32_t progress, uint32_t duration)
{
    if (progress >= duration) return to;
    return from + (to - from) * (int)progress / (int)duration;
}

void cd_welcome_init(cd_welcome_t *scene, const cd_model_t *model)
{
    if (scene == NULL) return;
    memset(scene, 0, sizeof(*scene));
    scene->phase = CD_WELCOME_INTRO;
    scene->anniversary = anniversary(model);
}

bool cd_welcome_active(const cd_welcome_t *scene)
{
    return scene != NULL && scene->phase != CD_WELCOME_DONE;
}

cd_welcome_phase_t cd_welcome_phase(const cd_welcome_t *scene)
{
    return scene != NULL ? scene->phase : CD_WELCOME_DONE;
}

bool cd_welcome_press(cd_welcome_t *scene)
{
    if (scene == NULL || scene->phase != CD_WELCOME_WAITING || scene->press_latched)
        return false;
    scene->press_latched = true;
    scene->exit_ms = 0;
    scene->phase = CD_WELCOME_ENTERING;
    return true;
}

bool cd_welcome_tick(cd_welcome_t *scene, uint32_t delta_ms,
                     const cd_model_t *model)
{
    if (!cd_welcome_active(scene)) return false;
    cd_welcome_t before = *scene;
    scene->anniversary = anniversary(model);
    /* Promote before adding so an unusually long scheduler delay cannot
     * wrap the accumulator. At most one rendered frame is requested. */
    uint64_t accumulated = (uint64_t)scene->fraction_ms + delta_ms;
    uint32_t steps = (uint32_t)(accumulated / CD_WELCOME_FRAME_MS);
    scene->fraction_ms = (uint32_t)(accumulated % CD_WELCOME_FRAME_MS);
    if (steps != 0) {
        uint64_t increment = (uint64_t)steps * CD_WELCOME_FRAME_MS;
        if (scene->phase == CD_WELCOME_INTRO || scene->phase == CD_WELCOME_WAITING) {
            /* Waiting needs only a two-second prompt clock after the intro.
             * This bound also keeps a device waiting for years safe. */
            uint64_t elapsed = (uint64_t)scene->elapsed_ms + increment;
            if (elapsed >= CD_WELCOME_INTRO_MS) {
                scene->phase = CD_WELCOME_WAITING;
                elapsed = CD_WELCOME_INTRO_MS + (elapsed - CD_WELCOME_INTRO_MS) % 2000u;
            }
            scene->elapsed_ms = (uint32_t)elapsed;
        } else {
            const uint32_t total = CD_WELCOME_RUN_HOME_MS + CD_WELCOME_DOOR_MS + CD_WELCOME_WIPE_MS;
            uint64_t elapsed = (uint64_t)scene->exit_ms + increment;
            scene->exit_ms = elapsed > total ? total : (uint32_t)elapsed;
            if (scene->exit_ms >= total) scene->phase = CD_WELCOME_DONE;
            else if (scene->exit_ms >= CD_WELCOME_RUN_HOME_MS + CD_WELCOME_DOOR_MS)
                scene->phase = CD_WELCOME_WIPING;
        }
    }
    cd_welcome_rect_t dirty;
    return cd_welcome_dirty_rect(scene, &before, &dirty);
}

cd_welcome_cat_pose_t cd_welcome_cat_pose(const cd_welcome_t *scene,
                                         unsigned kind)
{
    cd_welcome_cat_pose_t pose = {0, CAT_Y, CD_CAT_IDLE, 0, 1, false};
    if (scene == NULL || kind >= CD_CAT_COUNT || scene->phase == CD_WELCOME_DONE)
        return pose;
    bool orange = kind == CD_CAT_ORANGE;
    pose.direction = orange ? 1 : -1;
    pose.x = orange ? ORANGE_X : BLACK_X;
    if (scene->phase == CD_WELCOME_ENTERING || scene->phase == CD_WELCOME_WIPING) {
        uint32_t run = scene->exit_ms;
        pose.x = interpolate(pose.x, orange ? 166 : 194, run, CD_WELCOME_RUN_HOME_MS);
        pose.y = interpolate(CAT_Y, 129, run, CD_WELCOME_RUN_HOME_MS);
        pose.state = run < CD_WELCOME_RUN_HOME_MS ? CD_CAT_WALK : CD_CAT_IDLE;
        pose.frame = pose.state == CD_CAT_WALK ? (int)(run / CD_WELCOME_FRAME_MS % 6u) : 0;
        /* The open doorway conceals one cat, then the other in two opaque
         * sprite frames. No scale or alpha is added to the existing cats. */
        pose.visible = scene->phase == CD_WELCOME_ENTERING &&
            run < CD_WELCOME_RUN_HOME_MS + (orange ? 100u : 200u);
        return pose;
    }
    uint32_t start = orange ? ORANGE_MS : BLACK_MS;
    uint32_t time = scene->elapsed_ms;
    if (time < start) return pose;
    pose.visible = true;
    uint32_t run = time - start;
    if (run < CAT_RUN_MS) {
        pose.x = interpolate(orange ? -CD_CAT_WIDTH : CD_WIDTH, pose.x, run, CAT_RUN_MS);
        pose.state = CD_CAT_WALK;
        pose.frame = (int)(run / CD_WELCOME_FRAME_MS % 6u);
    } else if (time >= ARRIVAL_MS && time < ARRIVAL_MS + 200u) {
        pose.state = CD_CAT_BLINK;
        pose.frame = (int)((time - ARRIVAL_MS) / CD_WELCOME_FRAME_MS);
    } else if (time >= ARRIVAL_MS + 200u && time < HEART_MS) {
        pose.frame = time < ARRIVAL_MS + 300u ? 1 : 3;
    }
    return pose;
}

static bool revealed(const painter_t *painter, int x, int y)
{
    if (painter->scene->phase != CD_WELCOME_WIPING) return false;
    uint32_t wipe = painter->scene->exit_ms - CD_WELCOME_RUN_HOME_MS - CD_WELCOME_DOOR_MS;
    unsigned step = (unsigned)(wipe * TILE_STEPS / CD_WELCOME_WIPE_MS);
    int dx = x / TILE_SIZE - 12;
    int dy = y / TILE_SIZE - 8;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return (unsigned)(dx + dy) < step;
}

static void block(painter_t *painter, int x, int y, int width, int height,
                   unsigned rgb)
{
    if (width <= 0 || height <= 0) return;
    int right = x + width, bottom = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > CD_WIDTH) right = CD_WIDTH;
    if (bottom > CD_HEIGHT) bottom = CD_HEIGHT;
    uint16_t color = cd_color(rgb);
    for (int row = y; row < bottom; ++row)
        for (int col = x; col < right; ++col)
            if (!revealed(painter, col, row))
                painter->pixels[row * CD_WIDTH + col] = color;
}

static void border(painter_t *painter, int x, int y, int width, int height,
                    unsigned fill, unsigned edge, int thickness)
{
    block(painter, x, y, width, height, edge);
    block(painter, x + thickness, y + thickness, width - thickness * 2,
        height - thickness * 2, fill);
}

static void composite(painter_t *painter, int x, int y, int width, int height)
{
    for (int row = 0; row < height; ++row) {
        int yy = y + row;
        if (yy < 0 || yy >= CD_HEIGHT) continue;
        for (int col = 0; col < width; ++col) {
            int xx = x + col;
            uint16_t color = s_sprite[row * SPRITE_STRIDE + col];
            if (xx >= 0 && xx < CD_WIDTH && color != 0 && !revealed(painter, xx, yy))
                painter->pixels[yy * CD_WIDTH + xx] = color;
        }
    }
}

static void label(painter_t *painter, int x, int y, const char *text,
                   unsigned rgb, int scale)
{
    /* Paint one existing bitmap glyph at a time into the cat-size scratch.
     * This lets the tile mask preserve room pixels without copying a frame. */
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        char character[5] = {0};
        unsigned bytes = *cursor < 0x80u ? 1u : (*cursor < 0xe0u ? 2u : (*cursor < 0xf0u ? 3u : 4u));
        for (unsigned i = 0; i < bytes && cursor[i]; ++i) character[i] = (char)cursor[i];
        memset(s_sprite, 0, sizeof(s_sprite));
        drawPixelLabel(s_sprite, SPRITE_STRIDE, 0, 0, character, rgb, scale);
        int advance = (*cursor < 0x80u ? 6 : 13) * scale;
        composite(painter, x, y, advance, 12 * scale);
        x += advance;
        cursor += bytes;
    }
}

static void center_label(painter_t *painter, int center, int y,
                          const char *text, unsigned rgb, int scale)
{
    label(painter, center - cd_text_width(text, scale) / 2, y, text, rgb, scale);
}

static void heart(painter_t *painter, int x, int y, bool outline, unsigned color)
{
    memset(s_sprite, 0, sizeof(s_sprite));
    if (outline) drawPixelHeartOutline(s_sprite, SPRITE_STRIDE, 0, 0, 1, color);
    else drawPixelHeart(s_sprite, SPRITE_STRIDE, 0, 0, 1, color);
    composite(painter, x, y, 7, 7);
}

static void night(painter_t *painter)
{
    block(painter, 0, 0, CD_WIDTH, CD_HEIGHT, PT_NIGHT_BLUE);
    static const uint16_t stars[][2] = {
        {27, 30}, {61, 61}, {100, 22}, {139, 33}, {207, 18},
        {252, 27}, {311, 38}, {354, 24}, {377, 65}, {41, 112}, {350, 105}
    };
    for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); ++i)
        block(painter, stars[i][0], stars[i][1], 1, 1, PT_BLUE_LIGHT);
    /* Low shrubs and a short stone path echo the room's muted green rug,
     * timber and parchment. The house stays the only bright silhouette. */
    block(painter, 0, 192, CD_WIDTH, 48, PT_GREEN_DARK);
    block(painter, 0, 189, 70, 3, PT_GREEN_DARK);
    block(painter, 324, 189, 76, 3, PT_GREEN_DARK);
    block(painter, 0, 201, CD_WIDTH, 1, PT_NIGHT);
    for (int row = 0; row < 8; ++row) {
        int inset = 8 - row;
        block(painter, 161 + inset * 3, 166 + row * 6, 78 - inset * 6, 6, PT_WOOD_DARK);
        block(painter, 163 + inset * 3, 166 + row * 6, 74 - inset * 6, 1, PT_WOOD);
    }
    const int grass[][2] = {{31, 199}, {60, 215}, {103, 202}, {292, 211}, {337, 205}, {365, 224}};
    for (unsigned i = 0; i < sizeof(grass) / sizeof(grass[0]); ++i) {
        block(painter, grass[i][0], grass[i][1], 1, 3, PT_GREEN);
        block(painter, grass[i][0] + 2, grass[i][1] - 1, 1, 4, PT_GREEN);
    }
}

static void plant(painter_t *painter, int x, int y)
{
    block(painter, x + 6, y, 1, 14, PT_GREEN_DARK);
    block(painter, x + 2, y + 2, 5, 3, PT_GREEN_DARK);
    block(painter, x + 7, y + 5, 5, 3, PT_GREEN_DARK);
    block(painter, x + 3, y + 2, 3, 1, PT_GREEN);
    block(painter, x + 8, y + 5, 3, 1, PT_GREEN);
    border(painter, x + 1, y + 12, 13, 4, PT_WOOD_LIGHT, PT_OUTLINE, 1);
    border(painter, x + 3, y + 16, 9, 9, PT_WOOD, PT_OUTLINE, 1);
    block(painter, x + 4, y + 17, 2, 5, PT_WOOD_LIGHT);
}

static void window(painter_t *painter, int x, unsigned level)
{
    const unsigned light[] = {PT_NIGHT, PT_WOOD_DARK, PT_GOLD, PT_PAPER};
    if (level >= 2u) {
        block(painter, x - 3, 88, 39, 36, PT_WOOD_DARK);
        block(painter, x - 1, 90, 35, 32, PT_WOOD);
    }
    border(painter, x, 91, 33, 31, PT_WOOD, PT_OUTLINE, 2);
    border(painter, x + 4, 95, 25, 23, light[level], PT_WOOD_DARK, 1);
    if (level >= 2u) {
        block(painter, x + 6, 97, 9, 1, PT_PAPER_LIGHT);
        block(painter, x + 18, 97, 8, 1, PT_PAPER_LIGHT);
        block(painter, x + 7, 114, 6, 2, PT_GOLD);
        block(painter, x + 20, 111, 5, 5, PT_GOLD);
    }
    block(painter, x + 15, 95, 3, 23, PT_WOOD_DARK);
    block(painter, x + 4, 106, 25, 2, PT_WOOD_DARK);
    block(painter, x - 2, 121, 37, 3, PT_OUTLINE);
    block(painter, x - 1, 121, 35, 1, PT_WOOD_LIGHT);
}

static void hut(painter_t *painter)
{
    block(painter, 96, 77, 208, 89, PT_OUTLINE);
    block(painter, 100, 78, 200, 83, PT_WALL_SHADE);
    block(painter, 105, 80, 190, 76, PT_WALL);
    block(painter, 100, 78, 200, 5, PT_WOOD_DARK);
    for (int y = 86; y < 154; y += 14) {
        block(painter, 105, y, 190, 1, PT_WALL_SHADE);
        block(painter, 109 + ((y / 14) % 2) * 15, y + 1, 13, 1, PT_PAPER_DARK);
    }
    /* Original broad stepped roof with a shallow porch. No camera motion,
     * imported art or game-specific silhouettes are used. */
    for (int row = 0; row < 7; ++row) {
        int left = 140 - row * 9;
        block(painter, left, 40 + row * 5, 120 + row * 18, 5, PT_OUTLINE);
        block(painter, left + 4, 41 + row * 5, 112 + row * 18, 3, PT_WOOD_DARK);
        block(painter, left + 7, 41 + row * 5, 106 + row * 18, 1, PT_WOOD);
        for (int x = left + 12; x < 388 - left; x += 24)
            block(painter, x, 42 + row * 5, 2, 2, PT_OUTLINE);
    }
    block(painter, 80, 74, 240, 5, PT_OUTLINE);
    block(painter, 83, 74, 234, 1, PT_WOOD_LIGHT);
    block(painter, 93, 161, 214, 6, PT_OUTLINE);
    block(painter, 96, 161, 208, 2, PT_WOOD_LIGHT);
    block(painter, 102, 163, 196, 2, PT_WOOD);
    block(painter, 178, 166, 44, 3, PT_WOOD_DARK);
    window(painter, 117, window_level(painter->scene));
    window(painter, 250, window_level(painter->scene));

    border(painter, 178, 103, 44, 60, PT_WOOD_LIGHT, PT_OUTLINE, 2);
    border(painter, 182, 107, 36, 54, PT_WOOD, PT_WOOD_DARK, 1);
    if (door_open(painter->scene)) {
        block(painter, 183, 108, 34, 53, PT_GOLD);
        block(painter, 186, 108, 28, 53, PT_PAPER);
        block(painter, 188, 109, 24, 48, PT_PAPER_LIGHT);
        uint32_t opening = painter->scene->exit_ms - CD_WELCOME_RUN_HOME_MS;
        int width = opening < 100u ? 17 : opening < 200u ? 8 : 3;
        border(painter, 217 - width, 108, width, 53, PT_WOOD, PT_OUTLINE, 1);
        block(painter, 183, 163, 34, 4, PT_GOLD);
        block(painter, 179, 168, 42, 4, PT_WOOD_LIGHT);
        block(painter, 175, 173, 50, 4, PT_WOOD);
    } else {
        for (int x = 188; x < 216; x += 7) block(painter, x, 109, 1, 49, PT_WOOD_DARK);
        block(painter, 184, 109, 1, 49, PT_WOOD_LIGHT);
        border(painter, 207, 134, 4, 4, PT_GOLD, PT_OUTLINE, 1);
    }
    plant(painter, 102, 139);
    plant(painter, 285, 139);
}

static void title(painter_t *painter)
{
    block(painter, 146, 58, 2, 13, PT_OUTLINE);
    block(painter, 252, 58, 2, 13, PT_OUTLINE);
    block(painter, 130, 72, 142, 41, PT_WOOD_DARK);
    block(painter, 127, 69, 146, 40, PT_OUTLINE);
    block(painter, 129, 71, 142, 36, PT_WOOD);
    block(painter, 131, 72, 138, 2, PT_WOOD_LIGHT);
    block(painter, 131, 74, 2, 29, PT_WOOD_LIGHT);
    block(painter, 132, 103, 136, 2, PT_WOOD_DARK);
    block(painter, 135, 88, 10, 1, PT_WOOD_DARK);
    block(painter, 250, 92, 12, 1, PT_WOOD_DARK);
    block(painter, 143, 100, 13, 1, PT_WOOD_LIGHT);
    center_label(painter, 201, 80, "欢迎回家", PT_OUTLINE, 2);
    center_label(painter, 200, 79, "欢迎回家", PT_PAPER_LIGHT, 2);
    const int nails[] = {133, 265};
    for (unsigned i = 0; i < sizeof(nails) / sizeof(nails[0]); ++i) {
        block(painter, nails[i], 77, 2, 2, PT_OUTLINE);
        block(painter, nails[i], 98, 2, 2, PT_OUTLINE);
        block(painter, nails[i], 77, 1, 1, PT_GOLD);
    }
    if (painter->scene->anniversary) {
        border(painter, 135, 112, 130, 21, PT_WOOD_DARK, PT_OUTLINE, 1);
        block(painter, 137, 113, 126, 1, PT_WOOD_LIGHT);
        int text_width = cd_text_width("两周年快乐", 1);
        int left = 200 - (text_width + 28) / 2;
        heart(painter, left, 119, true, PT_ROSE_LIGHT);
        label(painter, left + 14, 117, "两周年快乐", PT_PAPER_LIGHT, 1);
        heart(painter, left + 21 + text_width, 119, true, PT_ROSE_LIGHT);
    }
}

void cd_welcome_render(uint16_t *pixels, const uint16_t *home_background,
                        const cd_model_t *model, const cd_welcome_t *scene)
{
    if (pixels == NULL || !cd_welcome_active(scene)) return;
    if (scene->phase == CD_WELCOME_WIPING && home_background != NULL && model != NULL)
        cd_render_frame(pixels, home_background, model);
    painter_t painter = {pixels, scene};
    night(&painter);
    if (has_hut(scene)) hut(&painter);
    if (has_title(scene)) title(&painter);
    for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
        cd_welcome_cat_pose_t pose = cd_welcome_cat_pose(scene, kind);
        if (!pose.visible) continue;
        memset(s_sprite, 0, sizeof(s_sprite));
        catdesk_draw_cat(s_sprite, SPRITE_STRIDE, 0, 0, (int)kind,
            pose.state, pose.frame, pose.direction < 0);
        composite(&painter, pose.x, pose.y, CD_CAT_WIDTH, CD_CAT_HEIGHT);
    }
    if (has_hearts(scene)) {
        heart(&painter, 197, 178, false, PT_HEART);
        if (scene->anniversary) {
            heart(&painter, 186, 171, false, PT_HEART);
            heart(&painter, 208, 171, false, PT_HEART);
        }
    }
    if (scene->phase == CD_WELCOME_WAITING)
        center_label(&painter, 200, 219, "轻触屏幕进入",
            prompt_bright(scene) ? PT_PAPER_LIGHT : PT_WOOD, 1);
}

static void add_rect(cd_welcome_rect_t *rect, int x, int y, int width, int height)
{
    int right = x + width, bottom = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > CD_WIDTH) right = CD_WIDTH;
    if (bottom > CD_HEIGHT) bottom = CD_HEIGHT;
    if (x >= right || y >= bottom) return;
    if (rect->width == 0 || rect->height == 0) {
        *rect = (cd_welcome_rect_t){x, y, right - x, bottom - y};
        return;
    }
    int old_right = rect->x + rect->width, old_bottom = rect->y + rect->height;
    if (x > rect->x) x = rect->x;
    if (y > rect->y) y = rect->y;
    if (right < old_right) right = old_right;
    if (bottom < old_bottom) bottom = old_bottom;
    *rect = (cd_welcome_rect_t){x, y, right - x, bottom - y};
}

bool cd_welcome_dirty_rect(const cd_welcome_t *scene,
                           const cd_welcome_t *previous,
                           cd_welcome_rect_t *rect)
{
    if (scene == NULL || rect == NULL) return false;
    *rect = (cd_welcome_rect_t){0, 0, 0, 0};
    if (previous == NULL || scene->phase == CD_WELCOME_DONE ||
        scene->phase == CD_WELCOME_WIPING || previous->phase == CD_WELCOME_WIPING) {
        if (previous != NULL && scene->phase == CD_WELCOME_DONE && previous->phase == CD_WELCOME_DONE)
            return false;
        if (previous != NULL && scene->phase == CD_WELCOME_WIPING &&
            previous->phase == CD_WELCOME_WIPING && scene->exit_ms == previous->exit_ms &&
            scene->anniversary == previous->anniversary)
            return false;
        add_rect(rect, 0, 0, CD_WIDTH, CD_HEIGHT);
        return true;
    }
    if (has_hut(scene) != has_hut(previous)) add_rect(rect, 78, 39, 244, 140);
    if (window_level(scene) != window_level(previous)) add_rect(rect, 114, 88, 173, 36);
    if (has_title(scene) != has_title(previous) ||
        (scene->anniversary != previous->anniversary && (has_title(scene) || has_title(previous))))
        add_rect(rect, 125, 58, 151, 76);
    if (has_hearts(scene) != has_hearts(previous) ||
        (scene->anniversary != previous->anniversary && (has_hearts(scene) || has_hearts(previous))))
        add_rect(rect, 184, 169, 33, 18);
    bool prompt = scene->phase == CD_WELCOME_WAITING;
    bool old_prompt = previous->phase == CD_WELCOME_WAITING;
    if (prompt != old_prompt || (prompt && prompt_bright(scene) != prompt_bright(previous)))
        add_rect(rect, 160, 219, 80, 12);
    if (door_open(scene) != door_open(previous) ||
        (door_open(scene) && scene->exit_ms != previous->exit_ms &&
            previous->exit_ms < CD_WELCOME_RUN_HOME_MS + 200u))
        add_rect(rect, 174, 103, 52, 75);
    for (unsigned kind = 0; kind < CD_CAT_COUNT; ++kind) {
        cd_welcome_cat_pose_t now = cd_welcome_cat_pose(scene, kind);
        cd_welcome_cat_pose_t old = cd_welcome_cat_pose(previous, kind);
        if (now.visible != old.visible || (now.visible && (now.x != old.x || now.y != old.y ||
            now.state != old.state || now.frame != old.frame || now.direction != old.direction))) {
            if (now.visible) add_rect(rect, now.x, now.y, CD_CAT_WIDTH, CD_CAT_HEIGHT);
            if (old.visible) add_rect(rect, old.x, old.y, CD_CAT_WIDTH, CD_CAT_HEIGHT);
        }
    }
    return rect->width != 0 && rect->height != 0;
}
