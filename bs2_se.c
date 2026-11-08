/* The Space Echo: a Roland RE-201's tape on the FX bus (docs/RE201_ROADMAP.md). One loop of tape, one record head,
 * three playback heads at 1 : 1.91 : 2.88 of head 1's time (Outer Space 2's, measured; not Roland's own models' 1:2:3).
 * The heads the mode picks are summed into one preamp, as on the unit (one recovery preamp for all three); that sum
 * goes out through ECHO VOLUME and back to the record head through INTENSITY, summed with the input and saturated as
 * tape does. REPEAT RATE is the motor: head 1 from 175 ms (slow) to 60 ms (fast), followed as a first-order lag
 * (0.33 s, measured), so turning it bends the pitch. The tape's own filters, the EQ, wow and the spring come later.
 * Levels in the app's units (a 16-bit sample's); the tape is 16-bit, so silence rounds to nothing. */
#define SE_HEAD2 1.91f                       /* heads 2 and 3, in head 1's times */
#define SE_HEAD3 2.88f
#define SE_SLOW_MS 175.0f                    /* head 1 at REPEAT RATE 0 and 10 */
#define SE_FAST_MS 60.0f
#define SE_MOTOR_S 0.33f                     /* the motor's lag, a time constant */
#define SE_SAT 20000.0f                      /* the tape's saturation, a tanh's knee (about -4 dBFS) */
#define SE_FB 1.4f                           /* INTENSITY 10's loop gain: unity near 7 (the service notes' trim) */
#define SE_LEN 22400u                        /* the tape: head 3 at its slowest (504 ms) and the interpolation's 3 */
#define SE_STEP 32u                          /* the motor's lag, worked out every this many samples */
#define SE_TRY 0.0f                          /* phase 1's trial: the room's sends into the echo too (0: off; the
                                              * instruments' own sends come in phase 4) */

/* the modes, 1..12: the heads playing (bit 0: head 1) and the spring's (5..12; the spring comes in phase 3) */
static const uint8_t SE_HEADS[12] = {1, 2, 4, 6, 1, 2, 4, 3, 6, 5, 7, 0};

static int16_t se_tape[SE_LEN] PLAT_POOL;
static struct {
    float rate, intensity, echo;             /* the knobs, 0..1 */
    uint8_t mode;                            /* 0..11: modes 1..12 */
    float d, dv;                             /* head 1's time now (samples), and its step a sample, to the next count */
    uint32_t w, step;                        /* the record head; samples to the motor's next count */
    uint32_t quiet;                          /* samples since the tape last took anything (asleep after a whole loop) */
} se PLAT_POOL;

static inline float se_target(void) { return (SE_SLOW_MS + (SE_FAST_MS - SE_SLOW_MS) * se.rate) * (BS2_RATE / 1000.0f); }

static void se_init(void)
{
    uint32_t i;
    for (i = 0; i < SE_LEN; i++)
        se_tape[i] = 0;
    se.rate = 0.5f, se.intensity = 0.5f, se.echo = 0.5f, se.mode = 0;
    se.d = se_target(), se.dv = 0.0f;
    se.w = 0, se.step = 0;
    se.quiet = 0xFFFFFFFFu;
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

/* the echo's next sample, from x (audio interrupt) */
static inline float se_next(float x)
{
    const uint32_t heads = SE_HEADS[se.mode];
    float sum = 0.0f, rec;
    if (fm_fabsf(x) < 0.5f && se.quiet >= SE_LEN) {
        se.d = se_target(), se.dv = 0.0f;    /* asleep: the tape silent all round; the motor where it's set */
        return 0.0f;
    }
    if (!se.step--) {                        /* the motor: a first-order lag, as a straight line to each count */
        const float k = 1.0f - fm_expf(-(float)SE_STEP / (SE_MOTOR_S * BS2_RATE));
        se.dv = (se_target() - se.d) * k * (1.0f / (float)SE_STEP);
        se.step = SE_STEP - 1u;
    }
    se.d += se.dv;
    if (heads & 1u)
        sum += se_head(se.d);
    if (heads & 2u)
        sum += se_head(se.d * SE_HEAD2);
    if (heads & 4u)
        sum += se_head(se.d * SE_HEAD3);
    rec = SE_SAT * fm_tanhf((x + sum * SE_FB * se.intensity) * (1.0f / SE_SAT));
    se.quiet = fm_fabsf(rec) < 0.5f ? se.quiet + 1u : 0u;
    se_tape[se.w] = (int16_t)(rec + (rec < 0.0f ? -0.5f : 0.5f));
    se.w = se.w + 1u >= SE_LEN ? 0u : se.w + 1u;
    return sum * 3.0f * se.echo * se.echo;   /* ECHO VOLUME: 5 puts a head 2.4 dB under the dry (a tone's, measured) */
}
