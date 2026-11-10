# Bass Something 2 v0.8

Four instruments on one M-VAVE FM-1 now: the bass synth, Drum Something 400, a third instrument, Piano Something
368X, and Space Something 201, a tape echo with a spring reverb that every instrument sends into. The full guide is
`docs/MANUAL.md`.

## New instruments

- **Piano Something 368X:** an electric piano inspired by the RMI Electra-piano 368X, with tone tablets, ORGAN MODE and
  ACCENTER. It runs on the FM-1's second core, beside the bass and drums. Each drum pattern has its own piano part.
  Overdub it, and songs follow it. PIANO HOME and PIANO PANEL show it.
- **Space Something 201:** a tape echo inspired by the Roland RE-201 Space Echo. It has three heads, a modelled tape
  with wow, flutter and saturation, and a three-spring reverb tank. The bass, drums and piano each have a send into it.
  ECHO VOICE sets its voicing to taste. It has its own HOME and pages.
- **DRUM ROOM:** a room on the drums (FX, after DRUM MODS), with decay, tone, input tone and level.

## Live performance

- **Motion recorder:** records knob moves as a loop of whole bars on the instrument's clock. It works on the bass's
  Frequency, Resonance, arp rhythm and octaves, each drum's volume and tuning, the roll key, the drums' pattern changes,
  and the Space Echo's knobs and sends. Press REC + OCT- to record, REC + OCT+ to clear.
- **PROX:** a Bluetooth controller's or phone's distance from the FM-1 turns one knob, from touching (0) to arm's
  length (127). Pick the target on GLO CONNECT (PRESET), or with CC 4 from a controller. Tap SEL, sweep from near to
  far, and tap SEL again to calibrate. The motion recorder records it like a hand.
- **128 patches with names**, as on a Bass Station II (the PATCH NAME mod page).
- **Pitch bend, mod wheel and aftertouch:** pitch bend (2 semitones by default), the mod wheel for vibrato or the
  filter, and pressure from the keys or MIDI on the filter, pitch and LFO speed (FX, AFTERTOUCH and WHEELS).
- **Drum recording follows MIDI clock.**

## MIDI and connections

- **CC map:** no CC number is shared between instruments, so all of them can share one channel. The bass keeps the
  Bass Station II chart. The Space Echo's CCs are on the piano's CC channel, and drum roll is on CC 66/67.
- **USB host:** GLO CONNECT's USB HOST makes the USB port a MIDI host for a keyboard or synth.
- **USB audio, 6 channels:** the master on 1-2, then the bass, the drums, the piano and the Space Echo's return
  (checked in Logic Pro).
- **Bluetooth:** GLO CONNECT shows the connected device's name. GLO pulses slowly while Bluetooth has a link.
- **GLO pages regrouped:** MIDI CHANNELS, CONNECT, CLOCK, KEYS, AUDIO, DISPLAY and DUMP. They show both cores' loads
  (CPU0 and CPU1).

## Other changes

- **New package identity, FM-1_69:** FM-1_62 was shared with another firmware on the firmware hub. Releases are
  FM-1_69XXYYZZ (this one is FM-1_69000800).
- **The first-boot demo is removed.**
- **Built at -Oz:** about 28.9 KB less flash.
- **Shorter cymbal takes:** the crash and ride EPROM takes are cut at 2.5 s with a fade (16 KB less).

## Platform

- **`plat_ble_rssi`:** Bluetooth signal strength for PROX. It's a local patch (`platform/fm1-base-prox.patch`) until
  upstream merges it (charlesvestal/fm1-base#47). The released firmware includes it.
- **USB audio as the synth's Ext input:** still a local patch (fm1-base #17).

## Memory

The firmware image is 514,028 B, with about 66 KB (67,536 B) of flash and about 9.9 KB of RAM free. The costs of each
change are in `docs/SIZE_LOG.md`.

## Known limits

- With every Space Echo knob turned up in its fullest mode, CPU0 peaks at about 79%.
- The FM-1's speaker has one driver, so it plays a mono sum. The headphones stay in stereo.

## Install

- **From the browser:** Chrome or Edge, from the site's installer.
- **From a terminal:** `python3 tools/fm1_install.py bass-something-2-0.8.fwsc`.
- **RemoteFM1 (iOS):** use `bass-something-2-0.8.rfm1`.

Novation and Bass Station are trademarks of Focusrite Audio Engineering Ltd. Bass Something 2 is not affiliated with
Novation, Sequential, Roland, RMI or M-VAVE.
