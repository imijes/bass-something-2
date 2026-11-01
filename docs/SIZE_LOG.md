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
