#!/usr/bin/env python3
"""Classify IRIX 4Dwm exports against the pinned Motif mwm source tree.

This is intentionally conservative. A textual source-name match only creates
an MWM-BASE-CANDIDATE. It never claims that SGI's machine code is identical.
"""
from __future__ import annotations
import argparse
import csv
import pathlib
import re
import sys

IDENT = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(')
SKIP = {"if", "for", "while", "switch", "return", "sizeof"}


def source_index(root: pathlib.Path):
    found = {}
    for path in root.rglob('*'):
        if path.suffix not in {'.c', '.h'} or not path.is_file():
            continue
        try:
            text = path.read_text(errors='ignore')
        except OSError:
            continue
        for name in IDENT.findall(text):
            if name in SKIP:
                continue
            found.setdefault(name, set()).add(str(path.relative_to(root)))
    return found


def strong_sgi_name(name: str) -> bool:
    exact = {
        'CreateRoom','DestroyRoom','F_CreateRoom','F_RemoveRoom','F_Room',
        'F_InAllRooms','DesksGeometry','DesksState','CheckRoomsClientMessage',
        'F_TellDesktop','F_ExplicitSave','F_AutoSave','F_SaveConfiguration',
        'F_LoadConfiguration','CreateRoomAppAtoms','AddARoomNameProperty',
        'DeleteARoomNameProperty','AppActivated','AppDeactivated','AppProperty',
        'AppRename','AppStateChanged','ActiveAppsIterate','ActiveXApp'
    }
    return name in exact or 'Room' in name or name.startswith('Desks')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exports', type=pathlib.Path)
    ap.add_argument('mwm_source', type=pathlib.Path,
                    help='path to upstream-motif/clients/mwm')
    ap.add_argument('-o', '--output', type=pathlib.Path)
    ns = ap.parse_args()
    idx = source_index(ns.mwm_source)
    rows = []
    with ns.exports.open(newline='') as f:
        r = csv.DictReader(f, delimiter='\t')
        for row in r:
            name = row['name']
            hits = sorted(idx.get(name, ()))
            if hits:
                cls = 'MWM-BASE-CANDIDATE'
                evidence = 'upstream source name match: ' + ','.join(hits)
            elif strong_sgi_name(name):
                cls = 'SGI-NEW-CANDIDATE'
                evidence = 'SGI rooms/DESKS naming; no upstream source-name match'
            else:
                cls = 'UNCLASSIFIED'
                evidence = 'no upstream source-name match'
            rows.append({**row, 'classification': cls, 'evidence': evidence})
    out = ns.output.open('w', newline='') if ns.output else sys.stdout
    try:
        w = csv.DictWriter(out,
            fieldnames=['address','size','name','classification','evidence'],
            delimiter='\t')
        w.writeheader()
        w.writerows(rows)
    finally:
        if out is not sys.stdout:
            out.close()

if __name__ == '__main__':
    main()
