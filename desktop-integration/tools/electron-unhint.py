#!/usr/bin/python3
"""Delete _MOTIF_WM_HINTS from Electron, CEF (steamwebhelper) and Chromium windows so 4Dwm frames them.

Watches the root window for new top-level windows and for the property being
set again.  A window counts as Electron when its _NET_WM_PID process runs an
electron binary or one of the app's own Electron launchers.  Usage:
    DISPLAY=:0 electron-unhint.py [--once]
"""
import os, sys
from Xlib import X, display, error, Xatom

MATCH = ("electron", "chrome-sandbox", "resources/app.asar", "claude-desktop", "steamwebhelper")

def is_electron(pid):
    try:
        exe = os.readlink("/proc/%d/exe" % pid)
        with open("/proc/%d/cmdline" % pid, "rb") as f:
            cmd = f.read().replace(b"\0", b" ").decode("utf-8", "replace")
    except OSError:
        return False
    s = (exe + " " + cmd).lower()
    return any(m in s for m in MATCH) or "--type=" in s and "chrome" in s

def main():
    d = display.Display()
    root = d.screen().root
    hints = d.intern_atom("_MOTIF_WM_HINTS")
    pidatom = d.intern_atom("_NET_WM_PID")
    seen = {}

    def check(win):
        try:
            if win.get_full_property(hints, X.AnyPropertyType) is None:
                return
            p = win.get_full_property(pidatom, X.AnyPropertyType)
            if p is None:
                return
            pid = int(p.value[0])
            if pid not in seen:
                seen[pid] = is_electron(pid)
            if seen[pid]:
                win.delete_property(hints)
                d.sync()
                print("removed _MOTIF_WM_HINTS from 0x%x (pid %d)" % (win.id, pid), flush=True)
        except (error.BadWindow, error.BadAccess):
            pass

    def watch(win):
        try:
            win.change_attributes(event_mask=X.PropertyChangeMask)
        except error.BadWindow:
            pass

    root.change_attributes(event_mask=X.SubstructureNotifyMask | X.PropertyChangeMask)
    def scan(w, depth):
        watch(w); check(w)
        if depth:
            try:
                for c in w.query_tree().children:
                    scan(c, depth - 1)
            except error.BadWindow:
                pass
    for w in root.query_tree().children:
        scan(w, 2)
    d.sync()
    if "--once" in sys.argv:
        return
    while True:
        e = d.next_event()
        if e.type == X.CreateNotify:
            scan(e.window, 1)
        elif e.type in (X.MapNotify, X.ReparentNotify):
            scan(e.window, 1)
        elif e.type == X.PropertyNotify and e.atom == hints and e.state == X.PropertyNewValue:
            check(e.window)

main()
