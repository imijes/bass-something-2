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
| 2026-11-01 | fcfb3b8+ | HOME: Transpose/Function under the keys' line (solid) | 329,424 | +0 | 252,140 | 77,396 | +0 | 20,908 | 102,832 |
| 2026-11-01 | fcfb3b8+ | HOME: flat face plate (no cheek shading) | 329,344 | -80 | 252,220 | 77,396 | +0 | 20,908 | 102,832 |
| 2026-11-01 | fcfb3b8+ | HOME: panel zoomed to equal margins, display to its right edge, scope 2 px shorter | 329,360 | +16 | 252,204 | 77,396 | +0 | 20,908 | 102,832 |
| 2026-11-01 | fcfb3b8+ | Keys strip centred (8 px each side), HOME panel's margin 8 to match | 329,360 | +0 | 252,204 | 77,396 | +0 | 20,908 | 102,832 |
| 2026-11-01 | fcfb3b8+ | HOME: no Transpose/Function buttons; manual's RND section opens on its splash | 329,328 | -32 | 252,236 | 77,396 | +0 | 20,908 | 102,832 |
| 2026-11-01 | 359b098+ | v0.6: version references | 329,328 | +0 | 252,236 | 77,396 | +0 | 20,908 | 102,832 |
| 2026-11-01 | 44d415f+ | Drum Something phase 1: Tom and Snare voices (bs2_drum.c), MIDI ch 10 | 330,960 | +1,632 | 250,604 | 77,540 | +144 | 20,764 | 103,240 |
| 2026-11-01 | 44d415f+ | Drum Something: Tom and Snare as resonator banks (modes from the recordings), VCF after the DAC, fresh noise each hit | 333,300 | +2,340 | 248,264 | 77,556 | +16 | 20,748 | 106,000 |
| 2026-11-01 | 44d415f+ | Drum Something: Toms' pitch falls in two stages (quick drop, then sinking through the tail) | 333,636 | +336 | 247,928 | 77,556 | +0 | 20,748 | 106,048 |
| 2026-11-01 | 44d415f+ | Drum Something: a small room (FDN) makes the drums' tails; Toms' body rings half as long | 334,772 | +1,136 | 246,792 | 77,556 | +0 | 20,748 | 129,856 |
| 2026-11-01 | 44d415f+ | Drum Something: Snare's wires broad (hiss, not ring), a smack, the wires rising | 334,996 | +224 | 246,568 | 77,556 | +0 | 20,748 | 129,904 |
| 2026-11-01 | 44d415f+ | Drum Something: Toms' VCF envelope (TN400-0), broad bloom; room shorter, darker, four diffusers | 335,172 | +176 | 246,392 | 77,556 | +0 | 20,748 | 135,088 |
| 2026-11-01 | 44d415f+ | Drum Something: measured Snare ROM rate (17.4 kHz), per-voice rates, Tom click, darker room, DAC option | 335,492 | +320 | 246,072 | 77,556 | +0 | 20,748 | 135,088 |
| 2026-11-01 | 44d415f+ | Drum Something: Snare's wire bands wider (no ring at the low tunings) | 335,492 | +0 | 246,072 | 77,556 | +0 | 20,748 | 135,088 |
| 2026-11-01 | 44d415f+ | Drum Something: Snare's first step in tune (5 % flat), less low-mid | 335,492 | +0 | 246,072 | 77,556 | +0 | 20,748 | 135,088 |
| 2026-11-01 | 44d415f+ | Drum Something: Snare's body longer at the three lowest tunings only | 335,572 | +80 | 245,992 | 77,556 | +0 | 20,748 | 135,088 |
| 2026-11-01 | 44d415f+ | Drum Something: voices as data (shared Tom voice), decay and ring helpers, Toms' body longer at the lowest tunings | 334,788 | -784 | 246,776 | 77,556 | +0 | 20,748 | 132,680 |
| 2026-11-01 | 44d415f+ | Drum Something: Rim and Bass voices; per-voice output filter, attack swell and gate; tools/dt_voice.py | 336,036 | +1,248 | 245,528 | 77,556 | +0 | 20,748 | 132,824 |
| 2026-11-01 | 44d415f+ | Drum Something: Bass's pitch fall and tuning from its first cycles (dt_voice.py --cycles, dt_compare --cycles) | 336,036 | +0 | 245,528 | 77,556 | +0 | 20,748 | 132,824 |
| 2026-11-01 | 44d415f+ | Drum Something: Bass's attack softer (short swell of the modes; the click apart) | 335,940 | -96 | 245,624 | 77,556 | +0 | 20,748 | 132,824 |
| 2026-11-01 | 44d415f+ | Drum Something: Bass's beater thud (fixed-pitch modes: mode flags DT_MW/DT_MF), stronger overtones | 336,260 | +320 | 245,304 | 77,556 | +0 | 20,748 | 132,824 |
| 2026-11-01 | 44d415f+ | Drum Something: Bass's thud lower (200 Hz), darker top (the user's pick of three) | 336,260 | +0 | 245,304 | 77,556 | +0 | 20,748 | 132,824 |
| 2026-11-01 | 99f1a81+ | Drum Something: Cowbell and Claps voices; repeated strikes, gate fall time, output high pass | 337,220 | +960 | 244,344 | 77,556 | +0 | 20,748 | 132,968 |
| 2026-11-01 | 99f1a81+ | Drum Something: Claps' strikes swell (no slapback); Cowbell's clang | 337,172 | -48 | 244,392 | 77,556 | +0 | 20,748 | 132,968 |
| 2026-11-01 | 99f1a81+ | Drum Something: Cowbell in tune at every step (dt_compare.py --pitch) | 337,172 | +0 | 244,392 | 77,556 | +0 | 20,748 | 132,968 |
| 2026-11-01 | 99f1a81+ | Drum Something: Claps the same shape at every step (high pass follows the tuning, fading strikes, --env) | 337,652 | +480 | 243,912 | 77,556 | +0 | 20,748 | 133,016 |
| 2026-11-01 | 99f1a81+ | Drum Something: Claps' strikes spread at random (no slapback) | 337,732 | +80 | 243,832 | 77,556 | +0 | 20,748 | 133,016 |
| 2026-11-01 | 99f1a81+ | Drum Something: Claps a little more room | 337,732 | +0 | 243,832 | 77,556 | +0 | 20,748 | 133,016 |
| 2026-11-01 | 99f1a81+ | Drum Something: Claps a hair more high-mids, more room | 337,732 | +0 | 243,832 | 77,556 | +0 | 20,748 | 133,016 |
| 2026-11-01 | d7f0e44+ | Drum Something: its own MIDI device (GLO DRUMS: channel and CC channel, CCs 33-58); Tom and Bass in tune | 338,612 | +880 | 242,952 | 77,556 | +0 | 20,748 | 133,024 |
| 2026-11-01 | d7f0e44+ | Drum Something: Snare 35 cents up (the user's pick of four) | 338,612 | +0 | 242,952 | 77,556 | +0 | 20,748 | 133,024 |
| 2026-11-01 | d7f0e44+ | Drum Something: Tom's body overtones louder at the lowest tunings (low_db) | 338,820 | +208 | 242,744 | 77,556 | +0 | 20,748 | 133,024 |
| 2026-11-01 | d7f0e44+ | Drum Something: Claps open up in the middle (a tail noise for the upper bands, DT_MT) | 339,572 | +752 | 241,992 | 77,556 | +0 | 20,748 | 133,120 |
| 2026-11-01 | d7f0e44+ | Drum Something: Claps a hair darker | 339,572 | +0 | 241,992 | 77,556 | +0 | 20,748 | 133,120 |
| 2026-11-01 | d7f0e44+ | Drum Something: Claps another hair darker | 339,572 | +0 | 241,992 | 77,556 | +0 | 20,748 | 133,120 |
| 2026-11-01 | 3ad0400+ | Drum Something: Tambourine and Cabasa; channel 6's VCF fixed (vcf_fixed) | 340,388 | +816 | 241,176 | 77,556 | +0 | 20,748 | 133,120 |
| 2026-11-01 | 3ad0400+ | Drum Something: jingle copies (DT_MC: the Tambourine's four pairs), 32 resonators a channel | 341,060 | +672 | 240,504 | 77,556 | +0 | 20,748 | 136,408 |
| 2026-11-01 | 3ad0400+ | Drum Something: Tambourine's six jingle pairs, tight (±3 %, 3 ms); 44 resonators a channel | 341,188 | +128 | 240,376 | 77,556 | +0 | 20,748 | 138,640 |
| 2026-11-01 | 3ad0400+ | Drum Something: Snare's three upper wire bands (1.7-3.2 kHz); low_db option on the Snare (off) | 341,284 | +96 | 240,280 | 77,556 | +0 | 20,748 | 138,640 |
| 2026-11-01 | 3ad0400+ | Drum Something: Snare's body louder (6 dB), not longer, at the lowest tunings | 341,284 | +0 | 240,280 | 77,556 | +0 | 20,748 | 138,640 |
| 2026-11-01 | 45726b6+ | Drum Something: open and closed hi-hats (one sample; the closed one through a real-time VCA: dt_voice_t vca_*) | 342,484 | +1,200 | 239,080 | 77,556 | +0 | 20,748 | 138,712 |
| 2026-11-01 | 45726b6+ | Drum Something: hi-hats' metal ringing purer (E) | 342,484 | +0 | 239,080 | 77,556 | +0 | 20,748 | 138,712 |
| 2026-11-01 | a22809c+ | Drum Something: Crash and Ride (all thirteen voices); dt_tune.py --ms | 343,796 | +1,312 | 237,768 | 77,556 | +0 | 20,748 | 138,712 |
| 2026-11-01 | a22809c+ | Drum Something: cymbals' tone (Crash brighter, Ride darker) and tuning by ear (dt_voice_t pitch) | 343,940 | +144 | 237,624 | 77,556 | +0 | 20,748 | 138,712 |
| 2026-11-01 | a22809c+ | Drum Something: cymbals tuned by ear (Crash +115 c, Ride +6 c), Ride's partials rebalanced and shorter | 343,940 | +0 | 237,624 | 77,556 | +0 | 20,748 | 138,712 |
| 2026-11-02 | a22809c+ | Drum Something: Crash tuned +94 c (by ear, at step 1) | 343,940 | +0 | 237,624 | 77,556 | +0 | 20,748 | 138,712 |
| 2026-11-02 | 605c31f+ | Drum Something: its mode (HOME held 1 s, splash), the keys its instruments, the drum HOME page (Drumtraks-style panel) | 346,888 | +2,948 | 234,676 | 77,556 | +0 | 20,748 | 138,768 |
| 2026-11-02 | 605c31f+ | Drum Something: the drum HOME's wooden cheeks, the 400's black face full height | 347,112 | +224 | 234,452 | 77,556 | +0 | 20,748 | 138,768 |
| 2026-11-02 | 605c31f+ | Drum Something: the drum HOME's cheeks and black face the plate's height only | 347,160 | +48 | 234,404 | 77,556 | +0 | 20,748 | 138,768 |
| 2026-11-02 | 605c31f+ | Drum Something: the drum HOME's panel a little smaller, as clear below as above | 347,288 | +128 | 234,276 | 77,556 | +0 | 20,748 | 138,768 |
| 2026-11-02 | 605c31f+ | drum keys from the low C (Cabasa on the top black key) | 347,304 | +16 | 234,260 | 77,556 | +0 | 20,748 | 138,768 |
| 2026-11-02 | c39ddb9+ | Drum Something: the drum HOME redrawn from the photo's measures (tools/dt_face.py): BS2 HOME's width, shared walls, rounded corners, silver-sided knobs, recessed keys, the keypad, little displays | 349,384 | +2,080 | 232,180 | 77,556 | +0 | 20,748 | 138,768 |
| 2026-11-02 | c39ddb9+ | Drum Something: drum HOME's print as white lines, the drumsomething logo, the little displays below their frame's line | 350,712 | +1,328 | 230,852 | 77,556 | +0 | 20,748 | 138,768 |
| 2026-11-02 | f587152+ | Drum Something: every button the drums', tap to select, K1-K4, Channel pages, key lights, Knob Zoom | 355,784 | +5,072 | 225,780 | 77,556 | +0 | 20,748 | 138,792 |
| 2026-11-02 | f587152+ | Drum Something: its sequencer (patterns, recording, Erase Instrument, Error Correct, metronome, cues, copy/append, saved) | 361,320 | +5,536 | 220,244 | 77,700 | +144 | 20,604 | 156,512 |
| 2026-11-02 | f587152+ | Drum Something: virtual EPROM playback, Snare and Crash tables (test) | 408,824 | +47,504 | 172,740 | 77,700 | +0 | 20,604 | 156,536 |
| 2026-11-02 | f587152+ | Drum Something: swing adjustable whenever | 408,920 | +96 | 172,644 | 77,700 | +0 | 20,604 | 156,536 |
| 2026-11-02 | f587152+ | Drum Something: clock master, MIDI Start/Stop, DRUM MODS plate with Swing Lock | 410,840 | +1,920 | 170,724 | 77,732 | +32 | 20,572 | 156,536 |
| 2026-11-02 | f587152+ | Drum Something: all thirteen voices from virtual EPROMs | 518,792 | +107,952 | 62,772 | 77,732 | +0 | 20,572 | 156,536 |
| 2026-11-02 | f587152+ | Drum Something: recorded as played, saved data repaired on load, PLAY and REC in time, length on ENV, function hints | 519,752 | +960 | 61,812 | 77,732 | +0 | 20,572 | 156,536 |
| 2026-11-03 | f587152+ | Drum Something: Bass drum +12 dB, TRANSPORT mod, REC steady | 520,072 | +320 | 61,492 | 77,732 | +0 | 20,572 | 156,568 |
| 2026-11-03 | e831006+ | Drum Something: songs and Build Mode, the manual's tempos, TEMPO on SELECT | 522,632 | +2,560 | 58,932 | 77,732 | +0 | 20,572 | 166,584 |
| 2026-11-03 | e831006+ | Drum Something: live voices on the host alone, Closed Hat shares Open's ROM, FONT_S out | 499,492 | -23,140 | 82,072 | 77,732 | +0 | 20,572 | 157,896 |
| 2026-11-03 | e831006+ | RND splash: 8 colours in an LZ4 block | 487,428 | -12,064 | 94,136 | 77,732 | +0 | 20,572 | 157,896 |
| 2026-11-03 | f5d32c7+ | Drum Something: PATTERN and SONG pages with the camera, GLO KNOBS on all its knobs, RND pages from the drums; splash ground black | 490,052 | +2,624 | 91,512 | 77,732 | +0 | 20,572 | 157,896 |
| 2026-11-03 | 99308e4+ | Drum Something: PATTERN and SONG pages as the faceplate, INFO plates | 494,520 | +4,468 | 87,044 | 77,748 | +16 | 20,556 | 157,896 |
| 2026-11-03 | 7d84e80+ | Drum Something: Channel pages and zoomed pages in the faceplate's style, no wood, smooth frames | 496,272 | +1,752 | 85,292 | 77,748 | +0 | 20,556 | 157,896 |
| 2026-11-03 | ce3e8f6+ | Drum Something: Channel pages keep the header and the left cheek | 496,372 | +100 | 85,192 | 77,764 | +16 | 20,540 | 157,896 |
| 2026-11-03 | ce3e8f6+ | Drum Something: the camera on the function SEQ or ARP lights | 497,076 | +704 | 84,488 | 77,764 | +0 | 20,540 | 157,896 |
| 2026-11-03 | d2beaff+ | Drum Something: Channel pages on the faceplate's own places, the next channel's rails, the header sliding in | 497,572 | +496 | 83,992 | 77,764 | +0 | 20,540 | 157,904 |
| 2026-11-03 | 233b1bf+ | Drum Something: the next channel's keys and print on the Channel pages | 497,796 | +224 | 83,768 | 77,764 | +0 | 20,540 | 157,904 |
| 2026-11-03 | 233b1bf+ | Drum Something: the active channel's plate a shade lighter | 497,844 | +48 | 83,720 | 77,764 | +0 | 20,540 | 157,904 |
| 2026-11-03 | 14fd38e+ | Drum Something: channels at their own widths; Build Mode PRESET step, ALGO pattern | 498,596 | +752 | 82,968 | 77,764 | +0 | 20,540 | 157,904 |
| 2026-11-03 | 7650a75+ | USB audio on macOS 15: MIDI on its own audio control interface | 498,644 | +48 | 82,920 | 77,764 | +0 | 20,540 | 157,904 |
| 2026-11-03 | 85f781b+ | EP0: a SETUP arriving before SentStall is handled is answered | 498,644 | +0 | 82,920 | 77,764 | +0 | 20,540 | 157,904 |
| 2026-11-03 | 73b5d98+ | HOME: K4 the filter's Frequency, Knob Zoom's dive to its knob | 497,444 | -1,200 | 84,120 | 77,764 | +0 | 20,540 | 157,904 |
| 2026-11-03 | 28dc8d8+ | SPEAKER (GLO SYS): the mono sum, BASS+ and OFF for the speaker alone, by the headphone jack (PB5) | 498,644 | +1,200 | 82,920 | 77,764 | +0 | 20,540 | 157,960 |
| 2026-11-03 | 28dc8d8+ | BASS+: its harmonics at a quarter of the rate (about 12 % of the CPU to about 2 %) | 498,692 | +48 | 82,872 | 77,764 | +0 | 20,540 | 157,968 |
| 2026-11-03 | aae80a5+ | GLOBAL pages by subject: SYSTEM split into CLOCK, AUDIO and CONTROLS | 499,540 | +848 | 82,024 | 77,764 | +0 | 20,540 | 157,968 |
| 2026-11-03 | aae80a5+ | GLOBAL CLOCK: a Tempo knob (K1); GLOBAL DRUMS: Channel on K4 | 499,764 | +224 | 81,800 | 77,764 | +0 | 20,540 | 157,968 |
| 2026-11-03 | 79332bf+ | The ! count leaves out flash writes in silence | 499,844 | +80 | 81,720 | 77,764 | +0 | 20,540 | 157,968 |
| 2026-11-03 | 342ca90+ | HOME held: the camera pans between the two HOMEs, for the splash (Drum Something's gone) | 499,296 | -548 | 82,268 | 77,748 | -16 | 20,556 | 157,976 |
| 2026-11-03 | 342ca90+ | HOME tapped on HOME switches between the instruments (a hold still does, from anywhere) | 499,424 | +128 | 82,140 | 77,748 | +0 | 20,556 | 157,976 |
| 2026-11-03 | 342ca90+ | Section buttons on the synth's HOME: the camera dives to the section's page | 499,632 | +208 | 81,932 | 77,748 | +0 | 20,556 | 157,976 |
| 2026-11-03 | 342ca90+ | A pan or randomizer effect skipped by a time stamped ahead of the frame's; FILTERS: Mod Env on K2, Resonance on K3 | 499,648 | +16 | 81,916 | 77,748 | +0 | 20,556 | 157,976 |
| 2026-11-03 | 342ca90+ | HOME's K3: the filter's Resonance, with the zooms | 499,696 | +48 | 81,868 | 77,748 | +0 | 20,556 | 157,976 |
| 2026-11-03 | 342ca90+ | HOME's ENV zoom on the section's left | 499,712 | +16 | 81,852 | 77,748 | +0 | 20,556 | 157,976 |
| 2026-11-03 | f5b1147+ | DRUM MODS: Swing Lock on K3, Transport on K4 | 499,664 | -48 | 81,900 | 77,748 | +0 | 20,556 | 157,976 |
| 2026-11-03 | 6845213+ | Drums' HOME K3, K4: the dive to the Channel page | 499,808 | +144 | 81,756 | 77,748 | +0 | 20,556 | 157,976 |
| 2026-11-03 | 6845213+ | Drum MIDI out: keys and patterns on the 400's note map | 501,088 | +1,280 | 80,476 | 77,780 | +32 | 20,524 | 158,000 |
| 2026-11-03 | 5ec7c5f+ | Backup and restore over USB (fm1-base's) | 504,884 | +3,796 | 76,680 | 78,356 | +576 | 19,948 | 161,840 |
| 2026-11-03 | abc85be+ | Drums: memory remaining on both OCTs | 505,172 | +288 | 76,392 | 78,388 | +32 | 19,916 | 161,848 |
| 2026-11-03 | abc85be+ | HOME from a section page: the zoom in reversed | 505,428 | +256 | 76,136 | 78,388 | +0 | 19,916 | 161,848 |
| 2026-11-03 | 57f94c3+ | Drums +12 dB in the mix; a soft knee at full scale | 505,876 | +448 | 75,688 | 78,388 | +0 | 19,916 | 161,848 |
| 2026-11-03 | 57f94c3+ | HOME's K2: the arp's Rhythm, with the zoom | 505,988 | +112 | 75,576 | 78,388 | +0 | 19,916 | 161,848 |
| 2026-11-03 | 57f94c3+ | Drums: a cut-off sound fades out, no click | 505,972 | -16 | 75,592 | 78,388 | +0 | 19,916 | 161,896 |
| 2026-11-03 | 57f94c3+ | Drums: ten voices tuned to the references' measures | 506,244 | +272 | 75,320 | 78,388 | +0 | 19,916 | 161,896 |
| 2026-11-03 | 46f1d8c+ | Drums: numbers and tempo one a click at any KNOBS | 506,292 | +48 | 75,272 | 78,388 | +0 | 19,916 | 161,896 |
| 2026-11-03 | 46f1d8c+ | Drums: PRESET's hint names the pattern or the song | 506,292 | +0 | 75,272 | 78,388 | +0 | 19,916 | 161,896 |
| 2026-11-03 | f32b696+ | Drum hints in the bass's form | 507,636 | +1,344 | 73,928 | 78,404 | +16 | 19,900 | 161,896 |
| 2026-11-04 | 7c69c52+ | Copy and append say what they did; no stuck FULL; bars counted from the length | 508,212 | +576 | 73,352 | 78,420 | +16 | 19,884 | 161,896 |
| 2026-11-04 | 7c69c52+ | Song copy says what it did and refuses what won't fit | 508,468 | +256 | 73,096 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 7c69c52+ | ERASE SONG works from song 99 | 508,468 | +0 | 73,096 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Grey cheeks on the bass pages, the drums' grain | 508,548 | +80 | 73,016 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Bass cheeks on HOME: the faceplate's height only | 508,564 | +16 | 73,000 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Bass cheeks on HOME move with the camera's pan | 508,564 | +0 | 73,000 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Cheek grain by the panel's rows, none jumping with y0 | 508,596 | +32 | 72,968 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Bass cheeks by section: FX right, SEL/ARP/SEQ left, ENV/LFO none | 508,644 | +48 | 72,920 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Bass section cheeks 4 wide; HOME's plate and cheeks one piece, the dive landing on them | 508,740 | +96 | 72,824 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Bass cheeks replaced by the case's rounded ends | 508,708 | -32 | 72,856 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Header tabs: FX first | 508,692 | -16 | 72,872 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Rounded ends on the bass HOME only | 508,596 | -96 | 72,968 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum Channel pages: the frames centred on the plate | 508,596 | +0 | 72,968 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Section dives: the cut in the move, a settle after, the map and hint once settled | 508,644 | +48 | 72,920 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum Channel pages: no header slide | 508,244 | -400 | 73,320 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum Channel pages: no header, the frames centred on the whole plate | 509,172 | +928 | 72,392 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum Channel pages: larger, in the header's rows | 508,692 | -480 | 72,872 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum Channel pages: fitted, the print readable | 508,244 | -448 | 73,320 | 78,420 | +0 | 19,884 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum EDIT steps through the drums; CH 6 in two pages | 508,164 | -80 | 73,400 | 78,404 | -16 | 19,900 | 161,896 |
| 2026-11-04 | 35d62b5+ | A dive's settle gives way to the camera's aim | 508,180 | +16 | 73,384 | 78,404 | +0 | 19,900 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum Channel pages: the gap back, CH 6 whole | 508,244 | +64 | 73,320 | 78,404 | +0 | 19,900 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum Channel pages: RUN/STOP, ACCENT, PER INSTRUMENT in the small print | 508,244 | +0 | 73,320 | 78,404 | +0 | 19,900 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum Channel pages: PER INSTRUMENT's bracket, the keys' names back | 508,324 | +80 | 73,240 | 78,404 | +0 | 19,900 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum Channel pages: the keys' names clear of the frame | 508,324 | +0 | 73,240 | 78,404 | +0 | 19,900 | 161,896 |
| 2026-11-04 | 35d62b5+ | Drum roll on the lowest F, at Error Correct's grid | 508,692 | +368 | 72,872 | 78,404 | +0 | 19,900 | 161,912 |
| 2026-11-04 | 35d62b5+ | ROLL on the SEQ list, a plate of its own: 1/2 to 1/32T | 509,524 | +832 | 72,040 | 78,404 | +0 | 19,900 | 161,920 |
| 2026-11-04 | 35d62b5+ | ROLL lit, stopped, arms recording | 509,524 | +0 | 72,040 | 78,404 | +0 | 19,900 | 161,920 |
| 2026-11-04 | 35d62b5+ | ROLL's plate smaller, its labels fit | 509,620 | +96 | 71,944 | 78,404 | +0 | 19,900 | 161,920 |
| 2026-11-04 | 35d62b5+ | ROLL's plate: its screws full size | 509,556 | -64 | 72,008 | 78,404 | +0 | 19,900 | 161,920 |
| 2026-11-04 | 35d62b5+ | ROLL's plate: the mods' label tape | 509,428 | -128 | 72,136 | 78,404 | +0 | 19,900 | 161,920 |
| 2026-11-04 | 35d62b5+ | ROLL's plate: its LED under the list's | 509,444 | +16 | 72,120 | 78,404 | +0 | 19,900 | 161,920 |
| 2026-11-04 | f302440+ | Drum Channel pages: no PRESET in the map | 509,428 | -16 | 72,136 | 78,404 | +0 | 19,900 | 161,920 |
| 2026-11-04 | f302440+ | Drum Channel pages: the header drops down as a knob turns | 509,700 | +272 | 71,864 | 78,404 | +0 | 19,900 | 161,928 |
| 2026-11-05 | 17fd537+ | fm1-base + felucca-bsp upstream update (2026-11-05) | 515,520 | +5,820 | 66,044 | 78,420 | +16 | 19,884 | 161,928 |
| 2026-11-05 | 14b74ec+ | Demo: P01 DUB PLATE, its roots line, drum pattern 00 (16 bars) | 516,512 | +992 | 65,052 | 78,420 | +0 | 19,884 | 161,928 |
| 2026-11-05 | dea535e+ | demo from the author's FM-1 backup | 516,496 | -16 | 65,068 | 78,420 | +0 | 19,884 | 161,928 |
| 2026-11-05 | 5a15fd8+ | snare and toms: the low take alone, read faster at every tuning | 493,776 | -22,720 | 87,788 | 78,420 | +0 | 19,884 | 161,928 |
| 2026-11-07 | 9791b3b+ | crash and ride cut at 2.5 s, faded over 0.4 s | 477,696 | -16,080 | 103,868 | 78,420 | +0 | 19,884 | 161,928 |
| 2026-11-07 | 1915bf5+ | room moved to the FX bus (bs2_fx.c), sent post-fader | 477,712 | +16 | 103,852 | 78,420 | +0 | 19,884 | 161,936 |
| 2026-11-07 | 5ca75f0+ | 128 patch slots (four bank pages) and patch names (PATCH NAME mod page) | 479,664 | +1,952 | 101,900 | 78,516 | +96 | 19,788 | 175,344 |
| 2026-11-07 | d6a6ac3+ | RMI 368X engine, no UI yet (bs2_rmi.c, rmi_tab.h; MIDI ch 3) | 494,268 | +14,604 | 87,296 | 78,852 | +336 | 19,452 | 175,344 |
| 2026-11-07 | d6a6ac3+ | per-instrument volumes (bass on K1: HOME, MASTER; RMI by CC 7), RMI level | 494,732 | +464 | 86,832 | 78,852 | +0 | 19,452 | 175,344 |
| 2026-11-07 | 23f62bb+ | RMI mode: RMI HOME, RMI PANEL, RMI MODS (voice, bass boost, organ, accenter, vol, velocity) | 498,544 | +3,812 | 83,020 | 78,916 | +64 | 19,388 | 175,384 |
| 2026-11-07 | 23f62bb+ | Electra-Something 368X HOME (header, decay bars, strip on the keys), pages from the photos | 499,152 | +608 | 82,412 | 79,188 | +272 | 19,116 | 175,384 |
| 2026-11-07 | 23f62bb+ | Electra pages from the photos: FRONT2 front panel on HOME, PANEL_CENTER close-up sliding to focus | 500,112 | +960 | 81,452 | 79,188 | +0 | 19,116 | 175,384 |
| 2026-11-07 | 23f62bb+ | Electra: measured key orders, harpsichord Organ Mode, Accenter | 503,024 | +2,912 | 78,540 | 79,188 | +0 | 19,116 | 175,384 |
| 2026-11-07 | 23f62bb+ | Electra hybrid: independent tone tablets (SEL cursor, PRESET on/off), PP -14 dB, LUTE filter, layers in phase | 503,272 | +248 | 78,292 | 79,252 | +64 | 19,052 | 175,392 |
| 2026-11-07 | 97af2ba+ | Electra on the second core (rmi_job), two-pass mix | 505,788 | +2,516 | 75,776 | 83,476 | +4,224 | 14,828 | 177,440 |
| 2026-11-08 | ac3bc9a | fm1-base and felucca-bsp upstream sync (update-2026-11-08) | 507,084 | +1,296 | 74,480 | 83,508 | +32 | 14,796 | 177,440 |
| 2026-11-08 | ac3bc9a+ | rmi_buf to the pool | 507,084 | +0 | 74,480 | 82,484 | -1,024 | 15,820 | 178,464 |
| 2026-11-08 | 720ef69+ | Space Echo phase 1: tape core (bs2_se.c), 44.8 KB tape in the pool; CPU0 45/49 (was 41/44) | 508,092 | +1,008 | 73,472 | 82,484 | +0 | 15,820 | 223,296 |
| 2026-11-08 | 5ac216b+ | Space Echo phase 2: tape filters by speed, input drive, wow/flutter, Bass/Treble; CPU0 48/52 | 510,876 | +2,784 | 70,688 | 82,484 | +0 | 15,820 | 224,992 |
| 2026-11-08 | 6a1da35+ | Space Echo phase 3: the three-spring tank (21 KB pool); CPU0 65/69 in mode 11 (heads + spring) | 512,412 | +1,536 | 69,152 | 82,484 | +0 | 15,820 | 246,416 |
| 2026-11-08 | 9222b25+ | Space Echo: the spring at half rate (10.6 KB pool back); CPU0 59/62 in mode 11 (was 65/69) | 512,364 | -48 | 69,200 | 82,484 | +0 | 15,820 | 235,792 |
| 2026-11-08 | a171529+ | GLOBAL RMI: the RMI's channels, ROUTE between it and the bass | 524,268 | +11,904 | 57,296 | 82,580 | +96 | 15,724 | 236,896 |
| 2026-11-08 | 95b9106+ | Piano art: FRONT1 HOME panel, PIANO PANEL at 0.5, PIANO PRESETS; Randomize on EDIT held | 525,868 | +1,600 | 55,696 | 82,580 | +0 | 15,724 | 236,896 |
| 2026-11-08 | 9ec5750+ | Piano part per drum pattern: REC/PLAY/undo/erase, saved with the drums | 530,220 | +4,352 | 51,344 | 82,772 | +192 | 15,532 | 254,328 |
| 2026-11-08 | e8ed799+ | ECHO VOICE: the Space Echo's voicing to taste, kept | 531,996 | +1,776 | 49,568 | 82,804 | +32 | 15,500 | 254,352 |
| 2026-11-08 | e8488b3+ | DRUM ROOM: the drums' room's decay, tone, input tone, level (FX) | 533,292 | +1,296 | 48,272 | 82,804 | +0 | 15,500 | 254,392 |
| 2026-11-09 | 526b859+ | Pitch bend (2 semitones) and the mod wheel (LFO 1 vibrato) from MIDI; randomize docs on hold EDIT | 533,580 | +288 | 47,984 | 82,820 | +16 | 15,484 | 254,392 |
| 2026-11-09 | b424e30+ | AFTERTOUCH and WHEELS pages (FX mods): pressure, bend range, mod wheel destinations as the Bass Station II's | 534,700 | +1,120 | 46,864 | 82,820 | +0 | 15,484 | 254,416 |
| 2026-11-09 | 45054bb+ | Motion recorder, first step: the bass's filter Frequency, REC + OCT- / OCT+, live loop on the bass's clock | 535,676 | +976 | 45,888 | 82,884 | +64 | 15,420 | 262,608 |
| 2026-11-09 | ef18d8d+ | Motion recorder: REC's own light, MOTION REC / n/m on the footer, the combo without the octave's hint | 536,316 | +640 | 45,248 | 82,884 | +0 | 15,420 | 262,608 |
| 2026-11-09 | 04ca6a4+ | Motion recorder: drums' VOLUME, TUNING and the roll key (replayed rolls not recorded); clear per instrument | 537,228 | +912 | 44,336 | 83,044 | +160 | 15,260 | 262,616 |
| 2026-11-09 | aa22a32+ | Motion recorder: drum pattern changes, cued again by the loop, back to its first at its top | 537,596 | +368 | 43,968 | 83,044 | +0 | 15,260 | 262,616 |
| 2026-11-09 | dd9d25b+ | Motion recorder: both combos on every page; REC + OCT+ clears the instrument on screen's, saying CLEARED or EMPTY | 537,724 | +128 | 43,840 | 83,044 | +0 | 15,260 | 262,616 |
| 2026-11-09 | 649d6c9+ | Space Echo: sends in the tape's units (SE_UNIT), tape store clamped | 537,596 | -128 | 43,968 | 83,044 | +0 | 15,260 | 262,616 |
| 2026-11-09 | 649d6c9+ | Space Echo before MASTER: sends undone into a 16-bit sample's units, return under MASTER | 537,772 | +176 | 43,792 | 83,044 | +0 | 15,260 | 262,616 |
| 2026-11-09 | 9b88010+ | motion: bass reso/rhythm, Space Echo knobs and sends | 537,756 | -16 | 43,808 | 83,140 | +96 | 15,164 | 262,616 |
| 2026-11-09 | 7f85f02+ | motion: arp octaves | 537,772 | +16 | 43,792 | 83,140 | +0 | 15,164 | 262,616 |
| 2026-11-09 | 1e0d757+ | USB audio: 6 channels (master, bass, drums, piano, echo) | 538,604 | +832 | 42,960 | 87,620 | +4,480 | 10,684 | 263,640 |
| 2026-11-09 | 085206a+ | GLO regrouped: CHANNELS (all six), CONNECT, KEYS (Local, Route); GLOBAL DRUMS and PIANO gone | 537,692 | -912 | 43,872 | 87,620 | +0 | 10,684 | 263,640 |
| 2026-11-09 | 85c7727+ | USB host: GLO CONNECT's USB HOST (K2), the device's name, its notes let go when lost | 541,148 | +3,456 | 40,416 | 88,084 | +464 | 10,220 | 263,640 |
| 2026-11-09 | eb0f231+ | First-boot demo removed (presets/demo.h, make_demo.py): P01 DUB PLATE, empty drums, v0.7's line | 540,204 | -944 | 41,360 | 88,084 | +0 | 10,220 | 263,640 |
| 2026-11-09 | c546738+ | CC map: no overlap across instruments (BS2 chart for the bass, piano/echo/drums moved), drum roll by CC 66/67, echo CCs on the piano channel, BLE device name on GLO CONNECT | 541,036 | +832 | 40,528 | 88,084 | +0 | 10,220 | 263,640 |
| 2026-11-09 | b489b01+ | GLO pulses slowly while Bluetooth has a link | 541,132 | +96 | 40,432 | 88,084 | +0 | 10,220 | 263,640 |
| 2026-11-09 | d4be3c3+ | unity_opt z (-Oz): CPU0 peak 62 -> 67 in the heavy case | 512,204 | -28,928 | 69,360 | 88,084 | +0 | 10,220 | 263,640 |
| 2026-11-09 | 3c0cdfc+ | PROX: the Bluetooth link's distance turns one knob (GLO CONNECT PRESET, SEL calibrates), plat_ble_rssi local patch | 513,820 | +1,616 | 67,744 | 88,116 | +32 | 10,188 | 263,640 |
| 2026-11-09 | 3c0cdfc+ | PROX: calibration sweep (SEL), Signal readout, CC 4 picks the target in spans, list reordered, drum volumes dropped | 514,028 | +208 | 67,536 | 88,116 | +0 | 10,188 | 263,640 |
| 2026-11-09 | 01e24a2+ | Button glow: only the buttons that do something in the mode (leds_out, btn_live) | 513,980 | -48 | 67,584 | 88,116 | +0 | 10,188 | 263,640 |
| 2026-11-09 | 780ce8b | Bluetooth audio trial (own stack, A2DP, GLO AUDIO), PROX on the own stack + headphones, FX bus on the second core (bt-audio-trial) | 466,304 | -47,676 | 115,260 | 72,656 | -15,460 | 25,648 | 254,824 |
| 2026-11-10 | ac26505 | Bluetooth audio New search + Forget, PROX settle (v0.8.1) | 466,816 | +512 | 114,748 | 72,672 | +16 | 25,632 | 254,824 |
