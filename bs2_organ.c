/* SPDX-License-Identifier: MPL-2.0 */
/* The organ, the fourth instrument (its engine: bs2_b3.c, after the Hammond B-3 and its Leslie): its mode (ui.b3, HOME
 * after the RMI's), its 32 presets as the bass's patches are (PRESET plays one at once; SAVE as a Bass Station II's: its
 * HOME, the slot blinking, PRESET picks, SAVE stores; PATCH NAME its name), its pages and its live buttons. Part of the
 * unity build, included by app.c after the panel's drawing:
 *   ORGAN HOME (PG_OHOME): the drawbars as an organ's, all nine; K1-K4 16' 5 1/3' 8' 4', SELECT ALGO PRESET 2 2/3' 2'
 *     1 3/5'; SEL there its other view: K1 K2 1 1/3' 1', PRESET the preset (the display's)
 *   ORGAN TABS (PG_OTABS): the percussion's tabs (K1 ON, K2 VOLUME SOFT, K3 DECAY FAST, K4 HARMONIC THIRD), the vibrato
 *     (SELECT on, ALGO its knob V1 V2 V3 C1 C2 C3)
 *   ORGAN LESLIE (PG_OLES): the cabinet turning; SELECT its speed, ALGO the brake, K1 WIDTH, K2 DRIVE, K3 CLICK, K4 VOL
 *   SEL: HOME's view, its other, TABS, LESLIE, PATCH NAME, round. ARP the Leslie chorale or tremolo (held: the brake),
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
    if (ui.page == PG_OHOME) {
        static const int8_t V0[NE] = {[EN_SELECT] = 4, [EN_ALGO] = 5, [EN_PRESET] = 6, [EN_K1] = 0, 1, 2, 3},
                            V1[NE] = {[EN_SELECT] = -1, [EN_ALGO] = -1, [EN_PRESET] = -1, [EN_K1] = 7, 8, -1, -1};
        const int32_t b = ui.oview ? V1[k] : V0[k];
        if (ui.oview && k == EN_PRESET) {    /* (the preset: its sound at once) */
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
        if (ui.page != PG_OHOME)
            page_go(PG_OHOME);
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
        if (ui.page == PG_OHOME && !ui.oview) {
            ui.oview = 1;
            ui.redraw = 1;
            app_say("ORGAN HOME", "1 1/3' 1' PRESET");
        } else {
            const uint32_t g = ui.page == PG_OHOME ? PG_OTABS : ui.page == PG_OTABS ? PG_OLES
                             : ui.page == PG_OLES ? PG_NAME : PG_OHOME;
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

/* its pages: an organ's front, dark wood; the drawbars pulled out of their slots, each its colour (brown the 16' and
 * 5 1/3', white the octaves and unisons, black the rest), its numbers along it, its footage on its handle */
#define O_WOOD RGB(56, 32, 18)
static void o_wood(void)
{
    int32_t y;
    for (y = 0; y < TALL_H; y++)             /* (the grain: a few rows darker, by a hash) */
        cv_rect(0, y, 240, 1, (uint32_t)y * 2654435761u >> 29 == 0u ? RGB(44, 24, 14) : O_WOOD);
}
static void o_drawbar(uint32_t b, int32_t cx, int dim, uint32_t k)
{
    static const uint8_t COL[9] = {0, 0, 1, 1, 2, 1, 2, 2, 1};   /* brown, white, black */
    const uint16_t hc = COL[b] == 0u ? RGB(120, 60, 30) : COL[b] == 1u ? RGB(236, 232, 220) : RGB(24, 24, 24),
                   tc = COL[b] == 1u ? RGB(20, 20, 20) : RGB(236, 232, 220);
    const int32_t top = 62, len = 4 + 9 * opatch.bar[b];
    int32_t i;
    cv_rect(cx - 9, top - 3, 18, 4, RGB(8, 6, 4));   /* (its slot) */
    cv_rect(cx - 4, top, 8, len, dim ? grey(80) : grey(200));   /* the shaft, its numbers' ticks */
    for (i = 1; i <= (int32_t)opatch.bar[b]; i++)
        cv_rect(cx - 4, top + 4 + 9 * (i - 1), 8, 1, grey(60));
    cv_rect(cx - 10, top + len, 20, 16, dim ? grey(60) : hc);   /* the handle, its footage */
    cv_rect(cx - 10, top + len + 15, 20, 1, RGB(0, 0, 0));
    print_c(cx, top + len + 2, b == 1u ? "5" : b == 4u ? "2" : b == 6u ? "1" : b == 7u ? "1" : OBAR[b], dim ? grey(36) : tc);
    if (k < NE)
        tag(cx, top + len + 8, k);
}
static void ohome_draw(void)
{
    static const uint8_t K0[9] = {EN_K1, EN_K2, EN_K3, EN_K4, EN_SELECT, EN_ALGO, EN_PRESET, NE, NE},
                         K1[9] = {NE, NE, NE, NE, NE, NE, NE, EN_K1, EN_K2};
    char t[8];
    uint32_t b;
    o_wood();
    cv_rect(0, 0, 240, 2, RGB(20, 12, 8));
    if (ui.osave_to && plat_ms() / SAVE_BLINK_MS & 1u)   /* (saving: its slot blinking) */
        t[0] = t[1] = t[2] = ' ', t[3] = 0;
    else
        slot_p_text(ui.osave_to ? ui.osave_to - 1u : set.b3_slot, t);
    seg_display(6, 4, t, 3);
    if (odirty)                              /* (changes not saved: its dot) */
        disc(78, 48, 2, RGB(252, 78, 112));
    if (ui.oview)
        tag(42, 28, EN_PRESET);
    cv_text(96, 6, &FONT_B, oname[0] ? oname : "ORGAN", RGB(236, 232, 220));
    cv_text(96, 28, &FONT_T, ui.obrake ? "LESLIE BRAKE" : ui.ofast ? "LESLIE FAST" : "LESLIE SLOW", RGB(200, 170, 120));
    cv_text(96, 42, &FONT_T, opatch.vib_on ? OVIB[opatch.vib] : "VIB OFF", RGB(200, 170, 120));
    cv_text(140, 42, &FONT_T, opatch.perc_on ? (opatch.perc_3rd ? "PERC 3RD" : "PERC 2ND") : "", RGB(200, 170, 120));
    for (b = 0; b < 9u; b++) {
        const int dim = ui.oview ? b < 7u : b >= 7u;
        o_drawbar(b, 16 + 26 * (int32_t)b, dim, ui.oview ? K1[b] : K0[b]);
    }
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
