# PandoraComponent::findUserPicture reverse-engineering notes

Target symbol:

```text
10012080 findUserPicture__16PandoraComponentGP7xpasswd
```

The function returns an open file descriptor for the selected image, or -1.

## Proven lookup order

The MIPS disassembly and rodata resolve to this order:

```text
1. <pw_dir>/.icons/login.icon
2. /usr/local/lib/faces/<pw_name>
3. /usr/lib/faces/<pw_name>
4. /usr/local/lib/faces/machineUserIcon
   OR
   /usr/local/lib/faces/userIcon
```

Each candidate is opened read-only with `open(path, 0)`. The first successful open is returned.

## Recovered policy fields

The previously unresolved state is now proven by the original `pandora` accessors and `pandora::initialize()`:

```text
PandoraComponent+0x98 = pandora::getWhereIcons()
pandora+0x0c          = getHowDisplay()
pandora+0x10          = getWhereIcons()
```

Value semantics recovered from `PwdNameList` parsing:

```text
howDisplay: 0 = display faces, 1 = display icons
whereIcons: 0 = look for pictures locally, 1 = look for pictures anywhere
```

## Home-directory gate

Before trying `$HOME/.icons/login.icon`, the function calls:

```text
checkDirectory(pw->pw_dir)
```

when `whereIcons != 1`. A non-zero result aborts picture lookup immediately.

## Database lookup gates

After the per-user home icon fails:

- `/usr/local/lib/faces/<user>` is attempted when `whereIcons == 1` OR `howDisplay != 0`.
- `/usr/lib/faces/<user>` is attempted when `whereIcons == 1`.

The second rule follows because `PandoraComponent+0x98` is copied from the same `getWhereIcons()` value used at `pandora+0x10`.

## Default image

If all user-specific candidates fail, the function calls:

```text
pandora::isBuiltInName(username)
```

That function is exactly equivalent to:

```text
PwdNameList::findEntry(username) != NULL
```

If true, `findUserPicture()` opens:

```text
/usr/local/lib/faces/machineUserIcon
```

otherwise it opens:

```text
/usr/local/lib/faces/userIcon
```

This does not test the `machineicon` flag directly; membership in `PwdNameList` is sufficient for this fallback choice.

## Reimplementation status

A portable resolver is implemented in:

```text
Icon/UserPictureResolver.h
Icon/UserPictureResolver.c++
```

It preserves the recovered path order and policy gates. `checkDirectory()` is represented by an externally supplied result until semantic names for its numeric return states are proven.
