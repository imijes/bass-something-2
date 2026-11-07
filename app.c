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
    uint8_t spk;                         /* SPEAKER (GLO AUDIO): SPK_*, 0 FLAT (was a byte of padding, stored as 0) */
    uint8_t knobs;                       /* KN_*: how far a knob goes as it turns faster (knob_accel) */
    uint8_t hints;                       /* Hints (GLO DISPLAY): a few words on what each control does, as it's used */
    uint8_t pop;                         /* the pop-ups' time (GLO DISPLAY): pop_d's order, 1.5 s stored as 0 */
    uint8_t zoom_off;                    /* KNOB ZOOM (GLO DISPLAY): 0 AUTO, 1 OFF (was a spare byte, 0: AUTO in older
                                          * records) */
    uint8_t dchan;                       /* Drum Something's MIDI channel in and out (GLO DRUMS): as chan, its own: the
                                          * drums and the synth are two devices on one MIDI network */
    uint8_t dcc_chan;                    /* the drums' CCs (bs2_drum.c's dt_cc) in: as cc_chan */
    uint8_t dswing_lock;                 /* Swing Lock (DRUM MODS): the bass's swing the running pattern's (was spare, 0: off) */
    uint8_t dtrans_off;                  /* TRANSPORT (DRUM MODS) off: the drums' run and stop leave the bass alone (was spare,
                                          * 0: on, as MIDI's start and stop) */
    uint8_t droll;                       /* the drums' roll: its rate, DQ_ROLL's (older records: settings_defaults') */
    uint8_t spare4[3];
} settings_t;
/* the roll's rates, as other machines' note repeat: ticks (96 a bar), the fraction, a triplet's */
enum { DQ_NROLL = 9, DQ_ROLL_DEF = 5 };   /* (1/16) */
static const uint8_t DQ_ROLL[DQ_NROLL] = {48, 24, 16, 12, 8, 6, 4, 3, 2}, DQ_ROLL_N[DQ_NROLL] = {2, 4, 4, 8, 8, 16, 16, 32, 32},
                     DQ_ROLL_T[DQ_NROLL] = {0, 0, 1, 0, 1, 0, 1, 0, 1};
/* now, as a time kept with 0 for none: never ahead of the clock, or now - it wraps round to a very long time */
static uint32_t ms_stamp(void) { return (plat_ms() - 1u) | 1u; }
#define SET_V1_SIZE 8u                   /* the record before the GLO pages: magic to ble_off */
#define SET_V2_SIZE offsetof(settings_t, mem)   /* the record before the sequences (16 bytes stored: a byte of padding) */
#define SET_V3_SIZE offsetof(settings_t, knobs)  /* the record before the knobs' response */
#define SET_V4_SIZE offsetof(settings_t, dchan)  /* the record before the drums' MIDI */
enum { CH_OFF = 0, CH_ALL = 17 };
enum { CLK_AUTO, CLK_INT, CLK_EXT, NCLK };   /* MIDI clock followed when it comes; never; always (none: stopped) */
enum { VC_SOFT, VC_NORMAL, VC_HARD, NVC };
enum { KN_FINE, KN_NORMAL, KN_FAST, KN_COARSE, NKN };   /* the knobs' response: one step a click, to big strides */
/* (the app's big zero-initialised state is PLAT_POOL: out of the 96 KB of .data and .bss, which is short) */
static settings_t set PLAT_POOL;
static settings_t set_kept PLAT_POOL;    /* the record as it is in the store (glo_keep) */
#define GLO_FROM offsetof(settings_t, ble_off)   /* the GLO pages' settings: ble_off on; kept on their own */
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */

static const char *const WAVE_NAME[NWAVES] = {"SAW", "SQUARE", "SINE", "TRI", "PULSE"};
static const uint8_t WAVE_SHOWN[NWAVES] = {WAVE_SINE, WAVE_TRI, WAVE_SAW, WAVE_SQUARE, WAVE_PULSE};   /* the BS2's
                                              * order, the panel's left to right: the knob's (knob_turn) */
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

/* AFX (ARP or SEQ held HOLD_MS): each key its own variation of the sound, the keyboard a kit. By the key (F3 the first, whatever
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
/* HOME's scope (scope_paint): the synth's output before the volume, every 4th sample (11 kHz, 93 ms of it), and
 * oscillator 1's step (cycles a sample), its period */
#define SCOPE_N 1024u
static int8_t scope_buf[SCOPE_N] PLAT_POOL;
static volatile uint32_t scope_w;
static volatile float scope_inc;
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
static int store_save(uint32_t obj, const void *src, uint32_t len);   /* plat_store_save, told if it is in silence */
#include "bs2_fx.c"                       /* the FX bus: one room every instrument sends into (fx_room_send, fx_next) */
#include "bs2_drum.c"                     /* Drum Something: the drum voices (dt_hit, dt_note, dt_next) */
/* the clock's BPM: the drums', the master, while a pattern runs (the bass on the same clock, as on a 400 sending MIDI
 * clock to a Bass Station II); else the patch's */
static float clock_bpm(void) { return dt_running ? (float)dt.tempo : (float)patch.tempo; }
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
    uint32_t ext_n, ext_span;                /* MIDI clocks since the last measure's start, and the samples since */
    float ext_bpm;                           /* MIDI clock's tempo, measured (0: not yet) */
    uint16_t ext_shown;                      /* it as shown, whole BPM (moves on a change over 0.7) */
    uint32_t lfo_ticks;                      /* MIDI clocks not yet counted by the LFOs (their sync) */
    float clk_out;                           /* clock ticks toward the next one out (GLO CLOCK's SEND CLK) */
    uint32_t apos, spos;                     /* the arp's notes played; the sequencer's next step */
    uint32_t scur;                           /* the sequencer's step sounding (NSTEPS: none yet) */
    uint32_t rpos;                           /* the 16ths since the start: the rhythm's place, and swing's pair */
    uint8_t sounding, tie;                   /* a note is on; the rhythm (or a legato step) holds it on */
    uint8_t leg;                             /* the sequencer's step sounding is legato: held into the next note */
    int32_t transpose;                       /* the sequencer: the last key down from its first note (0: as recorded) */
} eng = {.vel = 100, .ext_age = EXT_HOLD};

static int ext_clock(void)                   /* the arp and the sequencer follow MIDI clock (GLO CLOCK's SYNC) */
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
    const float lock = dt_lock_h;
    if (lock > 0.0f)                         /* (Swing Lock: the 16ths where the drums' swung 8ths put them) */
        len = (eng.rpos & 2u ? 24.0f - lock : lock) * 0.5f;
    if (!patch.latch)                        /* (latch turned off: the notes no longer down go) */
        for (i = eng.nheld; i-- > 0u;)
            if (!(eng.down[eng.held[i] >> 5] >> (eng.held[i] & 31u) & 1u))
                held_del(eng.held[i]);
    eng.ext_age = eng.ext_age < EXT_HOLD ? eng.ext_age + m : EXT_HOLD;
    eng.ext_span += m;
    if (set.clk_out && !ext_clock())         /* the tempo out as MIDI clock, whether anything plays or not */
        for (eng.clk_out += clock_bpm() * (24.0f / 60.0f / BS2_RATE) * (float)m; eng.clk_out >= 1.0f;
             eng.clk_out -= 1.0f)
            plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB | TO_BLE);
    if (src != eng.src) {                    /* started, stopped, the other took over, or MIDI's start */
        eng_quiet();
        eng.src = src;
        eng.tick = 0.0f;
        eng.apos = eng.spos = eng.rpos = 0;
        if (dt_running && !set.dtrans_off && src != SRC_NONE) {   /* (started under a running pattern: on its 16ths, in
                                              * its phase; TRANSPORT off, where it falls) */
            const float h = lock > 0.0f ? lock : 12.0f, p = dt_pos, b[4] = {0.0f, h * 0.5f, h, h + (24.0f - h) * 0.5f};
            const uint32_t q = (uint32_t)p / 24u;
            const float at = p - (float)q * 24.0f;
            uint32_t r = 3u;
            while (r && at < b[r])
                r--;
            eng.rpos = q * 4u + r;
            eng.tick = at - b[r];
        }
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
    if (lock > 0.0f) {                       /* Swing Lock: its place read from the pattern's, not counted, so a swing
                                              * changed as it plays leaves no drift */
        const float b4[4] = {0.0f, lock * 0.5f, lock, lock + (24.0f - lock) * 0.5f}, p = dt_pos;
        const float at = p - (float)((uint32_t)p / 24u * 24u);
        uint32_t r = 3u;
        while (r && at < b4[r])
            r--;
        eng.tick = at - b4[r];
        eng.ext_ticks = 0;
        if (r != (eng.rpos & 3u)) {
            eng.rpos += (r - eng.rpos) & 3u;
            eng.tie = 0;
            if (src == SRC_ARP)
                arp_beat();
            else
                seq_beat();
        }
        if (eng.sounding && !eng.tie && eng.tick >= (r & 2u ? 24.0f - lock : lock) * 0.5f * (float)patch.gate * 0.01f)
            eng_quiet();
        return;
    }
    eng.tick += ext_clock() ? (float)eng.ext_ticks : clock_bpm() * (24.0f / 60.0f / BS2_RATE) * (float)m;
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

/* MIDI clock's tempo, for the screen: the samples over each EXT_MEASURE clocks (the clocks seen a block at a time, so
 * each measure is within a block either way: ~1 % at 120 BPM), a jump taken at once, a drift averaged; from the
 * first clock after none came for EXT_HOLD */
#define EXT_MEASURE 48u                      /* two beats */
static void ext_measure(void)
{
    float b, d;
    if (eng.ext_age >= EXT_HOLD) {           /* (the clock just came: its first a measure's start) */
        eng.ext_n = eng.ext_span = 0;
        eng.ext_bpm = 0.0f;
        eng.ext_shown = 0;
        return;
    }
    if (++eng.ext_n < EXT_MEASURE || !eng.ext_span)
        return;
    b = (float)EXT_MEASURE * (60.0f / 24.0f) * BS2_RATE / (float)eng.ext_span;
    eng.ext_n = eng.ext_span = 0;
    d = b - eng.ext_bpm;
    eng.ext_bpm = eng.ext_bpm == 0.0f || fm_fabsf(d) > 0.04f * eng.ext_bpm ? b : eng.ext_bpm + 0.3f * d;
    if (!eng.ext_shown || fm_fabsf(eng.ext_bpm - (float)eng.ext_shown) > 0.7f)
        eng.ext_shown = (uint16_t)clamp((int32_t)(eng.ext_bpm + 0.5f), 1, 999);
}

static volatile uint32_t midi_last, midi_last_port, midi_count;   /* the last message in (shown), and how many */
static const char *midi_in_text(char *h)     /* the last message in as hex, "90 3C 64" into h (9): its port's name */
{
    static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
    static const char HEX[] = "0123456789ABCDEF";
    uint32_t m = midi_last, b;
    for (b = 0; b < 3u; b++) {
        uint32_t v = (m >> (8u + 8u * b)) & 0xFFu;
        h[3 * b] = HEX[v >> 4];
        h[3 * b + 1] = HEX[v & 15u];
        h[3 * b + 2] = b < 2u ? ' ' : 0;
    }
    return midi_last_port < NPORTS ? PORT[midi_last_port] : "";
}
#include "bs2_dseq.c"                     /* Drum Something: its sequencer (dq_block, dq_sample, dq_click_next) */
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
        if (st < 0xF0u) {                    /* Drum Something: a device of its own on the network (GLO DRUMS), its
                                              * channels apart from the synth's (one both are on plays both) */
            uint32_t ch = (st & 0x0Fu) + 1u;
            if ((st & 0xF0u) == 0x90u && d2 && (set.dchan == CH_ALL || set.dchan == ch))
                dt_note(d1, d2);             /* (Note Off: nothing, as the 400) */
            else if ((st & 0xF0u) == 0xB0u && (set.dcc_chan == CH_ALL || set.dcc_chan == ch))
                dt_cc(d1, d2);
        }
        if (st != 0xF8u && st != 0xFEu) {    /* (clock and active sensing would flood the screen) */
            midi_last = pkt;
            midi_last_port = (uint32_t)port;
            midi_count++;
        }
        if (st == 0xF8u) {                   /* MIDI clock: the arp and the sequencer follow it (CLOCK not INT) */
            if (set.clock != CLK_INT) {
                eng.ext_ticks++;
                dq.ext++;
                eng.lfo_ticks++;
                ext_measure();
                eng.ext_age = 0;
            }
        } else if (st == 0xFAu) {            /* start: from the first step */
            if (set.clock != CLK_INT) {
                eng.src = SRC_RESTART;
                if (dq.sel_song != DQ_NONE)  /* (the drums too: the manual's slave) */
                    dq_song_start(dq.sel_song);
                else if (dq.sel < DQ_NPAT && dqm.pat[dq.sel].n)
                    dq_start(dq.sel);
            }
        } else if (st == 0xFCu) {            /* stop: the drums */
            if (set.clock != CLK_INT)
                dq_stop();
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
    return ext_clock() ? (float)t : clock_bpm() * (24.0f / 60.0f / BS2_RATE) * (float)n;
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

#define DT_MIX 4.0f                          /* +12 dB */
#define OUT_FS 8300000.0f                    /* the output's full scale, as MASTER's gain has it */
#define OUT_KNEE (0.7f * OUT_FS)
/* the last thing out: drums on the bass near full MASTER rounded into full scale, not cut there; under the knee nothing */
static inline float out_soft(float s)
{
    const float a = fm_fabsf(s);
    if (a <= OUT_KNEE)
        return s;
    {
        const float r = OUT_FS - OUT_KNEE, y = OUT_KNEE + r * fm_tanhf((a - OUT_KNEE) / r);
        return s < 0.0f ? -y : y;
    }
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static uint32_t rng = 1u;                /* the noise's */
    static uint32_t scope_ph;                /* (the scope's every 4th) */
    static afx_t ax;                         /* AFX: the last key's (read at a block's start), or none */
    uint32_t i, j, m, wave, wave2 = patch.wave2, sync = patch.sync;
    int32_t note, cut;
    float mod[NDEST], lv[NLFO], ring = (float)patch.ring / 127.0f, noise;
    float l1 = (float)patch.lvl1 / 127.0f, l2 = (float)patch.lvl2 / 127.0f, ls = (float)patch.lvl_sub / 127.0f;
    float gl = glide_coef(patch.glide), dc = env_coef(1.2f, CTRL_RATE), sg;
    float hu = env_coef(30.0f, CTRL_RATE), hd = env_coef(60.0f, CTRL_RATE), rs = (float)patch.reso / 127.0f;
    float gain = (float)master_q12 / 4096.0f * OUT_FS;   /* MASTER, read once a block */
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
    dt_block();
    dq_block();
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
        scope_inc = inc;
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
            if (!(++scope_ph & 3u))          /* (HOME's scope: every 4th, before the volume) */
                scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int8_t)fm_clampf(s * 100.0f, -127.0f, 127.0f);
            s *= 0.3f * gain_now;
#ifdef PLAT_HOST
            dsum += s;
            dabs += fm_fabsf(s);
#endif
            dq_sample();
            {
                float g = DT_MIX * 0.3f * gain_now * ((float)dt.master / 127.0f);   /* the drums: after the bass's chain, over
                                                  * its level (hits, it sustains), under their own MASTER VOLUME (K2) */
                s += dt_next(g) + dq_click_next() * g;
            }
            s += fx_next();                  /* the FX bus's returns, on the master */
            s = out_soft(s);
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

/* the first sequence: the demo's (presets/demo.h: P01's, as saved on the author's FM-1), or, in the host simulator
 * without BS2_DEMO=1 (dq_demo_on), the tests' line: v0.7's, C3, D#3 tied over two steps */
_Static_assert(DEMO_PSIZE == sizeof(patch_t), "presets/demo.h's P01 is this patch_t's (make_demo.py again)");
#if defined(PLAT_HOST) && !defined(PLAT_WEB) && !defined(PLAT_RFM1)
#define SEQ_TEST_LINE {.len = 8, .step = {48, 48, 60, 48, 51, 51, 55, 58, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST,        \
                                          SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST,        \
                                          SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST,        \
                                          SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST, SEQ_REST},                 \
                       .leg = {1u << 4}}
#endif
static void seq_line(seqmem_t *q)
{
#if defined(PLAT_HOST) && !defined(PLAT_WEB) && !defined(PLAT_RFM1)
    static const seqmem_t T = SEQ_TEST_LINE;
    if (!dq_demo_on()) {
        *q = T;
        return;
    }
#endif
    memcpy(q, DEMO_PATCH + offsetof(patch_t, sq), sizeof *q);
}
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
    seq_line(&p->sq);
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
    int r = store_save(OBJ_SETTINGS, &set, sizeof set);
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
    s->chan = 1;
    s->local = 1;
    s->at_out = 1;
    s->clk_out = 1;
    s->tune = 0;
    s->clock = CLK_AUTO;
    s->vcurve = VC_NORMAL;
    s->cc_chan = 1;
    s->knobs = KN_NORMAL;
    s->hints = 1;
    s->spk = s->pop = s->zoom_off = 0;           /* (spk 0: FLAT; pop 0: 1.5 s; zoom_off 0: AUTO) */
    s->dchan = s->dcc_chan = 10;            /* (the drums: GM's channel) */
    s->dswing_lock = s->dtrans_off = 0;
    s->droll = DQ_ROLL_DEF;
    {
        static const seqmem_t E = {.len = 0, .step = {SEQ_REST}};
        uint32_t i, j;
        seq_line(&s->mem[0]);
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
#define NPSLOT 32u                       /* a bank page's slots: a store object each (OBJ_PATCHES, OBJ_PATCHES1..3) */
#define NBANKP 4u                        /* the pages */
#define NSLOTS (NBANKP * NPSLOT)         /* the slots: 128, as a Bass Station II's */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t nslots, psize;               /* (nslots: a page's, NPSLOT) */
    patch_t p[NPSLOT];
} bank_t;
_Static_assert(sizeof(bank_t) <= PLAT_STORE_MAX, "a bank page fits a store object");
_Static_assert(DEMO_BANK_VERSION == BANK_VERSION, "presets/demo.h's P01 is this version's (make_demo.py again)");
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
_Static_assert(sizeof(settings_t) == SET_V4_SIZE + 8u, "the settings end with no padding");
static bank_t bank[NBANKP] PLAT_POOL;
#define BANK_P(s) (bank[(s) / NPSLOT].p[(s) % NPSLOT])   /* slot s's patch */
static uint32_t bank_obj(uint32_t pg) { return pg ? OBJ_PATCHES1 + pg - 1u : OBJ_PATCHES; }   /* page pg's object */
/* the slots' names (OBJ_NAMES): a Bass Station II's 16 characters each, kept apart from the patches (so patch_t and
 * the bank's pages stay as they were); a name's unused end 0s, an empty one no name (a dump goes out by its slot) */
#define PNAME_N 16u
#define NAMES_MAGIC 0x4E325342u          /* "BS2N" */
typedef struct {
    uint32_t magic;
    char n[NSLOTS][PNAME_N];
} names_t;
_Static_assert(sizeof(names_t) <= PLAT_STORE_MAX, "the names fit a store object");
static names_t names PLAT_POOL;
static char cur_name[PNAME_N];               /* the sound's name (the edit buffer's, as patch: SAVE keeps it) */
/* PATCH NAME's letters, in K2's order (a dump's name may hold others: shown, K2 from the space) */
static const char NAME_CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-.,'&+!#/()";
#define NNAME_CH (sizeof NAME_CH - 1u)
static uint32_t name_ch(char c)              /* c's place in NAME_CH (0, the space: none) */
{
    uint32_t i;
    for (i = 0; i < NNAME_CH && NAME_CH[i] != c; i++)
        ;
    return i < NNAME_CH ? i : 0u;
}
static void patch_sane(patch_t *p);          /* (the UI's: by its controls' ranges) */
static void bank_factory(void);              /* (the factory patches: after the dumps' reader) */

static uint8_t names_dirty;                  /* the names changed since they were saved (save, bank_in_step) */
static void name_put(char *d, const char *t)   /* a name from t: its first PNAME_N characters, the rest 0s */
{
    uint32_t i;
    for (i = 0; i < PNAME_N; i++)
        d[i] = *t ? *t++ : 0;
}
static void name_set(uint32_t s, const char *t)   /* slot s's name */
{
    name_put(names.n[s], t);
    names_dirty = 1;
}
static void names_load(void)                 /* (before the bank: a page made anew names its slots) */
{
    if (plat_store_load(OBJ_NAMES, &names, sizeof names) != (int)sizeof names || names.magic != NAMES_MAGIC) {
        memset(&names, 0, sizeof names);
        names.magic = NAMES_MAGIC;
    }
}
static int names_save(void)                  /* 0: kept (or nothing to keep) */
{
    if (!names_dirty)
        return 0;
    if (store_save(OBJ_NAMES, &names, sizeof names) != 0)
        return -1;
    names_dirty = 0;
    return 0;
}
static void bank_page_load(uint32_t pg)   /* page pg from its object; its INIT patches (page 0: the factory's) if none */
{
    bank_t *b = &bank[pg];
    uint32_t i, ps, v;
    int n = plat_store_load(bank_obj(pg), b, sizeof *b);
    v = b->version;
    ps = v == 1u ? BANK_V1_PSIZE : v == 2u ? BANK_V2_PSIZE : v == 3u ? BANK_V3_PSIZE : v == 4u ? BANK_V4_PSIZE
       : v == 5u ? BANK_V5_PSIZE : v == 6u ? BANK_V6_PSIZE : v == 7u ? BANK_V7_PSIZE : v == 8u ? BANK_V8_PSIZE
       : v == 9u ? BANK_V9_PSIZE : v == 10u ? BANK_V10_PSIZE : v == 11u ? BANK_V11_PSIZE : v == 12u ? BANK_V12_PSIZE : 0u;
    if (ps && n == (int)(offsetof(bank_t, p) + NPSLOT * ps) && b->magic == BANK_MAGIC && b->nslots == NPSLOT
        && b->psize == ps) {               /* an older version: each patch moved up, what it lacks filled in */
        uint8_t *o = (uint8_t *)b->p;
        uint32_t gap = ps >= SEQ_GAP && v <= 7u ? SEQ_GAP_N : 0u, j;   /* (versions 3 to 7: the sequence's 16 steps,
                                                                    * then the gap) */
        for (i = NPSLOT * ps; i-- > 0u;)     /* (the last byte first: none is overwritten unread) */
            o[i / ps * sizeof(patch_t) + i % ps + (i % ps >= SEQ_GAP ? gap : 0u)] = o[i];
        for (i = 0; i < NPSLOT; i++) {
            patch_t *q = &b->p[i];
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
        b->version = BANK_VERSION;
        b->psize = sizeof(patch_t);
        n = sizeof *b;
    }
    if (n != (int)sizeof *b || b->magic != BANK_MAGIC
        || b->version != BANK_VERSION || b->nslots != NPSLOT || b->psize != sizeof(patch_t)) {
        b->magic = BANK_MAGIC;
        b->version = BANK_VERSION;
        b->nslots = NPSLOT;
        b->psize = sizeof(patch_t);
        if (pg)
            for (i = 0; i < NPSLOT; i++) {
                patch_defaults(&b->p[i]);
                name_set(pg * NPSLOT + i, "INIT");
            }
        else
            bank_factory();
    }
    for (i = 0; i < NPSLOT; i++)
        patch_sane(&b->p[i]);
}

static void bank_load(void)
{
    uint32_t i;
    names_load();
    for (i = 0; i < NBANKP; i++)
        bank_page_load(i);
}
static int bank_save(uint32_t s) { return store_save(bank_obj(s / NPSLOT), &bank[s / NPSLOT], sizeof(bank_t)); }   /* slot s's page */

/* a test build's (BS2_TEST_PATCHES 1): slots 24..32 hold the sounds to record (the waves, the pulse and its PWM, the
 * noise, the glide, the mod envelope on the pitches, an arp rhythm, the swing), played from slot 24 at octave 0, at
 * every power-on (unsaved: SAVE would keep them) */
#ifndef BS2_TEST_PATCHES
#define BS2_TEST_PATCHES 0
#endif
#define NTEST 9u
static void test_patches(void)
{
    patch_t *p = &BANK_P(NPSLOT - NTEST);
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
    set.slot = NPSLOT - NTEST;
    set.octave = 0;
}

static void set_sane(void);
static void rnd_load(void);
static void ui_start(void);
void app_init(void)
{
    if (set.droll >= DQ_NROLL)
        set.droll = DQ_ROLL_DEF;
    settings_t d;
    int n = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    uint32_t i;
    settings_defaults(&d);
    fx_init();
    dt_init();
    dq_init();
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
    patch = BANK_P(set.slot);
    memcpy(cur_name, names.n[set.slot], PNAME_N);
    ui_start();
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];                     /* a short message over the header */
    uint32_t msg_until;
    uint32_t map_until;                  /* the controls' map shown until (0: not shown) */
    uint32_t hint_until;                 /* Hints: a button's hint shown until (0: none) */
    uint32_t val_until;                  /* the knob turned last (last_enc): its value's pop-up until (0: none) */
    uint8_t last_enc;
    const char *hint;                    /* (that hint) */
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t vel[NKEYS], prs[NKEYS];      /* each key's last velocity, and its pressure while held */
    uint8_t och[NKEYS];                  /* the MIDI channel each held key's note went out on, 1..16 (0: none) */
    uint8_t dno[NKEYS];                  /* the drum's note each held key sent (DT_OUT; 0: the key's own, the synth's) */
    int32_t last_key;                    /* the key pressed last: its numbers in the header */
    uint8_t looking;                     /* Bluetooth MIDI devices: REC pressed, the scan's list up (lib/btdev) */
    uint8_t hold_k, hold_live;           /* ARP, SEQ, GLO or REC down: which, and its tap or hold still to act */
    uint32_t hold_at;                    /* when it went down */
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
    uint8_t mix_src;                     /* SUB OSC + MIXER's switch: what its K4 sets (MS_*) */
    uint8_t osc_sel;                     /* the OSCILLATORS' Osc Select: 0 oscillator 1, 1 oscillator 2 (osc_sel_k) */
    uint8_t pw_src;                      /* their pulse width's switch: what its knob sets (PW_*) */
    uint8_t flt_classic;                 /* the FILTERS' Classic setting while Acid is on (FLT_*), for its way back */
    uint8_t lfo_delay;                   /* the LFOS' Delay/Speed switch (ALGO) at Delay: their knobs the delays */
    uint8_t name_at;                     /* PATCH NAME's cursor: the letter its K2 changes, 0..PNAME_N-1 */
    uint8_t lfo_sel;                     /* the LFO mods' LFO (SELECT): 0 LFO 1, 1 LFO 2 */
    uint8_t env_sel;                     /* the ENVELOPES' Env Select (SELECT): what the faders set, 0 the amp envelope,
                                          * 1 the mod envelope, 2 both (Amp+Mod) */
    uint32_t lit;                        /* the keys whose notes are held (notes_lit): lit, on the keys and on screen */
    uint8_t drum;                        /* Drum Something's mode (HOME held): the keys its instruments, HOME its page */
    uint32_t dt_at[NDT];                 /* when each drum was last hit (dt_flash_take): its key lit a moment */
    uint8_t dt_sel;                      /* the drum last tapped: what VOLUME and TUNING (K3, K4) set */
    uint8_t dt_fn;                       /* the function lit in the 400's LED lists (DTF_*): SEQ and ARP step them */
    uint8_t dt_step, dt_pend;            /* BUILD MODE: the step shown; a value entered, not yet ENTERed */
    int16_t dt_ent;                      /* that value */
    uint8_t dt_pat, dt_song, dt_tgt;     /* the number each list's first function shows (a cue while it runs); the one
                                          * ERASE PATTERN or COPY/APPEND TO would act on */
    uint8_t dt_b_saved, dt_b_mode, dt_b_paused;   /* TRANSPORT: the drums started the bass's sequence (its arp mode before);
                                          * they stopped its arp, to come back with them */
    uint8_t dt_was;                      /* it was running (its stopping seen: the list back at its number) */
    uint8_t dt_show;                     /* DTS_*: what the header shows for a moment, till dt_until */
    uint32_t btn_in;                     /* the buttons held now, as a press is handled (btn: ui.btn's until it's done) */
    uint16_t dt_oct_tempo;               /* the tempo before an OCT's step, and whether that was unsaved: both OCTs put them back */
    uint8_t dt_oct_dirty;
    uint32_t dt_until;
    uint32_t dt_mid;                     /* the keys at the lights' middle level (a channel's others) */
    uint8_t dt_drop;                     /* a Channel page: the header's rows down over it, a knob turning (dt_drop_step) */
    uint32_t dt_drop_at;                 /* its last step */
    uint32_t mpan_at;                    /* the camera panning between the two HOMEs since (mpan_band; 0: not) */
} ui PLAT_POOL;
/* Knob Zoom's camera (below, zoom_step) */
#define ZOOM_MAX 256                         /* Q8, over 1x: 2x (crisp once there: whole pixels) */
#define ZOOM_HOLD_MS 1000u
#define DFN_ZOOM_MS 2200u                    /* the camera on a drum function just picked (SEQ, ARP) */
#define ZOOM_IN_MS 64u                       /* the damping's time constants */
#define ZOOM_OUT_MS 96u
enum { ZH_NONE, ZH_DIVE, ZH_PAGE, ZH_RISE, ZH_OPEN, ZH_CLOSE };   /* zoom.home: a CC on HOME (zoom_cc_in): the camera going in, on its
                                                * section page, coming out; a section's button in (sect_open, dt_open), HOME's out (home_back) */
#define ZOOM_DIVE_MS 450u                    /* HOME's dive, the whole way in (and out: ZOOM_RISE_MS) */
#define ZOOM_RISE_MS 550u
#define ZOOM_SETTLE_Z 26                     /* the page as a section's dive lands on it: 1.1x, easing to 1x (ZOOM_OUT_MS) */
static struct {                             /* (all 0 at rest: in the pool) */
    int16_t z, x, y, tx, ty;                 /* the scale over 1x (Q8, 0: none), the focus and where it heads (panel px) */
    int16_t hx, hy;                          /* HOME's: the CC's section's middle (panel px) */
    float u, lc;                             /* the dive's way along (0 HOME, 1 its page at 2x); HOME's scale there, log2 */
    uint8_t k, pg;                           /* the encoder (+1: 0 none) and page its target is of */
    uint8_t home, g, ke;                     /* ZH_*; the CC's section page, its encoder */
    uint8_t sv[5], v[5];                     /* HOME's views (zoom_views); the CC's page's */
    uint8_t hp, dsv, dnew;                   /* the HOME it dives from; Drum Something's: its selected drum, the CC's */
    uint8_t bk;                              /* ZH_OPEN: the section's button */
    uint8_t settle;                          /* its page just landed on, a little close: the camera easing back, its map and hint after */
    uint8_t fn_ok;                           /* the drums' lit function drawn: tx, ty its LED and words */
    uint32_t fn_at;                          /* when SEQ or ARP last moved it (0: none): the camera goes to it a while */
    uint32_t at, cc_at;                      /* the last step's time; the last CC's from HOME */
} zoom PLAT_POOL;
#define ORG_SHOW_MS 1500u                    /* a control's value on the MASTER's display, after its change */
static void org_set(const void *q)           /* a control of the sound changed: the Org. Value arrows', the display's */
{
    ui.org = q;
    ui.org_at = ms_stamp();
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
            char h[9];
            const char *port = midi_in_text(h);
            text_cat(t, ". MIDI IN ");
            text_cat(t, port);
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
 * DUMP; GLO held RND, the RND Synth's seeds: kept with the settings, not the patches). Each page's encoders by plat.h EN_*: a patch_t field each (a
 * settings_t field on the GLO pages, from PG_GMIDI on), its range, and its names when it is a choice; a page's
 * empty places set nothing */
enum { PG_OSC, PG_MIX, PG_MENV, PG_LFO1, PG_LFO2, PG_PATCH, PG_ARP, PG_SEQ, PG_AENV, PG_FX, PG_TUNE, PG_FMOD, PG_EMOD, PG_EFX, PG_XMOD, PG_AMOD, PG_HOME, PG_GMIDI, PG_GCLK, PG_GDISP, PG_GDUMP,
       PG_GRND, PG_RPAT, PG_RTRK, PG_GDRUM, PG_DHOME, PG_DCH1, PG_DCH2, PG_DCH3, PG_DCH4, PG_DCH5, PG_DCH6,
       PG_DMOD, PG_DPAT, PG_DSONG, PG_DPINFO, PG_DSINFO, PG_GAUD, PG_GCTL, PG_NAME, NPAGES };                             /* (each new page last: the pages' numbers before it kept) */
static const char *const PAGE_NAME[NPAGES] = {"OSCILLATORS", "PULSE WIDTH", "TRIGGERING", "LFOS", "LFO MODS", "PATCH", "ARP",
                                              "SEQ", "ENVELOPES", "SUB OSC + MIXER", "FILTERS", "FILTERS LFO 2", "ENV MODS", "EFFECTS", "FX MODS", "ARP MODS", "HOME", "GLOBAL MIDI", "GLOBAL CLOCK",
                                              "GLOBAL DISPLAY", "GLOBAL DUMP", "RND SEED", "RND PATCH", "RND TRACKS",
                                              "GLOBAL DRUMS", "DRUM HOME", "CHANNEL 1", "CHANNEL 2", "CHANNEL 3",
                                              "CHANNEL 4", "CHANNEL 5", "CHANNEL 6", "DRUM MODS", "PATTERN", "SONG", "PATTERN INFO", "SONG INFO", "GLOBAL AUDIO", "GLOBAL CONTROLS",
                                              "PATCH NAME"};
#define RND_PAGE(g) ((g) >= PG_GRND && (g) <= PG_RTRK)   /* the RND pages */
#define PATCH_PAGE(g) ((g) == PG_PATCH || (g) == PG_HOME)   /* (PRESET picks the patch: the MASTER, HOME) */
#define DRUM_PAGE(g) ((g) >= PG_DHOME && (g) < PG_GAUD)        /* Drum Something's pages (HOME held: its mode, ui.drum) */
#define DCH_PAGE(g) ((g) >= PG_DCH1 && (g) <= PG_DCH6)   /* its Channel pages */
/* the 400's two LED lists, one number each: PATTERN's, then SONG's, BUILD MODE's own under it */
enum { DTF_PAT, DTF_REC, DTF_ERASEI, DTF_ERROR, DTF_SWING, DTF_TIME, DTF_MEAS, DTF_ERASEP, DTF_COPYP, DTF_ROLL,
       DTF_SONG, DTF_ERASES, DTF_COPYS, DTF_BUILD, DTF_BPAT, DTF_BVOL, DTF_BTEMPO, DTF_BINS, DTF_BDEL, NDTF };
enum { DTS_NONE, DTS_FN, DTS_TEMPO, DTS_VOL, DTS_TUNE, DTS_METRO, DTS_MASTER };   /* the header's moment: a function's name, the tempo over both
                                              * displays, the selected drum's level or pitch on the left one */
#define DT_SHOW_MS 2500u                     /* (past Knob Zoom's way back out: the left display seen, its knob just left) */
/* the pop-ups' time (GLO DISPLAY), its knob's places; pop_d the place, 0 and 1 swapped in the store so a record from
 * before it (0) is 1.5 s */
enum { NPOP = 5 };
static const char *const POP_NAME[NPOP] = {"OFF", "1.5 S", "2 S", "3 S", "ALWAYS"};
static uint32_t pop_d(void) { return set.pop < 2u ? 1u - set.pop : set.pop; }   /* (its own inverse) */
static const char *const SUB_NAME[NSUBW] = {"SINE", "PULSE", "SQUARE"};
static const char *const SUB_OCT_NAME[3] = {"", "-1", "-2"};   /* (the sub's octave is 1 or 2) */
static const char *const TRIG_NAME[NENVT] = {"MULTI", "SINGLE", "AUTOGLIDE"};
static const char *const SPD_SNC[2] = {"SPEED", "SYNC"};
static const char *const COUNT_NAME[17] = {"ENDLESS", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
                                           "13", "14", "15", "16"};
static const char *const RANGE_NAME[NRANGE] = {"16'", "8'", "4'", "2'"};
static const char *const OFF_ON[2] = {"OFF", "ON"};
static const char *const AUTO_OFF[2] = {"AUTO", "OFF"};
static const char *const ON_OFF[2] = {"ON", "OFF"};   /* (ble_off) */
static const char *const CHAN_NAME[CH_ALL + 1] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11",
                                                  "12", "13", "14", "15", "16", "ALL"};
static const char *const CLK_NAME[NCLK] = {"AUTO", "INT", "EXT"};
#include "bs2_spk.c"                      /* what goes to the speaker: the mono sum, BASS+, OFF */
static const char *const SPK_NAME[NSPK] = {"FLAT", "BASS+", "OFF"};
void app_speaker(int32_t *lr, uint32_t n, int jack)
{
    static spk_t z PLAT_POOL;
    spk_process(&z, lr, n, set.spk, jack);
}
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
    /* the Bass Station II's OSCILLATORS, oscillator 1's (Osc Select, ui.osc_sel: 2's, OSC2_P; its encoder osc_sel_k):
     * the pitch's view, then the pulse width's (Sync 1-2; its switch, ui.pw_src, and the knob it sets: PW_P) */
    [PG_OSC] = {[EN_SELECT] = {"RANGE", PF(range1), 0, NRANGE - 1, RANGE_NAME},
                [EN_ALGO] = {"WAVE", PF(wave1), 0, NWAVES - 1, WAVE_NAME},
                [EN_K1] = {"COARSE", PF(coarse1), -12, 12, 0}, [EN_K2] = {"FINE", PF(fine1), -100, 100, 0},
                [EN_K3] = {"MOD ENV", PF(menv_o1), -63, 63, 0}, [EN_K4] = {"LFO 1", PF(lfo[0].depth[0]), -63, 63, 0}},
    [PG_MIX] = {[EN_SELECT] = {"SYNC", PF(sync), 0, 1, OFF_ON}, [EN_K4] = {"PW", PF(pw1), 5, 95, 0}},
    /* the Bass Station II's ENVELOPES: PG_AENV its panel (Env Select, ui.env_sel: the faders, K1 to K4, the amp
     * envelope's or MENV_P's), PG_MENV its Triggering (Env Select again, the switch on K4: the amp envelope's or
     * TRIGM_P's) */
    [PG_MENV] = {[EN_K4] = {"TRIGGER", PF(env_trig[0]), 0, NENVT - 1, TRIG_NAME}},
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
     * and REC the sequence: knob_turn); PRESET its On/Legato button, ALGO its Latch/Rest (recording: the step just
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
    [PG_TUNE] = {[EN_K1] = {"DRIVE", PF(drive), 0, 127, 0}, [EN_K2] = {"MOD ENV", PF(menv_cut), -63, 63, 0},
                 [EN_K3] = {"RESO", PF(reso), 0, 127, 0}, [EN_K4] = {"CUTOFF", PF(cutoff), 0, 127, 0}},
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
    /* the global settings by what they are about: the clock (the tempo on the display), the sound coming out, how the
     * controls answer; the settings on K3 and K4 (ENC_MAP) */
    [PG_GCLK] = {[EN_K3] = {"SEND CLK", SF(clk_out), 0, 1, OFF_ON}, [EN_K4] = {"SYNC", SF(clock), 0, NCLK - 1, CLK_NAME}},   /* (K1, the tempo: the patch's, knob_param) */
    [PG_GAUD] = {[EN_K1] = {"SPEAKER", SF(spk), 0, NSPK - 1, SPK_NAME}, [EN_K2] = {"TUNE", SF(tune), -50, 50, 0}},
    [PG_GCTL] = {[EN_K1] = {"VELOCITY", SF(vcurve), 0, NVC - 1, VC_NAME}, [EN_K2] = {"KNOBS", SF(knobs), 0, NKN - 1, KN_NAME}},
    /* (K1 turned and shown as pop_d's: knob_turn, knob_text) */
    [PG_GDRUM] = {[EN_SELECT] = {"CC CHAN", SF(dcc_chan), CH_OFF, CH_ALL, CHAN_NAME},
                  [EN_K1] = {"CHANNEL", SF(dchan), CH_OFF, CH_ALL, CHAN_NAME}},
    [PG_DMOD] = {[EN_K3] = {"SWING LOCK", SF(dswing_lock), 0, 1, OFF_ON},
                 [EN_K4] = {"TRANSPORT", SF(dtrans_off), 0, 1, ON_OFF}},
    [PG_GDISP] = {[EN_PRESET] = {"KNOB ZOOM", SF(zoom_off), 0, 1, AUTO_OFF},
                  [EN_K1] = {"HINTS", SF(hints), 0, 1, OFF_ON}, [EN_K2] = {"POP-UPS", SF(pop), 0, NPOP - 1, 0}},
};
/* SUB OSC + MIXER's switch, as the Bass Station II's: what its K4 sets, from the switch's bottom (NOISE, the start) up; EXT, the
 * external input's level: the computer's USB audio (plat_usb_audio_route) */
enum { MS_NOISE, MS_RING, MS_EXT, NMS };
static const param_t MIX_SRC[NMS] = {{"NOISE", PF(noise), 0, 127, 0}, {"RING", PF(ring), 0, 127, 0}, {"EXT", PF(lvl_ext), 0, 127, 0}};
/* the OSCILLATORS' Osc Select at 2: oscillator 2's, in PARAMS[PG_OSC]'s places */
static const param_t OSC2_P[NE] = {[EN_SELECT] = {"RANGE", PF(range2), 0, NRANGE - 1, RANGE_NAME},
                                   [EN_ALGO] = {"WAVE", PF(wave2), 0, NWAVES - 1, WAVE_NAME},
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
static const param_t TRIGM_P[NE] = {[EN_K4] = {"TRIGGER", PF(env_trig[1]), 0, NENVT - 1, TRIG_NAME}};
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
                                        &TRIGM_P[EN_K4], &EMODM_P[EN_K1], &EMODM_P[EN_K2], &EMODM_P[EN_K3],
                                        &EMODM_P[EN_K4], &LFOD_P[0], &LFOD_P[1], &LFOSV_P[0], &LFOSV_P[1],
                                        &LFOM2_P[EN_K1], &LFOM2_P[EN_K2], &LFOM2_P[EN_K3]};
/* a knob's whole name over the header, when its card's is short */
static const char *const TITLE[NPAGES][NE] = {
    [PG_OSC] = {[EN_SELECT] = "RANGE", [EN_ALGO] = "WAVEFORM", [EN_K1] = "COARSE",
                [EN_K2] = "FINE", [EN_K3] = "MOD ENV DEPTH", [EN_K4] = "LFO 1 DEPTH"},

    [PG_MIX] = {[EN_SELECT] = "SYNC 1-2"},
    [PG_FX] = {[EN_SELECT] = "SUB OSC OCTAVE", [EN_ALGO] = "SUB OSC WAVE", [EN_PRESET] = "SUB OSC LEVEL",
               [EN_K1] = "OSC 1 LEVEL", [EN_K2] = "OSC 2 LEVEL"},
    [PG_ARP] = {[EN_K1] = "ARP TEMPO", [EN_K2] = "ARP RHYTHM", [EN_K3] = "ARP NOTE MODE", [EN_K4] = "ARP OCTAVES"},
    [PG_AMOD] = {[EN_K1] = "SEQ RETRIG", [EN_K2] = "ARP SWING"},
    [PG_SEQ] = {[EN_SELECT] = "SEQUENCE", [EN_ALGO] = "SEQ LENGTH", [EN_K4] = "ARP RHYTHM"},
    [PG_TUNE] = {[EN_K1] = "OVERDRIVE", [EN_K2] = "FILTER MOD ENV DEPTH", [EN_K3] = "RESONANCE", [EN_K4] = "FREQUENCY"},
    [PG_FMOD] = {[EN_K1] = "FILTER LFO 2 DEPTH"},
    [PG_EFX] = {[EN_K1] = "DISTORTION", [EN_K2] = "OSC FILTER MOD"},
    [PG_XMOD] = {[EN_K1] = "FILTER KEY TRACKING", [EN_K2] = "VCA LIMITER"},
    [PG_LFO1] = {[EN_SELECT] = "GLIDE TIME", [EN_K1] = "LFO 1 WAVE", [EN_K4] = "LFO 2 WAVE"},

    [PG_GMIDI] = {[EN_SELECT] = "CC CHANNEL", [EN_K2] = "LOCAL CONTROL", [EN_K3] = "AFTER TOUCH OUT", [EN_K4] = "BLUETOOTH MIDI"},
    [PG_GCLK] = {[EN_K3] = "SEND MIDI CLOCK", [EN_K4] = "CLOCK SYNC"},
    [PG_GCTL] = {[EN_K2] = "KNOB RESPONSE"},
    [PG_GDISP] = {[EN_PRESET] = "KNOB ZOOM", [EN_K1] = "HINTS", [EN_K2] = "POP-UP TIME"},
    [PG_GDRUM] = {[EN_SELECT] = "DRUMS CC CHANNEL", [EN_K1] = "DRUMS CHANNEL"},
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
    {5, CCP(PG_LFO1, EN_SELECT)},   {16, CCP(PG_TUNE, EN_K4)},     {82, CCP(PG_TUNE, EN_K3)},
    {114, CCP(PG_TUNE, EN_K1)},     {29, &OSC2_P[EN_K2]},          {30, &OSC2_P[EN_K1]},
    {110, CCP(PG_MIX, EN_SELECT)},   {24, &MIX_SRC[MS_RING]},       {23, &MIX_SRC[MS_NOISE]},
    {74, &PW_P[0][PW_MANUAL]},      {79, &PW_P[1][PW_MANUAL]},     {71, CCP(PG_OSC, EN_K3)},
    {85, CCP(PG_TUNE, EN_K2)},      {76, &OSC2_P[EN_K3]},          {102, &MENV_P[EN_K1]},
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
static void zoom_cc_in(const param_t *q);
static void zoom_dt_cc(uint32_t cc);
static void zoom_views(const uint8_t *v);
static void cc_apply(uint32_t cc, uint32_t v)   /* the main loop's: a CC on CC CHAN, number and value */
{
    uint32_t i;
    if (cc == 108u) {                        /* (the arp's On: a button, no encoder) */
        patch.arp_on = v != 0u;
        ui.dt_b_saved = ui.dt_b_paused = 0;  /* (the user's now: TRANSPORT forgets what it was keeping) */
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
        if (!set.zoom_off && (ui.page == PG_HOME || zoom.home))   /* (Knob Zoom: from HOME, into its section page:
                                              * zoom_cc_in) */
            zoom_cc_in(q);
        set_dirty = 1;
        ui.redraw = 1;
    }
}

/* a splash over the screen: the RND Synth Companion's (rsplash_draw) as its pages open; up until (0: not), RSPLASH_MS;
 * a button or a knob ends it */
#define RSPLASH_MS 1800u
static uint32_t rsplash_until, rsplash_draws;   /* (draws: how many times it was drawn, once each time it's up) */
static void rsplash_draw(void);

/* the randomizer's moment on screen (rfx_draw): when it began (0: not showing), and what its knobs land on */
static uint32_t rfx_at, rfx_seed;

/* REC held HOLD_MS: every knob of the oscillators, their mix, the filter, both envelopes and both LFOs at random,
 * a new sound to start from (unsaved: SAVE keeps it); the filter left a little open and the attack under a second, so
 * it is heard */
static void randomize(void)
{
    static const param_t *const RND_P[] = {
        CCP(PG_OSC, EN_ALGO), &OSC2_P[EN_ALGO], &OSC2_P[EN_K1], &OSC2_P[EN_K2], CCP(PG_MIX, EN_SELECT),
        CCP(PG_OSC, EN_K3), &OSC2_P[EN_K3], CCP(PG_OSC, EN_K4), &OSC2_P[EN_K4], &PW_P[0][PW_MANUAL], &PW_P[1][PW_MANUAL],
        &PW_P[0][PW_LFO2], &PW_P[1][PW_LFO2], &FILT_P, CCP(PG_LFO1, EN_SELECT), CCP(PG_TUNE, EN_K1),
        CCP(PG_TUNE, EN_K3), CCP(PG_TUNE, EN_K2), CCP(PG_FMOD, EN_K1), &MIX_SRC[MS_RING], &MIX_SRC[MS_NOISE],
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
    rfx_at = ms_stamp();
    rfx_seed = rng;
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
    for (i = 0; i < NPSLOT; i++) {           /* (page 0's: P01..P32, named as their dumps are) */
        patch_defaults(&bank[0].p[i]);
        bsx_import(PRESETS[i], sizeof PRESETS[i], &bank[0].p[i], name);
        patch_sane(&bank[0].p[i]);
        name_set(i, name);
    }
    if (dq_demo_on()) {                      /* (P01: the demo's, as saved on the author's FM-1) */
        memcpy(&bank[0].p[0], DEMO_PATCH, sizeof bank[0].p[0]);
        patch_sane(&bank[0].p[0]);
        name_set(0, "DEMO");
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

static void slot_n_text(uint32_t s, char *t)   /* 01 .. 99, 100 .. 128 */
{
    uint32_t n = s + 1u, i = 0;
    if (n >= 100u)
        t[i++] = (char)('0' + n / 100u);
    t[i++] = (char)('0' + n / 10u % 10u);
    t[i++] = (char)('0' + n % 10u);
    t[i] = 0;
}
static void slot_text(char *t) { slot_n_text(set.slot, t); }
static void slot_disp(char *t)               /* the display's three digits: P01 .. P99, then 100 .. 128 (as a Bass
                                              * Station II's) */
{
    if (set.slot >= 99u) {
        slot_text(t);
        return;
    }
    t[0] = 'P';
    slot_text(t + 1);
}

#include "bs2_rnd.c"                      /* RND seeds: the RND Synth's, captured, kept in their table, sent back */

static void seq_keep(int now);
static int env_page(void) { return ui.page == PG_AENV || ui.page == PG_MENV || ui.page == PG_EMOD; }   /* the ENV group's */
static const param_t *env_mod_p(void)        /* the ENV group's page's settings for the mod envelope */
{
    return ui.page == PG_AENV ? MENV_P : ui.page == PG_MENV ? TRIGM_P : EMODM_P;
}
/* what encoder k sets on this page: as its switches are (SUB OSC + MIXER's K4; the OSCILLATORS' Osc Select, their pulse
 * width's switch) */
static const param_t *knob_param(uint32_t k)
{
    if (ui.page == PG_FX && k == EN_K4)
        return &MIX_SRC[ui.mix_src];
    if (ui.page == PG_GCLK && k == EN_K1)    /* (the tempo: ARP's knob, the patch's: knob_turn, knob_text) */
        return &PARAMS[PG_ARP][EN_K1];
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
/* the FM-1's encoders as each page uses them: the tables and drawings above number them as they were (EN_*: the
 * page's "logical" ones), and a page's byte moves them onto the physical ones as they sit. The knobs go right to left:
 * a page with n of K1-K4's (n < 4) uses the last n (its low bits: the shift). SELECT, ALGO and PRESET as the FM-1 has
 * them, MASTER at the top left, SELECT beside it, PRESET under MASTER, ALGO under SELECT: one control on PRESET, two
 * side by side on PRESET and ALGO, two one over the other on SELECT and ALGO, three as they lie (its high bits: EP_*) */
enum { EP_SAME, EP_SP, EP_AP, EP_ROW, EP_OSC };   /* SELECT/PRESET swapped; ALGO/PRESET swapped; logical SELECT, ALGO,
                                                * PRESET on physical PRESET, SELECT, ALGO; on ALGO, PRESET, SELECT */
static const uint8_t EP_LOG[5][3] = {{0, 1, 2}, {2, 1, 0}, {0, 2, 1}, {1, 2, 0}, {2, 0, 1}};   /* each physical's logical */
#define EMAP(p, ks) (uint8_t)((p) << 2 | (ks))
static const uint8_t ENC_MAP[NPAGES] = {
    [PG_OSC] = EMAP(EP_OSC, 0), [PG_MIX] = EMAP(EP_ROW, 0),   /* Range, Osc Select beside it, Waveform under; Osc Select,
                                                               * its switch beside, Sync under */
    [PG_FX] = EMAP(EP_AP, 0),                                  /* Octave over Wave, the Sub knob beside */
    [PG_AENV] = EMAP(EP_SP, 0), [PG_MENV] = EMAP(EP_SP, 0), [PG_EMOD] = EMAP(EP_SP, 0),   /* Env Select (Triggering beside) */
    [PG_LFO1] = EMAP(EP_SP, 0), [PG_LFO2] = EMAP(EP_SP, 1),   /* Glide, Delay/Speed beside; the mods' LFO, 3 knobs */
    [PG_GMIDI] = EMAP(EP_SP, 0),                               /* CC Chan */
    [PG_GCLK] = EMAP(EP_SAME, 0), [PG_GAUD] = EMAP(EP_SAME, 2), [PG_GCTL] = EMAP(EP_SAME, 2), [PG_NAME] = EMAP(EP_SAME, 1),
    [PG_GDRUM] = EMAP(EP_SP, 3),                               /* the drums' CC Chan; Channel on K4 */
    [PG_SEQ] = EMAP(EP_ROW, 0),                                /* SEQ, LENGTH, STEP */
    [PG_FMOD] = EMAP(EP_SAME, 3), [PG_EFX] = EMAP(EP_SAME, 2), [PG_XMOD] = EMAP(EP_SAME, 2),
    [PG_AMOD] = EMAP(EP_SAME, 2), [PG_GDISP] = EMAP(EP_SAME, 2), [PG_RPAT] = EMAP(EP_SAME, 1),
};
static uint32_t enc_logical(uint32_t p)       /* physical encoder p: what it is on this page (NE: nothing) */
{
    const uint32_t m = ENC_MAP[ui.page], ks = m & 3u;
    if (p < EN_K1)
        return EP_LOG[m >> 2][p];
    return p >= EN_K1 + ks ? p - ks : NE;
}
static uint32_t enc_physical(uint32_t k)      /* logical encoder k: the physical one that sets it on this page */
{
    const uint32_t m = ENC_MAP[ui.page];
    uint32_t p = 0;
    if (k >= EN_K1)
        return k + (m & 3u);
    while (EP_LOG[m >> 2][p] != k)
        p++;
    return p;
}
static uint32_t osc_sel_k(void) { return ui.page == PG_OSC ? EN_PRESET : EN_ALGO; }   /* Osc Select's (PG_OSC, PG_MIX) */
static int osc_page(void) { return ui.page == PG_OSC || ui.page == PG_MIX; }
/* the encoders' settings: each turn a step, held at its ends; 0 when this page has nothing on it */
/* a knob turned fast goes further, as GLO CONTROLS' KNOBS sets: the steps a click by the time since the last (under 20,
 * 35, 60, 100 ms, else), NORMAL's so a quick spin crosses 0..127 in a turn or two and a slow one still steps by one;
 * FINE always one, COARSE two at the slowest. Only a control of 48 steps or more (not the switches and choices). A
 * wider one (the LFOs' speeds, 0..255) as a 0..127 one: each click its share of 128 (2 steps). The host's "spin"
 * exact, its "turn" (80 ms apart) one a click at NORMAL. DT_PICK: the drums' numbers and tempo, one a click at the
 * slowest whatever KNOBS says, so none is stepped over */
static const param_t DT_PICK = {"PICK", 0, 0, 127, 0};
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
    if (q == &DT_PICK && gap >= 100u)
        return e;
    return e * ((q->hi - q->lo + 1) / 128 > 1 ? (q->hi - q->lo + 1) / 128 : 1) *
           MUL[set.knobs < NKN ? set.knobs : KN_NORMAL][gap < 20u ? 0 : gap < 35u ? 1 : gap < 60u ? 2 : gap < 100u ? 3 : 4];
}
/* Drum Something's knobs, on every one of its pages: the 400's four on K1-K4 (K3, K4 the drum last tapped's, shown on
 * the left display), PRESET the number its list's first function shows */
static void dt_show(uint32_t what)
{
    ui.dt_show = (uint8_t)what;
    ui.dt_until = (plat_ms() + DT_SHOW_MS) | 1u;
    ui.redraw = 1;
}
static void dt_refuse(const char *why)       /* (the manual's own limits, said as it happens) */
{
    ui.hint = why;
    ui.hint_until = (plat_ms() + DT_SHOW_MS) | 1u;
    ui.redraw = 1;
}
/* PRESET and ALGO are the 400's keypad and its INC/DEC, on whatever function is lit; recording, ALGO is Error Correct's,
 * whichever is lit */
static int dt_value(uint32_t k, int32_t e)
{
    dq_pat_t *p = &dqm.pat[ui.dt_pat < DQ_NPAT ? ui.dt_pat : 0];
    const uint32_t f = ui.dt_fn;
    const int none = ui.dt_pat >= DQ_NPAT, live = f == DTF_SWING;   /* (swing whenever: the 400 only stopped) */
    if (dq.run && (f == DTF_TIME || f == DTF_MEAS)) {   /* (the manual's: a length is set stopped, on an empty pattern) */
        dt_refuse("Stop to change length");
        return 0;
    }
    if (f == DTF_ROLL) {                     /* (the roll's rate: any time; kept with the settings, glo_keep) */
        set.droll = (uint8_t)clamp((int32_t)set.droll + e, 0, DQ_NROLL - 1);
        return 1;
    }
    if (dq.run && dq.rec && f != DTF_ERROR && !live)   /* (recording: only what's lit, nothing moved by chance) */
        return 0;
    if (f == DTF_PAT || (!dq.run && f == DTF_REC) || (dq.run && f < DTF_SONG && f != DTF_ERROR && !live)) {   /* running: a cue, the current one's till its end */
        if (dq.rec)
            return 0;
        ui.dt_pat = (uint8_t)clamp((int32_t)ui.dt_pat + e, 0, 99);
        ui.dt_show = DTS_NONE;
        if (dq.run)
            dq_cmd(DQC_CUE, ui.dt_pat, 0);
        return 1;
    }
    if (f >= DTF_BPAT) {                     /* BUILD MODE: PRESET the step (the left display), ALGO what goes there (the right) */
        ds_song_t *t = &dsm.s[ui.dt_song < DS_NSONG ? ui.dt_song : 0];
        if (ui.dt_song >= DS_NSONG)
            return 0;
        if (k == EN_PRESET && (f == DTF_BPAT || f >= DTF_BINS)) {
            ui.dt_step = (uint8_t)clamp((int32_t)ui.dt_step + e, 0, (int32_t)(ds_end(t) < DS_NSTEP ? ds_end(t) : DS_NSTEP - 1u));
            ui.dt_pend = 0;
        } else if (f == DTF_BPAT && k == EN_ALGO) {
            const uint32_t c = t->step[ui.dt_step];
            ui.dt_ent = (int16_t)clamp((ui.dt_pend ? ui.dt_ent : c < DQ_NPAT ? (int32_t)c : 0) + e, 0, 99);
            ui.dt_pend = 1;
        } else if (f == DTF_BTEMPO && !ui.dt_step) {   /* (step 00: the song's first tempo; 0, none) */
            const int32_t at = ui.dt_pend ? ui.dt_ent : t->tempo ? dq_tempo_at(t->tempo) + 1 : 0;
            ui.dt_ent = (int16_t)clamp(at + e, 0, 91);
            ui.dt_pend = 1;
        } else if (f == DTF_BVOL || f == DTF_BTEMPO) {
            ui.dt_ent = (int16_t)clamp((ui.dt_pend ? ui.dt_ent : 0) + e, -15, 15);
            ui.dt_pend = 1;
        }
        return 1;
    }
    if (f == DTF_ERASES || f == DTF_COPYS) {
        ui.dt_tgt = (uint8_t)clamp((int32_t)ui.dt_tgt + e, 0, 98);
        return 1;
    }
    if (f >= DTF_SONG) {                     /* running: a cue, as a pattern's */
        ui.dt_song = (uint8_t)clamp((int32_t)ui.dt_song + e, 0, 99);
        ui.dt_show = DTS_NONE;
        if (dq.run && dq.in_song)
            dq_cmd(DQC_SCUE, ui.dt_song, 0);
        return 1;
    }
    if (f == DTF_ERASEP || f == DTF_COPYP) {
        ui.dt_tgt = (uint8_t)clamp((int32_t)ui.dt_tgt + e, 0, 98);
        return 1;
    }
    if (none)
        return 0;
    if (f == DTF_ERROR)
        p->ec = (uint8_t)clamp((int32_t)p->ec + e, 0, 9);
    else if (f == DTF_SWING) {
        if (!dq_can_swing(p)) {
            dt_refuse("Swing needs x/4");
            return 0;
        }
        p->swing = (uint8_t)clamp((int32_t)p->swing + e, 0, 5);
    } else if (f == DTF_TIME || f == DTF_MEAS) {
        if (p->n) {
            dt_refuse("Erase pattern first");
            return 0;
        }
        if (f == DTF_MEAS)
            p->meas = (uint8_t)clamp((int32_t)p->meas + e, 1, 99);
        else if (k == EN_PRESET)
            p->num = (uint8_t)clamp((int32_t)p->num + e, 1, 99);
        else
            p->beat = (uint8_t)clamp((int32_t)p->beat + e, 0, 7);
        p->len = dq_bar(p) * p->meas;
        if (!dq_can_swing(p))
            p->swing = 0;
    } else
        return 0;
    dq.dirty = 1;
    return 1;
}
/* GLO CONTROLS' KNOBS on the drums' too, by the bass's rule (knob_accel): a control of 48 steps or more speeds up as it's spun; the
 * few-valued (K3, K4, Swing, a beat, Error Correct) a step a click */
static int dt_knob_wide(uint32_t k)
{
    const uint32_t f = ui.dt_fn;
    if (k == EN_K1 || k == EN_K2 || k == EN_SELECT)
        return 1;
    if (k == EN_PRESET)
        return f != DTF_ERROR && f != DTF_ROLL && f != DTF_SWING && f != DTF_BVOL && (f != DTF_BTEMPO || !ui.dt_step);
    if (k == EN_ALGO)
        return f != DTF_ERROR && f != DTF_ROLL && f != DTF_SWING && f != DTF_TIME && f != DTF_BVOL && (f != DTF_BTEMPO || !ui.dt_step);
    return 0;
}
#define DT_BOTH_OCT (1u << B_OCTDN | 1u << B_OCTUP)
/* the pool's % free, as the 400's left display has it: two digits, so 99 at most (its manual's table starts at 99%) */
static uint32_t dt_mem_free(void)
{
    const uint32_t f = (DQ_HITS - dqm.used) * 100u / DQ_HITS;
    return f > 99u ? 99u : f;
}
static void dt_tempo_step(int32_t e)         /* the manual's tempos, a step a click (MIDI clock's while it leads) */
{
    if (!ext_clock()) {
        dt.tempo = DQ_TEMPO[clamp(dq_tempo_at(dt.tempo) + e, 0, 90)];
        dq.dirty = 1;
    }
    dt_show(DTS_TEMPO);
}
static int dt_turn(uint32_t k, int32_t e)
{
    if (ui.page == PG_DMOD && (k == EN_K3 || k == EN_K4)) {   /* (mods: kept with the settings, glo_keep) */
        if (k == EN_K3)
            set.dswing_lock = e > 0;
        else
            set.dtrans_off = e < 0;
        return 1;
    }
    if (k == EN_SELECT) {
        dt_tempo_step(e);
        return 1;
    }
    if (k == EN_K1 || k == EN_K2) {
        if (k == EN_K1)
            dt.metro = (uint8_t)clamp((int32_t)dt.metro + e, 0, 127);
        else
            dt.master = (uint8_t)clamp((int32_t)dt.master + e, 0, 127);
        if (DCH_PAGE(ui.page))               /* (no knob of theirs there: on the header that drops down) */
            dt_show(k == EN_K1 ? DTS_METRO : DTS_MASTER);
    } else if (k == EN_K3) {
        dt.vol[ui.dt_sel] = (uint8_t)clamp((int32_t)dt.vol[ui.dt_sel] + e, 0, 15);
        dt_show(DTS_VOL);
    } else if (k == EN_K4) {
        dt.tune[ui.dt_sel] = (uint8_t)clamp((int32_t)dt.tune[ui.dt_sel] + e, 0, 15);
        dt_show(DTS_TUNE);
    } else if (k == EN_PRESET || k == EN_ALGO)
        return dt_value(k, e);
    else
        return 0;
    dq.dirty = 1;
    return 1;
}
static int name_turn(uint32_t k, int32_t e);
static int knob_turn(uint32_t k, int32_t e)
{
    if (ui.page == PG_NAME)
        return name_turn(k, e);
    if (ui.page == PG_GCLK && k == EN_K1) {  /* the tempo, as on ARP: the patch's, MIDI clock's while it leads */
        int r;
        ui.page = PG_ARP;
        r = knob_turn(k, e);
        ui.page = PG_GCLK;
        return r;
    }
    static const param_t RT_SPAN = {"RND", 0, 0, 127, 0};   /* (RND TRACKS' levels and sends: turned as a 0..127's) */
    const param_t *q = DRUM_PAGE(ui.page) && dt_knob_wide(k) && k != EN_K1 && k != EN_K2 ? &DT_PICK
                       : (ui.page == PG_RTRK && k >= EN_K1 && rnd_row != RR_ENG) || (DRUM_PAGE(ui.page) && dt_knob_wide(k))
                       ? &RT_SPAN : knob_param(k);
    e = knob_accel(k, e, q);
    if (DRUM_PAGE(ui.page))                  /* Drum Something's, the same on each of its pages (said there) */
        return dt_turn(k, e);
    if (ui.page == PG_AMOD && k == EN_K2 && dt_lock_h > 0.0f) {   /* Swing Lock: this knob the running pattern's swing */
        static char w[24];
        dq_pat_t *p = &dqm.pat[dq.cur];
        char n[6];
        if (dq_can_swing(p)) {
            p->swing = (uint8_t)clamp((int32_t)p->swing + (e > 0 ? 1 : -1), 0, 5);
            dq.dirty = 1;
        }
        w[0] = 0;
        text_cat(w, "Drums' swing ");
        int_text(DQ_SWING[p->swing], n);
        text_cat(w, n);
        dt_refuse(w);
        return 0;
    }
    if (ui.page == PG_GDISP && k == EN_K2) {   /* the pop-ups' time: its place (pop_d), stored swapped */
        const uint32_t d = (uint32_t)clamp((int32_t)pop_d() + e, 0, NPOP - 1);
        set.pop = (uint8_t)(d < 2u ? 1u - d : d);
        return 1;
    }
    if (PATCH_PAGE(ui.page) && k == EN_PRESET) {   /* a slot: its sound at once (what was not saved is left) */
        uint32_t s = (uint32_t)clamp((int32_t)set.slot + e, 0, NSLOTS - 1);
        ui.pdir = (int8_t)(e < 0 ? -1 : 1);
        if (s != set.slot) {
            uint8_t on = patch.arp_on, mode = patch.arp_mode, latch = patch.latch;
            seq_keep(1);                     /* (the sequence made here, kept before it goes) */
            ui.dt_b_saved = ui.dt_b_paused = 0;   /* (TRANSPORT's memory: of the sound that was) */
            set.slot = (uint8_t)s;
            patch = BANK_P(s);
            memcpy(cur_name, names.n[s], PNAME_N);
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
    if (ui.page == PG_ARP && k == EN_K1 && (ext_clock() || dq.run))   /* TEMPO: MIDI clock's while it leads (the patch's kept) */
        return 1;
    if (ui.page == PG_GRND && k == EN_PRESET) {   /* a seed slot: its seed shown */
        rnd_pick(e);
        return 1;
    }
    if (ui.page == PG_RPAT)                  /* the synth's locks (said there) */
        return rnd_lock_turn(k, e);
    if (ui.page == PG_RTRK)                  /* its tracks (said there) */
        return rnd_track_turn(k, e);
    if (ui.page == PG_ARP && (k == EN_PRESET || k == EN_ALGO)) {   /* On/Legato, Latch/Rest: buttons, turned (up on,
                                                                  * down off); recording, the step legato, a rest */
        if (seq_rec && e > 0) {
            if (k == EN_PRESET)
                step_legato();
            else
                step_enter(SEQ_REST);
        } else if (!seq_rec) {
            if (k == EN_PRESET) {
                patch.arp_on = e > 0;
                ui.dt_b_saved = ui.dt_b_paused = 0;
            }
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
    if (osc_page() && k == osc_sel_k()) {   /* Osc Select: oscillator 1, 2 */
        ui.osc_sel = (uint8_t)clamp((int32_t)ui.osc_sel + e, 0, 1);
        return 1;
    }
    if (ui.page == PG_MIX && k == EN_PRESET) {   /* the pulse width's switch: left to right, Mod Env, Manual, LFO 2 */
        static const uint8_t AT[NPW] = {PW_MENV, PW_MANUAL, PW_LFO2};
        ui.pw_src = AT[clamp((int32_t)PW_POS[ui.pw_src] + e, 0, NPW - 1)];
        return 1;
    }
    if (q->names == WAVE_NAME) {             /* an oscillator's Waveform: along the panel's LEDs (WAVE_SHOWN), not by
                                              * its number */
        uint32_t i = 0;
        while (i < NWAVES - 1u && WAVE_SHOWN[i] != (uint32_t)param_get(q, &patch))
            i++;
        param_put(q, &patch, WAVE_SHOWN[clamp((int32_t)i + e, 0, NWAVES - 1)]);
        org_set(q);
        set_dirty = 1;
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
    if (ui.page == PG_NAME)
        return k == EN_K1 ? "CURSOR" : k == EN_K2 ? "LETTER" : k == EN_K3 ? "CLEAR" : 0;
    if (ui.page == PG_SEQ && (k == EN_PRESET || k == EN_K1 || k == EN_K2))
        return k == EN_PRESET ? "STEP" : k == EN_K1 ? "NOTE" : "LEGATO";
    if (ui.page == PG_ARP && (k == EN_PRESET || k == EN_ALGO))
        return k == EN_PRESET ? (seq_rec ? "LEGATO" : "ARP") : seq_rec ? "REST" : "LATCH";
    if (ui.page == PG_ARP && k == EN_K4 && patch.arp_mode >= ARP_PLAY)   /* (the switch: the sequence in REC, PLAY) */
        return "SEQ";
    if (DRUM_PAGE(ui.page)) {
        static const char *const N[NE] = {[EN_K1] = "METRONOME", "MASTER", "VOLUME", "TUNING"};
        if (ui.page == PG_DMOD && (k == EN_K3 || k == EN_K4))
            return k == EN_K3 ? "SWING LOCK" : "TRANSPORT";
        if (k == EN_SELECT)
            return "TEMPO";
        if (ui.dt_fn >= DTF_BPAT && ui.dt_fn != DTF_BVOL && ui.dt_fn != DTF_BTEMPO && (k == EN_PRESET || ui.dt_fn == DTF_BPAT))
            return k == EN_PRESET ? "STEP" : "PATTERN";
        return k == EN_PRESET ? (ui.dt_fn >= DTF_SONG ? "SONG" : ui.dt_fn == DTF_PAT || ui.dt_fn == DTF_REC ? "PATTERN" : "VALUE")
               : k == EN_ALGO ? "VALUE" : N[k];
    }
    if (k == EN_PRESET && (PATCH_PAGE(ui.page) || ui.page == PG_GRND))
        return ui.page == PG_GRND ? "SEED" : "PATCH";
    if (ui.page == PG_FX && k == EN_K3)
        return "SOURCE";
    if (ui.page == PG_MIX && k == EN_PRESET)
        return "PW SOURCE";
    if (osc_page() && k == osc_sel_k())
        return "OSC SELECT";
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
    if (DRUM_PAGE(ui.page))
        return knob_name(k);
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
            {[EN_K4] = "TRIGGERING"},
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
/* the tempo ARP's TEMPO shows: MIDI clock's, measured, while it leads (0: not yet, or lost), else the patch's */
static int32_t arp_tempo(void)
{
    return !ext_clock() ? (dq.run || ui.drum ? (int32_t)dt.tempo : (int32_t)patch.tempo) : eng.ext_age < EXT_HOLD ? (int32_t)eng.ext_shown : 0;
}
static void dt_digits(char *l, char *r);
static int dt_knobs_split(void)              /* PRESET the left display, ALGO the right (the rest: both the one display) */
{
    const uint32_t f = ui.dt_fn;
    return f == DTF_TIME || f == DTF_BPAT || f >= DTF_BINS;
}
static void knob_text(uint32_t k, char *t)
{
    const param_t *q;
    if (ui.page == PG_GCLK && k == EN_K1) {  /* (the tempo, as on ARP) */
        ui.page = PG_ARP;
        knob_text(k, t);
        ui.page = PG_GCLK;
        return;
    }
    if (ui.page == PG_NAME) {                /* (the cursor's place, its letter) */
        t[0] = 0;
        if (k == EN_K1)
            int_text(ui.name_at + 1u, t);
        else if (k == EN_K2)
            t[0] = cur_name[ui.name_at] > ' ' ? cur_name[ui.name_at] : '_', t[1] = 0;
        return;
    }
    q = knob_param(k);
    int32_t v;
    t[0] = 0;
    if (DRUM_PAGE(ui.page)) {                /* (two digits, as its displays show them; the volumes 0..127) */
        if (ui.page == PG_DMOD && (k == EN_K3 || k == EN_K4)) {
            text_cat(t, OFF_ON[k == EN_K3 ? set.dswing_lock != 0u : !set.dtrans_off]);
            return;
        }
        v = k == EN_SELECT ? arp_tempo() : k == EN_K1 ? dt.metro : k == EN_K2 ? dt.master : k == EN_K3 ? dt.vol[ui.dt_sel] : k == EN_K4 ? dt.tune[ui.dt_sel] : -1;
        if (v >= 0 && k >= EN_K3) {
            t[0] = (char)('0' + v / 10);
            t[1] = (char)('0' + v % 10);
            t[2] = 0;
        } else if (v >= 0)
            int_text(v, t);
        else if (k == EN_PRESET || k == EN_ALGO) {   /* (as the displays have it) */
            char l[3], r[3];
            dt_digits(l, r);
            if (dt_knobs_split() && ui.dt_show != DTS_TEMPO) {   /* (its own display's) */
                text_cat(t, k == EN_PRESET ? l : r);
                return;
            }
            if (l[0] != ' ' || l[1] != ' ')
                text_cat(t, l);
            text_cat(t, r);
        }
        return;
    }
    if (ui.page == PG_ARP && (k == EN_PRESET || k == EN_ALGO)) {   /* the buttons: what they set (recording, its step) */
        text_cat(t, seq_rec ? (k == EN_PRESET ? "LEGATO" : "REST") : OFF_ON[k == EN_PRESET ? patch.arp_on : patch.latch]);
        return;
    }
    if (ui.page == PG_ARP && k == EN_K1 && (ext_clock() || dq.run)) {   /* MIDI clock's tempo (dashes until it is measured) */
        if (arp_tempo())
            int_text(arp_tempo(), t);
        else
            text_cat(t, "---");
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
    if (osc_page() && k == osc_sel_k()) {
        text_cat(t, ui.osc_sel ? "2" : "1");
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
    if (PATCH_PAGE(ui.page) && k == EN_PRESET) {
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
    if (ui.page == PG_GDISP && k == EN_K2) {
        text_cat(t, POP_NAME[pop_d()]);
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

#define POP_ALWAYS 0xFFFFFFFFu
static uint32_t pop_until(void)              /* a pop-up shown now: until (0: none; POP_ALWAYS: until what ends it) */
{
    static const uint16_t MS[NPOP] = {0, 1500, 2000, 3000, 0};
    const uint32_t d = pop_d();
    return d == NPOP - 1u ? POP_ALWAYS : d ? (plat_ms() + MS[d]) | 1u : 0u;
}
static void zoom_close_end(void)             /* (HOME's way back: over, a press or a knob's) */
{
    if (zoom.home == ZH_CLOSE) {
        zoom.home = ZH_NONE;
        zoom.z = 0;
        ui.redraw = 1;
    }
}
static void page_go(uint32_t g)
{
    char t[4];
    if (zoom.home && ui.page != PG_HOME)     /* (HOME's CC's page: HOME's views back) */
        zoom_views(zoom.sv);
    zoom.home = ZH_NONE;
    ui.page = (uint8_t)g;
    ui.redraw = 1;
    ui.map_until = pop_until();              /* (its controls' map for a moment: tags_draw) */
    ui.val_until = 0;
    slot_text(t);
    app_say(PAGE_NAME[g], PATCH_PAGE(g) ? t : "");
}

static void save(void)                       /* the settings, and the sound into its slot */
{
    uint32_t i;
    BANK_P(set.slot) = patch;
    for (i = PNAME_N; i && cur_name[i - 1u] <= ' '; i--)   /* (its spaces at the end left off) */
        cur_name[i - 1u] = 0;
    if (memcmp(names.n[set.slot], cur_name, PNAME_N)) {
        memcpy(names.n[set.slot], cur_name, PNAME_N);
        names_dirty = 1;
    }
    if (set_store() == 0 && bank_save(set.slot) == 0 && names_save() == 0) {
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
static int sounding(void)                    /* anything playing or ringing out */
{
    uint32_t i, drums = dq.run;              /* (the drums too: a write under them is a dropout) */
    for (i = 0; i < NDTCH; i++)
        drums |= dt.ch[i].drum != DT_NONE;
    return drums || eng.src != SRC_NONE || seq_auto || voice_note >= 0 || voice.amp.stage != ENV_IDLE;
}
static void quiet_step(void)                 /* the main loop's (input), before the keepers */
{
    static uint32_t loud_at;
    if (sounding()) {
        loud_at = plat_ms();
        hushed = 0;
    } else {
        hushed = plat_ms() - loud_at >= QUIET_MS;
    }
}
/* A flash write holds up the audio: the platform counts a missed deadline. In silence nothing is heard, so the count
 * shown leaves it out: what it shows is a dropout someone could hear. A write under sound still counts */
#define XR_QUIET_MS 200u
static uint32_t xr_base, xr_mark, xr_until;  /* the misses left out; the count they're from; till when (0: none now) */
static uint8_t backup_held;                  /* a backup streaming: nothing saved till it ends (app_backup_hold) */
static int store_save(uint32_t obj, const void *src, uint32_t len)
{
    int r;
    const int quiet = hushed;
    if (backup_held)                         /* (a SAVE pressed meanwhile: told it failed, nothing lost) */
        return -1;
    if (quiet && !xr_until)
        xr_mark = plat_xruns();              /* (those before it stay counted) */
    r = plat_store_save(obj, src, len);
    if (quiet)
        xr_until = (plat_ms() + XR_QUIET_MS) | 1u;
    return r;
}
static uint32_t xruns_shown(void)            /* (each frame, by cpu_meter) */
{
    const uint32_t x = plat_xruns();
    if (xr_until) {
        if ((int32_t)(plat_ms() - xr_until) < 0) {
            xr_base += x - xr_mark;
            xr_mark = x;
        } else {
            xr_until = 0;
        }
    }
    return x - xr_base;
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
    static seqkeep_t pend PLAT_POOL;         /* the sequence as last seen differing from the saved */
    static uint32_t pend_at;                 /* when it was (0: none waiting) */
    seqkeep_t cur, kept;
    patch_t *b = &BANK_P(set.slot);
    seq_of(&patch, &cur);
    seq_of(b, &kept);
    if (seq_same(&cur, &kept)) {
        pend_at = 0;
        return;
    }
    if (!pend_at || !seq_same(&cur, &pend)) {   /* a new change: the wait from now */
        pend = cur;
        pend_at = ms_stamp();
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
    if (bank_save(set.slot) != 0)
        app_say("SEQ KEEP", "FAILED");
}

/* the GLO pages' settings kept as they're set: into the store's record (the octave and the slot as they were last
 * saved, SAVE's) GLO_KEEP_MS after the last change once the FM-1 is quiet, one flash write for a run of turns;
 * through power-off without SAVE, and without SAVE's writing the sound over its slot */
#define GLO_KEEP_MS 2000u
static void glo_keep(void)                   /* the main loop's (input) */
{
    static settings_t pend PLAT_POOL;        /* the settings as last seen differing from the kept */
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
        pend_at = ms_stamp();
        return;
    }
    if (plat_ms() - pend_at < GLO_KEEP_MS || !hushed)
        return;
    pend_at = 0;
    if (store_save(OBJ_SETTINGS, &rec, sizeof rec) == 0)
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
static uint8_t sx_out[BSX_LEN] PLAT_POOL;
#ifdef PLAT_HOST
static uint8_t sx_bank[NSLOTS][BSX_LEN];
#endif
static struct {
    uint32_t at;                             /* when the last bank dump came in */
    uint8_t n, skipped;                      /* the slots it wrote, the patches past them */
    uint8_t pages;                           /* the bank's pages it wrote into, a bit each */
} bank_in;
static void sysex_slot(uint32_t s, const patch_t *p, int32_t to, char *name)   /* one dump out, named by its slot;
                                                                             * TO: the sound's (-1) or a bank's */
{
    const char *n = s == set.slot ? cur_name : names.n[s];   /* (the playing slot's as it sounds) */
    char t[4];
    uint32_t i;
    name[0] = 0;
    for (i = 0; i < PNAME_N && n[i]; i++)
        name[i] = n[i];
    name[i] = 0;
    if (!i) {                                /* (no name: by its slot, short: a dump's holds 16) */
        text_cat(name, "Bass Smth 2 ");
        slot_n_text(s, t);
        text_cat(name, t);
    }
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
        sysex_slot(s, s == set.slot ? &patch : &BANK_P(s), (int32_t)s, name);
    app_say("SYSEX SENT", "ALL 128");
}
static void bank_in_step(void)               /* the main loop's (input): a bank come in, written once it has ended */
{
    char t[24], v[12];
    uint32_t i;
    int bad = 0;
    if ((!bank_in.n && !bank_in.skipped) || plat_ms() - bank_in.at < BANK_IN_MS)
        return;
    int_text(bank_in.n, t);                  /* "32 IN, 96 LEFT", or "5 PATCHES" */
    text_cat(t, bank_in.skipped ? " IN, " : " PATCHES");
    if (bank_in.skipped) {
        int_text(bank_in.skipped, v);
        text_cat(t, v);
        text_cat(t, " LEFT");
    }
    for (i = 0; i < NBANKP && !bad; i++)    /* (each page a dump wrote) */
        if (bank_in.pages >> i & 1u)
            bad = bank_save(i * NPSLOT) != 0;
    if (!bad && names_save() == 0)
        app_say("BANK LOADED", t);
    else
        app_say("BANK LOAD", "FAILED");
    bank_in.n = bank_in.skipped = bank_in.pages = 0;
}
/* Backup and restore over USB (fm1-base's: device/backup.c, tools/fm1_backup.py, web/backup): everything saved. SAVE stays
 * the player's: a sound or drums changed and not saved aren't saved for it, so aren't in the backup (only what keeps itself:
 * the sequences, the GLO settings) */
int app_backup_playing(void) { return sounding(); }
void app_backup_flush(void)
{
    settings_t rec = set_kept;
    uint32_t i;
    seq_keep(1);
    for (i = GLO_FROM; i < sizeof set; i++)
        ((uint8_t *)&rec)[i] = ((const uint8_t *)&set)[i];
    if (!same(&rec, &set_kept, sizeof rec) && store_save(OBJ_SETTINGS, &rec, sizeof rec) == 0)
        set_kept = rec;
}
void app_backup_hold(int on) { backup_held = (uint8_t)on; }
void app_backup_restored(void)               /* as at power-on: HOME, on the synth */
{
    app_init();
    ui.drum = 0;
    zoom.home = ZH_NONE;
    zoom.z = 0;
    set_dirty = 0;
    dq.dirty = 0;
    ui.redraw = 1;
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
        q = BANK_P(to);                      /* (the slot's own sequence, tempo, gate kept) */
    if (!bsx_import(p, n, &q, name))
        return 0;
    patch_sane(&q);                          /* each field held to its control's range */
    if (to < 0) {
        patch = q;
        name_put(cur_name, name);
        set_dirty = 1;
        ui.redraw = 1;
        app_say("SYSEX LOADED", name);
        return 1;
    }
    BANK_P(to) = q;
    name_set((uint32_t)to, name);
    bank_in.pages |= (uint8_t)(1u << ((uint32_t)to / NPSLOT));
    if ((uint32_t)to == set.slot) {          /* (the slot playing: its new sound now; else no redraw, a dump's time) */
        patch = q;
        memcpy(cur_name, names.n[to], PNAME_N);
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

static uint32_t vel_curve(uint32_t v)        /* GLO CONTROLS' VELOCITY: a key's 1..127, softer (more for less) or harder */
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

/* ARP, SEQ, GLO and REC: tapped, their page (REC: recording on or off, on GLO MIDI a Bluetooth device); held HOLD_MS,
 * a mode (ARP or SEQ: AFX on or off; GLO: the RND pages, held again the GLOBAL ones; REC: a random sound). The hold
 * acts while still held, its release then nothing */
#define HOLD_MS 1000u
/* HOME's K2: the ARP's Rhythm; K3 and K4: the FILTERS' Resonance and Frequency; from HOME and on its way to that page
 * and back: Knob Zoom's dive, as for the knob's CC (zoom_cc_in); no zoom (KNOB ZOOM OFF): HOME's own knob moves. The
 * drums' HOME: K3 and K4, the selected drum's Volume and Tuning, the dive to its Channel page (zoom_dt_cc); no zoom: as
 * on their other pages */
#define HOME_KNOB_PAGE(p) ((p) == EN_K2 ? PG_ARP : PG_TUNE)
static int home_knob(uint32_t p)
{
    if (p != EN_K3 && p != EN_K4 && (ui.drum || p != EN_K2))
        return 0;
    if (ui.drum)
        return !set.zoom_off
               && (ui.page == PG_DHOME || (zoom.home && zoom.hp == PG_DHOME && DCH_PAGE(zoom.g) && zoom.ke == p));
    return ui.page == PG_HOME || (zoom.home && zoom.hp == PG_HOME && zoom.g == HOME_KNOB_PAGE(p) && zoom.ke == p);
}
static void home_knob_turn(uint32_t p, int32_t e)
{
    const uint8_t pg = ui.page;
    char t[12];
    if (!ui.drum)
        ui.page = HOME_KNOB_PAGE(p);
    knob_turn(p, e);
    knob_text(p, t);
    app_say(knob_title(p), t);
    ui.page = pg;
    if (ui.drum)
        zoom_dt_cc((p == EN_K3 ? DT_CC_VOL : DT_CC_TUNE) + ui.dt_sel);
    else if (!set.zoom_off)
        zoom_cc_in(&PARAMS[HOME_KNOB_PAGE(p)][p]);
    ui.redraw = 1;
}

/* Hints: a few words on what a control does, under its value as it turns (a button's at the panel's
 * foot as it's pressed) */
static const char *const HINT[NPAGES][NE] = {
    [PG_OSC] = {"Octave range", "Waveform", "Osc 1 or 2", "Pitch, semitones", "Pitch, cents", "Envelope to pitch",
                "Vibrato depth"},
    [PG_MIX] = {"Sync osc 2 to 1", "Osc 1 or 2", "What K4 sets"},
    [PG_MENV] = {"Which envelope", [EN_K4] = "How notes restart it"},
    [PG_AENV] = {"Which envelope", [EN_K1] = "Fade-in time", "Fall time", "Held level", "Fade-out time"},
    [PG_EMOD] = {"Which envelope", [EN_K1] = "Touch response", "Loop the envelope", "How many loops", "Drum-style hold"},
    [PG_LFO1] = {"Glide time", "Speed or delay", [EN_K1] = "LFO 1 shape", "LFO 1 speed", "LFO 2 speed", "LFO 2 shape"},
    [PG_LFO2] = {"Which LFO", [EN_K1] = "Restart each note", "Follow the tempo", "Smooth its edges"},
    [PG_PATCH] = {[EN_PRESET] = "Pick a patch"},
    [PG_HOME] = {[EN_PRESET] = "Pick a patch"},
    [PG_ARP] = {[EN_ALGO] = "Hold the notes", "Arp on or off", "Speed, BPM", "Rhythm pattern", "Note order",
                "Octave span"},
    [PG_AMOD] = {[EN_K1] = "Restart every 2 bars", "Swing feel"},
    [PG_SEQ] = {"Pick a sequence", "Steps played", "Step to edit", "Step's note", "Tie to next step", "Note length",
                "Rhythm pattern"},
    [PG_FX] = {"Sub octave", "Sub waveform", "Sub level", "Osc 1 level", "Osc 2 level", "What K4 sets"},
    [PG_TUNE] = {"Classic or Acid", "12 or 24 dB slope", "Low, band, high pass", "Filter drive", "Envelope to cutoff",
                 "Filter resonance", "Filter cutoff"},
    [PG_FMOD] = {[EN_K1] = "LFO 2 to cutoff"},
    [PG_EFX] = {[EN_K1] = "Distortion amount", "Osc 2 to cutoff"},
    [PG_XMOD] = {[EN_K1] = "Cutoff follows keys", "Level limiter"},
    [PG_GMIDI] = {"CC input channel", [EN_K1] = "Note channel", "Keys play the synth", "Pressure out as MIDI",
                  "Bluetooth MIDI"},
    [PG_GCLK] = {[EN_K1] = "Speed, BPM", [EN_K3] = "Send MIDI clock", "Clock to follow"},
    [PG_GAUD] = {[EN_K1] = "The speaker's sound, or off", "Master tune"},
    [PG_GCTL] = {[EN_K1] = "Touch curve", "Knob speed-up"},
    [PG_NAME] = {[EN_K1] = "Which letter", "Change it", "Clear to the end"},
    [PG_GDISP] = {[EN_PRESET] = "Zoom on what turns", [EN_K1] = "These hints", "Pop-up time"},
    [PG_GDRUM] = {"Drums' CC channel", [EN_K1] = "Drums' note channel"},
    [PG_GRND] = {[EN_PRESET] = "Pick a seed slot"},
    [PG_RPAT] = {"Lock patch type", "Lock track count", [EN_K1] = "Lock root note", "Lock root octave", "Lock scale"},
    [PG_RTRK] = {[EN_PRESET] = "Pick the knobs' row"},
};
static const char *hint_of(uint32_t k)       /* encoder k's here (as its page's switches are), or 0 */
{
    static const char *const PW_H[NPW] = {"Pulse width", "Envelope to width", "LFO 2 to width"};
    static const char *const MS_H[NMS] = {"Noise level", "Ring mod level", "USB audio level"};
    static const char *const ROW_H[NRR] = {"Track's engine", "Track's level", "Track's reverb"};
    static const char *const DT_H[NDTF] = {   /* PRESET's and ALGO's, on each drum function */
        [DTF_PAT] = "Pick the Pattern", [DTF_REC] = "Pick the Pattern", [DTF_ERROR] = "Timing grid, 96 = off", [DTF_SWING] = "Swing amount",
        [DTF_MEAS] = "Bars in the pattern", [DTF_ERASEP] = "Pattern to erase", [DTF_COPYP] = "Pattern to copy onto", [DTF_ROLL] = "Roll speed", [DTF_SONG] = "Pick the Song",
        [DTF_ERASES] = "Song to erase", [DTF_COPYS] = "Song to copy onto", [DTF_BUILD] = "Pick the Song", [DTF_BVOL] = "Volume change",
        [DTF_BTEMPO] = "Tempo change"};
    if (DRUM_PAGE(ui.page))
        return ui.page == PG_DMOD && k == EN_K3 ? "Bass follows Drums"
               : ui.page == PG_DMOD && k == EN_K4 ? "Drums start/stop Bass" : k == EN_SELECT ? "Tempo" : k == EN_K3 || k == EN_K4 ? DT_NAME[ui.dt_sel] : k == EN_K1 ? "Click level" : k == EN_K2 ? "Drums' level"
               : ui.dt_fn == DTF_TIME && k == EN_PRESET ? "Beats in a bar" : ui.dt_fn == DTF_TIME && k == EN_ALGO ? "The note that's a beat"
               : dt_knobs_split() && k == EN_PRESET ? "Pick the Step" : ui.dt_fn == DTF_BPAT && k == EN_ALGO ? "Pick the Pattern"
               : k == EN_PRESET || k == EN_ALGO ? DT_H[ui.dt_fn] : 0;
    if (ui.page == PG_MIX && k == EN_K4)
        return PW_H[ui.pw_src];
    if (ui.page == PG_FX && k == EN_K4)
        return MS_H[ui.mix_src];
    if (ui.page == PG_RTRK && k >= EN_K1)
        return ROW_H[rnd_row];
    if (ui.page == PG_LFO1 && ui.lfo_delay && (k == EN_K2 || k == EN_K3))
        return k == EN_K2 ? "LFO 1 fade-in" : "LFO 2 fade-in";
    return HINT[ui.page][k];
}
/* where a section's button goes, tapped on this page (its next page in the section: tap_page); NPAGES: no page (SEQ on
 * its page: recording) */
static uint32_t tap_page(uint32_t k)
{
    const uint32_t g = ui.page;
    switch (k) {
    case B_ENV:                              /* the envelopes; their triggering; the group's mods */
        return g == PG_AENV ? PG_MENV : g == PG_MENV ? PG_EMOD : PG_AENV;
    case B_LFO:
        return g == PG_LFO1 ? PG_LFO2 : PG_LFO1;
    case B_FX:                               /* the filters, LFO 2 on them, the effects, the group's mods */
        return g == PG_TUNE ? PG_FMOD : g == PG_FMOD ? PG_EFX : g == PG_EFX ? PG_XMOD : PG_TUNE;
    case B_EDIT:                             /* the oscillators, their pulse width, the sub osc and the mixer */
        return g == PG_OSC ? PG_MIX : g == PG_MIX ? PG_FX : PG_OSC;
    case B_ARP:                              /* the ARPEGGIATOR, again its mods */
        return g == PG_ARP ? PG_AMOD : PG_ARP;
    case B_SEQ:
        return g == PG_SEQ ? NPAGES : PG_SEQ;
    case B_GLO:                              /* on the RND pages their next; else the global settings' */
        if (RND_PAGE(g))
            return g == PG_GRND ? PG_RPAT : g == PG_RPAT ? PG_RTRK : PG_GRND;
        return g == PG_GMIDI ? PG_GDRUM : g == PG_GDRUM ? PG_GCLK : g == PG_GCLK ? PG_GAUD : g == PG_GAUD ? PG_GCTL
               : g == PG_GCTL ? PG_GDISP : g == PG_GDISP ? PG_GDUMP : PG_GMIDI;
    }
    return NPAGES;
}

/* the drum functions' round: SEQ's and ARP's next from f (dt_button) */
static uint32_t dt_seq_next(uint32_t f)
{
    if (dq.run && dq.rec)                    /* (in the loop: what's used in it) */
        return f == DTF_REC ? DTF_ERASEI : f == DTF_ERASEI ? DTF_ERROR : f == DTF_ERROR ? DTF_SWING : f == DTF_SWING ? DTF_ROLL : DTF_REC;
    if (dq.run)                              /* (playing: the number, the swing and the roll, nothing that arms it) */
        return f == DTF_SWING ? DTF_ROLL : f == DTF_ROLL ? DTF_PAT : DTF_SWING;
    return f < DTF_ROLL ? f + 1u : DTF_PAT;
}
static uint32_t dt_arp_next(uint32_t f)
{
    if (dq.run)
        return DTF_SONG;
    if (f >= DTF_BPAT)
        return f < DTF_BDEL ? f + 1u : DTF_BPAT;
    return f >= DTF_SONG && f < DTF_BUILD ? f + 1u : DTF_SONG;
}
static uint32_t dt_glo_next(uint32_t g)      /* the settings the two share, from the drums' own */
{
    return g == PG_GDRUM ? PG_GMIDI : g == PG_GMIDI ? PG_GCLK : g == PG_GCLK ? PG_GAUD
           : g == PG_GAUD ? PG_GCTL : g == PG_GCTL ? PG_GDISP : PG_GDRUM;
}
static const char *const DT_SHORT[NDT] = {"BASS", "SNARE", "RIM", "TOM 1", "TOM 2", "CRASH", "RIDE", "CLOSE", "OPEN",
                                          "CLAPS", "TAMB", "COW", "CABA"};   /* (a key's width) */
static const char *btn_hint(uint32_t k)      /* button k's, pressed on this page */
{
    static const char *const TAP_NAME[NPAGES] = {
        [PG_OSC] = "Oscillators", [PG_MIX] = "Pulse Width", [PG_FX] = "Sub + Mixer", [PG_AENV] = "Envelopes",
        [PG_MENV] = "Triggering", [PG_EMOD] = "Env Mods", [PG_LFO1] = "LFOs", [PG_LFO2] = "LFO Mods",
        [PG_TUNE] = "Filters", [PG_FMOD] = "Filter LFO 2", [PG_EFX] = "Effects", [PG_XMOD] = "FX Mods",
        [PG_ARP] = "Arp", [PG_AMOD] = "Arp Mods", [PG_SEQ] = "Sequencer", [PG_GMIDI] = "MIDI", [PG_GCLK] = "Clock", [PG_GAUD] = "Audio", [PG_GCTL] = "Controls", [PG_NAME] = "Patch Name",
        [PG_GDISP] = "Display", [PG_GDUMP] = "Dump", [PG_GRND] = "RND Seed", [PG_RPAT] = "RND Patch",
        [PG_RTRK] = "RND Tracks", [PG_GDRUM] = "Drums"};
    static char tap[48];                     /* "GLO tap: System, hold: RND Synth Companion" */
    uint32_t g;
    static const char *const H[NB] = {[B_HOME] = "The whole panel", "Save the patch", [B_PLAY] = "Play or stop", [B_OCTDN] = "Octave down",
                                      "Octave up"};
    static const char *const DH[NB] = {[B_HOME] = "The whole panel", [B_PLAY] = "Play or stop", [B_SAVE] = "Save the drums", [B_OCTDN] = "Tempo down",
                                       [B_OCTUP] = "Tempo up"};
    static const char *const FN_NAME[NDTF] = {   /* a function, as its button's tap goes there */
        [DTF_PAT] = "Patterns", [DTF_REC] = "Record", [DTF_ERASEI] = "Erase Instrument", [DTF_ERROR] = "Error Correct", [DTF_SWING] = "Swing",
        [DTF_TIME] = "Time Signature", [DTF_MEAS] = "Measures", [DTF_ERASEP] = "Erase Pattern", [DTF_COPYP] = "Copy Pattern", [DTF_ROLL] = "Roll", [DTF_SONG] = "Songs",
        [DTF_ERASES] = "Erase Song", [DTF_COPYS] = "Copy Song", [DTF_BUILD] = "Build Mode", [DTF_BPAT] = "Build Pattern", [DTF_BVOL] = "Build Volume",
        [DTF_BTEMPO] = "Build Tempo", [DTF_BINS] = "Build Insert", [DTF_BDEL] = "Build Delete"};
    static const char *const FN_DO[NDTF] = {   /* what the function lit waits for, after its button's next */
        [DTF_REC] = ", PLAY: record", [DTF_ROLL] = ", PLAY: record", [DTF_ERASEI] = ", hold a drum: erase", [DTF_ERASEP] = ", SEL: erase", [DTF_COPYP] = ", SEL: copy", [DTF_ERASES] = ", SEL: erase",
        [DTF_COPYS] = ", SEL: copy", [DTF_BUILD] = ", SEL: enter", [DTF_BPAT] = ", SEL: enter", [DTF_BVOL] = ", SEL: enter", [DTF_BTEMPO] = ", SEL: enter",
        [DTF_BINS] = ", SEL: insert", [DTF_BDEL] = ", SEL: delete"};
    static const char *const SEL_H[NDTF] = {   /* SEL's, on the function lit */
        [DTF_ERASEP] = "Erase the pattern", [DTF_COPYP] = "Copy the pattern", [DTF_ERASES] = "Erase the song", [DTF_COPYS] = "Copy the song",
        [DTF_BUILD] = "Enter Build Mode", [DTF_BPAT] = "Enter, or leave Build", [DTF_BVOL] = "Enter the step", [DTF_BTEMPO] = "Enter the step",
        [DTF_BINS] = "Insert a step", [DTF_BDEL] = "Delete the step"};
    const int rec = seq_rec && ui.page == PG_SEQ;
    if (k == B_HOME && ui.page == (ui.drum ? PG_DHOME : PG_HOME))   /* (on it: the other mode, mode_switch) */
        return ui.drum ? "HOME tap: Bass Something 2" : "HOME tap: Drum Something";
    if (ui.drum && !RND_PAGE(ui.page)) {     /* (every button the drums', as dt_button: the bass's form) */
        const uint32_t f = ui.dt_fn;
        const char *to = 0, *more = 0;
        switch (k) {
        case B_SEL:
            return dq.run ? 0 : SEL_H[f];
        case B_EDIT:
            to = DT_SHORT[DCH_PAGE(ui.page) ? (ui.dt_sel + 1u) % NDT : ui.dt_sel];
            break;
        case B_SEQ:
            to = ui.page == PG_DPAT ? FN_NAME[dt_seq_next(f)] : "Patterns";
            more = ui.page == PG_DPAT && (!dq.run || f == DTF_ERASEI) ? FN_DO[f] : 0;
            break;
        case B_ARP:
            to = ui.page == PG_DSONG ? FN_NAME[dt_arp_next(f)] : "Songs";
            more = ui.page == PG_DSONG && !dq.run ? FN_DO[f] : 0;
            break;
        case B_FX:
            to = ui.page == PG_DMOD ? "Pattern Info" : ui.page == PG_DPINFO ? "Song Info" : "Drum Mods";
            break;
        case B_ENV:
            to = f == DTF_MEAS ? "Time Signature" : "Measures";
            break;
        case B_GLO:
            to = TAP_NAME[dt_glo_next(ui.page)];
            more = ", hold: RND Synth Companion";
            break;
        case B_REC:
            to = ui.page == PG_GMIDI && FM1B_BLE_CENTRAL && !set.ble_off ? "Find BLE" : dq.rec == DQR_REC ? "Rec off" : "Rec on";
            more = dq.rec == DQR_ERASE ? ", hold a drum: erase" : dq.rec == DQR_REC && !dq.run ? ", PLAY: record" : ", hold: Erase";
            break;
        default:
            return DH[k];
        }
        tap[0] = 0;
        text_cat(tap, BTN_NAME[k]);
        text_cat(tap, " tap: ");
        text_cat(tap, to);
        if (more)
            text_cat(tap, more);
        return tap;
    }
    if (k == B_SEL)
        return rec ? "Tie last step" : ui.page == PG_GDUMP ? "Send this sound" : ui.page == PG_RPAT ? "Unlock everything"
               : ui.page == PG_GRND ? "Ask the RND Synth" : ui.page == PG_PATCH ? "Patch Name" : "Patch";
    if (k == B_FX && (rec || ui.page == PG_GDUMP || ui.page == PG_GRND))
        return rec ? "Rest step" : ui.page == PG_GDUMP ? "Send all 128" : "Send seed to RND";
    if (k == B_SAVE && ui.page == PG_GRND)
        return "Keep RND's patch";
    if ((g = tap_page(k)) < NPAGES || k == B_SEQ || k == B_REC) {   /* a section's (or REC): its next page (or what
                                              * a tap does), and what a hold does */
        tap[0] = 0;
        text_cat(tap, BTN_NAME[k]);
        text_cat(tap, " tap: ");
        text_cat(tap, g < NPAGES ? TAP_NAME[g] : k == B_REC && ui.page == PG_GMIDI ? "Find BLE" : seq_rec ? "Rec off"
                      : "Rec on");
        if (k == B_REC)
            text_cat(tap, ", hold: Randomize");
        else if (k == B_ARP || k == B_SEQ)
            text_cat(tap, ", hold: AFX mode");
        else if (k == B_GLO)
            text_cat(tap, RND_PAGE(ui.page) ? ", hold: Global" : ", hold: RND Synth Companion");
        return tap;
    }
    return H[k];
}

static void rec_toggle(void)                 /* recording on or off: the arp's REC, again PLAY */
{
    patch.arp_mode = seq_rec ? ARP_PLAY : ARP_REC;
    set_dirty = 1;
    app_say("REC", OFF_ON[seq_rec]);
}

static void hint_show(uint32_t k)            /* Hints: button k's, from the page it's on now */
{
    if (set.hints) {
        ui.hint = btn_hint(k);
        ui.hint_until = pop_until();
        ui.redraw = 1;
    }
}

static void dt_button(uint32_t k);
static void home_back(void);
static void dt_rec_tap(void);
static void dt_rec_erase(void);
/* TRANSPORT (DRUM MODS, on by default): the drums' run and stop reach the bass as MIDI's start and stop would: it plays its
 * sequence, or its arp if that's on, and stops with them; left as they found it */
static void dt_bass_transport(int run)
{
    if (seq_rec)
        return;
    if (run) {
        if (ui.dt_b_paused) {
            patch.arp_on = 1;
            ui.dt_b_paused = 0;
        } else if (!patch.arp_on) {
            ui.dt_b_saved = 1;
            ui.dt_b_mode = patch.arp_mode;
            patch.arp_mode = ARP_PLAY;
            patch.arp_on = 1;
            seq_auto = 1;
        } else if (patch.arp_mode == ARP_PLAY)
            seq_auto = 1;
    } else if (ui.dt_b_saved) {
        patch.arp_mode = ui.dt_b_mode;
        patch.arp_on = 0;
        seq_auto = 0;
        ui.dt_b_saved = 0;
    } else if (seq_playing()) {
        seq_auto = 0;
        patch.arp_on = 0;
    } else if (patch.arp_on && patch.arp_mode != ARP_REC) {
        patch.arp_on = 0;
        ui.dt_b_paused = 1;
    }
}
static void mode_switch(void);
static void sect_open(uint32_t g, uint32_t k);
static void hold_tap(uint32_t k)
{
    const char *const said = ui.hint;        /* (what the tap refuses, said over this) */
    if (k == B_GLO && ui.drum && !RND_PAGE(ui.page))
        dt_button(k);
    else if (k == B_REC && ui.drum && (ui.page != PG_GMIDI || !FM1B_BLE_CENTRAL || set.ble_off))
        dt_rec_tap();
    else if (k == B_HOME && ui.page == (ui.drum ? PG_DHOME : PG_HOME))   /* HOME on it: the other mode's */
        mode_switch();
    else if (k == B_HOME)                         /* HOME: the whole panel (the first page), the mode's */
        home_back();
    else if (k == B_REC && ui.page != PG_GMIDI)   /* recording, on any page (GLO MIDI's: a Bluetooth device) */
        rec_toggle();
    else if (k == B_REC && FM1B_BLE_CENTRAL && !set.ble_off)
        ble_device_button();
    else if (k == B_REC)
        app_say("BUTTON", "REC");
    else if (tap_page(k) < NPAGES)           /* ARP, SEQ, GLO: their section's next page (tap_page) */
        sect_open(tap_page(k), k);
    else                                     /* SEQ on its page: recording */
        rec_toggle();
    if (ui.hint == said)
        hint_show(k);                        /* (its hint again from where it went: the next tap's) */
}

/* HOME tapped on HOME, or held: the synth's mode or Drum Something's, on its HOME. From the other's HOME, the whole panel,
 * the camera pans there (mpan_band: the drums' panel under the synth's), its map after; from anywhere else (held), at once */
static void mode_switch(void)
{
    const int pan = ui.page == (ui.drum ? PG_DHOME : PG_HOME) && !zoom.z && !zoom.home;
    ui.drum = (uint8_t)!ui.drum;
    page_go(ui.drum ? PG_DHOME : PG_HOME);
    if (pan) {
        ui.mpan_at = ms_stamp();
        ui.map_until = 0;
    }
}

/* Drum Something's keys: its instruments from the low C up the white keys as the 400's panel has them (Bass ..
 * Tambourine, DT_*'s order; General MIDI's bass drum a C too), then Cowbell F#, Cabasa the top G; the first black key
 * ACCENT held */
static const uint8_t DT_PAD[NDT] = {7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 25, 26};
#define DT_ROLL_KEY 0u                       /* the lowest F: held, the selected drum rolls (dq_roll_on) */
static void dt_roll(uint32_t d, uint32_t v, uint32_t keys)
{
    static const uint8_t BK[11] = PLAT_BLACK_KEYS;
    dq_cmd(DQC_ROLL, d, v | (keys >> BK[0] & 1u) << 7);
}
static void dt_key(uint32_t k, uint32_t v, uint32_t keys)
{
    static const uint8_t BK[11] = PLAT_BLACK_KEYS;
    uint32_t i;
    if (k == DT_ROLL_KEY) {
        dt_roll(ui.dt_sel, v, keys);
        return;
    }
    for (i = 0; i < NDT; i++)
        if (DT_PAD[i] == k) {
            const uint32_t acc = keys >> BK[0] & 1u;
            if (dq.rec == DQR_ERASE)         /* (silent, as the manual: it takes the drum's hits out) */
                dq_cmd(DQC_EKEY, i, 0);
            else {
                dt_play(i, v, (int)acc);
                ui.och[k] = (uint8_t)(set.dchan == CH_ALL ? 1u : set.dchan);   /* (out at its velocity, an accent 120 up: as dt_note takes them) */
                ui.dno[k] = DT_OUT[i];
                key_send(0x90u, DT_OUT[i], acc ? (v > 120u ? v : 120u) : v < 119u ? v : 119u, ui.och[k]);
                if (dq.rec == DQR_REC && dq.run)
                    dq_cmd(DQC_HIT, i, acc);
                else if (dq.rec == DQR_REC && ui.dt_pat < DQ_NPAT) {   /* (stopped: its volume and tuning programmed) */
                    dqm.pat[ui.dt_pat].prog |= (uint16_t)(1u << i);
                    dq.dirty = 1;
                }
            }
            ui.dt_sel = (uint8_t)i;          /* (tapped: VOLUME and TUNING are its now) */
            if (keys >> DT_ROLL_KEY & 1u)    /* (and what's rolling) */
                dt_roll(i, ui.vel[DT_ROLL_KEY], keys);
            if (zoom.home && zoom.home != ZH_OPEN && zoom.home != ZH_CLOSE && zoom.hp == PG_DHOME) {   /* (on a dive: the tap's, there and back) */
                zoom.dnew = zoom.dsv = (uint8_t)i;
                if (ui.page == PG_DHOME && zoom.g != PG_DCH1 + DT_CH[i])   /* (still on HOME, another channel's: no dive) */
                    zoom.home = ZH_NONE;
            }
            if (DCH_PAGE(ui.page) && ui.page != PG_DCH1 + DT_CH[i])   /* (a Channel page: its channel's) */
                page_go(PG_DCH1 + DT_CH[i]);
        }
}

/* the keys lit in Drum Something's mode: each as it's hit; on a Channel page the drum selected, and at the lights'
 * middle level the rest of its channel */
static uint32_t dt_lit(void);
static uint32_t dt_keys_lit(uint32_t *mid)
{
    const uint32_t hit = dt_lit();
    uint32_t i, m = 0;
    *mid = 0;
    m |= ui.keys & 1u << DT_ROLL_KEY;
    for (i = 0; i < NDT; i++) {
        if (hit >> i & 1u)
            m |= 1u << DT_PAD[i];
        if (DCH_PAGE(ui.page) && DT_CH[i] == ui.page - PG_DCH1) {
            if (i == ui.dt_sel)
                m |= 1u << DT_PAD[i];
            else
                *mid |= 1u << DT_PAD[i];
        }
    }
    return m;
}

/* Drum Something's buttons, on all its pages and its GLO's: none does what it does for the synth */
static void dt_fn_go(uint32_t f)
{
    ui.dt_fn = (uint8_t)f;
    dt_show(DTS_FN);
    zoom.fn_at = ms_stamp();
    zoom.fn_ok = 0;
}
static void dt_list(uint32_t f)              /* the pattern list's: its function, and what that arms */
{
    if (f == DTF_ERASEP || f == DTF_COPYP)
        ui.dt_tgt = ui.dt_pat < DQ_NPAT ? ui.dt_pat : 0;
    if (ui.dt_pat < DQ_NPAT && !((f == DTF_SWING || f == DTF_ROLL) && dq.run))   /* (at the swing or the roll as it runs: recording or not, as it was) */
        dq.rec = f == DTF_REC || f == DTF_ERROR || f == DTF_ROLL ? DQR_REC : f == DTF_ERASEI ? DQR_ERASE : DQR_OFF;
    dt_fn_go(f);
}
static void dt_copied(uint32_t f, int added)  /* SEL on COPY / APPEND TO, done: said, back at list's number f (refused: said,
                                              * still on the function) */
{
    static char done[14];
    const char two[3] = {(char)('0' + ui.dt_tgt / 10u), (char)('0' + ui.dt_tgt % 10u), 0};
    done[0] = 0;
    text_cat(done, added ? "Added to " : "Copied to ");
    text_cat(done, two);
    dt_fn_go(f);
    dt_refuse(done);
}
static void dt_copy(void)
{
    const char *const no = dq_copy_no(ui.dt_pat, ui.dt_tgt);
    if (no) {
        dt_refuse(no);
        return;
    }
    dt_copied(DTF_PAT, dqm.pat[ui.dt_tgt].n != 0u);
    dq_cmd(DQC_COPY, ui.dt_pat, ui.dt_tgt);
}
static void dt_copy_song(void)
{
    const char *const no = ds_copy_no(ui.dt_song, ui.dt_tgt);
    const int added = ds_end(&dsm.s[ui.dt_tgt]) != 0u;
    if (no) {
        dt_refuse(no);
        return;
    }
    ds_copy(ui.dt_song, ui.dt_tgt);
    dt_copied(DTF_SONG, added);
}
static void dt_rec_tap(void)                 /* REC: recording on or off, it plays on; from Erase, back to recording */
{
    if (ui.dt_pat >= DQ_NPAT) {
        dt_refuse("99 can't record");
        return;
    }
    dq.rec = dq.rec == DQR_REC ? DQR_OFF : DQR_REC;
    dt_fn_go(dq.rec ? DTF_REC : DTF_PAT);
}
static void dt_rec_erase(void)               /* REC held: Erase Instrument (a tap, recording again) */
{
    if (ui.dt_pat >= DQ_NPAT) {
        dt_refuse("99 can't record");
        return;
    }
    dq.rec = DQR_ERASE;
    dt_fn_go(DTF_ERASEI);
}
/* ENTER on a song function: stopped, the main loop owns the songs */
static void dt_song_enter(uint32_t f)
{
    ds_song_t *t = &dsm.s[ui.dt_song];
    const uint32_t end = ds_end(t);
    uint32_t i;
    switch (f) {
    case DTF_BUILD:
        ui.dt_step = 0;
        ui.dt_pend = 0;
        dt_fn_go(DTF_BPAT);
        break;
    case DTF_BPAT:                           /* a value in: that step, the next; none: out */
        if (!ui.dt_pend) {
            dt_fn_go(DTF_SONG);
            break;
        }
        if (ui.dt_step == end && end + 1u < DS_NSTEP)   /* (the end moves on) */
            t->step[end + 1u] = DS_END;
        if (ui.dt_step < DS_NSTEP - 1u || t->step[ui.dt_step] != DS_END) {
            t->step[ui.dt_step] = (uint8_t)ui.dt_ent;
            if (ui.dt_step < DS_NSTEP - 1u)
                ui.dt_step++;
        }
        ui.dt_pend = 0;
        dq.dirty = 1;
        dt_show(DTS_FN);
        break;
    case DTF_BVOL:
    case DTF_BTEMPO:
        if (ui.dt_pend && f == DTF_BTEMPO && !ui.dt_step)
            t->tempo = ui.dt_ent ? DQ_TEMPO[ui.dt_ent - 1] : 0;
        else if (ui.dt_pend && (ui.dt_step || f == DTF_BVOL) && ui.dt_step < DS_NSTEP - 1u) {
            if (ui.dt_step == end)
                t->step[end + 1u] = DS_END;
            t->step[ui.dt_step] = (uint8_t)((f == DTF_BVOL ? DS_VOL : DS_TEMPO) | ((uint32_t)ui.dt_ent & 0x1Fu));
            ui.dt_step++;
        }
        ui.dt_pend = 0;
        dq.dirty = 1;
        dt_fn_go(DTF_BPAT);
        break;
    case DTF_BINS:                           /* a blank step here (filled, or ignored as it plays) */
        if (end + 1u < DS_NSTEP) {
            for (i = end + 1u; i > ui.dt_step; i--)
                t->step[i] = t->step[i - 1u];
            t->step[ui.dt_step] = DS_EMPTY;
            dq.dirty = 1;
        }
        dt_fn_go(DTF_BPAT);
        break;
    case DTF_BDEL:
        if (ui.dt_step < end) {
            for (i = ui.dt_step; i + 1u < DS_NSTEP; i++)
                t->step[i] = t->step[i + 1u];
            t->step[DS_NSTEP - 1u] = DS_END;
            dq.dirty = 1;
        }
        dt_fn_go(DTF_BPAT);
        break;
    default:
        break;
    }
}
static void dt_open(uint32_t g, uint32_t k);
static void dt_button(uint32_t k)
{
    uint32_t f = ui.dt_fn;
    switch (k) {
    case B_EDIT:                             /* the Channel pages: the selected drum's, then the next drum; a new page only at
                                              * the next channel */
        if (DCH_PAGE(ui.page)) {
            const uint32_t c = DT_CH[ui.dt_sel];
            ui.dt_sel = (uint8_t)((ui.dt_sel + 1u) % NDT);
            if (DT_CH[ui.dt_sel] == c) {
                ui.redraw = 1;
                break;
            }
        }
        dt_open(PG_DCH1 + DT_CH[ui.dt_sel], k);
        break;
    case B_SEQ:                              /* the 400's SELECT PATTERN FUNCTIONS: running in RECORD MODE, round what's
                                              * used in the loop (Record, Erase Instrument, Error Correct, and Swing, ours);
                                              * stopped, RECORD MODE and those two arm it */
        zoom.fn_at = ms_stamp();         /* (each press: the camera on the function lit) */
        zoom.fn_ok = 0;
        if (ui.page != PG_DPAT) {            /* first, its page: the list as it is (from the songs', the number) */
            ui.dt_pend = 0;
            if (f >= DTF_SONG)
                dt_fn_go(DTF_PAT);
            dt_open(PG_DPAT, k);
        } else
            dt_list(dt_seq_next(f));
        break;
    case B_ARP:                              /* its SELECT SONG FUNCTIONS (BUILD MODE: its own five); not while it runs: only
                                              * the number */
        ui.dt_pend = 0;
        zoom.fn_at = ms_stamp();
        zoom.fn_ok = 0;
        if (ui.page != PG_DSONG) {           /* first, its page: the list as it is (from the patterns', the number) */
            if (f < DTF_SONG) {
                if (!dq.run)
                    dq.rec = DQR_OFF;
                ui.dt_tgt = ui.dt_song < DS_NSONG ? ui.dt_song : 0;
                dt_fn_go(DTF_SONG);
            }
            dt_open(PG_DSONG, k);
        } else if (dq.run || f >= DTF_BPAT)
            dt_fn_go(dt_arp_next(f));
        else {
            if (f < DTF_SONG)
                dq.rec = DQR_OFF;
            ui.dt_tgt = ui.dt_song < DS_NSONG ? ui.dt_song : 0;
            dt_fn_go(dt_arp_next(f));
        }
        break;
    case B_SEL:                              /* its ENTER; what the two digits do on the 400: erase, copy or append
                                              * (stopped), back at the number */
        if (dq.run)
            break;
        if (f == DTF_COPYS)
            dt_copy_song();
        else if (f == DTF_ERASES) {          /* (its target's, whichever song is shown: 99's too) */
            dsong_clear(&dsm.s[ui.dt_tgt]);
            dq.dirty = 1;
            dt_fn_go(DTF_SONG);
        } else if (f >= DTF_SONG && ui.dt_song < DS_NSONG)
            dt_song_enter(f);
        else if (f == DTF_ERASEP) {
            dq_cmd(DQC_ERASE, ui.dt_tgt, 0);
            dt_fn_go(DTF_PAT);
        } else if (f == DTF_COPYP)
            dt_copy();
        break;
    case B_PLAY:                             /* RUN/STOP: an empty pattern won't run but in RECORD MODE; a function that
                                              * needs it stopped gives way to the number */
        if (dq.run)
            dq_cmd(DQC_STOP, 0, 0);
        else if (f >= DTF_SONG) {            /* song functions lit: the song (BUILD MODE: ENTER out first) */
            if (f >= DTF_BPAT)
                dt_refuse("Press SEL to leave Build");
            else if (ui.dt_song >= DS_NSONG || !ds_end(&dsm.s[ui.dt_song]))
                dt_refuse("That song is empty");
            else {
                ui.dt_fn = DTF_SONG;
                dq_cmd(DQC_SONG, ui.dt_song, 0);
            }
        } else if (ui.dt_pat < DQ_NPAT && (dq.rec || dqm.pat[ui.dt_pat].n)) {
            if (f > DTF_SWING && f < DTF_ROLL)
                ui.dt_fn = DTF_PAT;
            dq_cmd(DQC_RUN, ui.dt_pat, 0);
        }
        break;
    case B_SAVE:                             /* the drums' memory (a flash write stops the audio a moment: stopped) */
        if (dq.run)
            dt_refuse("Stop first");
        else if (dq_save() == 0)
            app_say("DRUMS", "SAVED");
        else
            app_say("SAVE", "FAILED");
        break;
    case B_OCTDN:                            /* TEMPO, SLOWER and FASTER (the one clock's, MIDI's while it leads) */
    case B_OCTUP:
        if (!dq.run && (ui.btn_in & DT_BOTH_OCT) == DT_BOTH_OCT) {   /* both, stopped: the 400's INC + DEC, % memory remaining
                                              * on the left display while held; the first one's step undone */
            static char t[20];
            if (ui.btn >> (k == B_OCTUP ? B_OCTDN : B_OCTUP) & 1u) {
                dt.tempo = ui.dt_oct_tempo;
                dq.dirty = ui.dt_oct_dirty;
            }
            ui.dt_show = DTS_NONE;
            t[0] = 0;
            int_text((int32_t)dt_mem_free(), t), text_cat(t, "% memory free");
            dt_refuse(t);
            break;
        }
        ui.dt_oct_tempo = dt.tempo;
        ui.dt_oct_dirty = dq.dirty;
        dt_tempo_step(k == B_OCTUP ? 1 : -1);
        break;
    case B_FX:                               /* the mods, then the plates with more to read */
        page_go(ui.page == PG_DMOD ? PG_DPINFO : ui.page == PG_DPINFO ? PG_DSINFO : PG_DMOD);
        break;
    case B_ENV:                              /* the pattern's length, straight there: its bars, again its time signature */
        if (dq.run)
            dt_refuse("Stop to change length");
        else if (ui.dt_pat < DQ_NPAT) {
            dt_list(f == DTF_MEAS ? DTF_TIME : DTF_MEAS);
            if (dqm.pat[ui.dt_pat].n)
                dt_refuse("Has notes: erase to change");
        }
        break;
    case B_GLO:                              /* the settings the two share, from the drums' own (no DUMP: only the
                                              * synth's so far) */
        page_go(dt_glo_next(ui.page));
        break;
    default:
        break;
    }
    ui.redraw = 1;
}

static int dt_blink(void) { return dq.run && dt_pos < 4.0f; }   /* the 400's blink on each loop's first beat */
/* REC's light: recording, steady (PLAY has the beat); erasing, two quick pulses each beat (stopped: on the clock's time) */
static float dt_beat_phase(void)             /* where in its beat the running pattern is, 0..1 */
{
    const dq_pat_t *p = &dqm.pat[dq.cur < DQ_NPAT ? dq.cur : 0];
    const float bt = (float)(96u / DQ_BEAT[p->beat]), b = dt_pos / bt;
    return b - (float)(uint32_t)b;
}
static int dt_play_light(void) { return dq.run && dt_beat_phase() < 0.3f; }   /* PLAY: the tempo, a pulse each beat */
static int dt_rec_light(void)
{
    float ph;
    if (dq.rec != DQR_ERASE)
        return dq.rec == DQR_REC;
    ph = dq.run ? dt_beat_phase() : (float)(plat_ms() % 600u) / 600.0f;
    return ph < 0.12f || (ph >= 0.24f && ph < 0.36f);
}
static void hold_step(uint32_t btn)
{
    if (!ui.hold_live)
        return;
    if (!(btn >> ui.hold_k & 1u)) {
        ui.hold_live = 0;
        hold_tap(ui.hold_k);
    } else if (plat_ms() - ui.hold_at >= HOLD_MS) {
        ui.hold_live = 0;
        if (ui.hold_k == B_HOME)
            mode_switch();
        else if (ui.hold_k == B_REC && ui.drum) {
            dt_rec_erase();
            if (dq.rec == DQR_ERASE)
                hint_show(B_REC);
        }
        else if (ui.hold_k == B_REC)
            randomize();
        else if (ui.hold_k != B_GLO) {
            afx_on = (uint8_t)!afx_on;
            app_say("AFX", OFF_ON[afx_on]);
        } else if (RND_PAGE(ui.page))
            page_go(ui.drum ? PG_GDRUM : PG_GMIDI);
        else {                               /* the RND pages (bs2_rnd.c), the synth asked how it is as they open;
                                              * their splash first (rsplash_draw) */
            rnd_ask();
            page_go(PG_GRND);
            rsplash_until = plat_ms() + RSPLASH_MS;
        }
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, lit, below, above, mid;
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
            if (ui.drum) {                   /* Drum Something's: its instruments (dt_key) */
                dt_key(k, v, keys);
                continue;
            }
            if (set.local)                   /* (LOCAL off: MIDI out alone) */
                fifo_put(&keyq, (uint32_t)note | v << 8);
            if (seq_rec)
                step_enter((uint32_t)note);
            ui.och[k] = (uint8_t)(set.chan == CH_ALL ? 1u : set.chan);
            key_send(0x90u, (uint32_t)note, v, ui.och[k]);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            ui.prs[k] = 0;
            if (k == DT_ROLL_KEY)            /* (in either mode: HOME may have left the drums while it rolled) */
                dq_cmd(DQC_ROLLOFF, 0, 0);
            fifo_put(&keyq, (uint32_t)note);  /* (LOCAL turned off while it was held: still let go) */
            key_send(0x80u, ui.dno[k] ? ui.dno[k] : (uint32_t)note, 0, ui.och[k]);
            ui.och[k] = ui.dno[k] = 0;
        } else if ((keys & bit) && touch_pressure(k) != ui.prs[k]) {   /* pressure: poly aftertouch */
            ui.prs[k] = touch_pressure(k);
            ui.redraw = 1;
            if (set.at_out && !ui.dno[k])
                key_send(0xA0u, (uint32_t)note, ui.prs[k], ui.och[k]);
        }
    }
    ui.btn_in = btn;
    for (k = 0; k < NB; k++) {               /* a button pressed: its name, and what it does here */
        uint32_t bit = 1u << k;
        const char *said;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        rsplash_until = 0;                   /* (the RND splash: over) */
        ui.mpan_at = ui.mpan_at ? 1u : 0u;   /* (the camera's pan: over, app_frame's next look) */
        if (zoom.home == ZH_OPEN) {          /* (a press as it dives: there at once) */
            zoom.z = 0;
            page_go(zoom.g);
        }
        zoom_close_end();
        zoom.settle = 0;
        hint_show(k);                        /* (Hints: what it does, from the page it's pressed on) */
        said = ui.hint;
        if (ui.drum && k != B_HOME && k != B_REC && k != B_GLO
            && !(RND_PAGE(ui.page) && (k == B_SEL || k == B_FX || k == B_SAVE)))   /* (the RND pages keep their own) */
            dt_button(k);
        else if (k == B_ARP || k == B_SEQ || k == B_GLO || k == B_REC || k == B_HOME) {   /* on release, or once held
                                              * HOLD_MS (hold_step) */
            ui.hold_k = (uint8_t)k;
            ui.hold_live = 1;
            ui.hold_at = plat_ms();
        } else if (k == B_OCTDN)
            octave_step(-1);
        else if (k == B_OCTUP)
            octave_step(1);
        else if (seq_rec && ui.page == PG_SEQ && k == B_FX)   /* recording, on SEQ: FX a rest (the Bass Station
                                              * II's REST), SEL the step before legato (its LEGATO); elsewhere their
                                              * own, so REC left on doesn't take them */
            step_enter(SEQ_REST);
        else if (seq_rec && ui.page == PG_SEQ && k == B_SEL)
            step_legato();
        else if (k == B_SEL && ui.page == PG_GDUMP)   /* GLO DUMP's: the sound out as a Bass Station II patch dump */
            sysex_send();
        else if (k == B_SEL && ui.page == PG_RPAT)   /* every lock let go */
            rnd_free_all();
        else if (k == B_SEL && ui.page == PG_GRND) {   /* the synth asked again */
            rnd_ask();
            app_say("RND", "ASKED");
        } else if (k == B_SEL)                   /* the MASTER: you select a patch there; again, its mod: the name */
            sect_open(ui.page == PG_PATCH ? PG_NAME : PG_PATCH, k);
        else if (k == B_SAVE && ui.page == PG_GRND)   /* the seed shown into its slot */
            rnd_store();
        else if (k == B_SAVE)
            save();
        else if (k == B_ENV || k == B_LFO || k == B_EDIT)   /* their section's next page (tap_page; Env Select
                                                  * stays as it is: SELECT's) */
            sect_open(tap_page(k), k);
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
        } else if (k == B_FX && ui.page == PG_GDUMP)   /* every slot's */
            sysex_dump_all();
        else if (k == B_FX && ui.page == PG_GRND)    /* the seed shown, as the RND Synth's SysEx */
            rnd_send();
        else if (k == B_FX)                      /* the filters, LFO 2 on them, the effects, the group's mods */
            sect_open(tap_page(k), k);
        else {
            app_say("BUTTON", BTN_NAME[k]);
        }
        if (ui.hint == said && (k == B_ENV || k == B_LFO || k == B_EDIT || k == B_FX || (ui.drum && (k == B_ARP || k == B_SEQ))))   /* (a section's:
                                                  * its hint again from the page it went to, the next tap's; what a button
                                                  * refused stays) */
            hint_show(k);
    }
    hold_step(btn);
    if (ui.dt_was && !dq.run) {              /* stopped (STOP, or an empty cue): out of RECORD MODE, the number */
        dq.rec = DQR_OFF;
        dq.full = 0;
        if (ui.dt_fn < DTF_SONG)
            ui.dt_fn = DTF_PAT;
        ui.redraw = 1;
    }
    if (ui.dt_was != dq.run && set.clk_out && !ext_clock())   /* (SEND CLK: the drums the master, start and stop out) */
        plat_midi_send(0x0Fu | (dq.run ? 0xFAu : 0xFCu) << 8, TO_USB | TO_BLE);
    if (ui.dt_was != dq.run && !set.dtrans_off)
        dt_bass_transport(dq.run);
    ui.dt_was = dq.run;
    dq.sel = ui.dt_pat;
    dq.sel_song = ui.dt_fn >= DTF_SONG && ui.dt_song < DS_NSONG && ds_end(&dsm.s[ui.dt_song]) ? ui.dt_song : DQ_NONE;
    {                                        /* the drums' keys held: what Erase Instrument takes out as it passes */
        uint32_t h = 0;
        for (k = 0; k < NDT; k++)
            if (ui.drum && keys >> DT_PAD[k] & 1u)
                h |= 1u << k;
        dq.held = h;
    }
    if (dt_cc_last) {                        /* the drums' CCs: from their HOME, Knob Zoom's dive to the drum's channel */
        const uint32_t c = dt_cc_last;
        dt_cc_last = 0;
        ui.redraw = 1;
        if (ui.drum && !set.zoom_off && (ui.page == PG_DHOME || zoom.home))
            zoom_dt_cc(c);
    }
    for (k = 0; k < NE; k++) {               /* an encoder turned (physical k): what it sets, and its count */
        char t[12];
        uint32_t l;
        if ((e = plat_enc((int)k)) == 0)
            continue;
        ui.turns[k] += e;
        rsplash_until = 0;
        ui.mpan_at = ui.mpan_at ? 1u : 0u;
        if (zoom.home == ZH_OPEN) {          /* (a section's page on its way: there at once, the knob's) */
            zoom.z = 0;
            page_go(zoom.g);
        }
        zoom_close_end();
        if (home_knob(k)) {
            home_knob_turn(k, e);
            continue;
        }
        if ((l = enc_logical(k)) >= NE)      /* (none on this page) */
            continue;
        ui.turned_at[l] = plat_ms();
        zoom.fn_at = 0;                      /* (a knob: the camera on it) */
        zoom.home = ZH_NONE;                 /* (HOME's CC's page: kept, the user's now) */
        ui.map_until = ui.hint_until = 0;    /* (a knob turned: the map goes, and a button's hint; its value stays) */
        zoom.settle = 0;
        ui.last_enc = (uint8_t)l;
        ui.val_until = pop_until();
        ui.redraw = 1;
        if (l == EN_ALGO && FM1B_BLE_CENTRAL && ui.looking && ui.page == PG_GMIDI) {
            btdev_choose(btdev_pick() + e);
            ble_device_say();
        } else if (knob_turn(l, e)) {
            knob_text(l, t);
            app_say(knob_title(l), t);
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
    if (!backup_held)
        bank_in_step();
    if (!backup_held)
        seq_keep(0);
    if (!backup_held)
        glo_keep();
    rnd_step();
    lit = notes_lit(&below, &above);
    mid = 0;
    if (ui.drum)
        lit = dt_keys_lit(&mid);
    if (lit != ui.lit)
        ui.redraw = 1;
    ui.lit = lit;
    plat_glow(1);                            /* the unlit buttons glow, readable in the dark */
    plat_play_red(ui.drum && dq.rec);        /* (PLAY: red while it records) */
    if (ui.drum)
        plat_leds(btn | (dq.dirty ? 1u << B_SAVE : 0u) | (uint32_t)dt_play_light() << B_PLAY
                  | (uint32_t)dt_rec_light() << B_REC, keys | lit);
    else
        plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u) | (uint32_t)patch.arp_on << B_ARP | (uint32_t)seq_rec << B_SEQ
                  | (uint32_t)seq_rec << B_REC              | (uint32_t)seq_playing() << B_PLAY | below << B_OCTDN | above << B_OCTUP, keys | lit);   /* (OCT-, OCT+: a
                                              * note held out of the keys' reach, that way) */
    if (mid || ui.dt_mid)                    /* (a channel's other keys; none: let go once) */
        plat_lights_mid(0, mid);
    ui.dt_mid = mid;
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
/* the firmware's version as shown (the splash, GLO DISPLAY): a release's "v0.6", a development build's the last release
 * and "DEV" ("v0.6 DEV": built after it; BS2_RELEASE bumped with each release) */
#define BS2_RELEASE "0.7.2"
#define VERSION_TEXT (APP_VERSION[0] >= '0' && APP_VERSION[0] <= '9' ? "v" APP_VERSION : "v" BS2_RELEASE " " APP_VERSION)
static void ui_start(void) { ui.page = PG_HOME; }   /* the first page, after the splash: HOME */

/* the tabs: the buttons, each its pages: SEL the MASTER, EDIT the oscillators and the mixer, FX the filters and the
 * effects */
enum { SEC_FX, SEC_SEL, SEC_ENV, SEC_LFO, SEC_ARP, SEC_SEQ, SEC_EDIT, SEC_GLO, NSEC };
static const char *const SEC_NAME[NSEC] = {"FX", "SEL", "ENV", "LFO", "ARP", "SEQ", "EDIT", "GLO"};
static const uint8_t PAGE_SEC[NPAGES] = {[PG_OSC] = SEC_EDIT, [PG_MIX] = SEC_EDIT, [PG_MENV] = SEC_ENV,
                                         [PG_LFO1] = SEC_LFO, [PG_LFO2] = SEC_LFO, [PG_PATCH] = SEC_SEL, [PG_NAME] = SEC_SEL,
                                         [PG_ARP] = SEC_ARP, [PG_SEQ] = SEC_SEQ, [PG_AENV] = SEC_ENV, [PG_FX] = SEC_EDIT,
                                         [PG_TUNE] = SEC_FX, [PG_FMOD] = SEC_FX,
                                         [PG_EMOD] = SEC_ENV, [PG_EFX] = SEC_FX, [PG_XMOD] = SEC_FX, [PG_AMOD] = SEC_ARP, [PG_HOME] = NSEC,
                                         [PG_GMIDI] = SEC_GLO, [PG_GCLK] = SEC_GLO, [PG_GAUD] = SEC_GLO, [PG_GCTL] = SEC_GLO, [PG_GDUMP] = SEC_GLO, [PG_GDISP] = SEC_GLO,
                                         [PG_GRND] = SEC_GLO, [PG_RPAT] = SEC_GLO,
                                         [PG_RTRK] = SEC_GLO, [PG_GDRUM] = SEC_GLO, [PG_DHOME] = NSEC,
                                         [PG_DCH1] = SEC_EDIT, [PG_DCH2] = SEC_EDIT, [PG_DCH3] = SEC_EDIT,
                                         [PG_DCH4] = SEC_EDIT, [PG_DCH5] = SEC_EDIT, [PG_DCH6] = SEC_EDIT, [PG_DMOD] = SEC_FX, [PG_DPAT] = SEC_SEQ, [PG_DSONG] = SEC_ARP, [PG_DPINFO] = SEC_FX, [PG_DSINFO] = SEC_FX};
/* the tabs span the panel's bar (x 2..235), a pixel between each; they end 3 rows over it, as its gaps are wide */
#define TAB_X(i) (2 + (int32_t)(i) * 235 / NSEC)
static uint8_t ycol[232] PLAT_POOL;          /* a picture's curve: each column's row */

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
#define RND_H 220                              /* the RND pages': the keys' strip theirs too (rows 20..239), room for
                                               * larger print */
static int32_t pan_h = TALL_H;                 /* the panel being drawn: its height */
static const felucca_font_t *pr_font = &FONT_T;   /* print_c's: the RND pages' FONT_XS */
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
static void slide(int32_t x, int32_t y, int32_t n, int32_t pos, uint32_t f, int hz)   /* f: KF_LIT, KF_DIM; hz: lying down */
{
    int32_t len = 9 * n + 15, i, e, cx = x + (hz ? 11 + pos * 10 : 9), cy = y + (hz ? 9 : 11 + pos * 10);
    for (i = 0; i < 18; i++) {
        int32_t m = i < 17 - i ? i : 17 - i;
        uint16_t c = grey((f & KF_DIM ? 60 : 114) + (f & KF_DIM ? 40 : 93) * (m > 5 ? 5 : m) / 5);
        e = chord(9, i < 9 ? i - 9 : i - 8);
        if (hz)
            cv_rect(x + 9 - e, y + i, len - 18 + 2 * e, 1, c);
        else
            cv_rect(x + i, y + 9 - e, 1, len - 18 + 2 * e, c);
    }
    cv_rect(x + (hz ? 3 : 2), y + (hz ? 2 : 3), hz ? len - 6 : 14, hz ? 14 : len - 6, P_KSHADOW);
    disc(cx, cy, 7, f & KF_LIT ? P_CYAN : f & KF_DIM ? P_DIM : RGB(224, 226, 231));
    for (i = -6; i <= 6; i++) {
        e = chord(6, i);
        cv_rect(cx + i, cy - e, 1, 2 * e + 1, (i + 6) / 2 & 1 ? RGB(16, 17, 29) : RGB(53, 54, 65));
    }
}
static void slide_v(int32_t x, int32_t y, int32_t n, int32_t pos, uint32_t f) { slide(x, y, n, pos, f, 0); }
/* the same lying down at x, y: pos 0 at the left */
static void slide_h(int32_t x, int32_t y, int32_t n, int32_t pos, uint32_t f) { slide(x, y, n, pos, f, 1); }
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
/* Knob Zoom: an encoder turned, the panel's camera closes in on its control (where its tag goes: tag_v) to ZOOM_MAX,
 * stays while it turns and goes back out ZOOM_HOLD_MS after the last turn; the scale and the focus each move a share
 * of the way a frame (damped, ZOOM_IN_MS / ZOOM_OUT_MS), so a second knob pans to its own. Drawn by scaling each half
 * of the panel up within the canvas itself (zoom_blow: no buffer of its own) */
static int32_t zoom_ease(int32_t v, int32_t to, uint32_t dt, uint32_t tau)   /* v a share of the way to to */
{
    const int32_t d = (to - v) * (int32_t)(dt < tau ? dt : tau) / (int32_t)tau;
    return v + (d ? d : to > v ? 1 : to < v ? -1 : 0);
}
/* HOME's Knob Zoom: a MIDI CC there dives into its control's section page, one camera move: HOME closes in on the
 * section (its middle) until it fills the screen as its page does, then that page is up (its views as the control
 * needs, the control as though its encoder turned), going on in to 2x on the control; it stays while CCs come, then
 * the same way back out to HOME, its views as they were. The scale goes in log steps along a smoothstep (zoom.u), the
 * page changing where HOME's scale is the page's 1x. A turn or a page of the user's own ends it there */
static void home_sect(uint32_t g, int16_t *x, int16_t *y, float *lc);
static void zoom_views(const uint8_t *v)
{
    ui.osc_sel = v[0], ui.env_sel = v[1], ui.mix_src = v[2], ui.pw_src = v[3], ui.lfo_delay = v[4];
}
static uint32_t zoom_find(const param_t *q, uint8_t *v, uint32_t *kp)   /* q's section page (PG_HOME: none), its
                                                * encoder, the views it needs there (from HOME's, zoom.sv) */
{
    const uint8_t pg = ui.page, was[5] = {ui.osc_sel, ui.env_sel, ui.mix_src, ui.pw_src, ui.lfo_delay};
    uint32_t g = PG_HOME, k = 0, i, ok;
    for (i = 0; i < 5u; i++)
        v[i] = zoom.sv[i];
    if (q >= &PARAMS[0][0] && q < &PARAMS[PG_HOME][0]) {   /* (a page's own: its views at their first) */
        i = (uint32_t)(q - &PARAMS[0][0]);
        g = i / NE, k = i % NE;
        v[0] = v[4] = 0;
        if (v[1] == 1u)
            v[1] = 0;
    } else if (q >= OSC2_P && q < OSC2_P + NE) {
        g = PG_OSC, k = (uint32_t)(q - OSC2_P), v[0] = 1;
    } else if (q >= MENV_P && q < MENV_P + NE) {
        g = PG_AENV, k = (uint32_t)(q - MENV_P), v[1] = 1;
    } else if (q >= EMODM_P && q < EMODM_P + NE) {
        g = PG_EMOD, k = (uint32_t)(q - EMODM_P), v[1] = 1;
    } else if (q >= MIX_SRC && q < MIX_SRC + NMS) {
        g = PG_FX, k = EN_K4, v[2] = (uint8_t)(q - MIX_SRC);
    } else if (q >= &PW_P[0][0] && q < &PW_P[0][0] + 2 * NPW) {
        i = (uint32_t)(q - &PW_P[0][0]);
        g = PG_MIX, k = EN_K4, v[0] = (uint8_t)(i / NPW), v[3] = (uint8_t)(i % NPW);
    } else if (q >= LFOD_P && q < LFOD_P + NLFO) {
        g = PG_LFO1, k = EN_K2 + (uint32_t)(q - LFOD_P), v[4] = 1;
    } else if (q == &LATCH_P) {
        g = PG_ARP, k = EN_ALGO;
    }
    zoom_views(v);                           /* (what k sets there, so: q?) */
    ui.page = (uint8_t)g;
    ok = g != PG_HOME && knob_param(k) == q;
    zoom_views(was);
    ui.page = pg;
    *kp = k;
    return ok ? g : PG_HOME;
}
static void zoom_page(void)                  /* the CC's page up, its control as though turned */
{
    zoom_views(zoom.v);
    if (zoom.hp == PG_DHOME)
        ui.dt_sel = zoom.dnew;
    ui.page = zoom.g;
    ui.redraw = 1;
    ui.turned_at[zoom.ke] = plat_ms();
    ui.last_enc = zoom.ke;
    ui.map_until = ui.hint_until = 0;
    ui.val_until = pop_until();
}
static void zoom_keep(uint32_t home)         /* a dive from this HOME about to start: its views, for the way back */
{
    zoom_close_end();
    if (zoom.home)
        return;
    zoom.sv[0] = ui.osc_sel, zoom.sv[1] = ui.env_sel, zoom.sv[2] = ui.mix_src, zoom.sv[3] = ui.pw_src,
    zoom.sv[4] = ui.lfo_delay, zoom.u = 0.0f;
    zoom.hp = (uint8_t)home, zoom.dsv = ui.dt_sel;
}
static void zoom_go(uint32_t g, uint32_t k, const uint8_t *v)   /* the CC's page g and its encoder k: in */
{
    uint32_t i;
    zoom.g = (uint8_t)g, zoom.ke = (uint8_t)k;
    for (i = 0; i < 5u; i++)
        zoom.v[i] = v[i];
    zoom.cc_at = ms_stamp();
    if (zoom.home == ZH_PAGE) {              /* (on its page: another control there, or another page, from 1x) */
        if (ui.page != g)
            zoom.z = 0;
        zoom_page();
    } else {
        zoom.home = ZH_DIVE;
    }
}
static void zoom_cc_in(const param_t *q)     /* a CC's control q changed (on HOME, or on the way from it) */
{
    uint8_t v[5];
    uint32_t k, g;
    if (zoom.home == ZH_OPEN)                /* (a section's page on its way: that page's) */
        return;
    zoom_keep(PG_HOME);
    if ((g = zoom_find(q, v, &k)) == PG_HOME)   /* (none: as it is) */
        return;
    if (g != zoom.g || !zoom.home)
        home_sect(g, &zoom.hx, &zoom.hy, &zoom.lc);
    zoom_go(g, k, v);
}
static int zoom_step(uint32_t now)           /* a frame: 1 when the camera moved (the panel drawn again) */
{
    const uint32_t dt = zoom.at ? now - zoom.at : 0u, last = ui.turned_at[ui.last_enc];
    const int fn = zoom.fn_at && zoom.fn_ok && now - zoom.fn_at < DFN_ZOOM_MS;   /* (a function just picked: to read it) */
    const int aim = !set.zoom_off && zoom.pg == ui.page
                    && (fn || (zoom.k == ui.last_enc + 1u && last && now - last < ZOOM_HOLD_MS));
    const int16_t z = zoom.z, x = zoom.x, y = zoom.y;
    zoom.at = now;
    if (zoom.home == ZH_OPEN || zoom.home == ZH_CLOSE) {   /* (a section's button on HOME: in on its box, its page at the
                                              * end; HOME from that page: on its box, out) */
        const int in = zoom.home == ZH_OPEN;
        const float u = fm_clampf(zoom.u + (in ? (float)dt / ZOOM_DIVE_MS : -(float)dt / ZOOM_RISE_MS), 0.0f, 1.0f);
        zoom.u = u;
        zoom.x = zoom.hx, zoom.y = zoom.hy, zoom.pg = ui.page;
        zoom.z = (int16_t)(256.0f * fm_exp2f(u * u * (2.0f - u) * zoom.lc) - 255.5f);   /* (still moving at the page's end: the cut in the move) */
        if (!in && u <= 0.0f) {
            zoom.home = ZH_NONE;
            zoom.z = 0;
            ui.map_until = pop_until();
        } else if (in && u >= 1.0f) {
            page_go(zoom.g);
            ui.map_until = 0;                /* (the map and hint: once it's settled) */
            zoom.pg = ui.page, zoom.z = ZOOM_SETTLE_Z, zoom.settle = 1;
            zoom.x = zoom.tx = DCH_PAGE(zoom.g) ? 0 : 120, zoom.y = zoom.ty = DCH_PAGE(zoom.g) ? 0 : TALL_H / 2;   /* (a Channel page: from its top left, as HOME's row lands) */
        }
        return 1;
    }
    if (zoom.home == ZH_PAGE && !aim)        /* (the CCs done: back out) */
        zoom.home = ZH_RISE;
    if (zoom.home == ZH_DIVE || zoom.home == ZH_RISE) {   /* (HOME's dive: along its path) */
        const int in = zoom.home == ZH_DIVE;
        float u = zoom.u + (in ? (float)dt / ZOOM_DIVE_MS : -(float)dt / ZOOM_RISE_MS), l;
        u = fm_clampf(u, 0.0f, 1.0f);
        zoom.u = u;
        l = u * u * (3.0f - 2.0f * u) * (zoom.lc + 1.0f);
        if (l < zoom.lc) {                   /* (HOME, on the section) */
            if (ui.page != zoom.hp) {
                zoom_views(zoom.sv);
                if (zoom.hp == PG_DHOME)
                    ui.dt_sel = zoom.dsv;
                ui.page = zoom.hp;
                ui.redraw = 1;
            }
            zoom.x = zoom.hx, zoom.y = zoom.hy;
        } else {                             /* (its page, on the control: where its tag is, tag_v) */
            if (ui.page != zoom.g)
                zoom_page();
            l -= zoom.lc;
            zoom.x = zoom.tx, zoom.y = zoom.ty;
        }
        zoom.z = (int16_t)(256.0f * fm_exp2f(l) - 255.5f);
        zoom.pg = ui.page;
        if (in && u >= 1.0f) {               /* (in: its page's as for a turn from now, CCs keeping it) */
            zoom.home = ZH_PAGE;
            zoom.k = (uint8_t)(zoom.ke + 1u);
            ui.turned_at[zoom.ke] = now;
            ui.last_enc = zoom.ke;
        } else if (!in && u <= 0.0f) {
            zoom.home = ZH_NONE;
            zoom.z = 0;
        }
        return zoom.z != z || zoom.x != x || zoom.y != y || ui.redraw;
    }
    if (zoom.pg != ui.page)                  /* (another page: from the whole of it) */
        zoom.z = 0, zoom.settle = 0;
    zoom.z = (int16_t)zoom_ease(zoom.z, aim ? ZOOM_MAX : 0, dt, aim ? ZOOM_IN_MS : ZOOM_OUT_MS);
    if (zoom.settle && (!zoom.z || aim)) {   /* (a section's dive settled, or the camera on to a knob or function: its map, its button's hint) */
        zoom.settle = 0;
        ui.map_until = pop_until();
        hint_show(zoom.bk);
        ui.redraw = 1;
    }
    if (!z && aim)                           /* (from the whole panel: straight at it) */
        zoom.x = zoom.tx, zoom.y = zoom.ty;
    else if (zoom.z) {
        zoom.x = (int16_t)zoom_ease(zoom.x, zoom.tx, dt, ZOOM_IN_MS);
        zoom.y = (int16_t)zoom_ease(zoom.y, zoom.ty, dt, ZOOM_IN_MS);
    }
    return zoom.z != z || (zoom.z && (zoom.x != x || zoom.y != y));
}
/* the canvas's rows (a half of the panel, its rows from the window's first) scaled up in place: screen (x, r) from
 * canvas ((x16 + x step) >> 16, (y16 + r step) >> 16), Q16, step <= 1. The window first to the left edge, then from
 * the last pixel back: each read at or before its write, so none read is written yet */
static void zoom_blow(uint32_t x16, uint32_t y16, uint32_t step)
{
    const uint32_t w = cv_w, dx = x16 >> 16, xf = x16 & 0xFFFFu, rows = ((y16 + (cv_h - 1u) * step) >> 16) + 1u;
    uint32_t r, x;
    for (r = 0; dx && r < rows; r++)
        for (x = 0; x + dx < w; x++)
            cv_px[r * w + x] = cv_px[r * w + x + dx];
    for (r = cv_h; r-- > 0;) {
        const uint16_t *s = cv_px + ((y16 + r * step) >> 16) * w;
        uint16_t *d = cv_px + r * w;
        for (x = w; x-- > 0;)
            d[x] = s[(xf + x * step) >> 16];
    }
}

/* the controls' tags, one look for both: as a page opens, for the pop-ups' time (pop_until) or until a knob turns, the map: each control
 * tagged with what sets it on the FM-1 (an encoder: cyan; a button: white); and while a control turns, its value in
 * its tag (cyan). The page's drawing says where its controls are (tag*), tags_draw puts them over it */
#define NTAGS 16
static const char *const ENC_NAME[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};
static struct {
    int16_t x, y;
    uint8_t btn, lit;
    const char *s, *h;                       /* (h: its hint, Hints on, lit) */
    char v[16];                              /* (lit: its value) */
} tags[NTAGS] PLAT_POOL;
static uint32_t ntags;
static void tag_at(int32_t cx, int32_t cy, const char *s, int btn)   /* centred on cx, cy: the map's alone */
{
    if (!ui.map_until || ntags >= NTAGS)
        return;
    tags[ntags].x = (int16_t)cx;
    tags[ntags].y = (int16_t)cy;
    tags[ntags].btn = (uint8_t)btn;
    tags[ntags].lit = 0;
    tags[ntags].h = 0;
    tags[ntags++].s = s;
}
static void tag_v(int32_t cx, int32_t cy, uint32_t k, const char *v)   /* encoder k's; v its value (0: knob_text's) */
{
    const int lit = knob_lit(k) || (ui.val_until && k == ui.last_enc);   /* (turning, then the pop-ups' time) */
    if (knob_lit(k) && k == ui.last_enc && (int32_t)(ui.turned_at[k] - zoom.cc_at) >= 0) {   /* (turning, since the
                                              * last CC: Knob Zoom's target) */
        zoom.tx = (int16_t)cx, zoom.ty = (int16_t)cy;
        zoom.k = (uint8_t)(k + 1u), zoom.pg = ui.page;
    }
    if ((!ui.map_until && !lit) || ntags >= NTAGS)
        return;
    tags[ntags].v[0] = 0;
    if (lit && v)
        text_cat(tags[ntags].v, v);
    else if (lit)
        knob_text(k, tags[ntags].v);
    tags[ntags].x = (int16_t)cx;
    tags[ntags].y = (int16_t)cy;
    tags[ntags].btn = 0;
    tags[ntags].lit = (uint8_t)lit;
    tags[ntags].h = lit && set.hints ? hint_of(k) : 0;
    tags[ntags++].s = ENC_NAME[enc_physical(k)];
}
static void tag(int32_t cx, int32_t cy, uint32_t k) { tag_v(cx, cy, k, 0); }   /* encoder k */
static void tag_sv(int32_t x, int32_t y, int32_t n, uint32_t k) { tag(x + 9, y + (9 * n + 15) / 2, k); }   /* a slide_v's */
static void tag_sh(int32_t x, int32_t y, int32_t n, uint32_t k) { tag(x + (9 * n + 15) / 2, y + 9, k); }   /* a slide_h's */
static void hint_pill(int32_t cx, int32_t y, const char *s)   /* a hint: white on dark, its edge cyan */
{
    int32_t w = text_w(&FONT_XS, s) + 8, x = clamp(cx - w / 2, 2, 238 - w);
    cv_rect(x - 1, y - 1, w + 2, 17, P_CYAN);
    cv_rect(x, y, w, 15, RGB(16, 20, 24));
    cv_text(x + 4, y, &FONT_XS, s, P_SILK);
}
static void tags_draw(void)
{
    uint32_t i;
    if (ui.hint_until && ui.hint && RND_PAGE(ui.page))   /* a button's hint, at the panel's foot (the RND pages': no
                                                          * keys; elsewhere on the keys, keys_draw) */
        hint_pill(120, pan_h - 26, ui.hint);
    for (i = 0; i < ntags; i++) {            /* (the map's names; else the values of what turns) */
        const char *s = ui.map_until ? tags[i].s : tags[i].lit && tags[i].v[0] ? tags[i].v : 0;
        int32_t w, x, y = tags[i].y - 7;
        if (!s)
            continue;
        w = text_w(&FONT_XS, s) + 6;
        x = clamp(tags[i].x - w / 2, 2, 238 - w);   /* (kept on the screen) */
        cv_rect(x - 1, y - 1, w + 2, 17, RGB(0, 0, 0));
        cv_rect(x, y, w, 15, tags[i].btn ? P_SILK : P_CYAN);
        cv_text(x + 3, y, &FONT_XS, s, RGB(0, 0, 0));
        if (!ui.map_until && tags[i].h)      /* its hint under it (over it, near the foot) */
            hint_pill(tags[i].x, y + 17 + 15 < pan_h - 6 ? y + 18 : y - 18, tags[i].h);
    }
}
static void panel_bg(void)                   /* one grey, 24: a 20-to-30 gradient banded on the 565 panel (green's steps
                                              * apart from red's and blue's: five stripes, each its own tint) */
{
    cv_rect(0, 0, 240, pan_h, grey(24));
}
/* the bar, x0 to x1, its gaps at the sections' edges (each a line down from it, its left), the line along the bottom */
static void panel_bars(int32_t x0, int32_t x1, const int32_t *edge, int32_t n)
{
    int32_t i;
    cv_rect(x0, 1, x1 - x0, 15, P_BAR);
    for (i = 0; i < n; i++) {
        cv_rect(edge[i], 1, 3, pan_h - 9, P_BAR);
        cv_rect(edge[i] + 3, 1, 3, 15, grey(20));
    }
    cv_rect(x0, pan_h - 5, x1 - x0, 3, P_BAR);
}
static void bar_text(int32_t x, int32_t y, const char *s)   /* a name in the bar: white, its capitals in the middle */
{
    cv_text(x, y, &FONT_B, s, P_SILK);
}
static void panel_head(const char *name)    /* a page's panel: its grey, the bar (one section) with its name in the middle */
{
    static const int32_t EDGE[1] = {2};
    panel_bg();
    panel_bars(2, 236, EDGE, 1);
    bar_text((10 + 236 - text_w(&FONT_B, name)) / 2, 1, name);
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
    bs2_knob(cx, cy, 20, param_get(q, page_rec(ui.page)) - q->lo, q->hi - q->lo, knob_lit(k) ? KF_LIT : 0u);
    tag(cx, cy, k);
    if (name)
        silk_text(cx, cy + 24, name, P_SILK);
}
/* SUB OSC + MIXER: the end of the OSCILLATORS section (the sub osc: its octave and wave, switches with their LEDs) and the
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
    tag_sv(40, 42, 2, EN_SELECT);
    silk_text(32, 78, "Octave", P_SILK);
    for (i = 0; i < NSUBW; i++) {            /* the wave: sine, narrow pulse, square */
        led(16, 107 + i * 16, patch.sub_wave == (uint32_t)i);
        wave_icon(24, 104 + i * 16, (uint32_t)i);
    }
    slide_v(40, 104, NSUBW, patch.sub_wave, knob_lit(EN_ALGO));
    tag_sv(40, 104, NSUBW, EN_ALGO);
    panel_knob(104, 47, EN_K1, "Osc 1");
    panel_knob(199, 47, EN_K2, "Osc 2");
    panel_knob(104, 111, EN_PRESET, "Sub Osc");
    panel_knob(199, 111, EN_K4, 0);
    slide_v(142, 56, NMS, NMS - 1 - ui.mix_src, knob_lit(EN_K3));   /* Ext at the top, Noise at the bottom */
    tag_sv(142, 56, NMS, EN_K3);
    for (i = 0; i < NMS; i++)
        cv_text(144, 99 + (NMS - 1 - i) * 12, &FONT_XS, SRC[i], P_SILK);
    cv_rect(151, 142, 1, 9, P_SILK);         /* the bracket from the names to their knob */
    cv_rect(151, 150, 47, 1, P_SILK);
    cv_rect(197, 142, 1, 9, P_SILK);
}

static void print_c(int32_t cx, int32_t y, const char *s, uint16_t c)   /* small print centred on cx */
{
    const felucca_font_t *f = text_w(pr_font, s) > 232 ? &FONT_T : pr_font;   /* (too wide: the small print) */
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
/* a panel knob with its print under it (two lines: l2 may be 0), its value in its place, cyan, while it turns: encoder
 * k's setting q, mine when this view's (else dim) */
static void print_knob(int32_t cx, int32_t cy, int32_t r, const param_t *q, uint32_t k, int mine, const char *l1,
                       const char *l2)
{
    uint32_t lit = mine && knob_lit(k);
    int32_t ly = cy + r + 4;
    bs2_knob(cx, cy, r, param_get(q, page_rec(ui.page)) - q->lo, q->hi - q->lo,
             (lit ? KF_LIT : 0u) | (q->lo < 0 ? KF_BIP : 0u) | (mine ? 0u : KF_DIM));
    if (mine)
        tag(cx, cy, k);
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
    static const char *const SH[3] = {"LP", "BP", "HP"};
    const int md = ui.page == PG_FMOD, acid = patch.filter == FLT_ACID;
    const uint32_t f = patch.filter, shape = acid ? 0u : f / 2u, d24 = acid || !(f & 1u);   /* (Acid's: as it rests) */
    uint16_t on = md ? P_DIM : P_SILK, cl = md || acid ? P_DIM : P_SILK;
    uint32_t i, fl = md ? KF_DIM : 0u;
    panel_head("FILTERS");
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
    if (!md) {                               /* (the map: the switches the main view's) */
        tag_sh(29, 38, 2, EN_SELECT);
        tag_sh(29, 81, 2, EN_ALGO);
        tag_sh(24, 112, 3, EN_PRESET);
    }
    for (i = 0; i < 3u; i++) {
        led(28 + 17 * (int32_t)i, 137, !acid && shape == i);
        print_c(28 + 17 * (int32_t)i, 143, SH[i], cl);
    }
    print_knob(100, 40, 15, CCP(PG_TUNE, EN_K1), EN_K1, !md, "Overdrive", 0);   /* (2 px apart: their names clear) */
    print_knob(152, 40, 15, CCP(PG_TUNE, EN_K3), EN_K3, !md, "Resonance", 0);
    print_knob(102, 104, 15, CCP(PG_TUNE, EN_K2), EN_K2, !md, "Mod Env", "depth");
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
    tag_sv(48, 56, 3, EN_SELECT);
}
/* the ENVELOPES (the Bass Station II's): Env Select at the left (SELECT: Amp, Mod, Amp+Mod; what the faders set), the
 * Attack, Decay, Sustain and Release faders (K1 to K4). (Triggering: its own page, trig_draw; the velocities: the
 * group's mods) */
static void env_draw(void)
{
    static const char *const ST[4] = {"Attack", "Decay", "Sustain", "Release"};
    uint32_t i;
    panel_head("ENVELOPES");
    env_select();
    cv_rect(66, 77, 6, 1, P_SILK);           /* its line to the faders */
    for (i = 0; i < 4u; i++) {               /* the faders: K1 to K4, the envelope it picks (Amp+Mod: the amp's) */
        const param_t *q = knob_param(EN_K1 + i);
        int32_t cx = 92 + 40 * (int32_t)i;
        uint32_t lit = knob_lit(EN_K1 + i);
        fader(cx, 22, 110, param_get(q, &patch), 127, lit ? KF_LIT : 0u);
        tag(cx, 77, EN_K1 + i);
        print_c(cx, 138, ST[i], P_SILK);
    }
}
/* the ENVELOPES' Triggering, the Bass Station II's switch (K4, the switch in its column at the right): Autoglide,
 * Single, Multi (top to bottom), an LED at each, the labels left of them; of the envelope Env Select picks (Amp+Mod:
 * both, the amp's shown), as on a Bass Station II */
static void trig_draw(void)
{
    static const char *const TRIG[NENVT] = {"Multi", "Single", "Autoglide"};
    const uint32_t v = (uint32_t)param_get(knob_param(EN_K4), &patch);
    uint32_t i;
    panel_head("ENVELOPES");
    env_select();
    cv_rect(66, 77, 180 - text_w(&FONT_T, "Autoglide") - 66, 1, P_SILK);   /* Env Select's line to the switch it sets */
    print_c(180, 40, "Triggering", P_SILK);
    slide_v(203, 56, 3, (int32_t)(NENVT - 1u - v), knob_lit(EN_K4) ? KF_LIT : 0u);
    tag_sv(203, 56, 3, EN_K4);
    for (i = 0; i < NENVT; i++) {            /* (top to bottom: Autoglide, Single, Multi) */
        uint32_t w = NENVT - 1u - i;
        led(192, 67 + 10 * (int32_t)i, v == w);
        cv_text(184 - text_w(&FONT_T, TRIG[w]), 62 + 10 * (int32_t)i, &FONT_T, TRIG[w], v == w ? P_SILK : P_DIM);
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
    int32_t y;
    panel_head(grp);
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
    if (env == 1 || env == 2)                /* (its title: which envelope, which LFO) */
        tag(120, 36, EN_SELECT);
    for (i = EN_K1; i <= EN_K4; i++)
        n += knob_param(i)->name != 0;
    for (i = 0; i < n; i++) {                /* its knobs, K1 on: each one's tape, its setting printed under it */
        const param_t *q = knob_param(EN_K1 + i);
        int32_t cx = 10 + 220 * (2 * (int32_t)i + 1) / (2 * (int32_t)n);
        uint32_t lit = knob_lit(EN_K1 + i);
        bs2_knob(cx, 76, n > 2u ? 16 : 20, param_get(q, &patch) - q->lo, q->hi - q->lo,
                 KF_PLATE | (lit ? KF_LIT : 0u) | (q->lo < 0 ? KF_BIP : 0u));
        tag(cx, 76, EN_K1 + i);
        dymo(cx, 101, LBL[env][i], RGB(235, 235, 235));
        knob_text(EN_K1 + i, t);
        print_c(cx, 117, t, grey(36));
    }
}
/* PATCH NAME (SEL on the MASTER: the patch's mod, as the Bass Station II has no way to name one): the name's 16
 * letters, K1 the cursor, K2 its letter (NAME_CH), K3 the name cleared from the cursor on; SAVE keeps it with the
 * sound. Said whole as it changes */
static int name_turn(uint32_t k, int32_t e)
{
    char w[PNAME_N + 1u];
    uint32_t i;
    if (k == EN_K1)
        ui.name_at = (uint8_t)clamp((int32_t)ui.name_at + e, 0, PNAME_N - 1);
    else if (k == EN_K2) {
        for (i = 0; i < ui.name_at; i++)     /* (a letter past the end: spaces up to it) */
            if (!cur_name[i])
                cur_name[i] = ' ';
        i = (uint32_t)(((int32_t)name_ch(cur_name[ui.name_at]) + e % (int32_t)NNAME_CH + (int32_t)NNAME_CH)
                       % (int32_t)NNAME_CH);
        cur_name[ui.name_at] = NAME_CH[i];
        set_dirty = 1;
    } else if (k == EN_K3) {
        for (i = ui.name_at; i < PNAME_N; i++)
            cur_name[i] = 0;
        set_dirty = 1;
    } else
        return 0;
    for (i = 0; i < PNAME_N && cur_name[i]; i++)
        w[i] = cur_name[i];
    w[i] = 0;
    app_say("PATCH NAME", i ? w : "NO NAME");
    return 0;
}
static void name_draw(void)
{
    static const char *const LBL[3] = {"CURSOR", "LETTER", "CLEAR"};
    char t[4] = {0};
    uint32_t i;
    plate("PATCH");
    dymo(120, 30, "PATCH NAME", RGB(235, 235, 235));
    for (i = 0; i < PNAME_N; i++) {          /* its letters, a cell each; the cursor's lit, underlined */
        const int32_t x = 24 + 12 * (int32_t)i;
        const int at = i == ui.name_at;
        cv_rect(x, 46, 11, 20, at ? P_CDIM : grey(28));
        t[0] = cur_name[i] > ' ' ? cur_name[i] : 0;
        t[1] = 0;
        if (t[0])
            print_c(x + 6, 50, t, at ? P_CYAN : RGB(235, 235, 235));
        if (at)
            cv_rect(x, 64, 11, 2, P_CYAN);
    }
    for (i = 0; i < 3u; i++) {               /* its knobs, each one's tape, what it sets under it */
        const int32_t cx = 10 + 220 * (2 * (int32_t)i + 1) / 6;
        const int32_t v = i == 0u ? ui.name_at : i == 1u ? (int32_t)name_ch(cur_name[ui.name_at]) : 0;
        const int32_t span = i == 0u ? PNAME_N - 1 : i == 1u ? (int32_t)NNAME_CH - 1 : 1;
        bs2_knob(cx, 96, 14, v, span, KF_PLATE | (knob_lit(EN_K1 + i) ? KF_LIT : 0u));
        tag(cx, 96, EN_K1 + i);
        dymo(cx, 117, LBL[i], RGB(235, 235, 235));
        knob_text(EN_K1 + i, t);
        print_c(cx, 131, t, grey(36));
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
        tag(kx, 54, EN_K2 + i);
        tag(lx, 52, i ? EN_K4 : EN_K1);      /* (its waves' button) */
        print_c(kx, 75, i ? "LFO 2" : "LFO 1", P_SILK);
        knob_text(EN_K2 + i, t);             /* (what it sets: its speed, sync value or delay) */
        print_c(kx, 84, t, P_DIM);
        cv_rect(kx, 96, 1, 16, P_SILK);      /* its line to the switch */
        cv_rect(i ? 157 : kx, 111, i ? kx - 156 : 139 - kx, 1, P_SILK);
    }
    print_c(148, 75, "Delay", ui.lfo_delay ? P_SILK : P_DIM);   /* the Delay/Speed switch (Delay on the LFOs' names
                                                                 * line, clear of their speeds) */
    slide_v(139, 96, 2, !ui.lfo_delay, knob_lit(EN_ALGO) ? KF_LIT : 0u);
    tag_sv(139, 96, 2, EN_ALGO);
    print_c(148, 131, "Speed", ui.lfo_delay ? P_DIM : P_SILK);
}
/* the EFFECTS (the Bass Station II's): Distortion (K1) and Osc Filter Mod (K2), its two knobs */
static void effects_draw(void)
{
    panel_head("EFFECTS");
    print_knob(72, 72, 24, CCP(PG_EFX, EN_K1), EN_K1, 1, "Distortion", 0);
    print_knob(168, 72, 24, CCP(PG_EFX, EN_K2), EN_K2, 1, "Osc Filter Mod", 0);
}
/* a seven-segment digit at x, y (16 x 28), the Bass Station II display's red: c a character (0..9, A..F, a few
 * letters more, -, space); its segments' ghosts (all 8s) dim under it, its lit ones glowing */
static uint32_t seg_mask(char c)             /* c's lit segments, gfedcba (0: blank) */
{
    static const char CH[] = "0123456789ABCDEFP-LnOtSrU";
    static const uint8_t SEG[sizeof CH - 1] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C,
                                               0x39, 0x5E, 0x79, 0x71, 0x73, 0x40, 0x38, 0x54, 0x3F, 0x78,
                                               0x6D, 0x50, 0x3E};   /* gfedcba */
    uint32_t i;
    for (i = 0; CH[i]; i++)
        if (CH[i] == c)
            return SEG[i];
    return 0;
}
static void seg7(int32_t x, int32_t y, char c)
{
    static const int8_t R[7][4] = {{3, 0, 10, 3}, {13, 2, 3, 11}, {13, 15, 3, 11}, {3, 25, 10, 3},
                                   {0, 15, 3, 11}, {0, 2, 3, 11}, {3, 12, 10, 3}};   /* a..g: x, y, w, h */
    uint32_t m = seg_mask(c), i;
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
    o = param_get(q, &BANK_P(set.slot));
    return v < o ? 1 : v > o ? -1 : 0;
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
/* the MASTER (the Bass Station II's, the first page): the Volume (the FM-1's MASTER knob, shown), the name; the display
 * (the patch, P01..P32, its dot lit while it has changes not saved), the Save button and the Patch buttons (PRESET
 * picks; SAVE keeps), the Org. Value LEDs and their arrows (org_dir) */
static void master_draw(void)
{
    char t[4];
    int32_t i;
    uint32_t vol = plat_master();
    panel_head("MASTER");
    bs2_knob(44, 56, 20, (int32_t)vol, 4095, 0u);   /* Volume */
    print_c(44, 80, "Volume", P_SILK);
    tag_at(44, 56, "MASTER", 0);
    cv_text(8, 106, &FONT_B, "BASS", P_SILK);   /* the name, where a Bass Station II has its maker's */
    cv_text(cv_text(8, 122, &FONT_B, "SOMETHING", P_SILK) + 5, 122, &FONT_B, "2", P_BAR);
    cv_rect(96, 22, 136, 50, RGB(4, 4, 4));  /* the display: black, its window, three digits */
    cv_rect(100, 26, 72, 42, RGB(20, 16, 20));
    slot_disp(t);
    if (ui.org_at) {                         /* (a control just changed: its value, as the Bass Station II's, -63..127) */
        seg_num(param_get(ui.org, &patch), t);
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
    tag_at(114, 92, "SAVE", 1);
    tag(184, 107, EN_PRESET);
}
/* a panel button at x, y (w x h): its shadow, its face (lit: red, as the Bass Station II's are), its name on it */
static void panel_button(int32_t x, int32_t y, int32_t w, int32_t h, const char *name, int lit, int turning)
{
    cv_rect(x, y, w, h, P_KSHADOW);
    cv_rect(x + 2, y + 2, w - 4, h - 4, lit ? RGB(150, 26, 44) : turning ? P_CDIM : grey(34));
    print_c(x + w / 2, y + (h - pr_font->h) / 2, name, P_SILK);
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
static void cpu_text(char *t);
/* GLOBAL: the Bass Station II's Function settings, on the FM-1 a section of their own. Its bar, the page's own bar
 * under it at the left; at the right the audio's load (cpu_text: for testing), or what was just done while it's said */
static void section_frame(const char *bar, const char *name, const char *right, uint16_t rc)
{
    int32_t x;
    panel_head(bar);
    cv_rect(4, 19, text_w(&FONT_XS, name) + 12, 15, P_BAR);
    cv_text(10, 19, &FONT_XS, name, P_SILK);
    if (plat_ms() < ui.msg_until) {
        x = 232 - text_w(&FONT_XS, ui.msg[1]);
        cv_text(x, 20, &FONT_XS, ui.msg[1], P_CYAN);
        cv_text(x - 6 - text_w(&FONT_XS, ui.msg[0]), 20, &FONT_XS, ui.msg[0], P_SILK);
        return;
    }
    cv_text(232 - text_w(&FONT_XS, right), 20, &FONT_XS, right, rc);
}
static void glo_frame(const char *name)
{
    char t[24];
    cpu_text(t);
    section_frame("GLOBAL", name, t, xruns_shown() ? P_CYAN : P_DIM);
}
/* the RND pages' (bs2_rnd.c): the RND SYNTH COMPANION bar; at the right the synth's firmware once it has answered */
static void rnd_frame_draw(const char *name)   /* at the right: the seed playing on the synth, once it has said */
{
    char t[24] = "PLAYING ";
    seed_text(rnd.seed, t + 8);
    section_frame("RND SYNTH COMPANION", name, rnd.seed != RND_EMPTY ? t : rnd.heard ? "RND" : "NOT HEARD YET",
                  rnd.heard ? P_CYAN : P_DIM);
}
/* a setting's two-way switch lying down at cx, cy (its middle): its ends' names over it (the one set white), its name
 * under it (its setting instead, cyan, while it moves) */
static void print_switch(int32_t cx, int32_t cy, uint32_t k, const char *n0, const char *n1, const char *name)
{
    const param_t *q = knob_param(k);
    int32_t v = param_get(q, page_rec(ui.page)) - q->lo;
    int lit = knob_lit(k);
    cv_text(cx - 16, cy - 20, &FONT_T, n0, v ? P_DIM : P_SILK);
    cv_text(cx + 17 - text_w(&FONT_T, n1), cy - 20, &FONT_T, n1, v ? P_SILK : P_DIM);
    slide_h(cx - 16, cy - 9, 2, v, lit ? KF_LIT : 0u);
    tag(cx, cy, k);
    print_c(cx, cy + 18, name, P_SILK);   /* (level with a GLO_ROW knob's name) */
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
/* GLOBAL DRUMS: Drum Something's MIDI, a device of its own beside the synth's (GLOBAL MIDI): its channel on the
 * display, its knob (K1, on K4); its CC Chan (SELECT), and what its CCs do */
static void glo_drum_draw(void)
{
    char t[4], o[8] = "Ch ";
    glo_frame("Drums");
    text_cat(o, CHAN_NAME[set.dchan == CH_ALL || set.dchan == CH_OFF ? 1u : set.dchan]);
    if (set.dchan == CH_OFF || set.dchan == CH_ALL)
        memcpy(t, set.dchan ? "ALL" : "OFF", 3);
    else
        seg_num(set.dchan, t);
    seg_display(8, 40, t, 3);
    info(146, 42, 46, "Out", o, P_CYAN);
    info(146, 42, 58, "CC", set.dcc_chan == CH_ALL ? "Any" : set.dcc_chan ? CHAN_NAME[set.dcc_chan] : "Off", P_CYAN);
    print_knob(34, GLO_ROW, 14, knob_param(EN_SELECT), EN_SELECT, 1, "CC Chan", 0);
    cv_text(74, GLO_ROW - 12, &FONT_XS, "CC 33-45: drum tuning", P_SILK);
    cv_text(74, GLO_ROW + 3, &FONT_XS, "CC 46-58: drum volume", P_SILK);
    print_knob(208, GLO_ROW, 14, knob_param(EN_K1), EN_K1, 1, "Channel", 0);
}
/* GLOBAL CLOCK: the tempo on the display (MIDI clock's, measured, while it leads; dashes while it is awaited), what the
 * clock is doing; the Tempo under it (K1), Send Clk (K3) and Sync (K4) */
static void glo_clock_draw(void)
{
    char t[4];
    if (ext_clock() && (eng.ext_age >= EXT_HOLD || !eng.ext_shown))
        memcpy(t, "---", 3);
    else
        seg_num(ext_clock() ? eng.ext_shown : (uint32_t)arp_tempo(), t);
    glo_frame("Clock");
    seg_display(8, 40, t, 3);
    if (ext_clock())
        print_c(50, 90, eng.ext_age < EXT_HOLD ? "MIDI clock" : "Waiting for clock", P_SILK);
    info(120, 40, 52, "Clock", ext_clock() ? "MIDI in" : set.clk_out ? "Sending" : "Internal",
         ext_clock() || set.clk_out ? P_CYAN : P_SILK);
    bs2_knob(34, GLO_ROW, 14, clamp(arp_tempo() ? arp_tempo() : (int32_t)patch.tempo, 40, 240) - 40, 200,
             (knob_lit(EN_K1) ? KF_LIT : 0u) | (ext_clock() ? KF_DIM : 0u));
    tag(34, GLO_ROW, EN_K1);
    print_c(34, GLO_ROW + 18, "Tempo", P_SILK);
    print_switch(150, GLO_ROW, EN_K3, "Off", "On", "Send Clk");
    print_knob(208, GLO_ROW, 14, knob_param(EN_K4), EN_K4, 1, "Sync", 0);
}
/* GLOBAL AUDIO: Speaker (K1, on K3: its three places) and Tune (K2, on K4), with what the speaker's setting reaches */
static void glo_audio_draw(void)
{
    const int32_t x = 136;
    glo_frame("Audio");
    cv_text(10, 42, &FONT_XS, "Speaker: the FM-1's own speaker alone.", P_SILK);
    cv_text(10, 57, &FONT_XS, "Headphones and USB stay stereo and flat.", P_SILK);
    cv_text(10, 72, &FONT_XS, "Tune: the master pitch, in cents.", P_SILK);
    cv_text(x - 37, GLO_ROW - 20, &FONT_T, "Flat", set.spk == SPK_FLAT ? P_SILK : P_DIM);
    cv_text(x - text_w(&FONT_T, "Bass+") / 2, GLO_ROW - 20, &FONT_T, "Bass+", set.spk == SPK_BASS ? P_SILK : P_DIM);
    cv_text(x + 37 - text_w(&FONT_T, "Off"), GLO_ROW - 20, &FONT_T, "Off", set.spk == SPK_OFF ? P_SILK : P_DIM);
    slide_h(x - 21, GLO_ROW - 9, 3, set.spk, knob_lit(EN_K1) ? KF_LIT : 0u);
    tag(x, GLO_ROW, EN_K1);
    print_c(x, GLO_ROW + 18, "Speaker", P_SILK);
    print_knob(208, GLO_ROW, 14, knob_param(EN_K2), EN_K2, 1, "Tune", 0);
}
/* GLOBAL CONTROLS: how the keys and the knobs answer: Velocity (K1, on K3) and Knobs (K2, on K4) */
static void glo_controls_draw(void)
{
    glo_frame("Controls");
    cv_text(10, 42, &FONT_XS, "Velocity: how hard a key must be played", P_SILK);
    cv_text(10, 57, &FONT_XS, "for its notes to sound loud.", P_SILK);
    cv_text(10, 72, &FONT_XS, "Knobs: how far they go as they turn faster.", P_SILK);
    print_knob(150, GLO_ROW, 14, knob_param(EN_K1), EN_K1, 1, "Velocity", 0);
    print_knob(208, GLO_ROW, 14, knob_param(EN_K2), EN_K2, 1, "Knobs", 0);
}
/* GLOBAL DISPLAY: Knob Zoom (PRESET), Hints (K1, on K3) and the pop-ups' time (K2, on K4: the controls' labels as a
 * page opens, a button's hint, a knob's value and hint after it turns), with what each does */
static void glo_disp_draw(void)
{
    const uint32_t lit = knob_lit(EN_K2);
    glo_frame("Display");
    cv_text(10, 42, &FONT_XS, "Pop-ups: the controls' labels as a page opens,", P_SILK);   /* (as wide as the bar) */
    cv_text(10, 57, &FONT_XS, "a knob's value as it turns. Hints add a few", P_SILK);
    cv_text(10, 72, &FONT_XS, "words on what it does.", P_SILK);
    info(10, 50, 88, "Version", VERSION_TEXT, P_CYAN);
    print_switch(34, GLO_ROW, EN_PRESET, "", "", "Knob Zoom");   /* (PRESET's, at the left as it is; its names further
                                                                 * apart: Auto is wide) */
    cv_text(34 - 24, GLO_ROW - 20, &FONT_T, "Auto", set.zoom_off ? P_DIM : P_SILK);
    cv_text(34 + 24 - text_w(&FONT_T, "Off"), GLO_ROW - 20, &FONT_T, "Off", set.zoom_off ? P_SILK : P_DIM);
    print_switch(150, GLO_ROW, EN_K1, "Off", "On", "Hints");   /* (K3's and K4's columns) */
    bs2_knob(208, GLO_ROW, 14, (int32_t)pop_d(), NPOP - 1, lit ? KF_LIT : 0u);
    tag(208, GLO_ROW, EN_K2);
    print_c(208, GLO_ROW + 18, "Pop-ups", P_SILK);
}
/* GLOBAL DUMP: the patch on the display; SEL sends it, FX all 128, as the Bass Station II's SysEx (two buttons, as
 * the FM-1's are: FX at the left, SEL at the right) */
static void glo_dump_draw(void)
{
    char t[4];
    slot_disp(t);
    glo_frame("SysEx Dump");
    seg_display(8, 40, t, 3);
    print_c(50, 90, "Patch", P_SILK);
    cv_text(118, 44, &FONT_T, "Bass Station II", P_SILK);
    cv_text(118, 54, &FONT_T, "patch dumps, over USB", P_SILK);
    cv_text(118, 70, &FONT_T, "GLO settings save", P_DIM);
    cv_text(118, 80, &FONT_T, "themselves", P_DIM);
    panel_button(30, 108, 72, 28, "All 128", 0, 0);
    tag_at(66, 108, "FX", 1);              /* (buttons: on their top edges, their names left showing) */
    tag_at(174, 108, "SEL", 1);
    print_c(66, 139, "FX", P_SILK);
    panel_button(138, 108, 72, 28, "Sound", 0, 0);
    print_c(174, 139, "SEL", P_SILK);
}
/* RND SEED: the slot's seed on an eight-digit display (dashes: empty), its LEDs (Locks: the id holds locks; Playing:
 * it's the synth's patch now), its names (seed_words); the seed playing on the synth at the top right; what the slot is (the synth's patch as it said, when it's the one playing; else
 * its locks); the slot (PRESET), Save (SAVE: the patch playing into the slot), Send (FX: the slot's to the synth, its
 * locks first) and Ask (SEL: the synth's state asked again) */
static void lock_line(const rlock_t *l, int eng, char *t)   /* its type, root, scale held ("-": free); or engines */
{
    uint32_t i;
    t[0] = 0;
    if (eng) {
        for (i = 0; i < 4u; i++) {
            text_cat(t, i ? "  " : "");
            text_cat(t, l->eng[i] >= 0 ? RND_ENG_S[l->eng[i]] : "-");
        }
        return;
    }
    text_cat(t, l->type >= 0 ? RND_TYPE[l->type] : "ANY TYPE");
    text_cat(t, "  ");
    text_cat(t, l->root >= 0 ? RND_ROOT[l->root] : "ANY ROOT");
    text_cat(t, "  ");
    text_cat(t, l->scale >= 0 ? RND_SCALE[l->scale] : "ANY SCALE");
}
static void rnd_seed_draw(void)
{
    char s[64], v[8], *w;
    uint32_t i;
    const uint32_t seed = seeds.seed[rnd_slot], code = seeds.code[rnd_slot];
    const int live = seed != RND_EMPTY && seed == rnd.seed && code == rnd.code;
    const rlock_t l = lock_get(code);
    seed_text(seed, s);
    if (seed == RND_EMPTY)
        memcpy(s, "--------", 8);
    rnd_frame_draw("Seed");
    seg_display(4, 40, s, 8);
    led(219, 50, seed != RND_EMPTY && code != 0u);
    print_c(219, 58, "Locks", P_SILK);
    led(219, 76, live);
    print_c(219, 84, "Playing", P_SILK);
    if (seed == RND_EMPTY) {
        print_c(120, 100, "Empty: SAVE keeps", P_DIM);
        print_c(120, 115, "the patch playing", P_DIM);
    } else {                                 /* the seed's three words, under them the locks' */
        seed_words(seed, code, s);
        for (w = s, i = 0; *w; w++)
            if (*w == ' ' && ++i == 3u) {
                *w++ = 0;
                break;
            }
        print_c(120, 98, s, P_SILK);
        print_c(120, 113, w, P_SILK);
    }
    if (live && rnd.type < 3u && rnd.root < 12u && rnd.scale < NRSCALE) {   /* the patch as the synth said */
        s[0] = 0;
        text_cat(s, RND_TYPE[rnd.type]);
        text_cat(s, "  ");
        text_cat(s, RND_ROOT[rnd.root]);
        text_cat(s, " ");
        text_cat(s, RND_SCALE[rnd.scale]);
        text_cat(s, "  ");
        int_text(rnd.bpm, v);
        text_cat(s, v);
        text_cat(s, " BPM");
        print_c(120, 133, s, P_SILK);
        s[0] = 0;
        for (i = 0; i < rnd.ntr; i++) {
            text_cat(s, i ? "  " : "");
            text_cat(s, rnd.eng[i] < NENG ? RND_ENG_S[rnd.eng[i]] : "?");
        }
        print_c(120, 148, s, P_CYAN);
    } else if (seed != RND_EMPTY) {          /* another: what it holds */
        lock_line(&l, 0, s);
        print_c(120, 133, code ? s : "Nothing locked", P_SILK);
        if (code) {
            lock_line(&l, 1, s);
            print_c(120, 148, s, P_CYAN);
        }
    }
    bs2_knob(28, 178, 13, rnd_slot, NSEEDS - 1, knob_lit(EN_PRESET) ? KF_LIT : 0u);
    tag(28, 178, EN_PRESET);
    tag_at(85, 166, "SAVE", 1);
    tag_at(145, 166, "FX", 1);
    tag_at(205, 166, "SEL", 1);
    seed_slot_text(s);
    print_c(28, 195, s, P_SILK);
    panel_button(58, 166, 54, 26, "Save", 0, 0);
    print_c(85, 195, "SAVE", P_SILK);
    panel_button(118, 166, 54, 26, "Send", 0, 0);
    print_c(145, 195, "FX", P_SILK);
    panel_button(178, 166, 54, 26, "Ask", 0, 0);
    print_c(205, 195, "SEL", P_SILK);
}
/* RND PATCH: what the synth holds across new patches, grouped as the Bass Station II groups its panel: PATCH (the
 * Type and Tracks switches, SELECT and ALGO, the LED under the place set; RND: the synth picks) and KEY (Root, Octave,
 * Scale: K1-K3). By each group's name, in cyan, what the synth plays now; SEL lets every lock go */
static void rnd_switch(int32_t cx, int32_t y, uint32_t k, uint32_t n, int32_t pos, const char *const *names,
                       int32_t gap)
{
    uint32_t i;
    int32_t x0 = cx - gap * (int32_t)(n - 1u) / 2;
    slide_h(cx - (9 * (int32_t)n + 15) / 2, y, (int32_t)n, pos, knob_lit(k) ? KF_LIT : 0u);
    tag_v(cx, y + 9, k, names[pos]);
    for (i = 0; i < n; i++) {
        led(x0 + gap * (int32_t)i, y + 27, (int32_t)i == pos);
        print_c(x0 + gap * (int32_t)i, y + 34, names[i], P_SILK);
    }
}
static void rnd_knob(int32_t cx, int32_t cy, uint32_t k, int32_t v, int32_t span, const char *name, const char *val,
                     int dim)   /* its name under it, what it holds under that (cyan while it turns) */
{
    const int lit = knob_lit(k) && !dim;
    bs2_knob(cx, cy, 15, v, span, (lit ? KF_LIT : 0u) | (dim ? KF_DIM : 0u));
    tag_v(cx, cy, k, val);
    print_c(cx, cy + 19, name, dim ? P_DIM : P_SILK);
    print_c(cx, cy + 33, val, P_DIM);
}
static void rnd_patch_draw(void)
{
    static const char *const TY[4] = {"RND", "SEQ", "DRN", "JNG"};
    static const char *const TR[5] = {"RND", "1", "2", "3", "4"};
    const rlock_t l = lock_get(rnd.code);
    char t[24], v[8];
    rnd_frame_draw("Patch Locks");
    cv_rect(114, 36, 3, pan_h - 41, P_BAR);   /* PATCH | KEY */
    cv_text(8, 40, &FONT_XS, "Type", P_SILK);
    if (rnd.type < 3u)
        cv_text(110 - text_w(&FONT_XS, RND_TYPE[rnd.type]), 40, &FONT_XS, RND_TYPE[rnd.type], P_CYAN);
    rnd_switch(57, 60, EN_SELECT, 4, l.type + 1, TY, 26);
    cv_text(8, 122, &FONT_XS, "Tracks", P_SILK);
    if (rnd.ntr) {
        int_text(rnd.ntr, t);
        text_cat(t, " PLAYING");
        cv_text(110 - text_w(&FONT_XS, t), 122, &FONT_XS, t, P_CYAN);
    }
    rnd_switch(57, 142, EN_ALGO, 5, l.tracks < 0 ? 0 : l.tracks, TR, 21);
    cv_text(124, 40, &FONT_XS, "Key", P_SILK);
    if (rnd.root < 12u && rnd.scale < NRSCALE) {   /* what the synth plays: its own line */
        t[0] = 0;
        text_cat(t, RND_ROOT[rnd.root]);
        text_cat(t, " ");
        text_cat(t, RND_SCALE[rnd.scale]);
        print_c(176, 56, t, P_CYAN);
    }
    rnd_knob(148, 94, EN_K1, l.root + 1, 12, "Root", l.root < 0 ? "RANDOM" : RND_ROOT[l.root], 0);
    int_text(l.oct, v);
    rnd_knob(208, 94, EN_K2, l.oct + 3, 6, "Octave", l.root < 0 ? "-" : v, l.root < 0);
    rnd_knob(148, 160, EN_K3, l.scale + 1, NRSCALE, "Scale", l.scale < 0 ? "RANDOM" : RND_SCALE[l.scale], 0);
    panel_button(180, 148, 54, 26, "All RND", 0, 0);
    tag_at(207, 148, "SEL", 1);
    print_c(207, 178, "SEL", P_SILK);
}
/* RND TRACKS: the synth's four tracks as a mixer: Row Select at the left (PRESET: Engine, Level, Reverb: what K1-K4
 * set, as Env Select picks the ENVELOPES' faders), a strip a track (K1-K4): its number and the engine it plays (cyan;
 * dim: not playing), its engine held (RND: the synth picks), its level fader and its reverb knob; the rows K1-K4
 * don't set dim */
static void rnd_tracks_draw(void)
{
    static const char *const ROW[NRR] = {"Engine", "Level", "Reverb"};
    static const int32_t RY[NRR] = {73, 129, 188};   /* (Level: the faders' middle) */
    const rlock_t l = lock_get(rnd.code);
    uint32_t i;
    char v[16];
    rnd_frame_draw("Tracks");
    print_c(33, 40, "Row", P_SILK);
    print_c(33, 54, "Select", P_SILK);
    for (i = 0; i < NRR; i++)                /* each row's name beside it: the engines, the faders, the knobs */
        cv_text(62 - text_w(&FONT_XS, ROW[i]), RY[i], &FONT_XS, ROW[i], i == rnd_row ? P_SILK : P_DIM);
    slide_v(6, 100, NRR, rnd_row, knob_lit(EN_PRESET) ? KF_LIT : 0u);
    tag_sv(6, 100, NRR, EN_PRESET);
    cv_rect(66, 36, 3, pan_h - 41, P_BAR);   /* Row Select | the tracks */
    for (i = 0; i < 4u; i++) {
        const int32_t cx = 92 + 40 * (int32_t)i;
        const int on = i < rnd.ntr;
        const uint32_t lit = knob_lit(EN_K1 + i) ? KF_LIT : 0u;
        const char n[2] = {(char)('1' + i), 0};
        print_c(cx, 38, n, on ? P_SILK : P_DIM);
        print_c(cx, 54, on && rnd.eng[i] < NENG ? RND_ENG_S[rnd.eng[i]] : "-", on ? P_CYAN : P_DIM);
        cv_rect(cx - 19, 72, 38, 17, P_KSHADOW);   /* the engine held, on a black window */
        print_c(cx, 73, l.eng[i] < 0 ? "RND" : RND_ENG_S[l.eng[i]],
                rnd_row != RR_ENG ? P_DIM : P_SILK);
        fader(cx, 96, 80, rnd.vol[i] > 127u ? 64 : rnd.vol[i], 127, rnd_row == RR_LEVEL ? lit : KF_DIM);
        bs2_knob(cx, 195, 11, rnd.rev[i] > 127u ? 64 : rnd.rev[i], 127, rnd_row == RR_REVERB ? lit : KF_DIM);
        if (rnd_row == RR_LEVEL)                 /* (on the row they set, its value) */
            level_text(rnd.vol[i] > 127u ? 64u : rnd.vol[i], v);
        else if (rnd_row == RR_REVERB)
            reverb_text(i, rnd.rev[i] > 127u ? 64u : rnd.rev[i], v);
        tag_v(cx, rnd_row == RR_ENG ? 80 : rnd_row == RR_LEVEL ? 136 : 195, EN_K1 + i,
              rnd_row == RR_ENG ? (l.eng[i] < 0 ? "RANDOM" : RND_ENG_S[l.eng[i]]) : v);
    }
}
/* the ARPEGGIATOR (the Bass Station II's): Tempo (its LED on the beat) and Rhythm, the Note Mode knob with its modes
 * round it, Octaves (Arp Octaves, or the sequence: Seq); the On/Legato and Latch/Rest buttons (PRESET, ALGO), their
 * LEDs lit while the arp is on, latched */
static void arp_draw(void)
{
    static const char *const MODES[NARPM] = {"Up", "Dn", "UpDn 1", "UpDn 2", "Played", "Random", "Play", "Record"};
    const int run = eng.src == SRC_ARP || eng.src == SRC_SEQ;
    uint32_t i;
    int32_t x, y;
    panel_head("ARPEGGIATOR");
    if (ext_clock()) {                       /* TEMPO at MIDI clock's (the patch's until it is measured) */
        bs2_knob(30, 50, 16, clamp(arp_tempo() ? arp_tempo() : (int32_t)patch.tempo, 40, 240) - 40, 200,
                 knob_lit(EN_K1) ? KF_LIT : 0u);
        tag(30, 50, EN_K1);
        print_c(30, 79, "MIDI clock", P_SILK);
    } else
        print_knob(30, 50, 16, CCP(PG_ARP, EN_K1), EN_K1, 1, "", "Tempo");   /* (its name under its range) */
    cv_text(7, 70, &FONT_T, "40", P_SILK);   /* (clear of the panel's edge) */
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
        tag(172, 120, EN_K4);
        for (i = 0; i < 4u; i++) {
            char d[2] = {(char)('1' + i), 0};
            pt_at(172, 120, 21, ((float)i / 3.0f - 1.0f) * (FM_PI * 3.0f / 4.0f), &x, &y);
            print_c(x, y - 5, d, lit && i == v ? P_CYAN : P_SILK);
        }
    }
    cv_text(196, 102, &FONT_T, "Arp", patch.arp_mode < ARP_PLAY ? P_SILK : P_DIM);
    cv_text(196, 113, &FONT_T, "Octaves", patch.arp_mode < ARP_PLAY ? P_SILK : P_DIM);
    cv_text(196, 125, &FONT_T, "Seq", patch.arp_mode >= ARP_PLAY ? P_SILK : P_DIM);
    led(12, 113, patch.arp_on);              /* On / Legato (PRESET), Latch / Rest (ALGO): their LEDs lit, as on the panel */
    panel_button(22, 103, 40, 20, "On", 0, knob_lit(EN_PRESET));
    print_c(42, 127, "Legato", P_SILK);
    panel_button(72, 103, 40, 20, "Latch", 0, knob_lit(EN_ALGO));
    tag(42, 103, EN_PRESET);
    tag(92, 103, EN_ALGO);
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
        tag(66 + 54 * (int32_t)i, 33, EN_SELECT + i);
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
        tag(41 + 52 * (int32_t)i, 133, EN_K1 + i);   /* (under its tape: its value left showing) */
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
/* the OSCILLATORS (the Bass Station II's, without the sub osc: SUB OSC + MIXER's): five knobs (Coarse, Fine, the pitch's Mod
 * Env and LFO 1 depths, Pulse Width), the Range and the Waveform with their LEDs, Osc Select (SEL), Sync 1-2, the pulse
 * width's switch. Two views: PG_OSC the pitch's (the width dim), PG_MIX the width's (the rest dim) */
static void osc_draw(void)
{
    static const char *const L1[5] = {"Coarse", "Fine", "Mod Env", "LFO 1", "Pulse"}, *const L2[5] = {0, 0, "depth", "depth", "Width"};
    static const char *const FT[NRANGE] = {"16'", "8'", "4'", "2'"};
    const int pw = ui.page == PG_MIX;
    const param_t *q[5];
    const param_t *op = ui.osc_sel ? OSC2_P : PARAMS[PG_OSC], *pq = &PW_P[ui.osc_sel][ui.pw_src];
    uint32_t i, range = (uint32_t)param_get(&op[EN_SELECT], &patch), wave = (uint32_t)param_get(&op[EN_ALGO], &patch);
    uint16_t on = pw ? P_DIM : P_SILK, wo = pw ? P_SILK : P_DIM;   /* the pitch's print, the width's */
    panel_head("OSCILLATORS");
    q[0] = &op[EN_K1], q[1] = &op[EN_K2], q[2] = &op[EN_K3], q[3] = &op[EN_K4], q[4] = pq;
    for (i = 0; i < 5u; i++) {               /* the knobs: K1 to K4 (the pitch's view), the width's K4 */
        int32_t cx = 29 + 46 * (int32_t)i, v = param_get(q[i], &patch);
        uint32_t k = i < 4u ? EN_K1 + i : EN_K4, mine = (i == 4u) == pw, lit = mine && knob_lit(k);
        bs2_knob(cx, 38, 16, v - q[i]->lo, q[i]->hi - q[i]->lo,
                 (lit ? KF_LIT : 0u) | (q[i]->lo < 0 ? KF_BIP : 0u) | (mine ? 0u : KF_DIM));
        if (mine)
            tag(cx, 38, k);
        print_c(cx, 56, L1[i], mine ? P_SILK : P_DIM);
        if (L2[i])
            print_c(cx, 65, L2[i], mine ? P_SILK : P_DIM);
    }
    cv_rect(100, 79, 1, 6, on);              /* Pitch: Mod Env and LFO 1 depth's bracket */
    cv_rect(188, 79, 1, 6, on);
    cv_rect(100, 84, 89, 1, on);
    cv_rect(144 - text_w(&FONT_T, "Pitch") / 2 - 3, 79, text_w(&FONT_T, "Pitch") + 6, 10, grey(25));
    print_c(144, 77, "Pitch", on);
    cv_text(6, 101, &FONT_T, "Range", on);   /* Range (SELECT, on ALGO: ENC_MAP): the footage's LEDs */
    for (i = 0; i < NRANGE; i++) {
        print_c(60 + 16 * (int32_t)i, 89, FT[i], on);
        led(60 + 16 * (int32_t)i, 107, range == i);
    }
    cv_text(6, 129, &FONT_T, "Waveform", on);   /* Waveform (ALGO) */
    for (i = 0; i < NWAVES; i++) {
        osc_wave_icon(56 + 14 * (int32_t)i, 120, WAVE_SHOWN[i], on);
        led(60 + 14 * (int32_t)i, 137, wave == WAVE_SHOWN[i]);
    }
    print_c(141, 90, "Osc Select", P_SILK);  /* Osc Select (osc_sel_k): both views */
    cv_text(116, 106, &FONT_T, "1", P_SILK);
    slide_h(124, 102, 2, ui.osc_sel, knob_lit(osc_sel_k()) ? KF_LIT : 0u);
    cv_text(161, 106, &FONT_T, "2", P_SILK);
    print_c(141, 124, "Sync 1-2", wo);       /* Sync 1-2 (the width's view: its SELECT) */
    led(134, 143, patch.sync);
    cv_text(143, 137, &FONT_T, "On", wo);
    if (pw && knob_lit(EN_SELECT))
        disc(134, 143, 1, P_CYAN);
    cv_text(174, 90, &FONT_T, "Mod Env", wo);   /* the width's switch (PRESET): Mod Env, Manual, LFO 2 */
    cv_rect(211, 91, 1, 9, wo);
    cv_text(236 - text_w(&FONT_T, "LFO 2"), 90, &FONT_T, "LFO 2", wo);
    slide_h(184, 104, NPW, PW_POS[ui.pw_src], (pw && knob_lit(EN_PRESET) ? KF_LIT : 0u) | (pw ? 0u : KF_DIM));
    print_c(205, 125, "Manual", wo);
    tag_sh(124, 102, 2, osc_sel_k());
    if (pw) {
        tag_sh(184, 104, NPW, EN_PRESET);
        tag(134, 143, EN_SELECT);            /* (Sync's LED) */
    } else {
        tag(84, 107, EN_SELECT);             /* Range's LEDs, Waveform's */
        tag(88, 137, EN_ALGO);
    }
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
/* HOME's scope, between the name and the panel: the synth's output (scope_buf), two of its periods (oscillator 1's,
 * the sub's when it's in the mix: an octave or two under) from its steepest rising zero crossing, so a held note stands
 * still; scaled to its own level (a quiet sound as big), its trace with a
 * dim glow under it; a flat line in silence */
enum { SC_X = 4, SC_Y = 59, SC_W = 232, SC_H = 29 };   /* (clear of the display's edge, over the panel's) */
static int scope_live(void) { return voice.amp.stage != ENV_IDLE; }
static void scope_paint(void)
{
    static float gain = 1.0f;
    const uint32_t w = scope_w, M = SCOPE_N - 1u;
    const float inc = scope_inc;
    uint32_t i, j, span, per, start, best = 0;
    int32_t pk = 0, sl = 0;
    cv_rect(SC_X, SC_Y + SC_H / 2, SC_W, 1, grey(40));   /* (its middle) */
    if (!scope_live() || inc <= 0.0f) {
        cv_rect(SC_X, SC_Y + SC_H / 2, SC_W, 1, P_CDIM);
        return;
    }
    per = (uint32_t)clamp((int32_t)(0.25f / inc * (patch.lvl_sub ? patch.sub_oct == 2u ? 4.0f : 2.0f : 1.0f)), 24, 300);
    span = 2u * per;                         /* (at a quarter of the rate) */
    start = w - span - 64u;                  /* (the block the audio writes, behind it) */
    for (i = 0; i < per; i++) {              /* (a period back: the rising crossing that rises fastest) */
        j = start - i;
        if (scope_buf[(j - 1u) & M] < 0 && scope_buf[j & M] >= 0
            && scope_buf[(j + 2u) & M] - scope_buf[(j - 2u) & M] > sl)
            sl = scope_buf[(j + 2u) & M] - scope_buf[(j - 2u) & M], best = i;
    }
    start -= best;
    for (i = 0; i < SC_W; i++) {
        int32_t v = scope_buf[(start + i * span / SC_W) & M];
        pk = v > pk ? v : -v > pk ? -v : pk;
    }
    gain += ((pk > 6 ? 0.9f * 127.0f / (float)pk : 0.9f * 127.0f / 6.0f) - gain) * 0.3f;
    for (i = 0; i < SC_W; i++)
        level_col((int32_t)i, (float)scope_buf[(start + i * span / SC_W) & M] * gain * (1.0f / 127.0f), SC_H);
    trace(SC_X, SC_Y + 1, SC_W, SC_H, P_CDIM, 0);
    trace(SC_X, SC_Y, SC_W, SC_H, P_CYAN, 0);
}
static void scope_frame(void)                /* the scope alone, over HOME as it's drawn */
{
    cv_begin(240, SC_H + 1, P_BG);
    cv_oy = -SC_Y;
    scope_paint();
    cv_blit(0, 20 + SC_Y);
}
/* the audio's load on the GLO pages' bar (glo_frame), for testing: "CPU 34/41", now (the most of the last quarter second) and
 * the most of the last 5 s, then "!n" after any dropout since boot (the audio missed its deadline: a click) */
#define CPU_NOW_MS 250u
#define CPU_PEAK_MS 5000u
static uint32_t cpu_now, cpu_peak;
static int cpu_meter(uint32_t now)           /* a frame: 1 when the numbers shown changed */
{
    static uint32_t win, win_at, peak_at, shown;
    uint32_t c = plat_cpu_pct(), was = cpu_now | cpu_peak << 8 | (xruns_shown() & 0xFFFFu) << 16;
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
    if (xruns_shown()) {
        text_cat(t, " !");
        int_text((int32_t)(xruns_shown() > 999u ? 999u : xruns_shown()), n);
        text_cat(t, n);
    }
}

/* HOME: the Bass Station II's whole front panel to the footer keys' scale: they are its keys and its wheels (from the
 * wheels' left edge to the keys' right end, 125.4 of bgoonz/bass-station's rem, over the footer's 224 px: 25/14 px a
 * rem), its foot on them; each control where that layout has it (in tenths of its rem: h_*) and as the sound has it.
 * Over it the name and the display, large */
#define HOME_Y 90                            /* the panel's top (its 38 rem down to the keys) */
#define HOME_L 8                             /* the panel's bars' margin, each side alike (63 .. 1293 tenths), as the
                                              * keys' (keys_draw) */
#define HL(r) ((r) * (240 - 2 * HOME_L) / 1230)   /* tenths of a rem: px */
#define HX(r) (HOME_L + HL((r) - 63))
#define HY(r) (HOME_Y + HL(r))
static void h_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* at least a pixel each way */
{
    cv_rect(HX(x), HY(y), HX(x + w) - HX(x) > 1 ? HX(x + w) - HX(x) : 1, HY(y + h) - HY(y) > 1 ? HY(y + h) - HY(y) : 1, c);
}
static void h_knob(int32_t x, int32_t y, int big, const param_t *q, int32_t v)   /* its dots' top left; v of q's range */
{
    const int32_t c = big ? 45 : 23, r = big ? 7 : 4, cap = big ? 5 : 2, cx = HX(x + c), cy = HY(y + c);
    const float a = ((float)(v - q->lo) / (float)(q->hi - q->lo) - 0.5f) * (FM_PI * 5.0f / 3.0f);
    int32_t i, x0, y0, x1, y1;
    for (i = 0; i < 12; i++)                 /* the dots (a centred one's top brighter), none straight down */
        if (i != 6) {
            pt_at(cx, cy, r + 2, (float)i * (FM_PI / 6.0f), &x0, &y0);
            cv_pset(x0, y0, i == 0 && q->lo < 0 ? P_SILK : grey(120));
        }
    disc(cx + 1, cy + 1, r, P_KSHADOW);
    shade_disc(cx, cy, r, 32, 40);
    shade_disc(cx, cy, cap, 226, 30);
    pt_at(cx, cy, cap, a, &x0, &y0);         /* its line: dark across the cap, white on the skirt */
    pt_at(cx, cy, r, a, &x1, &y1);
    cv_line(cx, cy, x0, y0, P_KSHADOW);
    cv_line(x0, y0, x1, y1, P_SILK);
}
static void h_param(int32_t x, int32_t y, int big, const param_t *q) { h_knob(x, y, big, q, param_get(q, &patch)); }
/* a slide switch of n positions (2: 3.3 rem long, 3: 4.2), bgoonz's top left before its turn; up: turned upright, pos
 * 0 at its top */
static void h_switch(int32_t x, int32_t y, int32_t n, int32_t pos, int up)
{
    const int32_t len = n == 2 ? 33 : 42, w = HL(len), cx = HX(x + len / 2), cy = HY(y + 9), t = 1 + pos * (w - 4) / (n - 1);
    const int32_t ww = up ? 3 : w, hh = up ? w : 3, x0 = cx - ww / 2, y0 = cy - hh / 2;
    cv_rect(x0, y0, ww, hh, grey(170));
    cv_rect(x0 + 1, y0 + 1, ww - 2, hh - 2, P_KSHADOW);
    cv_rect(up ? x0 : x0 + t, up ? y0 + t : y0, up ? 3 : 2, up ? 2 : 3, grey(96));
}
static void h_led(int32_t x, int32_t y, int on)   /* a bulb's top left */
{
    const int32_t cx = HX(x + 3), cy = HY(y + 3);
    if (on)
        cv_rect(cx - 1, cy - 1, 4, 4, RGB(84, 12, 16));
    cv_rect(cx, cy, 2, 2, on ? RGB(232, 40, 52) : P_LEDOFF_R);
}
static void h_button(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t face)
{
    h_rect(x, y, w, h, P_KSHADOW);
    cv_rect(HX(x) + 1, HY(y) + 1, HX(x + w) - HX(x) - 2, HY(y + h) - HY(y) - 2, face);
}
static void h_fader(int32_t x, int32_t y, int32_t v)   /* v 0..127 */
{
    const int32_t cx = HX(x + 20), y0 = HY(y), h = HL(111), cy = y0 + h - 3 - (h - 5) * v / 127;
    int32_t i;
    for (i = 0; i <= 4; i++)                 /* its scale */
        cv_rect(cx - 3, y0 + 2 + (h - 5) * i / 4, 7, 1, grey(90));
    cv_rect(cx - 1, y0, 3, h, grey(30));
    cv_rect(cx, y0, 1, h, P_KSHADOW);
    cv_rect(cx - 2, cy - 1, 5, 4, grey(56));   /* its cap, the line on it */
    cv_rect(cx - 2, cy, 5, 1, P_SILK);
}
static void h_digit_c(int32_t x, int32_t y, char c, uint16_t col)   /* a digit, 3 x 5 */
{
    static const int8_t R[7][4] = {{0, 0, 3, 1}, {2, 0, 1, 3}, {2, 2, 1, 3}, {0, 4, 3, 1},
                                   {0, 2, 1, 3}, {0, 0, 1, 3}, {0, 2, 3, 1}};   /* a..g */
    uint32_t m = seg_mask(c), i;
    for (i = 0; i < 7u; i++)
        if (m >> i & 1u)
            cv_rect(x + R[i][0], y + R[i][1], R[i][2], R[i][3], col);
}
static void h_digit(int32_t x, int32_t y, char c) { h_digit_c(x, y, c, RGB(252, 78, 112)); }   /* the display's */
/* the case's rounded ends: the face going on round them, the light on the shoulder, dark where it turns away (steps of
 * 8: no tint on the 565 panel) */
static void bs_ends(void)
{
    static const uint8_t E[4] = {8, 24, 40, 32};   /* from the outside in; then the face's 24 */
    int32_t i;
    for (i = 0; i < 4; i++) {
        cv_rect(i, HOME_Y, 1, pan_h - HOME_Y, grey(E[i]));
        cv_rect(239 - i, HOME_Y, 1, pan_h - HOME_Y, grey(E[i]));
    }
}
static void home_draw(void)
{
    static const int16_t TOP_GAP[3] = {333, 753, 924}, MID_GAP[4] = {333, 402, 693, 1140};
    static const int16_t KT_GAP[7] = {395, 526, 767, 860, 940, 982, 1062};
    static const uint8_t WAVE_LED[NWAVES] = {2, 3, 0, 1, 3};   /* WAVE_*: the LEDs' sine, triangle, saw, square */
    const param_t *osc = ui.osc_sel ? OSC2_P : PARAMS[PG_OSC], *env = ui.env_sel == 1u ? MENV_P : PARAMS[PG_AENV];
    const uint32_t f = patch.filter, acid = f == FLT_ACID, trig = patch.env_trig[ui.env_sel == 1u];
    const int dir = org_dir();
    char d[4];
    int32_t i;
    /* the name, the display (as the MASTER's), what plays */
    cv_text(10, 12, &FONT_B, "BASS", P_SILK);
    cv_text(cv_text(10, 30, &FONT_B, "SOMETHING", P_SILK) + 5, 30, &FONT_B, "2", P_BAR);
    cv_rect(10, 49, 116, 2, P_BAR);
    slot_disp(d);
    if (ui.org_at)                           /* (a control just changed: its value) */
        seg_num(param_get(ui.org, &patch), d);
    seg_display(240 - HOME_L - 84, 6, d, 3);   /* (its right edge the panel's: 84 wide) */
    if (set_dirty)
        disc(240 - HOME_L - 10, 43, 2, RGB(252, 78, 112));
    tag(240 - HOME_L - 42, 31, EN_PRESET);
    scope_paint();                           /* (what plays and its tempo: the footer's) */
    /* the panel: its face, round its ends, the blue bars (their gaps, the lines down from them), the
     * keys' strip */
    cv_rect(0, HOME_Y, 240, pan_h - HOME_Y, grey(24));
    bs_ends();
    h_rect(63, 33, 1230, 15, P_BAR);
    h_rect(63, 190, 1230, 3, P_BAR);
    for (i = 0; i < 3; i++) {
        h_rect(TOP_GAP[i], 33, 3, 15, grey(20));
        h_rect(TOP_GAP[i] - 3, 48, 3, 142, P_BAR);
    }
    h_rect(63, 321, 1230, 15, P_BAR);
    for (i = 0; i < 4; i++) {
        h_rect(MID_GAP[i], 321, 3, 15, grey(20));
        h_rect(MID_GAP[i] - 3, 193, 3, 128, P_BAR);
    }
    h_rect(690, 51, 45, 15, P_BAR);          /* Sub Osc's */
    h_rect(690, 65, 3, 118, P_BAR);
    h_rect(63, 343, 1227, 3, P_BAR);         /* over the keys: their second functions' (from the left, as the bar
                                              * above) */
    h_rect(1287, 343, 3, 12, P_BAR);
    for (i = 0; i < 7; i++)
        h_rect(KT_GAP[i], 343, 3, 3, grey(24));
    h_rect(70, 146, 110, 14, P_SILK);        /* the maker's name and the model's (print too small to read) */
    h_rect(70, 168, 105, 10, P_CYAN);
    /* the MASTER: Volume, the display, the Org. Value LEDs, Save and the Patch buttons */
    {
        static const param_t VOL = {"VOLUME", 0, 0, 4095, 0};
        h_knob(90, 65, 0, &VOL, (int32_t)plat_master());
    }
    h_rect(175, 54, 135, 45, RGB(4, 4, 4));
    h_rect(180, 58, 75, 36, RGB(20, 16, 20));
    for (i = 0; i < 3; i++)
        h_digit(HX(185) + 4 * i, HY(62), d[i]);
    h_led(291, 62, dir > 0);
    h_led(291, 82, dir < 0);
    h_rect(223, 112, 84, 1, P_SILK);
    h_button(175, 120, 39, 18, grey(34));
    h_button(221, 120, 37, 34, knob_lit(EN_PRESET) && ui.pdir < 0 ? P_CDIM : grey(34));
    h_button(267, 120, 37, 34, knob_lit(EN_PRESET) && ui.pdir > 0 ? P_CDIM : grey(34));
    h_rect(242, 166, 46, 1, P_SILK);
    /* the OSCILLATORS (Osc Select's): Coarse, Fine, Mod Env and LFO 1 depths, Pulse Width; the Range and Waveform
     * LEDs and buttons, Osc Select, Sync 1-2's LED, the PW switch */
    for (i = 0; i < 4; i++)
        h_param(349 + 71 * i, 66, 0, &osc[EN_K1 + i]);
    h_param(633, 66, 0, &PW_P[ui.osc_sel][ui.pw_src]);
    for (i = 0; i < 4; i++) {
        h_led(350 + 13 * i, 146, i == param_get(&osc[EN_SELECT], &patch));
        h_led(420 + 13 * i, 146, i == WAVE_LED[param_get(&osc[EN_ALGO], &patch)]);
    }
    h_button(345, 167, 51, 15, grey(34));
    h_button(418, 167, 51, 15, grey(34));
    h_rect(514, 131, 1, 8, P_SILK);          /* Pitch's bracket under Mod Env and LFO 1 */
    h_rect(514, 138, 68, 1, P_SILK);
    h_rect(581, 131, 1, 8, P_SILK);
    h_switch(533, 168, 2, ui.osc_sel, 0);
    h_led(596, 172, patch.sync);
    h_rect(654, 130, 1, 10, P_SILK);
    h_switch(635, 145, 3, PW_POS[ui.pw_src], 0);
    /* SUB OSC: its octave and wave, LEDs and switches; the MIXER: Osc 1, Osc 2, Sub Osc, the switch's level */
    h_led(698, 77, patch.sub_oct == 1u);
    h_led(698, 95, patch.sub_oct == 2u);
    h_switch(744, 74, 2, patch.sub_oct - 1, 1);
    for (i = 0; i < NSUBW; i++)
        h_led(698, 135 + 16 * i, i == patch.sub_wave);
    h_switch(744, 136, 3, patch.sub_wave, 1);
    h_param(767, 58, 0, CCP(PG_FX, EN_K1));
    h_param(862, 58, 0, CCP(PG_FX, EN_K2));
    h_param(767, 126, 0, CCP(PG_FX, EN_PRESET));
    h_param(862, 126, 0, &MIX_SRC[ui.mix_src]);
    h_switch(826, 88, 3, NMS - 1 - ui.mix_src, 1);   /* (between the columns, as MIX's: bgoonz's 84.6 rem is on Osc 2) */
    h_rect(837, 174, 1, 9, P_SILK);          /* its names' bracket to their knob */
    h_rect(837, 182, 46, 1, P_SILK);
    h_rect(882, 174, 1, 9, P_SILK);
    /* the FILTERS: Type, Slope and Shape (switches, LEDs), Overdrive, Resonance, Mod Env and LFO 2 depths, Frequency */
    h_led(940, 54, !acid);
    h_led(988, 54, acid);
    h_switch(951, 70, 2, acid, 0);
    h_led(940, 95, !acid && f & 1u);
    h_led(988, 95, !acid && !(f & 1u));
    h_switch(951, 110, 2, !(f & 1u), 0);
    h_switch(948, 146, 3, acid ? 0 : f / 2u, 0);
    for (i = 0; i < 3; i++)
        h_led(948 + 17 * i, 168, !acid && (uint32_t)i == f / 2u);
    h_rect(929, 58, 1, 96, P_SILK);
    h_rect(929, 58, 9, 1, P_SILK);
    h_rect(929, 118, 17, 1, P_SILK);
    h_rect(929, 154, 14, 1, P_SILK);
    h_param(1018, 57, 0, CCP(PG_TUNE, EN_K1));
    h_param(1096, 57, 0, CCP(PG_TUNE, EN_K3));
    h_param(1018, 128, 0, CCP(PG_TUNE, EN_K2));
    h_param(1096, 128, 0, CCP(PG_FMOD, EN_K1));
    h_param(1191, 56, 1, CCP(PG_TUNE, EN_K4));
    h_rect(1009, 130, 5, 1, P_SILK);         /* the depths' brackets, their lines to Frequency */
    h_rect(1009, 130, 1, 41, P_SILK);
    h_rect(1009, 170, 5, 1, P_SILK);
    h_rect(1145, 130, 5, 1, P_SILK);
    h_rect(1150, 130, 1, 41, P_SILK);
    h_rect(1145, 170, 5, 1, P_SILK);
    h_rect(1071, 151, 19, 1, P_SILK);
    h_rect(1155, 151, 54, 1, P_SILK);
    /* the ARPEGGIATOR: Tempo, Rhythm, Note Mode, Octaves, On and Latch */
    for (i = 0; i < 3; i++)
        h_param(i ? 168 + 76 * (i - 1) : 90, 209, 0, CCP(PG_ARP, EN_K1 + i));
    h_param(244, 265, 0, CCP(PG_ARP, EN_K4));
    h_led(140, 207, patch.arp_on);
    h_led(70, 283, patch.arp_on);
    h_button(96, 280, 39, 18, patch.arp_on ? RGB(150, 26, 44) : grey(34));
    h_button(147, 280, 39, 18, patch.latch ? RGB(150, 26, 44) : grey(34));
    h_led(207, 283, patch.latch);
    /* PORTA, the LFOS (each its wave button and LEDs, its knob: speed, sync value or delay), Delay/Speed */
    h_param(343, 208, 0, CCP(PG_LFO1, EN_SELECT));
    for (i = 0; i < (int32_t)NLFO; i++) {
        int32_t j;
        h_button(i ? 662 : 411, 202, 15, 52, grey(34));
        for (j = 0; j < NLFOW; j++)
            h_led(i ? 643 : 442, 206 + 13 * j, j == patch.lfo[i].wave);
        h_param(i ? 571 : 476, 209, 0,
                ui.lfo_delay ? &LFOD_P[i] : patch.lfo_sync[i] ? &LFOSV_P[i] : CCP(PG_LFO1, EN_K2 + i));
    }
    h_switch(555, 260, 2, !ui.lfo_delay, 1);
    /* the ENVELOPES: Env Select, the four faders (the envelope it picks), Triggering (its LEDs, Autoglide at the top) */
    h_switch(730, 231, 3, ui.env_sel, 1);    /* (as far in from the section's left as Triggering from its right: bgoonz's
                                              * 78.3 rem leaves a gap) */
    h_rect(762, 250, 50, 1, P_SILK);
    for (i = 0; i < 4; i++)
        h_fader(812 + 543 * i / 10, 195, param_get(&env[EN_K1 + i], &patch));
    h_switch(1061, 231, 3, (int32_t)(NENVT - 1u - trig), 1);
    for (i = 0; i < NENVT; i++)
        h_led(1079, 232 + 13 * i, NENVT - 1u - (uint32_t)i == trig);
    /* the EFFECTS: Distortion, Osc Filter Mod */
    h_param(1160, 209, 0, CCP(PG_EFX, EN_K1));
    h_param(1233, 209, 0, CCP(PG_EFX, EN_K2));
}

static void home_sect(uint32_t g, int16_t *x, int16_t *y, float *lc)   /* page g's section on HOME: its middle, and
                                              * the scale (log2) it fills the screen at, as its page does */
{
    static const int16_t S[8][3] = {{333, 690, 33}, {690, 924, 33}, {924, 1293, 33}, {63, 333, 190},
                                    {333, 693, 190}, {693, 1140, 190}, {1140, 1293, 190}, {63, 333, 33}};   /* across, its top */
    const uint32_t i = g == PG_OSC || g == PG_MIX ? 0u : g == PG_FX ? 1u : g == PG_TUNE || g == PG_FMOD ? 2u
                       : g == PG_ARP || g == PG_AMOD || g == PG_SEQ ? 3u : g == PG_LFO1 || g == PG_LFO2 ? 4u
                       : g == PG_EFX || g == PG_XMOD ? 6u : g == PG_PATCH || g == PG_NAME ? 7u : 5u;
    const int32_t w = HX(S[i][1]) - HX(S[i][0]), h = HL(S[i][2] == 33 ? 157 : 146);
    *x = (int16_t)((HX(S[i][0]) + HX(S[i][1])) / 2);
    *y = (int16_t)(HY(S[i][2]) + h / 2);
    *lc = fm_log2f(fm_minf(fm_minf(240.0f / (float)w, (float)TALL_H / (float)h), 6.0f));
    if (g == PG_AENV) {                      /* (the section's left, Env Select and the faders: 4x lands HOME's faders on
                                              * the page's) */
        *x = 155;
        *y = 136;
        *lc = fm_log2f(4.05f);
    }
}

/* a section's button: its page, the camera going in on its section if from the synth's HOME (no section for GLO's); its
 * button's hint after, from there (the next tap's) */
static void sect_open(uint32_t g, uint32_t k)
{
    if (set.zoom_off || ui.page != PG_HOME || zoom.home || PAGE_SEC[g] == SEC_GLO) {
        page_go(g);
        return;
    }
    zoom_keep(PG_HOME);
    home_sect(g, &zoom.hx, &zoom.hy, &zoom.lc);
    zoom.g = (uint8_t)g;
    zoom.bk = (uint8_t)k;
    zoom.u = 0.0f;
    zoom.home = ZH_OPEN;
    ui.redraw = 1;
}

/* Drum Something's HOME: the drum machine's whole panel, after the Sequential Drumtraks' (black, its sections in
 * thin white frames sharing their walls, red LEDs and displays, black keys, the keypad's grey). Places are a photo's
 * pixels measured (tools/dt_face.py), its frames across 151..1348 onto HOME's 224 columns (HOME_L's margin, as the
 * BS2's panel: 0.187 px each). Its name and its two displays over it; an instrument's key lit as it plays (dt_lit) */
#define DT_FACE_G 32                         /* the plate's grey */
#define DT_FACE grey(DT_FACE_G)
#define DT_LINE_G 232                        /* its frames (4.5 of the photo's pixels: nearly a whole pixel) */
#define DT_THIN grey(160)                    /* its thin lines (3 of them) */
#define DT_LIT_MS 120u                       /* a key lit after its hit */
/* a photo's x and y, to the column and row they're in: HOME's whole panel; the section pages' zoomed crops set these while
 * they draw (dt_crop) */
static int32_t dt_ux0 = 151, dt_x0 = HOME_L, dt_sx = 187, dt_vy0 = 56, dt_y0 = 47, dt_sy = 187;
static int32_t dt_px(int32_t u) { return dt_x0 + (u - dt_ux0) * dt_sx / 1000; }
static int32_t dt_py(int32_t v) { return dt_y0 + (v - dt_vy0) * dt_sy / 1000; }
static uint32_t dt_lit(void)                 /* the instruments lit: held (ui.drum's keys) or just hit (ui.dt_at) */
{
    const uint32_t now = plat_ms();
    uint32_t i, m = 0;
    for (i = 0; i < NDT; i++)
        if ((ui.dt_at[i] && now - ui.dt_at[i] < DT_LIT_MS) || (ui.drum && ui.keys >> DT_PAD[i] & 1u))
            m |= 1u << i;
    return m;
}
static void dt_frame_at(int32_t x, int32_t y, int32_t X, int32_t Y, int r3);
static void dt_frame(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int r3)   /* a section's frame on the photo's
                                              * lines; its corners' arcs (r 2.3 or, r3, 3.2 px) in eighths of the line */
{
    dt_frame_at(dt_px(x0), dt_py(y0), dt_px(x1), dt_py(y1), r3);
}
static void dt_frame_at(int32_t x, int32_t y, int32_t X, int32_t Y, int r3)   /* (in the panel's pixels) */
{
    static const uint8_t A2[3][3] = {{0, 5, 8}, {5, 5, 0}, {8, 0, 0}};
    static const uint8_t A3[4][4] = {{0, 3, 6, 8}, {3, 7, 2, 0}, {6, 2, 0, 0}, {8, 0, 0, 0}};
    const int32_t r = r3 ? 3 : 2;
    int32_t i, j;
    cv_rect(x + r, y, X - x - 2 * r + 1, 1, grey(DT_LINE_G));
    cv_rect(x + r, Y, X - x - 2 * r + 1, 1, grey(DT_LINE_G));
    cv_rect(x, y + r, 1, Y - y - 2 * r + 1, grey(DT_LINE_G));
    cv_rect(X, y + r, 1, Y - y - 2 * r + 1, grey(DT_LINE_G));
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            const int32_t c = r3 ? A3[j][i] : A2[j][i];
            const uint16_t g = grey(DT_FACE_G + (DT_LINE_G - DT_FACE_G) * c / 8);
            if (c) {
                cv_pset(x + i, y + j, g);
                cv_pset(X - i, y + j, g);
                cv_pset(x + i, Y - j, g);
                cv_pset(X - i, Y - j, g);
            }
        }
}
static void dt_bkey(int32_t u, int32_t v, int lit)   /* a lower black key, its recess's top left at the photo's u, v
                                              * (57 x 56 there): the recess black, its walls a shade up, the cap, the
                                              * bright wall at its right, mitred; lit, the cap white */
{
    const int32_t x = dt_px(u), y = dt_py(v);
    cv_rect(x, y, 11, 11, RGB(0, 0, 0));
    cv_rect(x + 1, y + 1, 9, 9, grey(16));
    cv_rect(x + 2, y + 2, 6, 7, lit ? P_SILK : grey(40));
    cv_rect(x + 8, y + 2, 1, 7, lit ? P_SILK : grey(200));
    cv_pset(x + 8, y + 1, grey(96));
    cv_pset(x + 8, y + 9, grey(120));
}
static void dt_ukey(int32_t u, int32_t v)    /* an upper black key (44 x 43 there; the light's not on its wall): its
                                              * black recess, the cap, a faint right wall */
{
    const int32_t x = dt_px(u), y = dt_py(v);
    cv_rect(x, y, 8, 8, RGB(0, 0, 0));
    cv_rect(x + 1, y + 1, 6, 6, grey(48));
    cv_rect(x + 6, y + 1, 1, 6, grey(88));
}
static void dt_gkey(int32_t u, int32_t v, char c)   /* a keypad key (35 x 34 there): the recessed outer square, lit
                                              * below and at the right, the face, its digit or arrow */
{
    const int32_t x = dt_px(u), y = dt_py(v);
    int32_t i;
    cv_rect(x, y, 7, 1, grey(184));
    cv_rect(x, y + 1, 1, 6, grey(120));
    cv_rect(x + 6, y + 1, 1, 6, grey(216));
    cv_rect(x + 1, y + 6, 6, 1, grey(232));
    cv_rect(x + 1, y + 1, 5, 5, grey(176));
    cv_rect(x + 1, y + 1, 5, 1, grey(128));  /* (the groove round the face, its shadowed sides) */
    cv_rect(x + 1, y + 2, 1, 4, grey(128));
    if (c == '<' || c == '>')
        for (i = 0; i < 3; i++)
            cv_rect(c == '<' ? x + 2 + i : x + 4 - i, y + 3 - i, 1, 2 * i + 1, P_SILK);
    else
        h_digit_c(x + 2, y + 1, c, grey(224));
}
static void dt_led(int32_t u, int32_t v, int on)
{
    cv_rect(dt_px(u), dt_py(v), 2, 1, on ? RGB(255, 40, 72) : RGB(128, 16, 32));   /* (a row: they're 2.5 rows apart) */
}
static void dt_vline(int32_t u, int32_t v0, int32_t v1) { cv_rect(dt_px(u), dt_py(v0), 1, dt_py(v1) - dt_py(v0) + 1, DT_THIN); }
static void dt_hline(int32_t u0, int32_t u1, int32_t v) { cv_rect(dt_px(u0), dt_py(v), dt_px(u1) - dt_px(u0) + 1, 1, DT_THIN); }
/* a black knob at the photo's u, v (r 22.5 there), a pixel at a time: nothing but its silver side tells it from the
 * plate, the side seen from a little off top-down, brightest towards 15 degrees below the right (the photo's rim by
 * angle, base + amp * cos), thick by wide where it's lit; its shadow out at sx, sy; its short line at val of 0..10
 * (dt_knob_at: anywhere, radius R, val 0..1) */
static void dt_knob_at(float X, float Y, float R, float val, int32_t base, int32_t amp, float wide, int32_t sx, int32_t sy);
static void dt_knob(int32_t u, int32_t v, int32_t val, int32_t base, int32_t amp, float wide, int32_t sx, int32_t sy)
{
    const float X = (float)HOME_L + (float)(u - 151) * 0.187f + 0.5f, Y = 47.0f + (float)(v - 56) * 0.187f + 0.5f;
    dt_knob_at(X, Y, 4.2f, (float)val / 10.0f, base, amp, wide, sx, sy);
}
static void dt_knob_at(float X, float Y, float R, float val, int32_t base, int32_t amp, float wide, int32_t sx, int32_t sy)
{
    const float a = (val - 0.5f) * (FM_PI * 5.0f / 3.0f);
    const int32_t cx = (int32_t)X, cy = (int32_t)Y, n = (int32_t)R + 3;
    int32_t x, y, x0, y0, x1, y1;
    for (y = cy - n; y <= cy + n; y++)
        for (x = cx - n; x <= cx + n; x++) {
            const float dx = (float)x + 0.5f - X, dy = (float)y + 0.5f - Y, d = fm_sqrtf(dx * dx + dy * dy);
            const float sd = fm_sqrtf((dx - (float)sx) * (dx - (float)sx) + (dy - (float)sy) * (dy - (float)sy));
            const float c = d > 0.01f ? (dx * 0.966f + dy * 0.259f) / d : 0.0f;
            const float w = 0.5f + (c > 0.0f ? c * wide : 0.0f);
            const float rim = fm_minf(1.0f, fm_maxf(0.0f, w + 0.5f - (d > R ? d - R : R - d)));
            const float in = fm_minf(1.0f, fm_maxf(0.0f, R - d + 0.5f));
            const float sh = fm_minf(1.0f, fm_maxf(0.0f, R + 0.5f - sd));
            float g = (float)DT_FACE_G + ((float)16 - (float)DT_FACE_G) * sh;
            if (rim <= 0.0f && in <= 0.0f && sh <= 0.0f)
                continue;
            g += ((float)40 - g) * in;
            g += ((float)base + (float)amp * c - g) * rim;
            cv_pset(x, y, grey((int32_t)g));
        }
    pt_at(cx, cy, (int32_t)(R * 0.48f), a, &x0, &y0);
    pt_at(cx, cy, (int32_t)(R * 0.95f), a, &x1, &y1);
    cv_line(x0, y0, x1, y1, grey(216));
}
static void dt_digit(int32_t x, int32_t y, char c)   /* the plate's little display's digit, 5 x 6, leaning as the
                                              * photo's (the unlit segments show) */
{
    static const int8_t R[7][4] = {{1, 0, 4, 1}, {4, 0, 1, 3}, {3, 3, 1, 3}, {0, 5, 4, 1}, {0, 3, 1, 3}, {1, 0, 1, 3},
                                   {1, 3, 3, 1}};   /* a..g */
    const uint32_t m = seg_mask(c);
    uint32_t i, on;
    for (on = 0; on < 2u; on++)
        for (i = 0; i < 7u; i++)
            if (!on || (m >> i & 1u))
                cv_rect(x + R[i][0], y + R[i][1], R[i][2], R[i][3], on ? RGB(252, 60, 96) : RGB(72, 16, 24));
}
static void dt_small(int32_t u, const char *t)   /* its window (the photo's, 62 x 43, its pale left edge) and digits */
{
    const int32_t x = dt_px(u), y = dt_py(61) + 1;   /* (a row clear of the frame above) */
    cv_rect(x, y, 13, 8, RGB(32, 16, 20));
    cv_rect(x, y, 1, 8, grey(112));
    dt_digit(x + 1, y + 1, t[0]);
    dt_digit(x + 7, y + 1, t[1]);
}
static void dt_display(int32_t x, int32_t y, const char *t)   /* the 400's display: two red digits (seg7) */
{
    cv_rect(x, y, 54, 42, RGB(4, 4, 4));
    cv_rect(x + 3, y + 3, 48, 36, RGB(20, 12, 14));
    seg7(x + 6, y + 7, t[0]);
    seg7(x + 28, y + 7, t[1]);
}
/* a wooden cheek, 6 wide at x from row y0 down the panel (the 400's wooden ends, the plate's height): streaks a shade
 * apart down each column, at random but the same every draw (rfx_hash); edge its lit column, shadow its dark one by
 * the plate */
static uint32_t rfx_hash(uint32_t a, uint32_t b);
static void dt_cheek(int32_t x, int32_t y0, int32_t edge, int32_t shadow)
{
    static const uint16_t W[4] = {RGB(168, 96, 48), RGB(152, 80, 40), RGB(184, 112, 56), RGB(128, 64, 32)};
    const int32_t h = pan_h - y0;
    int32_t i, y;
    for (i = 0; i < 6; i++) {
        cv_rect(x + i, y0, 1, h, W[0]);
        for (y = -4; y < (int32_t)pan_h; y += 8) {   /* (by the panel's rows: the same grain whatever y0) */
            const uint32_t r = rfx_hash((uint32_t)(x + i), (uint32_t)(y + 8) / 8u);
            const int32_t at = y + (int32_t)(r % 6u), n = 3 + (int32_t)(r >> 4 & 7u) % 6;
            if (at >= y0)                    /* (none above the plate) */
                cv_rect(x + i, at, 1, at + n > (int32_t)pan_h ? (int32_t)pan_h - at : n, W[1u + (r >> 8) % 3u]);
        }
    }
    cv_rect(edge, y0, 1, h, RGB(200, 128, 72));
    cv_rect(shadow, y0, 1, h, RGB(80, 40, 16));
}
/* the print, too small to read here: a white line for each word (the photo's x0, x1, its middle's y) */
static void dt_print(void)
{
    static const int16_t WORD[][3] = {
        {214, 270, 86}, {424, 467, 111}, {476, 509, 112}, {519, 585, 112}, {951, 994, 111}, {1006, 1058, 111},
        {1071, 1137, 111}, {442, 484, 141}, {971, 1034, 141}, {442, 478, 154}, {487, 519, 154}, {972, 1018, 154},
        {1025, 1059, 154}, {442, 528, 168}, {535, 550, 168}, {1000, 1036, 168}, {1044, 1121, 168}, {189, 220, 177},
        {262, 294, 177}, {1225, 1302, 172}, {442, 477, 182}, {485, 519, 182}, {1001, 1039, 182}, {1046, 1098, 182},
        {1239, 1288, 193}, {473, 533, 196}, {972, 1028, 196}, {473, 496, 209}, {504, 551, 209}, {972, 1001, 209},
        {1011, 1075, 210}, {473, 511, 223}, {520, 566, 223}, {972, 1001, 223}, {1009, 1074, 223}, {222, 263, 235},
        {473, 515, 236}, {972, 1010, 236}, {1024, 1075, 236}, {473, 516, 250}, {972, 1004, 251}, {1011, 1069, 251},
        {1075, 1090, 251}, {507, 546, 284}, {1238, 1287, 309}, {182, 229, 322}, {256, 300, 322}, {1239, 1287, 331},
        {435, 455, 452}, {460, 468, 452}, {541, 560, 451}, {568, 577, 451}, {682, 703, 451}, {711, 721, 451},
        {825, 846, 451}, {854, 863, 451}, {969, 989, 452}, {996, 1005, 452}, {1184, 1205, 452}, {1214, 1224, 452},
        {174, 221, 486}, {302, 346, 484}, {208, 230, 499}, {236, 311, 499}, {436, 466, 603}, {508, 548, 603},
        {586, 609, 602}, {648, 680, 603}, {720, 753, 603}, {791, 831, 603}, {865, 892, 603}, {932, 976, 603},
        {1008, 1040, 603}, {1076, 1115, 603}, {165, 228, 604}, {296, 342, 603}, {1153, 1186, 604},
        {1215, 1270, 604}, {1291, 1340, 604}, {822, 870, 621}, {968, 982, 620}, {986, 1009, 620}};
    uint32_t i;
    for (i = 0; i < sizeof WORD / sizeof WORD[0]; i++) {
        const int32_t x = dt_px(WORD[i][0]), w = dt_px(WORD[i][1]) - x;
        cv_rect(x, dt_py(WORD[i][2]), w > 0 ? w : 1, 1, grey(216));
    }
}
/* our name where the 400 has its own, right to its frames' edge, in round letters like its: 8 rows of them drawn by
 * hand, # whole, + half, . faint */
static void dt_logo(void)
{
    static const char *const R[8] = {
        " .###                                                  ##    ##",
        "   ##                                              .   ##",
        " .### ###+ ## ## #+#+#+. .### .###. #+#+#+. .###. #### ##+#. ## ##+#. .####",
        "##.## ## # ## ## ## ## # ##   ##.## ## ## # ##.##  ##  ##.## ## ##.## ##.##",
        "## ## ###. ## ## ## ## # .##. ## ## ## ## # ####.  ##  ## ## ## ## ## ## ##",
        "##.## ## # ##.## ## ## #   ## ##.## ## ## # ##     ##. ## ## ## ## ## .####",
        " ###+ ## # .#### ## ## # ###. .###. ## ## # .###+  .## ## ## ## ## ##    ##",
        "                                                                      .###.",
    };
    int32_t x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; R[y][x]; x++)
            if (R[y][x] != ' ')
                cv_pset(232 - 75 + x, 104 + y, grey(R[y][x] == '#' ? 240 : R[y][x] == '+' ? 168 : 96));
}
/* the top of each of its pages: its name (for a moment the function just picked), and the two displays: the pattern's
 * number on the right, the song's on the left, as the 400; for a moment the tempo over both ("t1" "20"), or on the left
 * the level or pitch just turned */
static void dt_two(char *d, uint32_t v) { d[0] = (char)('0' + v / 10u); d[1] = (char)('0' + v % 10u); }
static void dt_digits(char *l, char *r)
{
    const uint32_t s = ui.dt_show, f = ui.dt_fn;
    const dq_pat_t *p = &dqm.pat[ui.dt_pat < DQ_NPAT ? ui.dt_pat : 0];
    const int32_t t = arp_tempo();
    l[0] = l[1] = r[0] = r[1] = ' ';
    l[2] = r[2] = 0;
    if (!dq.run && (ui.btn & DT_BOTH_OCT) == DT_BOTH_OCT) {
        dt_two(l, dt_mem_free());
        return;
    }
    if (dq.full) {                           /* (blinking on the 400) */
        if (plat_ms() / 300u % 2u) {
            l[0] = 'F', l[1] = 'U', r[0] = 'L', r[1] = 'L';
        }
        return;
    }
    if (s == DTS_METRO || s == DTS_MASTER) {  /* (0 .. 127: C the click, U the volume, then the hundreds) */
        const uint32_t v = s == DTS_METRO ? dt.metro : dt.master;
        l[0] = s == DTS_METRO ? 'C' : 'U';
        l[1] = v >= 100u ? '1' : ' ';
        dt_two(r, v % 100u);
        return;
    }
    if (s == DTS_TEMPO) {
        l[0] = 't';
        l[1] = t >= 100 ? (char)('0' + t / 100) : ' ';
        dt_two(r, (uint32_t)t % 100u);
        return;
    }
    if (f >= DTF_BPAT) {                     /* BUILD MODE: the step on the left, what's there on the right (the manual's 12) */
        const uint32_t c = dsm.s[ui.dt_song < DS_NSONG ? ui.dt_song : 0].step[ui.dt_step];
        dt_two(l, ui.dt_step);
        if (f == DTF_BTEMPO && !ui.dt_step) {
            const uint32_t b = ui.dt_pend ? (ui.dt_ent ? DQ_TEMPO[ui.dt_ent - 1] : 0u)
                               : dsm.s[ui.dt_song < DS_NSONG ? ui.dt_song : 0].tempo;
            l[0] = 't', l[1] = b >= 100u ? (char)('0' + b / 100u) : ' ';
            dt_two(r, b % 100u);
        } else if (f == DTF_BVOL || f == DTF_BTEMPO) {
            const int32_t v = ui.dt_pend ? ui.dt_ent : (c & 0xE0u) == (f == DTF_BVOL ? DS_VOL : DS_TEMPO) ? DS_VAL(c) : 0;
            l[0] = f == DTF_BVOL ? 'U' : 't', l[1] = v < 0 ? '-' : ' ';
            dt_two(r, (uint32_t)(v < 0 ? -v : v));
        } else if (ui.dt_pend && f == DTF_BPAT)
            dt_two(r, (uint32_t)ui.dt_ent);
        else if (c < DQ_NPAT + 1u)
            dt_two(r, c);
        else if (c == DS_END)
            r[0] = 'E';
        else if ((c & 0xE0u) == DS_VOL)
            r[0] = 'U';
        else if ((c & 0xE0u) == DS_TEMPO)
            r[0] = 't';
        return;
    }
    if (f == DTF_ERASES || f == DTF_COPYS)
        dt_two(l, ui.dt_tgt);
    else if (f >= DTF_SONG && dq.run && dq.in_song) {   /* (running: the step, its pattern) */
        dt_two(l, dq.sstep ? dq.sstep - 1u : 0u);
        dt_two(r, dq.cur);
    } else if (f >= DTF_SONG)
        dt_two(l, ui.dt_song);
    else if (f == DTF_ERASEI)
        r[0] = 'E', r[1] = 'r';
    else if (f == DTF_ERROR)
        l[0] = '1', l[1] = '-', dt_two(r, DQ_EC[p->ec]);
    else if (f == DTF_ROLL)
        l[0] = '1', l[1] = DQ_ROLL_T[set.droll] ? 't' : '-', dt_two(r, DQ_ROLL_N[set.droll]);
    else if (f == DTF_SWING)
        dt_two(r, DQ_SWING[p->swing]);
    else if (f == DTF_TIME)
        dt_two(l, p->num), dt_two(r, DQ_BEAT[p->beat]);
    else if (f == DTF_MEAS)
        dt_two(r, dq_bars(p) < 99u ? dq_bars(p) : 99u);
    else if (f == DTF_ERASEP || f == DTF_COPYP)
        dt_two(r, ui.dt_tgt);
    else
        dt_two(r, ui.dt_pat);
    if (s == DTS_VOL)
        dt_two(l, dt.vol[ui.dt_sel]);
    else if (s == DTS_TUNE)
        dt_two(l, dt.tune[ui.dt_sel]);
}
static void dt_head_top(void)                /* the name and the two displays, on their dark ground */
{
    static const char *const NAME[NDTF][2] = {
        {"PATTERN", "NUMBER"}, {"RECORD", "MODE"}, {"ERASE", "INSTRUMENT"}, {"ERROR", "CORRECT"}, {"SWING", "PERCENT"},
        {"TIME", "SIGNATURE"}, {"NUMBER OF", "MEASURES"}, {"ERASE", "PATTERN"}, {"COPY OR", "APPEND TO"}, {"ROLL", "RATE"}, {"SONG", "NUMBER"},
        {"ERASE", "SONG"}, {"COPY OR", "APPEND TO"}, {"BUILD", "MODE"}, {"BUILD:", "PATTERN"}, {"BUILD:", "VOLUME"},
        {"BUILD:", "TEMPO"}, {"BUILD:", "INSERT"}, {"BUILD:", "DELETE"}};
    const int fn = ui.dt_show == DTS_FN || dq.rec;   /* (recording or erasing: said all the while) */
    char l[3], r[3];
    dt_digits(l, r);
    cv_rect(0, 0, 240, 45, P_BG);
    cv_text(10, 6, &FONT_B, fn ? NAME[ui.dt_fn][0] : "DRUM", P_SILK);
    cv_text(10, 24, &FONT_B, fn ? NAME[ui.dt_fn][1] : "SOMETHING", P_SILK);
    cv_rect(10, 42, 96, 2, RGB(252, 60, 72));
    dt_display(114, 1, l);
    dt_display(174, 1, r);
}
static void dt_head(int right)               /* (right: its right cheek too) */
{
    dt_head_top();
    cv_rect(0, 45, 240, pan_h - 45, DT_FACE);
    dt_cheek(0, 45, 0, 5);
    if (right)
        dt_cheek(234, 45, 239, 234);
}
/* PRESET's tag and Knob Zoom's aim: the display it shows on. K1, K2 (on the Channel pages, their knobs off the page):
 * their value in a tag while they turn, the camera staying out */
static void dt_tag_preset(void)
{
    const int left = ui.dt_fn >= DTF_SONG && ui.dt_fn != DTF_BVOL && ui.dt_fn != DTF_BTEMPO;
    tag(left || dt_knobs_split() ? 141 : 201, 22, EN_PRESET);
    if (!ui.map_until || dt_knobs_split())   /* (ALGO the same display's: no second name in the map) */
        tag(left && !dt_knobs_split() ? 141 : 201, 22, EN_ALGO);
}
static void dt_tag_away(uint32_t k, int32_t x)
{
    if (ui.map_until)
        return;
    tag(x, 62, k);
    if (zoom.k == k + 1u)
        zoom.k = 0;
}
static const int16_t DT_CH_U[NDTCH + 1] = {415, 487, 630, 773, 916, 1059, 1348};   /* its channels' frames across HOME */

static void dch_key(int32_t x, int32_t y, int lit)   /* a black key, 28 x 32: its recess, its walls, the cap, the bright
                                              * wall at its right; lit, the cap white */
{
    cv_rect(x, y, 28, 32, RGB(0, 0, 0));
    cv_rect(x + 2, y + 2, 24, 28, grey(16));
    cv_rect(x + 4, y + 4, 18, 24, lit ? P_SILK : grey(40));
    cv_rect(x + 22, y + 4, 2, 24, lit ? P_SILK : grey(200));
}
/* the two sections' pages (SEQ, ARP): the faceplate itself, closer: the section and the displays and keypad beside it, on
 * the photo's measures at 0.43 px across, 0.5 down (the print's rows want the height); its print in small capitals */
static void dt_tiny(int32_t x, int32_t y, const char *s, uint16_t c)   /* 3 x 5 capitals: the photo's print at this size */
{
    static const char CH[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#%/";
    static const uint16_t G[sizeof CH - 1] = {   /* five rows of three bits, the top row high */
        0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79A7, 0x79A4, 0x396B, 0x5BED, 0x7497, 0x126A, 0x5BAD, 0x4927, 0x5FED,
        0x6B6D, 0x2B6A, 0x6BA4, 0x2B7B, 0x6BAD, 0x388E, 0x7492, 0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7,
        0x2B6A, 0x2C97, 0x62A7, 0x628E, 0x5BC9, 0x798E, 0x39AA, 0x7292, 0x2AAA, 0x2ACE, 0x5F7D, 0x52A5, 0x12A4,};
    for (; *s; s++, x += 4) {
        uint32_t i, r, b;
        for (i = 0; CH[i] && CH[i] != *s; i++)
            ;
        if (*s == ' ') {
            x -= 2;
            continue;
        }
        if (!CH[i])
            continue;
        for (r = 0; r < 5u; r++)
            for (b = 0; b < 3u; b++)
                if (G[i] >> (14u - r * 3u - b) & 1u)
                    cv_pset(x + (int32_t)b, y + (int32_t)r, c);
    }
}
static void dfn_row(int32_t led_u, int32_t text_u, int32_t v, int lit, const char *s)
{
    const int32_t y = dt_py(v);
    if (lit && zoom.fn_at) {                 /* (the camera's aim: its LED and its words together) */
        int32_t n = 0;                       /* (its words' length: no strlen on the device) */
        while (s[n])
            n++;
        zoom.tx = (int16_t)((dt_px(led_u) + dt_px(text_u) + 4 * n) / 2);
        zoom.ty = (int16_t)y;
        zoom.pg = ui.page;
        zoom.fn_ok = 1;
    }
    cv_rect(dt_px(led_u) - 2, y - 1, 5, 3, lit ? RGB(255, 40, 72) : RGB(96, 16, 32));
    dt_tiny(dt_px(text_u), y - 2, s, lit ? P_SILK : grey(190));
}
static void dfn_key(int32_t u, int32_t v, int lit)   /* an upper black key at this size: its recess, its cap, its wall */
{
    const int32_t x = dt_px(u), y = dt_py(v);
    cv_rect(x, y, 19, 19, RGB(0, 0, 0));
    cv_rect(x + 2, y + 2, 15, 15, lit ? P_SILK : grey(48));
    cv_rect(x + 14, y + 2, 3, 15, lit ? P_SILK : grey(88));
}
static void dfn_digit(int32_t x, int32_t y, char c)   /* a display's digit at this size, 10 x 14, unlit segments showing */
{
    static const int8_t R_[7][4] = {{2, 0, 8, 2}, {8, 1, 2, 6}, {7, 7, 2, 6}, {0, 12, 7, 2}, {0, 7, 2, 6}, {1, 1, 2, 6}, {2, 6, 6, 2}};   /* a..g */
    const uint32_t m = seg_mask(c);
    uint32_t i, on;
    for (on = 0; on < 2u; on++)
        for (i = 0; i < 7u; i++)
            if (!on || (m >> i & 1u))
                cv_rect(x + R_[i][0], y + R_[i][1], R_[i][2], R_[i][3], on ? RGB(252, 60, 96) : RGB(72, 16, 24));
}
/* the section's frames at this size, the photo's: two boxes sharing a wall, their lines 2 px, corners of 6, smooth, so at the
 * shared wall both rails curve into it (its Y). How much of a pixel a box's line covers: a rounded box's distance */
#define DFN_R 6.0f
#define DFN_T 2.0f
static float dfn_cover(float l, float t, float r, float b, float px, float py)
{
    const float qx = fm_fabsf(px - (l + r) * 0.5f) - (r - l) * 0.5f + DFN_R, qy = fm_fabsf(py - (t + b) * 0.5f) - (b - t) * 0.5f + DFN_R;
    const float ax = fm_maxf(qx, 0.0f), ay = fm_maxf(qy, 0.0f);
    const float d = fm_sqrtf(ax * ax + ay * ay) + fm_minf(fm_maxf(qx, qy), 0.0f) - DFN_R;   /* (0 on its outside edge, < 0 within) */
    return fm_clampf(DFN_T * 0.5f + 0.5f - fm_fabsf(d + DFN_T * 0.5f), 0.0f, 1.0f);
}
static void dfn_box(int32_t x0, int32_t y0, int32_t x1, int32_t y1)   /* one box, in the panel's pixels */
{
    const int32_t R_ = (int32_t)DFN_R;
    const uint16_t line = grey(DT_LINE_G);
    int32_t a, c, x, y;
    cv_rect(x0 + R_, y0, x1 - x0 - 2 * R_, 2, line);
    cv_rect(x0 + R_, y1 - 2, x1 - x0 - 2 * R_, 2, line);
    cv_rect(x0, y0 + R_, 2, y1 - y0 - 2 * R_, line);
    cv_rect(x1 - 2, y0 + R_, 2, y1 - y0 - 2 * R_, line);
    for (a = 0; a < 2; a++)
        for (c = 0; c < 2; c++) {
            const int32_t X = a ? x1 - R_ : x0, Y = c ? y1 - R_ : y0;
            for (y = Y; y < Y + R_; y++)
                for (x = X; x < X + R_; x++) {
                    const float k = dfn_cover((float)x0, (float)y0, (float)x1, (float)y1, (float)x + 0.5f, (float)y + 0.5f);
                    cv_pset(x, y, grey(DT_FACE_G + (int32_t)((float)(DT_LINE_G - DT_FACE_G) * k)));
                }
        }
}
static void dfn_display(int32_t x, int32_t y, const char *t)   /* a display at this size: its window, its pale left edge, two digits */
{
    cv_rect(x, y, 27, 19, RGB(32, 16, 20));
    cv_rect(x, y, 1, 19, grey(112));
    dfn_digit(x + 3, y + 2, t[0]);
    dfn_digit(x + 15, y + 2, t[1]);
}
static void dfn_chain(const int32_t *w, int32_t n, int32_t y0, int32_t y1)   /* n boxes side by side, sharing walls w[0..n], in
                                              * pixels (the walls' middles and the rails' inner rows; the last may run off the screen) */
{
    const int32_t R_ = (int32_t)DFN_R;
    const uint16_t line = grey(DT_LINE_G);
    int32_t i, x, y, c;
    for (i = 0; i <= n && w[i] - R_ - 2 < 240; i++) {
        const int32_t xa = i ? w[i] - R_ - 2 : w[i] - 1, xb = i < n ? w[i] + R_ + 1 : w[i];
        if (i < n) {                         /* the rails, between the curves */
            cv_rect(w[i] - 1 + R_, y0 - 1, w[i + 1] - w[i] + 1 - 2 * R_, 2, line);
            cv_rect(w[i] - 1 + R_, y1 - 1, w[i + 1] - w[i] + 1 - 2 * R_, 2, line);
        }
        cv_rect(w[i] - 1, y0 - 1 + R_, 2, y1 - y0 - 2 * R_, line);   /* the wall */
        for (c = 0; c < 2; c++) {            /* the curves: each pixel as the more of either box covers it */
            const int32_t Y0 = c ? y1 - R_ - 1 : y0 - 1;
            for (y = Y0; y < Y0 + R_ + 2; y++)
                for (x = xa; x <= xb; x++) {
                    const float px = (float)x + 0.5f, py = (float)y + 0.5f;
                    const float a = i ? dfn_cover((float)w[i - 1] - 1.0f, (float)y0 - 1.0f, (float)w[i] + 1.0f, (float)y1 + 1.0f, px, py) : 0.0f;
                    const float b = i < n ? dfn_cover((float)w[i] - 1.0f, (float)y0 - 1.0f, (float)w[i + 1] + 1.0f, (float)y1 + 1.0f, px, py) : 0.0f;
                    const float k = fm_maxf(a, b);
                    cv_pset(x, y, grey(DT_FACE_G + (int32_t)((float)(DT_LINE_G - DT_FACE_G) * k)));
                }
        }
    }
}
static void dfn_pair(int32_t x0, int32_t xm, int32_t x1, int32_t y0, int32_t y1)
{
    const int32_t w[3] = {x0, xm, x1};
    dfn_chain(w, 2, y0, y1);
}
static void dfn_frames(int32_t u0, int32_t um, int32_t u1)   /* the two boxes, u0..um and um..u1 across */
{
    dfn_pair(dt_px(u0), dt_px(um), dt_px(u1), dt_py(57), dt_py(350));
}
static void dfn_center(int32_t ux0)          /* the plate, its two displays, its keypad */
{
    static const char PAD[] = "123456789<0>";
    char l[3], r[3];
    int32_t i;
    (void)ux0;
    cv_rect(0, 0, 240, pan_h, DT_FACE);
    dt_digits(l, r);
    dfn_display(dt_px(660), dt_py(61) + 2, l);
    dfn_display(dt_px(770), dt_py(61) + 2, r);
    for (i = 0; i < 12; i++) {               /* 1-9, then left, 0, right */
        const int32_t x = dt_px(676 + 55 * (i % 3)), y = dt_py(133 + 54 * (i / 3) + (i / 3 == 3));
        const char c = PAD[i];
        cv_rect(x, y, 15, 15, grey(184));
        cv_rect(x, y + 1, 1, 13, grey(120));
        cv_rect(x + 14, y + 1, 1, 13, grey(216));
        cv_rect(x + 1, y + 14, 14, 1, grey(232));
        cv_rect(x + 1, y + 1, 13, 13, grey(176));
        cv_rect(x + 2, y + 2, 11, 11, grey(128));
        if (c == '<' || c == '>') {
            int32_t k;
            for (k = 0; k < 4; k++)
                cv_rect(c == '<' ? x + 5 + k : x + 9 - k, y + 7 - k, 1, 2 * k + 1, P_SILK);
        } else {
            char d[2];
            d[0] = c, d[1] = 0;
            dt_tiny(x + 6, y + 5, d, P_SILK);
        }
    }
}
static void dt_crop(int32_t u0)              /* the section page's measures: u0 at the plate's left, down the height */
{
    dt_ux0 = u0, dt_x0 = 8, dt_sx = 430, dt_vy0 = 57, dt_y0 = 7, dt_sy = 500;
}
static void dt_crop_end(void) { dt_ux0 = 151, dt_x0 = HOME_L, dt_sx = 187, dt_vy0 = 56, dt_y0 = 47, dt_sy = 187; }
static void dfn_tags(void)                   /* what turns here: PRESET and ALGO on the display they show on; the rest, in a tag */
{
    const int left = ui.dt_fn >= DTF_SONG && ui.dt_fn != DTF_BVOL && ui.dt_fn != DTF_BTEMPO;
    const int32_t y = dt_py(61) + 11, xl = dt_px(660) + 13, xr = dt_px(770) + 13;
    tag(left || dt_knobs_split() ? xl : xr, y, EN_PRESET);
    if (!ui.map_until || dt_knobs_split())
        tag(left && !dt_knobs_split() ? xl : xr, y, EN_ALGO);
    dt_tag_away(EN_SELECT, 50);
    dt_tag_away(EN_K1, 100);
    dt_tag_away(EN_K2, 150);
    dt_tag_away(EN_K3, 100);
    dt_tag_away(EN_K4, 150);
}
/* the roll's own place on the list: a plate screwed on under the print, as a modification to the machine would be
 * (plate()'s aluminium, its Dymo tape), its LED lit as the rest; SEQ goes to it after COPY / APPEND TO */
static void dt_roll_plate(void)
{
    /* (x0: its LED under the list's) */
    const int lit = ui.dt_fn == DTF_ROLL;
    const int32_t x0 = dt_px(951) - 15, x1 = x0 + 86, y0 = dt_py(262), y1 = y0 + 37;
    int32_t y;
    char t[7] = "1/", n[4];
    int_text(DQ_ROLL_N[set.droll], n);
    text_cat(t, n);
    if (DQ_ROLL_T[set.droll])
        text_cat(t, "T");
    if (lit && zoom.fn_at) {                 /* (the camera's aim, as dfn_row's) */
        zoom.tx = (int16_t)((x0 + x1) / 2);
        zoom.ty = (int16_t)((y0 + y1) / 2);
        zoom.pg = ui.page;
        zoom.fn_ok = 1;
    }
    cv_rect(x0 + 2, y0 + 2, x1 - x0, y1 - y0, grey(8));
    for (y = y0; y < y1; y++) {
        const uint32_t h = (uint32_t)y * 2654435761u >> 27;
        cv_rect(x0, y, x1 - x0, 1, grey(158 + (int32_t)(h % 16u) - (y % 3 == 0 ? 6 : 0)));
    }
    cv_rect(x0, y0, x1 - x0, 1, grey(225));
    cv_rect(x0, y0, 1, y1 - y0, grey(205));
    cv_rect(x0, y1 - 1, x1 - x0, 1, grey(90));
    cv_rect(x1 - 1, y0, 1, y1 - y0, grey(100));
    screw(x0 + 6, (y0 + y1) / 2);
    screw(x1 - 7, (y0 + y1) / 2);
    cv_rect(x0 + 13, (y0 + y1) / 2 - 1, 5, 3, lit ? RGB(255, 40, 72) : RGB(96, 16, 32));
    dymo(x0 + 45, y0 + 3, "ROLL", RGB(235, 235, 235));   /* its label tape, as the mods' */
    dymo(x0 + 45, y0 + 19, t, lit ? P_CYAN : RGB(235, 235, 235));
}
static void dpat_draw(void)
{
    static const struct { int16_t led, text, v; uint8_t f; const char *s; } L[9] = {
        {950, 971, 141, DTF_PAT, "PATTERN #"}, {950, 971, 155, DTF_REC, "RECORD MODE"}, {978, 1000, 168, DTF_ERASEI, "ERASE INSTRUMENT"},
        {979, 1000, 183, DTF_ERROR, "ERROR CORRECT"}, {951, 972, 196, DTF_SWING, "SWING %"}, {951, 972, 210, DTF_TIME, "TIME SIGNATURE"},
        {951, 972, 223, DTF_MEAS, "# OF MEASURES"}, {951, 972, 237, DTF_ERASEP, "ERASE PATTERN"}, {951, 972, 251, DTF_COPYP, "COPY / APPEND TO"}};
    uint32_t i;
    dt_crop(643);
    dfn_center(643);
    dfn_frames(643, 852, 1166);
    dfn_key(896, 92, 0);
    dt_tiny(dt_px(951), dt_py(111) - 2, "SELECT", grey(216));
    dt_tiny(dt_px(1006) + 2, dt_py(111) - 2, "PATTERN", grey(216));
    dt_tiny(dt_px(1071) + 3, dt_py(111) - 2, "FUNCTIONS", grey(216));
    dt_vline(951, 161, 183);
    dt_hline(951, 975, 169);
    dt_hline(951, 975, 183);
    for (i = 0; i < 9u; i++)
        dfn_row(L[i].led, L[i].text, L[i].v, ui.dt_fn == L[i].f, L[i].s);
    dt_roll_plate();
    dfn_tags();
    dt_crop_end();
}
static void dsong_draw(void)
{
    static const struct { int16_t led, text, v; uint8_t f; const char *s; } L[9] = {
        {429, 442, 141, DTF_SONG, "SONG #"}, {429, 442, 154, DTF_ERASES, "ERASE SONG"}, {429, 442, 168, DTF_COPYS, "COPY / APPEND TO"},
        {429, 442, 182, DTF_BUILD, "BUILD MODE"}, {457, 473, 196, DTF_BPAT, "PATTERN #"}, {457, 473, 209, DTF_BVOL, "VOL CHANGE"},
        {457, 473, 223, DTF_BTEMPO, "TEMPO CHANGE"}, {457, 473, 236, DTF_BINS, "INSERT"}, {457, 473, 250, DTF_BDEL, "DELETE"}};
    static const int16_t TREE[5] = {197, 211, 224, 238, 251};
    uint32_t i;
    dt_crop(330);
    dfn_center(330);
    dfn_frames(330, 643, 852);
    dfn_key(373, 92, 0);
    dfn_key(453, 263, 0);
    dt_tiny(dt_px(424), dt_py(111) - 2, "SELECT", grey(216));
    dt_tiny(dt_px(476) + 6, dt_py(111) - 2, "SONG", grey(216));
    dt_tiny(dt_px(519) + 5, dt_py(111) - 2, "FUNCTIONS", grey(216));
    dt_tiny(dt_px(507), dt_py(284) - 2, "ENTER", grey(216));
    dt_vline(429, 188, 251);
    for (i = 0; i < 5u; i++)
        dt_hline(429, 452, TREE[i]);
    for (i = 0; i < 9u; i++)
        dfn_row(L[i].led, L[i].text, L[i].v, ui.dt_fn == L[i].f || (L[i].f == DTF_BUILD && ui.dt_fn >= DTF_BPAT), L[i].s);
    dfn_tags();
    dt_crop_end();
}
/* a Channel page: the faceplate's lower row, closer: PER INSTRUMENT, the channel's frame beside it, at one scale for all six so
 * VOLUME and TUNING stay put from page to page, the two displays over them; its keys the FM-1's piano keys', lit as they are;
 * the drum selected, its LED lit */
#define DCH_ACTIVE_G 64                      /* the active channel's plate (the rest: DT_FACE_G) */
#define DT_DROP_HOLD_MS 1000u                /* a knob's last turn till the header goes back up (they're endless: no letting go to see) */
#define DT_DROP_MS 150u                      /* its way, either way */
static const uint8_t DCH_AWAY[5] = {EN_PRESET, EN_ALGO, EN_SELECT, EN_K1, EN_K2};   /* K3, K4: on the page */
static int dt_drop_step(uint32_t now)        /* a frame: 1 when the header moved */
{
    const uint32_t was = ui.dt_drop, dt = now - ui.dt_drop_at;
    int32_t at = (int32_t)was, step = (int32_t)(dt * 45u / DT_DROP_MS);
    uint32_t i;
    int on = 0;
    for (i = 0; DCH_PAGE(ui.page) && i < sizeof DCH_AWAY; i++)
        on |= ui.turned_at[DCH_AWAY[i]] && now - ui.turned_at[DCH_AWAY[i]] < DT_DROP_HOLD_MS;
    if (!DCH_PAGE(ui.page)) {
        ui.dt_drop = 0;
        return 0;
    }
    if (step < 1 && !(on ? was < 45u : was > 0u))
        return 0;
    if (step < 1)                            /* (too soon for a row: the next frame's) */
        return 1;
    ui.dt_drop_at = now;
    at = on ? at + step : at - step;
    ui.dt_drop = (uint8_t)clamp(at, 0, 45);
    return ui.dt_drop != was;
}
static int32_t dch_width(uint32_t c)         /* a channel's frame: 31 a key */
{
    int32_t i, n = 0;
    for (i = 0; i < (int32_t)NDT; i++)
        n += DT_CH[i] == c;
    return 31 * n + 5;
}
static void dch_draw(void)
{
    static const uint8_t BK[11] = PLAT_BLACK_KEYS;
    const uint32_t c = ui.page - PG_DCH1, lit = dt_lit();
    const uint16_t print = grey(216);
    char t[5] = "CH 1";
    int32_t i, k, x0, X, j = 0, top, low, ky, ly, tk;
    /* the faceplate's lower row, one scale both ways, from where HOME's camera lands (dt_open): the cut unseen */
    dt_ux0 = 151, dt_x0 = 9, dt_sx = 430, dt_vy0 = 409, dt_sy = 560;
    dt_y0 = 15 + (zoom.settle ? 13 * zoom.z / ZOOM_SETTLE_Z : 0);   /* (taller, the header's rows its own, centred on the plate; as the camera lands, from where HOME's row is) */
    top = dt_py(409), low = dt_py(642), ky = dt_py(540), ly = dt_py(605);
    cv_rect(0, 0, 240, pan_h, DT_FACE);  /* the plate alone, its left cheek: PER INSTRUMENT's end of the faceplate */
    dt_cheek(0, 0, 0, 5);
    dfn_box(dt_px(151), top, 105, low);
    dt_knob_at(33.0f, (float)top + 20.0f, 12.0f, (float)dt.vol[ui.dt_sel] / 15.0f, 150, 100, 1.0f, 1, 0);
    dt_knob_at(81.0f, (float)top + 20.0f, 12.0f, (float)dt.tune[ui.dt_sel] / 15.0f, 150, 100, 1.0f, 1, 0);
    tk = top + 20;
    silk_text(33, top + 34, "VOLUME", print);
    silk_text(81, top + 34, "TUNING", print);
    dt_tiny(57 - 27, top + 50, "PER INSTRUMENT", grey(190));
    cv_rect(23, top + 45, 1, 8, grey(190));  /* its bracket, up to VOLUME and TUNING */
    cv_rect(23, top + 52, 5, 1, grey(190));
    cv_rect(91, top + 45, 1, 8, grey(190));
    cv_rect(87, top + 52, 5, 1, grey(190));
    tag(33, tk, EN_K3);
    tag(81, tk, EN_K4);
    cv_rect(31, ky - 6, 5, 3, dq.run && !dt_blink() ? RGB(255, 40, 72) : RGB(96, 16, 32));   /* RUN's LED */
    dch_key(19, ky, dq.run && !dt_blink());
    dch_key(67, ky, ui.drum && ui.keys >> BK[0] & 1u);
    silk_text(36, ly, "RUN/STOP", print);    /* (each a little right of its key: clear of the frame, of each other) */
    silk_text(83, ly, "ACCENT", print);
    x0 = c == 5u ? 108 : 127;                /* (the faceplate's gap after PER INSTRUMENT; CH 6, the widest: nearer, to fit) */
    X = x0 + dch_width(c);
    if (c == 5u)
        dfn_box(x0, top, X, low);
    else {                                   /* the channels after it, their own widths: on until the screen's edge cuts one */
        int32_t w[NDTCH + 1], m = 1;
        w[0] = x0 + 1, w[1] = X;
        for (k = (int32_t)c + 1; k < (int32_t)NDTCH; k++, m++)
            w[m + 1] = w[m] + dch_width((uint32_t)k);
        dfn_chain(w, m, top + 1, low - 1);
    }
    {                                        /* the active channel: its plate a shade lighter, to its frame's inner curve */
        static const uint8_t IN[4] = {3, 2, 1, 0};
        const int32_t l_ = x0 + 2, r_ = c == 5u ? X - 2 : X - 1, t_ = top + 2, b_ = low - 2;
        int32_t y;
        for (y = t_; y < b_; y++) {
            const int32_t e = y - t_ < 4 ? y - t_ : b_ - 1 - y < 4 ? b_ - 1 - y : 4;
            const int32_t k = e < 4 ? IN[e] : 0;
            cv_rect(l_ + k, y, r_ - l_ - 2 * k, 1, grey(DCH_ACTIVE_G));
        }
    }
    t[3] = (char)('1' + c);
    silk_text((x0 + X) / 2, top + 5, t, print);
    for (i = 0; i < (int32_t)NDT; i++)
        if (DT_CH[i] == c) {
            const int32_t kx = x0 + 4 + 31 * j++, sel = i == (int32_t)ui.dt_sel;
            cv_rect(kx + 12, ky - 6, 5, 3, sel ? RGB(255, 40, 72) : RGB(96, 16, 32));
            dch_key(kx, ky, (int)(lit >> i & 1u));
            silk_text(kx + 14, ly, DT_SHORT[i], sel ? P_SILK : grey(170));
        }
    for (k = (int32_t)c + 1; k < (int32_t)NDTCH && X < 240; X += dch_width((uint32_t)k), k++) {   /* the ones after, as the faceplate has them */
        t[3] = (char)('1' + k);
        j = 0;
        for (i = 0; i < (int32_t)NDT; i++)
            if (DT_CH[i] == (uint32_t)k) {
                const int32_t kx = X + 4 + 31 * j++;
                cv_rect(kx + 12, ky - 6, 5, 3, RGB(96, 16, 32));
                dch_key(kx, ky, (int)(lit >> i & 1u));
                silk_text(kx + 14, ly, DT_SHORT[i], grey(170));
            }
        silk_text(X + dch_width((uint32_t)k) / 2, top + 5, t, print);
    }
    for (i = 0; i < (int32_t)sizeof DCH_AWAY; i++)   /* (the camera stays out: the header says, dt_drop_step) */
        if (zoom.k == DCH_AWAY[i] + 1u)
            zoom.k = 0;
    if (ui.dt_drop) {                        /* a knob with none here turning: the header down over the plate, to say */
        const int32_t d = (int32_t)ui.dt_drop - 45;
        cv_oy += d;
        dt_head_top();
        cv_rect(0, 45, 240, 1, RGB(0, 0, 0));
        cv_oy -= d;
    }
    dt_crop_end();
}
/* FX again: plates with more to read than the faceplate shows */
static void dinfo_line(int32_t y, const char *a, const char *b)
{
    print_c(90, y, a, grey(36));
    print_c(160, y, b, grey(36));
}
static void dpinfo_draw(void)
{
    const dq_pat_t *p = &dqm.pat[ui.dt_pat < DQ_NPAT ? ui.dt_pat : 0];
    char t[14], n[6];
    plate("DRUM SOMETHING");
    dymo(120, 30, "PATTERN INFO", RGB(235, 235, 235));
    t[0] = 0;
    int_text(ui.dt_pat, t);
    dinfo_line(44, "PATTERN", t);
    t[0] = 0;
    int_text(p->num, n), text_cat(t, n), text_cat(t, "/"), int_text(DQ_BEAT[p->beat], n), text_cat(t, n);
    dinfo_line(55, "TIME", t);
    int_text((int32_t)(p->len / dq_bar(p)), t);
    if (p->len % dq_bar(p)) {                /* (appended in another time signature: a part of a bar, in tenths) */
        const char tenth[3] = {'.', (char)('0' + p->len % dq_bar(p) * 10u / dq_bar(p)), 0};
        text_cat(t, tenth);
    }
    dinfo_line(66, "BARS", t);
    t[0] = 0;
    text_cat(t, "1/"), int_text(DQ_EC[p->ec], n), text_cat(t, n);
    dinfo_line(77, "GRID", t);
    t[0] = 0;
    int_text(DQ_SWING[p->swing], n), text_cat(t, n), text_cat(t, "%");
    dinfo_line(88, "SWING", t);
    int_text(p->n, t);
    dinfo_line(99, "HITS", t);
    t[0] = 0;
    int_text((int32_t)dt_mem_free(), n), text_cat(t, n), text_cat(t, "%");
    dinfo_line(110, "MEMORY FREE", t);
}
static void dsinfo_draw(void)
{
    const uint32_t song = dq.run && dq.in_song ? dq.song : ui.dt_song < DS_NSONG ? ui.dt_song : 0u;
    const ds_song_t *t = &dsm.s[song];
    const int32_t at = dq.run && dq.in_song ? (dq.sstep ? dq.sstep - 1 : 0) : ui.dt_fn >= DTF_BPAT ? ui.dt_step : 0;
    const int32_t from = at > 2 ? at - 2 : 0;
    char w[8], n[6];
    int32_t i;
    plate("DRUM SOMETHING");
    dymo(120, 30, "SONG INFO", RGB(235, 235, 235));
    w[0] = 0;
    text_cat(w, "SONG ");
    int_text((int32_t)song, n);
    text_cat(w, n);
    print_c(120, 48, w, grey(36));
    for (i = 0; i < 6; i++) {                /* the steps round the one it's on: each its number, what's there */
        const int32_t st = from + i, x = 22 + 33 * i;
        if (st < (int32_t)DS_NSTEP) {
            const uint32_t c = t->step[st];
            int_text(st, w);
            cv_rect(x, 68, 30, 44, st == at ? RGB(0, 0, 0) : grey(60));
            cv_text(x + 3, 70, &FONT_T, w, grey(190));
            w[0] = 0;
            if (c < DQ_NPAT + 1u)
                dt_two(w, c), w[2] = 0;
            else if (c == DS_END)
                text_cat(w, "END");
            else if (c == DS_EMPTY)
                text_cat(w, "--");
            else {
                text_cat(w, (c & 0xE0u) == DS_VOL ? "V" : "T");
                int_text(DS_VAL(c), n);
                text_cat(w, n);
            }
            cv_text(x + 3, 88, &FONT_XS, w, P_SILK);
            if (c == DS_END)
                break;
        }
    }
}
static void dt_box(uint32_t g)                /* page g's box on the drums' HOME: what its page shows, to its scale: the cut hardly seen */
{
    if (g >= PG_DCH1 && g <= PG_DCH6) {      /* (a Channel page: PER INSTRUMENT and that channel, the lower row) */
        zoom.hx = 56;                        /* (so HOME's lower row lands on the page's, at 2.3x: PER INSTRUMENT and CH 1 exact) */
        zoom.hy = 127;
    } else {
        zoom.hx = (int16_t)dt_px(g == PG_DSONG ? 591 : 904);
        zoom.hy = (int16_t)dt_py(203);
    }
    zoom.lc = 1.2f;
}
/* a section's button: its page, the camera going in on its box if from the drums' HOME */
static void dt_open(uint32_t g, uint32_t k)
{
    if (set.zoom_off || ui.page != PG_DHOME || zoom.home) {
        page_go(g);
        return;
    }
    zoom_keep(PG_DHOME);
    dt_box(g);
    zoom.g = (uint8_t)g;
    zoom.bk = (uint8_t)k;
    zoom.u = 0.0f;
    zoom.home = ZH_OPEN;
    ui.redraw = 1;
}
/* HOME from another page: from a section's, its button's way in backwards: HOME up close on the section, the camera out
 * to the whole panel; a dive's, back along its path; the rest (GLO's, the drums' plates: no way in) at once */
static void home_back(void)
{
    const uint32_t g = ui.page, h = ui.drum ? PG_DHOME : PG_HOME;
    ui.turned_at[ui.last_enc] = 0;           /* (a knob just turned, a function just picked: not on HOME as it lands) */
    zoom.fn_at = 0;
    if (zoom.home == ZH_DIVE || zoom.home == ZH_PAGE || zoom.home == ZH_RISE) {
        zoom.home = ZH_RISE;
        return;
    }
    if (set.zoom_off || !(ui.drum ? DCH_PAGE(g) || g == PG_DPAT || g == PG_DSONG : PAGE_SEC[g] != SEC_GLO)) {
        page_go(h);
        return;
    }
    if (ui.drum)
        dt_box(g);
    else
        home_sect(g, &zoom.hx, &zoom.hy, &zoom.lc);
    zoom_keep(h);
    page_go(h);
    zoom.u = 1.0f;
    zoom.home = ZH_CLOSE;
    ui.map_until = 0;                        /* (its controls' map: as it lands) */
}
/* DRUM MODS: what the 400 hasn't, on a plate as the synth's mods (one so far) */
static void dmod_draw(void)
{
    plate("DRUM SOMETHING");
    dymo(120, 30, "DRUM MODS", RGB(235, 235, 235));
    bs2_knob(70, 76, 20, set.dswing_lock != 0u, 1, KF_PLATE | (knob_lit(EN_K3) ? KF_LIT : 0u));
    tag(70, 76, EN_K3);
    dymo(70, 101, "SWING LOCK", RGB(235, 235, 235));
    print_c(70, 117, OFF_ON[set.dswing_lock != 0u], grey(36));
    bs2_knob(170, 76, 20, !set.dtrans_off, 1, KF_PLATE | (knob_lit(EN_K4) ? KF_LIT : 0u));
    tag(170, 76, EN_K4);
    dymo(170, 101, "TRANSPORT", RGB(235, 235, 235));
    print_c(170, 117, OFF_ON[!set.dtrans_off], grey(36));
}
static void zoom_dt_cc(uint32_t cc)          /* a drum's CC (dt_cc): its channel's page, its knob */
{
    const uint32_t vol = cc >= DT_CC_VOL, d = cc - (vol ? DT_CC_VOL : DT_CC_TUNE), c = DT_CH[d], g = PG_DCH1 + c;
    uint8_t v[5];
    uint32_t i;
    zoom_keep(PG_DHOME);
    for (i = 0; i < 5u; i++)
        v[i] = zoom.sv[i];
    if (g != zoom.g || !zoom.home) {
        const int32_t x = dt_px(DT_CH_U[c]), X = dt_px(DT_CH_U[c + 1u]), h = dt_py(642) - dt_py(409);
        zoom.hx = (int16_t)((x + X) / 2);
        zoom.hy = (int16_t)(dt_py(409) + h / 2);
        zoom.lc = fm_log2f(fm_minf(fm_minf(240.0f / (float)(X - x), (float)TALL_H / (float)h), 6.0f));
    }
    zoom.dnew = (uint8_t)d;
    zoom_go(g, vol ? EN_K3 : EN_K4, v);
}
static void dhome_draw(void)
{
    static const int16_t FRAME[13][5] = {                   /* (the last: its corners' wider arc) */
        {166, 57, 318, 205, 1}, {166, 205, 318, 350, 1}, {330, 57, 643, 350, 0}, {643, 57, 852, 350, 0},
        {852, 56, 1166, 350, 0}, {1180, 56, 1348, 350, 1},  /* cassette, tempo, song, display, pattern, volumes */
        {151, 409, 364, 642, 0}, {415, 409, 487, 642, 0}, {487, 409, 630, 642, 0}, {630, 409, 773, 642, 0},
        {773, 409, 916, 642, 0}, {916, 409, 1059, 642, 0}, {1059, 409, 1348, 642, 0}};   /* per instrument, CH1 .. CH6 */
    static const int16_t UKEY[7][2] = {{183, 119}, {255, 119}, {183, 263}, {255, 263}, {373, 92}, {453, 263}, {896, 92}};
    static const int16_t LED[21][2] = {
        {204, 107}, {276, 108}, {195, 519},                 /* cassette's, per instrument's */
        {429, 141}, {429, 154}, {429, 168}, {429, 182}, {457, 195}, {457, 209}, {457, 223}, {457, 236}, {457, 250},
        {950, 141}, {950, 155}, {978, 168}, {979, 183}, {951, 196}, {951, 210}, {951, 223}, {951, 237}, {951, 250}};
    static const int16_t INST[NDT] = {422, 495, 566, 638, 709, 781, 852, 924, 995, 1067, 1138, 1211, 1283};
    static const int16_t TREE[5] = {197, 211, 224, 238, 251};   /* BUILD MODE's lines */
    static const char PAD[] = "123456789<0>";
    static const uint8_t BK[11] = PLAT_BLACK_KEYS;
    const uint32_t lit = dt_lit(), fl = ui.dt_fn < DTF_SONG ? 12u + ui.dt_fn : 3u + (ui.dt_fn - DTF_SONG);
    char l[3], r[3];
    int32_t i;
    dt_digits(l, r);
    dt_head(1);                                  /* the name, the two displays; under them the plate */
    for (i = 0; i < 13; i++)
        dt_frame(FRAME[i][0], FRAME[i][1], FRAME[i][2], FRAME[i][3], FRAME[i][4]);
    dt_vline(429, 188, 285);                     /* BUILD MODE's and RECORD MODE's lines */
    for (i = 0; i < 5; i++)
        dt_hline(429, 452, TREE[i]);
    dt_hline(429, 452, 285);
    dt_vline(951, 161, 183);
    dt_hline(951, 975, 169);
    dt_hline(951, 975, 183);
    dt_vline(195, 487, 498);                     /* PER INSTRUMENT's, CYMBAL's, HI HAT's brackets */
    dt_hline(195, 205, 498);
    dt_vline(321, 487, 498);
    dt_hline(314, 321, 498);
    dt_vline(809, 608, 621);
    dt_hline(809, 819, 621);
    dt_vline(879, 610, 621);
    dt_hline(873, 879, 621);
    dt_vline(950, 606, 620);
    dt_hline(950, 965, 620);
    dt_vline(1024, 606, 620);
    dt_hline(1012, 1024, 620);
    dt_print();
    dt_logo();
    for (i = 0; i < 7; i++)
        dt_ukey(UKEY[i][0], UKEY[i][1]);
    for (i = 0; i < 21; i++)                     /* (the function picked, BUILD MODE's own over it; RUN's) */
        dt_led(LED[i][0], LED[i][1], (uint32_t)i == fl || (i == 6 && ui.dt_fn >= DTF_BPAT) || (i == 2 && dq.run && !dt_blink()));
    for (i = 0; i < 12; i++)                     /* the keypad: 1-9, then left, 0, right */
        dt_gkey(676 + 55 * (i % 3), 133 + 54 * (i / 3) + (i / 3 == 3), PAD[i]);
    dt_small(660, l);
    dt_small(770, r);
    for (i = 0; i < 11; i++) {                   /* the volumes' scales: a dot for each number, 0..10 */
        int32_t x, y;
        pt_at(dt_px(1276), dt_py(120), 8, ((float)i / 10.0f - 0.5f) * (FM_PI * 5.0f / 3.0f), &x, &y);
        cv_pset(x, y, grey(200));
        pt_at(dt_px(1276), dt_py(260), 8, ((float)i / 10.0f - 0.5f) * (FM_PI * 5.0f / 3.0f), &x, &y);
        cv_pset(x, y, grey(200));
    }
    dt_knob(1276, 120, dt.metro * 10 / 127, 178, 78, 0.5f, -2, 1);   /* METRONOME, MASTER; VOLUME, TUNING (lit from their
                                                                      * other side), the drum selected's */
    dt_knob(1276, 260, dt.master * 10 / 127, 178, 78, 0.5f, -2, 1);
    dt_knob(180, 453, dt.vol[ui.dt_sel] * 10 / 15, 150, 100, 1.0f, 1, 0);
    dt_knob(309, 453, dt.tune[ui.dt_sel] * 10 / 15, 150, 100, 1.0f, 1, 0);
    dt_bkey(168, 530, dq.run && !dt_blink());                 /* RUN/STOP, ACCENT (its key held) */
    dt_bkey(289, 530, ui.drum && ui.keys >> BK[0] & 1u);
    for (i = 0; i < (int32_t)NDT; i++)
        dt_bkey(INST[i], 530, (int)(lit >> i & 1u));
    tag(dt_px(1276), dt_py(120), EN_K1);          /* (the encoders on the panel, Knob Zoom's aims) */
    tag(dt_px(1276), dt_py(260), EN_K2);
    tag(dt_px(180), dt_py(453), EN_K3);
    tag(dt_px(309), dt_py(453), EN_K4);
    tag(dt_px(242), dt_py(278), EN_SELECT);       /* TEMPO's box */
    dt_tag_preset();
}

static void head_draw(void)                  /* the section tabs (each page's panel under them) */
{
    int32_t x;
    uint32_t i;
    cv_begin(240, 20, P_BG);
    for (i = 0; i < NSEC; i++) {
        int on = PAGE_SEC[ui.page] == i, run = !ui.drum && ((i == SEC_ARP && patch.arp_on) || (i == SEC_SEQ && (seq_playing() || seq_rec)));
        int32_t w = TAB_X(i + 1) - TAB_X(i) - 1;
        x = TAB_X(i);
        cv_rect(x, 1, w, 17, on ? P_BAR : P_CARD);   /* (the one you're in: the bars' cyan, its name white) */
        cv_text(x + (w - text_w(&FONT_XS, SEC_NAME[i])) / 2, 2, &FONT_XS, SEC_NAME[i],
                on ? P_SILK : run ? P_CYAN : P_LABEL);
    }
    cv_blit(0, 0);
}

/* the randomizer at work, RFX_MS over the panel, on top of everything on it (HOME's scope waits: app_tick): a card, its four knobs whirling and stopping one after another at
 * random, a strip of label tape whose letters tumble until each lands on RANDOMIZE (cyan once they all have) */
#define RFX_MS 1000u
static uint32_t rfx_hash(uint32_t a, uint32_t b) { return ((a ^ b * 0x9E3779B9u) * 2654435761u) >> 8; }
static void rfx_draw(void)
{
    static const char W[] = "RANDOMIZE";
    const uint32_t t = plat_ms() - rfx_at;
    const int32_t w = 156, h = 76, x = 120 - w / 2, y = 80 - h / 2;   /* (the same place on every page: the screen's
                                                                        * y 100) */
    char s[sizeof W];
    uint32_t i;
    if (!rfx_at || t >= RFX_MS)
        return;
    cv_rect(x - 1, y - 1, w + 2, h + 2, P_CYAN);
    cv_rect(x, y, w, h, RGB(16, 20, 24));
    for (i = 0; i < 4u; i++) {               /* each knob swings to and fro, fast, till its turn to stop */
        const uint32_t ph = (t * (5u + i) / 4u + i * 37u) % 128u;
        const int stop = t >= 280u + 130u * i;
        bs2_knob(x + 30 + 32 * (int32_t)i, y + 28, 12, stop ? (int32_t)(rfx_hash(rfx_seed, i) % 64u)
                 : (int32_t)(ph < 64u ? ph : 127u - ph), 63, stop ? 0u : KF_LIT);
    }
    for (i = 0; i + 1u < sizeof W; i++)      /* the letters: at random, 25 times a second, till each lands */
        s[i] = t >= 300u + 60u * i ? W[i] : (char)('A' + rfx_hash(t / 40u, i) % 26u);
    s[i] = 0;
    dymo(120, y + 54, s, t >= 300u + 60u * (sizeof W - 2u) ? P_CYAN : RGB(235, 235, 235));
}

/* the camera between the two HOMEs: band i of the panel, its rows s down the synth's panel with the drums' under it */
#define MPAN_MS 450u
static void mpan_band(uint32_t i)
{
    const uint32_t page = ui.page, drum = ui.drum;
    const float u = fm_clampf((float)(plat_ms() - ui.mpan_at) / (float)MPAN_MS, 0.0f, 1.0f), e = u * u * (3.0f - 2.0f * u);
    const int32_t s = (int32_t)((float)TALL_H * (drum ? e : 1.0f - e) + 0.5f), top = (int32_t)i * (TALL_H / 2) + s;
    if (top < TALL_H) {
        ui.page = PG_HOME, ui.drum = 0;
        cv_oy = -top;
        home_draw();
    }
    if (top + TALL_H / 2 > TALL_H) {
        ui.page = PG_DHOME, ui.drum = 1;
        cv_oy = TALL_H - top;
        cv_rect(0, 0, 240, TALL_H, P_BG);
        dhome_draw();
    }
    ui.page = (uint8_t)page, ui.drum = (uint8_t)drum;
    ntags = 0;                               /* (the controls' labels: when it lands) */
}

static void panel_draw(void)                 /* the page's section panel, from under the tabs: in two halves (the
                                              * canvas holds 144 rows); through Knob Zoom's camera (zoom_blow) */
{
    uint32_t i, step = 0, x16 = 0, y16 = 0;
    pan_h = RND_PAGE(ui.page) ? RND_H : TALL_H;
    pr_font = RND_PAGE(ui.page) ? &FONT_XS : &FONT_T;
    if (zoom.z && zoom.pg == ui.page && !ui.mpan_at) {   /* (the window: around the focus, inside the panel) */
        step = (256u << 16) / (256u + (uint32_t)zoom.z);
        x16 = (uint32_t)clamp(((int32_t)zoom.x << 16) - (int32_t)(120u * step), 0, (int32_t)((240u << 16) - 240u * step));
        y16 = (uint32_t)clamp(((int32_t)zoom.y << 16) - (int32_t)(pan_h / 2u * step), 0,
                              (int32_t)((pan_h << 16) - pan_h * step));
    }
    for (i = 0; i < 2u; i++) {
        const uint32_t sy16 = y16 + i * (pan_h / 2u) * step;   /* (this half's first row in the window) */
        cv_begin(240, pan_h / 2, P_BG);
        cv_oy = step ? -(int32_t)(sy16 >> 16) : -(int32_t)i * (pan_h / 2);
        ntags = 0;
        if (ui.mpan_at)
            mpan_band(i);
        else if (ui.page == PG_FX)
            mixer_draw();
        else if (ui.page == PG_TUNE || ui.page == PG_FMOD)
            filters_draw();
        else if (ui.page == PG_AENV)
            env_draw();
        else if (ui.page == PG_MENV)
            trig_draw();
        else if (ui.page == PG_EMOD || ui.page == PG_LFO2 || ui.page == PG_XMOD || ui.page == PG_AMOD)
            mod_draw();
        else if (ui.page == PG_NAME)
            name_draw();
        else if (ui.page == PG_ARP)
            arp_draw();
        else if (ui.page == PG_SEQ)
            seq_draw();
        else if (ui.page == PG_EFX)
            effects_draw();
        else if (ui.page == PG_PATCH)
            master_draw();
        else if (ui.page == PG_HOME)
            home_draw();
        else if (ui.page == PG_DHOME)
            dhome_draw();
        else if (DCH_PAGE(ui.page))
            dch_draw();
        else if (ui.page == PG_DMOD)
            dmod_draw();
        else if (ui.page == PG_DPAT)
            dpat_draw();
        else if (ui.page == PG_DSONG)
            dsong_draw();
        else if (ui.page == PG_DPINFO)
            dpinfo_draw();
        else if (ui.page == PG_DSINFO)
            dsinfo_draw();
        else if (ui.page == PG_LFO1)
            lfos_draw();
        else if (ui.page == PG_GMIDI)
            glo_midi_draw();
        else if (ui.page == PG_GDRUM)
            glo_drum_draw();
        else if (ui.page == PG_GCLK)
            glo_clock_draw();
        else if (ui.page == PG_GAUD)
            glo_audio_draw();
        else if (ui.page == PG_GCTL)
            glo_controls_draw();
        else if (ui.page == PG_GDISP)
            glo_disp_draw();
        else if (ui.page == PG_GDUMP)
            glo_dump_draw();
        else if (ui.page == PG_GRND)
            rnd_seed_draw();
        else if (ui.page == PG_RPAT)
            rnd_patch_draw();
        else if (ui.page == PG_RTRK)
            rnd_tracks_draw();
        else
            osc_draw();
        tags_draw();
        if (step)
            zoom_blow(x16, sy16 & 0xFFFFu, step);
        cv_oy = -(int32_t)i * (pan_h / 2);   /* (over the camera's view, unzoomed) */
        rfx_draw();
        cv_blit(0, 20 + i * (pan_h / 2));
    }
    pr_font = &FONT_T;
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

static void keys_draw(void)                  /* the keys; the octave, the slot, the tempo, the volume; the battery,
                                              * Bluetooth, MIDI in, what plays */
{
    char t[24];
    uint32_t i, pct = plat_master() * 100u / 4096u;
    int32_t x;
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    /* black above white: a held key fills to its velocity (cyan), its pressure over that (white); a key whose note
     * MIDI in holds, all cyan */
    enum { KX = (240 - (16 * 14 - 1)) / 2 }; /* (the sixteen white keys in the middle: 8 px each side, as HOME_L) */
    cv_begin(240, 60, P_BG);
    for (i = 0; i < 16u; i++)
        key_cell(KX + (int32_t)i * 14, 14, 13, 14, WK[i], P_KEYW);
    for (i = 0; i < 11u; i++) {
        static const uint8_t AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};   /* the white key each one follows */
        cv_rect(KX + (int32_t)AT[i] * 14 + 7, 0, 13, 13, P_EDGE);
        key_cell(KX + (int32_t)AT[i] * 14 + 8, 0, 11, 12, BK[i], P_KEYB);
    }
    octave_text(t);
    if (ui.hint_until && ui.hint)            /* a button's hint: on the keys, its middle where black meets white (off
                                              * the panel's controls and their hints) */
        hint_pill(120, 6, ui.hint);
    cv_text(4, 31, &FONT_XS, "OCT", P_LABEL);
    cv_text(30, 31, &FONT_XS, t, P_CYAN);
    slot_text(t + 1);
    t[0] = 'P';
    x = cv_text(64, 31, &FONT_XS, t, P_TEXT);
    if (set_dirty)                           /* not saved yet */
        cv_text(x + 2, 31, &FONT_XS, "*", P_CYAN);
    int_text(arp_tempo(), t);                /* the tempo: lit while the arp or the sequencer plays */
    x = cv_text(102, 31, &FONT_XS, t, patch.arp_on || seq_playing() ? P_CYAN : P_TEXT);
    cv_text(x + 4, 31, &FONT_XS, "BPM", P_LABEL);
    if (afx_on)                              /* each key its own sound */
        cv_text(150, 31, &FONT_XS, "AFX", P_AFX);
    cv_text(178, 31, &FONT_XS, "VOL", P_LABEL);
    int_text((int32_t)pct, t);
    text_cat(t, "%");
    cv_text(204, 31, &FONT_XS, t, P_CYAN);
    battery_draw(5, 49);
    x = 30;
    if (FM1B_BLE)                            /* Bluetooth: grey while it waits, lit while a computer is connected */
        x = cv_text(x, 45, &FONT_XS, "BLE", plat_ble_connected() ? P_CYAN : P_EDGE) + 6;
    if (FM1B_BLE_CENTRAL)                    /* a Bluetooth MIDI device: lit while connected */
        cv_text(x, 45, &FONT_XS, "DEV", plat_ble_device() == BLE_DEV_READY ? P_CYAN : P_EDGE);
    if (midi_count) {                        /* the last MIDI message in, and where from */
        char h[9];
        const char *port = midi_in_text(h);
        cv_text(82, 45, &FONT_XS, port, P_CYAN);
        cv_text(106, 45, &FONT_XS, h, P_TEXT);
    }
    t[0] = 0;                                /* what plays the keys, at the right: recording (its steps so far), the
                                              * sequencer, the arp */
    text_cat(t, ui.drum ? "DRUMS" : seq_rec ? "SEQ REC " : seq_playing() ? "SEQ PLAY"
                : patch.arp_on ? "ARP " : "ARP OFF");
    if (seq_rec && !ui.drum) {               /* (Drum Something's mode: its name alone, till its sequencer) */
        int_text(ui.rec_n, t + 8);
        text_cat(t, "/32");
    } else if (patch.arp_on && !seq_playing() && !ui.drum)
        text_cat(t, ARP_NAME[patch.arp_mode]);
    cv_text(236 - text_w(&FONT_XS, t), 45, &FONT_XS, t, seq_rec || seq_playing() || patch.arp_on ? P_CYAN : P_LABEL);
    cv_blit(0, 180);
}

static void draw(void)
{
    head_draw();
    panel_draw();
    if (!RND_PAGE(ui.page))                  /* (the RND pages': the whole screen under the tabs) */
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
        return (eng.src == SRC_ARP || eng.src == SRC_SEQ ? eng.rpos % RHY_STEPS + 1u : 0u)
               | (uint32_t)ext_clock() << 7 | (uint32_t)arp_tempo() << 8;   /* (TEMPO: MIDI clock's as it moves) */
    case PG_SEQ:
        return eng.src == SRC_SEQ ? eng.rpos + 1u : 0u;
    case PG_DHOME:                           /* (the keys lit, a moment after each hit; ACCENT) */
    case PG_DCH1:
    case PG_DCH2:
    case PG_DCH3:
    case PG_DCH4:
    case PG_DCH5:
    case PG_DCH6:
    case PG_DMOD:
    case PG_DPAT:
    case PG_DSONG:
    case PG_DPINFO:
    case PG_DSINFO:
        return dt_lit() | (ui.keys >> 1 & 1u) << 31 | (uint32_t)dt_blink() << 30 | (uint32_t)(dq.run != 0u) << 29
               | (uint32_t)(dq.full && plat_ms() / 300u % 2u) << 28;
    case PG_GMIDI:
    case PG_GCLK:
        return (uint32_t)ext_clock() | (uint32_t)(eng.ext_age < EXT_HOLD) << 1 | (uint32_t)plat_ble_connected() << 2
               | (uint32_t)eng.ext_shown << 3;
    default:
        return 0;
    }
}

#ifndef PLAT_HOST                             /* the platform's screens (device/sys_text.c): the app's fonts, so FONT_S stays out */
static const felucca_font_t *app_sys_face(int face) { return face == SYS_BIG ? &APP_BIG_FONT : face == SYS_B ? &FONT_B : &FONT_XS; }
static int32_t app_sys_h(int face) { return app_sys_face(face)->h; }
static int32_t app_sys_w(int face, const char *s) { return text_w(app_sys_face(face), s); }
static void app_sys_text(int32_t x, int32_t y, int face, const char *s, uint16_t c) { cv_text(x, y, app_sys_face(face), s, c); }
#endif
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

/* the RND Synth Companion's splash, the whole screen: RND SYNTH, the RND Synth beside the FM-1 (rnd/splash.h:
 * bs2/rnd/make_splash.py, 256 colours), COMPANION, in the cyan, the three together in the middle */
#include "rnd/splash.h"
static void lz4_unpack(const uint8_t *s, uint8_t *d, uint32_t n)   /* an LZ4 block, n bytes out (ours: rnd/make_splash.py) */
{
    const uint8_t *const end = d + n;
    while (d < end) {
        const uint32_t t = *s++;
        uint32_t k = t >> 4, o;
        if (k == 15u)
            do
                k += *s;
            while (*s++ == 255u);
        while (k--)
            *d++ = *s++;
        if (d >= end)
            break;
        o = (uint32_t)s[0] | (uint32_t)s[1] << 8;
        s += 2;
        k = t & 15u;
        if (k == 15u)
            do
                k += *s;
            while (*s++ == 255u);
        for (k += 4u; k; k--, d++)
            *d = d[-(int32_t)o];
    }
}
static void rsplash_line(uint32_t y, const char *s, uint16_t c)   /* a line of the big print, centred */
{
    cv_begin(240, FONT_L.h, S_BG);
    cv_text((240 - text_w(&FONT_L, s)) / 2, 0, &FONT_L, s, c);
    cv_blit(0, y);
}
static void rsplash_draw(void)
{
    enum { GAP = 10, DX = 6 };               /* (DX: the devices right of the middle, as the FM-1's screen shows it: its
                                              * leftmost columns under the bezel) */
    const uint32_t y = (240u - RND_SPLASH_H - 2u * (FONT_L.h + GAP)) / 2u;
    uint32_t i;
    lcd_fill(0, 0, 240, 240, S_BG);
    rsplash_line(y, "RND SYNTH", P_CYAN);
    cv_begin(RND_SPLASH_W, RND_SPLASH_H, S_BG);
    {                                        /* unpacked into the canvas's own far end, then each pixel through the palette
                                              * from its start, as the panel takes it: each read before its place is written */
        const uint8_t *const b = (const uint8_t *)cv_px + 2u * RND_SPLASH_W * RND_SPLASH_H - RND_SPLASH_BYTES;
        lz4_unpack(RND_SPLASH, (uint8_t *)b, RND_SPLASH_BYTES);
        for (i = 0; i < RND_SPLASH_W * RND_SPLASH_H; i++) {
            const uint32_t v = b[i >> 1];
            cv_px[i] = RND_SPLASH_PAL[i & 1u ? v & 15u : v >> 4];
        }
    }
    cv_blit((240 - RND_SPLASH_W) / 2 + DX, y + FONT_L.h + GAP);
    rsplash_line(y + FONT_L.h + 2u * GAP + RND_SPLASH_H, "COMPANION", P_CYAN);
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
    web_splash_at = ms_stamp();
#endif
}

void app_frame(void)
{
    static uint32_t last_pct, lit_until, last_ble, last_midi, last_live, last_foot, scope_at, rfx_frame;
    static uint8_t splash_shown;             /* the RND splash drawn, while it's up */
    static int scope_was;
    static int32_t last_note = -1;
    uint32_t i, now = plat_ms(), pct = plat_master() * 100u / 4096u;
    int moved;
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
    now = plat_ms();                         /* (what input just stamped: not after now) */
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
    i = (uint32_t)arp_tempo() | (uint32_t)patch.arp_on << 16 | (uint32_t)patch.arp_mode << 17
        | (uint32_t)seq_playing() << 21 | (uint32_t)seq_rec << 22 | (uint32_t)afx_on << 23;
    if (i != last_foot) {                    /* the footer's tempo, what plays (MIDI clock's tempo as it moves) */
        last_foot = i;
        ui.redraw = 1;
    }
    if (ui.map_until && now >= ui.map_until) {   /* the map's moment over */
        ui.map_until = 0;
        ui.redraw = 1;
    }
    if (ui.dt_show && now >= ui.dt_until) {  /* the drums' header's moment */
        ui.dt_show = DTS_NONE;
        ui.redraw = 1;
    }
    if (ui.hint_until && now >= ui.hint_until) {   /* a button's hint's */
        ui.hint_until = 0;
        ui.redraw = 1;
    }
    if (ui.val_until && now >= ui.val_until) {   /* a knob's value's */
        ui.val_until = 0;
        ui.redraw = 1;
    }
    if (pct != last_pct || ble_batt() != last_ble || (ui.msg_until && now >= ui.msg_until)) {
        last_pct = pct;
        last_ble = ble_batt();
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (rsplash_until && (int32_t)(now - rsplash_until) >= 0) {   /* the RND splash over: the page */
        rsplash_until = 0;
        ui.redraw = 1;
    }
    if (rsplash_until) {                     /* (the RND splash up: the screen its own, drawn once as it comes, the
                                              * redraws asked meanwhile left: each a flash) */
        if (!splash_shown) {
            splash_shown = 1;
            rsplash_draws++;
            rsplash_draw();
        }
        ui.redraw = 0;
        return;
    }
    splash_shown = 0;
    if (cpu_meter(now) && ui.page >= PG_GMIDI && !DRUM_PAGE(ui.page) && !ui.redraw)   /* (the panel alone: its load in the bar) */
        panel_draw();
    {
        const uint32_t hit = dt_flash_take();   /* the drums just hit: their keys lit a moment (live) */
        for (i = 0; i < NDT; i++)
            if (hit >> i & 1u)
                ui.dt_at[i] = now | 1u;
    }
    i = live();
    moved = zoom_step(now);
    moved |= dt_drop_step(now);
    if (ui.mpan_at) {                        /* (the camera's pan: its next frame, else landed; a dive takes over) */
        moved = 1;
        if (now - ui.mpan_at >= MPAN_MS || zoom.z || zoom.home) {
            ui.mpan_at = 0;
            ui.map_until = pop_until();
            ui.redraw = 1;
        }
    }
    if (rfx_at && now - rfx_at >= RFX_MS) {  /* the randomizer's moment: over, else its next frame (30 a second) */
        rfx_at = 0;
        moved = 1;
    } else if (rfx_at && now - rfx_frame >= 33u) {
        rfx_frame = now;
        moved = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    } else if (i != last_live || moved) {    /* the picture moved, or the camera: its panel alone */
        panel_draw();
    }
    last_live = i;
    if (ui.page == PG_HOME && !zoom.z && !rfx_at && !ui.mpan_at && now - scope_at >= 33u && (scope_live() || scope_was)) {
        scope_at = now;                      /* (HOME's scope: 30 times a second while it sounds, once more after) */
        scope_was = scope_live();
        scope_frame();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "hints"))
        *got = set.hints;
    else if (!strcmp(what, "xruns"))         /* the count GLO's pages show */
        *got = (int)xruns_shown();
    else if (!strcmp(what, "spk"))
        *got = set.spk;
    else if (!strcmp(what, "zoom_off"))
        *got = set.zoom_off;
    else if (!strcmp(what, "mpan"))          /* the camera panning between the two HOMEs */
        *got = ui.mpan_at != 0u;
    else if (!strcmp(what, "rsplash"))       /* the RND Synth Companion's splash up */
        *got = rsplash_until != 0u;
    else if (!strcmp(what, "rsplash_draws"))  /* the times it has been drawn (once each time it's up: no flashes) */
        *got = (int)rsplash_draws;
    else if (!strcmp(what, "rfx"))           /* the randomizer's moment on screen */
        *got = rfx_at != 0u;
    else if (!strcmp(what, "zoom"))          /* Knob Zoom's scale over 1x (Q8: 256 2x) */
        *got = zoom.z;
    else if (!strcmp(what, "pop"))
        *got = (int)pop_d();
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
    else if (!strcmp(what, "env_sel"))       /* the ENVELOPES' Env Select (0 amp, 1 mod, 2 both) */
        *got = ui.env_sel;
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
    else if (!strcmp(what, "drum"))          /* Drum Something's mode (HOME held) */
        *got = ui.drum;
    else if (!strcmp(what, "dlit"))          /* the drum HOME's instruments lit (dt_lit), a bit each */
        *got = (int)dt_lit();
    else if (!strcmp(what, "hushed"))        /* quiet long enough for a flash write */
        *got = hushed;
    else if (!strcmp(what, "dt_step"))       /* BUILD MODE's step; a song running, its next step, its pattern */
        *got = ui.dt_step;
    else if (!strcmp(what, "dt_slen"))
        *got = (int)ds_end(&dsm.s[ui.dt_song < DS_NSONG ? ui.dt_song : 0]);
    else if (!strcmp(what, "dt_sstep"))
        *got = dq.sstep;
    else if (!strcmp(what, "dt_insong"))
        *got = dq.in_song;
    else if (!strcmp(what, "dt_svol"))
        *got = dq.svol;
    else if (!strcmp(what, "dtrans_off"))    /* TRANSPORT off; the bass's arp on; its engine running */
        *got = set.dtrans_off;
    else if (!strcmp(what, "arp_on"))
        *got = patch.arp_on;
    else if (!strcmp(what, "bass_src"))
        *got = (int)eng.src;
    else if (!strcmp(what, "rec_light"))
        *got = dt_rec_light();
    else if (!strcmp(what, "dt_tempo"))      /* the drums' tempo; Swing Lock's setting, its first 8th (x100); the bass's 16th */
        *got = dt.tempo;
    else if (!strcmp(what, "dt_drop"))
        *got = ui.dt_drop;
    else if (!strcmp(what, "droll"))
        *got = set.droll;
    else if (!strcmp(what, "dswing_lock"))
        *got = set.dswing_lock;
    else if (!strcmp(what, "dt_lock"))
        *got = (int)(dt_lock_h * 100.0f + 0.5f);
    else if (!strcmp(what, "rpos"))
        *got = (int)eng.rpos;
    else if (!strcmp(what, "bass_on_drums")) {   /* the bass's place against the running pattern's, on its swing's curve: 1 if
                                              * within half a tick */
        const dq_pat_t *p = &dqm.pat[dq.cur];
        const float h = dt_lock_h > 0.0f ? dt_lock_h : 12.0f, b[4] = {0.0f, h * 0.5f, h, h + (24.0f - h) * 0.5f};
        const float at = b[eng.rpos % 4u] + eng.tick, pos = dt_pos - (float)((uint32_t)dt_pos / 24u * 24u);
        const float d = fm_fabsf(at - pos);
        (void)p;
        *got = d < 0.5f || d > 23.5f;
    }
    else if (!strcmp(what, "dt_sel"))        /* the drum selected; what K1-K4, PRESET, SEQ, ARP, PLAY and REC set */
        *got = ui.dt_sel;
    else if (!strcmp(what, "dt_vol"))
        *got = dt.vol[ui.dt_sel];
    else if (!strcmp(what, "dt_tune"))
        *got = dt.tune[ui.dt_sel];
    else if (!strcmp(what, "dt_metro"))
        *got = dt.metro;
    else if (!strcmp(what, "dt_master"))
        *got = dt.master;
    else if (!strcmp(what, "dt_fn"))
        *got = ui.dt_fn;
    else if (!strcmp(what, "dt_run"))
        *got = dq.run;
    else if (!strcmp(what, "dt_rec"))
        *got = dq.rec;
    else if (!strcmp(what, "dt_n"))          /* the pattern shown's hits, its length in ticks; the pool's; the beats */
        *got = dqm.pat[ui.dt_pat].n;
    else if (!strcmp(what, "dt_len"))
        *got = (int)dqm.pat[ui.dt_pat].len;
    else if (!strcmp(what, "dt_left")) {     /* the left display's number (-1: blank or letters) */
        char l[3], r[3];
        dt_digits(l, r);
        *got = l[0] >= '0' && l[0] <= '9' && l[1] >= '0' && l[1] <= '9' ? (l[0] - '0') * 10 + l[1] - '0' : -1;
    } else if (!strcmp(what, "dt_used"))
        *got = dqm.used;
    else if (!strcmp(what, "dt_clicks"))
        *got = (int)dq_clicks;
    else if (!strcmp(what, "dt_v0"))         /* its first hit's programmed volume (0: not programmed) */
        *got = dqm.pat[ui.dt_pat].n ? (int)DQ_V(dqm.ev[dqm.pat[ui.dt_pat].start]) : -1;
    else if (!strcmp(what, "dt_t0"))         /* the pattern shown's first hit's time */
        *got = dqm.pat[ui.dt_pat].n ? (int)DQ_T(dqm.ev[dqm.pat[ui.dt_pat].start]) : -1;
    else if (!strcmp(what, "dt_cur"))
        *got = dq.cur;
    else if (!strcmp(what, "dt_num"))
        *got = dqm.pat[ui.dt_pat].num;
    else if (!strcmp(what, "dt_meas"))
        *got = (int)dq_bars(&dqm.pat[ui.dt_pat]);
    else if (!strcmp(what, "dt_prog"))       /* the drums programmed; a hit that found no room's FULL */
        *got = dqm.pat[ui.dt_pat].prog;
    else if (!strcmp(what, "dt_full"))
        *got = dq.full;
    else if (!strcmp(what, "dt_ec"))
        *got = dqm.pat[ui.dt_pat].ec;
    else if (!strcmp(what, "dt_swing"))
        *got = dqm.pat[ui.dt_pat].swing;
    else if (!strcmp(what, "dt_pat"))
        *got = ui.dt_pat;
    else if (!strcmp(what, "dt_song"))
        *got = ui.dt_song;
    else if (!strcmp(what, "dt_mid"))
        *got = (int)ui.dt_mid;
    else if (!strcmp(what, "dt_hits"))       /* the drums' hits played so far */
        *got = dt_hits;
    else if (!strcmp(what, "dchan"))         /* GLO DRUMS: the drums' channel, their CC channel */
        *got = set.dchan;
    else if (!strcmp(what, "dcc_chan"))
        *got = set.dcc_chan;
    else if (!strncmp(what, "dtune", 5) || !strncmp(what, "dvol", 4))   /* dtuneN, dvolN: drum N's (DT_*) */
        *got = what[1] == 't' ? dt.tune[atoi(what + 5) % NDT] : dt.vol[atoi(what + 4) % NDT];
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
    else if (!strcmp(what, "rec_n"))         /* the steps recorded since REC (the bottom line's SEQ REC n/32) */
        *got = ui.rec_n;
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
    else if (!strcmp(what, "ext_bpm"))       /* MIDI clock's tempo as GLO CLOCK shows it (0: not measured) */
        *got = eng.ext_shown;
    else if (!strcmp(what, "tempo"))         /* the patch's TEMPO */
        *got = patch.tempo;
    else if (!strcmp(what, "lit"))           /* the keys lit for the notes held (bit 0: F3 at octave 0) */
        *got = (int)ui.lit;
    else if (!strcmp(what, "seed_hi"))       /* the slot's seed, in halves (RND_EMPTY: 65535 65535) */
        *got = (int)(seeds.seed[rnd_slot] >> 16);
    else if (!strcmp(what, "seed_lo"))
        *got = (int)(seeds.seed[rnd_slot] & 0xFFFFu);
    else if (!strcmp(what, "seed_slot"))
        *got = rnd_slot;
    else if (!strcmp(what, "play_hi"))      /* the seed playing on the synth, in halves */
        *got = (int)(rnd.seed >> 16);
    else if (!strcmp(what, "play_lo"))
        *got = (int)(rnd.seed & 0xFFFFu);
    else if (!strcmp(what, "seed_code"))     /* the slot's lock code */
        *got = (int)seeds.code[rnd_slot];
    else if (!strcmp(what, "rnd_tx"))        /* messages to the synth so far */
        *got = (int)rnd_ntx;
    else if (!strncmp(what, "rnd_op", 6) || !strncmp(what, "rnd_arg", 7)) {   /* message N's op (or status), first byte */
        uint32_t i = (uint32_t)atoi(what + (what[4] == 'o' ? 6 : 7));
        *got = i < rnd_ntx && i < 64u ? rnd_tx[i][what[4] == 'o' ? 0 : 1] : -1;
    } else if (!strcmp(what, "rnd_ntr"))     /* the synth's: tracks playing, lock code */
        *got = rnd.ntr;
    else if (!strcmp(what, "rnd_code"))
        *got = (int)rnd.code;
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
        *got = BANK_P(set.slot).tempo;
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
    h->nslots = NPSLOT;
    h->psize = (uint8_t)ps;
    for (i = 0; i < NPSLOT; i++) {
        for (j = 0; j < ps; j++)
            r[offsetof(bank_t, p) + i * ps + j] = ((const uint8_t *)&d)[j < SEQ_GAP || v >= 8u ? j : j + SEQ_GAP_N];
        if (v <= 7u)
            OLD(i, sq.step[5]) = V7_TIE;
    }
    return r;
}

static int host_fails;                       /* the app's own checks failed (rnd_words) */
int app_host_fails(void) { return host_fails; }
#include <time.h>
static long dt_prof_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000L + t.tv_nsec;
}
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    static uint8_t syx[32768];
    FILE *fp;
    size_t n;
    (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "dt_dump")) {           /* the pattern shown: its settings and hits */
        const dq_pat_t *p = &dqm.pat[ui.dt_pat];
        uint32_t i;
        printf("pattern %u: %u/%u x%u len %u ec %u swing %u, %u hits (cur %u):", ui.dt_pat, p->num, DQ_BEAT[p->beat], (unsigned)dq_bars(p), (unsigned)p->len,
               DQ_EC[p->ec], DQ_SWING[p->swing], p->n, dq.cur);
        for (i = 0; i < p->n; i++)
            printf(" %u:%s", (unsigned)DQ_T(dqm.ev[p->start + i]), DT_NAME[DQ_D(dqm.ev[p->start + i])]);
        printf("\n");
        return 1;
    }
    if (!strcmp(cmd, "song_is")) {           /* song_is TEMPO STEP..: the song shown's first tempo, its steps' raw values; a
                                              * step past its end not an end, "!" */
        const ds_song_t *t = &dsm.s[ui.dt_song < DS_NSONG ? ui.dt_song : 0];
        const char *const want = plat_host_line + strlen("song_is ");
        static char have[512];
        size_t n = strlen(want), at = (size_t)sprintf(have, "%u", t->tempo);
        uint32_t i;
        for (i = 0; i < ds_end(t); i++)
            at += (size_t)sprintf(have + at, " %u", t->step[i]);
        for (; i < DS_NSTEP; i++)
            if (t->step[i] != DS_END) {
                at += (size_t)sprintf(have + at, " !");
                break;
            }
        while (n && (want[n - 1] == '\n' || want[n - 1] == '\r' || want[n - 1] == ' '))
            n--;                             /* (the line's end) */
        if (strlen(have) != n || strncmp(have, want, n)) {
            printf("FAIL song_is: %s, not %.*s\n", have, (int)n, want);
            host_fails++;
        }
        return 1;
    }
    if (!strcmp(cmd, "hint")) {              /* hint TEXT: the button's hint up is TEXT */
        const char *const want = plat_host_line + strlen("hint ");
        size_t n = strlen(want);
        while (n && (want[n - 1] == '\n' || want[n - 1] == '\r' || want[n - 1] == ' '))
            n--;                             /* (the line's end) */
        if (!ui.hint_until || !ui.hint || strlen(ui.hint) != n || strncmp(ui.hint, want, n)) {
            printf("FAIL hint: %s, not %.*s\n", ui.hint_until && ui.hint ? ui.hint : "(none)", (int)n, want);
            host_fails++;
        }
        return 1;
    }
    if (!strcmp(cmd, "dt_eprom")) {          /* dt_eprom 0|1: every voice live, or from its EPROM if it has one (the A/B) */
        dt_use_eprom = (uint8_t)atoi(a);
        return 1;
    }
    if (!strcmp(cmd, "dt_export")) {         /* dt_export NAME STEP FILE: a voice's ROM, as its DAC's numbers (tools/dt_eprom.py) */
        uint32_t k;
        const uint8_t was = dt_use_eprom;
        for (k = 0; k < NDT && strcmp(a, DT_NAME[k]); k++)
            ;
        if (k < NDT) {
            dt_ch_t *ch = &dt.ch[DT_CH[k]];
            FILE *f = fopen(c, "wb");
            dt_use_eprom = 0;
            dt_hit(k, (uint32_t)atoi(b) - 1u, 255u);
            while (f && ch->k < ch->len) {
                const float x = dt_dac(dt_gen(ch));
                const signed char o = (signed char)(int32_t)(x * 127.0f + (x < 0.0f ? -0.5f : 0.5f));
                fwrite(&o, 1, 1, f);
                ch->k++;
            }
            ch->drum = DT_NONE;
            dt_use_eprom = was;
            if (f)
                fclose(f);
        }
        return 1;
    }
    if (!strcmp(cmd, "dt_bake")) {           /* dt_bake FROM TO LOW: TO's ROM made now from FROM's voice, as tools/dt_eprom.py
                                              * makes it (LOW 1: its low take too), so a tunable is heard through its
                                              * table without a build of the tables */
        uint32_t from, to, t;
        const uint8_t was = dt_use_eprom;
        for (from = 0; from < NDT && strcmp(a, DT_NAME[from]); from++)
            ;
        for (to = 0; to < NDT && strcmp(b, DT_NAME[to]); to++)
            ;
        if (from < NDT && to < NDT) {
            dt_use_eprom = 0;
            for (t = 0; t < (atoi(c) ? 2u : 1u); t++) {
                dt_ch_t *ch = &dt.ch[DT_CH[from]];
                uint32_t n = 0;
                int8_t *d;
                dt_hit(from, t ? 0u : 7u, 255u);
                d = malloc(ch->len + 1u);
                while (ch->k < ch->len) {
                    const float x = dt_dac(dt_gen(ch));
                    d[n++] = (int8_t)(int32_t)(x * 127.0f + (x < 0.0f ? -0.5f : 0.5f));
                    ch->k++;
                }
                ch->drum = DT_NONE;
                while (n && !d[n - 1u])      /* (the zeros at its end: cut, as there) */
                    n--;
                free(dt_baked[to][t].d);
                dt_baked[to][t].d = d;
                dt_baked[to][t].n = n ? n : 1u;
            }
            dt_use_eprom = was;
        }
        return 1;
    }
    if (!strcmp(cmd, "dt_prof")) {           /* each drum's cost, a block at a time, against a bass note's (the FM-1's load
                                              * is known for that: GLO's CPU meter) */
        static int32_t lr[2 * APP_BLOCK];
        uint32_t d, st, i;
        long t0, base = 0;
        fifo_put(&keyq, 60u | 100u << 8);
        for (i = 0; i < 200u; i++) {
            app_render(lr, APP_BLOCK, 2048u);
            if (i >= 100u) {
                t0 = dt_prof_ns();
                app_render(lr, APP_BLOCK, 2048u);
                base += dt_prof_ns() - t0;
            }
        }
        base /= 100;
        fifo_put(&keyq, 60u);
        for (i = 0; i < 2000u; i++)
            app_render(lr, APP_BLOCK, 2048u);
        printf("bass block %ld ns\n", base);
        for (d = 0; d < NDT; d++)
            for (st = 0; st < 16u; st += 7u + (st == 14u)) {
                long pk = 0, sum = 0, h;
                uint32_t b = 0;
                t0 = dt_prof_ns();
                dt_hit(d, st, 175u);
                h = dt_prof_ns() - t0;
                while (dt.ch[DT_CH[d]].drum != DT_NONE && b < 4000u) {
                    long e;
                    t0 = dt_prof_ns();
                    for (i = 0; i < APP_BLOCK; i++)
                        (void)dt_next(1.0f), (void)fx_next();   /* (the room too, as it was in dt_next) */
                    e = dt_prof_ns() - t0;
                    sum += e;
                    if (e > pk)
                        pk = e;
                    b++;
                }
                printf("%-8s step %2u  res %2u  hit %3ld%%  peak %3ld%%  avg %3ld%%  blocks %u\n", DT_NAME[d], st + 1u,
                       (unsigned)dt.ch[DT_CH[d]].n, h * 20 / base, pk * 20 / base, b ? sum / b * 20 / base : 0, b);
            }
        return 1;
    }
    if (!strcmp(cmd, "dt_gain")) {           /* dt_gain X: the drums' output times X (tools/dt_compare.py's: its
                                              * renders well above the host's 16-bit files' floor) */
        dt_test_gain = (float)atof(a);
        return 1;
    }
    if (!strcmp(cmd, "drum")) {              /* drum NAME STEP [A]: a hit at tuning step STEP (1..16), full volume,
                                              * accented with A (tools/dt_compare.py's) */
        uint32_t k;
        for (k = 0; k < NDT && strcmp(a, DT_NAME[k]); k++)
            ;
        if (k == NDT || atoi(b) < 1 || atoi(b) > 16) {
            printf("FAIL drum: %s %s\n", a, b);
            host_fails++;
        } else
            fifo_put(&dtq, k | (uint32_t)(atoi(b) - 1) << 4 | (175u + (*c == 'A' ? 80u : 0u)) << 8);
        return 1;
    }
    if (!strcmp(cmd, "rnd_words")) {         /* rnd_words WORD..: the slot's names, as RND SEED shows them
                                              * (in capitals) */
        char w[64];
        const char *want = plat_host_line + strlen("rnd_words ");
        size_t n = strlen(want);
        while (n && (want[n - 1] == '\n' || want[n - 1] == '\r' || want[n - 1] == ' '))
            n--;                             /* (the line's end) */
        seed_words(seeds.seed[rnd_slot], seeds.code[rnd_slot], w);
        if (strlen(w) != n || strncmp(w, want, n)) {
            printf("FAIL rnd_words: %s, not %.*s\n", w, (int)n, want);
            host_fails++;
        }
        return 1;
    }
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
                                              * unnamed, saved and playing: what the older tests start from */
        uint32_t i;
        for (i = 0; i < NSLOTS; i++) {       /* (no names: each dump out by its slot) */
            patch_defaults(&BANK_P(i));
            name_set(i, "");
        }
        memset(cur_name, 0, PNAME_N);
        for (i = 0; i < NBANKP; i++)
            bank_save(i * NPSLOT);
        names_save();
        patch = BANK_P(set.slot);
        return 1;
    }
    if (!strcmp(cmd, "set_v2")) {            /* the settings as the firmware before the sequences saved them: 16 bytes,
                                              * the last padding (2 here, as one device had it) */
        uint8_t r[16];
        memcpy(r, &set, SET_V2_SIZE);
        r[15] = 2;
        store_save(OBJ_SETTINGS, r, sizeof r);
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
        store_save(OBJ_PATCHES, r, offsetof(bank_t, p) + NPSLOT * ps);
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
