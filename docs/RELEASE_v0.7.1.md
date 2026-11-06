# Bass Something 2 v0.7.1

A demo to play along with from the first power-on. Everything else is as in v0.7 (`docs/RELEASE_v0.7.md`).

## The demo

- **P01 DUB PLATE:** a new first patch, a dub bass: two detuned saws and a loud sine sub under the 24 dB filter, swept
  slowly by LFO 2, with some drive and distortion to carry on the speaker. It replaces SUB ZERO. Its tempo is 122 BPM.
- **Sequence 1:** an original two-bar roots line in G minor, in the Studio One manner: the 1 left empty, long low notes,
  a call and its answer, and a pickup run back into the top.
- **Drum pattern 00:** sixteen bars of dub techno at 122 BPM with a little swing. The kick plays quarter notes, and
  the 3 is accented with the rim; the hats are on the off-beats, with the cabasa after them. Every fourth bar has its
  own fill (toms, a snare roll, falling toms, claps and cowbell), with a crash on the bar after.
- **The drums' first settings for it:** the kick on tuning 5, the snare 6, the rim 7, the closed hat 15 and the open
  hat 5, both hats at volume 5, the other cymbals and percussion high (11), and the drums' MASTER at 48 so the bass
  sits with them.

Hold HOME to switch to the drums and press PLAY: the bass sequence starts with them. It's loaded only on a new FM-1
(or after a reset) and in the emulator, so nothing saved is changed by updating.

## Memory

The firmware is 516,528 B (+1,008), with about 65 KB of flash free.

## Install

- **From the browser:** in Chrome or Edge, from the site's installer.
- **From a terminal:** `python3 tools/fm1_install.py bass-something-2-0.7.1.fwsc`.
- **RemoteFM1 (iOS):** use `bass-something-2-0.7.1.rfm1`.

Novation and Bass Station are trademarks of Focusrite Audio Engineering Ltd. Bass Something 2 is not affiliated with
Novation, Sequential or M-VAVE.
