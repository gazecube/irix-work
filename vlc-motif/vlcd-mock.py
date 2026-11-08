#!/usr/bin/env python3
"""Stand-in for vlcd: speaks the protocol with a fake player, so the Motif front end can be tested without libvlc."""
import sys, time, select, re

def enc(s): return re.sub(r"[%\t\r\n]", lambda m: "%%%02x" % ord(m.group(0)), str(s))
def dec(s): return re.sub(r"%([0-9a-fA-F]{2})", lambda m: chr(int(m.group(1), 16)), s)
def out(*f): sys.stdout.write("\t".join(enc(x) for x in f) + "\n"); sys.stdout.flush()

pl, cur, state, pos, length, vol, muted, last = [], -1, "idle", 0, 0, 100, 0, time.time()
def send_pl(): out("PLAYLIST", len(pl), cur); [out("ITEM", i, p.rsplit("/", 1)[-1]) for i, p in enumerate(pl)]
def send_tracks():
    for kind, names in (("audio", ["Disable", "Track 1 - English", "Track 2 - Japanese"]), ("sub", ["Disable", "English", "Deutsch"]),
                        ("title", ["Title 1", "Title 2"]), ("chapter", ["Chapter %d" % i for i in range(1, 6)])):
        out("TRACKS", kind, len(names))
        for i, n in enumerate(names): out("TRACK", kind, i, 1 if i == (1 if kind in ("audio",) else 0) else 0, n)
def start(i):
    global cur, state, pos, length
    cur, state, pos, length = i, "playing", 0, 95000
    out("STATE", "opening"); out("STATE", "playing"); out("META", pl[i].rsplit("/", 1)[-1], "Artist", "Album", ""); send_pl(); send_tracks()
out("READY"); out("VOLUME", vol, muted)
buf = ""
while True:
    r, _, _ = select.select([sys.stdin], [], [], 0.2)
    now = time.time()
    if state == "playing":
        pos += int((now - last) * 1000)
        if pos >= length: state = "ended"; out("STATE", "ended")
    last = now
    if state in ("playing", "paused"): out("TIME", pos, length)
    if not r: continue
    line = sys.stdin.readline()
    if not line: break
    f = [dec(x) for x in line.rstrip("\n").split("\t")]
    c = f[0]
    if c == "OPEN": pl[:] = f[1:]; start(0)
    elif c == "ADD":
        pl.extend(f[1:]); send_pl()
        if state in ("idle", "stopped", "ended"): start(len(pl) - len(f[1:]))
    elif c == "PLAYINDEX": start(int(f[1]))
    elif c in ("PLAY", "PAUSE", "TOGGLE"):
        if state == "playing" and c != "PLAY": state = "paused"
        elif state == "paused": state = "playing"
        elif pl and state in ("idle", "stopped", "ended"): start(max(cur, 0))
        out("STATE", state)
    elif c == "STOP": state, pos = "stopped", 0; out("STATE", state); out("TIME", 0, length)
    elif c == "SEEK": pos = int(length * int(f[1]) / 1000)
    elif c == "JUMP": pos = max(0, min(length, pos + int(f[1])))
    elif c == "NEXT" and cur + 1 < len(pl): start(cur + 1)
    elif c == "PREV" and cur > 0: start(cur - 1)
    elif c == "CLEAR": pl[:] = []; cur = -1; state = "idle"; send_pl(); out("STATE", state)
    elif c == "VOLUME": vol = int(f[1]); out("VOLUME", vol, muted)
    elif c == "MUTE": muted = int(f[1]); out("VOLUME", vol, muted)
    elif c == "QUIT": break
