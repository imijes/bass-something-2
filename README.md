# Bass Something 2

A Bass Station 2-style monosynth firmware for the M-VAVE FM-1, on fm1-base, started from fm1-base's `template/`.
The manual (`docs/MANUAL.md`) describes everything it does.

| File | What |
| --- | --- |
| `app.h` | The app's settings (name, block size, the store's objects) and its half of the contract with the platform |
| `app.c` | The app: input, sound, screen, saving |
| `bs2_sysex.c` | Bass Station II patch dumps in and out |
| `bs2_rnd.c` | RND seeds: the RND Synth's seeds captured, kept in their own table (`OBJ_SEEDS`), sent back |
| `fm1.toml` | Build and site settings: name, package identity (`FM-1_62`), USB names, tagline, licence |
| `words.tsv` | The screen reader's words (fm1-reader) |
| `tests/*.txt` | Host simulator checks: basic, glo, sysex, rnd, seqkeep, lit, voice (`tests/syx`: the SysEx they send) |
| `tests/screens.txt` | The manual's screenshots |
| `docs/MANUAL.md`, `site/landing.md` | The manual and the site's front page |
| `web/emu/index.html` | The emulator's page: fm1-base's, in the firmware's colours, with a quick guide |
| `site/theme.css`, `site/make_site.py` | The site (front page, installer, manual) in the firmware's colours, with its tabs |
| `site/publish_pages.py` | The built site to GitHub Pages (the `gh-pages` branch) |
| `platform/*.patch` | The changes to fm1-base and felucca-bsp it needs (below) |

## Build, test, install

Bass Something 2 builds in an [fm1-base](https://github.com/charlesvestal/fm1-base) checkout (bs2/ at its root) at
d8ff8f4, with its felucca-bsp at 305c372, plus two small platform patches that aren't upstream yet (`platform/`):
the computer's USB audio as the synth's external input (`plat_usb_audio_route`, `plat_usb_audio_in`), and the battery
read while the keys' velocity is on. From the fm1-base root:

    git apply bs2/platform/fm1-base.patch
    git -C bsp apply ../bs2/platform/felucca-bsp.patch

Then, from the repository root:

    tools/build.py bs2 --host                   # the host simulator, then:
    bs2/build/host/bass-something-2_host bs2/tests/basic.txt bs2/build/host
    tools/build.py bs2                          # bs2/build/bass-something-2.fwsc (JieLi toolchain, AC79 SDK)
    tools/build.py bs2 --web                    # the browser emulator: bs2/build/emu/
    python3 tools/fm1_install.py bs2/build/bass-something-2.fwsc

The site (bs2/build/site), from a release:

    tools/build.py bs2 --release 0.2 && tools/build.py bs2 --web --release 0.2
    tools/build.py bs2 --rfm1 --release 0.2               # RemoteFM1 (iOS): bs2/build/rfm1/bass_something_2-0.2.rfm1,
                                                          # a release asset; build it from a pushed commit (its source_url)
    python3 bs2/site/make_site.py bs2/build/bass-something-2-0.2.fwsc 0.2
    (cd bs2/build/site && python3 -m http.server 8765)    # http://localhost:8765
    python3 bs2/site/publish_pages.py 0.2                 # https://imijes.github.io/bass-something-2/

Bass Something 2 is by John Brown ([@imijes](https://github.com/imijes)). Credits for everyone whose work it builds on: `docs/MANUAL.md`, section 12.

Novation and Bass Station are trademarks of Focusrite Audio Engineering Ltd.; Bass Something 2 is not affiliated with Novation
or M-VAVE.
