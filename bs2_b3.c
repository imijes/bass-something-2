/* SPDX-License-Identifier: MPL-2.0 */
/* A tonewheel organ after the Hammond B-3, and its Leslie 122: a CPU PROTOTYPE (BS2_B3_PROTO), no panel. Played by
 * the piano's keys and channel beside the RMI (rmi_route), and alone on MIDI channel 16 (b3_cc: its test controls);
 * made on the second core after the RMI's block, into its buffer (b3_job). Our own code, from how the organ works:
 *   the generator: 91 wheels always turning (C1 = 32.7 Hz up, equal tempered), a phase each, never reset; a wheel's
 *     wave a sine with a little 2nd and 3rd (its pickup's), the top wheels pure; only the wheels sounding are made
 *   the manual: 61 keys (C2..C7), 9 drawbars a key, each bar the wheel nearest its harmonic (16' 5 1/3' 8' 4' 2 2/3'
 *     2' 1 3/5' 1 1/3' 1': -12 +7 0 +12 +19 +24 +28 +31 +36 semitones), folded back by octaves at the generator's
 *     ends; a step of a drawbar about 3 dB; two keys on one wheel add, as on the busbar; leakage: a wheel's
 *     neighbours a little (B3_LEAK)
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
#if BS2_B3_PROTO                             /* (off: nothing of it built) */
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
#define B3_LEVEL 2.0f                        /* into the RMI's buffer (its level 1) */
static const int8_t B3_BAR[9] = {-12, 7, 0, 12, 19, 24, 28, 31, 36};
static float b3_wave[2][B3_TB + 1u] PLAT_POOL;   /* the low wheels' (a little 2nd and 3rd), the high wheels' (sine) */
static float b3_scan[B3_SCAN_N] PLAT_POOL;
static float b3_les[2][B3_LES_N] PLAT_POOL;      /* the horn's line, the drum's */
static struct {
    uint32_t ph[B3_NW], inc[B3_NW];          /* the wheels: each one's phase at t 0, its step (its phase: ph + inc * t) */
    float kc[B3_NW];                         /* 2 cos(its step): its sine a sample on from the two before */
    uint32_t t;                              /* the generator's samples (the wheels turn without being made) */
    float g[B3_NW], dg[B3_NW];               /* each wheel's level on the bus, its step a sample to the target */
    float pg[B3_NW], dpg[B3_NW];             /* the percussion's, on its own bus */
    uint8_t act[B3_NW], nact;                /* the wheels sounding or ramping */
    uint32_t held[2];                        /* the keys down (61), a bit each */
    uint32_t pk[2];                          /* the keys the percussion struck */
    float lv[9];                             /* each drawbar's level (b3_levels) */
    float perc, perc_k;                      /* its envelope, its fall a sample */
    uint32_t perc_t;                         /* samples since it struck (a chord's keys join it within B3_CH * 8) */
    uint8_t bar[9];                          /* the drawbars, 0..8 */
    uint8_t perc_on, perc_3rd, perc_soft, perc_fast, scan, click;
    float expr, drive;
    struct { float e, k, x1, y1, y2, amt; } cl[B3_NCL];   /* the clicks: their noise's level and fall, a band pass */
    uint32_t cl_next, rng;
    float sc_ph, sc_x;                       /* the scanner: its rotor (0..1), its line's low pass */
    uint32_t sc_w;
    float hp_x, hp_y;                        /* the preamp's input high pass */
    float xo[2][4];                          /* the crossover's biquads' states (x1 x2 y1 y2) */
    float hz[2], to[2], ang[2];              /* the rotors: speed now, its aim, the angle (0..1) */
    float hlp;                               /* the horn's facing low pass */
    uint32_t lw;
    uint8_t fast, brake, awake;
    uint8_t tab;                             /* (test, CC 90: the wheels from the table, not the recurrence) */
    uint8_t dirty;                           /* a key or a setting changed: the targets again (b3_step) */
    uint8_t cap;                             /* the wheels it may sound at most (b3_cap: an overload's) */
    uint32_t quiet;                          /* samples its input has been silent (past the lines: the chain skipped) */
} b3;
static float b3_lp_b[3], b3_lp_a[2];     /* the crossover's Butterworth, 800 Hz */

static void b3_levels(void)                  /* the drawbars' levels (3 dB a step; percussion on: the 1' bar off), the
                                              * percussion's fall a step: from the settings, here, not on the second core */
{
    uint32_t b;
    for (b = 0; b < 9u; b++)
        b3.lv[b] = b3.bar[b] && !(b == 8u && b3.perc_on) ? fm_exp2f(((float)b3.bar[b] - 8.0f) * 0.5f) : 0.0f;
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
    for (w = 0; w < B3_NW; w++)
        b3.inc[w] = (uint32_t)(32.70319566f * fm_exp2f((float)w / 12.0f) / B3_RATE * 4294967040.0f),
        b3.ph[w] = (uint32_t)w * 2654435761u,   /* (each wheel somewhere of its own) */
        b3.kc[w] = 2.0f * fm_cosf(2.0f * FM_PI * (float)b3.inc[w] * (1.0f / 4294967296.0f));
    {                                        /* the crossover: RBJ's Butterworth (Q 0.7071) low pass, twice (the high:
                                              * the rest) */
        const float om = 2.0f * FM_PI * 800.0f / B3_RATE, al = fm_sinf(om) / (2.0f * 0.70710678f), c = fm_cosf(om),
                    a0 = 1.0f + al;
        b3_lp_b[0] = b3_lp_b[2] = (1.0f - c) * 0.5f / a0, b3_lp_b[1] = (1.0f - c) / a0;
        b3_lp_a[0] = -2.0f * c / a0, b3_lp_a[1] = (1.0f - al) / a0;
    }
    b3.bar[0] = b3.bar[1] = b3.bar[2] = 8;   /* 888000000, the jazz organ's */
    b3.perc_on = 1, b3.perc_3rd = 1, b3.perc_fast = 1, b3.scan = 6, b3.click = 64;
    b3.expr = 1.0f, b3.drive = 0.3f;
    b3.hz[0] = b3.to[0] = 0.83f, b3.hz[1] = b3.to[1] = 0.67f;
    b3.rng = 22222u;
    b3.cap = B3_NW;
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

void b3_note(uint32_t note, uint32_t vel)   /* a key (vel 0: up; NOTE_ALL_OFF: every one) */
{
    uint32_t k, c;
    if (note >= 128u) {
        b3.held[0] = b3.held[1] = 0;
        return;
    }
    while (note < B3_K0)                     /* (the FM-1's keys and MIDI past the manual: by octaves onto it) */
        note += 12u;
    while (note >= B3_K0 + B3_NK)
        note -= 12u;
    k = note - B3_K0;
    b3.awake = 1;
    b3.dirty = 1;
    if (vel && !b3_held(k)) {
        if (!b3.held[0] && !b3.held[1]) {    /* (none down: the percussion strikes again) */
            b3.pk[0] = b3.pk[1] = 0;
            b3.perc = b3.perc_soft ? 0.5f : 1.0f;
            b3.perc_t = 0;
        }
        if (b3.perc_t < B3_CH * 8u)          /* (a chord's keys: struck with it) */
            b3.pk[k >> 5] |= 1u << (k & 31u);
        b3.held[k >> 5] |= 1u << (k & 31u);
    } else if (!vel)
        b3.held[k >> 5] &= ~(1u << (k & 31u));
    c = b3.cl_next++ % B3_NCL;               /* the contacts' click: at make, softer at break */
    b3.cl[c].e = (float)b3.click * (vel ? 1.0f : 0.4f) * (1.0f / 127.0f);
    b3.cl[c].k = vel ? 0.9897f : 0.9972f;    /* (2.2 ms, 8 ms to 1/e) */
    b3.cl[c].amt = 0.25f + 0.6f * (float)k / (float)B3_NK;   /* (its band: higher for a higher key) */
}

void b3_cc(uint32_t c, uint32_t v)           /* its test controls (MIDI channel 16) */
{
    if (c >= 12u && c <= 20u)
        b3.bar[c - 12u] = (uint8_t)((v * 8u + 63u) / 127u);
    else if (c == 21u)
        b3.perc_on = v >= 64u;
    else if (c == 22u)
        b3.perc_3rd = v >= 64u;
    else if (c == 23u)
        b3.perc_soft = v >= 64u;
    else if (c == 24u)
        b3.perc_fast = v >= 64u;
    else if (c == 25u)                       /* off, V1 V2 V3, C1 C2 C3 */
        b3.scan = (uint8_t)(v * 6u / 127u);
    else if (c == 26u)
        b3.drive = (float)v * (1.0f / 127.0f);
    else if (c == 28u)
        b3.click = (uint8_t)v;
    else if (c == 1u)                        /* (the mod wheel: the Leslie fast) */
        b3.fast = v >= 64u;
    else if (c == 27u)
        b3.brake = v >= 64u;
    else if (c == 11u)
        b3.expr = (float)v * (1.0f / 127.0f);
    else if (c == 90u)
        b3.tab = v >= 64u;
    b3_levels();
    b3.dirty = 1;
}
void b3_cap(uint32_t n)                      /* the wheels it may sound at most (an overload's: the quietest go first) */
{
    n = n > B3_NW ? B3_NW : n;
    if (n != b3.cap)
        b3.cap = (uint8_t)n, b3.dirty = 1;
}

static B3_HOT void b3_step(void)             /* a modulation step: each wheel's level to its target over B3_CH; the
                                              * targets again only when a key or a setting changed (dirty) or the
                                              * percussion falls */
{
    static float tg[B3_NW], tp[B3_NW];
    const float *lv = b3.lv;
    float pl;
    uint32_t k, b, w;
    const int perc = b3.perc_on && b3.perc > 1e-4f;
    b3.perc *= b3.perc_k;                    /* (the percussion's fall, a step at a time) */
    b3.perc_t += B3_CH;
    if (!b3.dirty && !perc) {                /* (nothing changed: each wheel at its target, those silent let go) */
        uint32_t n = 0;
        for (k = 0; k < b3.nact; k++) {
            w = b3.act[k];
            b3.g[w] = tg[w], b3.pg[w] = tp[w], b3.dg[w] = b3.dpg[w] = 0.0f;
            if (tg[w] > 0.0f || tp[w] > 0.0f)
                b3.act[n++] = (uint8_t)w;
        }
        b3.nact = (uint8_t)n;
        goto rotors;
    }
    b3.dirty = 0;
    for (w = 0; w < B3_NW; w++)              /* (a loop: memset is in the flash) */
        tg[w] = tp[w] = 0.0f;
    for (k = 0; k < B3_NK; k++)
        if (b3_held(k)) {
            for (b = 0; b < 9u; b++)
                if (lv[b] > 0.0f) {
                    w = b3_wheel((int32_t)(B3_K0 + k) + B3_BAR[b]);
                    tg[w] += lv[b];
                    if (B3_LEAK > 0.0f && w)
                        tg[w - 1u] += lv[b] * B3_LEAK;
                    if (B3_LEAK > 0.0f && w + 1u < B3_NW)
                        tg[w + 1u] += lv[b] * B3_LEAK;
                }
        }
    pl = b3.perc_on ? b3.perc : 0.0f;
    if (pl > 1e-4f)
        for (k = 0; k < B3_NK; k++)
            if (b3_bit(b3.pk, k) && b3_held(k))
                tp[b3_wheel((int32_t)(B3_K0 + k) + (b3.perc_3rd ? 19 : 12))] += pl;
    {                                        /* an overload's cap: past it, the quietest wheels' targets 0 (they fade) */
        uint32_t n = 0;
        for (w = 0; w < B3_NW; w++)
            n += tg[w] > 0.0f;
        if (n > b3.cap) {                    /* (the level the cap's loudest reach: halved, 8 times, from the top) */
            float lo = 0.0f, hi = 0.0f, m;
            uint32_t it, c;
            for (w = 0; w < B3_NW; w++)
                hi = tg[w] > hi ? tg[w] : hi;
            for (it = 0; it < 8u; it++) {
                m = 0.5f * (lo + hi);
                for (c = 0, w = 0; w < B3_NW; w++)
                    c += tg[w] > m;
                if (c > b3.cap)
                    lo = m;
                else
                    hi = m;
            }
            for (w = 0; w < B3_NW; w++)
                if (tg[w] <= hi)
                    tg[w] = 0.0f;
        }
    }
    b3.nact = 0;
    for (w = 0; w < B3_NW; w++) {
        b3.dg[w] = (tg[w] - b3.g[w]) * (1.0f / B3_CH);
        b3.dpg[w] = (tp[w] - b3.pg[w]) * (1.0f / B3_CH);
        if (tg[w] > 0.0f || b3.g[w] > 0.0f || tp[w] > 0.0f || b3.pg[w] > 0.0f)
            b3.act[b3.nact++] = (uint8_t)w;
    }
    if (perc)                                /* (the percussion falling: its targets again next step) */
        b3.dirty = 1;
rotors:
    for (b = 0; b < 2u; b++) {               /* the rotors: chorale or tremolo, the brake; each its own run */
        static const float CHO[2] = {0.83f, 0.67f}, TRE[2] = {6.75f, 5.75f}, UP[2] = {0.6f, 3.5f},
                           DOWN[2] = {1.3f, 4.5f};
        const float to = b3.brake ? 0.0f : b3.fast ? TRE[b] : CHO[b];
        const float tau = to > b3.hz[b] ? UP[b] : DOWN[b];
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
static B3_HOT void b3_chunk(float *o)
{
    float bus[B3_CH], pbus[B3_CH];
    uint32_t i, k;
    b3_step();
    for (i = 0; i < B3_CH; i++)
        bus[i] = pbus[i] = 0.0f;
    for (k = 0; k < b3.nact; k++) {          /* the wheels sounding: each a sine from its two samples before (2 cos(step)
                                              * y1 - y2), set again from its true phase at each step's start, so it never
                                              * drifts */
        const uint32_t w = b3.act[k], ph = b3.ph[w] + b3.inc[w] * b3.t;
        const float kc = b3.kc[w];
        float a = b3_tab(b3_wave[1], ph), b = b3_tab(b3_wave[1], ph + b3.inc[w]), nx;
        float g = b3.g[w], dg = b3.dg[w], pg = b3.pg[w], dpg = b3.dpg[w];
        if (b3.tab) {                        /* (test: the table's way, a lookup a sample) */
            const uint32_t inc = b3.inc[w];
            const float *tb = b3_wave[w >= 60u];
            uint32_t p = ph;
            for (i = 0; i < B3_CH; i++, p += inc, g += dg, pg += dpg) {
                const float v = b3_tab(tb, p);
                bus[i] += g * v;
                pbus[i] += pg * v;
            }
        } else if (pg == 0.0f && dpg == 0.0f)
            for (i = 0; i < B3_CH; i++, g += dg) {
                bus[i] += g * a;
                nx = kc * b - a, a = b, b = nx;
            }
        else
            for (i = 0; i < B3_CH; i++, g += dg, pg += dpg) {
                bus[i] += g * a;
                pbus[i] += pg * a;
                nx = kc * b - a, a = b, b = nx;
            }
        b3.g[w] = g, b3.pg[w] = pg;
    }
    b3.t += B3_CH;
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
            o[i] = 0.0f;
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
    {                                        /* the preamp, the crossover, the rotors */
        static const float SWING[2] = {19.0f, 6.0f}, REF[2] = {137.0f, 295.0f};
        const float g = 1.0f + 6.0f * b3.drive, bias = 0.08f + 0.2f * b3.drive, tb = b3_clip(bias),
                    og = 1.0f / (0.5f + 0.5f * g);
        float c[2][2], dc[2][2];
        uint32_t r, j;
        for (r = 0; r < 2u; r++) {           /* each rotor's two angles (its face, its reflection a quarter turn on)
                                              * at the step's ends */
            float a1 = b3.ang[r] + b3.hz[r] * B3_CH / B3_RATE;
            if (a1 >= 1.0f)
                a1 -= 1.0f;
            for (j = 0; j < 2u; j++) {
                float e = a1 + 0.25f * (float)j;
                float s0 = b3.ang[r] + 0.25f * (float)j;
                e -= e >= 1.0f ? 1.0f : 0.0f;
                s0 -= s0 >= 1.0f ? 1.0f : 0.0f;
                c[r][j] = b3_cos(s0);
                dc[r][j] = (b3_cos(e) - c[r][j]) * (1.0f / B3_CH);
            }
            b3.ang[r] = a1;
        }
        for (i = 0; i < B3_CH; i++) {
            const float x = (bus[i] + pbus[i] * 0.125f) * b3.expr;
            float y, hi, lo, out = 0.0f;
            b3.hp_y = 0.996f * (b3.hp_y + x - b3.hp_x);   /* (the input transformer: a high pass) */
            b3.hp_x = x;
            y = (b3_clip(g * b3.hp_y + bias) - tb) * og;
            lo = b3_bq(b3_bq(y, b3.xo[0], b3_lp_b, b3_lp_a), b3.xo[1], b3_lp_b, b3_lp_a);
            hi = y - lo;                     /* (the horn's: the rest, so the two add back to the whole) */
            b3_les[0][b3.lw & (B3_LES_N - 1u)] = hi;
            b3_les[1][b3.lw & (B3_LES_N - 1u)] = lo;
            for (r = 0; r < 2u; r++) {       /* its path's delay, its level by its facing; the reflections */
                const float *l = b3_les[r], cf = c[r][0];
                const float dir = r ? 0.75f + 0.25f * cf * cf : 0.22f + 0.78f * (0.5f + 0.5f * cf) * (0.5f + 0.5f * cf);
                float s = b3_line(l, b3.lw, 2.0f + SWING[r] * (1.0f - cf)) * dir
                          + 0.4f * b3_line(l, b3.lw, REF[r] + SWING[r] * (1.0f - c[r][1]));   /* (one reflection) */
                if (!r) {                    /* (the horn: brighter facing the microphone) */
                    b3.hlp += (s - b3.hlp) * (0.3f + 0.6f * dir);
                    s = b3.hlp;
                }
                out += s;
                c[r][0] += dc[r][0], c[r][1] += dc[r][1];
            }
            b3.lw++;
            o[i] = out * B3_LEVEL;
        }
    }
}

/* the organ's block, after the RMI's, added into buf (the second core's job; b3_note and b3_cc before it, in the
 * render; n a multiple of B3_CH); 1 if it sounds */
B3_HOT uint32_t b3_job(float *buf, uint32_t n)
{
    float o[B3_CH];
    uint32_t i, j;
    if (!b3.awake)
        return 0;
    if (b3.quiet > B3_LES_N + B3_SCAN_N + 4096u && !b3.dirty && !b3.nact) {   /* (silent: the rotors turn on alone) */
        float o2[B3_CH];
        for (i = 0; i + B3_CH <= n; i += B3_CH)
            b3_chunk(o2);
        return 0;
    }
    for (i = 0; i + B3_CH <= n; i += B3_CH) {
        b3_chunk(o);
        for (j = 0; j < B3_CH; j++)
            buf[i + j] += o[j];
    }
    return 1;
}
#endif
