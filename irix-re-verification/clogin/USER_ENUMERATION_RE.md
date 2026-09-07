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

Special passwd names beginning with `+` and `-` receive separate NIS/YP compatibility handling.

For ordinary entries the recovered decision path includes:

1. Duplicate/name handling through `PandoraComponent::alreadyKnown()`.
2. Lookup in the global `PwdNameList` with `PwdNameList::findEntry(username)`.
3. If an entry exists, `(flags & 0x03) != 0` suppresses normal addition.
   - `0x01 = noshow`
   - `0x02 = noicon`
4. `IsInIgnoreList(pw_dir)` is checked. This is how pseudo-tree paths such as `/hosts` and `/net` participate in filtering.
5. `checkDirectory(pw_dir)` is consulted. The original distinguishes multiple numeric directory states rather than a simple `stat()` boolean.
6. One branch performs `stat()` and explicitly verifies `S_IFDIR` before allowing the account.
7. Accounts that survive are passed to `PandoraComponent::addUser()`.

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
    +-- special +/- NIS handling
    +-- duplicate check
    +-- PwdNameList flags (noshow/noicon)
    +-- ignore-list path check (/hosts, /net, ...)
    +-- directory-state / directory-existence checks
    |
    v
addUser()
```

## Still unresolved

- semantic names for every `checkDirectory()` return value
- exact role of `show` / getShowAnywayList in surrounding libc/passwd behavior
- exact original class name of the 0x1c-byte per-user object
