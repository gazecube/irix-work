# Toolchest Motif parser baseline

The IRIX 6.5 `toolchest` binary contains a parser family whose names and roles overlap OSF/Motif 1.2.4 `mwm` `WmResParse.c`:

```
ParseMenuSet
ParseMwmMenuStr
ParseKeyEvent
FreeMenuItem
ScanAlphanumeric
ScanWhitespace
ToLower
```

The pinned 4Dwm Motif baseline is therefore also useful here, but only as a source comparison target. Toolchest is not being treated as mwm code wholesale.

Binary comparison has already separated exact/simple behavior from modified behavior:

- `ToLower` @ 0x1000e29c: byte-wise loop; uppercase bytes are replaced with `tolower(byte)`. The IRIX body does not call `mblen`, unlike the surviving upstream 1.2.4 source. This is `MWM-MODIFIED` behavior.
- `ScanWhitespace` @ 0x1000e324: advances `*linePP` while the current byte is whitespace. The IRIX body does not call `mblen`. `MWM-MODIFIED`.
- `ScanAlphanumeric` @ 0x100135d0: advances while the current byte is classified alphanumeric. `MWM-MODIFIED`.
- `ParseMwmMenuStr` @ 0x1000e16c: installs the caller menu string into parser state, clears parser line/state globals, then enters the Toolchest menu parser. Architecture matches mwm, but its globals/callee need offset/name recovery before source integration.
- `FreeMenuItem` @ 0x1000e1b4: frees the label at +0x04, accelerator text at +0x18, conditionally frees function arguments at +0x20 depending on the function pointer at +0x1c, then frees the item. The field offsets match the classic 32-bit Motif `MenuItem` shape through `wmFunction`; Toolchest has a function-specific ownership table that must be recovered exactly before this body is landed.
- `ParseKeyEvent` @ 0x100130bc delegates to the shared event parser and returns false on parser failure; on success its boolean result is derived from the caller output byte. The exact Toolchest closure/table entry is still being resolved.

The small byte-scanner implementations that are fully determined by machine code live in `recovered/MenuParser.c`. The larger routines remain out until their parser state and ownership rules are typed instead of guessed.
