#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Copyright (C) 2026 John Brown
"""Bass Something 2's site: fm1-base's tools/make_site.py, then each page in the firmware's own colours (bs2/site/theme.css)
with the section tabs on top (Home, Emulator, Install, SysEx, Manual). The emulator and the SysEx page bring their own
(bs2/web/emu/index.html, bs2/web/sysex/index.html).

  python3 bs2/site/make_site.py bs2/build/bass-something-2-VERSION.fwsc VERSION [OUT_DIR]   (OUT_DIR: bs2/build/site)

The package must be a release build: tools/build.py bs2 --release VERSION; and the emulator built: tools/build.py bs2
--web."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

APP = Path(__file__).resolve().parents[1]
SRC = APP.parent
PAGES = (("", "Home"), ("emu/", "Emulator"), ("install/", "Install"), ("sysex/", "SysEx"), ("manual/", "Manual"))


def tabs(here, up, cls=""):
    """the site's tabs, the page at HERE lit; UP the way to the site's top from it"""
    cur = ' aria-current="page"'
    items = "".join(f'<li><a href="{up + p or "./"}"{cur if p == here else ""}>{t}</a></li>' for p, t in PAGES)
    return f'<nav aria-label="Site"><ul class="bs2-tabs {cls}">{items}</ul></nav>\n'


# the installer's footer, fm1-base's (its own credits and licence link), as the front page's: who made it, the licence
# (this repo's LICENSE), and the credits in one place, the manual's section 12
FOOTER = ('<footer>\n  <p class="small">Bass Something 2 by John Brown (<a href="https://github.com/imijes" target="_blank" '
          'rel="noopener">@imijes<span class="sr" data-t="newtab"> (opens in a new tab)</span></a>), <a href="https://github.com/imijes/bass-something-2/blob/main/LICENSE" '
          'target="_blank" rel="noopener">MPL-2.0<span class="sr" data-t="newtab"> (opens in a new tab)</span></a>, built on '
          'the work of others: see <a href="{up}manual/#s12">Credits</a> in the manual.<br>\n'
          '    M-VAVE and FM-1 are trademarks of their respective owners; Novation and Bass Station of Focusrite Audio '
          'Engineering Ltd. Bass Something 2 is not affiliated with M-VAVE or Novation.</p>\n</footer>')


# each page's title and summary, as a shared link shows them (fm1-base's are put together from fm1.toml's tagline and
# read oddly): <title>, the description, and Open Graph's (the front page's screenshot as the picture)
URL = "https://imijes.github.io/bass-something-2/"
WHAT = ("a monosynth inspired by the legendary Novation Bass Station II, for the M-VAVE FM-1, and a companion for "
        "Cyma Forma's RND Synth: seeds, locks and a track mixer")
META = {
    "": ("Bass Something 2: a monosynth inspired by the legendary Novation Bass Station II, for the M-VAVE FM-1",
         f"Bass Something 2 is {WHAT}."),
    "install/": ("Install · Bass Something 2", "Install Bass Something 2 on the M-VAVE FM-1 from Chrome or Edge, over USB."),
    "manual/": ("Manual · Bass Something 2", "The manual for Bass Something 2, a monosynth inspired by the legendary Novation "
                "Bass Station II, for the M-VAVE FM-1: every page and control, the arp and the sequencer, patches, SysEx, "
                "MIDI and the RND Synth."),
}


def meta(html, here):
    title, desc = (x.replace("&", "&amp;").replace('"', "&quot;") for x in META[here])
    html, n = re.subn(r"<title>.*?</title>", f"<title>{title}</title>", html, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"{here or 'front page'}: no <title>")
    html = re.sub(r'<meta name="description"[^>]*>\n?', "", html)
    tags = (f'<meta name="description" content="{desc}">\n'
            f'<meta property="og:type" content="website">\n<meta property="og:site_name" content="Bass Something 2">\n'
            f'<meta property="og:title" content="{title}">\n<meta property="og:description" content="{desc}">\n'
            f'<meta property="og:url" content="{URL}{here}">\n'
            f'<meta property="og:image" content="{URL}manual/img/screen-home.png">\n'
            f'<meta property="og:image:alt" content="Bass Something 2 on the FM-1\'s screen: the HOME page">\n'
            f'<meta name="twitter:card" content="summary">\n')
    return html.replace("</title>", "</title>\n" + tags.rstrip("\n"), 1)


def theme(page, here, css):
    html = page.read_text(encoding="utf-8")
    if html.count("</head>") != 1:
        raise SystemExit(f"{page}: no </head> to put the theme before")
    html = html.replace("</head>", f"<style>\n{css}</style>\n</head>")
    html = html.replace("Bass-Something-2", "Bass Something 2")   # fm1.toml's name, a file name's spelling
    html = meta(html, here)
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
    shutil.copytree(APP / "web" / "sysex", out / "sysex", dirs_exist_ok=True)   # patches to and from the FM-1 (Web MIDI)
    css = (APP / "site" / "theme.css").read_text(encoding="utf-8")
    for here in ("", "install/", "manual/"):
        if (out / here / "index.html").exists():
            theme(out / here / "index.html", here, css)
    print(f"site: {out} in Bass Something 2's colours")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
