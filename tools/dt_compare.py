#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Drum Something's voices against the reference recordings: renders a drum at each tuning step (plain and
accented) in the host build (its `drum NAME STEP [A]` command), measures them as tools/dt_measure.py measures the
recordings, and prints the two side by side: the overall figures at every step, then at a few steps the reference's
strongest tones (a tone missing from ours shows as a low level there), the octave bands' level and tonality (a ring
reads high, a hiss low) in the hit, body and tail, and the attack (the hit over its body, and its snap).

After python3 tools/build.py bs2 --host, from the fm1-base root:
    python3 bs2/tools/dt_compare.py /path/to/recordings TOM1           # or SNARE, TOM2 ...
    python3 bs2/tools/dt_compare.py /path/to/recordings SNARE --keep /tmp/snare   # and keep the renders
    python3 bs2/tools/dt_compare.py /path/to/recordings SNARE --ab /tmp/ab.wav    # and an A/B to listen to
(tools/dt_tune.py runs it over settings of bs2_drum.c's tunables.)
"""
import argparse
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dt_measure as dm                      # noqa: E402

BS2 = Path(__file__).resolve().parent.parent
HOST = BS2 / "build" / "host" / "bass-something-2_host"
REF = {"BASS": "BASS", "SNARE": "SNARE", "RIM": "RIM", "TOM1": "TOM", "TOM2": "TOM", "CRASH": "CRASH",
       "RIDE": "RIDE", "CHH": "CLOSED HI HAT", "OHH": "OPEN HI HAT", "CLAPS": "CLAPS", "TAMB": "TAMB",
       "COWBELL": "COWBELL", "CABASA": "CABASA"}
STEPS = range(1, 17)
EDGES = [60, 120, 250, 500, 1000, 2000, 4000, 8000, 16000]
WINDOWS = [(0.0, 0.02), (0.02, 0.08), (0.08, 0.25)]


def find_refs(folder, drum, chain):
    """The recordings of a drum on a chain: {(accented, step): path}."""
    refs = {}
    for p in folder.rglob("*.wav"):
        m = dm.NAME.match(p.name)
        if m and m["chain"] == chain and dm.ALIAS.get(m["drum"], m["drum"]) == REF[drum]:
            refs[(bool(m["acc"]), int(m["step"]))] = p
    return refs


def render(drum, out, ms):
    """Each step's hit, plain and accented, as out/<drum>_<n>.wav and out/<drum>_A<n>.wav: {(accented, step): path}."""
    lines = ["wait 600", "dt_eprom 0\ndt_gain 10"]       # (+20 dB: the host's files are 16-bit, a quiet tail in their floor)
    for acc in ("", "A"):
        for n in STEPS:
            lines += [f"wav {drum}_{acc}{n}.wav", f"drum {drum} {n} {acc}".rstrip(), f"wait {ms}", "wavstop",
                      "wait 20"]
    script = out / "render.txt"
    script.write_text("\n".join(lines) + "\n")
    r = subprocess.run([str(HOST), str(script), str(out)], capture_output=True, text=True)
    if r.returncode or "FAIL" in r.stdout:
        sys.exit("the host run failed:\n" + (r.stdout + r.stderr)[-2000:])
    return {(acc == "A", n): out / f"{drum}_{acc}{n}.wav" for acc in ("", "A") for n in STEPS}


def measure_side(paths):
    """Each hit measured, with its rate against the first step's and its level against the loudest hit's (the
    recordings are normalised: only their levels against each other mean anything)."""
    side = {k: dm.measure(p) for k, p in paths.items() if k[1] in STEPS}
    base = side[(False, 1)]
    for r in side.values():
        r["ratio"] = dm.spec_ratio(r["spec"], base["spec"], 0.9, 6.0)
    top = max(r["rms"] for r in side.values())
    for r in side.values():
        r["rel"] = r["rms"] - top
    return side


def overall_report(ours, theirs, steps=None):
    """Each step's overall figures, ours / theirs (all steps, or those given); then the mean errors over all."""
    print(f"{'step':>5} {'rel dB':>13} {'d20 ms':>11} {'d40 ms':>11} {'d60 ms':>11} {'rate':>11} {'partial Hz':>11}"
          f" {'centroid Hz':>13}")
    err = []
    for key in sorted(theirs):
        o, t = ours[key], theirs[key]
        if steps is None or (not key[0] and key[1] in steps):
            print(f"{('A' if key[0] else '') + str(key[1]):>5} {o['rel']:6.1f}/{t['rel']:<6.1f} "
                  f"{o['d20']:5.0f}/{t['d20']:<5.0f} {o['d40']:5.0f}/{t['d40']:<5.0f} {o['d60']:5.0f}/{t['d60']:<5.0f}"
                  f" {o['ratio']:5.2f}/{t['ratio']:<5.2f} {o['f0']:5.0f}/{t['f0']:<5.0f}"
                  f" {o['cen']:6.0f}/{t['cen']:<6.0f}")
        err.append([np.log2(max(o[k], 1) / max(t[k], 1)) for k in ("d20", "d40", "cen")] + [o["rel"] - t["rel"]])
    e = np.abs(np.array(err)).mean(axis=0)
    print(f"mean |error|: d20 {100 * (2 ** e[0] - 1):.0f} %, d40 {100 * (2 ** e[1] - 1):.0f} %, centroid "
          f"{100 * (2 ** e[2] - 1):.0f} %, level {e[3]:.1f} dB")


def near(m, hz, spread=0.03):
    """The strongest level within spread of hz (a tone a little off still counts), and where it is."""
    return max((dm.tone_level(m["x"], m["rate"], m["onset"], f), f) for f in np.linspace(hz * (1 - spread),
                                                                                           hz * (1 + spread), 13))


def tone_report(ours, theirs, steps, n, detail=True):
    """At each step, the reference's n strongest tones (5..200 ms): Hz, then level (dB under the strongest) and
    decay (ms, time constant) in ours / theirs, ours the strongest within 3 % of it; then the mean differences."""
    dl, dt = [], []
    for st in steps:
        o, t = ours[(False, st)], theirs[(False, st)]
        ref = dm.tones(t["x"], t["rate"], t["onset"], n)
        top = near(o, ref[0][0])[0] if ref else 0.0
        row = []
        for h, db, tau in sorted(ref):
            lo, hz = near(o, h)
            lo -= top
            to = dm.tone_decay(o["x"], o["rate"], o["onset"], hz)
            row.append(f"{h:6.0f} {lo:5.0f}/{db:<4.0f} {1000 * to:4.0f}/{1000 * tau:<4.0f}")
            dl.append(abs(lo - db))
            dt.append(abs(np.log2(max(to, 1e-3) / max(tau, 1e-3))))
        if detail:
            print(f"step {st} tones: Hz  level ours/theirs  decay ms ours/theirs")
            for i in range(0, len(row), 3):
                print("   " + " | ".join(row[i:i + 3]))
    print(f"tones: mean |level difference| {np.mean(dl):.1f} dB, mean decay difference "
          f"{100 * (2 ** np.mean(dt) - 1):.0f} %")


def bands(m, a, b):
    """Octave bands' levels (dB under the strongest) and tonality (each band's peak over its median, dB: a ring
    reads high, a hiss low) over a..b seconds after the attack."""
    seg = m["x"][m["onset"] + int(a * m["rate"]):m["onset"] + int(b * m["rate"])]
    p = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 16)) ** 2
    f = np.fft.rfftfreq(1 << 16, 1 / m["rate"])
    sel = [(f >= lo) & (f < hi) for lo, hi in zip(EDGES, EDGES[1:])]
    e = np.array([p[s].sum() for s in sel])
    return 10 * np.log10(e / e.max() + 1e-12), np.array([10 * np.log10(p[s].max() / np.median(p[s])) for s in sel])


def band_report(ours, theirs, steps, detail_steps=None):
    """At each step, in three windows (the hit, its body, its tail), each octave band's level and tonality, ours
    minus theirs (printed for detail_steps, default all); then the mean differences."""
    dl, dt = [], []
    print("bands " + " ".join(f"{lo}-{hi}" for lo, hi in zip(EDGES, EDGES[1:])) + " Hz: ours - theirs, level dB"
          " (tonality dB)")
    for st in steps:
        for a, b in WINDOWS:
            lo, to = bands(ours[(False, st)], a, b)
            lt, tt = bands(theirs[(False, st)], a, b)
            dl += list(np.abs(lo - lt))
            dt += list(np.abs(to - tt))
            if detail_steps is None or st in detail_steps:
                print(f"step {st:2d} {int(a * 1000):3d}-{int(b * 1000):3d} ms " + " ".join(
                    f"{x:+4.0f}({y:+3.0f})" for x, y in zip(lo - lt, to - tt)))
    print(f"bands: mean |level difference| {np.mean(dl):.1f} dB, mean |tonality difference| {np.mean(dt):.1f} dB")


def attack(m):
    """The hit against its body: the first 5 ms's level over 20..60 ms's (dB), and its snap (the same of their
    first differences: how much brighter the hit is than the body)."""
    x, r, on = m["x"], m["rate"], m["onset"]
    ms = lambda t: on + int(t * r / 1000)     # noqa: E731

    def db(v):
        return 20 * np.log10(np.sqrt(np.mean(v ** 2)) + 1e-12)
    hit, body = x[ms(0):ms(5)], x[ms(20):ms(60)]
    return db(hit) - db(body), db(np.diff(hit)) - db(np.diff(body))


def attack_report(ours, theirs, steps):
    """At each step, the attack (attack()), ours / theirs."""
    for st in steps:
        (oh, os_), (th, ts) = attack(ours[(False, st)]), attack(theirs[(False, st)])
        print(f"attack step {st:2d}: hit over body {oh:+5.1f}/{th:+5.1f} dB, snap {os_:+5.1f}/{ts:+5.1f} dB")


def cycles(m, n=5, lp=300.0):
    """The first n cycles of a low drum, from the upward zero crossings of the sound low-passed at lp Hz: each
    (start ms, Hz, its peak dB under the loudest), and where the whole sound peaks (ms)."""
    x, r, on = m["x"], m["rate"], m["onset"]
    spec = np.fft.rfft(x)
    spec[np.fft.rfftfreq(len(x), 1 / r) > lp] = 0
    y = np.fft.irfft(spec, len(x))
    z = np.nonzero((y[on:-1] <= 0) & (y[on + 1:] > 0))[0] + on
    top = np.abs(y).max()
    out = [(1000 * (a - on) / r, r / (b - a), 20 * np.log10(np.abs(y[a:b]).max() / top + 1e-9))
           for a, b in zip(z[:n], z[1:n + 1])]
    return out, 1000 * int(np.argmax(np.abs(x[on:]))) / r


def cycle_report(ours, theirs, steps):
    """At each step, the first cycles (cycles()), ours then theirs: a kick's or a tom's pitch fall as it is played."""
    for st in steps:
        for who, m in (("ours  ", ours[(False, st)]), ("theirs", theirs[(False, st)])):
            cyc, peak = cycles(m)
            print(f"cycles step {st:2d} {who}: " + " ".join(f"{t:.0f}ms:{hz:.0f}Hz({db:.0f})" for t, hz, db in cyc)
                  + f" | peak {peak:.1f} ms")


def rate_table(drum):
    """The drum's tuning table (Q12) as bs2_drum.c has it, and its name (DT_RATE_<drum>; both Toms' is TOM)."""
    import re
    name = "DT_RATE_" + {"TOM1": "TOM", "TOM2": "TOM"}.get(drum, drum)
    m = re.search(name + r"\[16\] = \{([^}]*)\}", (BS2 / "bs2_drum.c").read_text())
    if not m:
        sys.exit(f"no {name} in bs2_drum.c")
    return [int(v) for v in m.group(1).replace("\n", " ").split(",")], name


def pitch_fix(ours, theirs, drum, low=False):
    """Each step's strongest partial, ours / theirs, and the drum's tuning table corrected so ours lands on
    theirs (each step's rate times their pitch over ours); low: from the first cycles (a low drum), not the
    partial."""
    table, name = rate_table(drum)
    fixed = []
    for st in STEPS:
        o, t = ours[(False, st)], theirs[(False, st)]
        if low:
            oh = np.median([h for _, h, _ in cycles(o, 3)[0]])
            th = np.median([h for _, h, _ in cycles(t, 3)[0]])
        else:
            oh, th = o["f0"], t["f0"]
        fixed.append(int(round(table[st - 1] * th / oh)))
        print(f"pitch step {st:2d}: {oh:7.1f}/{th:<7.1f} Hz ({1200 * np.log2(oh / th):+5.0f} cents)")
    print(f"{name} corrected: {{" + ", ".join(str(v) for v in fixed) + "}")


def env_report(ours, theirs, steps, drum, bins=22, ms=8.0):
    """At each step, the RMS envelope in ROM time (bins of ms milliseconds of the sample's own time, from the
    drum's tuning table: the same shape at every step is a ROM's; one that changes with the step is something in real
    time), dB under the peak, ours then theirs."""
    table, _ = rate_table(drum)
    for st in steps:
        rate = table[st - 1] / table[0]
        for who, m in (("ours  ", ours[(False, st)]), ("theirs", theirs[(False, st)])):
            x, r, on = m["x"], m["rate"], m["onset"]
            hop = max(1, int(r * ms / 1000 / rate))
            top = np.abs(x).max()
            e = [20 * np.log10(np.sqrt(np.mean(x[on + i * hop:on + (i + 1) * hop] ** 2)) / top + 1e-9)
                 for i in range(bins)]
            print(f"env step {st:2d} {who} ({ms:.0f} ms ROM a bin): " + " ".join(f"{v:4.0f}" for v in e))


def write_ab(path, theirs_paths, ours_paths, steps, seconds):
    """An A/B to listen to: at each step the reference, then ours, each normalised (the recordings are), at 44.1 kHz
    mono 16-bit; a short gap between the pair, a longer one between steps."""
    def load(p):
        x, r = dm.read_wav(p)
        n = int(44100 * seconds)
        y = np.interp(np.arange(n) / 44100, np.arange(len(x)) / r, x, right=0.0)
        return y / max(np.abs(y).max(), 1e-9) * 0.7
    out = []
    for st in steps:
        out += [load(theirs_paths[(False, st)]), np.zeros(8820), load(ours_paths[(False, st)]), np.zeros(22050)]
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes((np.concatenate(out) * 32767).astype("<i2").tobytes())
    print(f"wrote {path}")


def compare(folder, drum, chain="535-LA", ms=2000, keep=None):
    """Renders the drum and measures both sides: (ours, theirs, our paths, their paths)."""
    refs = find_refs(folder, drum, chain)
    if not refs:
        sys.exit(f"no {REF[drum]} recordings under {folder}")
    with tempfile.TemporaryDirectory() as tmp:
        out = keep or Path(tmp)
        out.mkdir(parents=True, exist_ok=True)
        paths = render(drum, out, ms)
        ours = measure_side(paths)
        if keep is None:                     # (the measurements hold the samples: the files may go)
            paths = {}
    return ours, measure_side(refs), paths, refs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", type=Path, help="the reference recordings")
    ap.add_argument("drum", help="the voice, as the host's drum command names it (TOM1, SNARE ...)")
    ap.add_argument("--chain", default="535-LA")
    ap.add_argument("--ms", type=int, default=2000, help="each render's length")
    ap.add_argument("--keep", type=Path, help="render into this folder and keep it")
    ap.add_argument("--tones", type=int, default=10, help="how many of the reference's tones to follow")
    ap.add_argument("--steps", default="1,8,16", help="the steps the detailed reports cover")
    ap.add_argument("--summary", action="store_true", help="the detailed steps' figures and the means only")
    ap.add_argument("--cycles", action="store_true", help="the first cycles too (a low drum's pitch fall)")
    ap.add_argument("--env", action="store_true", help="the envelopes in ROM time (a real-time effect shows as a "
                                                       "shape changing with the step)")
    ap.add_argument("--pitch", action="store_true", help="each step's pitch, ours / theirs, and the tuning table "
                                                         "corrected to theirs (with --cycles: from the first cycles)")
    ap.add_argument("--ab", type=Path, help="write an A/B to listen to (the reference, then ours, at --ab-steps)")
    ap.add_argument("--ab-steps", default="1,8,16")
    ap.add_argument("--ab-seconds", type=float, default=1.0, help="each A/B hit's length")
    a = ap.parse_args()
    drum = a.drum.upper()
    if drum not in REF:
        sys.exit(f"no drum {drum}: one of {', '.join(REF)}")
    steps = [int(v) for v in a.steps.split(",")]
    keep = a.keep
    if a.ab and not keep:
        keep = Path(tempfile.mkdtemp())
    ours, theirs, our_paths, their_paths = compare(a.folder, drum, a.chain, a.ms, keep)
    print(f"{drum} against {REF[drum]} ({a.chain}); each pair ours / theirs")
    overall_report(ours, theirs, steps if a.summary else None)
    tone_report(ours, theirs, steps, a.tones, not a.summary)
    band_report(ours, theirs, steps, steps[:1] if a.summary else None)
    attack_report(ours, theirs, steps)
    if a.cycles:
        cycle_report(ours, theirs, steps)
    if a.env:
        env_report(ours, theirs, steps, drum)
    if a.pitch:
        pitch_fix(ours, theirs, drum, a.cycles)
    if a.ab:
        write_ab(a.ab, their_paths, our_paths, [int(v) for v in a.ab_steps.split(",")], a.ab_seconds)


if __name__ == "__main__":
    main()
