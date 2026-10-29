#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Copyright (C) 2026 John Brown
"""Bass Something 2's site: fm1-base's tools/make_site.py, then each page in the firmware's own colours (bs2/site/theme.css)
with the section tabs on top (Home, Emulator, Install, Manual). The emulator brings its own (bs2/web/emu/index.html).

  python3 bs2/site/make_site.py bs2/build/bass-something-2-VERSION.fwsc VERSION [OUT_DIR]   (OUT_DIR: bs2/build/site)

The package must be a release build: tools/build.py bs2 --release VERSION; and the emulator built: tools/build.py bs2
--web."""
import re
import subprocess
import sys
from pathlib import Path

APP = Path(__file__).resolve().parents[1]
SRC = APP.parent
PAGES = (("", "Home"), ("emu/", "Emulator"), ("install/", "Install"), ("manual/", "Manual"))


def tabs(here, up, cls=""):
    """the site's tabs, the page at HERE lit; UP the way to the site's top from it"""
    cur = ' aria-current="page"'
    items = "".join(f'<li><a href="{up + p or "./"}"{cur if p == here else ""}>{t}</a></li>' for p, t in PAGES)
    return f'<nav aria-label="Site"><ul class="bs2-tabs {cls}">{items}</ul></nav>\n'


# the installer's footer, fm1-base's (its own credits, a licence link to the repo Bass Something 2 doesn't have yet), as the
# front page's: who made it, and the credits in one place, the manual's section 12
FOOTER = ('<footer>\n  <p class="small">Bass Something 2 by John Brown (<a href="https://github.com/imijes" target="_blank" '
          'rel="noopener">@imijes<span class="sr" data-t="newtab"> (opens in a new tab)</span></a>), MPL-2.0, built on '
          'the work of others: see <a href="{up}manual/#s12">Credits</a> in the manual.<br>\n'
          '    M-VAVE and FM-1 are trademarks of their respective owners; Novation and Bass Station of Focusrite Audio '
          'Engineering Ltd. Bass Something 2 is not affiliated with M-VAVE or Novation.</p>\n</footer>')


def theme(page, here, css):
    html = page.read_text(encoding="utf-8")
    if html.count("</head>") != 1:
        raise SystemExit(f"{page}: no </head> to put the theme before")
    html = html.replace("</head>", f"<style>\n{css}</style>\n</head>")
    html = html.replace("Bass-Something-2", "Bass Something 2")   # fm1.toml's name, a file name's spelling
    up = "../" * here.count("/")
    if '<div class="page">' in html:            # the manual and the front page (tools/manual_html.py)
        html = html.replace('<div class="page">', '<div class="page">\n' + tabs(here, up), 1)
    else:                                         # the installer
        html = re.sub(r"<body>\n", lambda m: m.group(0) + tabs(here, up, "narrow"), html, count=1)
        html, n = re.subn(r"<footer>.*?</footer>", lambda m: FOOTER.format(up=up), html, count=1, flags=re.S)
        if n != 1:
            raise SystemExit(f"{page}: no <footer> to put the credits' pointer in")
    page.write_text(html, encoding="utf-8")


def main(pkg, version, out=None):
    out = Path(out) if out else APP / "build" / "site"
    subprocess.run([sys.executable, str(SRC / "tools" / "make_site.py"), str(APP), pkg, version, str(out)], check=True)
    css = (APP / "site" / "theme.css").read_text(encoding="utf-8")
    for here in ("", "install/", "manual/"):
        if (out / here / "index.html").exists():
            theme(out / here / "index.html", here, css)
    print(f"site: {out} in Bass Something 2's colours")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
