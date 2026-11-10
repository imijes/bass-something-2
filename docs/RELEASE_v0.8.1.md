# Bass Something 2 v0.8.1

Bluetooth headphones and speakers, PROX on them, and more room on the FM-1's first core. The full guide is
`docs/MANUAL.md`.

## Bluetooth

- **Bluetooth audio:** the FM-1's sound plays through Bluetooth headphones or a speaker. On GLOBAL AUDIO (GLO five
  times), ALGO picks a device (the last four used first, then the ones a search found) and SEL connects it or hangs up.
  While it streams, the FM-1's speaker and headphone jack go quiet; USB audio carries on.
- **New search and Forget:** past the last device ALGO lists, New search finds new ones (SEL); past that, a Forget for
  each remembered device (SEL twice).
- **An SP-1 running Tape Looper 4.0 works as a Bluetooth speaker:** pair it the same way and record the FM-1 into it.
- **PROX follows headphones:** with no Bluetooth MIDI link, PROX reads the headphones' (or the SP-1's) link, so the
  knob follows your head.
- **PROX waits for the list to settle:** scrolling PROX's targets on GLOBAL CONNECT leaves the knobs you pass alone; the
  one you stop on follows after 0.7 s.
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
- USB host mode: troubleshooting for a device that's never found (it needs a powered hub) and for a device's audio
  (host mode carries MIDI only).

The firmware is 466,816 B, with about 115 KB of flash and 25 KB of RAM free.
