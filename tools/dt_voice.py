#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Drum Something: a new voice's starting point, from the reference recordings of its drum. Prints, ready for
bs2_drum.c: the tuning steps' rate table (Q12; from the strongest partial's pitch where it is steady, else from the
spectra's shift), the ROM's sample rate (from the sample-and-hold notches, where the drum is bright enough to show
them), the mode table (the strongest tones in ROM time, merged over several steps), the pitch's fall (as dt_voice_t's
sweep, sweep_tau, sweep2, sweep2_tau) and the sample's length and decays. tools/dt_compare.py and dt_tune.py take it
from there.

From the fm1-base root:
    python3 bs2/tools/dt_voice.py /path/to/recordings RIM
    python3 bs2/tools/dt_voice.py /path/to/recordings BASS --tones 12 --steps 1,5,9,13
"""
import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dt_compare as dc                      # noqa: E402
import dt_measure as dm                      # noqa: E402


def rates(side):
    """Each step's rate against the first: from the strongest partial where it rises steadily (a tonal drum), else
    from the spectra's shift (each searched from the step before's, as tools/dt_measure.py does). (rates, how)"""
    steps = sorted(k[1] for k in side if not k[0])
    f0 = np.array([side[(False, s)]["f0"] for s in steps])
    part = f0 / f0[0]
    spec, prev = [1.0], 1.0
    for s in steps[1:]:
        prev = dm.spec_ratio(side[(False, s)]["spec"], side[(False, 1)]["spec"], prev * 0.97, prev * 1.6)
        spec.append(prev)
    spec = np.array(spec)
    steady = np.all(np.diff(part) > -0.01) and np.all(np.abs(np.log(part[1:] / spec[1:])) < np.log(1.25))
    return (part, "partial") if steady else (spec, "spectrum")


def rates_by_cycles(side, n=3):
    """Each step's rate against the first from its first n cycles' pitches (tools/dt_compare.py's cycles()): for a
    low drum whose pitch falls fast, where a window's average pitch reads the fall, not the rate."""
    steps = sorted(k[1] for k in side if not k[0])
    first = [hz for _, hz, _ in dc.cycles(side[(False, 1)], n)[0]]
    out = []
    for s in steps:
        hz = [h for _, h, _ in dc.cycles(side[(False, s)], n)[0]]
        out.append(float(np.median([a / b for a, b in zip(hz, first)])))
    return np.array(out)


def notches(m, lo=4000.0, depth=8.0):
    """The sample-and-hold notches over the first 150 ms: frequencies where the smoothed spectrum dips depth dB under
    its neighbourhood's median (a sample rate's multiples)."""
    x, r, on = m["x"], m["rate"], m["onset"]
    seg = x[on:on + int(r * 0.15)]
    p = 10 * np.log10(np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 16)) ** 2 + 1e-20)
    f = np.fft.rfftfreq(1 << 16, 1 / r)
    k = int(200 / f[1])
    ps = np.convolve(p, np.ones(k) / k, mode="same")
    w = int(2000 / f[1])
    out = set()
    for i in np.nonzero((f > lo) & (f < r / 2 - 2000))[0][::k // 2]:
        a = max(i - w, 0)
        j = a + int(np.argmin(ps[a:i + w]))
        if abs(j - i) < k // 2 and np.median(ps[a:i + w]) - ps[j] > depth:
            out.add(round(f[j] / 100) * 100)
    return sorted(out)


def modes(side, ratio, steps, n):
    """The strongest tones in ROM time (Hz and decays divided by / times each step's rate), merged over steps (within
    3 %): those seen in two steps or more, strongest first, as (Hz, dB, tau s, seen)."""
    found = []
    for s in steps:
        m = side[(False, s)]
        for hz, db, tau in dm.tones(m["x"], m["rate"], m["onset"], n):
            found.append((hz / ratio[s - 1], db, tau * ratio[s - 1]))
    found.sort()
    groups = []
    for t in found:
        if groups and t[0] / groups[-1][-1][0] < 1.03:
            groups[-1].append(t)
        else:
            groups.append([t])
    out = [(float(np.mean([t[0] for t in g])), float(np.mean([t[1] for t in g])), float(np.median([t[2] for t in g])),
            len(g)) for g in groups if len(g) >= 2]
    top = max((o[1] for o in out), default=0.0)
    return sorted([(h, db - top, tau, c) for h, db, tau, c in out], key=lambda o: -o[1])


def sweep_fit(m, hz, ratio):
    """The strongest tone's pitch over time, in ROM time, fitted as rest * (1 + A e^-t/ta + B e^-t/tb), followed
    while it is within 30 dB of its start, in frames of four of its periods (at least 40 ms):
    (rest Hz, A, ta, B, tb, the track as [(ROM s, Hz)])."""
    ts = np.array([0.005, 0.015, 0.03, 0.05, 0.08, 0.12, 0.18, 0.26, 0.36, 0.5, 0.7, 1.0])
    pts = dm.pitch_track(m["x"], m["rate"], m["onset"], hz, ts, win=max(0.04, 4.0 / hz), floor=30.0)
    tr = np.array([t for t, _ in pts]) * ratio
    track = np.array([h for _, h in pts]) / ratio
    best = (np.inf, (float(track[-1]), 0.0, 0.035, 0.0, 0.33))
    for rest in track[-1] * np.linspace(0.94, 1.0, 7):
        y = track / rest - 1
        for ta in (0.01, 0.02, 0.035, 0.05, 0.08):
            for tb in (0.1, 0.2, 0.33, 0.5, 0.8):
                if tb <= ta or len(tr) < 3:
                    continue
                basis = np.stack([np.exp(-tr / ta), np.exp(-tr / tb)], 1)
                coef = np.maximum(np.linalg.lstsq(basis, y, rcond=None)[0], 0)
                err = float(np.sum((basis @ coef - y) ** 2))
                if err < best[0]:
                    best = (err, (float(rest), float(coef[0]), ta, float(coef[1]), tb))
    return best[1] + (list(zip(tr, track)),)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", type=Path)
    ap.add_argument("drum", help="as tools/dt_compare.py names it (RIM, BASS, ...)")
    ap.add_argument("--chain", default="535-LA")
    ap.add_argument("--tones", type=int, default=12)
    ap.add_argument("--steps", default="1,5,9,13", help="the steps whose tones are merged")
    ap.add_argument("--cycles", action="store_true", help="rates from the first cycles' pitches (a low drum whose "
                                                         "pitch falls fast)")
    a = ap.parse_args()
    drum = a.drum.upper()
    refs = dc.find_refs(a.folder, drum, a.chain)
    side = {k: dm.measure(p) for k, p in refs.items() if not k[0] and k[1] <= 16}
    ratio, how = (rates_by_cycles(side), "first cycles") if a.cycles else rates(side)
    print(f"{drum} ({dc.REF[drum]}, {a.chain}): {len(side)} steps")
    print(f"\nrates, from the {how} (Q12, against the first step's):")
    q = [int(round(4096 * v)) for v in ratio]
    print("    {" + ", ".join(str(v) for v in q) + "}")
    print(f"    ({12 * np.log2(ratio[-1]) / (len(ratio) - 1):+.2f} semitones a step on average)")

    print("\nsample-and-hold notches (Hz) and the ROM rate at the first step they imply:")
    for s in (1, 2, 3, 4):
        ns = notches(side[(False, s)])
        if ns:
            print(f"    step {s}: {ns}  -> {ns[0] / ratio[s - 1]:.0f} Hz" + (
                f" (2nd: {ns[1] / ratio[s - 1] / 2:.0f})" if len(ns) > 1 and 1.8 < ns[1] / ns[0] < 2.2 else ""))
        else:
            print(f"    step {s}: none (too dark, or filtered)")

    ms = modes(side, ratio, [int(v) for v in a.steps.split(",")], a.tones)
    print("\nmodes in ROM time (Hz, dB under the strongest, decay s; [steps seen in]):")
    print("    " + ", ".join(f"{{{h:.0f}, {db:.0f}, {tau:.3f}f, 0}}" for h, db, tau, _ in ms))
    print("    seen: " + " ".join(str(c) for *_, c in ms))

    mid = side[(False, 5)]
    rest, A, ta, B, tb, track = sweep_fit(mid, ms[0][0] * ratio[4] if ms else mid["f0"], ratio[4])
    print(f"\npitch's fall (step 5, ROM time): rest {rest:.0f} Hz; .sweep = {A:.2f}f, .sweep_tau = {ta}f, "
          f".sweep2 = {B:.2f}f, .sweep2_tau = {tb}f")
    print("    track: " + " ".join(f"{1000 * t:.0f}ms:{h:.0f}" for t, h in track))

    first = side[(False, 1)]
    print(f"\nstep 1: d20 {first['d20']:.0f} ms, d40 {first['d40']:.0f} ms, d60 {first['d60']:.0f} ms "
          f"(.len about {first['d60'] / 1000 * 1.1:.2f}f s: ROM time is the first step's)")


if __name__ == "__main__":
    main()
