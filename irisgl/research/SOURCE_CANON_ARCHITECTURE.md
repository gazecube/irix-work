# IRIS GL source-canon architecture notes

This note records architecture evidence from original SGI source-side artifacts. Binary symbols remain secondary evidence and must not override source names or source behavior.

## Evidence hierarchy

1. Original SGI source
2. Original SGI headers, build files, stress tests, manifests
3. Original SGI binaries/disassembly
4. SGI manpages/documentation
5. Reconstruction inference, explicitly marked

## Separate canonical `gfx` source hierarchy

SGI census/build artifacts in the leaked tree reference a distinct graphics source hierarchy rooted at `gfx/`. The public IRIX source snapshot does not contain that full hierarchy, but the manifests preserve many canonical file names.

Examples recovered from `stand/arcs/IP22prom/census` include:

- `gfx/include/CLOVER1/gl/glcmds.h`
- `gfx/include/CLOVER1/gl/globals.h`
- `gfx/include/CLOVER1/gl/gshm.h`
- `gfx/include/CLOVER1/gl/imattrib.h`
- `gfx/include/CLOVER1/gl/imcmd.h`
- `gfx/include/CLOVER1/gl/imdraw.h`
- `gfx/include/CLOVER1/gl/immatrix.h`
- `gfx/include/CLOVER1/gl/immed.h`
- `gfx/include/CLOVER1/gl/immodes.h`
- `gfx/include/CLOVER1/gl/imsetup.h`
- `gfx/include/CLOVER1/gl/modes.h`
- `gfx/include/CLOVER1/gl/private.h`
- `gfx/include/CLOVER1/gl/shmem.h`
- `gfx/include/CLOVER1/gl/sstatep.h`
- `gfx/include/CLOVER1/gl/wstatep.h`
- `gfx/include/CLOVER2/gl/context.h`
- `gfx/include/CLOVER2/gl/feed.h`
- `gfx/include/CLOVER2/gl/gllocal.h`
- `gfx/include/CLOVER2/gl/globals.h`
- `gfx/include/CLOVER2/gl/gshm.h`
- `gfx/include/CLOVER2/gl/imattrib.h`
- `gfx/include/CLOVER2/gl/imdraw.h`
- `gfx/include/CLOVER2/gl/immatrix.h`
- `gfx/include/CLOVER2/gl/imsetup.h`
- `gfx/include/CLOVER2/gl/modes.h`
- `gfx/include/CLOVER2/gl/private.h`
- `gfx/include/CLOVER2/gl/sstatep.h`
- `gfx/include/CLOVER2/gl/wstatep.h`

The manifest also gives revision/date metadata for many of these files. This is useful for recovering original module names and subsystem boundaries even when the source file body is absent.

A separate diskless-layout document describes machine-specific installed graphics support at `/usr/gfx/libgl/<gfx+cpu arch>/libgl*` and machine-specific graphics include files such as `feed.h` under `/usr/gfx/include/<gfx+cpu arch>/`.

## Installed kernel source headers from the IRIX tree

The supplied IRIX filesystem contains original SGI headers `usr/include/sys/gfx.h` and `usr/include/sys/rrm.h`. These are source-side interface artifacts and are preferred over binary-name inference for kernel/runtime architecture.

### `sys/gfx.h`

`gfx.h` defines device-independent GFX STREAMS ioctls and explicitly describes three classes of graphics commands:

- device-independent GFX operations
- RRM rendering-resource-manager commands
- board-specific commands

The device-independent ioctl set includes board discovery/attachment and operations such as initialize, start, download, blank screen, and mapping.

More importantly, the kernel-side structures explicitly name IrisGL behavior:

- a graphics thread group contains `iglruncnt`, documented as the number of IrisGL group members running
- a thread's graphics connection list is shared for an "irisgl sproc share group"
- shared group data is documented as serving either an IrisGL sproc share group or an OpenGL pthread application
- `GTHREAD_IGL_SGRAPH` marks a member of an IrisGL thread group just given graphics

The header also contains a canonical `SUPPORT_NATIVE_IRISGL` distinction. Its comment states that correct IrisGL sproc support in the face of kernel preemption requires atomic `gfxlock` changes. IP27/IP30/IP32 are excluded from this native-IrisGL path, with the comment explicitly contrasting native IrisGL with "only IGLOO".

This is stronger evidence than the `parallel.c` binary path alone: native IrisGL had kernel-visible shared-process graphics semantics that the implementation must eventually model.

The board data also carries two separate vertical-sync counters:

- `vsync_cnt` for IrisGL, writable by applications
- `vsync_cnt_ogl` for OpenGL, not writable by applications

This is direct source evidence that the kernel intentionally maintained distinct IrisGL/OpenGL synchronization semantics.

### `sys/rrm.h`

`rrm.h` defines the device-independent Rendering Resource Manager contract.

Canonical resource bits include:

- `RRM_RMASK_PCX`: graphics context
- `RRM_RMASK_CLIP`: clipping region
- `RRM_RMASK_BUFFER`: buffer / double buffering
- `RRM_RMASK_SWAPBUF`: ability to swap buffers
- retrace, message, read, and reference-monitor resources

Canonical RRM operations include:

- `RRM_OPENRN` / `RRM_CLOSERN`: open/close rendering node
- `RRM_BINDPROCTORN`: bind process to rendering node
- `RRM_BINDRNTOCLIP` / `RRM_UNBINDRNFROMCLIP`
- `RRM_SWAPBUF`
- `RRM_SETSWAPINTERVAL`
- `RRM_WAITFORRETRACE`
- `RRM_SETDISPLAYMODE`
- `RRM_INVALIDATERN`
- `RRM_VALIDATECLIP`
- `RRM_VALIDATESWAPBUF`
- swap-group operations
- `RRM_SETVSYNC`, `RRM_GETVSYNC`, `RRM_WAITVSYNC`

`struct RRM_SwapBuf` carries a rendering-node ID together with hardware-dependent and hardware-independent display-mode values.

The RRM interface also exposes X-window IDs in swap/cushion-buffer operations, and clip validation records carry window origin/size, clipping piece lists, a window ID, obscured/moved state, hardware-independent mode, and framebuffer offset.

This source evidence strongly suggests that the canonical IRIS GL runtime architecture was not merely "draw into an X window". It maintained a rendering-node/resource model with explicit graphics-context, clipping, buffer, swap-buffer, display-mode, and vsync resources, while X window IDs participated in resource binding and swap management.

## Immediate reconstruction consequences

For the `flash.c` milestone, do not flatten the runtime into a one-off window + pixel-fill shim. The smallest source-faithful internal model should preserve at least these concepts, even if the first implementation has only one concrete backend:

1. process/thread graphics state
2. current rendering/window object
3. requested vs committed framebuffer/display mode (`RGBmode` + `gconfig`)
4. clipping/window geometry state
5. color state
6. a resource/backend boundary for context/buffer/swap handling
7. event/window-system binding kept separate from draw-state logic

Do not yet reproduce native hardware ioctls on Linux. The modern port may replace the kernel/device boundary, but the user-space state transitions and subsystem separations should follow the SGI source model wherever source evidence exists.

## Binary symbols

Names such as `gl_g_*`, `gl_d_*`, `gl_i_*`, and `gl_c_*` found in the IP22 binary are retained only as binary-derived cross-reference labels until source-side names or source structures corroborate them. Their observed behavior may be useful for implementation details, but they are not canonical architecture names by themselves.
