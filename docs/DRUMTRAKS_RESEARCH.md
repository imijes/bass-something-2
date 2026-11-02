# Phase 0: a Drumtraks-inspired drum mode for Bass Something 2

Research notes (2026-11-01) for a second instrument on the FM-1, inspired by the Sequential Circuits Model 400
Drumtraks (1983). Nothing here is built yet.

**Sources read:**
- the Operation Manual CM400A (Nov 1983, 75 pages), including its MIDI Implementation of 17 Dec 1983;
- the Preliminary Service Data TN400-0 (Dec 1983: theory of operation, schematics SD400-1A and SD400-2A) and
  TN400-1 (1985, software update).

Both PDFs are scans, so they were OCR'd. Anything marked *(OCR)* should be checked against the page image.

**Not read:**
- the three YouTube walkthroughs (YouTube returned a captcha);
- the elektrotanya schematic PNGs (the service manual carries the same SD400 sheets).

Anything that needs listening (tuning range in semitones, decay lengths, swing feel) is marked **TO MEASURE**.

## 0. Corrections to the brief

- **There are no claves.** The 13 instruments are Bass, Snare, Rim, Tom 1, Tom 2, Crash Cymbal, Ride Cymbal,
  Closed Hi-Hat, Open Hi-Hat, Claps, Tambourine, Cowbell and Cabasa (manual p. v, spec 15-1, MIDI Table 7).
- **MIDI was not channel 10.** The factory basic channel is **15**, and the unit always powers up in **Omni On**.
  Channel 10 for drums came later, with GM. The 400's key map is nearly the later GM drum map, though (§3).
- **The Drumtraks has no step recording.** It records in real time, quantized by "Error Correct" (up to 1/96).
  A step grid would be an FM-1 extra, so it belongs on a mod plate, as in the BS2 UI.

## 1. Voice architecture and sound engine

### Hardware (TN400-0)
- **CPU:** Z80A at 4 MHz, with an 8 MHz crystal divided down.
- **Program:** in 2764 EPROMs.
- **Battery-backed RAM:** holds 7,680 bytes of song and pattern data (0x2392–0x3FFF).
- **Interrupts:** the tempo counter (from 4.8 kHz) drives INT; MIDI receive uses NMI.

**Six audio channels.** Each is a sample ROM read by a 12-bit counter, clocked by its own voltage-controlled
oscillator. The VCO rate *is* the tuning. Each channel has an 8-bit multiplying DAC, whose reference is the
channel's volume CV, so volume is applied in the DAC and costs no extra multiply.

| Ch | Instruments (one sounds at a time) | Extras in the channel |
|----|------------------------------------|-----------------------|
| 1 | Bass | VCF (low-pass) with its own decay envelope; its cutoff follows the tuning CV |
| 2 | Snare, Rim (one ROM; the top address bit picks) | none, the simplest channel |
| 3 | Tom 1, Tom 2 | as channel 1: a VCF that follows the tuning, plus an envelope |
| 4 | Crash, Ride | the longest sounds: four ROMs each, read in turn as the counter wraps |
| 5 | Closed HH, Open HH | the clock halved by a flip-flop; a closed hat cuts the open hat's volume (choke); a VCA-style volume pull-down at the end |
| 6 | Claps, Tambourine, Cowbell, Cabasa (2 ROMs × 2 sounds) | "similar to channel 1" (a VCF and envelope) |

- **Choke and voice stealing come from the channel layout.** "Only one instrument in each channel can be played
  simultaneously" (spec 15-1): a snare cuts a rim, a ride cuts a crash, a cowbell cuts a clap.
- **Samples:** 8-bit numbers in 2732/2764 EPROMs (4 or 8 KB each), so the sample ROM is roughly 100 KB.
  The sample rates aren't documented; they're the VCO clocks. **TO MEASURE.**
- **Tuning:** 16 steps (00–15) per instrument, set by holding the instrument key and turning PER INSTRUMENT
  TUNING. A lower CV means a faster clock and a higher pitch. The span in semitones isn't given. **TO MEASURE.**
- **Volume:** 16 steps (00–15), set the same way with PER INSTRUMENT VOLUME.

**Volume arithmetic (manual 14-4).** The final level of each instrument is 0–255 "units":

| Source | Contribution |
|--------|--------------|
| PER INSTRUMENT VOLUME | 0–175 (11 units a step, 16 steps) |
| ACCENT | +80 (so an accent still works at volume 15: 175 + 80 = 255) |
| Song volume change | ±16 steps × 8 units = ±128 |
| MIDI velocity | replaces the volume knob, quantized to the same 16 × 11 steps (so MIDI notes can still be accented) |
| MASTER VOLUME | analog, after the mix |

- **Metronome:** a separate pulse, accented on the first beat of a loop. It is mixed into AUDIO OUT only while
  recording.
- **Panel:** a mono mix, plus six channel outputs and a metronome out.

### What to take to the FM-1
- **Six voice slots with the same grouping.** This gives the original's choke behaviour (open hat cut by closed,
  snare cuts rim, and so on) and bounds the CPU at six voices.
- **The level per hit:** `level = min(255, vol*11 + accent*80 + song_dv*8)`, as an integer.
- **The tuning per hit:** 16 steps that set a playback-rate ratio. Channels 1, 3 and 6 also move the VCF cutoff.
- **The character:** 8-bit output with no interpolation, and a varispeed sample clock (zero-order hold, so the
  aliasing moves with the tuning). Most of the Drumtraks "sound" comes from this.

## 2. Sequencer and rhythm architecture

**Patterns:** 99 (00–98). Pattern 99 is always empty and acts as "stop at the end".
- Length: 1–99 measures (spec 15-1 says up to 100).
- Time signature: numerator 1–99; beat value 1/2, 1/4, 1/6, 1/8, 1/12, 1/16, 1/24 or 1/32. It can be set only on
  an erased pattern.
- New patterns start at Error Correct 1/16, swing 50 %, 4/4 and 1 measure.

**Timebase:** 24 PPQN (each event time is a 1/96 note).

**Recording:**
- Recording is real-time loop overdub only. The metronome clicks on each beat and is accented on the loop's
  downbeat. What you play is heard on the next pass.
- Each hit is quantized to the Error Correct value (1/2, 1/4, 1/6, 1/8, 1/12, 1/16, 1/24, 1/32, 1/48 or 1/96),
  which can be changed between overdubs. It affects only what is recorded from then on.
- **Erase Instrument:** holding an instrument key while the pattern loops erases that instrument's hits under the
  playhead. Erasing selectively means tapping on (or just before) the unwanted note.
- **Programmed volume and tuning:** each overdub records the instrument's *current* volume and tuning into its
  events. Hitting the keys once in RECORD MODE while stopped records them too.
- Accents (ACCENT held while hitting a key) cost less memory than programmed volume.
- Copy and Append work on patterns, including a pattern appended to itself or to one in a different time
  signature.

**Swing:** 50, 54, 58, 62, 66 or 70 %, stored with each pattern and applied on playback only. It moves the
off-beat 8th: at 66 % the first 8th lasts 2/3 of the beat. It is available only when the beat value is a quarter.

**Tempo:** 40–250 BPM in a non-uniform table of 128 values (manual 7-9; song data stores the index 0–127). The
default is 100. Song tempo changes are relative, in table steps (±15 per song step).

**Cueing:** choosing another pattern while one runs starts the new one at the end of the current loop. Cueing an
empty pattern stops playback exactly at the end.

**Songs:** 99 (00–98), each up to 100 steps.
- A step is a pattern, a relative volume change (±15), a relative tempo change (±15), empty or end.
- A song can also have an optional initial tempo.
- A song loops unless it ends on an empty pattern.
- Volume changes accumulate across loops.
- Songs support insert, delete, copy and append.
- The NEXT/REPEAT footswitch repeats the current pattern while it is held.

**Memory:** 3,289 notes, pooled across all patterns and songs. Usage shows as % remaining, and the display reads
"FULL" when it runs out.

**Pattern event format (Table 6):**
- A 3-byte header: the numerator, error correct and swing, beat value and measures.
- Normal events take 2 bytes: `tttttttt 00EAdddd`, a time byte, a clock-extend bit E, an accent bit A and the
  drum number d.
- Dynamic events take 3 bytes: the same plus `vvvvpppp`, a volume and pitch for that event.
- The END marker is `tttttttt 1000xxxx`.

**Song step bytes (Table 5):**

| Byte | Meaning |
|------|---------|
| `0ppppppp` | pattern number |
| `100vvvvv` | relative volume change |
| `101ttttt` | relative tempo change |
| `11100000` | empty step |
| `11000000` | end |
| byte 0 `0ttttttt` | initial tempo index (`10000000` = none) |

**Sync:**
- Clock in: 24, 48 or 96 PPQN, or MIDI clock (hidden function: hold both TEMPO buttons + ENTER, then DEC).
- Clock out: 24 PPQN square or 48 PPQN pulse, during playback only. External clock is ignored while recording.

**Hidden functions:**

| Combo | Function |
|-------|----------|
| hold INC + DEC | memory remaining |
| 7 + 8 + 9 + INC + DEC | clear all memory |
| ENTER + 0 (with INC/DEC) | MIDI channel |
| ENTER + 1 / 2 | 24 / 48 PPQN clock out |
| ENTER + 3 | program dump request |
| ENTER + 4 / 5 | pads out on MIDI on / off |
| ENTER + 7 / 8 | MIDI and clock loop tests |

**Fitting this to the FM-1:** keep the **event model** (a time plus a drum, accent, volume and pitch). Don't use a
16-step grid: an event list is what makes Error Correct, triplets, odd meters and "programmed tuning per overdub"
possible, and it is very compact. A step-grid *view* can be drawn from the events (an FM-1 extra, on a mod plate).

## 3. MIDI implementation (400, 17 Dec 1983)

**Receive:**
- **Note On:** velocity 1–127 is mapped to 16 volume levels. Note Offs (and velocity 0) are ignored.
- **Channel:** ignored in Omni On, which is the state at power-up. It is checked in Omni Off.
- **Mode messages:** Omni Off and Omni On, as the channel-mode controllers 124/125. These must arrive on the basic
  channel.
- **Song Select (00–98):** works only while stopped. It also switches the unit to song mode.
- **Start:** works in song mode only. It starts the current song and selects MIDI clock.
- **Clock:** obeyed while playing on MIDI clock.
- **Stop:** stops playback, after which clocks are ignored.

**Transmit:**
- Note On for pads, only when enabled (ENTER + 4). The velocity comes from the programmed volume; accents and song
  volume changes are not sent. Each Note On is followed at once by a Note Off (velocity 0, running status), five
  bytes per hit. The duplicate sounds are sent on keys 36, 40, 43, 44 and 47.
- Song Select.
- Start, sent immediately before the first clock.
- Timing Clock at 24 PPQN during playback.
- Stop.

**SysEx (Sequential's ID 01):**

| Message | Bytes |
|---------|-------|
| Pattern marker (after the first clock of each pattern, except at the start) | `F0 01 7F F7` *(OCR)* |
| Program dump | `F0 01 06` + the whole 7,680-byte memory as 15,360 nibbles (right-justified, LS nibble first) + `F7` |
| Dump request | `F0 01 00 F7` *(OCR)* |

The spec page also lists Song Position Pointer, Continue and System Reset, which the implementation sheet doesn't
cover (probably later firmware: the MIDIGUIDE MG400).

**No CCs.** Volume, tuning, tempo and swing can't be set over MIDI.

**Key map (Table 1):**

| Key | Drum | GM name for that key |
|-----|------|----------------------|
| 35, 36 | Bass | Acoustic / Bass Drum 1 |
| 37 | Rim | Side Stick |
| 38, 40 | Snare | Acoustic / Electric Snare |
| 39 | Claps | Hand Clap |
| 41, 43 | Tom 1 | Low Floor / High Floor Tom |
| 42, 44 | Closed HH | Closed / Pedal Hi-Hat |
| 45, 47 | Tom 2 | Low / Low-Mid Tom |
| 46 | Open HH | Open Hi-Hat |
| 49 | Crash | Crash Cymbal 1 |
| 51 | Ride | Ride Cymbal 1 |
| 54 | Tambourine | Tambourine |
| 56 | Cowbell | Cowbell |
| 58 | Cabasa | *Vibraslap* (GM Cabasa is 69) |

**Recommendation for the FM-1:**
- Use the 400's map as is, which is GM-compatible apart from Cabasa. Also accept 69 for Cabasa.
- Default to **channel 10** (the modern expectation). Keep a GLO MIDI option for "Omni (as the 400)".
- Add our own CCs for each voice's level and tune. The original has none, so this is an extra.
- Share BS2's existing clock handling (app.c around line 838 follows 0xF8), so the bass sequencer and the drums
  lock to one clock.

## 4. Integration blueprint

### Budget today (docs/SIZE_LOG.md, v0.6)
- **App flash:** 329,328 B of 581,564 used, so 252 KB free.
- **RAM:** 77,396 B of 98,304 used, so **20.9 KB free**. RAM is the tight budget.
- **Flash data area:** BS2 uses only its store objects (0xD8000–0xDFFFF). The data window 0x97000–0xD7FFF
  (**260 KB**) is unused, as are XDATA0 (16 KB) and XDATA1 (92 KB). These don't count against the 581 KB app slot.
  Writes are main-loop only and stop the audio for a moment, and bytes 0xF00–0xF07 of each XDATA sector must stay
  erased.

### Sound source

**Decided (user, 2026-11-01): A, plus B as a user feature.** The synthesized voices are built in. A user can upload
their own samples to replace any of them. **No samples ship with the firmware.**

The reference recordings are a commercial sample pack, "EpromTraks – Sequential Circuits Drumtraks Sample Set". It
covers every voice across its whole tuning range, aliasing included, recorded at 24-bit/96 kHz on a Focusrite
A16R. The "via 535-LA" files are each voice's direct output through an API 535-LA, which is near-clean and is the
set we measure. The "via M-A-S" files are the master out through an Overstayer M-A-S saturator, which colours them.
The pack is kept outside the repo at `../SequentialCircuitsDrumtraks/` and is used only to measure and compare:
pitch per tuning step, decay, accent level and spectrum. No audio from it goes into the firmware or the repo.
A second set, from angelspit.net/sci-drumtraks (2016; one hit per drum, at 44.1 kHz and 15.625 kHz), is **not**
licensed for reuse: its page gives no licence and says the samples came from "the internet" and were processed
("beefed up"). It is trimmed and processed, so it is at most a loose second reference.
The pack's own description treats aliasing as part of the sound, so the drum voices step through their rate with
no interpolation and no anti-aliasing, as the 400's counter-clocked ROMs do.

The options considered:
| Option | Flash | RAM | Notes |
|---|---|---|---|
| **A. Synthesized voices** that imitate the 8-bit, varispeed sound (sine sweeps for kick and toms; tone plus noise for snare and rim; 808-style six-square metal for the hats and cymbals; noise bursts through band-pass filters for clap, tamb and cabasa; two squares for cowbell), then 8-bit quantization and a zero-order hold at the tuned rate | ~6–10 KB code | ~0.5 KB | No licensing problem in a public MPL repo. Every parameter is tunable. Less authentic. **Recommended for phase 1.** |
| **B. The user's own samples**, 8-bit at about 20–25 kHz, ~100–150 KB, in the data area | 0 in the app slot | ~0.3 KB | The most authentic if they are good recordings. Needs a loader (`APP_USB_CMD`, then `plat_host_read` into `plat_data_write`, as LOOM-8 does). |
| C. Dumps of the original ROMs | ~100 KB | — | **Don't.** Sequential's copyrighted data, not shippable in this repo. |

### State partitioning
The drums run **alongside** the synth: render mixes both, and the mode switch moves only the keys and UI. A
Drumtraks with a bass line is the point, and both fit the budget. So there is no union with `patch_t` or the BS2
voice; the drum state is its own small block in `PLAT_POOL`.

```c
/* bs2_drum.c (new): the drum machine's half; app.c calls dt_render() inside app_render and dt_ui() for its pages */
enum { DT_BASS, DT_SNARE, DT_RIM, DT_TOM1, DT_TOM2, DT_CRASH, DT_RIDE, DT_CHH, DT_OHH,
       DT_CLAP, DT_TAMB, DT_COW, DT_CABASA, NDT };          /* the 400's drum numbers (Table 7) */
#define NDTCH 6u                                            /* its channels: one sound each at a time */
typedef struct { uint16_t t; uint8_t d, vp; } dt_ev_t;      /* a hit: tick (24 PPQN in the pattern), drum | accent<<7,
                                                             * volume<<4 | pitch (the 400's dynamic event) */
typedef struct {                                            /* a pattern's header (Table 6's prologue) */
    uint8_t num, den, meas, ec, swing;                      /* den/ec/swing as Table 7's indices */
    uint16_t n;                                             /* its events */
} dt_pat_t;
typedef struct {                                            /* per channel, audio only */
    uint32_t pos, inc;                                      /* phase and rate (Q16), from tuning */
    uint8_t drum, lvl;                                      /* playing; 0..255 units */
    float env, flt;                                         /* VCF envelope and state (ch 1, 3, 6) */
} dt_ch_t;
typedef struct {                                            /* DrumtrakState: ~2.7 KB at DT_MAXEV 512 */
    uint8_t vol[NDT], tune[NDT];                            /* the panel's 0..15 each */
    uint8_t pat, cue, song, mode, rec, ec_now, tempo_ix;
    int8_t song_dv;
    uint32_t tick;                                          /* the pattern's, shared clock */
    dt_pat_t hdr;
    dt_ev_t ev[DT_MAXEV];                                   /* the current pattern, edited in RAM */
    dt_ch_t ch[NDTCH];
} dt_state_t;
static dt_state_t dt PLAT_POOL;
```

**Persistence:** use a flat event pool with a pointer table, as the 400 does, but in our own data-area sectors
below 0xD8000 (for example 16 KB at 0xD4000). Load the current pattern into `dt.ev` and write it back on change
(the `glo_keep` debounce idea). Sizes:
- 32 patterns × up to 512 events × 4 B is 64 KB worst case, but it is pooled, so 16 KB holds about 4,000 hits:
  more than the 400's 3,289.
- 16 songs × 100 steps is 1.6 KB.

The `PLAT_STORE_MAX` (3,840 B) objects are too small for this, so it goes in raw sectors via
`plat_data_erase`/`plat_data_write`. It needs a small header with a magic and version, like `BANK_VERSION`.

**Lock-free handoff:**
- Pads, edits and cue changes go from the main loop to the audio thread through the existing `fifo_t` pattern
  (`keyq`).
- Recorded hits go back to the main loop through a `recq`-style FIFO, and the main loop inserts them into `dt.ev`.
- Inserting means sorting into a fixed array. The audio thread only reads `dt.ev` between a pattern's loops, or
  double-buffers a 2-byte index. No malloc anywhere.

**Cost estimate (option A):**
- **Flash:** +12–18 KB. That covers the engine, sequencer, song steps and three or four panel pages in the BS2 art
  style.
- **RAM:** **+3 KB**, which leaves about 18 KB free.
- **CPU:** six voices at most, each a few multiply-adds a sample, plus three one-pole filters. Check this on the
  device with GLO's CPU meter. Hats and cymbals with six-square banks are the costliest.

### Reuse (bs2 project rules)
- **The clock and tempo:** the BS2 arp's, with MIDI clock following already done. Extend the BPM range to 250.
- **Swing:** the BS2's ARP MODS swing becomes the 400's six values when in drum mode.
- **Velocity:** `APP_TOUCH` velocity is used directly as the 400's "MIDI velocity → 16 levels".
- **AFX** (app.c:73) already makes the keyboard a kit of variations. Look at it as a model for drum key handling.
- **REC and PLAY** keep their meanings. REC arms overdub (with the metronome); PLAY is RUN/STOP.
- **The UI:** `seg_display`/`seg7` give the 400's two-digit LED readouts, and `panel_bars`/`bs2_knob` give a
  "DRUMTRAKS-style" panel page.
- **Hold handling:** `HOLD_MS`.
- **The SysEx framing:** bs2_sysex.c's (for an optional 400-format program dump/import, `F0 01 06`, as a later
  phase).

### Keys and controls in drum mode
The FM-1 has 27 keys, F3..G5.

- **The 13 drums from the low C up the white keys (C4 → F5, Bass .. Tambourine), then Cowbell on F#5
  and Cabasa on G5**,
  in the 400's panel order; the bass drum on a C as in General MIDI. The black keys give the
  duplicates (as MIDI's "fast rolls" keys) or stay free.
- **Volume and tuning:** hold a drum key and turn K1 (VOLUME 0–15) or K2 (TUNING 0–15). That is the 400's own
  gesture, and the value shows on the seg display.
- **Accent:** velocity, or SEL held as the ACCENT button.
- **Erase Instrument:** hold FX (or ALGO pushed) while the drum key is held.
- **Encoders:** PRESET selects or cues the pattern, SELECT the pattern function (the 400's SELECT PATTERN
  FUNCTIONS list: Error Correct, Swing, Time Sig, Measures), and ALGO changes its value (the INC/DEC).

**The 400's panel** (from a photo kept outside the repo, a reference only: the pages are drawn our own way):
- Top row, left to right: CASSETTE (SAVE, LOAD) over TEMPO (SLOWER, FASTER); SELECT SONG FUNCTIONS with its LED list
  (SONG#, ERASE SONG, COPY/APPEND TO, BUILD MODE and under it PATTERN#, VOL CHANGE, TEMPO CHANGE, INSERT, DELETE) and
  ENTER; two red two-digit LED displays over a keypad (1–9, ◄ 0 ►); SELECT PATTERN FUNCTIONS with its LED list
  (PATTERN#, RECORD MODE and under it ERASE INSTRUMENT and ERROR CORRECT, SWING %, TIME SIGNATURE, # OF MEASURES,
  ERASE PATTERN, COPY/APPEND TO); METRONOME VOLUME and MASTER VOLUME knobs (0–10).
- Bottom row: VOLUME and TUNING knobs, labelled "PER INSTRUMENT", with RUN/STOP (and its LED) and ACCENT; then the
  13 drum buttons in boxes CH 1 (BASS), CH 2 (SNARE, RIM), CH 3 (TOM1, TOM2), CH 4 (CRASH, RIDE: "CYMBAL"), CH 5
  (CLOSED, OPEN: "HI HAT"), CH 6 (CLAPS, TAMB, COWBELL, CABASA). The channel boxes are the voice channels, as
  `bs2_drum.c`'s DTC_*.
- The look: a black panel, white rounded outline boxes grouping the controls, small white capitals, red LEDs and
  displays, wood end cheeks. A drum page can echo this (boxes per channel, an LED list per function menu, two
  two-digit readouts) in the BS2's own skeuomorphic style.

### Mode switch
**Decided (user, 2026-11-01): HOME held 1 s** (`HOLD_MS`) toggles BASS ⇄ DRUMS. Going to the drums shows a
"Drum Something" splash, then the drum mode.
- It is free (HOME has only a tap), and it matches the existing hold idiom (ARP or SEQ held: AFX; GLO held: RND;
  REC held: random).
- HOME tapped still goes to the current mode's home page.
- Show it with the Hints line and a short banner.
- The other mode's sound keeps running; the keys and pages follow the mode.

An alternative is OCT− and OCT+ pressed together. It is faster but easy to hit by accident.

## 5. Open questions before phase 1
1. ~~Option A, B or both, for the sound source.~~ Decided: synthesized, plus user-uploaded samples; none supplied.
2. Pattern and song counts on the FM-1. The 400 has 99 patterns (00–98; 99 always empty, the stop), 99 songs
   (00–98) of up to 100 steps, in one pooled memory of 3,289 notes. Proposed: match it exactly, since only the
   current pattern lives in RAM and the pool is in data-area flash. (Not objected to, 2026-11-01: going ahead.)
3. ~~Drums with the bass, or exclusively?~~ Decided (user, 2026-11-01): two instruments on the same MIDI and audio
   paths, played independently and together.
4. ~~The name.~~ Decided (user, 2026-11-01): **Drum Something**, to pair with Bass Something 2.
5. ~~Measurements needed from recordings.~~ First pass done (2026-11-01): `tools/dt_measure.py` →
   `docs/DT_MEASURE.md` (the 535-LA set). What it shows:
   - **Tuning is varispeed, and the span differs by voice.** From step 1 to the top step the rate rises about 1.9×
     (Rim, the hats), 2.1–2.8× (Claps, Snare, Tamb, Cabasa) or 4.5–5× (Tom, Ride, Cowbell, Crash): roughly
     0.75, 1.1 or 1.75 semitones a step. The two methods (spectrum shift, decay length) agree on most voices.
   - **Bass is the exception.** Its partial goes 44 → 89 Hz (one octave), but its spectrum and decay don't scale
     the same way: the VCF and envelope on channel 1 don't follow the rate one to one. Model it as a pitched
     voice with its own filter/decay, not a pure varispeed sample.
   - **The hats are likely one sample.** Closed and open have the same partial (7.2 kHz at step 1) and the same
     rate steps. The open hat plays to the end of its sample (about 330 ms at step 1, a hard stop), while the
     closed hat's length stays about 120 ms at every step: a fixed gate or VCA, not the sample's end. So one hat
     voice with two envelope lengths, and the closed hit choking the open one.
   - **Accent is about +3.3 dB** on every voice, with no change in pitch or decay.
   - **Lengths at step 1 (−40 dB):** Bass 170 ms, Snare 235, Rim 145, Tom 1.2 s, Claps 360, Tamb 365, Cabasa 170,
     Cowbell 180, Crash 3.0 s, Ride 2.8 s.
   - Not yet known: which step is the 400's default for each voice (the manual's factory tunings), and the top
     steps where the spectrum is mostly aliasing (Claps 14–16, Crash 13/17/18 read low there).
6. ~~Transport with two instruments.~~ Decided (user, 2026-11-01): PLAY in each mode starts and stops that
   instrument; a GLO setting "PLAY starts both" (off by default) links them; MIDI Start/Stop always starts and stops
   both. One shared tempo and clock. Drums receive on MIDI channel 10 by default, the bass on its own channel.
