/* SPDX-License-Identifier: MPL-2.0 */
/* What goes to the FM-1's speaker. One DAC feeds the speaker and the headphones, and the speaker plays its left channel
 * alone: with nothing in the headphone jack (the jack cuts the speaker in hardware, and PB5 tells: fm1_jack_in) that
 * channel carries left and right summed, as SPEAKER (GLO SYS) has it: FLAT; BASS+, the bass the small speaker cannot play
 * heard through its harmonics; OFF. Headphones are left as they are, in stereo. Called after the USB audio input's tap and
 * Bluetooth's feed (APP_SPEAKER), so neither hears any of it.
 *
 * BASS+: the band under ~150 Hz, scaled by its own level to 0..1 so its loudness does not change the mix of harmonics,
 * goes through Chebyshev polynomials (T2, T3, T4 of a sine are its 2nd, 3rd and 4th harmonics), back at its level, and is
 * band-limited to ~180..900 Hz; the lows under ~70 Hz are taken out of the rest, to leave the speaker's travel and the
 * headroom for what it can play. A soft ceiling keeps it under full scale. */
#include <stdint.h>

enum { SPK_FLAT, SPK_BASS, SPK_OFF, NSPK };

#define SPK_FS 8388608.0f                /* the render's full scale */
#define SPK_RAMP 0.0045f                 /* a share a sample: changes over about 5 ms, no click */
#define SPK_LP1 0.0211f                  /* one-poles at 44.1 kHz: ~150 Hz, the bass band */
#define SPK_HP1 0.0975f                  /* ~180 Hz, the harmonics' floor, at a quarter of the rate */
#define SPK_LP2 0.401f                   /* ~900 Hz, their top, the same */
#define SPK_HP2 0.00993f                 /* ~70 Hz, out of the rest */
#define SPK_HARM 1.0f                    /* the harmonics' level */
#define SPK_CEIL 0.7f                    /* the soft ceiling starts here, and stays under 1 */
#define SPK_JACK_FRAMES 2048u            /* the jack's contacts settled: unchanged about 46 ms */

typedef struct {
    float a;                             /* 1: the speaker's signal on both channels; 0: the stereo as it came */
    float on;                            /* 0: SPEAKER OFF */
    float wet;                           /* 1: BASS+ */
    float lp[2], env, hp[2], top, now, step, cut[2];
    uint32_t ph;
    uint32_t held;                       /* frames the jack has read other than jack */
    int jack;                            /* the jack, settled */
    int started;
} spk_t;

static float spk_bass(spk_t *z, float m)   /* one sample, full scale 1 */
{
    float y, abs_y;
    z->lp[0] += (m - z->lp[0]) * SPK_LP1;
    z->lp[1] += (z->lp[0] - z->lp[1]) * SPK_LP1;
    if (!(z->ph++ & 3u)) {               /* the harmonics stay under 1 kHz: every 4th sample, drawn straight between */
        const float low = z->lp[1];
        float u = low < 0.0f ? -low : low, u2, h = 0.0f;
        z->env = u > z->env ? z->env + (u - z->env) * 0.0776f : z->env * 0.998f;
        if (z->env > 1e-5f) {
            u = low / z->env;
            u = u > 1.0f ? 1.0f : u < -1.0f ? -1.0f : u;
            u2 = u * u;
            h = z->env * (0.5f * (2.0f * u2 - 1.0f) + 0.35f * u * (4.0f * u2 - 3.0f)
                          + 0.2f * (8.0f * u2 * u2 - 8.0f * u2 + 1.0f));
        } else {
            z->env = 0.0f;
        }
        z->hp[0] += (h - z->hp[0]) * SPK_HP1;
        h -= z->hp[0];
        z->hp[1] += (h - z->hp[1]) * SPK_HP1;
        h -= z->hp[1];
        z->top += (h - z->top) * SPK_LP2;
        z->step = (z->top - z->now) * 0.25f;
    }
    z->now += z->step;
    z->cut[0] += (m - z->cut[0]) * SPK_HP2;
    y = m - z->cut[0];
    z->cut[1] += (y - z->cut[1]) * SPK_HP2;
    y = y - z->cut[1] + z->now * SPK_HARM;
    abs_y = y < 0.0f ? -y : y;
    if (abs_y > SPK_CEIL) {              /* over the ceiling: towards 1, never reaching it */
        const float x = (abs_y - SPK_CEIL) / (1.0f - SPK_CEIL);
        abs_y = SPK_CEIL + (1.0f - SPK_CEIL) * x / (1.0f + x);
        y = y < 0.0f ? -abs_y : abs_y;
    }
    return y;
}

/* n stereo frames in place; mode SPK_*; jack: the raw reading, 1 something in the headphone jack */
static void spk_process(spk_t *z, int32_t *lr, uint32_t n, uint32_t mode, int jack)
{
    const float wet_to = mode == SPK_BASS ? 1.0f : 0.0f, on_to = mode == SPK_OFF ? 0.0f : 1.0f;
    float a_to;
    uint32_t i;
    if (!z->started) {                   /* (as they are from the first frame) */
        z->started = 1;
        z->jack = jack;
        z->a = jack ? 0.0f : 1.0f;
        z->wet = wet_to;
        z->on = on_to;
    }
    if (jack == z->jack) {
        z->held = 0;
    } else if ((z->held += n) >= SPK_JACK_FRAMES) {
        z->jack = jack;
        z->held = 0;
    }
    a_to = z->jack ? 0.0f : 1.0f;
    if (z->jack && z->a == 0.0f)         /* (headphones, settled: nothing to do) */
        return;
    for (i = 0; i < n; i++) {
        const float l = (float)lr[2u * i], r = (float)lr[2u * i + 1u];
        float s = (l + r) * 0.5f;
        z->a += (a_to - z->a) * SPK_RAMP;
        z->on += (on_to - z->on) * SPK_RAMP;
        z->wet += (wet_to - z->wet) * SPK_RAMP;
        if (z->wet > 0.001f)
            s += (spk_bass(z, s * (1.0f / SPK_FS)) * SPK_FS - s) * z->wet;
        s *= z->on;
        lr[2u * i] = (int32_t)(l + (s - l) * z->a);
        lr[2u * i + 1u] = (int32_t)(r + (s - r) * z->a);
    }
    if (a_to == 0.0f && z->a < 0.0005f)  /* (the ramps' tails: done) */
        z->a = 0.0f;
    if (on_to == 0.0f && z->on < 0.0005f)
        z->on = 0.0f;
    if (wet_to == 0.0f && z->wet < 0.001f) {
        uint32_t k;
        z->wet = z->env = z->top = z->now = z->step = 0.0f;
        for (k = 0; k < 2u; k++)
            z->lp[k] = z->hp[k] = z->cut[k] = 0.0f;
    }
}
