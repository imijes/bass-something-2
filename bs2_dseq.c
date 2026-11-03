/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 John Brown */
/* Drum Something's sequencer, as the Drumtraks' operation manual has it (sections 2-4, 14): 99 patterns sharing one pool
 * of hits, each hit a time in 1/96 notes (24 PPQN), a drum, an accent and, once programmed, a volume and a tuning. A
 * pattern's length is its time signature x its measures, set while it is empty (longer later by appending). Recording
 * overdubs a loop: each hit moved to the Error Correct's grid, heard from the next pass; Erase Instrument takes a held
 * drum's hits out as the loop reaches them. The audio interrupt owns the pool while anything runs: the main loop asks
 * through dqq (a hit, a run, an erase) and sets only a stopped pattern's fields. No allocation; the pool in PLAT_POOL,
 * saved in OBJ_DQ0.. (bs2.h's store map) in slices of a store object */
#define DQ_NPAT 99u                          /* 00..98; 99 always empty: a cue of it stops at the end */
#define DQ_EMPTY 99u
#define DQ_HITS 4000u                        /* the 400's 3,289 notes, at a little more */
#define DQ_NONE 0xFFu
#define DQ_MAGIC 0x44515332u                 /* (..31: saved before recording went uncorrected by default: dq_init) */
#define DQ_MAGIC1 0x44515331u
/* a hit: time 19 bits (99 measures of 99/2 fit), drum 4, accent 1, volume 4, tuning 4; volume and tuning both 0: not
 * programmed, played at the drum's own as it plays (a programmed silence keeps a tuning of 1) */
#define DQ_T(e) ((e) & 0x7FFFFu)
#define DQ_D(e) ((e) >> 19 & 15u)
#define DQ_A(e) ((e) >> 23 & 1u)
#define DQ_V(e) ((e) >> 24 & 15u)
#define DQ_U(e) ((e) >> 28)
static const uint8_t DQ_BEAT[8] = {2, 4, 6, 8, 12, 16, 24, 32};   /* a time signature's beat values */
static const uint8_t DQ_EC[10] = {2, 4, 6, 8, 12, 16, 24, 32, 48, 96};   /* Error Correct's */
static const uint8_t DQ_SWING[6] = {50, 54, 58, 62, 66, 70};
#define DQ_BEAT4 1u                          /* DQ_BEAT's quarter note: swing only there */
#define DQ_EC_NONE 9u                        /* DQ_EC's 1/96: the clock's own tick, as played (the user's choice: no correcting
                                              * unless asked; the 400's default was 1/16) */
typedef struct {
    uint32_t len;                            /* in ticks: num x beat x meas, more once appended */
    uint16_t start, n;                       /* its hits in the pool */
    uint16_t prog;                           /* the drums programmed (hit in RECORD MODE, stopped): theirs record their
                                              * volume and tuning */
    uint8_t num, beat, meas, swing, ec;      /* beats a measure, DQ_BEAT's, measures, DQ_SWING's, DQ_EC's */
} dq_pat_t;
typedef struct {
    uint32_t magic;
    uint8_t vol[NDT], tune[NDT], metro, master, tempo;   /* (dt's, as they were saved; tempo in what was padding, 0 there:
                                              * the first) */
    uint16_t used;
    dq_pat_t pat[DQ_NPAT];
    uint32_t ev[DQ_HITS];
} dq_mem_t;
static dq_mem_t dqm PLAT_POOL;
/* the tempos, the manual's (7-9): a song's tempo changes count in these, SLOWER and FASTER step them */
static const uint8_t DQ_TEMPO[91] = {
    40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
    70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 86, 87, 89, 91, 92, 94, 95, 97, 98, 100, 102, 103, 105, 107,
    109, 111, 113, 115, 118, 120, 122, 125, 128, 130, 133, 136, 139, 143, 146, 150, 154, 158, 162, 167, 171, 176, 182, 187,
    193, 200, 207, 214, 222, 231, 240, 250};
static int32_t dq_tempo_at(uint32_t bpm)     /* the nearest at or under */
{
    int32_t i = 90;
    while (i > 0 && DQ_TEMPO[i] > bpm)
        i--;
    return i;
}
/* the songs: 99 (00..98; 99 always empty), up to 100 steps, as the manual's Table 5: a pattern, a relative volume or tempo
 * change, empty (ignored), or the end */
#define DS_NSONG 99u
#define DS_NSTEP 100u
#define DS_MAGIC 0x44535331u
#define DS_END 0xC0u
#define DS_EMPTY 0xE0u
#define DS_VOL 0x80u                         /* | -15..15 in 5 bits */
#define DS_TEMPO 0xA0u
#define DS_VAL(c) ((int32_t)((c) & 0x1Fu) - ((c) & 0x10u ? 32 : 0))
typedef struct {
    uint8_t tempo;                           /* its first tempo, BPM (0: none, as it's set) */
    uint8_t step[DS_NSTEP];
} ds_song_t;
typedef struct {
    uint32_t magic;
    ds_song_t s[DS_NSONG];
} ds_mem_t;
static ds_mem_t dsm PLAT_POOL;
enum { DQR_OFF, DQR_REC, DQR_ERASE };        /* RECORD MODE, ERASE INSTRUMENT */
static struct {
    volatile uint8_t run, rec, full, dirty;  /* running; DQR_*; a hit found no room; unsaved */
    volatile uint32_t held;                  /* the drums' keys held (the main loop's): Erase Instrument's */
    volatile uint32_t ext;                   /* MIDI clock's ticks since the last block */
    uint8_t cur, cue;                        /* the pattern playing; the one to take over at its end (DQ_NONE: none) */
    uint8_t in_song, song, scue, sstep;      /* a song running: which, the one to take over at its end, its next step */
    volatile uint8_t sel_song;               /* the song a MIDI Start runs (DQ_NONE: the pattern shown) */
    int16_t svol;                            /* its volume changes so far, in final volume units */
    volatile uint8_t sel;                    /* the pattern shown: what a MIDI Start runs */
    float inc, next;                         /* ticks a sample; the next thing to do there (where it is: dt_pos) */
    uint32_t idx, click_t;                   /* the next hit; the next metronome beat */
    int32_t skip[NDT];                       /* a hit recorded ahead of the playhead: not this pass (heard on the next) */
    float click, click_ph, click_inc, click_g;   /* the metronome's envelope, phase, pitch, level */
} dq PLAT_POOL;
static fifo_t dqq;                           /* main loop -> audio: op | a << 8 | b << 16 */
enum { DQC_RUN, DQC_STOP, DQC_CUE, DQC_HIT, DQC_EKEY, DQC_ERASE, DQC_COPY, DQC_SONG, DQC_SCUE };
#ifdef PLAT_HOST
static uint32_t dq_clicks;                   /* (the host's: the metronome's beats, for tests/drumseq.txt) */
#endif

static void dq_cmd(uint32_t op, uint32_t a, uint32_t b) { fifo_put(&dqq, op | a << 8 | b << 16); }
static uint32_t dq_bar(const dq_pat_t *p) { return (uint32_t)p->num * (96u / DQ_BEAT[p->beat]); }   /* a measure's ticks */
static void dq_defaults(dq_pat_t *p)         /* a new pattern's: the manual's 4/4, a measure, 50 %; recorded as played */
{
    p->n = p->prog = 0;
    p->num = 4;
    p->beat = DQ_BEAT4;
    p->meas = 1;
    p->swing = 0;
    p->ec = DQ_EC_NONE;
    p->len = dq_bar(p);
}
static int dq_can_swing(const dq_pat_t *p) { return p->beat == DQ_BEAT4; }
static float dq_sw(const dq_pat_t *p, uint32_t t)   /* a tick to when it plays: the off-beat 8th moved by the swing */
{
    const uint32_t q = t % 24u;
    float h;
    if (!p->swing || p->beat != DQ_BEAT4)
        return (float)t;
    h = 0.24f * (float)DQ_SWING[p->swing];
    return (float)(t - q) + (q < 12u ? (float)q * h / 12.0f : h + (float)(q - 12u) * (24.0f - h) / 12.0f);
}
static float dq_unsw(const dq_pat_t *p, float pos)   /* its way back: what the playhead's place is in ticks */
{
    float q, h, b;
    if (!p->swing || p->beat != DQ_BEAT4)
        return pos;
    b = (float)((uint32_t)pos / 24u * 24u);
    q = pos - b;
    h = 0.24f * (float)DQ_SWING[p->swing];
    return b + (q < h ? q * 12.0f / h : 12.0f + (q - h) * 12.0f / (24.0f - h));
}
/* the pool's one list: the other patterns' places as a hit goes in or out at abs (an empty pattern has no place) */
static void dq_open(uint32_t abs, uint32_t by, const dq_pat_t *self)
{
    uint32_t i;
    for (i = (uint32_t)dqm.used; i-- > abs;)
        dqm.ev[i + by] = dqm.ev[i];
    for (i = 0; i < DQ_NPAT; i++)
        if (&dqm.pat[i] != self && dqm.pat[i].start >= abs)
            dqm.pat[i].start = (uint16_t)(dqm.pat[i].start + by);
    dqm.used = (uint16_t)(dqm.used + by);
    dq.dirty = 1;
}
static void dq_close(uint32_t abs, uint32_t by, const dq_pat_t *self)
{
    uint32_t i;
    for (i = abs; i + by < dqm.used; i++)
        dqm.ev[i] = dqm.ev[i + by];
    for (i = 0; i < DQ_NPAT; i++)
        if (&dqm.pat[i] != self && dqm.pat[i].start > abs)
            dqm.pat[i].start = (uint16_t)(dqm.pat[i].start - by);
    dqm.used = (uint16_t)(dqm.used - by);
    dq.dirty = 1;
}
static void dq_next(const dq_pat_t *p)       /* what's next on the way: a hit, a beat or the loop's end */
{
    float n = (float)p->len, t;
    if (dq.idx < p->n && (t = dq_sw(p, DQ_T(dqm.ev[p->start + dq.idx]))) < n)
        n = t;
    if (dq.rec && dq.click_t < p->len && (float)dq.click_t < n)
        n = (float)dq.click_t;
    dq.next = n;
}
static void dq_remove(dq_pat_t *p, uint32_t i)
{
    dq_close(p->start + i, 1u, p);
    p->n--;
    if (dq.idx > i)
        dq.idx--;
}
static void dq_stop(void)
{
    dq.run = 0;
    dt_running = 0;
    dq.in_song = 0;
    dq.svol = 0;
    dq.cue = DQ_NONE;
}
static void dq_start(uint32_t g)
{
    uint32_t i;
    dq.cur = (uint8_t)g;
    dq.cue = DQ_NONE;
    dq.in_song = 0;
    dt_pos = 0.0f;
    dq.idx = dq.click_t = 0;
    for (i = 0; i < NDT; i++)
        dq.skip[i] = -1;
    dq_next(&dqm.pat[g]);
    dq.run = 1;
    dt_running = 1;                          /* the clock master: an arp or a sequence playing starts over with it (TRANSPORT
                                              * off, DRUM MODS: left where it is) */
    if (eng.src != SRC_NONE && !set.dtrans_off)
        eng.src = SRC_RESTART;
}
/* a hit recorded (audio interrupt): to the nearest of the grid, into the loop; ahead of the playhead, not sounded this
 * pass (it was just played) */
static void dq_hit_in(uint32_t d, uint32_t acc)
{
    dq_pat_t *p;
    uint32_t i, t, e, v = 0, u = 0, grid;
    float at;
    if (!dq.run || dq.rec != DQR_REC || dq.cur >= DQ_NPAT)
        return;
    p = &dqm.pat[dq.cur];
    grid = 96u / DQ_EC[p->ec];
    t = ((uint32_t)(dq_unsw(p, dt_pos) + 0.5f) + grid / 2u) / grid * grid;
    if (t >= p->len)
        t = 0;
    if (p->prog >> d & 1u) {
        v = dt.vol[d];
        u = dt.tune[d];
        if (!v && !u)
            u = 1;
    }
    e = t | d << 19 | (acc ? 1u : 0u) << 23 | v << 24 | u << 28;
    for (i = 0; i < p->n && DQ_T(dqm.ev[p->start + i]) <= t; i++)
        if (DQ_T(dqm.ev[p->start + i]) == t && DQ_D(dqm.ev[p->start + i]) == d) {   /* (one there: this one instead) */
            dqm.ev[p->start + i] = e;
            dq.dirty = 1;
            return;
        }
    if (dqm.used >= DQ_HITS) {               /* FULL: no more recorded */
        dq.full = 1;
        return;
    }
    if (!p->n)                               /* (a pattern's first: at the pool's end, out of the others' way) */
        p->start = dqm.used;
    dq_open(p->start + i, 1u, p);
    dqm.ev[p->start + i] = e;
    p->n++;
    at = dq_sw(p, t);
    if (at <= dt_pos && i <= dq.idx)         /* (behind the playhead) */
        dq.idx++;
    else
        dq.skip[d] = (int32_t)t;
    dq_next(p);
}
static void dq_erase_key(uint32_t d)         /* a drum's key just pressed, erasing: a hit it just came too late for too */
{
    dq_pat_t *p;
    uint32_t i;
    float from;
    if (!dq.run || dq.cur >= DQ_NPAT)
        return;
    p = &dqm.pat[dq.cur];
    from = dt_pos - 2.0f;
    for (i = 0; i < p->n; i++) {
        const uint32_t e = dqm.ev[p->start + i];
        const float at = dq_sw(p, DQ_T(e));
        if (at > dt_pos)
            break;
        if (DQ_D(e) == d && at >= from) {
            dq_remove(p, i);
            i--;
        }
    }
    dq_next(p);
}
/* a song's next pattern: the steps up to it done (their volume and tempo changes, which accumulate over its loops, as the
 * manual); at its end, what's cued, or round again. 0: it stops here (an empty song, an empty pattern, a cue of 99) */
static int dq_song_walk(void)
{
    uint32_t guard;
    for (guard = 0; guard < 2u * DS_NSTEP + 2u; guard++) {
        const ds_song_t *t = &dsm.s[dq.song];
        const uint32_t c = dq.sstep < DS_NSTEP ? t->step[dq.sstep] : DS_END;
        if (c == DS_END) {
            if (dq.scue != DQ_NONE) {
                if (dq.scue >= DS_NSONG)
                    return 0;
                dq.song = dq.scue;
                dq.scue = DQ_NONE;
                dq.sstep = 0;
                if (dsm.s[dq.song].tempo)
                    dt.tempo = dsm.s[dq.song].tempo;
                continue;
            }
            if (!dq.sstep)
                return 0;
            dq.sstep = 0;
            continue;
        }
        dq.sstep++;
        if (c < DQ_NPAT + 1u) {
            if (c >= DQ_NPAT || !dqm.pat[c].n)
                return 0;
            dq.cur = (uint8_t)c;
            return 1;
        }
        if ((c & 0xE0u) == DS_VOL)
            dq.svol = (int16_t)clamp((int32_t)dq.svol + DS_VAL(c) * 8, -128, 127);
        else if ((c & 0xE0u) == DS_TEMPO)
            dt.tempo = DQ_TEMPO[clamp(dq_tempo_at(dt.tempo) + DS_VAL(c), 0, 90)];
    }
    return 0;
}
static void dq_song_start(uint32_t n)
{
    if (n >= DS_NSONG)
        return;
    dq.song = (uint8_t)n;
    dq.scue = DQ_NONE;
    dq.sstep = 0;
    dq.svol = 0;
    if (dsm.s[n].tempo)
        dt.tempo = dsm.s[n].tempo;
    if (dq_song_walk()) {
        dq_start(dq.cur);
        dq.in_song = 1;
    }
}
static void dq_copy(uint32_t a, uint32_t b)  /* pattern a onto b: empty, a copy, its settings too; else a after its hits */
{
    dq_pat_t *s = &dqm.pat[a], *d = &dqm.pat[b];
    const uint32_t n = s->n, off = d->n ? d->len : 0u, sl = s->len;
    uint32_t i, at;
    if (!n || a >= DQ_NPAT || b >= DQ_NPAT)
        return;
    if (dqm.used + n > DQ_HITS || off + sl > 0x7FFFFu) {
        dq.full = 1;
        return;
    }
    if (d->n) {
        d->len = off + sl;
        i = d->len / dq_bar(d);              /* (its time signature kept, as the manual; its measures if they're whole) */
        if (d->len % dq_bar(d) == 0u)
            d->meas = (uint8_t)(i > 99u ? 99u : i);
    } else {
        const uint16_t st = d->start;
        *d = *s;
        d->start = st;
        d->n = 0;
        d->start = dqm.used;
    }
    at = d->start + d->n;
    dq_open(at, n, d);                       /* (s's place moves on if it's after: read from it after) */
    for (i = 0; i < n; i++) {
        const uint32_t e = dqm.ev[s->start + i];
        dqm.ev[at + i] = (e & ~0x7FFFFu) | (DQ_T(e) + off);
    }
    d->n = (uint16_t)(d->n + n);
}
/* its next thing's come (audio interrupt): the hits due, the metronome, the loop's end and what's cued */
static void dq_step(void)
{
    dq_pat_t *p = &dqm.pat[dq.cur];
    for (;;) {
        while (dq.idx < p->n) {
            const uint32_t e = dqm.ev[p->start + dq.idx], d = DQ_D(e);
            if (dq_sw(p, DQ_T(e)) > dt_pos)
                break;
            if (dq.rec == DQR_ERASE && (dq.held >> d & 1u)) {   /* (the manual's: held as the loop passes them) */
                dq_remove(p, dq.idx);
                continue;
            }
            dq.idx++;
            if (dq.skip[d] == (int32_t)DQ_T(e))
                dq.skip[d] = -1;
            else if (d < NDT) {
                const uint32_t pr = (e >> 24) != 0u, v = pr ? DQ_V(e) : dt.vol[d], u = pr ? DQ_U(e) : dt.tune[d];
                dt_hit(d, u, (uint32_t)clamp((int32_t)((175u + (DQ_A(e) ? 80u : 0u)) * v / 15u) + (v ? dq.svol : 0), 0, 255));
            }
        }
        if (dq.rec && dq.click_t < p->len && (float)dq.click_t <= dt_pos) {   /* on each beat, the loop's first accented */
            dq.click = dq.click_t ? 0.7f : 1.0f;
            dq.click_inc = (dq.click_t ? 1600.0f : 2400.0f) / BS2_RATE;
            dq.click_g = (float)dt.metro * (1.4f / 127.0f);
            dq.click_ph = 0.0f;
#ifdef PLAT_HOST
            dq_clicks++;
#endif
        }
        if (dq.click_t <= dt_pos)
            dq.click_t = ((uint32_t)dt_pos / (96u / DQ_BEAT[p->beat]) + 1u) * (96u / DQ_BEAT[p->beat]);
        if (dt_pos < (float)p->len)
            break;
        dt_pos -= (float)p->len;             /* the loop's end: on, or what's cued, or stopped there */
        dq.idx = dq.click_t = 0;
        if (dq.in_song) {                    /* (a song: its next step) */
            if (!dq_song_walk()) {
                dq_stop();
                return;
            }
            p = &dqm.pat[dq.cur];
        } else if (dq.cue != DQ_NONE) {
            if (dq.cue >= DQ_NPAT) {
                dq_stop();
                return;
            }
            dq.cur = dq.cue;
            dq.cue = DQ_NONE;
            p = &dqm.pat[dq.cur];
        }
        if (!p->n && !dq.rec) {              /* (an empty one, or all its hits erased) */
            dq_stop();
            return;
        }
        if (dt_pos >= (float)p->len)         /* (a shorter one, a tick's worth: from its top) */
            dt_pos = 0.0f;
    }
    dq_next(p);
}
static inline void dq_sample(void)           /* each sample (audio interrupt) */
{
    if (dq.run) {
        dt_pos += dq.inc;
        if (dt_pos >= dq.next)
            dq_step();
    }
}
static inline float dq_click_next(void)      /* the metronome, its beats a short square's */
{
    if (dq.click < 0.002f)
        return 0.0f;
    dq.click *= 0.9977f;                     /* (about 10 ms) */
    dq.click_ph += dq.click_inc;
    if (dq.click_ph >= 1.0f)
        dq.click_ph -= 1.0f;
    return (dq.click_ph < 0.5f ? dq.click : -dq.click) * dq.click_g;
}
/* a block's start (audio interrupt): what the main loop asked; the clock (MIDI's only while not recording, as the
 * manual) */
static void dq_block(void)
{
    uint32_t c;
    const uint32_t x = dq.ext;
    dq.ext -= x;
    while (fifo_get(&dqq, &c)) {
        const uint32_t a = c >> 8 & 0xFFu, b = c >> 16 & 0xFFu;
        switch (c & 0xFFu) {
        case DQC_RUN:
            if (a < DQ_NPAT)
                dq_start(a);
            break;
        case DQC_STOP:
            dq_stop();
            break;
        case DQC_SONG:
            dq_song_start(a);
            break;
        case DQC_SCUE:
            dq.scue = (uint8_t)a;
            break;
        case DQC_CUE:
            dq.cue = (uint8_t)a;
            break;
        case DQC_HIT:
            dq_hit_in(a, b);
            break;
        case DQC_EKEY:
            dq_erase_key(a);
            break;
        case DQC_ERASE:
            if (a < DQ_NPAT) {
                if (dqm.pat[a].n)
                    dq_close(dqm.pat[a].start, dqm.pat[a].n, &dqm.pat[a]);
                dq_defaults(&dqm.pat[a]);
                dq.full = 0;
                dq.dirty = 1;
            }
            break;
        case DQC_COPY:
            dq_copy(a, b);
            dq.dirty = 1;
            break;
        default:
            break;
        }
    }
    if (ext_clock() && !dq.rec) {
        dq.inc = 0.0f;
        if (dq.run) {
            dt_pos += (float)x;
            if (dt_pos >= dq.next)
                dq_step();
        }
    } else {
        dq.inc = (float)dt.tempo * (24.0f / 60.0f / BS2_RATE);
    }
    dt_lock_h = set.dswing_lock && dq.run && dq.cur < DQ_NPAT ? 0.24f * (float)DQ_SWING[dqm.pat[dq.cur].swing] : 0.0f;
    if (dq.run && dq.cur < DQ_NPAT)          /* (RECORD MODE just on: its metronome) */
        dq_next(&dqm.pat[dq.cur]);
}

/* saved whole, a store object a slice (a slice unchanged is not written: its sectors' erase stops the audio a moment) */
#define DQ_NOBJ 5u
#define DS_NOBJ 3u
static uint32_t dq_sum[DQ_NOBJ], ds_sum[DS_NOBJ];
static uint32_t dq_hash(const uint8_t *s, uint32_t n)
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *s++) * 16777619u;
    return h;
}
static int dq_load(void *mem, uint32_t sz, uint32_t obj, uint32_t *sum, uint32_t nobj)   /* 1: every slice read */
{
    uint32_t i, at = 0;
    int ok = 1;
    for (i = 0; i < nobj && at < sz; i++) {
        const uint32_t n = sz - at < PLAT_STORE_MAX ? sz - at : PLAT_STORE_MAX;
        if (plat_store_load(obj + i, (uint8_t *)mem + at, n) != (int)n)
            ok = 0;
        else
            sum[i] = dq_hash((uint8_t *)mem + at, n);
        at += n;
    }
    return ok;
}
static int dq_store(const void *mem, uint32_t sz, uint32_t obj, uint32_t *sum, uint32_t nobj)   /* 0: done */
{
    uint32_t i, at = 0;
    for (i = 0; i < nobj && at < sz; i++) {
        const uint32_t n = sz - at < PLAT_STORE_MAX ? sz - at : PLAT_STORE_MAX;
        const uint32_t h = dq_hash((const uint8_t *)mem + at, n);
        if (h != sum[i]) {
            if (plat_store_save(obj + i, (const uint8_t *)mem + at, n) != 0)
                return 1;
            sum[i] = h;
        }
        at += n;
    }
    return 0;
}
static void ds_clear(ds_song_t *t)
{
    uint32_t i;
    t->tempo = 0;
    for (i = 0; i < DS_NSTEP; i++)
        t->step[i] = DS_END;
}
static uint32_t ds_end(const ds_song_t *t)   /* where its end step is */
{
    uint32_t i = 0;
    while (i < DS_NSTEP && t->step[i] != DS_END)
        i++;
    return i;
}
static void dq_init(void)
{
    uint32_t i;
    int ok = dq_load(&dqm, (uint32_t)sizeof dqm, OBJ_DQ0, dq_sum, DQ_NOBJ);
    if (ok && dqm.magic == DQ_MAGIC1) {      /* (from then: each to as played, the cymbals to their new level) */
        for (i = 0; i < DQ_NPAT; i++)
            dqm.pat[i].ec = DQ_EC_NONE;
        if (dqm.vol[DT_CRASH] == 15u)
            dqm.vol[DT_CRASH] = DT_CYM_VOL;
        if (dqm.vol[DT_RIDE] == 15u)
            dqm.vol[DT_RIDE] = DT_CYM_VOL;
        dqm.magic = DQ_MAGIC;
    }
    if (ok && dqm.magic == DQ_MAGIC) {
        uint32_t sum = 0;                    /* (never trusted as read: a record from before the tempo has its count where
                                              * the tempo is now; the tempo's own range, the hits counted) */
        for (i = 0; i < DQ_NPAT; i++)
            sum += dqm.pat[i].n;
        dqm.used = (uint16_t)(sum <= DQ_HITS ? sum : DQ_HITS);
        if (dqm.tempo < 40u || dqm.tempo > 250u)
            dqm.tempo = 0;
        for (i = 0; i < NDT; i++) {
            dt.vol[i] = dqm.vol[i];
            dt.tune[i] = dqm.tune[i];
        }
        dt.metro = dqm.metro;
        dt.master = dqm.master;
        if (dqm.tempo)
            dt.tempo = dqm.tempo;
    } else {
        for (i = 0; i < DQ_NOBJ; i++)
            dq_sum[i] = 0;
        dqm.magic = DQ_MAGIC;
        dqm.used = 0;
        for (i = 0; i < DQ_NPAT; i++) {
            dqm.pat[i].start = 0;
            dq_defaults(&dqm.pat[i]);
        }
    }
    if (!dq_load(&dsm, (uint32_t)sizeof dsm, OBJ_DS0, ds_sum, DS_NOBJ) || dsm.magic != DS_MAGIC) {
        for (i = 0; i < DS_NOBJ; i++)
            ds_sum[i] = 0;
        dsm.magic = DS_MAGIC;
        for (i = 0; i < DS_NSONG; i++)
            ds_clear(&dsm.s[i]);
    }
    dq.cue = dq.scue = dq.sel_song = DQ_NONE;
    dq.cur = 0;
}
static int dq_save(void)                     /* 0: done (stopped: the main loop's alone) */
{
    uint32_t i;
    for (i = 0; i < NDT; i++) {
        dqm.vol[i] = dt.vol[i];
        dqm.tune[i] = dt.tune[i];
    }
    dqm.metro = dt.metro;
    dqm.master = dt.master;
    dqm.tempo = dt.tempo;
    if (dq_store(&dqm, (uint32_t)sizeof dqm, OBJ_DQ0, dq_sum, DQ_NOBJ) || dq_store(&dsm, (uint32_t)sizeof dsm, OBJ_DS0, ds_sum, DS_NOBJ))
        return 1;
    dq.dirty = 0;
    return 0;
}
