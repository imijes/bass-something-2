# Bass Something 2 v0.7

Two instruments on one M-VAVE FM-1: the bass synth, inspired by the Novation Bass Station II, and Drum Something, a drum
machine inspired by the Sequential Circuits Drumtraks. The full guide is `docs/MANUAL.md`.

## Two instruments

- **Bass Something 2:** the monosynth from v0.6, with two oscillators, a sub oscillator, filters, two envelopes, two LFOs,
  the arpeggiator and effects. It imports and exports Bass Station II SysEx patches, one at a time or as a bank of 32.
- **Drum Something:** thirteen synthesized voices, each played from its own virtual EPROM, tuned against recordings of
  the original by measurement and by ear. Each channel has its own volume and tuning. HOME switches between the
  instruments, and both play at once.

## Live performance

- **Sequence recorder:** up to 32 steps per sequence, with 4 sequences, recorded in the arp's rhythm.
- **Patterns and songs:** 99 drum songs of up to 99 steps each. A step plays a pattern, changes the volume or changes
  the tempo (±15 per step).
- **Drum recording:** Error Correct from 1/2 to 1/48, and a ROLL key with nine rates. Swing Lock and Transport are on
  DRUM MODS.
- **Drum MIDI out:** on the drum machine's own MIDI channel, using the 400's note map.
- **HOME shortcuts:** K2 sets the arp's Rhythm, K3 Resonance and K4 the filter's Frequency. Knob Zoom dives to each
  one's page.
- **Backup and restore over USB:** everything saved, from the site's Backup tab.

## Platform (fm1-base and felucca-bsp, 2026-11-05)

- **Fast install:** the loader writes only the sectors that changed, so a reinstall takes seconds, not minutes.
- **Newer flash chip:** FM-1s with the UC25HQ80 flash chip are supported, including recovery with `fm1_rescue`.
- **macOS 15 USB audio:** USB MIDI is now the last interface (`usb_midi_last`), so macOS 15 shows the FM-1 as an audio
  input and output alongside its MIDI port. Tested on macOS 15.6.1.
- **SysEx reliability:** received SysEx is queued upstream now. Banks of 32 arrive whole in both directions with no
  pacing, and a control request that arrives during a stall is answered (felucca-bsp #28).
- **Headphone jack:** fm1-base's own jack sense now decides what the speaker plays (SPEAKER, GLO AUDIO).

## Memory

The firmware image is 515,520 B, with about 66 KB (66,044 B) of flash and about 19.9 KB of RAM free. The costs of each
change are in `docs/SIZE_LOG.md`.

## Known limits

- The bank import keeps patches P01 to P32 only, and patch names are fixed ("Bass Smth 2 NN").
- The FM-1's speaker has one driver, so it plays a mono sum. The headphones stay in stereo.
- One platform patch is still not upstream: the computer's USB audio as the synth's Ext input (fm1-base #17), in
  `platform/`.

## Install

- **From the browser:** Chrome or Edge, from the site's installer.
- **From a terminal:** `python3 tools/fm1_install.py bass-something-2-0.7.fwsc`.
- **RemoteFM1 (iOS):** use `bass_something_2-0.7.rfm1`.

Novation and Bass Station are trademarks of Focusrite Audio Engineering Ltd. Bass Something 2 is not affiliated with
Novation, Sequential or M-VAVE.
