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
    uint8_t spare[2];
} b3_patch_t;
void b3_init(void);
void b3_note(uint32_t note, uint32_t vel);   /* vel 0: up; 128 and over: every key up */
void b3_set(const b3_patch_t *p);            /* a preset's sound */
void b3_leslie(uint32_t fast, uint32_t brake);   /* the Leslie: chorale or tremolo, the brake (live) */
void b3_expr(uint32_t v);                    /* the expression pedal, 0..127 (live) */
uint32_t b3_job(float *mid, float *side, uint32_t n);   /* the second core's: a block, its middle into mid, its
                                              * stereo sides into side; 1 if it sounds */
void b3_cap(uint32_t n);                     /* the wheels it may sound at most (an overload's; 91: all) */
#endif
