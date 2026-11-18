/* The FX bus: one of each effect, shared by every instrument, as a mixer's aux sends into one rack (one room, so its
 * memory once). An instrument sends into an effect after its own level (a post-fader send: fx_room_send) and wakes the
 * bus while it plays (fx_wake); fx_take is the effects' returns, a sample at a time, on the master after the
 * instruments. Each instrument's send shows as its own mod; the effect behind it is the one here. Today: the room, and
 * the Space Echo (bs2_se.c). The bus runs a block late, on the second core when there is one (fx_job, beside the
 * RMI's block): see fxq. */
#include <stdint.h>
#include "plat.h"
/* (the fast maths the RAM code uses, made always inline here: else -O2 may leave one in the flash, out of the RAM
 * code's reach) */
#define FM_AI static inline __attribute__((always_inline))
FM_AI float fm_fabsf(float x);
FM_AI float fm_maxf(float a, float b);
FM_AI float fm_clampf(float x, float lo, float hi);
FM_AI float fm_flush(float x);
FM_AI float fm_floorf(float x);
FM_AI float fm_exp2_nf(int32_t n, float f);
FM_AI float fm_exp2f(float x);
FM_AI float fm_expf(float x);
FM_AI float fm_tanhf(float x);
#include "fastmath.h"
#include "app.h"
#include "bs2_fx.h"
#ifdef PLAT_HOST
#define FX_HOT
#else
#define FX_HOT __attribute__((section(".c1_text")))   /* (RAM, as the organ's: off the shared cache's misses; it calls
                                              * only what is inlined into it) */
#endif
#define FX_IN static inline __attribute__((always_inline))
#include "bs2_se.c"                          /* the Space Echo */

/* The room: a drum's tail is mostly the space it was played in (the 400's own samples had theirs in them), and a
 * resonator ringing on sounds a synth's. So the voices' modes ring short and a small room makes the tail, in real
 * time (not ROM time: the same room at every tuning). Two allpasses smear the hit, then four delay lines (23..41 ms)
 * fed back through a Hadamard mix, each with its walls' low pass; their sum is the room's sound. */
#define FX_NRL 4u                            /* the delay lines */
#define FX_NAP 4u                            /* the allpasses smearing the hit (four: two left a tin can's ring) */
static const uint16_t FX_RL[FX_NRL + FX_NAP] = {1031, 1327, 1523, 1801, 142, 107, 379, 277};   /* lines, allpasses */
typedef struct {
    uint16_t at, len, i;                     /* a ring in fx_room.buf: where it starts, its length, its place */
} fx_ring_t;
static struct {
    float buf[1031 + 1327 + 1523 + 1801 + 142 + 107 + 379 + 277];
    fx_ring_t ring[FX_NRL + FX_NAP];
    float in, lp[FX_NRL], g[FX_NRL];         /* the input's filter, the walls', each line's feedback for its time */
    float rt, damp, in_k, wet;               /* DRUM ROOM's (fx_room_set): its time, walls' step, input's step, level */
    uint32_t idle;                           /* the bus asleep after this long unwoken (its tails out): twice its time */
} fx_room PLAT_POOL;

#define FX_IDLE (fx_room.idle)
fx_t fx PLAT_POOL;
volatile uint32_t fx_woke;

fxq_t fxq PLAT_POOL;

/* DRUM ROOM: its time (s), its walls' and input's low passes' steps, its level out (the main loop's: a float each, read
 * whole by the audio interrupt) */
void fx_room_set(float rt, float damp, float in_k, float wet)
{
    uint32_t i;
    if (rt != fx_room.rt) {
        for (i = 0; i < FX_NRL; i++)         /* -60 dB in rt: 10^(-3 len / (rt rate)) a pass */
            fx_room.g[i] = fm_exp2f(-3.0f * 3.321928f * (float)FX_RL[i] / (rt * BS2_RATE));
        fx_room.rt = rt;
        fx_room.idle = (uint32_t)(2.0f * rt * BS2_RATE);
    }
    fx_room.damp = damp, fx_room.in_k = in_k, fx_room.wet = wet;
}

void fx_init(void)
{
    uint32_t i;
    uint16_t at = 0;
    for (i = 0; i < FX_NRL + FX_NAP; i++) {
        fx_room.ring[i] = (fx_ring_t){at, FX_RL[i], 0};
        at = (uint16_t)(at + FX_RL[i]);
    }
    fx_room.rt = 0.0f;
    fx_room_set(FX_ROOM_RT, 1.0f - FX_ROOM_DAMP, FX_ROOM_IN, FX_ROOM_WET);
    fx.room = 0.0f;
    fx.idle = 0xFFFFFFFFu;
    fxq.n = fxq.rw = fxq.rr = 0;
    se_init();
}


/* a ring's sample now, and the ring on a sample */
FX_IN float *fx_ring_at(fx_ring_t *r) { return &fx_room.buf[r->at + r->i]; }
FX_IN void fx_ring_on(fx_ring_t *r) { r->i = (uint16_t)(r->i + 1u >= r->len ? 0u : r->i + 1u); }

/* the room's next sample, from x (audio interrupt) */
FX_IN float fx_room_next(float x)
{
    float y[FX_NRL], h0, h1, h2, h3, out = 0.0f;
    uint32_t k;
    x = fx_room.in += (x - fx_room.in) * fx_room.in_k;
    for (k = FX_NRL; k < FX_NRL + FX_NAP; k++) {    /* the allpasses (0.6) */
        float *p = fx_ring_at(&fx_room.ring[k]), t = *p;
        *p = x + 0.6f * t;
        x = t - 0.6f * *p;
        fx_ring_on(&fx_room.ring[k]);
    }
    for (k = 0; k < FX_NRL; k++) {           /* the lines' outputs, through the walls */
        y[k] = *fx_ring_at(&fx_room.ring[k]);
        out += y[k];
        fx_room.lp[k] += (y[k] - fx_room.lp[k]) * fx_room.damp;
    }
    h0 = fx_room.lp[0] + fx_room.lp[1];      /* the Hadamard mix (a half: it keeps the energy) */
    h1 = fx_room.lp[0] - fx_room.lp[1];
    h2 = fx_room.lp[2] + fx_room.lp[3];
    h3 = fx_room.lp[2] - fx_room.lp[3];
    y[0] = h0 + h2, y[1] = h1 + h3, y[2] = h0 - h2, y[3] = h1 - h3;
    for (k = 0; k < FX_NRL; k++) {
        *fx_ring_at(&fx_room.ring[k]) = x + 0.5f * y[k] * fx_room.g[k];
        fx_ring_on(&fx_room.ring[k]);
    }
    return out * (0.25f * fx_room.wet);
}

float se_own = 1.0f;                  /* the echo's own volume (SELECT on its pages: se_apply), on its return */
/* the effects' returns, a sample (fx_job's), from x into the room and s into the Space Echo: the room left alone while
 * the bus sleeps (the echo sleeps on its own: its tail can outlast the bus's). g: MASTER's gain then; *echo the echo's
 * alone. The echo works before MASTER, in a 16-bit sample's units as at full MASTER: its sends (after MASTER) undone
 * into them, its return under MASTER again, so the tape saturates the same at any MASTER and MASTER turns down a
 * runaway too */
FX_HOT float fx_next(float x, float s, float g, float *echo)
{
    const float k = fm_maxf(g, 1.0f) * (1.0f / 32767.0f), e = se_next((s + SE_TRY * x) / k) * k * se_own;
    *echo = e;
    if (fx.idle >= FX_IDLE)
        return e;
    fx.idle++;
    return e + fx_room_next(x);
}

/* the last block's sends into the ring's returns (the render's job, after the RMI's block: the second core's when there
 * is one) */
FX_HOT void fx_job(void)
{
    const uint32_t n = fxq.n;
    uint32_t i;
    fxq.n = 0;
    if (fxq.wake)
        fx.idle = 0;
    for (i = 0; i < n && fxq.rw - fxq.rr < FX_RET; i++, fxq.rw++)   /* (full: the render took fewer than it gave) */
        fxq.ret[fxq.rw % FX_RET] = fx_next(fxq.room[i], fxq.se[i], fxq.g0 + fxq.dg * (float)i,
                                           &fxq.echo[fxq.rw % FX_RET]);
}

int fx_awake(void)                           /* the room still sounding (the main loop's: a flash write waits) */
{
    return fx.idle < FX_IDLE;
}
