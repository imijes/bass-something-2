/* SPDX-License-Identifier: MPL-2.0 */
/* The organ, after the Hammond B-3 with its Leslie (bs2_b3.c, a unit of its own at -O2: fm1.toml o2) */
#ifndef BS2_B3_H
#define BS2_B3_H
#include <stdint.h>
#ifndef BS2_B3
#define BS2_B3 1                             /* the organ (0: none of it built) */
#endif
typedef struct {                             /* an organ preset's sound (app.c's bank of them) */
    uint8_t bar[9];                          /* the drawbars, 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1': 0..8 */
    uint8_t perc_on, perc_3rd, perc_soft, perc_fast;   /* the percussion's tabs: on, THIRD (else SECOND), SOFT, FAST */
    uint8_t vib_on, vib;                     /* the vibrato/chorus: on; its knob, 0..5 (V1 V2 V3 C1 C2 C3) */
    uint8_t drive, click, width;             /* the preamp's drive, the key click, the Leslie's stereo width: 0..127 */
    uint8_t vol_soft, vib_lower;             /* the VOLUME tab SOFT (6 dB down); VIBRATO LOWER */
    uint8_t ua[9];                           /* the upper manual's A# set (bar: its B set, the percussion's) */
    uint8_t la[9], lbar[9];                  /* the lower manual's A# and B sets */
    uint8_t ub, lb;                          /* each manual's preset key: 1 its B set, 0 its A# */
    uint8_t les[8];                          /* the Leslie's trimmers (b3_rotor's), 0..127 (64: the 122's) */
    uint8_t speed, swell, swell_rest, spare; /* the Leslie's speed (0 slow, 1 the brake, 2 fast); KEY SWELL, its rest */
} b3_patch_t;
void b3_init(void);
uint32_t b3_key(uint32_t lower, uint32_t note);   /* a note's key code on a manual (b3_note's) */
uint32_t b3_unkey(uint32_t code, uint32_t *lower);   /* its note again, its manual */
void b3_note(uint32_t code, uint32_t vel);
uint32_t b3_down(void);                     /* a key held */   /* vel 0: up; 128 and over: every key up */
void b3_set(const b3_patch_t *p);            /* a preset's sound */
void b3_leslie(uint32_t fast, uint32_t brake);   /* the Leslie: chorale or tremolo, the brake (live) */
void b3_rotor(const uint8_t t[8]);         /* the Leslie's trimmers, 0..127 (64: the 122's), the horn's then the drum's:
                                              * RISE TIME, FALL TIME, SLOW SPEED, FAST SPEED (live) */
float b3_trim(uint32_t i, uint32_t v);       /* a trimmer's value at its setting (i: b3_rotor's order): s or Hz */
float b3_angle(uint32_t r);                  /* a rotor's angle, 0..1 (0 the horn, 1 the drum) */
float b3_speed(uint32_t r);
float b3_level(void);                        /* the last block's peak out (the AMP page's valves) */                  /* its speed now, Hz */
void b3_expr(uint32_t v);                    /* the expression pedal, 0..127 (live) */
uint32_t b3_job(float *mid, float *side, uint32_t n);   /* the second core's: a block, its middle into mid, its
                                              * stereo sides into side; 1 if it sounds */
void b3_cap(uint32_t n);                     /* the wheels it may sound at most (an overload's; 91: all) */
#endif
