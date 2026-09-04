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

## Home-directory gate

Before trying `$HOME/.icons/login.icon`, the function conditionally calls the original global:

```text
checkDirectory(pw->pw_dir)
```

If that check is active and fails, the function returns -1 immediately. The precise semantic name of the `PandoraComponent` member controlling whether this check is bypassed is not yet recovered; it is read at `PandoraComponent+0x98` and compared with 1. Do not name that member yet.

## Database lookup gates

After the per-user home icon fails:

- `/usr/local/lib/faces/<user>` is attempted when either the unresolved `PandoraComponent+0x98` mode equals 1 or `pandora+0x0c` is non-zero.
- `/usr/lib/faces/<user>` is attempted when either the same unresolved mode equals 1 or `pandora+0x10` is non-zero.

The exact semantic names of `pandora+0x0c` and `pandora+0x10` are still to recover. They should not be guessed merely from the neighboring `PwdNameList` configuration fields.

## Default image

If all user-specific candidates fail, the function calls:

```text
pandora::isBuiltInName(username)
```

That function is proven to call `PwdNameList::findEntry(username)` and return true when an entry exists.

If true, `findUserPicture()` opens:

```text
/usr/local/lib/faces/machineUserIcon
```

otherwise it opens:

```text
/usr/local/lib/faces/userIcon
```

The original symbol name `isBuiltInName` therefore appears to mean "present in the shipped/configured PwdNameList", not necessarily the narrower reconstructed idea of the `machineicon` flag. Function-level consumers still need to be cross-checked before assigning stronger semantics.

## Reimplementation status

The path precedence is now binary-confirmed. The remaining work before implementing this as the final Linux resolver is to recover the exact meanings of:

```text
PandoraComponent+0x98
pandora+0x0c
pandora+0x10
```

`readyToDisplay()` and `getHowDisplay__7pandoraGv` are the next cross-checks for those fields.
