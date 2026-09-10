#include <gl/gl.h>
#include <gl/device.h>

#include "igl_backend.h"
#include "igl_juniper_runtime.h"
#include "igl_state.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct igl_runtime_state igl;
static int igl_initialized;

static struct igl_colormap_entry *igl_find_colormap_entry(Colorindex index,
                                                           int create)
{
    struct igl_colormap_entry *entry;

    for (entry = igl.colormap; entry; entry = entry->next) {
        if (entry->index == (long)index)
            return entry;
    }

    if (!create)
        return NULL;

    entry = calloc(1, sizeof(*entry));
    if (!entry)
        return NULL;

    entry->index = (long)index;
    entry->next = igl.colormap;
    igl.colormap = entry;
    return entry;
}

static void igl_set_colormap_entry(Colorindex index,
                                   RGBvalue red,
                                   RGBvalue green,
                                   RGBvalue blue)
{
    struct igl_colormap_entry *entry;

    entry = igl_find_colormap_entry(index, 1);
    if (!entry)
        return;

    entry->red = (int16_t)red;
    entry->green = (int16_t)green;
    entry->blue = (int16_t)blue;
}

static void igl_init_default_colormap(void)
{
    igl_set_colormap_entry(BLACK,   0,   0,   0);
    igl_set_colormap_entry(RED,   255,   0,   0);
    igl_set_colormap_entry(GREEN,   0, 255,   0);
    igl_set_colormap_entry(YELLOW,255, 255,   0);
    igl_set_colormap_entry(BLUE,    0,   0, 255);
    igl_set_colormap_entry(MAGENTA,255, 0, 255);
    igl_set_colormap_entry(CYAN,    0, 255, 255);
    igl_set_colormap_entry(WHITE, 255, 255, 255);
}

static void igl_init_once(void)
{
    if (igl_initialized)
        return;

    memset(&igl, 0, sizeof(igl));
    igl.next_rnid = 1;
    igl.next_clipid = 1;
    igl.next_wid = 1;
    igl_init_default_colormap();
    igl.backend = igl_backend_create();
    igl_juniper_runtime_init(&igl);
    igl_initialized = 1;
}

static void igl_refresh_clip(struct igl_window_state *window)
{
    int x = 0, y = 0, width = 0, height = 0;

    if (!window || !window->backend_window || !igl.backend)
        return;

    if (!igl_backend_get_geometry(igl.backend, window->backend_window,
                                  &x, &y, &width, &height))
        return;

    window->pane.clip.xorg = x;
    window->pane.clip.yorg = y;
    window->pane.clip.xsize = width;
    window->pane.clip.ysize = height;
    window->pane.clip.changed = 0;
    window->pane.validmask |= IGL_RMASK_CLIP;
    window->rnode.validmask |= IGL_RMASK_CLIP;
}

static uint32_t igl_host_rgba8(void)
{
    uint32_t red = (uint16_t)igl.current_red & 0xffu;
    uint32_t green = (uint16_t)igl.current_green & 0xffu;
    uint32_t blue = (uint16_t)igl.current_blue & 0xffu;

    /* Reconstructed private host surface format: 0xAARRGGBB. */
    return 0xff000000u | (red << 16) | (green << 8) | blue;
}

void prefsize(long width, long height)
{
    igl_init_once();
    igl.next_window_prefs.have_size = 1;
    igl.next_window_prefs.width = (int)width;
    igl.next_window_prefs.height = (int)height;
}

void prefposition(long xmin, long xmax, long ymin, long ymax)
{
    igl_init_once();
    igl.next_window_prefs.have_position = 1;
    igl.next_window_prefs.xmin = (int)xmin;
    igl.next_window_prefs.xmax = (int)xmax;
    igl.next_window_prefs.ymin = (int)ymin;
    igl.next_window_prefs.ymax = (int)ymax;

    igl.next_window_prefs.have_size = 1;
    igl.next_window_prefs.width = (int)(xmax - xmin + 1);
    igl.next_window_prefs.height = (int)(ymax - ymin + 1);
}

void foreground(void)
{
    igl_init_once();
}

long winopen(const char *name)
{
    struct igl_window_state *window;
    int x = 0;
    int y = 0;
    int width = 100;
    int height = 100;

    igl_init_once();
    if (!igl.backend)
        return -1;

    window = calloc(1, sizeof(*window));
    if (!window)
        return -1;

    window->prefs = igl.next_window_prefs;
    if (window->prefs.have_size) {
        width = window->prefs.width;
        height = window->prefs.height;
    }
    if (window->prefs.have_position) {
        x = window->prefs.xmin;
        y = window->prefs.ymin;
    }

    window->wid = igl.next_wid++;
    window->rnode.rnid = igl.next_rnid++;
    window->rnode.clipid = igl.next_clipid++;
    window->pane.clipid = window->rnode.clipid;
    window->pane.clip.rnid = window->rnode.rnid;
    window->pane.clip.clipid = window->rnode.clipid;

    window->backend_window = igl_backend_open_window(
        igl.backend, name, x, y, width, height,
        window->prefs.have_position);
    if (!window->backend_window) {
        free(window);
        return -1;
    }

    igl.current_window = window;
    memset(&igl.next_window_prefs, 0, sizeof(igl.next_window_prefs));
    igl_refresh_clip(window);
    igl_juniper_runtime_ensure_surface(&igl, window);
    return window->wid;
}

void RGBmode(void)
{
    igl_init_once();
    if (!igl.current_window)
        return;

    igl.current_window->requested.rgb_mode = 1;
    igl.current_window->rnode.reqmask |= IGL_RMASK_BUFFER;
}

void cmode(void)
{
    igl_init_once();
    if (!igl.current_window)
        return;

    igl.current_window->requested.rgb_mode = 0;
    igl.current_window->rnode.reqmask |= IGL_RMASK_BUFFER;
}

void singlebuffer(void)
{
    igl_init_once();
    if (!igl.current_window)
        return;

    igl.current_window->requested.double_buffer = 0;
    igl.current_window->rnode.reqmask |= IGL_RMASK_BUFFER | IGL_RMASK_SWAPBUF;
}

void doublebuffer(void)
{
    igl_init_once();
    if (!igl.current_window)
        return;

    igl.current_window->requested.double_buffer = 1;
    igl.current_window->rnode.reqmask |= IGL_RMASK_BUFFER | IGL_RMASK_SWAPBUF;
}

void gconfig(void)
{
    struct igl_window_state *window;

    igl_init_once();
    window = igl.current_window;
    if (!window)
        return;

    if (!igl_backend_set_doublebuffer(igl.backend,
                                      window->backend_window,
                                      window->requested.double_buffer))
        return;

    window->committed = window->requested;
    window->rnode.validmask |= window->rnode.reqmask;
    window->pane.validmask |= window->rnode.reqmask;
    window->rnode.reqmask = 0;

    igl.current_red = 0;
    igl.current_green = 0;
    igl.current_blue = 0;
    igl.current_color_index = 0;
    igl_backend_set_rgb(igl.backend, window->backend_window, 0, 0, 0);

    igl_refresh_clip(window);
    igl_juniper_runtime_ensure_surface(&igl, window);
}

void gsync(void)
{
    igl_init_once();
    if (!igl.current_window)
        return;

    igl_backend_gsync(igl.backend, igl.current_window->backend_window);
}

void RGBcolor(short red, short green, short blue)
{
    igl_init_once();
    if (!igl.current_window)
        return;

    igl.current_red = red;
    igl.current_green = green;
    igl.current_blue = blue;
    igl_backend_set_rgb(igl.backend, igl.current_window->backend_window,
                        red, green, blue);
}

void mapcolor(Colorindex index, RGBvalue red, RGBvalue green, RGBvalue blue)
{
    igl_init_once();
    igl_set_colormap_entry(index, red, green, blue);
}

void color(Colorindex index)
{
    struct igl_colormap_entry *entry;

    igl_init_once();
    if (!igl.current_window)
        return;

    igl.current_color_index = (long)index;
    entry = igl_find_colormap_entry(index, 0);
    if (!entry)
        return;

    igl.current_red = entry->red;
    igl.current_green = entry->green;
    igl.current_blue = entry->blue;
    igl_backend_set_rgb(igl.backend, igl.current_window->backend_window,
                        entry->red, entry->green, entry->blue);
}

void clear(void)
{
    int hardware_clear;
    struct igl_window_state *window;

    igl_init_once();
    window = igl.current_window;
    if (!window)
        return;

    hardware_clear = igl_juniper_runtime_clear(&igl, window, igl_host_rgba8());

    if (hardware_clear && !window->committed.double_buffer) {
        if (igl_juniper_runtime_present(&igl, window))
            return;
    } else if (hardware_clear && window->host_color.native_present) {
        /*
         * A native double-buffer Present has already completed for this
         * surface set, so the Juniper draw BO is now the authoritative back
         * buffer. Do not also paint the compatibility X11 backbuffer.
         */
        return;
    }

    igl_backend_clear(igl.backend, window->backend_window);
}

void swapbuffers(void)
{
    igl_init_once();
    if (!igl.current_window || !igl.current_window->committed.double_buffer)
        return;

    if (igl_juniper_runtime_swap(&igl, igl.current_window))
        return;

    igl_backend_swapbuffers(igl.backend, igl.current_window->backend_window);
}

long qtest(void)
{
    struct igl_backend_window *backend_window = NULL;
    long device = 0;
    short value = 0;

    igl_init_once();
    if (!igl.backend)
        return 0;

    if (!igl_backend_poll_event(igl.backend, &backend_window,
                                &device, &value, 0))
        return 0;

    igl.events = (struct igl_event_queue *)(uintptr_t)device;
    return device;
}

long qread(short *value)
{
    long device;
    struct igl_backend_window *backend_window = NULL;
    short event_value = 0;

    igl_init_once();

    if (igl.events) {
        device = (long)(uintptr_t)igl.events;
        igl.events = NULL;
        if (value)
            *value = (short)(igl.current_window ? igl.current_window->wid : 0);
        return device;
    }

    for (;;) {
        if (igl_backend_poll_event(igl.backend, &backend_window,
                                   &device, &event_value, 1)) {
            if (value)
                *value = (short)(igl.current_window ? igl.current_window->wid : 0);
            return device;
        }
    }
}

void reshapeviewport(void)
{
    igl_init_once();
    if (!igl.current_window)
        return;

    igl.current_window->pane.clip.changed = 1;
    igl_refresh_clip(igl.current_window);
    igl_juniper_runtime_ensure_surface(&igl, igl.current_window);
}

long sginap(long ticks)
{
    struct timespec req;

    if (ticks <= 0)
        return 0;

    req.tv_sec = ticks / 100;
    req.tv_nsec = (ticks % 100) * 10000000L;
    nanosleep(&req, NULL);
    return 0;
}
