#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bass Something 2's size log: builds the device firmware, reads its sizes (flash image, RAM, pool) from
tools/build.py's report, and adds a row to bs2/docs/SIZE_LOG.md with the change from the row before.

From the fm1-base checkout's root, with the device toolchain set up (see the README):
    python3 bs2/tools/size_log.py "what changed"
    python3 bs2/tools/size_log.py --dry "what changed"     # print the row, log nothing
"""
import argparse
import datetime
import os
import re
import subprocess
import sys
from pathlib import Path

BS2 = Path(__file__).resolve().parent.parent
LOG = BS2 / "docs" / "SIZE_LOG.md"
ROOT = BS2.parent
APP_SLOT = 581564                            # tools/fm1pkg_make.py's APP_SLOT: the app's flash slot
HEAD = """# Size log

What each change costs on the FM-1, from the device build (`tools/build.py bs2`): the flash image (its slot holds
581,564 B), RAM (.data + .bss, 98,304 B) and the pool (344,064 B). A row a change, added by
`python3 bs2/tools/size_log.py "what changed"` from the fm1-base root; Δ is against the row before.

| Date | Commit | Change | Flash | Δ | Flash free | RAM | Δ | RAM free | Pool |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
"""


def build():
    env = dict(os.environ)
    env.setdefault("JIELI_TOOLCHAIN", str(Path.home() / ".jieli/jieli-linux-toolchains-20260730.1"))
    env.setdefault("AC79_SDK", str(Path.home() / "fw-AC79_AIoT_SDK"))
    out = subprocess.run([sys.executable, "tools/build.py", "bs2"], cwd=ROOT, env=env, capture_output=True, text=True)
    m = re.search(r"image (\d+) B;(?: RAM code [^;]*;)? RAM \.data\+\.bss (\d+) B of (\d+); pool (\d+) B of (\d+)", out.stdout + out.stderr)
    if out.returncode or not m:
        sys.exit("the device build failed or gave no sizes:\n" + (out.stdout + out.stderr)[-2000:])
    return [int(g) for g in m.groups()]


def last_row():
    if not LOG.exists():
        return None
    rows = [l for l in LOG.read_text().splitlines() if l.startswith("| 20")]
    if not rows:
        return None
    c = [x.strip() for x in rows[-1].strip("|").split("|")]
    return int(c[3].replace(",", "")), int(c[6].replace(",", ""))


def commit():
    git = lambda *a: subprocess.run(["git", *a], cwd=BS2, capture_output=True, text=True).stdout.strip()
    h = git("rev-parse", "--short", "HEAD")
    return h + ("+" if git("status", "--porcelain", "--untracked-files=no") else "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("change", help="what changed since the last row")
    ap.add_argument("--dry", action="store_true", help="print the row, log nothing")
    a = ap.parse_args()
    img, ram, ram_max, pool, _ = build()
    prev = last_row()
    d = lambda now, was: "—" if was is None else f"{now - was:+,}"
    row = (f"| {datetime.date.today()} | {commit()} | {a.change.replace('|', '/')} | {img:,} | "
           f"{d(img, prev and prev[0])} | {APP_SLOT - img:,} | {ram:,} | {d(ram, prev and prev[1])} | "
           f"{ram_max - ram:,} | {pool:,} |")
    print(row)
    if a.dry:
        return
    if not LOG.exists():
        LOG.write_text(HEAD)
    with LOG.open("a") as f:
        f.write(row + "\n")
    print(f"logged in {LOG.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
