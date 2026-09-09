#ifndef IRISGL_IGL_STATE_H
#define IRISGL_IGL_STATE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Private reconstruction scaffold for the first IRIS GL bring-up slice.
 *
 * Canonical concepts are taken from original SGI sys/gfx.h and sys/rrm.h.
 * Struct names beginning with igl_ are reconstructed names, not claimed to
 * be original libgl private names.
 */

/* Canonical RRM resource-mask values from SGI sys/rrm.h. */
enum {
    IGL_RMASK_PCX       = 0x00000001u,
    IGL_RMASK_CLIP      = 0x00000002u,
    IGL_RMASK_BUFFER    = 0x00000004u,
    IGL_RMASK_SWAPBUF   = 0x00000008u,
    IGL_RMASK_RETRACE   = 0x00000010u,
    IGL_RMASK_MESSAGE   = 0x00000020u,
    IGL_RMASK_READ      = 0x00000040u,
    IGL_RMASK_REFERENCE = 0x00000080u
};

/* Reconstructed counterpart of the source-defined RRM clip piece concept. */
struct igl_clip_piece {
    int x;
    int y;
    int xsize;
    int ysize;
};

/*
 * Reconstructed userspace clip state.
 * Fields mirror source-proven RRM_ValidateClip concepts only.
 */
struct igl_clip_state {
    int rnid;
    int clipid;
    int xorg;
    int yorg;
    int xsize;
    int ysize;
    int numpieces;
    struct igl_clip_piece *pieces;
    int wid;
    int obscured;
    int widcheck;
    int changed;
    uint32_t hwi_mode;
    int fboffset;
};

/*
 * Reconstructed userspace rendering-node state.
 * Canonical field concepts come from struct rrm_rnode.
 */
struct igl_rnode_state {
    int rnid;
    uint16_t reqmask;
    uint16_t validmask;
    void *pcx;
    int pcxsize;
    int clipid;
    int readclipid;
    int swapbufid;
    int swapinterval;
    int16_t pcxid;
};

/*
 * Reconstructed userspace pane state.
 * Canonical field concepts come from struct pane.
 */
struct igl_pane_state {
    int clipid;
    uint16_t validmask;
    uint16_t waitmask;
    int16_t displaybank;
    int swapbufid;
    int swapinterval;
    uint32_t hwi_mode;
    struct igl_clip_state clip;
};

/*
 * Reconstructed window preference state.
 * This is userspace bookkeeping for prefsize()/prefposition(); it is not
 * claimed to match an original SGI private structure.
 */
struct igl_window_preferences {
    int have_size;
    int width;
    int height;

    int have_position;
    int xmin;
    int xmax;
    int ymin;
    int ymax;
};

/*
 * Reconstructed configuration state.
 * Keep requested and committed state separate so RGBmode()/cmode() and
 * buffer-mode requests are not flattened across gconfig().
 */
struct igl_config_state {
    int rgb_mode;
    int double_buffer;
};

/*
 * Sparse reconstructed userspace representation of IRIS GL colormap state.
 * Original source establishes indexed LUT semantics; this deliberately does
 * not invent a maximum index before source evidence identifies one.
 */
struct igl_colormap_entry {
    long index;
    int16_t red;
    int16_t green;
    int16_t blue;
    struct igl_colormap_entry *next;
};

/* Host/backend object. Layout deliberately hidden from the IRIS GL core. */
struct igl_backend_window;
struct igl_backend;
struct igl_event_queue;

/* Reconstructed userspace window association. */
struct igl_window_state {
    long wid;
    struct igl_window_preferences prefs;

    struct igl_config_state requested;
    struct igl_config_state committed;

    struct igl_rnode_state rnode;
    struct igl_pane_state pane;

    struct igl_backend_window *backend_window;
};

/* Reconstructed process/runtime state for milestone 0. */
struct igl_runtime_state {
    struct igl_window_preferences next_window_prefs;
    struct igl_window_state *current_window;

    int next_rnid;
    int next_clipid;
    long next_wid;

    int16_t current_red;
    int16_t current_green;
    int16_t current_blue;
    long current_color_index;
    struct igl_colormap_entry *colormap;

    struct igl_event_queue *events;
    struct igl_backend *backend;
};

#endif /* IRISGL_IGL_STATE_H */
