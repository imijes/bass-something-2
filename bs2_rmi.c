/* The RMI Electra-piano 368X, the third instrument (rmi_*): 68 keys from F1, each a tone generator keyed by its own
 * envelope, the stops their tone. Its tables (rmi_tab.h, made by a local tool, not in the repository): per stop and
 * half-octave band its cycles (the harpsichord's two, crossfaded from its peak to 0.3 s after), per key its
 * envelope's first 0.3 s, the slope after it and its level. The rest of the tail is the room's (bs2_fx.c), as the
 * drums': the note fades a little faster than the recording and its send fills it in. Polyphonic: RMI_NV voices, the
 * oldest taken for a new key. Its panel (RMI PANEL, kept with the GLO settings): the voice (its tablets down), ORGAN
 * MODE (the stops keyed with its own envelopes: no percussive decay, a slow one), ACCENTER (its keying's thump at
 * each note's start, under the harpsichord and the lute: the low keys', to A#2, and the rest's, as recorded), BASS (a
 * boost about 98 Hz, as its 1.2 H / 2.2 uF tank) and VOL. MIDI channel 3 plays it too, CC 7 its volume, CC 64 the
 * sustain pedal (CC 20/21/22: PIANO, HARPSI, ORGAN MODE, the tests') */
#include "rmi_tab.h"

#define RMI_NV 8u                            /* voices */
#define RMI_LO 29u                           /* its lowest key: F1 */
#define RMI_NK 68u
#define RMI_FADE 1.25f                       /* the fade after the body, against the recording's (the room the rest) */
#define RMI_SEND 0.5f                        /* into the room */
#define RMI_REL 0.9993f                      /* a key let go: its level's step a sample (about 60 ms to -40 dB) */
#define RMI_XF 0.3f                          /* the harpsichord's crossfade, s from its peak */
#define RMI_PP 0.2f                          /* a PP tablet's level against its stop's (pianissimo: the manual's "the
                                              * same but softer", its path's resistor; not measurable here: -14 dB, by
                                              * ear, -8 dB too little) */
#define RMI_VEL 0.7f                         /* the keys' touch (GLO CONTROLS' Velocity curve): a note's level from
                                              * 1 - RMI_VEL (softest) to 1 (hardest); the 368X had none */
#define RMI_LUTE 2.0f                        /* LUTE's level: the harpsichord's cycles through its low pass (by ear: about the harpsichord's) */
#define RMI_BASS_DB 12.0f                    /* the BASS slider's reach, dB each way from the middle */
#define RMI_LUTE_K 0.17f                     /* its low pass's step (two one-poles, ~1.3 kHz: the schematic's 100 mH
                                              * into 820 ohms) */
typedef struct {
    uint8_t key;                             /* 0..RMI_NK-1, or RMI_NK: free */
    uint8_t held;                            /* its key down (or the sustain pedal holding it) */
    uint32_t ph, inc;                        /* its cycle's phase and step, a whole cycle 2^32 */
    uint32_t t, age;                         /* samples since its key went down; its order among the voices */
    float rel;                               /* 1 while held, then falling */
    float vg;                                /* its level by its key's touch (RMI_VEL) */
    float g[3], dg[3];                       /* this block's level for the piano's, the harpsichord's and the lute's
                                              * (the harpsichord's cycles, low-passed after) layers, its step */
} rmi_voice_t;
static struct {
    rmi_voice_t v[RMI_NV];
    uint8_t sustain, cc_p, cc_h;             /* (the pedal; CC 20 and 21's tablets, the tests') */
    uint8_t bass;                            /* the BASS its boost was made for (0xFF: none yet) */
    uint32_t age;
    float b0, b1, b2, a1, a2, x1, x2, y1, y2;   /* the boost: a peaking biquad's, its state */
    float l1, l2;                            /* LUTE's low pass, on every voice's lute layer at once (as the 368X's
                                              * tone changer, after the keys) */
} rmi = {.bass = 0xFFu};

static void rmi_init(void)
{
    uint32_t i;
    for (i = 0; i < RMI_NV; i++)
        rmi.v[i].key = RMI_NK;
}

static uint32_t rmi_band(uint32_t k) { return k * RMI_NB / 72u; }   /* key k's half-octave band (F1's 0) */

/* key k's level at t samples, from its envelope (rmi_key_t): its body, then its slope (RMI_FADE of it); 0 dB its
 * loudest */
static float rmi_env(const rmi_key_t *e, uint32_t t)
{
    const float s = (float)t * (100.0f / BS2_RATE);   /* (10 ms steps) */
    float db;
    if (s < (float)(RMI_BODY - 1u)) {
        const uint32_t i = (uint32_t)s;
        const float f = s - (float)i;
        db = -0.5f * ((float)e->body[i] * (1.0f - f) + (float)e->body[i + 1u] * f);
    } else {
        db = -0.5f * (float)e->body[RMI_BODY - 1u] + RMI_FADE * 0.5f * (float)e->slope * (s - (float)(RMI_BODY - 1u)) * 0.01f;
    }
    db -= 0.5f * (float)e->level;
    return db < -90.0f ? 0.0f : fm_exp2f(db * (1.0f / 6.0206f));
}

/* a key (MIDI note) down at vel (0: up), from MIDI in or the keys (audio interrupt) */
static void rmi_note(uint32_t note, uint32_t vel)
{
    uint32_t i, k = note - RMI_LO;
    rmi_voice_t *v = 0;
    if (note == NOTE_ALL_OFF) {              /* (the octave changed: no key's note-off would find its note) */
        for (i = 0; i < RMI_NV; i++)
            if (rmi.v[i].held == 1u)
                rmi.v[i].held = rmi.sustain ? 2u : 0u;
        return;
    }
    if (note < RMI_LO || k >= RMI_NK)
        return;
    if (!vel) {
        for (i = 0; i < RMI_NV; i++)
            if (rmi.v[i].key == k && rmi.v[i].held)
                rmi.v[i].held = rmi.sustain ? 2u : 0u;   /* (2: held by the pedal) */
        return;
    }
    for (i = 0; i < RMI_NV && !v; i++)       /* its own voice again, else a free one, else the oldest */
        if (rmi.v[i].key == k)
            v = &rmi.v[i];
    for (i = 0; i < RMI_NV && !v; i++)
        if (rmi.v[i].key == RMI_NK)
            v = &rmi.v[i];
    if (!v) {
        v = &rmi.v[0];
        for (i = 1; i < RMI_NV; i++)
            if (rmi.v[i].age < v->age)
                v = &rmi.v[i];
    }
    v->key = (uint8_t)k;
    v->held = 1;
    v->ph = 0;
    v->inc = (uint32_t)(fm_exp2f(((float)note - 69.0f) * (1.0f / 12.0f)) * 440.0f * (4294967296.0f / BS2_RATE));
    v->t = 0;
    v->age = ++rmi.age;
    v->rel = 1.0f;
    v->vg = 1.0f - RMI_VEL * (1.0f - (float)vel * (1.0f / 127.0f));
    v->g[0] = v->g[1] = v->g[2] = v->dg[0] = v->dg[1] = v->dg[2] = 0.0f;
}

static void rmi_cc(uint32_t c, uint32_t v)   /* (until its pages: its stops by CC, the sustain pedal) */
{
    uint32_t i;
    if (c == 20u || c == 21u) {              /* (PIANO, HARPSI: their tablets) */
        const uint32_t b = 1u << (c == 20u ? RT_PIANO : RT_HARPSI);
        set.rmi_tabs = (uint8_t)(v >= 64u ? set.rmi_tabs | b : set.rmi_tabs & ~b);
    }
    if (c == 22u)
        set.rmi_organ = v >= 64u;
    if (c == 7u)                             /* (its volume, the faceplate's fader: kept with the settings) */
        set.rvol_off = (uint8_t)(127u - v);
    if (c == 64u) {                          /* the sustain pedal: up, what it held let go */
        rmi.sustain = v >= 64u;
        for (i = 0; !rmi.sustain && i < RMI_NV; i++)
            if (rmi.v[i].held == 2u)
                rmi.v[i].held = 0;
    }
}

/* a block's start (audio interrupt): each voice's levels for the block, a step a sample to them */
static void rmi_block(void)
{
    const uint32_t tb = set.rmi_tabs;        /* the tone tablets down: each layer's level, a PP's softer */
    const float gv[3] = {(float)(tb >> RT_PIANO & 1u) + RMI_PP * (float)(tb >> RT_PIANO_PP & 1u),
                         (float)(tb >> RT_HARPSI & 1u) + RMI_PP * (float)(tb >> RT_HARPSI_PP & 1u),
                         RMI_LUTE * (float)(tb >> RT_LUTE & 1u)};
    uint32_t i;
    if (set.rmi_bass != rmi.bass) {          /* the BASS slider moved: its boost again (RBJ's peaking EQ, Q 0.9) */
        const float a = fm_exp2f(((float)set.rmi_bass - 64.0f) * (RMI_BASS_DB / 64.0f / 40.0f) * 3.321928f);
        const float w = 2.0f * FM_PI * 98.0f / BS2_RATE, al = fm_sinf(w) / 1.8f, cw = fm_cosf(w), a0 = 1.0f + al / a;
        rmi.bass = set.rmi_bass;
        rmi.b0 = (1.0f + al * a) / a0, rmi.b1 = -2.0f * cw / a0, rmi.b2 = (1.0f - al * a) / a0;
        rmi.a1 = -2.0f * cw / a0, rmi.a2 = (1.0f - al / a) / a0;
    }
    for (i = 0; i < RMI_NV; i++) {
        rmi_voice_t *v = &rmi.v[i];
        float to[3];
        uint32_t s;
        if (v->key == RMI_NK)
            continue;
        for (s = 0; s < 3u; s++) {           /* (each layer's envelope: its own, or ORGAN MODE's; the lute's the
                                              * harpsichord's, its cycles' keying) */
            const rmi_key_t *e = set.rmi_organ ? (s ? &RMI_K_HORGAN[v->key] : &RMI_K_ORGAN[v->key])
                                 : s ? &RMI_K_HARPSI[v->key] : &RMI_K_PIANO[v->key];
            to[s] = gv[s] > 0.0f ? gv[s] * v->vg * rmi_env(e, v->t + APP_BLOCK) : 0.0f;
            v->dg[s] = (to[s] - v->g[s]) * (1.0f / APP_BLOCK);
        }
        if (to[0] + to[1] + to[2] < 1e-5f && v->g[0] + v->g[1] + v->g[2] < 1e-5f)   /* (gone quiet: free) */
            v->key = RMI_NK;
    }
}

/* the instrument's next sample at level 1 (rmi_job's: on the second core when there is one, so it touches nothing
 * the first core's render does: its room send and the FX bus's wake are the render's, from its sample and *woke) */
static inline float rmi_next(uint32_t *woke)
{
    float s = 0.0f, lute = 0.0f;
    uint32_t i;
    for (i = 0; i < RMI_NV; i++) {
        rmi_voice_t *v = &rmi.v[i];
        const uint32_t b = rmi_band(v->key);
        float o = 0.0f;
        if (v->key == RMI_NK)
            continue;
        *woke = 1;
        if (v->g[0] > 0.0f || v->dg[0] > 0.0f) {   /* the piano: its band's cycle */
            const uint32_t n = RMI_PIANO[b].n;
            const float p = (float)(v->ph >> 8) * ((float)n * (1.0f / 16777216.0f));
            const uint32_t j = (uint32_t)p;
            const float f = p - (float)j;
            const int8_t *d = RMI_PIANO[b].d[0];
            o += v->g[0] * ((float)d[j & (n - 1u)] * (1.0f - f) + (float)d[(j + 1u) & (n - 1u)] * f);
        }
        if (v->g[1] > 0.0f || v->dg[1] > 0.0f || v->g[2] > 0.0f || v->dg[2] > 0.0f) {   /* the harpsichord: its two,
                                              * crossfaded from its peak on (the lute's layer too, low-passed after) */
            const uint32_t n = RMI_HARPSI[b].n;
            const float p = (float)(v->ph >> 8) * ((float)n * (1.0f / 16777216.0f));
            const uint32_t j = (uint32_t)p, j1 = (j + 1u) & (n - 1u);
            const float f = p - (float)j;
            const float w = fm_clampf(((float)v->t * (1.0f / BS2_RATE) - 0.01f * (float)RMI_K_HARPSI[v->key].peak)
                                      * (1.0f / RMI_XF), 0.0f, 1.0f);
            const int8_t *d0 = RMI_HARPSI[b].d[0], *d1 = RMI_HARPSI[b].d[1];
            const float a = (float)d0[j & (n - 1u)] * (1.0f - f) + (float)d0[j1] * f;
            const float c = (float)d1[j & (n - 1u)] * (1.0f - f) + (float)d1[j1] * f;
            o += v->g[1] * (a + (c - a) * w);
            lute += v->g[2] * v->rel * (a + (c - a) * w);
        }
        if (set.rmi_accent && set.rmi_tabs & (1u << RT_HARPSI | 1u << RT_HARPSI_PP | 1u << RT_LUTE)) {   /* ACCENTER: its
                                              * thump, under the harpsichord or the lute, at the note's start */
            const uint32_t lo = v->key + RMI_LO <= 46u ? 0u : 1u, j = v->t >> 3;
            if (j + 1u < RMI_ACC[lo].n) {
                const float f = (float)(v->t & 7u) * 0.125f;
                o += 127.0f * RMI_ACC[lo].lvl * v->vg
                     * ((float)RMI_ACC[lo].d[j] * (1.0f - f) + (float)RMI_ACC[lo].d[j + 1u] * f) * (1.0f / 127.0f);
            }
        }
        v->g[0] += v->dg[0];
        v->g[1] += v->dg[1];
        v->g[2] += v->dg[2];
        v->ph += v->inc;
        v->t++;
        if (!v->held) {                      /* (let go: falling) */
            v->rel *= RMI_REL;
            if (v->rel < 1e-4f)
                v->key = RMI_NK;
        }
        s += o * v->rel;
    }
    rmi.l1 += (lute - rmi.l1) * RMI_LUTE_K;   /* LUTE: its low pass, every voice's lute layer at once */
    rmi.l2 += (rmi.l1 - rmi.l2) * RMI_LUTE_K;
    s = (s + rmi.l2) * (1.0f / 127.0f);
    {                                        /* BASS: its boost */
        const float y = rmi.b0 * s + rmi.b1 * rmi.x1 + rmi.b2 * rmi.x2 - rmi.a1 * rmi.y1 - rmi.a2 * rmi.y2;
        rmi.x2 = rmi.x1, rmi.x1 = s, rmi.y2 = rmi.y1, rmi.y1 = y;
        s = y;
    }
    return s;
}

/* a block of the instrument (the render's job, from after rmi_block: on the second core while the first renders the
 * bass and the drums, else on the first before them), into rmi_buf at level 1 */
static float rmi_buf[APP_BLOCK];
static volatile uint32_t rmi_woke;           /* a voice sounded in it (the FX bus woken for it) */
static void rmi_job(uint32_t n)
{
    uint32_t i, w = 0;
    for (i = 0; i < n; i++)
        rmi_buf[i] = rmi_next(&w);
    rmi_woke = w;
}
