/* SPDX-License-Identifier: MPL-2.0 */
/* The B3 organ's CPU prototype (bs2_b3.c, a unit of its own at -O2: fm1.toml o2) */
#ifndef BS2_B3_H
#define BS2_B3_H
#include <stdint.h>
#ifndef BS2_B3_PROTO
#define BS2_B3_PROTO 0                       /* 1: the B3 organ's CPU prototype beside the RMI (0: none of it built) */
#endif
void b3_init(void);
void b3_note(uint32_t note, uint32_t vel);   /* vel 0: up; 128 and over: every key up */
void b3_cc(uint32_t c, uint32_t v);          /* its test controls (MIDI channel 16) */
uint32_t b3_job(float *mid, float *side, uint32_t n);   /* the second core's: a block, its middle added into mid,
                                              * its stereo sides written into side; 1 if it sounds */
void b3_cap(uint32_t n);                     /* the wheels it may sound at most (an overload's; 91: all) */
#endif
