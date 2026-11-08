# Space Echo (RE-201) roadmap

This is a plan for a Roland RE-201 Space Echo model on the FX bus: a tape echo with three heads and a spring reverb,
as one effect, sitting beside the room (which gives the drums their tails). The sources stay outside the repo:
the Gemini research and the Roland manuals in `~/Documents/VibeCoding/re-201-ref/`.

## Phase 0: what the sources say

The sources are the RE-201 owner's manual (Jun '82), the RE-201/101 service notes (2nd ed., 30 Jul 1978) and the
RE-20 and RE-202 manuals (Roland's own models of the RE-201). The Gemini research gets several points wrong; the
corrections are marked ✗.

**Mode chart** (owner's manual; the research had modes 7, 9 and 10 wrong ✗):

| Mode | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Heads | 1 | 2 | 3 | 2+3 | 1 | 2 | 3 | 1+2 | 2+3 | 1+3 | 1+2+3 | – |
| Spring | | | | | ● | ● | ● | ● | ● | ● | ● | ● (spring only) |

**Signal flow** (from the service notes' block diagram):
- Every selected head goes into **one shared preamp**, the "recovery" preamp.
- That preamp's output splits two ways:
  - **Intensity** (feedback) back into the recording preamp, summed with the input.
  - **Echo Volume → Tone (Bass/Treble) → output.**
- **So the EQ is outside the feedback loop ✗.** It colours what you hear of the echo, not the build-up of the
  repeats. The owner's manual agrees: Bass/Treble act on "echo sound only". What darkens each repeat is the tape and
  the heads, not the knobs. A self-oscillation's colour comes from the tape path.
- **The spring is fed with the dry input only,** taken before the tape's feedback is summed in. The echoes never go
  into the spring. Reverb Volume and the mode switch (SW1a) bring it to the output.
- The motor is a DC brushless motor with its own speed circuit. Repeat Rate sets its speed.

**Numbers:**
- **Tape speed:** 12–40 cm/s, approx (service notes).
- **Tone:** ±10 dB at 100 Hz (bass) and at 5 kHz (treble) (service notes).
- **Intensity:** the factory trims it (VR12) so that self-oscillation starts at about 7 on the knob. The manual says
  to stay under 6–7.
- **S/N:** 60 dB, A-weighted. This only matters if we add hiss.
- **Preamp:** "one transistor, at an oddball 17 V" (Catalinbread). The input stage is a weak load, and a buffer in
  front makes it "richer and fuller".
- **Head spacing:** Roland's own models (RE-20, RE-202) use exactly 1 : 2 : 3. The research's table (69/131/189 ms)
  gives about 1 : 1.9 : 2.75. Neither source measured a real unit.
  - **Decision:** start at 1 : 2 : 3, as Roland does, with the ratios as constants, and correct them from a
    reference recording (a click through mode 11 gives all three heads).
- **Delay range:** serviced units reach about 600 ms in all (Soundgas). That doesn't contradict the research's
  69–177 ms on head 1, i.e. 207–531 ms on head 3 at 1 : 2 : 3, but every unit differs (four motor types, wear, no
  trim).
- **Head levels:** the ideal is all three heads at an equal level (the head-alignment step in the service notes). The
  one shared preamp sets the gain; more gain means more hiss and harder feedback (Soundgas).

**Measured from AudioThing Outer Space 2** (2026-11-08). These come from the plugin, rendered offline with
`re-201-ref/render_refs.py` and analysed with `analyse_refs.py`. Settings: tape RT, PreAmp on, Low/Hi Cut open, no
noise or drops, everything else at Init.
- **Head times:** slow 175 / 340 / 500 ms, mid 118 / 221 / 338 ms, fast 60 / 115 / 175 ms. That makes the ratios
  **1 : 1.91 : 2.88** (spread ±0.03), **not 1 : 2 : 3**, and close to the research's table. Head 1 runs 60–175 ms,
  so head 3 reaches at most 0.5 s.
  - **Decision:** `SE_HEAD2 = 1.91`, `SE_HEAD3 = 2.88`.
- **Head levels:** all three are equal, about −8.5 dB under the dry signal at Echo Volume 5.
- **Motor:** the delay follows the knob as a first-order lag with **τ ≈ 0.33 s**. A jump from slow to fast bends the
  pitch +22% at first, and it's back to true within about 1.5 s. The plugin's own "Rate Ballistic" reads 2.0 s, which
  is its settling time.
- **One trip through the tape** (dB relative to 1 kHz):
  - **Lows:** about **−4 @100 Hz, −11 @50 Hz, −20 @30 Hz**, the same at every speed (a 2nd-order high-pass near
    90 Hz).
  - **Highs:** fall with tape speed: slow −10 @5 kHz / −24 @10 kHz; mid −4 / −12; fast −2.7 / −7.5. The corner
    moves roughly in proportion to the speed.
- **Saturation:**
  - **Low level:** the echo path is about −2.4 dB.
  - **H2** sits at a steady **−31 dB** relative to the fundamental at every level, an asymmetry that scales with the
    signal (e.g. x + 0.07|x|).
  - **H3** rises 2 dB per dB of input (a cubic soft clip), from −80 dB at −36 dBFS to −16 dB at 0 dBFS.
  - **Compression:** it starts about −9 dBFS in, and the output ceiling is about −5 dBFS with hot input.
- **Intensity, per repeat:** 3 → −9.8 dB, 5 → −5.0, 6 → −3.4, 7 → −2.0. Extrapolated, unity falls near 8.3, and at
  8.5 it barely decays. That's gentler than Roland's trim, where it runs away at about 7.
  - **Decision:** follow the service manual and put unity near 7.
- **The EQ is outside the loop in the plugin too:** with the runaway at Treble 0 and at Treble 10, the tails decay at
  the same rate and meet.
- **Bass/Treble range:** the plugin goes to about **±20 dB** (at 100 Hz and 5 kHz), but Roland's spec says ±10 dB.
  - **Decision:** ±10 dB, unless it sounds too tame.
- **Wow and flutter:** the first figure here (0.13%) was wrong: it was the resolution of a coarse FFT. Measured
  properly, it's **0.012% rms** at the plugin's default (10%), and the knob goes with its square: 0.11% at 30%,
  0.49% at 60%, 1.33% at full. The wobble is random, spread over 1–7 Hz, with about a third of its power below
  4 Hz (wow) and two thirds above (flutter).
- **Spring** (long decay), in two types:
  - **Dark:** centroid 1.3 kHz, a 7 ms onset, RT60 about 2.3 s.
  - **Bright:** centroid 3.2 kHz, RT60 about 2.9 s.

**Still wanted:** a real RE-201 recording, if one turns up, to check the plugin against the hardware.

**JE-201** ([je3928/RE201models](https://github.com/je3928/RE201models)) is a university plugin.
- **Licence:** **GPL-3, so read none of its code.** Its README is fine as a list of methods:
  - Chowdhury's tape hysteresis (DAFx 2019) or an erf sigmoid;
  - a tone stack after Yeh (DAFx 2006) or Samad's WDF shelves;
  - tape-speed-dependent EQ;
  - control ballistics;
  - wow and flutter.
- **Its spring isn't a model of the RE-201:** it's either an impulse response of a *Boss RE-2* or a waveguide its
  author calls "robotic".
- **Use as a reference:** its built AU plugin is fine to listen to.

## The budget

The room uses 26.4 KB of pool (6,587 floats) and is already counted in the 165 KB free.

| | Beside the room (chosen) | If the Space Echo replaced the room |
|---|---|---|
| Tape: 0.52 s × 44.1 kHz × 16-bit (head 3 at most 0.5 s, plus wow) | 46 KB | 46 KB |
| Spring: 3 lines ≤ 45 ms + dispersion | 12–24 KB | 12–24 KB |
| **Pool left** | **~88–100 KB** | ~114–126 KB |
| RAM | ~0 (everything `PLAT_POOL`) | ~0 |
| CPU0 (~56% free) | Space Echo + room | Space Echo only |

The CPU is unknown until it's measured after each phase with `rmi_cpu.py`. The spring's dispersion chain is the big
cost.

## Licence

We are MPL-2.0. Write the code from published papers, not open-source plugins. ChowTape and JE-201 are both GPL-3, so
read none of their code. Papers:
- Välimäki, Parker & Abel, "Parametric spring reverberation effect" (JAES 2010);
- Parker, "Efficient dispersion generation structures for spring reverb emulation" (EURASIP 2011);
- Chowdhury, "Real-time physical modelling for analog tape machines" (DAFx 2019);
- Yeh, "Discretization of the '59 Fender Bassman tone stack" (DAFx 2006).

## Decisions (2026-11-08)
1. **The spring sits beside the room.** The room keeps the drums' tails.
2. **Repeat Rate keeps the original range** (head 1 about 69–177 ms, so about 0.53 s at most on head 3).
3. **Tempo sync is a mod** (as other mods are), not authentic, off by default.
4. **Mono**, as all the instruments are.

## Phases

Each phase ends with a host build, the tests, a device build, an install, a CPU reading and a size-log row.

### Phase 0: ground truth ✓ (except recordings)
- Manuals checked; findings above.
- Still to do: **reference recordings** (the user is looking). The most useful are:
  1. a click or snare hit through mode 11, at the slowest, the middle and the fastest Repeat Rate (head ratios and
     the delay range);
  2. a sustained sine or organ chord through mode 1 (wow and flutter);
  3. a Repeat Rate sweep while it repeats (motor lag);
  4. a click through mode 12 (the spring alone);
  5. a self-oscillation with Intensity at about 8.

### Phase 1: tape core (modes 1–4, echo only)
- `bs2_se.c`, beside `bs2_fx.c`, called from `fx_next`. Each instrument gets its own send, the way `fx_room_send`
  works.
- **Tape:** one `int16_t` ring of about 0.6 s in the pool, with one write head and three read taps at τ1,
  τ1·`SE_HEAD2` and τ1·`SE_HEAD3` (2 and 3 to start). Each tap reads through a 4-point Hermite; linear if the CPU
  needs it.
- The selected heads are summed into one preamp gain, as on the real unit.
- **Repeat Rate:** sets a target speed, and the speed follows through a one-pole lag (the motor), so turning it
  glides the pitch.
- **Intensity:** feedback from the summed heads into the record input, through a soft clip (a cheap tanh), calibrated
  so that self-oscillation starts at about 7 of 10.
- **Echo Volume** sets the echo's level out, after the feedback tap.
- **Host tests:**
  - an impulse in gives peaks at the head times;
  - full Intensity stays bounded, with no NaNs or denormals;
  - a Repeat Rate sweep glides without clicks.

### Phase 2: tape character
- **Input drive:** one transistor stage of asymmetric soft clipping, plus a peak lamp on the screen.
- **Tape and heads inside the loop:** a high-pass around 50 Hz and a speed-dependent low-pass (darker at a slower
  tape), so every repeat gets darker. This, not the EQ, colours a self-oscillation.
- **Bass/Treble:** RBJ shelves, ±10 dB at 100 Hz and 5 kHz, **on the echo's way out, outside the loop**.
- **Wow and flutter:** a slow LFO (0.5–2 Hz), a fast one (5–15 Hz) and filtered noise, all added to the tape speed so
  the heads move together. The depths are set from recording 2.
- **Optional hiss,** off by default.

### Phase 3: the spring (modes 5–12)
- **The Z-3F, three springs:** per spring, a chain of stretched first-order allpasses (the chirp), a delay of 27–43 ms
  and a damping low-pass in the loop, with the three summed. Start with 8–12 allpasses per spring, then measure.
- **It's fed with the dry send only** (not the echoes), as on the real unit, and the mode switches it in.
- **Mode 12** is the spring alone, with the tape stopped.
- **Reverb Volume.**

### Phase 4: the instrument side
- **A send per instrument** (bass, drums, RMI), each shown as a mod, as the room's are.
- **The tempo-sync mod:** Repeat Rate snaps head 1 to a note value, clamped to the original range.
- **A panel page** in the skeuomorphic style: the 12-position mode knob, Repeat Rate, Intensity, Bass, Treble, Echo
  Volume, Reverb Volume and Input, in the one cyan.
- **Storage and docs:** patch storage, SysEx dump bits, MIDI CCs and a MANUAL section.

### Phase 5: voicing
- A/B against the reference recordings, the JE-201 plugin and the AudioThing Springs RE-201 preset.
- Tune the head ratios, the wow and flutter, the loop filters and the self-oscillation by ear.
