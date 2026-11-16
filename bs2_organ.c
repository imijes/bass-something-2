/* SPDX-License-Identifier: MPL-2.0 */
/* The organ, the fourth instrument (its engine: bs2_b3.c, after the Hammond B-3 and its Leslie): its mode (ui.b3, HOME
 * after the RMI's), its 32 presets as the bass's patches are (PRESET plays one at once; SAVE as a Bass Station II's: its
 * HOME, the slot blinking, PRESET picks, SAVE stores; PATCH NAME its name), its pages and its live buttons. Part of the
 * unity build, included by app.c after the panel's drawing:
 *   ORGAN HOME (PG_OHOME): the console as the photo has it, its drawbars and tabs as the sound has them; SELECT ALGO
 *     16' 5 1/3', K1-K4 8' 4' 2 2/3' 2' (a turn dives to DRAWBARS: zoom_organ); PRESET the preset (the display's)
 *   ORGAN DRAWBARS (PG_OBARS): the drawbars up close, the same six; SEL there its other view: K2-K4 1 3/5' 1 1/3' 1';
 *     PRESET the preset on both
 *   ORGAN TABS (PG_OTABS): the percussion's tabs (K1 ON, K2 VOLUME SOFT, K3 DECAY FAST, K4 HARMONIC THIRD), the vibrato
 *     (SELECT on, ALGO its knob V1 V2 V3 C1 C2 C3)
 *   ORGAN LESLIE (PG_OLES): the cabinet turning; SELECT its speed, ALGO the brake, K1 WIDTH, K2 DRIVE, K3 CLICK, K4 VOL
 *   SEL: DRAWBARS, its other view, TABS, LESLIE, PATCH NAME, round. ARP the Leslie chorale or tremolo (held: the brake),
 *   SEQ the percussion's harmonic (at once: to flutter it), FX the percussion on or off, EDIT the vibrato on or off.
 * Its MIDI: notes on its channel (b3_chan), its CCs on its CC channel (organ_cc) */
#define ONSLOT 32u
#define ORG_MAGIC 0x31334F42u                /* "BO31" */
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
static void organ_factory(uint32_t s)        /* slot s as the factory has it */
{
    b3_patch_t *p = &obank.p[s];
    const char *n = "INIT", *b = "888000000";
    uint32_t i;
    __builtin_memset(p, 0, sizeof *p);
    __builtin_memset(obank.name[s], 0, PNAME_N);
    p->vib = 5, p->drive = 20, p->click = 64, p->width = 100;
    if (s < sizeof OFACT / sizeof OFACT[0]) {
        n = OFACT[s].name, b = OFACT[s].bars;
        p->perc_on = OFACT[s].perc, p->perc_3rd = OFACT[s].third, p->perc_fast = OFACT[s].fast;
        p->vib_on = OFACT[s].vib_on, p->vib = OFACT[s].vib, p->drive = OFACT[s].drive;
    }
    for (i = 0; i < 9u; i++)
        p->bar[i] = (uint8_t)(b[i] - '0');
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
}
static void organ_load(void)                 /* (app_init: its bank, else the factory's) */
{
    uint32_t s;
    if (plat_store_load(OBJ_ORGAN, &obank, sizeof obank) != (int)sizeof obank || obank.magic != ORG_MAGIC) {
        obank.magic = ORG_MAGIC;
        for (s = 0; s < ONSLOT; s++)
            organ_factory(s);
    }
    organ_pick(set.b3_slot < ONSLOT ? set.b3_slot : 0u);
}
static void organ_block(void)                /* the render's, before the second core's job: the sound, the Leslie */
{
    if (o_sent) {
        o_sent = 0;
        b3_set(&opatch);
    }
    b3_leslie(ui.ofast, ui.obrake);
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
static uint32_t organ_lights(void)           /* FX lit while its percussion is on, EDIT while its vibrato is */
{
    return (uint32_t)opatch.perc_on << B_FX | (uint32_t)opatch.vib_on << B_EDIT;
}
static void organ_changed(void)
{
    odirty = 1;
    o_sent = 1;
    ui.redraw = 1;
}

/* its CCs (its CC channel): 12-20 the drawbars, 21 the percussion, 22 its THIRD, 23 SOFT, 24 FAST, 30 the vibrato,
 * 25 its knob, 26 the drive, 28 the click, 29 the width, 7 its volume; live: 1 (the mod wheel) the Leslie fast, 27
 * the brake, 11 the expression pedal; 64 and up: on */
static void organ_cc(uint32_t c, uint32_t v)
{
    if (c >= 12u && c <= 20u)
        opatch.bar[c - 12u] = (uint8_t)((v * 8u + 63u) / 127u);
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
    } else if (c == 11u) {
        b3_expr(v);
        return;
    } else
        return;
    organ_changed();
}

/* the knobs: each page's (knob_turn's for the organ's pages), said as they go */
static void o_say_bar(uint32_t b)
{
    char t[4];
    t[0] = (char)('0' + opatch.bar[b]), t[1] = 0;
    app_say(OBAR[b], t);
}
static void o_flag(uint8_t *f, int32_t e, const char *name, const char *off, const char *on)
{
    *f = e > 0;
    organ_changed();
    app_say(name, *f ? on : off);
}
static int o_turn(uint32_t k, int32_t e)
{
    if (ui.page == PG_OHOME || ui.page == PG_OBARS) {
        static const int8_t V0[NE] = {[EN_SELECT] = 0, [EN_ALGO] = 1, [EN_PRESET] = -1, [EN_K1] = 2, 3, 4, 5},
                            V1[NE] = {[EN_SELECT] = -1, [EN_ALGO] = -1, [EN_PRESET] = -1, [EN_K1] = -1, 6, 7, 8};
        const int view = ui.page == PG_OBARS && ui.oview;
        const int32_t b = view ? V1[k] : V0[k];
        if (k == EN_PRESET) {                /* (the preset, on every view: its sound at once) */
            char t[8];
            organ_pick((uint32_t)clamp((int32_t)set.b3_slot + e, 0, ONSLOT - 1));
            slot_p_text(set.b3_slot, t);
            app_say(t, oname);
            return 0;
        }
        if (b < 0)
            return 0;
        opatch.bar[b] = (uint8_t)clamp((int32_t)opatch.bar[b] + (e > 0 ? 1 : -1), 0, 8);
        organ_changed();
        o_say_bar((uint32_t)b);
        return 0;
    }
    if (ui.page == PG_OTABS) {
        if (k == EN_SELECT)
            o_flag(&opatch.vib_on, e, "VIBRATO", "OFF", "ON");
        else if (k == EN_ALGO) {
            opatch.vib = (uint8_t)clamp((int32_t)opatch.vib + (e > 0 ? 1 : -1), 0, 5);
            organ_changed();
            app_say("VIBRATO", OVIB[opatch.vib]);
        } else if (k == EN_K1)
            o_flag(&opatch.perc_on, e, "PERCUSSION", "OFF", "ON");
        else if (k == EN_K2)
            o_flag(&opatch.perc_soft, e, "PERC VOLUME", "NORMAL", "SOFT");
        else if (k == EN_K3)
            o_flag(&opatch.perc_fast, e, "PERC DECAY", "SLOW", "FAST");
        else if (k == EN_K4)
            o_flag(&opatch.perc_3rd, e, "PERC HARMONIC", "SECOND", "THIRD");
        return 0;
    }
    if (ui.page == PG_OLES) {
        char t[8];
        uint8_t *v = k == EN_K1 ? &opatch.width : k == EN_K2 ? &opatch.drive : k == EN_K3 ? &opatch.click : 0;
        if (k == EN_SELECT) {
            ui.ofast = e > 0;
            ui.redraw = 1;
            app_say("LESLIE", ui.ofast ? "FAST" : "SLOW");
        } else if (k == EN_ALGO) {
            ui.obrake = e > 0;
            ui.redraw = 1;
            app_say("LESLIE BRAKE", OFF_ON[ui.obrake]);
        } else if (k == EN_K4) {             /* (its volume: a setting, kept with GLO's; SAVE stays dark) */
            set.b3vol_off = (uint8_t)(127 - clamp(127 - (int32_t)set.b3vol_off + e, 0, 127));
            int_text(127 - (int32_t)set.b3vol_off, t);
            ui.redraw = 1;
            app_say("ORGAN VOLUME", t);
        } else if (v) {
            *v = (uint8_t)clamp((int32_t)*v + e, 0, 127);
            organ_changed();
            int_text(*v, t);
            app_say(k == EN_K1 ? "LESLIE WIDTH" : k == EN_K2 ? "DRIVE" : "KEY CLICK", t);
        }
    }
    return 0;
}

/* SAVE, as the bass's (save_button): its HOME's other view, the slot blinking on its display; PRESET picks; SAVE again
 * stores the sound and its name there */
static void o_save_say(void)
{
    char t[8];
    slot_p_text(ui.osave_to - 1u, t);
    app_say("SAVE TO", t);
    ui.msg_until = plat_ms() + 600000u;
}
static void o_save_button(void)
{
    uint32_t s, i;
    if (!ui.osave_to) {
        ui.oview = 1;
        if (ui.page != PG_OBARS)
            page_go(PG_OBARS);
        ui.osave_to = (uint8_t)(set.b3_slot + 1u);
        o_save_say();
        return;
    }
    s = ui.osave_to - 1u;
    ui.osave_to = 0;
    for (i = PNAME_N; i && oname[i - 1u] <= ' '; i--)   /* (its spaces at the end left off) */
        oname[i - 1u] = 0;
    obank.p[s] = opatch;
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
static void o_save_pick(int32_t e)
{
    ui.osave_to = (uint8_t)(clamp((int32_t)ui.osave_to - 1 + e, 0, ONSLOT - 1) + 1);
    o_save_say();
}

/* its buttons (but HOME, REC, GLO, ARP and the octaves: hold_tap and hold_step have ARP) */
static void organ_leslie_tap(void)           /* ARP: chorale or tremolo (the brake let go) */
{
    ui.ofast = (uint8_t)!ui.ofast;
    ui.obrake = 0;
    ui.redraw = 1;
    app_say("LESLIE", ui.ofast ? "FAST" : "SLOW");
}
static void organ_brake(void)                /* ARP held: the brake */
{
    ui.obrake = 1;
    ui.redraw = 1;
    app_say("LESLIE", "BRAKE");
}
static void o_button(uint32_t k)
{
    if (k == B_SEL) {                        /* HOME's view, its other, TABS, LESLIE, PATCH NAME, round */
        if (zoom.home && zoom.hp == PG_OHOME)   /* (on HOME's dive, or its way back: HOME's SEL) */
            ui.page = PG_OHOME, ui.oview = 0;
        if (ui.page == PG_OBARS && !ui.oview) {
            ui.oview = 1;
            ui.redraw = 1;
            app_say("ORGAN DRAWBARS", "1 3/5' 1 1/3' 1'");
        } else {
            const uint32_t g = ui.page == PG_OHOME ? PG_OBARS : ui.page == PG_OBARS ? PG_OTABS
                             : ui.page == PG_OTABS ? PG_OLES : ui.page == PG_OLES ? PG_NAME : PG_OHOME;
            ui.oview = 0;
            sect_open(g, k);
        }
    } else if (k == B_SAVE)
        o_save_button();
    else if (k == B_SEQ) {                   /* (at once, to flutter it) */
        opatch.perc_3rd = (uint8_t)!opatch.perc_3rd;
        organ_changed();
        app_say("PERC HARMONIC", opatch.perc_3rd ? "THIRD" : "SECOND");
    } else if (k == B_FX) {
        opatch.perc_on = (uint8_t)!opatch.perc_on;
        organ_changed();
        app_say("PERCUSSION", OFF_ON[opatch.perc_on]);
    } else if (k == B_EDIT) {
        opatch.vib_on = (uint8_t)!opatch.vib_on;
        organ_changed();
        app_say("VIBRATO", OFF_ON[opatch.vib_on]);
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
 * with the drawbars (OB: from x 990, the B set first, the upper manual's A# set and the pedals' two left out, the
 * lower's two sets closed up after it; the B set as the sound has it, each tip lower by its setting, its shaft over it;
 * the rest in); at the keys' level the cheeks: the left's vibrato knob and tabs (the first the vibrato, down: on), the
 * right's percussion tabs (down: on), the name board between; under them the upper manual, a second roll of keys over
 * the footer's (the FM-1's: the lower manual). ORGAN DRAWBARS is HOME's own console twice as big (oz), its left half
 * from the ledge down, under the same head: the dive in from HOME lands on what it showed */
#define OB(x) (4 + ((int32_t)(x) - 990) * 3057 / 10000)
#define OH_RAIL 78                           /* the rail's top; the tips at rest under it, a stop 4 px further */
#define OH_ROLL (TALL_H - 29)                /* the upper manual's keys (as the footer's: blacks 13, whites 14 under) */
#define OH_CHEEK (OH_ROLL - 11)              /* the cheeks' row */
#define OZ_Y0 (OH_RAIL - 2)                  /* (DRAWBARS: HOME's row at its ledge's foot on the head's: a tip at
                                              * 8 to the page's foot) */
#define OZ_Y 72
static int32_t oz = 1, oz_y0, oz_y;          /* the console's scale; HOME's row oz_y0 at the page's oz_y */
#define OZX(x) ((x) * oz)
#define OZY(y) (((y) - oz_y0) * oz + oz_y)
static void oh_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { cv_rect(OZX(x), OZY(y), w * oz, h * oz, c); }
static void oh_tab(int32_t cx, int on)       /* a cheek's tab, its middle at cx */
{
    oh_rect(cx - 4, OH_CHEEK, 9, 10, RGB(10, 10, 10));
    oh_rect(cx - 4, OH_CHEEK + (on ? 3 : 0), 9, 7, on ? RGB(200, 196, 184) : RGB(237, 235, 222));
    oh_rect(cx - 4, OH_CHEEK + (on ? 3 : 0), 9, 1, on ? grey(120) : RGB(255, 255, 255));
}
static void oh_bar(int32_t x, uint32_t v, uint32_t col, int dim, const char *ft, uint32_t k)
{                                            /* a drawbar at the photo's x, pulled to v; dimmed; up close its footage, its
                                              * encoder's tag */
    static const uint16_t TIP[3] = {RGB(92, 44, 32), RGB(248, 244, 231), RGB(20, 18, 16)},
                          TOP[3] = {RGB(150, 86, 70), RGB(255, 255, 255), RGB(100, 100, 100)},
                          TC[3] = {O_IVORY, RGB(30, 24, 20), O_IVORY};
    const int32_t cx = OB(x), y = OH_RAIL + 2 + 4 * (int32_t)v;
    int32_t i;
    if (v) {                                 /* its shaft, a line a stop */
        oh_rect(cx - 1, OH_RAIL + 2, 3, y - OH_RAIL - 2, dim ? grey(70) : RGB(206, 202, 196));
        for (i = 1; i <= (int32_t)v; i++)
            oh_rect(cx - 1, y - 4 * i + 1, 3, 1, dim ? grey(40) : RGB(70, 66, 62));
    }
    oh_rect(cx - 3, y, 6, 8, dim ? grey(48) : TIP[col]);
    oh_rect(cx - 3, y, 6, 1, dim ? grey(60) : TOP[col]);
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
    if (ui.osave_to && plat_ms() / SAVE_BLINK_MS & 1u)   /* (saving: its slot blinking) */
        d[0] = d[1] = d[2] = ' ', d[3] = 0;
    else
        slot_p_text(ui.osave_to ? ui.osave_to - 1u : set.b3_slot, d);
    seg_display(240 - 8 - 84, 6, d, 3);
    if (odirty)
        disc(240 - 8 - 10, 43, 2, RGB(252, 78, 112));
    cv_text(232 - (cv_text(-1000, 52, &FONT_XS, nm, P_CYAN) + 1000), 52, &FONT_XS, nm, P_CYAN);   /* (its right edge
                                              * the display's) */
    tag(240 - 8 - 42, 26, EN_PRESET);
    text_cat(st, ui.obrake ? "LESLIE BRAKE" : ui.ofast ? "LESLIE FAST" : "LESLIE SLOW");
    text_cat(st, "  ");
    text_cat(st, opatch.vib_on ? OVIB[opatch.vib] : "VIB OFF");
    if (opatch.perc_on)
        text_cat(st, opatch.perc_3rd ? "  PERC 3RD" : "  PERC 2ND");
    cv_text(10, 56, &FONT_T, st, P_SILK);
}
static void oh_face(int dim0, int dim1, const uint8_t *keys)   /* the console at oz: the B set's bars dim0..dim1-1
                                                  * dimmed, keys their encoders (up close) */
{
    static const uint8_t COL[9] = {0, 0, 1, 1, 2, 1, 2, 2, 1};   /* the tips: brown, ivory, black */
    static const char *const FT[9] = {"16'", "5'", "8'", "4'", "2'", "2'", "1'", "1'", "1'"};
    static const int32_t SET[3] = {1005, 1266, 1526};   /* (the B set; the lower's two closed up on it) */
    static const uint8_t AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
    int32_t i, b;
    oh_rect(0, OH_RAIL - 4, 240, 3, RGB(120, 57, 34));   /* the rack's ledge, walnut */
    oh_rect(0, OH_RAIL - 1, 240, 1, RGB(90, 31, 13));
    oh_rect(0, OH_RAIL, 240, OH_ROLL - OH_RAIL, RGB(39, 36, 33));   /* the rail, the tips' slot */
    oh_rect(0, OH_RAIL, 240, 2, RGB(12, 10, 8));
    for (i = 0; i < 3; i++)
        for (b = 0; b < 9; b++)
            oh_bar(SET[i] + (int32_t)b * 2575 / 100, i == 0 ? opatch.bar[b] : 0u, COL[b], i == 0 && b >= dim0 && b < dim1,
                   FT[b], i == 0 && keys ? keys[b] : NE);
    oh_rect(0, OH_CHEEK - 2, 240, OH_ROLL - OH_CHEEK + 2, RGB(16, 14, 12));   /* the cheeks' row, the name board */
    disc(OZX(10), OZY(OH_CHEEK + 4), 4 * oz, RGB(30, 28, 26));   /* the vibrato's knob, its pointer V1..C3 */
    {
        const float a = (0.75f + 0.3f * (float)opatch.vib) * FM_PI;
        cv_line(OZX(10), OZY(OH_CHEEK + 4), OZX(10) + (int32_t)(fm_cosf(a) * 4.0f * (float)oz),
                OZY(OH_CHEEK + 4) + (int32_t)(fm_sinf(a) * 4.0f * (float)oz), RGB(236, 232, 220));
    }
    oh_tab(26, opatch.vib_on);
    oh_tab(38, 0);
    oh_tab(50, 0);
    if (oz == 1)
        print_c(120, OH_CHEEK - 1, "ORGAN SOMETHING", O_GOLD);
    oh_tab(190, opatch.perc_on);
    oh_tab(202, opatch.perc_soft);
    oh_tab(214, opatch.perc_fast);
    oh_tab(226, opatch.perc_3rd);
    for (i = 0; i < 16; i++)                 /* the upper manual: as the footer's keys */
        oh_rect(8 + i * 14, OH_ROLL + 14, 13, 14, P_KEYW);
    for (i = 0; i < 11; i++) {
        oh_rect(8 + (int32_t)AT[i] * 14 + 7, OH_ROLL, 13, 13, P_EDGE);
        oh_rect(8 + (int32_t)AT[i] * 14 + 8, OH_ROLL, 11, 12, P_KEYB);
    }
}
static void ohome_draw(void)
{
    oz = 1, oz_y0 = 0, oz_y = 0;
    cv_rect(0, 0, 240, TALL_H, P_BG);
    oh_face(9, 9, 0);
    oh_head("SOMETHING", "B3");
}
static void zoom_organ(uint32_t k)           /* ORGAN HOME's drawbar turned: the camera in on DRAWBARS (its middle: HOME's
                                              * x 60, the row under the page's 80) */
{
    zoom_dive(PG_OHOME, PG_OBARS, k, 0, 60, OZ_Y0 + (80 - OZ_Y) / 2, 1.0f);
}
static void obars_draw(void)                 /* ORGAN DRAWBARS: HOME's console twice as big; its other view the last three */
{
    static const uint8_t K0[9] = {EN_SELECT, EN_ALGO, EN_K1, EN_K2, EN_K3, EN_K4, NE, NE, NE},
                         K1[9] = {NE, NE, NE, NE, NE, NE, EN_K2, EN_K3, EN_K4};
    oz = 2, oz_y0 = OZ_Y0, oz_y = OZ_Y;
    cv_rect(0, 0, 240, TALL_H, P_BG);
    oh_face(ui.oview ? 0 : 6, ui.oview ? 6 : 9, ui.oview ? K1 : K0);
    oz = 1, oz_y0 = 0, oz_y = 0;
    oh_head("DRAWBARS", ui.oview ? "2" : "1");
}
static void o_tab(int32_t cx, int on, uint32_t k, const char *top, const char *bottom, int lit)   /* a rocker tab */
{
    cv_rect(cx - 13, 58, 26, 46, RGB(16, 10, 6));
    cv_rect(cx - 11, on ? 64 : 60, 22, 38, lit ? RGB(250, 246, 230) : RGB(226, 218, 196));
    cv_rect(cx - 11, on ? 64 : 60, 22, 2, on ? grey(150) : RGB(255, 255, 255));
    tag(cx, 80, k);
    print_c(cx, 108, top, RGB(236, 232, 220));
    print_c(cx, 120, bottom, RGB(200, 170, 120));
}
static void otabs_draw(void)
{
    uint32_t i;
    o_wood();
    panel_head("PERCUSSION + VIBRATO");
    o_tab(28, opatch.perc_on, EN_K1, "PERC", opatch.perc_on ? "ON" : "OFF", knob_lit(EN_K1));
    o_tab(64, opatch.perc_soft, EN_K2, "VOLUME", opatch.perc_soft ? "SOFT" : "NORMAL", knob_lit(EN_K2));
    o_tab(100, opatch.perc_fast, EN_K3, "DECAY", opatch.perc_fast ? "FAST" : "SLOW", knob_lit(EN_K3));
    o_tab(136, opatch.perc_3rd, EN_K4, "HARMONIC", opatch.perc_3rd ? "THIRD" : "SECOND", knob_lit(EN_K4));
    o_tab(176, opatch.vib_on, EN_SELECT, "VIBRATO", opatch.vib_on ? "ON" : "OFF", knob_lit(EN_SELECT));
    bs2_knob(214, 80, 14, opatch.vib, 5, knob_lit(EN_ALGO) ? KF_LIT : 0u);   /* the vibrato's knob, its six round it */
    tag(214, 80, EN_ALGO);
    for (i = 0; i < 6u; i++)
        cv_text(198 + (int32_t)(i % 3u) * 12, i < 3u ? 54 : 100, &FONT_T, OVIB[i], i == opatch.vib ? P_CYAN : grey(150));
    print_c(214, 120, OVIB[opatch.vib], RGB(200, 170, 120));
}
static void o_switch(int32_t cx, int32_t cy, uint32_t k, int on, const char *n0, const char *n1, const char *name)
{                                            /* a two-way switch lying down: its name over it, its setting under it */
    print_c(cx, cy - 22, name, P_SILK);
    slide_h(cx - 16, cy - 9, 2, on != 0, knob_lit(k) ? KF_LIT : 0u);
    tag(cx, cy, k);
    print_c(cx, cy + 12, on ? n1 : n0, P_CYAN);
}
static void oles_draw(void)
{
    static uint32_t at;
    static float ang, hz;
    const uint32_t now = plat_ms();
    const float to = ui.obrake ? 0.0f : ui.ofast ? 6.75f : 0.83f, dt = (float)(now - at) * 0.001f;
    static const char *const LBL[4] = {"WIDTH", "DRIVE", "CLICK", "VOLUME"};
    const int32_t vals[4] = {opatch.width, opatch.drive, opatch.click, 127 - (int32_t)set.b3vol_off};
    int32_t i, hx, hy;
    char t[8];
    at = now;
    if (dt < 0.5f) {                         /* (the horn as the engine turns it: its speed's own run) */
        hz += (to - hz) * fm_minf(dt / (to > hz ? 0.6f : 1.3f), 1.0f);
        ang += hz * dt;
        ang -= (float)(int32_t)ang;
    }
    o_wood();
    panel_head("LESLIE");
    cv_rect(14, 24, 74, 90, RGB(70, 42, 24));   /* the cabinet: its louvres, the horn turning in them */
    for (i = 0; i < 6; i++)
        cv_rect(20, 30 + 6 * i, 62, 3, RGB(20, 12, 8));
    hx = (int32_t)(fm_cosf(2.0f * FM_PI * ang) * 26.0f), hy = (int32_t)(fm_sinf(2.0f * FM_PI * ang) * 4.0f);
    cv_line(51 - hx, 48 - hy, 51 + hx, 48 + hy, RGB(220, 200, 160));
    disc(51 + hx, 48 + hy, 3, RGB(240, 220, 180));
    disc(51, 92, 14, RGB(30, 18, 10));
    cv_text(26, 116, &FONT_T, ui.obrake ? "BRAKE" : ui.ofast ? "TREMOLO" : "CHORALE", P_CYAN);
    o_switch(112, 54, EN_SELECT, ui.ofast, "SLOW", "FAST", "Speed");
    o_switch(112, 106, EN_ALGO, ui.obrake, "OFF", "ON", "Brake");
    for (i = 0; i < 4; i++) {
        const int32_t cx = 160 + 46 * (i & 1), cy = 52 + 56 * (i >> 1);
        bs2_knob(cx, cy, 13, vals[i], 127, knob_lit(EN_K1 + (uint32_t)i) ? KF_LIT : 0u);
        tag(cx, cy, EN_K1 + (uint32_t)i);
        print_c(cx, cy + 16, LBL[i], RGB(236, 232, 220));
        int_text(vals[i], t);
        print_c(cx, cy + 28, t, RGB(200, 170, 120));
    }
}
