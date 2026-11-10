# Bass Something 2

Bass Something 2 turns the M-VAVE FM-1 into a monosynth inspired by the Novation Bass Station II: two oscillators and a sub,
ring modulation and noise, a Classic or Acid filter with drive, two envelopes, two LFOs, distortion, the Bass Station
II's 32 arp rhythms with its 32-step sequencer (four sequences) and 32 patches. Patches go to and from a Bass Station II editor as SysEx, and
MIDI comes in over USB, Bluetooth and TRS, and goes out over USB and Bluetooth.

A second instrument plays beside the synth: Drum Something 400, a drum machine inspired by the Sequential Circuits
Drumtraks, with 13 synthesized drums, patterns, songs and MIDI out. A third, Piano Something 368X, is an electric piano
inspired by the RMI Electra-piano 368X, with its own sequenced part on each drum pattern. Space Something 201 is a tape
echo and spring reverb inspired by the Roland RE-201, and all three instruments can send into it.

It's also a companion for Cyma Forma's RND Synth (firmware 1.4): 32 seed slots with their locks, and a mixer for its
four tracks.

![Bass Something 2's HOME page with a note playing: the BASS SOMETHING 2 name, the red P01 display, the scope drawing the wave, and the whole Bass Station II panel above the keys](manual/img/screen-home.png "Bass Something 2 on the FM-1's screen")
![Drum Something 400's HOME page: the panel, with two red displays above the keypad and the drums' buttons along the bottom](manual/img/screen-drum-home.png "Drum Something 400 on the FM-1's screen")
![Piano Something 368X's HOME page: its name, the tone toggle that's on, and the RMI Electra-piano's front panel above the keys](manual/img/screen-piano-home.png "Piano Something 368X on the FM-1's screen")
![Space Something 201's HOME page: its name and mode above the whole RE-201 front panel, the keys under it](manual/img/screen-echo-home.png "Space Something 201 on the FM-1's screen")

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
