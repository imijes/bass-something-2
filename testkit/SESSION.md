# Bass Station II recording session

Bass Something 2 is a Bass Station II-style synth for the M-VAVE FM-1. These recordings let it be tuned against a real Bass
Station II: how the filter follows the keys, the cutoff's range, resonance, envelope times, levels, drive,
distortion, glide and LFO speed. It takes about half an hour.

Nothing here changes your Bass Station II's saved patches. Each test patch loads into the sound you're playing now,
and it's only kept if you press Save.

## What you need

- The Bass Station II, on firmware 2.5 or later (the filter tracking setting arrived in 2.5).
- A computer connected to it by USB, with a program that sends `.syx` files: SysEx Librarian on a Mac (free), MIDI-OX
  on Windows, or Novation Components.
- An audio interface recording the Bass Station II's output. Record at 48 kHz (44.1 is fine), 24-bit, mono or stereo.
- The `syx` folder from this kit: 25 files, `TK01_RESO_TRACK.syx` to `TK25_RING.syx`.

## Before you start

1. Turn off any effects, compression or normalising in the recording program.
2. Make sure the arpeggiator and latch are off, the mod wheel is down and pitch bend is centred.
3. Set the Bass Station II's volume knob to about three quarters, and leave it there for the whole session.
4. Set the level: send `TK09_SAW_OPEN.syx`, play a low C and set the interface's gain so it peaks around −12 dB.
   Don't change the gain after this; the takes are compared with each other.

## Each take

1. Send the take's `.syx` file to the Bass Station II. Its display or Components shows the name, such as TK01 RESO
   TRACK.
2. Don't touch any knob: moving one changes the test sound.
3. Start a new recording named after the take: `TK01.wav`, `TK02.wav` and so on.
4. Play what the table says, then stop recording.

Timings are approximate: "C2 3 s" means hold that C for about three seconds, then let go and wait about a second.
How hard you play doesn't matter, since every test patch ignores velocity.

C1 to C5 are five Cs an octave apart. Exact names don't matter: start from a low C, using the octave buttons to
reach it, and go up an octave each time. Takes 1 to 3 have a faint sine in them that shows which key was played.

If a take goes wrong, record it again; the last recording counts.

## The takes

<!-- takes -->
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
<!-- /takes -->

## One more thing: a patch dump

This settles one detail of how the Bass Station II stores its patches.

1. Send `TK09_SAW_OPEN.syx` again.
2. Set oscillator 1's wave to pulse.
3. Set oscillator 1's mod envelope pulse width depth to about +20, and oscillator 2's to about −30. Write down the
   exact values the display shows.
4. Send the patch to the computer as a SysEx dump (Components' librarian, or the Bass Station II's own patch dump
   with the computer's SysEx program recording), and save it as `PWDUMP.syx`.

## Optional: your favourite sounds

If there's time, pick two or three patches you like, especially bass sounds. For each, save a dump of the patch
(`.syx`) and record a few bars playing it. They're great for comparing the two synths by ear.

## Send back

- `TK01.wav` to `TK25.wav`
- `PWDUMP.syx`, with the two values you wrote down
- The Bass Station II's firmware version
- Any favourite-sound pairs (`.syx` and `.wav`)
