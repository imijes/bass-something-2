/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 John Brown */
/* RND: Bass Something 2 as a companion to Cyma Forma's RND Synth (firmware 1.4): a keeper of its patches and, on
 * the RND pages (GLO held), a remote for it. They don't touch Bass Something 2's sound. The one exception is the
 * tempo: a new seed's tempo becomes the FM-1's TEMPO, so the arp, the sequencer and SEND CLK follow the synth's new
 * direction.
 *
 * A patch's id is its seed (32 bits) and its lock code: what is held across new patches (type, root and its octave,
 * scale, the number of tracks, each track's engine), as RND pixels (pixels.cymaforma.com) counts it. NSEEDS slots keep
 * ids through power-off, in a table of their own (OBJ_SEEDS, apart from the patch bank). Recalling one sends the
 * locks, then the seed, a step every RQ_MS (notes and CCs leave the FM-1 after any SysEx queued before them, but a
 * SysEx frame can overtake a note still waiting: spaced out, they arrive in order).
 *
 * The synth's SysEx, F0 6F 62 78 op ... F7, numbers in 7-bit parts, low first. From it (a patch: 10, 20, 21, 25, 26,
 * 28, then 22 and 24 for each track playing; it answers only while it is clocked):
 *   10 s0..s4          the seed            21 type tl th root scale   the patch (tempo tl | th << 7)
 *   20                 the tracks follow   22 t role ? name.. 00      track t's engine
 *   23 major minor     its firmware        24 t send                  track t's reverb as rolled
 *   25 clk bl bh       clock source, BPM   26 c0..c4                  the lock code
 *   28 v1..v4 r1..r4 f the tracks' levels and reverb sends (64: as rolled)   29 era
 * To it: 18 all of the above, 19 its version, 10 a seed, 1A type (7F: free), 1B t name 00 (an engine held),
 * 1C every lock off, 1F tracks (7F: free), 27 0 / 1 root / scale free. A root held: a note on channel 1
 * (60 + 12 octave + root); a scale: CC 9 on channel 1 (scale + 1). Track t's level CC 7, reverb CC 91, on channel
 * t + 2. F0 7D 42 53 32 02 F7 (a computer's) sends the slot's id. Part of app.c (included after slot_text): main loop
 * only, nothing in the audio path */
#define RND_EMPTY 0xFFFFFFFFu                /* no seed */
#define SEEDS_MAGIC 0x32444E52u              /* "RND2": the id table; another magic (or size) starts it empty */
#define NSEEDS 32u
#define RQ_MS 8u                             /* a recall's steps apart */
#define NENG 8u
#define NRSCALE 20u
#define RND_FREE (-1)                        /* a lock's setting: not held */
typedef struct {
    uint32_t magic;
    uint32_t seed[NSEEDS];                   /* each slot's, RND_EMPTY: none */
    uint32_t code[NSEEDS];                   /* its lock code (0: nothing held) */
} seeds_t;
_Static_assert(sizeof(seeds_t) <= PLAT_STORE_MAX, "the seed table fits a store object");
typedef struct {                             /* a lock code, apart: RND_FREE or the setting */
    int8_t type, root, oct, scale, tracks, eng[4];
} rlock_t;
static seeds_t seeds PLAT_POOL;
static const uint8_t RND_ID[3] = {0x6F, 0x62, 0x78};
static const char *const RND_ENG[NENG] = {"Subtractive", "FM", "Acid (303)", "Noise", "Speech", "Plucked String",
                                          "SuperSaw", "Harmonic"};   /* (its names, as 22 and 1B spell them) */
static const char *const RND_ENG_S[NENG] = {"SUBTR", "FM", "ACID", "NOISE", "SPEECH", "PLUCK", "SSAW", "HARM"};
static const char *const RND_TYPE[3] = {"SEQUENCE", "DRONE", "JINGLES"};
static const char *const RND_ROOT[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const RND_SCALE[NRSCALE] = {"MAJOR", "NAT MINOR", "HARM MINOR", "BLUES", "MAJ PENTA", "MIN PENTA",
                                               "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN", "LOCRIAN", "WHOLE TONE",
                                               "DBL HARMONIC", "HUNGARIAN MIN", "PHRYG DOM", "HIRAJOSHI", "IN SEN",
                                               "PROMETHEUS", "OCTATONIC", "PERSIAN"};
static struct {                              /* the synth as it last said (0xFF: not said) */
    uint32_t seed, code;                     /* its patch's seed (RND_EMPTY: none yet), its lock code */
    uint32_t heard;                          /* plat_ms of its last frame (0: none yet) */
    uint16_t bpm;                            /* its patch's tempo */
    uint8_t ver[2], type, root, scale, clk, ntr;   /* firmware; the patch; clock source; tracks playing */
    uint8_t eng[4], vol[4], rev[4];          /* each track's engine (RND_ENG), level and reverb send (64: as rolled) */
    uint8_t send[4];                         /* its reverb send as rolled (24: /90) */
} rnd;
static uint8_t rnd_slot;                     /* the slot PRESET picks, 0..NSEEDS-1: the one shown, SAVE's and FX's */
static struct {                              /* a recall under way: its id, the next step, when */
    uint32_t seed, code, at;
    uint8_t step;
} rq = {RND_EMPTY, 0, 0, 0xFF};
#ifdef PLAT_HOST
static uint8_t rnd_tx[64][2];                /* what went to the synth (the tests'): SysEx op or status, first byte */
static uint32_t rnd_ntx;
#endif

static int same(const void *a, const void *b, uint32_t n)   /* (no libc on the device) */
{
    const uint8_t *x = a, *y = b;
    while (n && *x == *y)
        n--, x++, y++;
    return n == 0;
}
static uint32_t d7(const uint8_t *p, uint32_t n)   /* n 7-bit parts, low first */
{
    uint32_t v = 0;
    while (n--)
        v = v << 7 | (p[n] & 0x7Fu);
    return v;
}

/* the lock code, as RND pixels makes it: each setting + 1 (0: free) in mixed radix, the root's octave over it all */
#define LK_SPAN 191766120u
#define LK_MAX 35823060u                     /* (codes from here on: another format, read as nothing held) */
static rlock_t lock_get(uint32_t c)
{
    rlock_t l;
    uint32_t i, o = c / LK_SPAN, e;
    for (i = 0; i < sizeof l; i++)
        ((int8_t *)&l)[i] = RND_FREE;
    l.oct = 0;
    c %= LK_SPAN;
    if (c == 0u || c >= LK_MAX || o > 7u)
        return l;
    l.type = (int8_t)(c % 4u) - 1;
    l.root = (int8_t)(c / 4u % 13u) - 1;
    l.scale = (int8_t)(c / 52u % 21u) - 1;
    l.tracks = (int8_t)(c / 1092u % 5u);
    if (!l.tracks)
        l.tracks = RND_FREE;
    for (e = c / 5460u, i = 4; i--; e /= 9u)
        l.eng[i] = (int8_t)(e % 9u) - 1;
    l.oct = l.root >= 0 && o ? (int8_t)(o - 4u) : 0;
    return l;
}
static uint32_t lock_code(const rlock_t *l)
{
    uint32_t e = 0, i;
    for (i = 0; i < 4u; i++)
        e = e * 9u + (uint32_t)(l->eng[i] + 1);
    return (uint32_t)(l->type + 1) + (uint32_t)(l->root + 1) * 4u + (uint32_t)(l->scale + 1) * 52u +
           (uint32_t)(l->tracks < 0 ? 0 : l->tracks) * 1092u + e * 5460u +
           (l->root >= 0 && l->oct ? (uint32_t)(l->oct + 4) * LK_SPAN : 0u);
}

static void rnd_load(void)                   /* app_init: the table, or an empty one; nothing shown yet */
{
    uint32_t i;
    rnd_slot = 0;
    rnd.seed = RND_EMPTY;
    for (i = 0; i < 4u; i++)
        rnd.eng[i] = rnd.vol[i] = rnd.rev[i] = rnd.send[i] = 0xFF;
    rnd.type = rnd.root = rnd.scale = rnd.clk = 0xFF;
    if (plat_store_load(OBJ_SEEDS, &seeds, sizeof seeds) != (int)sizeof seeds || seeds.magic != SEEDS_MAGIC) {
        seeds.magic = SEEDS_MAGIC;
        for (i = 0; i < NSEEDS; i++)
            seeds.seed[i] = RND_EMPTY, seeds.code[i] = 0;
    }
}

static void seed_text(uint32_t s, char *t)   /* "1A2B3C4D", or "NONE" */
{
    uint32_t i;
    if (s == RND_EMPTY) {
        t[0] = 0;
        text_cat(t, "NONE");
        return;
    }
    for (i = 0; i < 8u; i++)
        t[i] = "0123456789ABCDEF"[s >> (28u - 4u * i) & 15u];
    t[8] = 0;
}
/* the names RND pixels gives an id: the seed as three words of BIP39's list (its bits 31..21, 20..10, 9..0), the lock
 * code after them in base 2048, low first ("VIABLE INJURY CLINIC BRIGHT IMPULSE ABSURD") */
#include "rnd/words.h"                       /* WORD_BITS, WORD_AT (bs2/rnd/make_words.py) */
static uint32_t word_bits(uint32_t *at, uint32_t n)
{
    uint32_t v = 0, i;
    for (i = 0; i < n; i++, (*at)++)
        v |= (uint32_t)(WORD_BITS[*at >> 3] >> (*at & 7u) & 1u) << i;
    return v;
}
static void word_cat(uint32_t w, char *t)    /* word w (0..2047), in capitals, onto t */
{
    uint32_t at = WORD_AT[w / WORD_EVERY], j, n;
    for (j = w % WORD_EVERY; j; j--)
        at += 5u * word_bits(&at, 4);
    n = word_bits(&at, 4);
    while (*t)                               /* (its end: the device has no string.h) */
        t++;
    while (n--)
        *t++ = (char)('A' - 1 + (int)word_bits(&at, 5));
    *t = 0;
}
static void seed_words(uint32_t s, uint32_t code, char *t)   /* (t: 6 x 9 bytes) */
{
    t[0] = 0;
    word_cat(s >> 21, t);
    text_cat(t, " ");
    word_cat(s >> 10 & 2047u, t);
    text_cat(t, " ");
    word_cat(s & 1023u, t);
    for (; code; code >>= 11) {
        text_cat(t, " ");
        word_cat(code & 2047u, t);
    }
}
static void seed_slot_text(char *t)          /* "S01" .. "S32" */
{
    t[0] = 'S';
    slot_n_text(rnd_slot, t + 1);
}

/* to the synth */
static void rnd_tx_log(uint8_t a, uint8_t b)
{
#ifdef PLAT_HOST
    if (rnd_ntx < 64u)
        rnd_tx[rnd_ntx][0] = a, rnd_tx[rnd_ntx][1] = b;
    rnd_ntx++;
#endif
    (void)a, (void)b;
}
static void rnd_frame(uint8_t op, const uint8_t *d, uint32_t n)   /* F0 6F 62 78 op d.. F7 */
{
    uint8_t f[24];
    uint32_t i;
    if (n > sizeof f - 6u)
        return;
    f[0] = 0xF0;
    for (i = 0; i < 3u; i++)
        f[1 + i] = RND_ID[i];
    f[4] = op;
    for (i = 0; i < n; i++)
        f[5 + i] = d[i] & 0x7Fu;
    f[5 + n] = 0xF7;
    plat_sysex_send(f, n + 6u);
    rnd_tx_log(op, n ? d[0] : 0);
}
static void rnd_midi(uint8_t st, uint8_t a, uint8_t b)   /* a channel message, to USB only (where the synth is) */
{
    plat_midi_send((uint32_t)(st >> 4) | (uint32_t)st << 8 | (uint32_t)a << 16 | (uint32_t)b << 24, TO_USB);
    rnd_tx_log(st, a);
}
static void rnd_ask(void)                    /* its version, then everything (answered only while it's clocked) */
{
    rnd_frame(0x19, 0, 0);
    rnd_frame(0x18, 0, 0);
}
static void rnd_engine_out(uint32_t t, int32_t e)   /* track t's engine held (e >= 0) */
{
    uint8_t d[18];
    uint32_t n = 0;
    const char *s = RND_ENG[e];
    d[n++] = (uint8_t)t;
    while (*s && n < 16u)
        d[n++] = (uint8_t)*s++;
    d[n++] = 0;
    rnd_frame(0x1B, d, n);
}
static void rnd_seed_out(uint32_t s)
{
    uint8_t d[5];
    uint32_t i;
    for (i = 0; i < 5u; i++)
        d[i] = (uint8_t)(s >> 7 * i & 0x7Fu);
    rnd_frame(0x10, d, 5);
}
static uint8_t scale_cc(int32_t sc)          /* a scale held, as CC 9's value (before 1.4: spread over 0..127) */
{
    return (uint8_t)(rnd.ver[0] == 1 && rnd.ver[1] < 4 ? ((uint32_t)sc * 128u + 64u) / 20u : (uint32_t)sc + 1u);
}
/* a recall's steps: every lock off, the id's locks, its seed (RQ_SEED); one a call, the free settings' skipped */
enum { RQ_CLEAR, RQ_TYPE, RQ_ROOT, RQ_ROOT_OFF, RQ_SCALE, RQ_TRACKS, RQ_ENG, RQ_SEED = RQ_ENG + 4, RQ_DONE };
static int rq_out(uint32_t step, const rlock_t *l)   /* 1: something sent */
{
    uint8_t v;
    int32_t note = 60 + 12 * l->oct + l->root;
    switch (step) {
    case RQ_CLEAR:
        rnd_frame(0x1C, 0, 0);
        return 1;
    case RQ_TYPE:
        if (l->type < 0)
            return 0;
        v = (uint8_t)l->type;
        rnd_frame(0x1A, &v, 1);
        return 1;
    case RQ_ROOT:
    case RQ_ROOT_OFF:
        if (l->root < 0)
            return 0;
        rnd_midi(step == RQ_ROOT ? 0x90 : 0x80, (uint8_t)note, step == RQ_ROOT ? 100 : 0);
        return 1;
    case RQ_SCALE:
        if (l->scale < 0)
            return 0;
        rnd_midi(0xB0, 9, scale_cc(l->scale));
        return 1;
    case RQ_TRACKS:
        if (l->tracks < 0)
            return 0;
        v = (uint8_t)l->tracks;
        rnd_frame(0x1F, &v, 1);
        return 1;
    case RQ_SEED:
        rnd_seed_out(rq.seed);
        return 1;
    default:
        if (step < RQ_ENG + 4u && l->eng[step - RQ_ENG] >= 0) {
            rnd_engine_out(step - RQ_ENG, l->eng[step - RQ_ENG]);
            return 1;
        }
        return 0;
    }
}
static uint8_t root_on = 0xFF;               /* a root's note sounding (RND PATCH's), let go ROOT_MS on */
static uint32_t root_at;
#define ROOT_MS 60u
static void root_off(void)
{
    if (root_on != 0xFF)
        rnd_midi(0x80, root_on, 0);
    root_on = 0xFF;
}
static void rnd_step(void)                   /* the main loop's: a root's note let go; a recall's next step, when due */
{
    rlock_t l;
    if (root_on != 0xFF && plat_ms() - root_at >= ROOT_MS)
        root_off();
    if (rq.step >= RQ_DONE || plat_ms() - rq.at < RQ_MS)
        return;
    l = lock_get(rq.code);
    while (rq.step < RQ_DONE && !rq_out(rq.step, &l))
        rq.step++;
    rq.step++;
    rq.at = plat_ms();
}
static void rnd_recall(uint32_t s, uint32_t c)   /* the id s, c to the synth (an empty seed: the locks only) */
{
    rq.seed = s;
    rq.code = c;
    rq.step = RQ_CLEAR;
    rq.at = plat_ms() - RQ_MS;
    rnd_step();
}

static void rnd_pick(int32_t e)              /* PRESET on RND SEED: a slot, its id shown (an empty one: kept) */
{
    char t[24], s[24];
    uint32_t n = (uint32_t)clamp((int32_t)rnd_slot + e, 0, NSEEDS - 1);
    if (n == rnd_slot)
        return;
    rnd_slot = (uint8_t)n;
    t[0] = 0;
    text_cat(t, "RND SEED ");
    seed_slot_text(t + 9);
    ui.redraw = 1;
    if (seeds.seed[n] == RND_EMPTY) {
        app_say(t, "EMPTY");
        return;
    }
    seed_text(seeds.seed[n], s);
    if (seeds.code[n])
        text_cat(s, " LOCKED");
    app_say(t, s);
}

static void rnd_store(void)                  /* SAVE on RND SEED: the synth's patch playing into the slot (and the settings) */
{
    char t[24], s[12];
    if (rnd.seed == RND_EMPTY) {
        app_say("RND SEED", "NONE PLAYING");
        return;
    }
    seeds.seed[rnd_slot] = rnd.seed;
    seeds.code[rnd_slot] = rnd.code;
    if (store_save(OBJ_SEEDS, &seeds, sizeof seeds) == 0 && set_store() == 0) {
        t[0] = 0;
        text_cat(t, "SEED SAVED ");
        seed_slot_text(t + 11);
        seed_text(rnd.seed, s);
        app_say(t, s);
    } else {
        app_say("SEED SAVE", "FAILED");
    }
    ui.redraw = 1;
}

static void rnd_send(void)                   /* FX on RND SEED: the slot's patch to the synth, its locks first */
{
    char t[12];
    if (seeds.seed[rnd_slot] == RND_EMPTY) {
        app_say("RND SEED", "EMPTY");
        return;
    }
    rnd_recall(seeds.seed[rnd_slot], seeds.code[rnd_slot]);
    seed_text(seeds.seed[rnd_slot], t);
    app_say("RND SEED SENT", t);
}

static void rnd_tempo(uint32_t bpm, int seed_new)   /* the synth's tempo as the FM-1's TEMPO (held to its range) */
{
    char t[24], v[8];
    const param_t *q = &PARAMS[PG_ARP][EN_K1];
    patch.tempo = (uint8_t)clamp((int32_t)bpm, q->lo, q->hi);
    set_dirty = 1;
    ui.redraw = 1;
    t[0] = 0;
    if (seed_new) {                          /* "1A2B3C4D 120 BPM": the seed and its tempo, said together */
        seed_text(rnd.seed, t);
        text_cat(t, " ");
    }
    int_text(patch.tempo, v);
    text_cat(t, v);
    text_cat(t, " BPM");
    app_say(seed_new ? "RND SEED IN" : "RND TEMPO", t);
}

static uint8_t engine_of(const uint8_t *p, uint32_t n)   /* an engine's name (to its 00 or n): RND_ENG's, or 0xFF */
{
    uint32_t i, j;
    for (i = 0; i < NENG; i++) {
        const char *s = RND_ENG[i];
        for (j = 0; j < n && p[j] && s[j] == (char)p[j]; j++)
            ;
        if (!s[j] && (j == n || !p[j]))
            return (uint8_t)i;
    }
    return 0xFF;
}

static int rnd_sysex(const uint8_t *p, uint32_t n)   /* a frame (F0 and F7 off): 1 if it was the RND Synth's */
{
    static uint32_t last_bpm;                /* the last tempo it sent (0: none yet) */
    static uint8_t seed_new;                 /* a new seed came: its tempo frame to be taken */
    const uint8_t *d = p + 4;
    uint32_t s, i, m = n - 4u;
    char t[12];
    if (n < 4u || !same(p, RND_ID, 3))
        return 0;
    rnd.heard = plat_ms() | 1u;
    ui.redraw = 1;
    switch (p[3]) {
    case 0x10:                               /* the seed (the same again: a patch resent) */
        if (m < 5u || d[4] > 0x0Fu)
            break;
        s = d7(d, 5);
        if (s == rnd.seed)
            break;
        rnd.seed = s;                        /* (its lock code next: 26) */
        seed_new = 1;
        seed_text(s, t);
        app_say("RND SEED IN", t);
        break;
    case 0x20:                               /* the tracks follow */
        rnd.ntr = 0;
        for (i = 0; i < 4u; i++)
            rnd.eng[i] = 0xFF;
        break;
    case 0x21:                               /* the patch: type, tempo, root, scale; the tempo taken */
        if (m < 5u)
            break;
        rnd.type = d[0];
        rnd.bpm = (uint16_t)(d[1] | d[2] << 7);
        rnd.root = d[3];
        rnd.scale = d[4];
        if (rnd.bpm && (seed_new || rnd.bpm != last_bpm))
            rnd_tempo(rnd.bpm, seed_new);
        if (rnd.bpm)
            seed_new = 0;
        last_bpm = rnd.bpm;
        break;
    case 0x22:                               /* a track's engine */
        if (m < 4u || d[0] > 3u)
            break;
        rnd.eng[d[0]] = engine_of(d + 3, m - 3u);
        if (rnd.ntr < d[0] + 1u)
            rnd.ntr = (uint8_t)(d[0] + 1u);
        break;
    case 0x24:                               /* a track's reverb send as rolled */
        if (m >= 2u && d[0] < 4u)
            rnd.send[d[0]] = d[1];
        break;
    case 0x23:                               /* its firmware */
        if (m >= 2u)
            rnd.ver[0] = d[0], rnd.ver[1] = d[1];
        break;
    case 0x25:                               /* its clock */
        if (m >= 1u)
            rnd.clk = d[0];
        break;
    case 0x26:                               /* the lock code */
        if (m < 5u)
            break;
        rnd.code = d7(d, 5);
        break;
    case 0x28:                               /* the tracks' levels and sends */
        if (m < 8u)
            break;
        for (i = 0; i < 4u; i++)
            rnd.vol[i] = d[i], rnd.rev[i] = d[4 + i];
        break;
    }
    return 1;                                /* (its others: taken, dropped) */
}

/* RND PATCH: the locks held across new patches (RND_FREE: the synth picks), each sent as it turns: SELECT the type,
 * ALGO the number of tracks (a new patch from both at once), K1 the root, K2 its octave, K3 the scale (from the next
 * patch on); SEL lets them all go. The code shown at once, the synth's answer (26) then the truth */
static void rnd_say_lock(const char *what, int32_t v, const char *const *names, int32_t num)   /* (num: v a number, never RANDOM) */
{
    char t[8];
    if (v < 0 && !num) {
        app_say(what, "RANDOM");
        return;
    }
    if (names) {
        app_say(what, names[v]);
        return;
    }
    int_text(v, t);
    app_say(what, t);
}
static int rnd_lock_turn(uint32_t k, int32_t e)   /* RND PATCH's encoders (0: said here, not by the caller) */
{
    rlock_t l = lock_get(rnd.code);
    int32_t v;
    uint8_t b;
    switch (k) {
    case EN_SELECT:                          /* type: RANDOM, SEQUENCE, DRONE, JINGLES */
        v = clamp(l.type + e, RND_FREE, 2);
        if (v == l.type)
            return 0;
        l.type = (int8_t)v;
        b = v < 0 ? 0x7F : (uint8_t)v;
        rnd_frame(0x1A, &b, 1);
        rnd_say_lock("RND TYPE", v, RND_TYPE, 0);
        break;
    case EN_ALGO:                            /* tracks: RANDOM, 1..4 */
        v = clamp((l.tracks < 0 ? 0 : l.tracks) + e, 0, 4);
        if (v == (l.tracks < 0 ? 0 : l.tracks))
            return 0;
        l.tracks = (int8_t)(v ? v : RND_FREE);
        b = v ? (uint8_t)v : 0x7F;
        rnd_frame(0x1F, &b, 1);
        rnd_say_lock("RND TRACKS", l.tracks, 0, 0);   /* (RANDOM, or its number) */
        break;
    case EN_K1:                              /* root: RANDOM, C..B (its octave K2's) */
    case EN_K2:
        if (k == EN_K1)
            v = clamp(l.root + e, RND_FREE, 11);
        else if (l.root < 0) {
            app_say("RND OCTAVE", "LOCK A ROOT FIRST");
            return 0;
        } else
            v = clamp(l.oct + e, -3, 3);
        if (v == (k == EN_K1 ? l.root : l.oct))
            return 0;
        if (k == EN_K1)
            l.root = (int8_t)v;
        else
            l.oct = (int8_t)v;
        root_off();
        if (l.root < 0) {
            b = 0;
            rnd_frame(0x27, &b, 1);
        } else {
            root_on = (uint8_t)(60 + 12 * l.oct + l.root);
            root_at = plat_ms();
            rnd_midi(0x90, root_on, 100);
        }
        if (k == EN_K1)
            rnd_say_lock("RND ROOT", l.root, RND_ROOT, 0);
        else
            rnd_say_lock("RND OCTAVE", l.oct, 0, 1);
        break;
    case EN_K3:                              /* scale: RANDOM, RND_SCALE */
        v = clamp(l.scale + e, RND_FREE, (int32_t)NRSCALE - 1);
        if (v == l.scale)
            return 0;
        l.scale = (int8_t)v;
        if (v < 0) {
            b = 1;
            rnd_frame(0x27, &b, 1);
        } else
            rnd_midi(0xB0, 9, scale_cc(v));
        rnd_say_lock("RND SCALE", v, RND_SCALE, 0);
        break;
    default:
        return 0;
    }
    rnd.code = lock_code(&l);
    ui.redraw = 1;
    return 0;
}
static void rnd_free_all(void)               /* SEL on RND PATCH: every lock let go (the engines' too) */
{
    root_off();
    rnd_frame(0x1C, 0, 0);
    rnd.code = 0;
    ui.redraw = 1;
    app_say("RND LOCKS", "ALL RANDOM");
}

/* RND TRACKS: a mixer of the synth's four tracks. SELECT picks the row K1-K4 set (rnd_row): each track's engine held
 * (RND_FREE: the synth picks; 1B t name, or no name to let it go), its level (CC 7) or its reverb send (CC 91), on
 * channel t + 2. Levels and sends: 64 as rolled; said in dB as RND pixels shows them */
enum { RR_ENG, RR_LEVEL, RR_REVERB, NRR };
static uint8_t rnd_row;
static void db_text(float g, const char *none, char *t)   /* gain g in dB to a tenth: "+2.5 DB", "-6.0 DB"; none */
{
    char n[8];
    int32_t d;
    t[0] = 0;
    if (g <= 1e-4f) {
        text_cat(t, none);
        return;
    }
    d = (int32_t)(60.206f * fm_log2f(g) + (g >= 1.0f ? 0.5f : -0.5f));   /* (20 log10: 6.0206 log2, in tenths) */
    if (d > -2 && d < 2)
        d = 0;
    text_cat(t, d > 0 ? "+" : d < 0 ? "-" : "");
    int_text((d < 0 ? -d : d) / 10, n);
    text_cat(t, n);
    text_cat(t, ".");
    int_text((d < 0 ? -d : d) % 10, n);
    text_cat(t, n);
    text_cat(t, " DB");
}
static void level_text(uint32_t v, char *t)   /* "+2.5 DB", "OFF": the level's gain, v * 2 / 127 */
{
    db_text((float)v * 2.0f / 127.0f, "OFF", t);
}
static void reverb_text(uint32_t t4, uint32_t v, char *t)   /* the send of track t4 at v: below 64 to none, above to 1.5 */
{
    float base = rnd.send[t4] == 0xFF ? 1.0f : (float)rnd.send[t4] / 90.0f, w = (float)v / 127.0f;
    db_text(w <= 0.5f ? base * w * 2.0f : base + (1.5f - base) * (w - 0.5f) * 2.0f, "DRY", t);
}
static int rnd_track_turn(uint32_t k, int32_t e)   /* RND TRACKS' encoders (0: said here) */
{
    char w[16] = "RND T", v[16];
    uint32_t t4;
    int32_t x;
    if (k == EN_PRESET) {                   /* Row Select */
        x = clamp((int32_t)rnd_row + e, 0, NRR - 1);
        rnd_row = (uint8_t)x;
        app_say("RND ROW", x == RR_ENG ? "ENGINE" : x == RR_LEVEL ? "LEVEL" : "REVERB");
        return 0;
    }
    if (k < EN_K1)
        return 0;
    t4 = k - EN_K1;
    w[5] = (char)('1' + t4);
    w[6] = 0;
    if (rnd_row == RR_ENG) {
        rlock_t l = lock_get(rnd.code);
        x = clamp(l.eng[t4] + e, RND_FREE, (int32_t)NENG - 1);
        if (x == l.eng[t4])
            return 0;
        l.eng[t4] = (int8_t)x;
        if (x >= 0)
            rnd_engine_out(t4, x);
        else {
            uint8_t d[2] = {(uint8_t)t4, 0};
            rnd_frame(0x1B, d, 2);
        }
        rnd.code = lock_code(&l);
        text_cat(w, " ENGINE");
        app_say(w, x < 0 ? "RANDOM" : RND_ENG_S[x]);
    } else {
        uint8_t *c = rnd_row == RR_LEVEL ? &rnd.vol[t4] : &rnd.rev[t4];
        x = clamp((*c == 0xFF ? 64 : *c) + e, 0, 127);
        *c = (uint8_t)x;
        rnd_midi((uint8_t)(0xB1u + t4), rnd_row == RR_LEVEL ? 7 : 91, (uint8_t)x);
        if (rnd_row == RR_LEVEL)
            level_text((uint32_t)x, v);
        else
            reverb_text(t4, (uint32_t)x, v);
        text_cat(w, rnd_row == RR_LEVEL ? " LEVEL" : " REVERB");
        app_say(w, v);
    }
    ui.redraw = 1;
    return 0;
}
