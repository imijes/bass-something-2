/* SPDX-License-Identifier: MPL-2.0 */
/* The organ (the fourth instrument, after the Hammond B-3) and its Leslie 122: its sound from a preset (b3_set), its
 * keys (b3_note), the Leslie's speed and the expression live; made on the second core after the RMI's block (b3_job),
 * a unit of its own at -O2. Our own code, from how the organ works:
 *   the generator: 91 wheels always turning (C1 = 32.7 Hz up, equal tempered), a phase each, never reset; a wheel's
 *     wave a sine, its 2nd and 3rd on the wheels above (B3_HARM); only the wheels sounding are made
 *   the manual: 61 keys (C2..C7), 9 drawbars a key, each bar the wheel nearest its harmonic (16' 5 1/3' 8' 4' 2 2/3'
 *     2' 1 3/5' 1 1/3' 1': -12 +7 0 +12 +19 +24 +28 +31 +36 semitones), folded back by octaves at the generator's
 *     ends (the 16' on the bottom octave too), its level by the key (the taper, B3_TAPER); a step of a drawbar
 *     about 3 dB; two keys on one wheel add, as on the busbar; leakage: a wheel's neighbours a little (B3_LEAK)
 *   the key contacts: the gains ramped over a modulation step (B3_CH samples), a click of filtered noise at make
 *     and, softer, at break
 *   percussion: the 2nd (4') or 3rd (2 2/3') of the keys struck when none was held (single trigger), its own decay
 *     (fast 0.115 s, slow 0.34 s to 1/e), soft -6 dB; it takes the 1' bar off while on; past the scanner
 *   the scanner vibrato and chorus: a delay of 0.9 ms swept 0.65 ms times V1/V2/V3's depth at 6.9 Hz, the chorus
 *     half dry
 *   the preamp: a soft asymmetric clip (tanh round a bias), its drive
 *   the Leslie: an 800 Hz crossover (Linkwitz-Riley, two Butterworths a side), the horn above and the drum below,
 *     each turning, its path to the microphone its delay (the Doppler), its level by the way it faces (the horn's
 *     brighter facing), two reflections more; chorale and tremolo, each rotor's own run up and down, a brake */
#include <stdint.h>
#include "plat.h"
#include "fastmath.h"
#include "bs2_b3.h"
#include "bs2_prof.h"
#if BS2_B3                                   /* (off: nothing of it built) */
#define B3_RATE 44100.0f
#ifdef PLAT_HOST
#define B3_HOT
#else
#define B3_HOT __attribute__((section(".c1_text")))   /* (RAM: off the shared cache's misses; it calls only what is
                                              * inlined into it, nothing in the flash: -fno-builtin, fm1.toml's
                                              * o2_cflags, so no loop of its becomes a memset) */
#endif
#define B3_IN static inline __attribute__((always_inline))
#define B3_NW 91u                            /* wheels: C1 (MIDI 24) .. F#8 */
#define B3_NT (B3_NW + 20u)                  /* and above them, to 18.8 kHz, the top wheels' own 2nd and 3rd's alone (a
                                              * wheel's harmonics made on the wheels above it: the generator's top
                                              * octave and a half had none) */
#define B3_W0 24u                            /* the first wheel's MIDI note */
#define B3_K0 36u                            /* the manual: C2 .. C7 */
#define B3_NK 61u
#define B3_CH 32u                            /* the gains' step (CTRL_N's) */
#define B3_TB 1024u                          /* a wave's table */
#define B3_LEAK 0.0f                         /* a sounding wheel's neighbours, each (0.006: about -44 dB; 0: none, as
                                              * they double the wheels made) */
#define B3_NCL 8u                            /* key clicks at once */
#define B3_SCAN_N 128u                       /* the scanner's line (a power of 2) */
#define B3_LES_N 1024u                       /* a rotor's line (a power of 2): its swing and its reflections */
/* the cabinet's voicing, a reference organ's Leslie measured (its response over the same chord direct: the horn and
 * its driver 10-16 dB over the drum's 350 Hz from 700 Hz to 3 kHz, falling past 5 kHz; the drum's hump at 200 Hz;
 * tools/b3_compare.py): the horn's gain and a broad peak and a low pass on it, a peak on the drum's */
#define B3_EQ_HORN 13.1f                     /* dB */
#define B3_HORN_H2 0.3f                      /* the horn's compression driver: its even harmonic (x + this x^2, its DC
                                              * off): a reference organ's C6 2nd 9 dB more through the horn */
#define B3_EQ_TRIM -6.7f                     /* the whole, dB: as loud as before it (the organ's mix kept) */
#define B3_HORN_SW 5.1f                      /* each rotor's Doppler: its path's swing, samples (the horn's, the
                                              * drum's) */
#define B3_DRUM_SW 8.5f
#define B3_HORN_AM 0.85f                     /* each rotor's level turned away, to facing (its tremolo) */
#define B3_DRUM_AM 0.2f
#define B3_MIC 0.25f                         /* the microphones either side of the front, a turn's (a quarter: the
                                              * sides), */
#define B3_DMIC 0.06f                        /* the horn's and the drum's (the drum's louvres heard nearly as one) */
#define B3_HORN_REF 0.15f                    /* the horn's reflection, the two microphones' in common (the drum's
                                              * 0.4) */
#define B3_HMIC_D 8.0f                       /* the right microphone that much farther from the horn, samples (the two
                                              * apart: a reference organ's L/R) */
#define B3_LEVEL 2.0f                        /* into the RMI's buffer (its level 1) */
static const int8_t B3_BAR[9] = {-12, 7, 0, 12, 19, 24, 28, 31, 36};
/* the manual's taper: each drawbar's level at C2 C3 C4 C5 C6 C7, half dB to the 8' at C4 (between, in a line); the
 * upper bars louder on the low keys, all falling to the top, as a B-3's resistors and wheels give it */
static const int8_t B3_TAPER[9][6] = {
    {-19, -7, 5, 9, 2, -3}, {4, 2, 2, -18, -8, -10}, {-3, -7, 3, -7, -7, -7}, {-2, 0, -6, -18, -30, -33},
    {21, 10, -19, -17, -32, -42}, {20, 0, -16, -23, -29, -37}, {10, -9, -17, -21, -35, -40},
    {2, -17, -17, -22, -36, -40}, {-13, -15, -24, -25, -40, -42}};
static float b3_tap[9][B3_NK] PLAT_POOL;     /* it for each key (b3_init's), a gain */
/* a wheel's own 2nd and 3rd harmonics (its tooth's shape, its pickup's), dB to it at C2 C3 .. C8, the low wheels
 * the richest: made on the wheels an octave up (2:1 on a B-3's gears) and an octave and a fifth (3:1 within 2 cents) */
static const int8_t B3_HARM[2][7] = {{-14, -23, -32, -43, -42, -34, -32}, {-40, -43, -46, -50, -53, -65, -57}};
static float b3_hm[2][B3_NW] PLAT_POOL;
/* the busbars' crosstalk: a key down, every drawbar of its manual not drawn still heard faintly while one is (none:
 * silence, as a B-3's), dB under the key's loudest drawn bar (a reference organ's, measured: its undrawn 1 1/3' and
 * 1' 45-55 dB down, its 1 3/5' about 65); its wheels made plain (no harmonics) */
static const int8_t B3_XT[9] = {-52, -52, -52, -52, -52, -52, -65, -50, -52};
static float b3_xt[9];                       /* it as gains (b3_init's) */      /* it for each wheel (b3_init's), a gain (0: under -60 dB) */
static float b3_wave[2][B3_TB + 1u] PLAT_POOL;   /* the low wheels' (a little 2nd and 3rd), the high wheels' (sine) */
static float b3_scan[B3_SCAN_N] PLAT_POOL;
static float b3_les[2][B3_LES_N] PLAT_POOL;      /* the horn's line, the drum's */
static struct {
    uint32_t ph[B3_NT], inc[B3_NT];          /* the wheels: each one's phase at t 0, its step (its phase: ph + inc * t) */
    float kc[B3_NT];                         /* 2 cos(its step): its sine a sample on from the two before */
    uint32_t t;                              /* the generator's samples (the wheels turn without being made) */
    float g[B3_NT], dg[B3_NT];               /* each wheel's level on the bus, its step a sample to the target */
    float d[B3_NT], dd[B3_NT];               /* on the dry bus (a manual's VIBRATO off: past the scanner) */
    float pg[B3_NT], dpg[B3_NT];             /* the percussion's, on its own bus */
    uint8_t act[B3_NT], nact;                /* the wheels sounding or ramping */
    uint32_t held[4];                        /* the keys down, a bit each: the upper manual's 61, the lower's from 64 */
    uint32_t pk[2];                          /* the upper manual's keys the percussion struck */
    float lv[2][9];                          /* each drawbar's level (b3_levels), the upper manual's sounding set's, the
                                              * lower's */
    float perc, perc_k;                      /* its envelope, its fall a sample */
    uint32_t perc_t;                         /* samples since it struck (a chord's keys join it within B3_CH * 8) */
    uint8_t bar[2][9];                       /* the drawbars of the set each manual sounds, 0..8 */
    uint8_t vib[2];                          /* each manual through the scanner (VIBRATO UPPER, LOWER) */
    uint8_t perc_on, perc_3rd, perc_soft, perc_fast, scan, click;
    float expr, drive;                       /* the swell pedal (0 closed .. 1 open), the preamp's drive */
    float sw_lo, sw_hi, sw_x, sw_tlo, sw_thi;                /* the swell's gains now, under its split and over it (slewed a step at a
                                              * time), the split's low pass, their aims (b3_expr's) */
    struct { float e, k, x1, y1, y2, amt; } cl[B3_NCL];   /* the clicks: their noise's level and fall, a band pass */
    uint32_t cl_next, rng;
    float sc_ph, sc_x;                       /* the scanner: its rotor (0..1), its line's low pass */
    uint32_t sc_w;
    float hp_x, hp_y;                        /* the preamp's input high pass */
    float xo[4][4];                          /* the crossover's biquads' states (x1 x2 y1 y2): the low pass's two, the
                                              * high pass's two */
    float eq[3][4];                          /* the cabinet's voicing (B3_EQ): the horn's lift and its top's fall, the
                                              * drum's hump */
    float hdc;                               /* the horn driver's even harmonic's own DC (its low pass) */
    float hz[2], to[2], ang[2];              /* the rotors: speed now, its aim, the angle (0..1) */
    float cho[2], tre[2], up[2], down[2];    /* each rotor's trimmers: its slow and fast speeds (Hz), its rise and fall
                                              * (s, a time constant) */
    float hlp[2];                            /* the horn's facing low pass, at each microphone */
    float width;                             /* the stereo's: 0 mono .. 1 the two microphones as they are (CC 29) */
    uint32_t lw;
    uint8_t fast, brake, awake;
    uint8_t dirty;                           /* a key or a setting changed: the targets again (b3_step) */
    uint8_t cap;                             /* the wheels it may sound at most (b3_cap: an overload's) */
    uint32_t quiet;                          /* samples its input has been silent (past the lines: the chain skipped) */
} b3;
static float b3_eq_b[3][3], b3_eq_a[3][2];   /* the cabinet's voicing's biquads (b3_init's) */
static float b3_eq_hg, b3_eq_tg;             /* its gains: the horn's (with the trim), the drum's (the trim) */
static float b3_lp_b[3], b3_lp_a[2], b3_hp_b[3];   /* the crossover's Butterworths, 800 Hz: the low pass, the high
                                              * pass's (the same poles) */
#ifdef PLAT_HOST
uint8_t b3_test_direct;                      /* (the host's b3_direct: the preamp's out, no Leslie, tools/b3_compare.py's) */
#endif

static void b3_levels(void)                  /* the drawbars' levels (3 dB a step; percussion on: the 1' bar off), the
                                              * percussion's fall a step: from the settings, here, not on the second core */
{
    uint32_t b, m;
    for (m = 0; m < 2u; m++)
        for (b = 0; b < 9u; b++)
            b3.lv[m][b] = b3.bar[m][b] && !(b == 8u && b3.perc_on && !m) ? fm_exp2f(((float)b3.bar[m][b] - 8.0f) * 0.5f)
                                                                         : 0.0f;
    b3.perc_k = fm_exp2f(-(float)B3_CH / ((b3.perc_fast ? 0.115f : 0.34f) * B3_RATE) * 1.442695f);
}
void b3_init(void)
{
    uint32_t i, w;
    for (i = 0; i <= B3_TB; i++) {
        const float x = 2.0f * FM_PI * (float)i / (float)B3_TB;
        b3_wave[1][i] = fm_sinf(x);
        b3_wave[0][i] = (fm_sinf(x) + 0.04f * fm_sinf(2.0f * x) + 0.02f * fm_sinf(3.0f * x)) * (1.0f / 1.06f);
    }
    for (w = 0; w < B3_NT; w++)
        b3.inc[w] = (uint32_t)(32.70319566f * fm_exp2f((float)w / 12.0f) / B3_RATE * 4294967040.0f),
        b3.ph[w] = (uint32_t)w * 2654435761u,   /* (each wheel somewhere of its own) */
        b3.kc[w] = 2.0f * fm_cosf(2.0f * FM_PI * (float)b3.inc[w] * (1.0f / 4294967296.0f));
    {                                        /* the crossover, Linkwitz-Riley: RBJ's Butterworths (Q 0.7071), a low pass
                                              * twice and a high pass twice */
        const float om = 2.0f * FM_PI * 800.0f / B3_RATE, al = fm_sinf(om) / (2.0f * 0.70710678f), c = fm_cosf(om),
                    a0 = 1.0f + al;
        b3_lp_b[0] = b3_lp_b[2] = (1.0f - c) * 0.5f / a0, b3_lp_b[1] = (1.0f - c) / a0;
        b3_lp_a[0] = -2.0f * c / a0, b3_lp_a[1] = (1.0f - al) / a0;
        b3_hp_b[0] = b3_hp_b[2] = (1.0f + c) * 0.5f / a0, b3_hp_b[1] = -(1.0f + c) / a0;
    }
    {                                        /* the cabinet's voicing: RBJ's peaks (0: the horn's, 2: the drum's) and a
                                              * low pass (1: the horn's top) */
        static const float F[3] = {1039.0f, 2711.0f, 207.0f}, Q[3] = {0.30f, 0.50f, 1.92f}, G[3] = {8.1f, 0.0f, 11.9f};
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            const float om = 2.0f * FM_PI * F[k] / B3_RATE, al = fm_sinf(om) / (2.0f * Q[k]), c = fm_cosf(om),
                        A = fm_exp2f(G[k] * (3.321928f / 40.0f)), a0 = k == 1u ? 1.0f + al : 1.0f + al / A;
            if (k == 1u)
                b3_eq_b[k][0] = b3_eq_b[k][2] = (1.0f - c) * 0.5f / a0, b3_eq_b[k][1] = (1.0f - c) / a0,
                b3_eq_a[k][1] = (1.0f - al) / a0;
            else
                b3_eq_b[k][0] = (1.0f + al * A) / a0, b3_eq_b[k][1] = -2.0f * c / a0,
                b3_eq_b[k][2] = (1.0f - al * A) / a0, b3_eq_a[k][1] = (1.0f - al / A) / a0;
            b3_eq_a[k][0] = -2.0f * c / a0;
        }
        b3_eq_hg = fm_exp2f((B3_EQ_HORN + B3_EQ_TRIM) * (3.321928f / 20.0f));
        b3_eq_tg = fm_exp2f(B3_EQ_TRIM * (3.321928f / 20.0f));
    }
    for (w = 0; w < 9u; w++)
        for (i = 0; i < B3_NK; i++) {
            const uint32_t j = i / 12u < 4u ? i / 12u : 4u;
            const float f = (float)(i - 12u * j) * (1.0f / 12.0f),
                        h = (float)B3_TAPER[w][j] + ((float)B3_TAPER[w][j + 1u] - (float)B3_TAPER[w][j]) * f;
            b3_tap[w][i] = fm_exp2f(h * (0.5f / 6.0206f));
        }
    for (w = 0; w < 9u; w++)
        b3_xt[w] = fm_exp2f((float)B3_XT[w] * (3.321928f / 20.0f));
    for (w = 0; w < 2u; w++)
        for (i = 0; i < B3_NW; i++) {
            const int32_t n = (int32_t)(i + B3_W0) - 36;
            const uint32_t j = n <= 0 ? 0u : (uint32_t)n / 12u < 6u ? (uint32_t)n / 12u : 5u;
            const float f = n <= 0 ? 0.0f : n >= 72 ? 1.0f : (float)(n - 12 * (int32_t)j) * (1.0f / 12.0f),
                        h = (float)B3_HARM[w][j] + ((float)B3_HARM[w][j + 1u] - (float)B3_HARM[w][j]) * f;
            b3_hm[w][i] = h < -60.0f ? 0.0f : fm_exp2f(h * (1.0f / 6.0206f));
        }
    b3.bar[0][0] = b3.bar[0][1] = b3.bar[0][2] = 8;   /* 888000000, the jazz organ's */
    b3.vib[0] = 1;
    b3.perc_on = 1, b3.perc_3rd = 1, b3.perc_fast = 1, b3.scan = 6, b3.click = 64;
    b3.expr = 1.0f, b3.drive = 0.3f;
    b3.sw_lo = b3.sw_hi = b3.sw_tlo = b3.sw_thi = 1.0f;
    b3.hz[0] = b3.to[0] = 0.77f, b3.hz[1] = b3.to[1] = 0.70f;
    {
        static const uint8_t MID[8] = {64, 64, 64, 64, 64, 64, 64, 64};
        b3_rotor(MID);
    }
    b3.rng = 22222u;
    b3.cap = B3_NT;
    b3.width = 0.8f;
    b3_levels();
}

B3_IN uint32_t b3_wheel(int32_t n)          /* a MIDI note's nearest wheel, folded back into the generator */
{
    while (n < (int32_t)B3_W0)
        n += 12;
    while (n > (int32_t)(B3_W0 + B3_NW - 1u))
        n -= 12;
    return (uint32_t)n - B3_W0;
}
B3_IN uint32_t b3_bit(const uint32_t *m, uint32_t k) { return m[k >> 5] >> (k & 31u) & 1u; }
B3_IN uint32_t b3_held(uint32_t k) { return b3_bit(b3.held, k); }

uint32_t b3_down(void) { return (b3.held[0] | b3.held[1] | b3.held[2] | b3.held[3]) != 0u; }   /* (a key held: the
                                              * host's checks) */
uint32_t b3_key(uint32_t lower, uint32_t note)   /* a note's key code on a manual: the upper's its note C2..C7 (36..96),
                                              * the lower's the codes either side, 0..35 and 97..121, its key 0..60 */
{
    while (note < B3_K0)                     /* (the FM-1's keys and MIDI past the manual: by octaves onto it) */
        note += 12u;
    while (note >= B3_K0 + B3_NK)
        note -= 12u;
    return !lower ? note : note - B3_K0 < B3_K0 ? note - B3_K0 : note - B3_K0 - B3_K0 + B3_K0 + B3_NK;
}
uint32_t b3_unkey(uint32_t code, uint32_t *lower)   /* a key code's note, its manual */
{
    *lower = code < B3_K0 || code >= B3_K0 + B3_NK;
    return !*lower ? code : code < B3_K0 ? code + B3_K0 : code - B3_NK + B3_K0;
}
void b3_note(uint32_t code, uint32_t vel)   /* a key (its code, b3_key's; vel 0: up; NOTE_ALL_OFF: every one) */
{
    uint32_t k, c, lower;
    if (code >= 128u) {
        b3.held[0] = b3.held[1] = b3.held[2] = b3.held[3] = 0;
        return;
    }
    k = b3_unkey(code, &lower) - B3_K0 + (lower ? 64u : 0u);
    b3.awake = 1;
    b3.dirty = 1;
    if (vel && !b3_held(k)) {
        if (!lower && !b3.held[0] && !b3.held[1]) {   /* (none of the upper's down: the percussion strikes again) */
            b3.pk[0] = b3.pk[1] = 0;
            b3.perc = b3.perc_soft ? 0.5f : 1.0f;
            b3.perc_t = 0;
        }
        if (!lower && b3.perc_t < B3_CH * 8u)   /* (a chord's keys: struck with it) */
            b3.pk[k >> 5] |= 1u << (k & 31u);
        b3.held[k >> 5] |= 1u << (k & 31u);
    } else if (!vel)
        b3.held[k >> 5] &= ~(1u << (k & 31u));
    k -= lower ? 64u : 0u;
    c = b3.cl_next++ % B3_NCL;               /* the contacts' click: at make, softer at break */
    b3.cl[c].e = (float)b3.click * (vel ? 1.0f : 0.4f) * (1.0f / 127.0f);
    b3.cl[c].k = vel ? 0.9897f : 0.9972f;    /* (2.2 ms, 8 ms to 1/e) */
    b3.cl[c].amt = 0.25f + 0.6f * (float)k / (float)B3_NK;   /* (its band: higher for a higher key) */
}

static uint8_t b3_key_bar(uint32_t key, uint32_t a, uint32_t b)   /* a drawbar as the preset keys down have it */
{
    const uint32_t v = key & 4u ? 0u : key & 2u ? (a > b ? a : b) : key & 1u ? b : a;
    return (uint8_t)(v > 8u ? 8u : v);
}
void b3_set(const b3_patch_t *p)             /* a preset's sound (the organ's, app.c's: before the second core's job) */
{
    uint32_t b;
    for (b = 0; b < 9u; b++)                 /* (each manual's set: its B preset key's, its A#'s, both's, CANCEL's none) */
        b3.bar[0][b] = b3_key_bar(p->ub, p->ua[b], p->bar[b]), b3.bar[1][b] = b3_key_bar(p->lb, p->la[b], p->lbar[b]);
    b3.vib[0] = p->vib_on != 0u, b3.vib[1] = p->vib_lower != 0u;
    b3.perc_on = p->perc_on && (p->ub & 3u) && !(p->ub & 4u), b3.perc_3rd = p->perc_3rd != 0u;
    b3.perc_soft = p->perc_soft != 0u, b3.perc_fast = p->perc_fast != 0u;
    b3.scan = p->vib_on || p->vib_lower ? (uint8_t)(1u + (p->vib > 5u ? 5u : p->vib)) : 0u;   /* (off, V1 V2 V3, C1
                                              * C2 C3) */
    b3.drive = (float)p->drive * (1.0f / 127.0f);
    b3.click = p->click;
    b3.width = (float)p->width * (1.0f / 127.0f);
    b3_levels();
    b3.dirty = 1;
}
void b3_leslie(uint32_t fast, uint32_t brake) { b3.fast = fast != 0u, b3.brake = brake != 0u; }   /* (live) */
float b3_trim(uint32_t i, uint32_t v)        /* a trimmer's (i: b3_rotor's order) value at its setting: 64 as modelled
                                              * (the 122's), the times a half to twice it, the speeds 0.7 to 1.4 times */
{
    static const float AT[8] = {0.25f, 1.5f, 0.77f, 6.81f, 2.5f, 3.6f, 0.70f, 6.6f};   /* (a reference organ's,
                                              * measured: tools/b3_compare.py's leslie) */
    return AT[i & 7u] * fm_exp2f(((float)(v > 127u ? 127u : v) - 64.0f) * ((i & 2u) ? 0.5f / 64.0f : 1.0f / 64.0f));
}
void b3_rotor(const uint8_t t[8])            /* the Leslie's trimmers (live) */
{
    uint32_t r;
    for (r = 0; r < 2u; r++)
        b3.up[r] = b3_trim(4u * r, t[4u * r]), b3.down[r] = b3_trim(4u * r + 1u, t[4u * r + 1u]),
        b3.cho[r] = b3_trim(4u * r + 2u, t[4u * r + 2u]), b3.tre[r] = b3_trim(4u * r + 3u, t[4u * r + 3u]);
}
static volatile float b3_pk_out;             /* the last block's peak (b3_level) */
float b3_level(void) { return b3_pk_out; }
float b3_angle(uint32_t r) { return b3.ang[r & 1u]; }   /* a rotor's angle, 0..1 (the pages' animation) */
float b3_speed(uint32_t r) { return b3.hz[r & 1u]; }    /* its speed now, Hz */
void b3_expr(uint32_t v)                     /* (its gains' aims here, off the second core's fast code) */
{
    b3.expr = (float)v * (1.0f / 127.0f);
    b3.sw_tlo = fm_exp2f(-2.0f * (1.0f - b3.expr)), b3.sw_thi = fm_exp2f(-4.0f * (1.0f - b3.expr));
}   /* the expression pedal (CC 11), 0..127 (live) */
void b3_cap(uint32_t n)                      /* the wheels it may sound at most (an overload's: the quietest go first) */
{
    n = n > B3_NT ? B3_NT : n;
    if (n != b3.cap)
        b3.cap = (uint8_t)n, b3.dirty = 1;
}

static B3_HOT void b3_step(void)             /* a modulation step: each wheel's level to its target over B3_CH; the
                                              * targets again only when a key or a setting changed (dirty) or the
                                              * percussion falls */
{
    static float tg[B3_NT], td[B3_NT], tp[B3_NT];
    float pl;
    uint32_t k, b, w, m;
    const int perc = b3.perc_on && b3.perc > 1e-4f;
    b3.perc *= b3.perc_k;                    /* (the percussion's fall, a step at a time) */
    b3.perc_t += B3_CH;
    if (!b3.dirty && !perc) {                /* (nothing changed: each wheel at its target, those silent let go) */
        uint32_t n = 0;
        for (k = 0; k < b3.nact; k++) {
            w = b3.act[k];
            b3.g[w] = tg[w], b3.d[w] = td[w], b3.pg[w] = tp[w], b3.dg[w] = b3.dd[w] = b3.dpg[w] = 0.0f;
            if (tg[w] > 0.0f || td[w] > 0.0f || tp[w] > 0.0f)
                b3.act[n++] = (uint8_t)w;
        }
        b3.nact = (uint8_t)n;
        goto rotors;
    }
    b3.dirty = 0;
    for (w = 0; w < B3_NT; w++)              /* (a loop: memset is in the flash) */
        tg[w] = td[w] = tp[w] = 0.0f;
    for (m = 0; m < 2u; m++) {               /* each manual's keys: onto the scanner's bus or the dry one (its VIBRATO
                                              * tab; the scanner off, both dry) */
        const float *lv = b3.lv[m];
        float *t = b3.vib[m] && b3.scan ? tg : td;
        for (k = 0; k < B3_NK; k++)
            if (b3_held(k + 64u * m)) {
                float top = 0.0f;            /* (the key's loudest drawn bar: the crosstalk under it) */
                for (b = 0; b < 9u; b++)
                    top = lv[b] * b3_tap[b][k] > top ? lv[b] * b3_tap[b][k] : top;
                for (b = 0; b < 9u; b++)
                    if (lv[b] <= 0.0f) {
                        if (top > 0.0f)      /* (the crosstalk: an undrawn bar, while one is drawn) */
                            t[b3_wheel((int32_t)(B3_K0 + k) + B3_BAR[b] + (!b && k < 12u ? 12 : 0))] += b3_xt[b] * top;
                    } else {
                        w = b3_wheel((int32_t)(B3_K0 + k) + B3_BAR[b] + (!b && k < 12u ? 12 : 0));   /* (the 16'
                                              * folded back on the bottom octave) */
                        const float v = lv[b] * b3_tap[b][k];
                        t[w] += v;
                        if (w + 12u < B3_NT)
                            t[w + 12u] += v * b3_hm[0][w];
                        if (w + 19u < B3_NT)
                            t[w + 19u] += v * b3_hm[1][w];
                        if (B3_LEAK > 0.0f && w)
                            t[w - 1u] += lv[b] * B3_LEAK;
                        if (B3_LEAK > 0.0f && w + 1u < B3_NW)
                            t[w + 1u] += lv[b] * B3_LEAK;
                    }
            }
    }
    pl = b3.perc_on ? b3.perc : 0.0f;
    if (pl > 1e-4f)
        for (k = 0; k < B3_NK; k++)
            if (b3_bit(b3.pk, k) && b3_held(k))
                tp[b3_wheel((int32_t)(B3_K0 + k) + (b3.perc_3rd ? 19 : 12))] += pl;
    {                                        /* an overload's cap: past it, the quietest wheels' targets 0 (they fade) */
        uint32_t n = 0;
        for (w = 0; w < B3_NT; w++)
            n += tg[w] + td[w] > 0.0f;
        if (n > b3.cap) {                    /* (the level the cap's loudest reach: halved, 8 times, from the top) */
            float lo = 0.0f, hi = 0.0f, mid;
            uint32_t it, c;
            for (w = 0; w < B3_NT; w++)
                hi = tg[w] + td[w] > hi ? tg[w] + td[w] : hi;
            for (it = 0; it < 8u; it++) {
                mid = 0.5f * (lo + hi);
                for (c = 0, w = 0; w < B3_NT; w++)
                    c += tg[w] + td[w] > mid;
                if (c > b3.cap)
                    lo = mid;
                else
                    hi = mid;
            }
            for (w = 0; w < B3_NT; w++)
                if (tg[w] + td[w] <= hi)
                    tg[w] = td[w] = 0.0f;
        }
    }
    b3.nact = 0;
    for (w = 0; w < B3_NT; w++) {
        b3.dg[w] = (tg[w] - b3.g[w]) * (1.0f / B3_CH);
        b3.dd[w] = (td[w] - b3.d[w]) * (1.0f / B3_CH);
        b3.dpg[w] = (tp[w] - b3.pg[w]) * (1.0f / B3_CH);
        if (tg[w] > 0.0f || b3.g[w] > 0.0f || td[w] > 0.0f || b3.d[w] > 0.0f || tp[w] > 0.0f || b3.pg[w] > 0.0f)
            b3.act[b3.nact++] = (uint8_t)w;
    }
    if (perc)                                /* (the percussion falling: its targets again next step) */
        b3.dirty = 1;
rotors:
    for (b = 0; b < 2u; b++) {               /* the rotors: chorale or tremolo, the brake; each its own run */
        const float to = b3.brake ? 0.0f : b3.fast ? b3.tre[b] : b3.cho[b];
        const float tau = to > b3.hz[b] ? b3.up[b] : b3.down[b];
        b3.hz[b] += (to - b3.hz[b]) * ((float)B3_CH / (tau * B3_RATE));
    }
}

B3_IN float b3_tab(const float *t, uint32_t ph)   /* a wave at a phase (linear between its points) */
{
    const uint32_t j = ph >> 22;
    const float f = (float)(ph & 0x3FFFFFu) * (1.0f / 4194304.0f);
    return t[j] + (t[j + 1u] - t[j]) * f;
}
B3_IN float b3_cos(float a)          /* cos of a turn's fraction (0..1) */
{
    return b3_tab(b3_wave[1], (uint32_t)(a * 4294967040.0f) + 0x40000000u);
}
B3_IN float b3_bq(float x, float *s, const float *bb, const float *aa)   /* a biquad (x1 x2 y1 y2) */
{
    const float y = bb[0] * x + bb[1] * s[0] + bb[2] * s[1] - aa[0] * s[2] - aa[1] * s[3];
    s[1] = s[0], s[0] = x, s[3] = s[2], s[2] = y;
    return y;
}
B3_IN float b3_line(const float *l, uint32_t w, float d)   /* a rotor's line d samples back (linear) */
{
    const uint32_t i = (uint32_t)d;
    const float f = d - (float)i, a = l[(w - i) & (B3_LES_N - 1u)], b = l[(w - i - 1u) & (B3_LES_N - 1u)];
    return a + (b - a) * f;
}
B3_IN float b3_clip(float x)         /* a soft clip, no division: x - x^3/6.75 to +-1.5, flat past it */
{
    x = x > 1.5f ? 1.5f : x < -1.5f ? -1.5f : x;
    return x - x * x * x * (1.0f / 6.75f);
}

/* a step of the organ (B3_CH samples) through its Leslie, into o: the wheels a wheel at a time over the step (its
 * state held while it runs), then the rest a sample at a time, the rotors' and the scanner's angles from the step's
 * ends */
static B3_HOT void b3_chunk(float *ol, float *orr)
{
    float bus[B3_CH], dbus[B3_CH], pbus[B3_CH];
    uint32_t i, k;
    PROF2_AT(pt);
    b3_step();
    for (i = 0; i < B3_CH; i++)
        bus[i] = dbus[i] = pbus[i] = 0.0f;
    for (k = 0; k < b3.nact; k++) {          /* the wheels sounding: each a sine from its two samples before (2 cos(step)
                                              * y1 - y2), set again from its true phase at each step's start, so it never
                                              * drifts */
        const uint32_t w = b3.act[k], ph = b3.ph[w] + b3.inc[w] * b3.t;
        const float kc = b3.kc[w];
        float a = b3_tab(b3_wave[1], ph), b = b3_tab(b3_wave[1], ph + b3.inc[w]), nx;
        float g = b3.g[w], dg = b3.dg[w], d = b3.d[w], dd = b3.dd[w], pg = b3.pg[w], dpg = b3.dpg[w];
        const int dry = d != 0.0f || dd != 0.0f;
        if (pg == 0.0f && dpg == 0.0f && !dry)
            for (i = 0; i < B3_CH; i++, g += dg) {
                bus[i] += g * a;
                nx = kc * b - a, a = b, b = nx;
            }
        else if (pg == 0.0f && dpg == 0.0f && g == 0.0f && dg == 0.0f)
            for (i = 0; i < B3_CH; i++, d += dd) {
                dbus[i] += d * a;
                nx = kc * b - a, a = b, b = nx;
            }
        else
            for (i = 0; i < B3_CH; i++, g += dg, d += dd, pg += dpg) {
                bus[i] += g * a;
                dbus[i] += d * a;
                pbus[i] += pg * a;
                nx = kc * b - a, a = b, b = nx;
            }
        b3.g[w] = g, b3.d[w] = d, b3.pg[w] = pg;
    }
    b3.t += B3_CH;
    PROF2_ADD(0, pt);
    b3.quiet = b3.nact ? 0u : b3.quiet + B3_CH;
    for (k = 0; k < B3_NCL; k++)             /* the key clicks: noise, high passed by amt */
        if (b3.cl[k].e > 1e-4f)
            for (b3.quiet = 0, i = 0; i < B3_CH; i++) {
                float n;
                b3.rng = b3.rng * 1664525u + 1013904223u;
                n = (float)(int32_t)b3.rng * (1.0f / 2147483648.0f) * b3.cl[k].e;
                b3.cl[k].y1 += (n - b3.cl[k].x1 - b3.cl[k].y1) * b3.cl[k].amt;
                b3.cl[k].x1 = n;
                bus[i] += b3.cl[k].y1 * 0.5f;
                b3.cl[k].e *= b3.cl[k].k;
            }
    if (b3.quiet > B3_LES_N + B3_SCAN_N + 4096u) {   /* (silent past the lines and the preamp's settling: the chain
                                              * would make silence; the rotors and the scanner turn on alone) */
        uint32_t r;
        b3.sc_ph += 6.9f * B3_CH / B3_RATE;
        b3.sc_ph -= b3.sc_ph >= 1.0f ? 1.0f : 0.0f;
        for (r = 0; r < 2u; r++) {
            b3.ang[r] += b3.hz[r] * B3_CH / B3_RATE;
            b3.ang[r] -= b3.ang[r] >= 1.0f ? 1.0f : 0.0f;
        }
        b3.lw += B3_CH;
        b3.sc_w += B3_CH;
        for (i = 0; i < B3_CH; i++)
            ol[i] = orr[i] = 0.0f;
        return;
    }
    if (b3.scan) {                           /* the scanner: V1-3 the delay alone, C1-3 half dry; its sweep's ends */
        static const float DEPTH[3] = {0.30f, 0.55f, 0.85f};
        const uint32_t s = b3.scan - 1u, ch = s >= 3u;
        const float dp = B3_RATE * 0.00065f * DEPTH[s % 3u], d0b = B3_RATE * 0.0009f;
        float a1 = b3.sc_ph + 6.9f * B3_CH / B3_RATE, d, dd;
        if (a1 >= 1.0f)
            a1 -= 1.0f;
        d = d0b + dp * (0.5f + 0.5f * b3_cos(b3.sc_ph));
        dd = (d0b + dp * (0.5f + 0.5f * b3_cos(a1)) - d) * (1.0f / B3_CH);
        b3.sc_ph = a1;
        for (i = 0; i < B3_CH; i++, d += dd) {
            const uint32_t j = (uint32_t)d;
            const float f = d - (float)j;
            float a, b;
            b3_scan[b3.sc_w++ & (B3_SCAN_N - 1u)] = bus[i] * 0.125f;
            a = b3_scan[(b3.sc_w - 1u - j) & (B3_SCAN_N - 1u)], b = b3_scan[(b3.sc_w - 2u - j) & (B3_SCAN_N - 1u)];
            b3.sc_x += (a + (b - a) * f - b3.sc_x) * 0.75f;   /* (the line's loss: about 9.5 kHz) */
            bus[i] = ch ? 0.5f * (bus[i] * 0.125f + b3.sc_x) : b3.sc_x;
        }
    } else
        for (i = 0; i < B3_CH; i++)
            bus[i] *= 0.125f;
    PROF2_ADD(1, pt);
    {                                        /* the preamp, the crossover, the rotors heard by two microphones */
        static const float SWING[2] = {B3_HORN_SW, B3_DRUM_SW}, REF[2] = {137.0f, 295.0f},
                           AT[2][3] = {{-B3_MIC, B3_MIC, 0.5f}, {-B3_DMIC, B3_DMIC, 0.5f}};   /* (the microphones
                                              * either side of the cabinet's front; the reflection off its back, both
                                              * hear it) */
        const float g = 1.0f + 6.0f * b3.drive, bias = 0.08f + 0.2f * b3.drive, tb = b3_clip(bias),
                    og = 1.0f / (0.5f + 0.5f * g);
        float c[2][3], dc[2][3];
        uint32_t r, j;
        for (r = 0; r < 2u; r++) {           /* each rotor's angle at each microphone and its reflection's, at the
                                              * step's ends */
            float a1 = b3.ang[r] + b3.hz[r] * B3_CH / B3_RATE;
            if (a1 >= 1.0f)
                a1 -= 1.0f;
            for (j = 0; j < 3u; j++) {
                float e = a1 + AT[r][j] + 1.0f, s0 = b3.ang[r] + AT[r][j] + 1.0f;
                e -= (float)(uint32_t)e;
                s0 -= (float)(uint32_t)s0;
                c[r][j] = b3_cos(s0);
                dc[r][j] = (b3_cos(e) - c[r][j]) * (1.0f / B3_CH);
            }
            b3.ang[r] = a1;
        }
        /* the swell pedal (the AO-28's): closed, 24 dB down over 250 Hz and 12 under it, the bass kept as the ear
         * loses it; its gains slewed over the step (no steps heard as it moves) */
        const float dlo = (b3.sw_tlo - b3.sw_lo) * (1.0f / B3_CH), dhi = (b3.sw_thi - b3.sw_hi) * (1.0f / B3_CH);
        for (i = 0; i < B3_CH; i++) {
            const float raw = bus[i] + (dbus[i] + pbus[i]) * 0.125f;
            float x, y, hi, lo, out[2] = {0.0f, 0.0f};
            uint32_t m;
            b3.sw_x += (raw - b3.sw_x) * (2.0f * FM_PI * 250.0f / B3_RATE);
            x = b3.sw_x * b3.sw_lo + (raw - b3.sw_x) * b3.sw_hi;
            b3.sw_lo += dlo, b3.sw_hi += dhi;
            b3.hp_y = 0.996f * (b3.hp_y + x - b3.hp_x);   /* (the input transformer: a high pass) */
            b3.hp_x = x;
            y = (b3_clip(g * b3.hp_y + bias) - tb) * og;
#ifdef PLAT_HOST
            if (b3_test_direct) {
                ol[i] = orr[i] = y * B3_LEVEL;
                continue;
            }
#endif
            lo = b3_bq(b3_bq(y, b3.xo[0], b3_lp_b, b3_lp_a), b3.xo[1], b3_lp_b, b3_lp_a);
            hi = b3_bq(b3_bq(y, b3.xo[2], b3_hp_b, b3_lp_a), b3.xo[3], b3_hp_b, b3_lp_a);   /* (the horn's: its own
                                              * high pass; the rest (y - lo) left a bass note half its level, the
                                              * low pass's phase not the note's) */
            hi = b3_bq(b3_bq(hi, b3.eq[0], b3_eq_b[0], b3_eq_a[0]), b3.eq[1], b3_eq_b[1], b3_eq_a[1]) * b3_eq_hg;
            {                                /* (the horn's driver: its even harmonic, its DC off at about 20 Hz) */
                const float h2 = hi * hi;
                b3.hdc += (h2 - b3.hdc) * 0.003f;
                hi += B3_HORN_H2 * (h2 - b3.hdc);
            }
            lo = b3_bq(lo, b3.eq[2], b3_eq_b[2], b3_eq_a[2]) * b3_eq_tg;
            b3_les[0][b3.lw & (B3_LES_N - 1u)] = hi;
            b3_les[1][b3.lw & (B3_LES_N - 1u)] = lo;
            for (r = 0; r < 2u; r++) {       /* each microphone: the rotor's path's delay, its level by its facing;
                                              * the reflection, one for both */
                const float *l = b3_les[r];
                const float rf = (r ? 0.4f : B3_HORN_REF) * b3_line(l, b3.lw, REF[r] + SWING[r] * (1.0f - c[r][2]));
                for (m = 0; m < 2u; m++) {
                    const float cf = c[r][m];
                    const float q = 0.5f + 0.5f * cf, dir = r ? B3_DRUM_AM + (1.0f - B3_DRUM_AM) * q * q
                                                              : B3_HORN_AM + (1.0f - B3_HORN_AM) * q * q;
                    float v = b3_line(l, b3.lw, 2.0f + (!r && m ? B3_HMIC_D : 0.0f) + SWING[r] * (1.0f - cf)) * dir
                              + rf;
                    if (!r) {                /* (the horn: brighter facing the microphone) */
                        b3.hlp[m] += (v - b3.hlp[m]) * (0.3f + 0.6f * dir);
                        v = b3.hlp[m];
                    }
                    out[m] += v;
                }
                c[r][0] += dc[r][0], c[r][1] += dc[r][1], c[r][2] += dc[r][2];
            }
            b3.lw++;
            ol[i] = out[0] * B3_LEVEL;
            orr[i] = out[1] * B3_LEVEL;
        }
    }
    PROF2_ADD(2, pt);
}

/* the organ's block, after the RMI's: its middle (left and right's mean) into mid, its sides (half their difference,
 * by the width) into side (the second core's job; b3_note and b3_set before it, in the render; n a multiple of
 * B3_CH); 1 if it sounds (else both silent) */
B3_HOT uint32_t b3_job(float *mid, float *side, uint32_t n)
{
    float ol[B3_CH], orr[B3_CH], pk = 0.0f;
    uint32_t i, j;
    for (i = 0; i < n; i++)
        mid[i] = side[i] = 0.0f;
    b3_pk_out = 0.0f;
    if (!b3.awake)
        return 0;
    if (b3.quiet > B3_LES_N + B3_SCAN_N + 4096u && !b3.dirty && !b3.nact) {   /* (silent: the rotors turn on alone) */
        for (i = 0; i + B3_CH <= n; i += B3_CH)
            b3_chunk(ol, orr);
        return 0;
    }
    for (i = 0; i + B3_CH <= n; i += B3_CH) {
        b3_chunk(ol, orr);
        for (j = 0; j < B3_CH; j++) {
            mid[i + j] = 0.5f * (ol[j] + orr[j]);
            side[i + j] = 0.5f * b3.width * (ol[j] - orr[j]);
            {                                /* (no call: the fast code's) */
                const float a = mid[i + j] < 0.0f ? -mid[i + j] : mid[i + j];
                pk = a > pk ? a : pk;
            }
        }
    }
    b3_pk_out = pk;
    return 1;
}
#endif
