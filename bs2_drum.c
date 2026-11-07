/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 John Brown */
/* Drum Something: the drum voices, after the Sequential Drumtraks (docs/DRUMTRAKS_RESEARCH.md). Voices so far: all
 * thirteen.
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
 * filter or distortion. MIDI notes on GLO DRUMS' channel play them (the 400's key map), its CCs set each drum's
 * tuning and volume (dt_cc); the main loop's hits come in dtq. */
enum { DT_BASS, DT_SNARE, DT_RIM, DT_TOM1, DT_TOM2, DT_CRASH, DT_RIDE, DT_CHH, DT_OHH, DT_CLAPS, DT_TAMB, DT_COWBELL,
       DT_CABASA, NDT };
enum { DTC_BASS, DTC_SNARE, DTC_TOMS, DTC_CYM, DTC_HATS, DTC_PERC, NDTCH };
#define DT_DAC_MU 0                          /* 1: the DAC companding (mu-law); 0: linear 8 bits (TN400-0
                                              * names no DAC part: linear until known) */
#define DT_SN_RATE 17400.0f                  /* the Snare's ROM samples a second at the first step (its recordings'
                                              * sample-and-hold notches: 17.5 and 34.7 kHz) */
#define DT_TOM_RATE 17400.0f                 /* the Toms' (their VCF hides the notches: a guess) */
#define DT_NONE 0xFFu
#define DT_NM 44u                            /* resonators a channel (the Tambourine: 6 jingles x 6 pairs, 8 more) */
#define DT_NCOPY 8u                          /* a jingle's copies, at most (dt_voice_t copies) */
/* the tunables (bs2/tools/dt_compare.py against the recordings) */
#define DT_STICK_TAU 0.0004f                 /* the stick's noise burst's decay, seconds in ROM time */
#define DT_TOM_SW 0.588f                     /* the Toms' pitch's quick fall: its excess at the hit (sweep) */
#define DT_TOM_SW_T 0.0343f                  /* and its time constant, s in ROM time */
#define DT_TOM_GAIN 0.5f                     /* the Toms' level */
#define DT_TOM_STICK 0.105f                  /* the stick's noise burst into their resonators */
#define DT_TOM_VCF 9800.0f                   /* their VCF's open cutoff at the first step, Hz (follows the tuning) */
#define DT_TOM_VCF_LO 0.025f                 /* where its envelope closes it to, against open */
#define DT_TOM_VCF_T 0.0245f                 /* its envelope's decay (time constant, s; real time: a capacitor's) */
#define DT_TOM_LP 0.6f                       /* their output filter (dt_voice_t lp) */
#define DT_RIM_GAIN 0.6f                     /* the Rim's level */
#define DT_RIM_STICK 0.42f                   /* its stick's noise burst into the modes */
#define DT_RIM_CLICK 0.0f                    /* its stick's burst straight out (its crack) */
#define DT_RIM_LP 0.315f                     /* its output filter (dt_voice_t lp; 0: none) */
#define DT_RIM_LOWM -9                       /* its two lowest modes, dB more */
#define DT_RIM_HISS 3                        /* its crackle's two bands, dB */
#define DT_RIM_WIRE_G 0.4f                   /* the crackle's noise into them */
#define DT_RIM_WIRES 0.03f                   /* its decay, s in ROM time */
#define DT_RIM_BODY 0.4f                     /* its modes' decays against the recordings' */
#define DT_RIM_ROOM 0.1f                     /* its send into the room */
#define DT_RIM_RATE 24700.0f                 /* its ROM samples a second at the first step (its notches: steps 1-4
                                              * agree within 1 %) */
#define DT_BD_GAIN 0.8f                      /* the Bass's level */
#define DT_BD_HZ 32                          /* its pitch at rest at the first step, Hz in ROM time */
#define DT_BD_SW 2.66f                       /* its pitch's quick fall: the excess at the hit (dt_voice_t sweep) */
#define DT_BD_SW_T 0.014f                    /* and its time constant, s in ROM time */
#define DT_BD_SW2 0.07f                      /* the slower fall's excess */
#define DT_BD_SW2_T 0.06f                    /* and its time constant */
#define DT_BD_STICK 0.28f                    /* its beater's noise burst into the modes */
#define DT_BD_OVER 3                         /* its overtones, dB more */
#define DT_BD_THUD 200.0f                    /* its beater's thud's lowest mode, Hz (the others 2.17 and 4.33 times) */
#define DT_BD_MID -10                        /* its beater's thud's level, dB (three broad modes in the middle) */
#define DT_BD_CLICK 0.98f                    /* and straight out */
#define DT_BD_BODY 1.2f                      /* its modes' decays against the recordings' */
#define DT_BD_LEN 0.3f                       /* its sample's length, s (it stops about 110 ms in at the first step) */
#define DT_BD_VCF 2800.0f                    /* its VCF's open cutoff at the first step, Hz (follows the tuning) */
#define DT_BD_VCF_LO 0.0245f                 /* where its envelope closes it to, against open */
#define DT_BD_VCF_T 0.012f                   /* its envelope's decay (time constant, s) */
#define DT_BD_LP 0.49392f                    /* its output filter (dt_voice_t lp) */
#define DT_BD_ATTACK 0.003f                  /* its swell at the start (dt_voice_t attack), s */
#define DT_BD_GATE 0.098f                    /* its cliff, s in ROM time (dt_voice_t gate) */
#define DT_BD_GATE_DB 26.0f                  /* how far it falls, dB */
#define DT_BD_ROOM 0.2f                      /* its send into the room */
#define DT_BD_POST 4.0f                      /* +12 dB out: its filters leave it 10 dB under the others' peaks, 14 dB above 120 Hz */
#define DT_BD_RATE 17400.0f                  /* its ROM samples a second (its VCF hides the notches: a guess) */
#define DT_PERC_RATE 11100.0f                /* channel 6's ROM samples a second at the first step (the Claps' notches:
                                              * 11.0 kHz, steps 1-4; the Cabasa's 11.4) */
#define DT_CB_GAIN 0.7f                      /* the Cowbell's level */
#define DT_CB_STICK 0.05f                    /* its stick's noise burst into the modes */
#define DT_CB_BODY 2.8f                      /* its modes' decays against the recordings' */
#define DT_CB_VCF 8232.0f                    /* its VCF's open cutoff at the first step, Hz (channel 6's: as 1's) */
#define DT_CB_VCF_LO 0.21f                   /* where its envelope closes it to, against open */
#define DT_CB_VCF_T 0.07f                    /* its envelope's decay (time constant, s) */
#define DT_CB_HOLD 0.0343f                   /* its hold, s (ROM time): then it falls (dt_voice_t gate) */
#define DT_CB_FALL 0.0784f                   /* with this time constant, s */
#define DT_CB_LP 0.22f                       /* its output filter (dt_voice_t lp; 0: none) */
#define DT_CB_HI 4                           /* its upper partials (587 Hz up), dB more */
#define DT_CB_CLANG -6                       /* its clang's two bands, dB */
#define DT_CB_WIRE_G 0.1f                    /* the clang's noise into them */
#define DT_CB_WIRES 0.028f                   /* and its decay, s in ROM time */
#define DT_CB_ATTACK 0.0028f                 /* its swell at the start, s (dt_voice_t attack) */
#define DT_CB_ROOM 0.07f                     /* its send into the room */
#define DT_CP_GAIN 0.6f                      /* the Claps' level */
#define DT_CP_WIRE_G 0.98f                   /* their noise into their bands */
#define DT_CP_NOISE 0.14f                    /* and straight out */
#define DT_CP_BURSTS 6                       /* the hands: strikes */
#define DT_CP_GAP 0.0238f                    /* s apart (ROM time) */
#define DT_CP_BURST_T 0.02352f               /* the noise's decay between them, s */
#define DT_CP_BURST_DB 2.8f                  /* each strike under the one before, dB */
#define DT_CP_JIT 0.4f                       /* each gap's random spread, 0..1 */
#define DT_CP_TAIL 0.0392f                   /* and after the last, s */
#define DT_CP_RISE 0.0056f                   /* their noise's rise, s (dt_voice_t wires_rise) */
#define DT_CP_HI 3                           /* their high-mid bands (1.4, 2 kHz), dB more */
#define DT_CP_TAIL_G 0.72f                   /* their hiss (the upper bands' noise): its level, */
#define DT_CP_TAIL_T 0.06f                   /* its decay, s in ROM time, */
#define DT_CP_TAIL_R 0.04f                   /* and its swell */
#define DT_CP_Q 0.002f                       /* their bands' decay, s: their width */
#define DT_CP_VCF 5600.0f                    /* their VCF's open cutoff at the first step, Hz */
#define DT_CP_VCF_LO 0.7f                    /* where its envelope closes it to */
#define DT_CP_VCF_T 0.1f                     /* its envelope's decay, s */
#define DT_CP_LP 0.36f                       /* their output filter (dt_voice_t lp; 0: none) */
#define DT_CP_HP 420.0f                      /* their output high pass at the first step, Hz (dt_voice_t hp) */
#define DT_CP_ROOM 0.3f                      /* their send into the room */
#define DT_HH_RATE 17400.0f                  /* the hi-hats' ROM samples a second at the first step (their notches:
                                              * 17.5 and 35 kHz at the first step, moving with the rate) */
#define DT_HH_GAIN 0.5f                      /* the open hi-hat's level */
#define DT_HH_LEN 0.335f                     /* its sample's length, s (ROM time): it ends there, at full level */
#define DT_HH_Q 0.00035f                     /* its noise bands' decay, s: their width */
#define DT_HH_RING 0.01225f                  /* its metal's tones' decay, s: their width (longer: they ring) */
#define DT_HH_TONE -6                        /* the metal's tones, dB */
#define DT_HH_LOW -12                        /* its bands under 2 kHz, dB */
#define DT_HH_STICK 0.0f                     /* the stick's noise burst into the bands */
#define DT_HH_VCF 12600.0f                   /* their filter after the DAC, two poles, Hz at the first step (0: none) */
#define DT_HH_VCF_FIXED 1                    /* fixed, not moving with the rate */
#define DT_HH_HP 588.0f                      /* their output high pass at the first step, Hz (dt_voice_t hp) */
#define DT_HH_LP 0.0f                        /* their output filter (dt_voice_t lp; 0: none) */
#define DT_HH_ROOM 0.0f                      /* its send into the room (the 400's own was in the sample) */
#define DT_CHH_GAIN 0.5f                     /* the closed hi-hat's level: the same sample, */
#define DT_CHH_VCA 0.049f                    /* through a VCA falling with this time constant, s (real time: the same
                                              * at every tuning), */
#define DT_CHH_HOLD 0.014f                   /* after a hold, s, */
#define DT_CHH_END 0.0819f                   /* and cut here, s */
#define DT_CR_RATE 10700.0f                  /* the Crash's ROM samples a second at the first step (its notches) */
#define DT_CR_PITCH 1.0558f                  /* its partials tuned by ear (dt_voice_t pitch) */
#define DT_CR_GAIN 0.5f                      /* its level */
#define DT_CR_STICK 0.6f                     /* the stick's noise burst into its partials */
#define DT_CR_SWELL 0.0f                     /* its partials' swell, s (dt_voice_t attack: a crash blooms) */
#define DT_CR_LOW 1.0f                       /* its low partials' (140-410 Hz) decays against the recordings' */
#define DT_CR_MID 0.008f                     /* its mid partials' (0.8-2.5 kHz) decay, s: their width (they ring in
                                              * the wash's noise, a crash's shimmer: struck, they rang a bell) */
#define DT_CR_LOMID -7                       /* its lower mids (0.8-1.4 kHz), dB */
#define DT_CR_HI 4                           /* its wash's lower bands (2.9-4 kHz), dB: its brightness */
#define DT_CR_WASH 0.3f                      /* its wash (the tail's noise in broad upper bands): level, */
#define DT_CR_WASH_T 1.2f                    /* decay, s, */
#define DT_CR_WASH_R 0.02f                   /* and swell, s */
#define DT_CR_Q 0.001f                       /* the wash's bands' decay, s: their width */
#define DT_CR_GATE 2.5f                      /* where its sample fades out, s (ROM time: dt_voice_t gate), */
#define DT_CR_GATE_T 0.25f                   /* how fast */
#define DT_CR_ROOM 0.1f                      /* its send into the room */
#define DT_RD_RATE 11600.0f                  /* the Ride's ROM samples a second at the first step (its notches) */
#define DT_RD_PITCH 1.0035f                  /* its partials tuned by ear (dt_voice_t pitch) */
#define DT_RD_GAIN 0.5f                      /* its level */
#define DT_RD_STICK 0.49f                    /* the stick's noise burst: its ping */
#define DT_RD_LOW -3                         /* its low partials (170-360 Hz), dB */
#define DT_RD_BELL -3                        /* its bell's partials (400-480 Hz), dB, */
#define DT_RD_BELL_T 1.12f                   /* their decays against the others' */
#define DT_RD_HI -9                          /* its upper mids (2.4-3 kHz), dB: its brightness */
#define DT_RD_BODY 0.63f                     /* its partials' decays against the recordings' */
#define DT_RD_WASH 1.12f                     /* its wash: level, */
#define DT_RD_WASH_T 0.7f                    /* decay, s */
#define DT_RD_GATE 2.4f                      /* where its sample fades out, s (ROM time), */
#define DT_RD_GATE_T 0.4116f                 /* how fast */
#define DT_RD_ROOM 0.1f                      /* its send into the room */
#define DT_TB_GAIN 0.5f                      /* the Tambourine's level */
#define DT_TB_STICK 0.3f                     /* the hand's noise burst into the jingles */
#define DT_TB_BODY 1.0f                      /* the jingles' decays against the recordings' */
#define DT_TB_FRAME 0                        /* the frame's thump, dB (three broad modes, 220-750 Hz) */
#define DT_TB_HI 2                           /* the upper jingles (3.9-6 kHz), dB more */
#define DT_TB_RING 0.1f                      /* the main jingles' (3.4, 3.7 kHz) ring, s: the tail's tone */
#define DT_TB_COPIES 6                       /* the jingle pairs: each its own (dt_voice_t copies) */
#define DT_TB_DETUNE 0.03f                   /* their spread in pitch, a ratio either way */
#define DT_TB_CGAP 0.003f                    /* and in time, s (ROM time) between strikes */
#define DT_TB_RATTLE 3                       /* the rattle's bands, dB */
#define DT_TB_WIRE_G 0.84f                   /* the rattle's noise into them */
#define DT_TB_BURSTS 8                       /* the jingles struck again: times */
#define DT_TB_GAP 0.011f                     /* s apart (ROM time) */
#define DT_TB_BURST_T 0.006f                 /* the noise's decay between, s */
#define DT_TB_BURST_DB 2.0f                  /* each strike under the one before, dB */
#define DT_TB_JIT 0.35f                      /* each gap's random spread */
#define DT_TB_TAIL 0.098f                    /* the rattle's decay after the last, s */
#define DT_TB_VCF 6000.0f                    /* channel 6's VCF, fixed, Hz (dt_voice_t vcf_fixed) */
#define DT_TB_HP 600.0f                      /* the output high pass at the first step, Hz */
#define DT_TB_ATTACK 0.00588f                /* the jingles' swell at the start, s (dt_voice_t attack) */
#define DT_TB_ROOM 0.04f                     /* its send into the room */
#define DT_CA_GAIN 0.6f                      /* the Cabasa's level */
#define DT_CA_TAIL_G 1.4f                    /* its beads' noise (the tail's: dt_voice_t tail): level, */
#define DT_CA_TAIL_T 0.1f                    /* decay, s in ROM time, */
#define DT_CA_TAIL_R 0.049f                  /* and swell */
#define DT_CA_Q 0.00112f                     /* its bands' decay, s: their width */
#define DT_CA_VCF 9000.0f                    /* channel 6's VCF, fixed, Hz */
#define DT_CA_HP 1260.0f                     /* the output high pass at the first step, Hz */
#define DT_CA_GATE 0.095f                    /* the shake's end, s (ROM time): its cliff (dt_voice_t gate) */
#define DT_CA_GATE_DB 32.0f                  /* how far it falls, dB */
#define DT_CA_GATE_T 0.01176f                /* how fast, s */
#define DT_CA_ROOM 0.105f                    /* its send into the room */
#define DT_SN_GAIN 0.6f                      /* the Snare's level */
#define DT_SN_STICK 0.392f                   /* the stick's noise burst into the shell's resonators */
#define DT_SN_WIRES 0.08f                    /* the wires' decay, seconds in ROM time */
#define DT_SN_WIRE_G 0.55f                   /* the wires' noise into their bands */
#define DT_SN_NOISE 0.15f                    /* the wires' noise straight out */
#define DT_TOM_CLICK 4.0f                    /* the Toms' stick crack, straight out (dt_voice_t click) */
#define DT_TOM_MID 5                         /* the Toms' bloom's level, dB (three broad modes in the middle) */
#define DT_TOM_LOW 0.8f                      /* the Toms' body longer by this at the first step (dt_voice_t low) */
#define DT_TOM_LOW_DB 8.0f                   /* the Toms' body louder by this at the first step (dt_voice_t low_db) */
#define DT_TOM_BODY 0.665f                   /* the Toms' modes' decays against the recordings' (the room the rest) */
#define DT_TOM_ROOM 0.35f                    /* the Toms' send into the room */
#define DT_SN_ROOM 0.06f                     /* the Snare's */
#define DT_SN_LOW 0.0f                       /* the Snare's body longer by this at the first step (dt_voice_t low) */
#define DT_SN_LOW_DB 4.2f                    /* its body louder by this at the first step (dt_voice_t low_db: the
                                              * hit not as far over it at the low tunings) */
#define DT_SN_BODY 1.0f                      /* the Snare's modes' decays against the recordings' */
#define DT_SN_SMACK -16                      /* the smack's level, dB (two broad modes, 300 and 420 Hz) */
#define DT_SN_WIRE_RISE 0.0056f              /* the wires' noise's rise (s): they buzz once the head has moved */
#define DT_SN_WIRE_LO -6                     /* the wires' three lowest bands, dB more (their low-mid weight) */
#define DT_SN_WIRE_Q 0.0006f                 /* the wires' bands' decay (s): their width (it scales with the
                                              * rate: narrower, they ring metal at the low tunings) */
#define DT_SN_WIRE_HI 0                      /* the wires' upper bands (1.7-6.5 kHz), dB more */
#define DT_SN_NHI 3                          /* how many of them (0..6: the measured three, then three above) */
#define DT_SN_LP 0.8f                        /* the Snare's output filter (dt_voice_t lp) */

static const char *const DT_NAME[NDT] = {"BASS", "SNARE", "RIM", "TOM1", "TOM2", "CRASH", "RIDE", "CHH", "OHH",
                                         "CLAPS", "TAMB", "COWBELL", "CABASA"};
static const uint8_t DT_CH[NDT] = {DTC_BASS, DTC_SNARE, DTC_SNARE, DTC_TOMS, DTC_TOMS, DTC_CYM, DTC_CYM, DTC_HATS,
                                   DTC_HATS, DTC_PERC, DTC_PERC, DTC_PERC, DTC_PERC};
static const uint16_t DT_RATE_COWBELL[16] = {4161, 4350, 5127, 5698, 6311, 6981, 7738, 8539, 9427, 10417, 11495,
                                             12703, 14044, 15516, 16884, 18126};   /* (dt_compare.py --pitch) */
static const uint16_t DT_RATE_CLAPS[16] = {4096, 4215, 4264, 4428, 4858, 5280, 5710, 6136, 6578, 7111, 7557, 8098,
                                           8679, 9249, 9941, 10592};   /* (their decays': the spectra alias above 12) */
static const uint16_t DT_RATE_TAMB[16] = {4096, 4216, 4277, 4435, 4836, 5274, 5710, 6137, 6597, 7090, 7567, 8075,
                                          8679, 9262, 9884, 10624};
static const uint16_t DT_RATE_CABASA[16] = {4096, 4226, 4278, 4446, 4866, 5298, 5733, 6179, 6648, 7180, 7660, 8192,
                                            8772, 9375, 10015, 10703};
/* each tuning step's rate, Q12, 4096 the modes' own (docs/DT_MEASURE.md's spectrum shifts; the Snare's first
 * step set by its pitch, 92 Hz: its shift read it 5 % low) */
static const uint16_t DT_RATE_TOM[16] = {4161, 4327, 4924, 5831, 6512, 7304, 8186, 8946, 10149, 10997, 12238, 13511,
                                         15080, 16437, 18123, 19777};   /* (dt_compare.py --pitch) */
static const uint16_t DT_RATE_SNARE[16] = {4419, 5998, 6541, 7030, 7611, 8062, 8359, 8732, 9183, 9384, 9797,
                                           10156, 10457, 10457, 10842, 11322};   /* (35 cents up: picked by ear) */
static const uint16_t DT_RATE_RIM[16] = {4096, 4477, 4791, 4998, 5200, 5430, 5609, 5839, 6052, 6365, 6528, 6774,
                                         7043, 7323, 7620, 8282};
static const uint16_t DT_RATE_BASS[16] = {4211, 5517, 6405, 6648, 6920, 7195, 7504, 7804, 8122, 8479, 8726, 8726,
                                          9053, 9473, 9884, 10263};   /* (tools/dt_voice.py --cycles) */
static const uint16_t DT_RATE_HAT[16] = {4096, 4467, 4801, 5014, 5198, 5428, 5628, 5835, 6093, 6363, 6549, 6839,
                                         7090, 7351, 7621, 7959};   /* (both hats: one ROM, as the 400's) */
static const uint16_t DT_RATE_CRASH[16] = {4096, 4371, 4801, 5793, 6790, 7351, 7902, 8493, 9195, 9813, 11337, 13098,
                                           13098, 15133, 17484, 18658};   /* (steps 12 and 13 alike: the 400's) */
static const uint16_t DT_RATE_RIDE[16] = {4096, 4871, 5793, 6226, 6790, 7246, 7788, 8432, 9063, 9672, 10471, 11255,
                                          12453, 13877, 16033, 19344};
/* the MIDI key map (the 400's, 17 Dec 1983): notes 35..58, DT_NONE where nothing plays */
static const uint8_t DT_KEY[24] = {DT_BASS, DT_BASS, DT_RIM, DT_SNARE, DT_CLAPS, DT_SNARE, DT_TOM1, DT_CHH, DT_TOM1,
                                   DT_CHH, DT_TOM2, DT_OHH, DT_TOM2, DT_NONE, DT_CRASH, DT_NONE, DT_RIDE, DT_NONE,
                                   DT_NONE, DT_TAMB, DT_NONE, DT_COWBELL, DT_NONE, DT_CABASA};

/* A voice is a bank of resonators, each a drum's mode: in ROM time its frequency, its level (dB under the
 * strongest), its decay (time constant, s) and its flags: struck by the stick (a click and a burst of noise), or
 * DT_MW driven by the wires' noise while it lasts, or DT_MT by the tail's; following the voice's pitch fall or,
 * DT_MF, not (a beater's thud is the beater's, not the head's); DT_MC, played as the voice's copies (a
 * tambourine's jingles: each pair a little apart in pitch and in time, ringing together). The modes are the
 * recordings' strongest tones, divided by each step's rate (bs2/tools/dt_measure.py's tones(): they hold still
 * across the steps, as a ROM's would). The noise is new each hit: no two alike. */
typedef struct {
    float hz, db, tau;
    uint8_t flags;                           /* DT_MW, DT_MF */
} dt_mode_t;
#define DT_MW 1u                             /* a wire: driven by the wires' noise, not struck */
#define DT_MF 2u                             /* fixed: not following the pitch's fall */
#define DT_MT 4u                             /* driven by the tail's noise (its own swell and decay), not struck */
#define DT_MC 8u                             /* a jingle's: played as the voice's copies, each its own jingle */
#ifdef PLAT_HOST                             /* (the live voices: made on the host, tools/dt_eprom.py; the FM-1 reads their EPROMs) */
static const dt_mode_t DT_MODES_TOM[] = {    /* the head's modes at rest; all start a third higher (DT_VOICE) */
    {62, 3, 0.35f, 0}, {90, -30, 0.30f, 0}, {101, -24, 0.25f, 0}, {124, -18, 0.28f, 0}, {137, -22, 0.18f, 0},
    {152, -33, 0.168f, 0}, {174, -21, 0.14f, 0}, {193, -26, 0.20f, 0}, {239, -25, 0.18f, 0}, {268, -21, 0.15f, 0},
    {393, -12, 0.056f, 0},
    {450, DT_TOM_MID, 0.003f, 0}, {900, DT_TOM_MID - 2, 0.002f, 0}, {1600, DT_TOM_MID - 4, 0.0015f, 0}};   /* the
                                              * shell's bloom as the stick lands: broad, short, the middle */
static const dt_mode_t DT_MODES_SNARE[] = {  /* the shell's cluster, its smack (broad), then the wires' bands (broad:
                                              * they hiss; narrow, they ring like a pipe on a plate) */
    {86, -12, 0.0455f, 0}, {90, 3, 0.060f, 0}, {102, -16, 0.042f, 0}, {111, -16, 0.065f, 0}, {134, -14, 0.050f, 0},
    {171, -19, 0.060f, 0}, {187, -15, 0.075f, 0}, {300, DT_SN_SMACK, 0.005f, 0}, {420, DT_SN_SMACK, 0.004f, 0},
    {622, -21 + DT_SN_WIRE_LO, DT_SN_WIRE_Q, DT_MW}, {757, -24 + DT_SN_WIRE_LO, DT_SN_WIRE_Q, DT_MW},
    {900, -23 + DT_SN_WIRE_LO, DT_SN_WIRE_Q, DT_MW},
    {1095, -21, DT_SN_WIRE_Q, DT_MW}, {1420, -21, DT_SN_WIRE_Q, DT_MW}, {2035, -22, DT_SN_WIRE_Q, DT_MW},
    {2870, -21, DT_SN_WIRE_Q, DT_MW},
    {1726, -22 + DT_SN_WIRE_HI, DT_SN_WIRE_Q, DT_MW}, {2507, -23 + DT_SN_WIRE_HI, DT_SN_WIRE_Q, DT_MW},
    {3245, -23 + DT_SN_WIRE_HI, DT_SN_WIRE_Q, DT_MW}, {4100, -24 + DT_SN_WIRE_HI, DT_SN_WIRE_Q, DT_MW},
    {5200, -25 + DT_SN_WIRE_HI, DT_SN_WIRE_Q, DT_MW}, {6500, -26 + DT_SN_WIRE_HI, DT_SN_WIRE_Q, DT_MW}};
static const dt_mode_t DT_MODES_RIM[] = {    /* the stick on the rim: a bright woody cluster, short; its crackle */
    {1026, 0, 0.0294f, 0}, {1172, 0, 0.028f, 0}, {835, -6, 0.025f, 0}, {1892, -3, 0.030f, 0}, {2732, -3, 0.0308f, 0},
    {1197, -11, 0.047f, 0}, {983, -3, 0.043f, 0}, {691, -6, 0.0392f, 0}, {394, -3, 0.02254f, 0}, {523, -7, 0.017f, 0},
    {196, -7 + DT_RIM_LOWM, 0.043f, 0}, {136, -7 + DT_RIM_LOWM, 0.066f, 0}, {575, -2, 0.0238f, 0},
    {3600, DT_RIM_HISS, DT_SN_WIRE_Q, DT_MW}, {5200, DT_RIM_HISS, DT_SN_WIRE_Q, DT_MW}};   /* its crackle */
static const dt_mode_t DT_MODES_BASS[] = {   /* the kick at rest (it starts nearly three times higher: DT_BD_SW),
                                              * a few weak overtones */
    {DT_BD_HZ, 0, 0.12f, 0}, {66, -19 + DT_BD_OVER, 0.03f, 0}, {76, -26 + DT_BD_OVER, 0.04f, 0},
    {95, -26 + DT_BD_OVER, 0.04f, 0}, {100, -29 + DT_BD_OVER, 0.04f, 0},
    {108, -29 + DT_BD_OVER, 0.04f, 0},
    {DT_BD_THUD, DT_BD_MID, 0.004f, DT_MF}, {DT_BD_THUD * 2.17f, DT_BD_MID - 4, 0.003f, DT_MF},
    {DT_BD_THUD * 4.33f, DT_BD_MID - 8, 0.002f, DT_MF}};   /* the
                                              * beater's thud: broad, short, the middle */
static const dt_mode_t DT_MODES_COWBELL[] = {   /* a bell's partials (the upper ones raised by DT_CB_HI); its clang */
    {224, 0, 0.0294f, 0}, {230, -19, 0.044f, 0}, {482, -19, 0.0658f, 0}, {587, -38 + DT_CB_HI, 0.033f, 0},
    {735, -31 + DT_CB_HI, 0.022f, 0}, {967, -32 + DT_CB_HI, 0.043f, 0}, {1060, -26 + DT_CB_HI, 0.055f, 0},
    {1238, -36 + DT_CB_HI, 0.067f, 0}, {1411, -28 + DT_CB_HI, 0.041f, 0}, {1752, -36 + DT_CB_HI, 0.044f, 0},
    {2500, DT_CB_CLANG, DT_SN_WIRE_Q, DT_MW}, {4000, DT_CB_CLANG, DT_SN_WIRE_Q, DT_MW}};   /* its clang: noise */
static const dt_mode_t DT_MODES_CLAPS[] = {  /* hands: noise in a broad band, 350 Hz to 3 kHz; the upper the hiss */
    {400, 0, DT_CP_Q, DT_MW}, {500, 0, DT_CP_Q, DT_MW}, {620, 0, DT_CP_Q, DT_MW}, {760, 0, DT_CP_Q, DT_MW},
    {900, 0, DT_CP_Q, DT_MW}, {1050, 0, DT_CP_Q, DT_MW}, {1400, -4 + DT_CP_HI, DT_CP_Q, DT_MT},
    {2000, -8 + DT_CP_HI, DT_CP_Q, DT_MT}, {2800, -10 + DT_CP_HI, DT_CP_Q, DT_MT}};   /* (the upper: the tail's) */
static const dt_mode_t DT_MODES_TAMB[] = {   /* the frame's thump (broad, fixed); the jingles: metal tones, the
                                              * main ones played as DT_TB_COPIES pairs; their rattle (noise, struck
                                              * again and again) */
    {220, DT_TB_FRAME, 0.006f, DT_MF}, {420, DT_TB_FRAME + 2, 0.005f, DT_MF}, {750, DT_TB_FRAME, 0.004f, DT_MF},
    {2037, -12, 0.040f, 0}, {2439, -9, 0.050f, DT_MC}, {3435, 0, DT_TB_RING, DT_MC}, {3714, -1, DT_TB_RING, DT_MC},
    {3916, -8 + DT_TB_HI, 0.072f, DT_MC}, {5029, -5 + DT_TB_HI, 0.062f, DT_MC}, {6033, -9 + DT_TB_HI, 0.078f, DT_MC},
    {2300, DT_TB_RATTLE, DT_SN_WIRE_Q, DT_MW}, {3500, DT_TB_RATTLE, DT_SN_WIRE_Q, DT_MW},
    {5000, DT_TB_RATTLE, DT_SN_WIRE_Q, DT_MW}, {7000, DT_TB_RATTLE - 3, DT_SN_WIRE_Q, DT_MW}};
static const dt_mode_t DT_MODES_HAT[] = {    /* a hi-hat's sizzle: noise in broad bands, 1.2 to 8.3 kHz, and the
                                              * metal's tones in it; held, the open hat's sample a steady wash */
    {1250, DT_HH_LOW, DT_HH_Q, DT_MT}, {1770, DT_HH_LOW + 3, DT_HH_Q, DT_MT}, {2150, DT_HH_LOW + 6, DT_HH_Q, DT_MT},
    {2600, -4, DT_HH_Q, DT_MT}, {3200, 0, DT_HH_Q, DT_MT}, {3900, -1, DT_HH_Q, DT_MT}, {4700, -2, DT_HH_Q, DT_MT},
    {5500, -2, DT_HH_Q, DT_MT}, {6400, -1, DT_HH_Q, DT_MT}, {7400, -2, DT_HH_Q, DT_MT}, {8300, -6, DT_HH_Q, DT_MT},
    {2342, DT_HH_TONE, DT_HH_RING, DT_MT}, {2574, DT_HH_TONE, DT_HH_RING, DT_MT},
    {3222, DT_HH_TONE + 3, DT_HH_RING, DT_MT}, {3972, DT_HH_TONE + 2, DT_HH_RING, DT_MT},
    {5105, DT_HH_TONE, DT_HH_RING, DT_MT}, {5877, DT_HH_TONE, DT_HH_RING, DT_MT}};
static const dt_mode_t DT_MODES_CRASH[] = {  /* low partials first, gone soon; the mids carry it; a wash over them */
    {143, -6, 0.41f * DT_CR_LOW, 0}, {155, -6, 0.40f * DT_CR_LOW, 0}, {183, -10, 0.30f * DT_CR_LOW, 0},
    {220, 0, 1.10f * DT_CR_LOW, 0}, {240, -4, 1.10f * DT_CR_LOW, 0}, {251, -4, 0.57f * DT_CR_LOW, 0},
    {274, -1, 0.73f * DT_CR_LOW, 0}, {305, -4, 0.88f * DT_CR_LOW, 0}, {320, -8, 0.80f * DT_CR_LOW, 0},
    {360, -8, 0.70f * DT_CR_LOW, 0}, {388, -9, 1.40f * DT_CR_LOW, 0}, {411, -10, 0.97f * DT_CR_LOW, 0},
    {778, -8 + DT_CR_LOMID, DT_CR_MID, DT_MT}, {1045, -5 + DT_CR_LOMID, DT_CR_MID, DT_MT},
    {1197, -8 + DT_CR_LOMID, DT_CR_MID, DT_MT}, {1370, -6 + DT_CR_LOMID, DT_CR_MID, DT_MT},
    {1514, -7, DT_CR_MID, DT_MT}, {1591, -2, DT_CR_MID, DT_MT}, {1693, -5, DT_CR_MID, DT_MT},
    {1803, -7, DT_CR_MID, DT_MT}, {2017, -2, DT_CR_MID, DT_MT}, {2163, -6, DT_CR_MID, DT_MT},
    {2277, -4, DT_CR_MID, DT_MT}, {2538, -8, DT_CR_MID, DT_MT}, {2900, -6 + DT_CR_HI, DT_CR_Q, DT_MT},
    {3400, -8 + DT_CR_HI, DT_CR_Q, DT_MT}, {3950, -10 + DT_CR_HI, DT_CR_Q, DT_MT}, {4500, -12, DT_CR_Q, DT_MT},
    {5000, -12, DT_CR_Q, DT_MT}};
static const dt_mode_t DT_MODES_RIDE[] = {   /* the bell's and the bow's partials; above 3 kHz gone first */
    {176, -12 + DT_RD_LOW, 1.5f, 0}, {230, DT_RD_LOW, 1.33f, 0}, {275, -11 + DT_RD_LOW, 2.0f, 0},
    {299, -8 + DT_RD_LOW, 2.0f, 0}, {314, -6 + DT_RD_LOW, 1.57f, 0}, {362, -10 + DT_RD_LOW, 2.0f, 0},
    {403, -13 + DT_RD_BELL, 2.0f * DT_RD_BELL_T, 0}, {426, -5 + DT_RD_BELL, 2.0f * DT_RD_BELL_T, 0},
    {452, -9 + DT_RD_BELL, 1.8f * DT_RD_BELL_T, 0}, {478, -11 + DT_RD_BELL, 1.7f * DT_RD_BELL_T, 0},
    {628, -15, 2.8f, 0}, {723, -13, 1.0f, 0}, {793, -13, 2.8f, 0}, {1165, -12, 2.0f, 0}, {1287, -4, 1.3f, 0},
    {1618, -9, 1.68f, 0}, {1790, -6, 0.49f, 0}, {2043, -9, 1.82f, 0}, {2249, -8, 1.26f, 0},
    {2387, -7 + DT_RD_HI, 0.48f, 0}, {2567, -3 + DT_RD_HI, 0.8f, 0}, {3015, -10, 0.48f, 0}, {3172, -15, 0.8f, 0},
    {3335, -9, 0.89f, 0}, {3663, -13, 0.8f, 0}, {3871, -5, 0.75f, 0}, {3905, -10, 0.61f, 0}, {4318, -2, 0.2989f, 0},
    {3000, -6 + DT_RD_HI, DT_CR_Q, DT_MT}, {4000, -6, DT_CR_Q, DT_MT}, {5000, -8, DT_CR_Q, DT_MT}};
static const dt_mode_t DT_MODES_CABASA[] = { /* beads on steel: noise in a broad band, 1.3 to 4.2 kHz, swelling */
    {1300, -3, DT_CA_Q, DT_MT}, {1620, 0, DT_CA_Q, DT_MT}, {2030, -3, DT_CA_Q, DT_MT}, {2500, -3, DT_CA_Q, DT_MT},
    {3180, -5, DT_CA_Q, DT_MT}, {3700, -5, DT_CA_Q, DT_MT}, {4200, -6, DT_CA_Q, DT_MT}};
#else
#define DT_MODES_TOM 0
#define DT_MODES_SNARE 0
#define DT_MODES_RIM 0
#define DT_MODES_BASS 0
#define DT_MODES_COWBELL 0
#define DT_MODES_CLAPS 0
#define DT_MODES_TAMB 0
#define DT_MODES_HAT 0
#define DT_MODES_CRASH 0
#define DT_MODES_RIDE 0
#define DT_MODES_CABASA 0
#endif
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
    float tail, tail_tau, tail_rise;         /* the tail's noise (DT_MT modes): its level, decay and swell (s): what
                                              * builds and lingers after the strikes (a clap's hiss opening up) */
    uint8_t bursts;                          /* the wires' noise struck this many times (0, 1: once), */
    float burst_gap, burst_tau;              /* gap s apart (ROM time), decaying with burst_tau between (a clap's
                                              * hands); after the last, with wires_tau */
    float burst_db;                          /* each strike this many dB under the one before (the hands tire) */
    float burst_jit;                         /* each gap's random spread, 0..1 of it (hands are not a metronome:
                                              * evenly spaced, the strikes read as a slapback) */
    float pitch;                             /* the modes' frequencies times this (0: as listed): tuned by ear, the
                                              * rate and so the length left alone */
    float body, low;                         /* the modes' decays' scale; longer by low at the first step, fading out
                                              * by the fourth (the 400's lowest tunings are their own: the Snare's
                                              * first step is off its others' pitch pattern too) */
    float low_db;                            /* the body's overtones (the modes after the first, under 300 Hz, not
                                              * wires or fixed) this many dB louder at the first step, fading out by
                                              * the fourth: more body in the attack */
    float vcf, vcf_lo, vcf_t;                /* the VCF after the DAC (channels 1 and 3, TN400-0 pg. 7): its cutoff
                                              * open at the first step, Hz, moving with the rate (0: none); its own
                                              * envelope closing it to vcf_lo of that, with time constant vcf_t (s) */
    uint8_t vcf_fixed;                       /* its cutoff not moving with the rate (channel 6's: TN400-0 pg. 3, only
                                              * channels 1 and 3 take the tuning CV to their VCF) */
    float lp;                                /* the output low pass (fixed: not in ROM time), a one-pole's step at
                                              * BS2_RATE (1 - e^(-2 pi fc / rate)); 0: none */
    float hp;                                /* the output high pass's cutoff at the first step, Hz, moving with the
                                              * rate (a fixed one would cut each step's sound differently); 0: none */
    float attack;                            /* the modes' swell at the start, s in ROM time (a kick's head pushed
                                              * out over its first cycle; the click is not swelled); 0: at once */
    float gate, gate_db, gate_t;             /* at gate s (ROM time) the level falls by gate_db, with time constant
                                              * gate_t s (0: about 2 ms), and plays on there (a VCA pulled down: the
                                              * Bass's cliff, the closed hi-hat's; a held bell's fall); 0: none */
    float vca_t, vca_hold, vca_end;          /* a VCA after the DAC, in real time (the same at every tuning): held
                                              * vca_hold s, then falling with time constant vca_t s, cut at vca_end s
                                              * (the closed hi-hat: the open one's sample, choked); 0: none */
    uint8_t copies;                          /* the DT_MC modes played this many times (2..DT_NCOPY; 0, 1: once), */
    float copy_detune, copy_gap;             /* each copy detuned across +-copy_detune (a ratio) and struck about
                                              * copy_gap s (ROM time) after the one before, at random around both */
    float room;                              /* the send into the room */
    float post;                              /* its level after the VCF and filters (0: as it is) */
} dt_voice_t;
static const dt_voice_t DT_V_SNARE = {
    .rate = DT_RATE_SNARE, .m = DT_MODES_SNARE, .n = 16 + DT_SN_NHI, .rom = DT_SN_RATE, .len = 0.40f,
    .gain = DT_SN_GAIN, .stick = DT_SN_STICK, .wires = DT_SN_WIRE_G, .wires_tau = DT_SN_WIRES,
    .wires_rise = DT_SN_WIRE_RISE, .noise = DT_SN_NOISE, .body = DT_SN_BODY, .low = DT_SN_LOW, .lp = DT_SN_LP,
    .low_db = DT_SN_LOW_DB, .room = DT_SN_ROOM};
static const dt_voice_t DT_V_TOM = {         /* (both Toms: one ROM, as the 400's) */
    .rate = DT_RATE_TOM, .m = DT_MODES_TOM, .n = 14, .rom = DT_TOM_RATE, .len = 1.33f, .gain = DT_TOM_GAIN,
    .sweep = DT_TOM_SW, .sweep_tau = DT_TOM_SW_T, .sweep2 = 0.20f, .sweep2_tau = 0.33f, .stick = DT_TOM_STICK,
    .click = DT_TOM_CLICK, .body = DT_TOM_BODY, .low = DT_TOM_LOW,
    .low_db = DT_TOM_LOW_DB, .vcf = DT_TOM_VCF, .vcf_lo = DT_TOM_VCF_LO,
    .vcf_t = DT_TOM_VCF_T, .lp = DT_TOM_LP, .room = DT_TOM_ROOM};
static const dt_voice_t DT_V_RIM = {
    .rate = DT_RATE_RIM, .m = DT_MODES_RIM, .n = 15, .rom = DT_RIM_RATE, .len = 0.42f, .gain = DT_RIM_GAIN,
    .stick = DT_RIM_STICK, .click = DT_RIM_CLICK, .wires = DT_RIM_WIRE_G, .wires_tau = DT_RIM_WIRES,
    .body = DT_RIM_BODY, .lp = DT_RIM_LP, .room = DT_RIM_ROOM};
static const dt_voice_t DT_V_BASS = {
    .rate = DT_RATE_BASS, .m = DT_MODES_BASS, .n = 9, .rom = DT_BD_RATE, .len = DT_BD_LEN, .gain = DT_BD_GAIN,
    .sweep = DT_BD_SW, .sweep_tau = DT_BD_SW_T, .sweep2 = DT_BD_SW2, .sweep2_tau = DT_BD_SW2_T, .stick = DT_BD_STICK,
    .click = DT_BD_CLICK, .body = DT_BD_BODY, .vcf = DT_BD_VCF, .vcf_lo = DT_BD_VCF_LO, .vcf_t = DT_BD_VCF_T,
    .lp = DT_BD_LP, .attack = DT_BD_ATTACK, .gate = DT_BD_GATE, .gate_db = DT_BD_GATE_DB, .room = DT_BD_ROOM,
    .post = DT_BD_POST};
static const dt_voice_t DT_V_COWBELL = {
    .rate = DT_RATE_COWBELL, .m = DT_MODES_COWBELL, .n = 12, .rom = DT_PERC_RATE, .len = 0.36f, .gain = DT_CB_GAIN,
    .stick = DT_CB_STICK, .wires = DT_CB_WIRE_G, .wires_tau = DT_CB_WIRES, .body = DT_CB_BODY, .vcf = DT_CB_VCF,
    .vcf_lo = DT_CB_VCF_LO, .vcf_t = DT_CB_VCF_T,
    .gate = DT_CB_HOLD, .gate_db = 40.0f, .gate_t = DT_CB_FALL, .attack = DT_CB_ATTACK, .lp = DT_CB_LP,
    .room = DT_CB_ROOM};
static const dt_voice_t DT_V_CLAPS = {
    .rate = DT_RATE_CLAPS, .m = DT_MODES_CLAPS, .n = 9, .rom = DT_PERC_RATE, .len = 0.44f, .gain = DT_CP_GAIN,
    .wires = DT_CP_WIRE_G, .wires_tau = DT_CP_TAIL, .wires_rise = DT_CP_RISE, .noise = DT_CP_NOISE,
    .tail = DT_CP_TAIL_G, .tail_tau = DT_CP_TAIL_T, .tail_rise = DT_CP_TAIL_R, .bursts = DT_CP_BURSTS,
    .burst_gap = DT_CP_GAP, .burst_tau = DT_CP_BURST_T, .burst_db = DT_CP_BURST_DB,
    .burst_jit = DT_CP_JIT,
    .body = 1.0f, .vcf = DT_CP_VCF,
    .vcf_lo = DT_CP_VCF_LO, .vcf_t = DT_CP_VCF_T, .lp = DT_CP_LP, .hp = DT_CP_HP, .room = DT_CP_ROOM};
static const dt_voice_t DT_V_TAMB = {
    .rate = DT_RATE_TAMB, .m = DT_MODES_TAMB, .n = 14, .rom = DT_PERC_RATE, .len = 0.57f, .gain = DT_TB_GAIN,
    .stick = DT_TB_STICK, .wires = DT_TB_WIRE_G, .wires_tau = DT_TB_TAIL, .bursts = DT_TB_BURSTS,
    .burst_gap = DT_TB_GAP, .burst_tau = DT_TB_BURST_T, .burst_db = DT_TB_BURST_DB, .burst_jit = DT_TB_JIT,
    .body = DT_TB_BODY, .vcf = DT_TB_VCF, .vcf_lo = 1.0f, .vcf_fixed = 1, .hp = DT_TB_HP, .attack = DT_TB_ATTACK,
    .copies = DT_TB_COPIES, .copy_detune = DT_TB_DETUNE, .copy_gap = DT_TB_CGAP, .room = DT_TB_ROOM};
#define DT_V_HAT_ .rate = DT_RATE_HAT, .m = DT_MODES_HAT, .n = 17, .rom = DT_HH_RATE, .len = DT_HH_LEN, \
    .stick = DT_HH_STICK, .tail = 1.0f, .tail_tau = 10.0f, .body = 1.0f, .vcf = DT_HH_VCF, .vcf_lo = 1.0f, \
    .vcf_fixed = DT_HH_VCF_FIXED, .lp = DT_HH_LP, .hp = DT_HH_HP, .room = DT_HH_ROOM   /* (both hats') */
static const dt_voice_t DT_V_OHH = {DT_V_HAT_, .gain = DT_HH_GAIN};
static const dt_voice_t DT_V_CHH = {DT_V_HAT_, .gain = DT_CHH_GAIN, .vca_t = DT_CHH_VCA,
    .vca_hold = DT_CHH_HOLD, .vca_end = DT_CHH_END};
static const dt_voice_t DT_V_CRASH = {
    .rate = DT_RATE_CRASH, .m = DT_MODES_CRASH, .n = 29, .rom = DT_CR_RATE, .len = 3.45f, .gain = DT_CR_GAIN,
    .stick = DT_CR_STICK, .tail = DT_CR_WASH, .tail_tau = DT_CR_WASH_T, .tail_rise = DT_CR_WASH_R, .body = 1.0f,
    .attack = DT_CR_SWELL, .pitch = DT_CR_PITCH, .gate = DT_CR_GATE, .gate_db = 60.0f, .gate_t = DT_CR_GATE_T,
    .room = DT_CR_ROOM};
static const dt_voice_t DT_V_RIDE = {
    .rate = DT_RATE_RIDE, .m = DT_MODES_RIDE, .n = 31, .rom = DT_RD_RATE, .len = 3.1f, .gain = DT_RD_GAIN,
    .stick = DT_RD_STICK, .tail = DT_RD_WASH, .tail_tau = DT_RD_WASH_T, .body = DT_RD_BODY, .gate = DT_RD_GATE,
    .gate_db = 60.0f, .gate_t = DT_RD_GATE_T, .pitch = DT_RD_PITCH,
    .room = DT_RD_ROOM};
static const dt_voice_t DT_V_CABASA = {
    .rate = DT_RATE_CABASA, .m = DT_MODES_CABASA, .n = 7, .rom = DT_PERC_RATE, .len = 0.39f, .gain = DT_CA_GAIN,
    .tail = DT_CA_TAIL_G, .tail_tau = DT_CA_TAIL_T, .tail_rise = DT_CA_TAIL_R, .body = 1.0f, .vcf = DT_CA_VCF,
    .vcf_lo = 1.0f, .vcf_fixed = 1, .hp = DT_CA_HP, .gate = DT_CA_GATE, .gate_db = DT_CA_GATE_DB,
    .gate_t = DT_CA_GATE_T, .room = DT_CA_ROOM};
static const dt_voice_t *const DT_VOICE[NDT] = {[DT_BASS] = &DT_V_BASS, [DT_SNARE] = &DT_V_SNARE,
                                                [DT_RIM] = &DT_V_RIM, [DT_TOM1] = &DT_V_TOM, [DT_TOM2] = &DT_V_TOM,
                                                [DT_CLAPS] = &DT_V_CLAPS, [DT_COWBELL] = &DT_V_COWBELL,
                                                [DT_TAMB] = &DT_V_TAMB, [DT_CABASA] = &DT_V_CABASA,
                                                [DT_CHH] = &DT_V_CHH, [DT_OHH] = &DT_V_OHH,
                                                [DT_CRASH] = &DT_V_CRASH, [DT_RIDE] = &DT_V_RIDE};

typedef struct {
    float last, fade;                        /* its last output sample; what's left of the sound it cut off (dt_next) */
    uint32_t pos, inc;                       /* the read position (ROM samples, Q16) and its step an output sample */
    uint32_t k, len;                         /* the ROM sample being held, and where the sample ends */
    float out, lvl;                          /* the held sample (after the DAC) and the level, 0..1 */
    float v1, v2, vk;                        /* the VCF: two one-poles, their step (1: none) */
    float vw, vlo, ve, ve_k;                 /* its open cutoff (rad a sample), its floor, its envelope and decay */
    uint32_t vt;                             /* output samples since the hit (the envelope's steps) */
    float lp, lpk;                           /* the output filter (fixed: not in ROM time) and its step */
    float hz, hpk;                           /* the output high pass: its low part, its step (0: none) */
    float att, att_k;                        /* the attack's swell still to come, its decay a sample */
    uint32_t gate;                           /* the ROM sample where the gate falls (0: none) */
    float gl, glo, gl_k;                     /* the gate's level now, where it falls to, its step a sample */
    float send;                              /* into the room */
    float post;                              /* its level out (dt_voice_t post) */
    float va, va_k;                          /* the VCA's level (over 1: held) and its fall a sample (va_k 0: none) */
    uint32_t vn;                             /* output samples till it is cut */
#ifdef PLAT_HOST
    float burst, burst_k, wire, wire_k;      /* the stick's burst and the wires' noise: levels, decays a sample */
    uint32_t bn, bnext, bgap;                /* the wires' strikes still to come, the next's ROM sample, the gap */
    float bk;                                /* the last strike's decay, kept till then */
    float bl, bl_k;                          /* the next strike's level, and its fall each strike */
    float rise, rise_k;                      /* the wires' rise: subtracted from their level, falling away */
    float tl, tl_k, tr, tr_k;                /* the tail's noise: its level and swell, their decays a sample */
    float sw, sw_k, sw2, sw2_k;              /* the pitch's two excesses, their decays every 8 samples */
    uint8_t n;                               /* the resonators played (the modes, copies counted) */
    uint8_t fl[DT_NM];                       /* each one's mode's flags */
    uint16_t st[DT_NM];                      /* each one's strike, a ROM sample (0: the hit's, with the stick's) */
    float y1[DT_NM], y2[DT_NM];              /* the resonators: their last two outputs, */
    float a1[DT_NM], a2[DT_NM], r[DT_NM], w[DT_NM], g[DT_NM];   /* coefficients, radius, angle, input gain */
#endif
    const dt_voice_t *v;
    const int8_t *tab;                       /* its virtual EPROM (0: made live) */
    uint8_t drum;                            /* DT_*, or DT_NONE: silent */
} dt_ch_t;

static struct {
    uint8_t vol[NDT], tune[NDT];             /* each drum's VOLUME and TUNING, 0..15 */
    uint8_t metro, master;                   /* the METRONOME's and the drums' MASTER VOLUME, 0..127 */
    uint8_t tempo;                           /* their TEMPO, BPM, their own (the manual's 100 at first): running, the clock
                                              * master, the bass on it (clock_bpm) */
    dt_ch_t ch[NDTCH];
} dt PLAT_POOL;
static fifo_t dtq;                           /* main loop -> audio: a hit, drum | tune << 4 | level << 8 */
static volatile uint32_t dt_flash;           /* audio -> main loop: the drums hit since the screen last looked, a bit
                                              * each (dt_flash_take) */
#ifdef PLAT_HOST
static uint32_t dt_rng = 1u;                 /* the noise's: never reset, so each hit's is its own */
#endif
#ifdef PLAT_HOST
static float dt_test_gain = 1.0f;            /* (the host's dt_gain: renders well above its 16-bit files' floor) */
static uint32_t dt_hits;                     /* (the host's: hits played, for tests/drums.txt) */
#endif

#define DT_CYM_VOL 10u                       /* the cymbals' first VOLUME: about 3.5 dB under the rest (user: too loud at 15) */
static void dt_init(void)
{
    uint32_t i;
    for (i = 0; i < NDT; i++) {
        dt.vol[i] = 15u;
        dt.tune[i] = 7u;
    }
    dt.metro = 90u;
    dt.vol[DT_CRASH] = dt.vol[DT_RIDE] = DT_CYM_VOL;
    dt.tempo = 100u;
    dt.master = 127u;
    dt.tune[DT_TOM1] = 9u;                   /* (placeholders until the 400's own defaults are known) */
    dt.tune[DT_TOM2] = 5u;
    for (i = 0; i < NDTCH; i++)
        dt.ch[i].drum = DT_NONE;
}

#ifdef PLAT_HOST
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
    float n = dt_noise(), xs, xw, xt, s = 0.0f;
    uint32_t i;
    if (c->sw + c->sw2 > 1e-4f && !(c->k & 7u)) {   /* the pitch falling: the resonators retuned every 8 samples */
        float f = 1.0f + c->sw + c->sw2;
        c->sw *= c->sw_k;
        c->sw2 *= c->sw2_k;
        for (i = 0; i < c->n; i++)
            if (!(c->fl[i] & DT_MF))
                c->a1[i] = 2.0f * c->r[i] * fm_cosf(c->w[i] * f);
    }
    xs = n * c->burst + (c->k ? 0.0f : 1.0f);   /* the stick: a click, then its noise */
    if (c->bn && c->k >= c->bnext) {         /* the wires struck again: swelling from where they are */
        c->rise = c->bl - (c->wire - c->rise);
        c->wire = c->bl;
        c->bl *= c->bl_k;
        c->bnext += (uint32_t)((float)c->bgap * (1.0f + v->burst_jit * dt_noise()));   /* (spread: jit <= 1) */
        if (!--c->bn)
            c->wire_k = c->bk;
    }
    xw = n * (c->wire - c->rise);
    xt = c->tl > 0.0f ? dt_noise() * (c->tl - c->tr) : 0.0f;   /* (its own noise) */
    c->tl *= c->tl_k;
    c->tr *= c->tr_k;
    c->burst *= c->burst_k;
    c->wire *= c->wire_k;
    c->rise *= c->rise_k;
    for (i = 0; i < c->n; i++) {
        float x = c->fl[i] & DT_MW ? xw : c->fl[i] & DT_MT ? xt                   /* (what drives it) */
                  : c->st[i] ? (c->k == c->st[i] ? 1.0f : 0.0f) : xs;            /* (a later copy: its own click) */
        float y = c->a1[i] * c->y1[i] - c->a2[i] * c->y2[i] + c->g[i] * x;
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

#endif

/* a decay's factor every per samples at rate samples a second, time constant tau seconds (0: gone at once) */
static float dt_decay(float tau, float rate, float per)
{
    return tau > 0.0f ? fm_expf(-per / (tau * rate)) : 0.0f;
}

/* the virtual EPROMs (dt_eprom.h, tools/dt_eprom.py): a voice made once, offline, by dt_gen at its full cost, kept as the
 * 8-bit numbers its DAC would be given, and read at the tuned rate as the 400's ROMs were; the VCF, the level, the
 * output filters and the room stay live. [1] the low tunings' take where a voice has one (its low fields: steps 1-3) */
#include "dt_eprom.h"
#define DT_LOWONLY_TRIM 0.708f               /* -3 dB: a voice with its low take alone (DT_EPROM_LOWONLY), at steps 4 on, where
                                              * that take's body is louder than the take it replaced (since v0.7.3) */
static uint8_t dt_use_eprom = 1;             /* 0: every voice made live (the host's A/B) */
#ifdef PLAT_HOST
static struct { int8_t *d; uint32_t n; } dt_baked[NDT][2];   /* (dt_bake: a voice's ROM made here and now, before its table) */
#endif
static const int8_t *dt_eprom_of(uint32_t d, uint32_t tune, uint32_t *n)
{
#ifdef PLAT_HOST
    if (dt_use_eprom && d < NDT && dt_baked[d][0].d) {
        const uint32_t l = tune < 3u && dt_baked[d][1].d;
        *n = dt_baked[d][l].n;
        return dt_baked[d][l].d;
    }
#endif
    const uint32_t low = tune < 3u && d < sizeof DT_EPROM / sizeof DT_EPROM[0] && DT_EPROM[d][1].d;
    if (!dt_use_eprom || d >= sizeof DT_EPROM / sizeof DT_EPROM[0] || !DT_EPROM[d][0].d)
        return NULL;
    *n = DT_EPROM[d][low].n;
    return DT_EPROM[d][low].d;
}

/* a hit: its channel restarted on drum d at tuning step tune (0..15), level 0..255 */
static void dt_hit(uint32_t d, uint32_t tune, uint32_t level)
{
    const dt_voice_t *v = d < NDT ? DT_VOICE[d] : NULL;
    dt_ch_t *c;
    float rom, rate, was;
#ifdef PLAT_HOST
    uint32_t i, j, q, nc;
    float body, lowf, lowg, cdet[DT_NCOPY], cg, pitch;
    uint16_t cst[DT_NCOPY];
#endif
    if (!v)
        return;
#ifdef PLAT_HOST
    dt_hits++;
#endif
    dt_flash |= 1u << d;
    tune &= 15u;
    rom = v->rom;
    rate = (float)v->rate[tune] * (1.0f / 4096.0f);
#ifdef PLAT_HOST
    lowf = (float)(tune < 3u ? 3u - tune : 0u) * (1.0f / 3.0f);   /* (1 at the first step, 0 from the fourth) */
    body = v->body * (1.0f + v->low * lowf);
    lowg = fm_db2lin(v->low_db * lowf);
    pitch = v->pitch > 0.0f ? v->pitch : 1.0f;
#endif
    c = &dt.ch[DT_CH[d]];
    was = c->fade + (c->drum != DT_NONE ? c->last : 0.0f);   /* (what it was playing: faded out, no step to a click) */
    *c = (dt_ch_t){0};
    c->fade = was;
    c->v = v;
    c->drum = (uint8_t)d;
    c->inc = (uint32_t)(rate * rom * (65536.0f / BS2_RATE));
    c->len = (uint32_t)(v->len * rom);
    c->lvl = (float)level * (1.0f / 255.0f);
    c->tab = dt_eprom_of(d, tune, &c->len);
    if (c->tab && tune >= 3u && (DT_EPROM_LOWONLY >> d & 1u))
        c->lvl *= DT_LOWONLY_TRIM;           /* (the low take read at the higher tunings: their old level) */
    if (!c->tab)
        c->len = (uint32_t)(v->len * rom);
    c->lpk = v->lp > 0.0f ? v->lp : 1.0f;
    c->hpk = v->hp > 0.0f ? 1.0f - fm_expf(-FM_TWO_PI * v->hp * rate / BS2_RATE) : 0.0f;
    c->att = v->attack > 0.0f ? 1.0f : 0.0f;
    c->att_k = dt_decay(v->attack, rom, 1.0f);
    c->gate = (uint32_t)(v->gate * rom);
    c->gl = 1.0f;
    c->glo = fm_db2lin(-v->gate_db);
    c->gl_k = 1.0f - dt_decay(v->gate_t > 0.0f ? v->gate_t : 0.002f, rom, 1.0f);
    c->send = v->room;
    c->post = v->post > 0.0f ? v->post : 1.0f;
    if (v->vca_t > 0.0f) {
        c->va = fm_expf(v->vca_hold / v->vca_t);
        c->va_k = dt_decay(v->vca_t, BS2_RATE, 1.0f);
        c->vn = (uint32_t)(v->vca_end * BS2_RATE);
    }
    c->vk = 1.0f;
    if (v->vcf > 0.0f) {
        c->vw = FM_TWO_PI * v->vcf * (v->vcf_fixed ? 1.0f : rate) / BS2_RATE;
        c->vlo = v->vcf_lo;
        c->ve = 1.0f;
        c->ve_k = dt_decay(v->vcf_t, BS2_RATE, 16.0f);
    }
#ifdef PLAT_HOST
    c->burst = v->stick;
    c->burst_k = dt_decay(DT_STICK_TAU, rom, 1.0f);
    c->wire = c->rise = v->wires;
    c->wire_k = dt_decay(v->wires_tau, rom, 1.0f);
    if (v->bursts > 1u) {                    /* (between strikes, their own decay; the last's, wires_tau) */
        c->bn = v->bursts - 1u;
        c->bgap = c->bnext = (uint32_t)(v->burst_gap * rom);
        c->bk = c->wire_k;
        c->wire_k = dt_decay(v->burst_tau, rom, 1.0f);
        c->bl_k = fm_db2lin(-v->burst_db);
        c->bl = v->wires * c->bl_k;
    }
    c->rise_k = dt_decay(v->wires_rise, rom, 1.0f);
    c->tl = c->tr = v->tail;
    c->tl_k = dt_decay(v->tail_tau, rom, 1.0f);
    c->tr_k = dt_decay(v->tail_rise, rom, 1.0f);
    c->sw = v->sweep;
    c->sw_k = dt_decay(v->sweep_tau, rom, 8.0f);
    c->sw2 = v->sweep2;
    c->sw2_k = dt_decay(v->sweep2_tau, rom, 8.0f);
    nc = v->copies > 1u ? (v->copies < DT_NCOPY ? v->copies : DT_NCOPY) : 1u;
    cg = 1.0f;
    cst[0] = 0;
    cdet[0] = 1.0f;
    if (nc > 1u) {                           /* each jingle: its detune and its strike, its partials sharing them */
        uint32_t at = 0;
        cg = fm_rsqrtf((float)nc);           /* (together about as loud as one) */
        for (q = 0; q < nc; q++) {
            cdet[q] = 1.0f + v->copy_detune * (2.0f * (float)q / (float)(nc - 1u) - 1.0f + 0.25f * dt_noise());
            if (q)
                at += (uint32_t)(v->copy_gap * rom * (1.0f + 0.5f * dt_noise()));
            cst[q] = (uint16_t)(at < 65535u ? at : 65535u);
        }
    }
    for (i = 0, j = 0; !c->tab && i < v->n && j < DT_NM; i++) {   /* (from an EPROM: no resonators) */
        const dt_mode_t *m = &v->m[i];
        uint32_t k = m->flags & DT_MC ? nc : 1u;
        for (q = 0; q < k && j < DT_NM; q++, j++) {
            float hz = m->hz * pitch * (k > 1u ? cdet[q] : 1.0f);
            c->fl[j] = m->flags;
            c->st[j] = k > 1u ? cst[q] : 0u;
            c->w[j] = FM_TWO_PI * hz / rom;
            c->r[j] = dt_decay(m->tau * body, rom, 1.0f);
            c->a1[j] = 2.0f * c->r[j] * fm_cosf(c->w[j] * (m->flags & DT_MF ? 1.0f : 1.0f + v->sweep + v->sweep2));
            c->a2[j] = c->r[j] * c->r[j];
            c->g[j] = fm_db2lin(m->db) * fm_sinf(c->w[j]) * (k > 1u ? cg : 1.0f);   /* (a struck resonator rings
                                              * at its gain over sin w) */
            if (i && !(m->flags & (DT_MW | DT_MF)) && m->hz < 300.0f)   /* (the body's overtones: over the first
                                              * mode, under the bloom and the smack) */
                c->g[j] *= lowg;
        }
    }
    c->n = (uint8_t)j;
#endif
}

/* a hit's level, 0..255: velocity 1..127 as the 400's 16 volume steps, accented (+80), under the drum's VOLUME (15:
 * as played) */
static uint32_t dt_level(uint32_t d, uint32_t vel, int acc)
{
    return ((vel >> 3) * 11u + 10u + (acc ? 80u : 0u)) * dt.vol[d] / 15u;
}

/* a MIDI note on the drums' channel (audio interrupt): accented at velocity 120 up, as the 400's; recorded as a key's */
static void dq_hit_in(uint32_t d, uint32_t acc);
static void dt_note(uint32_t note, uint32_t vel)
{
    uint32_t d = note >= 35u && note < 35u + 24u ? DT_KEY[note - 35u] : DT_NONE;
    if (d != DT_NONE && vel) {
        dt_hit(d, dt.tune[d], dt_level(d, vel, vel >= 120u));
        dq_hit_in(d, vel >= 120u);
    }
}

/* a hit from the main loop (the FM-1's keys, its pads): to the audio through dtq */
static void dt_play(uint32_t d, uint32_t vel, int acc)
{
    if (d < NDT)
        fifo_put(&dtq, d | (uint32_t)dt.tune[d] << 4 | dt_level(d, vel, acc) << 8);
}

/* the drums hit since the screen last looked (dt_flash) */
static uint32_t dt_flash_take(void)
{
    uint32_t f = dt_flash;
    dt_flash &= ~f;                          /* (a hit between the two: kept for the next look, or one look late) */
    return f;
}

/* a MIDI CC on the drums' CC channel (audio interrupt): 33..45 each drum's tuning, 46..58 its volume (Bass to
 * Cabasa, the panel's order; 0..127 to 0..15) */
#define DT_CC_TUNE 33u
#define DT_CC_VOL (DT_CC_TUNE + NDT)
/* the sequencer's (bs2_dseq.c), for the bass's clock, which comes first in app.c */
static volatile uint8_t dt_running;          /* a pattern runs */
static volatile float dt_pos;                /* where in its loop, ticks */
static volatile float dt_lock_h;             /* Swing Lock (GLO: DRUM MODS) on and running: a quarter's first 8th in ticks, 0 off */
static volatile uint8_t dt_cc_last;          /* audio -> main loop: the last of those CCs (0: none since it looked) */
static void dt_cc(uint32_t cc, uint32_t v)
{
    if (cc >= DT_CC_TUNE && cc < DT_CC_TUNE + NDT)
        dt.tune[cc - DT_CC_TUNE] = (uint8_t)(v >> 3);
    else if (cc >= DT_CC_VOL && cc < DT_CC_VOL + NDT)
        dt.vol[cc - DT_CC_VOL] = (uint8_t)(v >> 3);
    else
        return;
    dt_cc_last = (uint8_t)cc;
}

/* a sound cut off, by the next hit on its channel or its sample's end: its last output falls away in about 2 ms */
#define DT_FADE_K 0.98873f
/* the drums' next output sample at level g (audio interrupt), -1..1 at a full level; their room send, at g too, into
 * the FX bus (bs2_fx.c) */
static inline float dt_next(float g)
{
    uint32_t i;
    float s = 0.0f, r = 0.0f;
    for (i = 0; i < NDTCH; i++) {
        dt_ch_t *c = &dt.ch[i];
        uint32_t to;
        float o;
        if (c->fade != 0.0f) {               /* (a sound cut off: fading out, DT_FADE_K) */
            s += c->fade;
            c->fade *= DT_FADE_K;
            if (c->fade > -1e-5f && c->fade < 1e-5f)
                c->fade = 0.0f;
        }
        if (c->drum == DT_NONE)
            continue;
        fx_wake();
        c->pos += c->inc;
        to = c->pos >> 16;
        while (c->k < to) {                  /* each ROM sample passed: made, to 8 bits, through the DAC */
#ifdef PLAT_HOST
            c->out = c->tab ? (float)c->tab[c->k] * (1.0f / 127.0f) * c->lvl : dt_dac(dt_gen(c)) * c->lvl;
#else
            c->out = c->tab ? (float)c->tab[c->k] * (1.0f / 127.0f) * c->lvl : 0.0f;
#endif
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
        o = c->lp;
        if (c->va_k > 0.0f) {                 /* the VCA: held, falling, then cut (in about 2 ms) */
            o *= fm_minf(c->va, 1.0f);
            c->va *= c->va_k;
            if (c->vn && !--c->vn)
                c->va_k = dt_decay(0.002f, BS2_RATE, 1.0f);
        }
        if (c->hpk > 0.0f) {                  /* the high pass: less its low part */
            c->hz += (o - c->hz) * c->hpk;
            o -= c->hz;
        }
        c->last = o * c->post;
        s += c->last;
        r += o * c->send;
        if (c->drum == DT_NONE)              /* (the sample's end: faded out too) */
            c->fade += c->last;
    }
#ifdef PLAT_HOST
    g *= dt_test_gain;
#endif
    fx_room_send(r * g);                     /* (after the drums' level: a post-fader send) */
    return s * g;
}

/* a block's start (audio interrupt): the main loop's hits */
static void dt_block(void)
{
    uint32_t e;
    while (fifo_get(&dtq, &e))
        dt_hit(e & 15u, (e >> 4) & 15u, (e >> 8) & 0xFFu);
}
