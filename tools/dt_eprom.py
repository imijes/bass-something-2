#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 John Brown
"""Make Drum Something's virtual EPROMs: each voice run once by the host simulator's dt_gen, as the 8-bit numbers its DAC
would be given, in ROM time. Writes bs2/eprom/NAME.wav (8-bit, at the voice's ROM rate at the first tuning step, to listen
to) and bs2/dt_eprom.h (the firmware's tables). The zeros at a voice's end are cut.

  tools/build.py bs2 --host, then from the fm1-base root: python3 bs2/tools/dt_eprom.py BASS SNARE:lowonly RIM TOM1:lowonly TOM2 CRASH@2.5 RIDE@2.5 CHH OHH CLAPS TAMB COWBELL CABASA
  (NAME:low also makes the take its low tunings use: steps 1-3 differ from the rest where a voice has low fields;
  NAME:lowonly keeps that take alone, read faster for every tuning as one ROM would be: the Snare's and the Toms' since
  v0.7.3, 22.7 KB less, the firmware 3 dB down at steps 4 on (DT_EPROM_LOWONLY) to keep their old level there),
  NAME@SECONDS cuts a take there, faded out over its last 0.4 s (the cymbals' tails since v0.7.3: CRASH@2.5 RIDE@2.5),
  then tools/build.py bs2 --host again."""
import math
import re
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

BS2 = Path(__file__).resolve().parent.parent
HOST = BS2 / "build/host/bass-something-2_host"
ORDER = "BASS SNARE RIM TOM1 TOM2 CRASH RIDE CHH OHH CLAPS TAMB COWBELL CABASA".split()
ALIAS = {"TOM2": "TOM1", "CHH": "OHH"}      # (one voice, one ROM: the closed hat's cut is live, its VCA)


def rom_rates():
    """each voice's ROM rate from the DT_V_* it is built with (.rom = ...), through its #define"""
    src = (BS2 / "bs2_drum.c").read_text()
    out = {}
    for name in ORDER:
        m = re.search(r"DT_V_%s\b.*?\.rom = (\w+)" % ("HAT_" if name in ("CHH", "OHH") else "TOM" if name.startswith("TOM") else name), src, re.S)
        d = re.search(r"#define %s ([\d.]+)f?" % m.group(1), src) if m else None
        out[name] = int(float(d.group(1))) if d else 17400
    return out


def export(name, step, tmp):
    raw = Path(tmp) / f"{name}_{step}.raw"
    script = Path(tmp) / "x.txt"
    script.write_text(f"wait 600\ndt_export {name} {step} {raw.name}\n")   # (relative: the host's words are short)
    subprocess.run([HOST, script.name, "."], check=True, stdout=subprocess.DEVNULL, cwd=tmp)
    data = raw.read_bytes().rstrip(b"\0")
    return data


FADE = 0.4                                   # NAME@SECONDS: the take cut there, faded out over its last FADE s


def fade(d, secs, rate):
    """a take cut at SECS, its last FADE s faded out (a raised cosine), as 8-bit numbers again"""
    n, f = min(len(d), int(secs * rate)), int(FADE * rate)
    v = [b - 256 if b > 127 else b for b in d[:n]]
    for i in range(max(0, n - f), n):
        v[i] = round(v[i] * 0.5 * (1 + math.cos(math.pi * (i - (n - f)) / f)))
    return bytes(x & 255 for x in v).rstrip(b"\0")


def main():
    args = [re.fullmatch(r"(\w+)(?::(\w+))?(?:@([\d.]+))?", a).groups() for a in sys.argv[1:]]
    wanted = {n: k or "" for n, k, _ in args}
    cut = {n: float(t) for n, _, t in args if t}
    lowonly = 0
    rates = rom_rates()
    (BS2 / "eprom").mkdir(exist_ok=True)
    arrays, rows, total = [], [], 0
    with tempfile.TemporaryDirectory() as tmp:
        for name in ORDER:
            if name not in wanted:
                rows.append("{{0, 0}, {0, 0}}")
                continue
            if name in ALIAS:                # (the same voice: the same ROM, filled in below)
                rows.append(name)
                continue
            takes = {"": [("", 8)], "low": [("", 8), ("_LOW", 1)], "lowonly": [("_LOW", 1)]}[wanted[name]]
            if wanted[name] == "lowonly":
                lowonly |= 1 << ORDER.index(name)
                lowonly |= sum(1 << ORDER.index(a) for a, b in ALIAS.items() if b == name)
            row = []
            for suffix, step in takes:
                d = export(name, step, tmp)
                if name in cut:
                    d = fade(d, cut[name], rates[name])
                total += len(d)
                with wave.open(str(BS2 / "eprom" / f"{name}{suffix}.wav"), "wb") as w:
                    w.setnchannels(1)
                    w.setsampwidth(1)
                    w.setframerate(rates[name])
                    w.writeframes(bytes((b + 128) & 255 for b in d))
                vals = ",".join(str(b - 256 if b > 127 else b) for b in d)
                arrays.append(f"static const int8_t DT_E_{name}{suffix}[{len(d)}] = {{{vals}}};")
                row.append(f"{{DT_E_{name}{suffix}, {len(d)}u}}")
                print(f"{name}{suffix}: {len(d)} B, {len(d) / rates[name]:.2f} s at {rates[name]} Hz")
            rows.append("{" + ", ".join(row + ["{0, 0}"] * (2 - len(row))) + "}")
    rows = [next(r for r in rows if re.search(rf"DT_E_{ALIAS[n]}(_LOW)?,", r)) if n in ALIAS else n for n in rows]
    (BS2 / "dt_eprom.h").write_text(
        "/* SPDX-License-Identifier: MPL-2.0\n * Copyright (C) 2026 John Brown */\n"
        "/* Drum Something's virtual EPROMs, made from its own voices by tools/dt_eprom.py: don't edit. */\n"
        + "\n".join(arrays) + "\nstatic const struct { const int8_t *d; uint32_t n; } DT_EPROM[][2] = {\n    "
        + ",\n    ".join(rows) + "};\n"
        + f"#define DT_EPROM_LOWONLY 0x{lowonly:04X}u   /* (the voices with their low take alone: :lowonly) */\n")
    print(f"total {total} B")


main()
