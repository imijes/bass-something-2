# Size log

What each change costs on the FM-1, from the device build (`tools/build.py bs2`): the flash image (its slot holds
581,564 B), RAM (.data + .bss, 98,304 B) and the pool (344,064 B). A row a change, added by
`python3 bs2/tools/size_log.py "what changed"` from the fm1-base root; Δ is against the row before.

| Date | Commit | Change | Flash | Δ | Flash free | RAM | Δ | RAM free | Pool |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2026-10-31 | 6639cf3+ | Baseline: v0.3 + RND remote, hold buttons, RND full-screen pages, control labels, value pop-ups, Manual Mode, GLOBAL DISPLAY, flat panel grey | 304,316 | — | 277,248 | 83,060 | — | 15,244 | 96,008 |
| 2026-10-31 | 1750657+ | Code cleanup and RAM optimization pass | 303,500 | -816 | 278,064 | 77,300 | -5,760 | 21,004 | 101,760 |
| 2026-10-31 | 1750657+ | K1-K4 right to left; SELECT/ALGO/PRESET by position (per-page encoder map) | 303,660 | +160 | 277,904 | 77,300 | +0 | 21,004 | 101,760 |
| 2026-10-31 | 1750657+ | SUB OSC + MIXER on EDIT (3rd); SEL opens MASTER, HOME free; Osc Select on SELECT, Sync on the PW page | 303,644 | -16 | 277,920 | 77,300 | +0 | 21,004 | 101,760 |
| 2026-10-31 | 81c188f+ | HOME page: the whole BS2 front panel to the footer keys' scale; HOME button, first page | 308,252 | +4,608 | 273,312 | 77,300 | +0 | 21,004 | 101,760 |
| 2026-10-31 | fdf92da | Env Select balanced; button hints over the keys | 308,300 | +48 | 273,264 | 77,300 | +0 | 21,004 | 101,760 |
| 2026-10-31 | 6695c8b+ | Knob Zoom: the panel's camera eases in on the turned encoder's control (2x), out 1 s after | 309,212 | +912 | 272,352 | 77,300 | +0 | 21,004 | 101,784 |
| 2026-10-31 | 6695c8b+ | Knob Zoom: HOME zooms on a MIDI CC's control (3x); GLO DISPLAY KNOB ZOOM AUTO/OFF | 309,500 | +288 | 272,064 | 77,300 | +0 | 21,004 | 101,784 |
| 2026-10-31 | 6695c8b+ | GLO DISPLAY: KNOB ZOOM at the left (PRESET's place), POP-UPS and MANUAL in K3's and K4's columns | 309,516 | +16 | 272,048 | 77,300 | +0 | 21,004 | 101,784 |
| 2026-10-31 | 6695c8b+ | GLO: DUMP the last page (MIDI, SYS, DISPLAY, DUMP) | 309,516 | +0 | 272,048 | 77,300 | +0 | 21,004 | 101,784 |
| 2026-10-31 | 6695c8b+ | ENV: Triggering on K4 | 309,548 | +32 | 272,016 | 77,300 | +0 | 21,004 | 101,784 |
| 2026-10-31 | 6695c8b+ | HOME CC: dive to the section page | 310,924 | +1,376 | 270,640 | 77,300 | +0 | 21,004 | 101,800 |
| 2026-10-31 | 6695c8b+ | HOME CC: one camera move into the section page; HOME's key line straight | 311,196 | +272 | 270,368 | 77,300 | +0 | 21,004 | 101,808 |
| 2026-10-31 | 6695c8b+ | GLO DISPLAY: Manual Mode now Hints (K3), Pop-ups on K4 | 311,228 | +32 | 270,336 | 77,300 | +0 | 21,004 | 101,808 |
| 2026-10-31 | 6695c8b+ | HOME scope (the synth's output) in place of ARP/BPM; footer: tempo, what plays | 312,176 | +948 | 269,388 | 77,332 | +32 | 20,972 | 102,832 |
| 2026-10-31 | 6695c8b+ | HOME scope 4 px lower (clear of the display) | 312,176 | +0 | 269,388 | 77,332 | +0 | 20,972 | 102,832 |
| 2026-10-31 | 6695c8b+ | OSC Waveform knob in the panel's order (sine, tri, saw, square, pulse) | 312,272 | +96 | 269,292 | 77,332 | +0 | 20,972 | 102,832 |
| 2026-10-31 | 6695c8b+ | HOME: the unsaved dot inside the display, by the digits | 312,272 | +0 | 269,292 | 77,332 | +0 | 20,972 | 102,832 |
| 2026-10-31 | 6695c8b+ | Type: GLO names level with the knobs' (switches +5 px), Filters' Overdrive/Resonance apart, LFOs' Delay on the names line, ARP's 40 off the edge, Tempo/Patch under the display | 312,272 | +0 | 269,292 | 77,332 | +0 | 20,972 | 102,832 |
| 2026-11-01 | c546dca+ | REC: tap records, hold 1 s random, SEQ REC n/32 | 312,416 | +144 | 269,148 | 77,332 | +0 | 20,972 | 102,832 |
| 2026-11-01 | c546dca+ | Install defaults (ch 1, CC 1, clock out, Hints on), section tap hints, HOME keys line | 313,008 | +592 | 268,556 | 77,364 | +32 | 20,940 | 102,832 |
| 2026-11-01 | c546dca+ | GLO DISPLAY: its text as wide as the bar, three lines | 312,976 | -32 | 268,588 | 77,364 | +0 | 20,940 | 102,832 |
| 2026-11-01 | c546dca+ | REC hint: tap/hold format, hold: Randomize | 313,008 | +32 | 268,556 | 77,364 | +0 | 20,940 | 102,832 |
| 2026-11-01 | c546dca+ | Hints: hold AFX mode, RND Synth Companion; RND pages' title | 312,624 | -384 | 268,940 | 77,380 | +16 | 20,924 | 102,832 |
| 2026-11-01 | c546dca+ | Randomizer pop-up: whirling knobs, tumbling RANDOMIZE label | 313,008 | +384 | 268,556 | 77,380 | +0 | 20,924 | 102,832 |
| 2026-11-01 | c546dca+ | Randomizer pop-up on top of HOME's scope, one place on every page | 313,024 | +16 | 268,540 | 77,380 | +0 | 20,924 | 102,832 |
| 2026-11-01 | c546dca+ | RND Synth Companion splash (160 px photo) | 364,384 | +51,360 | 217,180 | 77,396 | +16 | 20,908 | 102,832 |
| 2026-11-01 | c546dca+ | RND splash: own photo, RND Synth + FM-1 cut out (232x86) | 353,088 | -11,296 | 228,476 | 77,396 | +0 | 20,908 | 102,832 |
| 2026-11-01 | c546dca+ | RND splash: 256 colours (a byte a pixel), 208 px, RND SYNTH / COMPANION in the big print | 329,744 | -23,344 | 251,820 | 77,396 | +0 | 20,908 | 102,832 |
| 2026-11-01 | c546dca+ | RND splash: the RND's acrylic corners whole | 329,552 | -192 | 252,012 | 77,396 | +0 | 20,908 | 102,832 |
| 2026-11-01 | c546dca+ | RND splash drawn once while up (no flicker) | 329,424 | -128 | 252,140 | 77,396 | +0 | 20,908 | 102,832 |
| 2026-11-01 | c546dca+ | RND splash: the devices 6 px right (the FM-1's screen) | 329,424 | +0 | 252,140 | 77,396 | +0 | 20,908 | 102,832 |
