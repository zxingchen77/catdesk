#!/usr/bin/env python3
"""Generate original RPG pet NPCs on 40-pixel canvases and C row spans.

The art is authored on the logical pixel grid. There is no filtering, tracing,
external image asset, or antialiasing in the generation or firmware renderer.
Run from any directory with Python 3 and Pillow: python3 tools/generate_cats.py
"""

from pathlib import Path
import re
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SIZE = 40
THEME_FILE = ROOT / "firmware/components/catdesk_ui/include/pixel_theme.h"


def read_theme():
    """Read direct RGB constants and backward aliases from the shared C enum."""
    colors = {}
    entries = re.findall(r"\b(PT_[A-Z_]+)\s*=\s*(0x[0-9A-Fa-f]+|PT_[A-Z_]+)",
                         THEME_FILE.read_text())
    for name, value in entries:
        colors[name] = int(value, 16) if value.startswith("0x") else colors[value]
    return colors


THEME = read_theme()
PALETTE_NAMES = [
    None,                     # 0: transparent; never emitted as a span
    "PT_CAT_ORANGE_OUTLINE",   # 1
    "PT_CAT_ORANGE",           # 2
    "PT_CAT_ORANGE_LIGHT",     # 3
    "PT_CAT_BELLY",            # 4
    "PT_CAT_ORANGE_SHADOW",    # 5
    "PT_CAT_NOSE",             # 6
    "PT_CAT_BLACK",            # 7
    "PT_CAT_BLACK_LIGHT",      # 8
    "PT_CAT_BLACK_OUTLINE",    # 9
    "PT_CAT_EYES",             # 10
    "PT_PAPER_LIGHT",          # 11: glint uses the room's shared cream
    "PT_CAT_BLACK_LIGHT",      # 12: muzzle shares the fur highlight
    "PT_CAT_BLACK_SHADOW",     # 13
]
PALETTE = [f"#{THEME[name]:06X}" if name else "#000000" for name in PALETTE_NAMES]
STATES = [("IDLE", 4), ("BLINK", 2), ("WALK", 6), ("SLEEP", 4), ("TAP", 6)]


class Cat:
    def __init__(self, black=False):
        self.black = black
        self.im = Image.new("P", (SIZE, SIZE), 0)
        self.d = ImageDraw.Draw(self.im)
        self.out = 9 if black else 1
        self.fur = 7 if black else 2
        self.light = 8 if black else 3
        self.cream = 12 if black else 4
        self.shadow = 13 if black else 5

    def poly(self, points, fill, outline=None):
        self.d.polygon(points, fill=fill)
        if outline is not None:
            self.d.line(points + [points[0]], fill=outline, width=1)

    def line(self, points, fill, width=1):
        self.d.line(points, fill=fill, width=width)

    def box(self, coords, fill):
        self.d.rectangle(coords, fill=fill)

    def dots(self, points, fill):
        self.d.point(points, fill=fill)

    def sitting_tail(self, phase=0, raised=False, wag=False):
        """Narrow background tail; body stays the largest readable mass."""
        tx, ty = [(8, 23), (7, 22), (8, 21), (9, 22)][phase % 4]
        if self.black:
            ty -= 3
        if raised:
            tx, ty = 8 + phase % 2, 13 + phase % 2
        if wag:
            tx, ty = 6 + phase % 3, 20
        self.poly([(15, 34), (11, 34), (8, 32), (6, 28),
                   (tx - 1, ty + 1), (tx, ty - 1), (tx + 2, ty - 1),
                   (tx + 3, ty + 1), (tx + 2, ty + 3), (10, 28),
                   (12, 30), (16, 31)], self.fur, self.out)
        self.line([(9, 30), (8, 27), (tx + 1, ty + 1)], self.light)
        if not self.black:
            self.line([(tx, ty + 3), (tx + 2, ty + 3)], self.shadow)
            self.line([(7, 28), (9, 28)], self.shadow)

    def sitting_body(self, dy=0, crouch=False, airborne=False):
        top = 23 if crouch else 21
        self.poly([(20, top), (25, top), (27, 26), (28, 30),
                   (28, 34), (26, 36), (13, 36), (11, 34),
                   (12, 29), (15, 25)], self.fur, self.out)
        self.poly([(14, 28), (17, 26), (20, 27), (21, 31),
                   (19, 34), (13, 34), (13, 31)], self.light)
        self.line([(14, 34), (21, 34), (23, 32)], self.shadow)
        if self.black:
            self.line([(25, 24), (26, 27), (26, 31)], self.shadow)
            self.line([(21, 23), (23, 23)], self.light)
        else:
            self.poly([(24, 23), (25, 23), (26, 27), (25, 30),
                       (23, 31), (23, 26)], self.cream)
            self.line([(14, 29), (16, 30)], self.shadow)
            self.line([(13, 32), (16, 32)], self.shadow)
            self.line([(19, 26), (19, 28)], self.shadow)
        # Rear haunch and narrow forelegs avoid the old plush-toy proportions.
        self.line([(19, 29), (21, 31), (21, 34)], self.out)
        self.poly([(20, 31), (22, 31), (22, 34), (24, 35),
                   (24, 36), (20, 36), (19, 35)], self.shadow, self.out)
        if airborne:
            self.poly([(24, 28), (27, 28), (29, 31), (28, 33),
                       (25, 33), (24, 31)], self.fur, self.out)
            self.line([(25, 30), (27, 31)], self.light)
            self.poly([(14, 32), (18, 32), (19, 34), (17, 35),
                       (13, 35), (12, 34)], self.fur, self.out)
            self.line([(14, 33), (17, 33)], self.light)
        else:
            self.poly([(24, 28), (27, 28), (27, 34), (29, 35),
                       (29, 36), (24, 36), (23, 35)], self.fur, self.out)
            self.line([(25, 30), (25, 34)], self.light)
            self.line([(25, 35), (28, 35)], self.light)
            self.poly([(13, 34), (18, 34), (19, 35), (18, 36),
                       (12, 36), (12, 35)], self.fur, self.out)
            self.line([(13, 35), (17, 35)], self.light)

    def front_head(self, dy=0, blink=0, turn=0, squint=False, dx=0):
        """A twelve-pixel skull and two-pixel eyes, authored as an RPG NPC."""
        if turn:
            points = [(20, 15), (20, 11), (21, 10), (24, 13),
                      (28, 13), (30, 10), (31, 11), (32, 17),
                      (31, 20), (29, 22), (22, 22), (20, 20), (19, 17)]
        else:
            points = [(20, 15), (21, 10), (22, 10), (24, 13),
                      (27, 13), (29, 11), (30, 12), (30, 16),
                      (32, 18), (32, 20), (29, 22), (22, 22),
                      (20, 20), (20, 17)]
        self.poly([(x + dx, y + dy) for x, y in points], self.fur, self.out)
        ear = self.light if self.black else 6
        self.dots([(22 + dx, 12 + dy), (29 + dx, 13 + dy)], ear)
        self.line([(22 + dx, 14 + dy), (26 + dx, 14 + dy)], self.light)
        self.line([(21 + dx, 16 + dy), (21 + dx, 19 + dy)], self.light)
        self.line([(23 + dx, 21 + dy), (28 + dx, 21 + dy)], self.shadow)
        if not self.black:
            self.line([(24 + dx, 14 + dy), (24 + dx, 16 + dy)], self.shadow)
            self.dots([(21 + dx, 18 + dy)], self.shadow)
        eye_positions = [22, 28] if turn else [27]
        for eye in eye_positions:
            if blink == 2 or squint:
                self.line([(eye + dx, 18 + dy), (eye + 1 + dx, 18 + dy)], self.out)
            elif blink == 1:
                self.dots([(eye + 1 + dx, 17 + dy)], self.out)
            else:
                self.line([(eye + dx, 17 + dy), (eye + 1 + dx, 17 + dy)],
                          10 if self.black else self.out)
                if self.black:
                    self.dots([(eye + dx, 17 + dy)], 11)
        if turn:
            self.dots([(26 + dx, 19 + dy)], self.light if self.black else 6)
            self.dots([(26 + dx, 20 + dy)], self.out)
        else:
            if not self.black:
                self.box((29 + dx, 19 + dy, 31 + dx, 20 + dy), self.cream)
            else:
                self.dots([(30 + dx, 20 + dy)], self.light)
            self.dots([(32 + dx, 18 + dy)], self.light if self.black else 6)
            self.line([(30 + dx, 21 + dy), (31 + dx, 21 + dy)], self.out)


def sitting(black, phase=0, blink=0, head_dy=0, turn=0, squint=False,
            crouch=False, airborne=False, raised=False, wag=False):
    c = Cat(black)
    c.sitting_tail(phase, raised=raised, wag=wag)
    c.sitting_body(0, crouch=crouch, airborne=airborne)
    c.front_head(head_dy, blink, turn, squint)
    return c.im


def walk(black, frame):
    c = Cat(black)
    near_rear = [12, 10, 11, 14, 16, 14][frame]
    near_front = [26, 28, 27, 24, 22, 24][frame]
    far_rear = [17, 18, 16, 13, 12, 15][frame]
    far_front = [23, 21, 23, 27, 28, 26][frame]
    tx = [7, 6, 7, 8, 7, 6][frame]
    c.poly([(13, 27), (10, 27), (7, 24), (tx - 1, 18),
            (tx - 1, 14), (tx, 12), (tx + 2, 12), (tx + 3, 14),
            (tx + 1, 16), (tx + 1, 20), (10, 23), (14, 24)], c.fur, c.out)
    c.line([(tx + 1, 14), (tx, 18), (8, 22)], c.light)
    if not black:
        c.line([(tx - 1, 18), (tx + 1, 18)], c.shadow)
        c.dots([(8, 23)], c.shadow)
    for foot, hip in [(far_rear, 16), (far_front, 25)]:
        c.poly([(hip - 1, 28), (hip + 1, 28), (foot, 33),
                (foot + 2, 34), (foot + 2, 35), (foot - 1, 35),
                (foot - 1, 33)], c.shadow, c.out)
    c.poly([(12, 24), (16, 23), (23, 23), (27, 25),
            (28, 28), (26, 30), (14, 30), (11, 28)], c.fur, c.out)
    c.line([(14, 24), (23, 24)], c.light)
    c.poly([(13, 26), (16, 25), (19, 26), (18, 28), (13, 28)], c.light)
    c.line([(15, 29), (25, 29)], c.shadow)
    if not black:
        for xx in (16, 20, 24):
            c.line([(xx, 24), (xx, 26)], c.shadow)
        c.line([(23, 28), (26, 28)], c.cream)
    for foot, hip in [(near_rear, 14), (near_front, 25)]:
        c.poly([(hip - 1, 28), (hip + 2, 29), (foot + 1, 34),
                (foot + 3, 35), (foot + 3, 36), (foot - 1, 36),
                (foot - 2, 35), (foot - 1, 32)], c.fur, c.out)
        c.line([(foot, 35), (foot + 2, 35)], c.light)
    c.front_head(dy=3, dx=2)
    return c.im


def sleeping(black, frame):
    c = Cat(black)
    dy = [0, 0, -1, -1][frame]
    c.poly([(10, 30 + dy), (12, 27 + dy), (16, 26 + dy),
            (23, 27 + dy), (27, 30 + dy), (28, 34), (26, 36),
            (12, 36), (9, 34), (9, 32)], c.fur, c.out)
    c.poly([(12, 29 + dy), (16, 28 + dy), (22, 29 + dy),
            (21, 32), (13, 33), (11, 32)], c.light)
    c.line([(12, 34), (24, 34)], c.shadow)
    if not black:
        c.line([(14, 28 + dy), (15, 30 + dy)], c.shadow)
        c.line([(19, 29 + dy), (19, 31 + dy)], c.shadow)
    ear_y = 23 + dy + (frame % 2)
    c.poly([(22, 28 + dy), (23, ear_y), (24, ear_y), (26, 26 + dy),
            (29, 26 + dy), (31, 24 + dy), (32, 25 + dy), (32, 28 + dy),
            (34, 30 + dy), (33, 33), (24, 33), (22, 31)], c.fur, c.out)
    c.dots([(24, ear_y + 2), (31, 27 + dy)], c.light if black else 6)
    c.line([(24, 27 + dy), (28, 27 + dy)], c.light)
    c.line([(28, 29 + dy), (29, 29 + dy)], c.out)
    if not black:
        c.box((30, 31, 32, 32), c.cream)
    c.dots([(33, 30 + dy)], c.light if black else 6)
    c.poly([(10, 32), (12, 33), (20, 33), (21 + frame % 2, 32),
            (23, 33), (22, 35), (19, 36), (12, 36), (9, 34)], c.fur, c.out)
    c.line([(12, 34), (19, 34), (21, 33)], c.light)
    if not black:
        c.line([(14, 34), (14, 35)], c.shadow)
        c.line([(18, 34), (18, 35)], c.shadow)
    return c.im


def tap(black, frame):
    if black:
        # A reserved little turn, pleased squint, then a tail held straight up.
        return sitting(True, phase=frame, head_dy=[0, 0, 1, 1, 0, 0][frame],
                       turn=[0, 1, 1, 1, 0, 0][frame],
                       squint=frame in (2, 3, 4), raised=frame >= 2,
                       blink=1 if frame == 1 else 0)
    # Anticipation -> push -> tucked airborne paws -> landing -> tail wag.
    return sitting(False, phase=frame, head_dy=[2, 1, -2, -1, 1, 0][frame],
                   blink=2 if frame in (0, 4) else 0,
                   crouch=frame in (0, 4), airborne=frame in (2, 3),
                   raised=frame in (2, 3), wag=frame == 5)


def all_frames():
    result = []
    for black in (False, True):
        for state, count in STATES:
            for frame in range(count):
                if state == "IDLE":
                    im = sitting(black, phase=frame, head_dy=[0, 0, 1, 0][frame])
                elif state == "BLINK":
                    im = sitting(black, phase=0, blink=frame + 1)
                elif state == "WALK":
                    im = walk(black, frame)
                elif state == "SLEEP":
                    im = sleeping(black, frame)
                else:
                    im = tap(black, frame)
                result.append((black, state, frame, im))
    return result


def validate_frames(frames):
    """Keep the animation contract and palette limits in the authoring tool."""
    assert len(frames) == 44
    assert 24 <= len(set(THEME.values())) <= 32
    theme_colors = {f"#{value:06X}" for value in THEME.values()}
    for black in (False, True):
        cat_frames = [frame for frame in frames if frame[0] == black]
        colors = set()
        for _kind, state, _frame, im in cat_frames:
            left, top, right, bottom = im.getbbox()
            assert 5 <= left and right <= 36 and bottom == 37
            if state != "SLEEP":
                assert 24 <= bottom - top <= 30
            colors.update(PALETTE[im.getpixel((x, y))]
                          for y in range(SIZE) for x in range(SIZE)
                          if im.getpixel((x, y)))
        assert 5 <= len(colors) <= 7
        assert colors <= theme_colors
        for state, count in STATES:
            state_frames = [im.tobytes() for _, st, _, im in cat_frames if st == state]
            assert len(state_frames) == count and len(set(state_frames)) == count


def rgba(im):
    out = Image.new("RGBA", im.size)
    pix = [((int(PALETTE[v][1:3], 16), int(PALETTE[v][3:5], 16),
             int(PALETTE[v][5:7], 16), 255) if v else (0, 0, 0, 0))
           for v in [im.getpixel((x, y)) for y in range(SIZE) for x in range(SIZE)]]
    out.putdata(pix)
    return out


def rgb565(color):
    r, g, b = (int(color[n:n + 2], 16) for n in (1, 3, 5))
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def runs(im):
    data = im.load()
    result = []
    for y in range(SIZE):
        x = 0
        while x < SIZE:
            color = data[x, y]
            end = x + 1
            while end < SIZE and data[end, y] == color:
                end += 1
            if color:
                result.append((x, y, end - x, color))
            x = end
    return result


def emit_c(frames):
    header = """/* Generated by tools/generate_cats.py. Original CatDesk pixel art. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "catdesk_model.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { CD_CAT_WIDTH = 40, CD_CAT_HEIGHT = 40, CD_CAT_BASELINE = 36 };

/* State loop lengths: IDLE 4, BLINK 2, WALK 6, SLEEP 4, TAP 6.
 * Draw onto a host-endian RGB565 framebuffer; zero pixels are transparent.
 * x/y are the upper-left of the 40x40 logical sprite; the feet sit at y+36.
 * Negative left/top edges and the right edge are clipped. The caller must
 * ensure y+CD_CAT_HEIGHT does not exceed its framebuffer's height.
 * facing_left mirrors the whole silhouette. Frame indices wrap per state.
 */
void catdesk_draw_cat(uint16_t *pixels, int stride, int x, int y,
                      int kind, int state, int frame, bool facing_left);

#ifdef __cplusplus
}
#endif
"""
    pieces = ["/* Generated by tools/generate_cats.py. Original CatDesk pixel art. */",
              '#include "catdesk_cats.h"', '#include "pixel_theme.h"', "#include <stddef.h>", "",
              "typedef struct { uint8_t x, y, length, color; } cat_span_t;",
              "typedef struct { uint16_t first, count; } cat_frame_t;", "",
              "#define CAT_RGB565(c) ((uint16_t)((((c) >> 19) & 31) << 11 | (((c) >> 10) & 63) << 5 | (((c) >> 3) & 31)))",
              "static const uint16_t s_palette[] = {",
              "    " + ", ".join(f"CAT_RGB565({name})" if name else "0" for name in PALETTE_NAMES),
              "};", "#undef CAT_RGB565", "", "static const cat_span_t s_spans[] = {"]
    offsets = []
    total = 0
    for black, state, frame, im in frames:
        spans = runs(im)
        offsets.append((total, len(spans)))
        total += len(spans)
        pieces.append(f"    /* {'Black' if black else 'Orange'} {state.lower()} {frame} */")
        for i in range(0, len(spans), 6):
            pieces.append("    " + ", ".join("{%d,%d,%d,%d}" % p for p in spans[i:i + 6]) + ",")
    assert total < 65536
    pieces.extend(["};", "", "static const cat_frame_t s_frames[] = {"])
    for i in range(0, len(offsets), 6):
        pieces.append("    " + ", ".join("{%d,%d}" % p for p in offsets[i:i + 6]) + ",")
    pieces.extend(["};", "", "static const uint8_t s_state_first[] = {0, 4, 6, 12, 16};",
                   "static const uint8_t s_state_count[] = {4, 2, 6, 4, 6};", "",
                   """void catdesk_draw_cat(uint16_t *pixels, int stride, int x, int y,
                      int kind, int state, int frame, bool facing_left)
{
    if (!pixels || stride <= 0 || x >= stride || x <= -CD_CAT_WIDTH) return;
    if (state < CD_CAT_IDLE || state > CD_CAT_TAP) state = CD_CAT_IDLE;
    int count = s_state_count[state];
    frame %= count;
    if (frame < 0) frame += count;
    int sprite = (kind == CD_CAT_BLACK ? 22 : 0) + s_state_first[state] + frame;
    cat_frame_t f = s_frames[sprite];
    for (unsigned i = 0; i < f.count; ++i) {
        cat_span_t p = s_spans[f.first + i];
        int py = y + p.y;
        if (py < 0) continue;
        int px = x + (facing_left ? CD_CAT_WIDTH - p.x - p.length : p.x);
        int end = px + p.length;
        if (px < 0) px = 0;
        if (end > stride) end = stride;
        if (px >= end) continue;
        uint16_t *row = pixels + (size_t)py * (size_t)stride;
        uint16_t color = s_palette[p.color];
        for (; px < end; ++px) row[px] = color;
    }
}
"""])
    (ROOT / "firmware/components/catdesk_ui/include/catdesk_cats.h").write_text(header)
    (ROOT / "firmware/components/catdesk_ui/catdesk_cats.c").write_text("\n".join(pieces))
    print(f"Generated {len(frames)} frames, {total} spans, {total * 4 + len(frames) * 4} bytes of sprite data")


def preview(frames):
    cell_w, cell_h = 174, 184
    margin_x, top = 106, 72
    color = lambda name: f"#{THEME[name]:06X}"
    sheet = Image.new("RGB", (margin_x + cell_w * 6 + 18, top + cell_h * 10 + 30), color("PT_PAPER_LIGHT"))
    d = ImageDraw.Draw(sheet)
    font = ImageFont.load_default(size=16)
    small = ImageFont.load_default(size=12)
    title = ImageFont.load_default(size=24)
    def label(pos, value, font, fill):
        # A one-bit mask keeps even contact-sheet labels free of antialiasing.
        mask = Image.new("1", sheet.size, 0)
        ImageDraw.Draw(mask).text(pos, value, fill=1, font=font)
        sheet.paste(fill, (0, 0), mask)

    label((22, 18), "CATDESK  /  ORIGINAL RPG PET NPCS", title, color("PT_OUTLINE"))
    label((22, 49), "40 x 40 canvas  |  25-29px standing silhouettes  |  44 frames  |  6 colors per cat", small, color("PT_WOOD_DARK"))
    for black, state, frame, im in frames:
        row = [s for s, _ in STATES].index(state) + (5 if black else 0)
        x, y = margin_x + frame * cell_w, top + row * cell_h
        if frame == 0:
            label((16, y + 16), "BLACK" if black else "ORANGE", font, color("PT_OUTLINE"))
            label((16, y + 40), state, small, color("PT_WOOD_DARK"))
        d.rectangle((x, y, x + 162, y + 162), fill=color("PT_PAPER_DARK" if black else "PT_WALL"))
        # A quiet grounding line makes each frame's change of silhouette clear.
        d.line((x + 8, y + 148, x + 154, y + 148), fill=color("PT_WOOD_LIGHT"), width=1)
        sheet.paste(rgba(im).resize((160, 160), Image.Resampling.NEAREST), (x + 1, y + 1),
                    rgba(im).resize((160, 160), Image.Resampling.NEAREST))
        label((x + 4, y + 165), f"{state.lower()} / {frame + 1}", small, color("PT_WOOD_DARK"))
    path = ROOT / "assets/cats_preview.png"
    sheet.save(path)
    print(path)


if __name__ == "__main__":
    authored = all_frames()
    validate_frames(authored)
    emit_c(authored)
    preview(authored)
