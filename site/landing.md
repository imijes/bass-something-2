# Bass Something 2

Bass Something 2 turns the M-VAVE FM-1 into a monosynth inspired by the Novation Bass Station II: two oscillators and a sub,
ring modulation and noise, a Classic or Acid filter with drive, two envelopes, two LFOs, distortion, the Bass Station
II's 32 arp rhythms with its 32-step sequencer (four sequences) and 32 patches. Patches go to and from a Bass Station II editor as SysEx, and
MIDI goes in and out over USB, Bluetooth and TRS.

![Bass Something 2's MASTER page: the Volume knob, the red P01 display with the Org. Value arrows, Save and the Patch buttons, and the BASS SOMETHING 2 name](manual/img/screen-ready.png "Bass Something 2 on the FM-1's screen")

Version {{VERSION}}. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [SysEx](sysex/): send Bass Station II patches to the FM-1 and back its patches up, from Chrome or Edge.
- [Manual](manual/): every page and control, the arp and the sequencer, patches, SysEx, MIDI.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/imijes/bass-something-2): [MPL-2.0](https://github.com/imijes/bass-something-2/blob/main/LICENSE). It builds inside an [fm1-base](https://github.com/charlesvestal/fm1-base) checkout, at bs2/.
- [Credits](manual/#s12): Bass Something 2 by John Brown ([@imijes](https://github.com/imijes)), built on the work of others;
  everyone is listed there.

M-VAVE and FM-1 are trademarks of their owners, as are Novation and Bass Station. Bass Something 2 is not affiliated with M-VAVE or Novation.
