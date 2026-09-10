# createIconList / user-enumeration reconstruction

Recovered from `pandora::createIconList()` and `PandoraComponent::addUser()` in `/usr/Cadmin/bin/clogin`.

## No UID threshold

The original code enumerates passwd entries with `setpwent()` / `getpwent()`. No generic minimum-UID cutoff appears in this path.

This directly contradicts the old Linux prototype's `UID >= 1000` heuristic as a reconstruction of SGI behavior.

## Local-home policy changes passwd enumeration

`pandora::createIconList()` calls `getWhereHome()` before enumeration.

When the policy is local-only (`getWhereHome() == 0`), the binary sets the IRIX libc private global:

```text
_getpwent_no_yp = 1
```

before using `getpwent()`.

When home directories may be anywhere, it leaves/enables YP passwd enumeration instead. The function restores `_getpwent_no_yp` to zero on exit paths.

This is important: "home directory must be local" affects the account source itself, not merely a post-enumeration filesystem test.

The function also writes `_pw_stayopen` during enumeration.

## Entry processing

For each returned passwd record, the binary makes its own copy with `pwd0_copyPwd()` and tracks a count in `pandora +0x1c`.

For ordinary entries the recovered decision path includes:

1. Duplicate/name handling through `PandoraComponent::alreadyKnown()`.
2. Lookup in the global `PwdNameList` with `PwdNameList::findEntry(username)`.
3. If an entry exists, `(flags & 0x03) != 0` suppresses normal addition.
   - `0x01 = noshow`
   - `0x02 = noicon`
4. `IsInIgnoreList(pw_dir)` is checked. This is how pseudo-tree paths such as `/hosts` and `/net` participate in filtering.
5. `checkDirectory(pw_dir)` is consulted.
6. Return state `2` performs an ordinary `stat()` fallback and explicitly verifies `S_IFDIR`.
7. Return state `3` suppresses the account directly.
8. Return states `0` and `1` continue without the state-2 fallback.
9. Accounts that survive are passed to `PandoraComponent::addUser()`.

## Special `+` / `-` passwd records

Direct MIPS disassembly resolves the compatibility flow more precisely.

### Leading `-`

When the copied passwd name begins with `-`, `createIconList()` frees the copied passwd record and returns from the enumeration routine immediately.

This is not the normal per-user filtering path.

### Leading `+`

When the copied passwd name begins with `+`, the binary strips the leading `+` before continuing.

If the copied passwd record then has an empty home-directory field, the routine:

1. frees the current copied passwd record;
2. temporarily preserves the `_getpwent_no_yp` state;
3. calls `getpwnam()` with the stripped name;
4. copies the returned passwd record with `pwd0_copyPwd()`;
5. restores the passwd/YP state;
6. calls `setpwent()`;
7. advances `getpwent()` the number of records already counted in `pandora +0x1c`, restoring the enumeration position disturbed by the name lookup;
8. continues through the normal duplicate, policy, ignore-list, and directory checks.

A second leading `+` check exists on the `getpwnam()` result and strips that prefix as well before restoring enumeration position.

This behavior is consistent with IRIX passwd/NIS compatibility handling. The implementation notes intentionally stop at the observable binary behavior rather than assigning broader NIS semantics that are not proven by this executable.

## `show` / getShowAnywayList

`PwdNameList::getShowAnywayList()` is exported by the binary, but there is no direct call to it in the executable's `createIconList()` path.

The per-user `PwdNameList` check in `createIconList()` calls `findEntry(username)` and tests only `(flags & 0x03)` for suppression. Therefore the high-confidence statement for this path is:

```text
noshow (0x01) or noicon (0x02) -> suppress ordinary addition
```

The exact effect of `show` (`0x10`) outside this path remains unresolved and should not be inferred from the getter name alone.

## addUser object construction

`PandoraComponent::addUser(struct xpasswd *)` allocates a 0x1c-byte object and constructs SGI `ImdString` values from at least:

```text
pw_name
pw_gecos / passwd display-name field
```

It then inserts the object into the component's view/model collection at `PandoraComponent +0x50`.

The object is subsequently consumed by the Imd icon-panel/layout pipeline. The exact class name of this 0x1c-byte allocation is not exported, so it remains intentionally unnamed here.

## High-confidence filtering model

```text
IRIX passwd/YP source
    |
    +-- local-home policy may disable YP enumeration entirely
    |
    v
passwd entry
    |
    +-- '-' record: cleanup + return from enumeration
    +-- '+' record: strip prefix, resolve empty-home entry by name,
    |                restore passwd enumeration position
    +-- duplicate check
    +-- PwdNameList flags (noshow/noicon)
    +-- ignore-list path check (/hosts, /net, ...)
    +-- directory-state / directory-existence checks
    |
    v
addUser()
```

## Still unresolved

- exact effect of `show` (`0x10`) outside the proven `createIconList()` path
- exact original class name of the 0x1c-byte per-user object

## 2026-09-10 caller-semantics refinement

Direct disassembly of the shipped IRIX `clogin` binary with MIPS cross-binutils resolved the `checkDirectory()` caller contract inside `pandora::createIconList()`.

- return 3: suppress the user
- return 2: call ordinary `stat()` on `pw_dir`; suppress on stat failure or when `st_mode & S_IFMT` is not `S_IFDIR`
- return 0 or 1: continue the normal add-user decision without the state-2 fallback
- `getWhereHome()==0` controls passwd/NIS enumeration through IRIX libc `_getpwent_no_yp`; it is not implemented as `checkDirectory()==LOCAL` filtering after enumeration

This is distinct from `PandoraComponent::findUserPicture()`, where local-picture mode requires `checkDirectory(pw_dir)==0` before trying the home icon.
