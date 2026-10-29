#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 John Brown
"""Bass Something 2's site to GitHub Pages: the built site (bs2/site/make_site.py) as one commit on this repository's
gh-pages branch, pushed to origin, as fm1-base's own site is published. The working tree and main are not touched.

  python3 bs2/site/publish_pages.py VERSION [SITE_DIR]   (SITE_DIR: bs2/build/site)

Then https://imijes.github.io/bass-something-2/ (emu/, install/, manual/)."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

APP = Path(__file__).resolve().parents[1]


def git(*args, env=None):
    return subprocess.run(["git", "-C", str(APP), *args], check=True, capture_output=True, text=True,
                          env=env).stdout.strip()


def main(version, site=None):
    site = Path(site) if site else APP / "build" / "site"
    if not (site / "index.html").exists() or not (site / "emu" / "index.html").exists():
        sys.exit(f"{site}: no site; make it first: python3 bs2/site/make_site.py ... {version}")
    with tempfile.TemporaryDirectory() as tmp:     # an index of its own: the site's files, nothing of main's
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(tmp) / "index"))
        git("--work-tree", str(site), "add", "-A", ".", env=env)
        tree = git("write-tree", env=env)
    git("fetch", "-q", "origin")
    parent = subprocess.run(["git", "-C", str(APP), "rev-parse", "-q", "--verify", "origin/gh-pages"],
                            capture_output=True, text=True).stdout.strip()
    msg = f"Site for Bass Something 2 {version} (from {git('rev-parse', '--short', 'HEAD')})"
    commit = git("commit-tree", tree, "-m", msg, *(["-p", parent] if parent else []))
    git("push", "-q", "origin", f"{commit}:refs/heads/gh-pages")
    print(f"gh-pages: {commit[:7]} {msg}")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    main(*sys.argv[1:])
