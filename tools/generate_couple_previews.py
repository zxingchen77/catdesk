#!/usr/bin/env python3
"""Export and verify actual C-rendered couple assets; software previews only."""
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, PngImagePlugin
from generate_previews import save_exact_gif

ROOT=Path(__file__).resolve().parents[1]
COMPONENT=ROOT/"firmware/components/catdesk_ui"
OUTPUT=ROOT/"assets/couple-walk-2026-10-06"
SOURCES=[ROOT/"tools/render_preview.c"]+[COMPONENT/name for name in (
    "catdesk_model.c","catdesk_render.c","catdesk_couple.c","catdesk_room.c",
    "catdesk_font.c","catdesk_cats.c","pixel_ui.c")]


def main():
    OUTPUT.mkdir(parents=True,exist_ok=True)
    manifest={"description":"Actual production C renderer; software previews, not LCD photographs.",
              "logical_size":[400,240],"output_size":[800,480],"scale":"2x nearest-neighbor",
              "sprite_size":[16,24],"poses":["front","side","sit","walk_a","walk_b"],"colors_per_person":9,
              "packed_sprite_bytes":1920,"new_palette_colors":0,"couple_photo":False,
              "snapshots":{},"animations":{},
              "source_sha256":{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in SOURCES+[COMPONENT/"catdesk_people.inc"]}}
    with tempfile.TemporaryDirectory(prefix="catdesk-couple-") as directory:
        directory=Path(directory);binary=directory/"render";ppm=directory/"frame.ppm"
        subprocess.run(["clang","-std=c11","-O2","-Wall","-Wextra","-Werror","-pedantic",
                        "-I",str(COMPONENT/"include"),*map(str,SOURCES),"-o",str(binary)],check=True)

        def render(mode,elapsed):
            result=subprocess.run([str(binary),str(ppm),mode,str(elapsed)],check=True,capture_output=True,text=True)
            state=json.loads(result.stderr.strip().splitlines()[-1])
            with Image.open(ppm) as original:
                large=original.convert("RGB")
                assert large.size==(800,480)
                small=large.resize((400,240),Image.Resampling.NEAREST)
                assert ImageChops.difference(large,small.resize((800,480),Image.Resampling.NEAREST)).getbbox() is None
                assert small.getcolors(32) is not None
            return small,state

        snapshots=(
            ("couple-sprites",0,"couple_sprites.png"),
            ("couple-walk",0,"couple_walk_entry.png"),
            ("couple-walk",6000,"couple_walk.png"),
            ("room-day",0,"couple_room_day.png"),
            ("room-dusk",0,"couple_room_dusk.png"),
            ("room-night",0,"couple_room_night.png"),
            ("couple-sofa",4000,"couple_sofa.png"),
            ("anniversary",8500,"couple_anniversary_finale.png"),
            ("anniversary",11000,"couple_anniversary_restored.png"))
        for mode,elapsed,name in snapshots:
            small,state=render(mode,elapsed)
            info=PngImagePlugin.PngInfo()
            info.add_text("Description",manifest["description"])
            info.add_text("State",json.dumps(state,ensure_ascii=False))
            small.resize((800,480),Image.Resampling.NEAREST).save(OUTPUT/name,pnginfo=info,optimize=True)
            manifest["snapshots"][name]=state
        for mode,end,step,name in (
            ("couple-walk",35000,100,"couple_walk.gif"),
            ("anniversary",11000,100,"couple_anniversary.gif"),
            ("couple-sofa",16000,200,"couple_sofa.gif")):
            frames=[];states=[]
            for elapsed in range(0,end+1,step):
                small,state=render(mode,elapsed)
                frames.append(small);states.append(state)
            if mode=="anniversary":
                assert states[22]["phase"]==2 and states[22]["heart_ms"]==5000
                assert states[72]["couple_finale_ms"]==2500
                assert states[96]["couple_finale_ms"]==100 and states[97]["couple_finale_ms"]==0
                assert not states[-1]["couple_finale_ms"]
            elif mode=="couple-walk":
                assert all(person["visible"] for person in states[0]["people"])
                assert all(not state["inspection_only"] for state in states)
                assert all(state["popup"]==0 for state in states)
                for person in range(2):
                    positions={(state["people"][person]["x"],state["people"][person]["y"]) for state in states}
                    poses={state["people"][person]["pose"] for state in states}
                    assert len(positions)>8 and {3,4}.issubset(poses)
                    assert all(150<=x<=240 and 128<=y<=146 for x,y in positions)
            else:
                assert 8000<=states[0]["couple_sofa_ms"]<=15000
                assert states[-1]["couple_sofa_ms"]==0
                assert all(state["inspection_only"] for state in states)
            large=[frame.resize((800,480),Image.Resampling.NEAREST) for frame in frames]
            save_exact_gif(large,OUTPUT/name,step,manifest["description"])
            expanded=[]
            with Image.open(OUTPUT/name) as gif:
                for index in range(gif.n_frames):
                    gif.seek(index)
                    expanded.extend([gif.convert("RGB").copy()]*(gif.info["duration"]//step))
            assert len(expanded)==len(large)
            assert all(ImageChops.difference(a,b).getbbox() is None for a,b in zip(large,expanded))
            manifest["animations"][name]={"sample_ms":step,"states":states,"every_pixel_verified":True}
        manifest["sofa_fixture"]="Software-only first successful PRNG opportunity after skipping the 20min wait; production timers render appearance and expiry. No hardware trigger or debug UI."
    (OUTPUT/"couple_preview_states.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n")
    print("Couple previews: ten sprites, visible room entry, 35s random walking, three room periods, retained 8-15s sofa, original anniversary + 2.5s finale, no couple photo; exact 2x and every decoded GIF pixel PASS.")


if __name__=="__main__":
    main()
