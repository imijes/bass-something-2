#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Measure a Drumtraks faceplate photo: where its parts are, for the drum HOME page (app.c dhome_draw).

    python3 bs2/tools/dt_face.py PHOTO [--out DIR]

Prints every part in photo pixels, to a tenth of a pixel: the white frames and their corners' radii, the bracket
lines, the black keys (recess, cap, bright bevel), the grey keys (rim, face), the LEDs, the displays (window, digits)
the knobs (cap's circle, silver rim's angle and width) and the print's words. With --out, also writes overlay.png (the measures drawn on
the photo at 2x, to check them by eye) and face.json. The photo itself is a reference and stays out of the repo.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


# the frames, from the lines measured (neighbours share a wall)
FRAMES = {"cassette": (165.6, 56.6, 317.5, 205.3), "tempo": (165.6, 205.3, 317.5, 349.7),
          "song": (329.5, 56.8, 643.0, 349.7), "display": (643.0, 56.7, 851.8, 349.8),
          "pattern": (851.8, 56.3, 1166.2, 350.2), "volumes": (1179.7, 55.5, 1347.5, 350.4),
          "per inst": (150.7, 408.8, 363.7, 642.4), "ch1": (414.8, 408.5, 486.9, 642.1),
          "ch2": (486.9, 408.6, 629.8, 641.6), "ch3": (629.8, 408.5, 772.5, 641.6),
          "ch4": (772.5, 408.5, 915.8, 641.8), "ch5": (915.8, 408.4, 1058.8, 641.7),
          "ch6": (1058.8, 408.5, 1347.5, 642.5)}
KNOBS = ((1274, 121), (1274, 261), (181, 452), (310, 452))    # about where the knobs are (knob() finds them exactly)


def components(mask):
    """8-connected blobs of a boolean array: a list of (y0, x0, y1, x1, n), y1 and x1 exclusive, with their pixels."""
    h, w = mask.shape
    seen = np.zeros(mask.shape, bool)
    out = []
    for sy, sx in zip(*np.nonzero(mask)):
        if seen[sy, sx]:
            continue
        seen[sy, sx] = True
        stack, ys, xs = [(sy, sx)], [], []
        while stack:
            y, x = stack.pop()
            ys.append(y)
            xs.append(x)
            for ny in range(max(y - 1, 0), min(y + 2, h)):
                for nx in range(max(x - 1, 0), min(x + 2, w)):
                    if mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        out.append((min(ys), min(xs), max(ys) + 1, max(xs) + 1, np.array(ys), np.array(xs)))
    return out


def edge_at(profile, level):
    """Where a profile first crosses level, to a fraction of a pixel."""
    for i in range(len(profile) - 1):
        a, b = profile[i], profile[i + 1]
        if (a - level) * (b - level) <= 0 and a != b:
            return i + (level - a) / (b - a)
    return None


def line_centre(profile):
    """A bright line's centre across its profile: the centroid above the background."""
    p = np.clip(profile - np.median(profile), 0, None)
    return float((p * np.arange(len(p))).sum() / p.sum()) if p.sum() else None


def frames(grey):
    """The white frames: the long straight runs of bright line, each side found by its row or column's profile."""
    bright = grey > 150
    hs, vs = [], []
    rows = bright[:, 120:1380]
    for y in range(grey.shape[0]):                        # a row's runs of 40+ bright pixels: a frame's top or bottom
        run = 0
        for x in range(rows.shape[1] + 1):
            if x < rows.shape[1] and rows[y, x]:
                run += 1
                continue
            if run >= 40:
                hs.append((y, x - run + 120, x + 120))
            run = 0
    cols = bright[20:655, :]
    for x in range(120, 1380):
        run = 0
        for y in range(cols.shape[0] + 1):
            if y < cols.shape[0] and cols[y, x]:
                run += 1
                continue
            if run >= 40:
                vs.append((x, y - run + 20, y + 20))
            run = 0
    return merge(hs), merge(vs)


def merge(runs):
    """Runs a line's width apart along the same stretch: one line (position, from, to, thickness)."""
    out = []
    for p, a, b in sorted(runs):
        for o in out:
            if p - o[0][-1] <= 1 and min(b, o[2]) - max(a, o[1]) > 20:
                o[0].append(p)
                o[1], o[2] = min(a, o[1]), max(b, o[2])
                break
        else:
            out.append([[p], a, b])
    return [(sum(o[0]) / len(o[0]), o[1], o[2], len(o[0])) for o in out]


def refine(grey, lines, horizontal):
    """Each line's centre, a tenth of a pixel, along its middle half."""
    out = []
    for p, a, b, t in lines:
        q, m0, m1 = int(round(p)), a + (b - a) // 4, b - (b - a) // 4
        lo, hi = q - 6, q + 7
        prof = grey[lo:hi, m0:m1].mean(1) if horizontal else grey[m0:m1, lo:hi].mean(0)
        c = line_centre(prof)
        half = prof.max() / 2 + np.median(prof) / 2
        out.append({"at": round(lo + c, 1), "from": a, "to": b, "w": round(float((prof > half).sum()), 1)})
    return out


def corner_radius(grey, x, y, sx, sy, r_max=40):
    """A corner's radius: the circle (centre inside the corner, diagonal from the frame's sides) that best fits the
    bright pixels near it. x, y: where the frame's sides would meet; sx, sy: +1/-1, towards the frame's inside."""
    best, best_r = -1.0, None
    h, w = grey.shape
    for r10 in range(40, r_max * 10):
        r = r10 / 10
        cx, cy = x + sx * r, y + sy * r
        s = 0.0
        n = 0
        for a in np.linspace(0, np.pi / 2, 24):
            px = cx - sx * r * np.sin(a)
            py = cy - sy * r * np.cos(a)
            ix, iy = int(round(px)), int(round(py))
            if 0 <= ix < w and 0 <= iy < h:
                s += grey[iy, ix]
                n += 1
        s = s / max(n, 1)
        if s > best:
            best, best_r = s, r
    return best_r


def black_keys(grey):
    """The black keys: square blobs of near black; inside, the cap (lighter) and the bevel at the right (bright)."""
    out = []
    for y0, x0, y1, x1, ys, xs in components(grey < 13):      # (the plate darkens to the left: a low threshold)
        w, h = int(x1 - x0), int(y1 - y0)
        y0, x0, y1, x1 = int(y0), int(x0), int(y1), int(x1)
        if not (36 <= w <= 72 and 36 <= h <= 72 and 0.85 < w / h < 1.18) or len(ys) < 0.2 * w * h:
            continue
        if any(x0 - 8 < kx < x1 + 8 and y0 - 8 < ky < y1 + 8 for kx, ky in KNOBS):   # (a knob's shadow)
            continue
        cy0, cy1 = y0 + h // 4, y1 - h // 4
        cx0, cx1 = x0 + w // 4, x1 - w // 4
        row = grey[cy0:cy1, x0 - 3:x1 + 4].mean(0)
        col = grey[y0 - 3:y1 + 4, cx0:cx1].mean(1)
        cap = float(np.median(grey[cy0:cy1, cx0:cx1]))
        rec = float(np.percentile(grey[y0:y1, x0:x1], 8))
        mid = (cap + rec) / 2
        c = {"box": [x0, y0, x1, y1], "w": w, "h": h, "cap_grey": round(cap, 1), "recess_grey": round(rec, 1)}
        # from the cap's middle outward, each way, to where it falls to the recess
        mx = (x0 + x1) // 2 - (x0 - 3)
        my = (y0 + y1) // 2 - (y0 - 3)
        left = edge_at(row[mx::-1], mid)
        top = edge_at(col[my::-1], mid)
        bot = edge_at(col[my:], mid)
        c["cap"] = [None if left is None else round(x0 - 3 + mx - left, 1),
                    None if top is None else round(y0 - 3 + my - top, 1), None,
                    None if bot is None else round(y0 - 3 + my + bot, 1)]
        c["row"], c["col"] = [round(float(v)) for v in row], [round(float(v)) for v in col]
        bev = np.nonzero(row[mx:] > max(cap + 40, 90))[0]
        if len(bev):
            c["bevel"] = [int(x0 - 3 + mx + bev[0]), int(x0 - 3 + mx + bev[-1] + 1)]
            c["bevel_grey"] = round(float(row[mx:][bev].max()), 1)
            c["cap"][2] = c["bevel"][0]
        out.append(c)
    return sorted(out, key=lambda c: (c["box"][1] // 80, c["box"][0]))


def upper_key(grey, cx, cy):
    """A low-contrast black key near cx, cy (the upper ones, no bright bevel): its recess's box, the darkest square
    around it against the plate, and the cap's inside it."""
    win = grey[cy - 34:cy + 35, cx - 34:cx + 35]
    plate = float(np.median(np.r_[win[:6].ravel(), win[-6:].ravel()]))
    row, col = win[24:45].mean(0), win[:, 24:45].mean(1)
    level = plate - 6
    xs = [i for i in range(69) if win[20:49, i].min() < level]
    ys = [i for i in range(69) if win[i, 20:49].min() < level]
    return {"box": [cx - 34 + min(xs), cy - 34 + min(ys), cx - 34 + max(xs) + 1, cy - 34 + max(ys) + 1],
            "plate": round(plate), "cap_grey": round(float(np.median(win[26:43, 26:43]))),
            "row": [round(float(v)) for v in row], "col": [round(float(v)) for v in col]}


def bright_spans(grey, box, level=130):
    """A thin print line's pixels in box: bright rows' and columns' spans, longest first."""
    x0, y0, x1, y1 = box
    m = grey[y0:y1, x0:x1] > level
    rows = sorted(((int(m[i].sum()), y0 + i, x0 + int(np.nonzero(m[i])[0].min()), x0 + int(np.nonzero(m[i])[0].max()) + 1)
                   for i in range(m.shape[0]) if m[i].any()), reverse=True)[:6]
    cols = sorted(((int(m[:, i].sum()), x0 + i, y0 + int(np.nonzero(m[:, i])[0].min()), y0 + int(np.nonzero(m[:, i])[0].max()) + 1)
                   for i in range(m.shape[1]) if m[:, i].any()), reverse=True)[:4]
    return {"rows (n, y, x0, x1)": rows, "cols (n, x, y0, y1)": cols}


def labels(grey, gap=5, level=115):
    """The print: each word's extent, its rows' middle. Everything else bright is blanked first (frames, keys, keypad,
    displays, knobs and their numbers, the logo, the wood); letters closer than gap are one word."""
    m = grey > level
    for x0, y0, x1, y1 in FRAMES.values():
        for a, b, c, d in ((x0, y0, x1, y0), (x0, y1, x1, y1), (x0, y0, x0, y1), (x1, y0, x1, y1)):
            m[int(b) - 7:int(d) + 8, int(a) - 7:int(c) + 8] = False
    m[:, :140] = m[:, 1360:] = False
    m[:45] = m[655:] = False
    m[360:402, 1060:] = False                              # the logo
    m[60:112, 655:845] = m[125:340, 665:830] = False       # the displays, the keypad
    m[186:292, 424:462] = m[136:160, 420:440] = False      # the song tree and its LEDs
    m[136:256, 942:968] = m[160:190, 942:994] = False      # the pattern tree and its LEDs
    m[491:506, 190:330] = m[609:627, 800:1030] = False     # the brackets, with the words between them: given below
    m[525:592, 160:1345] = False                           # the keys
    for cx, cy in KNOBS:
        m[cy - 46:cy + (40 if cy < 300 else 26), cx - 48:cx + 48] = False   # (the lower two have no numbers)
    ys, xs = np.nonzero(m)
    d = m.copy()
    for y, x in zip(ys, xs):                               # letters to words: each smeared across the gap
        d[y, x:min(x + gap + 1, m.shape[1])] = True
    out = []
    for y0, x0, y1, x1, py, px in components(d):
        h, w = int(y1 - y0), int(x1 - x0) - gap
        if 5 <= h <= 18 and w >= 6:
            out.append((int(x0), int(x1) - gap, int(y0), int(y1)))
    # read by eye off the overlay: the words inside the brackets (PER, INSTRUMENT, CYMBAL, HI, HAT)
    out += [(208, 230, 495, 503), (236, 311, 495, 503), (822, 870, 617, 625), (968, 982, 616, 624), (986, 1009, 616, 624)]
    return sorted(out, key=lambda c: (c[2] // 8, c[0]))


def grey_keys(grey):
    """The keypad's grey keys: rim (the whole key), face (the darker square inside)."""
    out = []
    for y0, x0, y1, x1, ys, xs in components(grey > 105):
        w, h = x1 - x0, y1 - y0
        if not (28 <= w <= 45 and 28 <= h <= 45) or not (660 < x0 < 840):
            continue
        cy, cx = (y0 + y1) // 2, (x0 + x1) // 2
        row = grey[cy - 8:cy + 9, x0 - 2:x1 + 3].mean(0)
        col = grey[y0 - 2:y1 + 3, cx - 8:cx + 9].mean(1)
        y0, x0, y1, x1 = int(y0), int(x0), int(y1), int(x1)
        out.append({"box": [x0, y0, x1, y1], "w": w, "h": h,
                    "face_grey": round(float(np.median(grey[y0 + 8:y1 - 8, x0 + 8:x1 - 8])), 1),
                    "row": [round(float(v)) for v in row], "col": [round(float(v)) for v in col]})
    return sorted(out, key=lambda c: (c["box"][1] // 30, c["box"][0]))


def leds(rgb):
    """The LEDs and the lit digits: pixels red over green and blue."""
    r, g, b = (rgb[..., i].astype(float) for i in range(3))
    red = (r - np.maximum(g, b) > 22) & (r > 40)
    out = []
    for y0, x0, y1, x1, ys, xs in components(red):
        if len(ys) >= 3:
            w = r[ys, xs]
            out.append({"x": round(float((xs * w).sum() / w.sum()), 1), "y": round(float((ys * w).sum() / w.sum()), 1),
                        "box": [int(x0), int(y0), int(x1), int(y1)], "n": len(ys), "peak": int(w.max())})
    return sorted(out, key=lambda c: (c["y"], c["x"]))


def display(rgb, grey, box):
    """A display's window: the darker, redder rectangle inside box, its digits' extent, a digit's width and height."""
    x0, y0, x1, y1 = box
    r, g = rgb[..., 0].astype(float), rgb[..., 1].astype(float)
    red = (r - g)[y0:y1, x0:x1]
    win = red > 5
    ys = np.nonzero(win.mean(1) > 0.35)[0]
    xs = np.nonzero(win.mean(0) > 0.35)[0]
    seg = red > max(30, red.max() * 0.45)
    sy = np.nonzero(seg.any(1))[0]
    sx = np.nonzero(seg.any(0))[0]
    d = {"window": [int(x0 + xs[0]), int(y0 + ys[0]), int(x0 + xs[-1] + 1), int(y0 + ys[-1] + 1)],
         "digits": [int(x0 + sx[0]), int(y0 + sy[0]), int(x0 + sx[-1] + 1), int(y0 + sy[-1] + 1)]}
    gaps = np.nonzero(~seg.any(0)[sx[0]:sx[-1]])[0]
    d["gap_cols"] = [int(gaps.min() + x0 + sx[0]), int(gaps.max() + x0 + sx[0] + 1)] if len(gaps) else None
    left = grey[y0:y1, x0:x1].max(0)
    d["left_strip_grey"] = round(float(left[xs[0]:xs[0] + 3].max()), 1) if len(xs) else None
    return d


def knob(grey, box):
    """A knob: the circle whose edge is brightest (a coarse Hough on the rim), then the rim's brightness by angle."""
    x0, y0, x1, y1 = box
    g = grey[y0:y1, x0:x1].astype(float)
    best, found = -1.0, None
    h, w = g.shape
    angles = np.linspace(0, 2 * np.pi, 96, endpoint=False)
    for r10 in range(180, 330, 5):
        r = r10 / 10
        px, py = np.cos(angles) * r, np.sin(angles) * r
        for cy in np.arange(r + 1, h - r - 1, 0.5):
            for cx in np.arange(r + 1, w - r - 1, 0.5):
                ix, iy = np.rint(cx + px).astype(int), np.rint(cy + py).astype(int)
                # the rim is brighter than just inside it: the cap's edge
                ix2, iy2 = np.rint(cx + px * 0.82).astype(int), np.rint(cy + py * 0.82).astype(int)
                s = (g[iy, ix] - g[iy2, ix2]).mean()
                if s > best:
                    best, found = s, (cx, cy, r)
    cx, cy, r = found
    prof = []
    for a in range(0, 360, 30):                            # the rim by angle: 0 at the right, clockwise on the photo
        t = np.radians(a)
        radial = [g[int(round(cy + np.sin(t) * q)), int(round(cx + np.cos(t) * q))]
                  if 0 <= int(round(cy + np.sin(t) * q)) < h and 0 <= int(round(cx + np.cos(t) * q)) < w else 0
                  for q in np.arange(r - 8, r + 10, 1.0)]
        peak = int(np.argmax(radial))
        half = (max(radial) + np.median(radial)) / 2
        prof.append({"deg": a, "peak_grey": round(float(max(radial))), "at_r": round(float(r - 8 + peak), 1),
                     "width": int((np.array(radial) > half).sum())})
    inner = g[int(cy - r / 2):int(cy + r / 2), int(cx - r / 2):int(cx + r / 2)]
    return {"cx": round(float(x0 + cx), 1), "cy": round(float(y0 + cy), 1), "r": round(float(r), 1),
            "cap_grey": round(float(np.median(inner)), 1), "rim": prof}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("photo")
    ap.add_argument("--out", help="a directory for overlay.png and face.json")
    args = ap.parse_args()
    img = Image.open(args.photo).convert("RGB")
    rgb = np.asarray(img)
    grey = np.asarray(img.convert("L")).astype(float)
    print(f"photo {img.width} x {img.height}")

    hs, vs = frames(grey)
    hl, vl = refine(grey, hs, True), refine(grey, vs, False)
    print("\nhorizontal lines (y, x from..to, width):")
    for s in hl:
        print(f"  y {s['at']:7.1f}  x {s['from']:5d} .. {s['to']:5d}  w {s['w']}")
    print("vertical lines (x, y from..to, width):")
    for s in vl:
        print(f"  x {s['at']:7.1f}  y {s['from']:5d} .. {s['to']:5d}  w {s['w']}")

    print("\nframes' corners' radii (left top, right top, left bottom, right bottom):")
    for name, (x0, y0, x1, y1) in FRAMES.items():
        print(f"  {name:9s} ({x0}, {y0}, {x1}, {y1}):", [corner_radius(grey, x, y, sx, sy)
              for x, y, sx, sy in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1))])

    bk = black_keys(grey)
    print(f"\nblack keys ({len(bk)}):")
    for c in bk:
        print(f"  box {c['box']} {c['w']}x{c['h']} cap {c['cap']} bevel {c.get('bevel')} "
              f"grey cap {c['cap_grey']} recess {c['recess_grey']} bevel {c.get('bevel_grey')}")
    if bk:
        print("  a key's row, left to right:", bk[-1]["row"])
        print("  a key's col, top to bottom:", bk[-1]["col"])
    words = labels(grey)
    print(f"\nprint: {len(words)} words (x0, x1, y0, y1):")
    print(" ", words)
    print("  as app.c's DT_WORD (x0, x1, y):", ", ".join(f"{{{a}, {b}, {(c + d) // 2}}}" for a, b, c, d in words))
    print("\nupper black keys:")
    upper = {n: upper_key(grey, *c) for n, c in {"save": (205, 141), "load": (277, 141), "slower": (205, 285),
                                                 "faster": (277, 285), "song": (394, 113), "enter": (475, 285),
                                                 "pattern": (917, 113)}.items()}
    for n, c in upper.items():
        print(f"  {n}: box {c['box']} plate {c['plate']} cap {c['cap_grey']}")
    print("  save's row:", upper["save"]["row"])
    print("  save's col:", upper["save"]["col"])
    print("\nthin lines:")
    for n, b in {"song tree": (424, 186, 456, 292), "pattern tree": (946, 156, 978, 190),
                 "per inst L": (190, 486, 212, 503), "per inst R": (300, 486, 330, 503),
                 "cymbal L": (800, 606, 824, 628), "cymbal R": (868, 606, 890, 628),
                 "hi hat L": (944, 606, 968, 628), "hi hat R": (1008, 606, 1036, 628)}.items():
        print(f"  {n}: {bright_spans(grey, b)}")
    gk = grey_keys(grey)
    print(f"\ngrey keys ({len(gk)}):")
    for c in gk:
        print(f"  box {c['box']} {c['w']}x{c['h']} face {c['face_grey']}")
    if gk:
        print("  a key's row, left to right:", gk[4]["row"])
        print("  a key's col, top to bottom:", gk[4]["col"])

    led = leds(rgb)
    print(f"\nred blobs ({len(led)}):")
    for c in led:
        print(f"  x {c['x']:7.1f} y {c['y']:6.1f} box {c['box']} n {c['n']} peak {c['peak']}")

    disp = {"song": display(rgb, grey, (650, 60, 735, 110)), "pattern": display(rgb, grey, (760, 60, 845, 110))}
    print("\ndisplays:", json.dumps(disp))

    kn = {name: knob(grey, b) for name, b in {"metronome": (1240, 88, 1310, 155), "master": (1240, 228, 1310, 295),
                                              "volume": (154, 420, 222, 486), "tuning": (276, 420, 344, 486)}.items()}
    print("\nknobs:")
    for name, k in kn.items():
        print(f"  {name}: centre {k['cx']}, {k['cy']} r {k['r']} cap grey {k['cap_grey']}")
        print("    rim peak by angle (0 right, 90 down):", [(p['deg'], p['peak_grey'], p['width']) for p in k['rim']])

    if args.out:
        out = Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        (out / "face.json").write_text(json.dumps({"h": hl, "v": vl, "black": bk, "grey": gk, "red": led,
                                                   "display": disp, "knobs": kn}, indent=1, default=float))
        z = 2
        ov = img.resize((img.width * z, img.height * z), Image.LANCZOS)
        d = ImageDraw.Draw(ov)
        for s in hl:
            d.line([(s["from"] * z, s["at"] * z), (s["to"] * z, s["at"] * z)], fill=(0, 255, 255))
        for s in vl:
            d.line([(s["at"] * z, s["from"] * z), (s["at"] * z, s["to"] * z)], fill=(0, 255, 255))
        for c in bk + gk:
            d.rectangle([v * z for v in c["box"]], outline=(0, 255, 0))
        for c in bk:
            if None not in c["cap"]:
                d.rectangle([v * z for v in c["cap"]], outline=(255, 255, 0))
        for c in led:
            d.ellipse([(c["x"] - 3) * z, (c["y"] - 3) * z, (c["x"] + 3) * z, (c["y"] + 3) * z], outline=(255, 0, 255))
        for x0, x1, y0, y1 in words:
            d.rectangle([x0 * z, y0 * z, x1 * z, y1 * z], outline=(255, 64, 64))
        for k in kn.values():
            d.ellipse([(k["cx"] - k["r"]) * z, (k["cy"] - k["r"]) * z, (k["cx"] + k["r"]) * z, (k["cy"] + k["r"]) * z],
                      outline=(255, 128, 0))
        for w in disp.values():
            d.rectangle([v * z for v in w["window"]], outline=(0, 128, 255))
            d.rectangle([v * z for v in w["digits"]], outline=(255, 255, 255))
        ov.save(out / "overlay.png")
        print(f"\nwrote {out}/overlay.png, face.json")


if __name__ == "__main__":
    main()
