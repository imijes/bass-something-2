# Bass Something 2 v0.9.3

Fixes for installing and restoring. The full guide is `docs/MANUAL.md`.

- **Restore works with the echo ringing:** a backup's restore said STOP FIRST, "Bass Something 2 is playing", whenever
  the Space Echo was still ringing, forever when it ran away. It now waits only for what's played (the drums, the
  sequencer and arp, keys, notes); the echo's and the room's tails are just cut short.
- **The Space Echo starts at mode 7:** a first start (or a reset) sets its MODE SELECTOR to 7, head 3 and the spring; 10 and 11 ran away at the knobs' standard settings.
- **The installer page:** installing over the stock firmware could end with "installed, but the device reports
  FM-1_690" although it had worked. The page now asks the FM-1 again for its whole identity while its first start
  settles.

Everything else is as v0.9.2 (`docs/RELEASE_v0.9.2.md`).
