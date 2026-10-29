/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 John Brown */
/* Bass Something 2: started from fm1-base's template; its voice now a saw or square oscillator through an ADSR. */
/* The template app: every control on the screen as it arrives, by the name the app knows it by (plat.h B_*, EN_*,
 * the keys), and a few of them doing something: the keys play a voice and go out over USB-MIDI (MIDI in plays it
 * too), OCT- / OCT+ set the octave, PRESET the wave, MASTER the volume, SAVE stores the settings; the keys are
 * played with velocity and pressure (lib/touch.h), shown on them. One of each thing the platform offers, kept short. Part of the unity build (device/unity.c, host/plat_host.c), after the platform:
 * gfx.c and announce.c are in scope. */
#include <stddef.h>
#include "fastmath.h"

#define SET_MAGIC 0x54504C31u            /* "TPL1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t slot;                        /* the patch played (bank_t), 0..NSLOTS-1 (was the wave: 0 or 1) */
    uint8_t ble_off;                     /* Bluetooth switched off (GLO SYS); 0, the default: on */
    /* the GLO pages' (a record of the first eight bytes, before them: these from the defaults) */
    uint8_t chan;                        /* MIDI channel in and out: CH_OFF, 1..16, CH_ALL (in any; out on 1) */
    uint8_t local;                       /* the keys play the voice (0: their MIDI out alone) */
    uint8_t at_out;                      /* the keys' pressure out as poly aftertouch */
    uint8_t clk_out;                     /* MIDI clock out at the tempo, and start / stop at PLAY (not while following) */
    int8_t tune;                         /* the master tune, cents: -50 .. +50 */
    uint8_t clock;                       /* CLK_*: what the arp and the sequencer follow */
    uint8_t vcurve;                      /* VC_*: the keys' velocity, softer or harder */
    uint8_t cc_chan;                     /* the sound's knobs from MIDI CC (CC_MAP): CH_OFF, 1..16, CH_ALL; apart from
                                          * chan (the notes) */
    seqmem_t mem[NSEQ];                  /* the Bass Station II's four sequences: kept with the settings (glo_keep),
                                          * apart from the patches; the arp's OCTAVES in REC and PLAY picks one into
                                          * the patch, recording writes both */
} settings_t;
#define SET_V1_SIZE 8u                   /* the record before the GLO pages: magic to ble_off */
#define SET_V2_SIZE offsetof(settings_t, mem)   /* the record before the sequences (16 bytes stored: a byte of padding) */
enum { CH_OFF = 0, CH_ALL = 17 };
enum { CLK_AUTO, CLK_INT, CLK_EXT, NCLK };   /* MIDI clock followed when it comes; never; always (none: stopped) */
enum { VC_SOFT, VC_NORMAL, VC_HARD, NVC };
static settings_t set;
static settings_t set_kept;              /* the record as it is in the store (glo_keep) */
#define GLO_FROM offsetof(settings_t, ble_off)   /* the GLO pages' settings: ble_off on; kept on their own */
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */

static const char *const WAVE_NAME[NWAVES] = {"SAW", "SQUARE", "SINE", "TRI", "PULSE"};
static const char *const DEST_NAME[NDEST] = {"CUTOFF", "PITCH", "OSC2"};
static const char *const LFO_NAME[NLFOW] = {"TRI", "SINE", "SAW", "SQUARE", "S&H"};
/* the controls by their names in plat.h, in its order */
static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+"};

/* ------------------------------------------------------------------ voice --- */
/* one voice, the last note wins; the main loop and MIDI in set the gate, the audio interrupt plays it */
static volatile int32_t voice_note = -1;     /* MIDI note held, -1 = released (the gate) */
static volatile float voice_vel = 1.0f;      /* the note's velocity, 0..1 */
static volatile uint32_t voice_trig;         /* note-ons: each one (re)starts the envelope */
static voice_t voice = {.note = -1, .sag_g = 1.0f, .vm = 1.0f};

/* AFX (REC + OCT+): each key its own variation of the sound, the keyboard a kit. By the key (F3 the first, whatever
 * the octave), MIDI in's notes by the key they would be: osc 1's wave (-1 the patch's), then offsets to the cutoff,
 * both envelopes' decay and the noise; not kept (off at power-on) */
static volatile uint8_t afx_on;
typedef struct {
    int8_t wave, cut, dec, noise;
} afx_t;
static const afx_t AFX[NKEYS] = {
    {WAVE_SINE, -40, -30, 0},   {WAVE_SQUARE, 20, -50, 0},  {WAVE_TRI, 10, -35, 90},    {-1, 30, -45, 70},
    {WAVE_SINE, -20, -20, 0},   {-1, 60, -60, 127},         {WAVE_TRI, -10, -25, 0},    {-1, 60, -20, 110},
    {WAVE_SAW, -30, -40, 0},    {WAVE_SQUARE, 25, -40, 0},  {WAVE_PULSE, 40, -55, 0},   {-1, 0, 0, 0},
    {WAVE_SINE, -30, -25, 0},   {WAVE_SQUARE, 35, -50, 0},  {WAVE_TRI, 20, -30, 100},   {-1, 40, -40, 80},
    {WAVE_SINE, -10, -15, 0},   {-1, 63, -63, 127},         {WAVE_TRI, 0, -20, 0},      {-1, 63, -10, 120},
    {WAVE_SAW, -15, -30, 0},    {WAVE_SQUARE, 40, -35, 0},  {WAVE_PULSE, 55, -50, 0},   {-1, 10, 10, 0},
    {WAVE_SINE, -20, 0, 0},     {WAVE_SAW, 20, -45, 30},    {WAVE_PULSE, 30, 20, 0},
};
static lfo_t lfo[NLFO];
static float gain_now;                       /* glides to MASTER: no step at a block's edge */
static float vel_now;                        /* glides to the note's velocity: a legato note's is no step (a click) */
#define CTRL_N 32u                           /* the modulation's step: every 32 samples (0.7 ms) */
#define CTRL_RATE (BS2_RATE / (float)CTRL_N)
#ifdef PLAT_HOST
static volatile int32_t cut_now, cents_now, pitch_now;   /* what the modulation made of the last block (app_expect) */
static volatile int32_t dc_now;          /* the output's mean over its mean size, the last 0.2 s or so, 1/1000 */
static volatile int32_t sag_now = 1000;  /* the supply's sag's gain, the last step, 1/1000 (app_expect) */
static volatile int32_t fc_now;
static volatile int32_t trim_now, heat_now;
static volatile int32_t pw1_now;         /* oscillator 1's pulse width, the last step, 1/1000 (app_expect) */   /* the cutoff's trim (1/10000) and the filter's warming (1/1000) */          /* the cutoff's last step, Hz (app_expect) */
#endif

static float env_coef(float sec, float rate) /* one pole: about 99 % of the way in sec */
{
    return 1.0f - fm_expf(-4.6f / (sec * rate));
}
/* the attack charges like a capacitor toward ATT_AIM, over the top, and stops at 1 (as an analog envelope's comparator
 * does): fastest at its start, so a short one snaps; it takes as long as a linear one did (ln(AIM / (AIM - 1))) */
#define ATT_AIM 1.3f
#define ATT_K 1.4663f
static void adsr_set(adsr_t *e, float a, float d, float s, float r, float rate)   /* times in seconds, steps a s */
{
    e->att = 1.0f - fm_expf(-ATT_K / (fm_maxf(a, 0.001f) * rate));
    e->dec = env_coef(fm_maxf(d, 0.001f), rate);
    e->sus = fm_clampf(s, 0.0f, 1.0f);
    e->rel = env_coef(fm_maxf(r, 0.001f), rate);
}
static float env_time(uint32_t v)            /* a knob's 0..127: 2 ms .. 10 s */
{
    return 0.002f * fm_exp2f((float)v * (12.3f / 127.0f));
}

/* a step of an envelope: each stage moves on from the level it is at, so a retrigger or an early release
 * does not jump (no click) */
static inline float adsr_step(env_t *v, const adsr_t *e)
{
    switch (v->stage) {
    case ENV_ATTACK:
        if ((v->lvl += (ATT_AIM - v->lvl) * e->att) >= 1.0f) {
            v->lvl = 1.0f;
            v->stage = ENV_DECAY;
        }
        break;
    case ENV_DECAY:
        v->lvl += (e->sus - v->lvl) * e->dec;
        if (v->lvl - e->sus < 1e-4f)
            v->stage = ENV_SUSTAIN;
        break;
    case ENV_SUSTAIN:
        v->lvl = e->sus;
        break;
    case ENV_RELEASE:
        v->lvl -= v->lvl * e->rel;
        if (v->lvl < 1e-5f) {
            v->lvl = 0.0f;
            v->stage = ENV_IDLE;
        }
        break;
    default:
        v->lvl = 0.0f;
    }
    return v->lvl;
}
static void env_release(env_t *v)
{
    if (v->stage != ENV_IDLE && v->stage != ENV_RELEASE)
        v->stage = ENV_RELEASE;
}

/* an LFO's step of n samples: -1..1. Free-running (a note does not restart it); S&H a new value at each wrap */
static float lfo_step(lfo_t *l, const lfo_patch_t *p, uint32_t n)
{
    static uint32_t rng = 1u;
    float ph;
    l->ph += 0.05f * fm_exp2f((float)p->rate * (9.2f / 127.0f)) * (float)n / BS2_RATE;
    if (l->ph >= 1.0f) {
        l->ph -= fm_floorf(l->ph);
        rng = rng * 1664525u + 1013904223u;
        l->hold = (float)(int32_t)rng * (1.0f / 2147483648.0f);
    }
    ph = l->ph;
    switch (p->wave) {
    case LFO_SINE:
        return fm_sin_turns(ph);
    case LFO_SAW:
        return 2.0f * ph - 1.0f;
    case LFO_SQUARE:
        return ph < 0.5f ? 1.0f : -1.0f;
    case LFO_SH:
        return l->hold;
    default:                                 /* TRI */
        return 1.0f - 4.0f * fm_fabsf(ph - 0.5f);
    }
}

/* the step's residual at a wrap (PolyBLEP): t the phase since it, dt the step, both in turns */
static inline float blep(float t, float dt, float inv_dt)
{
    if (t < dt) {
        t *= inv_dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * inv_dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
/* the corner's residual (PolyBLAMP, the step's integrated): a slope's change, rounded over a sample either side */
static inline float blamp(float t, float dt, float inv_dt)
{
    if (t < dt) {
        t = 1.0f - t * inv_dt;
        return t * t * t * (1.0f / 3.0f);
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * inv_dt + 1.0f;
        return t * t * t * (1.0f / 3.0f);
    }
    return 0.0f;
}
static inline float wrap1(float h) { return h >= 1.0f ? h - 1.0f : h; }
/* -1..1, band-limited (the sine has nothing to band-limit); pw the pulse's width 0..1 (the square's 0.5), its DC
 * taken off */
static inline float osc(float ph, float dt, float inv_dt, uint32_t wave, float pw)
{
    switch (wave) {
    case WAVE_SAW:
        return 2.0f * ph - 1.0f - blep(ph, dt, inv_dt);
    case WAVE_SINE:
        return fm_sin_turns(ph);
    case WAVE_TRI:                           /* its corners at 0 (the slope -4 to +4 a turn) and a half (back) */
        return 1.0f - 4.0f * fm_fabsf(ph - 0.5f) + 4.0f * dt * (blamp(ph, dt, inv_dt) - blamp(wrap1(ph + 0.5f), dt, inv_dt));
    default:                                 /* SQUARE, PULSE: up at 0, down at pw */
        return (ph < pw ? 1.0f : -1.0f) + 1.0f - 2.0f * pw + blep(ph, dt, inv_dt)
               - blep(wrap1(ph + 1.0f - pw), dt, inv_dt);
    }
}
static inline float osc_naive(float ph, uint32_t wave, float pw)   /* the same, not band-limited (sync's steps) */
{
    switch (wave) {
    case WAVE_SAW:
        return 2.0f * ph - 1.0f;
    case WAVE_SINE:
        return fm_sin_turns(ph);
    case WAVE_TRI:
        return 1.0f - 4.0f * fm_fabsf(ph - 0.5f);
    default:
        return ph < pw ? 1.0f : -1.0f;
    }
}

/* oscillator 2 restarted where oscillator 1 wrapped, t samples ago (0..1): its sample, with the restart's step
 * band-limited on both sides (PolyBLEP: the side before it on *prev, the sample held back for that). osc() took
 * the restart for a wrap of its own (a full step: saw -2, the pulses +2, none for the others); the step is from
 * where it was instead */
static inline float osc_sync(float *ph, float *prev, float t, float dt, float inv_dt, uint32_t wave, float pw)
{
    float pr = *ph - t * dt, d, o, full; /* where it was at the restart */
    if (pr < 0.0f)
        pr += 1.0f;
    *ph = t * dt;
    o = osc(*ph, dt, inv_dt, wave, pw);
    d = osc_naive(0.0f, wave, pw) - osc_naive(pr, wave, pw);
    full = wave == WAVE_SAW ? -2.0f : wave == WAVE_SQUARE || wave == WAVE_PULSE ? 2.0f : 0.0f;
    o -= 0.5f * (d - full) * (1.0f - t) * (1.0f - t);
    *prev += 0.5f * d * t * t;
    return o;
}

/* ----------------------------------------------------------------- filter --- */
/* four one-pole stages (trapezoidal, zero-delay feedback) with the fourth fed back through a tanh at the input,
 * the drive before it. Classic: equal stages, its modes mixed from the stages' outputs. Acid, a diode ladder
 * approximated: the stages spread (a diode ladder's poles do not coincide), a high pass in the feedback (the bass
 * stays under the resonance, as in a 303), the tanh a little asymmetric, and no make-up gain (it thins).
 * The resonance is a share of the feedback at which the loop starts to sing (the gain at its -180 degrees): Classic's
 * 4 at every cutoff, Acid's from ACID_K; at the top a little past it, so both self-oscillate everywhere, settling
 * where the tanh gives up that little (a clean sine). The drive is its own tanh before the loop, its
 * antiderivative's difference (ADAA): what it adds above Nyquist folds back 10 dB or more less */
typedef struct {
    float G[4], om[4];                       /* each stage's g / (1 + g), and 1 - that */
    float P, c1, c2;                         /* G4 G3 G2 G1, G4 G3 G2, G4 G3 */
    float Gh, a;                             /* the feedback high pass: its G, and 1 - G (Classic: 0 and 1) */
    float k, pre, gin, post, bias, bias0;    /* resonance, drive, make-up in and out, tanh(bias) */
    uint32_t mode;                           /* FLT_* */
} fcoef_t;

static const char *const FLT_NAME[NFLT] = {"LP24", "LP12", "BP24", "BP12", "HP24", "HP12", "ACID"};
static patch_t patch;

/* the filter's parts: each FM-1's cutoff a little off the dial's (up to 1 %, from its flash's ID: its own, every start,
 * like one analog unit's against another), and warming as it works: a note held loud, harder with the resonance up,
 * heats it over half a minute or so (a minute to cool), its cutoff then down up to 2 % (about 35 cents) */
#define FLT_TOL 0.01f
#define FLT_HEAT 0.02f
static float flt_tol = 1.0f;                 /* app_init: this FM-1's */
static void tol_init(void)
{
    const uint8_t *u = plat_chip_uid();
    uint32_t i, x = 2166136261u;             /* (FNV-1a over the ID: 0..255 to -1..1) */
    if (!u)
        return;
    for (i = 0; i < 16u; i++)
        x = (x ^ u[i]) * 16777619u;
    flt_tol = 1.0f + FLT_TOL * ((float)(x & 255u) / 127.5f - 1.0f);
}
static void heat_step(float r, float up, float down)   /* a modulation step: r the resonance, 0..1 */
{
    float load = voice.amp.lvl * vel_now * (0.5f + 0.5f * r);
    voice.heat += (load - voice.heat) * (load > voice.heat ? up : down);
}

static void filter_coefs(fcoef_t *c, const patch_t *p, float cutoff)   /* each modulation step; cutoff 0..127 */
{
    static const float ACID_SPREAD[4] = {1.6f, 1.0f, 1.0f, 0.7f};
    static const float ACID_K[17] = {5.72f, 5.63f, 5.63f, 5.63f, 5.62f, 5.62f, 5.62f, 5.62f, 5.19f,   /* the loop's */
                                     4.91f, 4.72f, 4.61f, 4.56f, 4.57f, 4.76f, 6.07f, 5.57f};    /* every 8 */
    /* 40 Hz .. 17.8 kHz: the bottom of the dial still under a bass note's fundamentals, not shut (from 20 Hz a
     * patch made low on a Bass Station II, 47 Sub Bass at 11, was 25 dB under its others) */
    float fc = 40.0f * fm_exp2f(cutoff * (8.8f / 127.0f));
#ifdef PLAT_HOST
    fc_now = (int32_t)(fc + 0.5f);
    trim_now = (int32_t)(10000.0f * flt_tol * (1.0f - FLT_HEAT * voice.heat) + 0.5f);
    heat_now = (int32_t)(1000.0f * voice.heat);
#endif
    fc *= flt_tol * (1.0f - FLT_HEAT * voice.heat);
    float r = (float)p->reso / 127.0f, kc = 4.0f;
    uint32_t i, acid = p->filter == FLT_ACID;
    for (i = 0; i < 4u; i++) {
        float g = fm_tanf(FM_PI * fm_minf(fc * (acid ? ACID_SPREAD[i] : 1.0f), 0.45f * BS2_RATE) / BS2_RATE);
        c->G[i] = g / (1.0f + g);
        c->om[i] = 1.0f - c->G[i];
    }
    c->c2 = c->G[3] * c->G[2];
    c->c1 = c->c2 * c->G[1];
    c->P = c->c1 * c->G[0];
    c->Gh = 0.0f;
    if (acid) {                              /* (under a low cutoff it follows it down: the loop still sings there) */
        float g = fm_tanf(FM_PI * fm_minf(150.0f, 0.25f * fc) / BS2_RATE), x = cutoff * 0.125f;
        c->Gh = g / (1.0f + g);
        i = x < 15.0f ? (uint32_t)x : 15u;
        kc = ACID_K[i] + (ACID_K[i + 1u] - ACID_K[i]) * (x - (float)i);
    }
    c->a = 1.0f - c->Gh;
    c->k = r * kc * 1.12f;
    c->pre = 1.0f + (float)p->drive * (7.0f / 127.0f);
    /* the loop fed at 0.3 and made up after it: a note playing does not choke its tanh, so it still sings over one
     * (fed whole, the loop's gain under the note fell short of the little past singing) */
    c->post = fm_rsqrtf(1.0f + 0.4f * (c->pre - 1.0f)) * (1.0f / 0.3f);   /* full drive about 1 dB louder */
    c->gin = 0.3f * (acid ? 1.0f : 1.0f + 0.5f * c->k);   /* Classic: make-up for the loss in the pass band */
    c->bias = acid ? 0.1f : 0.0f;
    c->bias0 = fm_tanhf(c->bias);
    c->mode = p->filter;
}

static inline float lncosh(float x)          /* ln(cosh x): tanh's antiderivative, without overflow */
{
    float a = fm_fabsf(x);
    return a + fm_logf(1.0f + fm_expf(-2.0f * a)) - FM_LN2;
}
static inline float overdrive(adaa_t *f, float x)   /* tanh averaged over the sample (first-order ADAA) */
{
    float d = x - f->x1, F = lncosh(x);
    float y = fm_fabsf(d) > 0.01f ? (F - f->F1) / d : fm_tanhf(0.5f * (x + f->x1));   /* (close: the midpoint's) */
    f->x1 = x;
    f->F1 = F;
    return y;
}

/* the mixer's bus into the filter: the sum of the oscillators, the sub and the noise goes through the drive's tanh (at
 * DRIVE 0 too: its loud peaks round off, waves together intermodulate), a little off its centre as an analog summing
 * stage is, so it warms with even harmonics as well (its DC: the output's coupling's) */
#define MIX_BIAS 0.15f
#define MIX_BIAS0 0.148885f                  /* tanh(MIX_BIAS) */
static inline float ladder(ladder_t *f, const fcoef_t *c, float in)
{
    float u = (overdrive(&f->drv, in * c->pre + MIX_BIAS) - MIX_BIAS0) * c->gin, x, y1, y2, y3, y4, v;
    float S = c->c1 * f->s[0] * c->om[0] + c->c2 * f->s[1] * c->om[1] + c->G[3] * f->s[2] * c->om[2]
              + f->s[3] * c->om[3];
    float kPa = c->k * c->P * c->a;
    y4 = (c->P * u + kPa * f->hs + S) / (1.0f + kPa);   /* the loop solved linearly, then the tanh on it */
    x = fm_tanhf(u - c->k * c->a * (y4 - f->hs) + c->bias) - c->bias0;
    v = (x - f->s[0]) * c->G[0];
    y1 = v + f->s[0];
    f->s[0] = y1 + v;
    v = (y1 - f->s[1]) * c->G[1];
    y2 = v + f->s[1];
    f->s[1] = y2 + v;
    v = (y2 - f->s[2]) * c->G[2];
    y3 = v + f->s[2];
    f->s[2] = y3 + v;
    v = (y3 - f->s[3]) * c->G[3];
    y4 = v + f->s[3];
    f->s[3] = y4 + v;
    v = (y4 - f->hs) * c->Gh;
    f->hs += 2.0f * v;
    switch (c->mode) {
    case FLT_LP12:
        y4 = y2;
        break;
    case FLT_BP24:
        y4 = 4.0f * (y2 - 2.0f * y3 + y4);
        break;
    case FLT_BP12:
        y4 = 2.0f * (y1 - y2);
        break;
    case FLT_HP24:
        y4 = x - 4.0f * y1 + 6.0f * y2 - 4.0f * y3 + y4;
        break;
    case FLT_HP12:
        y4 = x - 2.0f * y1 + y2;
        break;
    default:                                 /* LP24, ACID */
        break;
    }
    return y4 * c->post;
}

/* ------------------------------------------------------ arp and sequencer --- */
/* the audio interrupt keeps the notes held and runs the clock, at each modulation step: started (the arp's first note
 * held; in PLAY the sequencer's, or the PLAY button) it plays the rhythm's first 16th at once, then one each 16th of
 * the clock; a note released at its gate (unless the rhythm holds it on, or a legato step). The keys' notes come over
 * from the main loop in a queue (one writer, one reader), so the notes held have one owner; MIDI in's land here
 * directly. With neither running, the last note wins as before (and LATCH holds it: the Bass Station II's key hold) */
typedef struct {
    volatile uint32_t w, r;
    volatile uint32_t q[32];
} fifo_t;
static int fifo_put(fifo_t *f, uint32_t v)
{
    if (f->w - f->r >= 32u)
        return 0;
    f->q[f->w % 32u] = v;
    f->w++;
    return 1;
}
static int fifo_get(fifo_t *f, uint32_t *v)
{
    if (f->r == f->w)
        return 0;
    *v = f->q[f->r % 32u];
    f->r++;
    return 1;
}
static fifo_t keyq;                          /* main loop -> audio: note | velocity << 8 (0: off); NOTE_ALL_OFF */
static fifo_t recq;                          /* audio -> main loop: MIDI in's notes while recording */
static fifo_t ccq;                           /* audio -> main loop: MIDI in's CCs on CC CHAN, number | value << 8 */
#define NOTE_ALL_OFF 0xFFu
static volatile uint8_t seq_auto;            /* the main loop's: the PLAY button's, the sequence played with no key held
                                              * (at its own key, until one comes) */
#define seq_rec (patch.arp_mode == ARP_REC)  /* the arp's REC: the keys play as they are, each a step recorded */
#define STEP_TICKS 6.0f                      /* a 16th in clock ticks (24 a quarter note) */
/* the arp's rhythms, the Bass Station II's: two bars of 16ths each, x a note, - it held on (a longer note, or a
 * tie), . a rest. 1..30 as transcribed in notation (BSIIRhythm.pdf), 31 and 32 from Tronstoner's chart (2013, CC-BY-SA),
 * which agrees on the rest but for two notes' lengths (4's crotchets quavers there, 20's 11th 16th a rest) */
static const char RHYTHM[NRHY][33] = {
    "x---x---x---x---x---x---x---x---", "x-..x-..x-..x-..x-..x-..x-..x-..", "x---x-..x---x-..x---x-..x---x-x-",
    "x-x---x---x---x---x---x---x---x-", "x-..x-..x-x-x-x---x-..x-x-..x-..", "x---x-x-x-x-..x---x-x-..x-..x-..",
    "x---x-x-..x-..x-x-x-..x-x-..x-x-", "x---x-x-x---x-x-x---x-x-x---x-x-", "x-x-x-..x-x-x-x-..x-..x-x-..x-x-",
    "x-x-..x-x-x-x-x-..x-x-x-x-..x-x-", "x---x---x-x-x-x-x-x-x-x-x-x-x-x-", "x---x-x-x-x-x-x-x-x-x-x-x-x-x-x-",
    "x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "x-.x-.x.x-.x-.x.x-.x-.x.x-.x-.x.",
    "x-.x-.x-x-.x-.x-x-.x-xx.x-.x-.xx", "x..x.x.x.x.x.x.x.x.x.x.x.x.x.x..", "x-.x-.x.x.x.x.x.x.x.x-.xx-.xx-.x",
    "xxx.x.x.x.x.x.xxx.x.x.x.x.x.x.x.", "xxxx..x..x-xx.x.x.xx..x..x-xx.xx", "x.x.xxx.x.x.x.x.xxx.x.x.x.x.x.xx",
    "x.x-.xxxx.x-.xxxx.x-.xxxx.x-.xxx", "xxxxx-x.x.x.x.xxx.x.x.x.x.x.x.xx", "x.x-.xxxxx.xx-.xx.x-.xxxxxxx-x.x",
    "x.xx.xxxx.xx.xxxx.xx.xxxx.xx.xxx", "xxx-.xx.xx.xxxxxxxx-.xx.xxxxxxxx", "xxxx.x.xxxxx.xxxxxxx.x.xxxxx.xxx",
    "xxxxxxxxxxx.x.x.xxxxxxxxxxx-x-xx", "x-.x.xxxxxxxxxxxx.xxxx.xxxxxxxxx", "x.xxxxxxx.xxxxxxx.xxxxxxx.xxxxx.",
    "x.xxxxxxxxxxx.xxx.xxxxxxxxxxx.xx", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
};
#define RHY_STEPS 32u
#define NHELD 16u
#define EXT_HOLD 22050u                      /* MIDI clock: followed until none comes for half a second (samples) */
enum { SRC_NONE, SRC_ARP, SRC_SEQ, SRC_RESTART };
static struct {
    uint8_t held[NHELD], nheld;              /* the notes the arp plays, in the order played */
    uint32_t down[4];                        /* the notes down now (128 bits), and how many */
    uint32_t ndown;
    uint32_t vel;                            /* the last note-on's velocity */
    uint32_t src;                            /* SRC_*: what plays the voice */
    float tick;                              /* clock ticks into the step */
    uint32_t ext_ticks, ext_age;             /* MIDI clocks not yet counted; samples since the last */
    float clk_out;                           /* clock ticks toward the next one out (GLO SYS's CLK OUT) */
    uint32_t apos, spos;                     /* the arp's notes played; the sequencer's next step */
    uint32_t scur;                           /* the sequencer's step sounding (NSTEPS: none yet) */
    uint32_t rpos;                           /* the 16ths since the start: the rhythm's place, and swing's pair */
    uint8_t sounding, tie;                   /* a note is on; the rhythm (or a legato step) holds it on */
    uint8_t leg;                             /* the sequencer's step sounding is legato: held into the next note */
    int32_t transpose;                       /* the sequencer: the last key down from its first note (0: as recorded) */
} eng = {.vel = 100, .ext_age = EXT_HOLD};

static int ext_clock(void)                   /* the arp and the sequencer follow MIDI clock (GLO MIDI's CLOCK) */
{
    return set.clock == CLK_EXT || (set.clock == CLK_AUTO && eng.ext_age < EXT_HOLD);
}

static int arp_takes(void)                  /* the arp (or its sequencer) plays the keys, not the keys the voice */
{
    return patch.arp_on && patch.arp_mode != ARP_REC;
}
static int32_t seq_first(void)               /* the sequence's first note: a key plays it as recorded (C4: none) */
{
    uint32_t i;
    for (i = 0; i < patch.sq.len; i++)
        if (patch.sq.step[i] != SEQ_REST)
            return patch.sq.step[i];
    return 60;
}
static int seq_leg(const seqmem_t *q, uint32_t i) { return q->leg[i >> 3] >> (i & 7u) & 1u; }

static int seq_playing(void) { return eng.src == SRC_SEQ || seq_auto; }

static void held_del(uint32_t note)
{
    uint32_t i, j = 0;
    for (i = 0; i < eng.nheld; i++)
        if (eng.held[i] != note)
            eng.held[j++] = eng.held[i];
    eng.nheld = (uint8_t)j;
}
static void note_in(uint32_t note, uint32_t vel)   /* a key or MIDI in: vel 0 a note-off */
{
    uint32_t bit = 1u << (note & 31u), *w = &eng.down[(note >> 5) & 3u];
    if (note == NOTE_ALL_OFF) {              /* (the octave changed: no key's note-off would find its note) */
        eng.nheld = 0;
        eng.down[0] = eng.down[1] = eng.down[2] = eng.down[3] = eng.ndown = 0;
        voice_note = -1;
        return;
    }
    if (vel) {
        if (patch.latch && !eng.ndown) {     /* latched: a new chord replaces the last, from its first step */
            eng.nheld = 0;
            eng.src = SRC_RESTART;
        }
        if (!(*w & bit))
            eng.ndown++;
        *w |= bit;
        held_del(note);
        if (eng.nheld == NHELD)              /* (full: the oldest goes) */
            held_del(eng.held[0]);
        eng.held[eng.nheld++] = (uint8_t)note;
        eng.vel = vel;
        if (arp_takes() && patch.arp_mode == ARP_PLAY)
            eng.transpose = (int32_t)note - seq_first();
        else if (!arp_takes()) {
            voice_vel = (float)vel / 127.0f;
            voice_note = (int32_t)note;
            voice_trig++;
        }
        return;
    }
    if (*w & bit)
        eng.ndown--;
    *w &= ~bit;
    if (!patch.latch)
        held_del(note);
    if (!arp_takes() && !patch.latch && voice_note == (int32_t)note)   /* (LATCH, the arp off: the key held) */
        voice_note = -1;
}

static void eng_play(int32_t note, uint32_t vel)
{
    voice_vel = (float)vel / 127.0f;
    voice_note = clamp(note, 0, 127);
    voice_trig++;
    eng.sounding = 1;
}
static void eng_glide(int32_t note)          /* a legato step: the note changes, its envelopes go on */
{
    voice_note = clamp(note, 0, 127);
    eng.sounding = 1;
}
static void eng_quiet(void)
{
    if (eng.sounding)
        voice_note = -1;
    eng.sounding = 0;
}
static void arp_step(void)
{
    static uint32_t rng = 1u;
    uint8_t s[NHELD];
    uint32_t n = eng.nheld, len = n * patch.arp_oct, i, j, per;
    if (!n) {
        eng_quiet();
        return;
    }
    for (i = 0; i < n; i++) {                /* the notes low to high (as played: as they are) */
        uint8_t v = eng.held[i];
        for (j = i; patch.arp_mode != ARP_PLAYED && j > 0u && s[j - 1u] > v; j--)
            s[j] = s[j - 1u];
        s[j] = v;
    }
    switch (patch.arp_mode) {
    case ARP_DOWN:
        i = len - 1u - eng.apos % len;
        break;
    case ARP_UPDOWN:                         /* (the ends once each) */
        per = len > 1u ? 2u * len - 2u : 1u;
        i = eng.apos % per;
        if (i >= len)
            i = per - i;
        break;
    case ARP_UPDOWN2:                        /* (the ends twice) */
        i = eng.apos % (2u * len);
        if (i >= len)
            i = 2u * len - 1u - i;
        break;
    case ARP_RANDOM:
        rng = rng * 1664525u + 1013904223u;
        i = (rng >> 16) % len;
        break;
    default:                                 /* UP, PLAYED */
        i = eng.apos % len;
    }
    eng.apos++;
    eng_play((int32_t)s[i % n] + 12 * (int32_t)(i / n), eng.vel);
}
static void arp_beat(void)                   /* a 16th of the arp's rhythm: a note, the one before held on, or a rest */
{
    const char *r = RHYTHM[patch.rhythm - 1u];
    uint32_t at = eng.rpos % RHY_STEPS;
    if (patch.arp_retrig && !at)             /* (retrig: the notes from their first at the rhythm's start) */
        eng.apos = 0;
    eng.tie = r[(at + 1u) % RHY_STEPS] == '-';
    if (r[at] == 'x')
        arp_step();
    else if (r[at] == '.')
        eng_quiet();
}
static void seq_note(void)                   /* a note of the rhythm: the sequence's next step */
{
    uint32_t len = patch.sq.len, v, leg = eng.leg;
    if (!len) {
        eng.leg = 0;
        eng_quiet();
        return;
    }
    if (eng.spos >= len)
        eng.spos = 0;
    eng.scur = eng.spos;
    v = patch.sq.step[eng.spos];
    eng.leg = v != SEQ_REST && seq_leg(&patch.sq, eng.spos);   /* (a rest is never tied) */
    eng.spos++;
    if (v == SEQ_REST)
        eng_quiet();
    else if (leg && eng.sounding)            /* (the step before legato: this one's note without an attack) */
        eng_glide((int32_t)v + eng.transpose);
    else
        eng_play((int32_t)v + eng.transpose, eng.vel);
}
static void seq_beat(void)                   /* a 16th of the arp's rhythm, the sequencer's: a step, held on, or a rest */
{
    const char *r = RHYTHM[patch.rhythm - 1u];
    uint32_t at = eng.rpos % RHY_STEPS;
    if (patch.arp_retrig && !at)             /* (SEQ RETRIG: the sequence from its first every two bars) */
        eng.spos = 0;
    if (r[at] == 'x')
        seq_note();
    else if (r[at] == '.' && !eng.leg)       /* (a legato note held through the rhythm's rests to the next) */
        eng_quiet();
    eng.tie = r[(at + 1u) % RHY_STEPS] == '-' || eng.leg;
}
static void eng_run(uint32_t m)              /* each modulation step, m samples */
{
    uint32_t i, src = !arp_takes() ? SRC_NONE
                    : patch.arp_mode == ARP_PLAY ? (eng.nheld || seq_auto ? SRC_SEQ : SRC_NONE)
                    : eng.nheld ? SRC_ARP : SRC_NONE;
    float len = STEP_TICKS * (float)(eng.rpos & 1u ? 100u - patch.swing : patch.swing) * 0.02f;   /* swing: a pair's
                                                                                                   * first longer */
    if (!patch.latch)                        /* (latch turned off: the notes no longer down go) */
        for (i = eng.nheld; i-- > 0u;)
            if (!(eng.down[eng.held[i] >> 5] >> (eng.held[i] & 31u) & 1u))
                held_del(eng.held[i]);
    eng.ext_age = eng.ext_age < EXT_HOLD ? eng.ext_age + m : EXT_HOLD;
    if (set.clk_out && !ext_clock())         /* the tempo out as MIDI clock, whether anything plays or not */
        for (eng.clk_out += (float)patch.tempo * (24.0f / 60.0f / BS2_RATE) * (float)m; eng.clk_out >= 1.0f;
             eng.clk_out -= 1.0f)
            plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB | TO_BLE);
    if (src != eng.src) {                    /* started, stopped, the other took over, or MIDI's start */
        eng_quiet();
        eng.src = src;
        eng.tick = 0.0f;
        eng.apos = eng.spos = eng.rpos = 0;
        eng.scur = NSTEPS;
        eng.tie = eng.leg = 0;
        eng.ext_ticks = 0;
        if (src == SRC_NONE)
            return;
        if (src == SRC_ARP)
            arp_beat();
        else {                               /* (the key held last, or none: as recorded) */
            eng.transpose = eng.nheld ? (int32_t)eng.held[eng.nheld - 1u] - seq_first() : 0;
            seq_beat();
        }
        return;
    }
    if (src == SRC_NONE)
        return;
    eng.tick += ext_clock() ? (float)eng.ext_ticks : (float)patch.tempo * (24.0f / 60.0f / BS2_RATE) * (float)m;
    eng.ext_ticks = 0;
    if (eng.tick >= len) {
        eng.tick = eng.tick >= 2.0f * len ? 0.0f : eng.tick - len;
        eng.tie = 0;
        eng.rpos++;
        if (src == SRC_ARP)
            arp_beat();
        else
            seq_beat();
    }
    if (eng.sounding && !eng.tie && eng.tick >= len * (float)patch.gate * 0.01f)
        eng_quiet();
}

static volatile uint32_t midi_last, midi_last_port, midi_count;   /* the last message in (shown), and how many */
static void midi_in(void)                    /* audio interrupt: the keys' notes, and the packets that came in */
{
    uint32_t pkt, e;
    int port;
    while (fifo_get(&keyq, &e))
        note_in(e & 0xFFu, e >> 8);
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        uint32_t ours = set.chan == CH_ALL || (set.chan != CH_OFF && (st & 0x0Fu) == set.chan - 1u);   /* (GLO MIDI) */
        uint32_t cc = set.cc_chan == CH_ALL || (set.cc_chan != CH_OFF && (st & 0x0Fu) == set.cc_chan - 1u);
        if (st != 0xF8u && st != 0xFEu) {    /* (clock and active sensing would flood the screen) */
            midi_last = pkt;
            midi_last_port = (uint32_t)port;
            midi_count++;
        }
        if (st == 0xF8u) {                   /* MIDI clock: the arp and the sequencer follow it (CLOCK not INT) */
            if (set.clock != CLK_INT) {
                eng.ext_ticks++;
                eng.ext_age = 0;
            }
        } else if (st == 0xFAu) {            /* start: from the first step */
            if (set.clock != CLK_INT)
                eng.src = SRC_RESTART;
        } else if ((st & 0xF0u) == 0xB0u) {  /* a CC: a knob of the sound's on CC CHAN alone (cc_apply) */
            if (cc)
                fifo_put(&ccq, d1 | d2 << 8);
        } else if (!ours)                    /* a note on another channel */
            ;
        else if ((st & 0xF0u) == 0x90u && d2) {
            note_in(d1, d2);
            if (seq_rec)
                fifo_put(&recq, d1);
        } else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            note_in(d1, 0);
    }
}

/* the modulation's step: the LFOs summed per destination (each -1..1 at full), each LFO's value in lv; the mod
 * envelope's level returned, a 63rd (its depths are its own: the caller's) */
static float mod_step(const adsr_t *menv, uint32_t n, float *mod, float *lv)
{
    uint32_t j;
    for (j = 0; j < NDEST; j++)
        mod[j] = 0.0f;
    for (j = 0; j < NLFO; j++) {
        lv[j] = lfo_step(&lfo[j], &patch.lfo[j], n);
        mod[patch.lfo[j].dest] += lv[j] * (float)patch.lfo[j].amt * (1.0f / 63.0f);
    }
    return adsr_step(&voice.menv, menv) * (1.0f / 63.0f);
}
/* a pulse's width: 0.05..0.95 (the square's 0.5); LFO 2 on it by pwm, the mod envelope (menv: its depth times its
 * level, -63..63) */
static float pulse_width(uint32_t wave, uint32_t pw, int32_t pwm, float lfo2, float menv)
{
    if (wave != WAVE_PULSE)
        return 0.5f;
    return fm_clampf(((float)pw + ((float)pwm * lfo2 + menv) * (45.0f / 63.0f)) * 0.01f, 0.05f, 0.95f);
}
/* velocity on an envelope (the Bass Station II's VELOCITY AMP ENV and MOD ENV): amt -63..63 of v's 0..1; at +63 the
 * level is the velocity, at 0 always full, at -63 a soft note full and a hard one none */
static float vel_gain(float v, int32_t amt)
{
    float k = (float)amt * (1.0f / 63.0f);
    return k >= 0.0f ? 1.0f - k * (1.0f - v) : 1.0f + k * v;
}
#define TRK_C3 48.0f                         /* the key tracking's pivot: the cutoff as set at C3 */
#define TRK_SEMI (127.0f / (8.8f * 12.0f))   /* a semitone on the cutoff's 0..127 (8.8 octaves) */
#define DC_POLE (1.0f - 2.0f * FM_PI * 5.0f / BS2_RATE)   /* the output's coupling: 5 Hz */
static const uint8_t SUB_WAVE[NSUBW] = {WAVE_SINE, WAVE_PULSE, WAVE_SQUARE};
static float glide_coef(uint32_t v)          /* the pitch's one pole a modulation step: 1 at once, 3 ms .. 2.5 s */
{
    return v ? env_coef(0.003f * fm_exp2f((float)v * (9.7f / 127.0f)), CTRL_RATE) : 1.0f;
}

/* the analog's warmth: each oscillator's pitch wanders a few cents, slowly and on its own (an aim at random every
 * DRIFT_HOLD steps, smoothed twice: no steps, no LFO's regularity), so 1 and 2 beat against each other as the
 * Bass Station II's do warm; the phases run on through every note (never reset: each attack starts elsewhere) */
#define DRIFT_CENTS 3.0f
#define DRIFT_HOLD 1800u                     /* steps between aims: about 1.3 s */
static void drift_step(float c)              /* a modulation step; c the smoothing's coefficient */
{
    static uint32_t rng = 7u, t;
    uint32_t k;
    for (k = 0; k < 2u; k++) {
        if (t == k * (DRIFT_HOLD / 2u)) {    /* (the two aims a half apart) */
            rng = rng * 1664525u + 1013904223u;
            voice.drift_t[k] = (float)(int32_t)rng * (DRIFT_CENTS * 0.01f / 2147483648.0f);
        }
        voice.drift_x[k] += (voice.drift_t[k] - voice.drift_x[k]) * c;
        voice.drift[k] += (voice.drift_x[k] - voice.drift[k]) * c;
    }
    if (++t >= DRIFT_HOLD)
        t = 0;
}
/* the supply's sag: the amplifier's output followed (2 ms up, 80 ms down); over SAG_TH its gain gives way, softly
 * (a full-velocity note: about -1.5 dB; a soft one untouched), as an analog supply's rail droops under a heavy bass note */
#define SAG_TH 0.15f
#define SAG_K 1.2f
static float sag_gain(void)                  /* a modulation step: the gain the level followed leaves */
{
    return 1.0f / (1.0f + SAG_K * fm_maxf(voice.sag_f - SAG_TH, 0.0f));
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static uint32_t rng = 1u;                /* the noise's */
    static afx_t ax;                         /* AFX: the last key's (read at a block's start), or none */
    uint32_t i, j, m, wave, wave2 = patch.wave2, sync = patch.sync;
    int32_t note, cut;
    float mod[NDEST], lv[NLFO], ring = (float)patch.ring / 127.0f, noise;
    float l1 = (float)patch.lvl1 / 127.0f, l2 = (float)patch.lvl2 / 127.0f, ls = (float)patch.lvl_sub / 127.0f;
    float gl = glide_coef(patch.glide), dc = env_coef(1.2f, CTRL_RATE), sg;
    float hu = env_coef(30.0f, CTRL_RATE), hd = env_coef(60.0f, CTRL_RATE), rs = (float)patch.reso / 127.0f;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;   /* MASTER, read once a block */
    float r1 = 12.0f * ((float)patch.range1 - RANGE_8), r2 = 12.0f * ((float)patch.range2 - RANGE_8);
    /* the distortion: a tanh after the amplifier (ADAA), into it up to 16 times, made up after it about as loud */
    float dg = 1.0f + (float)patch.dist * (15.0f / 127.0f), dm = fm_rsqrtf(1.0f + 0.25f * (dg - 1.0f));
    uint32_t subw = SUB_WAVE[patch.sub_wave < NSUBW ? patch.sub_wave : SUB_SQUARE];
    adsr_t menv, aenv;
    fcoef_t fc;
#ifdef PLAT_HOST
    float dsum = 0.0f, dabs = 0.0f;
#endif
    midi_in();
    if (!afx_on)
        ax = (afx_t){-1, 0, 0, 0};
    else if (voice_note >= 0)
        ax = AFX[(uint32_t)(((voice_note - 53 - 12 * set.octave) % NKEYS + NKEYS) % NKEYS)];
    wave = ax.wave >= 0 ? (uint32_t)ax.wave : patch.wave1;
    cut = clamp((int32_t)patch.cutoff + ax.cut, 0, 127);
    noise = (float)clamp((int32_t)patch.noise + ax.noise, 0, 127) * (0.7f / 127.0f);
    adsr_set(&menv, env_time(patch.menv_a), env_time((uint32_t)clamp((int32_t)patch.menv_d + ax.dec, 0, 127)),
             (float)patch.menv_s / 127.0f, env_time(patch.menv_r), CTRL_RATE);
    adsr_set(&aenv, env_time(patch.amp_a), env_time((uint32_t)clamp((int32_t)patch.amp_d + ax.dec, 0, 127)),
             (float)patch.amp_s / 127.0f,
             env_time(patch.amp_r), BS2_RATE);
    for (i = 0; i < 4u; i++)                 /* (a silent filter's states would sink into denormals) */
        voice.flt.s[i] = fm_flush(voice.flt.s[i]);
    voice.flt.hs = fm_flush(voice.flt.hs);
    voice.dc_y = fm_flush(voice.dc_y);
    for (i = 0; i < n; i += m) {             /* a modulation step, then its samples */
        float inc = 0.0f, inc2 = 0.0f, inv, inv2, incs, invs, e, pw1, pw2, kt;
        m = n - i < CTRL_N ? n - i : CTRL_N;
        eng_run(m);
        note = voice_note;                   /* the gate, read once a step */
        if (voice_trig != voice.trig) {      /* a note-on: both envelopes attack from where they are */
            voice.trig = voice_trig;
            voice.vel = vel_gain(voice_vel, patch.vel_amp);
            voice.vm = vel_gain(voice_vel, patch.vel_menv);
            if (voice.amp.stage == ENV_IDLE) /* (from silence: at once, the attack as it was) */
                vel_now = voice.vel;
            voice.amp.stage = voice.menv.stage = ENV_ATTACK;
        }
        if (note >= 0) {
            if (voice.note < 0)              /* (the first note: nothing to glide from) */
                voice.pitch = (float)note;
            voice.note = note;
        } else {
            env_release(&voice.amp);
            env_release(&voice.menv);
        }
        e = mod_step(&menv, m, mod, lv) * voice.vm;
        drift_step(dc);
        heat_step(rs, hu, hd);
        sg = sag_gain();
        mod[DEST_CUTOFF] += e * (float)patch.menv_cut;
        kt = TRK_SEMI * (voice.pitch - TRK_C3) * (float)patch.key_trk * (1.0f / 7.0f);
        filter_coefs(&fc, &patch, fm_clampf((float)cut + kt + 127.0f * mod[DEST_CUTOFF], 0.0f, 127.0f));
        if (voice.note >= 0) {               /* (held under Nyquist: PolyBLEP wants a step under half a turn) */
            float p1 = voice.pitch - 69.0f + 12.0f * mod[DEST_PITCH] + (float)set.tune * 0.01f;
            voice.pitch += ((float)voice.note - voice.pitch) * gl;
            inc = fm_minf(440.0f * fm_exp2f((p1 + voice.drift[0] + r1 + (float)patch.coarse1 + (float)patch.fine1 * 0.01f
                                            + 12.0f * e * (float)patch.menv_o1) / 12.0f) / BS2_RATE, 0.4f);
            inc2 = fm_minf(440.0f * fm_exp2f((p1 + voice.drift[1] + r2 + (float)patch.coarse2 + (float)patch.fine2 * 0.01f
                                              + 12.0f * (mod[DEST_OSC2] + e * (float)patch.menv_o2)) / 12.0f)
                               / BS2_RATE, 0.4f);
        }
        inv = inc > 0.0f ? 1.0f / inc : 0.0f;
        inv2 = inc2 > 0.0f ? 1.0f / inc2 : 0.0f;
        pw1 = pulse_width(wave, patch.pw1, patch.pwm1, lv[1], 63.0f * e * (float)patch.menv_pw1);
        pw2 = pulse_width(wave2, patch.pw2, patch.pwm2, lv[1], 63.0f * e * (float)patch.menv_pw2);
        incs = inc * (patch.sub_oct == 2u ? 0.25f : 0.5f);
        invs = incs > 0.0f ? 1.0f / incs : 0.0f;
#ifdef PLAT_HOST
        cut_now = (int32_t)fm_clampf((float)cut + kt + 127.0f * mod[DEST_CUTOFF], 0.0f, 127.0f);
        pw1_now = (int32_t)(1000.0f * pw1);
        cents_now = (int32_t)(1200.0f * (mod[DEST_PITCH] + e * (float)patch.menv_o1));
        pitch_now = (int32_t)(100.0f * voice.pitch);
        sag_now = (int32_t)(1000.0f * sg);
#endif
        for (j = 0; j < m; j++) {
            float s, o1, o2;
            int32_t *o = lr + 2u * (i + j);
            uint32_t wrap = (voice.phase += inc) >= 1.0f;
            gain_now += (gain - gain_now) * 0.004f;                  /* ~5 ms: turning MASTER does not click */
            vel_now += (voice.vel - vel_now) * 0.004f;
            if (wrap)
                voice.phase -= 1.0f;
            if ((voice.phase2 += inc2) >= 1.0f)
                voice.phase2 -= 1.0f;
            if ((voice.phase_sub += incs) >= 1.0f)
                voice.phase_sub -= 1.0f;
            o1 = osc(voice.phase, inc, inv, wave, pw1);
            if (wrap && sync)                /* hard sync: 1's wrap restarts 2 */
                o2 = osc_sync(&voice.phase2, &voice.o2, voice.phase * inv, inc2, inv2, wave2, pw2);
            else
                o2 = osc(voice.phase2, inc2, inv2, wave2, pw2);
            rng = rng * 1664525u + 1013904223u;
            /* the mixer: 1 + 2 at their levels faded to their ring modulation (twice: as loud), the sub (its narrow
             * pulse a quarter) and the noise; 1 and 2 a sample late */
            s = 0.4f * ((1.0f - ring) * (l1 * voice.o1 + l2 * voice.o2) + 2.0f * ring * voice.o1 * voice.o2
                        + ls * osc(voice.phase_sub, incs, invs, subw, 0.25f)
                        + noise * (float)(int32_t)rng * (1.0f / 2147483648.0f));
            voice.o1 = o1;
            voice.o2 = o2;
            s = ladder(&voice.flt, &fc, s);
            /* the output's coupling (a one-pole high pass at 5 Hz): what DC the filter passes (the ring's, a tanh's
             * on an uneven wave) settles while the voice runs between notes, so the amplifier's gate is no step */
            voice.dc_y = s - voice.dc_x + DC_POLE * voice.dc_y;
            voice.dc_x = s;
            s = voice.dc_y * adsr_step(&voice.amp, &aenv) * vel_now;
            voice.sag_f += (fm_fabsf(s) - voice.sag_f) * (fm_fabsf(s) > voice.sag_f ? 0.01f : 0.0003f);
            voice.sag_g += (sg - voice.sag_g) * 0.02f;
            s *= voice.sag_g;
            if (patch.dist)
                s = overdrive(&voice.dist, s * dg) * dm;
            else                             /* (off: from 0 when it comes on, no step) */
                voice.dist.x1 = voice.dist.F1 = 0.0f;
            s *= 0.3f * gain_now;
#ifdef PLAT_HOST
            dsum += s;
            dabs += fm_fabsf(s);
#endif
            o[0] = (int32_t)s;
            o[1] = (int32_t)s;
        }
    }
#ifdef PLAT_HOST
    {
        static float ds, da;                 /* the sums, leaking a block's 3 % */
        ds = 0.97f * ds + dsum;
        da = 0.97f * da + dabs;
        dc_now = da > 0.0f ? (int32_t)(1000.0f * fm_fabsf(ds) / da) : 0;
    }
#endif
}

void app_panic(void)
{
    seq_auto = 0;
    voice_note = -1;
    voice.amp.lvl = voice.menv.lvl = 0.0f;
    voice.amp.stage = voice.menv.stage = ENV_IDLE;
}

/* a bass line to start from (sequence 1's at first power-on): C3 (a key plays it from there), D#3 tied over two steps */
#define SEQ_LINE {.len = 8, .step = {48, 48, 60, 48, 51, 51, 55, 58, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST,             \
                                     SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST,  \
                                     SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST,  \
                                     SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST},                                         \
                  .leg = {1u << 4}}
static void patch_defaults(patch_t *p)       /* the sound at first power-on: no modulation (the amounts at 0) */
{
    static const patch_t D = {
        .wave1 = WAVE_SAW, .wave2 = WAVE_SAW, .coarse2 = 0,
        .fine2 = 7,                          /* a little apart: the two saws beat */
        .sub_oct = 1, .filter = FLT_LP24, .cutoff = 100, .reso = 20, .drive = 0,
        .menv_a = 0, .menv_d = 70, .menv_s = 0, .menv_r = 50, .menv_cut = 0, .menv_o1 = 0,
        .lfo = {{.wave = LFO_TRI, .rate = 70, .dest = DEST_PITCH, .amt = 0},
                {.wave = LFO_TRI, .rate = 50, .dest = DEST_CUTOFF, .amt = 0}},
        .arp_on = 0, .arp_mode = ARP_UP, .arp_oct = 1, .latch = 0, .gate = 50, .tempo = 120,
        .sq = SEQ_LINE,
        .menv_o2 = 0, .pw1 = 50, .pw2 = 50, .pwm1 = 0, .pwm2 = 0, .noise = 0, .glide = 0, .rhythm = NRHY,
        .swing = 50,
        .lvl1 = 127, .lvl2 = 127, .lvl_sub = 89,   /* (the sub at 0.7: version 4's mix) */
        .sub_wave = SUB_SQUARE, .range1 = RANGE_8, .range2 = RANGE_8,
        .amp_a = 14, .amp_d = 75, .amp_s = 89, .amp_r = 64,   /* version 4's: 5 ms, 300 ms to 70 %, 150 ms */
        .dist = 0,
        .arp_retrig = 1,
        .vel_amp = 63,                       /* (a soft note quieter: as the FM-1 has always played) */
        .key_trk = 7,                        /* (full: the Bass Station II's own) */
    };
    *p = D;
}
static void patch_fill(patch_t *p, uint32_t from)   /* the bytes from FROM on: 0 to the arp, then the defaults' */
{
    patch_t d;
    uint32_t i;
    patch_defaults(&d);
    for (i = from; i < sizeof(patch_t); i++)
        ((uint8_t *)p)[i] = i < offsetof(patch_t, arp_on) ? 0u : ((const uint8_t *)&d)[i];
}

/* --------------------------------------------------------------- settings --- */
static int set_store(void)                   /* the settings into the store: 0 done */
{
    int r = plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    if (r == 0)
        set_kept = set;
    return r;
}
static void settings_defaults(settings_t *s)
{
    s->magic = SET_MAGIC;
    s->octave = 0;
    s->slot = 0;
    s->ble_off = 0;
    s->chan = CH_ALL;
    s->local = 1;
    s->at_out = 1;
    s->clk_out = 0;
    s->tune = 0;
    s->clock = CLK_AUTO;
    s->vcurve = VC_NORMAL;
    s->cc_chan = CH_OFF;
    {
        static const seqmem_t L = SEQ_LINE, E = {.len = 0, .step = {SEQ_REST}};
        uint32_t i, j;
        s->mem[0] = L;
        for (i = 1; i < NSEQ; i++) {         /* (2..4 empty: recorded into) */
            s->mem[i] = E;
            for (j = 0; j < NSTEPS; j++)
                s->mem[i].step[j] = SEQ_REST;
        }
    }
}

/* the patch bank (OBJ_PATCHES): every slot's patch, saved whole. A record of another magic, version, slot count or
 * patch size (another layout of patch_t) is not read: the bank starts from the defaults; and each patch read is held
 * to its controls' ranges (patch_sane), so a damaged field cannot reach the audio as an index out of its table */
#define BANK_MAGIC 0x50325342u           /* "BS2P" */
#define BANK_VERSION 8u                  /* patch_t's layout: one more at each change (2: sync, ring; 3: arp, seq;
                                          * 4: the mod envelope's three depths, PW, noise, glide, rhythm, swing;
                                          * 5: the levels, the sub's wave, the ranges, the amp envelope, distortion;
                                          * 6: the arp's retrig, and its UPDOWN2 before PLAYED;
                                          * 7: oscillator 1's tune, the mod envelope on the widths, the velocities,
                                          * the key tracking; 8: the sequence's 32 steps and their legato) */
#define BANK_V1_PSIZE 24u                /* version 1's patch_t: version 2's up to the ring (sync its spare, 0) */
#define BANK_V2_PSIZE 25u                /* version 2's: version 3's up to the arp */
#define BANK_V3_PSIZE 49u                /* version 3's: version 4's up to menv_o2 (its menv_cut, menv_o1 a DEST_*
                                          * and an amount) */
#define BANK_V4_PSIZE 58u                /* version 4's: version 5's up to the levels */
#define BANK_V5_PSIZE 69u                /* version 5's: version 6's up to the arp's retrig */
#define BANK_V6_PSIZE 70u                /* version 6's: version 7's up to oscillator 1's tune */
#define BANK_V7_PSIZE 77u                /* version 7's: version 8's but the sequence's steps 17..32 and legato */
#define SEQ_GAP (offsetof(patch_t, sq.step) + 16u)   /* where version 7's patches (3's on) lack those, SEQ_GAP_N */
#define SEQ_GAP_N (NSTEPS - 16u + NSTEPS / 8u)
#define V7_TIE 129u                      /* version 7's tie, a step of its own: legato from the step before now */
#define NSLOTS 32u
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t nslots, psize;
    patch_t p[NSLOTS];
} bank_t;
_Static_assert(sizeof(bank_t) <= PLAT_STORE_MAX, "the bank fits a store object");
_Static_assert(offsetof(patch_t, ring) == BANK_V1_PSIZE, "version 1's patches are version 2's without the ring");
_Static_assert(offsetof(patch_t, arp_on) == BANK_V2_PSIZE, "version 2's patches are version 3's without the arp");
_Static_assert(SEQ_GAP > BANK_V2_PSIZE && SEQ_GAP <= BANK_V3_PSIZE, "the gap: in version 3's patches on, not 2's");
_Static_assert(offsetof(patch_t, menv_o2) == BANK_V3_PSIZE + SEQ_GAP_N, "version 3's: 4's without menv_o2 on");
_Static_assert(offsetof(patch_t, lvl1) == BANK_V4_PSIZE + SEQ_GAP_N, "version 4's: 5's without the levels on");
_Static_assert(offsetof(patch_t, arp_retrig) == BANK_V5_PSIZE + SEQ_GAP_N, "version 5's: 6's without retrig");
_Static_assert(offsetof(patch_t, coarse1) == BANK_V6_PSIZE + SEQ_GAP_N, "version 6's: 7's without coarse1 on");
_Static_assert(sizeof(patch_t) == BANK_V7_PSIZE + SEQ_GAP_N, "version 7's patches are version 8's without the gap");
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "the settings fit a store object");
static bank_t bank;
static void patch_sane(patch_t *p);          /* (the UI's: by its controls' ranges) */
static void bank_factory(void);              /* (the factory patches: after the dumps' reader) */

static void bank_load(void)
{
    uint32_t i, ps, v;
    int n = plat_store_load(OBJ_PATCHES, &bank, sizeof bank);
    v = bank.version;
    ps = v == 1u ? BANK_V1_PSIZE : v == 2u ? BANK_V2_PSIZE : v == 3u ? BANK_V3_PSIZE : v == 4u ? BANK_V4_PSIZE
       : v == 5u ? BANK_V5_PSIZE : v == 6u ? BANK_V6_PSIZE : v == 7u ? BANK_V7_PSIZE : 0u;
    if (ps && n == (int)(offsetof(bank_t, p) + NSLOTS * ps) && bank.magic == BANK_MAGIC && bank.nslots == NSLOTS
        && bank.psize == ps) {               /* an older version: each patch moved up, what it lacks filled in */
        uint8_t *b = (uint8_t *)bank.p;
        uint32_t gap = ps >= SEQ_GAP ? SEQ_GAP_N : 0u, j;   /* (version 3's on: the sequence's 16 steps, then the gap) */
        for (i = NSLOTS * ps; i-- > 0u;)     /* (the last byte first: none is overwritten unread) */
            b[i / ps * sizeof(patch_t) + i % ps + (i % ps >= SEQ_GAP ? gap : 0u)] = b[i];
        for (i = 0; i < NSLOTS; i++) {
            patch_t *q = &bank.p[i];
            int8_t a = q->menv_o1;           /* the mod envelope's one destination to its three depths */
            uint32_t d = (uint8_t)q->menv_cut;
            patch_fill(q, ps + gap);
            if (gap) {                       /* the steps 17..32 rests, no legato; a tie the note before, legato */
                for (j = 16u; j < NSTEPS; j++)
                    q->sq.step[j] = SEQ_REST;
                for (j = 0; j < NSTEPS / 8u; j++)
                    q->sq.leg[j] = 0;
                for (j = 0; j < 16u; j++)
                    if (q->sq.step[j] == V7_TIE && j && q->sq.step[j - 1u] != SEQ_REST) {
                        q->sq.step[j] = q->sq.step[j - 1u];
                        q->sq.leg[(j - 1u) >> 3] |= (uint8_t)(1u << ((j - 1u) & 7u));
                    } else if (q->sq.step[j] == V7_TIE)
                        q->sq.step[j] = SEQ_REST;
            }
            if (v >= 7u)
                continue;
            q->key_trk = 0;                  /* (no tracking: an older patch sounds as it did) */
            if (v >= 3u && v <= 5u && q->arp_mode >= ARP_UPDOWN2)   /* (PLAYED and RANDOM one on: UPDOWN2 before them) */
                q->arp_mode++;
            if (v >= 4u)                     /* (version 4's are three already) */
                continue;
            q->menv_cut = d == DEST_CUTOFF ? a : 0;
            q->menv_o1 = d == DEST_PITCH ? a : 0;
            q->menv_o2 = d == DEST_PITCH || d == DEST_OSC2 ? a : 0;
        }
        bank.version = BANK_VERSION;
        bank.psize = sizeof(patch_t);
        n = sizeof bank;
    }
    if (n != (int)sizeof bank || bank.magic != BANK_MAGIC
        || bank.version != BANK_VERSION || bank.nslots != NSLOTS || bank.psize != sizeof(patch_t)) {
        bank.magic = BANK_MAGIC;
        bank.version = BANK_VERSION;
        bank.nslots = NSLOTS;
        bank.psize = sizeof(patch_t);
        bank_factory();
    }
    for (i = 0; i < NSLOTS; i++)
        patch_sane(&bank.p[i]);
}

/* a test build's (BS2_TEST_PATCHES 1): slots 24..32 hold the sounds to record (the waves, the pulse and its PWM, the
 * noise, the glide, the mod envelope on the pitches, an arp rhythm, the swing), played from slot 24 at octave 0, at
 * every power-on (unsaved: SAVE would keep them) */
#ifndef BS2_TEST_PATCHES
#define BS2_TEST_PATCHES 0
#endif
#define NTEST 9u
static void test_patches(void)
{
    patch_t *p = &bank.p[NSLOTS - NTEST];
    uint32_t i;
    for (i = 0; i < NTEST; i++) {            /* the defaults, the filter open, no beating */
        patch_defaults(&p[i]);
        p[i].cutoff = 127;
        p[i].reso = 0;
        p[i].fine2 = 0;
    }
    p[0].wave1 = p[0].wave2 = WAVE_SINE;                                      /* 24 sine */
    p[1].wave1 = p[1].wave2 = WAVE_TRI;                                       /* 25 triangle */
    p[2].wave1 = p[2].wave2 = WAVE_PULSE;                                     /* 26 a 10 % pulse */
    p[2].pw1 = p[2].pw2 = 10;
    p[3].wave1 = p[3].wave2 = WAVE_PULSE;                                     /* 27 PWM, the two apart */
    p[3].lfo[1].rate = 40;
    p[3].pwm1 = 40;
    p[3].pwm2 = -40;
    p[4].noise = 127;                                                         /* 28 noise, the filter swept */
    p[4].cutoff = 80;
    p[4].reso = 70;
    p[4].lfo[1].rate = 30;
    p[4].lfo[1].amt = 30;
    p[5].glide = 90;                                                          /* 29 glide (about 0.35 s) */
    p[6].menv_o1 = 40;                                                        /* 30 the mod env: pitch zaps */
    p[6].menv_o2 = -40;
    p[6].menv_cut = 30;
    p[6].cutoff = 70;
    p[6].menv_d = 60;
    p[7].arp_on = 1;                                                          /* 31 arp, rhythm 8 */
    p[7].rhythm = 8;
    p[7].cutoff = 90;
    p[8].arp_on = 1;                                                          /* 32 arp, swing 70 % */
    p[8].swing = 70;
    p[8].cutoff = 90;
    for (i = 0; i < NTEST; i++)
        patch_sane(&p[i]);
    set.slot = NSLOTS - NTEST;
    set.octave = 0;
}

static void set_sane(void);
static void rnd_load(void);
void app_init(void)
{
    settings_t d;
    int n = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    uint32_t i;
    settings_defaults(&d);
    if (n < (int)SET_V1_SIZE || set.magic != SET_MAGIC || set.octave < -2 || set.octave > 2 || set.slot >= NSLOTS
        || set.ble_off > 1u)
        set = d;
    else                                 /* an older, shorter record: what it lacks from the defaults (the sequences
                                          * whole: before them its last byte was padding) */
        for (i = (uint32_t)n < sizeof set && (uint32_t)n > SET_V2_SIZE ? SET_V2_SIZE : (uint32_t)n; i < sizeof set; i++)
            ((uint8_t *)&set)[i] = ((const uint8_t *)&d)[i];
    set_sane();
    tol_init();
    set_kept = set;
    bank_load();
    rnd_load();
    if (BS2_TEST_PATCHES)
        test_patches();
    patch = bank.p[set.slot];
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];                     /* a short message over the header */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t vel[NKEYS], prs[NKEYS];      /* each key's last velocity, and its pressure while held */
    uint8_t och[NKEYS];                  /* the MIDI channel each held key's note went out on, 1..16 (0: none) */
    int32_t last_key;                    /* the key pressed last: its numbers in the header */
    uint8_t looking;                     /* Bluetooth MIDI devices: REC pressed, the scan's list up (lib/btdev) */
    uint8_t rec_held;                    /* REC down, its release still to act (REC + OCT+ took it: none) */
    int32_t turns[NE];                   /* detents since boot, per encoder */
    uint32_t turned_at[NE];              /* when each was last turned: it shows lit for a moment */
    uint8_t page;                        /* PG_*: what the encoders set */
    uint8_t step;                        /* the sequencer's cursor: the step PRESET picks, recording writes */
    uint8_t rec_n;                       /* the steps recorded since REC (0: the next note starts a new sequence) */
    uint8_t mode;                        /* the arp's mode as last seen: REC's start */
    uint8_t redraw;
    uint32_t lit;                        /* the keys whose notes are held (notes_lit): lit, on the keys and on screen */
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void octave_text(char *t)
{
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
}

/* a screen reader that has just started listening (or asks) hears the whole screen */
static void speak(void)
{
    char t[96], o[4];
    if (ann_on() && ann_where()) {
        octave_text(o);
        t[0] = 0;
        text_cat(t, APP_NAME ". OCTAVE ");
        text_cat(t, o);
        text_cat(t, ". WAVE ");
        text_cat(t, WAVE_NAME[patch.wave1]);
        if (midi_count) {                    /* the last MIDI message in, as the screen shows it */
            static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
            static const char HEX[] = "0123456789ABCDEF";
            uint32_t m = midi_last, b;
            char h[10];
            for (b = 0; b < 3u; b++) {
                uint32_t v = (m >> (8u + 8u * b)) & 0xFFu;
                h[3 * b] = HEX[v >> 4];
                h[3 * b + 1] = HEX[v & 15u];
                h[3 * b + 2] = b < 2u ? ' ' : 0;
            }
            text_cat(t, ". MIDI IN ");
            text_cat(t, midi_last_port < NPORTS ? PORT[midi_last_port] : "");
            text_cat(t, " ");
            text_cat(t, h);
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void note_name(int32_t note, char *b)
{
    static const char *const N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[12];
    b[0] = 0;
    text_cat(b, N[note % 12]);
    int_text(note / 12 - 1, o);
    text_cat(b, o);
}

static void octave_step(int32_t d)
{
    int32_t o = clamp(set.octave + d, -2, 2);
    char t[4];
    if (o == set.octave)
        return;
    fifo_put(&keyq, NOTE_ALL_OFF);           /* a key held across the change would not find its note off */
    set.octave = (int8_t)o;
    set_dirty = 1;
    octave_text(t);
    app_say("OCTAVE", t);
}

int bs2_ble_off(void) { return set.ble_off; }

/* REC: a Bluetooth MIDI device (FM1B_BLE_CENTRAL, lib/btdev): look for devices, then connect to the one ALGORITHM
 * picks, then let it go */
static void ble_device_say(void)
{
    char name[21];
    if (btdev_found(btdev_pick(), name, 0))
        app_say("DEVICE", name);
    else
        app_say("BLE", "SCANNING");
}
static void ble_device_button(void)
{
    int ev;
    if (!btdev_busy() && !ui.looking) {
        ui.looking = 1;
        app_say("BLE", "SCANNING");
        return;
    }
    ev = btdev_save();
    ui.looking = 0;
    app_say(ev == BTDEV_CONNECTING ? btdev_word(ev) : "BLE", ev == BTDEV_CONNECTING ? btdev_name() : btdev_word(ev));
}

/* the pages: HOME the oscillators and the filter, again their mix (the glide, the noise, the pulses' widths), ENV the
 * mod envelope (its three depths), again the amp envelope, FX the mixer's levels, the ranges and the distortion, LFO
 * LFO 1 then 2 (and its PW modulation), EDIT the patch (PRESET picks a slot and
 * plays it; SAVE stores the sound there), ARP the arpeggiator (its mode, REC and PLAY the sequencer's, its rhythm;
 * again: on / off), SEQ the sequencer (which of the four, PRESET its step, K1 the step's note, K2 its legato; again:
 * recording), GLO the global settings (MIDI, then SYS, then
 * DUMP; GLO + OCT+ RND, the RND Synth's seeds: kept with the settings, not the patches). Each page's encoders by plat.h EN_*: a patch_t field each (a
 * settings_t field on the GLO pages, from PG_GMIDI on), its range, and its names when it is a choice; a page's
 * empty places set nothing */
enum { PG_OSC, PG_MIX, PG_MENV, PG_LFO1, PG_LFO2, PG_PATCH, PG_ARP, PG_SEQ, PG_AENV, PG_FX, PG_TUNE, PG_GMIDI, PG_GSYS, PG_GDUMP,
       PG_GRND, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"OSC FILTER", "OSC MIX", "MOD ENV", "LFO 1", "LFO 2", "PATCH", "ARP",
                                              "SEQ", "AMP ENV", "MIX FX", "OSC TUNE", "GLOBAL MIDI", "GLOBAL SYSTEM",
                                              "GLOBAL DUMP", "GLOBAL RND"};
static const char *const SUB_NAME[NSUBW] = {"SINE", "PULSE", "SQUARE"};
static const char *const RANGE_NAME[NRANGE] = {"16'", "8'", "4'", "2'"};
static const char *const OFF_ON[2] = {"OFF", "ON"};
static const char *const ON_OFF[2] = {"ON", "OFF"};   /* (ble_off) */
static const char *const CHAN_NAME[CH_ALL + 1] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11",
                                                  "12", "13", "14", "15", "16", "ALL"};
static const char *const CLK_NAME[NCLK] = {"AUTO", "INT", "EXT"};
static const char *const VC_NAME[NVC] = {"SOFT", "NORMAL", "HARD"};
static const char *const ARP_NAME[NARPM] = {"UP", "DOWN", "UP/DN", "UP/DN2", "PLAYED", "RANDOM", "PLAY", "REC"};
typedef struct {
    const char *name;
    uint8_t off;                             /* offsetof(patch_t or settings_t, the field): int8_t when lo < 0, else
                                              * uint8_t */
    int16_t lo, hi;
    const char *const *names;
} param_t;
#define PF(f) (uint8_t)offsetof(patch_t, f)
#define SF(f) (uint8_t)offsetof(settings_t, f)
#define PARAM_LFO(n, ...) {[EN_SELECT] = {"DEST", PF(lfo[n].dest), 0, NDEST - 1, DEST_NAME},                    \
                           [EN_ALGO] = {"WAVE", PF(lfo[n].wave), 0, NLFOW - 1, LFO_NAME},                       \
                           [EN_K1] = {"RATE", PF(lfo[n].rate), 0, 127, 0},                                     \
                           [EN_K2] = {"AMOUNT", PF(lfo[n].amt), -63, 63, 0}, __VA_ARGS__}
static const param_t PARAMS[NPAGES][NE] = {
    [PG_OSC] = {[EN_SELECT] = {"OSC2", PF(coarse2), -12, 12, 0}, [EN_ALGO] = {"FILTER", PF(filter), 0, NFLT - 1, FLT_NAME},
                [EN_PRESET] = {"WAVE", PF(wave1), 0, NWAVES - 1, WAVE_NAME}, [EN_K1] = {"CUTOFF", PF(cutoff), 0, 127, 0},
                [EN_K2] = {"RESO", PF(reso), 0, 127, 0}, [EN_K3] = {"DRIVE", PF(drive), 0, 127, 0},
                [EN_K4] = {"FINE", PF(fine2), -100, 100, 0}},
    [PG_MIX] = {[EN_SELECT] = {"SYNC", PF(sync), 0, 1, OFF_ON}, [EN_ALGO] = {"WAVE2", PF(wave2), 0, NWAVES - 1, WAVE_NAME},
                [EN_PRESET] = {"GLIDE", PF(glide), 0, 127, 0}, [EN_K1] = {"RING", PF(ring), 0, 127, 0},
                [EN_K2] = {"NOISE", PF(noise), 0, 127, 0}, [EN_K3] = {"PW1", PF(pw1), 5, 95, 0},
                [EN_K4] = {"PW2", PF(pw2), 5, 95, 0}},
    [PG_MENV] = {[EN_SELECT] = {"OSC1", PF(menv_o1), -63, 63, 0}, [EN_ALGO] = {"FILTER", PF(menv_cut), -63, 63, 0},
                 [EN_PRESET] = {"OSC2", PF(menv_o2), -63, 63, 0}, [EN_K1] = {"ATTACK", PF(menv_a), 0, 127, 0},
                 [EN_K2] = {"DECAY", PF(menv_d), 0, 127, 0}, [EN_K3] = {"SUSTAIN", PF(menv_s), 0, 127, 0},
                 [EN_K4] = {"RELEASE", PF(menv_r), 0, 127, 0}},
    [PG_LFO1] = PARAM_LFO(0),
    [PG_LFO2] = PARAM_LFO(1, [EN_K3] = {"PWM1", PF(pwm1), -63, 63, 0}, [EN_K4] = {"PWM2", PF(pwm2), -63, 63, 0}),
    [PG_ARP] = {[EN_SELECT] = {"MODE", PF(arp_mode), 0, NARPM - 1, ARP_NAME}, [EN_ALGO] = {"RETRIG", PF(arp_retrig), 0, 1, OFF_ON},
                [EN_PRESET] = {"LATCH", PF(latch), 0, 1, OFF_ON}, [EN_K1] = {"OCTAVES", PF(arp_oct), 1, 4, 0},
                [EN_K2] = {"SWING", PF(swing), 3, 97, 0}, [EN_K3] = {"TEMPO", PF(tempo), 40, 240, 0},
                [EN_K4] = {"RHYTHM", PF(rhythm), 1, NRHY, 0}},
    [PG_SEQ] = {[EN_SELECT] = {"SEQ", PF(arp_oct), 1, NSEQ, 0}, [EN_ALGO] = {"LENGTH", PF(sq.len), 0, NSTEPS, 0},
                [EN_K3] = {"GATE", PF(gate), 5, 100, 0}, [EN_K4] = {"TEMPO", PF(tempo), 40, 240, 0}},
    [PG_AENV] = {[EN_SELECT] = {"AMP VEL", PF(vel_amp), -63, 63, 0}, [EN_ALGO] = {"MOD VEL", PF(vel_menv), -63, 63, 0},
                 [EN_K1] = {"ATTACK", PF(amp_a), 0, 127, 0}, [EN_K2] = {"DECAY", PF(amp_d), 0, 127, 0},
                 [EN_K3] = {"SUSTAIN", PF(amp_s), 0, 127, 0}, [EN_K4] = {"RELEASE", PF(amp_r), 0, 127, 0}},
    [PG_FX] = {[EN_SELECT] = {"SUBWAVE", PF(sub_wave), 0, NSUBW - 1, SUB_NAME},
               [EN_ALGO] = {"RANGE1", PF(range1), 0, NRANGE - 1, RANGE_NAME},
               [EN_PRESET] = {"RANGE2", PF(range2), 0, NRANGE - 1, RANGE_NAME}, [EN_K1] = {"OSC1", PF(lvl1), 0, 127, 0},
               [EN_K2] = {"OSC2", PF(lvl2), 0, 127, 0}, [EN_K3] = {"SUB", PF(lvl_sub), 0, 127, 0},
               [EN_K4] = {"DIST", PF(dist), 0, 127, 0}},
    [PG_TUNE] = {[EN_SELECT] = {"OSC1", PF(coarse1), -12, 12, 0}, [EN_ALGO] = {"KEY TRK", PF(key_trk), 0, 7, 0},
                 [EN_K1] = {"FINE1", PF(fine1), -100, 100, 0}, [EN_K2] = {"PW1 ENV", PF(menv_pw1), -63, 63, 0},
                 [EN_K3] = {"PW2 ENV", PF(menv_pw2), -63, 63, 0}},
    [PG_GMIDI] = {[EN_SELECT] = {"CC CHAN", SF(cc_chan), CH_OFF, CH_ALL, CHAN_NAME},
                  [EN_K1] = {"CHANNEL", SF(chan), CH_OFF, CH_ALL, CHAN_NAME},
                  [EN_K2] = {"LOCAL", SF(local), 0, 1, OFF_ON},
                  [EN_K3] = {"AT OUT", SF(at_out), 0, 1, OFF_ON},
                  [EN_K4] = {"CLOCK", SF(clock), 0, NCLK - 1, CLK_NAME}},
    [PG_GSYS] = {[EN_K1] = {"TUNE", SF(tune), -50, 50, 0}, [EN_K2] = {"CLK OUT", SF(clk_out), 0, 1, OFF_ON},
                 [EN_K3] = {"VELOCITY", SF(vcurve), 0, NVC - 1, VC_NAME},
                 [EN_K4] = {"BLE", SF(ble_off), 0, 1, ON_OFF}},
};
/* a knob's whole name over the header, when its card's is short */
static const char *const TITLE[NPAGES][NE] = {
    [PG_OSC] = {[EN_K2] = "RESONANCE"},
    [PG_MIX] = {[EN_ALGO] = "OSC 2 WAVE", [EN_K3] = "OSC 1 PULSE WIDTH", [EN_K4] = "OSC 2 PULSE WIDTH"},
    [PG_LFO1] = {[EN_SELECT] = "DESTINATION"},
    [PG_LFO2] = {[EN_SELECT] = "DESTINATION", [EN_K3] = "OSC 1 PULSE WIDTH MOD", [EN_K4] = "OSC 2 PULSE WIDTH MOD"},
    [PG_FX] = {[EN_K4] = "DISTORTION"},
    [PG_AENV] = {[EN_SELECT] = "AMP ENV VELOCITY", [EN_ALGO] = "MOD ENV VELOCITY"},
    [PG_ARP] = {[EN_SELECT] = "ARP MODE", [EN_ALGO] = "SEQ RETRIG", [EN_K2] = "ARP SWING"},
    [PG_SEQ] = {[EN_SELECT] = "SEQUENCE", [EN_ALGO] = "SEQ LENGTH"},
    [PG_TUNE] = {[EN_SELECT] = "OSC 1 COARSE", [EN_ALGO] = "FILTER KEY TRACKING", [EN_K1] = "OSC 1 FINE",
                 [EN_K2] = "MOD ENV OSC 1 PW", [EN_K3] = "MOD ENV OSC 2 PW"},
    [PG_GMIDI] = {[EN_SELECT] = "CC CHANNEL", [EN_K2] = "LOCAL CONTROL", [EN_K3] = "AFTER TOUCH OUT", [EN_K4] = "ARP/SEQ CLOCK"},
    [PG_GSYS] = {[EN_K2] = "MIDI CLOCK OUT", [EN_K4] = "BLUETOOTH MIDI"},
};
static void *page_rec(uint32_t g)            /* what a page's encoders set: the sound, or the settings */
{
    return g >= PG_GMIDI ? (void *)&set : (void *)&patch;
}

static int32_t param_get(const param_t *q, const void *p)
{
    const uint8_t *b = (const uint8_t *)p + q->off;
    return q->lo < 0 ? (int32_t)(int8_t)*b : (int32_t)*b;
}
static void param_put(const param_t *q, void *p, int32_t v)   /* held to its range */
{
    ((uint8_t *)p)[q->off] = (uint8_t)clamp(v, q->lo, q->hi);
}
static void seq_sane(seqmem_t *q)            /* (the steps: by the cursor's K1) */
{
    uint32_t k;
    if (q->len > NSTEPS)
        q->len = NSTEPS;
    for (k = 0; k < NSTEPS; k++)
        if (q->step[k] > SEQ_REST)
            q->step[k] = SEQ_REST;
}
static void patch_sane(patch_t *p)
{
    uint32_t g, k;
    for (g = 0; g < PG_GMIDI; g++)
        for (k = 0; k < NE; k++)
            if (PARAMS[g][k].name)
                param_put(&PARAMS[g][k], p, param_get(&PARAMS[g][k], p));
    if (p->sub_oct != 2u)                    /* (no encoder of its own) */
        p->sub_oct = 1;
    p->arp_on = p->arp_on != 0u;             /* (the ARP button's) */
    seq_sane(&p->sq);
}

static void set_sane(void)                   /* the settings held to the GLO pages' ranges */
{
    uint32_t g, k;
    for (g = PG_GMIDI; g < NPAGES; g++)
        for (k = 0; k < NE; k++)
            if (PARAMS[g][k].name)
                param_put(&PARAMS[g][k], &set, param_get(&PARAMS[g][k], &set));
    for (k = 0; k < NSEQ; k++)
        seq_sane(&set.mem[k]);
}

/* MIDI CC in (on GLO MIDI's CC CHAN): the sound's knobs by the Bass Station II's own CC numbers (its user guide's
 * MIDI parameters list), so a controller or an editor made for it works; a number of two CCs (16:48, the cutoff) by
 * its first, the coarse one. Each a page's encoder, held to its range as a turn is: a value from the middle of 0..127
 * (64) when it goes either way, a choice's by its number (the ranges' from 63, the BS2's; a switch on at 1), else
 * 0..127 across it. Not said (a sweep would flood the screen and the reader); the patch EDITED, SAVE lit */
typedef struct {
    uint8_t cc, page, k;
} cc_map_t;
static const cc_map_t CC_MAP[] = {
    {5, PG_MIX, EN_PRESET},   {16, PG_OSC, EN_K1},      {82, PG_OSC, EN_K2},      {114, PG_OSC, EN_K3},
    {29, PG_OSC, EN_K4},      {30, PG_OSC, EN_SELECT},  {110, PG_MIX, EN_SELECT}, {24, PG_MIX, EN_K1},
    {23, PG_MIX, EN_K2},      {74, PG_MIX, EN_K3},      {79, PG_MIX, EN_K4},      {71, PG_MENV, EN_SELECT},
    {85, PG_MENV, EN_ALGO},   {76, PG_MENV, EN_PRESET}, {102, PG_MENV, EN_K1},    {103, PG_MENV, EN_K2},
    {104, PG_MENV, EN_K3},    {105, PG_MENV, EN_K4},    {88, PG_LFO1, EN_ALGO},   {18, PG_LFO1, EN_K1},
    {89, PG_LFO2, EN_ALGO},   {19, PG_LFO2, EN_K1},     {73, PG_LFO2, EN_K3},     {78, PG_LFO2, EN_K4},
    {118, PG_ARP, EN_SELECT}, {109, PG_ARP, EN_PRESET}, {111, PG_ARP, EN_K1},     {119, PG_ARP, EN_K4},
    {116, PG_ARP, EN_K2},     {90, PG_AENV, EN_K1},     {91, PG_AENV, EN_K2},     {92, PG_AENV, EN_K3},
    {93, PG_AENV, EN_K4},     {80, PG_FX, EN_SELECT},   {70, PG_FX, EN_ALGO},     {75, PG_FX, EN_PRESET},
    {20, PG_FX, EN_K1},       {21, PG_FX, EN_K2},       {22, PG_FX, EN_K3},       {94, PG_FX, EN_K4},
    {27, PG_TUNE, EN_SELECT}, {26, PG_TUNE, EN_K1},     {72, PG_TUNE, EN_K2},     {77, PG_TUNE, EN_K3},
    {112, PG_AENV, EN_SELECT}, {113, PG_AENV, EN_ALGO},
};
static void cc_apply(uint32_t cc, uint32_t v)   /* the main loop's: a CC on CC CHAN, number and value */
{
    uint32_t i;
    if (cc == 108u) {                        /* (the arp's On: a button, no encoder) */
        patch.arp_on = v != 0u;
        set_dirty = 1;
        ui.redraw = 1;
        return;
    }
    for (i = 0; i < sizeof CC_MAP / sizeof CC_MAP[0] && CC_MAP[i].cc != cc; i++)
        ;
    if (i < sizeof CC_MAP / sizeof CC_MAP[0]) {
        const param_t *q = &PARAMS[CC_MAP[i].page][CC_MAP[i].k];
        int32_t x = (int32_t)v, span = q->hi - q->lo;
        if (q->names == RANGE_NAME)
            x -= 63;
        else if (q->names && span == 1)
            x = x != 0;
        else if (!q->names && q->lo < 0)
            x = x < 64 ? q->lo * (64 - x) / 64 : q->hi * (x - 64) / 63;
        else if (!q->names)
            x = q->lo + (x * span + 63) / 127;
        param_put(q, &patch, q->names ? q->lo + x : x);
        set_dirty = 1;
        ui.redraw = 1;
    }
}

/* REC (not on GLO SYS): every knob of the oscillators, their mix, the filter, both envelopes and both LFOs at random,
 * a new sound to start from (unsaved: SAVE keeps it); the filter left a little open and the attack under a second, so
 * it is heard */
static void randomize(void)
{
    static const uint8_t pages[] = {PG_OSC, PG_MIX, PG_MENV, PG_AENV, PG_LFO1, PG_LFO2};
    static uint32_t rng = 1u;
    uint32_t i, k;
    rng ^= plat_ms();
    for (i = 0; i < sizeof pages; i++)
        for (k = 0; k < NE; k++) {
            const param_t *q = &PARAMS[pages[i]][k];
            if (!q->name)
                continue;
            rng = rng * 1664525u + 1013904223u;
            param_put(q, &patch, q->lo + (int32_t)((rng >> 16) % (uint32_t)(q->hi - q->lo + 1)));
        }
    patch.cutoff = (uint8_t)clamp(patch.cutoff, 24, 127);
    patch.amp_a = (uint8_t)clamp(patch.amp_a, 0, 64);
    patch.vel_amp = (int8_t)clamp(patch.vel_amp, 0, 63);   /* (never inverted: a hard note silent) */
    set_dirty = 1;
    app_say("SOUND", "RANDOMIZED");
}

#include "bs2_sysex.c"                    /* Bass Station II patch dumps: bsx_import, bsx_export */
#include "presets/presets.h"              /* the factory patches: PRESETS (bs2/presets/make_presets.py) */

/* the bank at first power-on (no bank in the store, or one not read): the factory patches, a Bass Station II bank's
 * dumps each read into the defaults (what a dump lacks: the default sequence, tempo, gate), as a bank's import has
 * them */
static void bank_factory(void)
{
    char name[BSX_NAME_N + 1u];
    uint32_t i;
    for (i = 0; i < NSLOTS; i++) {
        patch_defaults(&bank.p[i]);
        bsx_import(PRESETS[i], sizeof PRESETS[i], &bank.p[i], name);
        patch_sane(&bank.p[i]);
    }
}

static void step_text(uint32_t v, char *t)   /* a step: its note, or REST */
{
    if (v == SEQ_REST) {
        t[0] = 0;
        text_cat(t, "REST");
    } else {
        note_name((int32_t)v, t);
    }
}
static uint32_t cursor(void)                 /* the sequencer's cursor, inside its length */
{
    if (ui.step >= patch.sq.len)
        ui.step = (uint8_t)(patch.sq.len ? patch.sq.len - 1u : 0u);
    return ui.step;
}
static void seq_changed(void)                /* the patch's sequence into the one it was picked from (kept: glo_keep) */
{
    set.mem[patch.arp_oct - 1u] = patch.sq;
    set_dirty = 1;
}
static void seq_pick(uint32_t n)             /* one of the four into the patch, to play and record */
{
    patch.arp_oct = (uint8_t)n;
    patch.sq = set.mem[n - 1u];
    ui.rec_n = 0;
    set_dirty = 1;
}
static void leg_put(uint32_t i, uint32_t on)
{
    uint8_t *b = &patch.sq.leg[i >> 3];
    *b = (uint8_t)(on ? *b | 1u << (i & 7u) : *b & ~(1u << (i & 7u)));
}
static void step_say(uint32_t i, const char *what)   /* "STEP 3", what */
{
    char t[24];
    t[0] = 0;
    text_cat(t, "STEP ");
    int_text((int32_t)i + 1, t + 5);
    app_say(t, what);
}
/* recording (the arp's REC): each note or rest the next step, the first since REC a new sequence (the Bass Station
 * II's: the steps as many as were played, up to NSTEPS; the rest not kept) */
static void step_enter(uint32_t v)
{
    char n[8];
    uint32_t i;
    if (ui.rec_n >= NSTEPS) {
        app_say("SEQ FULL", "32 STEPS");
        return;
    }
    if (!ui.rec_n)
        for (i = 0; i < NSTEPS / 8u; i++)
            patch.sq.leg[i] = 0;
    i = ui.rec_n++;
    patch.sq.step[i] = (uint8_t)v;
    leg_put(i, 0);
    patch.sq.len = ui.rec_n;
    ui.step = (uint8_t)i;
    seq_changed();
    step_text(v, n);
    step_say(i, n);
}
static void step_legato(void)                /* recording: the step just played legato (a tie when the next is the same
                                              * note), or not again */
{
    uint32_t i = ui.rec_n - 1u;
    if (!ui.rec_n)
        return;
    leg_put(i, !seq_leg(&patch.sq, i));
    seq_changed();
    step_say(i, seq_leg(&patch.sq, i) ? "LEGATO ON" : "LEGATO OFF");
}

static void slot_n_text(uint32_t s, char *t)   /* 01 .. 32 */
{
    t[0] = (char)('0' + (s + 1u) / 10u);
    t[1] = (char)('0' + (s + 1u) % 10u);
    t[2] = 0;
}
static void slot_text(char *t) { slot_n_text(set.slot, t); }

#include "bs2_rnd.c"                      /* RND seeds: the RND Synth's, captured, kept in their table, sent back */

static void seq_keep(int now);
/* the encoders' settings: each turn a step, held at its ends; 0 when this page has nothing on it */
static int knob_turn(uint32_t k, int32_t e)
{
    const param_t *q = &PARAMS[ui.page][k];
    if (ui.page == PG_PATCH && k == EN_PRESET) {   /* a slot: its sound at once (what was not saved is left) */
        uint32_t s = (uint32_t)clamp((int32_t)set.slot + e, 0, NSLOTS - 1);
        if (s != set.slot) {
            seq_keep(1);                     /* (the sequence made here, kept before it goes) */
            set.slot = (uint8_t)s;
            patch = bank.p[s];
            set_dirty = 1;
        }
        return 1;
    }
    if (ui.page == PG_GRND && k == EN_PRESET) {   /* a seed slot: its seed shown */
        rnd_pick(e);
        return 1;
    }
    if ((ui.page == PG_SEQ && k == EN_SELECT) || (ui.page == PG_ARP && k == EN_K1 && patch.arp_mode >= ARP_PLAY)) {
        seq_pick((uint32_t)clamp((int32_t)patch.arp_oct + e, 1, NSEQ));   /* (OCTAVES, the switch, picks it there) */
        return 1;
    }
    if (ui.page == PG_SEQ && k == EN_PRESET) {   /* the cursor */
        ui.step = (uint8_t)clamp((int32_t)cursor() + e, 0, patch.sq.len ? (int32_t)patch.sq.len - 1 : 0);
        return 1;
    }
    if (ui.page == PG_SEQ && (k == EN_K1 || k == EN_K2) && patch.sq.len) {   /* the step's note (REST, then the notes
                                                                             * up), its legato */
        uint32_t c = cursor(), v = patch.sq.step[c];
        int32_t x = clamp((v == SEQ_REST ? 0 : (int32_t)v + 1) + e, 0, 128);
        if (k == EN_K1)
            patch.sq.step[c] = (uint8_t)(x ? x - 1 : SEQ_REST);
        else
            leg_put(c, e > 0);
        seq_changed();
        return 1;
    }
    if (ui.page == PG_SEQ && k == EN_ALGO)   /* the length: the steps after it kept, as they were */
        ui.redraw = 1;
    if (!q->name)
        return 0;
    param_put(q, page_rec(ui.page), param_get(q, page_rec(ui.page)) + e);
    if (ui.page < PG_GMIDI)                  /* (the GLO pages' keep themselves: glo_keep) */
        set_dirty = 1;
    if (ui.page == PG_SEQ && k == EN_ALGO)   /* (the length: into the one picked too) */
        seq_changed();
    if (ui.page == PG_GSYS && k == EN_K4)    /* Bluetooth: at once */
        plat_ble_enable(!set.ble_off);
    return 1;
}
static const char *knob_name(uint32_t k)
{
    if (ui.page == PG_SEQ && (k == EN_PRESET || k == EN_K1 || k == EN_K2))
        return k == EN_PRESET ? "STEP" : k == EN_K1 ? "NOTE" : "LEGATO";
    if (ui.page == PG_ARP && k == EN_K1 && patch.arp_mode >= ARP_PLAY)   /* (the switch: the sequence in REC, PLAY) */
        return "SEQ";
    if (k == EN_PRESET && (ui.page == PG_PATCH || ui.page == PG_GRND))
        return ui.page == PG_PATCH ? "PATCH" : "SEED";
    return PARAMS[ui.page][k].name;
}
static const char *knob_title(uint32_t k)    /* over the header: the whole name */
{
    const char *t = knob_name(k) == PARAMS[ui.page][k].name ? TITLE[ui.page][k] : 0;
    return t ? t : knob_name(k);
}
static void knob_text(uint32_t k, char *t)
{
    const param_t *q = &PARAMS[ui.page][k];
    int32_t v;
    t[0] = 0;
    if (ui.page == PG_PATCH && k == EN_PRESET) {
        slot_text(t);
        return;
    }
    if (ui.page == PG_GRND && k == EN_PRESET) {
        seed_slot_text(t);
        return;
    }
    if (ui.page == PG_SEQ && k == EN_PRESET) {
        int_text((int32_t)cursor() + 1, t);
        return;
    }
    if (ui.page == PG_SEQ && (k == EN_K1 || k == EN_K2)) {
        if (!patch.sq.len)
            text_cat(t, "EMPTY");
        else if (k == EN_K1)
            step_text(patch.sq.step[cursor()], t);
        else
            text_cat(t, OFF_ON[seq_leg(&patch.sq, cursor())]);
        return;
    }
    if (!q->name)
        return;
    v = param_get(q, page_rec(ui.page));
    if (q->names) {
        text_cat(t, q->names[v]);
        return;
    }
    if (q->lo < 0 && v > 0)
        *t++ = '+';
    int_text(v, t);
}

static void page_go(uint32_t g)
{
    char t[4];
    ui.page = (uint8_t)g;
    ui.redraw = 1;
    slot_text(t);
    app_say(PAGE_NAME[g], g == PG_PATCH ? t : "");
}

static void save(void)                       /* the settings, and the sound into its slot */
{
    bank.p[set.slot] = patch;
    if (set_store() == 0 && plat_store_save(OBJ_PATCHES, &bank, sizeof bank) == 0) {
        char t[4];
        set_dirty = 0;
        slot_text(t);
        app_say("SAVED", t);
    } else {
        app_say("SAVE", "FAILED");
    }
}

/* the FM-1 silent a while: nothing sounding (no note, the amp envelope done), the sequencer stopped, the arp with
 * no notes; QUIET_MS of it. A flash write (a sector's erase) stops the audio for tens of ms: the keepers below write
 * only then, where the gap is not heard */
#define QUIET_MS 500u
static uint8_t hushed;                       /* quiet_step's: 1 when silent QUIET_MS */
static void quiet_step(void)                 /* the main loop's (input), before the keepers */
{
    static uint32_t loud_at;
    if (eng.src != SRC_NONE || seq_auto || voice_note >= 0 || voice.amp.stage != ENV_IDLE) {
        loud_at = plat_ms();
        hushed = 0;
    } else {
        hushed = plat_ms() - loud_at >= QUIET_MS;
    }
}

/* the sequence kept as it's made: its steps, which of the four, the gate, the tempo and the swing into the
 * slot's saved patch, SEQ_KEEP_MS after the last change (a run of steps or turns, one flash write) once the FM-1 is
 * quiet (a sequence edited as it plays: kept when it stops), and at once before PRESET leaves the slot; through
 * power-off without SAVE. The rest of the sound still waits for SAVE */
#define SEQ_KEEP_MS 2000u
typedef struct {
    seqmem_t sq;
    uint8_t arp_oct, gate, tempo, swing;
} seqkeep_t;
static void seq_of(const patch_t *p, seqkeep_t *k)
{
    k->sq = p->sq;
    k->arp_oct = p->arp_oct;
    k->gate = p->gate;
    k->tempo = p->tempo;
    k->swing = p->swing;
}
static int seq_same(const seqkeep_t *a, const seqkeep_t *b)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    uint32_t i;
    for (i = 0; i < sizeof *a; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}
static void seq_keep(int now)                /* the main loop's (input); NOW: at once (a slot change) */
{
    static seqkeep_t pend;                   /* the sequence as last seen differing from the saved */
    static uint32_t pend_at;                 /* when it was (0: none waiting) */
    seqkeep_t cur, kept;
    patch_t *b = &bank.p[set.slot];
    seq_of(&patch, &cur);
    seq_of(b, &kept);
    if (seq_same(&cur, &kept)) {
        pend_at = 0;
        return;
    }
    if (!pend_at || !seq_same(&cur, &pend)) {   /* a new change: the wait from now */
        pend = cur;
        pend_at = plat_ms() | 1u;
        if (!now)
            return;
    }
    if (!now && (plat_ms() - pend_at < SEQ_KEEP_MS || !hushed))
        return;
    b->sq = cur.sq;
    b->arp_oct = cur.arp_oct;
    b->gate = cur.gate;
    b->tempo = cur.tempo;
    b->swing = cur.swing;
    pend_at = 0;
    if (plat_store_save(OBJ_PATCHES, &bank, sizeof bank) != 0)
        app_say("SEQ KEEP", "FAILED");
}

/* the GLO pages' settings kept as they're set: into the store's record (the octave and the slot as they were last
 * saved, SAVE's) GLO_KEEP_MS after the last change once the FM-1 is quiet, one flash write for a run of turns;
 * through power-off without SAVE, and without SAVE's writing the sound over its slot */
#define GLO_KEEP_MS 2000u
static void glo_keep(void)                   /* the main loop's (input) */
{
    static settings_t pend;                  /* the settings as last seen differing from the kept */
    static uint32_t pend_at;                 /* when it was (0: none waiting) */
    settings_t rec = set_kept;
    uint32_t i;
    for (i = GLO_FROM; i < sizeof set; i++)
        ((uint8_t *)&rec)[i] = ((const uint8_t *)&set)[i];
    if (same(&rec, &set_kept, sizeof rec)) {
        pend_at = 0;
        return;
    }
    if (!pend_at || !same(&rec, &pend, sizeof rec)) {   /* a new change: the wait from now */
        pend = rec;
        pend_at = plat_ms() | 1u;
        return;
    }
    if (plat_ms() - pend_at < GLO_KEEP_MS || !hushed)
        return;
    pend_at = 0;
    if (plat_store_save(OBJ_SETTINGS, &rec, sizeof rec) == 0)
        set_kept = rec;
    else
        app_say("GLO KEEP", "FAILED");
}

/* Bass Station II patch dumps over USB. The sound's (the edit buffer's) loads into the sound (unsaved: SAVE keeps
 * it); a bank's (each "store into patch N", as a whole bank's .syx file is) into slot N, for N under NSLOTS, the
 * patches after them counted and left (a Bass Station II has 128). A bank's slots keep their own sequence, tempo and
 * gate; it is written to the store once, BANK_IN_MS after its last dump. The sound goes out as one, named by its
 * slot, from FX on the PATCH page or when a computer asks (F0 7D 42 53 32 01 F7); every slot as a bank's dumps from FX
 * on GLO DUMP or when asked (F0 7D 42 53 32 03 F7), a file that comes back into the same slots */
#define BANK_IN_MS 1000u
static uint8_t sx_out[BSX_LEN];
#ifdef PLAT_HOST
static uint8_t sx_bank[NSLOTS][BSX_LEN];
#endif
static struct {
    uint32_t at;                             /* when the last bank dump came in */
    uint8_t n, skipped;                      /* the slots it wrote, the patches past them */
} bank_in;
static void sysex_slot(uint32_t s, const patch_t *p, int32_t to, char *name)   /* one dump out, named by its slot;
                                                                             * TO: the sound's (-1) or a bank's */
{
    char t[4];
    name[0] = 0;
    text_cat(name, "Bass Smth 2 ");   /* (the name, short: a dump's holds 16) */
    slot_n_text(s, t);
    text_cat(name, t);
    bsx_export(p, name, to, sx_out);
    plat_sysex_send(sx_out, BSX_LEN);
#ifdef PLAT_HOST
    if (to >= 0 && (uint32_t)to < NSLOTS)    /* (the host's: a bank's dumps kept, for sysex_bank_out) */
        memcpy(sx_bank[to], sx_out, BSX_LEN);
#endif
}
static void sysex_send(void)
{
    char name[BSX_NAME_N + 1u];
    sysex_slot(set.slot, &patch, -1, name);
    app_say("SYSEX SENT", name);
}
static void sysex_dump_all(void)             /* GLO DUMP: every slot, a bank's dump each; the one playing as it sounds */
{
    char name[BSX_NAME_N + 1u];
    uint32_t s;
    for (s = 0; s < NSLOTS; s++)
        sysex_slot(s, s == set.slot ? &patch : &bank.p[s], (int32_t)s, name);
    app_say("SYSEX SENT", "ALL 32");
}
static void bank_in_step(void)               /* the main loop's (input): a bank come in, written once it has ended */
{
    char t[24], v[12];
    if ((!bank_in.n && !bank_in.skipped) || plat_ms() - bank_in.at < BANK_IN_MS)
        return;
    int_text(bank_in.n, t);                  /* "32 IN, 96 LEFT", or "5 PATCHES" */
    text_cat(t, bank_in.skipped ? " IN, " : " PATCHES");
    if (bank_in.skipped) {
        int_text(bank_in.skipped, v);
        text_cat(t, v);
        text_cat(t, " LEFT");
    }
    if (plat_store_save(OBJ_PATCHES, &bank, sizeof bank) == 0)
        app_say("BANK LOADED", t);
    else
        app_say("BANK LOAD", "FAILED");
    bank_in.n = bank_in.skipped = 0;
}
int app_sysex(const uint8_t *p, uint32_t n)  /* main loop: 1 = this app's */
{
    static const uint8_t ASK[5] = {0x7D, 0x42, 0x53, 0x32, 0x01};   /* 7D (non-commercial) "BS2", 1: send the sound */
    char name[BSX_NAME_N + 1u], t[24];
    int32_t to = -1;
    patch_t q = patch;
    uint32_t i;
    if (rnd_sysex(p, n))                     /* the RND Synth's */
        return 1;
    for (i = 0; i < 4u && i < n && p[i] == ASK[i]; i++)
        ;
    if (i == 4u && n == 5u && p[4] >= 0x01u && p[4] <= 0x03u) {   /* 2: the seed shown (bs2_rnd.c); 3: every slot */
        if (p[4] == 0x01u)
            sysex_send();
        else if (p[4] == 0x02u)
            rnd_send();
        else
            sysex_dump_all();
        return 1;
    }
    to = bsx_slot(p, n);
    if (to >= (int32_t)NSLOTS) {             /* a bank's patch past the FM-1's slots: counted, left */
        if (!bsx_import(p, n, &q, name))
            return 0;
        bank_in.skipped++;
        bank_in.at = plat_ms();
        return 1;
    }
    if (to >= 0)
        q = bank.p[to];                      /* (the slot's own sequence, tempo, gate kept) */
    if (!bsx_import(p, n, &q, name))
        return 0;
    patch_sane(&q);                          /* each field held to its control's range */
    if (to < 0) {
        patch = q;
        set_dirty = 1;
        ui.redraw = 1;
        app_say("SYSEX LOADED", name);
        return 1;
    }
    bank.p[to] = q;
    if ((uint32_t)to == set.slot) {          /* (the slot playing: its new sound now; else no redraw, a dump's time) */
        patch = q;
        ui.redraw = 1;
    }
    bank_in.n++;
    bank_in.at = plat_ms();
    if (bank_in.n == 1u) {                   /* (said once: a word and a redraw each would cost the dumps after it
                                              * their place in the platform's one-frame queue) */
        t[0] = 0;
        text_cat(t, "BANK IN P");
        slot_n_text((uint32_t)to, t + 9);
        app_say(t, name);
    }
    return 1;
}

static uint32_t vel_curve(uint32_t v)        /* GLO SYS's VELOCITY: a key's 1..127, softer (more for less) or harder */
{
    if (set.vcurve == VC_SOFT)
        return (uint32_t)(fm_sqrtf((float)v * 127.0f) + 0.5f);
    if (set.vcurve == VC_HARD)
        return v * v / 127u ? v * v / 127u : 1u;
    return v;
}
static void key_send(uint32_t st, uint32_t note, uint32_t v, uint32_t ch)   /* a key's message on channel ch */
{
    if (ch)
        plat_midi_send(st >> 4 | (st | (ch - 1u)) << 8 | note << 16 | v << 24, TO_USB | TO_BLE);   /* BLE: if built
                                                                                                * with it */
}

/* the notes held now (the keys' and MIDI in's, eng.down) as the keys that play them at this octave, so a note stuck on
 * (its note-off lost) is found and let go by pressing its key; *below, *above: one held under or over the keys */
static uint32_t notes_lit(uint32_t *below, uint32_t *above)
{
    int32_t lo = 53 + 12 * set.octave, n;
    uint32_t m = 0;
    *below = *above = 0;
    for (n = 0; n < 128; n++)
        if (eng.down[n >> 5] >> (n & 31) & 1u) {
            if (n < lo)
                *below = 1;
            else if (n >= lo + (int32_t)NKEYS)
                *above = 1;
            else
                m |= 1u << (n - lo);
        }
    return m;
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, lit, below, above;
    int32_t e;
    touch_poll();                            /* the sensed keys' presses, velocities and pressure */
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k, v;
        int32_t note = 53 + (int32_t)k + 12 * set.octave;
        if ((keys & bit) && !(ui.keys & bit)) {
            v = vel_curve(touch_vel(k));
            ui.vel[k] = (uint8_t)v;
            ui.prs[k] = 0;
            ui.last_key = (int32_t)k;
            if (set.local)                   /* (LOCAL off: MIDI out alone) */
                fifo_put(&keyq, (uint32_t)note | v << 8);
            if (seq_rec)
                step_enter((uint32_t)note);
            ui.och[k] = (uint8_t)(set.chan == CH_ALL ? 1u : set.chan);
            key_send(0x90u, (uint32_t)note, v, ui.och[k]);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            ui.prs[k] = 0;
            fifo_put(&keyq, (uint32_t)note);  /* (LOCAL turned off while it was held: still let go) */
            key_send(0x80u, (uint32_t)note, 0, ui.och[k]);
            ui.och[k] = 0;
        } else if ((keys & bit) && touch_pressure(k) != ui.prs[k]) {   /* pressure: poly aftertouch */
            ui.prs[k] = touch_pressure(k);
            ui.redraw = 1;
            if (set.at_out)
                key_send(0xA0u, (uint32_t)note, ui.prs[k], ui.och[k]);
        }
    }
    for (k = 0; k < NB; k++) {               /* a button pressed: its name, and what it does here */
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_OCTUP && ui.rec_held) {  /* REC + OCT+: AFX on or off (REC's release then nothing) */
            ui.rec_held = 0;
            afx_on = (uint8_t)!afx_on;
            app_say("AFX", OFF_ON[afx_on]);
        } else if (k == B_OCTUP && (btn >> B_GLO & 1u))   /* GLO + OCT+: the RND Synth's seeds (bs2_rnd.c) */
            page_go(PG_GRND);
        else if (k == B_OCTDN)
            octave_step(-1);
        else if (k == B_OCTUP)
            octave_step(1);
        else if (seq_rec && k == B_FX)       /* recording: FX a rest (the Bass Station II's REST), SEL the step
                                              * before legato (its LEGATO) */
            step_enter(SEQ_REST);
        else if (seq_rec && k == B_SEL)
            step_legato();
        else if (k == B_SEL) {
            patch.sub_oct = (uint8_t)(3u - patch.sub_oct);   /* the sub: one octave down or two */
            set_dirty = 1;
            app_say("SUB", patch.sub_oct == 2u ? "-2 OCT" : "-1 OCT");
        } else if (k == B_REC)
            ui.rec_held = 1;                 /* on release (unless OCT+ comes first) */
        else if (k == B_SAVE && ui.page == PG_GRND)   /* the seed shown into its slot */
            rnd_store();
        else if (k == B_SAVE)
            save();
        else if (k == B_HOME)                    /* the oscillators and filter, again their mix, again their tune */
            page_go(ui.page == PG_OSC ? PG_MIX : ui.page == PG_MIX ? PG_TUNE : PG_OSC);
        else if (k == B_ENV)                     /* the mod envelope, again the amp envelope */
            page_go(ui.page == PG_MENV ? PG_AENV : PG_MENV);
        else if (k == B_LFO)                     /* LFO 1, again LFO 2 */
            page_go(ui.page == PG_LFO1 ? PG_LFO2 : PG_LFO1);
        else if (k == B_EDIT)
            page_go(PG_PATCH);
        else if (k == B_ARP && ui.page == PG_ARP) {   /* the page, again the arp on or off */
            patch.arp_on = (uint8_t)!patch.arp_on;
            set_dirty = 1;
            app_say("ARP", OFF_ON[patch.arp_on]);
        } else if (k == B_ARP)
            page_go(PG_ARP);
        else if (k == B_SEQ && ui.page == PG_SEQ) {   /* the page, again recording: the arp's REC, again PLAY */
            patch.arp_mode = seq_rec ? ARP_PLAY : ARP_REC;
            set_dirty = 1;
            app_say("REC", OFF_ON[seq_rec]);
        } else if (k == B_SEQ)
            page_go(PG_SEQ);
        else if (k == B_PLAY) {              /* the sequence (the arp on, in PLAY) at its own key, or stopped */
            uint32_t run = !seq_playing();
            seq_auto = (uint8_t)run;
            if (run && (!patch.arp_on || patch.arp_mode != ARP_PLAY)) {
                patch.arp_on = 1;
                patch.arp_mode = ARP_PLAY;
                set_dirty = 1;
            } else if (!run && patch.arp_on) {
                patch.arp_on = 0;
                set_dirty = 1;
            }
            if (set.clk_out && !ext_clock())  /* (CLK OUT: start or stop to what follows the clock) */
                plat_midi_send(0x0Fu | (run ? 0xFAu : 0xFCu) << 8, TO_USB | TO_BLE);
            app_say("SEQ", run ? "PLAY" : "STOP");
        } else if (k == B_GLO)               /* the global settings: MIDI, again SYS, again DUMP (RND: GLO + OCT+) */
            page_go(ui.page == PG_GMIDI ? PG_GSYS : ui.page == PG_GSYS ? PG_GDUMP : PG_GMIDI);
        else if (k == B_FX && ui.page == PG_PATCH)   /* the sound out as a Bass Station II patch dump */
            sysex_send();
        else if (k == B_FX && ui.page == PG_GDUMP)   /* every slot's */
            sysex_dump_all();
        else if (k == B_FX && ui.page == PG_GRND)    /* the seed shown, as the RND Synth's SysEx */
            rnd_send();
        else if (k == B_FX)
            page_go(PG_FX);
        else {
            app_say("BUTTON", BTN_NAME[k]);
        }
    }
    if (ui.rec_held && !(btn >> B_REC & 1u)) {   /* REC let go: on GLO SYS a Bluetooth device, elsewhere a random
                                                  * sound */
        ui.rec_held = 0;
        if (ui.page != PG_GSYS)
            randomize();
        else if (FM1B_BLE_CENTRAL && !set.ble_off)
            ble_device_button();
        else
            app_say("BUTTON", "REC");
    }
    for (k = 0; k < NE; k++) {               /* an encoder turned: what it sets, and its count */
        char t[12];
        if ((e = plat_enc((int)k)) == 0)
            continue;
        ui.turns[k] += e;
        ui.turned_at[k] = plat_ms();
        ui.redraw = 1;
        if (k == EN_ALGO && FM1B_BLE_CENTRAL && ui.looking && ui.page == PG_GSYS) {
            btdev_choose(btdev_pick() + e);
            ble_device_say();
        } else if (knob_turn(k, e)) {
            knob_text(k, t);
            app_say(knob_title(k), t);
        }
    }
    if (patch.arp_mode != ui.mode) {         /* REC from its start: the next note a new sequence */
        ui.mode = patch.arp_mode;
        ui.rec_n = 0;
    }
    if (!patch.arp_on || patch.arp_mode != ARP_PLAY)   /* (PLAY's own start ends with the arp's PLAY) */
        seq_auto = 0;
    while (fifo_get(&recq, &k))              /* MIDI in's notes while recording */
        if (seq_rec)
            step_enter(k);
    while (fifo_get(&ccq, &k))               /* MIDI in's CCs on CC CHAN */
        cc_apply(k & 0x7Fu, k >> 8);
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    quiet_step();
    bank_in_step();
    seq_keep(0);
    glo_keep();
    lit = notes_lit(&below, &above);
    if (lit != ui.lit)
        ui.redraw = 1;
    ui.lit = lit;
    plat_glow(1);                            /* the unlit buttons glow, readable in the dark */
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u) | (uint32_t)patch.arp_on << B_ARP | (uint32_t)seq_rec << B_SEQ
              | (uint32_t)seq_playing() << B_PLAY | below << B_OCTDN | above << B_OCTUP, keys | lit);   /* (OCT-, OCT+: a
                                              * note held out of the keys' reach, that way) */
}

void app_input_only(void) { input(); }

/* the screen: a small Bass Station II front panel. At the top the sections, a tab each (the page's lit cyan; ARP
 * and SEQ in cyan while they play), then the page's name (or a message for a moment) and the note. The section's
 * card: a picture of its sound (the waves, the filter's curve, the envelope, the LFO, the rhythm, the steps, the
 * mixer) beside its switches (SELECT, ALGO, PRESET), its knobs K1-K4 under them, each with its value and a slider;
 * what was just turned lit cyan. The keys and the status under it. Matte black, grey cards, Novation's cyan */
#define P_BG RGB(18, 18, 18)                 /* the panel: matte black */
#define P_CARD RGB(38, 38, 40)               /* a section's card */
#define P_EDGE RGB(64, 64, 68)               /* its edge, a slider's notches */
#define P_TRACK RGB(54, 54, 58)              /* a slider's track */
#define P_LABEL RGB(150, 150, 156)           /* names */
#define P_TEXT RGB(255, 255, 255)            /* values */
#define P_CYAN RGB(0, 180, 216)              /* live: the page, a knob just turned, a curve, a level */
#define P_AFX RGB(0, 168, 204)               /* AFX on: its mark */
#define P_RND P_AFX                          /* GLO RND: the seed (#00a8cc) */
#define P_CDIM RGB(0, 66, 80)                /* under a curve, the steps that play */
#define P_KEYW RGB(206, 206, 210)
#define P_KEYB RGB(10, 10, 10)
/* the firmware's version as shown (the splash, GLO SYS): a release's "v0.1", a development build's "DEV" */
#define VERSION_TEXT (APP_VERSION[0] >= '0' && APP_VERSION[0] <= '9' ? "v" APP_VERSION : APP_VERSION)
enum { SEC_OSC, SEC_ENV, SEC_LFO, SEC_FX, SEC_ARP, SEC_SEQ, SEC_EDIT, SEC_GLO, NSEC };
static const char *const SEC_NAME[NSEC] = {"OSC", "ENV", "LFO", "FX", "ARP", "SEQ", "EDIT", "GLO"};
static const uint8_t PAGE_SEC[NPAGES] = {[PG_OSC] = SEC_OSC, [PG_MIX] = SEC_OSC, [PG_MENV] = SEC_ENV,
                                         [PG_LFO1] = SEC_LFO, [PG_LFO2] = SEC_LFO, [PG_PATCH] = SEC_EDIT,
                                         [PG_ARP] = SEC_ARP, [PG_SEQ] = SEC_SEQ, [PG_AENV] = SEC_ENV, [PG_FX] = SEC_FX,
                                         [PG_TUNE] = SEC_OSC,
                                         [PG_GMIDI] = SEC_GLO, [PG_GSYS] = SEC_GLO, [PG_GDUMP] = SEC_GLO,
                                         [PG_GRND] = SEC_GLO};
#define TAB_W 29                             /* a section's tab, and its step */
static uint8_t ycol[232];                    /* a picture's curve: each column's row */

static void card(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t edge)
{
    cv_rect(x, y, w, h, edge);
    cv_rect(x + 1, y + 1, w - 2, h - 2, P_CARD);
}
static int knob_lit(uint32_t k) { return ui.turned_at[k] && plat_ms() - ui.turned_at[k] < 300u; }

/* ycol's first w columns as a line from x, y (row 0 its top), filled under to row h - 1 when fill is not 0 */
static void trace(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint16_t fill)
{
    int32_t i;
    if (fill)
        for (i = 0; i < w; i++)
            cv_rect(x + i, y + ycol[i], 1, h - ycol[i], fill);
    for (i = 1; i < w; i++)
        cv_line(x + i - 1, y + ycol[i - 1], x + i, y + ycol[i], c);
}
static void level_col(int32_t i, float v, int32_t h)   /* -1..1 to the column's row */
{
    ycol[i] = (uint8_t)((1.0f - fm_clampf(v, -1.0f, 1.0f)) * 0.5f * (float)(h - 1) + 0.5f);
}
static void wave_pic(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t wave, float pw)   /* two cycles */
{
    int32_t i;
    for (i = 0; i < w; i++) {
        float ph = (float)i * 2.0f / (float)w;
        level_col(i, osc_naive(ph - fm_floorf(ph), wave, pw), h);
    }
    trace(x, y, w, h, P_CYAN, 0);
}
static void lfo_pic(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t n)   /* two cycles, and where it is */
{
    static const int8_t SH[8] = {40, -70, 90, 10, -30, 70, -90, -10};   /* S&H: some steps */
    static const uint8_t AS[NLFOW] = {WAVE_TRI, WAVE_SINE, WAVE_SAW, WAVE_SQUARE, WAVE_SQUARE};
    uint32_t wv = patch.lfo[n].wave;
    int32_t i;
    for (i = 0; i < w; i++) {
        float t = (float)i * 2.0f / (float)w;
        level_col(i, wv == LFO_SH ? (float)SH[(uint32_t)(t * 4.0f) & 7u] * 0.01f
                                  : osc_naive(t - fm_floorf(t), AS[wv], 0.5f), h);
    }
    trace(x, y, w, h, P_CYAN, P_CDIM);
    cv_rect(x + (int32_t)(lfo[n].ph * (float)(w / 2)), y, 1, h, P_TEXT);
}
/* the filter's response, 20 Hz .. 20 kHz across, +12 .. -48 dB down: two poles at the cutoff (the resonance's Q),
 * low, band or high pass; 24 dB another two (a Butterworth's) */
static void filter_pic(int32_t x, int32_t y, int32_t w, int32_t h)
{
    uint32_t f = patch.filter == FLT_ACID ? FLT_LP24 : patch.filter, type = f / 2u, i;
    float fc = 40.0f * fm_exp2f((float)patch.cutoff * (8.8f / 127.0f));
    float q = 0.6f + (float)patch.reso * (6.0f / 127.0f), iq2 = 1.0f / (q * q);
    for (i = 0; i < (uint32_t)w; i++) {
        float r = 20.0f * fm_exp2f((float)i * (10.0f / (float)(w - 1))) / fc, r2 = r * r, m;
        float num = type == 0u ? 1.0f : type == 1u ? r2 : r2 * r2, a = (1.0f - r2) * (1.0f - r2);
        m = num * (type == 1u ? iq2 : 1.0f) / (a + r2 * iq2);
        if (!(f & 1u))
            m *= num * (type == 1u ? 2.0f : 1.0f) / (a + 2.0f * r2);
        m = 3.0103f * fm_log2f(fm_maxf(m, 1e-9f));   /* dB */
        level_col((int32_t)i, (m + 18.0f) / 30.0f, h);
    }
    trace(x, y, w, h, P_CYAN, P_CDIM);
}
/* an envelope: attack, decay and release as long as their knobs, the sustain held a while at its level; the stage
 * it is in lit */
static void adsr_pic(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *adsr, uint32_t stage)
{
    int32_t seg = (w - w / 5) / 3, px[5], py[5], i, k;
    px[0] = x;
    px[1] = px[0] + 1 + adsr[0] * (seg - 1) / 127;
    px[2] = px[1] + 1 + adsr[1] * (seg - 1) / 127;
    px[3] = px[2] + w / 5;
    px[4] = px[3] + 1 + adsr[3] * (seg - 1) / 127;
    py[0] = py[4] = y + h - 1;
    py[1] = y;
    py[2] = py[3] = y + h - 1 - adsr[2] * (h - 1) / 127;
    for (k = 1, i = 0; i <= px[4] - x; i++) {
        while (x + i > px[k])
            k++;
        ycol[i] = (uint8_t)(py[k - 1] - y + (py[k] - py[k - 1]) * (x + i - px[k - 1]) / (px[k] - px[k - 1]));
    }
    trace(x, y, px[4] - x + 1, h, P_TEXT, P_CDIM);
    if (stage != ENV_IDLE) {
        cv_line(px[stage - 1], py[stage - 1], px[stage], py[stage], P_CYAN);
        cv_line(px[stage - 1], py[stage - 1] + 1, px[stage], py[stage] + 1, P_CYAN);
    }
}
/* the rhythm's two bars, a row each: a note's 16ths joined (its first lit while it plays), the rests dark */
static void rhythm_pic(int32_t x, int32_t y, int32_t w, int32_t h)
{
    const char *r = RHYTHM[patch.rhythm - 1u];
    uint32_t i, now = eng.src == SRC_ARP || eng.src == SRC_SEQ ? eng.rpos % RHY_STEPS : RHY_STEPS;
    int32_t cw = w / 16, rh = (h - 4) / 2;
    for (i = 0; i < RHY_STEPS; i++) {
        int32_t cx = x + (int32_t)(i % 16u) * cw, cy = y + (int32_t)(i / 16u) * (rh + 4);
        int32_t join = r[i] == '-' && i % 16u ? 1 : 0;   /* (held: no gap before it, but at a row's start) */
        cv_rect(cx - join, cy, cw - 1 + join, rh, i == now ? P_CYAN : r[i] == '.' ? P_EDGE : P_CDIM);
    }
}
/* the sequencer's 32 steps, four rows: a note's height its pitch (the sequence's lowest to its highest), a legato
 * step joined to the next, the one playing cyan, the cursor white, those past the length dark */
static void steps_pic(int32_t x, int32_t y, int32_t w, int32_t h)
{
    uint32_t i, c = cursor(), len = patch.sq.len;
    uint32_t now = eng.src == SRC_SEQ ? eng.scur : NSTEPS;
    int32_t cw = w / 8, ch = (h - 6) / 4, lo = 127, hi = 0;
    for (i = 0; i < len; i++)
        if (patch.sq.step[i] != SEQ_REST) {
            lo = patch.sq.step[i] < lo ? patch.sq.step[i] : lo;
            hi = patch.sq.step[i] > hi ? patch.sq.step[i] : hi;
        }
    for (i = 0; i < NSTEPS; i++) {
        int32_t sx = x + (int32_t)(i % 8u) * cw, sy = y + (int32_t)(i / 8u) * (ch + 2), v = patch.sq.step[i];
        cv_rect(sx, sy, cw - 2, ch, i == c && len ? P_TEXT : P_EDGE);
        cv_rect(sx + 1, sy + 1, cw - 4, ch - 2, i < len ? P_BG : P_CARD);
        if (i >= len || v == SEQ_REST)
            continue;
        v = 1 + (hi > lo ? (v - lo) * (ch - 3) / (hi - lo) : (ch - 3) / 2);   /* (1 .. ch - 2 high) */
        cv_rect(sx + 1, sy + ch - 1 - v, cw - 4, v, i == now ? P_CYAN : P_CDIM);
        if (seq_leg(&patch.sq, i) && i % 8u < 7u)   /* (legato: over the gap to the next) */
            cv_rect(sx + cw - 3, sy + ch - 3, 3, 2, i == now ? P_CYAN : P_CDIM);
    }
}
static void mixer_pic(int32_t x, int32_t y, int32_t w, int32_t h)   /* the levels: oscillators 1, 2, sub, noise */
{
    static const char *const N[4] = {"1", "2", "S", "N"};
    const uint8_t lv[4] = {patch.lvl1, patch.lvl2, patch.lvl_sub, patch.noise};
    int32_t i, cw = w / 4, fh = h - 15;
    for (i = 0; i < 4; i++) {
        int32_t cx = x + i * cw + cw / 2, f = lv[i] * fh / 127;
        cv_rect(cx - 2, y, 5, fh, P_TRACK);
        cv_rect(cx - 2, y + fh - f, 5, f, P_CYAN);
        cv_text(cx - text_w(&FONT_XS, N[i]) / 2, y + fh, &FONT_XS, N[i], P_LABEL);
    }
}

/* the section's picture in the card at x, y, w wide: its name, then the picture under it */
static void chan_row(int32_t x, int32_t y, const char *name, uint32_t ch)   /* GLO MIDI's: "IN  CH 5" */
{
    cv_text(x, y, &FONT_XS, name, P_LABEL);
    cv_text(x + 40, y, &FONT_XS, ch == CH_ALL ? "ANY" : ch ? "CH" : "OFF", P_TEXT);
    if (ch && ch != CH_ALL)
        cv_text(x + 62, y, &FONT_XS, CHAN_NAME[ch], P_CYAN);
}
/* OSC TUNE's picture: the cutoff over the keys (flat: no tracking; steeper to full, an octave an octave), C3 marked */
static void tune_pic(int32_t x, int32_t y, int32_t w)
{
    int32_t i, h = 26, by = y + 22 + h, c = x + w * 3 / 8;
    cv_text(x, y, &FONT_XS, "CUTOFF / KEYS", P_LABEL);
    for (i = 0; i < w; i++) {                /* (the keys across, five octaves from C1; the line's slope the tracking) */
        int32_t dy = (int32_t)((float)(i - (c - x)) * (float)h * (float)patch.key_trk / (7.0f * (float)w));
        ycol[i] = (uint8_t)clamp(h / 2 - dy, 0, h - 1);
    }
    trace(x, by - h, w, h, P_CYAN, P_CDIM);
    cv_line(c, by - h, c, by - 1, P_EDGE);
}
static void picture(int32_t x, int32_t y, int32_t w)
{
    const int32_t py = y + 18, ph = 38;
    char t[8];
    switch (ui.page) {
    case PG_OSC:
        cv_text(x, y, &FONT_XS, "OSC 1", P_LABEL);
        cv_text(x + 52, y, &FONT_XS, FLT_NAME[patch.filter], P_LABEL);
        wave_pic(x, py, 46, ph, patch.wave1, pulse_width(patch.wave1, patch.pw1, 0, 0.0f, 0.0f));
        filter_pic(x + 52, py, w - 52, ph);
        break;
    case PG_MIX:
        cv_text(x, y, &FONT_XS, "OSC 1", P_LABEL);
        cv_text(x + 52, y, &FONT_XS, patch.sync ? "2 SYNC" : "OSC 2", patch.sync ? P_CYAN : P_LABEL);
        wave_pic(x, py, 46, ph, patch.wave1, pulse_width(patch.wave1, patch.pw1, 0, 0.0f, 0.0f));
        wave_pic(x + 52, py, w - 52, ph, patch.wave2, pulse_width(patch.wave2, patch.pw2, 0, 0.0f, 0.0f));
        break;
    case PG_MENV:
    case PG_AENV: {
        const uint8_t *e = ui.page == PG_MENV ? &patch.menv_a : &patch.amp_a;
        cv_text(x, y, &FONT_XS, PAGE_NAME[ui.page], P_LABEL);
        adsr_pic(x, py, w, ph, e, ui.page == PG_MENV ? voice.menv.stage : voice.amp.stage);
        break;
    }
    case PG_LFO1:
    case PG_LFO2:
        cv_text(x, y, &FONT_XS, PAGE_NAME[ui.page], P_LABEL);
        lfo_pic(x, py, w, ph, ui.page - PG_LFO1);
        break;
    case PG_FX:
        cv_text(x, y, &FONT_XS, patch.sub_oct == 2u ? "SUB -2" : "SUB -1", P_LABEL);
        cv_text(x + 52, y, &FONT_XS, "MIXER", P_LABEL);
        wave_pic(x, py, 46, ph, patch.sub_wave == SUB_SINE ? WAVE_SINE : WAVE_PULSE,
                 patch.sub_wave == SUB_PULSE ? 0.25f : 0.5f);
        mixer_pic(x + 52, py, w - 52, ph);
        break;
    case PG_TUNE:                            /* oscillator 1's tune, the key tracking's line, the widths' envelope */
        tune_pic(x, y, w);
        break;
    case PG_ARP:
        cv_text(x, y, &FONT_XS, "RHYTHM", P_LABEL);
        cv_text(x + w - text_w(&FONT_XS, OFF_ON[patch.arp_on]), y, &FONT_XS, OFF_ON[patch.arp_on],
                patch.arp_on ? P_CYAN : P_LABEL);
        rhythm_pic(x, py + 6, w, ph - 12);
        break;
    case PG_SEQ:
        t[0] = 0;                            /* "SEQ 2", what it does: REC, PLAY */
        text_cat(t, "SEQ ");
        int_text(patch.arp_oct, t + 4);
        cv_text(x, y, &FONT_XS, t, P_LABEL);
        if (seq_playing() || seq_rec)
            cv_text(x + w - text_w(&FONT_XS, seq_rec ? "REC" : "PLAY"), y, &FONT_XS, seq_rec ? "REC" : "PLAY", P_CYAN);
        steps_pic(x, py, w, ph);
        break;
    case PG_GMIDI:                           /* where the knobs' CCs, the notes come from and go, and the clock */
        chan_row(x, y, "CC", set.cc_chan);
        chan_row(x, py, "IN", set.chan);
        chan_row(x, py + 13, "OUT", set.chan == CH_ALL ? 1u : set.chan);
        cv_text(x, py + 26, &FONT_XS, "CLOCK", P_LABEL);
        cv_text(x + 40, py + 26, &FONT_XS, ext_clock() ? "MIDI IN" : set.clk_out ? "SENDING" : "INTERNAL",
                ext_clock() || set.clk_out ? P_CYAN : P_TEXT);
        break;
    case PG_GSYS:                            /* the tempo's source, Bluetooth */
        cv_text(x, y, &FONT_XS, "SYSTEM", P_LABEL);
        cv_text(x, py, &FONT_XS, "TEMPO", P_LABEL);
        if (ext_clock()) {
            cv_text(x + 58, py, &FONT_XS, eng.ext_age < EXT_HOLD ? "MIDI CLOCK" : "WAITING FOR CLOCK", P_CYAN);
        } else {
            int_text(patch.tempo, t);
            text_cat(t, " BPM");
            cv_text(x + 58, py, &FONT_XS, t, P_TEXT);
        }
        cv_text(x, py + 13, &FONT_XS, "BLE", P_LABEL);
        cv_text(x + 58, py + 13, &FONT_XS, set.ble_off ? "OFF" : plat_ble_connected() ? "CONNECTED" : "ON",
                !set.ble_off && plat_ble_connected() ? P_CYAN : P_TEXT);
        cv_text(x, py + 26, &FONT_XS, "FIRMWARE", P_LABEL);
        cv_text(cv_text(x + 58, py + 26, &FONT_XS, APP_NAME, P_TEXT) + 4, py + 26, &FONT_XS, VERSION_TEXT, P_CYAN);
        break;
    case PG_GDUMP:                           /* the bank, large */
        cv_text(x, y, &FONT_XS, "SYSEX DUMP", P_LABEL);
        cv_text(x, py, &FONT_L, "ALL 32", P_CYAN);
        break;
    case PG_GRND: {                          /* the seed shown; where it came from; kept in the slot (the chip) */
        char s[12];
        seed_text(rnd_cur, s);
        cv_text(x, y, &FONT_XS, "RND SEED", P_LABEL);
        cv_text(x, y + 16, &FONT_M, s, rnd_cur == RND_EMPTY ? P_LABEL : P_RND);
        cv_text(x, y + 42, &FONT_XS, rnd_src == RS_SYNTH ? "FROM RND" : rnd_src == RS_SLOT ? "FROM SLOT" : "", P_RND);
        if (rnd_cur != RND_EMPTY && seeds.seed[rnd_slot] == rnd_cur)
            cv_text(x + w - text_w(&FONT_XS, "SAVED"), y + 42, &FONT_XS, "SAVED", P_TEXT);
        break;
    }
    default:                                 /* PG_PATCH: the slot, large */
        cv_text(x, y, &FONT_XS, set_dirty ? "PATCH  EDITED" : "PATCH", P_LABEL);
        t[0] = 'P';
        slot_text(t + 1);
        cv_text(x, py, &FONT_L, t, P_CYAN);
        break;
    }
}

/* a knob's slider: a choice's positions (the one set lit), else its value from the left (from the middle when it
 * goes either way) with a thumb */
static void slider(int32_t x, int32_t y, int32_t w, const param_t *q)
{
    int32_t v = param_get(q, page_rec(ui.page)) - q->lo, span = q->hi - q->lo, p = v * (w - 1) / span, i;
    if (q->names && span < 8) {              /* (more, as a value: the MIDI channel) */
        for (i = 0; i <= span; i++)
            cv_rect(x + i * w / (span + 1), y, w / (span + 1) - 1, 5, i == v ? P_CYAN : P_EDGE);
        return;
    }
    cv_rect(x, y + 1, w, 3, P_TRACK);
    if (q->lo < 0) {
        int32_t c = -q->lo * (w - 1) / span;
        cv_rect(x + (p < c ? p : c), y + 1, (p < c ? c - p : p - c) + 1, 3, P_CYAN);
        cv_rect(x + c, y - 1, 1, 7, P_LABEL);
    } else {
        cv_rect(x, y + 1, p + 1, 3, P_CYAN);
    }
    cv_rect(x + p - 1, y - 1, 3, 7, P_TEXT);
}
static void chip(int32_t x, int32_t y, int32_t w, uint32_t k)   /* a switch: its name, its setting */
{
    char t[12];
    int lit = knob_lit(k);
    card(x, y, w, 20, lit ? P_CYAN : P_EDGE);
    cv_text(x + 5, y + 2, &FONT_XS, knob_name(k), P_LABEL);
    knob_text(k, t);
    cv_text(x + w - 5 - text_w(&FONT_XS, t), y + 2, &FONT_XS, t, lit ? P_CYAN : P_TEXT);
}
static void knob_card(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t k)   /* K1-K4: name, value, slider */
{
    static const char *const TAG[4] = {"K1", "K2", "K3", "K4"};
    const param_t *q = &PARAMS[ui.page][k];
    int lit = knob_lit(k);
    char t[12];
    card(x, y, w, h, lit ? P_CYAN : P_EDGE);
    cv_text(x + 5, y + 3, &FONT_XS, TAG[k - EN_K1], lit ? P_CYAN : P_CDIM);
    if (!knob_name(k))
        return;
    cv_text(x + 5, y + 19, &FONT_XS, knob_name(k), P_LABEL);
    knob_text(k, t);
    cv_text(x + 5, y + 36, text_w(&FONT_B, t) <= w - 10 ? &FONT_B : &FONT_XS, t, lit ? P_CYAN : P_TEXT);
    if (q->name)
        slider(x + 5, y + h - 12, w - 10, q);
}

/* the audio's load on the GLO pages' header, for testing: "CPU 34/41", now (the most of the last quarter second) and
 * the most of the last 5 s, then "!n" after any dropout since boot (the audio missed its deadline: a click) */
#define CPU_NOW_MS 250u
#define CPU_PEAK_MS 5000u
static uint32_t cpu_now, cpu_peak;
static int cpu_meter(uint32_t now)           /* a frame: 1 when the numbers shown changed */
{
    static uint32_t win, win_at, peak_at, shown;
    uint32_t c = plat_cpu_pct(), was = cpu_now | cpu_peak << 8 | (plat_xruns() & 0xFFFFu) << 16;
    win = c > win ? c : win;
    if (now - win_at >= CPU_NOW_MS) {
        cpu_now = win;
        win = 0;
        win_at = now;
        if (cpu_now >= cpu_peak || now - peak_at >= CPU_PEAK_MS) {
            cpu_peak = cpu_now;
            peak_at = now;
        }
    }
    if (was == shown)
        return 0;
    shown = was;
    return 1;
}
static void cpu_text(char *t)                /* "CPU 34/41", "CPU 34/41 !2" */
{
    char n[12];
    t[0] = 0;
    text_cat(t, "CPU ");
    int_text((int32_t)cpu_now, n);
    text_cat(t, n);
    text_cat(t, "/");
    int_text((int32_t)cpu_peak, n);
    text_cat(t, n);
    if (plat_xruns()) {
        text_cat(t, " !");
        int_text((int32_t)(plat_xruns() > 999u ? 999u : plat_xruns()), n);
        text_cat(t, n);
    }
}

static void head_draw(void)                  /* the section tabs; the page's name or a message, the note (on GLO: the
                                              * audio's load) */
{
    char t[24];
    int32_t note = voice_note, x;
    uint32_t i;
    cv_begin(240, 36, P_BG);
    for (i = 0; i < NSEC; i++) {
        int on = PAGE_SEC[ui.page] == i, run = (i == SEC_ARP && patch.arp_on) || (i == SEC_SEQ && (seq_playing() || seq_rec));
        x = 5 + (int32_t)i * TAB_W;
        cv_rect(x, 1, TAB_W - 1, 18, on ? P_CYAN : P_CARD);
        cv_text(x + (TAB_W - 1 - text_w(&FONT_XS, SEC_NAME[i])) / 2, 2, &FONT_XS, SEC_NAME[i],
                on ? P_BG : run ? P_CYAN : P_LABEL);
    }
    if (plat_ms() < ui.msg_until) {
        x = cv_text(6, 20, &FONT_B, ui.msg[0], P_TEXT);
        cv_text(x + 8, 20, &FONT_B, ui.msg[1], P_CYAN);
    } else {
        cv_text(6, 20, &FONT_B, PAGE_NAME[ui.page], P_TEXT);
    }
    if (ui.page >= PG_GMIDI && (plat_ms() >= ui.msg_until || !ui.msg[1][0])) {   /* (not over a message's value) */
        cpu_text(t);
        cv_text(234 - text_w(&FONT_XS, t), 21, &FONT_XS, t, plat_xruns() ? P_CYAN : P_LABEL);
    } else if (plat_ms() >= ui.msg_until) {
        if (note >= 0) {                     /* the note, and the last key's velocity and pressure */
            note_name(note, t);
            x = 234 - text_w(&FONT_B, t);
            cv_text(x, 20, &FONT_B, t, P_CYAN);
            if (ui.keys >> ui.last_key & 1u) {
                char n[24] = "V ";
                int_text(ui.vel[ui.last_key], t);
                text_cat(n, t);
                text_cat(n, "  P ");
                int_text(ui.prs[ui.last_key], t);
                text_cat(n, t);
                cv_text(x - 8 - text_w(&FONT_XS, n), 21, &FONT_XS, n, P_LABEL);
            }
        }
    }
    cv_blit(0, 0);
}

static void panel_draw(void)                 /* the section's card: its picture, switches and knobs */
{
    uint32_t i;
    int32_t gw = knob_name(EN_SELECT) || knob_name(EN_ALGO) || knob_name(EN_PRESET) ? 112 : 232;
    cv_begin(240, 144, P_BG);
    card(4, 2, gw, 62, P_EDGE);
    picture(9, 5, gw - 10);
    for (i = EN_SELECT; i <= EN_PRESET; i++)
        if (knob_name(i))
            chip(120, 2 + (int32_t)i * 21, 116, i);
    if (ui.page == PG_PATCH) {               /* no knobs: what the buttons do here */
        cv_text(6, 76, &FONT_XS, "PRESET  picks a patch, plays it", P_LABEL);
        cv_text(6, 94, &FONT_XS, "SAVE  stores the sound there", P_LABEL);
        cv_text(6, 112, &FONT_XS, "FX  sends it as a BS2 SysEx dump", P_LABEL);
    } else if (ui.page == PG_GDUMP) {
        cv_text(6, 76, &FONT_XS, "FX  sends every patch, 01 to 32,", P_LABEL);
        cv_text(6, 94, &FONT_XS, "     as BS2 SysEx dumps over USB", P_LABEL);
        cv_text(6, 112, &FONT_XS, "GLO settings save themselves", P_LABEL);
    } else if (ui.page == PG_GRND) {         /* no knobs: what the buttons do here */
        cv_text(6, 76, &FONT_XS, "PRESET  picks a slot, shows its seed", P_LABEL);
        cv_text(6, 94, &FONT_XS, "SAVE  keeps the seed shown there", P_LABEL);
        cv_text(6, 112, &FONT_XS, "FX  sends it to the RND Synth (USB)", P_LABEL);
    } else {
        for (i = EN_K1; i <= EN_K4; i++)
            knob_card(4 + (int32_t)(i - EN_K1) * 59, 68, 55, 74, i);
    }
    cv_blit(0, 36);
}

static void key_cell(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t k, uint16_t off)
{
    int32_t v, p;
    cv_rect(x, y, w, h, off);
    if (!(ui.keys >> k & 1u)) {              /* not pressed: its note held all the same (MIDI in), filled */
        if (ui.lit >> k & 1u)
            cv_rect(x, y, w, h, P_CYAN);
        return;
    }
    v = (ui.vel[k] * h + 126) / 127;
    p = (ui.prs[k] * h + 126) / 127;
    cv_rect(x, y + h - v, w, v, P_CYAN);
    if (p)
        cv_rect(x + 2, y + h - p, w - 4, p, P_TEXT);
}

static void keys_draw(void)                  /* the keys; the octave, the slot, Bluetooth, the volume, MIDI in */
{
    char t[24];
    uint32_t i, pct = plat_master() * 100u / 4096u;
    int32_t x;
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    /* black above white: a held key fills to its velocity (cyan), its pressure over that (white); a key whose note
     * MIDI in holds, all cyan */
    cv_begin(240, 60, P_BG);
    for (i = 0; i < 16u; i++)
        key_cell(4 + (int32_t)i * 14, 14, 13, 14, WK[i], P_KEYW);
    for (i = 0; i < 11u; i++) {
        static const uint8_t AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};   /* the white key each one follows */
        cv_rect(4 + (int32_t)AT[i] * 14 + 7, 0, 13, 13, P_EDGE);
        key_cell(4 + (int32_t)AT[i] * 14 + 8, 0, 11, 12, BK[i], P_KEYB);
    }
    octave_text(t);
    cv_text(4, 31, &FONT_XS, "OCT", P_LABEL);
    cv_text(30, 31, &FONT_XS, t, P_CYAN);
    slot_text(t + 1);
    t[0] = 'P';
    x = cv_text(64, 31, &FONT_XS, t, P_TEXT);
    if (set_dirty)                           /* not saved yet */
        cv_text(x + 2, 31, &FONT_XS, "*", P_CYAN);
    x = 104;
    if (FM1B_BLE)                            /* Bluetooth: grey while it waits, lit while a computer is connected */
        x = cv_text(x, 31, &FONT_XS, "BLE", plat_ble_connected() ? P_CYAN : P_EDGE) + 6;
    if (FM1B_BLE_CENTRAL)                    /* a Bluetooth MIDI device: lit while connected */
        cv_text(x, 31, &FONT_XS, "DEV", plat_ble_device() == BLE_DEV_READY ? P_CYAN : P_EDGE);
    cv_text(178, 31, &FONT_XS, "VOL", P_LABEL);
    int_text((int32_t)pct, t);
    text_cat(t, "%");
    cv_text(204, 31, &FONT_XS, t, P_CYAN);
    if (midi_count) {                        /* the last MIDI message in, and where from */
        static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
        static const char HEX[] = "0123456789ABCDEF";
        uint32_t m = midi_last, b;
        char h[12];
        for (b = 0; b < 3u; b++) {
            uint32_t v = (m >> (8u + 8u * b)) & 0xFFu;
            h[3 * b] = HEX[v >> 4];
            h[3 * b + 1] = HEX[v & 15u];
            h[3 * b + 2] = b < 2u ? ' ' : 0;
        }
        cv_text(4, 45, &FONT_XS, "MIDI IN", P_LABEL);
        cv_text(52, 45, &FONT_XS, midi_last_port < NPORTS ? PORT[midi_last_port] : "", P_CYAN);
        cv_text(82, 45, &FONT_XS, h, P_TEXT);
    }
    if (seq_rec || seq_playing() || patch.arp_on)   /* what plays the keys: recording, the sequencer, the arp */
        cv_text(178, 45, &FONT_XS, seq_rec ? "REC" : patch.arp_mode == ARP_PLAY ? "SEQ" : "ARP", P_CYAN);
    if (afx_on)                              /* each key its own sound */
        cv_text(212, 45, &FONT_XS, "AFX", P_AFX);
    cv_blit(0, 180);
}

static void draw(void)
{
    head_draw();
    panel_draw();
    keys_draw();
}

/* what a section's picture shows moving (an envelope's stage, an LFO, the arp's or sequencer's step): its panel
 * drawn again when this changes */
static uint32_t live(void)
{
    switch (ui.page) {
    case PG_MENV:
        return voice.menv.stage;
    case PG_AENV:
        return voice.amp.stage;
    case PG_LFO1:
    case PG_LFO2:
        return plat_ms() / 50u;              /* 20 times a second */
    case PG_ARP:
        return eng.src == SRC_ARP || eng.src == SRC_SEQ ? eng.rpos % RHY_STEPS + 1u : 0u;
    case PG_SEQ:
        return eng.src == SRC_SEQ ? eng.rpos + 1u : 0u;
    case PG_GMIDI:
    case PG_GSYS:
        return (uint32_t)ext_clock() | (uint32_t)(eng.ext_age < EXT_HOLD) << 1 | (uint32_t)plat_ble_connected() << 2;
    default:
        return 0;
    }
}

#define SPLASH_SUB "Inspired by the Bass Station 2"
/* the splash's ground: pure black, not the panel's matte black. The LCD keeps a faint shadow of the keys after long
 * hours of them (image retention), and a dark grey shows it; black hides it */
#define S_BG RGB(0, 0, 0)
static int splash_on;                        /* it is up (the host simulator never shows it) */

void app_splash(void)                       /* the platform's, on the panel's black and in its cyan */
{
    lcd_fill(0, 0, 240, 240, S_BG);
    cv_begin(240, APP_BIG_FONT.h, S_BG);
    cv_text((240 - text_w(&APP_BIG_FONT, APP_NAME)) / 2, 0, &APP_BIG_FONT, APP_NAME, P_CYAN);
    cv_blit(0, 92);
    cv_begin(240, FONT_XS.h, S_BG);          /* under it, in the names' grey */
    cv_text((240 - text_w(&FONT_XS, SPLASH_SUB)) / 2, 0, &FONT_XS, SPLASH_SUB, P_LABEL);
    cv_blit(0, 100 + APP_BIG_FONT.h);
    cv_begin(240, FONT_XS.h, S_BG);          /* the version, in the bottom right corner */
    cv_text(232 - text_w(&FONT_XS, VERSION_TEXT), 0, &FONT_XS, VERSION_TEXT, P_LABEL);
    cv_blit(0, 232 - FONT_XS.h);
    splash_on = 1;
}

/* after the platform's start: the splash a while longer, a scope under the name whose wave morphs as the panel's OSC
 * WAVE steps (sine, triangle, saw, then a square whose width sweeps). Always SPLASH_SCOPE_MS, on the FM-1 and in the
 * emulator alike: the morph run in however long the splash had been up (as little as 0.4 s) blurred the waves */
#define SPLASH_SCOPE_MS 2400u
static void boot_scope(float u)              /* the scope at u, 0..1 of the way through */
{
    static const uint8_t W[] = { WAVE_SINE, WAVE_TRI, WAVE_SAW, WAVE_PULSE };
    enum { SX = 20, SY = 164, SW = 200, SH = 48 };
    float pos = u * 3.999f, a = pos - fm_floorf(pos), pw = 0.5f + 0.4f * fm_sin_turns(u * 2.0f);
    uint32_t k = (uint32_t)pos, i;
    a = a * a * (3.0f - 2.0f * a);           /* each change eased */
    for (i = 0; i < SW; i++) {
        float ph = (float)i * 2.0f / (float)SW + u * 3.0f;
        ph -= fm_floorf(ph);
        level_col((int32_t)i, 0.85f * ((1.0f - a) * osc_naive(ph, W[k], pw) + a * osc_naive(ph, W[k < 3u ? k + 1u : 3u], pw)), SH);
    }
    cv_begin(SW, SH, S_BG);
    trace(0, 0, SW, SH, P_CYAN, P_CDIM);
    cv_blit(SX, SY);
}
void app_boot(void)
{
    uint32_t t0 = plat_ticks24(), t;
    if (!splash_on)
        return;
    while ((t = (plat_ticks24() - t0) / (1000u * PLAT_TICKS_PER_US)) < SPLASH_SCOPE_MS) {
        uint32_t f = plat_ticks24();
        boot_scope((float)t / (float)SPLASH_SCOPE_MS);
        while (plat_ticks24() - f < 16667u * PLAT_TICKS_PER_US)   /* ~60 frames/s, as the emulator's */
            ;
    }
    splash_on = 0;
}

/* the emulator: the platform shows no splash and its boot cannot wait, so the splash and its scope run here, a frame
 * at a time, before the panel */
#ifdef PLAT_WEB
static uint32_t web_splash_at;               /* plat_ms() when it went up; 0: done */
#endif

void app_ui_init(void)
{
    touch_on(1);                             /* velocity and pressure: the keys' readings on */
    lcd_fill(0, 0, 240, 240, P_BG);
    ui.redraw = 1;
#ifdef PLAT_WEB
    app_splash();
    web_splash_at = plat_ms() | 1u;
#endif
}

void app_frame(void)
{
    static uint32_t last_pct, lit_until, last_ble, last_midi, last_live;
    static int32_t last_note = -1;
    uint32_t i, now = plat_ms(), pct = plat_master() * 100u / 4096u;
#ifdef PLAT_WEB
    if (web_splash_at) {                     /* the splash, then the panel */
        if (now - web_splash_at < SPLASH_SCOPE_MS) {
            boot_scope((float)(now - web_splash_at) / (float)SPLASH_SCOPE_MS);
            return;
        }
        web_splash_at = 0;
        splash_on = 0;
        lcd_fill(0, 0, 240, 240, P_BG);
        ui.redraw = 1;
    }
#endif
    input();
    speak();
    if (voice_note != last_note) {           /* the note in the header: the arp's and the sequencer's change alone */
        last_note = voice_note;
        ui.redraw = 1;
    }
    for (i = 0; i < NE; i++)                 /* an encoder's light goes out */
        if (ui.turned_at[i] && now - ui.turned_at[i] < 400u && now > lit_until)
            lit_until = ui.turned_at[i] + 300u;
    if (lit_until && now >= lit_until) {
        lit_until = 0;
        ui.redraw = 1;
    }
    if (FM1B_BLE_CENTRAL) {                  /* looking: a device found is named at once (REC connects to it) */
        static int found;
        int n = ui.looking ? btdev_count() : 0, ev;
        btdev_page(ui.looking && !set.ble_off);
        if (n > found && btdev_pick() < n)
            ble_device_say();
        found = n;
        while ((ev = btdev_event()) != BTDEV_NONE)   /* a device connected, given up or lost */
            if (ev == BTDEV_CONNECTED || ev == BTDEV_NO_ANSWER || ev == BTDEV_LOST)
                app_say("DEVICE", btdev_word(ev));
    }
    if (midi_count != last_midi) {
        last_midi = midi_count;
        ui.redraw = 1;
    }
    if (pct != last_pct || (uint32_t)(plat_ble_connected() | plat_ble_device() << 1) != last_ble ||
        (ui.msg_until && now >= ui.msg_until)) {
        last_pct = pct;
        last_ble = (uint32_t)(plat_ble_connected() | plat_ble_device() << 1);
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (cpu_meter(now) && ui.page >= PG_GMIDI && !ui.redraw)   /* (the header alone) */
        head_draw();
    i = live();
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    } else if (i != last_live) {             /* the picture moved: its panel alone */
        panel_draw();
    }
    last_live = i;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "wave"))
        *got = patch.wave1;
    else if (!strcmp(what, "note"))
        *got = voice_note;
    else if (!strcmp(what, "filter"))
        *got = patch.filter;
    else if (!strcmp(what, "cutoff"))
        *got = patch.cutoff;
    else if (!strcmp(what, "reso"))
        *got = patch.reso;
    else if (!strcmp(what, "drive"))
        *got = patch.drive;
    else if (!strcmp(what, "coarse2"))
        *got = patch.coarse2;
    else if (!strcmp(what, "fine2"))
        *got = patch.fine2;
    else if (!strcmp(what, "sub"))
        *got = patch.sub_oct;
    else if (!strcmp(what, "sync"))
        *got = patch.sync;
    else if (!strcmp(what, "ring"))
        *got = patch.ring;
    else if (!strcmp(what, "wave2"))
        *got = patch.wave2;
    else if (!strcmp(what, "noise"))
        *got = patch.noise;
    else if (!strcmp(what, "glide"))
        *got = patch.glide;
    else if (!strcmp(what, "pw1"))
        *got = patch.pw1;
    else if (!strcmp(what, "pw2"))
        *got = patch.pw2;
    else if (!strcmp(what, "rhythm"))
        *got = patch.rhythm;
    else if (!strcmp(what, "swing"))
        *got = patch.swing;
    else if (!strcmp(what, "lvl1"))
        *got = patch.lvl1;
    else if (!strcmp(what, "lvl2"))
        *got = patch.lvl2;
    else if (!strcmp(what, "lvl_sub"))
        *got = patch.lvl_sub;
    else if (!strcmp(what, "sub_wave"))
        *got = patch.sub_wave;
    else if (!strcmp(what, "range1"))
        *got = patch.range1;
    else if (!strcmp(what, "range2"))
        *got = patch.range2;
    else if (!strcmp(what, "amp_a"))
        *got = patch.amp_a;
    else if (!strcmp(what, "amp_d"))
        *got = patch.amp_d;
    else if (!strcmp(what, "amp_s"))
        *got = patch.amp_s;
    else if (!strcmp(what, "amp_r"))
        *got = patch.amp_r;
    else if (!strcmp(what, "dist"))
        *got = patch.dist;
    else if (!strcmp(what, "pitch_now"))
        *got = pitch_now;
    else if (!strcmp(what, "arp"))
        *got = patch.arp_on;
    else if (!strcmp(what, "arp_mode"))
        *got = patch.arp_mode;
    else if (!strcmp(what, "seq_run"))      /* the sequencer playing (or PLAY's start, before its first 16th) */
        *got = seq_playing();
    else if (!strcmp(what, "seq_len"))
        *got = patch.sq.len;
    else if (!strncmp(what, "leg", 3) && what[3] >= '0' && what[3] <= '9')   /* legN: step N's legato */
        *got = seq_leg(&patch.sq, (uint32_t)atoi(what + 3) % NSTEPS);
    else if (!strncmp(what, "mem", 3) && what[3] >= '1' && what[3] <= '4')   /* memN: sequence N's length */
        *got = set.mem[what[3] - '1'].len;
    else if (!strcmp(what, "rec"))
        *got = seq_rec;
    else if (!strcmp(what, "step"))
        *got = ui.step;
    else if (!strcmp(what, "held"))
        *got = eng.nheld;
    else if (!strncmp(what, "seq", 3) && what[3] >= '0' && what[3] <= '9')   /* seqN: step N (0..31) */
        *got = patch.sq.step[atoi(what + 3) % NSTEPS];
    else if (!strcmp(what, "env"))           /* the envelopes' stages, ENV_* */
        *got = voice.amp.stage;
    else if (!strcmp(what, "menv"))
        *got = voice.menv.stage;
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "slot"))
        *got = set.slot;
    else if (!strcmp(what, "chan"))          /* the GLO pages' settings */
        *got = set.chan;
    else if (!strcmp(what, "local"))
        *got = set.local;
    else if (!strcmp(what, "cc_chan"))
        *got = set.cc_chan;
    else if (!strcmp(what, "at_out"))
        *got = set.at_out;
    else if (!strcmp(what, "clk_out"))
        *got = set.clk_out;
    else if (!strcmp(what, "tune"))
        *got = set.tune;
    else if (!strcmp(what, "clock"))
        *got = set.clock;
    else if (!strcmp(what, "vcurve"))
        *got = set.vcurve;
    else if (!strcmp(what, "ble_off"))
        *got = set.ble_off;
    else if (!strcmp(what, "afx"))
        *got = afx_on;
    else if (!strcmp(what, "tempo"))         /* the patch's TEMPO */
        *got = patch.tempo;
    else if (!strcmp(what, "lit"))           /* the keys lit for the notes held (bit 0: F3 at octave 0) */
        *got = (int)ui.lit;
    else if (!strcmp(what, "seed_hi"))       /* the seed shown, in halves (RND_EMPTY: 65535 65535) */
        *got = (int)(rnd_cur >> 16);
    else if (!strcmp(what, "seed_lo"))
        *got = (int)(rnd_cur & 0xFFFFu);
    else if (!strcmp(what, "seed_slot"))
        *got = rnd_slot;
    else if (!strcmp(what, "seed_src"))      /* RS_* */
        *got = rnd_src;
    else if (!strcmp(what, "rpos"))          /* the arp's or sequencer's steps since it started */
        *got = (int)eng.rpos;
    else if (!strcmp(what, "fc_now"))
        *got = fc_now;
    else if (!strcmp(what, "drift1") || !strcmp(what, "drift2"))
        *got = (int32_t)(1000.0f * voice.drift[what[5] - '1']);
    else if (!strcmp(what, "drift_gap"))
        *got = (int32_t)(1000.0f * fm_fabsf(voice.drift[0] - voice.drift[1]));
    else if (!strcmp(what, "cpu_peak"))
        *got = (int32_t)cpu_peak;
    else if (!strcmp(what, "coarse1"))
        *got = patch.coarse1;
    else if (!strcmp(what, "fine1"))
        *got = patch.fine1;
    else if (!strcmp(what, "menv_pw1"))
        *got = patch.menv_pw1;
    else if (!strcmp(what, "menv_pw2"))
        *got = patch.menv_pw2;
    else if (!strcmp(what, "vel_amp"))
        *got = patch.vel_amp;
    else if (!strcmp(what, "vel_menv"))
        *got = patch.vel_menv;
    else if (!strcmp(what, "key_trk"))
        *got = patch.key_trk;
    else if (!strcmp(what, "menv_gain"))
        *got = (int32_t)(1000.0f * voice.vm);
    else if (!strcmp(what, "pw1_now"))
        *got = pw1_now;
    else if (!strcmp(what, "kept_tempo"))   /* the slot's saved patch's tempo (the store's) */
        *got = bank.p[set.slot].tempo;
    else if (!strcmp(what, "hushed"))
        *got = hushed;
    else if (!strcmp(what, "amp_lvl"))
        *got = (int32_t)(1000.0f * voice.amp.lvl);
    else if (!strcmp(what, "heat_now"))
        *got = heat_now;
    else if (!strcmp(what, "trim_now"))
        *got = trim_now;
    else if (!strcmp(what, "sag_now"))
        *got = sag_now;
    else if (!strcmp(what, "dc_now"))
        *got = dc_now;
    else if (!strcmp(what, "cut_now"))
        *got = cut_now;
    else if (!strcmp(what, "cents_abs"))
        *got = cents_now < 0 ? -cents_now : cents_now;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "vel"))           /* the last key's velocity and pressure */
        *got = ui.vel[ui.last_key];
    else if (!strcmp(what, "prs"))
        *got = ui.prs[ui.last_key];
    else if (!strncmp(what, "turns", 5))    /* turnsN: encoder N's count (plat.h EN_*) */
        *got = ui.turns[atoi(what + 5) % NE];
    else {                                   /* a control on this page, by its name on the screen */
        uint32_t k;
        for (k = 0; k < NE; k++)
            if (PARAMS[ui.page][k].name && !strcmp(what, PARAMS[ui.page][k].name)) {
                *got = param_get(&PARAMS[ui.page][k], page_rec(ui.page));
                return 1;
            }
        return 0;
    }
    return 1;
}

/* the host's older banks: a version V bank of the defaults, its patches PS bytes (version 3's on without the
 * sequence's gap, its tie on step 6 as the default line had it); OLD(slot, field) a field of one */
static uint32_t old_off(uint32_t off) { return off >= SEQ_GAP + SEQ_GAP_N ? off - SEQ_GAP_N : off; }
#define OLD(sl, f) r[offsetof(bank_t, p) + (sl) * ps + old_off(offsetof(patch_t, f))]
static uint8_t *bank_old(uint32_t v, uint32_t ps)
{
    static uint8_t r[sizeof(bank_t)];
    bank_t *h = (bank_t *)r;
    patch_t d;
    uint32_t i, j;
    patch_defaults(&d);
    h->magic = BANK_MAGIC;
    h->version = (uint16_t)v;
    h->nslots = NSLOTS;
    h->psize = (uint8_t)ps;
    for (i = 0; i < NSLOTS; i++) {
        for (j = 0; j < ps; j++)
            r[offsetof(bank_t, p) + i * ps + j] = ((const uint8_t *)&d)[j < SEQ_GAP ? j : j + SEQ_GAP_N];
        OLD(i, sq.step[5]) = V7_TIE;
    }
    return r;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    static uint8_t syx[32768];
    FILE *fp;
    size_t n;
    (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "sysex_in")) {          /* a .syx file: each frame, F0 and F7 off, as the platform hands it */
        size_t i, s0 = 0;
        int in = 0;
        if (!(fp = fopen(a, "rb")))
            return perror(a), 1;
        n = fread(syx, 1, sizeof syx, fp);
        fclose(fp);
        for (i = 0; i < n; i++)
            if (syx[i] == 0xF0u)
                s0 = i, in = 1;
            else if (syx[i] == 0xF7u && in) {
                in = 0;
                if (!app_sysex(syx + s0 + 1, (uint32_t)(i - s0 - 1u)))
                    printf("sysex_in %s: frame at %u not taken\n", a, (unsigned)s0);
            }
        return 1;
    }
    if (!strcmp(cmd, "bank_init")) {         /* every slot the INIT patch (the defaults: before the factory patches),
                                              * saved and playing: what the older tests start from */
        uint32_t i;
        for (i = 0; i < NSLOTS; i++)
            patch_defaults(&bank.p[i]);
        plat_store_save(OBJ_PATCHES, &bank, sizeof bank);
        patch = bank.p[set.slot];
        return 1;
    }
    if (!strcmp(cmd, "set_v2")) {            /* the settings as the firmware before the sequences saved them: 16 bytes,
                                              * the last padding (2 here, as one device had it) */
        uint8_t r[16];
        memcpy(r, &set, SET_V2_SIZE);
        r[15] = 2;
        plat_store_save(OBJ_SETTINGS, r, sizeof r);
        return 1;
    }
    if (!strncmp(cmd, "bank_v", 6) && cmd[6] >= '3' && cmd[6] <= '7' && !cmd[7]) {   /* an older bank saved: a reboot
                                                                                    * then reads it as one */
        static const uint8_t PS[8] = {0, 0, 0, BANK_V3_PSIZE, BANK_V4_PSIZE, BANK_V5_PSIZE, BANK_V6_PSIZE, BANK_V7_PSIZE};
        uint32_t v = (uint32_t)(cmd[6] - '0'), ps = PS[v];
        uint8_t *r = bank_old(v, ps);
        if (v == 3u) {                       /* (slot 1's mod envelope on the pitch at +40, slot 2's on oscillator 2 at
                                              * -20, its cutoff 33) */
            OLD(0, menv_cut) = DEST_PITCH;
            OLD(0, menv_o1) = 40;
            OLD(1, menv_cut) = DEST_OSC2;
            OLD(1, menv_o1) = (uint8_t)-20;
            OLD(1, cutoff) = 33;
        } else if (v == 4u) {                /* (slot 1's swing 70 and mod envelope on oscillator 2 at -30) */
            OLD(0, swing) = 70;
            OLD(0, menv_o2) = (uint8_t)-30;
        } else if (v == 5u) {                /* (slot 1's arp PLAYED and slot 2's RANDOM as version 5 had them, 3, 4) */
            OLD(0, arp_mode) = 3;
            OLD(1, arp_mode) = 4;
        } else if (v == 6u) {                /* (UPDOWN2 as version 6 had it: kept as it is; its patches untracked) */
            OLD(0, arp_mode) = ARP_UPDOWN2;
        } else {                             /* (slot 1's steps 1 and 2 ties: nothing before them to hold, rests;
                                              * its tracking kept) */
            OLD(0, sq.step[0]) = V7_TIE;
            OLD(0, sq.step[1]) = V7_TIE;
            OLD(0, key_trk) = 3;
        }
        plat_store_save(OBJ_PATCHES, r, offsetof(bank_t, p) + NSLOTS * ps);
        return 1;
    }
    if (!strcmp(cmd, "rnd_out")) {           /* the last seed frame sent */
        if (!(fp = fopen(a, "wb")))
            return perror(a), 1;
        fwrite(rnd_out, 1, sizeof rnd_out, fp);
        fclose(fp);
        return 1;
    }
    if (!strcmp(cmd, "sysex_bank_out")) {    /* the last bank sent (GLO DUMP's), into FILE: a .syx of 32 dumps */
        if (!(fp = fopen(a, "wb")))
            return perror(a), 1;
        fwrite(sx_bank, 1, sizeof sx_bank, fp);
        fclose(fp);
        return 1;
    }
    if (!strcmp(cmd, "sysex_out")) {         /* the last dump sent */
        if (!(fp = fopen(a, "wb")))
            return perror(a), 1;
        fwrite(sx_out, 1, sizeof sx_out, fp);
        fclose(fp);
        return 1;
    }
    return 0;
}
#endif
