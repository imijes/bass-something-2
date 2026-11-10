# Bass Something 2

A monosynth inspired by the legendary Novation Bass Station II, built for the M-VAVE FM-1. It's also a companion for Cyma Forma's RND Synth (firmware
1.4): 32 seed slots with their locks, and a mixer for its four tracks. It runs on an iPhone or iPad in RemoteFM1 too. It's built on fm1-base, started from fm1-base's `template/`.
It also has a second instrument, Drum Something 400: a drum machine inspired by the Sequential Circuits Drumtraks, with
synthesized voices on virtual EPROMs, patterns, songs and MIDI out. Since v0.8 there are two more: Piano Something 368X, an
electric piano inspired by the RMI Electra-piano, and Space Something 201, a tape echo and spring reverb inspired by the
Roland RE-201 that every instrument sends into. The manual (`docs/MANUAL.md`) describes everything
it does. To see what's new, read `docs/RELEASE_v0.8.md` (earlier: `docs/RELEASE_v0.7.md` to `v0.7.2`). The v0.8 firmware is 514,028 B, with about 66 KB of
flash and 9.9 KB of RAM free (`docs/SIZE_LOG.md`).

| File | What |
| --- | --- |
| `app.h` | The app's settings (name, block size, the store's objects) and its half of the contract with the platform |
| `app.c` | The app: input, sound, screen, saving |
| `bs2_sysex.c` | Bass Station II patch dumps in and out |
| `bs2_rnd.c` | RND Synth (firmware 1.4): its patches (seed and locks) kept in their own table (`OBJ_SEEDS`) and sent back; its locks and its tracks' mixer set from the RND pages |
| `rnd/` | The words RND SEED names patches with: BIP39's English list (`bip39_english.txt`, MIT) packed into `words.h` by `make_words.py` |
| `fm1.toml` | Build and site settings: name, package identity (`FM-1_69`, `FM-1_62` up to v0.7.2), USB names, tagline, licence |
| `words.tsv` | The screen reader's words (fm1-reader) |
| `tests/*.txt` | Host simulator checks: basic, glo, sysex, rnd, seqkeep, lit, voice, speaker (`speaker_test.c`, the speaker's sound, built on its own) (`tests/syx`: the SysEx they send) |
| `tests/screens.txt` | The manual's screenshots |
| `docs/MANUAL.md`, `site/landing.md` | The manual and the site's front page |
| `docs/SIZE_LOG.md`, `tools/size_log.py` | What each change costs in flash and RAM: `python3 bs2/tools/size_log.py "what changed"` (from the fm1-base root) builds the firmware and adds a row |
| `web/emu/index.html` | The emulator's page: fm1-base's, in the firmware's colours, with a quick guide |
| `site/theme.css`, `site/make_site.py` | The site (front page, installer, manual) in the firmware's colours, with its tabs |
| `site/publish_pages.py` | The built site to GitHub Pages (the `gh-pages` branch) |
| `platform/*.patch` | The changes to fm1-base and felucca-bsp it needs (below) |

## Build, test, install

Bass Something 2 builds in an [fm1-base](https://github.com/charlesvestal/fm1-base) checkout (bs2/ at its root) at
698e2cb, with its felucca-bsp at f878800, plus the changes in `platform/` that aren't upstream yet:
- the computer's USB audio as the synth's external input (`plat_usb_audio_route`, `plat_usb_audio_in`; fm1-base #17);
- the speaker told about the headphone jack (`APP_SPEAKER`);
- the host's xrun count, for tests;
- felucca-bsp's EP0 fix (#28);
- a Bluetooth link's signal strength for PROX (`plat_ble_rssi`; `platform/fm1-base-prox.patch`, its draft PR beside it).

From the fm1-base root:

    git apply bs2/platform/fm1-base.patch
    git apply bs2/platform/fm1-base-prox.patch
    git -C bsp apply ../bs2/platform/felucca-bsp.patch

Then, from the repository root:

    tools/build.py bs2 --host                   # the host simulator, then:
    bs2/build/host/bass-something-2_host bs2/tests/basic.txt bs2/build/host
    tools/build.py bs2                          # bs2/build/bass-something-2.fwsc (JieLi toolchain, AC79 SDK)
    tools/build.py bs2 --web                    # the browser emulator: bs2/build/emu/
    python3 tools/fm1_install.py bs2/build/bass-something-2.fwsc

The site (bs2/build/site), from a release:

    tools/build.py bs2 --release 0.8 && tools/build.py bs2 --web --release 0.8
    tools/build.py bs2 --rfm1 --release 0.8               # RemoteFM1 (iOS): bs2/build/bass-something-2-0.8.rfm1,
                                                          # a release asset; build it from a pushed commit (its source_url)
    python3 bs2/site/make_site.py bs2/build/bass-something-2-0.8.fwsc 0.8
    (cd bs2/build/site && python3 -m http.server 8765)    # http://localhost:8765
    python3 bs2/site/publish_pages.py 0.8                 # https://imijes.github.io/bass-something-2/

Bass Something 2 is by John Brown ([@imijes](https://github.com/imijes)). Credits for everyone whose work it builds on: `docs/MANUAL.md`, section 13.

Novation and Bass Station are trademarks of Focusrite Audio Engineering Ltd.; Bass Something 2 is not affiliated with Novation
or M-VAVE.
