# checkDirectory reconstruction

Target: `checkDirectory` at `0x1000e4b8` in the IRIX `clogin` binary.

## Recovered return values

- `0`: known local filesystem (`efs` or `xfs` in the original IRIX implementation)
- `1`: NFS filesystem
- `2`: other/unknown filesystem class; also used for paths the helper declines to classify directly
- `3`: path does not exist or is not a directory
- `4`: internal cache-miss sentinel returned by `searchFoundDirList()` only

## Supporting helpers

`findFsType(path)`:

1. calls `statfs(path, ...)`
2. converts the filesystem type using `sysfs(...)`
3. returns `1` for `nfs`
4. returns `0` for `efs` or `xfs`
5. returns `2` for any other filesystem or classification failure

`addToFoundDirList(path, state)` stores a duplicated path, the state, and a next pointer in a global singly-linked cache.

`searchFoundDirList(path)` returns the cached state for an exact path match, or `4` on a cache miss.

## Path handling

- null/non-absolute paths return `2`
- `/dev/null` is explicitly special-cased and returns `2`
- the path is duplicated and progressively shortened at `/` boundaries while consulting the cache
- directory entries are inspected with the BSD `opendir/readdir/closedir` interfaces
- after a filesystem state is established it is cached with `addToFoundDirList()`
- for states other than `2`, a final `stat()` verifies the target; failure or a non-directory mode changes the result to `3`

## Why this matters

The caller intentionally distinguishes `2` from `3`:

- `3` is a definite rejection (missing/not-directory)
- `2` is an unknown/other filesystem and callers may perform an additional ordinary `stat()` check

That behavior should be preserved in the Linux port even though Linux filesystem classification differs from IRIX.
