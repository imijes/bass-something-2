#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 John Brown
"""The Bass Station II test kit: patches to play on a real Bass Station II and record, so Bass Something 2 can be calibrated
against it (the key tracking's pivot, the cutoff's curve, resonance, envelope times, levels, drive, distortion, glide,
LFO rates).

From one list of takes (TAKES below) it writes:
  syx/TKnn_NAME.syx   each take's patch, a Bass Station II dump (154 bytes, named), built on the INIT patch
  fm1_sim.txt         the host simulator's script: each patch loaded and played as the guide says, a WAV a take
  SESSION.md          its table of takes (between its <!-- takes --> marks), the rest of it as written

The dumps' fields and the INIT patch are read from ../bs2_sysex.c, so the kit and the FM-1's import agree. Every
take sets both velocities to 0 (VELOCITY AMP ENV and MOD ENV at 64): how hard a key is played changes nothing.

  python3 bs2/testkit/make_kit.py          (from the repository's root)
then the FM-1's side:
  bs2/build/host/bass-something-2_host bs2/testkit/fm1_sim.txt bs2/build/testkit
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "bs2_sysex.c")
NAME_AT, NAME_N, LEN = 137, 16, 154


def read_layout():
    src = open(SRC).read()
    tab = {m[0]: (int(m[1]), int(m[2])) for m in re.findall(r"\[(BX_\w+)\]\s*=\s*\{(\d+),\s*(\d+)\}", src)}
    init = src[src.index("BSX_INIT[BSX_MIN - 1u] = {"):]
    init = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", init[: init.index("};")])]
    return tab, init


TAB, INIT = read_layout()


def put(d, field, v):
    pos, w = TAB["BX_" + field]
    assert 0 <= v < 1 << w, (field, v)
    for i in range(w):
        p = pos + i
        b = 1 << (6 - p % 7)
        d[p // 7] = d[p // 7] | b if v >> (w - 1 - i) & 1 else d[p // 7] & ~b


def c7(a):  # a bipolar 7-bit value (-63..63) as the Bass Station II stores it
    return a + 63 if a < 0 else a + 64


def c8(a):  # an 8-bit one (-127..127)
    return a + 127 if a < 0 else a + 128


# oscillator 2 a fifth up: 7 semitones in the dump's coarse tune (bs2_sysex.c bsx_coarse's)
FIFTH = 128 + (7 * 255 + 12) // 24
NOTES = {"C1": 24, "C2": 36, "C3": 48, "C4": 60, "C5": 72}
QUIET = dict(LVL1=0, LVL2=0, LVLSUB=0, NOISE=0)          # every source off
MARK = dict(W1=0, LVL1=24)                                # a faint sine at the note: which key it was, in a recording
PLAIN = dict(VAMP=64, VMOD=64, AA=0, AD=0, AS=127, AR=20)  # velocity off; the note on at once, a short release

# a take: its number, its name (on the BS2's dump, 12 characters at most after "TKnn "), what it measures, its fields
# over the INIT patch, and what to play: (note, held s, then silence s), or ("legato", low, high, ...) for the glide
TAKES = [
    (1, "RESO TRACK", "the key tracking's pivot and slope: the filter ringing alone, its pitch at each C",
     dict(QUIET, **MARK, RESO=127, FREQ=128, TRACK=0), [("C1", 3, 1), ("C2", 3, 1), ("C3", 3, 1), ("C4", 3, 1), ("C5", 3, 1)]),
    (2, "RESO NOISE", "the same with noise through it (if take 1 is silent: the filter does not ring alone)",
     dict(QUIET, **MARK, NOISE=128, RESO=110, FREQ=128, TRACK=0), [("C1", 3, 1), ("C2", 3, 1), ("C3", 3, 1), ("C4", 3, 1), ("C5", 3, 1)]),
    (3, "RESO NO TRK", "no tracking: the ring's pitch the same on every key (the cutoff as set)",
     dict(QUIET, **MARK, RESO=127, FREQ=128, TRACK=7), [("C2", 3, 1), ("C4", 3, 1)]),
    (4, "CUT 0", "the cutoff's curve: noise through a resonant filter, the dial at 0 (of 255 in the dump)",
     dict(QUIET, NOISE=255, RESO=100, FREQ=0, TRACK=7), [("C3", 3, 1)]),
    (5, "CUT 64", "the same, the dial at a quarter", dict(QUIET, NOISE=255, RESO=100, FREQ=64, TRACK=7), [("C3", 3, 1)]),
    (6, "CUT 128", "the same, at half", dict(QUIET, NOISE=255, RESO=100, FREQ=128, TRACK=7), [("C3", 3, 1)]),
    (7, "CUT 192", "the same, at three quarters", dict(QUIET, NOISE=255, RESO=100, FREQ=192, TRACK=7), [("C3", 3, 1)]),
    (8, "CUT 255", "the same, fully open", dict(QUIET, NOISE=255, RESO=100, FREQ=255, TRACK=7), [("C3", 3, 1)]),
    (9, "SAW OPEN", "the level reference: oscillator 1's saw, the filter open",
     dict(TRACK=7), [("C2", 3, 1), ("C4", 3, 1)]),
    (10, "SAW CUT", "the filter's slope and tone: the saw under a low cutoff", dict(FREQ=100, TRACK=7), [("C2", 3, 1)]),
    (11, "SUB SQUARE", "the sub's level and shape: its square alone, an octave down",
     dict(QUIET, LVLSUB=255, SUBWAVE=2, SUBOCT=1, TRACK=7), [("C2", 3, 1)]),
    (12, "SUB SINE", "its sine alone", dict(QUIET, LVLSUB=255, SUBWAVE=0, SUBOCT=1, TRACK=7), [("C2", 3, 1)]),
    (13, "TWO SAWS", "both saws, oscillator 2 seven cents up: their level together, their beating",
     dict(LVL2=255, FINE2=c8(7), TRACK=7), [("C2", 6, 1)]),
    (14, "DRIVE 64", "the filter's drive at half, the saw", dict(DRIVE=64, TRACK=7), [("C2", 3, 1)]),
    (15, "DRIVE 127", "and at full", dict(DRIVE=127, TRACK=7), [("C2", 3, 1)]),
    (16, "DIST 64", "the distortion at half", dict(DIST=64, TRACK=7), [("C2", 3, 1)]),
    (17, "DIST 127", "and at full", dict(DIST=127, TRACK=7), [("C2", 3, 1)]),
    (18, "AMP ENV MID", "the amp envelope's times: every knob at half, a note held 4 s",
     dict(AA=64, AD=64, AS=64, AR=64, TRACK=7), [("C2", 4, 4)]),
    (19, "AMP SNAP", "a short envelope's snap: four short notes",
     dict(AA=20, AD=40, AS=0, AR=30, TRACK=7), [("C2", 0.3, 1)] * 4),
    (20, "FILTER ENV", "the mod envelope on the cutoff: its sweep and times",
     dict(FREQ=40, RESO=60, MEFILT=c7(40), MA=30, MD=70, MS=0, MR=40, TRACK=7), [("C2", 3, 1), ("C2", 3, 1)]),
    (21, "GLIDE 64", "the glide's time: C2 held, C4 pressed while it is, then C4 let go, then C2",
     dict(GLIDE=64, TRACK=7), [("legato", "C2", "C4")]),
    (22, "LFO SLOW", "LFO 1's rate at half: a wide vibrato on oscillator 1",
     dict(L1SPEED=128, L1OSC1=c8(60), L1WAVE=0, TRACK=7), [("C3", 6, 1)]),
    (23, "LFO FAST", "and faster", dict(L1SPEED=220, L1OSC1=c8(60), L1WAVE=0, TRACK=7), [("C3", 4, 1)]),
    (24, "ACID RESO", "the Acid filter under resonance, the saw", dict(TYPE=1, RESO=100, FREQ=100, TRACK=7),
     [("C2", 3, 1)]),
    (25, "RING", "the ring modulator alone: the saws a fifth apart",
     dict(QUIET, RING=255, COARSE2=FIFTH, TRACK=7), [("C3", 3, 1)]),
]


def build(num, name, fields):
    d = (INIT + [0] * LEN)[:LEN]
    for k, v in {**PLAIN, **fields}.items():
        put(d, k, v)
    title = ("TK%02d %s" % (num, name))[:NAME_N].ljust(NAME_N)
    for i, ch in enumerate(title):
        d[NAME_AT + i] = ord(ch) & 0x7F
    d[LEN - 1] = 0xF7
    return bytes(d), title.rstrip()


def play_text(play):
    out = []
    for p in play:
        if p[0] == "legato":
            out.append("hold %s; while held, %s for 2 s; let %s go (1 s), then %s" % (p[1], p[2], p[2], p[1]))
        else:
            n, held, rest = p
            out.append("%s %s s" % (n, ("%g" % held)))
    if len(set(out)) == 1 and len(out) > 1:
        return "%s, %d times (1 s apart)" % (out[0], len(out))
    return ", ".join(out)


def sim_lines(play):
    out = []
    for p in play:
        if p[0] == "legato":
            lo, hi = NOTES[p[1]], NOTES[p[2]]
            out += ["midi 90 %02X 64" % lo, "wait 1500", "midi 90 %02X 64" % hi, "wait 2000",
                    "midi 80 %02X 00" % hi, "wait 2000", "midi 80 %02X 00" % lo, "wait 1500"]
        else:
            n, held, rest = p
            out += ["midi 90 %02X 64" % NOTES[n], "wait %d" % int(held * 1000), "midi 80 %02X 00" % NOTES[n],
                    "wait %d" % int(rest * 1000)]
    return out


def main():
    os.makedirs(os.path.join(HERE, "syx"), exist_ok=True)
    sim = ["# Bass Something 2's side of the Bass Station II test kit (written by make_kit.py: edit that, not this)",
           "# from the repository's root:",
           "#   bs2/build/host/bass-something-2_host bs2/testkit/fm1_sim.txt bs2/build/testkit",
           "wait 600", "listen"]
    rows = ["| Take | Patch (its name on the BS2) | Play | What it measures |", "| --- | --- | --- | --- |"]
    for num, name, what, fields, play in TAKES:
        data, title = build(num, name, fields)
        fn = "TK%02d_%s.syx" % (num, name.replace(" ", "_"))
        open(os.path.join(HERE, "syx", fn), "wb").write(data)
        rows.append("| %d | `%s` (%s) | %s | %s |" % (num, fn, title, play_text(play), what))
        sim += ["# take %d: %s" % (num, what),
                "sysex_in bs2/testkit/syx/%s" % fn, "wait 30", "said SYSEX LOADED %s" % title, "wait 270",
                "wav TK%02d.wav" % num]
        sim += sim_lines(play)
        sim += ["wavstop", "wait 300"]
    open(os.path.join(HERE, "fm1_sim.txt"), "w").write("\n".join(sim) + "\n")
    guide = os.path.join(HERE, "SESSION.md")
    g = open(guide).read()
    a, b = g.index("<!-- takes -->") + len("<!-- takes -->"), g.index("<!-- /takes -->")
    open(guide, "w").write(g[:a] + "\n" + "\n".join(rows) + "\n" + g[b:])
    print("%d takes: syx/, fm1_sim.txt, SESSION.md's table" % len(TAKES))


if __name__ == "__main__":
    main()
