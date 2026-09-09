# IRIS GL flash.c minimal runtime state

This note defines the smallest internal state model we can justify from original SGI source evidence for the first bring-up target, `irix/cmd/stress/Gfx/flash.c`.

## Evidence policy

Source is canonical. The installed IRIX headers from the extracted IRIX tree are the primary basis here:

- `/usr/include/sys/gfx.h`
- `/usr/include/sys/rrm.h`

Binary names and disassembly are cross-checks only when source is silent.

## Canonical source-side objects

### Rendering node: `struct rrm_rnode`

The RRM defines a device-independent rendering node with these concepts relevant to early IRIS GL bring-up:

- `gr_rnid`: rendering-node ID
- `gr_reqmask`: requested rendering resources
- `gr_validmask`: currently valid rendering resources
- `gr_pcx`: graphics/pipe context pointer
- `gr_pcxid`: hardware context ID
- `gr_clipid`: opaque clip-region ID
- `gr_readclipid`: opaque read clip-region ID
- `gr_swapbufid`: opaque swap-buffer/display ID
- `gr_swapinterval`: swap interval
- association with the creating/current graphics connection and board

The source-defined resource bits are:

- `RRM_RMASK_PCX`: graphics context
- `RRM_RMASK_CLIP`: clipping region
- `RRM_RMASK_BUFFER`: double-buffering resource
- `RRM_RMASK_SWAPBUF`: ability to swap buffers
- `RRM_RMASK_RETRACE`: retrace event
- `RRM_RMASK_MESSAGE`
- `RRM_RMASK_READ`
- `RRM_RMASK_REFERENCE`

For `flash.c`, source evidence proves that context, clipping/window state, and display-mode state are architectural concepts that must exist. It does not yet prove the exact `gr_reqmask` value chosen by `winopen()`/`gconfig()`, so that transition remains unresolved.

### Pane: `struct pane`

The RRM pane cache contains the per-window/display-facing state. Relevant fields include:

- `p_clipid`: opaque clip ID; the source comment explicitly equates this with the window for RRM purposes
- `p_validmask`: valid rendering resources
- `p_waitmask`: resources awaiting validation
- `p_displaybank`: currently displayed buffer
- `p_swapbufid`: swap-buffer/display ID
- `p_swapinterval`
- `p_hwi_mode`: hardware-independent display mode
- `p_vclip`: cached `RRM_ValidateClip`

This is the strongest source evidence so far that IRIS GL window state was not merely an X window plus drawing globals. Window geometry, clipping, display-bank state, and display mode are cached and validated as rendering resources.

### Clip validation: `struct RRM_ValidateClip`

The canonical clip-validation structure carries:

- `rnid`
- `clipid`
- window origin: `xorg`, `yorg`
- window size: `xsize`, `ysize`
- clip-piece count and piece list
- `wid`
- obscured flag
- WID-check flag
- movement/size-change state
- current hardware-independent mode
- framebuffer offset

The piece-list element contains lower-left origin plus width/height.

### Display mode and buffering

The RRM defines explicit operations for:

- `RRM_SETDISPLAYMODE`
- `RRM_GETDISPLAYMODES`
- `RRM_LOADDISPLAYMODE`
- `RRM_SWAPBUF`
- `RRM_VALIDATESWAPBUF`
- swap interval and retrace/vsync operations

This is source-level justification for keeping a real `gconfig()` commit phase instead of treating `RGBmode()` as an immediate backend toggle.

## Minimal reconstructed userspace state

The following userspace objects are reconstructed names. They are not claimed to be original SGI libgl struct names. Their fields mirror only source-proven concepts.

### `igl_rnode_state` [reconstructed name]

Minimum fields:

- rendering-node ID
- requested resource mask
- valid resource mask
- context handle/opaque pointer
- context ID
- clip ID
- read clip ID
- swap-buffer ID

### `igl_pane_state` [reconstructed name]

Minimum fields:

- clip ID
- valid/wait resource masks
- display bank
- swap-buffer ID
- swap interval
- hardware-independent display mode
- current clip geometry and pieces

### `igl_window_state` [reconstructed name]

Userspace association needed by the modern host boundary:

- IRIS GL window ID
- host-window handle
- requested/pending size or position from `prefsize()` / `prefposition()`
- associated reconstructed pane state
- associated reconstructed rendering-node state

The host-window handle is explicitly a porting addition, not an SGI field.

### `igl_config_state` [reconstructed name]

Two phases are required:

1. requested/pending configuration
2. committed configuration after `gconfig()`

For the `flash.c` milestone the only proven requested mode is RGB mode. Later stress tests add color-index mode and single/double-buffer transitions.

### `igl_runtime_state` [reconstructed name]

Minimum process/thread-level state:

- current IRIS GL window/rendering node
- next IDs for reconstructed userspace objects
- current RGB drawing color
- event queue
- host/backend connection

The source also proves that thread-group graphics state exists in the kernel (`gthread_t`, `gfx_group_t`) and that native IrisGL sproc support has special scheduling/locking semantics. We do not need to implement that for `flash.c`, but the runtime must not be designed in a way that makes later shared-group support impossible.

## flash.c call-to-state map

### `prefsize()` / `prefposition()`

Populate pending window geometry. No rendering-node mutation should be assumed until confirmed from source/binary evidence.

### `foreground()`

Behavior remains to be pinned down from source/manpage/binary evidence. Do not invent focus or parenting semantics yet.

### `winopen()`

Must create the userspace window association and establish the current rendering/window object. Source evidence requires us to have places for a rendering node, clip state, and display-mode state even if the first host backend is minimal.

### `RGBmode()`

Marks a requested display/framebuffer mode. It should not silently bypass the later `gconfig()` commit.

### `gconfig()`

Commits pending configuration into active window/rendering state and backend state. Exact RRM requests/masks used by original libgl remain to be recovered.

### `RGBcolor()`

Updates current RGB drawing state.

### `clear()`

Consumes the committed framebuffer/display configuration and current drawing color. The host backend is responsible for carrying out the actual clear, but the public IRIS GL call must operate through the runtime state rather than directly through X11.

### `qtest()` / `qread()` / `reshapeviewport()`

Operate on the IRIS GL event/window state. `REDRAW` handling must eventually update clip/geometry state consistently with the RRM model.

## Explicit non-goals for milestone 0

Do not implement yet:

- matrices
- geometry submission
- Z buffering
- lighting/materials
- object system
- fonts
- multiple current windows
- double buffering / swap
- sproc sharing
- retrace/vsync semantics

The design should reserve room for those canonical RRM concepts but should not speculate about their libgl-side representation.

## Next code step

Create a private internal header with reconstructed userspace structs matching the concepts above, clearly annotate every reconstructed name, and keep the host backend behind a narrow interface. Then implement only enough lifecycle/config state transitions to let the original SGI `flash.c` compile and exercise the runtime without flattening `gconfig()` or clip/resource state into direct Xlib calls.
