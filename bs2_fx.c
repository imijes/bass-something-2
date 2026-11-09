/* The FX bus: one of each effect, shared by every instrument, as a mixer's aux sends into one rack (one room, so its
 * memory once). An instrument sends into an effect after its own level (a post-fader send: fx_room_send) and wakes the
 * bus while it plays (fx_wake); fx_next is the effects' returns, a sample at a time, on the master after the
 * instruments. Each instrument's send shows as its own mod; the effect behind it is the one here. Today: the room, and
 * the Space Echo (bs2_se.c). */

/* The room: a drum's tail is mostly the space it was played in (the 400's own samples had theirs in them), and a
 * resonator ringing on sounds a synth's. So the voices' modes ring short and a small room makes the tail, in real
 * time (not ROM time: the same room at every tuning). Two allpasses smear the hit, then four delay lines (23..41 ms)
 * fed back through a Hadamard mix, each with its walls' low pass; their sum is the room's sound. */
#define FX_ROOM_RT 0.45f                     /* the room's reverberation time (-60 dB), s */
#define FX_ROOM_DAMP 0.85f                   /* its walls' damping: a one-pole low pass's step in its loop */
#define FX_ROOM_IN 0.06f                     /* the room's input low pass, a one-pole's step (darker in, no tin) */
#define FX_ROOM_WET 0.6f                     /* its level out */
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
static struct {
    float room;                              /* this sample's sends into the room, summed */
    float se;                                /* and into the Space Echo (bs2_se.c) */
    float echo;                              /* the Space Echo's return, the last sample's (the USB audio's own channel) */
    uint32_t idle;                           /* samples since an instrument last woke the bus (it stops once quiet) */
} fx PLAT_POOL;

/* DRUM ROOM: its time (s), its walls' and input's low passes' steps, its level out (the main loop's: a float each, read
 * whole by the audio interrupt) */
static void fx_room_set(float rt, float damp, float in_k, float wet)
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

static void fx_init(void)
{
    uint32_t i;
    uint16_t at = 0;
    for (i = 0; i < FX_NRL + FX_NAP; i++) {
        fx_room.ring[i] = (fx_ring_t){at, FX_RL[i], 0};
        at = (uint16_t)(at + FX_RL[i]);
    }
    fx_room.rt = 0.0f;
    fx_room_set(FX_ROOM_RT, 1.0f - FX_ROOM_DAMP, FX_ROOM_IN, FX_ROOM_WET);
    fx.room = 0.0f, fx.se = 0.0f;
    fx.idle = 0xFFFFFFFFu;
    se_init();
}

/* an instrument playing (audio interrupt): the bus on until its tails are out */
static inline void fx_wake(void) { fx.idle = 0; }
/* an instrument's send into the room, after its level (audio interrupt) */
static inline void fx_room_send(float x) { fx.room += x; }

/* a ring's sample now, and the ring on a sample */
static inline float *fx_ring_at(fx_ring_t *r) { return &fx_room.buf[r->at + r->i]; }
static inline void fx_ring_on(fx_ring_t *r) { r->i = (uint16_t)(r->i + 1u >= r->len ? 0u : r->i + 1u); }

/* the room's next sample, from x (audio interrupt) */
static inline float fx_room_next(float x)
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

/* an instrument's send into the Space Echo, after its level (audio interrupt) */
static inline void fx_se_send(float x) { fx.se += x; }

/* the effects' returns, a sample (audio interrupt): the sends taken, the room left alone while the bus sleeps (the
 * echo sleeps on its own: its tail can outlast the bus's). g: MASTER's gain now. The echo works before MASTER, in a
 * 16-bit sample's units as at full MASTER: its sends (after MASTER) undone into them, its return under MASTER again,
 * so the tape saturates the same at any MASTER and MASTER turns down a runaway too */
static inline float fx_next(float g)
{
    const float x = fx.room, k = fm_maxf(g, 1.0f) * (1.0f / 32767.0f), e = se_next((fx.se + SE_TRY * x) / k) * k;
    fx.room = 0.0f, fx.se = 0.0f, fx.echo = e;
    if (fx.idle >= FX_IDLE)
        return e;
    fx.idle++;
    return e + fx_room_next(x);
}
