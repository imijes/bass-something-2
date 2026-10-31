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
    uint8_t ble_off;                     /* Bluetooth switched off (GLO MIDI); 0, the default: on */
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
    uint8_t pad;                         /* (the record before knobs ended in a byte of padding: not read) */
    uint8_t knobs;                       /* KN_*: how far a knob goes as it turns faster (knob_accel) */
    uint8_t spare[3];                    /* (0: the record's end, no padding) */
} settings_t;
#define SET_V1_SIZE 8u                   /* the record before the GLO pages: magic to ble_off */
#define SET_V2_SIZE offsetof(settings_t, mem)   /* the record before the sequences (16 bytes stored: a byte of padding) */
#define SET_V3_SIZE offsetof(settings_t, knobs)  /* the record before the knobs' response */
enum { CH_OFF = 0, CH_ALL = 17 };
enum { CLK_AUTO, CLK_INT, CLK_EXT, NCLK };   /* MIDI clock followed when it comes; never; always (none: stopped) */
enum { VC_SOFT, VC_NORMAL, VC_HARD, NVC };
enum { KN_FINE, KN_NORMAL, KN_FAST, KN_COARSE, NKN };   /* the knobs' response: one step a click, to big strides */
static settings_t set;
static settings_t set_kept;              /* the record as it is in the store (glo_keep) */
#define GLO_FROM offsetof(settings_t, ble_off)   /* the GLO pages' settings: ble_off on; kept on their own */
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */

static const char *const WAVE_NAME[NWAVES] = {"SAW", "SQUARE", "SINE", "TRI", "PULSE"};
static const char *const DEST_NAME[NDEST] = {"CUTOFF", "PITCH", "OSC2"};
static const char *const LFO_NAME[NLFOW] = {"TRI", "SAW", "SQUARE", "S&H"};
static const char *const SYNC_NAME[NSYNCV] = {   /* the sync values as a Bass Station II's display shows them */
    "64b", "48b", "42b", "36b", "32b", "30b", "28b", "24b", "213", "20b", "183", "18b", "16b", "133", "12b", "102", "8b",
    "6b",  "5b3", "4b",  "3b",  "8x3", "2n",  "4d",  "4x3", "4n",  "8d",  "4t",  "8n",  "16d", "8t",  "16n", "16t",
    "32n", "32t"};
/* the controls by their names in plat.h, in its order */
static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+"};

/* ------------------------------------------------------------------ voice --- */
/* one voice, the last note wins; the main loop and MIDI in set the gate, the audio interrupt plays it */
static volatile int32_t voice_note = -1;     /* MIDI note held, -1 = released (the gate) */
static volatile float voice_vel = 1.0f;      /* the note's velocity, 0..1 */
static volatile uint32_t voice_trig;         /* note-ons: each one (re)starts the envelopes (as their Triggering is) */
static volatile uint8_t voice_legato;        /* the last note-on played legato: a key still held under it */
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
/* an envelope's settings: times in seconds, steps a second; its retriggering (loop, count) and fixed sustain (its
 * decay time then the sustain's) */
static void adsr_set(adsr_t *e, float a, float d, float s, float r, float rate, uint32_t loop, uint32_t count,
                     uint32_t fixed)
{
    e->fixed = fixed != 0u;
    e->loop = loop != 0u;
    e->count = (uint8_t)count;
    e->att = 1.0f - fm_expf(-ATT_K / (fm_maxf(a, 0.0005f) * rate));
    e->dec = env_coef(e->fixed ? 0.001f : fm_maxf(d, 0.001f), rate);
    e->sus = fm_clampf(s, 0.0f, 1.0f);
    e->rel = env_coef(fm_maxf(r, 0.001f), rate);
    e->hold = d * rate;
}
/* the faders' times, the Bass Station II's (its user guide: the attack at once at the bottom, about 250 ms at the
 * middle, over 5 s at the top; the decay about 150 ms at the middle; the release about 360 ms at the middle, over 10 s
 * at the top), each an exponential through them: lo + a (2^(b v) - 1). (The decay's top the guide does not give: its
 * curve the release's, 4.3 s) */
static float env_curve(uint32_t v, float lo, float a, float b) { return lo + a * (fm_exp2f((float)v * b) - 1.0f); }
static float att_time(uint32_t v) { return env_curve(v, 0.0005f, 0.011804f, 0.069819f); }   /* 0.5 ms .. 5.5 s */
static float dec_time(uint32_t v) { return env_curve(v, 0.002f, 0.005117f, 0.076614f); }    /* 2 ms .. 4.3 s */
static float rel_time(uint32_t v) { return env_curve(v, 0.002f, 0.012377f, 0.076614f); }    /* 2 ms .. 10.5 s */

/* a step of an envelope: each stage moves on from the level it is at, so a retrigger or an early release
 * does not jump (no click) */
static inline float adsr_step(env_t *v, const adsr_t *e)
{
    switch (v->stage) {
    case ENV_ATTACK:
        if ((v->lvl += (ATT_AIM - v->lvl) * e->att) >= 1.0f) {
            v->lvl = 1.0f;
            v->stage = ENV_DECAY;
            v->held = e->hold;
        }
        break;
    case ENV_DECAY:                          /* (fixed-duration: no decay, a millisecond's to the sustain) */
        v->lvl += (e->sus - v->lvl) * e->dec;
        if (v->lvl - e->sus < 1e-4f) {
            if (!e->fixed && e->loop && (!e->count || v->loops < e->count)) {   /* retriggering: again */
                v->loops++;
                v->stage = ENV_ATTACK;
            } else
                v->stage = ENV_SUSTAIN;
        }
        break;
    case ENV_SUSTAIN:
        v->lvl = e->sus;
        if (e->fixed && (v->held -= 1.0f) <= 0.0f)   /* fixed-duration: its time up, the release */
            v->stage = ENV_RELEASE;
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

/* an LFO's speed in Hz, the Bass Station II's: 8 bits, about 0.2 Hz at the bottom to about 190 at the top, 3 Hz at
 * 52 and 7.9 at 75 (its INIT PATCH's two, its user guide's); log2 of it a curve through those */
static float lfo_hz(uint32_t v)
{
    float x = (float)v;
    return fm_exp2f(-2.250353f + 0.0827845f * x - 0.000173623f * x * x);
}
/* a sync value's cycle in MIDI clock ticks (24 a quarter note): the Bass Station II's table, 64 beats .. a 32nd
 * triplet (its 42 beats 1008: the guide's 1002 a misprint, 2 cycles in 21 bars) */
static const uint16_t SYNC_TICKS[NSYNCV] = {1536, 1152, 1008, 864, 768, 720, 672, 576, 512, 480, 448, 432, 384, 320,
                                            288,  256,  192,  144, 128, 96,  72,  64,  48,  36,  32,  24,  18,  16,
                                            12,   9,    8,    6,   4,   3,   2};
/* an LFO's step of n samples (dt: the clock's ticks in them): -1..1. Free-running (keysync: from 0 at a note,
 * lfo_note); its speed the knob's, or the clock's (sync); S&H a new value at each wrap; the slew a one-pole on what
 * it gives (its edges rounded); the delay a fade-in from the note */
static float lfo_step(lfo_t *l, const lfo_patch_t *p, uint32_t i, uint32_t n, float dt, uint32_t slew, uint32_t delay,
                      uint32_t sync, uint32_t syncv)
{
    static uint32_t rng = 1u;
    float ph, w;
    l->ph += sync ? dt / (float)SYNC_TICKS[syncv < NSYNCV ? syncv : NSYNCV - 1u]
                  : lfo_hz(p->rate) * (float)n * (1.0f / BS2_RATE);
    if (l->ph >= 1.0f) {
        l->ph -= fm_floorf(l->ph);
        rng = rng * 1664525u + 1013904223u;
        l->hold = (float)(int32_t)rng * (1.0f / 2147483648.0f);
    }
    ph = l->ph;
    switch (p->wave) {
    case LFO_SAW:                            /* (falling, the Bass Station II's) */
        w = 1.0f - 2.0f * ph;
        break;
    case LFO_SQUARE:
        w = ph < 0.5f ? 1.0f : -1.0f;
        break;
    case LFO_SH:
        w = l->hold;
        break;
    default:                                 /* TRI */
        w = 1.0f - 4.0f * fm_fabsf(ph - 0.5f);
    }
    /* the slew: 0 none, else a time constant 0.5 ms .. 2 s (its scale the guide does not give) */
    l->out = slew ? l->out + (w - l->out) * (1.0f - fm_expf(-(float)n / (0.0005f * fm_exp2f((float)slew * (12.0f / 127.0f))
                                                                         * BS2_RATE)))
                  : w;
    /* the delay: a linear fade-in over the attack's time at its setting (the guide gives no scale) */
    l->fade = delay ? fm_minf(1.0f, l->fade + (float)n / (att_time(delay) * BS2_RATE)) : 1.0f;
    (void)i;
    return l->out * l->fade;
}
static void lfo_note(lfo_t *l, uint32_t keysync, uint32_t delay)   /* a note (not legato) starts: keysync, delay */
{
    if (keysync)
        l->ph = 0.0f;
    if (delay)
        l->fade = 0.0f;
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
    float g[4];                              /* each stage's g (tan's, pre-warped): Osc Filter Mod's to scale */
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
        c->g[i] = g;
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

/* Osc Filter Mod, the Bass Station II's: the cutoff moved at audio rate by oscillator 2, each stage's g scaled by m (2
 * to the octaves it moves) and the loop's products made again; the rest as the step's (c) */
static inline void fcoef_fm(fcoef_t *d, const fcoef_t *c, float m)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        float g = fm_minf(c->g[i] * m, 6.31f);   /* (held under 0.45 of the rate: tan's there) */
        d->G[i] = g / (1.0f + g);
        d->om[i] = 1.0f - d->G[i];
    }
    d->c2 = d->G[3] * d->G[2];
    d->c1 = d->c2 * d->G[1];
    d->P = d->c1 * d->G[0];
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
    uint32_t lfo_ticks;                      /* MIDI clocks not yet counted by the LFOs (their sync) */
    float clk_out;                           /* clock ticks toward the next one out (GLO SYS's SEND CLK) */
    uint32_t apos, spos;                     /* the arp's notes played; the sequencer's next step */
    uint32_t scur;                           /* the sequencer's step sounding (NSTEPS: none yet) */
    uint32_t rpos;                           /* the 16ths since the start: the rhythm's place, and swing's pair */
    uint8_t sounding, tie;                   /* a note is on; the rhythm (or a legato step) holds it on */
    uint8_t leg;                             /* the sequencer's step sounding is legato: held into the next note */
    int32_t transpose;                       /* the sequencer: the last key down from its first note (0: as recorded) */
} eng = {.vel = 100, .ext_age = EXT_HOLD};

static int ext_clock(void)                   /* the arp and the sequencer follow MIDI clock (GLO SYS's SYNC) */
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
            /* (another key held: legato; but not over a note-on the render has not taken yet, a chord struck in
             * one block: that one's start stands, or no envelope would start at all) */
            voice_legato = voice_note >= 0 && eng.ndown > 1u && (voice_trig == voice.trig || voice_legato);
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
    voice_legato = 0;                        /* (the arp's and the sequencer's notes: each its own) */
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
                eng.lfo_ticks++;
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

/* the modulation's step: the LFOs on their destinations (each -1..1 at full), each LFO's value in lv; the mod
 * envelope's level returned, a 63rd (its depths are its own: the caller's) */
/* the clock's ticks in n samples, for the LFOs' sync: MIDI clock's when it is followed, else the tempo's */
static float lfo_ticks(uint32_t n)
{
    uint32_t t = eng.lfo_ticks;
    eng.lfo_ticks = 0;
    return ext_clock() ? (float)t : (float)patch.tempo * (24.0f / 60.0f / BS2_RATE) * (float)n;
}
static float mod_step(const adsr_t *menv, uint32_t n, float *mod, float *lv)
{
    uint32_t j;
    float dt = lfo_ticks(n);
    for (j = 0; j < NLFO; j++)
        lv[j] = lfo_step(&lfo[j], &patch.lfo[j], j, n, dt, patch.lfo_slew[j], patch.lfo_delay[j], patch.lfo_sync[j],
                         patch.lfo_syncv[j]);
    mod[DEST_PITCH] = lv[0] * (float)patch.lfo[0].depth[0] * (1.0f / 63.0f);   /* LFO 1 the pitches, 2 the cutoff */
    mod[DEST_OSC2] = lv[0] * (float)patch.lfo[0].depth[1] * (1.0f / 63.0f);
    mod[DEST_CUTOFF] = lv[1] * (float)patch.lfo[1].depth[0] * (1.0f / 63.0f);
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
    float ofd = (float)patch.ofm * (3.0f / 127.0f);   /* Osc Filter Mod's octaves at oscillator 2's top */
    uint32_t subw = SUB_WAVE[patch.sub_wave < NSUBW ? patch.sub_wave : SUB_SQUARE];
    /* Ext: the computer's USB audio, its two sides as one, at its level into the mixer: through the filter, the amp
     * (so a note opens it, as the Bass Station II's Ext In) and the distortion; at 0 it plays apart, after the synth */
    const int32_t *ext = plat_usb_audio_in();
    float le = (float)patch.lvl_ext * (0.5f / 127.0f / 8388608.0f);
    adsr_t menv, aenv;
    fcoef_t fc, fm;
#ifdef PLAT_HOST
    float dsum = 0.0f, dabs = 0.0f;
#endif
    plat_usb_audio_route(patch.lvl_ext != 0u);   /* (from the next render) */
    midi_in();
    if (!afx_on)
        ax = (afx_t){-1, 0, 0, 0};
    else if (voice_note >= 0)
        ax = AFX[(uint32_t)(((voice_note - 53 - 12 * set.octave) % NKEYS + NKEYS) % NKEYS)];
    wave = ax.wave >= 0 ? (uint32_t)ax.wave : patch.wave1;
    cut = clamp((int32_t)patch.cutoff + ax.cut, 0, 127);
    noise = (float)clamp((int32_t)patch.noise + ax.noise, 0, 127) * (0.7f / 127.0f);
    adsr_set(&menv, att_time(patch.menv_a), dec_time((uint32_t)clamp((int32_t)patch.menv_d + ax.dec, 0, 127)),
             (float)patch.menv_s / 127.0f, rel_time(patch.menv_r), CTRL_RATE, patch.env_loop[1], patch.env_count[1],
             patch.env_fixed[1]);
    adsr_set(&aenv, att_time(patch.amp_a), dec_time((uint32_t)clamp((int32_t)patch.amp_d + ax.dec, 0, 127)),
             (float)patch.amp_s / 127.0f, rel_time(patch.amp_r), BS2_RATE, patch.env_loop[0], patch.env_count[0],
             patch.env_fixed[0]);
    for (i = 0; i < 4u; i++)                 /* (a silent filter's states would sink into denormals) */
        voice.flt.s[i] = fm_flush(voice.flt.s[i]);
    voice.flt.hs = fm_flush(voice.flt.hs);
    voice.dc_y = fm_flush(voice.dc_y);
    for (i = 0; i < n; i += m) {             /* a modulation step, then its samples */
        float inc = 0.0f, inc2 = 0.0f, inv, inv2, incs, invs, e, pw1, pw2, kt;
        m = n - i < CTRL_N ? n - i : CTRL_N;
        eng_run(m);
        note = voice_note;                   /* the gate, read once a step */
        if (voice_trig != voice.trig) {      /* a note-on: each envelope attacks from where it is, as its Triggering
                                              * has it (Multi: always; Single, Autoglide: not legato) */
            uint32_t leg = voice_legato;
            voice.trig = voice_trig;
            voice.vel = vel_gain(voice_vel, patch.vel_amp);
            voice.vm = vel_gain(voice_vel, patch.vel_menv);
            if (voice.amp.stage == ENV_IDLE) /* (from silence: at once, the attack as it was) */
                vel_now = voice.vel;
            if (!leg || patch.env_trig[0] == ENVT_MULTI) {
                voice.amp.stage = ENV_ATTACK;
                voice.amp.loops = 0;
            }
            if (!leg || patch.env_trig[1] == ENVT_MULTI) {
                voice.menv.stage = ENV_ATTACK;
                voice.menv.loops = 0;
            }
            if (!leg)                        /* the LFOs' keysync and delay: a note on its own */
                for (j = 0; j < NLFO; j++)
                    lfo_note(&lfo[j], patch.lfo_keysync[j], patch.lfo_delay[j]);
            /* Autoglide (either envelope's): the glide on legato notes alone, a note on its own at once */
            if (!leg && (patch.env_trig[0] == ENVT_AUTOGLIDE || patch.env_trig[1] == ENVT_AUTOGLIDE) && voice_note >= 0)
                voice.pitch = (float)voice_note;
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
        if (patch.ofm)
            fm = fc;                         /* (Osc Filter Mod's: the step's, its stages each sample's) */
        if (voice.note >= 0) {               /* (held under Nyquist: PolyBLEP wants a step under half a turn) */
            float p1 = voice.pitch - 69.0f + (float)set.tune * 0.01f;
            voice.pitch += ((float)voice.note - voice.pitch) * gl;
            inc = fm_minf(440.0f * fm_exp2f((p1 + voice.drift[0] + r1 + (float)patch.coarse1 + (float)patch.fine1 * 0.01f
                                            + 12.0f * (mod[DEST_PITCH] + e * (float)patch.menv_o1)) / 12.0f) / BS2_RATE,
                          0.4f);
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
             * pulse a quarter), the noise and Ext (full scale as loud as an oscillator); 1 and 2 a sample late */
            s = 0.4f * ((1.0f - ring) * (l1 * voice.o1 + l2 * voice.o2) + 2.0f * ring * voice.o1 * voice.o2
                        + ls * osc(voice.phase_sub, incs, invs, subw, 0.25f)
                        + noise * (float)(int32_t)rng * (1.0f / 2147483648.0f)
                        + (ext ? le * (float)(ext[2u * (i + j)] + ext[2u * (i + j) + 1u]) : 0.0f));
            voice.o1 = o1;
            voice.o2 = o2;
            if (patch.ofm) {                 /* Osc Filter Mod: oscillator 2 (as it is, before its level) on the cutoff,
                                              * up to 3 octaves either way (the guide gives no scale) */
                fcoef_fm(&fm, &fc, fm_exp2f(ofd * o2));
                s = ladder(&voice.flt, &fm, s);
            } else
                s = ladder(&voice.flt, &fc, s);
            /* the output's coupling (a one-pole high pass at 5 Hz): what DC the filter passes (the ring's, a tanh's
             * on an uneven wave) settles while the voice runs between notes, so the amplifier's gate is no step */
            voice.dc_y = s - voice.dc_x + DC_POLE * voice.dc_y;
            voice.dc_x = s;
            s = voice.dc_y;
            /* the distortion before the VCA, the Bass Station II's: its character the same as the envelope fades */
            if (patch.dist)
                s = overdrive(&voice.dist, s * dg) * dm;
            else                             /* (off: from 0 when it comes on, no step) */
                voice.dist.x1 = voice.dist.F1 = 0.0f;
            s *= adsr_step(&voice.amp, &aenv) * vel_now;
            voice.sag_f += (fm_fabsf(s) - voice.sag_f) * (fm_fabsf(s) > voice.sag_f ? 0.01f : 0.0003f);
            voice.sag_g += (sg - voice.sag_g) * 0.02f;
            s *= voice.sag_g;
            if (patch.lim) {                 /* the VCA limiter: over its threshold (0 to -24 dB under full), the level
                                              * held to it; the level followed at once up, over about 50 ms down */
                float a = fm_fabsf(s), th = fm_exp2f(-(float)patch.lim * (4.0f / 127.0f));
                voice.lim_e = a > voice.lim_e ? a : voice.lim_e * 0.99955f;
                if (voice.lim_e > th)
                    s *= th / voice.lim_e;
            }
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
        .lfo = {{.wave = LFO_TRI, .rate = 75}, {.wave = LFO_TRI, .rate = 52}},   /* (the INIT PATCH's: 7.9 Hz, 3 Hz) */
        .lfo_keysync = {0, 1},
        .arp_on = 0, .arp_mode = ARP_UP, .arp_oct = 1, .latch = 0, .gate = 50, .tempo = 120,
        .sq = SEQ_LINE,
        .menv_o2 = 0, .pw1 = 50, .pw2 = 50, .pwm1 = 0, .pwm2 = 0, .noise = 0, .glide = 0, .rhythm = NRHY,
        .swing = 50,
        .lvl1 = 127, .lvl2 = 127, .lvl_sub = 89,   /* (the sub at 0.7: version 4's mix) */
        .sub_wave = SUB_SQUARE, .range1 = RANGE_8, .range2 = RANGE_8,
        .amp_a = 14, .amp_d = 75, .amp_s = 89, .amp_r = 64,   /* version 4's: 12 ms, 270 ms to 70 %, 360 ms */
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
    s->knobs = KN_NORMAL;
    s->pad = s->spare[0] = s->spare[1] = s->spare[2] = 0;
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
#define BANK_VERSION 13u                 /* patch_t's layout: one more at each change (2: sync, ring; 3: arp, seq;
                                          * 4: the mod envelope's three depths, PW, noise, glide, rhythm, swing;
                                          * 5: the levels, the sub's wave, the ranges, the amp envelope, distortion;
                                          * 6: the arp's retrig, and its UPDOWN2 before PLAYED;
                                          * 7: oscillator 1's tune, the mod envelope on the widths, the velocities,
                                          * the key tracking; 8: the sequence's 32 steps and their legato;
                                          * 9: the external input's level; 10: the LFOs' depths, the Bass Station
                                          * II's places, for a destination and an amount; 11: the envelopes'
                                          * triggering, retriggering and fixed sustain; 12: the LFOs' delay, slew,
                                          * keysync and sync, their speed 8 bits, no sine; 13: Osc Filter Mod,
                                          * the VCA limiter) */
#define BANK_V1_PSIZE 24u                /* version 1's patch_t: version 2's up to the ring (sync its spare, 0) */
#define BANK_V2_PSIZE 25u                /* version 2's: version 3's up to the arp */
#define BANK_V3_PSIZE 49u                /* version 3's: version 4's up to menv_o2 (its menv_cut, menv_o1 a DEST_*
                                          * and an amount) */
#define BANK_V4_PSIZE 58u                /* version 4's: version 5's up to the levels */
#define BANK_V5_PSIZE 69u                /* version 5's: version 6's up to the arp's retrig */
#define BANK_V6_PSIZE 70u                /* version 6's: version 7's up to oscillator 1's tune */
#define BANK_V7_PSIZE 77u                /* version 7's: version 8's but the sequence's steps 17..32 and legato */
#define BANK_V8_PSIZE (BANK_V7_PSIZE + SEQ_GAP_N)   /* version 8's: version 9's up to the external input's level */
#define BANK_V9_PSIZE (BANK_V8_PSIZE + 1u)   /* version 9's: version 10's, the LFOs a destination and an amount */
#define BANK_V10_PSIZE BANK_V9_PSIZE    /* version 10's: version 11's up to the envelopes' triggering */
#define BANK_V11_PSIZE (BANK_V10_PSIZE + 8u)   /* version 11's: version 12's up to the LFOs' delay */
#define BANK_V12_PSIZE (BANK_V11_PSIZE + 10u)  /* version 12's: version 13's up to Osc Filter Mod */
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
_Static_assert(offsetof(patch_t, lvl_ext) == BANK_V8_PSIZE, "version 8's: 9's without lvl_ext");
_Static_assert(offsetof(patch_t, env_trig) == BANK_V10_PSIZE, "version 10's: 11's without the envelopes' modes");
_Static_assert(offsetof(patch_t, lfo_delay) == BANK_V11_PSIZE, "version 11's: 12's without the LFOs' delay on");
_Static_assert(offsetof(patch_t, ofm) == BANK_V12_PSIZE, "version 12's: 13's without Osc Filter Mod on");
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "the settings fit a store object");
_Static_assert(sizeof(settings_t) == SET_V3_SIZE + 4u, "the settings end with no padding");
static bank_t bank;
static void patch_sane(patch_t *p);          /* (the UI's: by its controls' ranges) */
static void bank_factory(void);              /* (the factory patches: after the dumps' reader) */

static void bank_load(void)
{
    uint32_t i, ps, v;
    int n = plat_store_load(OBJ_PATCHES, &bank, sizeof bank);
    v = bank.version;
    ps = v == 1u ? BANK_V1_PSIZE : v == 2u ? BANK_V2_PSIZE : v == 3u ? BANK_V3_PSIZE : v == 4u ? BANK_V4_PSIZE
       : v == 5u ? BANK_V5_PSIZE : v == 6u ? BANK_V6_PSIZE : v == 7u ? BANK_V7_PSIZE : v == 8u ? BANK_V8_PSIZE
       : v == 9u ? BANK_V9_PSIZE : v == 10u ? BANK_V10_PSIZE : v == 11u ? BANK_V11_PSIZE : v == 12u ? BANK_V12_PSIZE : 0u;
    if (ps && n == (int)(offsetof(bank_t, p) + NSLOTS * ps) && bank.magic == BANK_MAGIC && bank.nslots == NSLOTS
        && bank.psize == ps) {               /* an older version: each patch moved up, what it lacks filled in */
        uint8_t *b = (uint8_t *)bank.p;
        uint32_t gap = ps >= SEQ_GAP && v <= 7u ? SEQ_GAP_N : 0u, j;   /* (versions 3 to 7: the sequence's 16 steps,
                                                                    * then the gap) */
        for (i = NSLOTS * ps; i-- > 0u;)     /* (the last byte first: none is overwritten unread) */
            b[i / ps * sizeof(patch_t) + i % ps + (i % ps >= SEQ_GAP ? gap : 0u)] = b[i];
        for (i = 0; i < NSLOTS; i++) {
            patch_t *q = &bank.p[i];
            int8_t a = q->menv_o1;           /* the mod envelope's one destination to its three depths */
            uint32_t d = (uint8_t)q->menv_cut;
            patch_fill(q, ps + gap);
            for (j = 0; v <= 11u && j < NLFO; j++) {   /* (version 11's back:) the waves without the sine (as the triangle), the
                                              * speed's 7 bits to 8 */
                static const uint8_t W[5] = {LFO_TRI, LFO_TRI, LFO_SAW, LFO_SQUARE, LFO_SH};
                q->lfo[j].wave = W[q->lfo[j].wave < 5u ? q->lfo[j].wave : 0u];
                q->lfo[j].rate = (uint8_t)(q->lfo[j].rate * 2u);
            }
            for (j = 0; v <= 9u && j < NLFO; j++) {   /* (version 9's on back:) a destination and an amount to the
                                              * Bass Station II's depths: LFO 1 on
                                              * the pitch both oscillators', on oscillator 2 its; LFO 2 on the cutoff
                                              * the filter's (LFO 1 on the cutoff, LFO 2 on a pitch: none) */
                uint32_t to = (uint8_t)q->lfo[j].depth[0];
                int8_t amt = q->lfo[j].depth[1];
                q->lfo[j].depth[0] = (int8_t)(j ? (to == DEST_CUTOFF ? amt : 0) : (to == DEST_PITCH ? amt : 0));
                q->lfo[j].depth[1] = (int8_t)(!j && (to == DEST_PITCH || to == DEST_OSC2) ? amt : 0);
            }
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
    p[4].lfo[1].depth[0] = 30;
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
static void ui_start(void);
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
        for (i = (uint32_t)n < SET_V3_SIZE && (uint32_t)n > SET_V2_SIZE ? SET_V2_SIZE : (uint32_t)n; i < sizeof set; i++)
            ((uint8_t *)&set)[i] = ((const uint8_t *)&d)[i];
    set_sane();
    tol_init();
    set_kept = set;
    bank_load();
    rnd_load();
    if (BS2_TEST_PATCHES)
        test_patches();
    patch = bank.p[set.slot];
    ui_start();
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
    int8_t pdir;                         /* the MASTER's Patch buttons: the way PRESET last went, -1 down, +1 up */
    const void *org;                     /* (a param_t) the sound's control last changed (0: none since the patch came): the
                                          * MASTER's Value arrows, the way back to its saved value */
    uint32_t org_at;                     /* when it changed (0: its value no longer on the MASTER's display) */
    uint8_t page;                        /* PG_*: what the encoders set */
    uint8_t step;                        /* the sequencer's cursor: the step PRESET picks, recording writes */
    uint8_t rec_n;                       /* the steps recorded since REC (0: the next note starts a new sequence) */
    uint8_t mode;                        /* the arp's mode as last seen: REC's start */
    uint8_t redraw;
    uint8_t mix_src;                     /* MIX FX's switch: what its K4 sets (MS_*) */
    uint8_t osc_sel;                     /* the OSCILLATORS' Osc Select: 0 oscillator 1, 1 oscillator 2 (SEL) */
    uint8_t pw_src;                      /* their pulse width's switch: what its knob sets (PW_*) */
    uint8_t flt_classic;                 /* the FILTERS' Classic setting while Acid is on (FLT_*), for its way back */
    uint8_t lfo_delay;                   /* the LFOS' Delay/Speed switch (ALGO) at Delay: their knobs the delays */
    uint8_t lfo_sel;                     /* the LFO mods' LFO (SELECT): 0 LFO 1, 1 LFO 2 */
    uint8_t env_sel;                     /* the ENVELOPES' Env Select (SELECT): what the faders set, 0 the amp envelope,
                                          * 1 the mod envelope, 2 both (Amp+Mod) */
    uint32_t lit;                        /* the keys whose notes are held (notes_lit): lit, on the keys and on screen */
} ui;
#define ORG_SHOW_MS 1500u                    /* a control's value on the MASTER's display, after its change */
static void org_set(const void *q)           /* a control of the sound changed: the Org. Value arrows', the display's */
{
    ui.org = q;
    ui.org_at = plat_ms() | 1u;
}

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
enum { PG_OSC, PG_MIX, PG_MENV, PG_LFO1, PG_LFO2, PG_PATCH, PG_ARP, PG_SEQ, PG_AENV, PG_FX, PG_TUNE, PG_FMOD, PG_EMOD, PG_EFX, PG_XMOD, PG_AMOD, PG_GMIDI, PG_GSYS, PG_GDUMP,
       PG_GRND, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"OSCILLATORS", "PULSE WIDTH", "TRIGGERING", "LFOS", "LFO MODS", "PATCH", "ARP",
                                              "SEQ", "ENVELOPES", "MIX FX", "FILTERS", "FILTERS LFO 2", "ENV MODS", "EFFECTS", "FX MODS", "ARP MODS", "GLOBAL MIDI", "GLOBAL SYSTEM",
                                              "GLOBAL DUMP", "GLOBAL RND"};
static const char *const SUB_NAME[NSUBW] = {"SINE", "PULSE", "SQUARE"};
static const char *const SUB_OCT_NAME[3] = {"", "-1", "-2"};   /* (the sub's octave is 1 or 2) */
static const char *const TRIG_NAME[NENVT] = {"MULTI", "SINGLE", "AUTOGLIDE"};
static const char *const SPD_SNC[2] = {"SPEED", "SYNC"};
static const char *const COUNT_NAME[17] = {"ENDLESS", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
                                           "13", "14", "15", "16"};
static const char *const RANGE_NAME[NRANGE] = {"16'", "8'", "4'", "2'"};
static const char *const OFF_ON[2] = {"OFF", "ON"};
static const char *const ON_OFF[2] = {"ON", "OFF"};   /* (ble_off) */
static const char *const CHAN_NAME[CH_ALL + 1] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11",
                                                  "12", "13", "14", "15", "16", "ALL"};
static const char *const CLK_NAME[NCLK] = {"AUTO", "INT", "EXT"};
static const char *const VC_NAME[NVC] = {"SOFT", "NORMAL", "HARD"};
static const char *const KN_NAME[NKN] = {"FINE", "NORMAL", "FAST", "COARSE"};
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
static const param_t PARAMS[NPAGES][NE] = {
    /* the Bass Station II's OSCILLATORS, oscillator 1's (SEL its Osc Select: 2's, OSC2_P): the pitch's view, then the
     * pulse width's (its switch, ui.pw_src, and the knob it sets: PW_P) */
    [PG_OSC] = {[EN_SELECT] = {"RANGE", PF(range1), 0, NRANGE - 1, RANGE_NAME},
                [EN_ALGO] = {"WAVE", PF(wave1), 0, NWAVES - 1, WAVE_NAME}, [EN_PRESET] = {"SYNC", PF(sync), 0, 1, OFF_ON},
                [EN_K1] = {"COARSE", PF(coarse1), -12, 12, 0}, [EN_K2] = {"FINE", PF(fine1), -100, 100, 0},
                [EN_K3] = {"MOD ENV", PF(menv_o1), -63, 63, 0}, [EN_K4] = {"LFO 1", PF(lfo[0].depth[0]), -63, 63, 0}},
    [PG_MIX] = {[EN_K4] = {"PW", PF(pw1), 5, 95, 0}},
    /* the Bass Station II's ENVELOPES: PG_AENV its panel (Env Select, ui.env_sel: the faders, K1 to K4, the amp
     * envelope's or MENV_P's), PG_MENV its Triggering (Env Select again, the switch on ALGO: the amp envelope's or
     * TRIGM_P's) */
    [PG_MENV] = {[EN_ALGO] = {"TRIGGER", PF(env_trig[0]), 0, NENVT - 1, TRIG_NAME}},
    /* the Bass Station II's PORTA and LFOS, left to right: the Glide Time (SELECT); LFO 1's wave button (K1) and knob
     * (K2), LFO 2's knob (K3) and wave button (K4); each knob its speed, or its delay at the Delay/Speed switch's Delay
     * (ALGO: ui.lfo_delay), its sync value at Speed/Sync's Sync (knob_param) */
    [PG_LFO1] = {[EN_SELECT] = {"GLIDE", PF(glide), 0, 127, 0},
                 [EN_K1] = {"WAVE 1", PF(lfo[0].wave), 0, NLFOW - 1, LFO_NAME},
                 [EN_K2] = {"SPEED 1", PF(lfo[0].rate), 0, 255, 0}, [EN_K3] = {"SPEED 2", PF(lfo[1].rate), 0, 255, 0},
                 [EN_K4] = {"WAVE 2", PF(lfo[1].wave), 0, NLFOW - 1, LFO_NAME}},
    /* the LFO group's mods: LFO 1's (SELECT: which LFO, ui.lfo_sel; LFO 2's LFOM2_P), its On-Key settings on a Bass
     * Station II */
    [PG_LFO2] = {[EN_K1] = {"KEYSYNC", PF(lfo_keysync[0]), 0, 1, OFF_ON},
                 [EN_K2] = {"SYNC", PF(lfo_sync[0]), 0, 1, SPD_SNC}, [EN_K3] = {"SLEW", PF(lfo_slew[0]), 0, 127, 0}},
    /* the Bass Station II's ARPEGGIATOR: Tempo, Rhythm, its Note Mode (PLAY, REC: the sequencer's), Octaves (in PLAY
     * and REC the sequence: knob_turn); SELECT its On/Legato button, ALGO its Latch/Rest (recording: the step just
     * played legato, a rest: knob_turn). Its mods (ARP again): Seq Retrig and the swing, On-Key settings on a Bass
     * Station II */
    [PG_ARP] = {[EN_K1] = {"TEMPO", PF(tempo), 40, 240, 0}, [EN_K2] = {"RHYTHM", PF(rhythm), 1, NRHY, 0},
                [EN_K3] = {"MODE", PF(arp_mode), 0, NARPM - 1, ARP_NAME}, [EN_K4] = {"OCTAVES", PF(arp_oct), 1, 4, 0}},
    [PG_AMOD] = {[EN_K1] = {"RETRIG", PF(arp_retrig), 0, 1, OFF_ON}, [EN_K2] = {"SWING", PF(swing), 3, 97, 0}},
    /* the sequencer (SEQ), a mod: its 32 steps and the arp's rhythm in one display; the sequence (of four), its
     * length, the cursor (PRESET), the step's note and legato (K1, K2: knob_turn), the gate (the FM-1's), the rhythm */
    [PG_SEQ] = {[EN_SELECT] = {"SEQ", PF(arp_oct), 1, NSEQ, 0}, [EN_ALGO] = {"LENGTH", PF(sq.len), 0, NSTEPS, 0},
                [EN_K3] = {"GATE", PF(gate), 5, 100, 0}, [EN_K4] = {"RHYTHM", PF(rhythm), 1, NRHY, 0}},
    [PG_AENV] = {[EN_K1] = {"ATTACK", PF(amp_a), 0, 127, 0}, [EN_K2] = {"DECAY", PF(amp_d), 0, 127, 0},
                 [EN_K3] = {"SUSTAIN", PF(amp_s), 0, 127, 0}, [EN_K4] = {"RELEASE", PF(amp_r), 0, 127, 0}},
    /* (K3: the mixer's switch, ui.mix_src; K4 the level it picks, MIX_SRC) */
    [PG_FX] = {[EN_SELECT] = {"OCTAVE", PF(sub_oct), 1, 2, SUB_OCT_NAME},
               [EN_ALGO] = {"SUBWAVE", PF(sub_wave), 0, NSUBW - 1, SUB_NAME}, [EN_PRESET] = {"SUB", PF(lvl_sub), 0, 127, 0},
               [EN_K1] = {"OSC1", PF(lvl1), 0, 127, 0}, [EN_K2] = {"OSC2", PF(lvl2), 0, 127, 0}},
    /* the Bass Station II's FILTERS: SELECT, ALGO and PRESET its Type, Slope and Shape switches (the one FLT_*: knob_turn);
     * then its modulation's view: LFO 2's depth, and the key tracking (the BS2's, a setting there) */
    [PG_TUNE] = {[EN_K1] = {"DRIVE", PF(drive), 0, 127, 0}, [EN_K2] = {"RESO", PF(reso), 0, 127, 0},
                 [EN_K3] = {"MOD ENV", PF(menv_cut), -63, 63, 0}, [EN_K4] = {"CUTOFF", PF(cutoff), 0, 127, 0}},
    [PG_FMOD] = {[EN_K1] = {"LFO 2", PF(lfo[1].depth[0]), -63, 63, 0}},
    /* the mods: what this synth has that a Bass Station II's panel has not (its settings there, or none), each group's
     * last page, drawn as a plate added to the panel (mod_draw): the HOME group's, the ENV group's */
    /* the Bass Station II's EFFECTS: Distortion (before the VCA), Osc Filter Mod; the FX group's mods (the filters'
     * and the effects'): the filter's key tracking, the VCA limiter (settings on a Bass Station II) */
    [PG_EFX] = {[EN_K1] = {"DIST", PF(dist), 0, 127, 0}, [EN_K2] = {"OSC FM", PF(ofm), 0, 127, 0}},
    [PG_XMOD] = {[EN_K1] = {"KEY TRK", PF(key_trk), 0, 7, 0}, [EN_K2] = {"LIMITER", PF(lim), 0, 127, 0}},
    /* (the ENV group's: the amp envelope's, the Env Select's at Mod EMODM_P; on a Bass Station II its On-Key and
     * Function settings) */
    [PG_EMOD] = {[EN_K1] = {"VELOCITY", PF(vel_amp), -63, 63, 0}, [EN_K2] = {"RETRIG", PF(env_loop[0]), 0, 1, OFF_ON},
                 [EN_K3] = {"COUNT", PF(env_count[0]), 0, 16, COUNT_NAME},
                 [EN_K4] = {"FIXED", PF(env_fixed[0]), 0, 1, OFF_ON}},
    [PG_GMIDI] = {[EN_SELECT] = {"CC CHAN", SF(cc_chan), CH_OFF, CH_ALL, CHAN_NAME},
                  [EN_K1] = {"CHANNEL", SF(chan), CH_OFF, CH_ALL, CHAN_NAME},
                  [EN_K2] = {"LOCAL", SF(local), 0, 1, OFF_ON},
                  [EN_K3] = {"AT OUT", SF(at_out), 0, 1, OFF_ON},
                  [EN_K4] = {"BLE", SF(ble_off), 0, 1, ON_OFF}},
    [PG_GSYS] = {[EN_SELECT] = {"SEND CLK", SF(clk_out), 0, 1, OFF_ON},
                 [EN_K1] = {"TUNE", SF(tune), -50, 50, 0}, [EN_K2] = {"SYNC", SF(clock), 0, NCLK - 1, CLK_NAME},
                 [EN_K3] = {"VELOCITY", SF(vcurve), 0, NVC - 1, VC_NAME},
                 [EN_K4] = {"KNOBS", SF(knobs), 0, NKN - 1, KN_NAME}},
};
/* MIX FX's switch, as the Bass Station II's: what its K4 sets, from the switch's bottom (NOISE, the start) up; EXT, the
 * external input's level: the computer's USB audio (plat_usb_audio_route) */
enum { MS_NOISE, MS_RING, MS_EXT, NMS };
static const param_t MIX_SRC[NMS] = {{"NOISE", PF(noise), 0, 127, 0}, {"RING", PF(ring), 0, 127, 0}, {"EXT", PF(lvl_ext), 0, 127, 0}};
/* the OSCILLATORS' Osc Select at 2: oscillator 2's, in PARAMS[PG_OSC]'s places (the sync the same) */
static const param_t OSC2_P[NE] = {[EN_SELECT] = {"RANGE", PF(range2), 0, NRANGE - 1, RANGE_NAME},
                                   [EN_ALGO] = {"WAVE", PF(wave2), 0, NWAVES - 1, WAVE_NAME},
                                   [EN_PRESET] = {"SYNC", PF(sync), 0, 1, OFF_ON},
                                   [EN_K1] = {"COARSE", PF(coarse2), -12, 12, 0},
                                   [EN_K2] = {"FINE", PF(fine2), -100, 100, 0},
                                   [EN_K3] = {"MOD ENV", PF(menv_o2), -63, 63, 0},
                                   [EN_K4] = {"LFO 1", PF(lfo[0].depth[1]), -63, 63, 0}};
/* the pulse width's switch, as the Bass Station II's: what its knob sets, Manual (the width, the start), Mod Env (the
 * mod envelope's depth on it), LFO 2 (its depth); for each oscillator. Its positions left to right: PW_POS */
enum { PW_MANUAL, PW_MENV, PW_LFO2, NPW };
static const char *const PW_SRC_NAME[NPW] = {"MANUAL", "MOD ENV", "LFO 2"};
static const uint8_t PW_POS[NPW] = {1, 0, 2};
static const param_t PW_P[2][NPW] = {
    {{"PW", PF(pw1), 5, 95, 0}, {"PW ENV", PF(menv_pw1), -63, 63, 0}, {"PW LFO", PF(pwm1), -63, 63, 0}},
    {{"PW", PF(pw2), 5, 95, 0}, {"PW ENV", PF(menv_pw2), -63, 63, 0}, {"PW LFO", PF(pwm2), -63, 63, 0}},
};
static const param_t LATCH_P = {"LATCH", PF(latch), 0, 1, OFF_ON};   /* (the ARP's ALGO: its Latch button) */
static const param_t MENV_P[NE] = {[EN_K1] = {"ATTACK", PF(menv_a), 0, 127, 0},   /* the mod envelope's faders */
                                   [EN_K2] = {"DECAY", PF(menv_d), 0, 127, 0},
                                   [EN_K3] = {"SUSTAIN", PF(menv_s), 0, 127, 0},
                                   [EN_K4] = {"RELEASE", PF(menv_r), 0, 127, 0}};
/* the ENV group's other pages at Env Select's Mod: the mod envelope's Triggering, its velocity, retriggering, count and
 * fixed sustain */
static const param_t TRIGM_P[NE] = {[EN_ALGO] = {"TRIGGER", PF(env_trig[1]), 0, NENVT - 1, TRIG_NAME}};
static const param_t EMODM_P[NE] = {[EN_K1] = {"VELOCITY", PF(vel_menv), -63, 63, 0},
                                    [EN_K2] = {"RETRIG", PF(env_loop[1]), 0, 1, OFF_ON},
                                    [EN_K3] = {"COUNT", PF(env_count[1]), 0, 16, COUNT_NAME},
                                    [EN_K4] = {"FIXED", PF(env_fixed[1]), 0, 1, OFF_ON}};
/* the LFOs' knobs at Delay, their sync values at Sync (the LFOS page's K2, K3); LFO 2's mods */
static const param_t LFOD_P[NLFO] = {{"DELAY 1", PF(lfo_delay[0]), 0, 127, 0}, {"DELAY 2", PF(lfo_delay[1]), 0, 127, 0}};
static const param_t LFOSV_P[NLFO] = {{"SYNC 1", PF(lfo_syncv[0]), 0, NSYNCV - 1, SYNC_NAME},
                                      {"SYNC 2", PF(lfo_syncv[1]), 0, NSYNCV - 1, SYNC_NAME}};
static const param_t LFOM2_P[NE] = {[EN_K1] = {"KEYSYNC", PF(lfo_keysync[1]), 0, 1, OFF_ON},
                                    [EN_K2] = {"SYNC", PF(lfo_sync[1]), 0, 1, SPD_SNC},
                                    [EN_K3] = {"SLEW", PF(lfo_slew[1]), 0, 127, 0}};
static const param_t FILT_P = {"FILTER", PF(filter), 0, NFLT - 1, FLT_NAME};   /* (the FILTERS' three switches) */
/* the sound's settings not in PARAMS (the views' other places): held to their ranges with the rest (patch_sane) */
static const param_t *const MORE_P[] = {&OSC2_P[EN_SELECT], &OSC2_P[EN_ALGO], &OSC2_P[EN_K1], &OSC2_P[EN_K2],
                                        &OSC2_P[EN_K3], &OSC2_P[EN_K4], &PW_P[0][PW_MENV], &PW_P[0][PW_LFO2],
                                        &PW_P[1][PW_MENV], &PW_P[1][PW_MANUAL], &PW_P[1][PW_LFO2], &MIX_SRC[MS_NOISE],
                                        &MIX_SRC[MS_RING], &MIX_SRC[MS_EXT], &FILT_P,
                                        &MENV_P[EN_K1], &MENV_P[EN_K2], &MENV_P[EN_K3], &MENV_P[EN_K4], &LATCH_P,
                                        &TRIGM_P[EN_ALGO], &EMODM_P[EN_K1], &EMODM_P[EN_K2], &EMODM_P[EN_K3],
                                        &EMODM_P[EN_K4], &LFOD_P[0], &LFOD_P[1], &LFOSV_P[0], &LFOSV_P[1],
                                        &LFOM2_P[EN_K1], &LFOM2_P[EN_K2], &LFOM2_P[EN_K3]};
/* a knob's whole name over the header, when its card's is short */
static const char *const TITLE[NPAGES][NE] = {
    [PG_OSC] = {[EN_SELECT] = "RANGE", [EN_ALGO] = "WAVEFORM", [EN_PRESET] = "SYNC 1-2", [EN_K1] = "COARSE",
                [EN_K2] = "FINE", [EN_K3] = "MOD ENV DEPTH", [EN_K4] = "LFO 1 DEPTH"},

    [PG_FX] = {[EN_SELECT] = "SUB OSC OCTAVE", [EN_ALGO] = "SUB OSC WAVE", [EN_PRESET] = "SUB OSC LEVEL",
               [EN_K1] = "OSC 1 LEVEL", [EN_K2] = "OSC 2 LEVEL"},
    [PG_ARP] = {[EN_K1] = "ARP TEMPO", [EN_K2] = "ARP RHYTHM", [EN_K3] = "ARP NOTE MODE", [EN_K4] = "ARP OCTAVES"},
    [PG_AMOD] = {[EN_K1] = "SEQ RETRIG", [EN_K2] = "ARP SWING"},
    [PG_SEQ] = {[EN_SELECT] = "SEQUENCE", [EN_ALGO] = "SEQ LENGTH", [EN_K4] = "ARP RHYTHM"},
    [PG_TUNE] = {[EN_K1] = "OVERDRIVE", [EN_K2] = "RESONANCE", [EN_K3] = "FILTER MOD ENV DEPTH", [EN_K4] = "FREQUENCY"},
    [PG_FMOD] = {[EN_K1] = "FILTER LFO 2 DEPTH"},
    [PG_EFX] = {[EN_K1] = "DISTORTION", [EN_K2] = "OSC FILTER MOD"},
    [PG_XMOD] = {[EN_K1] = "FILTER KEY TRACKING", [EN_K2] = "VCA LIMITER"},
    [PG_LFO1] = {[EN_SELECT] = "GLIDE TIME", [EN_K1] = "LFO 1 WAVE", [EN_K4] = "LFO 2 WAVE"},

    [PG_GMIDI] = {[EN_SELECT] = "CC CHANNEL", [EN_K2] = "LOCAL CONTROL", [EN_K3] = "AFTER TOUCH OUT", [EN_K4] = "BLUETOOTH MIDI"},
    [PG_GSYS] = {[EN_SELECT] = "SEND MIDI CLOCK", [EN_K2] = "CLOCK SYNC", [EN_K4] = "KNOB RESPONSE"},
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
    for (k = 0; k < sizeof MORE_P / sizeof MORE_P[0]; k++)
        param_put(MORE_P[k], p, param_get(MORE_P[k], p));
    if (p->sub_oct != 2u)                    /* (1 or 2) */
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
 * 0..127 across it. Not said (a sweep would flood the screen and the reader); the patch EDITED, SAVE lit. Each the
 * setting itself, wherever its page shows it (a view's other place: OSC2_P, PW_P, MIX_SRC) */
typedef struct {
    uint8_t cc;
    const param_t *q;
} cc_map_t;
#define CCP(g, k) (&PARAMS[g][k])
static const cc_map_t CC_MAP[] = {
    {5, CCP(PG_LFO1, EN_SELECT)},   {16, CCP(PG_TUNE, EN_K4)},     {82, CCP(PG_TUNE, EN_K2)},
    {114, CCP(PG_TUNE, EN_K1)},     {29, &OSC2_P[EN_K2]},          {30, &OSC2_P[EN_K1]},
    {110, CCP(PG_OSC, EN_PRESET)},  {24, &MIX_SRC[MS_RING]},       {23, &MIX_SRC[MS_NOISE]},
    {74, &PW_P[0][PW_MANUAL]},      {79, &PW_P[1][PW_MANUAL]},     {71, CCP(PG_OSC, EN_K3)},
    {85, CCP(PG_TUNE, EN_K3)},      {76, &OSC2_P[EN_K3]},          {102, &MENV_P[EN_K1]},
    {103, &MENV_P[EN_K2]},          {104, &MENV_P[EN_K3]},         {105, &MENV_P[EN_K4]},
    {88, CCP(PG_LFO1, EN_K1)},      {18, CCP(PG_LFO1, EN_K2)},     {89, CCP(PG_LFO1, EN_K4)},
    {19, CCP(PG_LFO1, EN_K3)},      {86, &LFOD_P[0]},              {87, &LFOD_P[1]},      {73, &PW_P[0][PW_LFO2]},       {78, &PW_P[1][PW_LFO2]},
    {118, CCP(PG_ARP, EN_K3)},      {109, &LATCH_P},               {111, CCP(PG_ARP, EN_K4)},
    {119, CCP(PG_ARP, EN_K2)},      {116, CCP(PG_AMOD, EN_K2)},     {90, CCP(PG_AENV, EN_K1)},
    {91, CCP(PG_AENV, EN_K2)},      {92, CCP(PG_AENV, EN_K3)},     {93, CCP(PG_AENV, EN_K4)},
    {80, CCP(PG_FX, EN_ALGO)},      {70, CCP(PG_OSC, EN_SELECT)},  {75, &OSC2_P[EN_SELECT]},
    {20, CCP(PG_FX, EN_K1)},        {21, CCP(PG_FX, EN_K2)},       {22, CCP(PG_FX, EN_PRESET)},
    {94, CCP(PG_EFX, EN_K1)},                  {27, CCP(PG_OSC, EN_K1)},      {26, CCP(PG_OSC, EN_K2)},
    {72, &PW_P[0][PW_MENV]},        {77, &PW_P[1][PW_MENV]},       {112, CCP(PG_EMOD, EN_K1)},
    {113, &EMODM_P[EN_K1]},        {115, CCP(PG_EFX, EN_K2)},
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
        const param_t *q = CC_MAP[i].q;
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
        org_set(q);
        set_dirty = 1;
        ui.redraw = 1;
    }
}

/* REC (not on GLO MIDI): every knob of the oscillators, their mix, the filter, both envelopes and both LFOs at random,
 * a new sound to start from (unsaved: SAVE keeps it); the filter left a little open and the attack under a second, so
 * it is heard */
static void randomize(void)
{
    static const param_t *const RND_P[] = {
        CCP(PG_OSC, EN_ALGO), &OSC2_P[EN_ALGO], &OSC2_P[EN_K1], &OSC2_P[EN_K2], CCP(PG_OSC, EN_PRESET),
        CCP(PG_OSC, EN_K3), &OSC2_P[EN_K3], CCP(PG_OSC, EN_K4), &OSC2_P[EN_K4], &PW_P[0][PW_MANUAL], &PW_P[1][PW_MANUAL],
        &PW_P[0][PW_LFO2], &PW_P[1][PW_LFO2], &FILT_P, CCP(PG_LFO1, EN_SELECT), CCP(PG_TUNE, EN_K1),
        CCP(PG_TUNE, EN_K2), CCP(PG_TUNE, EN_K3), CCP(PG_FMOD, EN_K1), &MIX_SRC[MS_RING], &MIX_SRC[MS_NOISE],
        CCP(PG_TUNE, EN_K4), &MENV_P[EN_K1], &MENV_P[EN_K2], &MENV_P[EN_K3], &MENV_P[EN_K4],
        CCP(PG_EMOD, EN_K1), &EMODM_P[EN_K1], CCP(PG_AENV, EN_K1), CCP(PG_AENV, EN_K2), CCP(PG_AENV, EN_K3),
        CCP(PG_AENV, EN_K4), CCP(PG_LFO1, EN_K1), CCP(PG_LFO1, EN_K2), CCP(PG_LFO1, EN_K4), CCP(PG_LFO1, EN_K3)};
    static uint32_t rng = 1u;
    uint32_t i;
    rng ^= plat_ms();
    for (i = 0; i < sizeof RND_P / sizeof RND_P[0]; i++) {
        const param_t *q = RND_P[i];
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
static int env_page(void) { return ui.page == PG_AENV || ui.page == PG_MENV || ui.page == PG_EMOD; }   /* the ENV group's */
static const param_t *env_mod_p(void)        /* the ENV group's page's settings for the mod envelope */
{
    return ui.page == PG_AENV ? MENV_P : ui.page == PG_MENV ? TRIGM_P : EMODM_P;
}
/* what encoder k sets on this page: as its switches are (MIX FX's K4; the OSCILLATORS' Osc Select, their pulse
 * width's switch) */
static const param_t *knob_param(uint32_t k)
{
    if (ui.page == PG_FX && k == EN_K4)
        return &MIX_SRC[ui.mix_src];
    if (ui.page == PG_OSC && ui.osc_sel)
        return &OSC2_P[k];
    if (ui.page == PG_MIX && k == EN_K4)
        return &PW_P[ui.osc_sel][ui.pw_src];
    if (ui.page == PG_LFO1 && (k == EN_K2 || k == EN_K3)) {   /* an LFO's knob: its delay, its sync value, its speed */
        uint32_t i = k - EN_K2;
        return ui.lfo_delay ? &LFOD_P[i] : patch.lfo_sync[i] ? &LFOSV_P[i] : &PARAMS[PG_LFO1][k];
    }
    if (ui.page == PG_LFO2 && ui.lfo_sel)
        return &LFOM2_P[k];
    if (env_page() && ui.env_sel == 1u)      /* the ENV group's at Env Select's Mod */
        return &env_mod_p()[k];
    return &PARAMS[ui.page][k];
}
/* the encoders' settings: each turn a step, held at its ends; 0 when this page has nothing on it */
/* a knob turned fast goes further, as GLO SYS's KNOBS sets: the steps a click by the time since the last (under 20,
 * 35, 60, 100 ms, else), NORMAL's so a quick spin crosses 0..127 in a turn or two and a slow one still steps by one;
 * FINE always one, COARSE two at the slowest. Only a control of 48 steps or more (not the switches and choices). A
 * wider one (the LFOs' speeds, 0..255) as a 0..127 one: each click its share of 128 (2 steps). The host's "spin"
 * exact, its "turn" (80 ms apart) one a click at NORMAL */
static int32_t knob_accel(uint32_t k, int32_t e, const param_t *q)
{
    static uint32_t last[NE];
    uint32_t now = plat_ms(), gap = now - last[k];
#ifdef PLAT_HOST
    if (plat_host_exact)                     /* (and no time of its own for the next turn's) */
        return e;
#endif
    last[k] = now;
    if (!q->name || q->hi - q->lo < 48)
        return e;
    static const uint8_t MUL[NKN][5] = {{1, 1, 1, 1, 1}, {8, 4, 2, 1, 1}, {12, 8, 4, 2, 1}, {16, 12, 8, 4, 2}};
    return e * ((q->hi - q->lo + 1) / 128 > 1 ? (q->hi - q->lo + 1) / 128 : 1) *
           MUL[set.knobs < NKN ? set.knobs : KN_NORMAL][gap < 20u ? 0 : gap < 35u ? 1 : gap < 60u ? 2 : gap < 100u ? 3 : 4];
}
static int knob_turn(uint32_t k, int32_t e)
{
    const param_t *q = knob_param(k);
    e = knob_accel(k, e, q);
    if (ui.page == PG_PATCH && k == EN_PRESET) {   /* a slot: its sound at once (what was not saved is left) */
        uint32_t s = (uint32_t)clamp((int32_t)set.slot + e, 0, NSLOTS - 1);
        ui.pdir = (int8_t)(e < 0 ? -1 : 1);
        if (s != set.slot) {
            uint8_t on = patch.arp_on, mode = patch.arp_mode, latch = patch.latch;
            seq_keep(1);                     /* (the sequence made here, kept before it goes) */
            set.slot = (uint8_t)s;
            patch = bank.p[s];
            ui.org = 0;
            ui.org_at = 0;
            if (on) {                        /* the arp or the sequencer playing: on still, as it was (the new
                                              * sound's own notes, sequence and tempo; not kept into its slot) */
                patch.arp_on = on;
                patch.arp_mode = mode;
                patch.latch = latch;
            }
            set_dirty = 1;
        }
        return 1;
    }
    if (ui.page == PG_GRND && k == EN_PRESET) {   /* a seed slot: its seed shown */
        rnd_pick(e);
        return 1;
    }
    if (ui.page == PG_ARP && (k == EN_SELECT || k == EN_ALGO)) {   /* On/Legato, Latch/Rest: buttons, turned (up on,
                                                                  * down off); recording, the step legato, a rest */
        if (seq_rec && e > 0) {
            if (k == EN_SELECT)
                step_legato();
            else
                step_enter(SEQ_REST);
        } else if (!seq_rec) {
            if (k == EN_SELECT)
                patch.arp_on = e > 0;
            else
                patch.latch = e > 0;
            set_dirty = 1;
        }
        return 1;
    }
    if ((ui.page == PG_SEQ && k == EN_SELECT) || (ui.page == PG_ARP && k == EN_K4 && patch.arp_mode >= ARP_PLAY)) {
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
    if (ui.page == PG_FX && k == EN_K3) {   /* the mixer's switch: up, NOISE to RING to EXT */
        ui.mix_src = (uint8_t)clamp((int32_t)ui.mix_src + e, 0, NMS - 1);
        return 1;
    }
    if (ui.page == PG_LFO1 && k == EN_ALGO) {   /* the Delay/Speed switch: up Delay, down Speed */
        ui.lfo_delay = e < 0;
        return 1;
    }
    if (ui.page == PG_LFO2 && k == EN_SELECT) {   /* the mods' LFO */
        ui.lfo_sel = (uint8_t)clamp((int32_t)ui.lfo_sel + e, 0, 1);
        return 1;
    }
    if (env_page() && k == EN_SELECT) {      /* Env Select, on each of the ENV group's pages: Amp, Mod, Amp+Mod */
        ui.env_sel = (uint8_t)clamp((int32_t)ui.env_sel + e, 0, 2);
        return 1;
    }
    if (env_page() && ui.env_sel == 2u && q->name) {   /* Amp+Mod: the mod envelope's to the amp's, both */
        int32_t v = param_get(q, &patch) + e;
        param_put(q, &patch, v);
        param_put(&env_mod_p()[k], &patch, v);
        org_set(q);
        set_dirty = 1;
        return 1;
    }
    if (ui.page == PG_TUNE && k <= EN_PRESET) {   /* the FILTERS' Type, Slope (12 left, 24 right), Shape (LP, BP, HP) */
        uint32_t f = patch.filter, acid = f == FLT_ACID;
        if (k == EN_SELECT && (e > 0) != acid) {
            if (!acid)
                ui.flt_classic = (uint8_t)f;
            f = acid ? ui.flt_classic : FLT_ACID;
        } else if (k != EN_SELECT && !acid) {   /* (Acid: one low pass, its switches out) */
            uint32_t shape = f / 2u, d24 = !(f & 1u);
            if (k == EN_ALGO)
                d24 = e > 0;
            else
                shape = (uint32_t)clamp((int32_t)shape + e, 0, 2);
            f = shape * 2u + !d24;
        }
        patch.filter = (uint8_t)f;
        set_dirty = 1;
        return 1;
    }
    if (ui.page == PG_MIX && k == EN_PRESET) {   /* the pulse width's switch: left to right, Mod Env, Manual, LFO 2 */
        static const uint8_t AT[NPW] = {PW_MENV, PW_MANUAL, PW_LFO2};
        ui.pw_src = AT[clamp((int32_t)PW_POS[ui.pw_src] + e, 0, NPW - 1)];
        return 1;
    }
    if (!q->name)
        return 0;
    param_put(q, page_rec(ui.page), param_get(q, page_rec(ui.page)) + e);
    if (ui.page < PG_GMIDI) {                /* (the GLO pages' keep themselves: glo_keep) */
        org_set(q);
        set_dirty = 1;
    }
    if (ui.page == PG_SEQ && k == EN_ALGO)   /* (the length: into the one picked too) */
        seq_changed();
    if (ui.page == PG_GMIDI && k == EN_K4)   /* Bluetooth: at once */
        plat_ble_enable(!set.ble_off);
    return 1;
}
static const char *knob_name(uint32_t k)
{
    if (ui.page == PG_SEQ && (k == EN_PRESET || k == EN_K1 || k == EN_K2))
        return k == EN_PRESET ? "STEP" : k == EN_K1 ? "NOTE" : "LEGATO";
    if (ui.page == PG_ARP && (k == EN_SELECT || k == EN_ALGO))
        return k == EN_SELECT ? (seq_rec ? "LEGATO" : "ARP") : seq_rec ? "REST" : "LATCH";
    if (ui.page == PG_ARP && k == EN_K4 && patch.arp_mode >= ARP_PLAY)   /* (the switch: the sequence in REC, PLAY) */
        return "SEQ";
    if (k == EN_PRESET && (ui.page == PG_PATCH || ui.page == PG_GRND))
        return ui.page == PG_PATCH ? "PATCH" : "SEED";
    if (ui.page == PG_FX && k == EN_K3)
        return "SOURCE";
    if (ui.page == PG_MIX && k == EN_PRESET)
        return "PW SOURCE";
    if (env_page() && k == EN_SELECT)
        return "ENV SELECT";
    if (ui.page == PG_LFO1 && k == EN_ALGO)
        return "DELAY/SPEED";
    if (ui.page == PG_LFO2 && k == EN_SELECT)
        return "LFO";
    if (ui.page == PG_TUNE && k <= EN_PRESET)
        return k == EN_SELECT ? "TYPE" : k == EN_ALGO ? "SLOPE" : "SHAPE";
    return knob_param(k)->name;
}
static const char *knob_title(uint32_t k)    /* over the header: the whole name (the OSCILLATORS': which, "OSC 2 FINE") */
{
    static char w[32];
    const char *t = knob_name(k) == PARAMS[ui.page][k].name ? TITLE[ui.page][k] : 0;
    if ((ui.page == PG_LFO1 && (k == EN_K2 || k == EN_K3)) || (ui.page == PG_LFO2 && k != EN_SELECT && knob_param(k)->name)) {
        static const char *const WHAT[NE] = {[EN_K1] = "KEYSYNC", [EN_K2] = "SPEED/SYNC", [EN_K3] = "SLEW"};
        uint32_t i = ui.page == PG_LFO1 ? k - EN_K2 : ui.lfo_sel;   /* "LFO 2 DELAY", "LFO 1 SYNC", "LFO 1 SLEW" */
        w[0] = 0;
        text_cat(w, i ? "LFO 2 " : "LFO 1 ");
        text_cat(w, ui.page == PG_LFO2 ? WHAT[k] : ui.lfo_delay ? "DELAY" : patch.lfo_sync[i] ? "SYNC" : "SPEED");
        return w;
    }
    if (env_page() && k != EN_SELECT && knob_param(k)->name) {   /* "AMP ENV ATTACK", "MOD ENV TRIGGERING",
                                                                    * "AMP+MOD ENV RETRIGGER COUNT" */
        static const char *const WHAT[3][NE] = {
            {[EN_K1] = "ATTACK", [EN_K2] = "DECAY", [EN_K3] = "SUSTAIN", [EN_K4] = "RELEASE"},
            {[EN_ALGO] = "TRIGGERING"},
            {[EN_K1] = "VELOCITY", [EN_K2] = "RETRIGGER", [EN_K3] = "RETRIGGER COUNT", [EN_K4] = "FIXED SUSTAIN"}};
        w[0] = 0;
        text_cat(w, ui.env_sel == 1u ? "MOD ENV " : ui.env_sel ? "AMP+MOD ENV " : "AMP ENV ");
        text_cat(w, WHAT[ui.page == PG_AENV ? 0 : ui.page == PG_MENV ? 1 : 2][k]);
        return w;
    }
    if (ui.page == PG_OSC && k != EN_PRESET)
        t = TITLE[PG_OSC][k];
    else if (ui.page == PG_MIX && k == EN_K4)
        t = PW_SRC_NAME[ui.pw_src];
    else
        return t ? t : knob_name(k);
    w[0] = 0;                                /* "OSC 2 COARSE", "OSC 1 PW MOD ENV" */
    text_cat(w, ui.osc_sel ? "OSC 2 " : "OSC 1 ");
    if (ui.page == PG_MIX)
        text_cat(w, "PW ");
    text_cat(w, t);
    return w;
}
static void knob_text(uint32_t k, char *t)
{
    const param_t *q = knob_param(k);
    int32_t v;
    t[0] = 0;
    if (ui.page == PG_ARP && (k == EN_SELECT || k == EN_ALGO)) {   /* the buttons: what they set (recording, its step) */
        text_cat(t, seq_rec ? (k == EN_SELECT ? "LEGATO" : "REST") : OFF_ON[k == EN_SELECT ? patch.arp_on : patch.latch]);
        return;
    }
    if (ui.page == PG_FX && k == EN_K3) {   /* the switch */
        text_cat(t, MIX_SRC[ui.mix_src].name);
        return;
    }
    if (ui.page == PG_MIX && k == EN_PRESET) {
        text_cat(t, PW_SRC_NAME[ui.pw_src]);
        return;
    }
    if (ui.page == PG_LFO1 && k == EN_ALGO) {
        text_cat(t, ui.lfo_delay ? "DELAY" : "SPEED");
        return;
    }
    if (ui.page == PG_LFO2 && k == EN_SELECT) {
        text_cat(t, ui.lfo_sel ? "LFO 2" : "LFO 1");
        return;
    }
    if (q->off == PF(lfo[0].rate) || q->off == PF(lfo[1].rate)) {   /* a speed in Hz: "7.9 HZ", "190 HZ" */
        uint32_t c = (uint32_t)(lfo_hz((uint32_t)param_get(q, &patch)) * 10.0f + 0.5f);
        int_text((int32_t)(c / 10u), t);
        if (c < 1000u) {
            char d[4] = {'.', (char)('0' + c % 10u), 0};
            text_cat(t, d);
        }
        text_cat(t, " HZ");
        return;
    }
    if (env_page() && k == EN_SELECT) {
        text_cat(t, ui.env_sel == 1u ? "MOD ENV" : ui.env_sel ? "AMP+MOD ENV" : "AMP ENV");
        return;
    }
    if (ui.page == PG_TUNE && k <= EN_PRESET) {   /* (Acid: its slope and shape fixed) */
        static const char *const SH[3] = {"LP", "BP", "HP"};
        uint32_t f = patch.filter;
        text_cat(t, k == EN_SELECT ? (f == FLT_ACID ? "ACID" : "CLASSIC") : f == FLT_ACID ? "-"
                    : k == EN_ALGO ? (f & 1u ? "12 DB" : "24 DB") : SH[f / 2u]);
        return;
    }
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
        else if (seq_rec && ui.page == PG_SEQ && k == B_FX)   /* recording, on SEQ: FX a rest (the Bass Station
                                              * II's REST), SEL the step before legato (its LEGATO); elsewhere their
                                              * own, so REC left on doesn't take them */
            step_enter(SEQ_REST);
        else if (seq_rec && ui.page == PG_SEQ && k == B_SEL)
            step_legato();
        else if (k == B_SEL && (ui.page == PG_OSC || ui.page == PG_MIX)) {   /* the OSCILLATORS' Osc Select */
            ui.osc_sel ^= 1u;
            ui.redraw = 1;
            app_say("OSC SELECT", ui.osc_sel ? "2" : "1");
        } else if (k == B_SEL && ui.page == PG_GDUMP)   /* GLO DUMP's: the sound out as a Bass Station II patch dump */
            sysex_send();
        else if (k == B_SEL && ui.page == PG_PATCH)
            ;                                /* (the MASTER's: nothing) */
        else if (k == B_REC)
            ui.rec_held = 1;                 /* on release (unless OCT+ comes first) */
        else if (k == B_SAVE && ui.page == PG_GRND)   /* the seed shown into its slot */
            rnd_store();
        else if (k == B_SAVE)
            save();
        else if (k == B_HOME)                    /* the MASTER (the first page), again the sub osc and the mixer */
            page_go(ui.page == PG_PATCH ? PG_FX : PG_PATCH);
        else if (k == B_ENV)                     /* the envelopes; their triggering; the group's mods (Env Select
                                                  * stays as it is: SELECT's) */
            page_go(ui.page == PG_AENV ? PG_MENV : ui.page == PG_MENV ? PG_EMOD : PG_AENV);
        else if (k == B_LFO)                     /* LFO 1, again LFO 2 */
            page_go(ui.page == PG_LFO1 ? PG_LFO2 : PG_LFO1);
        else if (k == B_EDIT)                    /* the oscillators, again their pulse width */
            page_go(ui.page == PG_OSC ? PG_MIX : PG_OSC);
        else if (k == B_ARP)                     /* the ARPEGGIATOR, again its mods (On: its SELECT) */
            page_go(ui.page == PG_ARP ? PG_AMOD : PG_ARP);
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
            if (set.clk_out && !ext_clock())  /* (SEND CLK: start or stop to what follows the clock) */
                plat_midi_send(0x0Fu | (run ? 0xFAu : 0xFCu) << 8, TO_USB | TO_BLE);
            app_say("SEQ", run ? "PLAY" : "STOP");
        } else if (k == B_GLO)               /* the global settings: MIDI, again SYS, again DUMP (RND: GLO + OCT+) */
            page_go(ui.page == PG_GMIDI ? PG_GSYS : ui.page == PG_GSYS ? PG_GDUMP : PG_GMIDI);
        else if (k == B_FX && ui.page == PG_GDUMP)   /* every slot's */
            sysex_dump_all();
        else if (k == B_FX && ui.page == PG_GRND)    /* the seed shown, as the RND Synth's SysEx */
            rnd_send();
        else if (k == B_FX)                      /* the filters, LFO 2 on them, the effects, the group's mods */
            page_go(ui.page == PG_TUNE ? PG_FMOD : ui.page == PG_FMOD ? PG_EFX : ui.page == PG_EFX ? PG_XMOD : PG_TUNE);
        else {
            app_say("BUTTON", BTN_NAME[k]);
        }
    }
    if (ui.rec_held && !(btn >> B_REC & 1u)) {   /* REC let go: on GLO MIDI a Bluetooth device, elsewhere a random
                                                  * sound */
        ui.rec_held = 0;
        if (ui.page != PG_GMIDI)
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
        if (k == EN_ALGO && FM1B_BLE_CENTRAL && ui.looking && ui.page == PG_GMIDI) {
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
 * what was just turned lit cyan. The keys and the status under it. Matte black, grey cards, Novation's cyan (the
 * Bass Station II panel's own, #3aadec: its bars, the tab you're on, what is live) */
#define P_BG RGB(18, 18, 18)                 /* the panel: matte black */
#define P_CARD RGB(38, 38, 40)               /* a section's card */
#define P_EDGE RGB(64, 64, 68)               /* its edge, a slider's notches */
#define P_LABEL RGB(150, 150, 156)           /* names */
#define P_TEXT RGB(255, 255, 255)            /* values */
#define P_CYAN RGB(58, 173, 236)             /* live: the page, a knob just turned, a curve, a level (#3aadec) */
#define P_AFX P_CYAN                         /* AFX on: its mark */
#define P_CDIM RGB(18, 54, 74)               /* under a curve; a button lit (#12364a) */
#define P_KEYW RGB(206, 206, 210)
#define P_KEYB RGB(10, 10, 10)
/* the firmware's version as shown (the splash, GLO SYS): a release's "v0.2", a development build's "DEV" */
#define VERSION_TEXT (APP_VERSION[0] >= '0' && APP_VERSION[0] <= '9' ? "v" APP_VERSION : APP_VERSION)
static void ui_start(void) { ui.page = PG_PATCH; }   /* the first page, after the splash: the MASTER */

/* the tabs: the buttons, each its pages: HOME the MASTER and the mixer, EDIT the oscillators, FX the filters and the
 * effects */
enum { SEC_HOME, SEC_ENV, SEC_LFO, SEC_FX, SEC_ARP, SEC_SEQ, SEC_EDIT, SEC_GLO, NSEC };
static const char *const SEC_NAME[NSEC] = {"HOME", "ENV", "LFO", "FX", "ARP", "SEQ", "EDIT", "GLO"};
static const uint8_t PAGE_SEC[NPAGES] = {[PG_OSC] = SEC_EDIT, [PG_MIX] = SEC_EDIT, [PG_MENV] = SEC_ENV,
                                         [PG_LFO1] = SEC_LFO, [PG_LFO2] = SEC_LFO, [PG_PATCH] = SEC_HOME,
                                         [PG_ARP] = SEC_ARP, [PG_SEQ] = SEC_SEQ, [PG_AENV] = SEC_ENV, [PG_FX] = SEC_HOME,
                                         [PG_TUNE] = SEC_FX, [PG_FMOD] = SEC_FX,
                                         [PG_EMOD] = SEC_ENV, [PG_EFX] = SEC_FX, [PG_XMOD] = SEC_FX, [PG_AMOD] = SEC_ARP,
                                         [PG_GMIDI] = SEC_GLO, [PG_GSYS] = SEC_GLO, [PG_GDUMP] = SEC_GLO,
                                         [PG_GRND] = SEC_GLO};
/* the tabs span the panel's bar (x 2..235), a pixel between each; they end 3 rows over it, as its gaps are wide */
#define TAB_X(i) (2 + (int32_t)(i) * 235 / NSEC)
static uint8_t ycol[232];                    /* a picture's curve: each column's row */

static int knob_lit(uint32_t k) { return ui.turned_at[k] && plat_ms() - ui.turned_at[k] < 300u; }

#define P_KSHADOW RGB(6, 6, 6)               /* a knob's drop shadow, a slot's black */

static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)   /* filled, a row at a time: no sqrt */
{
    int32_t dy, dx = r;
    for (dy = 0; dy <= r; dy++) {
        while (dx * dx + dy * dy > r * r + r)
            dx--;
        cv_rect(cx - dx, cy - dy, 2 * dx + 1, 1, c);
        if (dy)
            cv_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}
static int32_t round_px(float f) { return (int32_t)(f < 0.0f ? f - 0.5f : f + 0.5f); }
static void pt_at(int32_t cx, int32_t cy, int32_t r, float a, int32_t *x, int32_t *y)   /* a: from 12 o'clock, clockwise */
{
    *x = cx + round_px(fm_sinf(a) * (float)r);
    *y = cy - round_px(fm_cosf(a) * (float)r);
}

/* The Bass Station II's panel, as bgoonz/bass-station draws it (about 10 px to its rem): a dark panel shaded across,
 * the sections' blue bar with their names, a gap where one meets the next and a blue line down its side, a blue line
 * along the bottom; the print white, the LEDs red */
#define P_SILK RGB(240, 240, 240)
#define P_BAR P_CYAN                           /* the sections' bars */
#define P_LEDON RGB(169, 18, 25)               /* #a91219, #340d17 */
#define P_LEDOFF_R RGB(52, 13, 23)
#define TALL_H 160                             /* a panel page's height: the header's second row too (screen rows
                                                * 20..179) */
static int32_t chord(int32_t r, int32_t d)   /* a circle's half height d from its middle (-1: outside) */
{
    int32_t e = r;
    if (d * d > r * r + r)
        return -1;
    while (d * d + e * e > r * r + r)
        e--;
    return e;
}
static uint16_t grey(int32_t g)
{
    g = clamp(g, 0, 250);
    return RGB(g, g, g + 4);
}
/* a disc lit from the top left: base at its middle, amp lighter and darker across it, more so at its rim */
static void shade_disc(int32_t cx, int32_t cy, int32_t r, int32_t base, int32_t amp)
{
    int32_t dx, dy;
    for (dx = -r; dx <= r; dx++)
        for (dy = -chord(r, dx); dy <= chord(r, dx); dy++) {
            int32_t t = -(dx + dy) * amp / (2 * r);
            cv_pset(cx + dx, cy + dy, grey(base + (dx * dx + dy * dy > (r - 2) * (r - 2) ? 2 * t : t)));
        }
}
/* a knob's ways: turning (its line cyan), going either way (its top dot a bar: the Bass Station II's centre), not
 * this view's (dim) */
#define KF_LIT 1u
#define KF_BIP 2u
#define KF_DIM 4u
#define KF_PLATE 8u                            /* on a mod's plate: its dots dark on the metal */
#define KF_NODOTS 16u                          /* no dots round it (a switch's: its positions printed instead) */
#define P_DIM RGB(96, 96, 100)                 /* a dim view's print */
/* a knob at cx, cy, r its skirt's radius, its line at angle a (from 12 o'clock, clockwise): the panel's dots, a ribbed
 * skirt, a raised middle, a white cap and its line */
static void knob_at(int32_t cx, int32_t cy, int32_t r, float a, uint32_t f)
{
    int32_t i, x, y, x1, y1, sx, sy, rise = r * 2 / 3, cap = r * 47 / 100;
    uint16_t silk = f & KF_PLATE ? grey(40) : f & KF_DIM ? P_DIM : P_SILK;
    for (i = 0; i < 12 && !(f & KF_NODOTS); i++)   /* every 30 degrees but straight down */
        if (i == 0 && f & KF_BIP)
            cv_rect(cx - 1, cy - r - 5, 2, 5, silk);
        else if (i != 6) {
            pt_at(cx, cy, r + 3, (float)i * (FM_PI / 6.0f), &x, &y);
            disc(x, y, 1, silk);
        }
    disc(cx + 1, cy + 2, r, P_KSHADOW);
    shade_disc(cx, cy, r, 32, 40);
    for (i = 0; i < 18; i++) {               /* the ribs turn with it */
        pt_at(cx, cy, rise, a + (float)i * (FM_PI / 9.0f), &x, &y);
        disc(x, y, 1, grey(70));
    }
    shade_disc(cx, cy, rise, 32, 90);
    shade_disc(cx, cy, cap, f & KF_DIM ? 110 : 226, 30);
    pt_at(cx, cy, cap, a, &x, &y);           /* the line, out from the cap across the middle, three pixels wide */
    pt_at(cx, cy, cap + 6, a, &x1, &y1);
    sx = (y1 > y ? y1 - y : y - y1) >= (x1 > x ? x1 - x : x - x1);
    sy = !sx;
    for (i = -1; i <= 1; i++)
        cv_line(x + i * sx, y + i * sy, x1 + i * sx, y1 + i * sy, f & KF_LIT ? P_CYAN : f & KF_DIM ? P_DIM : RGB(230, 230, 230));
}
/* turned to v of 0..span, 7 to 5 o'clock (the dots' ends) */
static void bs2_knob(int32_t cx, int32_t cy, int32_t r, int32_t v, int32_t span, uint32_t f)
{
    knob_at(cx, cy, r, ((float)v / (float)span - 0.5f) * (FM_PI * 5.0f / 3.0f), f);
}
/* a slide switch standing at x, y (its top left), n positions, pos 0 at the top: 18 wide, a silver housing round at its
 * ends, a black slot, a ribbed thumb (its rim cyan while it moves; all dim when not this view's) */
static void slide_v(int32_t x, int32_t y, int32_t n, int32_t pos, uint32_t f)   /* f: KF_LIT, KF_DIM */
{
    int32_t len = 9 * n + 15, i, e, ty = y + 11 + pos * 10;
    for (i = 0; i < 18; i++) {
        int32_t m = i < 17 - i ? i : 17 - i;
        e = chord(9, i < 9 ? i - 9 : i - 8);
        cv_rect(x + i, y + 9 - e, 1, len - 18 + 2 * e,
                grey((f & KF_DIM ? 60 : 114) + (f & KF_DIM ? 40 : 93) * (m > 5 ? 5 : m) / 5));
    }
    cv_rect(x + 2, y + 3, 14, len - 6, P_KSHADOW);
    disc(x + 9, ty, 7, f & KF_LIT ? P_CYAN : f & KF_DIM ? P_DIM : RGB(224, 226, 231));
    for (i = -6; i <= 6; i++) {
        e = chord(6, i);
        cv_rect(x + 9 + i, ty - e, 1, 2 * e + 1, (i + 6) / 2 & 1 ? RGB(16, 17, 29) : RGB(53, 54, 65));
    }
}
/* the same lying down at x, y: pos 0 at the left */
static void slide_h(int32_t x, int32_t y, int32_t n, int32_t pos, uint32_t f)
{
    int32_t len = 9 * n + 15, i, e, tx = x + 11 + pos * 10;
    for (i = 0; i < 18; i++) {
        int32_t m = i < 17 - i ? i : 17 - i;
        e = chord(9, i < 9 ? i - 9 : i - 8);
        cv_rect(x + 9 - e, y + i, len - 18 + 2 * e, 1, grey((f & KF_DIM ? 60 : 114) + (f & KF_DIM ? 40 : 93) * (m > 5 ? 5 : m) / 5));
    }
    cv_rect(x + 3, y + 2, len - 6, 14, P_KSHADOW);
    disc(tx, y + 9, 7, f & KF_LIT ? P_CYAN : f & KF_DIM ? P_DIM : RGB(224, 226, 231));
    for (i = -6; i <= 6; i++) {
        e = chord(6, i);
        cv_rect(tx + i, y + 9 - e, 1, 2 * e + 1, (i + 6) / 2 & 1 ? RGB(16, 17, 29) : RGB(53, 54, 65));
    }
}
static void led(int32_t cx, int32_t cy, int on)   /* a red LED: lit, it glows */
{
    if (on) {
        disc(cx, cy, 7, RGB(40, 8, 10));
        disc(cx, cy, 5, RGB(84, 12, 16));
    }
    disc(cx, cy, 4, P_KSHADOW);
    disc(cx, cy, 3, on ? P_LEDON : P_LEDOFF_R);
    if (on)
        cv_pset(cx - 1, cy - 1, RGB(225, 116, 116));
}
static void wave_icon(int32_t x, int32_t y, uint32_t w)   /* the sub's waves as printed, 10 x 7 from x, y: SUB_* */
{
    int32_t i, hi = w == SUB_PULSE ? 3 : 5;
    if (w == SUB_SINE) {
        for (i = 1; i <= 10; i++)
            cv_line(x + i - 1, y + 3 - round_px(3.0f * fm_sinf((float)(i - 1) * (FM_TWO_PI / 10.0f))), x + i,
                    y + 3 - round_px(3.0f * fm_sinf((float)i * (FM_TWO_PI / 10.0f))), P_SILK);
        return;
    }
    cv_rect(x, y, 1, 7, P_SILK);             /* up, along, down, along, up */
    cv_rect(x, y, hi, 1, P_SILK);
    cv_rect(x + hi - 1, y, 1, 7, P_SILK);
    cv_rect(x + hi - 1, y + 6, 10 - hi, 1, P_SILK);
    cv_rect(x + 9, y, 1, 7, P_SILK);
}
static void panel_bg(void)                   /* rgb 20 at the sides to 30 in the middle, as the reference's */
{
    int32_t i;
    for (i = 0; i < 240; i++)
        cv_rect(i, 0, 1, TALL_H, grey(20 + 10 * (i < 120 ? i : 239 - i) / 120));
}
/* the bar, x0 to x1, its gaps at the sections' edges (each a line down from it, its left), the line along the bottom */
static void panel_bars(int32_t x0, int32_t x1, const int32_t *edge, int32_t n)
{
    int32_t i;
    cv_rect(x0, 1, x1 - x0, 15, P_BAR);
    for (i = 0; i < n; i++) {
        cv_rect(edge[i], 1, 3, 151, P_BAR);
        cv_rect(edge[i] + 3, 1, 3, 15, grey(20));
    }
    cv_rect(x0, TALL_H - 5, x1 - x0, 3, P_BAR);
}
static void bar_text(int32_t x, int32_t y, const char *s)   /* a name in the bar: white, its capitals in the middle */
{
    cv_text(x, y, &FONT_B, s, P_SILK);
}
static void silk_text(int32_t cx, int32_t y, const char *s, uint16_t c)   /* centred on cx */
{
    cv_text(cx - text_w(&FONT_XS, s) / 2, y, &FONT_XS, s, c);
}
/* a knob of the panel: encoder k, its name printed under it (its value instead, cyan, while it turns; no name: only
 * then) */
static void panel_knob(int32_t cx, int32_t cy, uint32_t k, const char *name)
{
    const param_t *q = knob_param(k);
    int lit = knob_lit(k);
    char t[12];
    bs2_knob(cx, cy, 20, param_get(q, page_rec(ui.page)) - q->lo, q->hi - q->lo, lit ? KF_LIT : 0u);
    knob_text(k, t);
    if (lit || name)
        silk_text(cx, cy + 24, lit ? t : name, lit ? P_CYAN : P_SILK);
}
/* MIX FX: the end of the OSCILLATORS section (the sub osc: its octave and wave, switches with their LEDs) and the
 * MIXER (Osc 1, Osc 2, Sub Osc and what the Ext / Ring / Noise switch picks), where the Bass Station II has them */
static void mixer_draw(void)
{
    static const int32_t EDGE[2] = {64, 233};
    static const char *const SRC[NMS] = {"Noise", "Ring", "Ext"};
    int32_t i;
    panel_bg();
    panel_bars(2, 236, EDGE, 2);
    bar_text(134, 1, "MIXER");
    cv_rect(4, 19, 46, 15, P_BAR);           /* the sub osc's own bar, its line down */
    cv_rect(4, 19, 3, 132, P_BAR);
    cv_text(10, 19, &FONT_XS, "Sub Osc", P_SILK);
    for (i = 0; i < 2; i++) {                /* the octave: -1 at the top */
        led(16, 49 + i * 18, (patch.sub_oct == 2u) == i);
        cv_text(23, 42 + i * 18, &FONT_XS, i ? "- 2" : "- 1", P_SILK);
    }
    slide_v(40, 42, 2, patch.sub_oct == 2u, knob_lit(EN_SELECT));
    silk_text(32, 78, "Octave", P_SILK);
    for (i = 0; i < NSUBW; i++) {            /* the wave: sine, narrow pulse, square */
        led(16, 107 + i * 16, patch.sub_wave == (uint32_t)i);
        wave_icon(24, 104 + i * 16, (uint32_t)i);
    }
    slide_v(40, 104, NSUBW, patch.sub_wave, knob_lit(EN_ALGO));
    panel_knob(104, 47, EN_K1, "Osc 1");
    panel_knob(199, 47, EN_K2, "Osc 2");
    panel_knob(104, 111, EN_PRESET, "Sub Osc");
    panel_knob(199, 111, EN_K4, 0);
    slide_v(142, 56, NMS, NMS - 1 - ui.mix_src, knob_lit(EN_K3));   /* Ext at the top, Noise at the bottom */
    for (i = 0; i < NMS; i++)
        cv_text(144, 99 + (NMS - 1 - i) * 12, &FONT_XS, SRC[i], P_SILK);
    cv_rect(151, 142, 1, 9, P_SILK);         /* the bracket from the names to their knob */
    cv_rect(151, 150, 47, 1, P_SILK);
    cv_rect(197, 142, 1, 9, P_SILK);
}

static void print_c(int32_t cx, int32_t y, const char *s, uint16_t c)   /* small print centred on cx */
{
    cv_text(cx - text_w(&FONT_T, s) / 2, y, &FONT_T, s, c);
}
/* a panel knob with its print under it (two lines: l2 may be 0), its value in its place, cyan, while it turns: encoder
 * k's setting q, mine when this view's (else dim) */
static void print_knob(int32_t cx, int32_t cy, int32_t r, const param_t *q, uint32_t k, int mine, const char *l1,
                       const char *l2)
{
    char t[12];
    uint32_t lit = mine && knob_lit(k);
    int32_t ly = cy + r + 4;
    bs2_knob(cx, cy, r, param_get(q, page_rec(ui.page)) - q->lo, q->hi - q->lo,
             (lit ? KF_LIT : 0u) | (q->lo < 0 ? KF_BIP : 0u) | (mine ? 0u : KF_DIM));
    if (lit) {
        knob_text(k, t);
        print_c(cx, ly, t, P_CYAN);
        return;
    }
    print_c(cx, ly, l1, mine ? P_SILK : P_DIM);
    if (l2)
        print_c(cx, ly + 9, l2, mine ? P_SILK : P_DIM);
}
/* the FILTERS (the Bass Station II's): the Type, Slope and Shape switches with their LEDs (Slope and Shape the Classic
 * filter's, the arm from Classic to them), Overdrive and Resonance, the Mod Env and LFO 2 depths bracketed to the big
 * Frequency knob (the key tracking: the group's mods). Two views: PG_TUNE the filter's, PG_FMOD its
 * modulation's (LFO 2) */
static void filters_draw(void)
{
    static const int32_t EDGE[1] = {2};
    static const char *const SH[3] = {"LP", "BP", "HP"};
    const int md = ui.page == PG_FMOD, acid = patch.filter == FLT_ACID;
    const uint32_t f = patch.filter, shape = acid ? 0u : f / 2u, d24 = acid || !(f & 1u);   /* (Acid's: as it rests) */
    uint16_t on = md ? P_DIM : P_SILK, cl = md || acid ? P_DIM : P_SILK;
    uint32_t i, fl = md ? KF_DIM : 0u;
    panel_bg();
    panel_bars(2, 236, EDGE, 1);
    bar_text((10 + 236 - text_w(&FONT_B, "FILTERS")) / 2, 1, "FILTERS");
    cv_rect(7, 47, 1, 75, cl);               /* the arm: Classic's, to its slope and shape */
    cv_rect(7, 47, 4, 1, cl);
    cv_rect(7, 90, 4, 1, cl);
    cv_rect(7, 121, 14, 1, cl);
    print_c(45, 15, "Type", on);             /* Type (SELECT): Classic, Acid, an LED either side */
    print_c(22, 26, "Classic", on);
    print_c(70, 26, "Acid", on);
    led(15, 47, !acid);
    slide_h(29, 38, 2, acid, fl | (!md && knob_lit(EN_SELECT) ? KF_LIT : 0u));
    led(75, 47, acid);
    print_c(45, 58, "Slope", cl);            /* Slope (ALGO): 12 dB, 24 dB */
    print_c(22, 69, "12dB", cl);
    print_c(70, 69, "24dB", cl);
    led(15, 90, !acid && !d24);
    slide_h(29, 81, 2, (int32_t)d24, (md || acid ? KF_DIM : 0u) | (!md && knob_lit(EN_ALGO) ? KF_LIT : 0u));
    led(75, 90, !acid && d24);
    print_c(45, 101, "Shape", cl);           /* Shape (PRESET): LP, BP, HP */
    slide_h(24, 112, 3, (int32_t)shape, (md || acid ? KF_DIM : 0u) | (!md && knob_lit(EN_PRESET) ? KF_LIT : 0u));
    for (i = 0; i < 3u; i++) {
        led(28 + 17 * (int32_t)i, 137, !acid && shape == i);
        print_c(28 + 17 * (int32_t)i, 143, SH[i], cl);
    }
    print_knob(102, 40, 15, CCP(PG_TUNE, EN_K1), EN_K1, !md, "Overdrive", 0);
    print_knob(150, 40, 15, CCP(PG_TUNE, EN_K2), EN_K2, !md, "Resonance", 0);
    print_knob(102, 104, 15, CCP(PG_TUNE, EN_K3), EN_K3, !md, "Mod Env", "depth");
    print_knob(150, 104, 15, CCP(PG_FMOD, EN_K1), EN_K1, md, "LFO 2", "depth");
    cv_rect(82, 84, 5, 1, P_SILK);           /* the depths' brackets, and their line to the Frequency */
    cv_rect(82, 84, 1, 50, P_SILK);
    cv_rect(82, 133, 5, 1, P_SILK);
    cv_rect(166, 84, 5, 1, P_SILK);
    cv_rect(170, 84, 1, 50, P_SILK);
    cv_rect(166, 133, 5, 1, P_SILK);
    cv_rect(121, 104, 10, 1, P_SILK);
    cv_rect(171, 104, 12, 1, P_SILK);
    print_knob(207, 52, 24, CCP(PG_TUNE, EN_K4), EN_K4, !md, "Frequency", 0);
}
/* a fader at cx, its slot from y0 down h, at v of 0..span: the print's scale across it (a long line each half), a
 * dark slot split down its middle, a cap shaded across with a dark top and its white line (cyan while it moves) */
static void fader(int32_t cx, int32_t y0, int32_t h, int32_t v, int32_t span, uint32_t f)
{
    int32_t i, cy = y0 + h - 7 - (h - 14) * v / span;
    uint16_t silk = f & KF_DIM ? P_DIM : P_SILK;
    for (i = 0; i <= 10; i++)                /* the scale */
        cv_rect(cx - 13, y0 + 3 + (h - 7) * i / 10, 26, i % 5 ? 1 : 2, silk);
    cv_rect(cx - 5, y0, 10, h, grey(30));
    cv_rect(cx, y0, 1, h, grey(24));
    cv_rect(cx - 8, cy - 6, 19, 14, P_KSHADOW);   /* the cap: its shadow, its sides lit, its top */
    for (i = 0; i < 18; i++) {
        int32_t m = i < 17 - i ? i : 17 - i;
        cv_rect(cx - 9 + i, cy - 7, 1, 14, grey(f & KF_DIM ? 30 : 50 - 20 * (m > 4 ? 4 : m) / 4));
    }
    cv_rect(cx - 9, cy - 7, 18, 10, grey(20));
    cv_rect(cx - 9, cy - 3, 18, 2, f & KF_LIT ? P_CYAN : silk);
}
/* the ENVELOPES' Env Select (SELECT, on each of the group's pages): the switch, its names to its left; level with the
 * faders' middle (their slot 22..132) */
static void env_select(void)
{
    static const char *const SEL[3] = {"Amp Env", "Mod Env", "Amp+Mod"};
    uint32_t i;
    print_c(37, 40, "Env Select", P_SILK);
    for (i = 0; i < 3u; i++)
        cv_text(46 - text_w(&FONT_T, SEL[i]), 56 + 14 * (int32_t)i, &FONT_T, SEL[i], i == ui.env_sel ? P_SILK : P_DIM);
    slide_v(48, 56, 3, ui.env_sel, knob_lit(EN_SELECT) ? KF_LIT : 0u);
}
/* the ENVELOPES (the Bass Station II's): Env Select at the left (SELECT: Amp, Mod, Amp+Mod; what the faders set), the
 * Attack, Decay, Sustain and Release faders (K1 to K4). (Triggering: its own page, trig_draw; the velocities: the
 * group's mods) */
static void env_draw(void)
{
    static const int32_t EDGE[1] = {2};
    static const char *const ST[4] = {"Attack", "Decay", "Sustain", "Release"};
    uint32_t i;
    char t[12];
    panel_bg();
    panel_bars(2, 236, EDGE, 1);
    bar_text((10 + 236 - text_w(&FONT_B, "ENVELOPES")) / 2, 1, "ENVELOPES");
    env_select();
    cv_rect(66, 77, 6, 1, P_SILK);           /* its line to the faders */
    for (i = 0; i < 4u; i++) {               /* the faders: K1 to K4, the envelope it picks (Amp+Mod: the amp's) */
        const param_t *q = knob_param(EN_K1 + i);
        int32_t cx = 92 + 40 * (int32_t)i;
        uint32_t lit = knob_lit(EN_K1 + i);
        fader(cx, 22, 110, param_get(q, &patch), 127, lit ? KF_LIT : 0u);
        if (lit) {
            knob_text(EN_K1 + i, t);
            print_c(cx, 138, t, P_CYAN);
        } else
            print_c(cx, 138, ST[i], P_SILK);
    }
}
/* the ENVELOPES' Triggering, the Bass Station II's switch (ALGO): Autoglide, Single, Multi (top to bottom), an LED at
 * each; of the envelope Env Select picks (Amp+Mod: both, the amp's shown), as on a Bass Station II */
static void trig_draw(void)
{
    static const int32_t EDGE[1] = {2};
    static const char *const TRIG[NENVT] = {"Multi", "Single", "Autoglide"};
    const uint32_t v = (uint32_t)param_get(knob_param(EN_ALGO), &patch);
    uint32_t i;
    panel_bg();
    panel_bars(2, 236, EDGE, 1);
    bar_text((10 + 236 - text_w(&FONT_B, "ENVELOPES")) / 2, 1, "ENVELOPES");
    env_select();
    cv_rect(66, 77, 60, 1, P_SILK);          /* Env Select's line to the switch it sets */
    print_c(150, 40, "Triggering", P_SILK);
    slide_v(126, 56, 3, (int32_t)(NENVT - 1u - v), knob_lit(EN_ALGO) ? KF_LIT : 0u);
    for (i = 0; i < NENVT; i++) {            /* (top to bottom: Autoglide, Single, Multi) */
        uint32_t w = NENVT - 1u - i;
        led(155, 67 + 10 * (int32_t)i, v == w);
        cv_text(162, 62 + 10 * (int32_t)i, &FONT_T, TRIG[w], v == w ? P_SILK : P_DIM);
    }
}
/* the mods' pages: a plate screwed onto the panel, as a modification to a Bass Station II would be: brushed aluminium,
 * its edges bevelled, a screw at each corner, its labels Dymo tape (white letters pressed into black); its knobs the
 * panel's, K1 on, the encoders the group's others are */
static void dymo(int32_t cx, int32_t y, const char *s, uint16_t c)   /* a strip of label tape centred on cx */
{
    int32_t w = text_w(&FONT_T, s) + 10, x = cx - w / 2;
    cv_rect(x + 1, y + 1, w, 12, grey(90));  /* (its shadow on the metal) */
    cv_rect(x, y, w, 12, RGB(16, 16, 20));
    cv_rect(x, y, w, 1, grey(60));
    cv_text(x + 5, y, &FONT_T, s, c);
}
static void screw(int32_t cx, int32_t cy)    /* a pan head, its cross */
{
    disc(cx + 1, cy + 1, 4, grey(80));
    shade_disc(cx, cy, 4, 175, 70);
    cv_rect(cx - 2, cy, 5, 1, grey(70));
    cv_rect(cx, cy - 2, 1, 5, grey(70));
}
static void plate(const char *grp)          /* a group's bar, the plate under it */
{
    static const int32_t EDGE[1] = {2};
    int32_t y;
    panel_bg();
    panel_bars(2, 236, EDGE, 1);
    bar_text((10 + 236 - text_w(&FONT_B, grp)) / 2, 1, grp);
    cv_rect(14, 26, 220, 124, grey(8));      /* the plate: its shadow, brushed across, its edges */
    for (y = 0; y < 124; y++) {
        uint32_t h = (uint32_t)y * 2654435761u >> 27;
        cv_rect(10, 22 + y, 220, 1, grey(158 + (int32_t)(h % 16u) - (y % 3 == 0 ? 6 : 0)));
    }
    cv_rect(10, 22, 220, 1, grey(225));
    cv_rect(10, 22, 1, 124, grey(205));
    cv_rect(10, 145, 220, 1, grey(90));
    cv_rect(229, 22, 1, 124, grey(100));
    screw(18, 30);
    screw(221, 30);
    screw(18, 137);
    screw(221, 137);
}
static void mod_draw(void)
{
    static const char *const LBL[4][4] = {{"KEY TRACK", "LIMITER"}, {"VELOCITY", "RETRIGGER", "COUNT", "FIXED SUS"},
                                          {"KEYSYNC", "SPEED/SYNC", "SLEW"}, {"SEQ RETRIG", "SWING"}};
    static const char *const ENV_T[3] = {"ENV MOD: AMP ENV", "ENV MOD: MOD ENV", "ENV MOD: AMP+MOD"};
    const int env = ui.page == PG_EMOD ? 1 : ui.page == PG_LFO2 ? 2 : ui.page == PG_AMOD ? 3 : 0;   /* (0: PG_XMOD) */
    const char *grp = env == 1 ? "ENVELOPES" : env == 2 ? "LFOS" : env ? "ARPEGGIATOR" : "FILTERS + EFFECTS";
    uint32_t i, n = 0;
    char t[12];
    plate(grp);
    /* its title; the ENV group's: which envelope it sets, as Env Select is (SELECT), lit while it moves */
    dymo(120, 30, env == 1 ? ENV_T[ui.env_sel] : env == 2 ? (ui.lfo_sel ? "LFO MOD: LFO 2" : "LFO MOD: LFO 1")
                  : env ? "ARP MODS" : "FX MODS",
         (env == 1 || env == 2) && knob_lit(EN_SELECT) ? P_CYAN : RGB(235, 235, 235));
    for (i = EN_K1; i <= EN_K4; i++)
        n += knob_param(i)->name != 0;
    for (i = 0; i < n; i++) {                /* its knobs, K1 on: each one's tape, its setting printed under it */
        const param_t *q = knob_param(EN_K1 + i);
        int32_t cx = 10 + 220 * (2 * (int32_t)i + 1) / (2 * (int32_t)n);
        uint32_t lit = knob_lit(EN_K1 + i);
        bs2_knob(cx, 76, n > 2u ? 16 : 20, param_get(q, &patch) - q->lo, q->hi - q->lo,
                 KF_PLATE | (lit ? KF_LIT : 0u) | (q->lo < 0 ? KF_BIP : 0u));
        dymo(cx, 101, LBL[env][i], RGB(235, 235, 235));
        knob_text(EN_K1 + i, t);
        print_c(cx, 117, t, lit ? RGB(20, 96, 140) : grey(36));   /* (the cyan darker on the metal) */
    }
}
static void osc_wave_icon(int32_t x, int32_t y, uint32_t w, uint16_t c);
/* an LFO's waves as printed by its LEDs, 8 x 7 from x, y: LFO_* (the saw falling, the S&H steps) */
static void lfo_wave_icon(int32_t x, int32_t y, uint32_t w, uint16_t c)
{
    switch (w) {
    case LFO_SAW:
        cv_rect(x, y, 1, 7, c);
        cv_line(x, y, x + 7, y + 6, c);
        return;
    case LFO_SQUARE:
        osc_wave_icon(x, y, WAVE_SQUARE, c);
        return;
    case LFO_SH:                             /* (steps: up, down, up) */
        cv_rect(x, y + 4, 3, 1, c);
        cv_rect(x + 2, y, 1, 5, c);
        cv_rect(x + 2, y, 3, 1, c);
        cv_rect(x + 4, y, 1, 7, c);
        cv_rect(x + 4, y + 6, 4, 1, c);
        return;
    default:
        osc_wave_icon(x, y, WAVE_TRI, c);
    }
}
/* PORTA and the LFOS (the Bass Station II's): the Glide Time knob (SELECT); each LFO's wave LEDs (K1, K4: its button;
 * the lit one blinking with it) and its knob (K2, K3), the Delay/Speed switch between them (ALGO), its lines to
 * them; under each knob what it sets now */
static void lfos_draw(void)
{
    static const int32_t EDGE[2] = {2, 58};
    uint32_t i, j;
    char t[12];
    panel_bg();
    panel_bars(2, 236, EDGE, 2);
    bar_text((10 + 58 - text_w(&FONT_B, "PORTA")) / 2, 1, "PORTA");
    bar_text((64 + 236 - text_w(&FONT_B, "LFOS")) / 2, 1, "LFOS");
    print_knob(31, 62, 16, CCP(PG_LFO1, EN_SELECT), EN_SELECT, 1, "Glide", "Time");
    for (i = 0; i < NLFO; i++) {
        const param_t *q = knob_param(EN_K2 + i);
        uint32_t w = patch.lfo[i].wave, lit = knob_lit(EN_K2 + i), on = lfo[i].out * lfo[i].fade > 0.0f;
        int32_t kx = i ? 182 : 114, lx = i ? 224 : 70, ix = i ? 207 : 77;
        for (j = 0; j < NLFOW; j++) {        /* its waves: an LED each, the icon beside (LFO 2's mirrored) */
            led(lx, 32 + 13 * (int32_t)j, j == w && on);
            if (j == w && !on)               /* (the lit one dark while the LFO is under 0: it blinks) */
                disc(lx, 32 + 13 * (int32_t)j, 1, P_LEDON);
            lfo_wave_icon(ix, 29 + 13 * (int32_t)j, j, knob_lit(i ? EN_K4 : EN_K1) && j == w ? P_CYAN : P_SILK);
        }
        bs2_knob(kx, 54, 17, param_get(q, &patch) - q->lo, q->hi - q->lo, lit ? KF_LIT : 0u);
        print_c(kx, 75, i ? "LFO 2" : "LFO 1", P_SILK);
        knob_text(EN_K2 + i, t);             /* (what it sets: its speed, sync value or delay) */
        print_c(kx, 84, t, lit ? P_CYAN : P_DIM);
        cv_rect(kx, 96, 1, 16, P_SILK);      /* its line to the switch */
        cv_rect(i ? 157 : kx, 111, i ? kx - 156 : 139 - kx, 1, P_SILK);
    }
    print_c(148, 86, "Delay", ui.lfo_delay ? P_SILK : P_DIM);   /* the Delay/Speed switch */
    slide_v(139, 96, 2, !ui.lfo_delay, knob_lit(EN_ALGO) ? KF_LIT : 0u);
    print_c(148, 131, "Speed", ui.lfo_delay ? P_DIM : P_SILK);
}
/* the EFFECTS (the Bass Station II's): Distortion (K1) and Osc Filter Mod (K2), its two knobs */
static void effects_draw(void)
{
    static const int32_t EDGE[1] = {2};
    panel_bg();
    panel_bars(2, 236, EDGE, 1);
    bar_text((10 + 236 - text_w(&FONT_B, "EFFECTS")) / 2, 1, "EFFECTS");
    print_knob(72, 72, 24, CCP(PG_EFX, EN_K1), EN_K1, 1, "Distortion", 0);
    print_knob(168, 72, 24, CCP(PG_EFX, EN_K2), EN_K2, 1, "Osc Filter Mod", 0);
}
/* a seven-segment digit at x, y (16 x 28), the Bass Station II display's red: c a character (0..9, A..F, a few
 * letters more, -, space); its segments' ghosts (all 8s) dim under it, its lit ones glowing */
static void seg7(int32_t x, int32_t y, char c)
{
    static const char CH[] = "0123456789ABCDEFP-LnOtS";
    static const uint8_t SEG[sizeof CH - 1] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C,
                                               0x39, 0x5E, 0x79, 0x71, 0x73, 0x40, 0x38, 0x54, 0x3F, 0x78,
                                               0x6D};   /* gfedcba */
    static const int8_t R[7][4] = {{3, 0, 10, 3}, {13, 2, 3, 11}, {13, 15, 3, 11}, {3, 25, 10, 3},
                                   {0, 15, 3, 11}, {0, 2, 3, 11}, {3, 12, 10, 3}};   /* a..g: x, y, w, h */
    uint32_t m = 0, i;
    for (i = 0; CH[i]; i++)
        if (CH[i] == c)
            m = SEG[i];
    for (i = 0; i < 7u; i++) {
        const int8_t *r = R[i];
        if (m >> i & 1u) {
            cv_rect(x + r[0] - 1, y + r[1] - 1, r[2] + 2, r[3] + 2, RGB(90, 18, 34));   /* (its glow) */
            cv_rect(x + r[0], y + r[1], r[2], r[3], RGB(252, 78, 112));
        } else
            cv_rect(x + r[0], y + r[1], r[2], r[3], RGB(34, 22, 26));
    }
}
static void panel_button(int32_t x, int32_t y, int32_t w, int32_t h, const char *name, int lit, int turning);
/* the Bass Station II's Org. Value arrows: the way the control last changed must turn to its value saved in the
 * patch, +1 right (up), -1 left (down); 0 when it is there, or none changed */
static int org_dir(void)
{
    const param_t *q = ui.org;
    int32_t v, o;
    if (!q)
        return 0;
    v = param_get(q, &patch);
    o = param_get(q, &bank.p[set.slot]);
    return v < o ? 1 : v > o ? -1 : 0;
}
/* the MASTER (the Bass Station II's, the first page): the Volume (the FM-1's MASTER knob, shown), the name; the display
 * (the patch, P01..P32, its dot lit while it has changes not saved), the Save button and the Patch buttons (PRESET
 * picks; SAVE keeps), the Org. Value LEDs and their arrows (org_dir) */
static void master_draw(void)
{
    static const int32_t EDGE[1] = {2};
    char t[4];
    int32_t i;
    uint32_t vol = plat_master();
    panel_bg();
    panel_bars(2, 236, EDGE, 1);
    bar_text((10 + 236 - text_w(&FONT_B, "MASTER")) / 2, 1, "MASTER");
    bs2_knob(44, 56, 20, (int32_t)vol, 4095, 0u);   /* Volume */
    print_c(44, 80, "Volume", P_SILK);
    cv_text(8, 106, &FONT_B, "BASS", P_SILK);   /* the name, where a Bass Station II has its maker's */
    cv_text(cv_text(8, 122, &FONT_B, "SOMETHING", P_SILK) + 5, 122, &FONT_B, "2", P_BAR);
    cv_rect(96, 22, 136, 50, RGB(4, 4, 4));  /* the display: black, its window, three digits */
    cv_rect(100, 26, 72, 42, RGB(20, 16, 20));
    t[0] = 'P';
    slot_text(t + 1);
    if (ui.org_at) {                         /* (a control just changed: its value, as the Bass Station II's, -63..127) */
        const param_t *q = ui.org;
        int32_t v = param_get(q, &patch), j = 3;
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        t[0] = t[1] = t[2] = ' ';
        do
            t[--j] = (char)('0' + a % 10u);
        while ((a /= 10u) && j);
        if (v < 0 && j)
            t[j - 1] = '-';
    }
    seg7(104, 33, t[0]);
    seg7(126, 33, t[1]);
    seg7(148, 33, t[2]);
    if (set_dirty)                           /* (changes not saved: its dot) */
        disc(168, 62, 2, RGB(252, 78, 112));
    for (i = 0; i < 5; i++) {                /* the arrows, right over left, each its LED beside it */
        cv_rect(194 + i, 32 + i, 1, 9 - 2 * i, P_DIM);
        cv_rect(194 + i, 56 - i, 1, 1 + 2 * i, P_DIM);
    }
    led(214, 36, org_dir() > 0);
    led(214, 56, org_dir() < 0);
    cv_rect(140, 82, 88, 1, P_SILK);         /* Patch: its line over its buttons, the name in it */
    cv_rect(170, 78, 28, 9, grey(25));
    print_c(184, 76, "Patch", P_SILK);
    panel_button(96, 92, 36, 30, "Save", 0, 0);   /* the buttons: Save (unlit: the display's dot says), Patch down, up */
    cv_rect(140, 92, 40, 30, P_KSHADOW);
    cv_rect(142, 94, 36, 26, knob_lit(EN_PRESET) && ui.pdir < 0 ? P_CDIM : grey(34));   /* (lit: the way it went) */
    cv_rect(188, 92, 40, 30, P_KSHADOW);
    cv_rect(190, 94, 36, 26, knob_lit(EN_PRESET) && ui.pdir > 0 ? P_CDIM : grey(34));
    cv_line(151, 107, 167, 101, P_SILK);     /* their arrows */
    cv_line(151, 107, 167, 113, P_SILK);
    cv_line(167, 101, 167, 113, P_SILK);
    cv_line(217, 107, 201, 101, P_SILK);
    cv_line(217, 107, 201, 113, P_SILK);
    cv_line(201, 101, 201, 113, P_SILK);
    cv_rect(140, 134, 88, 1, P_SILK);        /* Value: the line under them, the name in it */
    cv_rect(170, 130, 28, 9, grey(25));
    print_c(184, 128, "Value", P_SILK);
}
/* a panel button at x, y (w x h): its shadow, its face (lit: red, as the Bass Station II's are), its name on it */
static void panel_button(int32_t x, int32_t y, int32_t w, int32_t h, const char *name, int lit, int turning)
{
    cv_rect(x, y, w, h, P_KSHADOW);
    cv_rect(x + 2, y + 2, w - 4, h - 4, lit ? RGB(150, 26, 44) : turning ? P_CDIM : grey(34));
    print_c(x + w / 2, y + (h - 12) / 2, name, P_SILK);
}
/* a display of n digits at x, y (22n + 18 x 50): black, its window, the digits of t */
static void seg_display(int32_t x, int32_t y, const char *t, uint32_t n)
{
    uint32_t i;
    cv_rect(x, y, 22 * (int32_t)n + 18, 50, RGB(4, 4, 4));
    cv_rect(x + 4, y + 4, 22 * (int32_t)n + 10, 42, RGB(20, 16, 20));
    for (i = 0; i < n; i++)
        seg7(x + 9 + 22 * (int32_t)i, y + 11, t[i]);
}
static void seg_num(int32_t v, char *t)      /* v in three digits, right to left: "  5", "-12", "120" */
{
    int32_t j = 3;
    uint32_t a = (uint32_t)(v < 0 ? -v : v);
    t[0] = t[1] = t[2] = ' ';
    do
        t[--j] = (char)('0' + a % 10u);
    while ((a /= 10u) && j);
    if (v < 0 && j)
        t[j - 1] = '-';
}
static void cpu_text(char *t);
/* GLOBAL: the Bass Station II's Function settings, on the FM-1 a section of their own. Its bar, the page's own bar
 * under it at the left; at the right the audio's load (cpu_text: for testing), or what was just done while it's said */
static void glo_frame(const char *name)
{
    static const int32_t EDGE[1] = {2};
    char t[24];
    int32_t x;
    panel_bg();
    panel_bars(2, 236, EDGE, 1);
    bar_text((10 + 236 - text_w(&FONT_B, "GLOBAL")) / 2, 1, "GLOBAL");
    cv_rect(4, 19, text_w(&FONT_XS, name) + 12, 15, P_BAR);
    cv_text(10, 19, &FONT_XS, name, P_SILK);
    if (plat_ms() < ui.msg_until) {
        x = 232 - text_w(&FONT_XS, ui.msg[1]);
        cv_text(x, 20, &FONT_XS, ui.msg[1], P_CYAN);
        cv_text(x - 6 - text_w(&FONT_XS, ui.msg[0]), 20, &FONT_XS, ui.msg[0], P_SILK);
        return;
    }
    cpu_text(t);
    cv_text(232 - text_w(&FONT_XS, t), 20, &FONT_XS, t, plat_xruns() ? P_CYAN : P_DIM);
}
/* a setting's two-way switch lying down at cx, cy (its middle): its ends' names over it (the one set white), its name
 * under it (its setting instead, cyan, while it moves) */
static void print_switch(int32_t cx, int32_t cy, uint32_t k, const char *n0, const char *n1, const char *name)
{
    const param_t *q = knob_param(k);
    int32_t v = param_get(q, page_rec(ui.page)) - q->lo;
    int lit = knob_lit(k);
    char t[12];
    cv_text(cx - 16, cy - 20, &FONT_T, n0, v ? P_DIM : P_SILK);
    cv_text(cx + 17 - text_w(&FONT_T, n1), cy - 20, &FONT_T, n1, v ? P_SILK : P_DIM);
    slide_h(cx - 16, cy - 9, 2, v, lit ? KF_LIT : 0u);
    knob_text(k, t);
    print_c(cx, cy + 13, lit ? t : name, lit ? P_CYAN : P_SILK);
}
/* "Bluetooth  Connected": its name at x, what it is at x + w */
static void info(int32_t x, int32_t w, int32_t y, const char *name, const char *v, uint16_t c)
{
    cv_text(x, y, &FONT_T, name, P_SILK);
    cv_text(x + w, y, &FONT_T, v, c);
}
#define GLO_ROW 124                          /* the knobs' and switches' row */
/* GLOBAL MIDI: the channel on the display, its knob (K1); what goes out and Bluetooth; CC Chan (SELECT), Local, AT Out
 * (K2, K3: switches) and Bluetooth (K4) along the bottom */
static void glo_midi_draw(void)
{
    char t[4], o[8] = "Ch ";
    glo_frame("MIDI");
    text_cat(o, CHAN_NAME[set.chan == CH_ALL || set.chan == CH_OFF ? 1u : set.chan]);
    if (set.chan == CH_OFF || set.chan == CH_ALL)
        memcpy(t, set.chan ? "ALL" : "OFF", 3);
    else
        seg_num(set.chan, t);
    seg_display(8, 40, t, 3);
    print_knob(118, 60, 15, knob_param(EN_K1), EN_K1, 1, "Channel", 0);
    info(146, 42, 46, "Out", o, P_CYAN);
    info(146, 42, 58, "CC", set.cc_chan == CH_ALL ? "Any" : set.cc_chan ? CHAN_NAME[set.cc_chan] : "Off", P_CYAN);
    info(146, 42, 70, "BLE", set.ble_off ? "Off" : plat_ble_connected() ? "Connected" : "On",
         !set.ble_off && plat_ble_connected() ? P_CYAN : P_SILK);
    print_knob(34, GLO_ROW, 14, knob_param(EN_SELECT), EN_SELECT, 1, "CC Chan", 0);
    print_switch(92, GLO_ROW, EN_K2, "Off", "On", "Local");
    print_switch(150, GLO_ROW, EN_K3, "Off", "On", "AT Out");
    print_switch(208, GLO_ROW, EN_K4, "On", "Off", "Bluetooth");
}
/* GLOBAL SYSTEM: the tempo on the display (dashes while MIDI clock leads), the Send Clk switch (SELECT), the clock
 * and the firmware; Tune, Sync (what the arp follows), the keys' Velocity curve and the knobs' response (K1-K4) along
 * the bottom */
static void glo_sys_draw(void)
{
    char t[4];
    if (ext_clock())
        memcpy(t, "---", 3);
    else
        seg_num(patch.tempo, t);
    glo_frame("System");
    seg_display(8, 40, t, 3);
    print_c(50, 92, ext_clock() ? (eng.ext_age < EXT_HOLD ? "MIDI clock" : "Waiting for clock") : "Tempo", P_SILK);
    print_switch(122, 64, EN_SELECT, "Off", "On", "Send Clk");
    info(152, 40, 52, "Clock", ext_clock() ? "MIDI in" : set.clk_out ? "Sending" : "Internal",
         ext_clock() || set.clk_out ? P_CYAN : P_SILK);
    info(152, 40, 64, "Version", VERSION_TEXT, P_CYAN);
    print_knob(34, GLO_ROW, 14, knob_param(EN_K1), EN_K1, 1, "Tune", 0);
    print_knob(92, GLO_ROW, 14, knob_param(EN_K2), EN_K2, 1, "Sync", 0);
    print_knob(150, GLO_ROW, 14, knob_param(EN_K3), EN_K3, 1, "Velocity", 0);
    print_knob(208, GLO_ROW, 14, knob_param(EN_K4), EN_K4, 1, "Knobs", 0);
}
/* GLOBAL DUMP: the patch on the display; SEL sends it, FX all 32, as the Bass Station II's SysEx (two buttons, as
 * the FM-1's are: FX at the left, SEL at the right) */
static void glo_dump_draw(void)
{
    char t[4];
    t[0] = 'P';
    slot_text(t + 1);
    glo_frame("SysEx Dump");
    seg_display(8, 40, t, 3);
    print_c(50, 92, "Patch", P_SILK);
    cv_text(118, 44, &FONT_T, "Bass Station II", P_SILK);
    cv_text(118, 54, &FONT_T, "patch dumps, over USB", P_SILK);
    cv_text(118, 70, &FONT_T, "GLO settings save", P_DIM);
    cv_text(118, 80, &FONT_T, "themselves", P_DIM);
    panel_button(30, 108, 72, 28, "All 32", 0, 0);
    print_c(66, 139, "FX", P_SILK);
    panel_button(138, 108, 72, 28, "Sound", 0, 0);
    print_c(174, 139, "SEL", P_SILK);
}
/* GLOBAL RND: the seed shown on an eight-digit display (dashes: none), where it came from; the slot (PRESET), Save
 * (SAVE: the seed into the slot) and Send (FX: to the RND Synth) */
static void glo_rnd_draw(void)
{
    char s[12];
    seed_text(rnd_cur, s);
    if (rnd_cur == RND_EMPTY)
        memcpy(s, "--------", 8);
    glo_frame("RND Seed");
    seg_display(8, 40, s, 8);
    print_c(110, 92, rnd_cur != RND_EMPTY && seeds.seed[rnd_slot] == rnd_cur ? "Saved in its slot"
                     : rnd_src == RS_SYNTH ? "From the RND Synth" : rnd_src == RS_SLOT ? "From its slot" : "No seed yet",
            P_SILK);
    bs2_knob(34, GLO_ROW, 14, rnd_slot, NSEEDS - 1, knob_lit(EN_PRESET) ? KF_LIT : 0u);
    seed_slot_text(s);
    print_c(34, GLO_ROW + 18, s, knob_lit(EN_PRESET) ? P_CYAN : P_SILK);
    panel_button(70, 110, 72, 28, "Save", 0, 0);
    print_c(106, 141, "SAVE", P_SILK);
    panel_button(156, 110, 72, 28, "Send", 0, 0);
    print_c(192, 141, "FX", P_SILK);
}
/* the ARPEGGIATOR (the Bass Station II's): Tempo (its LED on the beat) and Rhythm, the Note Mode knob with its modes
 * round it, Octaves (Arp Octaves, or the sequence: Seq); the On/Legato and Latch/Rest buttons (SELECT, ALGO), their
 * LEDs lit while the arp is on, latched */
static void arp_draw(void)
{
    static const int32_t EDGE[1] = {2};
    static const char *const MODES[NARPM] = {"Up", "Dn", "UpDn 1", "UpDn 2", "Played", "Random", "Play", "Record"};
    const int run = eng.src == SRC_ARP || eng.src == SRC_SEQ;
    uint32_t i;
    int32_t x, y;
    panel_bg();
    panel_bars(2, 236, EDGE, 1);
    bar_text((10 + 236 - text_w(&FONT_B, "ARPEGGIATOR")) / 2, 1, "ARPEGGIATOR");
    print_knob(30, 50, 16, CCP(PG_ARP, EN_K1), EN_K1, 1, "", "Tempo");   /* (its name under its range) */
    cv_text(4, 70, &FONT_T, "40", P_SILK);
    cv_text(46, 70, &FONT_T, "240", P_SILK);
    led(53, 34, run && eng.rpos % 4u == 0u);   /* (the tempo's LED: each quarter, playing) */
    print_knob(92, 50, 16, CCP(PG_ARP, EN_K2), EN_K2, 1, "", "Rhythm");
    cv_text(68, 70, &FONT_T, "1", P_SILK);
    cv_text(111, 70, &FONT_T, "32", P_SILK);
    print_knob(170, 52, 16, CCP(PG_ARP, EN_K3), EN_K3, 1, "", 0);   /* the Note Mode, its modes round it */
    for (i = 0; i < NARPM; i++) {            /* (the left's ending at the knob, the right's starting there) */
        pt_at(170, 52, 26, ((float)i / (float)(NARPM - 1u) - 0.5f) * (FM_PI * 5.0f / 3.0f), &x, &y);
        cv_text(i < NARPM / 2u ? x - text_w(&FONT_T, MODES[i]) : x, y - 6, &FONT_T, MODES[i],
                i == patch.arp_mode ? P_SILK : P_DIM);
    }
    {                                        /* Octaves, a switch: 1..4 at 7:30, 9, 10:30 and 12 o'clock, its left */
        const param_t *q = knob_param(EN_K4);   /* (Arp Octaves, or Seq) */
        uint32_t v = (uint32_t)(param_get(q, &patch) - q->lo), lit = knob_lit(EN_K4);
        knob_at(172, 120, 12, ((float)v / 3.0f - 1.0f) * (FM_PI * 3.0f / 4.0f), KF_NODOTS | (lit ? KF_LIT : 0u));
        for (i = 0; i < 4u; i++) {
            char d[2] = {(char)('1' + i), 0};
            pt_at(172, 120, 21, ((float)i / 3.0f - 1.0f) * (FM_PI * 3.0f / 4.0f), &x, &y);
            print_c(x, y - 5, d, lit && i == v ? P_CYAN : P_SILK);
        }
    }
    cv_text(196, 102, &FONT_T, "Arp", patch.arp_mode < ARP_PLAY ? P_SILK : P_DIM);
    cv_text(196, 113, &FONT_T, "Octaves", patch.arp_mode < ARP_PLAY ? P_SILK : P_DIM);
    cv_text(196, 125, &FONT_T, "Seq", patch.arp_mode >= ARP_PLAY ? P_SILK : P_DIM);
    led(12, 113, patch.arp_on);              /* On / Legato (SELECT), Latch / Rest (ALGO): their LEDs lit, as on the panel */
    panel_button(22, 103, 40, 20, "On", 0, knob_lit(EN_SELECT));
    print_c(42, 127, "Legato", P_SILK);
    panel_button(72, 103, 40, 20, "Latch", 0, knob_lit(EN_ALGO));
    led(122, 113, patch.latch);
    print_c(92, 127, "Rest", P_SILK);
}
/* the sequencer (SEQ): a mod, its plate a 32-step display: the arp's rhythm over it (its 32 16ths: a note lit, held on
 * a dash, a rest dark; where it is white), the sequence under (each step its note's height, the sequence's lowest to
 * its highest; legato joined to the next; past its length dark; playing white, the cursor marked cyan); the sequence,
 * its length and the cursor on top, the step's note, legato, the gate and the rhythm under */
static void seq_draw(void)
{
    const char *r = RHYTHM[patch.rhythm - 1u];
    uint32_t i, c = cursor(), len = patch.sq.len;
    uint32_t rnow = eng.src == SRC_ARP || eng.src == SRC_SEQ ? eng.rpos % RHY_STEPS : RHY_STEPS;
    uint32_t snow = eng.src == SRC_SEQ ? eng.scur : NSTEPS;
    int32_t lo = 127, hi = 0;
    char t[16], v[12];
    plate("SEQUENCER");
    for (i = 0; i < 3u; i++) {               /* on top: SEQ n (SELECT), LENGTH (ALGO), STEP (PRESET) */
        static const char *const N[3] = {"SEQ ", "LEN ", "STEP "};
        t[0] = 0;
        text_cat(t, N[i]);
        knob_text(EN_SELECT + i, v);
        text_cat(t, v);
        dymo(66 + 54 * (int32_t)i, 27, t, knob_lit(EN_SELECT + i) ? P_CYAN : RGB(235, 235, 235));
    }
    cv_rect(16, 44, 208, 62, RGB(6, 4, 6));  /* the display: dark, an LED's red */
    for (i = 0; i < len; i++)
        if (patch.sq.step[i] != SEQ_REST) {
            lo = patch.sq.step[i] < lo ? patch.sq.step[i] : lo;
            hi = patch.sq.step[i] > hi ? patch.sq.step[i] : hi;
        }
    for (i = 0; i < 32u; i++) {
        int32_t x = 24 + 6 * (int32_t)i, h, s = patch.sq.step[i];
        uint16_t on = RGB(252, 78, 112), dim = RGB(110, 30, 45), off = RGB(40, 18, 24);
        if (i == rnow)                       /* the rhythm: a note, a held one, a rest */
            cv_rect(x, 48, 5, 4, P_SILK);
        else if (r[i] == 'x')
            cv_rect(x, 48, 5, 4, on);
        else if (r[i] == '-')
            cv_rect(x - 1, 49, 7, 2, dim);
        else
            cv_rect(x + 2, 50, 1, 1, off);
        if (i >= len) {                      /* the step: past the length, a rest, a note */
            cv_rect(x, 99, 5, 1, off);
            continue;
        }
        if (s == SEQ_REST) {
            cv_rect(x + 1, 98, 3, 2, i == snow ? P_SILK : dim);
        } else {
            h = 4 + (hi > lo ? (s - lo) * 38 / (hi - lo) : 19);
            cv_rect(x, 100 - h, 5, h, i == snow ? P_SILK : on);
            if (seq_leg(&patch.sq, i) && i + 1u < len)
                cv_rect(x + 5, 100 - h, 1, 2, i == snow ? P_SILK : on);
        }
        if (i == c)
            cv_rect(x, 102, 5, 2, P_CYAN);
    }
    for (i = 0; i < 4u; i++) {               /* under: NOTE (K1), LEGATO (K2), GATE (K3), RHYTHM (K4) */
        static const char *const N[4] = {"", "LEG ", "GATE ", "RHY "};
        t[0] = 0;
        text_cat(t, N[i]);
        knob_text(EN_K1 + i, v);
        text_cat(t, v);
        dymo(41 + 52 * (int32_t)i, 113, t, knob_lit(EN_K1 + i) ? P_CYAN : RGB(235, 235, 235));
    }
}
/* an oscillator's waves as printed over its LEDs, 8 x 7 from x, y: WAVE_* */
static void osc_wave_icon(int32_t x, int32_t y, uint32_t w, uint16_t c)
{
    int32_t i, hi = w == WAVE_PULSE ? 2 : 4;
    switch (w) {
    case WAVE_SINE:
        for (i = 1; i <= 8; i++)
            cv_line(x + i - 1, y + 3 - round_px(3.0f * fm_sinf((float)(i - 1) * (FM_TWO_PI / 8.0f))), x + i,
                    y + 3 - round_px(3.0f * fm_sinf((float)i * (FM_TWO_PI / 8.0f))), c);
        return;
    case WAVE_TRI:
        cv_line(x, y + 6, x + 4, y, c);
        cv_line(x + 4, y, x + 8, y + 6, c);
        return;
    case WAVE_SAW:
        cv_line(x, y + 6, x + 7, y, c);
        cv_rect(x + 7, y, 1, 7, c);
        return;
    default:                                 /* the square, the pulse narrower */
        cv_rect(x, y, 1, 7, c);
        cv_rect(x, y, hi, 1, c);
        cv_rect(x + hi - 1, y, 1, 7, c);
        cv_rect(x + hi - 1, y + 6, 9 - hi, 1, c);
        cv_rect(x + 8, y, 1, 7, c);
    }
}
/* the OSCILLATORS (the Bass Station II's, without the sub osc: MIX FX's): five knobs (Coarse, Fine, the pitch's Mod
 * Env and LFO 1 depths, Pulse Width), the Range and the Waveform with their LEDs, Osc Select (SEL), Sync 1-2, the pulse
 * width's switch. Two views: PG_OSC the pitch's (the width dim), PG_MIX the width's (the rest dim) */
static void osc_draw(void)
{
    static const int32_t EDGE[1] = {2};
    static const char *const L1[5] = {"Coarse", "Fine", "Mod Env", "LFO 1", "Pulse"}, *const L2[5] = {0, 0, "depth", "depth", "Width"};
    static const char *const FT[NRANGE] = {"16'", "8'", "4'", "2'"};
    static const uint8_t SHOWN[NWAVES] = {WAVE_SINE, WAVE_TRI, WAVE_SAW, WAVE_SQUARE, WAVE_PULSE};   /* the BS2's order */
    const int pw = ui.page == PG_MIX;
    const param_t *q[5];
    const param_t *op = ui.osc_sel ? OSC2_P : PARAMS[PG_OSC], *pq = &PW_P[ui.osc_sel][ui.pw_src];
    uint32_t i, range = (uint32_t)param_get(&op[EN_SELECT], &patch), wave = (uint32_t)param_get(&op[EN_ALGO], &patch);
    uint16_t on = pw ? P_DIM : P_SILK, wo = pw ? P_SILK : P_DIM;   /* the pitch's print, the width's */
    char t[12];
    panel_bg();
    panel_bars(2, 236, EDGE, 1);
    bar_text((10 + 236 - text_w(&FONT_B, "OSCILLATORS")) / 2, 1, "OSCILLATORS");
    q[0] = &op[EN_K1], q[1] = &op[EN_K2], q[2] = &op[EN_K3], q[3] = &op[EN_K4], q[4] = pq;
    for (i = 0; i < 5u; i++) {               /* the knobs: K1 to K4 (the pitch's view), the width's K4 */
        int32_t cx = 29 + 46 * (int32_t)i, v = param_get(q[i], &patch);
        uint32_t k = i < 4u ? EN_K1 + i : EN_K4, mine = (i == 4u) == pw, lit = mine && knob_lit(k);
        bs2_knob(cx, 38, 16, v - q[i]->lo, q[i]->hi - q[i]->lo,
                 (lit ? KF_LIT : 0u) | (q[i]->lo < 0 ? KF_BIP : 0u) | (mine ? 0u : KF_DIM));
        if (lit) {                           /* its value, cyan, while it turns */
            knob_text(k, t);
            print_c(cx, 56, t, P_CYAN);
            continue;
        }
        print_c(cx, 56, L1[i], mine ? P_SILK : P_DIM);
        if (L2[i])
            print_c(cx, 65, L2[i], mine ? P_SILK : P_DIM);
    }
    cv_rect(100, 79, 1, 6, on);              /* Pitch: Mod Env and LFO 1 depth's bracket */
    cv_rect(188, 79, 1, 6, on);
    cv_rect(100, 84, 89, 1, on);
    cv_rect(144 - text_w(&FONT_T, "Pitch") / 2 - 3, 79, text_w(&FONT_T, "Pitch") + 6, 10, grey(25));
    print_c(144, 77, "Pitch", on);
    cv_text(6, 101, &FONT_T, "Range", on);   /* Range (SELECT): the footage's LEDs */
    for (i = 0; i < NRANGE; i++) {
        print_c(60 + 16 * (int32_t)i, 89, FT[i], on);
        led(60 + 16 * (int32_t)i, 107, range == i);
    }
    cv_text(6, 129, &FONT_T, "Waveform", on);   /* Waveform (ALGO) */
    for (i = 0; i < NWAVES; i++) {
        osc_wave_icon(56 + 14 * (int32_t)i, 120, SHOWN[i], on);
        led(60 + 14 * (int32_t)i, 137, wave == SHOWN[i]);
    }
    print_c(141, 90, "Osc Select", P_SILK);  /* Osc Select (SEL): both views */
    cv_text(116, 106, &FONT_T, "1", P_SILK);
    slide_h(124, 102, 2, ui.osc_sel, 0u);
    cv_text(161, 106, &FONT_T, "2", P_SILK);
    print_c(141, 124, "Sync 1-2", on);       /* Sync 1-2 (PRESET) */
    led(134, 143, patch.sync);
    cv_text(143, 137, &FONT_T, "On", on);
    if (!pw && knob_lit(EN_PRESET))
        disc(134, 143, 1, P_CYAN);
    cv_text(174, 90, &FONT_T, "Mod Env", wo);   /* the width's switch (PRESET): Mod Env, Manual, LFO 2 */
    cv_rect(211, 91, 1, 9, wo);
    cv_text(236 - text_w(&FONT_T, "LFO 2"), 90, &FONT_T, "LFO 2", wo);
    slide_h(184, 104, NPW, PW_POS[ui.pw_src], (pw && knob_lit(EN_PRESET) ? KF_LIT : 0u) | (pw ? 0u : KF_DIM));
    print_c(205, 125, "Manual", wo);
}

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
/* the audio's load on the GLO pages' bar (glo_frame), for testing: "CPU 34/41", now (the most of the last quarter second) and
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

static void head_draw(void)                  /* the section tabs (each page's panel under them) */
{
    int32_t x;
    uint32_t i;
    cv_begin(240, 20, P_BG);
    for (i = 0; i < NSEC; i++) {
        int on = PAGE_SEC[ui.page] == i, run = (i == SEC_ARP && patch.arp_on) || (i == SEC_SEQ && (seq_playing() || seq_rec));
        int32_t w = TAB_X(i + 1) - TAB_X(i) - 1;
        x = TAB_X(i);
        cv_rect(x, 1, w, 17, on ? P_BAR : P_CARD);   /* (the one you're in: the bars' cyan, its name white) */
        cv_text(x + (w - text_w(&FONT_XS, SEC_NAME[i])) / 2, 2, &FONT_XS, SEC_NAME[i],
                on ? P_SILK : run ? P_CYAN : P_LABEL);
    }
    cv_blit(0, 0);
}

static void panel_draw(void)                 /* the page's section panel, from under the tabs: in two halves (the
                                              * canvas holds 144 rows) */
{
    uint32_t i;
    for (i = 0; i < 2u; i++) {
        cv_begin(240, TALL_H / 2, P_BG);
        cv_oy = -(int32_t)i * (TALL_H / 2);
        if (ui.page == PG_FX)
            mixer_draw();
        else if (ui.page == PG_TUNE || ui.page == PG_FMOD)
            filters_draw();
        else if (ui.page == PG_AENV)
            env_draw();
        else if (ui.page == PG_MENV)
            trig_draw();
        else if (ui.page == PG_EMOD || ui.page == PG_LFO2 || ui.page == PG_XMOD || ui.page == PG_AMOD)
            mod_draw();
        else if (ui.page == PG_ARP)
            arp_draw();
        else if (ui.page == PG_SEQ)
            seq_draw();
        else if (ui.page == PG_EFX)
            effects_draw();
        else if (ui.page == PG_PATCH)
            master_draw();
        else if (ui.page == PG_LFO1)
            lfos_draw();
        else if (ui.page == PG_GMIDI)
            glo_midi_draw();
        else if (ui.page == PG_GSYS)
            glo_sys_draw();
        else if (ui.page == PG_GDUMP)
            glo_dump_draw();
        else if (ui.page == PG_GRND)
            glo_rnd_draw();
        else
            osc_draw();
        cv_blit(0, 20 + i * (TALL_H / 2));
    }
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

/* the battery under OCT: three bars (red, empty: none), a bolt on USB power (no charge line: charging or not), the
 * outline grey until the first reading */
static void battery_draw(int32_t x, int32_t y)
{
    int b = plat_battery(), i;
    uint16_t c = b < 0 ? P_EDGE : b == 0 ? P_LEDON : P_LABEL;
    cv_rect(x, y, 18, 1, c);
    cv_rect(x, y + 8, 18, 1, c);
    cv_rect(x, y, 1, 9, c);
    cv_rect(x + 17, y, 1, 9, c);
    cv_rect(x + 18, y + 3, 2, 3, c);
    if (b == 4) {                            /* a bolt */
        cv_line(x + 11, y + 2, x + 7, y + 4, P_CYAN);
        cv_line(x + 7, y + 4, x + 11, y + 4, P_CYAN);
        cv_line(x + 11, y + 4, x + 7, y + 6, P_CYAN);
        return;
    }
    for (i = 0; i < b && i < 3; i++)
        cv_rect(x + 2 + 5 * i, y + 2, 4, 5, P_CYAN);
}
/* what the footer shows of Bluetooth and the battery: a change redraws it */
static uint32_t ble_batt(void)
{
    return (uint32_t)(plat_ble_connected() | plat_ble_device() << 1 | (plat_battery() + 1) << 4);
}

static void keys_draw(void)                  /* the keys; the octave, the slot, Bluetooth, the volume, the battery,
                                              * MIDI in */
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
    battery_draw(5, 49);
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
        cv_text(32, 45, &FONT_XS, "MIDI IN", P_LABEL);
        cv_text(80, 45, &FONT_XS, midi_last_port < NPORTS ? PORT[midi_last_port] : "", P_CYAN);
        cv_text(110, 45, &FONT_XS, h, P_TEXT);
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
    case PG_AENV:
        return ui.env_sel == 1u ? voice.menv.stage : voice.amp.stage;
    case PG_LFO1:                            /* (its wave LEDs blink as the LFOs go) */
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
    if (ui.org_at && now - ui.org_at >= ORG_SHOW_MS) {   /* the MASTER's display: the patch again */
        ui.org_at = 0;
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
    if (pct != last_pct || ble_batt() != last_ble || (ui.msg_until && now >= ui.msg_until)) {
        last_pct = pct;
        last_ble = ble_batt();
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (cpu_meter(now) && ui.page >= PG_GMIDI && !ui.redraw)   /* (the panel alone: its load in the bar) */
        panel_draw();
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
    else if (!strcmp(what, "latch"))
        *got = patch.latch;
    else if (!strcmp(what, "retrig"))
        *got = patch.arp_retrig;
    else if (!strcmp(what, "swing"))
        *got = patch.swing;
    else if (!strcmp(what, "lvl1"))
        *got = patch.lvl1;
    else if (!strcmp(what, "lvl2"))
        *got = patch.lvl2;
    else if (!strcmp(what, "lvl_sub"))
        *got = patch.lvl_sub;
    else if (!strcmp(what, "lvl_ext"))
        *got = patch.lvl_ext;
    else if (!strcmp(what, "lfo1_o1"))
        *got = patch.lfo[0].depth[0];
    else if (!strcmp(what, "lfo1_o2"))
        *got = patch.lfo[0].depth[1];
    else if (!strcmp(what, "lfo2_cut"))
        *got = patch.lfo[1].depth[0];
    else if (!strcmp(what, "sub_wave"))
        *got = patch.sub_wave;
    else if (!strcmp(what, "range1"))
        *got = patch.range1;
    else if (!strcmp(what, "range2"))
        *got = patch.range2;
    else if (!strncmp(what, "lfo", 3) && (what[3] == '1' || what[3] == '2') && what[4] == '_') {
        /* lfo1_wave, lfo2_rate, lfo1_delay, lfo1_slew, lfo1_keysync, lfo1_sync, lfo1_syncv */
        uint32_t i = what[3] == '2';
        const char *f = what + 5;
        if (!strcmp(f, "wave"))
            *got = patch.lfo[i].wave;
        else if (!strcmp(f, "rate"))
            *got = patch.lfo[i].rate;
        else if (!strcmp(f, "delay"))
            *got = patch.lfo_delay[i];
        else if (!strcmp(f, "slew"))
            *got = patch.lfo_slew[i];
        else if (!strcmp(f, "keysync"))
            *got = patch.lfo_keysync[i];
        else if (!strcmp(f, "sync"))
            *got = patch.lfo_sync[i];
        else if (!strcmp(f, "syncv"))
            *got = patch.lfo_syncv[i];
        else if (!strcmp(f, "o1"))
            *got = patch.lfo[0].depth[0];
        else if (!strcmp(f, "o2"))
            *got = patch.lfo[0].depth[1];
        else if (!strcmp(f, "cut"))
            *got = patch.lfo[1].depth[0];
        else
            return 0;
    } else if (!strcmp(what, "eloopA") || !strcmp(what, "eloopM"))   /* the times an envelope retriggered */
        *got = what[5] == 'A' ? voice.amp.loops : voice.menv.loops;
    else if (!strncmp(what, "trig", 4) || !strncmp(what, "loop", 4) || !strncmp(what, "count", 5)
             || !strncmp(what, "fixed", 5)) {   /* trigA, trigM, loopA .. fixedM: the envelopes' modes */
        uint32_t i = what[strlen(what) - 1] == 'M';
        *got = what[0] == 't' ? patch.env_trig[i] : what[0] == 'l' ? patch.env_loop[i] : what[0] == 'c'
               ? patch.env_count[i] : patch.env_fixed[i];
    }
    else if (!strcmp(what, "menv_a"))
        *got = patch.menv_a;
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
    else if (!strcmp(what, "org_shown"))     /* the MASTER's display on a control's value, not the patch */
        *got = ui.org_at != 0u;
    else if (!strcmp(what, "org"))           /* the Org. Value arrow lit: +1 right, -1 left, 0 none */
        *got = org_dir();
    else if (!strcmp(what, "pdir"))          /* the MASTER's Patch button lit: -1 down, +1 up */
        *got = ui.pdir;
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
    else if (!strcmp(what, "knobs"))
        *got = set.knobs;
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
    else if (!strcmp(what, "pwm1"))
        *got = patch.pwm1;
    else if (!strcmp(what, "pwm2"))
        *got = patch.pwm2;
    else if (!strcmp(what, "menv_o1"))
        *got = patch.menv_o1;
    else if (!strcmp(what, "menv_o2"))
        *got = patch.menv_o2;
    else if (!strcmp(what, "menv_cut"))
        *got = patch.menv_cut;
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
    else if (!strcmp(what, "ofm"))
        *got = patch.ofm;
    else if (!strcmp(what, "lim"))
        *got = patch.lim;
    else if (!strcmp(what, "cents"))
        *got = cents_now;
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
            if (knob_param(k)->name && !strcmp(what, knob_param(k)->name)) {
                *got = param_get(knob_param(k), page_rec(ui.page));
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
            r[offsetof(bank_t, p) + i * ps + j] = ((const uint8_t *)&d)[j < SEQ_GAP || v >= 8u ? j : j + SEQ_GAP_N];
        if (v <= 7u)
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
    if (!strncmp(cmd, "bank_v", 6) && cmd[6] >= '3' && cmd[6] <= '8' && !cmd[7]) {   /* an older bank saved: a reboot
                                                                                    * then reads it as one */
        static const uint8_t PS[9] = {0, 0, 0, BANK_V3_PSIZE, BANK_V4_PSIZE, BANK_V5_PSIZE, BANK_V6_PSIZE, BANK_V7_PSIZE,
                                      BANK_V8_PSIZE};
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
        } else if (v == 8u) {                /* (slot 1's distortion 33 and step 3 legato: kept, its Ext 0; its LFO 1 on
                                              * the pitch at 20, LFO 2 on the cutoff at -15; slot 2's LFO 1 on
                                              * oscillator 2 at 30, LFO 2 on the pitch: none) */
            uint8_t *p0 = r + offsetof(bank_t, p), *p1 = p0 + ps;
            p0[offsetof(patch_t, dist)] = 33;
            p0[offsetof(patch_t, sq.leg[0])] = 1u << 2;
            p0[offsetof(patch_t, lfo[0].depth[0])] = DEST_PITCH;
            p0[offsetof(patch_t, lfo[0].depth[1])] = 20;
            p0[offsetof(patch_t, lfo[1].depth[0])] = DEST_CUTOFF;
            p0[offsetof(patch_t, lfo[1].depth[1])] = (uint8_t)-15;
            p1[offsetof(patch_t, lfo[0].depth[0])] = DEST_OSC2;
            p1[offsetof(patch_t, lfo[0].depth[1])] = 30;
            p1[offsetof(patch_t, lfo[1].depth[0])] = DEST_PITCH;
            p1[offsetof(patch_t, lfo[1].depth[1])] = 40;
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
