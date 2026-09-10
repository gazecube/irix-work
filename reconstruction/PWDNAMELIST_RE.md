# PwdNameList reverse-engineering notes

Target: `/usr/Cadmin/bin/clogin` from the recovered sysadmdesktop tree.

## Original symbols

```text
10015194  setSize__11PwdNameListGi
10015204  __ct__11PwdNameListGPc
100157b4  findEntry__11PwdNameListGPc
10015874  getIgnoreList__11PwdNameListGv
1001588c  getShowAnywayList__11PwdNameListGv
100158a4  getNoshowList__11PwdNameListGv
100158bc  __ct__16PwdNameListEntryGPc
100158cc  __dt__16PwdNameListEntryGv
```

## Original object layout

Recovered from constructor/setSize/findEntry MIPS behavior:

```text
PwdNameList
+0x00 int count
+0x04 int capacity
+0x08 PwdNameListEntry *entries
+0x0c int displayIcons       best-guess semantic name
+0x10 int picturesAnywhere   best-guess semantic name
+0x14 int homeAnywhere       best-guess semantic name
+0x18 int largeWindow        best-guess semantic name

PwdNameListEntry
+0x00 char *name
+0x04 unsigned flags
```

Constructor defaults:

```text
displayIcons     = 0
picturesAnywhere = 1
homeAnywhere     = 1
largeWindow      = 0
initial capacity = 16
```

The boolean member spellings above are reconstructed semantic names. Their values are proven by the exact directive branches below.

## Fixed directives

The constructor reads lines with `fgets(..., 256, ...)`, skips lines beginning with `#`, and tokenizes on `":\n"`.

Exact recognized fixed directives and stored values:

```text
display faces                   -> +0x0c = 0
display icons                   -> +0x0c = 1
look for pictures locally       -> +0x10 = 0
look for pictures anywhere      -> +0x10 = 1
home directory must be local    -> +0x14 = 0
home directory can be anywhere  -> +0x14 = 1
use small window                -> +0x18 = 0
use large window                -> +0x18 = 1
```

## Per-entry flag bits

Exact token-to-bit mapping recovered from the constructor:

```text
noshow      0x01
noicon      0x02
nodelete    0x04
machineicon 0x08
show        0x10
```

Both `noshow` and `noicon` also append the username to the global list returned by `getNoshowList()`.

`show` appends the username to the global list returned by `getShowAnywayList()`.

A leading-slash pseudo-entry whose next token is `ignore`, such as:

```text
/hosts:ignore
/net:ignore
```

is not inserted as a normal `PwdNameListEntry`; its name is appended to the global list returned by `getIgnoreList()`.

## Entry lookup

`findEntry(const char *)` linearly scans the 8-byte entry array and performs `strcmp(entry.name, requestedName)`. It returns the matching entry pointer or NULL.

## Portable implementation policy

`Base/PwdNameList.{h,c++}` preserves the recovered grammar and original public method names while using C++ strings/vectors rather than reproducing the SGI N32 heap layout. Exact binary layout is documented here and should only be used by ABI archaeology code.
