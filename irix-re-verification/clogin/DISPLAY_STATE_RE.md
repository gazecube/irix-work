# pandora display-state reverse engineering

Target: `/usr/Cadmin/bin/clogin`.

## Original accessors

```text
10014fe4 getHowDisplay__7pandoraGv
10015000 getWhereIcons__7pandoraGv
1001501c getWhereHome__7pandoraGv
10015038 getWindowSize__7pandoraGv
```

All four are direct field loads from the global `PwdNameList` object:

```text
pandora/PwdNameList config
+0x0c howDisplay
+0x10 whereIcons
+0x14 whereHome
+0x18 windowSize
```

The values are exactly those recovered from `PwdNameList` parsing:

```text
howDisplay: 0 = display faces, 1 = display icons
whereIcons: 0 = look for pictures locally, 1 = look for pictures anywhere
whereHome:  0 = home directory must be local, 1 = home directory can be anywhere
windowSize: 0 = use small window, 1 = use large window
```

## PandoraComponent copies

`pandora::initialize(PandoraComponent *, Display *)` performs:

```text
component+0x98 = getWhereIcons()
component+0x9c = getWhereHome()
```

These two fields are therefore now identified with original accessor semantics rather than guessed names.

## findUserPicture consequence

The previously unresolved `PandoraComponent+0x98` gate in `findUserPicture()` is `whereIcons`.

The function uses:

```text
component+0x98      -> whereIcons
component+0x58+0x0c -> pandora::howDisplay
component+0x58+0x10 -> pandora::whereIcons
```

This confirms the binary's path gating is based on the parsed display/icon-location policy.
