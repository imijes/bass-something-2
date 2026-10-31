/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 John Brown */
/* RND seeds: Bass Something 2 as a companion to Cyma Forma's RND Synth, a keeper of its seeds (32-bit numbers the synth makes
 * its music from). They don't touch Bass Something 2's sound. The synth's seeds are captured as they come; NSEEDS slots keep
 * them through power-off, in a table of their own (OBJ_SEEDS, apart from the patch bank and its Bass Station II
 * dumps); one goes back to the synth on request. On GLO RND (GLO + OCT+): PRESET picks a slot and shows its seed,
 * SAVE keeps the seed shown in that slot, FX sends the seed shown. A new seed's tempo becomes the FM-1's (TEMPO, so
 * the arp, the sequencer and SEND CLK follow the synth's new direction). The synth's SysEx (its frames read here anew,
 * after Dub-1-RND's):
 *   F0 6F 62 78 10 c0 c1 c2 c3 c4 F7    the seed, five 7-bit chunks, low first (bits 0..6, 7..13, .. 28..31)
 *   F0 6F 62 78 21 md tl th tn sc F7    after it: its mode, tempo (tl | th << 7, BPM; 0 none), tonic and scale
 * its other frames (6F 62 78 20, 22: the tracks) are taken and dropped. The synth sends the same frames again and
 * again: only a new seed is taken, and its tempo only with a new seed or when it changes (between them TEMPO is the
 * player's). F0 7D 42 53 32 02 F7 asks for the seed shown. Part of app.c (included after slot_text): main loop only,
 * nothing in the audio path */
#define RND_EMPTY 0xFFFFFFFFu                /* no seed */
#define RND_LEN 11u                          /* a seed frame, F0 .. F7 */
#define SEEDS_MAGIC 0x31444E52u              /* "RND1": the seed table; another magic (or size) starts it empty */
#define NSEEDS 32u
typedef struct {
    uint32_t magic;
    uint32_t seed[NSEEDS];                   /* each slot's, RND_EMPTY: none */
} seeds_t;
_Static_assert(sizeof(seeds_t) <= PLAT_STORE_MAX, "the seed table fits a store object");
static seeds_t seeds;
static const uint8_t RND_ID[3] = {0x6F, 0x62, 0x78};
enum { RS_NONE, RS_SYNTH, RS_SLOT };
static uint32_t rnd_cur = RND_EMPTY;         /* the seed shown: the synth's last, or a slot's */
static uint8_t rnd_src;                      /* RS_*: where it came from */
static uint8_t rnd_slot;                     /* the slot PRESET picks, 0..NSEEDS-1 */
static uint8_t rnd_out[RND_LEN];             /* the last seed frame sent (the host's rnd_out) */

static int same(const void *a, const void *b, uint32_t n)   /* (no libc on the device) */
{
    const uint8_t *x = a, *y = b;
    while (n && *x == *y)
        n--, x++, y++;
    return n == 0;
}

static void rnd_load(void)                   /* app_init: the table, or an empty one; no seed shown yet */
{
    uint32_t i;
    rnd_cur = RND_EMPTY;
    rnd_src = RS_NONE;
    rnd_slot = 0;
    if (plat_store_load(OBJ_SEEDS, &seeds, sizeof seeds) != (int)sizeof seeds || seeds.magic != SEEDS_MAGIC) {
        seeds.magic = SEEDS_MAGIC;
        for (i = 0; i < NSEEDS; i++)
            seeds.seed[i] = RND_EMPTY;
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
static void seed_slot_text(char *t)          /* "S01" .. "S32" */
{
    t[0] = 'S';
    slot_n_text(rnd_slot, t + 1);
}

static void rnd_pick(int32_t e)              /* PRESET on GLO RND: a slot, its seed shown (an empty one: kept) */
{
    char t[24], s[12];
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
    rnd_cur = seeds.seed[n];
    rnd_src = RS_SLOT;
    seed_text(rnd_cur, s);
    app_say(t, s);
}

static void rnd_store(void)                  /* SAVE on GLO RND: the seed shown into its slot, and the settings */
{
    char t[24], s[12];
    if (rnd_cur == RND_EMPTY) {
        app_say("RND SEED", "NONE YET");
        return;
    }
    seeds.seed[rnd_slot] = rnd_cur;
    if (plat_store_save(OBJ_SEEDS, &seeds, sizeof seeds) == 0 && set_store() == 0) {
        t[0] = 0;
        text_cat(t, "SEED SAVED ");
        seed_slot_text(t + 11);
        seed_text(rnd_cur, s);
        app_say(t, s);
    } else {
        app_say("SEED SAVE", "FAILED");
    }
    ui.redraw = 1;
}

static void rnd_send(void)                   /* FX on GLO RND: the seed shown out over USB in the RND Synth's frame */
{
    uint32_t i;
    char t[12];
    if (rnd_cur == RND_EMPTY) {
        app_say("RND SEED", "NONE YET");
        return;
    }
    rnd_out[0] = 0xF0;
    for (i = 0; i < 3u; i++)
        rnd_out[1 + i] = RND_ID[i];
    rnd_out[4] = 0x10;
    for (i = 0; i < 5u; i++)
        rnd_out[5 + i] = (uint8_t)(rnd_cur >> 7 * i & 0x7Fu);
    rnd_out[10] = 0xF7;
    plat_sysex_send(rnd_out, RND_LEN);
    seed_text(rnd_cur, t);
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
        seed_text(rnd_cur, t);
        text_cat(t, " ");
    }
    int_text(patch.tempo, v);
    text_cat(t, v);
    text_cat(t, " BPM");
    app_say(seed_new ? "RND SEED IN" : "RND TEMPO", t);
}

static int rnd_sysex(const uint8_t *p, uint32_t n)   /* a frame (F0 and F7 off): 1 if it was the RND Synth's */
{
    uint32_t s, i;
    static uint32_t last = RND_EMPTY;        /* the last seed it sent: its repeats ignored */
    static uint32_t last_bpm;                /* the last tempo it sent (0: none yet) */
    static uint8_t seed_new;                 /* a new seed came: its tempo frame to be taken */
    char t[12];
    if (n < 4u || !same(p, RND_ID, 3))
        return 0;
    if (p[3] == 0x21u && n >= 9u) {          /* the key, scale and tempo: the tempo taken */
        uint32_t bpm = (uint32_t)p[5] | (uint32_t)p[6] << 7;
        if (bpm && (seed_new || bpm != last_bpm))
            rnd_tempo(bpm, seed_new);
        if (bpm)
            seed_new = 0;
        last_bpm = bpm;
        return 1;
    }
    if (p[3] != 0x10u || n < 9u || p[8] > 0x0Fu)
        return 1;                            /* (its others, or a short one: taken, dropped) */
    for (s = 0, i = 0; i < 5u; i++)
        s |= (uint32_t)(p[4 + i] & 0x7Fu) << 7 * i;
    if (s == last)
        return 1;
    last = rnd_cur = s;
    rnd_src = RS_SYNTH;
    seed_new = 1;
    ui.redraw = 1;
    seed_text(s, t);
    app_say("RND SEED IN", t);
    return 1;
}
