# Size log

What each change costs on the FM-1, from the device build (`tools/build.py bs2`): the flash image (its slot holds
581,564 B), RAM (.data + .bss, 98,304 B) and the pool (344,064 B). A row a change, added by
`python3 bs2/tools/size_log.py "what changed"` from the fm1-base root; Δ is against the row before.

| Date | Commit | Change | Flash | Δ | Flash free | RAM | Δ | RAM free | Pool |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2026-10-31 | 6639cf3+ | Baseline: v0.3 + RND remote, hold buttons, RND full-screen pages, control labels, value pop-ups, Manual Mode, GLOBAL DISPLAY, flat panel grey | 304,316 | — | 277,248 | 83,060 | — | 15,244 | 96,008 |
