#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 John Brown
"""Bass Something 2's own factory patches: 32 sounds written here, as Bass Something 2's patch settings (app.h
patch_t's fields and ranges), into a Bass Station II bank file, bs2/presets/factory.syx (patch 0 into P01 ... 31 into
P32). Each dump is written as bs2_sysex.c's bsx_export writes one: its field places and the INIT PATCH it starts from
are read from that file, so the two cannot drift apart. Then make_presets.py turns factory.syx into presets.h:

    python3 bs2/presets/make_factory.py && python3 bs2/presets/make_presets.py

P01-P10 basses, P11-P20 leads, P21-P32 the rest of what the voice does (the arpeggiator, sample and hold, the ring,
Osc Filter Mod, the envelopes' retriggering, the filter's band and high pass, noise). What a dump has no place for
(the tempo, the gate, the sequence) a patch gets from the defaults when it is read."""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = (HERE.parent / "bs2_sysex.c").read_text()

# app.h's enums
SAW, SQUARE, SINE, TRI, PULSE = range(5)
SUB_SINE, SUB_PULSE, SUB_SQUARE = range(3)
R16, R8, R4, R2 = range(4)
LP24, LP12, BP24, BP12, HP24, HP12, ACID = range(7)
L_TRI, L_SAW, L_SQUARE, L_SH = range(4)
MULTI, SINGLE, AUTOGLIDE = range(3)
UP, DOWN, UPDOWN, UPDOWN2, PLAYED, RANDOM = range(6)
SYNC_HALF, SYNC_QUARTER, SYNC_8T, SYNC_8TH, SYNC_16TH = 22, 25, 27, 28, 31   # SYNC_TICKS: 48, 24, 16, 12, 6

# app.c patch_defaults: what a patch below does not set
DEFAULTS = dict(
    wave1=SAW, wave2=SAW, coarse1=0, fine1=0, coarse2=0, fine2=7, range1=R8, range2=R8, sync=0,
    pw1=50, pw2=50, pwm1=0, pwm2=0, sub_oct=1, sub_wave=SUB_SQUARE,
    lvl1=127, lvl2=127, lvl_sub=89, ring=0, noise=0,
    filter=LP24, cutoff=100, reso=20, drive=0, key_trk=7, ofm=0, dist=0,
    menv_a=0, menv_d=70, menv_s=0, menv_r=50, menv_cut=0, menv_o1=0, menv_o2=0, menv_pw1=0, menv_pw2=0,
    amp_a=14, amp_d=75, amp_s=89, amp_r=64, vel_amp=63, vel_menv=0,
    env_trig=(MULTI, MULTI), env_loop=(0, 0),
    lfo1_wave=L_TRI, lfo1_rate=75, lfo1_o1=0, lfo1_o2=0,
    lfo2_wave=L_TRI, lfo2_rate=52, lfo2_cut=0,
    lfo_delay=(0, 0), lfo_slew=(0, 0), lfo_keysync=(0, 1), lfo_sync=(0, 0), lfo_syncv=(0, 0),
    glide=0, arp_on=0, arp_mode=UP, arp_oct=1, latch=0, rhythm=32, swing=50,
)

PATCHES = [
    # ---- P01-P10: basses ----
    ("SUB ZERO", dict(        # a sine an octave down and the sub's under it: felt more than heard
        wave1=SINE, range1=R16, lvl2=0, sub_wave=SUB_SINE, lvl_sub=110, cutoff=50, reso=0, key_trk=4,
        amp_a=4, amp_d=60, amp_s=110, amp_r=40, vel_amp=30, drive=10)),
    ("STATION BASS", dict(    # the classic: two saws at 16', the square sub, the mod envelope on a low cutoff
        range1=R16, range2=R16, fine2=9, sub_wave=SUB_SQUARE, lvl_sub=100, cutoff=34, reso=35,
        menv_d=58, menv_s=25, menv_r=40, menv_cut=34, amp_a=2, amp_d=70, amp_s=110, amp_r=36, drive=20)),
    ("ACID SLIDE", dict(      # the Acid filter, glide on the legato notes: play it overlapping
        wave1=SAW, lvl2=0, range1=R16, lvl_sub=40, filter=ACID, cutoff=36, reso=110, drive=45,
        menv_d=52, menv_s=0, menv_r=30, menv_cut=40, vel_menv=30, amp_a=0, amp_d=80, amp_s=100, amp_r=20,
        glide=34, env_trig=(AUTOGLIDE, AUTOGLIDE))),
    ("RUBBER PLUCK", dict(    # a narrow pulse through the 12 dB filter, short and round
        wave1=PULSE, pw1=28, wave2=SINE, range1=R16, range2=R16, lvl2=70, lvl_sub=90, sub_wave=SUB_SINE,
        filter=LP12, cutoff=30, reso=62, menv_d=44, menv_s=0, menv_r=36, menv_cut=36,
        amp_a=0, amp_d=74, amp_s=0, amp_r=48)),
    ("REESE TUNNEL", dict(    # two saws far apart beating, the cutoff breathing slowly
        range1=R16, range2=R16, fine2=-22, fine1=4, lvl_sub=70, cutoff=52, reso=28, dist=24,
        lfo1_wave=L_TRI, lfo1_rate=18, lfo1_o2=2, lfo2_wave=L_TRI, lfo2_rate=14, lfo2_cut=10,
        lfo_keysync=(0, 0), amp_a=6, amp_d=80, amp_s=120, amp_r=50, glide=12, env_trig=(SINGLE, SINGLE))),
    ("FM GROWL", dict(        # oscillator 2 on the cutoff at audio rate: Osc Filter Mod's snarl
        wave1=SAW, wave2=SINE, range1=R16, range2=R8, coarse2=7, lvl2=40, lvl_sub=80, cutoff=46, reso=58,
        ofm=72, drive=40, menv_d=60, menv_s=40, menv_cut=22, amp_a=2, amp_d=70, amp_s=115, amp_r=34,
        lfo2_wave=L_TRI, lfo2_rate=60, lfo2_cut=12)),
    ("WOBBLE DUB", dict(      # LFO 2 on the cutoff in eighths of the tempo (or MIDI clock)
        wave1=SAW, wave2=PULSE, pw2=40, range1=R16, range2=R16, fine2=5, lvl_sub=100, cutoff=24, reso=74,
        drive=55, lfo2_wave=L_TRI, lfo2_cut=40, lfo_sync=(0, 1), lfo_syncv=(0, SYNC_8TH), lfo_keysync=(0, 1),
        amp_a=2, amp_d=80, amp_s=120, amp_r=36)),
    ("SYNC PUNCH", dict(      # oscillator 2 hard-synced to 1, its pitch swept down by the mod envelope
        sync=1, wave1=SAW, wave2=SAW, range1=R16, range2=R8, lvl1=60, lvl2=127, lvl_sub=100,
        menv_d=46, menv_s=0, menv_r=30, menv_o2=38, menv_cut=18, cutoff=64, reso=20,
        amp_a=0, amp_d=66, amp_s=96, amp_r=30, drive=30)),
    ("GRIT BASS", dict(       # squares and the sub through the drive and the distortion
        wave1=PULSE, pw1=50, wave2=PULSE, pw2=50, range1=R16, range2=R8, fine2=-6, lvl2=60, lvl_sub=110,
        cutoff=44, reso=40, drive=100, dist=55, menv_d=50, menv_s=20, menv_cut=24,
        amp_a=0, amp_d=70, amp_s=110, amp_r=30)),
    ("BOOM 808", dict(        # a sine whose pitch falls into the note, a long decay: a tuned kick
        wave1=SINE, range1=R16, lvl2=0, lvl_sub=60, sub_wave=SUB_SINE, cutoff=70, reso=0, key_trk=0,
        menv_a=0, menv_d=36, menv_s=0, menv_r=20, menv_o1=18, amp_a=0, amp_d=96, amp_s=0, amp_r=80,
        drive=35, vel_amp=40)),

    # ---- P11-P20: leads ----
    ("BRIGHT SAWS", dict(     # two saws open, glide on the legato notes, vibrato coming in
        fine2=11, lvl_sub=40, cutoff=96, reso=30, menv_d=60, menv_s=60, menv_cut=12,
        amp_a=8, amp_d=80, amp_s=115, amp_r=60, glide=26, env_trig=(AUTOGLIDE, AUTOGLIDE),
        lfo1_rate=80, lfo1_o1=2, lfo1_o2=2, lfo_delay=(80, 0), lfo_keysync=(1, 1))),
    ("SQUARE SOLO", dict(     # a square and one an octave up, hollow, vibrato coming in
        wave1=PULSE, pw1=50, wave2=PULSE, pw2=50, range2=R4, lvl2=60, lvl_sub=0, filter=LP12, cutoff=86,
        reso=24, amp_a=6, amp_d=80, amp_s=120, amp_r=50, glide=16, env_trig=(AUTOGLIDE, AUTOGLIDE),
        lfo1_rate=78, lfo1_o1=2, lfo1_o2=2, lfo_delay=(70, 0), lfo_keysync=(1, 1))),
    ("SYNC SCREAMER", dict(   # hard sync, the mod envelope on oscillator 2's pitch: the scream
        sync=1, range2=R4, lvl1=40, lvl2=127, lvl_sub=30, cutoff=88, reso=34,
        menv_a=10, menv_d=76, menv_s=50, menv_r=60, menv_o2=34, vel_menv=30,
        amp_a=4, amp_d=80, amp_s=115, amp_r=56, lfo1_rate=70, lfo1_o2=3, lfo_delay=(60, 0), glide=14)),
    ("PWM LEAD", dict(        # two pulses, their widths moved by LFO 2 in opposite ways
        wave1=PULSE, pw1=40, wave2=PULSE, pw2=60, fine2=6, lvl_sub=30, pwm1=30, pwm2=-30,
        lfo2_wave=L_TRI, lfo2_rate=38, lfo_keysync=(0, 0), cutoff=90, reso=26,
        amp_a=6, amp_d=80, amp_s=115, amp_r=56, glide=10)),
    ("ACID SCREAM", dict(     # the Acid filter high and close to howling, driven, distorted
        wave1=SAW, lvl2=0, lvl_sub=20, filter=ACID, cutoff=58, reso=112, drive=72, dist=40,
        menv_d=60, menv_s=30, menv_cut=24, vel_menv=30, amp_a=0, amp_d=80, amp_s=110, amp_r=40,
        glide=28, env_trig=(AUTOGLIDE, AUTOGLIDE))),
    ("FIFTH ELEMENT", dict(   # oscillator 2 a fifth up: every note a power chord
        coarse2=7, fine2=4, lvl2=100, lvl_sub=50, cutoff=80, reso=40, drive=30,
        menv_d=62, menv_s=40, menv_cut=16, amp_a=4, amp_d=80, amp_s=115, amp_r=50, glide=12)),
    ("RING LEAD", dict(       # the oscillators' ring modulation: a lead with a metallic edge
        wave1=TRI, wave2=SAW, coarse2=5, fine2=0, ring=96, lvl_sub=30, cutoff=92, reso=26,
        amp_a=4, amp_d=80, amp_s=110, amp_r=56, lfo1_rate=76, lfo1_o1=2, lfo_delay=(70, 0), glide=14)),
    ("PORTA SINE", dict(      # a soft sine and triangle that always glide, slowly
        wave1=SINE, wave2=TRI, range2=R4, lvl2=50, lvl_sub=0, cutoff=80, reso=10,
        amp_a=20, amp_d=80, amp_s=120, amp_r=70, glide=70,
        lfo1_rate=72, lfo1_o1=2, lfo1_o2=2, lfo_delay=(90, 0), lfo_keysync=(1, 1))),
    ("HOT WIRE", dict(        # a saw and a pulse pushed hard through the drive and the distortion
        wave2=PULSE, pw2=30, fine2=8, lvl_sub=50, cutoff=82, reso=56, drive=90, dist=80,
        menv_d=56, menv_s=50, menv_cut=14, amp_a=2, amp_d=80, amp_s=115, amp_r=46, glide=18,
        env_trig=(AUTOGLIDE, AUTOGLIDE))),
    ("BREATHY FLUTE", dict(   # a triangle with noise in it, a slow attack, vibrato coming in
        wave1=TRI, wave2=SINE, range2=R4, lvl2=30, lvl_sub=0, noise=22, filter=LP12, cutoff=72, reso=10,
        amp_a=46, amp_d=80, amp_s=115, amp_r=60, lfo1_rate=74, lfo1_o1=2, lfo1_o2=2, lfo_delay=(76, 0),
        lfo_keysync=(1, 1), vel_amp=40)),

    # ---- P21-P32: the rest of the voice ----
    ("ARP PLUCKS", dict(      # the arpeggiator, up over two octaves in a rhythm: hold a chord
        wave1=PULSE, pw1=36, lvl2=60, lvl_sub=50, filter=LP12, cutoff=48, reso=50, menv_d=40, menv_s=0,
        menv_cut=34, amp_a=0, amp_d=60, amp_s=0, amp_r=44, arp_on=1, arp_mode=UP, arp_oct=2, rhythm=8)),
    ("ARP RANDOMIZER", dict(  # random notes over three octaves, LFO 2's sample and hold on the cutoff, latched
        wave1=SAW, wave2=PULSE, pw2=30, fine2=6, lvl_sub=40, filter=LP24, cutoff=50, reso=70,
        lfo2_wave=L_SH, lfo2_cut=26, lfo_sync=(0, 1), lfo_syncv=(0, SYNC_16TH), lfo_keysync=(0, 0),
        amp_a=0, amp_d=52, amp_s=0, amp_r=36, arp_on=1, arp_mode=RANDOM, arp_oct=3, latch=1, rhythm=32)),
    ("S&H COMPUTER", dict(    # sample and hold on the cutoff with the resonance high: a 70s computer
        wave1=PULSE, pw1=50, lvl2=0, lvl_sub=0, filter=LP12, cutoff=58, reso=104, key_trk=7,
        lfo2_wave=L_SH, lfo2_rate=96, lfo2_cut=34, lfo_keysync=(0, 0),
        amp_a=0, amp_d=80, amp_s=120, amp_r=30)),
    ("SLOW SWEEP PAD", dict(  # the saws apart, a long attack and release, the cutoff swept by LFO 2
        range1=R8, range2=R8, fine2=14, lvl_sub=50, cutoff=52, reso=56,
        menv_a=96, menv_d=100, menv_s=60, menv_r=96, menv_cut=20,
        amp_a=86, amp_d=100, amp_s=115, amp_r=96, lfo2_wave=L_TRI, lfo2_rate=8, lfo2_cut=24,
        lfo1_rate=22, lfo1_o2=1, lfo_keysync=(0, 0), vel_amp=20)),
    ("LASER ZAP", dict(       # the mod envelope drops the pitch an octave into the note: pew
        wave1=SAW, wave2=PULSE, pw2=20, lvl_sub=0, cutoff=110, reso=40, key_trk=0,
        menv_a=0, menv_d=66, menv_s=0, menv_r=56, menv_o1=63, menv_o2=63, menv_cut=-20,
        amp_a=0, amp_d=82, amp_s=0, amp_r=60)),
    ("RING BELLS", dict(      # the ring of two oscillators a fifth and some cents apart, decaying like a bell
        wave1=TRI, wave2=SINE, range1=R4, coarse2=7, fine2=13, ring=127, lvl_sub=0, filter=LP12,
        cutoff=106, reso=0, amp_a=0, amp_d=100, amp_s=0, amp_r=96, vel_amp=50)),
    ("WINDY NOISE", dict(     # noise alone through a resonant band pass, LFO 2 moving it: hold a key
        lvl1=0, lvl2=0, lvl_sub=0, noise=127, filter=BP24, cutoff=56, reso=104, key_trk=7,
        lfo2_wave=L_TRI, lfo2_rate=10, lfo2_cut=10, lfo_slew=(0, 60), lfo_keysync=(0, 0),
        amp_a=80, amp_d=100, amp_s=120, amp_r=100)),
    ("STUTTER GATE", dict(    # the amp envelope retriggering at its decay's end: the note chopped
        fine2=9, lvl_sub=60, cutoff=70, reso=40, menv_d=56, menv_s=0, menv_cut=20, env_loop=(1, 1),
        amp_a=0, amp_d=56, amp_s=0, amp_r=40)),
    ("FM METAL", dict(        # Osc Filter Mod all the way, oscillator 2 at an odd interval: clangs
        wave1=TRI, wave2=SINE, range2=R4, coarse2=-1, fine2=30, lvl1=127, lvl2=20, lvl_sub=0, filter=BP12,
        cutoff=70, reso=60, ofm=127, drive=40, amp_a=0, amp_d=90, amp_s=20, amp_r=80, vel_menv=0)),
    ("HIGHPASS PLUCK", dict(  # the high pass filter thinning a saw, plucked by the mod envelope
        wave1=SAW, wave2=PULSE, pw2=30, fine2=10, lvl_sub=0, filter=HP12, cutoff=46, reso=60,
        menv_d=40, menv_s=0, menv_cut=30, amp_a=0, amp_d=72, amp_s=0, amp_r=52)),
    ("SIREN SWEEP", dict(     # LFO 1 on both oscillators' pitches, slowly: a siren on every key
        wave1=SAW, wave2=PULSE, pw2=50, range2=R4, lvl2=60, lvl_sub=0, cutoff=90, reso=30,
        lfo1_wave=L_TRI, lfo1_rate=24, lfo1_o1=14, lfo1_o2=14, lfo_keysync=(1, 0),
        amp_a=4, amp_d=80, amp_s=120, amp_r=56)),
    ("TALKING WAH", dict(     # the band pass opening and closing with the mod envelope: a wah on each note
        wave1=SAW, wave2=SAW, fine2=8, lvl_sub=60, filter=BP24, cutoff=36, reso=90, drive=30,
        menv_a=40, menv_d=70, menv_s=10, menv_r=60, menv_cut=40, amp_a=4, amp_d=80, amp_s=115, amp_r=50)),
]


def c_table():
    """bs2_sysex.c's field table (BX: name -> (first bit, width)) and its INIT PATCH (BSX_INIT's bytes)"""
    bx = {m[1]: (int(m[2]), int(m[3])) for m in re.finditer(r"\[(BX_\w+)\]\s*=\s*\{(\d+),\s*(\d+)\}", SRC)}
    init = re.search(r"BSX_INIT\[[^\]]*\]\s*=\s*\{(.*?)\};", SRC, re.S)[1]
    init = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", init)]
    if len(bx) != 72 or len(init) != 121:
        raise SystemExit(f"bs2_sysex.c: {len(bx)} fields, an INIT PATCH of {len(init)} bytes (72, 121 expected)")
    return bx, init


BX, INIT = c_table()
LEN, NAME_AT, NAME_N, TO, STORE = 154, 137, 16, 7, 0x01
WAVE_OUT = [2, 3, 0, 1, 3]                     # bsx_export's BSX_WAVE_OUT (the square a pulse at 50 %)


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def cdiv(a, b):                                # C's division, toward 0
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def u7(a): return clamp(a + 63 if a < 0 else a + 64, 0, 127)
def u8(a): return clamp(a + 127 if a < 0 else a + 128, 0, 255)
def wide(v): return v * 2 + (v >> 6)
def pw_raw(pc): return ((pc - 5) * 127 + 45) // 90
def coarse(s): return clamp(127 + cdiv(s * 255 - 12, 24) if s < 0 else 128 + cdiv(s * 255 + 12, 24), 0, 255)


def put(d, f, v):
    pos, w = BX[f]
    assert 0 <= v < 1 << w, (f, v)
    for i in range(w):
        p = pos + i
        b = 1 << (6 - p % 7)
        d[p // 7] = d[p // 7] | b if v >> (w - 1 - i) & 1 else d[p // 7] & ~b


def check(name, p):
    R = dict(wave1=(0, 4), wave2=(0, 4), coarse1=(-12, 12), coarse2=(-12, 12), fine1=(-100, 100), fine2=(-100, 100),
             range1=(0, 3), range2=(0, 3), pw1=(5, 95), pw2=(5, 95), sub_oct=(1, 2), sub_wave=(0, 2), filter=(0, 6),
             key_trk=(0, 7), arp_oct=(1, 4), rhythm=(1, 32), swing=(3, 97), arp_mode=(0, 5))
    for k, v in p.items():
        if isinstance(v, tuple):
            continue
        lo, hi = R.get(k, (-63, 63) if k.startswith(("menv_o", "menv_cut", "menv_pw", "pwm", "vel_", "lfo1_o", "lfo2_cut"))
                       else (0, 255) if k.endswith("_rate") else (0, 127))
        if not lo <= v <= hi:
            raise SystemExit(f"{name}: {k} = {v} outside {lo}..{hi}")
    if len(name) > NAME_N or not name.isascii():
        raise SystemExit(f"{name}: a name of at most {NAME_N} ASCII characters")


def dump(name, p, slot):
    """the patch as bsx_export writes it: a bank's dump into patch SLOT"""
    d = INIT + [0] * (LEN - len(INIT))
    d[TO], d[TO + 1] = STORE, slot
    put(d, "BX_W1", WAVE_OUT[p["wave1"]])
    put(d, "BX_W2", WAVE_OUT[p["wave2"]])
    put(d, "BX_PW1", pw_raw(p["pw1"]) if p["wave1"] == PULSE else 64)
    put(d, "BX_PW2", pw_raw(p["pw2"]) if p["wave2"] == PULSE else 64)
    put(d, "BX_PWM1", u7(p["pwm1"]))
    put(d, "BX_PWM2", u7(p["pwm2"]))
    put(d, "BX_NOISE", wide(p["noise"]))
    put(d, "BX_GLIDE", p["glide"])
    put(d, "BX_RANGE1", 63 + p["range1"])
    put(d, "BX_RANGE2", 63 + p["range2"])
    put(d, "BX_COARSE1", coarse(p["coarse1"]))
    put(d, "BX_FINE1", u8(p["fine1"]))
    put(d, "BX_MEPW1", u7(p["menv_pw1"]))
    put(d, "BX_MEPW2", u7(p["menv_pw2"]))
    put(d, "BX_VAMP", u7(p["vel_amp"]))
    put(d, "BX_VMOD", u7(p["vel_menv"]))
    put(d, "BX_ATRIG", p["env_trig"][0])
    put(d, "BX_MTRIG", p["env_trig"][1])
    put(d, "BX_ALOOP", p["env_loop"][0])
    put(d, "BX_MLOOP", p["env_loop"][1])
    put(d, "BX_TRACK", 7 - p["key_trk"])
    put(d, "BX_COARSE2", coarse(p["coarse2"]))
    put(d, "BX_FINE2", u8(p["fine2"]))
    put(d, "BX_SUBOCT", int(p["sub_oct"] == 1))
    put(d, "BX_SUBWAVE", p["sub_wave"])
    put(d, "BX_SYNC", p["sync"])
    acid = p["filter"] == ACID
    put(d, "BX_TYPE", int(acid))
    put(d, "BX_SHAPE", 0 if acid else p["filter"] // 2)
    put(d, "BX_SLOPE", int(acid or p["filter"] % 2 == 0))
    put(d, "BX_FREQ", wide(p["cutoff"]))
    for f, k in (("BX_RESO", "reso"), ("BX_DRIVE", "drive"), ("BX_MA", "menv_a"), ("BX_MD", "menv_d"),
                 ("BX_MS", "menv_s"), ("BX_MR", "menv_r"), ("BX_AA", "amp_a"), ("BX_AD", "amp_d"),
                 ("BX_AS", "amp_s"), ("BX_AR", "amp_r"), ("BX_DIST", "dist"), ("BX_OFM", "ofm")):
        put(d, f, p[k])
    put(d, "BX_MEFILT", u7(p["menv_cut"]))
    put(d, "BX_MEOSC1", u7(p["menv_o1"]))
    put(d, "BX_MEOSC2", u7(p["menv_o2"]))
    for i, n in enumerate("12"):
        put(d, f"BX_L{n}DELAY", p["lfo_delay"][i])
        put(d, f"BX_L{n}SLEW", p["lfo_slew"][i])
        put(d, f"BX_L{n}KSYNC", p["lfo_keysync"][i])
        put(d, f"BX_L{n}SSYNC", p["lfo_sync"][i])
        put(d, f"BX_L{n}SYNCV", p["lfo_syncv"][i])
    put(d, "BX_L1WAVE", p["lfo1_wave"])
    put(d, "BX_L1SPEED", p["lfo1_rate"])
    put(d, "BX_L1OSC1", u8(p["lfo1_o1"] * 2))
    put(d, "BX_L1OSC2", u8(p["lfo1_o2"] * 2))
    put(d, "BX_L2WAVE", p["lfo2_wave"])
    put(d, "BX_L2SPEED", p["lfo2_rate"])
    put(d, "BX_L2FILT", u8(p["lfo2_cut"] * 2))
    ring = p["ring"]                             # the levels faded by the ring, the sub's on their scale
    l1 = (p["lvl1"] * (127 - ring) * 255 + 8064) // 16129
    l2 = (p["lvl2"] * (127 - ring) * 255 + 8064) // 16129
    r = (ring * 255 + 63) // 127
    t = r + max(l1, l2)
    put(d, "BX_LVL1", l1)
    put(d, "BX_LVL2", l2)
    put(d, "BX_RING", r)
    put(d, "BX_LVLSUB", clamp((p["lvl_sub"] * t + 63) // 127, 0, 255) if t else wide(p["lvl_sub"]))
    put(d, "BX_ARP", p["arp_on"])
    put(d, "BX_LATCH", p["latch"])
    put(d, "BX_ARPOCT", p["arp_oct"])
    put(d, "BX_ARPMODE", p["arp_mode"])
    put(d, "BX_RHYTHM", p["rhythm"] - 1)
    put(d, "BX_SWING", p["swing"])
    d[NAME_AT:NAME_AT + NAME_N] = name.ljust(NAME_N).encode("ascii")
    d[LEN - 1] = 0xF7
    assert all(0 <= x < 0x80 for x in d[1:-1])
    return bytes(d)


def main():
    if len(PATCHES) != 32:
        raise SystemExit(f"{len(PATCHES)} patches, not 32")
    out = b""
    for slot, (name, over) in enumerate(PATCHES):
        bad = set(over) - set(DEFAULTS)
        if bad:
            raise SystemExit(f"{name}: no such setting {sorted(bad)}")
        p = {**DEFAULTS, **over}
        check(name, p)
        out += dump(name, p, slot)
    (HERE / "factory.syx").write_bytes(out)
    print(f"factory.syx: {len(PATCHES)} patches, {len(out)} bytes")


if __name__ == "__main__":
    main()
