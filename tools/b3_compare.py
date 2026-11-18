#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The organ (bs2_b3.c) against reference recordings of a tonewheel organ: the same takes rendered in the host build
(its b3_direct: the preamp's out, no Leslie, for the takes recorded direct) and measured side by side.

The references: a folder of WAVs named as below (bar1_C4.wav .. leslie_down.wav), each with 0.1-0.2 s before its
first note, recorded at a fixed gain. Levels are compared relative to bar3_C4 (the 8' alone on C4) in each set.

After python3 tools/build.py bs2 --host, from the fm1-base root:
    python3 bs2/tools/b3_compare.py /path/to/recordings            # every stage
    python3 bs2/tools/b3_compare.py /path/to/recordings --only bars
"""
import argparse
import math
import pathlib
import subprocess
import sys
import tempfile
import wave

import numpy as np

BS2 = pathlib.Path(__file__).resolve().parent.parent
HOST = BS2 / "build" / "host" / "bass-something-2_host"
BAR = [-12, 7, 0, 12, 19, 24, 28, 31, 36]   # the drawbars' semitones (bs2_b3.c's B3_BAR)
BAR_NAME = ["16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'"]
W_LO, W_HI = 24, 114                         # the generator's wheels, MIDI notes (C1 .. F#8)
CHORD = [60, 64, 67]
SWEEP = list(range(36, 97, 3))


def takes():
    """{name: (registration, extra CCs {cc: value}, [(ms, notes on or off)], length ms, direct)} in our CCs
    (bs2_organ.c's organ_cc: 21 percussion, 22 THIRD, 23 SOFT, 24 FAST, 30 vibrato, 25 its knob V1 V2 V3 C1 C2 C3,
    1 the Leslie fast)"""
    t = {}

    def hold(notes, ms):
        return [(0, notes, True), (ms, notes, False)]

    def stac(n, k, on, gap, t0=0):
        ev = []
        for i in range(k):
            ev += [(t0 + i * (on + gap), [n], True), (t0 + i * (on + gap) + on, [n], False)]
        return ev
    for i in range(9):
        t["bar%d_C4" % (i + 1)] = ("0" * i + "8" + "0" * (8 - i), {}, hold([60], 2000), 3000, True)
    t["bar1_C2"] = ("800000000", {}, hold([36], 2000), 3000, True)
    t["bar9_C6"] = ("000000008", {}, hold([84], 2000), 3000, True)
    for r in ("888000000", "888888888", "688600000"):
        for nm, n in (("C2", 36), ("C4", 60), ("C6", 84)):
            t["reg%s_%s" % (r, nm)] = (r, {}, hold([n], 2000), 3000, True)
        t["reg%s_chord" % r] = (r, {}, hold(CHORD, 4000), 5000, True)
    perc = hold([60], 2500) + stac(60, 4, 300, 300, 3000)
    t["perc3_fast"] = ("888000000", {21: 127, 22: 127, 23: 0, 24: 127}, perc, 6000, True)
    t["perc2_slow_soft"] = ("888000000", {21: 127, 22: 0, 23: 127, 24: 0}, perc, 6000, True)
    t["click"] = ("888888888", {}, stac(60, 8, 150, 350), 4500, True)
    t["vib_V1"] = ("888000000", {30: 127, 25: 0}, hold(CHORD, 4000), 5000, True)
    t["vib_C3"] = ("888000000", {30: 127, 25: 127}, hold(CHORD, 4000), 5000, True)
    t["leak_chord"] = ("000000000", {}, hold(CHORD, 4000), 5000, True)
    for i in range(9):                       # each drawbar alone up the keys, a key every 3 semitones
        ev = []
        for j, k in enumerate(SWEEP):
            ev += [(j * 600, [k], True), (j * 600 + 450, [k], False)]
        t["sweep_bar%d" % (i + 1)] = ("0" * i + "8" + "0" * (8 - i), {}, ev, len(SWEEP) * 600 + 400, True)
    t["leslie_slow"] = ("888000000", {1: 0}, hold(CHORD, 6000), 7000, False)
    t["leslie_fast"] = ("888000000", {1: 127}, hold(CHORD, 6000), 7000, False)
    t["leslie_up"] = ("888000000", {1: 0}, hold(CHORD, 8000) + [(2000, ("cc", 1, 127), None)], 9000, False)
    t["leslie_down"] = ("888000000", {1: 127}, hold(CHORD, 8000) + [(2000, ("cc", 1, 0), None)], 9000, False)
    return t


def render(names, out):
    """The takes in the host, as out/<name>.wav: {name: path}"""
    T = takes()
    lines = ["wait 600"]
    for n in names:
        reg, extra, ev, ms, direct = T[n]
        cc = {12 + i: round(int(d) * 127 / 8) for i, d in enumerate(reg)}
        cc.update({21: 0, 30: 0, 26: 0, 11: 127, 1: 0, 27: 0})
        cc.update(extra)
        lines += ["b3_direct %d" % direct] + ["midi B3 %02X %02X" % (c, v) for c, v in cc.items()]
        lines += ["wait %d" % (2500 if not direct else 400), "wav %s.wav" % n, "wait 100"]
        now = 0
        for at, notes, on in sorted(ev, key=lambda e: e[0]):
            if at > now:
                lines.append("wait %d" % (at - now))
                now = at
            if notes[0] == "cc":
                lines.append("midi B3 %02X %02X" % notes[1:])
            else:
                lines += ["midi %s %02X %s" % ("93" if on else "83", k, "64" if on else "00") for k in notes]
        lines += ["wait %d" % (ms - now + 100), "wavstop", "midi B3 7B 7F", "wait 300"]
    script = out / "b3.txt"
    script.write_text("\n".join(lines) + "\n")
    r = subprocess.run([str(HOST), str(script), str(out)], capture_output=True, text=True)
    if r.returncode:
        sys.exit("the host run failed:\n" + (r.stdout + r.stderr)[-2000:])
    return {n: out / ("%s.wav" % n) for n in names}


def read(path):
    """(samples [n, channels] -1..1, rate), 16- or 24-bit"""
    with wave.open(str(path)) as w:
        sw, ch, sr, b = w.getsampwidth(), w.getnchannels(), w.getframerate(), w.readframes(w.getnframes())
    if sw == 2:
        x = np.frombuffer(b, np.int16) / 32768.0
    else:
        a = np.frombuffer(b, np.uint8).reshape(-1, 3).astype(np.int32)
        v = a[:, 0] | a[:, 1] << 8 | a[:, 2] << 16
        x = np.where(v >= 1 << 23, v - (1 << 24), v) / float(1 << 23)
    return x.reshape(-1, ch), sr


def onset(m, sr):
    e = np.abs(m)
    floor = np.median(e[: sr // 20]) if len(e) > sr // 20 else 0.0
    return int(np.argmax(e > 10 * floor + 1e-4))


def mono(path):
    x, sr = read(path)
    m = x.mean(1)
    return m[onset(m, sr):], sr


def spectrum(m, sr, a=0.6, b=1.6):
    s = m[int(a * sr): int(b * sr)]
    s = s * np.hanning(len(s))
    n = 8 * len(s)
    return np.abs(np.fft.rfft(s, n)) * 2 / np.hanning(len(s)).sum(), sr / n


def partial(spec, hz, near=0.015):
    f, df = spec
    lo, hi = int(hz * (1 - near) / df), int(hz * (1 + near) / df) + 1
    k = lo + int(np.argmax(f[lo:hi]))
    return f[k], k * df


def note_hz(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def wheel(n):
    while n < W_LO:
        n += 12
    while n > W_HI:
        n -= 12
    return n


def bar_wheel(key, b):
    """the wheel a key's drawbar b sounds (the 16' folded back on the bottom octave, as bs2_b3.c's)"""
    return wheel(key + BAR[b] + (12 if b == 0 and key < 48 else 0))


def db(v):
    return 20 * math.log10(max(v, 1e-9))


def stage_bars(ref, ours):
    """each drawbar alone on C4: its partial's level (to the 8''s) and its pitch; the ends' foldback; the 8''s own
    harmonics (the wheel's pickup)"""
    print("== drawbars alone on C4: level to the 8', dB (ours - ref)")
    base = {}
    for side, d in (("ref", ref), ("ours", ours)):
        m, sr = mono(d["bar3_C4"])
        base[side] = partial(spectrum(m, sr), note_hz(60))[0]
    for i in range(9):
        row = []
        for side, d in (("ref", ref), ("ours", ours)):
            m, sr = mono(d["bar%d_C4" % (i + 1)])
            a, hz = partial(spectrum(m, sr), note_hz(bar_wheel(60, i)))
            row.append((db(a / base[side]), hz))
        print("  %-7s ref %6.1f dB %7.1f Hz   ours %6.1f dB %7.1f Hz   diff %+5.1f" %
              (BAR_NAME[i], row[0][0], row[0][1], row[1][0], row[1][1], row[1][0] - row[0][0]))
    print("== the ends: strongest partial")
    for n in ("bar1_C2", "bar9_C6"):
        row = []
        for d in (ref, ours):
            m, sr = mono(d[n])
            f, df = spectrum(m, sr)
            k = int(np.argmax(f[int(20 / df):])) + int(20 / df)
            row.append(k * df)
        print("  %-8s ref %7.1f Hz   ours %7.1f Hz" % (n, row[0], row[1]))
    print("== the 8' wheel's own harmonics (C4), dB to its fundamental")
    for h in (2, 3, 4, 5):
        row = []
        for d in (ref, ours):
            m, sr = mono(d["bar3_C4"])
            sp = spectrum(m, sr)
            row.append(db(partial(sp, note_hz(60) * h, 0.006)[0] / partial(sp, note_hz(60))[0]))
        print("  h%d  ref %6.1f   ours %6.1f   diff %+5.1f" % (h, row[0], row[1], row[1] - row[0]))


def stage_regs(ref, ours):
    """the registrations: each drawbar's partial to the 8''s, at C2 C4 C6; the overall level to bar3_C4"""
    for r in ("888000000", "888888888", "688600000"):
        for nm, n in (("C2", 36), ("C4", 60), ("C6", 84)):
            name = "reg%s_%s" % (r, nm)
            print("== %s: partials to the 8', dB (ours - ref)" % name)
            rows = {}
            for side, d in (("ref", ref), ("ours", ours)):
                m, sr = mono(d[name])
                sp = spectrum(m, sr)
                f8 = partial(sp, note_hz(wheel(n)))[0]
                rows[side] = [db(partial(sp, note_hz(bar_wheel(n, i)))[0] / f8) for i in range(9)]
            on = [i for i in range(9) if r[i] != "0"]
            print("  " + " ".join("%7s" % BAR_NAME[i] for i in on))
            print("  " + " ".join("%+7.1f" % (rows["ours"][i] - rows["ref"][i]) for i in on))
    print("== overall level (RMS 0.6-1.6 s) to bar3_C4's, dB")
    for n in [k for k in takes() if k.startswith("reg")]:
        row = []
        for d in (ref, ours):
            m0, sr = mono(d["bar3_C4"])
            m, sr = mono(d[n])
            w = lambda v: np.sqrt((v[int(0.6 * sr): int(1.6 * sr)] ** 2).mean())
            row.append(db(w(m) / w(m0)))
        print("  %-22s ref %+6.1f   ours %+6.1f   diff %+5.1f" % (n, row[0], row[1], row[1] - row[0]))


def bands_db(m, sr, a, secs=1.0, ms=20.0):
    """1/3-octave bands (31.5 Hz .. 16 kHz), dB, in frames of ms over secs from a seconds"""
    n = int(ms * sr / 1000)
    edges = 31.5 * 2 ** ((np.arange(29) - 0.5) / 3)
    fr = np.fft.rfftfreq(n, 1 / sr)
    idx = [(fr >= lo) & (fr < hi) for lo, hi in zip(edges[:-1], edges[1:])]
    out = []
    for k in range(int(secs * 1000 / ms)):
        s = m[int(a * sr) + k * n: int(a * sr) + (k + 1) * n]
        if len(s) < n:
            break
        p = np.abs(np.fft.rfft(s * np.hanning(n))) ** 2
        out.append([10 * math.log10(p[i].sum() + 1e-20) if i.any() else -200.0 for i in idx])
    return np.array(out)


def score(rp, op, a):
    """the piano's score (bs2-rmi): 100 - 10/3 x the mean |dB| of 1/3-octave bands in 20 ms frames over 1 s, each
    side floored 50 dB under its loudest, ours level matched (its median offset), 30 Hz high passed"""
    r, sr = mono(rp)
    o, so = mono(op)
    hp = lambda m, s: m - np.convolve(m, np.ones(int(s / 30)) / int(s / 30), "same")
    R, O = bands_db(hp(r, sr), sr, a), bands_db(hp(o, so), so, a)
    n = min(len(R), len(O))
    R, O = R[:n], O[:n]
    R, O = np.maximum(R, R.max() - 50), np.maximum(O, O.max() - 50)
    O = O + np.median(R - O)
    O = np.maximum(O, R.max() - 50)
    return 100 - 10 / 3 * np.abs(R - O).mean()


def stage_score(ref, ours):
    """each take's score (the window from 0.05 s; the Leslie's from 1 s, its rotors up to speed), and their mean by
    group"""
    print("== score (100: the same; 3 dB mean off: 90)")
    groups = {}
    for n in ref:
        if n.startswith("sweep"):
            continue
        s = score(ref[n], ours[n], 1.0 if n.startswith("leslie") else 0.05)
        g = n.split("_")[0].rstrip("0123456789")
        groups.setdefault(g, []).append(s)
        print("  %-22s %5.1f %%" % (n, s))
    allv = [v for g in groups.values() for v in g]
    for g, v in groups.items():
        print("  %-8s mean %5.1f %%" % (g, sum(v) / len(v)))
    print("  ALL      mean %5.1f %%" % (sum(allv) / len(allv)))


STAGES = {"bars": stage_bars, "regs": stage_regs, "score": stage_score}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("refs", help="the folder of reference WAVs")
    ap.add_argument("--only", choices=sorted(STAGES), action="append", help="these stages only")
    ap.add_argument("--keep", help="render into this folder (else a temporary one)")
    a = ap.parse_args()
    refs = pathlib.Path(a.refs)
    names = [n for n in takes() if (refs / ("%s.wav" % n)).exists()]
    ref = {n: refs / ("%s.wav" % n) for n in names}
    out = pathlib.Path(a.keep or tempfile.mkdtemp())
    out.mkdir(parents=True, exist_ok=True)
    ours = render(names, out)
    for s in a.only or STAGES:
        STAGES[s](ref, ours)


if __name__ == "__main__":
    main()
