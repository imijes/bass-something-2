/* SPDX-License-Identifier: MPL-2.0 */
/* The FX bus and its Space Echo (bs2_fx.c with bs2_se.c: a unit of its own at -O2, fm1.toml o2; fx_job's code in the
 * second core's RAM, as the organ's): what the rest of the app shares with them */
#ifndef BS2_FX_H
#define BS2_FX_H
#include <stdint.h>
#define SE_HEAD2 1.91f                       /* heads 2 and 3, in head 1's times */
#define SE_HEAD3 2.88f
#define SE_SLOW_MS 175.0f                    /* head 1 at REPEAT RATE 0 and 10 */
#define SE_FAST_MS 60.0f
#define SE_MOTOR_S 0.33f                     /* the motor's lag, a time constant */
#define SE_SAT 20000.0f                      /* the tape's saturation, a tanh's knee (about -4 dBFS) */
#define SE_ASYM 0.067f                       /* the input transistor's lean: x + this |x|, an H2 at -31 dB */
#define SE_FB 1.4f                           /* INTENSITY 10's loop gain: unity near 7 (the service notes' trim) */
#define SE_LOW_HZ 85.0f                      /* the tape's low cut: two one-poles */
#define SE_SHELF_DB 12.0f                    /* BASS/TREBLE's range, at the shelves' corners: about 10 dB at 100 Hz */
#define SE_BASS_HZ 200.0f                    /* and 5 kHz, the service notes' */
#define SE_TREBLE_HZ 2500.0f
#define SE_WOW_U 0.176f                      /* WOW 10's depth (as Outer Space 2's: 1.3 % rms at full, the knob squared), */
#define SE_FLUT_U 0.0282f                    /* the slow band's and the flutter's gains on their filtered noise */
#define SE_WOW_MAX 0.04f                     /* the speed's wobble at most (the tape's margin) */
#define SE_PEAK (0.5f * SE_SAT)              /* the PEAK lamp: the tape's input this near saturating */
#define SE_LEN 23120u                        /* the tape: head 3 at its slowest (504 ms), WOW's 4 %, the Hermite's 3 */
#define SE_STEP 32u                          /* the motor, the wobble and the filters, worked out every this many samples */
#define SE_SPRINGS 3u                        /* the spring tank's */
#define SE_SP_RATE (0.5f * BS2_RATE)         /* the spring's own rate: all it says is under 2 kHz, so half (half the work) */
#define SE_SP_LEN 880u                       /* a spring's loop, at most */
#define SE_SP_AP 12u                         /* the stretched allpasses before the springs */
#define SE_SP_A 0.6f                         /* their coefficient (and the loops') */
#define SE_SP_G 1.059f                       /* a round trip's gain: with the loop's filters, 0.912 at 1 kHz (2.9 s
                                              * to -60 dB at 38.5 ms, measured), and under it everywhere */
#define SE_SP_DAMP 0.5775f                   /* the loops' damping, a one-pole's step (3 kHz) */
#define SE_SP_LOW_HZ 400.0f                  /* and their low cut, a one-pole */
#define SE_SP_GAIN 1.9f                      /* REVERB VOLUME 10's level (Outer Space 2's) */
#define SE_SP_IDLE (uint32_t)(4.0f * BS2_RATE)   /* the spring asleep this long after its input stops (its tail out) */
#define SE_TRY 0.0f                          /* a trial: the room's sends into the echo too, in mode 11 (0: off; the
                                              * instruments' own sends come in phase 4) */

/* the modes, 1..12: the heads playing (bit 0: head 1) and the spring's (5..12; the spring comes in phase 3) */
static const uint8_t SE_HEADS[12] = {1, 2, 4, 6, 1, 2, 4, 3, 6, 5, 7, 0};

typedef struct {
    float b0, b1, b2, a1, a2, s1, s2;        /* a biquad, transposed direct form II */
} se_bq_t;
typedef struct {
    float rate, intensity, echo, input, bass, treble, wow, reverb;   /* the knobs, 0..1 */
    uint8_t mode;                            /* 0..11: modes 1..12 */
    float m, d, dv;                          /* the motor (head 1's time, samples); with the wobble, now and a sample's step */
    float lp_m, bass_at, treble_at;          /* the motor and knobs the filters were last worked out for */
    float h1, h2, hx, hx2;                   /* the low cut's two one-poles, and their inputs' last */
    se_bq_t lp, lo, hi, nw, nf, nw2, nf2;    /* the tape's high cut; BASS, TREBLE; the wobble's two noises (each a band,
                                              * then a low pass: steep, as a wobble's speed is its pitch) */
    uint32_t w, step, rng;                   /* the record head; samples to the next step; the noise */
    uint32_t quiet;                          /* samples since the tape last took anything (asleep after a whole loop) */
    uint16_t peak;                           /* the PEAK lamp's hold, samples (lit while nonzero) */
    float ig, vu, sync_ms;                   /* INPUT's gain; the VU meter's level (its input, about 0.3 s); TEMPO
                                              * SYNC's head 1, ms (0: REPEAT RATE's own) */
} se_t;
extern se_t se;
/* the voicing to taste (ECHO VOICE, app.c's se_apply): the constants above scaled, 1 as modelled */
typedef struct {
    float fb, multi, sat, wow, flut, spa, spg;   /* the loop's gain, two or three heads' too, the knee, WOW's depths, the
                                              * spring's dispersion and round-trip gain */
} se_v_t;
extern se_v_t se_v;
extern float se_own;                         /* the echo's own volume (SELECT on its pages: se_apply), on its return */
#ifdef PLAT_HOST
extern float se_in_max, se_out_max;          /* the tape's input and the echo's output at their loudest (expect) */
#endif
int se_ringing(void);                        /* its tape or its spring still awake (the main loop's) */

#define FX_ROOM_RT 0.45f                     /* the room's reverberation time (-60 dB), s */
#define FX_ROOM_DAMP 0.85f                   /* its walls' damping: a one-pole low pass's step in its loop */
#define FX_ROOM_IN 0.06f                     /* the room's input low pass, a one-pole's step (darker in, no tin) */
#define FX_ROOM_WET 0.6f                     /* its level out */
typedef struct {
    float room;                              /* this sample's sends into the room, summed (the first core's: the drums') */
    uint32_t idle;                           /* samples since an instrument last woke the bus (it stops once quiet: the
                                              * bus's, fx_job's) */
} fx_t;
extern fx_t fx;
extern volatile uint32_t fx_woke;            /* an instrument woke the bus in this block (the first core's: fx_wake) */

/* The bus a block late: the render's second half (the first core, after the job is back) keeps the block's sends here;
 * the next block's job (fx_job: the second core's, beside the bass and the drums) makes their returns into the ring,
 * which that block's second half takes. 5.8 ms more before the echo and the room, unheard on either. Neither core
 * touches a side while the other has it: the sends written after a job, read by the next; the ring the same */
#define FX_RET (2u * APP_BLOCK)              /* the returns' ring (a block's, as the device renders: room for a host's) */
typedef struct {
    float room[APP_BLOCK], se[APP_BLOCK];    /* a block's sends into the room and the Space Echo */
    float g0, dg;                            /* MASTER across it */
    uint32_t n, wake;                        /* its samples (0: taken); an instrument woke the bus in it */
    float ret[FX_RET], echo[FX_RET];         /* the returns; the echo's alone (the USB audio's own channel) */
    uint32_t rw, rr;                         /* the ring's written (the job's), read (the render's) */
} fxq_t;
extern fxq_t fxq;

void fx_init(void);
void fx_room_set(float rt, float damp, float in_k, float wet);   /* DRUM ROOM's */
void fx_job(void);                           /* the second core's (through PLAT_FAR: RAM code) */
float fx_next(float x, float s, float g, float *echo);   /* a sample of the returns (fx_job's; the host's tests) */
int fx_awake(void);                          /* the room still sounding (the main loop's) */

/* an instrument playing (audio interrupt, the first core's): the bus on until its tails are out (from fx_job's next) */
static inline void fx_wake(void) { fx_woke = 1; }
/* an instrument's send into the room, after its level (audio interrupt) */
static inline void fx_room_send(float x) { fx.room += x; }

/* the render's second half, a sample: the returns' next (0 before the first), *echo the echo's alone; x into the room
 * and s into the Space Echo kept as sample i of the block for the next job */
static inline float fx_take(uint32_t i, float x, float s, float *echo)
{
    float r = 0.0f;
    *echo = 0.0f;
    if (fxq.rr != fxq.rw) {
        r = fxq.ret[fxq.rr % FX_RET], *echo = fxq.echo[fxq.rr % FX_RET];
        fxq.rr++;
    }
    fxq.room[i] = x, fxq.se[i] = s;
    return r;
}
/* and after its last sample: n of them kept, MASTER across them (from g0 by dg a sample) */
static inline void fx_kept(uint32_t n, float g0, float dg, uint32_t woke)
{
    fxq.g0 = g0, fxq.dg = dg, fxq.wake = woke || fx_woke;
    fx_woke = 0;
    fxq.n = n;
}
#endif
