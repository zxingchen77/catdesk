#!/usr/bin/env python3
"""Export actual CatDesk welcome frames, with exact 2x pixels and theme colors.

PNG/GIF files are software previews, not LCD photographs. The adapter runs
the production C welcome, existing model, cats, Chinese font and home renderer.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, PngImagePlugin

from generate_previews import save_exact_gif

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "firmware/components/catdesk_ui"
FRAME_MS = 100
TOUCH_MS = 5600
SOURCES = [ROOT / "tools/render_welcome_preview.c"] + [COMPONENT / name for name in (
    "catdesk_welcome.c", "catdesk_model.c", "catdesk_render.c", "catdesk_couple.c", "catdesk_room.c",
    "catdesk_font.c", "catdesk_cats.c", "pixel_ui.c")]


def metadata(raw: bool, state: dict | None = None) -> PngImagePlugin.PngInfo:
    info = PngImagePlugin.PngInfo()
    info.add_text("Description", "Actual CatDesk firmware welcome software preview; "
                  "400x240 logical pixels, exact 2x nearest-neighbor scaling; not an LCD photograph.")
    info.add_text("Brightness", "raw RGB565 theme" if raw else "85%; actual integer RGB565 brightness mapping")
    if state is not None:
        info.add_text("State", json.dumps(state, ensure_ascii=False, sort_keys=True))
    return info


def theme_palette(raw: bool) -> set[tuple[int, int, int]]:
    text = (COMPONENT / "include/pixel_theme.h").read_text(encoding="utf-8")
    palette = set()
    for value in re.findall(r"PT_\w+\s*=\s*(0x[0-9A-Fa-f]+)", text):
        rgb = int(value, 16)
        r, g, b = (rgb >> 19) & 31, (rgb >> 10) & 63, (rgb >> 3) & 31
        if not raw:
            level = 35 + 85 * 65 // 100
            r, g, b = r * level // 100, g * level // 100, b * level // 100
        palette.add((r * 255 // 31, g * 255 // 63, b * 255 // 31))
    return palette


def validate_font() -> None:
    generated = (COMPONENT / "catdesk_font.c").read_text(encoding="utf-8")
    available = {int(value, 16) for value in re.findall(r"\{0x([0-9A-Fa-f]+), \{", generated)}
    # The two outlined hearts beside the anniversary text use the existing
    # 7-pixel heart sprite; only the actual bitmap text needs font glyphs.
    required = {ord(character) for character in "欢迎回家轻触屏幕进入两周年快乐"}
    missing = required - available
    if missing:
        raise RuntimeError("Missing welcome glyphs: " + "".join(chr(value) for value in sorted(missing)))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "assets/welcome-2026-10-06")
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--raw", action="store_true")
    parser.add_argument("--skip-gifs", action="store_true")
    args = parser.parse_args()
    validate_font()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    palette = theme_palette(args.raw)
    manifest = {
        "description": "Actual firmware welcome and home renderer software previews, not LCD photographs.",
        "logical_size": [400, 240], "output_size": [800, 480], "scale": "nearest-neighbor 2x",
        "sample_ms": FRAME_MS, "touch_ms": TOUCH_MS,
        "brightness_percent": 100 if args.raw else 85,
        "source_sha256": {str(source.relative_to(ROOT)): hashlib.sha256(source.read_bytes()).hexdigest()
                          for source in SOURCES},
        "snapshots": {}, "animations": {},
    }
    with tempfile.TemporaryDirectory(prefix="catdesk-welcome-preview-") as temporary:
        directory = Path(temporary)
        executable = directory / "render_welcome_preview"
        subprocess.run([args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        "-I", str(COMPONENT / "include"), *map(str, SOURCES), "-o", str(executable)], check=True)
        ppm = directory / "frame.ppm"

        home_palettes: dict[str, set[tuple[int, int, int]]] = {}

        def render(variant: str, elapsed: int, touch: int | None = None,
                   validate_palette: bool = True) -> tuple[Image.Image, dict]:
            command = [str(executable), str(ppm), variant, str(elapsed), "-" if touch is None else str(touch)]
            if args.raw:
                command.append("--raw")
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            state = json.loads(result.stderr.strip().splitlines()[-1])
            with Image.open(ppm) as image:
                large = image.convert("RGB")
                assert large.size == (800, 480)
                small = large.resize((400, 240), Image.Resampling.NEAREST)
                assert ImageChops.difference(large, small.resize((800, 480), Image.Resampling.NEAREST)).getbbox() is None
                budget = 32 if state["phase"] in {"intro", "waiting", "entering"} else 256
                colors = small.getcolors(maxcolors=budget)
                if colors is None:
                    raise RuntimeError("Welcome exceeded the shared theme palette budget")
                allowed = palette if state["phase"] in {"intro", "waiting", "entering"} else palette | home_palettes.get(variant, set())
                if validate_palette and not {color for _, color in colors} <= allowed:
                    raise RuntimeError("Welcome introduced a color outside the existing RGB565 pixel theme")
            return small, state

        for variant in ("normal", "anniversary"):
            home, state = render(variant, 8500, TOUCH_MS, validate_palette=False)
            assert state["phase"] == "done"
            home_palettes[variant] = {color for _, color in home.getcolors(maxcolors=256)}
        manifest["existing_home_rgb_colors"] = {variant: sorted(colors) for variant, colors in home_palettes.items()}

        def snapshot(variant: str, elapsed: int, name: str, touch: int | None = None) -> Image.Image:
            image, state = render(variant, elapsed, touch)
            large = image.resize((800, 480), Image.Resampling.NEAREST)
            large.save(args.output_dir / name, pnginfo=metadata(args.raw, state), optimize=True)
            manifest["snapshots"][name] = state
            return large

        snapshot("normal", 0, "welcome_night_preview.png")
        snapshot("normal", 4000, "welcome_waiting_preview.png")
        snapshot("normal", 5000, "welcome_waiting_dim_preview.png")
        snapshot("anniversary", 4000, "welcome_anniversary_preview.png")
        snapshot("normal", 6400, "welcome_doorway_preview.png", TOUCH_MS)
        snapshot("normal", 6900, "welcome_wipe_preview.png", TOUCH_MS)
        snapshot("normal", 7400, "welcome_home_preview.png", TOUCH_MS)
        story = Image.new("RGB", (1600, 1920))
        for index, elapsed in enumerate((0, 300, 1100, 2500, 4000, 6100, 6900, 7400)):
            frame, state = render("normal", elapsed, TOUCH_MS)
            story.paste(frame.resize((800, 480), Image.Resampling.NEAREST), ((index % 2) * 800, (index // 2) * 480))
        story.save(args.output_dir / "welcome_storyboard.png", pnginfo=metadata(args.raw), optimize=True)

        if not args.skip_gifs:
            for variant in ("normal", "anniversary"):
                frames, states = [], []
                for elapsed in range(0, 8501, FRAME_MS):
                    image, state = render(variant, elapsed, TOUCH_MS)
                    frames.append(image)
                    states.append(state)
                assert {state["phase"] for state in states} == {"intro", "waiting", "entering", "wiping", "done"}
                assert all(state["anniversary"] == (variant == "anniversary") for state in states)
                assert states[40]["phase"] == "waiting" and not states[40]["press_latched"]
                assert states[55]["phase"] == "waiting" and states[56]["phase"] == "entering"
                assert states[-1]["phase"] == "done" and not states[-1]["active"]
                name = f"welcome_{variant}_preview.gif"
                save_exact_gif(frames, args.output_dir / name, FRAME_MS,
                               "Actual CatDesk firmware welcome; 4s intro, waiting until a touch at 5600ms, "
                               "cats enter home, discrete 800ms tile wipe; 10FPS, exact RGB565 theme palette")
                # Re-open every GIF frame: palette encoding must preserve all
                # source pixels, including discrete prompt and wipe boundaries.
                with Image.open(args.output_dir / name) as animation:
                    expanded = []
                    for index in range(animation.n_frames):
                        animation.seek(index)
                        repeats = animation.info["duration"] // FRAME_MS
                        frame = animation.convert("RGB").resize((400, 240), Image.Resampling.NEAREST)
                        expanded.extend([frame.copy()] * repeats)
                    assert len(expanded) == len(frames)
                    for expected, actual in zip(frames, expanded):
                        assert ImageChops.difference(expected, actual).getbbox() is None
                manifest["animations"][name] = {"sample_count": len(frames), "duration_ms": len(frames) * FRAME_MS,
                                                "states": states, "exact_palette_and_pixels_verified": True}
    (args.output_dir / "preview_states.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("Welcome previews verified: actual C renderer; exact 400x240 to 800x480 pixel grid; existing theme colors only.")
    print(f"Saved to {args.output_dir}")


if __name__ == "__main__":
    main()
