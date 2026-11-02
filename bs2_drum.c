/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 John Brown */
/* Drum Something: the drum voices, after the Sequential Drumtraks (docs/DRUMTRAKS_RESEARCH.md). Voices so far: Bass,
 * Snare, Rim and the Toms; the other drums have none yet.
 *
 * As the 400: each voice is an 8-bit sample read at a rate the tuning sets (one rate, so a higher tuning is shorter,
 * brighter and higher at once: docs/DT_MEASURE.md), held from one sample to the next with nothing smoothing it (its
 * aliasing is the sound), through a DAC whose reference is the level; on channels 1 and 3 a VCF after it, its cutoff
 * moving with the rate and closed by its own envelope. The samples are not stored: each is made as it is read, in the
 * sample's own time (ROM time: the voice's rom samples a second at the first tuning step), by a bank of resonators
 * tuned to the drum's modes, so its pitch, decay and colour all move with the rate as a ROM's would. Six channels, one
 * sound each at a time, as the 400's (a hit restarts its channel: Snare and Rim share one, as do the Toms).
 *
 * Levels in the 400's units (0..255 into the DAC): the volume step's 11 each (0..175), an accent's 80 more (the
 * recordings' +3.3 dB is 255 against 175). The sound goes into app_render's output after the bass's, not through its
 * filter or distortion. MIDI notes on DT_CHAN play them (the 400's key map); the main loop's hits come in dtq. */
enum { DT_BASS, DT_SNARE, DT_RIM, DT_TOM1, DT_TOM2, DT_CRASH, DT_RIDE, DT_CHH, DT_OHH, DT_CLAPS, DT_TAMB, DT_COWBELL,
       DT_CABASA, NDT };
enum { DTC_BASS, DTC_SNARE, DTC_TOMS, DTC_CYM, DTC_HATS, DTC_PERC, NDTCH };
#define DT_CHAN 10u                          /* MIDI channel in (the 400's was 15 in Omni; GM's drums are 10) */
#define DT_DAC_MU 0                          /* 1: the DAC companding (mu-law); 0: linear 8 bits (TN400-0
                                              * names no DAC part: linear until known) */
#define DT_SN_RATE 17400.0f                  /* the Snare's ROM samples a second at the first step (its recordings'
                                              * sample-and-hold notches: 17.5 and 34.7 kHz) */
#define DT_TOM_RATE 17400.0f                 /* the Toms' (their VCF hides the notches: a guess) */
#define DT_NONE 0xFFu
#define DT_NM 16u                            /* resonators a channel */
/* the tunables (bs2/tools/dt_compare.py against the recordings) */
#define DT_STICK_TAU 0.0004f                 /* the stick's noise burst's decay, seconds in ROM time */
#define DT_TOM_GAIN 0.5f                     /* the Toms' level */
#define DT_TOM_STICK 0.15f                   /* the stick's noise burst into their resonators */
#define DT_TOM_VCF 5000.0f                   /* their VCF's open cutoff at the first step, Hz (follows the tuning) */
#define DT_TOM_VCF_LO 0.025f                 /* where its envelope closes it to, against open */
#define DT_TOM_VCF_T 0.035f                  /* its envelope's decay (time constant, s; real time: a capacitor's) */
#define DT_TOM_LP 0.6f                       /* their output filter (dt_voice_t lp) */
#define DT_RIM_GAIN 0.6f                     /* the Rim's level */
#define DT_RIM_STICK 0.6f                    /* its stick's noise burst into the modes */
#define DT_RIM_CLICK 0.0f                    /* its stick's burst straight out (its crack) */
#define DT_RIM_LP 0.45f                      /* its output filter (dt_voice_t lp; 0: none) */
#define DT_RIM_LOWM -12                      /* its two lowest modes, dB more */
#define DT_RIM_HISS 0                        /* its crackle's two bands, dB */
#define DT_RIM_WIRE_G 0.4f                   /* the crackle's noise into them */
#define DT_RIM_WIRES 0.03f                   /* its decay, s in ROM time */
#define DT_RIM_BODY 0.4f                     /* its modes' decays against the recordings' */
#define DT_RIM_ROOM 0.1f                     /* its send into the room */
#define DT_RIM_RATE 24700.0f                 /* its ROM samples a second at the first step (its notches: steps 1-4
                                              * agree within 1 %) */
#define DT_BD_GAIN 0.8f                      /* the Bass's level */
#define DT_BD_HZ 32                          /* its pitch at rest at the first step, Hz in ROM time */
#define DT_BD_SW 1.9f                        /* its pitch's quick fall: the excess at the hit (dt_voice_t sweep) */
#define DT_BD_SW_T 0.02f                     /* and its time constant, s in ROM time */
#define DT_BD_SW2 0.05f                      /* the slower fall's excess */
#define DT_BD_SW2_T 0.06f                    /* and its time constant */
#define DT_BD_STICK 0.2f                     /* its beater's noise burst into the modes */
#define DT_BD_OVER 0                         /* its overtones, dB more */
#define DT_BD_THUD 200.0f                    /* its beater's thud's lowest mode, Hz (the others 2.17 and 4.33 times) */
#define DT_BD_MID -10                        /* its beater's thud's level, dB (three broad modes in the middle) */
#define DT_BD_CLICK 0.7f                     /* and straight out */
#define DT_BD_BODY 1.2f                      /* its modes' decays against the recordings' */
#define DT_BD_LEN 0.3f                       /* its sample's length, s (it stops about 110 ms in at the first step) */
#define DT_BD_VCF 2000.0f                    /* its VCF's open cutoff at the first step, Hz (follows the tuning) */
#define DT_BD_VCF_LO 0.05f                   /* where its envelope closes it to, against open */
#define DT_BD_VCF_T 0.012f                   /* its envelope's decay (time constant, s) */
#define DT_BD_LP 0.18f                       /* its output filter (dt_voice_t lp) */
#define DT_BD_ATTACK 0.003f                  /* its swell at the start (dt_voice_t attack), s */
#define DT_BD_GATE 0.098f                    /* its cliff, s in ROM time (dt_voice_t gate) */
#define DT_BD_GATE_DB 26.0f                  /* how far it falls, dB */
#define DT_BD_ROOM 0.0f                      /* its send into the room */
#define DT_BD_RATE 17400.0f                  /* its ROM samples a second (its VCF hides the notches: a guess) */
#define DT_SN_GAIN 0.6f                      /* the Snare's level */
#define DT_SN_STICK 0.2f                     /* the stick's noise burst into the shell's resonators */
#define DT_SN_WIRES 0.08f                    /* the wires' decay, seconds in ROM time */
#define DT_SN_WIRE_G 0.55f                   /* the wires' noise into their bands */
#define DT_SN_NOISE 0.15f                    /* the wires' noise straight out */
#define DT_TOM_CLICK 4.0f                    /* the Toms' stick crack, straight out (dt_voice_t click) */
#define DT_TOM_MID 4                         /* the Toms' bloom's level, dB (three broad modes in the middle) */
#define DT_TOM_LOW 0.8f                      /* the Toms' body longer by this at the first step (dt_voice_t low) */
#define DT_TOM_BODY 0.95f                    /* the Toms' modes' decays against the recordings' (the room the rest) */
#define DT_TOM_ROOM 0.5f                     /* the Toms' send into the room */
#define DT_SN_ROOM 0.06f                     /* the Snare's */
#define DT_ROOM_RT 0.45f                     /* the room's reverberation time (-60 dB), s */
#define DT_ROOM_DAMP 0.85f                   /* its walls' damping: a one-pole low pass's step in its loop */
#define DT_ROOM_IN 0.06f                     /* the room's input low pass, a one-pole's step (darker in, no tin) */
#define DT_ROOM_WET 0.6f                     /* its level out */
#define DT_SN_LOW 0.5f                       /* the Snare's body longer by this at the first step (dt_voice_t low) */
#define DT_SN_BODY 1.0f                      /* the Snare's modes' decays against the recordings' */
#define DT_SN_SMACK -16                      /* the smack's level, dB (two broad modes, 300 and 420 Hz) */
#define DT_SN_WIRE_RISE 0.004f               /* the wires' noise's rise (s): they buzz once the head has moved */
#define DT_SN_WIRE_LO -6                     /* the wires' three lowest bands, dB more (their low-mid weight) */
#define DT_SN_WIRE_Q 0.0006f                 /* the wires' bands' decay (s): their width (it scales with the
                                              * rate: narrower, they ring metal at the low tunings) */
#define DT_SN_LP 0.8f                        /* the Snare's output filter (dt_voice_t lp) */

static const char *const DT_NAME[NDT] = {"BASS", "SNARE", "RIM", "TOM1", "TOM2", "CRASH", "RIDE", "CHH", "OHH",
                                         "CLAPS", "TAMB", "COWBELL", "CABASA"};
static const uint8_t DT_CH[NDT] = {DTC_BASS, DTC_SNARE, DTC_SNARE, DTC_TOMS, DTC_TOMS, DTC_CYM, DTC_CYM, DTC_HATS,
                                   DTC_HATS, DTC_PERC, DTC_PERC, DTC_PERC, DTC_PERC};
/* each tuning step's rate, Q12, 4096 the modes' own (docs/DT_MEASURE.md's spectrum shifts; the Snare's first
 * step set by its pitch, 92 Hz: its shift read it 5 % low) */
static const uint16_t DT_RATE_TOM[16] = {4096, 4215, 4768, 5710, 6410, 7143, 7959, 8806, 9884, 10777, 11923, 13099,
                                         14701, 16031, 17740, 19345};
static const uint16_t DT_RATE_SNARE[16] = {4331, 5878, 6410, 6889, 7459, 7901, 8192, 8557, 8999, 9196, 9601, 9953,
                                           10248, 10248, 10625, 11096};
static const uint16_t DT_RATE_RIM[16] = {4096, 4477, 4791, 4998, 5200, 5430, 5609, 5839, 6052, 6365, 6528, 6774,
                                         7043, 7323, 7620, 8282};
static const uint16_t DT_RATE_BASS[16] = {4096, 5403, 6405, 6648, 6920, 7195, 7504, 7804, 8122, 8479, 8726, 8726,
                                          9053, 9473, 9884, 10263};   /* (tools/dt_voice.py --cycles) */
/* the MIDI key map (the 400's, 17 Dec 1983): notes 35..58, DT_NONE where nothing plays */
static const uint8_t DT_KEY[24] = {DT_BASS, DT_BASS, DT_RIM, DT_SNARE, DT_CLAPS, DT_SNARE, DT_TOM1, DT_CHH, DT_TOM1,
                                   DT_CHH, DT_TOM2, DT_OHH, DT_TOM2, DT_NONE, DT_CRASH, DT_NONE, DT_RIDE, DT_NONE,
                                   DT_NONE, DT_TAMB, DT_NONE, DT_COWBELL, DT_NONE, DT_CABASA};

/* A voice is a bank of resonators, each a drum's mode: in ROM time its frequency, its level (dB under the
 * strongest), its decay (time constant, s) and its flags: struck by the stick (a click and a burst of noise) or,
 * DT_MW, driven by the wires' noise while it lasts; following the voice's pitch fall or, DT_MF, not (a beater's
 * thud is the beater's, not the head's). The modes are the recordings' strongest tones, divided by each step's rate
 * (bs2/tools/dt_measure.py's tones(): they hold still across the steps, as a ROM's would). The noise is new each
 * hit: no two alike. */
typedef struct {
    float hz, db, tau;
    uint8_t flags;                           /* DT_MW, DT_MF */
} dt_mode_t;
#define DT_MW 1u                             /* a wire: driven by the wires' noise, not struck */
#define DT_MF 2u                             /* fixed: not following the pitch's fall */
static const dt_mode_t DT_MODES_TOM[] = {    /* the head's modes at rest; all start a third higher (DT_VOICE) */
    {62, 0, 0.35f, 0}, {90, -30, 0.30f, 0}, {101, -21, 0.25f, 0}, {124, -21, 0.20f, 0}, {137, -22, 0.18f, 0},
    {152, -30, 0.12f, 0}, {174, -21, 0.20f, 0}, {193, -26, 0.20f, 0}, {239, -25, 0.18f, 0}, {268, -21, 0.15f, 0},
    {393, -12, 0.04f, 0},
    {450, DT_TOM_MID, 0.003f, 0}, {900, DT_TOM_MID - 2, 0.002f, 0}, {1600, DT_TOM_MID - 4, 0.0015f, 0}};   /* the
                                              * shell's bloom as the stick lands: broad, short, the middle */
static const dt_mode_t DT_MODES_SNARE[] = {  /* the shell's cluster, its smack (broad), then the wires' bands (broad:
                                              * they hiss; narrow, they ring like a pipe on a plate) */
    {82, -12, 0.065f, 0}, {90, 0, 0.060f, 0}, {102, -13, 0.060f, 0}, {111, -16, 0.065f, 0}, {128, -14, 0.050f, 0},
    {163, -16, 0.060f, 0}, {178, -15, 0.075f, 0}, {300, DT_SN_SMACK, 0.005f, 0}, {420, DT_SN_SMACK, 0.004f, 0},
    {622, -21 + DT_SN_WIRE_LO, DT_SN_WIRE_Q, DT_MW}, {757, -24 + DT_SN_WIRE_LO, DT_SN_WIRE_Q, DT_MW},
    {900, -23 + DT_SN_WIRE_LO, DT_SN_WIRE_Q, DT_MW},
    {1095, -21, DT_SN_WIRE_Q, DT_MW}, {1420, -21, DT_SN_WIRE_Q, DT_MW}, {2035, -22, DT_SN_WIRE_Q, DT_MW},
    {2870, -21, DT_SN_WIRE_Q, DT_MW}};
static const dt_mode_t DT_MODES_RIM[] = {    /* the stick on the rim: a bright woody cluster, short; its crackle */
    {1080, 0, 0.021f, 0}, {1116, 0, 0.028f, 0}, {795, -3, 0.025f, 0}, {1892, -3, 0.030f, 0}, {2876, -3, 0.022f, 0},
    {1197, -5, 0.047f, 0}, {983, -6, 0.043f, 0}, {691, -6, 0.028f, 0}, {375, -6, 0.023f, 0}, {498, -7, 0.017f, 0},
    {196, -7 + DT_RIM_LOWM, 0.043f, 0}, {136, -7 + DT_RIM_LOWM, 0.066f, 0}, {575, -8, 0.017f, 0},
    {3600, DT_RIM_HISS, DT_SN_WIRE_Q, DT_MW}, {5200, DT_RIM_HISS, DT_SN_WIRE_Q, DT_MW}};   /* its crackle */
static const dt_mode_t DT_MODES_BASS[] = {   /* the kick at rest (it starts nearly three times higher: DT_BD_SW),
                                              * a few weak overtones */
    {DT_BD_HZ, 0, 0.12f, 0}, {66, -19 + DT_BD_OVER, 0.03f, 0}, {76, -26 + DT_BD_OVER, 0.04f, 0},
    {95, -26 + DT_BD_OVER, 0.04f, 0}, {100, -29 + DT_BD_OVER, 0.04f, 0},
    {108, -29 + DT_BD_OVER, 0.04f, 0},
    {DT_BD_THUD, DT_BD_MID, 0.004f, DT_MF}, {DT_BD_THUD * 2.17f, DT_BD_MID - 4, 0.003f, DT_MF},
    {DT_BD_THUD * 4.33f, DT_BD_MID - 8, 0.002f, DT_MF}};   /* the
                                              * beater's thud: broad, short, the middle */
typedef struct {                             /* a voice: the generic parts dt_hit and dt_gen play from */
    const uint16_t *rate;                    /* the tuning steps' rates */
    const dt_mode_t *m;
    uint8_t n;                               /* its modes */
    float rom;                               /* ROM samples a second at the first step */
    float len, gain;                         /* the sample's length (s), the level */
    float sweep, sweep_tau, sweep2, sweep2_tau;   /* the pitch over the modes' at rest: two excesses, each falling
                                              * away (time constants, s): a quick drop, then a slow sinking all
                                              * through the tail (a real head's; a steady one sounds a synth's) */
    float stick, click;                      /* the stick's burst into the modes, and straight out (its crack) */
    float wires, wires_tau, wires_rise, noise;   /* the wires' noise into their bands: its level, decay and rise (s),
                                              * and straight out */
    float body, low;                         /* the modes' decays' scale; longer by low at the first step, fading out
                                              * by the fourth (the 400's lowest tunings are their own: the Snare's
                                              * first step is off its others' pitch pattern too) */
    float vcf, vcf_lo, vcf_t;                /* the VCF after the DAC (channels 1 and 3, TN400-0 pg. 7): its cutoff
                                              * open at the first step, Hz, moving with the rate (0: none); its own
                                              * envelope closing it to vcf_lo of that, with time constant vcf_t (s) */
    float lp;                                /* the output low pass (fixed: not in ROM time), a one-pole's step at
                                              * BS2_RATE (1 - e^(-2 pi fc / rate)); 0: none */
    float attack;                            /* the modes' swell at the start, s in ROM time (a kick's head pushed
                                              * out over its first cycle; the click is not swelled); 0: at once */
    float gate, gate_db;                     /* at gate s (ROM time) the level falls by gate_db over about 2 ms, and
                                              * plays on there (a VCA pulled down: the Bass's cliff, the closed
                                              * hi-hat's); 0: none */
    float room;                              /* the send into the room */
} dt_voice_t;
static const dt_voice_t DT_V_SNARE = {
    .rate = DT_RATE_SNARE, .m = DT_MODES_SNARE, .n = 16, .rom = DT_SN_RATE, .len = 0.40f, .gain = DT_SN_GAIN,
    .stick = DT_SN_STICK, .wires = DT_SN_WIRE_G, .wires_tau = DT_SN_WIRES, .wires_rise = DT_SN_WIRE_RISE,
    .noise = DT_SN_NOISE, .body = DT_SN_BODY, .low = DT_SN_LOW, .lp = DT_SN_LP, .room = DT_SN_ROOM};
static const dt_voice_t DT_V_TOM = {         /* (both Toms: one ROM, as the 400's) */
    .rate = DT_RATE_TOM, .m = DT_MODES_TOM, .n = 14, .rom = DT_TOM_RATE, .len = 1.33f, .gain = DT_TOM_GAIN,
    .sweep = 0.22f, .sweep_tau = 0.035f, .sweep2 = 0.20f, .sweep2_tau = 0.33f, .stick = DT_TOM_STICK,
    .click = DT_TOM_CLICK, .body = DT_TOM_BODY, .low = DT_TOM_LOW, .vcf = DT_TOM_VCF, .vcf_lo = DT_TOM_VCF_LO,
    .vcf_t = DT_TOM_VCF_T, .lp = DT_TOM_LP, .room = DT_TOM_ROOM};
static const dt_voice_t DT_V_RIM = {
    .rate = DT_RATE_RIM, .m = DT_MODES_RIM, .n = 15, .rom = DT_RIM_RATE, .len = 0.42f, .gain = DT_RIM_GAIN,
    .stick = DT_RIM_STICK, .click = DT_RIM_CLICK, .wires = DT_RIM_WIRE_G, .wires_tau = DT_RIM_WIRES,
    .body = DT_RIM_BODY, .lp = DT_RIM_LP, .room = DT_RIM_ROOM};
static const dt_voice_t DT_V_BASS = {
    .rate = DT_RATE_BASS, .m = DT_MODES_BASS, .n = 9, .rom = DT_BD_RATE, .len = DT_BD_LEN, .gain = DT_BD_GAIN,
    .sweep = DT_BD_SW, .sweep_tau = DT_BD_SW_T, .sweep2 = DT_BD_SW2, .sweep2_tau = DT_BD_SW2_T, .stick = DT_BD_STICK,
    .click = DT_BD_CLICK, .body = DT_BD_BODY, .vcf = DT_BD_VCF, .vcf_lo = DT_BD_VCF_LO, .vcf_t = DT_BD_VCF_T,
    .lp = DT_BD_LP, .attack = DT_BD_ATTACK, .gate = DT_BD_GATE, .gate_db = DT_BD_GATE_DB, .room = DT_BD_ROOM};
static const dt_voice_t *const DT_VOICE[NDT] = {[DT_BASS] = &DT_V_BASS, [DT_SNARE] = &DT_V_SNARE,
                                                [DT_RIM] = &DT_V_RIM, [DT_TOM1] = &DT_V_TOM, [DT_TOM2] = &DT_V_TOM};

typedef struct {
    uint32_t pos, inc;                       /* the read position (ROM samples, Q16) and its step an output sample */
    uint32_t k, len;                         /* the ROM sample being held, and where the sample ends */
    float out, lvl;                          /* the held sample (after the DAC) and the level, 0..1 */
    float v1, v2, vk;                        /* the VCF: two one-poles, their step (1: none) */
    float vw, vlo, ve, ve_k;                 /* its open cutoff (rad a sample), its floor, its envelope and decay */
    uint32_t vt;                             /* output samples since the hit (the envelope's steps) */
    float lp, lpk;                           /* the output filter (fixed: not in ROM time) and its step */
    float att, att_k;                        /* the attack's swell still to come, its decay a sample */
    uint32_t gate;                           /* the ROM sample where the gate falls (0: none) */
    float gl, glo, gl_k;                     /* the gate's level now, where it falls to, its step a sample */
    float send;                              /* into the room */
    float burst, burst_k, wire, wire_k;      /* the stick's burst and the wires' noise: levels, decays a sample */
    float rise, rise_k;                      /* the wires' rise: subtracted from their level, falling away */
    float sw, sw_k, sw2, sw2_k;              /* the pitch's two excesses, their decays every 8 samples */
    float y1[DT_NM], y2[DT_NM];              /* the resonators: their last two outputs, */
    float a1[DT_NM], a2[DT_NM], r[DT_NM], w[DT_NM], g[DT_NM];   /* coefficients, radius, angle, input gain */
    const dt_voice_t *v;
    uint8_t drum;                            /* DT_*, or DT_NONE: silent */
} dt_ch_t;

static struct {
    uint8_t vol[NDT], tune[NDT];             /* each drum's VOLUME and TUNING, 0..15 */
    dt_ch_t ch[NDTCH];
} dt PLAT_POOL;
/* The room: a drum's tail is mostly the space it was played in (the 400's own samples had theirs in them), and a
 * resonator ringing on sounds a synth's. So the voices' modes ring short and a small room makes the tail, in real
 * time (not ROM time: the same room at every tuning). Two allpasses smear the hit, then four delay lines (23..41 ms)
 * fed back through a Hadamard mix, each with its walls' low pass; their sum is the room's sound. */
#define DT_NRL 4u                            /* the delay lines */
#define DT_NAP 4u                            /* the allpasses smearing the hit (four: two left a tin can's ring) */
static const uint16_t DT_RL[DT_NRL + DT_NAP] = {1031, 1327, 1523, 1801, 142, 107, 379, 277};   /* lines, allpasses */
typedef struct {
    uint16_t at, len, i;                     /* a ring in dt_room.buf: where it starts, its length, its place */
} dt_ring_t;
static struct {
    float buf[1031 + 1327 + 1523 + 1801 + 142 + 107 + 379 + 277];
    dt_ring_t ring[DT_NRL + DT_NAP];
    float in, lp[DT_NRL], g[DT_NRL];         /* the input's filter, the walls', each line's feedback for DT_ROOM_RT */
    uint32_t idle;                           /* samples since a voice last played (the room stops once it is quiet) */
} dt_room PLAT_POOL;
static fifo_t dtq;                           /* main loop -> audio: a hit, drum | tune << 4 | level << 8 */
static uint32_t dt_rng = 1u;                 /* the noise's: never reset, so each hit's is its own */
#ifdef PLAT_HOST
static float dt_test_gain = 1.0f;            /* (the host's dt_gain: renders well above its 16-bit files' floor) */
#endif

static void dt_init(void)
{
    uint32_t i;
    for (i = 0; i < NDT; i++) {
        dt.vol[i] = 15u;
        dt.tune[i] = 7u;
    }
    dt.tune[DT_TOM1] = 9u;                   /* (placeholders until the 400's own defaults are known) */
    dt.tune[DT_TOM2] = 5u;
    for (i = 0; i < NDTCH; i++)
        dt.ch[i].drum = DT_NONE;
    {
        uint16_t at = 0;
        for (i = 0; i < DT_NRL + DT_NAP; i++) {
            dt_room.ring[i] = (dt_ring_t){at, DT_RL[i], 0};
            at = (uint16_t)(at + DT_RL[i]);
        }
        for (i = 0; i < DT_NRL; i++)         /* -60 dB in DT_ROOM_RT: 10^(-3 len / (RT rate)) a pass */
            dt_room.g[i] = fm_exp2f(-3.0f * 3.321928f * (float)DT_RL[i] / (DT_ROOM_RT * BS2_RATE));
        dt_room.idle = 0xFFFFFFFFu;
    }
}

/* a ring's sample now, and the ring on a sample */
static inline float *dt_ring_at(dt_ring_t *r) { return &dt_room.buf[r->at + r->i]; }
static inline void dt_ring_on(dt_ring_t *r) { r->i = (uint16_t)(r->i + 1u >= r->len ? 0u : r->i + 1u); }

/* the room's next sample, from x (audio interrupt) */
static inline float dt_room_next(float x)
{
    float y[DT_NRL], h0, h1, h2, h3, out = 0.0f;
    uint32_t k;
    x = dt_room.in += (x - dt_room.in) * DT_ROOM_IN;
    for (k = DT_NRL; k < DT_NRL + DT_NAP; k++) {    /* the allpasses (0.6) */
        float *p = dt_ring_at(&dt_room.ring[k]), t = *p;
        *p = x + 0.6f * t;
        x = t - 0.6f * *p;
        dt_ring_on(&dt_room.ring[k]);
    }
    for (k = 0; k < DT_NRL; k++) {           /* the lines' outputs, through the walls */
        y[k] = *dt_ring_at(&dt_room.ring[k]);
        out += y[k];
        dt_room.lp[k] += (y[k] - dt_room.lp[k]) * (1.0f - DT_ROOM_DAMP);
    }
    h0 = dt_room.lp[0] + dt_room.lp[1];      /* the Hadamard mix (a half: it keeps the energy) */
    h1 = dt_room.lp[0] - dt_room.lp[1];
    h2 = dt_room.lp[2] + dt_room.lp[3];
    h3 = dt_room.lp[2] - dt_room.lp[3];
    y[0] = h0 + h2, y[1] = h1 + h3, y[2] = h0 - h2, y[3] = h1 - h3;
    for (k = 0; k < DT_NRL; k++) {
        *dt_ring_at(&dt_room.ring[k]) = x + 0.5f * y[k] * dt_room.g[k];
        dt_ring_on(&dt_room.ring[k]);
    }
    return out * (0.25f * DT_ROOM_WET);
}

static float dt_noise(void)
{
    dt_rng = dt_rng * 1664525u + 1013904223u;
    return (float)(int32_t)dt_rng * (1.0f / 2147483648.0f);
}

/* a ROM sample to 8 bits and back: sign and 7 bits, mu-law (mu 255) when DT_DAC_MU (a companding DAC: fine steps
 * for the quiet tail, about 12 bits' range), else linear */
static inline float dt_dac(float x)
{
    float a = fm_minf(fm_fabsf(x), 1.0f), q;
#if DT_DAC_MU
    q = (float)(int32_t)(fm_log2f(1.0f + 255.0f * a) * (127.0f / 8.0f) + 0.5f) * (8.0f / 127.0f);
    q = (fm_exp2f(q) - 1.0f) * (1.0f / 255.0f);
#else
    q = (float)(int32_t)(a * 127.0f) * (1.0f / 127.0f);
#endif
    return x < 0.0f ? -q : q;
}

/* the next ROM sample (-1..1), in ROM time */
static float dt_gen(dt_ch_t *c)
{
    const dt_voice_t *v = c->v;
    float n = dt_noise(), xs, xw, s = 0.0f;
    uint32_t i;
    if (c->sw + c->sw2 > 1e-4f && !(c->k & 7u)) {   /* the pitch falling: the resonators retuned every 8 samples */
        float f = 1.0f + c->sw + c->sw2;
        c->sw *= c->sw_k;
        c->sw2 *= c->sw2_k;
        for (i = 0; i < v->n; i++)
            if (!(v->m[i].flags & DT_MF))
                c->a1[i] = 2.0f * c->r[i] * fm_cosf(c->w[i] * f);
    }
    xs = n * c->burst + (c->k ? 0.0f : 1.0f);   /* the stick: a click, then its noise */
    xw = n * (c->wire - c->rise);
    c->burst *= c->burst_k;
    c->wire *= c->wire_k;
    c->rise *= c->rise_k;
    for (i = 0; i < v->n; i++) {
        float y = c->a1[i] * c->y1[i] - c->a2[i] * c->y2[i] + c->g[i] * (v->m[i].flags & DT_MW ? xw : xs);
        c->y2[i] = c->y1[i];
        c->y1[i] = y;
        s += y;
    }
    s = (s * (1.0f - c->att) + xw * v->noise + (xs - (c->k ? 0.0f : 1.0f)) * v->click) * v->gain;   /* (the swell
                                              * is the modes': the stick's click lands at once) */
    c->att *= c->att_k;
    if (c->gate && c->k >= c->gate) {       /* the gate falling */
        c->gl += (c->glo - c->gl) * c->gl_k;
        s *= c->gl;
    }
    return s;
}

/* a decay's factor every per samples at rate samples a second, time constant tau seconds (0: gone at once) */
static float dt_decay(float tau, float rate, float per)
{
    return tau > 0.0f ? fm_expf(-per / (tau * rate)) : 0.0f;
}

/* a hit: its channel restarted on drum d at tuning step tune (0..15), level 0..255 */
static void dt_hit(uint32_t d, uint32_t tune, uint32_t level)
{
    const dt_voice_t *v = d < NDT ? DT_VOICE[d] : NULL;
    dt_ch_t *c;
    uint32_t i;
    float rom, body, rate;
    if (!v)
        return;
    tune &= 15u;
    rom = v->rom;
    rate = (float)v->rate[tune] * (1.0f / 4096.0f);
    body = v->body * (1.0f + v->low * (float)(tune < 3u ? 3u - tune : 0u) * (1.0f / 3.0f));
    c = &dt.ch[DT_CH[d]];
    *c = (dt_ch_t){0};
    c->v = v;
    c->drum = (uint8_t)d;
    c->inc = (uint32_t)(rate * rom * (65536.0f / BS2_RATE));
    c->len = (uint32_t)(v->len * rom);
    c->lvl = (float)level * (1.0f / 255.0f);
    c->lpk = v->lp > 0.0f ? v->lp : 1.0f;
    c->att = v->attack > 0.0f ? 1.0f : 0.0f;
    c->att_k = dt_decay(v->attack, rom, 1.0f);
    c->gate = (uint32_t)(v->gate * rom);
    c->gl = 1.0f;
    c->glo = fm_db2lin(-v->gate_db);
    c->gl_k = 1.0f - dt_decay(0.002f, rom, 1.0f);
    c->send = v->room;
    c->vk = 1.0f;
    if (v->vcf > 0.0f) {
        c->vw = FM_TWO_PI * v->vcf * rate / BS2_RATE;
        c->vlo = v->vcf_lo;
        c->ve = 1.0f;
        c->ve_k = dt_decay(v->vcf_t, BS2_RATE, 16.0f);
    }
    c->burst = v->stick;
    c->burst_k = dt_decay(DT_STICK_TAU, rom, 1.0f);
    c->wire = c->rise = v->wires;
    c->wire_k = dt_decay(v->wires_tau, rom, 1.0f);
    c->rise_k = dt_decay(v->wires_rise, rom, 1.0f);
    c->sw = v->sweep;
    c->sw_k = dt_decay(v->sweep_tau, rom, 8.0f);
    c->sw2 = v->sweep2;
    c->sw2_k = dt_decay(v->sweep2_tau, rom, 8.0f);
    for (i = 0; i < v->n; i++) {
        const dt_mode_t *m = &v->m[i];
        c->w[i] = FM_TWO_PI * m->hz / rom;
        c->r[i] = dt_decay(m->tau * body, rom, 1.0f);
        c->a1[i] = 2.0f * c->r[i] * fm_cosf(c->w[i] * (m->flags & DT_MF ? 1.0f : 1.0f + v->sweep + v->sweep2));
        c->a2[i] = c->r[i] * c->r[i];
        c->g[i] = fm_db2lin(m->db) * fm_sinf(c->w[i]);   /* (a struck resonator rings at its gain over sin w) */
    }
}

/* a MIDI note on DT_CHAN (audio interrupt): the 400's velocity, 16 levels of its volume steps, accented at 120 up */
static void dt_note(uint32_t note, uint32_t vel)
{
    uint32_t d = note >= 35u && note < 35u + 24u ? DT_KEY[note - 35u] : DT_NONE;
    if (d != DT_NONE && vel)
        dt_hit(d, dt.tune[d], (vel >> 3) * 11u + 10u + (vel >= 120u ? 80u : 0u));
}

/* the drums' next output sample (audio interrupt), -1..1 at a full level */
static inline float dt_next(void)
{
    uint32_t i;
    float s = 0.0f, r = 0.0f;
    for (i = 0; i < NDTCH; i++) {
        dt_ch_t *c = &dt.ch[i];
        uint32_t to;
        if (c->drum == DT_NONE)
            continue;
        dt_room.idle = 0;
        c->pos += c->inc;
        to = c->pos >> 16;
        while (c->k < to) {                  /* each ROM sample passed: made, to 8 bits, through the DAC */
            float x = dt_gen(c);
            c->out = dt_dac(x) * c->lvl;
            if (++c->k >= c->len) {
                c->drum = DT_NONE;
                c->out = 0.0f;
                break;
            }
        }
        if (c->vw > 0.0f && !(c->vt++ & 15u)) {   /* every 16 samples: the VCF's envelope */
            c->vk = fm_minf(1.0f, 1.0f - fm_expf(-c->vw * (c->vlo + (1.0f - c->vlo) * c->ve)));
            c->ve *= c->ve_k;
        }
        c->v1 += (c->out - c->v1) * c->vk;
        c->v2 += (c->v1 - c->v2) * c->vk;
        c->lp += (c->v2 - c->lp) * c->lpk;
        s += c->lp;
        r += c->lp * c->send;
    }
    if (dt_room.idle < (uint32_t)(2.0f * DT_ROOM_RT * BS2_RATE)) {   /* (the room quiet: left alone) */
        dt_room.idle++;
        s += dt_room_next(r);
    }
#ifdef PLAT_HOST
    s *= dt_test_gain;
#endif
    return s;
}

/* a block's start (audio interrupt): the main loop's hits */
static void dt_block(void)
{
    uint32_t e;
    while (fifo_get(&dtq, &e))
        dt_hit(e & 15u, (e >> 4) & 15u, (e >> 8) & 0xFFu);
}
