#!/usr/bin/env python3
import os, re, signal, struct, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TREE = os.path.join(REPO, 'IRIX-TREE')
RECOMP = os.path.join(REPO, 'build-irix-re', 'recomp')
RUNNER = os.path.join(RECOMP, 'irix-guest-exec')
DISPLAY = sys.argv[1] if len(sys.argv) > 1 else ':134'
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO, 'build-irix-re', 'sweep-results.tsv')
TIMEOUT = float(os.environ.get('SWEEP_TIMEOUT', '6'))
DIRS = ['sbin', 'usr/sbin', 'usr/bin', 'usr/bin/X11', 'usr/demos/bin', 'usr/gfx', 'usr/Cadmin/bin', 'usr/sysadm/bin']
DENY = set('''killall kill init telinit shutdown reboot halt fuser rm mv dd mkfs mkfs_efs mkfs_xfs fx restore su passwd
chown chmod chgrp swap umount mount sync uadmin sulogin getty login nvram nvlog flashinst dvhtool diskpatch fsck clri
growfs labelit mknod setmnt resymlink xlv_assemble xlv_labd xlv_mgr xlv_p cp ln install tar cpio find xargs nsmount ml
chroot suattr chacl chcap attr rmdir mkdir touch sh ksh csh bsh rsh wall write mesg sulogin'''.split())


def classify(path):
    try:
        with open(path, 'rb') as f:
            head = f.read(64)
    except OSError:
        return 'unreadable'
    if head[:2] == b'#!':
        return 'script'
    if head[:4] != b'\x7fELF':
        return 'data'
    if head[4] != 1:
        return 'elf64'
    flags = struct.unpack('>I', head[36:40])[0]
    if flags & 0x20:
        return 'n32'
    if flags & 0x1000:
        return 'o32'
    return 'elf32?'


def run_one(guest_path, name):
    env = dict(os.environ)
    env.update(DISPLAY=DISPLAY, IRIX_GUEST_DISPLAY='unix/' + DISPLAY, IRIX_RECOMP_ROOT=os.path.join(RECOMP, 'root'),
               IRIX_RUNTIME_TREE=TREE, IRIX_EXEC_BACKEND='static', IRIX_GUEST_EXEC_RUNNER=RUNNER,
               IRIX_GUEST_EXEC_PATH='/' + guest_path, HOME='/tmp/sweep-home', TERM='xterm',
               PATH='/usr/sbin:/usr/bsd:/sbin:/usr/bin:/usr/bin/X11')
    t0 = time.time()
    p = subprocess.Popen([RUNNER, name], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, start_new_session=True, cwd='/tmp/sweep-home')
    try:
        out, _ = p.communicate(timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        p.wait()
        return 'running', 'alive after %.0fs (long-running, GUI or waiting for input)' % TIMEOUT
    text = out.decode('latin1')
    m = re.search(r'UNIMPLEMENTED IRIX (?:SYSCALL|syssgi request) (\d+)', text)
    if m:
        return 'syscall', 'unimplemented ' + m.group(1)
    if 'INVALID GUEST CALL' in text or 'trapped:' in text or 'memory/trap failure' in text or 'unsupported pc' in text:
        m = re.search(r'(trapped: [^\n]{0,70}|unsupported[^\n]{0,50}|INVALID GUEST CALL[^\n]{0,50})', text)
        return 'trap', m.group(1) if m else 'trap'
    if 'failed to load' in text or 'MISSING IMPORT' in text:
        m = re.search(r'(failed to load[^\n]{0,70}|MISSING IMPORT[^\n]{0,50})', text)
        return 'load', m.group(1) if m else 'load'
    return 'ok' if p.returncode == 0 else 'exit', 'rc=%d %.1fs' % (p.returncode, time.time() - t0)


def main():
    os.makedirs('/tmp/sweep-home', exist_ok=True)
    items = []
    for d in DIRS:
        full = os.path.join(TREE, d)
        if not os.path.isdir(full):
            continue
        for n in sorted(os.listdir(full)):
            p = os.path.join(full, n)
            if os.path.isfile(p) and not os.path.islink(p) and os.access(p, os.X_OK):
                items.append((d, n, p))
    with open(OUT, 'w') as out:
        out.write('dir\tname\tkind\tstatus\tdetail\n')
        for d, n, p in items:
            kind = classify(p)
            if n in DENY:
                status, detail = 'skipped', 'deny-list'
            elif kind not in ('n32', 'script'):
                status, detail = 'skipped', kind
            else:
                try:
                    status, detail = run_one(d + '/' + n, n)
                except Exception as e:
                    status, detail = 'error', str(e)[:60]
            out.write('\t'.join([d, n, kind, status, detail.replace('\t', ' ').replace('\n', ' ')]) + '\n')
            out.flush()
        out.write('#done\n')


if __name__ == '__main__':
    main()
