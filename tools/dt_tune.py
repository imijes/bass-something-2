#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Drum Something's tunables, swept: for each setting (MACRO=VALUE pairs, comma separated), sets those #defines in
bs2/bs2_drum.c, builds the host and prints tools/dt_compare.py's summary for the drum. The last setting stays in
the file (run the one you want last; or --restore puts the file back as it was).

From the fm1-base root:
    python3 bs2/tools/dt_tune.py /path/to/recordings TOM1 "DT_TOM_LOW=0.3f" "DT_TOM_LOW=0.6f"
    python3 bs2/tools/dt_tune.py /path/to/recordings SNARE "DT_SN_WIRE_Q=0.001f,DT_SN_WIRE_G=0.6f" --restore
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dt_compare as dc                      # noqa: E402

SRC = dc.BS2 / "bs2_drum.c"
ROOT = dc.BS2.parent


def apply(text, setting):
    """text with each MACRO=VALUE of setting set in its #define (the comment after it kept)."""
    for kv in setting.split(","):
        k, v = kv.strip().split("=")
        text, n = re.subn(rf"^(#define {k} )\S+\s*", lambda m: (m.group(1) + v).ljust(45), text, count=1,
                          flags=re.M)
        if n != 1:
            sys.exit(f"no #define {k} in {SRC}")
    return text


def build():
    r = subprocess.run([sys.executable, "tools/build.py", "bs2", "--host"], cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.exit("the host build failed:\n" + (r.stdout + r.stderr)[-2000:])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", type=Path, help="the reference recordings")
    ap.add_argument("drum")
    ap.add_argument("settings", nargs="+", help="MACRO=VALUE[,MACRO=VALUE...]")
    ap.add_argument("--steps", default="1,8,16")
    ap.add_argument("--cycles", action="store_true", help="the first cycles too (a low drum's pitch fall)")
    ap.add_argument("--env", action="store_true", help="the envelopes in ROM time too")
    ap.add_argument("--restore", action="store_true", help="put bs2_drum.c back as it was at the end")
    a = ap.parse_args()
    drum = a.drum.upper()
    steps = [int(v) for v in a.steps.split(",")]
    original = SRC.read_text()
    try:
        for setting in a.settings:
            SRC.write_text(apply(SRC.read_text(), setting))
            build()
            ours, theirs, _, _ = dc.compare(a.folder, drum)
            print(f"== {setting}")
            dc.overall_report(ours, theirs, steps)
            dc.tone_report(ours, theirs, steps, 10, detail=False)
            dc.band_report(ours, theirs, steps, detail_steps=steps[:1])
            dc.attack_report(ours, theirs, steps)
            if a.cycles:
                dc.cycle_report(ours, theirs, steps)
            if a.env:
                dc.env_report(ours, theirs, steps, drum)
    finally:
        if a.restore:
            SRC.write_text(original)
            build()


if __name__ == "__main__":
    main()
