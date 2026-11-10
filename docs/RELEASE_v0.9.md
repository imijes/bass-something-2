# Bass Something 2 v0.9 (draft)

Not released yet: this collects what's on `drum-something` since v0.8. The full guide is `docs/MANUAL.md`.

## Bluetooth

- **Bluetooth audio:** the FM-1's sound plays through Bluetooth headphones or a speaker. On GLOBAL AUDIO, ALGO picks a
  device (the last four used first, then the ones a search found) and SEL connects it or hangs up. While it streams, the
  FM-1's speaker and headphone jack go quiet; USB audio carries on.
- **PROX follows headphones:** with no Bluetooth MIDI link, PROX reads the headphones' link, so the knob follows your
  head.
- **A new Bluetooth stack:** Henri David's, from Optimist, which brings Bluetooth audio. Bluetooth MIDI now has one link
  at a time: a computer or phone, or a device the FM-1 connects to (REC on GLOBAL CONNECT), not both at once.
  Headphones can be connected beside either. PROX's signal reads differently on it: calibrate PROX again (SEL on
  GLOBAL CONNECT).

## Sound

- **More room for the sound:** the Space Echo, its spring reverb and the drums' room run on the FM-1's second core,
  beside the piano. Everything at once (song mode, all three instruments, a runaway echo, the motion recorder) loads the
  first core about 64% (was about 89%).

## Screen

- **The buttons light dimly where they do something** in the instrument you're playing, brightly while on.

## Docs

- The manual's HOME pictures are animated: each instrument's HOME with a knob's dive.
- USB host mode gives no USB power: noted in the manual.
