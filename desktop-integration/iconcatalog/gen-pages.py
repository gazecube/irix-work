#!/usr/bin/env python3
"""Generate IRIX Icon Catalog system pages from what is actually installed.

Two sources feed the pages:

* IRIX install-set ``*.idb`` files.  Products populate pages at install time
  with exitops of the form
      iconbookedit -add "Category:File Name:/usr/sbin/nedit" -syspage WhatsNew
  The commands are parsed out of the idbs, so the page set matches the real
  products without installing them.  An entry is kept only when its target
  exists in the runtime tree (or on the host).
* XDG ``.desktop`` files of the host, mapped to pages by ``Categories=``.

The result is a list of ``iconcatalogedit -add`` invocations.  ``--print``
shows them; ``--apply`` runs them through the native iconcatalogedit runner so
the on-disk page format is exactly what Icon Catalog expects.
"""
import argparse
import configparser
import glob
import os
import re
import shlex
import subprocess
import sys

CMD_RE = re.compile(
    r'icon(?:book|catalog)edit\s+-add\s+"([^"]+)"\s+-syspage\s+([A-Za-z0-9_ ]+?)(?=\s*[;)\'"]|\s*$)')
NAME_RE = re.compile(r'File Name:(.+)$')

# freedesktop main categories -> catalog page.  First match wins.
XDG_PAGES = [
    ("Game", "Games"),
    ("Network", "Internet"),
    ("WebBrowser", "Internet"),
    ("Graphics", "Graphics"),
    ("AudioVideo", "Multimedia"),
    ("Audio", "Multimedia"),
    ("Video", "Multimedia"),
    ("Office", "Office"),
    ("Development", "Development"),
    ("System", "System Tools"),
    ("Settings", "System Tools"),
    ("Utility", "Utilities"),
]


def parse_idbs(dirs):
    """Return {page: [iconstring, ...]} in first-seen order."""
    pages = {}
    for d in dirs:
        for path in sorted(glob.glob(os.path.join(d, "**", "*.idb"), recursive=True)):
            try:
                text = open(path, errors="replace").read()
            except OSError:
                continue
            for m in CMD_RE.finditer(text):
                icon, page = m.group(1), m.group(2).strip()
                lst = pages.setdefault(page, [])
                if icon not in lst:
                    lst.append(icon)
    return pages


def target_of(iconstring):
    m = NAME_RE.search(iconstring)
    return m.group(1).strip() if m else None


def exists(path, root):
    if not path:
        return False
    if os.path.lexists(os.path.join(root, path.lstrip("/"))):
        return True
    return os.path.lexists(path)


def parse_desktop_files(dirs):
    """Return [(page, name, desktop_path)] for launchable host applications."""
    out = []
    seen = set()
    for d in dirs:
        for path in sorted(glob.glob(os.path.join(d, "*.desktop"))):
            cp = configparser.ConfigParser(interpolation=None, strict=False)
            cp.optionxform = str
            try:
                cp.read(path, encoding="utf-8")
                e = cp["Desktop Entry"]
            except Exception:
                continue
            if e.get("Type", "Application") != "Application":
                continue
            if e.get("NoDisplay", "false").lower() == "true" or \
               e.get("Hidden", "false").lower() == "true":
                continue
            name = e.get("Name")
            if not name or name in seen:
                continue
            cats = [c for c in e.get("Categories", "").split(";") if c]
            page = next((p for key, p in XDG_PAGES if key in cats), "Applications")
            seen.add(name)
            out.append((page, name, path))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--idb", action="append", default=[],
                    help="directory searched recursively for *.idb (repeatable)")
    ap.add_argument("--tree", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "IRIX-TREE"),
        help="IRIX runtime tree (entries whose target is missing are dropped)")
    ap.add_argument("--xdg", action="append", default=None,
                    help="XDG applications directory (default: standard dirs)")
    ap.add_argument("--no-host", action="store_true", help="skip host .desktop files")
    ap.add_argument("--runner", help="native iconcatalogedit runner to apply with")
    ap.add_argument("--print", dest="do_print", action="store_true",
                    help="print the iconcatalogedit commands (default)")
    ap.add_argument("--apply", action="store_true", help="run the commands")
    a = ap.parse_args()

    root = os.path.abspath(a.tree)
    plan = []  # (page, iconstring)

    for page, icons in parse_idbs(a.idb).items():
        for icon in icons:
            if exists(target_of(icon), root):
                plan.append((page, icon))

    if not a.no_host:
        dirs = a.xdg or ["/usr/share/applications",
                         os.path.expanduser("~/.local/share/applications")]
        for page, _name, path in parse_desktop_files(dirs):
            plan.append((page, "Category:File Name:" + path))

    if a.apply:
        if not a.runner or not os.access(a.runner, os.X_OK):
            sys.exit("--apply needs --runner pointing at the iconcatalogedit runner")
        failed = 0
        for page, icon in plan:
            rc = subprocess.run([a.runner, "-add", icon, "-syspage", page],
                                timeout=60).returncode
            failed += rc != 0
        print("applied %d entries, %d failed" % (len(plan), failed), file=sys.stderr)
        return 1 if failed else 0

    for page, icon in plan:
        print("iconcatalogedit -add %s -syspage %s" % (shlex.quote(icon), shlex.quote(page)))
    print("# %d entries, %d pages" % (len(plan), len({p for p, _ in plan})), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
