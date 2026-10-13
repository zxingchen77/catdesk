#!/usr/bin/env python3
"""Build CatDesk's small 1-bit UTF-8 font and its nearest-neighbor preview.

Chinese pixels are an unaltered subset of WenQuanYi Bitmap Song 9pt,
version 0.9.9.8, GPL-2.0 with its font embedding exception. See
docs/font-license.md. ASCII and the three small symbols are drawn here.
Run: python3 tools/generate_font.py [--bdf /path/to/wenquanyi_9pt.bdf]
Only Pillow (for the preview) is needed in addition to Python's stdlib.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_URL = (
    "https://raw.githubusercontent.com/olikraus/u8g2/"
    "c8c82e6ef7cb7edbba6ce2385ad44868c8ea6dd5/"
    "tools/font/bdf/wenquanyi_9pt.bdf"
)
SOURCE_SHA256 = "64a4f048908abdbf38b6b10af5fa2ba3e1dcf1e11151ecdf35b400feed6355dc"

TEXT = """两周年快乐我们在一起距离上海晴今天明日待办喝水写作业健身读书给猫加餐
专注一下开始暂停继续结束设置亮度喵已连接未连接电量充电内存分钟完成辛苦啦
示例数据关闭新增轻触陪伴每晚安正在休息回来取消重置小时同步本地时间需要连电脑校准
今日待办明天充电中使用电池连接电脑后同步时间时间待同步时间已同步再看一次
把日子过成诗为你留一盏灯开机时钟默认
欢迎回家轻触屏幕进入两周年快乐"""

# Original CatDesk 5x7 RPG lettering: sturdy stems, stepped bowls and slab
# terminals. Fixed six-pixel advance preserves every existing menu layout.
# These pixels are authored here, without borrowing any game's font.
# Rows are MSB-first, '/' separates rows.
ASCII = {
    " ": "00000/00000/00000/00000/00000/00000/00000",
    "!": "01110/01110/01110/00100/00100/00000/00100",
    '"': "01010/01010/01010/00000/00000/00000/00000",
    "#": "01010/01010/11111/01010/11111/01010/01010",
    "$": "00100/01111/10100/01110/00101/11110/00100",
    "%": "10001/00010/00010/00100/01000/01000/10001",
    "&": "01100/10010/10100/01000/10101/10010/01101",
    "'": "00100/00100/01000/00000/00000/00000/00000",
    "(": "00010/00100/01000/01000/01000/00100/00010",
    ")": "01000/00100/00010/00010/00010/00100/01000",
    "*": "00000/10101/01110/11111/01110/10101/00000",
    "+": "00000/00100/00100/11111/00100/00100/00000",
    ",": "00000/00000/00000/00000/00000/00100/01000",
    "-": "00000/00000/00000/11111/00000/00000/00000",
    ".": "00000/00000/00000/00000/00000/00000/00100",
    "/": "00011/00011/00110/00110/01100/11000/11000",
    "0": "01110/10001/10001/10001/10001/10001/01110",
    "1": "00100/01100/00100/00100/00100/00100/11111",
    "2": "01110/10001/00001/00110/01000/10000/11111",
    "3": "11110/00001/00001/01110/00001/00001/11110",
    "4": "00010/00110/01010/10010/11111/00010/00111",
    "5": "11111/10000/10000/11110/00001/00001/11110",
    "6": "01110/10000/10000/11110/10001/10001/01110",
    "7": "11111/00001/00010/00100/01000/01000/11100",
    "8": "01110/10001/10001/01110/10001/10001/01110",
    "9": "01110/10001/10001/01111/00001/00001/01110",
    ":": "00000/00110/00110/00000/00110/00110/00000",
    ";": "00000/00100/00100/00000/00100/00100/01000",
    "<": "00001/00010/00100/01000/00100/00010/00001",
    "=": "00000/00000/11111/00000/11111/00000/00000",
    ">": "10000/01000/00100/00010/00100/01000/10000",
    "?": "01110/11011/00011/00110/00100/00000/00100",
    "@": "01110/10001/10111/10101/10111/10000/01110",
    "A": "01110/11011/11011/11111/11011/11011/11011",
    "B": "11110/11011/11011/11110/11011/11011/11110",
    "C": "01111/11000/11000/11000/11000/11000/01111",
    "D": "11110/11011/11011/11011/11011/11011/11110",
    "E": "11111/11000/11000/11110/11000/11000/11111",
    "F": "11111/11000/11000/11110/11000/11000/11100",
    "G": "01111/11000/11000/11011/11011/11011/01111",
    "H": "11011/11011/11011/11111/11011/11011/11011",
    "I": "11111/00100/00100/00100/00100/00100/11111",
    "J": "00111/00011/00011/00011/11011/11011/01110",
    "K": "11011/11011/11110/11100/11110/11011/11011",
    "L": "11000/11000/11000/11000/11000/11000/11111",
    "M": "10001/11011/11111/10101/10101/10001/11011",
    "N": "11001/11101/11101/11011/11011/11011/11011",
    "O": "01110/11011/11011/11011/11011/11011/01110",
    "P": "11110/11011/11011/11110/11000/11000/11100",
    "Q": "01110/11011/11011/11011/11111/01110/00011",
    "R": "11110/11011/11011/11110/11100/11011/11011",
    "S": "01111/11000/11000/01110/00011/00011/11110",
    "T": "11111/10101/00100/00100/00100/00100/01110",
    "U": "11011/11011/11011/11011/11011/11011/01110",
    "V": "11011/11011/11011/11011/11011/01110/00100",
    "W": "10001/10001/10101/10101/11111/11011/10001",
    "X": "11011/11011/01110/00100/01110/11011/11011",
    "Y": "11011/11011/01110/00100/00100/00100/01110",
    "Z": "11111/00011/00110/01100/11000/11000/11111",
    "[": "01110/01000/01000/01000/01000/01000/01110",
    "\\": "10000/01000/00100/00100/00010/00001/00000",
    "]": "01110/00010/00010/00010/00010/00010/01110",
    "^": "00100/01010/10001/00000/00000/00000/00000",
    "_": "00000/00000/00000/00000/00000/00000/11111",
    "`": "01000/00100/00000/00000/00000/00000/00000",
    "a": "00000/00000/01110/00011/01111/11011/01111",
    "b": "11000/11000/11110/11011/11011/11011/11110",
    "c": "00000/00000/01111/11000/11000/11000/01111",
    "d": "00011/00011/01111/11011/11011/11011/01111",
    "e": "00000/00000/01110/11011/11111/11000/01110",
    "f": "00110/01111/01100/11110/01100/01100/01100",
    "g": "00000/01111/11011/11011/01111/00011/11110",
    "h": "11000/11000/11110/11011/11011/11011/11011",
    "i": "00110/00000/01110/00110/00110/00110/01111",
    "j": "00011/00000/00111/00011/00011/11011/01110",
    "k": "11000/11000/11011/11110/11100/11110/11011",
    "l": "01100/00100/00100/00100/00100/00100/01110",
    "m": "00000/00000/11010/11111/10101/10101/10101",
    "n": "00000/00000/11110/11011/11011/11011/11011",
    "o": "00000/00000/01110/11011/11011/11011/01110",
    "p": "00000/11110/11011/11011/11110/11000/11100",
    "q": "00000/01111/11011/11011/01111/00011/00111",
    "r": "00000/00000/11110/11011/11000/11000/11100",
    "s": "00000/00000/01111/11000/01110/00011/11110",
    "t": "01100/01100/11110/01100/01100/01100/00111",
    "u": "00000/00000/11011/11011/11011/11011/01111",
    "v": "00000/00000/11011/11011/11011/01110/00100",
    "w": "00000/00000/10101/10101/10101/11111/01010",
    "x": "00000/00000/11011/01110/00100/01110/11011",
    "y": "00000/11011/11011/11011/01111/00011/11110",
    "z": "00000/00000/11111/00011/01110/11000/11111",
    "{": "00010/00100/00100/01000/00100/00100/00010",
    "|": "00100/00100/00100/00100/00100/00100/00100",
    "}": "01000/00100/00100/00010/00100/00100/01000",
    "~": "00000/00000/01001/10110/00000/00000/00000",
}


# Original 3x5 micro lettering for labels physically engraved into furniture.
# Lowercase uses the same clear miniature capitals. Periods/colons/comma
# occupy one pixel, digits three; every glyph retains a one-pixel gap.
TINY = {
    " ": "0/0/0/0/0", "!": "1/1/1/0/1", '"': "101/101/000/000/000",
    "#": "101/111/101/111/101", "$": "011/110/010/011/110",
    "%": "101/001/010/100/101", "&": "010/101/010/101/011", "'": "1/1/0/0/0",
    "(": "01/10/10/10/01", ")": "10/01/01/01/10", "*": "101/010/111/010/101",
    "+": "000/010/111/010/000", ",": "0/0/0/1/1", "-": "000/000/111/000/000",
    ".": "0/0/0/0/1", "/": "001/001/010/100/100",
    "0": "111/101/101/101/111", "1": "010/110/010/010/111",
    "2": "110/001/111/100/111", "3": "110/001/011/001/110",
    "4": "101/101/111/001/001", "5": "111/100/110/001/110",
    "6": "011/100/111/101/111", "7": "111/001/010/010/010",
    "8": "111/101/111/101/111", "9": "111/101/111/001/110",
    ":": "0/1/0/1/0", ";": "0/1/0/1/1", "<": "001/010/100/010/001",
    "=": "000/111/000/111/000", ">": "100/010/001/010/100", "?": "110/001/010/000/010",
    "@": "010/101/111/100/011",
    "A": "010/101/111/101/101", "B": "110/101/110/101/110",
    "C": "011/100/100/100/011", "D": "110/101/101/101/110",
    "E": "111/100/110/100/111", "F": "111/100/110/100/100",
    "G": "011/100/101/101/011", "H": "101/101/111/101/101",
    "I": "111/010/010/010/111", "J": "001/001/001/101/010",
    "K": "101/101/110/101/101", "L": "100/100/100/100/111",
    "M": "101/111/111/101/101", "N": "101/111/111/111/101",
    "O": "010/101/101/101/010", "P": "110/101/110/100/100",
    "Q": "010/101/101/011/001", "R": "110/101/110/101/101",
    "S": "011/100/010/001/110", "T": "111/010/010/010/010",
    "U": "101/101/101/101/111", "V": "101/101/101/101/010",
    "W": "101/101/111/111/101", "X": "101/101/010/101/101",
    "Y": "101/101/010/010/010", "Z": "111/001/010/100/111",
    "[": "11/10/10/10/11", "\\": "100/100/010/001/001",
    "]": "11/01/01/01/11", "^": "010/101/000/000/000", "_": "000/000/000/000/111",
    "`": "1/0/0/0/0", "{": "011/010/110/010/011", "|": "1/1/1/1/1",
    "}": "110/010/011/010/110", "~": "000/011/110/000/000",
}
for letter in "abcdefghijklmnopqrstuvwxyz":
    TINY[letter] = TINY[letter.upper()]


def parse_bdf(source: str, chars: set[int]) -> dict[int, list[int]]:
    glyphs = {}
    for block in source.split("STARTCHAR ")[1:]:
        match = re.search(r"^ENCODING (\d+)$", block, re.M)
        if not match or int(match[1]) not in chars:
            continue
        code = int(match[1])
        match = re.search(r"^BBX (\d+) (\d+) (-?\d+) (-?\d+)$", block, re.M)
        width, height, off_x, off_y = map(int, match.groups())
        top = 10 - height - off_y
        data = block.split("BITMAP", 1)[1].split("ENDCHAR", 1)[0].strip().splitlines()
        rows = [0] * 12
        bit_width = ((width + 7) // 8) * 8
        for source_y, value in enumerate(data):
            dst_y = top + source_y
            if not 0 <= dst_y < 12:
                continue
            bits = int(value, 16)
            for source_x in range(width):
                dst_x = off_x + source_x
                if 0 <= dst_x < 12 and bits & (1 << (bit_width - source_x - 1)):
                    rows[dst_y] |= 1 << (15 - dst_x)
        glyphs[code] = rows
    missing = chars - glyphs.keys()
    if missing:
        raise ValueError("Missing Chinese glyphs: " + "".join(map(chr, sorted(missing))))
    return glyphs


C_RENDERER = r'''
/* Invalid UTF-8 consumes one byte, and never reads beyond its NUL terminator. */
static uint32_t next_codepoint(const unsigned char **text)
{
    const unsigned char *p = *text;
    uint32_t code;
    int length;
    if (*p < 0x80) { *text = p + 1; return *p; }
    if (*p >= 0xC2 && *p <= 0xDF) { code = *p & 0x1F; length = 2; }
    else if (*p >= 0xE0 && *p <= 0xEF) { code = *p & 0x0F; length = 3; }
    else if (*p >= 0xF0 && *p <= 0xF4) { code = *p & 7; length = 4; }
    else { *text = p + 1; return '?'; }
    for (int i = 1; i < length; ++i) {
        if (p[i] == 0 || (p[i] & 0xC0) != 0x80) {
            *text = p + 1; return '?';
        }
        code = (code << 6) | (p[i] & 0x3F);
    }
    if ((length == 3 && code < 0x800) || (length == 4 && code < 0x10000) ||
        (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) {
        *text = p + 1; return '?';
    }
    *text = p + length;
    return code;
}

static const cd_glyph_t *find_glyph(uint32_t code)
{
    size_t low = 0, high = sizeof(s_glyphs) / sizeof(s_glyphs[0]);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (s_glyphs[mid].code == code) return &s_glyphs[mid];
        if (s_glyphs[mid].code < code) low = mid + 1;
        else high = mid;
    }
    return &s_glyphs[sizeof(s_glyphs) / sizeof(s_glyphs[0]) - 1];
}

void cd_text(uint16_t *buf, int stride, int x, int y,
             const char *utf8, uint16_t color, int scale)
{
    if (!buf || !utf8 || stride <= 0 || scale <= 0) return;
    const unsigned char *p = (const unsigned char *)utf8;
    int64_t cursor_x = x, cursor_y = y;
    while (*p) {
        uint32_t code = next_codepoint(&p);
        if (code == '\n') { cursor_x = x; cursor_y += 14LL * scale; continue; }
        if (code == '\t') { cursor_x += 24LL * scale; continue; }
        if (code < 0x20 || code == 0x7F) continue;
        const cd_glyph_t *glyph = find_glyph(code);
        for (int row = 0; row < 12; ++row) {
            int64_t y0 = cursor_y + (int64_t)row * scale;
            int64_t y1 = y0 + scale;
            if (y0 >= 240 || y1 <= 0) continue;
            if (y0 < 0) y0 = 0;
            if (y1 > 240) y1 = 240;
            for (int col = 0; col < glyph->advance - 1; ++col) {
                if (!(glyph->rows[row] & (0x8000U >> col))) continue;
                int64_t x0 = cursor_x + (int64_t)col * scale;
                int64_t x1 = x0 + scale;
                if (x0 >= stride || x1 <= 0) continue;
                if (x0 < 0) x0 = 0;
                if (x1 > stride) x1 = stride;
                for (int64_t dy = y0; dy < y1; ++dy)
                    for (int64_t dx = x0; dx < x1; ++dx)
                        buf[(size_t)dy * (size_t)stride + (size_t)dx] = color;
            }
        }
        cursor_x += (int64_t)glyph->advance * scale;
    }
}

int cd_text_width(const char *utf8, int scale)
{
    if (!utf8 || scale <= 0) return 0;
    const unsigned char *p = (const unsigned char *)utf8;
    int64_t width = 0, maximum = 0;
    while (*p) {
        uint32_t code = next_codepoint(&p);
        if (code == '\n') {
            if (width > maximum) maximum = width;
            width = 0;
        } else if (code == '\t') width += 24;
        else if (code >= 0x20 && code != 0x7F) width += find_glyph(code)->advance;
    }
    if (width > maximum) maximum = width;
    if (maximum == 0) return 0;
    maximum = (maximum - 1) * scale;
    return maximum > INT_MAX ? INT_MAX : (int)maximum;
}
'''

TINY_RENDERER = r'''
static const cd_tiny_glyph_t *tiny_glyph(uint32_t code)
{
    if (code < 32 || code > 126) code = '?';
    return &s_tiny[code - 32];
}

void cd_tiny_text(uint16_t *buf, int stride, int x, int y,
                  const char *utf8, uint16_t color)
{
    if (!buf || !utf8 || stride <= 0) return;
    const unsigned char *p = (const unsigned char *)utf8;
    int64_t cursor_x = x, cursor_y = y;
    while (*p) {
        uint32_t code = next_codepoint(&p);
        if (code == '\n') { cursor_x = x; cursor_y += 6; continue; }
        if (code == '\t') { cursor_x += 8; continue; }
        if (code < 0x20 || code == 0x7F) continue;
        const cd_tiny_glyph_t *glyph = tiny_glyph(code);
        for (int row = 0; row < 5; ++row) {
            int64_t dy = cursor_y + row;
            if (dy < 0 || dy >= 240) continue;
            for (int col = 0; col < glyph->width; ++col) {
                int64_t dx = cursor_x + col;
                if (dx >= 0 && dx < stride && (glyph->rows[row] & (0x80U >> col)))
                    buf[(size_t)dy * (size_t)stride + (size_t)dx] = color;
            }
        }
        cursor_x += glyph->width + 1;
    }
}

int cd_tiny_width(const char *utf8)
{
    if (!utf8) return 0;
    const unsigned char *p = (const unsigned char *)utf8;
    int64_t width = 0, maximum = 0;
    while (*p) {
        uint32_t code = next_codepoint(&p);
        if (code == '\n') {
            if (width > maximum) maximum = width;
            width = 0;
        } else if (code == '\t') width += 8;
        else if (code >= 0x20 && code != 0x7F) width += tiny_glyph(code)->width + 1;
    }
    if (width > maximum) maximum = width;
    if (maximum == 0) return 0;
    --maximum;
    return maximum > INT_MAX ? INT_MAX : (int)maximum;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bdf", type=Path, help="Use a previously downloaded upstream BDF")
    parser.add_argument("--extra", default="", help="Additional Chinese UI text to subset")
    parser.add_argument("--no-preview", action="store_true", help="Only regenerate the font C source")
    args = parser.parse_args()
    source = args.bdf.read_bytes() if args.bdf else urllib.request.urlopen(SOURCE_URL, timeout=30).read()
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("The BDF does not match the documented upstream source SHA-256")
    corpus = TEXT + args.extra
    # Pick up additional UI literals, avoiding the generated font itself.
    for path in (ROOT / "firmware/components/catdesk_ui").rglob("*.c"):
        if path.name != "catdesk_font.c":
            corpus += path.read_text(encoding="utf-8")
    chinese = {ord(ch) for ch in corpus if "\u3400" <= ch <= "\u9fff"}
    glyphs = parse_bdf(source.decode("utf-8"), chinese)
    advances = {code: 13 for code in glyphs}
    assert set(ASCII) == {chr(code) for code in range(32, 127)}
    for ch, pattern in ASCII.items():
        rows = pattern.split("/")
        assert len(rows) == 7 and all(len(row) == 5 for row in rows), ch
        glyphs[ord(ch)] = [0, 0] + [int(row, 2) << 11 for row in rows] + [0] * 3
        advances[ord(ch)] = 6
    for ch, pattern in {
        "♥": [0x000, 0x000, 0x318, 0x7BC, 0x7FC, 0x7FC, 0x3F8, 0x1F0, 0x0E0, 0x040, 0, 0],
        "♡": [0x000, 0x000, 0x318, 0x4A4, 0x404, 0x404, 0x208, 0x110, 0x0A0, 0x040, 0, 0],
        "℃": [0xC00, 0xC00, 0x000, 0x0F0, 0x108, 0x100, 0x100, 0x100, 0x108, 0x0F0, 0, 0],
        "�": [0, 0x7FC, 0x404, 0x604, 0x504, 0x484, 0x444, 0x424, 0x414, 0x7FC, 0, 0],
    }.items():
        glyphs[ord(ch)] = [row << 4 for row in pattern]
        advances[ord(ch)] = 13
    lines = [
        '/* Generated by tools/generate_font.py; do not edit glyph rows by hand.',
        ' * Chinese font: (C) 2004-2010 WenQuanYi Project Board of Trustees,',
        ' * Qianqian Fang and contributors. GPL-2.0 with font embedding exception.',
        ' * Subset retains unaltered upstream glyph pixels. See docs/font-license.md.',
        ' * ASCII/symbol design and rendering code follow the project license. */',
        '#include "catdesk_font.h"', '#include <limits.h>', '#include <stddef.h>',
        'typedef struct { uint32_t code; uint16_t rows[12]; uint8_t advance; } cd_glyph_t;',
        'static const cd_glyph_t s_glyphs[] = {',
    ]
    for code, rows in sorted(glyphs.items()):
        values = ", ".join(f"0x{row:04X}" for row in rows)
        lines.append(f"    {{0x{code:04X}, {{{values}}}, {advances[code]}}}, /* U+{code:04X} */")
    assert set(TINY) == {chr(code) for code in range(32, 127)}
    lines += ['};', 'typedef struct { uint8_t rows[5]; uint8_t width; } cd_tiny_glyph_t;',
              'static const cd_tiny_glyph_t s_tiny[95] = {']
    for code in range(32, 127):
        rows = TINY[chr(code)].split("/")
        width = len(rows[0])
        assert len(rows) == 5 and 1 <= width <= 3 and all(len(row) == width for row in rows)
        values = ", ".join(f"0x{int(row,2) << (8-width):02X}" for row in rows)
        lines.append(f"    {{{{{values}}}, {width}}}, /* U+{code:04X} */")
    lines += ['};', C_RENDERER, TINY_RENDERER]
    target = ROOT / "firmware/components/catdesk_ui/catdesk_font.c"
    target.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")
    if args.no_preview:
        print(f"Generated {len(chinese)} Chinese + 95 ASCII + 4 symbols ({len(glyphs)} total glyphs)")
        print(f"BDF SHA256: {hashlib.sha256(source).hexdigest()}")
        print(target)
        return
    from PIL import Image
    samples = ["CatDesk 2026-10-06 22:59", "两周年快乐 ♥ 我们在一起", "距离两周年  01 DAY", "今日待办  喝水 写作业 健身", "读书 给猫加餐 专注一下", "开始 暂停 继续 重置 设置", "上海 晴 24℃  示例数据", "电量 充电中 已连接 未连接", "连接电脑后同步时间", "专注完成 辛苦啦 喵", "把日子过成诗 为你留一盏灯", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz", "0123456789 !@#$%^&*()[]{}"]
    image = Image.new("RGB", (400, 14 * len(samples) + 34), "#101820")
    for line, sample in enumerate(samples):
        x, y = 8, 6 + line * 14
        for ch in sample:
            code = ord(ch) if ord(ch) in glyphs else 0xFFFD
            for row, bits in enumerate(glyphs[code]):
                for col in range(advances[code] - 1):
                    if bits & (0x8000 >> col):
                        image.putpixel((x + col, y + row), (246, 224, 178))
            x += advances[code]
    x, y = 8, 14 * len(samples) + 11
    for ch in "2024.10.07  730 DAYS  24 C  MAC  ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        rows = TINY[ch].split("/")
        for row, bits in enumerate(rows):
            for col, bit in enumerate(bits):
                if bit == "1": image.putpixel((x+col,y+row), (246,224,178))
        x += len(rows[0]) + 1
    preview = ROOT / "assets/font_preview.png"
    preview.parent.mkdir(parents=True, exist_ok=True)
    image.resize((800, image.height * 2), Image.Resampling.NEAREST).save(preview)
    print(f"Generated {len(chinese)} Chinese + 95 ASCII + 4 symbols ({len(glyphs)} total glyphs)")
    print("Original RPG ASCII 5x7/advance6; micro ASCII 3x5 (date2024.10.07 width35)")
    print(f"BDF SHA256: {hashlib.sha256(source).hexdigest()}")
    print(target)
    print(preview)


if __name__ == "__main__":
    main()
