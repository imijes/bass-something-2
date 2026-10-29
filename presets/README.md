# Factory patches

`factory.syx` is the first 32 patches of **ULTIMATE PATCHES Volume 2**, a Bass Station II bank (its author is
credited in the manual's Credits, `bs2/docs/MANUAL.md` section 12). It's the bank a new Bass Something 2 starts
with: patch 0 (GHOST MACHINE I) in P01 to patch 31 (DATA RAIN) in P32.

`make_presets.py` turns it into `presets.h`, the table the firmware reads at first power-on (app.c `bank_factory`).
Run it again after changing `factory.syx`:

    python3 bs2/presets/make_presets.py
