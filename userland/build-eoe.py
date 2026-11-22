#!/usr/bin/env python3
"""Build every command directory of eoe/cmd with SGI's own Makefiles and smake, and tally what stops them.

    build-eoe.py [DIR ...]       (no names: every directory of eoe/cmd that has a Makefile; lib/NAME for eoe/lib)

Works on a copy of eoe/cmd (~/opt/claude-work/eoe/src/cmd; the source tree stays untouched) and installs into a
staging root (~/opt/claude-work/eoe/dest, with a .manifest of every installed path).  Each directory runs
"smake default install" inside bubblewrap with the file system read-only except the work area, at low priority, with
an address-space cap and a timeout.  Results: ~/opt/claude-work/eoe/summary.txt (one line per directory),
headers.txt (missing headers, most wanted first), symbols.txt (undefined symbols), errors.txt (first compile error
of every directory that failed for another reason).  Per-directory logs: ~/opt/claude-work/eoe/log/DIR.txt.
All paths are literal.
"""
import json
import os
import re
import resource
import subprocess
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path('/home/nekron/opt/claude-work/eoe/src')          # copies of eoe/cmd and eoe/lib
ROOT = Path('/home/nekron/opt/claude-work/eoe/root')       # staging root: IRIX headers, libraries, installed commands (setup-root.sh)
LOG = Path('/home/nekron/opt/claude-work/eoe/log')
OUT = Path('/home/nekron/opt/claude-work/eoe')
T = '/home/nekron/Projects/irix-work/userland/toolroot'
JOBS = 6
SANDBOX = ['bwrap', '--ro-bind', '/', '/', '--dev', '/dev', '--proc', '/proc', '--tmpfs', '/tmp',
           '--bind', '/home/nekron/opt/claude-work/eoe', '/home/nekron/opt/claude-work/eoe',
           '--unshare-net', '--unshare-pid', '--die-with-parent', '--new-session']


def low():
    os.nice(15)
    resource.setrlimit(resource.RLIMIT_AS, (4 << 30, 4 << 30))


def one(d):
    name = d.name if d.parent.name == 'cmd' else d.parent.name + '_' + d.name
    env = dict(os.environ, ROOT=str(ROOT), TOOLROOT=T, IRIX_DESTROOT=str(ROOT), PRODUCT='4DACE1_N32', STYLES_TO_BUILD='N32', PATH=T + '/usr/bin:' + os.environ['PATH'])
    cmd = SANDBOX + ['--chdir', str(d), 'ionice', '-c3', T + '/usr/sbin/smake', 'default', 'install']
    try:
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=1200, preexec_fn=low, errors='replace')
        text, rc = r.stdout + r.stderr, r.returncode
    except subprocess.TimeoutExpired as ex:
        text, rc = (ex.stdout or b'').decode('latin-1') + (ex.stderr or b'').decode('latin-1'), 'timeout'
    (LOG / (name + '.txt')).write_text(text)
    headers = sorted(set(re.findall(r'fatal error: ([^:]+): No such file', text)))
    syms = sorted(set(re.findall(r"undefined reference to `([^']+)'", text)))
    errs = re.findall(r'^\S+:\d+:\d+: error: (.*)$', text, re.M)
    rec = {'dir': name, 'rc': rc, 'headers': headers, 'symbols': syms, 'first_error': errs[0][:140] if errs else ''}
    if rc == 0:
        rec['status'] = 'ok'
    elif headers:
        rec['status'] = 'header: ' + ' '.join(headers[:3])
    elif syms:
        rec['status'] = 'link: %d undefined' % len(syms)
    elif errs:
        rec['status'] = 'compile: ' + errs[0][:80]
    else:
        last = [x for x in text.splitlines() if x.strip()][-3:]
        rec['status'] = 'failed: ' + (' | '.join(last))[:110]
    return rec


def main():
    LOG.mkdir(parents=True, exist_ok=True)
    # names are relative to SRC (cmd/ls, lib/libgen); a bare name means cmd/NAME
    dirs = [SRC / (x if '/' in x else 'cmd/' + x) for x in sys.argv[1:]] or sorted(p for p in (SRC / 'cmd').iterdir() if (p / 'Makefile').is_file())
    print('%d directories' % len(dirs), flush=True)
    recs = []
    with ThreadPoolExecutor(max_workers=JOBS) as ex:
        for rec in ex.map(one, dirs):
            recs.append(rec)
            print('%-22s %s' % (rec['dir'], rec['status']), flush=True)
    with open(OUT / 'summary.txt', 'w') as w:
        for r in recs:
            w.write('%-22s %s\n' % (r['dir'], r['status']))
        w.write('\n%d directories, %d built and installed\n' % (len(recs), sum(r['status'] == 'ok' for r in recs)))
    for key, fn in (('headers', 'headers.txt'), ('symbols', 'symbols.txt')):
        c = Counter(x for r in recs for x in r[key])
        with open(OUT / fn, 'w') as w:
            for x, n in c.most_common():
                w.write('%4d %s\n' % (n, x))
    with open(OUT / 'errors.txt', 'w') as w:
        for r in recs:
            if r['status'] != 'ok' and not r['headers'] and r['first_error']:
                w.write('%-22s %s\n' % (r['dir'], r['first_error']))
    (OUT / 'summary.json').write_text(json.dumps(recs, indent=1))


if __name__ == '__main__':
    main()
