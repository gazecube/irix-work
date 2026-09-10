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

struct igl_clip_piece {
    int x;
    int y;
    int xsize;
    int ysize;
};

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

struct igl_config_state {
    int rgb_mode;
    int double_buffer;
};

struct igl_colormap_entry {
    long index;
    int16_t red;
    int16_t green;
    int16_t blue;
    struct igl_colormap_entry *next;
};

struct igl_backend_window;
struct igl_backend;
struct igl_event_queue;
struct igl_juniper_device;
struct igl_juniper_bo;

/*
 * Reconstructed host color-surface association.
 * This is host implementation state, not an original SGI structure.
 *
 * Single-buffer mode uses bo[0]. Double-buffer mode uses both entries; draw
 * selects the back surface receiving commands, and front records the last
 * surface handed to Present. A front value of -1 means no Juniper surface
 * has been presented yet.
 *
 * token[] is a host lifetime identity, deliberately separate from the Radeon
 * GEM handle. GEM handles may be reused immediately after gconfig() replaces
 * a surface, while a cached DRI3 pixmap can still refer to the old storage.
 * native_present becomes true only after a Juniper BO has completed the
 * DRI3/Present path, allowing the compatibility X11 mirror to be suppressed.
 */
struct igl_host_color_surface {
    struct igl_juniper_bo *bo[2];
    uint32_t token[2];
    int width;
    int height;
    int stride;
    int count;
    int draw;
    int front;
    int native_present;
};

struct igl_window_state {
    long wid;
    struct igl_window_preferences prefs;

    struct igl_config_state requested;
    struct igl_config_state committed;

    struct igl_rnode_state rnode;
    struct igl_pane_state pane;

    struct igl_backend_window *backend_window;
    struct igl_host_color_surface host_color;
};

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
    struct igl_juniper_device *juniper;
};

#endif /* IRISGL_IGL_STATE_H */
