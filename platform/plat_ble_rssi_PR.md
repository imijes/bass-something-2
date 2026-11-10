# Draft PR for fm1-base: `plat_ble_rssi(link)`

Branch `plat-ble-rssi` (local, off upstream `main` b0976f2; one commit). The diff is `fm1-base-prox.patch` beside this.

## Title

plat_ble_rssi(link): a connected Bluetooth link's signal strength, for apps that play with distance

## Description

Adds one call to `plat.h`:

    int plat_ble_rssi(int link);   /* dBm, 0: no link or not known; link 0 the central's, 1 the device's */

It returns the controller's own reading of a connected link's signal (`ble_vendor_get_peer_rssi`, already in
`btctrler.a`), for the computer or phone that connected to the FM-1 (link 0) or the Bluetooth MIDI device it joined
(link 1, `ble_central`). Readings are new about every 30-40 ms and move a few dB with nothing moving, so an app
smooths them.

What it's for: Bass Something 2's PROX turns a knob by how far a Bluetooth controller or phone is from the FM-1.
Measured on an FM-1 with a TE SP-1 (feldd firmware) on the device link: -22 dBm touching (sd 0.7 dB), about -48 at a
foot, about -50 at arm's length while moving (-68 held still), every sweep tracked at one second each way. A phone on
the computer's link read -36 at 30 cm and about -80 at 2-3 m.

- JieLi stack: `ble/midi/le_midi.c` `ble_midi_rssi` reads `con_handle` / `mc_handle`; `device/radio.c` gates it on
  the stack being ready and Bluetooth on.
- `ble_stack = "own"` and no Bluetooth: returns 0 (the own controller keeps no per-link reading yet).
- Host: `ble rssi DBM [LINK]` in a script sets it while that link is connected, for an app's tests.

No change for apps that don't call it (the unity build drops it). Built: the template (host); Bass Something 2 on
the device, host tests passing, and the readings above taken on the FM-1.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
