/* SPDX-License-Identifier: MPL-2.0 */
/* The organ, the fourth instrument (its engine: bs2_b3.c, after the Hammond B-3 and its Leslie): its mode (ui.b3, HOME
 * after the RMI's), its 32 presets as the bass's patches are (PRESET plays one at once; SAVE as a Bass Station II's: its
 * HOME, the slot blinking, PRESET picks, SAVE stores; PATCH NAME its name), its pages and its live buttons. Part of the
 * unity build, included by app.c after the panel's drawing:
 *   the two manuals, upper and lower (ui.olower: the one the FM-1's keys play and the pages edit; SEL held the other),
 *     each with two sets of drawbars, its preset keys A# and B choosing the one it sounds (EDIT held the other); the
 *     percussion the upper's B set's alone; the lower on its own MIDI channel (GLO MIDI CHANNELS' ALGO)
 *   ORGAN HOME (PG_OHOME): the console as the photo has it, its four sets and tabs as the sound has them; ALGO 16',
 *     K1-K4 5 1/3' 8' 4' 2 2/3' of the manual played's set sounding (a turn dives to DRAWBARS: zoom_organ); PRESET the
 *     preset (the display's); SELECT its volume, as every HOME's (here and on DRAWBARS)
 *   ORGAN DRAWBARS (PG_OBARS): the manual played's two sets up close, the same five; SEL there its other view: K1-K4
 *     2' 1 3/5' 1 1/3' 1', then the other manual's two views; PRESET the preset on each
 *   ORGAN TABS (PG_OTABS): the percussion's tabs (K1 ON, K2 VOLUME SOFT, K3 DECAY FAST, K4 HARMONIC THIRD), the vibrato
 *     (SELECT on, ALGO its knob V1 V2 V3 C1 C2 C3)
 *   ORGAN LESLIE (PG_OLES): the 122, LFO again its views round (oles_draw): CABINET (K1 WIDTH, K2 DRIVE, K3 CLICK, K4
 *     VOLUME), ROTORS turning (K1-K4 the horn's and the drum's slow and fast speeds), HORN ROTOR and LOW ROTOR (the
 *     CONTROLS panel's trimmers: RISE, FALL, SLOW, FAST), AMP (K1 DRIVE, K4 VOLUME); ALGO the speed on each (SLOW,
 *     STOP, FAST), SELECT and PRESET as HOME's
 *   SEL: DRAWBARS, its other view, the other manual's two, PATCH NAME, round (VIBRATO, PERCUSSION, LESLIE: FX, ENV,
 *   LFO); held, the other manual. ARP the Leslie chorale or tremolo (held: the brake), FX ORGAN VIBRATO, ENV ORGAN
 *   PERCUSSION (again there: its main switch), LFO ORGAN LESLIE (again: its next view), EDIT the manual's vibrato on or
 *   off (held: its other preset key, A# or B);
 *   REC, PLAY and SEQ (held: cleared) its part of the pattern, as the piano's (PLAY, PLAY held and SEQ held: the manual played's notes alone;
 *   the drums' PLAY both manuals).
 * Its MIDI: notes on its channel (b3_chan), its CCs on its CC channel (organ_cc) */
#define ORG_MAGIC 0x34334F42u                /* "BO34" (MOJO's character in each preset) */
#define ORG_MAGIC_3 0x33334F42u              /* "BO33": the Leslie's settings in each preset */
#define ORG_MAGIC_2 0x32334F42u              /* "BO32": the two manuals, the Leslie's in the settings */
#define ORG_MAGIC_1 0x31334F42u              /* "BO31": one */
#define ORG_P2 __builtin_offsetof(b3_patch_t, les)   /* (a BO32 preset's size) */
#define ORG_P3 __builtin_offsetof(b3_patch_t, mchar) /* (a BO33's) */
_Static_assert(__builtin_offsetof(b3_patch_t, ua) == 20, "a bank from before the lower manual: its presets 20 bytes");
typedef struct {
    uint32_t magic;
    b3_patch_t p[ONSLOT];
    char name[ONSLOT][PNAME_N];
} organ_bank_t;
static organ_bank_t obank PLAT_POOL;
static b3_patch_t opatch;                    /* the sound now, as edited */
static char oname[PNAME_N];                  /* its name, as edited */
static uint8_t odirty;                       /* changes not saved (SAVE lit) */
static volatile uint8_t o_sent;              /* opatch changed: to the engine at the render's start (organ_block) */
static const char *const OBAR[9] = {"16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'"};
static const char *const OVIB[6] = {"V1", "V2", "V3", "C1", "C2", "C3"};
#define O_VPOS(v) ((v) < 3u ? 2u * (v) : 2u * ((v) - 3u) + 1u)   /* (its knob's position, V1 C1 V2 C2 V3 C3 round it) */
#define O_VAT(p) ((p) & 1u ? 3u + (p) / 2u : (p) / 2u)            /* (the setting at position p) */
#define O_VANG(p) ((1.0f + 2.0f * (float)(p)) * (FM_PI / 6.0f))     /* (position p's way: V1 at 4 o'clock, clockwise) */

/* the factory's: a classic registration each, named (drawbars as an organist writes them, 16' first), the rest INIT */
static const struct {
    const char *name, *bars;
    uint8_t perc, third, fast, vib_on, vib, drive;
} OFACT[] = {
    {"JAZZ", "888000000", 1, 1, 1, 1, 5, 30},       {"FULL ORGAN", "888888888", 0, 0, 0, 1, 5, 40},
    {"GOSPEL", "888800000", 1, 1, 1, 1, 5, 50},     {"BALLAD", "008800000", 0, 0, 0, 1, 4, 10},
    {"SOUL", "886000000", 1, 1, 1, 1, 5, 40},       {"BLUES", "888600000", 0, 0, 0, 1, 5, 60},
    {"ROCK", "888000000", 0, 0, 0, 1, 5, 110},      {"SHUFFLE", "838000000", 1, 1, 1, 1, 5, 40},
    {"FLUTE", "008000000", 0, 0, 0, 1, 2, 0},       {"CLARINET", "008080800", 0, 0, 0, 0, 0, 10},
    {"PERC SECOND", "808000000", 1, 0, 1, 1, 5, 30}, {"ORGAN LICKER", "207736358", 0, 0, 0, 1, 5, 40},
};
static void organ_manuals(b3_patch_t *p)     /* (a preset from before the lower manual: its upper A# set its B's, without
                                              * the percussion; the lower's A# 00 8600 000, B 00 8800 000; both on B) */
{
    uint32_t i;
    for (i = 0; i < 9u; i++)
        p->ua[i] = p->bar[i], p->la[i] = i == 2u ? 8u : i == 3u ? 6u : 0u, p->lbar[i] = i == 2u || i == 3u ? 8u : 0u;
    p->ub = p->lb = 1;
}
static void organ_leslie(b3_patch_t *p)      /* (a preset from before the Leslie's were its: the settings' then) */
{
    memcpy(p->les, set.les, sizeof p->les);
    p->speed = 0, p->swell = set.oswell, p->swell_rest = set.oswell_rest, p->mojo = 0;
}
static void organ_factory(uint32_t s)        /* slot s as the factory has it */
{
    b3_patch_t *p = &obank.p[s];
    const char *n = "INIT", *b = "888000000";
    uint32_t i;
    __builtin_memset(p, 0, sizeof *p);
    __builtin_memset(obank.name[s], 0, PNAME_N);
    p->vib = 5, p->drive = 20, p->click = 64, p->width = 100;
    __builtin_memset(p->les, 64, sizeof p->les);   /* (the Leslie's trimmers: the 122's; KEY SWELL's rest 70 %) */
    p->swell_rest = 89;
    if (s < sizeof OFACT / sizeof OFACT[0]) {
        n = OFACT[s].name, b = OFACT[s].bars;
        p->perc_on = OFACT[s].perc, p->perc_3rd = OFACT[s].third, p->perc_fast = OFACT[s].fast;
        p->vib_on = OFACT[s].vib_on, p->vib = OFACT[s].vib, p->drive = OFACT[s].drive;
    }
    for (i = 0; i < 9u; i++)
        p->bar[i] = (uint8_t)(b[i] - '0');
    organ_manuals(p);
    for (i = 0; n[i] && i < PNAME_N; i++)
        obank.name[s][i] = n[i];
}
static void organ_pick(uint32_t s)           /* slot s's sound, at once (what was not saved is left) */
{
    set.b3_slot = (uint8_t)s;
    opatch = obank.p[s];
    memcpy(oname, obank.name[s], PNAME_N);
    odirty = 0;
    o_sent = 1;
    ui.obrake = opatch.speed == 1u, ui.ofast = opatch.speed == 2u;   /* (its Leslie's speed as saved) */
}
static void organ_load(void)                 /* (app_init: its bank, else the factory's) */
{
    uint32_t s;
    enum { OLD_P = 20, OLD_SIZE = 4 + ONSLOT * (OLD_P + PNAME_N),   /* (a bank from before the lower manual; from */
           OLD_SIZE2 = 4 + ONSLOT * (ORG_P2 + PNAME_N),            /* before the Leslie's were each preset's; */
           OLD_SIZE3 = 4 + ONSLOT * (ORG_P3 + PNAME_N) };          /* before MOJO's character) */
    const int n = plat_store_load(OBJ_ORGAN, &obank, sizeof obank);
    const uint32_t op = n == OLD_SIZE && obank.magic == ORG_MAGIC_1 ? OLD_P
                      : n == OLD_SIZE2 && obank.magic == ORG_MAGIC_2 ? ORG_P2
                      : n == OLD_SIZE3 && obank.magic == ORG_MAGIC_3 ? ORG_P3 : 0u;
    if (op) {                                /* (its names to their place, then its presets, the last first) */
        uint8_t *const o = (uint8_t *)&obank + 4;
        memmove(obank.name, o + ONSLOT * op, ONSLOT * PNAME_N);
        for (s = ONSLOT; s-- > 0;) {
            memmove(&obank.p[s], o + s * op, op);
            __builtin_memset((uint8_t *)&obank.p[s] + op, 0, sizeof obank.p[s] - op);
            if (op == OLD_P)
                organ_manuals(&obank.p[s]);
            if (op < ORG_P3)
                organ_leslie(&obank.p[s]);
        }
        obank.magic = ORG_MAGIC;
    } else if (n != (int)sizeof obank || obank.magic != ORG_MAGIC) {
        obank.magic = ORG_MAGIC;
        for (s = 0; s < ONSLOT; s++)
            organ_factory(s);
    }
    organ_pick(set.b3_slot < ONSLOT ? set.b3_slot : 0u);
}
static void organ_block(void)                /* the render's, before the second core's job: the sound, the Leslie */
{
    static uint8_t can;                      /* (the CANCEL held last time) */
    static uint32_t noise = 0xFFFFFFFFu;     /* (the noises last given it) */
    const uint32_t nz = (uint32_t)set.b3_noise[0] | (uint32_t)set.b3_noise[1] << 8 | (uint32_t)set.b3_noise[2] << 16
                        | (uint32_t)set.b3_noise[3] << 24;
    if (nz != noise)
        noise = nz, b3_noise(set.b3_noise[0], set.b3_noise[1], set.b3_noise[2], set.b3_noise[3]);
    if (o_sent || ui.ocan != can) {          /* (CANCEL held: the manual's keys none, on a copy) */
        b3_patch_t p = opatch;
        o_sent = 0;
        can = ui.ocan;
        p.ub = (uint8_t)(p.ub | (can & 1u) << 2), p.lb = (uint8_t)(p.lb | (can & 2u) << 1);
        b3_set(&p);
    }
    b3_leslie(ui.ofast, ui.obrake);
    b3_rotor(opatch.les);
    {                                        /* the swell: MIDI's pedal, and with KEY SWELL the keys' pressure opening
                                              * it from its rest */
        uint32_t e = 127u - ui.oexpr_off;
        if (opatch.swell)
            e = e * (opatch.swell_rest + (at_okeys > 127u ? 127u : at_okeys) * (127u - opatch.swell_rest) / 127u) / 127u;
        o_swell = (uint8_t)e;
        b3_expr(e);
    }
}
static char *name_cur(void) { return ui.b3 ? oname : cur_name; }   /* PATCH NAME's: the bass's, the organ's */
static void name_changed(void)
{
    if (ui.b3)
        odirty = 1;
    else
        set_dirty = 1;
}
static int organ_dirty(void) { return odirty; }
static uint32_t organ_lights(void)           /* FX and EDIT lit while its vibrato is on, ENV while its percussion is */
{
    return (uint32_t)opatch.vib_on << B_FX | (uint32_t)(opatch.perc_on && (opatch.ub & 3u)) << B_ENV
         | (uint32_t)(ui.olower ? opatch.vib_lower : opatch.vib_on) << B_EDIT;
}
static void organ_changed(void)
{
    odirty = 1;
    o_sent = 1;
    ui.redraw = 1;
}

/* its CCs (its CC channel): 12-20 the drawbars, 21 the percussion, 22 its THIRD, 23 SOFT, 24 FAST, 30 the vibrato,
 * 25 its knob, 26 the drive, 28 the click, 29 the width, 31 its MOJO, 9 its CHARACTER (127 spread
 * over them), 7 its volume; live: 1 (the mod wheel) the
 * Leslie fast, 27 the brake, 11 the expression pedal; 64 and up: on */
static void organ_cc(uint32_t c, uint32_t v)
{
    if (c >= 12u && c <= 20u)
        (opatch.ub & 1u ? opatch.bar : opatch.ua)[c - 12u] = (uint8_t)((v * 8u + 63u) / 127u);   /* (the upper's set
                                              * sounding) */
    else if (c == 21u)
        opatch.perc_on = v >= 64u;
    else if (c == 22u)
        opatch.perc_3rd = v >= 64u;
    else if (c == 23u)
        opatch.perc_soft = v >= 64u;
    else if (c == 24u)
        opatch.perc_fast = v >= 64u;
    else if (c == 30u)
        opatch.vib_on = v >= 64u;
    else if (c == 31u)
        opatch.mojo = (uint8_t)v;
    else if (c == 9u)
        opatch.mchar = (uint8_t)(v * B3_NCHAR / 128u);
    else if (c == 25u)
        opatch.vib = (uint8_t)(v * 6u / 128u);
    else if (c == 26u)
        opatch.drive = (uint8_t)v;
    else if (c == 28u)
        opatch.click = (uint8_t)v;
    else if (c == 29u)
        opatch.width = (uint8_t)v;
    else if (c == 7u) {                      /* (a setting: SAVE stays dark) */
        set.b3vol_off = (uint8_t)(127u - v);
        ui.redraw = 1;
        return;
    } else if (c == 1u || c == 27u) {
        if (c == 1u)
            ui.ofast = v >= 64u;
        else
            ui.obrake = v >= 64u;
        ui.redraw = 1;
        return;
    } else if (c == 11u) {                   /* (organ_block's, with the keys') */
        ui.oexpr_off = (uint8_t)(127u - v);
        return;
    } else
        return;
    organ_changed();
}

static const char *const OMAN[2] = {"UPPER", "LOWER"};
static uint32_t o_key(void) { return ui.olower ? opatch.lb : opatch.ub; }   /* the manual played's preset keys */
static const char *o_key_name(void)          /* its preset keys as said: A#, B, A#+B, CANCEL (held) */
{
    return ui.ocan >> ui.olower & 1u ? "CANCEL" : o_key() & 2u ? "A#+B" : o_key() & 1u ? "B" : "A#";
}
static uint8_t *o_bars(void)                 /* the drawbars the pages edit: the set the manual played sounds (A#+B:
                                              * the one EDIT held chose) */
{
    return ui.olower ? (opatch.lb & 1u ? opatch.lbar : opatch.la) : opatch.ub & 1u ? opatch.bar : opatch.ua;
}
/* the knobs: each page's (knob_turn's for the organ's pages), said as they go */
static void o_say_bar(uint32_t b)
{
    char t[4];
    t[0] = (char)('0' + o_bars()[b]), t[1] = 0;
    app_say(OBAR[b], t);
}
static void o_flag(uint8_t *f, int32_t e, const char *name, const char *off, const char *on)
{
    *f = e > 0;
    organ_changed();
    app_say(name, *f ? on : off);
}
static const char *const OSPD[3] = {"SLOW", "STOP", "FAST"};
#define NOLES 7u                             /* (ORGAN LESLIE's views) */
static const char *const OLES_V[NOLES] = {"CABINET", "ROTORS", "HORN ROTOR", "LOW ROTOR", "AMP", "NOISES", "MOJO"};
static const char *const ONOISE[4] = {"KEY THUDS", "HUM", "ROTOR NOISE", "CLUNKS"};
static const char *const OTRIM[8] = {"HORN RISE TIME", "HORN FALL TIME", "HORN SLOW SPEED", "HORN FAST SPEED",
                                     "LOW RISE TIME", "LOW FALL TIME", "LOW SLOW SPEED", "LOW FAST SPEED"};
static uint32_t o_speed(void) { return ui.obrake ? 1u : ui.ofast ? 2u : 0u; }
static void o_trim_text(uint32_t i, char *t)  /* a trimmer's value: a time in tenths of a second, a speed in RPM */
{
    const float f = b3_trim(i, opatch.les[i]);
    if (i & 2u) {
        int_text((int32_t)(f * 60.0f + 0.5f), t);
        text_cat(t, " RPM");
    } else {
        const int32_t d = (int32_t)(f * 10.0f + 0.5f);
        int_text(d / 10, t);
        text_cat(t, ".");
        int_text(d % 10, t + __builtin_strlen(t));
        text_cat(t, " S");
    }
}
static int o_turn(uint32_t k, int32_t e)
{
    if ((ui.page == PG_OTABS || ui.page == PG_OVIB || ui.page == PG_OLES) && (k == EN_SELECT || k == EN_PRESET)) {   /* (the cheeks': its
                                              * volume and its preset, as HOME's) */
        const uint8_t pg = ui.page;
        ui.page = PG_OHOME;
        o_turn(k, e);
        ui.page = pg;
        return 0;
    }
    if (ui.page == PG_OVIB) {                /* the left cheek: VOLUME, VIBRATO LOWER, VIBRATO UPPER, VIBRATO & CHORUS */
        if (k == EN_K1)
            o_flag(&opatch.vol_soft, e, "ORGAN VOLUME TAB", "NORMAL", "SOFT");
        else if (k == EN_K2)
            o_flag(&opatch.vib_lower, e, "VIBRATO LOWER", "OFF", "ON");
        else if (k == EN_K3)
            o_flag(&opatch.vib_on, e, "VIBRATO UPPER", "OFF", "ON");
        else if (k == EN_K4) {               /* (round its positions, V1 C1 V2 C2 V3 C3, all the way round as the
                                              * B-3's: C3 on to V1) */
            opatch.vib = (uint8_t)O_VAT((O_VPOS(opatch.vib) + (e > 0 ? 1u : 5u)) % 6u);
            organ_changed();
            app_say("VIBRATO CHORUS", OVIB[opatch.vib]);
        }
        return 0;
    }
    if (ui.page == PG_OHOME || ui.page == PG_OBARS) {
        static const int8_t V0[NE] = {[EN_SELECT] = -1, [EN_ALGO] = 0, [EN_PRESET] = -1, [EN_K1] = 1, 2, 3, 4},
                            V1[NE] = {[EN_SELECT] = -1, [EN_ALGO] = -1, [EN_PRESET] = -1, [EN_K1] = 5, 6, 7, 8};
        const int view = ui.page == PG_OBARS && ui.oview;
        const int32_t b = view ? V1[k] : V0[k];
        if (k == EN_SELECT) {                /* (its own volume, as every HOME's: upside down, b3vol_off) */
            char t[8];
            e = knob_wide(k, e);
            set.b3vol_off = (uint8_t)(127 - clamp(127 - (int32_t)set.b3vol_off + e, 0, 127));
            int_text(127u - set.b3vol_off, t);
            app_say("ORGAN VOLUME", t);
            return 0;
        }
        if (k == EN_PRESET) {                /* (the preset, on every view: its sound at once) */
            char t[8];
            organ_pick((uint32_t)clamp((int32_t)set.b3_slot + e, 0, ONSLOT - 1));
            slot_p_text(set.b3_slot, t);
            app_say(t, oname);
            return 0;
        }
        if (b < 0)
            return 0;
        o_bars()[b] = (uint8_t)clamp((int32_t)o_bars()[b] + (e > 0 ? 1 : -1), 0, 8);
        organ_changed();
        o_say_bar((uint32_t)b);
        return 0;
    }
    if (ui.page == PG_OTABS) {               /* the right cheek: the percussion's four */
        if (k == EN_K1)
            o_flag(&opatch.perc_on, e, "PERCUSSION", "OFF", "ON");
        else if (k == EN_K2)
            o_flag(&opatch.perc_soft, e, "PERC VOLUME", "NORMAL", "SOFT");
        else if (k == EN_K3)
            o_flag(&opatch.perc_fast, e, "PERC DECAY", "SLOW", "FAST");
        else if (k == EN_K4)
            o_flag(&opatch.perc_3rd, e, "PERC HARMONIC", "SECOND", "THIRD");
        return 0;
    }
    if (ui.page == PG_OLES) {                /* (its views': oles_draw) */
        char t[12];
        static const uint8_t VT[2][4] = {{2, 3, 6, 7}, {0, 1, 2, 3}};   /* (ROTORS': the speeds; CONTROLS': a row) */
        if (k == EN_ALGO) {                  /* the speed: SLOW, STOP (the brake), FAST */
            const int32_t v = clamp((int32_t)o_speed() + e, 0, 2);
            ui.obrake = v == 1, ui.ofast = v == 2;
            ui.redraw = 1;
            app_say("LESLIE", OSPD[v]);
        } else if (k >= EN_K1 && (ui.oles == 1u || ui.oles == 2u || ui.oles == 3u)) {   /* the trimmers (the preset's, as
                                              * GLO settings are) */
            const uint32_t j = ui.oles == 1u ? VT[0][k - EN_K1] : VT[1][k - EN_K1] + (ui.oles == 3u ? 4u : 0u);
            opatch.les[j] = (uint8_t)clamp((int32_t)opatch.les[j] + knob_wide(k, e), 0, 127);
            organ_changed();
            o_trim_text(j, t);
            app_say(OTRIM[j], t);
        } else if (ui.oles == 5u && k >= EN_K1) {   /* NOISES: the organ's four (settings, every preset's) */
            uint8_t *v = &set.b3_noise[k - EN_K1];
            *v = (uint8_t)clamp((int32_t)*v + knob_wide(k, e), 0, 127);
            int_text(*v, t);
            ui.redraw = 1;
            app_say(ONOISE[k - EN_K1], t);
        } else if (ui.oles == 6u) {          /* MOJO: this organ's quirks (the preset's; K3 and K4: the knobs right to
                                              * left) */
            if (k == EN_K3) {
                opatch.mojo = (uint8_t)clamp((int32_t)opatch.mojo + knob_wide(k, e), 0, 127);
                organ_changed();
                int_text((int32_t)opatch.mojo * 100 / 127, t);
                text_cat(t, "%");
                app_say("MOJO", t);
            } else if (k == EN_K4) {         /* its CHARACTER: a quirk alone, or several */
                opatch.mchar = (uint8_t)clamp((int32_t)opatch.mchar + (e > 0 ? 1 : -1), 0, B3_NCHAR - 1);
                organ_changed();
                app_say("CHARACTER", b3_mojo_name(opatch.mchar));
            }
        } else if (ui.oles == 4u && k == EN_K2) {   /* KEY SWELL, its rest (the preset's) */
            opatch.swell = e > 0;
            organ_changed();
            app_say("KEY SWELL", OFF_ON[opatch.swell]);
        } else if (ui.oles == 4u && k == EN_K3) {
            opatch.swell_rest = (uint8_t)clamp((int32_t)opatch.swell_rest + knob_wide(k, e), 0, 127);
            int_text((int32_t)opatch.swell_rest * 100 / 127, t);
            text_cat(t, "%");
            organ_changed();
            app_say("SWELL REST", t);
        } else if (k == EN_K4) {             /* (its volume: a setting, kept with GLO's; SAVE stays dark) */
            set.b3vol_off = (uint8_t)(127 - clamp(127 - (int32_t)set.b3vol_off + knob_wide(k, e), 0, 127));
            int_text(127 - (int32_t)set.b3vol_off, t);
            ui.redraw = 1;
            app_say("ORGAN VOLUME", t);
        } else if (k == EN_K1 || (ui.oles == 0u && (k == EN_K2 || k == EN_K3))) {   /* the CABINET's WIDTH, DRIVE,
                                              * CLICK; the AMP's DRIVE */
            uint8_t *v = ui.oles == 4u || k == EN_K2 ? &opatch.drive : k == EN_K1 ? &opatch.width : &opatch.click;
            *v = (uint8_t)clamp((int32_t)*v + knob_wide(k, e), 0, 127);
            organ_changed();
            int_text(*v, t);
            app_say(v == &opatch.width ? "LESLIE WIDTH" : v == &opatch.drive ? "DRIVE" : "KEY CLICK", t);
        }
    }
    return 0;
}

/* Hints (GLO DISPLAY's): each knob's here, as the page and its view have it (hint_of's for the organ's pages); 0 none */
static const char *o_hint(uint32_t k)
{
    static const char *const FT[9] = {"16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'"};
    static const char *const LES[NOLES][4] = {
        {"Leslie stereo width", "Preamp drive", "Key click", "Organ's volume"},
        {"Horn's slow speed", "Horn's fast speed", "Drum's slow speed", "Drum's fast speed"},
        {"Horn's spin-up time", "Horn's slow-down time", "Horn's slow speed", "Horn's fast speed"},
        {"Drum's spin-up time", "Drum's slow-down time", "Drum's slow speed", "Drum's fast speed"},
        {"Amp's drive", "Key pressure opens swell", "Swell with no pressure", "Organ's volume"},
        {"Key thuds, all presets", "Floor hum, all presets", "Rotor noise, all presets", "Drawbar clunks, all"},
        {0, 0, "This organ's quirks", "Which quirks: one or a mix"}};
    static char t[32];
    if (k == EN_SELECT && ui.page != PG_OLES)
        return "Organ's volume";
    if (k == EN_PRESET && ui.page != PG_OLES)
        return "Pick the preset";
    if (ui.page == PG_OHOME || ui.page == PG_OBARS) {   /* (the manual played's set sounding: its drawbar) */
        const int32_t b = ui.page == PG_OBARS && ui.oview ? (k >= EN_K1 ? (int32_t)(k - EN_K1) + 5 : -1)
                        : k == EN_ALGO ? 0 : k >= EN_K1 ? (int32_t)(k - EN_K1) + 1 : -1;
        if (b < 0)
            return 0;
        t[0] = 0;
        text_cat(t, ui.olower ? "Lower " : "Upper ");
        text_cat(t, o_key_name());
        text_cat(t, " ");
        text_cat(t, FT[b]);
        text_cat(t, " drawbar");
        return t;
    }
    if (ui.page == PG_OVIB)
        return k == EN_K1 ? "Volume tab: soft, -6 dB" : k == EN_K2 ? "Vibrato on the lower"
             : k == EN_K3 ? "Vibrato on the upper" : k == EN_K4 ? "Vibrato or chorus, depth" : 0;
    if (ui.page == PG_OTABS)
        return k == EN_K1 ? "Percussion, upper B only" : k == EN_K2 ? "Percussion soft"
             : k == EN_K3 ? "Percussion fast decay" : k == EN_K4 ? "Percussion 2nd or 3rd" : 0;
    if (ui.page == PG_OLES)
        return k == EN_ALGO ? "Leslie slow, stop, fast" : k == EN_SELECT || k == EN_PRESET ? (k == EN_SELECT ? "Organ's volume" : "Pick the preset")
             : k >= EN_K1 && ui.oles < NOLES ? LES[ui.oles][k - EN_K1] : 0;
    return 0;
}

/* SAVE, as the bass's (save_button): its HOME's other view, the slot blinking on its display; PRESET picks; SAVE again
 * stores the sound and its name there */
static void o_save_button(void)
{
    if (!ui.save_to) {
        ui.oview = 1;
        if (ui.page != PG_OBARS)
            page_go(PG_OBARS);
        save_start(1, set.b3_slot);
    } else
        save_commit();
}
static void o_store(uint32_t s)              /* (save_commit's: the sound and its name into slot s) */
{
    uint32_t i;
    name_trim(oname);
    opatch.speed = (uint8_t)o_speed();      /* (the Leslie's speed now, the preset's to start with) */
    obank.p[s] = opatch;
    if (mo.n)                                /* (the drawbars as the hand left them, under the motion loop's) */
        for (i = 0; i < 36u; i++)
            o_bars_of(&obank.p[s], i / 9u)[i % 9u] = mo.hv[MO_ORG + i];
    memcpy(obank.name[s], oname, PNAME_N);
    set.b3_slot = (uint8_t)s;
    if (set_store() == 0 && store_save(OBJ_ORGAN, &obank, sizeof obank) == 0) {   /* (the settings too: the slot
                                              * it plays from now on, as the bass's SAVE) */
        char t[8];
        odirty = 0;
        slot_n_text(s, t);
        app_say("ORGAN SAVED", t);
    } else
        app_say("SAVE", "FAILED");
}

/* its buttons (but HOME, REC, GLO, ARP and the octaves: hold_tap and hold_step have ARP) */
static void organ_leslie_tap(void)           /* ARP: chorale or tremolo (the brake let go) */
{
    ui.ofast = (uint8_t)!ui.ofast;
    ui.obrake = 0;
    ui.redraw = 1;
    app_say("LESLIE", ui.ofast ? "FAST" : "SLOW");
}
static void o_manual_say(void)               /* the manual, its preset key: "UPPER B", "LOWER A#" */
{
    app_say(OMAN[ui.olower], o_key_name());
}
static void organ_manual(void)               /* SEL held: the other manual, the keys' and the pages' */
{
    ui.olower = (uint8_t)!ui.olower;
    ui.redraw = 1;
    o_manual_say();
}
static void organ_ab(void)                   /* EDIT held: the manual's other preset key, A# or B (A#+B: the other
                                              * one edited) */
{
    uint8_t *const f = ui.olower ? &opatch.lb : &opatch.ub;
    *f = (uint8_t)(*f ^ 1u);
    organ_changed();
    o_manual_say();
}
static void organ_both(void)                 /* EDIT held, OCT+: A#+B, the manual's two preset keys down at once */
{
    uint8_t *const f = ui.olower ? &opatch.lb : &opatch.ub;
    *f = (uint8_t)(*f ^ 2u);
    organ_changed();
    o_manual_say();
}
static void organ_brake(void)                /* ARP held: the brake */
{
    ui.obrake = 1;
    ui.redraw = 1;
    app_say("LESLIE", "BRAKE");
}
static void o_tab_dive(uint32_t g, uint32_t k);
static void o_button(uint32_t k)
{
    if (k == B_SEL) {                        /* HOME's view, its other, TABS, LESLIE, PATCH NAME, round */
        if (zoom.home && zoom.hp == PG_OHOME)   /* (on HOME's dive, or its way back: HOME's SEL) */
            ui.page = PG_OHOME, ui.oview = 0;
        if (ui.page == PG_OHOME)             /* (from HOME: the manual played first, then the other) */
            ui.osel2 = 0;
        if (ui.page == PG_OBARS && (!ui.oview || !ui.osel2)) {   /* (its other view; then the other manual's two) */
            if (ui.oview)
                ui.olower = (uint8_t)!ui.olower, ui.osel2 = 1;
            ui.oview = (uint8_t)!ui.oview;
            ui.redraw = 1;
            app_say(ui.olower ? "LOWER DRAWBARS" : "UPPER DRAWBARS", ui.oview ? "2' TO 1'" : "16' TO 2 2/3'");
        } else {
            const uint32_t g = ui.page == PG_OHOME ? PG_OBARS : ui.page == PG_OBARS ? PG_NAME   /* (VIBRATO, PERCUSSION
                                              * and the LESLIE: FX's, ENV's and LFO's, not SEL's) */
                             : PG_OHOME;
            ui.oview = ui.osel2 = 0;         /* (the manual last shown: the one played) */
            sect_open(g, k);
        }
    } else if (k == B_SAVE)
        o_save_button();
    else if (k == B_FX && ui.page != PG_OVIB)   /* the sections, as the bass's, left to right as the console's: FX
                                              * VIBRATO (an effect), ENV PERCUSSION, LFO the LESLIE (from HOME: the
                                              * camera in on its cheek); again there, its main switch */
        sect_open(PG_OVIB, k);
    else if (k == B_ENV && ui.page != PG_OTABS)
        sect_open(PG_OTABS, k);
    else if (k == B_LFO && ui.page != PG_OLES) {
        ui.oles = 0;
        sect_open(PG_OLES, k);
    } else if (k == B_LFO) {                 /* (LFO again: its views round) */
        ui.oles = (uint8_t)((ui.oles + 1u) % NOLES);
        ui.redraw = 1;
        app_say("LESLIE", OLES_V[ui.oles]);
    }
    else if (k == B_ENV) {
        opatch.perc_on = (uint8_t)!opatch.perc_on;
        organ_changed();
        app_say("PERCUSSION", OFF_ON[opatch.perc_on]);
    } else if (k == B_FX) {
        opatch.vib_on = (uint8_t)!opatch.vib_on;
        organ_changed();
        app_say("VIBRATO UPPER", OFF_ON[opatch.vib_on]);
    } else if (k == B_EDIT) {                /* (the manual's own: VIBRATO UPPER or LOWER) */
        uint8_t *const v = ui.olower ? &opatch.vib_lower : &opatch.vib_on;
        *v = (uint8_t)!*v;
        organ_changed();
        app_say(ui.olower ? "VIBRATO LOWER" : "VIBRATO UPPER", OFF_ON[*v]);
        o_tab_dive(PG_OVIB, ui.olower ? EN_K2 : EN_K3);
    } else
        app_say("ORGAN", "SEL: ITS PANELS");
}

/* its pages: the walnut, ivory and gold of the console (b3-front-closeup.jpeg's) */
#define O_WOOD RGB(106, 52, 24)
#define O_IVORY RGB(240, 234, 220)
#define O_GOLD RGB(200, 164, 84)
static void o_wood(void)                     /* (TABS', LESLIE's) */
{
    int32_t y;
    for (y = 0; y < TALL_H; y++)             /* (the grain: rows a shade darker or lighter, by a hash) */
        cv_rect(0, y, 240, 1, ((uint32_t)y * 2654435761u >> 28) == 0u ? RGB(90, 42, 20)
                              : ((uint32_t)y * 2654435761u >> 28) == 15u ? RGB(122, 62, 32) : O_WOOD);
}
/* ORGAN HOME: as the other HOMEs, its name, the display (the preset's name under it), what plays; then the console from
 * the player's seat, as b3-front-closeup.jpeg (2560 x 1707) has it, its rows set out to fit: the rack's ledge, the rail
 * with the drawbars (OH_SET: the upper manual's A# and B sets, the lower's, the pedals' two left out; each tip
 * lower by its setting, its shaft over it; the sets not sounding dimmed; their preset keys over them, oh_keys); at the keys' level the cheeks: the left's vibrato knob and tabs (the first the vibrato, down: on), the
 * right's percussion tabs (down: on), the name board between; under them the upper manual, a second roll of keys over
 * the footer's (the FM-1's: the lower manual). ORGAN DRAWBARS is HOME's own console twice as big (oz), its left half
 * from the ledge down, under the same head: the dive in from HOME lands on what it showed */
#define OH_RAIL 75                           /* the rail's top; the tips at rest under it, a stop 3 px further */
#define OH_ROLL (TALL_H - 29)                /* the upper manual's keys (as the footer's: blacks 13, whites 14 under) */
#define OH_CHEEK (OH_ROLL - 22)              /* the cheeks' row (its top 2 over), the drawbars' tips all over it */
#define O_KNOB_X 52                          /* the VIBRATO AND CHORUS knob, right of the left cheek's three tabs (as the
                                              * mk2's photo has it) */
#define OZ_Y0 (OH_RAIL - 2)                  /* (DRAWBARS: HOME's row at its ledge's foot on the head's: a tip at
                                              * 8 to the page's foot) */
#define OZ_Y 72
static int32_t oz = 1, oz_x0, oz_y0, oz_y;   /* the console's scale; HOME's column oz_x0 at the page's left, its row
                                              * oz_y0 at the page's oz_y */
#define OZX(x) (((x) - oz_x0) * oz)
#define OZY(y) (((y) - oz_y0) * oz + oz_y)
static void oh_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { cv_rect(OZX(x), OZY(y), w * oz, h * oz, c); }
static void oh_tab(int32_t cx, int on, const char *top, const char *bot, const char *n1, const char *n2, uint32_t k)
{                                            /* a cheek's rocker tab, its middle at cx: its top pressed (on) or its
                                              * bottom; up close its print (top, bottom, its name over it) and its tag */
    const int32_t x0 = OZX(cx - 4), w = 9 * oz, ty = OZY(OH_CHEEK + 6 + (on ? 3 : 0)), th = 13 * oz,
                  lip = (on ? 3 : 1) * oz, rad = oz < 2 ? 1 : oz, mid = th / 2;
    int32_t r;
    oh_rect(cx - 4, OH_CHEEK + 6, 9, 16, RGB(10, 10, 10));   /* its well */
    for (r = 0; r < th; r++) {               /* its ivory face, a rocker (b3-front-closeup): the pressed half tipped
                                              * away, in shade, the raised half lit, its outer end glossy; the
                                              * corners round, the sides a shade darker, the right's darkest */
        const int32_t e = r < rad ? rad - r : r >= th - rad ? r - (th - rad) + 1 : 0,
                      down = on ? r < mid : r >= mid, d = down ? (on ? mid - r : r - mid) : 0,
                      f = down ? 86 - 10 * d / (mid ? mid : 1) : 100 - (on ? 0 : 3 * r / th);
        const int32_t gl = !down && (on ? r == th - 2 * oz : r == oz);   /* (its gloss) */
        cv_rect(x0 + e, ty + r, w - 2 * e, 1, gl ? RGB(255, 254, 246) : RGB(237 * f / 100, 235 * f / 100, 222 * f / 100));
        if (w - 2 * e > 2) {
            cv_pset(x0 + e, ty + r, RGB(214 * f / 100, 211 * f / 100, 196 * f / 100));
            cv_pset(x0 + w - 1 - e, ty + r, RGB(176 * f / 100, 172 * f / 100, 158 * f / 100));
        }
    }
    cv_rect(x0 + 1, ty + mid, w - 2, 1, RGB(170, 166, 152));   /* (its pivot's crease) */
    for (r = 0; r < lip; r++) {              /* its front's edge under it, in shadow to its foot */
        const int32_t g = 150 - 90 * r / (lip > 1 ? lip - 1 : 1);
        cv_rect(x0 + 1, ty + th + r, w - 2, 1, RGB(g, g - 2, g - 10));
    }
    if (oz >= 3) {                           /* (the cheeks' pages alone: DRAWBARS shows none of their print) */
        const int32_t x = OZX(cx), y = OZY(OH_CHEEK + 6 + (on ? 3 : 0));
        cv_rect(x - 13, on ? y + 1 : y + 13 * oz - 10, 26, 9, P_CYAN);   /* (its setting: its word lit) */
        se_lbl(x, y + 3, top, on ? RGB(10, 10, 10) : RGB(150, 146, 138));
        se_lbl(x, y + 13 * oz - 8, bot, on ? RGB(150, 146, 138) : RGB(10, 10, 10));
        se_lbl(x, OZY(OH_CHEEK + 6) - 14, n1, O_GOLD);
        if (n2)
            se_lbl(x, OZY(OH_CHEEK + 6) - 7, n2, O_GOLD);
        if (x >= 0 && x < 240)              /* (the other cheek's, off the page: not tagged over this one's) */
            tag(x, OZY(OH_CHEEK + 22) - 10, k), tag_low();   /* (its hint under the cheek, off its print) */
    }
}
static void oh_bar(int32_t cx, uint32_t v, uint32_t col, int dim, const char *ft, uint32_t k)
{                                            /* a drawbar at HOME's x, pulled to v; dimmed; up close its footage, its
                                              * encoder's tag */
    static const uint16_t TIP[3] = {RGB(92, 44, 32), RGB(248, 244, 231), RGB(20, 18, 16)},
                          TOP[3] = {RGB(150, 86, 70), RGB(255, 255, 255), RGB(100, 100, 100)},
                          TC[3] = {O_IVORY, RGB(30, 24, 20), O_IVORY};
    const int32_t y = OH_RAIL + 2 + 3 * (int32_t)v;
    int32_t i;
    if (v) {                                 /* its shaft, a line a stop */
        oh_rect(cx - 1, OH_RAIL + 2, 3, y - OH_RAIL - 2, dim ? grey(70) : RGB(206, 202, 196));
        for (i = 1; i <= (int32_t)v; i++)
            oh_rect(cx - 1, y - 3 * i + 1, 3, 1, dim ? grey(40) : RGB(70, 66, 62));
    }
    oh_rect(cx - 2, y, 5, 6, dim ? grey(48) : TIP[col]);
    oh_rect(cx - 2, y, 5, 1, dim ? grey(60) : TOP[col]);
    if (oz > 1) {
        print_c(OZX(cx), OZY(y) + 4, ft, dim ? grey(30) : TC[col]);
        if (k < NE)
            tag(OZX(cx), OZY(y + 4), k);
    }
}
static void oh_head(const char *line2, const char *cyan)   /* the HOMEs' head: the name and its line, what plays; the
                                              * display at the right, the preset's name under it */
{
    char d[4], st[40] = "";
    const char *nm = oname[0] ? oname : "ORGAN";
    cv_rect(0, 0, 240, OZ_Y, P_BG);
    cv_text(10, 12, &FONT_B, "ORGAN", P_SILK);
    cv_text(cv_text(10, 30, &FONT_B, line2, P_SILK) + 5, 30, &FONT_B, cyan, P_BAR);
    cv_rect(10, 49, 116, 2, P_BAR);
    const uint32_t to = ui.save_who ? ui.save_to : 0u;   /* (saving: its slot blinking) */
    if (to && plat_ms() / SAVE_BLINK_MS & 1u)
        d[0] = d[1] = d[2] = ' ', d[3] = 0;
    else
        slot_p_text(to ? to - 1u : set.b3_slot, d);
    seg_small(232, 8, d);
    if (odirty)
        disc(232 - 6, 8 + SEG_SH - 6, 2, RGB(252, 78, 112));
    cv_text(232 - text_w(&FONT_XS, nm), 8 + SEG_SH + 3, &FONT_XS, nm, P_CYAN);   /* (its right edge the display's) */
    tag(232 - SEG_SW / 2, 8 + SEG_SH / 2, EN_PRESET);
    {                                        /* its own volume (SELECT), under the name */
        char v[16] = "VOLUME ";
        int_text(127u - set.b3vol_off, v + 7);
        cv_text(232 - text_w(&FONT_T, v), 8 + SEG_SH + 16, &FONT_T, v, P_SILK);
        tag(232 - text_w(&FONT_T, v) / 2, 8 + SEG_SH + 20, EN_SELECT);
    }
    if (opatch.swell || ui.oexpr_off) {        /* the swell pedal, side on, when it's played (KEY SWELL, MIDI's CC 11):
                                              * toe down open, its heel on its pivot; how open under it */
        char v[8] = "";
        const int32_t a = 127 - (int32_t)o_swell;   /* (0 open: the toe 3 px down; closed: 13 up) */
        int32_t i;
        cv_rect(134, 31, 34, 4, RGB(60, 58, 56));
        for (i = 0; i < 3; i++)
            cv_line(136, 28 + i, 166, 31 + i - a * 16 / 127, i ? RGB(30, 30, 30) : RGB(110, 110, 110));
        disc(137, 30, 2, RGB(150, 150, 150));
        int_text((int32_t)o_swell * 100 / 127, v);
        text_cat(v, "%");
        cv_text(151 - text_w(&FONT_XS, v) / 2, 37, &FONT_XS, v, o_swell < 127u ? P_CYAN : P_SILK);
    }
    {                                        /* the manual played, its preset key; the Leslie, its vibrato, the
                                              * percussion (the upper's B set's) */
        char mk[12] = "";
        int32_t x;
        text_cat(mk, OMAN[ui.olower]);
        text_cat(mk, " ");
        text_cat(mk, o_key_name());
        x = cv_text(10, 8 + SEG_SH + 16, &FONT_T, mk, P_CYAN);   /* (on VOLUME's line) */
        text_cat(st, ui.obrake ? "  LESLIE BRAKE" : ui.ofast ? "  LESLIE FAST" : "  LESLIE SLOW");
        text_cat(st, "  ");
        text_cat(st, (ui.olower ? opatch.vib_lower : opatch.vib_on) ? OVIB[opatch.vib] : "VIB OFF");
        if (opatch.perc_on && (opatch.ub & 3u))
            text_cat(st, opatch.perc_3rd ? "  PERC 3RD" : "  PERC 2ND");
        cv_text(x, 8 + SEG_SH + 16, &FONT_T, st, P_SILK);
    }
}
#define OH_SET(i) (9 + 57 * ((int32_t)(i) & 1) + 120 * ((int32_t)(i) >> 1))   /* a set's first bar on HOME: the upper
                                              * manual's A# and B sets in its left half, the lower's in its right (as
                                              * the B-3's, the pedals' two between them left out) */
#define OH_BX 6                              /* (its bars apart) */
static const uint8_t *o_set(uint32_t i)      /* set i's drawbars: the upper's A#, B, the lower's A#, B */
{
    return i == 0u ? opatch.ua : i == 1u ? opatch.bar : i == 2u ? opatch.la : opatch.lbar;
}
static int o_sounds(uint32_t i)              /* set i's preset key down (A#+B: both; CANCEL held: neither) */
{
    const uint32_t key = (i >> 1) ? opatch.lb : opatch.ub;
    return !(ui.ocan >> (i >> 1) & 1u) && ((key & 2u) || (i & 1u) == (key & 1u));
}
static void oh_keys(int32_t z, int32_t x0)   /* over each set its preset key, A# or B, lit while down (HOME's scale z,
                                              * its column x0 at the page's left) */
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        const int32_t cx = (OH_SET(i) + 4 * OH_BX - x0) * z;
        const char *const n = i & 1u ? "B" : "A#";
        if (cx > 0 && cx < 240)
            cv_text(cx - text_w(&FONT_XS, n) / 2, 64, &FONT_XS, n,
                    o_sounds(i) ? ((i >> 1) == ui.olower ? P_CYAN : P_SILK) : grey(70));
    }
}
static void oh_face(int dim0, int dim1, const uint8_t *keys)   /* the console at oz: the manual played's set sounding,
                                                  * its bars dim0..dim1-1 dimmed, keys their encoders (up close); the
                                                  * sets not sounding dimmed */
{
    static const uint8_t COL[9] = {0, 0, 1, 1, 2, 1, 2, 2, 1};   /* the tips: brown, ivory, black */
    static const char *const FT[9] = {"16'", "5'", "8'", "4'", "2'", "2'", "1'", "1'", "1'"};
    static const uint8_t AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
    int32_t i, b;
    if (oz < 3) {                            /* (the cheeks up close: their row alone) */
    oh_rect(0, OH_RAIL - 4, 240, 3, RGB(120, 57, 34));   /* the rack's ledge, walnut */
    oh_rect(0, OH_RAIL - 1, 240, 1, RGB(90, 31, 13));
    oh_rect(0, OH_RAIL, 240, OH_ROLL - OH_RAIL, RGB(39, 36, 33));   /* the rail, the tips' slot */
    oh_rect(0, OH_RAIL, 240, 2, RGB(12, 10, 8));
    for (i = 0; i < 4; i++) {
        const int ed = (uint32_t)i >> 1 == ui.olower && o_sounds((uint32_t)i);   /* (the set the pages edit) */
        for (b = 0; b < 9; b++)
            oh_bar(OH_SET(i) + b * OH_BX, o_set((uint32_t)i)[b], COL[b], !o_sounds((uint32_t)i) || (ed && b >= dim0 && b < dim1),
                   FT[b], ed && keys ? keys[b] : NE);
    }
    }
    oh_rect(0, OH_CHEEK - 2, 240, OH_ROLL - OH_CHEEK + 2, RGB(16, 14, 12));   /* the cheeks' row, the name board */
    if (oz > 1) {                            /* (up close: its black flecked, as the photo's) */
        uint32_t n, h = 0x9E3779B9u;
        const int32_t y0 = OZY(OH_CHEEK - 2), hh = (OH_ROLL - OH_CHEEK + 2) * oz;
        for (n = 0; n < 900u; n++) {
            int32_t x, y;
            h = h * 1664525u + 1013904223u;
            x = OZX(0) + (int32_t)((h >> 8) % (uint32_t)(240 * oz)), y = y0 + (int32_t)((h >> 20) % (uint32_t)hh);
            if (x >= 0 && x < 240)
                cv_pset(x, y, n & 1u ? RGB(52, 48, 44) : RGB(34, 31, 28));
        }
    }
    {                                        /* the vibrato's knob, from above (b3-top): a black square plate, its
                                              * print V1 C1 V2 C2 V3 C3 at 4, 6, 8, 10, 12 and 2 o'clock; the knob
                                              * black, glossy, its own print on its top, a white tick at its rim */
        const int32_t kx = OZX(O_KNOB_X), ky = OZY(OH_CHEEK + 12), kr = 6 * oz;
        const float a = O_VANG(O_VPOS(opatch.vib));
        oh_rect(O_KNOB_X - 10, OH_CHEEK + 2, 21, 21, RGB(22, 21, 20));   /* (the plate, its edge lit at the top) */
        oh_rect(O_KNOB_X - 10, OH_CHEEK + 2, 21, 1, RGB(44, 42, 40));
        oh_rect(O_KNOB_X - 10, OH_CHEEK + 22, 21, 1, RGB(8, 8, 8));
        disc(kx + oz, ky + oz + 1, kr, P_KSHADOW);
        disc(kx, ky, kr, RGB(6, 6, 6));      /* (its rim) */
        if (oz > 1) {
            shade_disc(kx, ky, kr - oz / 2 - 1, 22, 30);   /* (its top) */
            disc(kx - kr / 2, ky - kr / 2, oz / 2 + 1, grey(110));   /* (its gloss) */
            cv_pset(kx - kr / 2, ky - kr / 2, grey(220));
        } else
            disc(kx, ky, kr - 1, RGB(30, 28, 26));
        cv_line(kx + (int32_t)(fm_cosf(a) * (float)kr * 0.6f), ky + (int32_t)(fm_sinf(a) * (float)kr * 0.6f),
                kx + (int32_t)(fm_cosf(a) * (float)(kr - 1)), ky + (int32_t)(fm_sinf(a) * (float)(kr - 1)), RGB(240, 238, 228));
        if (oz > 2) {                        /* (its tick a stripe up close) */
            const int32_t px = fm_sinf(a) > 0.7f || fm_sinf(a) < -0.7f, py = !px;   /* (beside it, across its way) */
            cv_line(kx + (int32_t)(fm_cosf(a) * (float)kr * 0.6f) + px, ky + (int32_t)(fm_sinf(a) * (float)kr * 0.6f) + py,
                    kx + (int32_t)(fm_cosf(a) * (float)(kr - 1)) + px, ky + (int32_t)(fm_sinf(a) * (float)(kr - 1)) + py,
                    RGB(240, 238, 228));
        }
    }
    if (oz >= 3) {                           /* (up close: the plate's print round it, the knob's on it, its tag) */
        static const char *const VC[6] = {"V1", "C1", "V2", "C2", "V3", "C3"};
        uint32_t i;
        for (i = 0; i < 6u; i++) {
            const float a = O_VANG(i);
            const uint32_t v = O_VAT(i);
            se_lbl(OZX(O_KNOB_X) + (int32_t)(fm_cosf(a) * 8.4f * (float)oz), OZY(OH_CHEEK + 12) + (int32_t)(fm_sinf(a) * 8.4f * (float)oz) - 2,
                   VC[i], v == opatch.vib ? P_CYAN : O_IVORY);
        }
        se_lbl(OZX(O_KNOB_X), OZY(OH_CHEEK + 12) - 9, "VIBRATO", O_IVORY);
        se_lbl(OZX(O_KNOB_X), OZY(OH_CHEEK + 12) - 2, "AND", O_IVORY);
        se_lbl(OZX(O_KNOB_X), OZY(OH_CHEEK + 12) + 5, "CHORUS", O_IVORY);
        if (OZX(O_KNOB_X) >= 0 && OZX(O_KNOB_X) < 240)   /* (on the page: not the right cheek's) */
            tag(OZX(O_KNOB_X), OZY(OH_CHEEK + 22) - 10, EN_K4), tag_low();   /* (its hint under the knob, as the tabs') */
    }
    oh_tab(7, !opatch.vol_soft, "NORMAL", "SOFT", "VOLUME", 0, EN_K1);
    oh_tab(21, !opatch.vib_lower, "OFF", "ON", "VIBRATO", "LOWER", EN_K2);
    oh_tab(35, !opatch.vib_on, "OFF", "ON", "VIBRATO", "UPPER", EN_K3);
    if (oz == 1)
        print_c(120, OH_CHEEK + 5, "ORGAN SOMETHING", O_GOLD);
    oh_tab(184, opatch.perc_on, "ON", "OFF", "PERCUSSION", 0, EN_K1);
    oh_tab(199, opatch.perc_soft, "SOFT", "NORMAL", "PERCUSSION", "VOLUME", EN_K2);
    oh_tab(214, opatch.perc_fast, "FAST", "SLOW", "PERCUSSION", "DECAY", EN_K3);
    oh_tab(229, opatch.perc_3rd, "THIRD", "SECOND", "HARMONIC", "SELECTOR", EN_K4);
    if (oz < 3) {                            /* the upper manual: as the footer's keys (the lower's), those it plays
                                              * held lit to their velocity, as the footer's are */
        static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
        const uint32_t up = o_up_roll();
        for (i = 0; i < 16; i++) {
            oh_rect(8 + i * 14, OH_ROLL + 14, 13, 14, P_KEYW);
            if (up >> WK[i] & 1u)
                oh_rect(8 + i * 14, OH_ROLL + 28 - (ui.vel[WK[i]] * 14 + 126) / 127, 13, (ui.vel[WK[i]] * 14 + 126) / 127, P_CYAN);
            else if (ui.olit >> WK[i] & 1u)  /* (a note it sounds from MIDI in or its part: all lit) */
                oh_rect(8 + i * 14, OH_ROLL + 14, 13, 14, P_CYAN);
        }
        for (i = 0; i < 11; i++) {
            oh_rect(8 + (int32_t)AT[i] * 14 + 7, OH_ROLL, 13, 13, P_EDGE);
            oh_rect(8 + (int32_t)AT[i] * 14 + 8, OH_ROLL, 11, 12, P_KEYB);
            if (up >> BK[i] & 1u)
                oh_rect(8 + (int32_t)AT[i] * 14 + 8, OH_ROLL + 12 - (ui.vel[BK[i]] * 12 + 126) / 127, 11,
                        (ui.vel[BK[i]] * 12 + 126) / 127, P_CYAN);
            else if (ui.olit >> BK[i] & 1u)
                oh_rect(8 + (int32_t)AT[i] * 14 + 8, OH_ROLL, 11, 12, P_CYAN);
        }
    }
}
static void ohome_draw(void)
{
    oz = 1, oz_x0 = oz_y0 = oz_y = 0;
    cv_rect(0, 0, 240, TALL_H, P_BG);
    oh_face(9, 9, 0);
    oh_head("SOMETHING", "B3");
    oh_keys(1, 0);
}
static void zoom_organ(uint32_t k)           /* ORGAN HOME's drawbar turned: the camera in on DRAWBARS (its middle: HOME's
                                              * x 60, the row under the page's 80) */
{
    zoom_dive(PG_OHOME, PG_OBARS, k, 0, ui.olower ? 180 : 60, OZ_Y0 + (80 - OZ_Y) / 2, 1.0f);
}
static void obars_draw(void)                 /* ORGAN DRAWBARS: HOME's console twice as big; its other view the last three */
{
    static const uint8_t K0[9] = {EN_ALGO, EN_K1, EN_K2, EN_K3, EN_K4, NE, NE, NE, NE},
                         K1[9] = {NE, NE, NE, NE, NE, EN_K1, EN_K2, EN_K3, EN_K4};
    oz = 2, oz_x0 = ui.olower ? 120 : 0, oz_y0 = OZ_Y0, oz_y = OZ_Y;   /* (the manual played's half of HOME's rail) */
    cv_rect(0, 0, 240, TALL_H, P_BG);
    oh_face(ui.oview ? 0 : 5, ui.oview ? 5 : 9, ui.oview ? K1 : K0);
    oz = 1, oz_x0 = oz_y0 = oz_y = 0;
    oh_head(OMAN[ui.olower], o_key_name());
    oh_keys(2, ui.olower ? 120 : 0);
}
/* the sections of ORGAN HOME its pages show up close (SEL's open from HOME, HOME's back to it, the live buttons' dives):
 * DRAWBARS its rail's left, VIBRATO and PERCUSSION its cheeks; the middle on HOME, the scale (log2) it fills the screen at */
static void o_sect(uint32_t g, int16_t *x, int16_t *y, float *lc)
{
    *x = g == PG_OBARS ? (ui.olower ? 180 : 60) : g == PG_OVIB ? 35 : 207;
    *y = g == PG_OBARS ? OZ_Y0 + (80 - OZ_Y) / 2 : OH_CHEEK - 2 + (80 - 72) / 3;
    *lc = g == PG_OBARS ? 1.0f : 1.585f;
}
static void o_tab_dive(uint32_t g, uint32_t k)   /* a live button's tab on HOME: the camera in on its cheek and back */
{
    int16_t x, y;
    float lc;
    if (ui.page != PG_OHOME || set.zoom_off)
        return;
    o_sect(g, &x, &y, &lc);
    zoom_dive(PG_OHOME, g, k, 0, x, y, lc);
}
/* ORGAN VIBRATO and ORGAN PERCUSSION: HOME's cheeks four times as big, the left's (VOLUME, VIBRATO LOWER and UPPER,
 * VIBRATO AND CHORUS) and the right's (the percussion's four), as the B-3's are printed; under HOME's head */
static void ocheek_draw(int right)
{
    oz = 3, oz_x0 = right ? 167 : -5, oz_y0 = OH_CHEEK - 2, oz_y = 72;
    cv_rect(0, 0, 240, TALL_H, RGB(16, 14, 12));   /* (the cheek's black, round its row) */
    oh_face(9, 9, 0);
    oz = 1, oz_x0 = oz_y0 = oz_y = 0;
    oh_head(right ? "PERCUSSION" : "VIBRATO", "");
}
static void o_switch(int32_t cx, int32_t cy, uint32_t k, int on, const char *n0, const char *n1, const char *name)
{                                            /* a two-way switch lying down: its name over it, its setting under it */
    print_c(cx, cy - 22, name, P_SILK);
    slide_h(cx - 16, cy - 9, 2, on != 0, knob_lit(k) ? KF_LIT : 0u);
    tag(cx, cy, k);
    print_c(cx, cy + 12, on ? n1 : n0, P_CYAN);
}
/* ORGAN LESLIE: the Leslie 122 (Leslie_122H_Heritage_Series_Rotary_Speaker.webp and the photos of its inside), under
 * HOME's head; LFO there its views round (ui.oles): the CABINET from the front, its ROTORS turning (the horn's shelf,
 * its belt and pulleys; the drum under it), its CONTROLS (the HORN ROTOR's trimmers, then the LOW ROTOR's), its AMP.
 * The rotors as the engine turns them (b3_angle); ALGO on each the speed: SLOW, STOP (the brake), FAST, as its lamps */
static uint16_t o_rgb(int32_t r, int32_t g, int32_t b)
{
    return RGB(clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255));
}
static void o_grain(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t g, int32_t b, uint32_t seed)
{                                            /* veneer, its grain lying along: each row a shade of its own, a streak;
                                              * its figure, darker bands and a lighter, drifting as the rows go down
                                              * (as the walnut's in the photos) */
    int32_t i, j;
    for (i = 0; i < h; i++) {
        const uint32_t z = ((uint32_t)(y + i) + seed * 977u) * 2654435761u;
        const int32_t d = ((int32_t)(z >> 28) - 8) * 2;
        cv_rect(x, y + i, w, 1, o_rgb(r + d, g + d * 6 / 10, b + d / 3));
        if (((z >> 9) & 3u) == 0u && w > 8)
            cv_rect(x + (int32_t)((z >> 13) % (uint32_t)(w / 2)), y + i, w / 3, 1, o_rgb(r - 16, g - 12, b - 6));
        if (w > 16)
            for (j = 0; j < 3; j++) {        /* (the figure: three bands, each its own drift) */
                const float t = (float)(y + i) * (0.05f + 0.02f * (float)j) + (float)(seed + 5u * (uint32_t)j);
                const int32_t c = x + (int32_t)((float)w * (0.2f + 0.3f * (float)j + 0.12f * fm_sinf(t))),
                              l = w / 7 + (int32_t)((z >> (16 + j)) & 7u), k = j == 1 ? 8 : -13;
                const int32_t x0 = clamp(c - l / 2, x, x + w), x1 = clamp(c + l / 2, x, x + w);
                if (x1 > x0)
                    cv_rect(x0, y + i, x1 - x0, 1, o_rgb(r + d + k, g + d * 6 / 10 + k * 7 / 10, b + d / 3 + k / 3));
            }
    }
}
static void o_ell(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)   /* filled */
{
    int32_t dy;
    for (dy = -ry; dy <= ry; dy++) {
        const int32_t w = ry ? rx * chord(ry, dy) / ry : rx;
        if (w >= 0)
            cv_rect(cx - w, cy + dy, 2 * w + 1, 1, c);
    }
}
static void o_louvre(int32_t x, int32_t y, int32_t w)   /* a louvre's slot, routed, its ends round: its upper
                                              * edge's shadow, the dark inside, its lit lower lip */
{
    cv_rect(x + 2, y, w - 4, 1, RGB(60, 26, 12));
    cv_rect(x + 1, y + 1, w - 2, 1, RGB(34, 14, 6));
    cv_rect(x, y + 2, w, 1, RGB(46, 20, 9));
    cv_rect(x + 1, y + 3, w - 2, 1, RGB(92, 42, 20));
    cv_rect(x + 2, y + 4, w - 4, 1, RGB(222, 156, 98));
    cv_rect(x + 3, y + 1, w / 3, 1, RGB(70, 34, 18));   /* (its inside's top, a glint) */
}
static void o_seen(int32_t cx, int32_t y, int32_t w, float a, int32_t r, int sides)
{                                            /* a rotor seen through a louvre: its mouth (the horn's two) as it passes
                                              * the front */
    int32_t j;
    for (j = 0; j < sides; j++) {
        const float c = fm_cosf(2.0f * FM_PI * (a + 0.5f * (float)j)), s = fm_sinf(2.0f * FM_PI * (a + 0.5f * (float)j));
        if (s > 0.0f) {
            const int32_t x = cx + (int32_t)(c * (float)r), hw = 1 + (int32_t)(s * 4.0f);
            const int32_t x0 = clamp(x - hw, cx - w / 2 + 1, cx + w / 2 - 1), x1 = clamp(x + hw, cx - w / 2 + 1, cx + w / 2 - 1);
            if (x1 > x0)
                cv_rect(x0, y + 1, x1 - x0, 2, RGB(8, 6, 6));
        }
    }
}
static void o_ring(int32_t cx, int32_t cy, int32_t r, int32_t g)   /* a glossy black bezel of radius r: its rim lit
                                              * at the top left, a glint at the bottom right (Leslie-122H-Controls) */
{
    int32_t i, x, y;
    disc(cx + 1, cy + 1, r, P_KSHADOW);
    disc(cx, cy, r, RGB(12, 12, 12));
    for (i = 0; i < 8 * r; i++) {
        const float a = (float)i * (2.0f * FM_PI) / (float)(8 * r);
        const float c = fm_cosf(a + FM_PI / 4.0f);   /* (1 at the top left, -1 at the bottom right) */
        pt_at(cx, cy, r - 1, a, &x, &y);
        cv_pset(x, y, grey(c > 0.0f ? 20 + (int32_t)(c * c * (float)g) : 20 + (int32_t)(c * c * (float)g / 3)));
    }
}
static void o_lamp(int32_t cx, int32_t cy, int on, uint16_t lit, uint16_t glow, uint16_t off)
{                                            /* a lamp: its dome in a black bezel, lit glowing past it; unlit its glass
                                              * dark, a glint on it */
    if (on)
        disc(cx, cy, 7, glow);
    o_ring(cx, cy, 5, on ? 150 : 110);
    disc(cx, cy, 3, on ? lit : off);
    if (on) {
        disc(cx, cy, 1, RGB(255, 250, 230));
        cv_pset(cx - 2, cy - 2, RGB(255, 255, 245));
    } else
        cv_pset(cx - 1, cy - 2, RGB(170, 166, 160));
}
static void o_lamps(int32_t x, int32_t y, int32_t dx, uint16_t ink, int beside)   /* SLOW STOP FAST, the speed's
                                              * lamps, their names under them or beside */
{
    const uint32_t s = o_speed();
    o_lamp(x, y, s == 0u, RGB(255, 214, 120), RGB(96, 64, 20), RGB(70, 54, 30));
    o_lamp(x + dx, y, s == 1u, RGB(255, 70, 60), RGB(90, 16, 14), RGB(80, 20, 20));
    o_lamp(x + 2 * dx, y, s == 2u, RGB(110, 240, 140), RGB(16, 70, 30), RGB(20, 64, 34));
    if (beside) {
        uint32_t i;
        for (i = 0; i < 3u; i++)
            cv_text(x + (int32_t)i * dx + 8, y - 4, &FONT_XS, OSPD[i], ink);
    } else {
        print_c(x, y + 6, "SLOW", ink);
        print_c(x + dx, y + 6, "STOP", ink);
        print_c(x + 2 * dx, y + 6, "FAST", ink);
    }
    tag(x + dx, y, EN_ALGO);
}
static void o_cabinet(void)                  /* the cabinet from the front, a little to its left: its lid, the front's
                                              * louvres (two pairs over the horn, four long over the drum), its side's */
{
    const float ha = b3_angle(0), da = b3_angle(1);
    static const char *const LBL[4] = {"WIDTH", "DRIVE", "CLICK", "VOLUME"};
    const int32_t vals[4] = {opatch.width, opatch.drive, opatch.click, 127 - (int32_t)set.b3vol_off};
    int32_t i;
    cv_rect(0, 66, 240, TALL_H - 66, P_BG);
    o_grain(22, 66, 80, 3, 150, 76, 38, 7);  /* the lid: its top, its moulded front edge */
    o_grain(16, 69, 72, 4, 168, 88, 42, 3);
    cv_rect(16, 69, 72, 1, RGB(214, 140, 84));
    cv_rect(17, 73, 70, 1, RGB(96, 44, 20));
    o_grain(88, 68, 14, 5, 128, 58, 28, 5);
    o_grain(22, 74, 62, 76, 170, 94, 48, 11);   /* the front, its stiles; the side, darker, turned away */
    cv_rect(22, 74, 1, 76, RGB(222, 156, 96));   /* (its corners round: a post's light, its shade) */
    cv_rect(23, 74, 1, 76, RGB(194, 120, 66));
    cv_rect(82, 74, 1, 76, RGB(132, 66, 32));
    cv_rect(83, 74, 1, 76, RGB(206, 136, 80));
    o_grain(84, 74, 14, 76, 126, 62, 32, 13);
    cv_rect(84, 74, 1, 76, RGB(150, 82, 42));
    cv_rect(97, 74, 1, 76, RGB(80, 36, 18));
    o_grain(18, 150, 68, 8, 156, 78, 36, 17);   /* the plinth */
    cv_rect(18, 150, 68, 1, RGB(206, 130, 72));
    o_grain(86, 151, 14, 7, 120, 54, 26, 19);
    cv_rect(51, 76, 4, 1, O_GOLD);           /* (the Leslie script) */
    for (i = 0; i < 2; i++) {                /* the horn's louvres: the horn's mouths passing behind */
        o_louvre(27, 79 + 6 * i, 25);
        o_louvre(54, 79 + 6 * i, 25);
        o_seen(53, 79 + 6 * i, 52, ha, 22, 2);
        o_louvre(86, 79 + 6 * i, 10);
    }
    for (i = 0; i < 4; i++) {                /* the drum's: its opening going round */
        o_louvre(28, 126 + 6 * i, 51);
        o_seen(53, 126 + 6 * i, 50, da, 24, 1);
        o_louvre(86, 126 + 6 * i, 10);
    }
    o_lamps(134, 84, 34, P_SILK, 0);
    for (i = 0; i < 4; i++) {
        const int32_t cx = 122 + 32 * i, cy = 122;
        bs2_knob(cx, cy, 10, vals[i], 127, knob_lit(EN_K1 + (uint32_t)i) ? KF_LIT : 0u);
        tag(cx, cy, EN_K1 + (uint32_t)i);
        print_c(cx, cy + 13, LBL[i], P_SILK);
        {
            char t[8];
            int_text(vals[i], t);
            print_c(cx, cy + 23, t, P_CYAN);
        }
    }
}
static void o_arm(int32_t cx, int32_t cy, float a, int32_t l)   /* a horn's arm from the rotor's middle, flaring to its
                                              * mouth; facing us, its mouth open */
{
    const float c = fm_cosf(2.0f * FM_PI * a), s = fm_sinf(2.0f * FM_PI * a);
    int32_t i, ex = cx + (int32_t)(c * (float)l), ey = cy + (int32_t)(s * (float)l * 0.34f);
    for (i = 2; i <= 12; i++) {
        const float t = (float)i / 12.0f;
        disc(cx + (int32_t)(c * (float)l * t), cy + (int32_t)(s * (float)l * 0.34f * t) - 1, 3 + (int32_t)(5.0f * t * t),
             RGB(16, 16, 18));
    }
    for (i = 3; i <= 11; i++) {              /* (its gloss along the top, the light's streak on it) */
        const int32_t x = cx + (int32_t)(c * (float)l * (float)i / 12.0f), y = cy + (int32_t)(s * (float)l * 0.34f * (float)i / 12.0f);
        cv_pset(x, y - 3 - i / 4, RGB(96, 96, 104));
        cv_pset(x, y - 2 - i / 4, RGB(52, 52, 58));
        if (i > 5 && i < 10)
            cv_pset(x, y - 4 - i / 4, RGB(176, 176, 184));
    }
    if (s > 0.15f) {
        o_ell(ex, ey - 1, 2 + (int32_t)(7.0f * s), 8, RGB(74, 74, 78));
        o_ell(ex, ey - 1, 1 + (int32_t)(6.0f * s), 6, RGB(4, 4, 4));
    } else
        o_ell(ex, ey - 1, 3, 8, RGB(40, 40, 44));
}
static void o_wing(int32_t cx, int32_t cy)   /* a wing nut on its stud, its washer: bright steel, its wings lit */
{
    o_ell(cx, cy + 3, 4, 1, RGB(200, 200, 204));
    cv_rect(cx - 4, cy - 3, 3, 3, RGB(196, 196, 200));
    cv_rect(cx + 2, cy - 3, 3, 3, RGB(150, 150, 156));
    cv_rect(cx - 4, cy - 3, 3, 1, RGB(250, 250, 252));
    cv_rect(cx + 2, cy - 3, 3, 1, RGB(220, 220, 224));
    cv_rect(cx - 1, cy - 1, 3, 3, RGB(176, 176, 180));
    cv_pset(cx - 1, cy - 1, RGB(255, 255, 255));
    cv_rect(cx, cy + 2, 1, 1, RGB(110, 110, 110));
}
static void o_rotors(void)                   /* Leslie-122H-Rotor-and-Tension-Pulleys.webp: the horn's shelf, the horn
                                              * turning on its pulley, the motor's brass pulley, the idler and its
                                              * spring with their wing nuts, the belt; under it the drum, its opening
                                              * going round */
{
    const float ha = b3_angle(0), da = b3_angle(1);
    const int32_t hx = 156, hy = 92;
    int32_t i;
    char t[12];
    cv_rect(0, 66, 240, TALL_H - 66, P_BG);
    o_grain(0, 66, 240, 5, 206, 132, 70, 23);   /* the shelf over it, the back, the motor board, its edge */
    cv_rect(0, 70, 240, 1, RGB(120, 66, 30));
    o_grain(0, 71, 240, 14, 70, 42, 26, 29);
    for (i = 0; i < 24; i++)                 /* (the motor board: hardboard, smooth, lighter to its front) */
        cv_rect(0, 85 + i, 240, 1, o_rgb(98 + i * 5 / 4, 64 + i * 5 / 6, 42 + i / 2));
    o_ell(hx, 104, 18, 4, RGB(46, 30, 20));  /* (the rotor's hole in the board) */
    o_grain(0, 109, 240, 6, 212, 140, 76, 37);
    cv_rect(0, 109, 240, 1, RGB(236, 176, 110));
    cv_rect(0, 114, 240, 1, RGB(110, 60, 28));
    {                                        /* the pulleys and the belt: the motor's (x 50) turns 3 times the horn's */
        const float m = ha * 3.0f;
        cv_rect(22, 98, 14, 2, RGB(150, 150, 150));   /* (the motor's bracket, its wing nut) */
        cv_rect(22, 98, 2, 7, RGB(120, 120, 120));
        o_wing(29, 96);
        cv_line(50, 99, hx - 14, 101, RGB(10, 10, 10));
        cv_line(50, 104, 96, 105, RGB(10, 10, 10));
        cv_line(96, 105, hx - 12, 106, RGB(10, 10, 10));
        o_ell(50, 101, 6, 3, RGB(176, 140, 64));
        o_ell(50, 101, 3, 1, RGB(90, 70, 36));
        cv_pset(50 + (int32_t)(fm_cosf(2.0f * FM_PI * m) * 5.0f), 101 + (int32_t)(fm_sinf(2.0f * FM_PI * m) * 2.0f),
                RGB(255, 236, 170));
        cv_rect(76, 100, 18, 5, RGB(150, 150, 154));
        cv_rect(76, 100, 18, 1, RGB(206, 206, 210));
        cv_rect(76, 104, 18, 1, RGB(96, 96, 100));   /* the idler's bracket, its wing nut; the spring, its wire */
        o_wing(84, 98);
        for (i = 0; i < 10; i++)             /* (its coils) */
            cv_rect(107 + (i & 1), 98 + i, 7, 1, i & 1 ? RGB(96, 96, 100) : RGB(222, 222, 226));
        o_wing(111, 96);
        cv_line(70, 107, 108, 106, RGB(190, 190, 190));
        cv_line(114, 106, hx + 18, 103, RGB(190, 190, 190));
        o_ell(hx + 20, 103, 5, 2, RGB(232, 232, 228));   /* (the tension pulley, white) */
        cv_rect(hx + 19, 99, 2, 3, RGB(160, 160, 160));
    }
    o_ell(hx, 103, 14, 3, RGB(12, 12, 12));  /* the horn's pulley, its bolts going round */
    o_ell(hx, 101, 12, 2, RGB(30, 30, 32));
    cv_rect(hx - 10, 99, 20, 1, RGB(70, 70, 76));   /* (its rim's gloss) */
    cv_pset(hx + (int32_t)(fm_cosf(2.0f * FM_PI * ha) * 9.0f), 101 + (int32_t)(fm_sinf(2.0f * FM_PI * ha) * 2.0f),
            RGB(200, 200, 200));
    cv_rect(hx - 1, hy, 3, 7, RGB(20, 20, 20));
    if (fm_sinf(2.0f * FM_PI * ha) > 0.0f) { /* the horn: the far arm first */
        o_arm(hx, hy, ha + 0.5f, 58);
        disc(hx, hy - 1, 7, RGB(22, 22, 24));
        o_arm(hx, hy, ha, 58);
    } else {
        o_arm(hx, hy, ha, 58);
        disc(hx, hy - 1, 7, RGB(22, 22, 24));
        o_arm(hx, hy, ha + 0.5f, 58);
    }
    o_grain(0, 115, 240, 4, 196, 124, 64, 41);   /* the shelf under it; the drum's compartment */
    o_grain(0, 119, 240, 39, 56, 34, 22, 43);
    {                                        /* the drum: a cylinder lit from the front, its opening facing round */
        const int32_t cx = 176, r = 34, y0 = 128, h = 24;
        const float u0 = fm_sinf(2.0f * FM_PI * da), f0 = fm_cosf(2.0f * FM_PI * da);
        for (i = -r; i <= r; i++) {
            const float u = (float)i / (float)r, f = (float)chord(r, i) / (float)r;
            const int32_t g = 30 + (int32_t)(50.0f * f);
            cv_rect(cx + i, y0, 1, h, o_rgb(g, g - 4, g - 8));
            if (f0 > 0.0f && fm_fabsf(u - u0) < 0.42f * f0) {
                const int edge = fm_fabsf(u - u0) > 0.42f * f0 - 0.06f;
                cv_rect(cx + i, y0 + 3, 1, h - 6, edge ? RGB(120, 116, 108) : RGB(4, 4, 4));
            }
        }
        o_ell(cx, y0, r, 4, RGB(96, 92, 86));
        o_ell(cx, y0, r - 3, 2, RGB(70, 66, 62));
        cv_rect(cx - 1, y0 + h + 3, 3, 6, RGB(150, 150, 150));
        for (i = -r; i <= r; i += 1) {       /* (its lower rim) */
            const int32_t e = chord(r, i) * 4 / r;
            cv_pset(cx + i, y0 + h + e - 1, RGB(26, 24, 22));
        }
    }
    cv_text(8, 124, &FONT_T, "HORN", P_SILK);   /* the speeds the trimmers set, and each rotor's now */
    cv_text(8, 142, &FONT_T, "DRUM", P_SILK);
    for (i = 0; i < 4; i++) {
        const uint32_t j = i < 2 ? 2u + (uint32_t)i : 4u + (uint32_t)i;
        const int32_t x = 60 + 42 * (i & 1), y = i < 2 ? 124 : 142;
        int_text((int32_t)(b3_trim(j, opatch.les[j]) * 60.0f + 0.5f), t);
        print_c(x, y - 2, OSPD[(i & 1) * 2], P_DIM);
        print_c(x, y + 7, t, knob_lit(EN_K1 + (uint32_t)i) ? P_CYAN : P_SILK);
        tag(x, y + 10, EN_K1 + (uint32_t)i);
    }
    for (i = 0; i < 2; i++) {
        int_text((int32_t)(b3_speed((uint32_t)i) * 60.0f + 0.5f), t);
        text_cat(t, " RPM");
        cv_text(236 - text_w(&FONT_T, t), i ? 150 : 74, &FONT_T, t, P_CYAN);
    }
}
static void o_trimmer(int32_t cx, int32_t cy, uint32_t v, uint32_t f)   /* a trimmer (Leslie-122H-Controls): sunk
                                              * in its glossy black bezel, its flat face grey and grained, a fine line */
{
    const int32_t d = f & KF_DIM ? 2 : 1, g = 60 / d;
    int32_t i, x, y;
    o_ring(cx, cy, 8, 170 / d);
    disc(cx, cy, 6, RGB(4, 4, 4));           /* (its well) */
    disc(cx, cy, 5, grey(g));                /* (its face; its rim's top left lit, its grain) */
    for (i = 0; i < 24; i++) {
        pt_at(cx, cy, 5, (float)i * (FM_PI / 12.0f), &x, &y);
        cv_pset(x, y, grey(i >= 15 && i <= 21 ? g + 30 / d : g - 18 / d));
    }
    for (i = 0; i < 26; i++) {
        const uint32_t z = (uint32_t)(i + 1) * 2654435761u ^ (uint32_t)cx * 40503u;
        x = cx - 3 + (int32_t)(z % 7u), y = cy - 3 + (int32_t)((z >> 9) % 7u);
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) < 17)
            cv_pset(x, y, grey(z >> 31 ? g + 12 / d : g - 14 / d));
    }
    pt_at(cx, cy, 4, ((float)v / 127.0f - 0.5f) * (FM_PI * 5.0f / 3.0f), &x, &y);
    cv_line(cx, cy, x, y, f & KF_LIT ? P_CYAN : f & KF_DIM ? RGB(120, 120, 120) : RGB(236, 236, 236));
}
static void o_rule(int32_t y, const char *s)  /* a heading between two lines, as the panel's printed */
{
    const int32_t w = text_w(&FONT_T, s);
    cv_rect(12, y + 4, 120 - w / 2 - 18, 1, RGB(220, 220, 216));
    cv_rect(120 + w / 2 + 6, y + 4, 228 - (120 + w / 2 + 6), 1, RGB(220, 220, 216));
    cv_text(120 - w / 2, y, &FONT_T, s, RGB(230, 230, 226));
}
static void o_controls(int low)              /* Leslie-122H-Controls.webp: the panel, its two rows of trimmers, the
                                              * speed's lamps; K1-K4 the row this view's */
{
    static const char *const L1[4] = {"RISE", "FALL", "SLOW", "FAST"}, *const L2[4] = {"TIME", "TIME", "SPEED", "SPEED"};
    int32_t i;
    cv_rect(0, 66, 240, TALL_H - 66, RGB(10, 10, 10));
    cv_rect(4, 66, 232, TALL_H - 68, RGB(44, 43, 41));
    for (i = 0; i < 700; i++) {              /* (its wrinkle finish) */
        const uint32_t z = (uint32_t)i * 2654435761u;
        cv_pset(4 + (int32_t)(z % 232u), 66 + (int32_t)((z >> 12) % (uint32_t)(TALL_H - 68)),
                z >> 30 ? RGB(54, 53, 51) : RGB(32, 31, 30));
    }
    o_rule(66, "HORN ROTOR");
    o_rule(134, "LOW ROTOR");
    for (i = 0; i < 4; i++) {
        const int32_t cx = 24 + 54 * i;
        uint32_t r;
        cv_rect(cx, 91, 1, 3, RGB(220, 220, 216));
        print_c(cx, 95, L1[i], RGB(230, 230, 226));
        print_c(cx, 104, L2[i], RGB(230, 230, 226));
        cv_rect(cx, 113, 1, 2, RGB(220, 220, 216));
        for (r = 0; r < 2u; r++) {
            const uint32_t j = 4u * r + (uint32_t)i, on = r == (uint32_t)(low != 0);
            const int32_t cy = r ? 124 : 82;
            char t[12];
            o_trimmer(cx, cy, opatch.les[j], on ? (knob_lit(EN_K1 + (uint32_t)i) ? KF_LIT : 0u) : KF_DIM);
            if (on)
                tag(cx, cy, EN_K1 + (uint32_t)i);
            o_trim_text(j, t);
            cv_text(cx + 10, cy - 3, &FONT_XS, t, on ? P_CYAN : P_DIM);
        }
    }
    o_lamps((240 - 2 * 44 - 8 - text_w(&FONT_XS, "FAST") + 6) / 2, 150, 44, RGB(230, 230, 226), 1);   /* centred */
}
static void o_tube(int32_t cx, int32_t top, int32_t w, int32_t h, float blue, float plate)
{                                            /* a valve (Leslie-122H-Amp): its glass dark and clear, a sheen down each
                                              * side, its top silvered by the getter, its tip; inside, its plates grey
                                              * (dull red as they heat: red-plating), its heater's steady orange, the
                                              * blue-violet glow on its glass as the signal swings; the big ones' red
                                              * ring and brass base */
    const int32_t r = w / 2, big = w > 14, base = top + h, bh = big ? 9 : 3, gb = base - bh, my = top + r + (gb - top - r) / 2;
    int32_t i;
    disc(cx, top + r, r, RGB(40, 38, 36));   /* (the glass) */
    cv_rect(cx - r, top + r, w, gb - top - r, RGB(40, 38, 36));
    for (i = 0; i < r; i++)                  /* (its top silvered, darker to its middle) */
        cv_rect(cx - chord(r, r - i), top + i, 2 * chord(r, r - i) + 1, 1, grey(110 + 60 * i / r));
    cv_rect(cx - 1, top - 2, 3, 2, grey(150));   /* (its tip) */
    cv_rect(cx - r + 3, top + r + 3, w - 6, gb - top - r - 8, o_rgb(70 + (int32_t)(140.0f * plate), 68 + (int32_t)(10.0f * plate),
                                                                   66 - (int32_t)(20.0f * plate)));   /* the plates */
    for (i = cx - r + 5; i < cx + r - 4; i += 4)   /* (their seams) */
        cv_rect(i, top + r + 4, 1, gb - top - r - 10, RGB(30, 28, 26));
    if (blue > 0.02f)                        /* (the glass's glow: at the plates' edges, inside the glass) */
        for (i = 0; i < 2; i++)
            cv_rect(i ? cx + r - 3 : cx - r + 1, top + r + 2, 2, gb - top - r - 6,
                    o_rgb(58 + (int32_t)(60.0f * blue), 54 + (int32_t)(70.0f * blue), 50 + (int32_t)(205.0f * blue)));
    cv_rect(cx - r + 4, my - 2, w - 8, 3, RGB(255, 120, 40));   /* the heater, through the mica: steady */
    cv_rect(cx - 1, my - 1, 2, 1, RGB(255, 220, 160));
    if (big)
        for (i = 0; i < 3; i++)              /* (the heater's glints up the plates, as the photo's) */
            cv_pset(cx - r + 5 + i * (w - 10) / 2, top + r + 6 + i * 3, RGB(255, 96, 40));
    cv_rect(cx - r + 1, top + r - 2, 1, gb - top - r, RGB(170, 166, 158));   /* (the glass's sheen, each side) */
    cv_rect(cx + r - 2, top + r, 1, gb - top - r - 2, RGB(96, 92, 88));
    if (big) {
        cv_rect(cx - r, gb - 4, w, 3, RGB(200, 40, 30));   /* (its red ring) */
        cv_rect(cx - r, gb - 4, w, 1, RGB(250, 90, 70));
        for (i = 0; i < bh; i++)             /* its base, brass, lit from the left */
            cv_rect(cx - r + 1, gb + i, w - 2, 1, i == 1 ? RGB(240, 214, 150) : o_rgb(184 - 6 * i, 150 - 6 * i, 96 - 4 * i));
        cv_rect(cx + r - 5, gb, 3, bh, RGB(110, 84, 50));
    } else
        cv_rect(cx - r + 1, gb, w - 2, bh, RGB(26, 24, 22));   /* (its socket) */
}
static void o_amp(void)                      /* Leslie-122H-Amp.webp: the chassis in the dark, its two big valves and
                                              * two small, the transformer, the wires; the valves as they'd be: the
                                              * signal's swing (b3_level) a blue glow, louder and the more DRIVE the
                                              * more; the power valves' plates reddening as it's driven hard a while */
{
    static float blue, plate;
    const float d = (float)opatch.drive / 127.0f, lv = fm_minf(1.0f, b3_level() * 3.0f);
    const float to_b = lv * (0.35f + 0.65f * d), to_p = lv * d * d;
    char t[16];
    int32_t i;
    blue += (to_b - blue) * (to_b > blue ? 0.6f : 0.25f);   /* (the glow follows at once; the plates' heat slow) */
    plate += (to_p - plate) * (to_p > plate ? 0.03f : 0.015f);
    cv_rect(0, 66, 240, TALL_H - 66, RGB(6, 6, 6));
    {                                        /* the chassis, brushed aluminium lit from the left: its top, its face */
        int32_t x;
        for (x = 36; x < 236; x += 4) {
            const int32_t g = 150 - (x - 36) * 50 / 200;
            cv_rect(x, 112, 4, 12, RGB(g - 30, g - 30, g - 38));
            cv_rect(x, 124, 4, 27, RGB(g + 20, g + 20, g + 12));
        }
        for (i = 0; i < 9; i++)              /* (its brushing) */
            cv_rect(36 + (i * 53) % 120, 126 + i * 3, 60 + (i * 31) % 70, 1, RGB(168 - i * 4, 168 - i * 4, 160 - i * 4));
        cv_rect(36, 112, 200, 1, RGB(170, 170, 160));
        cv_rect(36, 124, 200, 1, RGB(210, 210, 200));
        cv_rect(36, 151, 200, 2, RGB(50, 50, 48));
        cv_rect(226, 145, 3, 3, RGB(20, 20, 20));   /* (a hole in it) */
    }
    for (i = 0; i < 71; i++)                 /* its end, bent up, brassy in the light; its screws, black */
        cv_rect(14, 82 + (int32_t)i, 22, 1, RGB(184 - i / 2, 172 - i / 2, 120 - i / 3));
    cv_rect(14, 82, 22, 1, RGB(220, 210, 160));
    cv_rect(35, 82, 1, 71, RGB(80, 76, 56));
    disc(25, 98, 3, RGB(20, 20, 20));
    disc(25, 142, 3, RGB(20, 20, 20));
    cv_pset(24, 97, grey(110));
    cv_pset(24, 141, grey(110));
    o_tube(96, 70, 26, 48, blue, plate);
    o_tube(134, 70, 26, 48, blue, plate);
    o_tube(62, 88, 12, 30, blue * 0.3f, 0.0f);
    o_tube(114, 92, 12, 28, blue * 0.3f, 0.0f);
    cv_rect(158, 94, 34, 24, RGB(96, 98, 96));   /* the transformer: its laminations' frame, its coil, its label */
    for (i = 0; i < 6; i++)
        cv_rect(158, 96 + 4 * (int32_t)i, 34, 1, RGB(70, 72, 70));
    cv_rect(158, 94, 34, 1, RGB(160, 162, 160));
    cv_rect(165, 99, 20, 16, RGB(222, 196, 86));
    cv_rect(165, 99, 20, 1, RGB(250, 232, 140));
    cv_rect(184, 99, 1, 16, RGB(160, 136, 50));
    cv_rect(168, 91, 14, 4, RGB(220, 220, 210));
    for (i = 0; i < 26; i++) {               /* the wires, twisted in pairs, to their strip */
        const int32_t x = 192 + i * 3 / 2, y = 102 - i, o = (i / 2) & 1;
        cv_rect(x, y + o, 2, 1, RGB(40, 150, 70));
        cv_rect(x, y + 1 - o, 2, 1, RGB(230, 230, 230));
        cv_rect(x + 4, y + 6 + o, 2, 1, RGB(210, 40, 40));
        cv_rect(x + 4, y + 7 - o, 2, 1, RGB(60, 80, 210));
    }
    cv_rect(228, 70, 10, 16, RGB(220, 216, 200));   /* (the strip) */
    cv_rect(230, 72, 6, 2, RGB(40, 40, 40));
    cv_rect(230, 78, 6, 2, RGB(40, 40, 40));
    {                                        /* its face's print: DRIVE, KEY SWELL, its REST, VOLUME */
        static const char *const LBL[4] = {"DRIVE", "KEY SWELL", "REST", "VOLUME"};
        uint32_t j;
        for (j = 0; j < 4u; j++) {
            const uint32_t i = j;
            const int32_t cx = 66 + 46 * (int32_t)i;
            if (i == 0u)
                int_text(opatch.drive, t);
            else if (i == 1u)
                t[0] = 0, text_cat(t, OFF_ON[opatch.swell]);
            else if (i == 2u)
                int_text((int32_t)opatch.swell_rest * 100 / 127, t), text_cat(t, "%");
            else
                int_text(127 - (int32_t)set.b3vol_off, t);
            print_c(cx, 128, LBL[i], RGB(24, 24, 24));
            print_c(cx, 138, t, knob_lit(EN_K1 + i) ? P_CYAN : RGB(30, 30, 30));
            tag(cx, 134, EN_K1 + i);
        }
    }
}
static void o_mojo(void)                     /* NOISES and MOJO: a mod's plate, as DRUM ROOM's (each instrument's mods
                                              * the same steel): NOISES its four knobs; MOJO its two, K3 and K4 (the
                                              * knobs right to left), CHARACTER's name under its own */
{
    char t[20];
    uint32_t i;
    plate("ORGAN SOMETHING");
    dymo(120, 30, ui.oles == 6u ? "ORGAN MOJO" : "ORGAN NOISES", RGB(235, 235, 235));
    for (i = ui.oles == 6u ? 2u : 0u; i < 4u; i++) {
        static const char *const NL[4] = {"THUDS", "HUM", "ROTOR", "CLUNKS"}, *const ML[4] = {"", "", "MOJO", "CHARACTER"};
        const int32_t cx = ui.oles == 6u ? 10 + 220 * (2 * (int32_t)i - 3) / 4   /* (MOJO's two across the plate) */
                                         : 10 + 220 * (2 * (int32_t)i + 1) / 8;
        t[0] = 0;
        if (ui.oles != 6u)
            int_text(set.b3_noise[i], t), mod_knob(cx, 86, 16, set.b3_noise[i], 127, 0u, EN_K1 + i, NL[i], t);
        else if (i == 2u)
            int_text((int32_t)opatch.mojo * 100 / 127, t), text_cat(t, "%"),
                mod_knob(cx, 86, 16, opatch.mojo, 127, 0u, EN_K3, ML[i], t);
        else
            text_cat(t, b3_mojo_name(opatch.mchar)), mod_knob(cx, 86, 16, opatch.mchar, B3_NCHAR - 1u, 0u, EN_K4, ML[i], t);
    }
}
static void oles_draw(void)
{
    if (ui.oles >= 5u) {                     /* (NOISES, MOJO: a mod's plate, its own head) */
        o_mojo();
        return;
    }
    oh_head("LESLIE", OLES_V[ui.oles][0] == 'H' ? "HORN" : OLES_V[ui.oles][0] == 'L' ? "LOW" : ui.oles ? OLES_V[ui.oles]
                                                                                                      : "122");
    if (ui.oles == 0u)
        o_cabinet();
    else if (ui.oles == 1u)
        o_rotors();
    else if (ui.oles == 4u)
        o_amp();
    else
        o_controls(ui.oles == 3u);
}
