/* SPDX-License-Identifier: MPL-2.0 */
/* PROF_TIMING: the render timed in parts, said every 2 s (app.c; tools/fm1_remote.py listen). The second core's parts
 * (the organ's, the FX bus's: their own units) add their ticks into prof2 through these */
#ifndef BS2_PROF_H
#define BS2_PROF_H
#ifndef PROF_TIMING
#define PROF_TIMING 0                        /* 1: each part of the render's share of a block, % (now/most), said every
                                              * 2 s: the first core's before the job (P), beside it (B), waiting for it
                                              * (W), after it (F); the second core's RMI (R), organ (O), FX bus (X); B's
                                              * the bass's step (c), its voice (v), the drums and the mix (d); the
                                              * organ's wheels (g), clicks and scanner (s), preamp and Leslie (L); the FX
                                              * bus's tape (t), spring (p), room (r) */
#endif
#if PROF_TIMING
#include <stdint.h>
#include "plat.h"
extern volatile uint32_t prof2[6];           /* the second core's parts' ticks this block (app.c takes them after it) */
#define PROF2_AT(v) uint32_t v = PLAT_FAR(plat_ticks24)()   /* (a far call: the second core's RAM code, the clock XIP) */
#define PROF2_ADD(k, v) do { const uint32_t t_ = PLAT_FAR(plat_ticks24)(); prof2[k] += (t_ - (v)) & 0xFFFFFFu, (v) = t_; } while (0)
#else
#define PROF2_AT(v)
#define PROF2_ADD(k, v)
#endif
#endif
