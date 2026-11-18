/* SPDX-License-Identifier: MPL-2.0 */
/* The organ (the fourth instrument, after the Hammond B-3) and its Leslie 122: its sound from a preset (b3_set), its
 * keys (b3_note), the Leslie's speed and the expression live; made on the second core after the RMI's block (b3_job),
 * a unit of its own at -O2. Our own code, from how the organ works:
 *   the generator: 91 wheels always turning (C1 = 32.7 Hz up, equal tempered), a phase each, never reset; a wheel's
 *     wave a sine and its own exact 2nd and 3rd (B3_HARM); only the wheels sounding are made
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
#define B3_NT B3_NW                          /* the wheels made (each its own harmonics: none made for them alone) */
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
#define B3_PRE_HR 0.4f                       /* the preamp at DRIVE 0: its input this much into the clip and made up after
                                              * (a full chord bent it: sum tones 42 dB down, a reference organ's 60) */
#define B3_RICH 0.005f                       /* a wheel's level under which its harmonics are not made (-46 dB: the
                                              * crosstalk's wheels) */
#define B3_EQ_TRIM -6.7f                     /* the whole, dB: as loud as before it (the organ's mix kept) */
#define B3_HORN_SW 5.1f                      /* each rotor's Doppler: its path's swing, samples (the horn's, the
                                              * drum's) */
#define B3_DRUM_SW 8.5f
#define B3_HORN_AM 0.7f                      /* each rotor's level turned away, to facing (its tremolo) */
#define B3_DRUM_AM 0.2f
#define B3_MIC 0.25f                         /* the microphones either side of the front, a turn's (a quarter: the
                                              * sides), */
#define B3_DMIC 0.06f                        /* the horn's and the drum's (the drum's louvres heard nearly as one) */
#define B3_HORN_REF 0.8f                     /* the horn's paths off the cabinet's back, each microphone's own (the
                                              * drum's one, 0.4, both hear), */
#define B3_HORN_RSW 20.0f                    /* the walls' paths' swing, samples (longer than the horn's own: their
                                              * phases against it turn through, so their sum notches) */
#define B3_HORN_REF2 0.6f                    /* and off its side walls, each */
#define B3_HMIC_D 8.0f                       /* the right microphone that much farther from the horn, samples (the two
                                              * apart: a reference organ's L/R) */
/* the noises (a reference organ's, measured with each alone at its full): its keys' thud a low thump (75 Hz, 40 ms)
 * at make and at break, about 18 dB under a note; the floor's 60 Hz hum and its harmonics and a faint hiss; the
 * rotors' wind, a rumble (30-250 Hz, falling past 1 kHz) at the fast speed, none at the slow; a drawbar's or a tab's
 * clunk (300-500 Hz, 20 ms) */
#define B3_THUD 0.078f                        /* the thud's strike at its full */
#define B3_HUM 0.0072f                       /* the hum's level, the hiss's */
#define B3_HISS 0.0024f
#define B3_WIND_FREE 0.018f                   /* and each microphone's own, round the cabinet */
#define B3_WIND_AIR 0.15f                    /* its air: a one pole's over the rumble's four, falling past 900 Hz */
#define B3_WIND_HORN 0.0015f                  /* the wind on the horn's path (its middle, 200 Hz-2 kHz): it turns
                                              * with the horn, its Doppler, its mouths, its walls */
#define B3_WHIRR 0.014f                       /* the rotor's whirr, to the rumble's noise */
#define B3_MOUTHS 0.0f                       /* its swell as the horn's mouths pass */
#define B3_BROOK 40.0f                       /* its tone's wander (the cutoff's, about 3 Hz) */
#define B3_CLUNK 0.00135f                       /* a clunk's */
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
 * the richest: made from its sine, exactly twice and three times it (the tempered drawbars beat against them) */
static const int8_t B3_HARM[2][7] = {{-14, -23, -32, -43, -42, -34, -32}, {-40, -43, -46, -50, -53, -65, -57}};
static float b3_hm[2][B3_NW] PLAT_POOL;
/* the busbars' crosstalk: a key down, every drawbar of its manual not drawn still heard faintly while one is (none:
 * silence, as a B-3's), dB under the key's loudest drawn bar (a reference organ's, measured: its undrawn 1 1/3' and
 * 1' 45-55 dB down, its 1 3/5' about 65); its wheels made plain (no harmonics) */
static const int8_t B3_XT[9] = {-52, -52, -52, -52, -52, -52, -65, -50, -52};
static float b3_xt[9];                       /* it as gains (b3_init's) */
/* MOJO: this organ's quirks (its own, from its story). A B-3 off the road: a generator rebuild left two tonewheels a
 * little off their centres, the A2 and the E3, their fields reaching the pickups near them, so they bleed into the
 * keys a fourth, a fifth or an octave from them; and a tired harness lets a wheel or two more whisper in on about a
 * third of the keys, no two manuals alike. No tech ever found it all; the players asked them not to. Each key's (both
 * manuals'): up to four wheels and their levels under its loudest drawn bar, made once (b3_init) from the organ's
 * serial; heard while a bar is drawn, by MOJO. Its other quirks, each a CHARACTER alone or several together: busbar
 * contacts gone dull (a drawbar on a key in four weak, its click scratchier), a tired power supply (the hum up, the
 * level sagging under a big chord), a tired Leslie motor (slower, lazier, never quite steady), its valves run warm
 * (more drive, more even harmonics) */
#define B3_MJ_N 4u
static uint8_t b3_mj_w[2][B3_NK][B3_MJ_N];  /* the wheels (B3_NW: none) */
static float b3_mj_g[2][B3_NK][B3_MJ_N];    /* their levels */
static uint8_t b3_mj_cb[2][B3_NK];          /* each key's dull contact: its drawbar (9: none), its loss at full */
static float b3_mj_cd[2][B3_NK];
enum { MJ_WHEELS, MJ_CONTACTS, MJ_SAG, MJ_MOTOR, MJ_WARM, MJ_N };
static const struct {
    const char *name;
    uint8_t q[MJ_N];                         /* each quirk's part, % */
} B3_CHAR[B3_NCHAR] = {
    {"LOOSE WHEELS", {100, 0, 0, 0, 0}}, {"DIRTY CONTACTS", {0, 100, 0, 0, 0}}, {"HUM + SAG", {0, 0, 100, 0, 0}},
    {"TIRED MOTOR", {0, 0, 0, 100, 0}},  {"TUBE WARMTH", {0, 0, 0, 0, 100}},     {"ROAD CASE", {80, 60, 30, 50, 40}},
    {"CHURCH", {30, 20, 10, 30, 80}},    {"JUKE JOINT", {60, 50, 100, 40, 100}}, {"BARN FIND", {100, 100, 70, 100, 50}},
};
const char *b3_mojo_name(uint32_t c) { return B3_CHAR[c < B3_NCHAR ? c : 0u].name; }
static void b3_mojo_init(void)
{
    uint32_t m, k, n, rng = 0x1963B3u;       /* (its serial) */
    for (m = 0; m < 2u; m++)
        for (k = 0; k < B3_NK; k++) {
            const uint32_t pc = (B3_K0 + k) % 12u;
            const float mid = 1.0f - 0.5f * fm_fabsf(((float)k - 30.0f) / 30.0f);   /* (strongest mid keyboard) */
            n = 0;
            for (uint32_t i = 0; i < B3_MJ_N; i++)
                b3_mj_w[m][k][i] = B3_NW, b3_mj_g[m][k][i] = 0.0f;
            if (pc == 9u || pc == 2u || pc == 4u)   /* the loose A2 (wheel 21): A, D, E keys */
                b3_mj_w[m][k][n] = 21u, b3_mj_g[m][k][n++] = 0.08f * mid;   /* (-22 dB mid keyboard) */
            if (pc == 4u || pc == 9u || pc == 11u)  /* the loose E3 (wheel 28): E, A, B keys */
                b3_mj_w[m][k][n] = 28u, b3_mj_g[m][k][n++] = 0.063f * mid;
            rng = rng * 1664525u + 1013904223u;   /* (a key in four a dull contact: a drawbar, a third to all lost) */
            b3_mj_cb[m][k] = (rng >> 24) < 64u ? (uint8_t)((rng >> 8) % 9u) : 9u;
            b3_mj_cd[m][k] = 0.35f + 0.65f * (float)((rng >> 16) & 255u) * (1.0f / 255.0f);
            for (uint32_t i = 0; i < 2u && n < B3_MJ_N; i++) {   /* the harness: a third of the keys a wheel, an eighth
                                              * two */
                static const int8_t OFF[8] = {-17, -12, -7, -5, 5, 7, 12, 19};
                rng = rng * 1664525u + 1013904223u;
                if ((rng >> 24) >= (i ? 32u : 85u))
                    break;
                {
                    const int32_t w = (int32_t)(B3_K0 + k) + OFF[(rng >> 8) & 7u] - (int32_t)B3_W0;
                    if (w >= 0 && w < (int32_t)B3_NW)
                        b3_mj_w[m][k][n] = (uint8_t)w,
                        b3_mj_g[m][k][n++] = fm_exp2f(-(28.0f + 10.0f * (float)((rng >> 16) & 255u) / 255.0f)
                                                      * (3.321928f / 20.0f));   /* (-28..-38 dB) */
                }
            }
        }
}      /* it for each wheel (b3_init's), a gain (0: under -60 dB) */
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
    float mojo, mq[MJ_N];                    /* MOJO, 0..1 (b3_set's); each quirk's part of it, its CHARACTER's */
    float mo_ph;                             /* the tired motor's wander: its phase */
    float n_thud, n_floor, n_rotor, n_mech;  /* the noises' levels, 0..1 (b3_noise's) */
    float th_in, th1, th2;                   /* the thud: its strike to come, its resonator */
    float mc_e, mc1, mc2;                    /* the clunk: its burst's level, its band pass */
    float hum_ph, hs1;                       /* the floor: its hum's phase, its hiss's low pass */
    float wf[2], wf2[2], wf3[2], wf4[2], wfa[2];   /* the wind, each microphone's: its rumble's four poles, its air */
    float bub[2], bub2[2], whr[2], wph;      /* its tone's wander; its whirr, its phase (the horn's mouths') */
    float hw0, hw1, hw2;                     /* the horn's wind: its band's one poles */
    float gust, gust2;                       /* the wind's slow gusts (twice low passed noise) */
    uint32_t nrng;                           /* (the noises' own) */
    uint8_t lastbar[2][9], lastsw;           /* (the drawbars and tabs last set: a change the clunk's) */
    float dlo, dout[2], dmid[2];             /* the drum's at half the rate: the pair's first, each microphone's last
                                              * and halfway */
    uint32_t dw;                             /* its line's write */
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
            const float om = 2.0f * FM_PI * F[k] / (k == 2u ? 0.5f * B3_RATE : B3_RATE),   /* (the drum's at half) */ al = fm_sinf(om) / (2.0f * Q[k]), c = fm_cosf(om),
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
    b3_mojo_init();
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
    if (b3.n_thud > 0.0f) {                  /* the key's thud: at make, at break a little softer, each its own */
        b3.nrng = b3.nrng * 1664525u + 1013904223u;
        b3.th_in += b3.n_thud * B3_THUD * (vel ? 1.0f : 0.8f)
                    * (0.7f + 0.6f * (float)(b3.nrng >> 8) * (1.0f / 16777216.0f));
    }
    c = b3.cl_next++ % B3_NCL;               /* the contacts' click: at make, softer at break (a dull contact's
                                              * louder, scratchier, longer) */
    {
        const float dc = b3_mj_cb[lower != 0u][k] < 9u ? b3.mq[MJ_CONTACTS] : 0.0f;
        b3.cl[c].e = ((float)b3.click * (1.0f / 127.0f) + 0.5f * dc) * (vel ? 1.0f : 0.4f);
        b3.cl[c].k = (vel ? 0.9897f : 0.9972f) + 0.0075f * dc * (vel ? 1.0f : 0.3f);   /* (2.2 ms, 8 ms to 1/e) */
    }
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
    b3.click = p->click;
    b3.mojo = (float)p->mojo * (1.0f / 127.0f);
    for (b = 0; b < MJ_N; b++)
        b3.mq[b] = b3.mojo * (float)B3_CHAR[p->mchar < B3_NCHAR ? p->mchar : 0u].q[b] * 0.01f;
    b3.drive = (float)p->drive * (1.0f / 127.0f) + 0.25f * b3.mq[MJ_WARM];   /* (the warm valves: more drive) */
    b3.drive = b3.drive > 1.0f ? 1.0f : b3.drive;
    {                                        /* (a drawbar or a tab moved: its clunk) */
        uint32_t m2, b2, ch = 0;
        const uint8_t sw = (uint8_t)(b3.perc_on | b3.perc_3rd << 1 | b3.perc_soft << 2 | b3.perc_fast << 3
                                     | b3.vib[0] << 4 | b3.vib[1] << 5);
        for (m2 = 0; m2 < 2u; m2++)
            for (b2 = 0; b2 < 9u; b2++)
                ch += b3.lastbar[m2][b2] != b3.bar[m2][b2], b3.lastbar[m2][b2] = b3.bar[m2][b2];
        ch += sw != b3.lastsw, b3.lastsw = sw;
        if (ch && b3.n_mech > 0.0f)
            b3.mc_e = b3.n_mech * B3_CLUNK, b3.awake = 1, b3.quiet = 0;
    }
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
void b3_noise(uint32_t thud, uint32_t floor, uint32_t rotor, uint32_t mech)
{
    b3.n_thud = (float)thud * (1.0f / 127.0f), b3.n_floor = (float)floor * (1.0f / 127.0f);
    b3.n_rotor = (float)rotor * (1.0f / 127.0f), b3.n_mech = (float)mech * (1.0f / 127.0f);
    b3.awake = 1;                            /* (the floor and the wind heard from now) */
}
void b3_cap(uint32_t n)                      /* the wheels it may sound at most (an overload's: the quietest go first) */
{
    n = n > B3_NT ? B3_NT : n;
    if (n != b3.cap)
        b3.cap = (uint8_t)n, b3.dirty = 1;
}

B3_IN float b3_spin(float s)                /* the rotors' noise at a speed (1: fast): chorale 7 dB under tremolo,
                                              * none stopped */
{
    return s * 3.8f < 0.35f + 0.65f * s ? s * 3.8f : 0.35f + 0.65f * s;
}
B3_IN float b3_tri(float a)                 /* a triangle at a turn's fraction (0..2: wrapped), -1..1 */
{
    a -= a >= 1.0f ? 1.0f : 0.0f;
    return 4.0f * (a < 0.5f ? a : 1.0f - a) - 1.0f;
}
static B3_HOT void b3_step(void)             /* a modulation step: each wheel's level to its target over B3_CH; the
                                              * targets again only when a key or a setting changed (dirty) or the
                                              * percussion falls */
{
    static float tg[B3_NT], td[B3_NT], tp[B3_NT];
    float pl, load = 0.0f;                   /* (the keys' loudest bars, summed: the tired supply's sag) */
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
                if (top > 0.0f && b3.mq[MJ_WHEELS] > 0.0f)   /* (MOJO: its quirks' wheels under it) */
                    for (b = 0; b < B3_MJ_N && b3_mj_w[m][k][b] < B3_NW; b++)
                        t[b3_mj_w[m][k][b]] += b3_mj_g[m][k][b] * top * b3.mq[MJ_WHEELS];
                load += top;
                for (b = 0; b < 9u; b++)
                    if (lv[b] <= 0.0f) {
                        if (top > 0.0f && b >= 5u)   /* (the crosstalk: an undrawn bar, while one is drawn; the 2'
                                              * and up's: the lower bars' under the harmonics) */
                            t[b3_wheel((int32_t)(B3_K0 + k) + B3_BAR[b] + (!b && k < 12u ? 12 : 0))] += b3_xt[b] * top;
                    } else {
                        w = b3_wheel((int32_t)(B3_K0 + k) + B3_BAR[b] + (!b && k < 12u ? 12 : 0));   /* (the 16'
                                              * folded back on the bottom octave) */
                        const float v = lv[b] * b3_tap[b][k]
                                        * (b == b3_mj_cb[m][k] ? 1.0f - b3.mq[MJ_CONTACTS] * b3_mj_cd[m][k] : 1.0f);
                        t[w] += v;
                        if (w + 12u < B3_NW)  /* (its 2nd on the octave's wheel: a B-3's octaves exact, 2:1) */
                            t[w + 12u] += v * b3_hm[0][w];
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
    if (b3.mq[MJ_SAG] > 0.0f && load > 1.0f) {   /* (the tired supply: the level sagging under a big chord) */
        const float sg = 1.0f / (1.0f + 0.04f * b3.mq[MJ_SAG] * (load - 1.0f));
        for (w = 0; w < B3_NT; w++)
            tg[w] *= sg, td[w] *= sg, tp[w] *= sg;
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
    b3.mo_ph += 0.23f * (float)B3_CH / B3_RATE;   /* (the tired motor: slower, lazier, wandering) */
    b3.mo_ph -= b3.mo_ph >= 1.0f ? 1.0f : 0.0f;
    for (b = 0; b < 2u; b++) {               /* the rotors: chorale or tremolo, the brake; each its own run */
        const float mt = b3.mq[MJ_MOTOR],
                    to = b3.brake ? 0.0f : (b3.fast ? b3.tre[b] : b3.cho[b])
                                           * (1.0f - mt * (0.08f - 0.03f * b3_tri(b3.mo_ph + 0.37f * (float)b))),
                    tau = (to > b3.hz[b] ? b3.up[b] : b3.down[b]) * (1.0f + 1.2f * mt);
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
B3_IN float b3_flush(float x) { return x > -1e-20f && x < 1e-20f ? 0.0f : x; }   /* (no denormals: fm_flush's,
                                              * inline in the RAM code) */
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
        const int rich = b3.g[w] + b3.d[w] + b3.dg[w] * B3_CH + b3.dd[w] * B3_CH > B3_RICH;   /* (a faint wheel, the
                                              * crosstalk's: its harmonics past hearing, not made) */
        const float kc = b3.kc[w], h3 = rich ? b3_hm[1][w] : 0.0f;   /* (its own 2nd and 3rd: 2a^2 - 1,
                                              * 3a - 4a^3 of its sine, exactly 2 and 3 times it, as a wheel's tooth
                                              * gives them: the tempered drawbars beat against them, the shimmer) */
        float a = b3_tab(b3_wave[1], ph), b = b3_tab(b3_wave[1], ph + b3.inc[w]), nx;
        float g = b3.g[w], dg = b3.dg[w], d = b3.d[w], dd = b3.dd[w], pg = b3.pg[w], dpg = b3.dpg[w];
        const int dry = d != 0.0f || dd != 0.0f;
        if (pg == 0.0f && dpg == 0.0f && !dry && !rich)
            for (i = 0; i < B3_CH; i++, g += dg) {
                bus[i] += g * a;
                nx = kc * b - a, a = b, b = nx;
            }
        else if (pg == 0.0f && dpg == 0.0f && !dry)
            for (i = 0; i < B3_CH; i++, g += dg) {
                const float a2 = a * a;
                bus[i] += g * (a + h3 * a * (3.0f - 4.0f * a2));
                nx = kc * b - a, a = b, b = nx;
            }
        else if (pg == 0.0f && dpg == 0.0f && g == 0.0f && dg == 0.0f && !rich)
            for (i = 0; i < B3_CH; i++, d += dd) {
                dbus[i] += d * a;
                nx = kc * b - a, a = b, b = nx;
            }
        else if (pg == 0.0f && dpg == 0.0f && g == 0.0f && dg == 0.0f)
            for (i = 0; i < B3_CH; i++, d += dd) {
                const float a2 = a * a;
                dbus[i] += d * (a + h3 * a * (3.0f - 4.0f * a2));
                nx = kc * b - a, a = b, b = nx;
            }
        else
            for (i = 0; i < B3_CH; i++, g += dg, d += dd, pg += dpg) {
                const float a2 = a * a, v = a + h3 * a * (3.0f - 4.0f * a2);
                bus[i] += g * v;
                dbus[i] += d * v;
                pbus[i] += pg * a;
                nx = kc * b - a, a = b, b = nx;
            }
        b3.g[w] = g, b3.d[w] = d, b3.pg[w] = pg;
    }
    b3.t += B3_CH;
    PROF2_ADD(0, pt);
    const float hwind = b3.n_rotor * B3_WIND_HORN * b3_spin(b3.hz[0] * (1.0f / 6.8f));
    if (hwind > 1e-6f)                       /* (the horn's wind heard: the chain kept up, no notes or not) */
        b3.quiet = 0;
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
                           AT[2][5] = {{-B3_MIC, B3_MIC, 0.5f, 0.2f, 0.8f}, {-B3_DMIC, B3_DMIC, 0.5f, 0.5f, 0.5f}},
                           /* (the microphones either side of the cabinet's front; the reflections: the drum's off its
                            * back, both hear it; the horn's off the back and the two side walls, each microphone its
                            * own paths, nearly as strong as the horn's own: their sum notches and swells two or three
                            * times a turn, a held note's shimmer) */
                           HRD[2][3] = {{137.0f, 211.0f, 0.0f}, {149.0f, 0.0f, 311.0f}},
                           HRG[3] = {B3_HORN_REF, B3_HORN_REF2, B3_HORN_REF2};
        const float hr = B3_PRE_HR + (1.0f - B3_PRE_HR) * b3.drive,   /* (the clip's headroom: DRIVE 0 nearly clean) */
                    g = (1.0f + 6.0f * b3.drive) * hr, bias = 0.28f * b3.drive + 0.12f * b3.mq[MJ_WARM],
                    tb = b3_clip(bias),
                    og = 1.0f / ((0.5f + 0.5f * g / hr) * hr);
        float c[2][5], dc[2][5];
        uint32_t r, j;
        for (r = 0; r < 2u; r++) {           /* each rotor's angle at each microphone and its reflection's, at the
                                              * step's ends */
            float a1 = b3.ang[r] + b3.hz[r] * B3_CH / B3_RATE;
            if (a1 >= 1.0f)
                a1 -= 1.0f;
            for (j = 0; j < (r ? 3u : 5u); j++) {
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
            if (b3.th_in != 0.0f || b3.th1 != 0.0f || b3.mc_e > 1e-6f) {   /* the thud's resonator (75 Hz, 40 ms) and
                                              * the clunk's burst (a band pass at 400 Hz), past the swell */
                const float th = 1.99875f * b3.th1 - 0.99887f * b3.th2 + b3.th_in;
                b3.th_in = 0.0f, b3.th2 = b3.th1, b3.th1 = b3_flush(th);
                b3.nrng = b3.nrng * 1664525u + 1013904223u;
                {
                    const float n = (float)(int32_t)b3.nrng * (1.0f / 2147483648.0f) * b3.mc_e,
                                mc = 1.96853f * b3.mc1 - 0.97192f * b3.mc2 + n;
                    b3.mc2 = b3.mc1, b3.mc1 = b3_flush(mc), b3.mc_e *= 0.9977f;
                    x += 0.05f * th + 0.1f * mc;
                }
                b3.quiet = 0;
            }
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
            if (hwind > 0.0f) {              /* the rotors' wind on the horn's path: noise in its middle band, so it
                                              * moves as the horn's own sound does */
                float n;
                b3.nrng = b3.nrng * 1664525u + 1013904223u;
                n = (float)(int32_t)b3.nrng * (1.0f / 2147483648.0f);
                b3.hw0 += (n - b3.hw0) * 0.15f;           /* (under 1.1 kHz, two poles) */
                b3.hw1 += (b3.hw0 - b3.hw1) * 0.15f;
                b3.hw2 += (b3.hw1 - b3.hw2) * 0.03f;      /* (over 200 Hz: the rest) */
                hi += hwind * (b3.hw1 - b3.hw2);
            }
            b3_les[0][b3.lw & (B3_LES_N - 1u)] = hi;
            if (b3.lw & 1u) {                /* the drum at half the rate (all it carries under 800 Hz, the low pass's
                                              * before it): a pair's mean in, its line in half samples; out, each
                                              * microphone halfway between its last two (a sample later) */
                const float *l = b3_les[1];
                const float x = b3_bq((lo + b3.dlo) * 0.5f, b3.eq[2], b3_eq_b[2], b3_eq_a[2]) * b3_eq_tg;
                float rf;
                b3_les[1][b3.dw & (B3_LES_N - 1u)] = x;
                rf = 0.4f * b3_line(l, b3.dw, 0.5f * (REF[1] + SWING[1] * (1.0f - c[1][2])));
                for (m = 0; m < 2u; m++) {
                    const float q = 0.5f + 0.5f * c[1][m],
                                v = b3_line(l, b3.dw, 0.5f * (2.0f + SWING[1] * (1.0f - c[1][m])))
                                    * (B3_DRUM_AM + (1.0f - B3_DRUM_AM) * q * q) + rf;
                    b3.dmid[m] = 0.5f * (b3.dout[m] + v);
                    b3.dout[m] = v;
                    out[m] += b3.dmid[m];
                }
                b3.dw++;
            } else {
                b3.dlo = lo;
                out[0] += b3.dout[0], out[1] += b3.dout[1];
            }
            c[1][0] += dc[1][0], c[1][1] += dc[1][1], c[1][2] += dc[1][2];
            for (r = 0; r < 1u; r++) {       /* the horn's microphones: its path's delay, its level by its facing */
                const float *l = b3_les[r];
                float wg[3];                 /* (the horn's walls' beamed gains, the sample's) */
                for (m = 0; m < 3u && !r; m++) {
                    const float qk = 0.5f + 0.5f * c[0][2u + m];
                    wg[m] = HRG[m] * (B3_HORN_AM + (1.0f - B3_HORN_AM) * qk * qk);
                }
                for (m = 0; m < 2u; m++) {
                    const float cf = c[r][m];
                    const float q = 0.5f + 0.5f * cf, dir = r ? B3_DRUM_AM + (1.0f - B3_DRUM_AM) * q * q
                                                              : B3_HORN_AM + (1.0f - B3_HORN_AM) * q * q;
                    float v = b3_line(l, b3.lw, 2.0f + (!r && m ? B3_HMIC_D : 0.0f) + SWING[r] * (1.0f - cf)) * dir;
                    if (!r) {                /* (the horn: its walls' paths, each microphone its own; brighter
                                              * facing the microphone) */
                        uint32_t k;
                        for (k = 0; k < 3u; k++)     /* (each wall's path loud as the horn faces that wall; a
                                              * microphone hears the back and its nearer side) */
                            if (HRD[m][k] > 0.0f)
                                v += wg[k] * b3_line(l, b3.lw, HRD[m][k] + B3_HORN_RSW * (1.0f - c[0][2u + k]));
                        b3.hlp[m] += (v - b3.hlp[m]) * (0.18f + 0.77f * q * q);
                        v = b3.hlp[m];
                    }
                    out[m] += v;
                }
                c[r][0] += dc[r][0], c[r][1] += dc[r][1], c[r][2] += dc[r][2];
                if (!r)
                    c[0][3] += dc[0][3], c[0][4] += dc[0][4];
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
/* the floor's hum and hiss, into the block's middle (after the chain: heard asleep too, as the organ's on) */
B3_IN void b3_noises(float *mid, float *side, uint32_t n)
{
    const float hum = (b3.n_floor + 1.5f * b3.mq[MJ_SAG]) * B3_HUM, hiss = b3.n_floor * B3_HISS,
                wa = b3.n_rotor * B3_WIND_FREE * b3_spin((b3.hz[0] + b3.hz[1]) * (1.0f / 13.4f));
    uint32_t i, m;
    if (hum <= 0.0f && wa <= 1e-7f)
        return;
    for (i = 0; i < n; i++) {
        if (wa > 1e-7f) {                    /* (the rotors' wind, each microphone its own: a rumble whose tone
                                              * wanders at random about three times a second (a brook's burble), its
                                              * level gusting slowly and swelling a little as the horn's two mouths
                                              * pass, twice a turn) */
            float w[2], h2;
            b3.wph += 2.0f * b3.hz[0] * (1.0f / B3_RATE);   /* (its own phase, the horn's two mouths', a sample
                                              * at a time: the block's angle would step) */
            b3.wph -= b3.wph >= 1.0f ? 1.0f : 0.0f;
            h2 = b3.wph;
            b3.nrng = b3.nrng * 1664525u + 1013904223u;
            b3.gust += ((float)(int32_t)b3.nrng * (1.0f / 2147483648.0f) - b3.gust) * 0.0003f;
            b3.gust2 += (b3.gust - b3.gust2) * 0.0003f;
            for (m = 0; m < 2u; m++) {
                float k;
                b3.nrng = b3.nrng * 1664525u + 1013904223u;
                b3.bub[m] += ((float)(int32_t)b3.nrng * (1.0f / 2147483648.0f) - b3.bub[m]) * 0.0005f;
                b3.bub2[m] += (b3.bub[m] - b3.bub2[m]) * 0.0005f;
                k = 0.035f * (1.0f + B3_BROOK * b3.bub2[m]);
                k = k < 0.008f ? 0.008f : k > 0.06f ? 0.06f : k;
                b3.nrng = b3.nrng * 1664525u + 1013904223u;
                b3.wf[m] += ((float)(int32_t)b3.nrng * (1.0f / 2147483648.0f) - b3.wf[m]) * k;
                b3.wf2[m] += (b3.wf[m] - b3.wf2[m]) * k;
                b3.wf3[m] += (b3.wf2[m] - b3.wf3[m]) * k;   /* (four poles: its rumble falls steeply past 250 Hz,
                                              * as the reference's) */
                b3.wf4[m] += (b3.wf3[m] - b3.wf4[m]) * k;
                b3.wfa[m] += (b3.wf[m] - b3.wfa[m]) * 0.12f;   /* (the air's own fall past 900 Hz) */
                {                            /* (the whirr: the horn's two mouths a turn, its 3rd to 9th, each
                                              * microphone a quarter turn on) */
                    float ph = h2 + (m ? 0.25f : 0.0f), c, cp, cn, wr = 0.0f;
                    uint32_t hh;
                    ph -= ph >= 1.0f ? 1.0f : 0.0f;
                    c = b3_cos(ph), cp = 1.0f, cn = c;
                    for (hh = 2u; hh <= 9u; hh++) {
                        const float nx = 2.0f * c * cn - cp;
                        cp = cn, cn = nx;
                        if (hh >= 3u)
                            wr += cn;
                    }
                    b3.whr[m] = wr;
                }
                w[m] = wa * B3_WHIRR * b3.whr[m]
                       + wa * (1.0f + 10.0f * b3.gust2)
                         * (1.0f + B3_MOUTHS * b3_cos(h2 + (m ? 0.25f : 0.0f) - (m && h2 >= 0.75f ? 1.0f : 0.0f)))
                       * (b3.wf4[m] + B3_WIND_AIR * b3.wfa[m]);   /* (its rumble, and a little air over it) */
            }
            mid[i] += 0.5f * (w[0] + w[1]);
            side[i] += 0.5f * b3.width * (w[0] - w[1]);
        }
        if (hum > 0.0f) {                    /* (60 Hz and its 2nd to 4th, from its cosine) */
            const float c = b3_cos(b3.hum_ph), c2 = 2.0f * c * c - 1.0f;
            b3.hum_ph += 60.0f / B3_RATE;
            b3.hum_ph -= b3.hum_ph >= 1.0f ? 1.0f : 0.0f;
            b3.nrng = b3.nrng * 1664525u + 1013904223u;
            b3.hs1 += ((float)(int32_t)b3.nrng * (1.0f / 2147483648.0f) - b3.hs1) * 0.12f;
            mid[i] += hum * (c + 0.18f * c2 + 0.15f * (4.0f * c * c - 3.0f) * c + 0.1f * (2.0f * c2 * c2 - 1.0f))
                      + hiss * b3.hs1;
        }
    }
}

B3_HOT uint32_t b3_job(float *mid, float *side, uint32_t n)
{
    float ol[B3_CH], orr[B3_CH], pk = 0.0f;
    uint32_t i, j;
    for (i = 0; i < n; i++)
        mid[i] = side[i] = 0.0f;
    b3_pk_out = 0.0f;
    if (!b3.awake) {
        b3_noises(mid, side, n);
        return 0;
    }
    if (b3.quiet > B3_LES_N + B3_SCAN_N + 4096u && !b3.dirty && !b3.nact) {   /* (silent: the rotors turn on alone) */
        for (i = 0; i + B3_CH <= n; i += B3_CH)
            b3_chunk(ol, orr);
        b3_noises(mid, side, n);
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
    b3_noises(mid, side, n);
    b3_pk_out = pk;
    return 1;
}
#endif
