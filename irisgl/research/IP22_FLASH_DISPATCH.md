# IP22 IRIS GL `flash` dispatch archaeology

Source binary: IP22 `libgl.so` extracted from the reconstructed IRIX tree.

This note records only names and behavior recovered directly from the original binary. No reconstructed subsystem names are introduced here.

## Public wrapper pattern

The public `flash.c` entry points are weak symbols aliased to SGI `__GL*` wrappers. Examples:

- `winopen` -> `__GLwinopen`
- `qread` -> `__GLqread`
- `qtest` -> `__GLqtest`
- `prefposition` -> `__GLprefposition`
- `prefsize` -> `__GLprefsize`
- `foreground` -> `__GLforeground`
- `RGBmode` -> `__GLRGBmode`
- `reshapeviewport` -> `__GLreshapeviewport`
- `gconfig` -> `__GLgconfig`
- `RGBcolor` -> `__GLRGBcolor`
- `clear` -> `__GLclear`

The longer wrappers (`qread`, `qtest`, preference/window calls) first call `gl_initializer`, restore the original argument registers, inspect global dispatch state, and tail-jump through a function pointer.

Shorter state/drawing wrappers (`RGBmode`, `reshapeviewport`, `gconfig`, `RGBcolor`, `clear`) skip the initializer call and directly select an implementation based on the same dispatch state.

## Recovered implementation families

For the `flash` slice, SGI's own internal names recovered from the dynamic symbol table are:

| API | Implementation targets |
| --- | --- |
| `winopen` | `gl_g_winopen`, `gl_d_winopen` |
| `qread` | `gl_g_qread`, `gl_d_qread` |
| `qtest` | `gl_g_qtest`, `gl_d_qtest` |
| `prefposition` | `gl_g_prefposition`, `gl_d_prefposition` |
| `prefsize` | `gl_g_prefsize`, `gl_d_prefsize` |
| `foreground` | `gl_g_foreground`, `gl_d_foreground` |
| `RGBmode` | `gl_g_RGBmode`, `gl_d_RGBmode` |
| `reshapeviewport` | `gl_g_reshapeviewport`, `gl_d_reshapeviewport` |
| `gconfig` | `gl_g_gconfig`, `gl_d_gconfig` |
| `RGBcolor` | `gl_i_RGBcolor`, `gl_c_RGBcolor`, `gl_d_RGBcolor` |
| `clear` | `gl_i_clear`, `gl_c_clear`, `gl_d_clear` |

The meaning of the `g`, `d`, `i`, and `c` prefixes is not yet asserted. Preserve the original names until binary evidence identifies their semantics.

## `gl_initializer`

All long wrappers call `gl_initializer` before dispatch.

Recovered instruction behavior:

1. Test an initialization global.
2. If already initialized, return immediately.
3. Otherwise call two helper functions.
4. Clear one dispatch-state global.
5. Call another helper and set a field at offset `0x194` in the returned object to `1`.

The helper names behind these GOT entries still need to be resolved.

## `gl_d_gconfig`

`gl_d_gconfig` writes a compact command into an internal command buffer:

- command word: `0x2e`
- command length: 4 bytes
- advances the command-buffer pointer
- compares against an end pointer
- invokes a flush/helper path when the pointer passes the end

This is direct evidence that one original implementation family of IRIS GL uses an encoded command stream rather than performing all state changes inline.

## `gl_d_RGBcolor`

`gl_d_RGBcolor` likewise appends an encoded command:

- command word: `0x82`
- red stored as signed 16-bit at `+4`
- green stored as signed 16-bit at `+6`
- blue stored as signed 16-bit at `+8`
- command consumes 12 bytes
- command buffer is flushed/refilled through the same helper path if full

This is particularly useful for reconstructing the ABI-visible RGB color path without guessing how the original command transport was represented.

## `gl_d_clear`

`gl_d_clear` appends a single-word command:

- command word: `0x14`
- command length: 4 bytes
- same command-buffer pointer/end-pointer mechanism as `gl_d_gconfig`

## `gl_i_RGBcolor`

`gl_i_RGBcolor` is not a command encoder. It obtains a current internal object through a global pointer, converts the three signed 16-bit arguments to floating point, and invokes a function pointer stored at object offset `0x460`.

Values below 256 are converted directly to float. Values >= 256 are replaced by a float value loaded from object offset `0x1c8` before the indirect call.

Do not assign reconstructed field names to offsets `0x1c8` or `0x460` until symbols or cross-references identify them.

## `gl_c_RGBcolor`

`gl_c_RGBcolor` constructs a small local argument block containing:

- an internal function/code pointer
- three signed 16-bit color components
- a zero 16-bit fourth component

It then calls a shared helper with operation/count value `6` and the local block.

The exact semantics of this helper remain unresolved.

## `gl_i_clear`

`gl_i_clear` obtains the current internal object and calls an indirect/helper function with that object.

## `gl_c_clear`

`gl_c_clear` builds a local block containing an internal function/code pointer and invokes the same shared helper family used by `gl_c_RGBcolor`, this time with operation/count value `2`.

## `gl_g_winopen`

`gl_g_winopen` is large (2984 bytes) and clearly contains substantial window-system setup. The opening portion:

- substitutes a default title/string if the caller passes null or an empty string
- increments a global window/open count
- performs one-time initialization on the first window
- invokes several helpers and parses/configures additional state before continuing into the full window creation path

The binary's dynamic dependencies include `libX11.so.1`, `libXi.so`, and `libXext.so`, so X11-side call resolution for this function is a priority in the next pass.

## `gl_d_winopen`

`gl_d_winopen` is only 76 bytes. It calls one helper, then calls another helper with the original title argument. Resolving those two functions should reveal what the `d` family represents for window creation.

## Immediate next steps

1. Resolve GOT/import targets inside `gl_g_winopen` to concrete X11/internal symbol names.
2. Resolve `gl_initializer` helpers and dispatch globals.
3. Disassemble `gl_g_RGBmode`, `gl_d_RGBmode`, `gl_g_reshapeviewport`, and `gl_d_reshapeviewport` fully.
4. Identify the command-buffer flush helper used by `gl_d_gconfig`, `gl_d_RGBcolor`, and `gl_d_clear`.
5. Compare the same internal symbols across another machine-family `libgl.so` to separate generic runtime structure from IP22-specific behavior.
