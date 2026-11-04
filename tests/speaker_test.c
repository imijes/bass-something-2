/* SPDX-License-Identifier: MPL-2.0 */
/* bs2_spk.c on its own: the mono sum, headphones left alone, OFF, BASS+'s harmonics and headroom, no clicks.
 *   cc -std=c99 -Wall -O1 bs2/tests/speaker_test.c -lm -o /tmp/speaker_test && /tmp/speaker_test */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../bs2_spk.c"

#define N 44100
#define FS_I 8388608.0
static int fails;
static int32_t b[2 * N];
static void check(const char *what, int ok, double v)
{
    printf("%s %-54s %.5f\n", ok ? "  ok" : "FAIL", what, v);
    fails += !ok;
}
static void tone(double hz, double l, double r)
{
    for (int i = 0; i < N; i++) {
        double s = sin(2.0 * M_PI * hz * i / 44100.0);
        b[2 * i] = (int32_t)(s * l * FS_I);
        b[2 * i + 1] = (int32_t)(s * r * FS_I);
    }
}
static void run(spk_t *z, uint32_t mode, int jack)   /* in blocks of 256, as rendered */
{
    for (uint32_t i = 0; i < N; i += 256u)
        spk_process(z, b + 2u * i, N - i < 256u ? N - i : 256u, mode, jack);
}
static double peak(int ch, int from)
{
    double p = 0.0;
    for (int i = from; i < N; i++) {
        double v = fabs(b[2 * i + ch] / FS_I);
        if (v > p)
            p = v;
    }
    return p;
}
static double at(double hz, int from)   /* the level of hz in the left channel */
{
    double c = 0.0, s = 0.0;
    int n = N - from;
    for (int i = from; i < N; i++) {
        double t = 2.0 * M_PI * hz * i / 44100.0, v = b[2 * i] / FS_I;
        c += v * cos(t);
        s += v * sin(t);
    }
    return 2.0 * sqrt(c * c + s * s) / n;
}
int main(void)
{
    spk_t z;
    memset(&z, 0, sizeof z);
    tone(440, 0.5, 0.0);
    run(&z, SPK_FLAT, 0);
    check("speaker, left alone in: on the left at half", fabs(peak(0, 2000) - 0.25) < 0.002, peak(0, 2000));
    check("speaker, left alone in: the same on the right", fabs(peak(1, 2000) - 0.25) < 0.002, peak(1, 2000));
    memset(&z, 0, sizeof z);
    tone(440, 0.0, 0.5);
    run(&z, SPK_FLAT, 0);
    check("speaker, right alone in: reaches the left (the speaker)", fabs(peak(0, 2000) - 0.25) < 0.002, peak(0, 2000));
    memset(&z, 0, sizeof z);
    tone(440, 0.5, 0.5);
    run(&z, SPK_FLAT, 0);
    check("speaker, the same both sides (the synth): level kept", fabs(peak(0, 2000) - 0.5) < 0.002, peak(0, 2000));

    for (uint32_t m = SPK_FLAT; m < NSPK; m++) {
        static int32_t in[2 * N];
        int same = 1;
        memset(&z, 0, sizeof z);
        tone(100, 0.5, 0.2);
        memcpy(in, b, sizeof in);
        run(&z, m, 1);
        for (int i = 0; i < 2 * N; i++)
            same &= b[i] == in[i];
        check("headphones: stereo, bit for bit, whatever SPEAKER is", same, m);
    }

    memset(&z, 0, sizeof z);
    tone(440, 0.8, 0.8);
    run(&z, SPK_OFF, 0);
    check("OFF, speaker: silent", peak(0, 0) == 0.0 && peak(1, 0) == 0.0, peak(0, 0));

    memset(&z, 0, sizeof z);
    tone(55, 0.5, 0.5);
    run(&z, SPK_FLAT, 0);
    {
        const double f_fund = at(55, 22050), f_h2 = at(220, 22050), f_h3 = at(165, 22050);
        double b_fund, b_h3, b_h4;
        memset(&z, 0, sizeof z);
        tone(55, 0.5, 0.5);
        run(&z, SPK_BASS, 0);
        b_fund = at(55, 22050), b_h3 = at(165, 22050), b_h4 = at(220, 22050);
        printf("     55 Hz at 0.5: FLAT fund %.3f 165 %.4f 220 %.4f; BASS+ fund %.3f 165 %.3f 220 %.3f\n", f_fund, f_h3,
               f_h2, b_fund, b_h3, b_h4);
        check("BASS+: harmonics a speaker can play, from a 55 Hz sine", b_h3 > 0.03 && b_h4 > 0.02, b_h3);
        check("BASS+: the 55 Hz itself cut", b_fund < f_fund * 0.7, b_fund);
    }
    {   /* the worst: full scale, at each bass pitch, sine and square (Felucca #180: a kick overloading) */
        double worst = 0.0;
        static const double HZ[] = {30, 41, 55, 82, 110, 150, 220, 440};
        for (unsigned h = 0; h < sizeof HZ / sizeof HZ[0]; h++)
            for (int sq = 0; sq < 2; sq++) {
                memset(&z, 0, sizeof z);
                tone(HZ[h], 1.0, 1.0);
                if (sq)
                    for (int i = 0; i < 2 * N; i++)
                        b[i] = b[i] < 0 ? -8388607 : 8388607;
                run(&z, SPK_BASS, 0);
                if (peak(0, 0) > worst)
                    worst = peak(0, 0);
            }
        check("BASS+: under full scale at full scale in", worst < 1.0, worst);
    }
    {   /* a decaying kick: what it adds at most, and it dies away to nothing */
        memset(&z, 0, sizeof z);
        for (int i = 0; i < N; i++) {
            const double t = i / 44100.0, e = exp(-t * 12.0), s = e * sin(2.0 * M_PI * (50.0 * t + 1.5 * (1.0 - exp(-t * 40.0))));
            b[2 * i] = b[2 * i + 1] = (int32_t)(s * 0.9 * FS_I);
        }
        run(&z, SPK_BASS, 0);
        check("BASS+: a full kick stays under full scale", peak(0, 0) < 1.0, peak(0, 0));
        memset(b, 0, sizeof b);
        run(&z, SPK_BASS, 0);
        check("BASS+: silence after it stays silent", peak(0, 22050) < 1e-5, peak(0, 22050));
        check("BASS+: no NaN, no stuck state", z.env == z.env && z.env < 1e-3f, z.env);
    }
    {   /* no click: OFF, BASS+ and the jack changing mid-tone */
        double step = 0.0;
        memset(&z, 0, sizeof z);
        tone(220, 0.5, 0.5);
        for (uint32_t i = 0; i < N; i += 256u) {
            const uint32_t mode = i < 11000u ? SPK_FLAT : i < 22000u ? SPK_BASS : i < 33000u ? SPK_OFF : SPK_FLAT;
            spk_process(&z, b + 2u * i, N - i < 256u ? N - i : 256u, mode, 0);
        }
        for (int i = 1; i < N; i++) {
            const double d = fabs((b[2 * i] - b[2 * i - 2]) / FS_I);
            if (d > step)
                step = d;
        }
        check("a change of SPEAKER: no click (largest step a sample)", step < 0.04, step);
    }
    {   /* a contact's bounce is not a plug: under 46 ms ignored; held, it takes */
        int32_t q[512];
        memset(&z, 0, sizeof z);
        memset(q, 0, sizeof q);
        spk_process(&z, q, 256, SPK_FLAT, 0);
        for (int i = 0; i < 4; i++)
            spk_process(&z, q, 256, SPK_FLAT, 1);
        spk_process(&z, q, 256, SPK_FLAT, 0);
        check("the jack: a bounce ignored", z.jack == 0, z.jack);
        for (int i = 0; i < 8; i++)
            spk_process(&z, q, 256, SPK_FLAT, 1);
        check("the jack: held, it takes", z.jack == 1, z.jack);
    }
    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}
