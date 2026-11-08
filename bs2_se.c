/* The Space Echo: a Roland RE-201's tape on the FX bus (docs/RE201_ROADMAP.md). One loop of tape, one record head,
 * three playback heads at 1 : 1.91 : 2.88 of head 1's time (Outer Space 2's, measured; not Roland's own models' 1:2:3).
 * The heads the mode picks are summed into one preamp, as on the unit (one recovery preamp for all three), through the
 * tape's own filters (a low cut at 85 Hz, and a high cut that falls with the tape's speed: 7.5 kHz fast, 2.5 kHz
 * slow, measured), so every repeat is darker. That sum goes back to the record head through INTENSITY, summed with
 * the input and saturated as tape does, and out through ECHO VOLUME and the BASS/TREBLE shelves (outside the loop, as
 * the RE-201's are). REPEAT RATE is the motor: head 1 from 175 ms (slow) to 60 ms (fast), followed as a first-order
 * lag (0.33 s, measured), so turning it bends the pitch; WOW wobbles the same speed (slow wow and faster flutter, as
 * filtered noise). INPUT drives the tape (a transistor's even harmonic, -31 dB, then the tape's tanh).
 * Levels in the app's units (a 16-bit sample's); the tape is 16-bit, so silence rounds to nothing. The spring: phase 3. */
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
#define SE_TRY 0.0f                          /* phase 1's trial: the room's sends into the echo too (0: off; the
                                              * instruments' own sends come in phase 4) */

/* the modes, 1..12: the heads playing (bit 0: head 1) and the spring's (5..12; the spring comes in phase 3) */
static const uint8_t SE_HEADS[12] = {1, 2, 4, 6, 1, 2, 4, 3, 6, 5, 7, 0};

typedef struct {
    float b0, b1, b2, a1, a2, s1, s2;        /* a biquad, transposed direct form II */
} se_bq_t;

static int16_t se_tape[SE_LEN] PLAT_POOL;
static struct {
    float rate, intensity, echo, input, bass, treble, wow;   /* the knobs, 0..1 */
    uint8_t mode;                            /* 0..11: modes 1..12 */
    float m, d, dv;                          /* the motor (head 1's time, samples); with the wobble, now and a sample's step */
    float lp_m, bass_at, treble_at;          /* the motor and knobs the filters were last worked out for */
    float h1, h2, hx, hx2;                   /* the low cut's two one-poles, and their inputs' last */
    se_bq_t lp, lo, hi, nw, nf, nw2, nf2;    /* the tape's high cut; BASS, TREBLE; the wobble's two noises (each a band,
                                              * then a low pass: steep, as a wobble's speed is its pitch) */
    uint32_t w, step, rng;                   /* the record head; samples to the next step; the noise */
    uint32_t quiet;                          /* samples since the tape last took anything (asleep after a whole loop) */
    uint16_t peak;                           /* the PEAK lamp's hold, samples (lit while nonzero) */
} se PLAT_POOL;

static inline float se_target(void) { return (SE_SLOW_MS + (SE_FAST_MS - SE_SLOW_MS) * se.rate) * (BS2_RATE / 1000.0f); }

static inline float se_bq(se_bq_t *q, float x)
{
    const float y = q->b0 * x + q->s1;
    q->s1 = q->b1 * x - q->a1 * y + q->s2;
    q->s2 = q->b2 * x - q->a2 * y;
    return y;
}

/* RBJ's cookbook: kind 0 a low pass, 1 a band pass (0 dB peak), 2 a low shelf, 3 a high shelf (g dB, slope 1) */
static void se_bq_set(se_bq_t *q, int kind, float hz, float qf, float g, float rate)
{
    const float w0 = 6.2831853f * hz / rate, cs = fm_cosf(w0), sn = fm_sinf(w0);
    float a0, al = sn / (2.0f * qf);
    if (kind < 2) {
        a0 = 1.0f + al;
        q->b0 = kind ? al : 0.5f * (1.0f - cs), q->b1 = kind ? 0.0f : 1.0f - cs, q->b2 = kind ? -al : q->b0;
        q->a1 = -2.0f * cs, q->a2 = 1.0f - al;
    } else {
        const float A = fm_db2lin(0.5f * g), s = 2.0f * fm_sqrtf(A) * sn * 0.70710678f, p = A + 1.0f, n = A - 1.0f;
        const float sg = kind == 2 ? 1.0f : -1.0f;
        a0 = p + sg * n * cs + s;
        q->b0 = A * (p - sg * n * cs + s), q->b1 = sg * 2.0f * A * (n - sg * p * cs), q->b2 = A * (p - sg * n * cs - s);
        q->a1 = -sg * 2.0f * (n + sg * p * cs), q->a2 = p + sg * n * cs - s;
    }
    a0 = 1.0f / a0;
    q->b0 *= a0, q->b1 *= a0, q->b2 *= a0, q->a1 *= a0, q->a2 *= a0;
}

/* the tape's high cut for head 1 at m samples: straight between the measured speeds (60, 118, 175 ms) */
static void se_lp_set(float m)
{
    const float ms = m * (1000.0f / BS2_RATE);
    const float hz = 7050.0f + (ms > 118.0f ? 71.0f : 80.0f) * (118.0f - ms);   /* (fitted as this biquad, at */
    const float q = 0.41f + (ms > 118.0f ? 0.0028f : 0.00155f) * (ms - 118.0f);    /* 44.1 kHz: 3 kHz slow, 11.7 fast) */
    se_bq_set(&se.lp, 0, fm_clampf(hz, 1500.0f, 14000.0f), q, 0.0f, BS2_RATE);
    se.lp_m = m;
}

static void se_init(void)
{
    uint32_t i;
    for (i = 0; i < SE_LEN; i++)
        se_tape[i] = 0;
    se.rate = 0.5f, se.intensity = 0.5f, se.echo = 0.5f, se.input = 0.5f, se.bass = 0.5f, se.treble = 0.5f;
    se.wow = 0.1f, se.mode = 0;
    se.m = se.d = se_target(), se.dv = 0.0f;
    se.h1 = se.h2 = se.hx = se.hx2 = 0.0f;
    se.lp = se.lo = se.hi = se.nw = se.nf = se.nw2 = se.nf2 = (se_bq_t){0};
    se_lp_set(se.m);
    se.bass_at = se.treble_at = -1.0f;
    se_bq_set(&se.nw, 1, 1.5f, 0.7f, 0.0f, BS2_RATE / (float)SE_STEP);   /* the slow wow's noise and the flutter's */
    se_bq_set(&se.nf, 1, 7.0f, 1.0f, 0.0f, BS2_RATE / (float)SE_STEP);
    se_bq_set(&se.nw2, 0, 3.0f, 0.7f, 0.0f, BS2_RATE / (float)SE_STEP);
    se_bq_set(&se.nf2, 0, 14.0f, 0.7f, 0.0f, BS2_RATE / (float)SE_STEP);
    se.w = 0, se.step = 0, se.rng = 22222u;
    se.quiet = 0xFFFFFFFFu, se.peak = 0;
}

/* a playback head d samples behind the record head: a 4-point Hermite between the tape's samples */
static inline float se_head(float d)
{
    float p = (float)se.w - d, f;
    int32_t i;
    uint32_t a, b, c, e;
    if (p < 0.0f)
        p += (float)SE_LEN;
    i = (int32_t)p;
    f = p - (float)i;
    b = (uint32_t)i;
    a = b ? b - 1u : SE_LEN - 1u;
    c = b + 1u >= SE_LEN ? b + 1u - SE_LEN : b + 1u;
    e = c + 1u >= SE_LEN ? c + 1u - SE_LEN : c + 1u;
    {
        const float y0 = se_tape[a], y1 = se_tape[b], y2 = se_tape[c], y3 = se_tape[e];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
}

/* every SE_STEP samples: the motor, the wobble, and the filters the motor and knobs move */
static void se_tick(void)
{
    float u, n;
    se.m += (se_target() - se.m) * (1.0f - fm_expf(-(float)SE_STEP / (SE_MOTOR_S * BS2_RATE)));
    se.rng = se.rng * 1664525u + 1013904223u;
    n = (float)(int32_t)se.rng * (1.0f / 2147483648.0f);
    u = se.wow * se.wow * (SE_WOW_U * se_bq(&se.nw2, se_bq(&se.nw, n)) + SE_FLUT_U * se_bq(&se.nf2, se_bq(&se.nf, n)));
    se.dv = (se.m * (1.0f + fm_clampf(u, -SE_WOW_MAX, SE_WOW_MAX)) - se.d) * (1.0f / (float)SE_STEP);
    if (fm_fabsf(se.m - se.lp_m) > 0.0002f * BS2_RATE)       /* (a fifth of a millisecond's move) */
        se_lp_set(se.m);
    if (se.bass != se.bass_at)
        se_bq_set(&se.lo, 2, SE_BASS_HZ, 0.0f, (2.0f * se.bass - 1.0f) * SE_SHELF_DB, BS2_RATE), se.bass_at = se.bass;
    if (se.treble != se.treble_at)
        se_bq_set(&se.hi, 3, SE_TREBLE_HZ, 0.0f, (2.0f * se.treble - 1.0f) * SE_SHELF_DB, BS2_RATE), se.treble_at = se.treble;
    se.lp.s1 = fm_flush(se.lp.s1), se.lp.s2 = fm_flush(se.lp.s2);   /* (no denormals as the tails die) */
    se.lo.s1 = fm_flush(se.lo.s1), se.lo.s2 = fm_flush(se.lo.s2);
    se.hi.s1 = fm_flush(se.hi.s1), se.hi.s2 = fm_flush(se.hi.s2);
    se.h1 = fm_flush(se.h1), se.h2 = fm_flush(se.h2);
}

/* the echo's next sample, from x (audio interrupt) */
static inline float se_next(float x)
{
    const uint32_t heads = SE_HEADS[se.mode];
    const float a = 1.0f / (1.0f + 6.2831853f * SE_LOW_HZ / BS2_RATE);
    float sum = 0.0f, rec;
    if (fm_fabsf(x) < 0.5f && se.quiet >= SE_LEN) {
        se.m = se.d = se_target(), se.dv = 0.0f;   /* asleep: the tape silent all round; the motor where it's set */
        se.peak = 0;
        return 0.0f;
    }
    if (!se.step--)
        se_tick(), se.step = SE_STEP - 1u;
    se.d += se.dv;
    if (heads & 1u)
        sum += se_head(se.d);
    if (heads & 2u)
        sum += se_head(se.d * SE_HEAD2);
    if (heads & 4u)
        sum += se_head(se.d * SE_HEAD3);
    se.h1 = a * (se.h1 + sum - se.hx), se.hx = sum;          /* the tape's low cut, */
    se.h2 = a * (se.h2 + se.h1 - se.hx2), se.hx2 = se.h1;
    sum = se_bq(&se.lp, se.h2);                              /* and its high cut */
    x *= fm_exp2f(4.0f * se.input - 2.0f);                   /* INPUT: -12..+12 dB */
    x += SE_ASYM * fm_fabsf(x);
    if (fm_fabsf(x) > SE_PEAK)
        se.peak = (uint16_t)(0.15f * BS2_RATE);
    else if (se.peak)
        se.peak--;
    rec = SE_SAT * fm_tanhf((x + sum * SE_FB * se.intensity) * (1.0f / SE_SAT));
    se.quiet = fm_fabsf(rec) < 0.5f ? se.quiet + 1u : 0u;
    se_tape[se.w] = (int16_t)(rec + (rec < 0.0f ? -0.5f : 0.5f));
    se.w = se.w + 1u >= SE_LEN ? 0u : se.w + 1u;
    sum *= 3.5f * se.echo * se.echo;         /* ECHO VOLUME: 5 puts a head 2.4 dB under the dry (a tone's, measured) */
    return se_bq(&se.hi, se_bq(&se.lo, sum));
}
