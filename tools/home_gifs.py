#!/usr/bin/env python3
"""The four HOME pages as animated GIFs for the manual and the site (bs2/docs/img/screen-*-home.gif): a few seconds on
HOME, then a knob turned (or a button tapped) and the camera's dive in, held, and back out to HOME, where the loop
starts again. Each from the host simulator, a shot every FRAME_MS. From the repository's root, after
tools/build.py bs2 --host:
    python3 bs2/tools/home_gifs.py [OUT_DIR]        (default bs2/docs/img; needs Pillow)
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOST = os.path.join(ROOT, "bs2/build/host/bass-something-2_host")
FRAME_MS = 50

CHORD1 = [(0, 127), (7, 127), (11, 120), (14, 120), (19, 127), (23, 115)]   # F3 C4 E4 G4 C5 E5
CHORD2 = [(2, 120), (9, 124), (12, 118), (16, 122), (21, 120)]               # G3 D4 F4 A4 D5
CHORD3 = [(4, 122), (11, 120), (14, 124), (18, 118), (23, 122)]              # A3 E4 G4 B4 E5


def chord(keys, at, held):
    """a chord struck at `at` ms, let go `held` ms later"""
    return [(at, [f"key {k} down {v}" for k, v in keys]), (at + held, [f"key {k} up" for k, _ in keys])]


def turns(enc, at, n, step, every=80):
    """n detents of `step` on enc, from `at` ms, `every` ms apart"""
    return [(at + i * every, [f"spin {enc} {step}"]) for i in range(n)]


# each: (file, the HOME taps from the bass's to its mode, what's done before the first frame (the bass's knob turned
# and back: the patch shown edited, P01. as at the loop's end), the length in ms, the events: (ms, [commands]))
GIFS = [
    ("screen-bass-home.gif", 0, ["spin K4 -1", "spin K4 1"], 7000,
     turns("K4", 2500, 10, -3) + turns("K4", 3500, 10, 3)),             # FREQUENCY down and back
    ("screen-drum-home.gif", 1, [], 9000,
     [(2500, ["tap SEQ"]), (4300, ["tap HOME"])]),                      # SEQ's patterns, and HOME again
    ("screen-piano-home.gif", 2, [], 8000,
     chord(CHORD1, 300, 900) + chord(CHORD2, 1400, 900) + chord(CHORD3, 2500, 1600)
     + [(3300, ["spin K2 1"]), (4200, ["spin K2 -1"])]
     + chord(CHORD2, 5900, 700) + chord(CHORD1, 6700, 900)),            # chords, ORGAN MODE on and off, chords
    ("screen-echo-home.gif", 3, [], 7000,
     turns("K1", 2500, 10, 3) + turns("K1", 3500, 10, -3)),             # REPEAT RATE up and back
]


def make(name, taps, prep, length, events, out_dir):
    with tempfile.TemporaryDirectory() as tmp:
        lines = ["bank_init", "wait 900"]
        for _ in range(taps):
            lines += ["tap HOME", "wait 3000"]
        lines += prep + ["wait 3000"]
        due = sorted(events, key=lambda e: e[0])
        frames = length // FRAME_MS
        for i in range(frames):
            t = i * FRAME_MS
            while due and due[0][0] <= t:
                lines += due.pop(0)[1]
            lines += [f"shot f{i:04d}.png", f"wait {FRAME_MS}"]
        script = os.path.join(tmp, "gif.txt")
        with open(script, "w") as f:
            f.write("\n".join(lines) + "\n")
        subprocess.run([HOST, script, tmp], check=True, stdout=subprocess.DEVNULL)
        ims = [Image.open(os.path.join(tmp, f"f{i:04d}.png")).convert("RGB") for i in range(frames)]
        pal = [im.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for im in ims]
        path = os.path.join(out_dir, name)
        pal[0].save(path, save_all=True, append_images=pal[1:], duration=FRAME_MS, loop=0, optimize=True,
                    disposal=1)
        print(f"{path}: {frames} frames, {os.path.getsize(path) // 1024} KB")


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "bs2/docs/img")
    for g in GIFS:
        make(*g, out_dir)


if __name__ == "__main__":
    main()
