#!/usr/bin/env python3
"""The RND Synth Companion's splash picture: the RND Synth beside the FM-1, cut out of John Brown's photo of them (the
drum throne they sit on taken away: each device's outline the hull of its orange, the RND Synth's clear acrylic edge
with it) onto the panel's black, W px wide (a margin each side: the FM-1's screen hides a few columns at its edges), in
8 colours: a palette of RGB565 byte-swapped as the FM-1's panel takes it, and a pixel a nibble in an LZ4 block (the
format's spec: github.com/lz4/lz4 doc/lz4_Block_format.md), unpacked by app.c as the splash opens (about 4 KB; it was 16).

    python3 bs2/rnd/make_splash.py IMG_1989.HEIC   # writes bs2/rnd/splash.png (what it made) and bs2/rnd/splash.h

A .HEIC is read through macOS's sips; any image Pillow reads works too (4032 x 3024, as the photo is)."""
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw

COLOURS = 8
W = 208                                      # the picture's width on the screen (its height as the devices are)
VIEW = 1000.0                                # the boxes below are in the photo scaled to this width
BOXES = (((180, 222, 410, 445), True),       # the RND Synth (its acrylic edge too), then the FM-1
         ((408, 220, 838, 470), False))
# the RND Synth's lowest acrylic layers' bottom corners, rounded (in the 4032 px photo): clear and dark against the
# throne, so no colour finds them
RND_EDGE = ((760, 1680), (770, 1705), (790, 1728), (815, 1746), (1575, 1742), (1600, 1728), (1612, 1700))

here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1]
if src.lower().endswith(".heic"):
    tmp = os.path.join(tempfile.mkdtemp(), "photo.png")
    subprocess.run(["sips", "-s", "format", "png", src, "--out", tmp], check=True, capture_output=True)
    src = tmp
im = Image.open(src).convert("RGB")
k = im.width / VIEW
a = np.asarray(im).astype(int)
r, g, b = a[..., 0], a[..., 1], a[..., 2]
orange = (r > 110) & (r > g * 1.25) & (r > b * 1.7)
pale = (r + g + b > 330) & (abs(r - b) < 60)   # the acrylic's edge: light, without colour


def hull(pts):                               # the convex hull (Andrew's monotone chain)
    pts = sorted(set(map(tuple, pts)))

    def half(p):
        h = []
        for q in p:
            while len(h) > 1 and (h[-1][0] - h[-2][0]) * (q[1] - h[-2][1]) - (h[-1][1] - h[-2][1]) * (q[0] - h[-2][0]) <= 0:
                h.pop()
            h.append(q)
        return h
    return half(pts)[:-1] + half(pts[::-1])[:-1]


mask = Image.new("L", im.size, 0)
for n, ((x0, y0, x1, y1), acrylic) in enumerate(BOXES):
    X0, Y0, X1, Y1 = (int(v * k) for v in (x0, y0, x1, y1))
    m = orange[Y0:Y1, X0:X1] | (pale[Y0:Y1, X0:X1] if acrylic else False)
    m &= (m.sum(1) > m.shape[1] * 0.15)[:, None] & (m.sum(0) > m.shape[0] * 0.15)[None, :]   # (no stray specks)
    ys, xs = np.nonzero(m)
    step = max(1, len(xs) // 40000)
    pts = np.stack([xs[::step] + X0, ys[::step] + Y0], 1)
    if n == 0:
        pts = np.concatenate([pts, (np.array(RND_EDGE) * im.width / 4032).astype(int)])
    ImageDraw.Draw(mask).polygon(hull(pts), fill=255)
box = mask.getbbox()
out = Image.composite(im, Image.new("RGB", im.size, (0, 0, 0)), mask).crop(box)
size = (W, round(W * out.height / out.width))
out = out.resize(size, Image.LANCZOS)
keep = np.asarray(mask.crop(box).resize(size, Image.BILINEAR)) >= 128
# the devices in all the colours but one (flat: dither is what an LZ block can't find); the last, the ground: pure black,
# the screen's own, every pixel outside their outlines, so nothing shows round or between them
fg = Image.fromarray(np.asarray(out)[keep].reshape(1, -1, 3)).quantize(COLOURS - 1, dither=Image.Dither.NONE)
out = out.quantize(palette=fg, dither=Image.Dither.NONE)
a_ = np.array(out)
a_[~keep] = COLOURS - 1
pal_ = fg.getpalette()[:(COLOURS - 1) * 3] + [0, 0, 0]
out = Image.fromarray(a_.astype(np.uint8), "P")
out.putpalette(pal_)
out.save(os.path.join(here, "splash.png"))

pal = out.getpalette()[:COLOURS * 3]
pal += [0] * (COLOURS * 3 - len(pal))
cols = []
for r, g, b in zip(pal[0::3], pal[1::3], pal[2::3]):
    v = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3
    cols.append("0x%04X" % ((v >> 8 | v << 8) & 0xFFFF))
px = out.tobytes()
packed = bytes(px[i] << 4 | (px[i + 1] if i + 1 < len(px) else 0) for i in range(0, len(px), 2))


def lz4_block(src):
    """an LZ4 block: greedy, the longest of the last few places a 4-byte run was seen (its end's rules: the last 5 bytes
    literals, no match starting in the last 12)"""
    def lens(n):
        return b"\xff" * (n // 255) + bytes([n % 255])
    out_, seen, anchor, i = bytearray(), {}, 0, 0
    last = len(src) - 12
    while i < last:
        k = src[i:i + 4]
        best, at = 0, 0
        for c in seen.get(k, [])[-32:]:
            if i - c <= 65535:
                m = 4
                while i + m < len(src) - 5 and src[c + m] == src[i + m]:
                    m += 1
                if m > best:
                    best, at = m, c
        seen.setdefault(k, []).append(i)
        if not best:
            i += 1
            continue
        lit = src[anchor:i]
        out_.append(min(len(lit), 15) << 4 | min(best - 4, 15))
        if len(lit) >= 15:
            out_ += lens(len(lit) - 15)
        out_ += lit + bytes([(i - at) & 255, (i - at) >> 8])
        if best - 4 >= 15:
            out_ += lens(best - 19)
        i += best
        anchor = i
    lit = src[anchor:]
    out_.append(min(len(lit), 15) << 4)
    if len(lit) >= 15:
        out_ += lens(len(lit) - 15)
    return bytes(out_ + lit)


def lz4_check(src, n):                       # (as app.c's lz4_unpack: what it would make)
    d, i = bytearray(), 0
    while len(d) < n:
        t = src[i]
        i += 1
        k = t >> 4
        if k == 15:
            while True:
                k += src[i]
                i += 1
                if src[i - 1] != 255:
                    break
        d += src[i:i + k]
        i += k
        if len(d) >= n:
            break
        o = src[i] | src[i + 1] << 8
        i += 2
        k = t & 15
        if k == 15:
            while True:
                k += src[i]
                i += 1
                if src[i - 1] != 255:
                    break
        for _ in range(k + 4):
            d.append(d[-o])
    return bytes(d)


block = lz4_block(packed)
assert lz4_check(block, len(packed)) == packed
with open(os.path.join(here, "splash.h"), "w") as f:
    f.write("/* generated by bs2/rnd/make_splash.py from John Brown's photo of the RND Synth and the FM-1: do not edit */\n")
    f.write("#define RND_SPLASH_W %d\n#define RND_SPLASH_H %d\n" % out.size)
    f.write("#define RND_SPLASH_BYTES %d   /* unpacked: a pixel a nibble */\n" % len(packed))
    f.write("static const uint16_t RND_SPLASH_PAL[%d] = {   /* RGB565, byte-swapped */\n    " % COLOURS)
    f.write(", ".join(cols) + ",\n")
    f.write("};\nstatic const uint8_t RND_SPLASH[%d] = {   /* an LZ4 block */\n" % len(block))
    idx = ["%d" % v for v in block]
    for i in range(0, len(idx), 24):
        f.write("    " + ",".join(idx[i:i + 24]) + ",\n")
    f.write("};\n")
print("%d x %d, %d colours: %d B, LZ4 %d B" % (out.width, out.height, COLOURS, len(packed), len(block)))
