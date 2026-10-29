| Take | Patch (its name on the BS2) | Play | What it measures |
| --- | --- | --- | --- |
| 1 | `TK01_RESO_TRACK.syx` (TK01 RESO TRACK) | C1 3 s, C2 3 s, C3 3 s, C4 3 s, C5 3 s | the key tracking's pivot and slope: the filter ringing alone, its pitch at each C |
| 2 | `TK02_RESO_NOISE.syx` (TK02 RESO NOISE) | C1 3 s, C2 3 s, C3 3 s, C4 3 s, C5 3 s | the same with noise through it (if take 1 is silent: the filter does not ring alone) |
| 3 | `TK03_RESO_NO_TRK.syx` (TK03 RESO NO TRK) | C2 3 s, C4 3 s | no tracking: the ring's pitch the same on every key (the cutoff as set) |
| 4 | `TK04_CUT_0.syx` (TK04 CUT 0) | C3 3 s | the cutoff's curve: noise through a resonant filter, the dial at 0 (of 255 in the dump) |
| 5 | `TK05_CUT_64.syx` (TK05 CUT 64) | C3 3 s | the same, the dial at a quarter |
| 6 | `TK06_CUT_128.syx` (TK06 CUT 128) | C3 3 s | the same, at half |
| 7 | `TK07_CUT_192.syx` (TK07 CUT 192) | C3 3 s | the same, at three quarters |
| 8 | `TK08_CUT_255.syx` (TK08 CUT 255) | C3 3 s | the same, fully open |
| 9 | `TK09_SAW_OPEN.syx` (TK09 SAW OPEN) | C2 3 s, C4 3 s | the level reference: oscillator 1's saw, the filter open |
| 10 | `TK10_SAW_CUT.syx` (TK10 SAW CUT) | C2 3 s | the filter's slope and tone: the saw under a low cutoff |
| 11 | `TK11_SUB_SQUARE.syx` (TK11 SUB SQUARE) | C2 3 s | the sub's level and shape: its square alone, an octave down |
| 12 | `TK12_SUB_SINE.syx` (TK12 SUB SINE) | C2 3 s | its sine alone |
| 13 | `TK13_TWO_SAWS.syx` (TK13 TWO SAWS) | C2 6 s | both saws, oscillator 2 seven cents up: their level together, their beating |
| 14 | `TK14_DRIVE_64.syx` (TK14 DRIVE 64) | C2 3 s | the filter's drive at half, the saw |
| 15 | `TK15_DRIVE_127.syx` (TK15 DRIVE 127) | C2 3 s | and at full |
| 16 | `TK16_DIST_64.syx` (TK16 DIST 64) | C2 3 s | the distortion at half |
| 17 | `TK17_DIST_127.syx` (TK17 DIST 127) | C2 3 s | and at full |
| 18 | `TK18_AMP_ENV_MID.syx` (TK18 AMP ENV MID) | C2 4 s | the amp envelope's times: every knob at half, a note held 4 s |
| 19 | `TK19_AMP_SNAP.syx` (TK19 AMP SNAP) | C2 0.3 s, 4 times (1 s apart) | a short envelope's snap: four short notes |
| 20 | `TK20_FILTER_ENV.syx` (TK20 FILTER ENV) | C2 3 s, 2 times (1 s apart) | the mod envelope on the cutoff: its sweep and times |
| 21 | `TK21_GLIDE_64.syx` (TK21 GLIDE 64) | hold C2; while held, C4 for 2 s; let C4 go (1 s), then C2 | the glide's time: C2 held, C4 pressed while it is, then C4 let go, then C2 |
| 22 | `TK22_LFO_SLOW.syx` (TK22 LFO SLOW) | C3 6 s | LFO 1's rate at half: a wide vibrato on oscillator 1 |
| 23 | `TK23_LFO_FAST.syx` (TK23 LFO FAST) | C3 4 s | and faster |
| 24 | `TK24_ACID_RESO.syx` (TK24 ACID RESO) | C2 3 s | the Acid filter under resonance, the saw |
| 25 | `TK25_RING.syx` (TK25 RING) | C3 3 s | the ring modulator alone: the saws a fifth apart |
