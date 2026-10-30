/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 John Brown */
/* Bass Station II patch dumps (Novation's SysEx, F0 00 20 29 00 33 ... F7): read into a patch_t, and a patch_t
 * written as one. Part of app.c's unity build (included there). Novation never published the layout; the fields'
 * places are as BS2-Web (github.com/francoisgeorgy/BS2-Web, src/bass-station-2/cc.js, nrpn.js) found them, checked
 * against dumps from a Bass Station II. The parameters are one bit stream from byte 13 on, 7 bits a byte (the MSB
 * first), each field its own width; firmware 2.5 and later add a 16-character name at byte 137 (154 bytes in all,
 * 122 before). What the FM-1's voice does not have (paraphony, the oscillator error ...), and what's place in a
 * dump is not known (the external input's level, the envelopes' retriggering count and fixed sustain, the VCA
 * limiter), is left out of
 * an import and goes out of an export as the INIT PATCH has it. The
 * velocities and the mod envelope's PW depths: BS2-Web's places (cc.js); oscillator 1's PW depth there is 6 bits from
 * 713, read here as 7 from 712 (its neighbours' 7 bits each 8 apart, the field ending where theirs says). The
 * filter's tracking: firmware 2.5's (0 full .. 7 none; before it always full, those dumps' bits 0) */

#define BSX_LEN 154u                     /* a dump with its name: F0 .. F7 */
#define BSX_MIN 122u                     /* one without (before firmware 2.5) */
#define BSX_NAME 137u                    /* the name's first byte */
#define BSX_NAME_N 16u

enum {                                   /* the fields the FM-1 has: (the first bit, counted from F0's, the width) */
    BX_SYNC, BX_W1, BX_RANGE1, BX_COARSE1, BX_FINE1, BX_W2, BX_RANGE2, BX_COARSE2, BX_FINE2, BX_SUBOCT,
    BX_LVL1, BX_LVL2, BX_RING, BX_FREQ, BX_RESO, BX_DRIVE, BX_SLOPE, BX_TYPE, BX_SHAPE,
    BX_MA, BX_MD, BX_MS, BX_MR, BX_L1WAVE, BX_L1SPEED, BX_L2WAVE, BX_L2SPEED,
    BX_LATCH, BX_ARP, BX_ARPOCT, BX_ARPMODE, BX_L1OSC1, BX_L1OSC2, BX_L2FILT, BX_MEOSC1, BX_MEOSC2, BX_MEFILT,
    BX_GLIDE, BX_PW1, BX_PW2, BX_NOISE, BX_RHYTHM, BX_SWING, BX_PWM1, BX_PWM2,
    BX_SUBWAVE, BX_LVLSUB, BX_AA, BX_AD, BX_AS, BX_AR, BX_DIST, BX_MEPW1, BX_MEPW2, BX_VAMP, BX_VMOD, BX_TRACK, BX_ATRIG, BX_MTRIG, BX_ALOOP, BX_MLOOP,
    BX_L1DELAY, BX_L1SLEW, BX_L1KSYNC, BX_L1SSYNC, BX_L1SYNCV, BX_L2DELAY, BX_L2SLEW, BX_L2KSYNC, BX_L2SSYNC, BX_L2SYNCV,
    BX_OFM, NBX
};
static const struct { uint16_t pos; uint8_t w; } BX[NBX] = {
    [BX_SYNC] = {126, 1},   [BX_W1] = {133, 2},     [BX_RANGE1] = {144, 7},  [BX_COARSE1] = {151, 8},
    [BX_FINE1] = {159, 8},  [BX_W2] = {173, 2},     [BX_RANGE2] = {184, 7},  [BX_COARSE2] = {191, 8},
    [BX_FINE2] = {199, 8},  [BX_SUBOCT] = {262, 1}, [BX_LVL1] = {263, 8},    [BX_LVL2] = {271, 8},
    [BX_RING] = {295, 8},   [BX_FREQ] = {311, 8},   [BX_RESO] = {320, 7},    [BX_DRIVE] = {328, 7},
    [BX_SLOPE] = {339, 1},  [BX_TYPE] = {340, 1},   [BX_SHAPE] = {341, 2},   [BX_MA] = {400, 7},
    [BX_MD] = {408, 7},     [BX_MS] = {416, 7},     [BX_MR] = {424, 7},      [BX_L1WAVE] = {445, 2},
    [BX_L1SPEED] = {463, 8}, [BX_L2WAVE] = {493, 2}, [BX_L2SPEED] = {511, 8}, [BX_LATCH] = {541, 1},
    [BX_ARP] = {542, 1},    [BX_ARPOCT] = {548, 3}, [BX_ARPMODE] = {556, 3}, [BX_L1OSC1] = {631, 8},
    [BX_L1OSC2] = {639, 8}, [BX_L2FILT] = {679, 8}, [BX_MEOSC1] = {688, 7},  [BX_MEOSC2] = {696, 7},
    [BX_MEFILT] = {736, 7}, [BX_GLIDE] = {96, 7},   [BX_PW1] = {136, 7},     [BX_PW2] = {176, 7},
    [BX_NOISE] = {287, 8},  [BX_RHYTHM] = {562, 5}, [BX_SWING] = {568, 7},   [BX_PWM1] = {656, 7},
    [BX_PWM2] = {664, 7},   [BX_SUBWAVE] = {253, 2}, [BX_LVLSUB] = {279, 8}, [BX_AA] = {352, 7},
    [BX_AD] = {360, 7},     [BX_AS] = {368, 7},     [BX_AR] = {376, 7},      [BX_DIST] = {752, 7},
    [BX_MEPW1] = {712, 7},  [BX_MEPW2] = {720, 7},  [BX_VAMP] = {344, 7},    [BX_VMOD] = {392, 7},
    [BX_TRACK] = {788, 3},
    /* the envelopes' Triggering (0 Multi, 1 Single, 2 Autoglide) and retriggering (firmware 2.5's): BS2-Web's
     * nrpn.js, its byte and mask as a bit here (byte x 7 + 6 - the mask's top bit) */
    [BX_ATRIG] = {389, 2},  [BX_MTRIG] = {437, 2},  [BX_ALOOP] = {798, 1},  [BX_MLOOP] = {806, 1},
    /* the LFOs' delay and slew, keysync, Speed/Sync and sync value: BS2-Web's (cc.js, nrpn.js), as the above */
    [BX_L1DELAY] = {448, 7}, [BX_L1SLEW] = {456, 7}, [BX_L1KSYNC] = {485, 1}, [BX_L1SSYNC] = {486, 1},
    [BX_L1SYNCV] = {473, 6}, [BX_L2DELAY] = {496, 7}, [BX_L2SLEW] = {504, 7}, [BX_L2KSYNC] = {533, 1},
    [BX_L2SSYNC] = {534, 1}, [BX_L2SYNCV] = {521, 6},
    [BX_OFM] = {744, 7},                 /* Osc Filter Mod (cc.js's fx_osc_filter_mod) */
};

/* the factory INIT PATCH (Bass Station II 70-127, as BS2-Web's notes have it): what an export starts from */
static const uint8_t BSX_INIT[BSX_MIN - 1u] = {
    0xF0, 0x00, 0x20, 0x29, 0x00, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x4C, 0x00, 0x00, 0x48, 0x04, 0x04, 0x02, 0x00, 0x02, 0x20, 0x10, 0x10, 0x08, 0x00, 0x01, 0x00,
    0x43, 0x40, 0x20, 0x00, 0x03, 0x7F, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x78, 0x00, 0x00,
    0x08, 0x20, 0x00, 0x00, 0x07, 0x78, 0x00, 0x00, 0x40, 0x00, 0x00, 0x0F, 0x70, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x12, 0x63, 0x10, 0x00, 0x00, 0x00, 0x00, 0x1A, 0x06, 0x20, 0x20, 0x20, 0x04, 0x00,
    0x1F, 0x19, 0x10, 0x09, 0x24, 0x02, 0x01, 0x14, 0x40, 0x20, 0x20, 0x10, 0x08, 0x02, 0x01, 0x00,
    0x40, 0x40, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x40, 0x20, 0x00, 0x00, 0x00, 0x03, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static uint32_t bsx_get(const uint8_t *d, uint32_t f)   /* d: the dump from its F0 */
{
    uint32_t v = 0, i, p;
    for (i = 0; i < BX[f].w; i++) {
        p = BX[f].pos + i;
        v = v << 1 | (d[p / 7u] >> (6u - p % 7u) & 1u);
    }
    return v;
}
static void bsx_put(uint8_t *d, uint32_t f, uint32_t v)
{
    uint32_t i, p, b;
    for (i = 0; i < BX[f].w; i++) {
        p = BX[f].pos + i;
        b = 1u << (6u - p % 7u);
        d[p / 7u] = (uint8_t)(v >> (BX[f].w - 1u - i) & 1u ? d[p / 7u] | b : d[p / 7u] & ~b);
    }
}

/* the BS2's values: 7 bits centred at 63/64 (both 0), 8 bits at 127/128; its coarse tune 8 bits for -12..+12
 * semitones (127/128 is 0, a semitone about 10.6 apart) */
static int32_t bsx_c7(uint32_t v) { return v < 64u ? (int32_t)v - 63 : (int32_t)v - 64; }
static int32_t bsx_c8(uint32_t v) { return v < 128u ? (int32_t)v - 127 : (int32_t)v - 128; }
static uint32_t bsx_u7(int32_t a) { return (uint32_t)clamp(a < 0 ? a + 63 : a + 64, 0, 127); }
static uint32_t bsx_u8(int32_t a) { return (uint32_t)clamp(a < 0 ? a + 127 : a + 128, 0, 255); }
static int32_t bsx_semis(uint32_t v)
{
    int32_t x = (2 * (int32_t)v - 255) * 12;
    return (x < 0 ? x - 127 : x + 127) / 255;   /* rounded */
}
static uint32_t bsx_coarse(int32_t s) { return (uint32_t)clamp(s < 0 ? 127 + (s * 255 - 12) / 24 : 128 + (s * 255 + 12) / 24, 0, 255); }
static uint32_t bsx_wide(uint32_t v) { return v * 2u + (v >> 6); }   /* 0..127 to 0..255 */
static uint32_t bsx_pw(uint32_t v) { return (v * 90u + 76u) / 127u + 5u; }   /* a manual PW's 0..127 to 5..95 % */
static uint32_t bsx_pw_raw(uint32_t pc) { return ((pc - 5u) * 127u + 45u) / 90u; }

static const uint8_t BSX_ARP_IN[8] = {ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_UPDOWN2, ARP_PLAYED, ARP_RANDOM, ARP_PLAY,
                                      ARP_REC};   /* (the sequencer's play and record: BS2-Web's 6 and 7) */
static const uint8_t BSX_ARP_OUT[NARPM] = {0, 1, 2, 3, 4, 5, 6, 7};

static const uint8_t BSX_WAVE_IN[4] = {WAVE_SINE, WAVE_TRI, WAVE_SAW, WAVE_PULSE};
static const uint8_t BSX_WAVE_OUT[NWAVES] = {2, 3, 0, 1, 3};   /* (the square a pulse at 50 %) */

#define BSX_TO 7u                        /* the dump's kind: 0 the sound (the edit buffer), BSX_STORE a patch's */
#define BSX_STORE 0x01u                  /* a bank's dump: into the patch numbered at BSX_TO + 1 (0..127) */
/* a frame's patch number when it is a bank's dump (a "store into patch N", as a whole bank's .syx is), else -1 (the
 * sound's, or not a dump); p as bsx_import has it */
static int32_t bsx_slot(const uint8_t *p, uint32_t n)
{
    return n + 2u >= BSX_MIN && p[BSX_TO - 1u] == BSX_STORE ? (int32_t)(p[BSX_TO] & 0x7Fu) : -1;
}

/* a frame from USB (p: the bytes between F0 and F7) into *o: 1 when it is a Bass Station II patch dump. Only the
 * fields a dump has are written (the tempo, the gate, the steps are left); then the caller's patch_sane */
static int bsx_import(const uint8_t *p, uint32_t n, patch_t *o, char *name)
{
    static const uint8_t HDR[5] = {0x00, 0x20, 0x29, 0x00, 0x33};
    uint8_t d[BSX_LEN];
    uint32_t i, r, l1, l2, m, t;
    if (n + 2u != BSX_MIN && n + 2u != BSX_LEN)
        return 0;
    for (i = 0; i < 5u; i++)
        if (p[i] != HDR[i])
            return 0;
    d[0] = 0xF0;
    for (i = 0; i < n; i++)
        d[i + 1u] = p[i] & 0x7Fu;
    o->wave1 = BSX_WAVE_IN[bsx_get(d, BX_W1)];
    o->wave2 = BSX_WAVE_IN[bsx_get(d, BX_W2)];
    o->pw1 = (uint8_t)bsx_pw(bsx_get(d, BX_PW1));
    o->pw2 = (uint8_t)bsx_pw(bsx_get(d, BX_PW2));
    o->pwm1 = (int8_t)bsx_c7(bsx_get(d, BX_PWM1));   /* (LFO 2 on the widths) */
    o->pwm2 = (int8_t)bsx_c7(bsx_get(d, BX_PWM2));
    o->noise = (uint8_t)(bsx_get(d, BX_NOISE) >> 1);
    o->glide = (uint8_t)bsx_get(d, BX_GLIDE);
    o->range1 = (uint8_t)clamp((int32_t)bsx_get(d, BX_RANGE1) - 63, RANGE_16, RANGE_2);   /* (63..66: 16' .. 2') */
    o->range2 = (uint8_t)clamp((int32_t)bsx_get(d, BX_RANGE2) - 63, RANGE_16, RANGE_2);
    o->coarse1 = (int8_t)clamp(bsx_semis(bsx_get(d, BX_COARSE1)), -12, 12);
    o->fine1 = (int8_t)clamp(bsx_c8(bsx_get(d, BX_FINE1)), -100, 100);
    o->coarse2 = (int8_t)clamp(bsx_semis(bsx_get(d, BX_COARSE2)), -12, 12);
    o->fine2 = (int8_t)clamp(bsx_c8(bsx_get(d, BX_FINE2)), -100, 100);
    o->menv_pw1 = (int8_t)bsx_c7(bsx_get(d, BX_MEPW1));
    o->menv_pw2 = (int8_t)bsx_c7(bsx_get(d, BX_MEPW2));
    o->vel_amp = (int8_t)bsx_c7(bsx_get(d, BX_VAMP));
    o->vel_menv = (int8_t)bsx_c7(bsx_get(d, BX_VMOD));
    o->env_trig[0] = (uint8_t)bsx_get(d, BX_ATRIG);   /* the envelopes' Triggering and retriggering; the retriggering's
                                                       * count and the fixed sustain (firmware 4's: their places not
                                                       * known) as the INIT PATCH has them */
    o->env_trig[1] = (uint8_t)bsx_get(d, BX_MTRIG);
    o->env_loop[0] = (uint8_t)bsx_get(d, BX_ALOOP);
    o->env_loop[1] = (uint8_t)bsx_get(d, BX_MLOOP);
    o->env_count[0] = o->env_count[1] = 0;
    o->env_fixed[0] = o->env_fixed[1] = 0;
    o->key_trk = (uint8_t)(7u - bsx_get(d, BX_TRACK));
    o->sub_oct = bsx_get(d, BX_SUBOCT) ? 1u : 2u;
    o->sub_wave = (uint8_t)bsx_get(d, BX_SUBWAVE);   /* (held to its range by the caller) */
    o->sync = (uint8_t)bsx_get(d, BX_SYNC);
    o->filter = bsx_get(d, BX_TYPE) ? FLT_ACID
              : (uint8_t)((uint32_t)clamp((int32_t)bsx_get(d, BX_SHAPE), 0, 2) * 2u + (bsx_get(d, BX_SLOPE) ? 0u : 1u));
    o->cutoff = (uint8_t)(bsx_get(d, BX_FREQ) >> 1);
    o->reso = (uint8_t)bsx_get(d, BX_RESO);
    o->drive = (uint8_t)bsx_get(d, BX_DRIVE);
    o->menv_a = (uint8_t)bsx_get(d, BX_MA);
    o->menv_d = (uint8_t)bsx_get(d, BX_MD);
    o->menv_s = (uint8_t)bsx_get(d, BX_MS);
    o->menv_r = (uint8_t)bsx_get(d, BX_MR);
    o->amp_a = (uint8_t)bsx_get(d, BX_AA);
    o->amp_d = (uint8_t)bsx_get(d, BX_AD);
    o->amp_s = (uint8_t)bsx_get(d, BX_AS);
    o->amp_r = (uint8_t)bsx_get(d, BX_AR);
    o->dist = (uint8_t)bsx_get(d, BX_DIST);
    o->ofm = (uint8_t)bsx_get(d, BX_OFM);
    o->lim = 0;                          /* (the VCA limiter: its place in a dump not known) */
    o->menv_cut = (int8_t)bsx_c7(bsx_get(d, BX_MEFILT));   /* the mod envelope's three depths */
    o->menv_o1 = (int8_t)bsx_c7(bsx_get(d, BX_MEOSC1));
    o->menv_o2 = (int8_t)bsx_c7(bsx_get(d, BX_MEOSC2));
    for (i = 0; i < NLFO; i++) {             /* LFO 1 on the oscillators' pitches, LFO 2 the cutoff; each its wave, its
                                              * speed's 8 bits, delay, slew, keysync and sync */
        o->lfo[i].wave = (uint8_t)bsx_get(d, i ? BX_L2WAVE : BX_L1WAVE);
        o->lfo[i].rate = (uint8_t)bsx_get(d, i ? BX_L2SPEED : BX_L1SPEED);
        o->lfo_delay[i] = (uint8_t)bsx_get(d, i ? BX_L2DELAY : BX_L1DELAY);
        o->lfo_slew[i] = (uint8_t)bsx_get(d, i ? BX_L2SLEW : BX_L1SLEW);
        o->lfo_keysync[i] = (uint8_t)bsx_get(d, i ? BX_L2KSYNC : BX_L1KSYNC);
        o->lfo_sync[i] = (uint8_t)bsx_get(d, i ? BX_L2SSYNC : BX_L1SSYNC);
        o->lfo_syncv[i] = (uint8_t)bsx_get(d, i ? BX_L2SYNCV : BX_L1SYNCV);
    }
    o->lfo[0].depth[0] = (int8_t)clamp(bsx_c8(bsx_get(d, BX_L1OSC1)) / 2, -63, 63);
    o->lfo[0].depth[1] = (int8_t)clamp(bsx_c8(bsx_get(d, BX_L1OSC2)) / 2, -63, 63);
    o->lfo[1].depth[0] = (int8_t)clamp(bsx_c8(bsx_get(d, BX_L2FILT)) / 2, -63, 63);
    o->lfo[1].depth[1] = 0;
    /* the mixer: the ring's share of it and the louder oscillator's (the FM-1's ring a fade between them), that
     * oscillator at 127 and the rest in proportion; without either, the sub as it is */
    r = bsx_get(d, BX_RING);
    l1 = bsx_get(d, BX_LVL1);
    l2 = bsx_get(d, BX_LVL2);
    m = l1 > l2 ? l1 : l2;
    t = r + m;
    o->ring = (uint8_t)(t ? (r * 127u + t / 2u) / t : 0u);
    o->lvl1 = (uint8_t)(m ? (l1 * 127u + m / 2u) / m : 0u);
    o->lvl2 = (uint8_t)(m ? (l2 * 127u + m / 2u) / m : 0u);
    o->lvl_sub = (uint8_t)(t ? clamp((int32_t)((bsx_get(d, BX_LVLSUB) * 127u + t / 2u) / t), 0, 127)
                             : (int32_t)(bsx_get(d, BX_LVLSUB) >> 1));
    o->arp_on = (uint8_t)bsx_get(d, BX_ARP);
    o->latch = (uint8_t)bsx_get(d, BX_LATCH);
    o->arp_oct = (uint8_t)clamp((int32_t)bsx_get(d, BX_ARPOCT), 1, 4);
    o->arp_mode = BSX_ARP_IN[bsx_get(d, BX_ARPMODE)];
    o->rhythm = (uint8_t)(bsx_get(d, BX_RHYTHM) + 1u);   /* (its 0..31 as 1..32) */
    o->swing = (uint8_t)bsx_get(d, BX_SWING);
    name[0] = 0;
    if (n + 2u == BSX_LEN) {                 /* the name, its spaces at the end left off */
        for (i = 0; i < BSX_NAME_N; i++)
            name[i] = (char)(d[BSX_NAME + i] >= 0x20u ? d[BSX_NAME + i] : ' ');
        while (i && name[i - 1u] == ' ')
            i--;
        name[i] = 0;
    }
    return 1;
}

/* *p as a dump into d (BSX_LEN bytes), named NAME; what the FM-1 lacks as the INIT PATCH has it. SLOT: the sound's
 * dump (-1), or a bank's into patch SLOT */
static void bsx_export(const patch_t *p, const char *name, int32_t slot, uint8_t *d)
{
    uint32_t i, l1, l2, r, t;
    for (i = 0; i < BSX_LEN; i++)
        d[i] = i < sizeof BSX_INIT ? BSX_INIT[i] : 0u;
    if (slot >= 0) {
        d[BSX_TO] = BSX_STORE;
        d[BSX_TO + 1u] = (uint8_t)slot;
    }
    bsx_put(d, BX_W1, BSX_WAVE_OUT[p->wave1]);
    bsx_put(d, BX_W2, BSX_WAVE_OUT[p->wave2]);
    bsx_put(d, BX_PW1, p->wave1 == WAVE_PULSE ? bsx_pw_raw(p->pw1) : 64u);
    bsx_put(d, BX_PW2, p->wave2 == WAVE_PULSE ? bsx_pw_raw(p->pw2) : 64u);
    bsx_put(d, BX_PWM1, bsx_u7(p->pwm1));
    bsx_put(d, BX_PWM2, bsx_u7(p->pwm2));
    bsx_put(d, BX_NOISE, bsx_wide(p->noise));
    bsx_put(d, BX_GLIDE, p->glide);
    bsx_put(d, BX_RANGE1, 63u + p->range1);
    bsx_put(d, BX_RANGE2, 63u + p->range2);
    bsx_put(d, BX_COARSE1, bsx_coarse(p->coarse1));
    bsx_put(d, BX_FINE1, bsx_u8(p->fine1));
    bsx_put(d, BX_MEPW1, bsx_u7(p->menv_pw1));
    bsx_put(d, BX_MEPW2, bsx_u7(p->menv_pw2));
    bsx_put(d, BX_VAMP, bsx_u7(p->vel_amp));
    bsx_put(d, BX_VMOD, bsx_u7(p->vel_menv));
    bsx_put(d, BX_ATRIG, p->env_trig[0]);
    bsx_put(d, BX_MTRIG, p->env_trig[1]);
    bsx_put(d, BX_ALOOP, p->env_loop[0]);
    bsx_put(d, BX_MLOOP, p->env_loop[1]);
    bsx_put(d, BX_TRACK, 7u - p->key_trk);
    bsx_put(d, BX_COARSE2, bsx_coarse(p->coarse2));
    bsx_put(d, BX_FINE2, bsx_u8(p->fine2));
    bsx_put(d, BX_SUBOCT, p->sub_oct == 1u);
    bsx_put(d, BX_SUBWAVE, p->sub_wave);
    bsx_put(d, BX_SYNC, p->sync);
    bsx_put(d, BX_TYPE, p->filter == FLT_ACID);
    bsx_put(d, BX_SHAPE, p->filter == FLT_ACID ? 0u : p->filter / 2u);
    bsx_put(d, BX_SLOPE, p->filter == FLT_ACID || p->filter % 2u == 0u);
    bsx_put(d, BX_FREQ, bsx_wide(p->cutoff));
    bsx_put(d, BX_RESO, p->reso);
    bsx_put(d, BX_DRIVE, p->drive);
    bsx_put(d, BX_MA, p->menv_a);
    bsx_put(d, BX_MD, p->menv_d);
    bsx_put(d, BX_MS, p->menv_s);
    bsx_put(d, BX_MR, p->menv_r);
    bsx_put(d, BX_AA, p->amp_a);
    bsx_put(d, BX_AD, p->amp_d);
    bsx_put(d, BX_AS, p->amp_s);
    bsx_put(d, BX_AR, p->amp_r);
    bsx_put(d, BX_DIST, p->dist);
    bsx_put(d, BX_OFM, p->ofm);
    bsx_put(d, BX_MEFILT, bsx_u7(p->menv_cut));
    bsx_put(d, BX_MEOSC1, bsx_u7(p->menv_o1));
    bsx_put(d, BX_MEOSC2, bsx_u7(p->menv_o2));
    for (i = 0; i < NLFO; i++) {
        bsx_put(d, i ? BX_L2DELAY : BX_L1DELAY, p->lfo_delay[i]);
        bsx_put(d, i ? BX_L2SLEW : BX_L1SLEW, p->lfo_slew[i]);
        bsx_put(d, i ? BX_L2KSYNC : BX_L1KSYNC, p->lfo_keysync[i]);
        bsx_put(d, i ? BX_L2SSYNC : BX_L1SSYNC, p->lfo_sync[i]);
        bsx_put(d, i ? BX_L2SYNCV : BX_L1SYNCV, p->lfo_syncv[i]);
    }
    bsx_put(d, BX_L1WAVE, p->lfo[0].wave);
    bsx_put(d, BX_L1SPEED, p->lfo[0].rate);
    bsx_put(d, BX_L1OSC1, bsx_u8(p->lfo[0].depth[0] * 2));
    bsx_put(d, BX_L1OSC2, bsx_u8(p->lfo[0].depth[1] * 2));
    bsx_put(d, BX_L2WAVE, p->lfo[1].wave);
    bsx_put(d, BX_L2SPEED, p->lfo[1].rate);
    bsx_put(d, BX_L2FILT, bsx_u8(p->lfo[1].depth[0] * 2));
    /* the oscillators' levels faded by the ring, the ring's, the sub's on the same scale (as an import reads them) */
    l1 = (p->lvl1 * (127u - p->ring) * 255u + 8064u) / 16129u;
    l2 = (p->lvl2 * (127u - p->ring) * 255u + 8064u) / 16129u;
    r = (p->ring * 255u + 63u) / 127u;
    t = r + (l1 > l2 ? l1 : l2);
    bsx_put(d, BX_LVL1, l1);
    bsx_put(d, BX_LVL2, l2);
    bsx_put(d, BX_RING, r);
    bsx_put(d, BX_LVLSUB, t ? (uint32_t)clamp((int32_t)((p->lvl_sub * t + 63u) / 127u), 0, 255) : bsx_wide(p->lvl_sub));
    bsx_put(d, BX_ARP, p->arp_on);
    bsx_put(d, BX_LATCH, p->latch);
    bsx_put(d, BX_ARPOCT, p->arp_oct);
    bsx_put(d, BX_ARPMODE, BSX_ARP_OUT[p->arp_mode]);
    bsx_put(d, BX_RHYTHM, p->rhythm - 1u);
    bsx_put(d, BX_SWING, p->swing);
    for (i = 0; i < BSX_NAME_N; i++, name += name[0] != 0)   /* padded with spaces */
        d[BSX_NAME + i] = (uint8_t)(name[0] ? name[0] & 0x7F : ' ');
    d[BSX_LEN - 1u] = 0xF7;
}
