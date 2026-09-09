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

The IP2 user library build enumerates the common implementation directories:

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

and archives their objects as `libgl${PREF}.a`. The corresponding kernel build
archives `kgl/` objects as `libkgl${PREF}.a`.

Therefore the source tree establishes this boundary:

```
application
    |
    v
libgl (user)
    compiler / curve / feedback / hardcopy
    immediate / object / services / windows
    |
    | grioctl + shared memory + command stream
    v
libkgl (kernel)
    grioctl / gr / retrace / queue / devices
    GE / FBC / cursor / font / textport / ...
    |
    v
GE/FBC/DC/UC hardware and ucode
```

## 2. Shared user/kernel state

`include/shmem.h` calls `struct shmem` the shared-memory data structure for
cooperative manipulation of graphics hardware between kernel and user, and
records compatibility string `GLVERSION "GL2.4"`.

Important fields include the shared `windowstate`, current input channel and
gfport, queue state, GE information, screen blanking, a small command/response
buffer, and a mapcolor producer/consumer ring.

`globals.h` exposes the current shared window state as `gl_wstatep` and also
contains user-library state including `gl_personal_buffer_mode`.

## 3. Original windowstate

`include/gltypes.h` defines the actual `windowstate` shared by user and kernel.
It contains graphics/character position, backface and zbuffer modes, shade and
line state, window bounds and visible rectangle list, viewport stack, matrix
stack, attribute stack, bitplane mask, and font/pattern/cursor RAM state.

`struct atrdata` contains the hardware configuration word, line style,
texture, current color index and write mask, line width/repeat, current font,
RGB color, and RGB write masks.

This is important to the late comparison: core current rendering attributes
are attached to a window state, not represented only as process-global color
variables.

## 4. Kernel window/input model

`include/window.h` defines distinct kernel objects:

- `piece`: one visible rectangle of an obscured window.
- `gfport`: graphics port with owner/input channel, bounds, and piece list.
- `txport`: text-port state.
- `inputchan`: process/input state including queue, current gfport, swap
  interval, display mode, cursor offsets, and device state.

Display modes are `MD_SINGLE`, `MD_DOUBLE`, and `MD_RGB`.

This already resembles concepts that later appear as distinct RRM resources:
process binding, clipping, buffer/display mode, swap state, and retrace.

## 5. User/kernel control path

`common/services/grioctl.c` is the user control-wrapper layer. Examples:

```
qdevice      -> grioctl(GR_QDEVICE)
qread        -> flush mapcolor; GR_QREAD; shared qdev/qvalue
qtest        -> shared qtop
RGBmode      -> flush mapcolor; GR_RGBMODE
singlebuffer -> GR_SINGLEBUFFER
doublebuffer -> GR_DOUBLEBUFFER
mapcolor     -> shared producer/consumer ring
swapinterval -> GR_SWAPINTERVAL
```

`kgl/grioctl.c` is the matching kernel dispatcher:

```
GR_SINGLEBUFFER -> gl_setsinglebuffer()
GR_DOUBLEBUFFER -> gl_setdoublebuffer()
GR_RGBMODE      -> gl_setrgbmode()
GR_WAITFORSWAP  -> dowaitforswap()
GR_QREAD        -> doqread()
```

## 6. GL2.4 buffer/display mode path

In GL2.4, `singlebuffer()`, `doublebuffer()`, and `RGBmode()` are not merely
userspace requests waiting for `gconfig()` to commit them. Their wrappers
immediately issue graphics ioctls. `gl_singlebuffer()` and
`gl_doublebuffer()` also update `gl_personal_buffer_mode`.

KGL's `gl_setsinglebuffer()`, `gl_setdoublebuffer()`, and `gl_setrgbmode()` in
`kgl/retrace.c` immediately update process display mode, global counts of
buffer/RGB users, global hardware/textport mode when first/last users cross a
boundary, `windowstate.curatrdata.myconfig`, bitplane masks, and hardware
configuration via `setconfig()`.

The old global graphics mode is therefore shared and reference-counted across
processes.

### GL2.4 `gconfig()`

The old implementation is much narrower:

```
gl_resolvecolor();
if (getdisplaymode())
    writemask((1 << getplanes())-1);
else
    RGBwritemask(0xff, 0xff, 0xff);
```

It resolves color mode and resets the appropriate write mask. It does not
perform the single/double transition in this implementation.

## 7. Swap path

`common/compiler/buffer.c` makes the immediate `swapbuffers` operation wait
only when the personal mode is double buffered and the window hardware config
also contains `UC_DOUBLE`.

The wait crosses through `gl_waitforswap()` -> `GR_WAITFORSWAP` -> KGL
`dowaitforswap()`. KGL owns per-input-channel swap interval and retrace
scheduling.

Our X11 Pixmap backend satisfies the visible `badifred` contract, but is only a
host approximation of this deeper retrace/bank model.

## 8. Color path

### Color-index

`color()` masks the index with `WS->bitplanemask`, stores it in
`WS->curatrdata.mycolor`, and emits an FBC color command.

`mapcolor()` instead writes index/R/G/B into a 64-entry shared ring. KGL
consumes those updates independently. This proves true palette semantics:
framebuffer pixels contain indices and palette mutation is independent from
redrawing those pixels.

Our current index-to-host-RGB-at-`color()` implementation is therefore a
functional bring-up approximation, not faithful indexed framebuffer behavior.

### RGB

`RGBcolor` and `RGBwritemask` store their values in current `windowstate`
attributes and emit FBC commands.

## 9. Window-manager boundary

`common/windows/hints.c` is explicitly the hints interface to the window
manager. It establishes old semantics for `prefsize`, `prefposition`,
`foreground`, `getport`, and `reshapeviewport`.

`prefposition` normalizes endpoint order with MIN/ABS and uses inclusive `+1`
dimensions. Our current implementation assumes increasing endpoints, so this
is a known old-version mismatch awaiting late-version confirmation.

`common/windows/winstuff.c` describes itself as the procedural interface to
MEX. `winopen` allocates a KGL gfport, allocates/saves a `windowstate`, switches
to it, sends a `PORTREQ` to `WINDOWMAN`, initializes stacks and viewport, then
emits current buffer configuration.

Thus old IRIS GL separates KGL graphics-port/resource management from explicit
MEX policy IPC.

## 10. Compiler vs immediate architecture

The tree establishes separate compiled/object and immediate execution forms.
`common/compiler` contains generated/root/interpreter forms for state and draw
operations, while `common/object` owns retained display-list storage/editing.
Private headers such as `imattrib.h`, `imdraw.h`, `immatrix.h`, `immed.h`, and
`imsetup.h` define immediate hardware-command forms.

The same private-header family names recur in the late IRIX 6.5 CLOVER census,
which is a strong architectural lineage marker.

## 11. 3.x -> 6.5 structural cross-reference

Strong conceptual correspondences, without claiming identical layouts:

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
gr_ioctl dispatcher             GFX/RRM ioctl/operation layer
GE/FBC command generation       late device-dependent command path
MEX explicit IPC                later window-system integration
```

Private-header-name continuity:

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

## 12. Late IRIX buffer/configuration evidence

This section resolves what can and cannot currently be claimed about the
GL2.4-vs-6.5 buffer-mode discrepancy.

### 12.1 Source-proven late kernel contract

The surviving IRIX 6.5 source-side headers establish a Rendering Resource
Manager with separate resources for:

```
RRM_RMASK_PCX       graphics context
RRM_RMASK_CLIP      clipping
RRM_RMASK_BUFFER    buffer/double-buffer resource
RRM_RMASK_SWAPBUF   swap-buffer resource
RRM_RMASK_RETRACE   retrace
RRM_RMASK_MESSAGE   message
RRM_RMASK_READ      read
RRM_RMASK_REFERENCE reference-monitor resource
```

The late RRM operation set includes `RRM_OPENRN`, process/rendering-node
binding, clip binding/validation, `RRM_SWAPBUF`, `RRM_SETSWAPINTERVAL`,
`RRM_WAITFORRETRACE`, `RRM_SETDISPLAYMODE`, invalidation/validation operations,
and vsync operations.

This proves that by late IRIX, buffer/display/swap configuration had become an
explicit rendering-resource contract rather than the old GL2.4 global
`inputchan` + hardware-mode counter arrangement.

It does NOT by itself prove which public call (`doublebuffer` or `gconfig`)
causes each RRM operation.

### 12.2 Source-proven late native-IrisGL distinction

`sys/gfx.h` explicitly retains native IrisGL kernel semantics, including
IrisGL sproc graphics groups, IrisGL-specific run counts, a
`GTHREAD_IGL_SGRAPH` state, and separate IrisGL/OpenGL vsync counters. It also
contains a `SUPPORT_NATIVE_IRISGL` distinction for machine families.

Therefore the late library was not merely a compatibility veneer over OpenGL;
it still participated in a kernel-visible IrisGL-specific resource model.

### 12.3 Binary-proven late dispatch

The IP22 `libgl.so` gives these original internal implementation names for the
relevant public calls:

```
RGBmode  -> gl_g_RGBmode / gl_d_RGBmode
gconfig  -> gl_g_gconfig / gl_d_gconfig
```

The prefix meanings remain intentionally unresolved.

`gl_d_gconfig` encodes command word `0x2e` into the same internal command
transport used by `gl_d_RGBcolor` and `gl_d_clear`. Therefore `gconfig` remains
a first-class operation in the late implementation and is not merely an empty
legacy wrapper.

This is compatible with, but does not alone prove, a later request/commit
model.

### 12.4 What is NOT yet proven

We do not currently possess the late CLOVER implementation bodies for
`singlebuffer`, `doublebuffer`, `RGBmode`, or `gconfig`. The surviving public
source tree contains the RRM contract and manifests naming private GL headers,
but not enough implementation source to place the public-call commit boundary
with source-level certainty.

Accordingly:

```
Claim                                                     Status
--------------------------------------------------------  --------
GL2.4 buffer mode changes immediately in KGL              PROVEN
GL2.4 gconfig does not perform buffer transition          PROVEN
6.5 has explicit RRM buffer/swap/display-mode resources   PROVEN
6.5 gconfig is a real encoded late GL operation           PROVEN
6.5 doublebuffer only records a request                   NOT PROVEN
6.5 gconfig performs the RRM buffer commit                NOT PROVEN
6.5 retained GL2.4 immediate mode-change semantics        NOT PROVEN
```

This corrects an earlier reconstruction assumption: requested-vs-committed
buffer state remains a useful RRM-shaped scaffold, but it must not be described
as source-proven late public API semantics yet.

### 12.5 Stress-test evidence

The surviving 6.5 stress programs deliberately use sequences such as:

```
doublebuffer();
gconfig();
```

and repeatedly:

```
singlebuffer();
gconfig();
...
doublebuffer();
gconfig();
```

`badifred` and `rbuf` therefore establish that `gconfig` is part of the
supported late configuration protocol. They do not distinguish whether the
preceding mode call is immediate, deferred, or partially immediate.

The tests are behavioral contracts, not sufficient evidence for an internal
commit boundary.

## 13. Current runtime cross-reference

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

- Window/clipping state is not flattened into drawing calls.
- Backend/resource separation leaves room for replacing X11.
- Buffer/swap state is explicit.
- Window preferences are separate from current rendering state.
- Late RRM concepts are represented instead of pretending X11 itself is IRIS
  GL.

### Known mismatches or unresolved assumptions

1. **Buffer commit boundary**
   Current `singlebuffer`/`doublebuffer` modify `requested`, while `gconfig`
   commits the backend. This matches a plausible interpretation of late RRM
   configuration but is NOT yet source/binary proven. GL2.4 is explicitly
   different.

2. **`gconfig` behavior**
   Current `gconfig` commits backend double buffering and resets color state.
   GL2.4 instead resolves color and resets write masks. Late `gconfig` is
   binary-proven to remain a real operation, but its full semantics remain
   unresolved.

3. **Colormap semantics**
   Current sparse map resolves index to host RGB at draw time. GL2.4 has a
   genuine independently mutable palette.

4. **Current state granularity**
   Current RGB/index values are process-global. GL2.4 puts core attributes in
   per-window `windowstate`, with save/restore on `winset`.

5. **Multiple windows**
   Current runtime has one current allocated window and no old gfport-style
   saved-state switching.

6. **Events**
   Current event handling is minimal compared with GL2.4 per-input-channel
   queues/devices/timers/retrace integration.

7. **Swap/retrace**
   Current X11 presentation is immediate. GL2.4 presentation is
   kernel/retrace coordinated. Late RRM also retains explicit swap/retrace
   resources.

8. **Window-manager IPC**
   Current backend directly creates/manages X windows. GL2.4 separates gfport
   allocation from MEX policy IPC.

9. **`prefposition` normalization**
   Old source accepts reversed endpoints; current code does not. Late behavior
   remains to be checked.

## 14. API evolution matrix

| API | GL2.4 | Late 6.5 evidence | Current portable | Disposition |
| --- | --- | --- | --- | --- |
| `prefsize` | MEX `portreq` hint | late `gl_g/gl_d_prefsize` symbols | next-window X11 preference | structurally plausible |
| `prefposition` | normalized MEX origin+size | late dispatch symbols | non-normalizing preference | old mismatch; late TBD |
| `foreground` | suppress background fork under MEX | late dispatch symbols | no-op | incomplete |
| `winopen` | gfport + `windowstate` + MEX `PORTREQ` | large X11-integrated `gl_g_winopen` | backend X window | intentionally substituted boundary |
| `RGBmode` | immediate `GR_RGBMODE` -> KGL | RRM display-mode contract + late dispatch | requested state | commit timing unresolved |
| `singlebuffer` | immediate `GR_SINGLEBUFFER` -> KGL | RRM buffer/display-mode contract | requested state | commit timing unresolved |
| `doublebuffer` | immediate `GR_DOUBLEBUFFER` -> KGL | RRM buffer/display-mode contract | requested state | commit timing unresolved |
| `gconfig` | resolve color + reset writemask | real late operation; `gl_d_gconfig` opcode `0x2e` | commits backend + resets color | late semantics unresolved |
| `swapbuffers` | KGL/retrace wait | RRM swap/retrace resources | backend present/copy | visible contract passes; deeper model incomplete |
| `mapcolor` | shared LUT update ring | late implementation not yet mapped | userspace sparse RGB map | known semantic approximation |
| `color` | masked index -> FBC | late implementation not yet mapped | index -> RGB lookup | known semantic approximation |
| `RGBcolor` | per-window attr -> FBC | late `i/c/d` implementations mapped | process-global RGB -> backend | granularity mismatch |
| `clear` | compiler/immediate FBC operation | late `i/c/d` implementations mapped | backend clear | basic visible semantics only |
| `reshapeviewport` | viewport from shared window extents | late dispatch symbols | refresh host geometry/clip | partial |

## 15. Decision for the portable runtime

No buffer/gconfig semantic rewrite is justified by the current evidence.

The correct next action is to keep the existing passing bring-up behavior while
continuing binary archaeology specifically on the late `gl_g/gl_d` buffer mode
entry points and their relationship to `gl_g/gl_d_gconfig` and RRM calls.

When that boundary is resolved, change the runtime once, from evidence, rather
than oscillating between the GL2.4 and guessed late models.

## 16. Next binary/source targets

Priority order:

1. Recover/disassemble late `singlebuffer` and `doublebuffer` implementation
   families and their public wrappers.
2. Fully disassemble `gl_g_RGBmode`, `gl_d_RGBmode`, `gl_g_gconfig`, and
   `gl_d_gconfig` and resolve callees/GOT targets.
3. Identify late calls or command records corresponding to
   `RRM_SETDISPLAYMODE`, buffer validation, and swap-buffer validation.
4. Map `swapbuffers`/swapinterval/retrace late paths.
5. Map late `color`/`mapcolor` and determine whether palette semantics remain
   hardware-indexed on the target path.
6. Compare private CLOVER manifest/header names against GL2.4 state fields and
   recover late layouts wherever binaries expose offsets.
7. Only then revise `igl_state.h` and public mode/gconfig semantics.
