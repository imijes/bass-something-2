/* The Space Echo (bs2_se.c) on its own, off the device: the reference tests' stimuli through it, to WAVs, for
 * comparing with a reference (docs/RE201_ROADMAP.md, Phase 0's tests by number). From bs2/:
 *   cc -O2 -I../include -o build/host/se_sim tools/se_sim.c && build/host/se_sim OUTDIR */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "fastmath.h"
#define BS2_RATE 44100.0f
#define PLAT_POOL
#include "../bs2_se.c"

#define SR 44100u
#define FS 32767.0f
static float buf[SR * 32];

static void wav(const char *dir, const char *name, const float *x, uint32_t n)
{
    char path[512];
    FILE *f;
    uint32_t i, v;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite("RIFF", 1, 4, f); v = 36 + n * 3; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    { uint16_t h[2] = {1, 1}; fwrite(h, 2, 2, f); }
    v = SR; fwrite(&v, 4, 1, f); v = SR * 3; fwrite(&v, 4, 1, f);
    { uint16_t h[2] = {3, 24}; fwrite(h, 2, 2, f); }
    fwrite("data", 1, 4, f); v = n * 3; fwrite(&v, 4, 1, f);
    for (i = 0; i < n; i++) {
        float s = x[i] < -1.0f ? -1.0f : x[i] > 1.0f ? 1.0f : x[i];
        int32_t q = (int32_t)lrintf(s * 8388607.0f);
        uint8_t b[3] = {(uint8_t)q, (uint8_t)(q >> 8), (uint8_t)(q >> 16)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}

/* the knobs, the motor run in from the middle for 6 s (as the reference's renders), then x through it */
static void run(const char *dir, const char *name, uint8_t mode, float rate, float inten, int dry, uint32_t n,
                float (*src)(uint32_t), int glide)
{
    uint32_t i;
    se_init();
    se.mode = (uint8_t)(mode - 1u), se.intensity = inten, se.echo = 0.5f;
    se.rate = glide ? 0.0f : rate, se.d = se_target();
    for (i = 0; i < n; i++) {
        const float t = (float)i / (float)SR, x = src(i);
        if (glide)
            se.rate = t >= 5.0f && t < 15.0f ? 1.0f : 0.0f;
        buf[i] = (se_next(x * FS) / FS) + (dry ? x : 0.0f);
    }
    wav(dir, name, buf, n);
}

static float impulse(uint32_t i) { return i == SR / 2u ? 0.5f : 0.0f; }
static float sine1k(uint32_t i) { return i >= SR / 2u ? 0.125893f * sinf(6.2831853f * 1000.0f * (float)i / SR) : 0.0f; }
static float steps(uint32_t i)               /* 200 Hz, -36..0 dBFS in 3 dB steps: 0.5 s on, 0.25 s off, from 0.5 s */
{
    const float t = (float)i / SR - 0.5f;
    const int k = (int)floorf(t / 0.75f);
    if (t < 0.0f || k > 12 || t - 0.75f * (float)k >= 0.5f)
        return 0.0f;
    return powf(10.0f, (float)(-36 + 3 * k) / 20.0f) * sinf(6.2831853f * 200.0f * (float)i / SR);
}

int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : ".";
    run(d, "SE 01 Heads slow", 11, 0.0f, 0.0f, 1, SR * 8, impulse, 0);
    run(d, "SE 02 Heads mid", 11, 0.5f, 0.0f, 1, SR * 8, impulse, 0);
    run(d, "SE 03 Heads fast", 11, 1.0f, 0.0f, 1, SR * 8, impulse, 0);
    run(d, "SE 08 Drive normal", 1, 0.5f, 0.0f, 0, SR * 12, steps, 0);
    run(d, "SE 10 Intensity 3", 1, 1.0f, 0.3f, 0, SR * 18, impulse, 0);
    run(d, "SE 11 Intensity 5", 1, 1.0f, 0.5f, 0, SR * 18, impulse, 0);
    run(d, "SE 12 Intensity 6", 1, 1.0f, 0.6f, 0, SR * 18, impulse, 0);
    run(d, "SE 13 Intensity 7", 1, 1.0f, 0.7f, 0, SR * 18, impulse, 0);
    run(d, "SE 14 Runaway", 1, 0.5f, 0.85f, 0, SR * 18, impulse, 0);
    run(d, "SE 19 Motor glide", 1, 0.0f, 0.0f, 0, SR * 27, sine1k, 1);
    printf("wrote the SE tests to %s\n", d);
    return 0;
}
