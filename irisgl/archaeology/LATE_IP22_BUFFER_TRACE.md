# Late IP22 IRIS GL buffer/configuration trace

This trace uses the original IP22NG1 `libgl.so` supplied in `shared_objects.tar`. Binary labels below are original symbols from that library; reconstructed field meanings remain explicitly provisional unless independently sourced.

## Artifact

`usr/gfx/arch/IP22NG1/libgl.so` is ELF32 big-endian MIPS-II and retains a large dynamic symbol table despite being stripped.

Relevant original symbols include:

```
__GLRGBmode       0x0f08ff70
__GLdoublebuffer  0x0f090bb0
__GLgconfig       0x0f0919b0
__GLsinglebuffer  0x0f091ef0

gl_d_doublebuffer 0x0f0bf774
gl_d_gconfig      0x0f0bfc80
gl_d_RGBmode      0x0f0c2c80
gl_d_singlebuffer 0x0f0c3274

gl_g_singlebuffer 0x0f1007f8
gl_g_doublebuffer 0x0f100820
gl_g_RGBmode      0x0f10089c
gl_set_displaymode 0x0f1011d4
gl_gconfig         0x0f101344
gl_g_gconfig       0x0f13d028
```

## Public dispatch wrappers

All four public wrappers select internal implementation families through the same dispatch-state pattern. For this question the important point is that both `g` and `d` implementations survive for mode calls and `gconfig`.

## `d` command stream

The `gl_d_*` family proves distinct opcodes:

```
gl_d_doublebuffer -> 0x23
gl_d_gconfig      -> 0x2e
gl_d_RGBmode      -> 0x84
gl_d_singlebuffer -> 0x8f
```

Each function writes its opcode to the same command buffer, advances the pointer by four bytes, checks the end pointer, and calls the common flush path if necessary.

This proves that the late transported representation preserves the mode calls and `gconfig` as separate operations.

## `g` mode calls are request/state mutations

This is the decisive result.

`gl_g_singlebuffer` is only 40 bytes. It performs no call or ioctl. It walks through the current GL state object and stores halfword value `1` at offset `+0x8` of a nested state structure.

`gl_g_doublebuffer` is 84 bytes. It examines a mode/type field at nested offset `+0x14`; for accepted states it stores halfword value `2` at nested offset `+0x8`. It performs no call or ioctl.

`gl_g_RGBmode` is 56 bytes. It checks the same mode/type family and, for the relevant state, stores zero at nested offset `+0x18`. It performs no call or ioctl.

Do not assign canonical names to these offsets yet. The behavioral conclusion does not require guessed names: the public late `g` mode implementations mutate library state and return without crossing into the kernel.

Therefore, unlike GL2.4, the late `g` path is genuinely deferred with respect to kernel/display reconfiguration.

## `gconfig` performs the late commit

The internal `gl_gconfig` function is 1424 bytes and performs substantial configuration work. Near its end it loads the GOT entry for original symbol `gl_set_displaymode` and calls it directly.

The relevant GOT entry is:

```
-25300(gp) -> gl_set_displaymode @ 0x0f1011d4
```

and the call occurs at the end of `gl_gconfig` around `0x0f10189c`.

Thus the late `g` path is now binary-proven as:

```
singlebuffer/doublebuffer/RGBmode
        |
        v
mutate requested/library configuration state
        |
        v
     gconfig
        |
        v
  gl_gconfig
        |
        v
 gl_set_displaymode
        |
        v
 kernel graphics ioctl
```

## `gl_set_displaymode` crosses the kernel boundary

`gl_set_displaymode` constructs a small display/configuration record from current GL state, then calls original symbol `_GLgfxioctl`.

Its GOT entry resolves as:

```
-22892(gp) -> _GLgfxioctl @ 0x0f13867c
```

Immediately before that call it loads ioctl/operation value `0x3f0` into argument register `$4` and passes a pointer to a three-word stack record in `$5`.

The record contains values copied/constructed from current GL state, including display/configuration bitfields. The exact public/source names for every bit are not assigned here without the matching late private headers.

This is direct binary evidence that late `gconfig` reaches the graphics-kernel interface through `gl_set_displaymode`.

## `gl_g_gconfig`

The separate `gl_g_gconfig` wrapper performs validation/state checks, updates state bits, invokes internal helpers, and eventually invokes the underlying configuration machinery. It is not an empty alias.

This is consistent with the public dispatch architecture and the large internal `gl_gconfig` implementation.

## Resolved evolution point

The earlier uncertainty is now resolved for the IP22NG1 late `g` implementation:

```
                         GL2.4                 late IP22 `g` path
                         -------------------   -------------------------
singlebuffer             immediate KGL ioctl  mutate requested state
doublebuffer             immediate KGL ioctl  mutate requested state
RGBmode                  immediate KGL ioctl  mutate requested state
gconfig                  color/writemask      perform configuration
                                                -> gl_set_displaymode
                                                -> _GLgfxioctl
```

So the buffer/display configuration commit boundary genuinely moved between GL2.4 and late IRIX.

## Consequence for our reconstruction

Our existing requested-vs-committed configuration architecture is not merely a plausible RRM-shaped guess anymore. Its core public transition model is supported directly by the late IP22 binary:

```
mode selection -> requested state
gconfig        -> configuration commit/kernel boundary
```

This does NOT prove that every field or side effect in our `gconfig` is correct. In particular, color/write-mask behavior, framebuffer allocation/validation, display-mode bit construction, error paths, multiwindow state, and swap resources still require mapping.

But the architectural decision to defer host buffer-mode realization until `gconfig` should be retained.

## Additional immediate targets

1. Decode ioctl value `0x3f0` against the exact 6.5 `gfx.h`/RRM definitions.
2. Map the three-word record passed by `gl_set_displaymode` to the source-side ioctl structure.
3. Resolve all GOT callees inside `gl_gconfig` and recover the ordering of context, buffer, swap, clip, and color configuration.
4. Trace late `gl_g_swapbuffers`/`gl_swapbuffers` into RRM swap/retrace operations.
5. Compare IP22NG1 against IP22GR2NG1 and IP22XGE to separate generic GL semantics from board-family differences.
6. Use this result to annotate, not rewrite, the current runtime before the next stress test.
