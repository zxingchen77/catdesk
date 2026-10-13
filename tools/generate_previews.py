#!/usr/bin/env python3
"""Reproduce CatDesk software previews from the actual firmware C renderer.

Compile the pure C model, renderer, bitmap font and cat sprites once with
host clang, then render each timestamp through that executable. PNGs and
GIFs use exact nearest-neighbor 2x pixels, with the firmware's default
85-percent RGB565 brightness mapping. These are software previews, not
photographs of the physical LCD. Use --raw to export the undimmed palette.

Requires Python 3, Pillow, and a host C compiler.
"""

from __future__ import annotations

import argparse
import ast
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, PngImagePlugin

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "firmware/components/catdesk_ui"
FRAME_MS = 120
ROOM_FRAME_MS = 400
ROOM_LOOP_MS = 300000
ROOM_ACTIVITY_NAMES = ("none", "sofa", "box", "yarn", "window", "mac", "sniff", "chase", "cuddle")
ROOM_INSPECTIONS = (
    ("sofa", 45000, {"rendered_orange":1}),
    ("box", 75000, {"rendered_black":2}),
    ("yarn", 105000, {"rendered_orange":3}),
    ("window", 135000, {"rendered_black":4}),
    ("mac", 165000, {"rendered_orange":5}),
    ("sniff", 195000, {"rendered_orange":6, "rendered_black":6}),
    ("chase", 230000, {"rendered_orange":7, "rendered_black":7}),
    ("cuddle", 270000, {"rendered_orange":8, "rendered_black":8}),
)
POPUPS = (
    ("anniversary-popup", "anniversary_popup_preview.png", 1),
    ("todo", "todo_preview.png", 2),
    ("focus", "focus_preview.png", 3),
    ("weather", "weather_preview.png", 4),
    ("mac", "mac_preview.png", 5),
    ("settings", "settings_preview.png", 6),
)


def validate_font_coverage() -> None:
    generator = ast.parse((ROOT / "tools/generate_font.py").read_text(encoding="utf-8"))
    seed_text = next(ast.literal_eval(node.value) for node in generator.body
                     if isinstance(node, ast.Assign) and
                     any(isinstance(target, ast.Name) and target.id == "TEXT" for target in node.targets))
    sources = "\n".join((COMPONENT / filename).read_text(encoding="utf-8")
                        for filename in ("catdesk_render.c", "catdesk_couple.c", "pixel_ui.c"))
    literals = "".join(re.findall(r'"(?:[^"\\]|\\.)*"', sources))
    required = {ord(char) for char in literals if ord(char) >= 128}
    seed = {ord(char) for char in seed_text}
    chinese = {code for code in required if 0x3400 <= code <= 0x9FFF}
    missing_seed = chinese - seed
    generated = (COMPONENT / "catdesk_font.c").read_text(encoding="utf-8")
    available = {int(code, 16) for code in re.findall(r"\{0x([0-9A-Fa-f]+), \{", generated)}
    missing_font = required - available
    missing_ascii = set(range(32, 127)) - available
    if missing_seed:
        print("Font TEXT seed does not yet include: " + "".join(map(chr, sorted(missing_seed))))
    if missing_font or missing_ascii:
        raise RuntimeError("Missing generated glyphs: " + "".join(map(chr, sorted(missing_font | missing_ascii))) +
                           "; rerun python3 tools/generate_font.py")
    print(f"Font coverage: all {len(required)} Unicode text characters ({len(chinese)} Chinese) and 95 printable ASCII present")


def png_info(raw: bool) -> PngImagePlugin.PngInfo:
    info = PngImagePlugin.PngInfo()
    info.add_text("Description", "CatDesk software preview rendered by the actual firmware C model and renderer; "
                  "400x240 logical pixels scaled 2x nearest neighbor. Not a photograph of the LCD.")
    info.add_text("Brightness", "raw palette" if raw else "85%; same integer RGB565 dimming as firmware")
    return info


def save_exact_gif(frames: list[Image.Image], target: Path, frame_ms: int = FRAME_MS,
                   comment: str = "Actual CatDesk firmware C renderer software preview") -> None:
    # A common palette preserves each renderer color with no dithering or
    # color drift between frames. The firmware uses far fewer than 256 colors.
    colors = set()
    for frame in frames:
        values = frame.getcolors(maxcolors=256)
        if values is None:
            raise RuntimeError("Renderer exceeded the exact GIF palette budget")
        colors.update(color for _, color in values)
    if len(colors) > 256:
        raise RuntimeError(f"Animation needs {len(colors)} colors; exact GIF limit is 256")
    palette = Image.new("P", (1, 1))
    ordered = sorted(colors)
    ordered += [ordered[-1]] * (256 - len(ordered))
    palette.putpalette([channel for color in ordered for channel in color])
    encoded = [frame.quantize(palette=palette, dither=Image.Dither.NONE).resize((800, 480), Image.Resampling.NEAREST)
               for frame in frames]
    encoded[0].save(target, save_all=True, append_images=encoded[1:], duration=frame_ms,
                    loop=0, optimize=True, disposal=1, comment=comment.encode("utf-8"))
    with Image.open(target) as verify:
        total_duration = 0
        for index in range(verify.n_frames):
            verify.seek(index)
            total_duration += verify.info.get("duration", 0)
        assert verify.size == (800, 480)
        assert total_duration == len(frames) * frame_ms
    print(f"{target.name}: {len(frames)} x {frame_ms} ms samples, {total_duration / 1000:.2f} s, "
          f"{target.stat().st_size / 1024:.1f} KiB")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "assets")
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--raw", action="store_true", help="Export the undimmed renderer palette")
    parser.add_argument("--skip-gifs", action="store_true", help="Only regenerate static PNGs")
    parser.add_argument("--skip-room-gif", action="store_true", help="Skip the full five-minute production room-life loop")
    parser.add_argument("--cats-duration-ms", type=int, default=42000,
                        help="Natural cat simulation duration; default includes tap, blink, walk and sleep")
    args = parser.parse_args()
    if not 12000 <= args.cats_duration_ms <= 600000:
        parser.error("--cats-duration-ms must be between 12000 and 600000")
    validate_font_coverage()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    metadata = png_info(args.raw)
    with tempfile.TemporaryDirectory(prefix="catdesk-preview-") as temporary:
        temporary = Path(temporary)
        executable = temporary / "render_preview"
        sources = [ROOT / "tools/render_preview.c"] + [COMPONENT / name for name in
                    ("catdesk_model.c", "catdesk_render.c", "catdesk_couple.c", "catdesk_room.c", "catdesk_font.c", "catdesk_cats.c", "pixel_ui.c")]
        subprocess.run([args.cc, "-std=c11", "-O2", "-I", str(COMPONENT / "include"),
                        *map(str, sources), "-o", str(executable)], check=True)
        ppm = temporary / "frame.ppm"

        def render(mode: str, elapsed: int = 0, taps: str = "none") -> tuple[Image.Image, dict]:
            command = [str(executable), str(ppm), mode, str(elapsed), taps]
            if args.raw:
                command.append("--raw")
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            state = json.loads(result.stderr.strip().splitlines()[-1])
            with Image.open(ppm) as source:
                assert source.size == (800, 480)
                # The C export is already exactly 2x nearest-neighbor. Keeping
                # logical pixels in memory lowers GIF generation memory use.
                frame = source.convert("RGB").resize((400, 240), Image.Resampling.NEAREST)
                assert ImageChops.difference(source.convert("RGB"), frame.resize((800,480), Image.Resampling.NEAREST)).getbbox() is None
                if frame.getcolors(maxcolors=32) is None:
                    raise RuntimeError(f"{mode} exceeds the shared 32-color theme budget")
            return frame, state

        snapshot_states = {}

        def snapshot(mode: str, filename: str, elapsed: int = 0, taps: str = "none") -> tuple[Image.Image, dict]:
            frame, state = render(mode, elapsed, taps)
            large = frame.resize((800, 480), Image.Resampling.NEAREST)
            info = png_info(args.raw)
            info.add_text("Scenario", mode)
            info.add_text("State", json.dumps(state, sort_keys=True))
            if state.get("inspection_only"):
                if state["inspection_kind"] == "room-life":
                    info.add_text("Inspection", "Room-life schedule inspection: actual model tick and actual renderer; "
                                  "the model's natural approach event is suppressed for this single snapshot. "
                                  "room_life_preview.gif retains all production behavior.")
                else:
                    info.add_text("Inspection", "Manually selected detail component for the 730-day typography; "
                                  "the actual October 7 home-frame tap directly replays the anniversary event.")
            large.save(args.output_dir / filename, pnginfo=info, optimize=True)
            snapshot_states[filename] = state
            return large, state

        snapshot("home", "home_preview.png", 600)
        snapshot("world", "world_preview.png", 600)
        snapshot("world-connected", "world_connected_preview.png", 600)
        snapshot("anniversary", "anniversary_preview.png", 3600)
        snapshot("home", "cats_tap_preview.png", 900, "orange")
        snapshot("home", "cats_rapid_tap_preview.png", 600, "rapid")
        sheet = Image.new("RGB", (1600, 1440))
        for index, (mode, filename, expected_popup) in enumerate(POPUPS):
            image, state = snapshot(mode, filename, 600)
            assert state["popup"] == expected_popup, f"Real UI hit target failed: {mode}"
            sheet.paste(image, ((index % 2) * 800, (index // 2) * 480))
        sheet.save(args.output_dir / "popups_preview.png", pnginfo=metadata, optimize=True)
        variants = (
            ("home-normal", "home_normal_preview.png", 600, {"date":"2026-10-08", "elapsed_days":731}),
            ("home-connected", "home_connected_preview.png", 600, {"mac_connected":1}),
            ("home-anniversary", "home_anniversary_preview.png", 11000, {"elapsed_days":730, "phase":0}),
            ("anniversary-day-popup", "anniversary_730_days_preview.png", 8000, {"elapsed_days":730, "phase":0, "popup":1, "inspection_only":1}),
            ("anniversary-replay", "anniversary_replay_preview.png", 11000, {"elapsed_days":730, "phase":2, "popup":0, "inspection_only":0}),
            ("todo-complete", "todo_complete_preview.png", 600, {"todo_count":5, "todo_checked":31}),
            ("todo-progress", "todo_progress_preview.png", 600, {"todo_count":5, "todo_checked":11}),
            ("focus-home", "focus_home_preview.png", 61000, {"focus_running":1, "popup":0}),
            ("focus-running", "focus_running_preview.png", 61000, {"focus_running":1, "popup":3}),
            ("focus-paused", "focus_paused_preview.png", 61000, {"focus_running":0, "popup":3, "focus_remaining_ms":1439000}),
            ("focus-completed", "focus_completed_preview.png", 600, {"focus_running":0, "focus_remaining_ms":0, "notice_ms":6000}),
            ("focus-completed-popup", "focus_completed_popup_preview.png", 600, {"focus_remaining_ms":0, "popup":3}),
            ("mac-connected", "mac_connected_preview.png", 600, {"mac_connected":1, "mac_charging":1}),
            ("mac-battery", "mac_battery_preview.png", 600, {"mac_connected":1, "mac_charging":0}),
            ("settings-min", "settings_min_preview.png", 600, {"brightness":20}),
            ("settings-max", "settings_max_preview.png", 600, {"brightness":100}),
            ("focus-pressed", "focus_button_pressed_preview.png", 600, {"popup":3, "pointer_active":1, "focus_running":0}),
            ("todo-pressed", "todo_button_pressed_preview.png", 600, {"popup":2, "pointer_active":1, "todo_count":3}),
            ("anniversary-pressed", "anniversary_button_pressed_preview.png", 600, {"popup":1, "pointer_active":1, "phase":0}),
            ("close-pressed", "close_button_pressed_preview.png", 600, {"popup":4, "pointer_active":1}),
            ("components", "pixel_components_preview.png", 600, {"popup":0, "pointer_active":0}),
        )
        state_images = {}
        for mode, filename, elapsed, expected in variants:
            image, state = snapshot(mode, filename, elapsed)
            assert all(state[key] == value for key, value in expected.items()), (mode,state,expected)
            state_images[mode] = image
        states = Image.new("RGB", (1600,1440))
        for index, mode in enumerate(("home-connected", "todo-progress", "focus-home", "focus-paused", "mac-connected", "focus-completed")):
            states.paste(state_images[mode], ((index % 2)*800,(index // 2)*480))
        states.save(args.output_dir / "states_preview.png", pnginfo=metadata, optimize=True)
        periods = Image.new("RGB", (2400,480))
        for index, period in enumerate(("day", "dusk", "night")):
            image, state = snapshot(f"room-{period}", f"room_{period}_preview.png", 12000)
            assert state["period"] == index and state["clock_synced"] == 1, (period, state)
            assert state["inspection_only"] == 0
            periods.paste(image, (index*800,0))
        periods.save(args.output_dir / "room_periods_preview.png", pnginfo=metadata, optimize=True)
        room_inspections = Image.new("RGB", (1600,1920))
        for index, (activity, elapsed, expected) in enumerate(ROOM_INSPECTIONS):
            image, state = snapshot("room-life-inspect", f"room_{activity}_inspection.png", elapsed)
            assert state["inspection_only"] == 1 and state["inspection_kind"] == "room-life"
            assert state["approach_active"] == 0
            assert all(state[kind]["activity"] == value for kind,value in expected.items()), (activity,state)
            room_inspections.paste(image, ((index%2)*800,(index//2)*480))
        inspection_metadata = png_info(args.raw)
        inspection_metadata.add_text("Inspection", "Eight room-life schedule inspections; each frame suppresses the natural "
                                     "model approach event for comparison. Row-major: sofa, box, yarn, window, mac, sniff, chase, cuddle.")
        room_inspections.save(args.output_dir / "room_life_inspections.png", pnginfo=inspection_metadata, optimize=True)
        preview_manifest = {
            "description":"Actual firmware software previews. See PNG metadata for inspection setup.",
            "room_activity_names":ROOM_ACTIVITY_NAMES,
            "snapshots":snapshot_states,
            "animations":{},
        }

        def write_manifest() -> None:
            (args.output_dir / "preview_states.json").write_text(
                json.dumps(preview_manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

        write_manifest()
        print("PNG snapshots: textless world, all 6 popups plus real Mac, todo, focus, anniversary and brightness states")
        print("Room snapshots: real synced day, dusk and night; eight explicitly labeled schedule inspections")
        print("Every source frame validated: exact 2x pixel grid and at most 32 shared colors")
        if args.skip_gifs:
            return
        anniversary_frames, phases = [], []
        for elapsed in range(0, 11041, FRAME_MS):
            frame, state = render("anniversary", elapsed)
            anniversary_frames.append(frame)
            phases.append(state["phase"])
        assert 1 in phases and 2 in phases and phases[-1] == 0, "Incomplete anniversary timeline"
        # Independently sample the exact model boundary: approach completes
        # at 2200 ms from initial positions and its hearts stay for 5000 ms.
        _, arrived = render("anniversary", 2200)
        _, last_heart = render("anniversary", 7199)
        _, ended = render("anniversary", 7200)
        assert arrived["phase"] == 2 and arrived["heart_ms"] == 5000
        assert last_heart["phase"] == 2 and ended["phase"] == 0
        _, last_still=render("anniversary",9699)
        _, restored=render("anniversary",9700)
        assert ended["couple_finale_ms"]==2500 and last_still["couple_finale_ms"]==1
        assert restored["couple_finale_ms"]==0
        save_exact_gif(anniversary_frames, args.output_dir / "anniversary_preview.gif")
        print("Anniversary: original 2200ms approach + 5000ms hearts; appended 2500ms static couple/cats finale")
        del anniversary_frames
        cat_frames, observed = [], set()
        orange_tap = black_tap = rapid_bubble = False
        sleep_saved = False
        for elapsed in range(0, args.cats_duration_ms + 1, FRAME_MS):
            frame, state = render("home", elapsed, "live")
            cat_frames.append(frame)
            observed.update((state["orange"], state["black"]))
            orange_tap |= state["orange"] == 4
            black_tap |= state["black"] == 4
            rapid_bubble |= state["bubble_ms"] > 0
            if not sleep_saved and 3 in (state["orange"], state["black"]):
                frame.resize((800, 480), Image.Resampling.NEAREST).save(
                    args.output_dir / "cats_sleep_preview.png", pnginfo=metadata, optimize=True)
                sleep_saved = True
        required_states = {0, 1, 2, 3, 4}
        if observed != required_states:
            raise RuntimeError(f"Cat timeline lacks states {sorted(required_states - observed)}; extend --cats-duration-ms")
        assert orange_tap and black_tap and rapid_bubble, "Incomplete real touch interaction timeline"
        save_exact_gif(cat_frames, args.output_dir / "cats_live_preview.gif")
        print("Cat model coverage: idle, blink, walk, sleep, orange jump, black paw and rapid-tap bubble")
        del cat_frames
        if not args.skip_room_gif:
            room_frames, activities, natural_approach_samples = [], set(), 0
            for elapsed in range(0, ROOM_LOOP_MS, ROOM_FRAME_MS):
                frame, state = render("room-life", elapsed)
                assert state["inspection_only"] == 0
                room_frames.append(frame)
                activities.update((state["rendered_orange"]["activity"], state["rendered_black"]["activity"]))
                natural_approach_samples += state["approach_active"] != 0
            assert activities == set(range(9)), ("Incomplete production room-life loop", activities)
            assert natural_approach_samples > 0, "Production model natural approaches were not exercised"
            save_exact_gif(room_frames, args.output_dir / "room_life_preview.gif", ROOM_FRAME_MS,
                           "Actual CatDesk firmware production room-life loop; 300 seconds sampled every 400ms; "
                           "real natural approaches retained, no inspection overrides")
            preview_manifest["animations"]["room_life_preview.gif"] = {
                "scenario":"room-life", "inspection_only":False, "duration_ms":ROOM_LOOP_MS,
                "sample_ms":ROOM_FRAME_MS, "sample_count":len(room_frames),
                "observed_activities":[ROOM_ACTIVITY_NAMES[activity] for activity in sorted(activities)],
                "natural_approach_samples":natural_approach_samples,
            }
            write_manifest()
            print("Production room-life coverage: five environmental activities and three paired events, with natural approach interruptions")


if __name__ == "__main__":
    main()
