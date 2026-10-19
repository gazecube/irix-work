#!/usr/bin/env python3
"""Remove build-cache entries that no current build references.

The IRIX app bundle builder keeps content-addressed caches next to the build
tree (.irix-dso-cache, .irix-translation-cache, .irix-object-cache). Entries are
hardlinked into every build that uses them, so an entry whose files all have a
link count of 1 is not part of any current build. Every runtime rebuild changes
the cache keys, so those entries accumulate (tens of GB) and are never reused.

    prune_build_caches.py BUILD_RECOMP_DIR                   dry run
    prune_build_caches.py BUILD_RECOMP_DIR --apply           delete unreferenced entries
    prune_build_caches.py BUILD_RECOMP_DIR --dedupe --apply  hardlink identical objects
    --min-age SECONDS skips entries modified more recently (default 0; the builder uses 3600)
"""
import fcntl
import hashlib
import os
import shutil
import sys
import time


def busy(lock_path):
    """True when a running build holds the entry's lock."""
    try:
        fd = os.open(lock_path, os.O_RDWR)
    except OSError:
        return False
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        fcntl.flock(fd, fcntl.LOCK_UN)
        return False
    except OSError:
        return True
    finally:
        os.close(fd)


def tree_size(path):
    total = 0
    for root, _dirs, files in os.walk(path):
        for f in files:
            try:
                total += os.lstat(os.path.join(root, f)).st_size
            except OSError:
                pass
    return total


def orphan(path):
    """A file or directory tree with no file hardlinked elsewhere."""
    if os.path.isdir(path):
        for root, _dirs, files in os.walk(path):
            for f in files:
                if os.lstat(os.path.join(root, f)).st_nlink > 1:
                    return False
        return True
    return os.lstat(path).st_nlink <= 1


def prune(base, apply_, min_age=0, quiet=True):
    now = time.time()
    freed = kept = 0
    for cache in ('.irix-dso-cache', '.irix-translation-cache'):
        d = os.path.join(base, cache)
        if not os.path.isdir(d):
            continue
        n = size = 0
        for name in sorted(os.listdir(d)):
            if name.endswith('.lock') or '.tmp.' in name:
                continue
            p = os.path.join(d, name)
            lock = p[:-3] + '.lock' if name.endswith('.so') else p + '.lock'
            if now - os.lstat(p).st_mtime < min_age or busy(lock) or not orphan(p):
                kept += 1
                continue
            sz = tree_size(p) if os.path.isdir(p) else os.lstat(p).st_size
            n += 1
            size += sz
            if apply_:
                if os.path.isdir(p):
                    shutil.rmtree(p)
                else:
                    os.unlink(p)
                if os.path.exists(lock):
                    os.unlink(lock)
        if not quiet or n:
            print('%-26s %s %5d entries, %.1f GB' % (cache, 'removed' if apply_ else 'would remove', n, size / 1e9))
        freed += size
    if not quiet or freed:
        print('%s %.1f GB; %d entries still in use' % ('freed' if apply_ else 'reclaimable', freed / 1e9, kept))
    return freed


def dedupe_objects(base, apply_):
    """Hardlink identical object files across every app's obj/ directory and the object cache."""
    import glob
    now = time.time()
    files = glob.glob(os.path.join(base, 'generated-*', 'obj', '*.o')) + \
        glob.glob(os.path.join(base, '.irix-object-cache', '*.o'))
    by_size = {}
    for f in files:
        st = os.lstat(f)
        if now - st.st_mtime < 60:
            continue
        by_size.setdefault(st.st_size, []).append((f, st.st_ino))
    saved = linked = 0
    for size, items in by_size.items():
        if len({ino for _, ino in items}) < 2:
            continue
        groups = {}
        for f, ino in items:
            h = hashlib.sha256()
            with open(f, 'rb') as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b''):
                    h.update(chunk)
            groups.setdefault(h.digest(), []).append((f, ino))
        for members in groups.values():
            members.sort(key=lambda m: ('.irix-object-cache' not in m[0], m[0]))   # prefer the cache copy
            keep_f, keep_ino = members[0]
            for f, ino in members[1:]:
                if ino == keep_ino:
                    continue
                saved += size
                linked += 1
                if apply_:
                    tmp = f + '.dedupe.%d' % os.getpid()
                    os.link(keep_f, tmp)
                    os.replace(tmp, f)
    print('objects: %s %d files, %.1f GB' % ('hardlinked' if apply_ else 'would hardlink', linked, saved / 1e9))
    return saved


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    apply_ = '--apply' in sys.argv
    if len(args) != 1:
        print(__doc__)
        return 2
    min_age = 0
    if '--min-age' in sys.argv:
        min_age = int(sys.argv[sys.argv.index('--min-age') + 1])
        args = [a for a in args if a != str(min_age)]
    if '--dedupe' in sys.argv:
        dedupe_objects(args[0], apply_)
    else:
        prune(args[0], apply_, min_age, quiet=False)


if __name__ == '__main__':
    sys.exit(main() or 0)
