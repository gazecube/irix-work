# IRIS GL 3.x stack map

This document maps the surviving IRIX 3.x IRIS GL implementation in
`JohnDTX/irix-3.x-src` and cross-references it against the current portable
IRIS GL reconstruction in this branch. The old SGI source is treated as canon
for the old implementation. It is NOT assumed that every 3.x behavior survived
unchanged into IRIX 6.5; late binaries, headers, kernel interfaces, and stress
tests remain the authority for late-version differences.

## 1. Build graph

The top-level `gl2/gl2/Makefile` says that processor-dependent trees do the
actual build. It builds `ucode` first, then selects `ip2` when `machine` is
`juniper`, otherwise `pm2`.

The IP2 user library build is especially useful because it enumerates the
entire common implementation. `ip2/Make.gl` searches these source directories:

```
common/compiler
common/curve
common/feedback
common/hardcopy
common/immediate
common/object
common/services
common/windows
```

and archives their objects as `libgl${PREF}.a`.

The corresponding `ip2/Make.kgl` builds the kernel graphics implementation
from `kgl/` as `libkgl${PREF}.a`.

Therefore the source tree itself establishes this boundary:

```
application
    |
    v
libgl (user)
    common/compiler
    common/curve
    common/feedback
    common/hardcopy
    common/immediate
    common/object
    common/services
    common/windows
    |
    | grioctl + shared memory + command stream
    v
libkgl (kernel)
    kgl/grioctl.c
    kgl/gr.c
    kgl/retrace.c
    kgl/queue.c
    kgl/devices.c
    kgl/ge.c
    kgl/fbc.c
    ...
    |
    v
GE/FBC/DC/UC hardware and ucode
```

This is not a guessed architecture. Both libraries and their object lists are
explicit in the SGI makefiles.

## 2. Shared user/kernel state

`include/shmem.h` calls `struct shmem` the shared-memory data structure for
"cooperative manipulation of the graphics hardware, between the kernel and the
user". It also records the compatibility string `GLVERSION "GL2.4"`.

Important fields include:

```
struct shmem
    EOFpending
    intbuf / intbuflen
    fastfeed / numhits / hitbits
    locked
    inputchan
    gfnum
    windowstate ws
    ge_mask / ge_found
    qtop / qdev / qvalue
    isblanked
    smallbuf[128]
    mapcolor producer/consumer ring
```

`globals.h` makes the current shared window state directly available as:

```
#define gl_wstatep (&(gl_shmemptr->ws))
```

and declares per-process/user-library state such as the object compiler,
picking/feedback state, initialization state, and
`gl_personal_buffer_mode`.

This is a major correction/extension to our earlier abstract state model: old
IRIS GL did not keep all rendering state purely inside a user library. A large
`windowstate` was deliberately shared with KGL.

## 3. Original windowstate

`include/gltypes.h` defines the actual `windowstate` used by both user and
kernel. It contains:

```
graphics position
character position
backface + zbuffer mode
shade-range state
line-style/pattern state
window xmin/ymin/xmax/ymax
visible rectangle list
viewport stack + current viewport
matrix stack
attribute stack + current attributes
bitplanemask
font RAM locations
```

The current attributes (`struct atrdata`) contain the hardware configuration
word, line style, texture, color index, write mask, line width, font, RGB color,
and RGB write masks.

The visible-region representation is especially relevant to the late RRM
comparison: 3.x stores a window rectangle list directly in `windowstate`, while
our late-IRIX reconstruction currently models RRM clip pieces separately.

## 4. Kernel window/input model

`include/window.h` defines the kernel-side windowing structures:

- `piece`: one visible rectangle of an obscured window.
- `gfport`: graphics port with owner/input channel, bounds, and piece list.
- `txport`: text-port state and display rows.
- `inputchan`: per-process input/window state including event queue, current
  gfport, swap interval, display mode, cursor offsets, and device state.

The original display modes are explicitly:

```
MD_SINGLE 0
MD_DOUBLE 1
MD_RGB    2
```

`inputchan.ic_displaymode` is the kernel's current mode for that graphics
process/window channel.

This is structurally important for comparison with late RRM: graphics-port,
clip-piece, process/input-channel, display-mode, and swap state are distinct
objects in 3.x rather than one flattened GL context.

## 5. User/kernel control path

`common/services/grioctl.c` is a user-space syscall/control wrapper layer.
Examples:

```
qdevice      -> grioctl(GR_QDEVICE)
qread        -> flush mapcolor; grioctl(GR_QREAD); read qdev/qvalue from shmem
qtest        -> gl_shmemptr->qtop
RGBmode      -> flush mapcolor; grioctl(GR_RGBMODE)
singlebuffer -> grioctl(GR_SINGLEBUFFER)
doublebuffer -> grioctl(GR_DOUBLEBUFFER)
mapcolor     -> shared producer/consumer ring
swapinterval -> grioctl(GR_SWAPINTERVAL)
```

`kgl/grioctl.c` is the matching kernel dispatcher. In particular:

```
GR_SINGLEBUFFER -> gl_setsinglebuffer()
GR_DOUBLEBUFFER -> gl_setdoublebuffer()
GR_RGBMODE      -> gl_setrgbmode()
GR_WAITFORSWAP  -> dowaitforswap()
GR_QREAD        -> doqread()
```

This gives us an exact end-to-end call path for several APIs we already
implemented.

## 6. Buffer/display mode path

This is the first major semantic finding.

In GL2.4, `singlebuffer()`, `doublebuffer()`, and `RGBmode()` are NOT merely
userspace requests waiting for `gconfig()` to commit them. Their user wrappers
immediately issue graphics ioctls. `gl_singlebuffer()` and
`gl_doublebuffer()` additionally update `gl_personal_buffer_mode`.

KGL's `gl_setsinglebuffer()`, `gl_setdoublebuffer()`, and `gl_setrgbmode()` in
`kgl/retrace.c` then immediately update:

- the process `ic_displaymode`;
- global counts of double-buffered and RGB processes;
- global hardware mode when the first/last user requires a mode transition;
- text-port mode;
- `windowstate.curatrdata.myconfig`;
- the window bitplane mask;
- hardware configuration via `setconfig()`.

The global hardware mode is therefore shared and reference-counted across
processes. A process changing mode can cause system-wide graphics/text-port
reconfiguration when it crosses the first/last-user boundary.

This also explains the old source comment in `greset()` concerning another
program running double-buffered under MEX.

### `gconfig()` in GL2.4

The original function is much narrower than our current reconstruction:

```
gl_resolvecolor();
if (getdisplaymode())
    writemask((1 << getplanes())-1);
else
    RGBwritemask(0xff, 0xff, 0xff);
```

It resolves color mode and resets the appropriate write mask. It does not
perform the `singlebuffer`/`doublebuffer` transition in this implementation.

IMPORTANT: do not immediately rewrite the portable runtime around this fact.
Our target is late IRIX compatibility and late RRM may have moved the commit
boundary. This is now an explicit 3.x-vs-6.5 evolution question.

## 7. Swap path

`common/compiler/buffer.c` defines `swapbuffers` through the GL command
compiler/immediate macro machinery. Its immediate operation waits only when:

```
gl_personal_buffer_mode == DMDOUBLE
AND
WS->curatrdata.myconfig contains UC_DOUBLE
```

The wait crosses into KGL through `gl_waitforswap()` ->
`grioctl(GR_WAITFORSWAP)` -> `dowaitforswap()`.

KGL's retrace subsystem owns swap interval and retrace scheduling. Thus old
IRIS GL swap is not simply "copy a back buffer now"; presentation is tied to
kernel-managed retrace state.

Our X11 Pixmap implementation currently satisfies the visible `badifred`
contract, but it is only a host-backend approximation of this deeper model.

## 8. Color path

There are two distinct color paths.

### Color-index

`color()` is generated through `common/compiler/config.c`. The immediate macro
in `imattrib.h`:

1. masks the index with `WS->bitplanemask`;
2. stores it in `WS->curatrdata.mycolor`;
3. emits an FBC color command.

`mapcolor()` is different: user-space writes index/R/G/B into a 64-entry shared
ring in `struct shmem`. KGL consumes that ring during retrace (`gl_domapcolors`).
`gl_flushmap()` waits for the consumer to catch up.

This proves true hardware palette semantics and explains why our current
index-to-RGB-at-draw-time implementation is incomplete: changing a palette
entry is independent from rewriting framebuffer pixels.

### RGB

`RGBcolor` and `RGBwritemask` are generated by `common/compiler/rgbconfig.c`.
The immediate macros store R/G/B values in `windowstate.curatrdata` and emit
FBC commands directly.

## 9. Window-manager boundary

`common/windows/hints.c` is explicitly "provide hints to the window manager"
and is attributed to Paul Haeberli, 1984.

It gives exact old semantics for APIs already reconstructed:

- `prefsize`: set preferred-size flag and dimensions.
- `prefposition`: normalize the two endpoints with MIN/ABS, set both preferred
  origin and preferred size, and use inclusive `+1` dimensions.
- `foreground`: clear `gl_putinbackground`.
- `reshapeviewport`: set viewport to current shared window extents.

Our current `prefposition()` does not normalize reversed endpoints; that is a
source-proven old-version mismatch worth fixing only after checking late
behavior.

`common/windows/winstuff.c` describes itself as the "procedural interface to
mex" (Rocky Rhodes, 1985). `winopen`/`winope`:

1. initializes GL/getport if needed;
2. allocates a kernel gfport with `GR_GETGFPORT`;
3. allocates/saves a `windowstate` for that gfport;
4. switches to it with `winset`;
5. sends a `PORTREQ` to `WINDOWMAN` through `GR_SEND`;
6. initializes stacks;
7. reshapes viewport and establishes an ortho2 mapping;
8. emits current buffer configuration.

Other routines send MEX messages for move, reshape, title, attach, push/pop,
window lookup, text-port open/close, and new hints.

This establishes a clean old boundary:

```
IRIS GL window API
    |
    +-- KGL gfport/windowstate allocation and switching
    |
    `-- explicit IPC messages to MEX for window-management policy
```

That is directly relevant to a future MEX Linux port: the original MEX-facing
protocol can potentially be preserved above a replacement kernel/backend
transport.

## 10. Compiler vs immediate architecture

The source layout and macro files establish two execution forms for many GL
operations.

`common/compiler` contains generated/root/interpreter forms for drawing and
state operations, including `color`, `RGBcolor`, `clear`, matrices, viewport,
geometry, buffer control, and object calls. `common/object` owns retained GL
object/display-list storage and editing. `globals.h` exposes object compiler
state such as current object/chunk pointers and function hooks.

`imattrib.h`, `imdraw.h`, `immatrix.h`, `imsetup.h`, and related headers define
the immediate hardware-command forms. This family of private header names
survives in the later IRIX 6.5 CLOVER census, making it a high-confidence
architectural lineage marker.

## 11. First 3.x -> 6.5 structural cross-reference

The following correspondences are already strong enough to guide archaeology,
although they are not claims of identical layouts:

```
GL2.4 / 3.x                    late IRIX 6.5 evidence
----------------------------   --------------------------------
windowstate + gfport            graphics context/window resources
piece / gf_piecelist            RRM clip pieces / clip resources
inputchan                       process/thread graphics binding
ic_displaymode                  framebuffer/display-mode state
ic_SwapInterval                 RRM swap interval
kernel retrace scheduler        RRM retrace/swap resources
shared user/kernel state        mapped graphics/RRM state machinery
gr_ioctl command dispatcher     GFX/RRM ioctl/operation layer
GE/FBC command generation       late device-dependent GL command path
MEX explicit IPC                later window-system integration
```

The private-header-name continuity is particularly striking:

```
3.x include/                    6.5 CLOVER census
----------------------------   ----------------------------
globals.h                       globals.h
imattrib.h                      imattrib.h
imdraw.h                        imdraw.h
immatrix.h                      immatrix.h
immed.h                         immed.h
imsetup.h                       imsetup.h
shmem.h                         shmem.h
```

This strongly suggests evolution of the same internal family rather than an
unrelated rewrite, but exact 6.5 definitions are still missing and must not be
invented.

## 12. Cross-reference against our current runtime

Current portable state:

```
igl_runtime_state
  -> current_window
       -> requested/committed config
       -> reconstructed RRM rnode/pane/clip
       -> backend_window
  -> userspace colormap
  -> host event queue/backend
```

### Good architectural matches

- Window and clipping state are not flattened into drawing calls.
- Backend/resource separation leaves room for replacing X11.
- Buffer/swap state is represented explicitly.
- Window preferences are separate from the current rendering window.
- Late RRM concepts are retained rather than pretending X11 is IRIS GL.

### Known or newly exposed mismatches

1. **GL2.4 buffer commit boundary**
   Our `singlebuffer`/`doublebuffer` modify `requested`, with `gconfig`
   committing. GL2.4 immediately crosses into KGL. Late behavior must now be
   checked before changing ours.

2. **GL2.4 `gconfig` semantics**
   Our `gconfig` commits backend double buffering and resets current color.
   GL2.4 `gconfig` resolves color and resets write mask. This needs direct
   late-version cross-checking.

3. **Colormap semantics**
   Our sparse map resolves an index to host RGB at `color()` time. GL2.4 has a
   real palette update path consumed independently by KGL/retrace. Existing
   framebuffer pixels therefore need not be redrawn when map entries change.

4. **Current state granularity**
   Our current RGB/index values are process-global. GL2.4 stores core current
   attributes in per-window `windowstate`, with explicit save/restore on
   `winset`.

5. **Multiple windows**
   Our runtime tracks one current allocated window without the old per-gfport
   save/restore machinery. GL2.4 `winset` explicitly swaps `windowstate`.

6. **Events**
   Our event shim is deliberately minimal. GL2.4 has per-input-channel queues,
   queued device state, REDRAW/INPUTCHANGE/mode/piece-change events, ties,
   valuators, timers, and retrace integration.

7. **Swap/retrace**
   Our X11 backend presents immediately. GL2.4 swaps are kernel/retrace
   coordinated with per-input-channel swap intervals.

8. **Window-manager IPC**
   Our X11 backend directly creates/manages a host window. GL2.4 separates KGL
   gfport allocation from explicit MEX policy IPC.

9. **`prefposition` endpoint normalization**
   GL2.4 accepts endpoints in either order and normalizes them. Our current
   code assumes increasing endpoints.

## 13. API tracing table - first pass

| API | GL2.4 user implementation | KGL/hardware path | Current portable path | Status |
| --- | --- | --- | --- | --- |
| `prefsize` | `common/windows/hints.c` -> `portreq` | MEX message at port creation | next-window prefs -> X11 | structurally similar |
| `prefposition` | normalized `portreq` origin+size | MEX | next-window prefs -> X11 | old semantic mismatch |
| `foreground` | disables getport background fork | MEX-sensitive process behavior | no-op | incomplete |
| `winopen` | `winstuff.c`, gfport + saved `windowstate` + PORTREQ | KGL + MEX | allocate reconstructed window + X11 | major abstraction replacement |
| `reshapeviewport` | viewport from shared window extents | GE/FBC viewport path | refresh host clip geometry | incomplete semantic match |
| `singlebuffer` | immediate GR ioctl + personal mode | KGL `gl_setsinglebuffer` | requested config | evolution question |
| `doublebuffer` | immediate GR ioctl + personal mode | KGL `gl_setdoublebuffer` | requested config | evolution question |
| `RGBmode` | immediate GR ioctl | KGL `gl_setrgbmode` | requested config | evolution question |
| `gconfig` | resolve color + reset writemask | immediate/FBC state | commit backend config | major evolution question |
| `swapbuffers` | compiler/immediate op -> wait for swap | KGL retrace | X11 back-buffer present | visible contract passes; internals incomplete |
| `mapcolor` | shared map ring | KGL/retrace -> hardware LUT | userspace sparse RGB map | incomplete |
| `color` | compiler/immediate attribute op | FBC color command | resolve index -> host RGB | incomplete LUT model |
| `RGBcolor` | compiler/immediate RGB op | FBC RGB command | host RGB | broadly analogous |
| `qtest` | read shared `qtop` | KGL queue | host event poll/cache | minimal substitute |
| `qread` | flush map ring, ioctl, read shared qdev/qvalue | KGL queue/block | host event poll | minimal substitute |

## 14. Immediate next cross-reference work

Before changing runtime semantics, map these late-IRIX points against the old
source:

1. late `gconfig` binary path and any RRM display-mode requests;
2. late `singlebuffer` / `doublebuffer` / `RGBmode` dispatch and when their RRM
   requests become valid;
3. late swap interval and swapbuffer resource ownership;
4. late clip validation vs old `piece`/`rectlist` behavior;
5. late per-window/context attribute storage vs old `windowstate`;
6. late `qread`/`qtest` event transport;
7. late `winopen` X/MEX-era replacement and how much of the old gfport model
   survived;
8. exact CLOVER private-header lineage using all names available in census;
9. old GE/FBC command encoding vs late `gl_i_`/`gl_c_`/`gl_d_` paths, without
   assigning meanings to those prefixes until source/binary evidence proves
   them.

## 15. Reconstruction policy after this discovery

Do not blindly transplant GL2.4 into the portable runtime and do not discard
the late-RRM scaffold. Instead:

```
old SGI source       -> original names, architecture, exact old semantics
late SGI source/API  -> evolved kernel/resource contracts
late libgl binary    -> final implementation cross-check
late stress suite    -> final observable behavior
portable runtime     -> preserve proven architecture; replace hardware/OS glue
```

Where old and late evidence agree, prefer the original SGI architecture and
names. Where they differ, record the evolution point explicitly and target the
late behavior unless the project deliberately exposes a historical GL2 mode.
