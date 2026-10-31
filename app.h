/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 John Brown */
/* Bass Something 2: a Bass Station 2-style monosynth for the M-VAVE FM-1, started from fm1-base's template.
 * This header is the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "BASS SOMETHING 2"
#define APP_SPLASH                       /* the boot screen: the name in the panel's cyan on its black (app.c) */
void app_splash(void);
#define APP_BOOT                         /* the splash held as long again, an oscillator sweeping on it (app.c) */
void app_boot(void);
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity and pressure from the keys (lib/touch.h) */
#define APP_BTDEV                        /* Bluetooth MIDI devices (lib/btdev.h) */
#define APP_BLE_AT_BOOT (!bs2_ble_off())   /* Bluetooth at power-on: as the player left it (GLO MIDI) */
int bs2_ble_off(void);

/* the store: the app's objects, the panel calibration (the platform's) and Bluetooth's address and pairing among
 * them, and their flash offsets; OBJ_PATCHES, the patch bank (bank_t, app.c), in the data area under the panel's;
 * OBJ_SEEDS, the RND seed table (seeds_t, bs2_rnd.c), under the bank */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_PATCHES, OBJ_SEEDS, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xDA000u, 0xD8000u}

/* the voice (app.c): two oscillators (saw, square, sine, triangle or a pulse of its own width, each at its own
 * footage, 2 hard-synced to 1 if asked), a sub (a sine, a narrow pulse or a square) and noise, mixed at their levels (1
 * and 2 or their ring modulation), through the filter, then the patch's ADSR amplitude envelope and the distortion;
 * the pitch glides; a mod envelope moves the cutoff and each oscillator's pitch by its own depth, two LFOs
 * the cutoff or the pitch, LFO 2 the pulses' widths too; static, no allocation */
#define BS2_RATE 44100.0f
enum { WAVE_SAW, WAVE_SQUARE, WAVE_SINE, WAVE_TRI, WAVE_PULSE, NWAVES };   /* (saw, square: BANK_VERSION 1's) */
enum { SUB_SINE, SUB_PULSE, SUB_SQUARE, NSUBW };   /* the sub's waves, in the Bass Station II's order */
enum { RANGE_16, RANGE_8, RANGE_4, RANGE_2, NRANGE };   /* an oscillator's footage: RANGE_8 the note itself */
enum { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
typedef struct {
    float att;                           /* attack: a one-pole coefficient toward ATT_AIM (a capacitor's charge) */
    float dec, rel;                      /* decay and release: one-pole coefficients (exponential) */
    float sus;                           /* sustain level, 0..1 */
    uint8_t loop, count, fixed;          /* the Bass Station II's retriggering (again at its decay's end, count times:
                                          * 0 for ever), its fixed-duration sustain (no decay: the sustain held this
                                          * many steps, then the release, the key held or not) */
    float hold;
} adsr_t;
typedef struct {
    float lvl;                           /* the level, 0..1 */
    uint8_t stage;                       /* ENV_* */
    uint8_t loops;                       /* retriggered since the note began */
    float held;                          /* a fixed-duration sustain: steps left */
} env_t;
/* the envelopes' Triggering, the Bass Station II's (its NRPN and dump values): every note starts them; only a note not
 * played legato does (no key still held); Single's with the glide on legato notes alone */
enum { ENVT_MULTI, ENVT_SINGLE, ENVT_AUTOGLIDE, NENVT };
/* the modulation's destinations (app.c mod[]): the cutoff, oscillator 1's pitch, oscillator 2's. (Version 8's LFOs
 * each had one of these, an LFO's DEST, PITCH then both oscillators: bank_load converts them) */
enum { DEST_CUTOFF, DEST_PITCH, DEST_OSC2, NDEST };
/* an LFO's wave, the Bass Station II's (its dump's values): triangle, (falling) saw, square, sample and hold */
enum { LFO_TRI, LFO_SAW, LFO_SQUARE, LFO_SH, NLFOW };
#define NSYNCV 35u                       /* an LFO's sync values: its cycle in clock ticks, 64 beats .. a 32nd triplet */
#define NLFO 2u
typedef struct {
    uint8_t wave;                        /* LFO_* */
    uint8_t rate;                        /* its speed, the Bass Station II's 8 bits: 0..255, 0.21 .. 190 Hz (BANK_VERSION
                                          * 11's 0..127) */
    int8_t depth[2];                     /* where it goes, the Bass Station II's: LFO 1 oscillator 1's pitch and 2's,
                                          * LFO 2 the cutoff (and nothing); -63..+63, full 12 semitones or the
                                          * cutoff's whole range (BANK_VERSION 9's: a DEST_* and an amount) */
} lfo_patch_t;
typedef struct {
    float ph;                            /* 0..1, free-running (keysync: from 0 at a note) */
    float hold;                          /* S&H: the value held since the last wrap */
    float out;                           /* what it gives, slewed */
    float fade;                          /* its delay's fade-in since the note: 0..1 */
} lfo_t;
/* the filter: Classic (a ladder: low, band or high pass at 24 or 12 dB) or Acid (a diode ladder, low pass);
 * the drive before it */
enum { FLT_LP24, FLT_LP12, FLT_BP24, FLT_BP12, FLT_HP24, FLT_HP12, FLT_ACID, NFLT };
/* the arpeggiator and its sequencer, the Bass Station II's: one clock (the tempo, or MIDI clock when it comes in) and
 * one rhythm (two bars of 16ths); the arp plays the notes held (or latched) in its order over its octaves, the
 * sequencer (the arp's mode PLAY) a recorded sequence's steps, one at each of the rhythm's notes. The modes the
 * Bass Station II's, by its MIDI values (CC 118, a dump's; BANK_VERSION 5's had no UPDOWN2, 7's no PLAY, REC) */
enum { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_UPDOWN2, ARP_PLAYED, ARP_RANDOM, ARP_PLAY, ARP_REC, NARPM };
#define NRHY 32u                         /* the arp's rhythms: 1..NRHY, each two bars of 16ths (NRHY: all notes) */
#define NSTEPS 32u
#define NSEQ 4u                          /* the sequences kept (the settings' mem): the arp's OCTAVES picks one */
#define SEQ_REST 128u                    /* a step: silent */
typedef struct {                         /* a sequence: its notes and rests, each step's legato a bit */
    uint8_t len;                         /* the steps recorded: 0 (none) .. NSTEPS */
    uint8_t step[NSTEPS];                /* each a MIDI note or SEQ_REST */
    uint8_t leg[NSTEPS / 8u];            /* step i's note held into the next one's (not played again: a tie when the
                                          * same note) */
} seqmem_t;
typedef struct {                         /* the sound's settings: the main loop writes them, the audio reads them;
                                          * stored as they are (app.c bank_t): a change here is a new BANK_VERSION */
    uint8_t wave1, wave2;                /* the oscillators' WAVE_* */
    int8_t coarse2;                      /* oscillator 2: semitones, -12..+12 */
    int8_t fine2;                        /* and cents, -100..+100 */
    uint8_t sub_oct;                     /* the sub oscillator: 1 or 2 octaves under oscillator 1 */
    uint8_t filter;                      /* FLT_* */
    uint8_t cutoff, reso, drive;         /* 0..127 */
    uint8_t menv_a, menv_d, menv_s, menv_r;   /* the mod envelope, 0..127: times 2 ms .. 10 s, sustain 0..1 */
    int8_t menv_cut, menv_o1;            /* its depths, -63..+63 as an LFO's: the cutoff, oscillator 1's pitch (and
                                          * oscillator 2's below; BANK_VERSION 3's DEST_* and amount here) */
    uint8_t sync;                        /* oscillator 2 hard-synced to 1: 0 or 1 (BANK_VERSION 1's spare byte) */
    lfo_patch_t lfo[NLFO];
    uint8_t ring;                        /* 0..127: the mix from oscillators 1 + 2 to their ring modulation 1 x 2 */
    uint8_t arp_on;                      /* the arpeggiator: 0 or 1 (from here on BANK_VERSION 3's) */
    uint8_t arp_mode;                    /* ARP_* */
    uint8_t arp_oct;                     /* its range: 1..4 octaves; in REC and PLAY the sequence, 1..NSEQ */
    uint8_t latch;                       /* the notes kept after the keys let go, until the next chord: 0 or 1 */
    uint8_t rate_v7;                     /* (version 7's sequencer step, a division: unused, the rhythm's 16ths now) */
    uint8_t gate;                        /* a step's note sounds for this share of it: 5..100 % */
    uint8_t tempo;                       /* 40..240 BPM, while no MIDI clock comes in */
    seqmem_t sq;                         /* the sequence the arp's PLAY plays (version 7's: 16 steps, a tie a step) */
    int8_t menv_o2;                      /* the mod envelope on oscillator 2's pitch (from here on BANK_VERSION 4's) */
    uint8_t pw1, pw2;                    /* the pulses' widths: 5..95 % */
    int8_t pwm1, pwm2;                   /* LFO 2 on them: -63..+63, full 45 % either way */
    uint8_t noise;                       /* white noise in the mix: 0..127 */
    uint8_t glide;                       /* the pitch from note to note: 0 at once, 1..127 3 ms .. 2.5 s */
    uint8_t rhythm;                      /* the arp's: 1..NRHY */
    uint8_t swing;                       /* the arp's (and so the sequencer's): 3..97 %, a pair's first 16th's share */
    uint8_t lvl1, lvl2, lvl_sub;         /* the mixer's levels: 0..127 (from here on BANK_VERSION 5's) */
    uint8_t sub_wave;                    /* SUB_* */
    uint8_t range1, range2;              /* the oscillators' RANGE_*: 16' .. 2' */
    uint8_t amp_a, amp_d, amp_s, amp_r;  /* the amplitude envelope, 0..127 as the mod envelope's */
    uint8_t dist;                        /* the distortion after the amplifier: 0 (none) .. 127 */
    uint8_t arp_retrig;                  /* SEQ RETRIG: the arp's notes and the sequence from their first at each
                                          * rhythm's start (else they run on across it): 0 or 1 (from
                                          * here on BANK_VERSION 6's) */
    int8_t coarse1;                      /* oscillator 1 (and the sub): semitones, -12..+12 (from here on BANK_VERSION
                                          * 7's: the Bass Station II's fields a dump had and the FM-1 had not) */
    int8_t fine1;                        /* and cents, -100..+100 */
    int8_t menv_pw1, menv_pw2;           /* the mod envelope on the pulses' widths: -63..+63, full 45 % either way */
    int8_t vel_amp, vel_menv;            /* velocity on the amp envelope (its level) and on the mod envelope (its
                                          * depths): -63..+63, 0 none, +63 a soft note quiet (as before), - inverted */
    uint8_t key_trk;                     /* the cutoff following the keys: 0 none .. 7 full (an octave an octave, from
                                          * C3; the Bass Station II's 7 .. 0) */
    uint8_t lvl_ext;                     /* the mixer's Ext: the computer's USB audio in, 0..127 (BANK_VERSION 9's) */
    uint8_t env_trig[2];                 /* the amp and the mod envelope's Triggering, ENVT_* (from here on
                                          * BANK_VERSION 11's) */
    uint8_t env_loop[2];                 /* their retriggering: 0 or 1 */
    uint8_t env_count[2];                /* its count: 0 for ever, 1..16 */
    uint8_t env_fixed[2];                /* fixed-duration sustain: 0 or 1 */
    uint8_t lfo_delay[2];                /* the LFOs' fade-in at a note: 0 (none) .. 127 (from here on BANK_VERSION
                                          * 12's) */
    uint8_t lfo_slew[2];                 /* their edges softened: 0 .. 127 */
    uint8_t lfo_keysync[2];              /* from the wave's start at a note: 0 or 1 */
    uint8_t lfo_sync[2];                 /* their speed the clock's (Speed/Sync at Sync): 0 or 1 */
    uint8_t lfo_syncv[2];                /* then their cycle: 0..NSYNCV-1 (64 beats .. a 32nd triplet) */
    uint8_t ofm;                         /* Osc Filter Mod: oscillator 2 on the cutoff at audio rate, 0..127 (from here
                                          * on BANK_VERSION 13's) */
    uint8_t lim;                         /* the VCA limiter: 0 (off) .. 127 */
} patch_t;
typedef struct {
    float x1, F1;                        /* a tanh's last input, and its antiderivative there (ADAA) */
} adaa_t;
typedef struct {
    float s[4];                          /* the ladder's four stages (trapezoidal integrators) */
    float hs;                            /* Acid: the high pass in the feedback */
    adaa_t drv;                          /* the drive */
} ladder_t;
typedef struct {
    float phase, phase2, phase_sub;      /* the oscillators, 0..1 */
    float pitch;                         /* gliding to the note: a MIDI note, in semitones */
    float o1, o2;                        /* oscillators 1 and 2 a sample late: a sync's step corrected on both sides */
    ladder_t flt;
    env_t amp, menv;                     /* the amplitude envelope, the mod envelope */
    adaa_t dist;                         /* the distortion */
    float lim_e;                         /* the VCA limiter's level followed */
    float dc_x, dc_y;                    /* the output's coupling: the filter's last sample, and its own */
    float vel;                           /* the note's level from its velocity (VELOCITY AMP), 0..1 */
    float vm;                            /* and the mod envelope's depths' (VELOCITY MOD), 0..1 */
    float drift[2], drift_x[2], drift_t[2];   /* oscillators 1 and 2's wander (semitones), its first smoothing, its aim */
    float sag_f, sag_g;                  /* the supply's sag: the output's level followed, the gain it leaves */
    float heat;                          /* the filter's warming: 0 cold .. 1 (playing loud, resonance high) */
    int32_t note;                        /* the note it sounds, kept through the release */
    uint32_t trig;                       /* the note-ons it has started */
} voice_t;

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */
#define APP_SYSEX                        /* Bass Station II patch dumps over USB (bs2_sysex.c) */
int app_sysex(const uint8_t *p, uint32_t n);

/* the host simulator's checks (host/plat_host.c "expect"): octave, wave, wave2, note (-1 none), dirty, turnsN,
 * env and menv (the envelopes' stages, ENV_*), the patch: filter, cutoff, reso, drive, coarse2, fine2, sub, sync,
 * ring, noise, glide, pw1, pw2, rhythm, swing, latch, retrig, lvl1, lvl2, lvl_sub, sub_wave, range1, range2, amp_a, amp_d, amp_s,
 * amp_r, dist, coarse1, fine1, menv_a, trigA, trigM, loopA, loopM, countA, countM, fixedA, fixedM (the envelopes' modes), eloopA, eloopM (the times each retriggered), cents (oscillator 1's pitch's offset, signed), ofm, lim, lfo1_wave .. lfo2_syncv (lfoN_wave, _rate, _delay, _slew, _keysync, _sync, _syncv), menv_o1, menv_o2, menv_cut, pwm1, pwm2, menv_pw1, menv_pw2, vel_amp, vel_menv, key_trk, lvl_ext, lfo1_o1, lfo1_o2, lfo2_cut, arp, arp_mode, the page, the slot, seq_run, rec, step (the sequencer's cursor),
 * seqN (step N), legN (its legato), seq_len, memN (sequence N's length, 1..4), held (the notes the arp has), what the modulation made of the last block (cut_now: the cutoff,
 * fc_now: it in Hz, sag_now: the supply's sag, 1/1000 of the gain, hushed: 1 when quiet long enough for the keepers' flash writes, kept_tempo: the slot's
 * saved tempo,
 * amp_lvl: the amp envelope's level, 1/1000, menv_gain: the mod envelope's velocity's
 * gain, 1/1000, pw1_now: oscillator 1's pulse width, 1/10 %,
 * heat_now: the filter's warming, 1/1000, trim_now: what it and
 * the parts' tolerance make of the cutoff, 1/10000 (fc_now is the dial's, before it), drift1 and drift2: the oscillators' wander, 1/10 cent, drift_gap: how far apart, cents_abs: oscillator 1's pitch's offset, pitch_now: the glide's pitch in cents, dc_now: the
 * output's mean over its mean size, 1/1000), and any control on the page by
 * its name on the screen (DEST, AMOUNT, ATTACK, RATE ...) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
/* and its commands: sysex_in FILE (a .syx file in over USB, as app_sysex has it), sysex_out FILE (the last patch
 * dump sent, into FILE), bank_v3 .. bank_v7 (an older bank saved, for a reboot to read) */
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
