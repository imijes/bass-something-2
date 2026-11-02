#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Drum Something's reference measurements: reads a folder of drum-machine recordings (one hit a file, named
"<DRUM> via <CHAIN> [A]<n>.wav", n the tuning step and A the accent) and writes a table, one row a hit, of what
the synth voices are tuned against: level, decay times, the playback-rate ratio to step 1 (two ways), the
strongest partial and the spectral centroid.

The recordings stay outside the repo (they are a reference only; nothing of them ships). Only these numbers do.
    python3 bs2/tools/dt_measure.py /path/to/recordings                 # the 535-LA chain, to stdout
    python3 bs2/tools/dt_measure.py /path/to/recordings --chain M-A-S --out bs2/docs/DT_MEASURE.md
"""
import argparse
import re
import struct
import sys
from pathlib import Path

import numpy as np

NAME = re.compile(r"^(?P<drum>.+?) via (?P<chain>\S+)\s+(?P<acc>A)?\s*(?P<step>\d+)\.wav$")
HOP_MS = 1.0                                 # the envelope's resolution
ALIAS = {"TAMP": "TAMB"}                     # the pack's spellings
BPO = 96                                     # the log spectrum's bins an octave
LOG_F = 30.0 * 2 ** (np.arange(int(BPO * np.log2(20000 / 30))) / BPO)


def read_wav(path):
    """Mono float samples and the rate, from a PCM (16/24/32-bit) or float WAV, any chunk layout."""
    b = path.read_bytes()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        raise ValueError(f"{path.name}: not a WAV")
    fmt = data = None
    i = 12
    while i + 8 <= len(b):
        cid, n = b[i:i + 4], struct.unpack("<I", b[i + 4:i + 8])[0]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", b[i + 8:i + 24])
        elif cid == b"data":
            data = b[i + 8:i + 8 + n]
        i += 8 + n + (n & 1)
    if not fmt or data is None:
        raise ValueError(f"{path.name}: no fmt or data chunk")
    tag, ch, rate, _, _, bits = fmt
    w = bits // 8
    data = data[:len(data) // (w * ch) * w * ch]
    if tag == 3:
        x = np.frombuffer(data, "<f4" if w == 4 else "<f8").astype(np.float64)
    elif w == 3:
        u = np.frombuffer(data, np.uint8).reshape(-1, 3).astype(np.int32)
        x = ((u[:, 0] | u[:, 1] << 8 | u[:, 2] << 16) << 8 >> 8) / 8388608.0
    else:
        x = np.frombuffer(data, {2: "<i2", 4: "<i4"}[w]) / float(1 << (bits - 1))
    return x.reshape(-1, ch).mean(axis=1), rate


def envelope_db(x, rate):
    """The peak level in each HOP_MS window, in dB."""
    hop = max(1, int(rate * HOP_MS / 1000))
    n = len(x) // hop
    e = np.abs(x[:n * hop]).reshape(n, hop).max(axis=1)
    return 20 * np.log10(np.maximum(e, 1e-7))


def decay_ms(env, below):
    """The time from the attack (the envelope first within 20 dB of its peak) until it last sits above
    (peak - below) dB. From the attack, not the peak: a held sound's peak can come late."""
    top = env.max()
    start = int(np.argmax(env > top - 20))
    above = np.nonzero(env > top - below)[0]
    return float(above[-1] - start) * HOP_MS


def spectrum(x, rate, onset):
    """The strongest partial (30 Hz..8 kHz) and the centroid, over the 60 ms after the attack."""
    a = onset + int(rate * 0.003)
    seg = x[a:a + int(rate * 0.06)]
    if len(seg) < 256:
        return 0.0, 0.0
    n = 1 << 16
    mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    f = np.fft.rfftfreq(n, 1 / rate)
    band = (f >= 30) & (f <= 8000)
    peak = float(f[band][mag[band].argmax()])
    full = f <= 20000
    cen = float((f[full] * mag[full]).sum() / max(mag[full].sum(), 1e-12))
    return peak, cen


def log_spectrum(x, rate, onset, d60):
    """The hit's average power spectrum (Welch, 4096-point frames) on a log-frequency grid, in dB re its peak."""
    seg = x[onset:onset + max(int(rate * min(d60, 1500) / 1000), 4096)]
    n = 4096
    frames = [seg[i:i + n] for i in range(0, len(seg) - n + 1, n // 2)] or [np.pad(seg, (0, n - len(seg)))]
    w = np.hanning(n)
    pw = np.mean([np.abs(np.fft.rfft(f * w)) ** 2 for f in frames], axis=0)
    db = 10 * np.log10(np.maximum(np.interp(LOG_F, np.fft.rfftfreq(n, 1 / rate), pw), 1e-20))
    return np.maximum(db - db.max(), -70.0)


def spec_ratio(spec, ref, lo=1 / 8, hi=8.0):
    """How much faster spec plays than ref: the shift along log frequency that lines ref's spectrum up with it
    best (within lo..hi). A varispeed sample's whole spectrum, its filter included, moves by the playback rate."""
    best, err = 0, np.inf
    for k in range(int(np.floor(BPO * np.log2(lo))), int(np.ceil(BPO * np.log2(hi))) + 1):
        a, b = (spec[k:], ref[:len(ref) - k]) if k >= 0 else (spec[:k], ref[-k:])
        if len(a) < 2 * BPO:
            continue
        keep = (a > -50) | (b > -50)                 # only where either has energy
        d = float(np.mean((a[keep] - b[keep]) ** 2)) if keep.any() else np.inf
        if d < err:
            best, err = k, d
    return 2 ** (best / BPO)


def rate_ratio(env, ref):
    """How much faster env plays than ref: the time stretch of ref's decay that fits env best (1/8..8)."""
    floor = -60.0
    e = np.maximum(env - env.max(), floor)[int(env.argmax()):]
    r = np.maximum(ref - ref.max(), floor)[int(ref.argmax()):]
    t = np.arange(len(r))
    best, err = 1.0, np.inf
    for s in np.geomspace(0.125, 8, 1441):
        m = min(len(e), int(len(r) / s))
        if m < 8:
            continue
        guess = np.interp(np.arange(m) * s, t, r, right=floor)
        d = float(np.mean((guess - e[:m]) ** 2))
        if d < err:
            best, err = s, d
    return best


def tone_level(x, rate, onset, hz, a=0.005, b=0.2):
    """A tone's level (dB) over a..b seconds after the attack: one Hann-windowed DFT bin, at hz exactly."""
    seg = x[onset + int(a * rate):onset + int(b * rate)]
    if len(seg) < 64:
        return -150.0
    w = np.hanning(len(seg))
    t = np.arange(len(seg)) / rate
    v = np.abs(np.dot(seg * w, np.exp(-2j * np.pi * hz * t))) * 2 / w.sum()
    return 20 * np.log10(max(v, 1e-9))


def tones(x, rate, onset, n=12, lo=40.0, hi=8000.0, a=0.005, b=0.2):
    """The n strongest tones over a..b seconds after the attack, strongest first: (Hz, dB re the strongest, decay
    time constant in seconds). A tone is a peak that tops its neighbours within 4 % either side."""
    seg = x[onset + int(a * rate):onset + int(b * rate)]
    nf = 1 << 18
    mag = 20 * np.log10(np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nf)) + 1e-12)
    f = np.fft.rfftfreq(nf, 1 / rate)
    df = f[1]
    pk = []
    for i in np.nonzero((f > lo) & (f < hi))[0]:
        h = max(2, int(f[i] * 0.04 / df))
        if mag[i] == mag[i - h:i + h + 1].max():
            pk.append(i)
    pk = sorted(pk, key=lambda i: -mag[i])[:n]
    top = mag[pk[0]] if pk else 0.0
    out = []
    return [(float(f[i]), float(mag[i] - top), tone_decay(x, rate, onset, f[i], a)) for i in pk]


def tone_decay(x, rate, onset, hz, a=0.005):
    """A tone's decay time constant (s): its level in 40 ms frames, 10 ms apart, fitted while within 40 dB of its
    start."""
    ts = np.arange(a, 0.5, 0.01)
    lv = np.array([tone_level(x, rate, onset, hz, t0, t0 + 0.04) for t0 in ts])
    keep = lv > lv[0] - 40
    k = int(np.argmin(keep)) if not keep.all() else len(keep)
    if k < 3:
        return 0.001
    slope = np.polyfit(ts[:k], lv[:k], 1)[0]
    return float(-8.686 / slope) if slope < 0 else 9.9


def pitch_track(x, rate, onset, hz, ts=(0.01, 0.03, 0.06, 0.1, 0.15), win=0.04, floor=None):
    """The tone near hz, followed: its frequency in win-second frames starting at each of ts (seconds after the
    attack), searched within 30 % of hz; with floor (dB), only while the tone is within floor dB of its first frame's
    level (after that it is noise's). A list of (t, Hz)."""
    out, first = [], None
    for t0 in ts:
        seg = x[onset + int(t0 * rate):onset + int((t0 + win) * rate)]
        if len(seg) < 64:
            break
        nf = 1 << 17
        m = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nf))
        f = np.fft.rfftfreq(nf, 1 / rate)
        band = (f > hz * 0.7) & (f < hz * 1.3)
        i = int(m[band].argmax())
        level = 20 * np.log10(m[band][i] + 1e-12)
        first = level if first is None else first
        if floor is not None and level < first - floor:
            break
        out.append((t0, float(f[band][i])))
    return out


def measure(path):
    x, rate = read_wav(path)
    env = envelope_db(x, rate)
    peak = float(np.abs(x).max())
    onset = int(np.argmax(np.abs(x) > peak * 0.1))
    f0, cen = spectrum(x, rate, onset)
    d60 = decay_ms(env, 60)
    return {"spec": log_spectrum(x, rate, onset, d60), "peak": 20 * np.log10(max(peak, 1e-7)),
            "rms": 20 * np.log10(max(float(np.sqrt(np.mean(x[onset:onset + int(rate * 0.05)] ** 2))), 1e-7)),
            "d20": decay_ms(env, 20), "d40": decay_ms(env, 40), "d60": d60,
            "f0": f0, "cen": cen, "env": env, "rate": rate, "x": x, "onset": onset}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", type=Path)
    ap.add_argument("--chain", default="535-LA", help="which recording chain to measure (default 535-LA)")
    ap.add_argument("--out", type=Path, help="write the Markdown here instead of stdout")
    a = ap.parse_args()

    hits = {}
    for p in sorted(a.folder.rglob("*.wav")):
        m = NAME.match(p.name)
        if m and m["chain"] == a.chain:
            hits.setdefault(ALIAS.get(m["drum"], m["drum"]), {})[(bool(m["acc"]), int(m["step"]))] = p
    if not hits:
        sys.exit(f"no '<DRUM> via {a.chain} [A]<n>.wav' files under {a.folder}")

    out = [f"# Drum Something: reference measurements ({a.chain} chain)", "",
           "Made by `bs2/tools/dt_measure.py` from recordings kept outside the repo. Levels are dBFS (peak; RMS "
           "over the first 50 ms). Decays are from the attack to the last time the hit is above −20/−40/−60 dB. "
           "Rate is the playback-rate ratio to step 1 (unaccented): *spec* from lining up the log-frequency "
           "spectra (a varispeed sample's whole spectrum moves with its rate; the main figure), *env* from "
           "fitting the decay envelopes (the sample's length shrinks with its rate; a check: where the two "
           "disagree, the voice's length does not follow its rate, or aliasing confuses the spectra). Each step's "
           "*spec* is searched from the step before's (the steps only go up), an accent's near its plain hit's. "
           "Partial is the strongest one from 30 Hz to 8 kHz over the "
           "60 ms after the attack; centroid is over 0–20 kHz.", ""]
    summary = ["## Summary", "", "| Drum | Steps | Rate span (spec) | Rate span (env) | Semitones a step (spec) | "
               "Partial Hz (first..last) | Accent gain dB (mean) | d40 ms (first..last) |",
               "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    detail = []
    for drum in sorted(hits):
        print(f"measuring {drum} ({len(hits[drum])} hits)", file=sys.stderr)
        res = {k: measure(p) for k, p in sorted(hits[drum].items())}
        ref = res.get((False, 1)) or next(iter(res.values()))
        prev = 1.0                                   # the steps only go up: each searched from the one before
        for key in sorted(res, key=lambda k: (k[0], k[1])):
            r = res[key]
            if key[0] and (False, key[1]) in res:    # an accent: near its plain hit's rate
                near = res[(False, key[1])]["ratio"]
                r["ratio"] = spec_ratio(r["spec"], ref["spec"], near / 1.12, near * 1.12)
            elif r is ref:
                r["ratio"] = 1.0
            else:
                r["ratio"] = prev = spec_ratio(r["spec"], ref["spec"], prev * 0.97, prev * 1.6)
            r["eratio"] = rate_ratio(r["env"], ref["env"])
        detail += [f"## {drum}", "", "| Step | Acc | Peak | RMS | d20 ms | d40 ms | d60 ms | Rate spec | Rate env "
                   "| Partial Hz | Centroid Hz |",
                   "| ---: | :-: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
        for (acc, step), r in res.items():
            detail.append(f"| {step} | {'A' if acc else ''} | {r['peak']:.1f} | {r['rms']:.1f} | {r['d20']:.0f} | "
                          f"{r['d40']:.0f} | {r['d60']:.0f} | {r['ratio']:.3f} | {r['eratio']:.3f} | {r['f0']:.0f} | "
                          f"{r['cen']:.0f} |")
        detail.append("")
        rows = [r for (acc, _), r in sorted(res.items()) if not acc]
        plain = [r["ratio"] for r in rows]
        steps = len(plain)
        geo = (plain[-1] / plain[0]) ** (1 / (steps - 1)) if steps > 1 else 1.0
        gains = [res[(True, s)]["rms"] - res[(False, s)]["rms"] for (acc, s) in res if acc and (False, s) in res]
        summary.append(f"| {drum} | {steps} | {min(plain):.2f}..{max(plain):.2f} | {rows[0]['eratio']:.2f}.."
                       f"{rows[-1]['eratio']:.2f} | {12 * np.log2(geo):+.2f} | "
                       f"{rows[0]['f0']:.0f}..{rows[-1]['f0']:.0f} | "
                       f"{np.mean(gains) if gains else float('nan'):+.1f} | "
                       f"{rows[0]['d40']:.0f}..{rows[-1]['d40']:.0f} |")
    text = "\n".join(out + summary + [""] + detail)
    if a.out:
        a.out.write_text(text)
        print(f"wrote {a.out}", file=sys.stderr)
    else:
        print(text)


if __name__ == "__main__":
    main()
