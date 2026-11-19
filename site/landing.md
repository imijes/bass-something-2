# Bass Something 2

**DAWless-in-a-box, inspired by my buddy's setup: Bass Station 2, Drumtraks, Electra-piano, B3 Organ, Space Echo, and acts as a Cyma Forma RND synth companion.**

![HOME tapped round the five: the camera panning from Bass Something 2's panel to Drum Something 400, Piano Something 368X, Organ Something B3, Space Something 201 and back](manual/img/screen-tour.gif "Five instruments on one FM-1")
![A walk round the organ: a chord, the 8' drawbar's dive, the Leslie's cabinet and its rotors speeding up to tremolo and back, the vibrato knob turned up close, and HOME again](manual/img/screen-walk.gif "A walk round the organ and its Leslie")

## How it started

It started with my buddy asking for a Novation Bass Station 2 clone for his FM-1, which I had convinced him to buy as a
pocket synth for his overseas travels. The Bass Station 2 is one of the centerpieces of his DAWless rig, along with his
Sequential Circuits Drumtraks 400 drum machine. So naturally, I had to add that to the mix next.

There were drums and bass, but it needed more. Piano. But what piano? We were talking about how Fela Kuti used to rock
hard on an RMI Electra-piano 368X. Added! It needs some FX. I'm a dub reggae fan, so I added a tape echo based upon the
legendary Roland RE-201 Space Echo.

Drum, Bass, Piano, Echo... It's almost there, but it's missing something obvious. My buddy is also a Hammond B3 player.
Blam! We got a B3 and Leslie now! But wait, that B3 needs to have some real MOJO... Done! A pristine Church Organ bathed
in holy water, or a dirty old Barn Find.

What about modifications? There are so many "hardware" modifications to each of the instruments. MIDI? So much MIDI! We
got TRS MIDI IN, USB MIDI IN / OUT, BLUETOOTH. USB Audio IN / OUT, BLUETOOTH Out. BLUETOOTH PROXIMITY TIED TO ALMOST ANY
MIDI PARAMETER?! WHAT?!

But that's not all! Every instrument can be assigned its own MIDI channel AND a completely different MIDI channel for
MIDI CC messages. Internal routing of the piano's MIDI notes to the bass synth, or the bass synth to the piano, or both
directions at once! Piano and B3 Organ record to the Drum's sequencer. DAW-like automation of knobs with the Motion
Recording feature, up to 64 bars on a separate loop track from the Drum's sequencer.

Unlike my buddy, I don't have any of this gear in my rig, but I do now. I also have a Cyma Forma RND synth in my DAWless
setup, so I added an RND synth companion as well. There's a bunch more cool stuff too, so [read the manual](manual/).

## What's in it

![Bass Something 2's HOME page, animated: the name, the red P01 display and the whole Bass Station II panel; then FREQUENCY turned, the camera diving into the filter's knob and back out to HOME](manual/img/screen-bass-home.gif "Bass Something 2 on the FM-1's screen")
![Drum Something 400's HOME page, animated: the panel with its two red displays; then SEQ tapped, the patterns' page, and HOME again](manual/img/screen-drum-home.gif "Drum Something 400 on the FM-1's screen")

Bass Something 2 turns the M-VAVE FM-1 into a monosynth inspired by the Novation Bass Station II: two oscillators and a sub,
ring modulation and noise, a Classic or Acid filter with drive, two envelopes, two LFOs, distortion, the Bass Station
II's 32 arp rhythms with its 32-step sequencer (four sequences) and 32 patches. Patches go to and from a Bass Station II editor as SysEx, and
MIDI comes in over USB, Bluetooth and TRS, and goes out over USB and Bluetooth, and the sound can play through
Bluetooth headphones or a speaker.

A second instrument plays beside the synth: Drum Something 400, a drum machine inspired by the Sequential Circuits
Drumtraks, with 13 synthesized drums, patterns, songs and MIDI out. A third, Piano Something 368X, is an electric piano
inspired by the RMI Electra-piano 368X, with its own sequenced part on each drum pattern, and a fourth, Organ Something B3, a tonewheel organ inspired by the
Hammond B-3 with its Leslie speaker, two manuals and its own part too. Space Something 201 is a tape echo and spring
reverb inspired by the Roland RE-201, and all four instruments can send into it.

It's also a companion for Cyma Forma's RND Synth (firmware 1.4): 32 seed slots with their locks, and a mixer for its
four tracks.

![Piano Something 368X's HOME page, animated: chords played, their notes ringing as bars over the RMI Electra-piano's front panel; then ORGAN MODE turned on and off, the camera diving into the tone tablets and back out](manual/img/screen-piano-home.gif "Piano Something 368X on the FM-1's screen")
![Organ Something B3's HOME page, animated: a chord played on the B-3 console with its drawbars; then the 8' drawbar turned, the camera diving into it and back out to HOME](manual/img/screen-organ-home.gif "Organ Something B3 on the FM-1")
![Space Something 201's HOME page, animated: its name and mode above the whole RE-201 front panel; then REPEAT RATE turned, the camera diving into the knob and back out](manual/img/screen-echo-home.gif "Space Something 201 on the FM-1's screen")
![The RND Synth companion, animated: GLO held on the bass's HOME, its splash, a patch arriving from the RND Synth kept in a seed slot, the slots scrolled, then PATCH's locks and TRACKS' mixer](manual/img/screen-rnd.gif "RND Synth companion")

Version {{VERSION}}. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [SysEx](sysex/): send Bass Station II patches to the FM-1 and back its patches up, from Chrome or Edge.
- [Manual](manual/): every page and control, the arp and the sequencer, patches, SysEx, MIDI.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/imijes/bass-something-2): [MPL-2.0](https://github.com/imijes/bass-something-2/blob/main/LICENSE). It builds inside an [fm1-base](https://github.com/charlesvestal/fm1-base) checkout, at bs2/.
- [Credits](manual/#s13): Bass Something 2 by John Brown ([@imijes](https://github.com/imijes)), built on the work of others;
  everyone is listed there.

M-VAVE and FM-1 are trademarks of their owners, as are Novation and Bass Station. Bass Something 2 is not affiliated with M-VAVE or Novation.
