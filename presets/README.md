# Factory patches

Bass Something 2's 32 factory patches are its own, written for it: P01-P10 basses, P11-P20 leads, and P21-P32 the
rest of what the voice can do (the arpeggiator, sample and hold, the ring modulator, Osc Filter Mod, the envelopes'
retriggering, the band and high pass filters, noise). They are the bank a new Bass Something 2 starts with.

- `make_factory.py` holds the patches, as Bass Something 2's own settings, and writes them as a Bass Station II bank,
  `factory.syx`. It reads the dump's layout from `bs2_sysex.c`, so it writes each dump as the firmware's export
  does.
- `make_presets.py` turns `factory.syx` into `presets.h`, the table the firmware reads at first power-on (app.c
  `bank_factory`).

After changing a patch:

    python3 bs2/presets/make_factory.py && python3 bs2/presets/make_presets.py

`factory.syx` also loads into a Bass Station II, or back into Bass Something 2 over SysEx to restore the factory
bank.
