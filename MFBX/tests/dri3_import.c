/*
 * Splits the DMA-BUF import from Present for the Xmfb GPU path.
 * A linear GBM buffer is filled from the CPU with known colours, imported with
 * DRI3 PixmapFromBuffer, then (a) copied into a window with core CopyArea and
 * (b) shown with Present.  The window is read back with GetImage after each.
 *
 *   DISPLAY=:29 ./dri3_import [linear|default]
 * build: cc dri3_import.c -o dri3_import -lxcb -lxcb-dri3 -lxcb-present -lxcb-image -lgbm -lX11-xcb -lX11
 */
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define W 128
#define H 128

static uint32_t read_pixel(xcb_connection_t *c, xcb_drawable_t d, int x, int y)
{
    xcb_get_image_reply_t *r = xcb_get_image_reply(c, xcb_get_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, d, x, y, 1, 1, ~0u), NULL);
    uint32_t v = 0xdeadbeef;
    if (r) { memcpy(&v, xcb_get_image_data(r), 4); free(r); }
    return v & 0xffffff;
}

int main(int argc, char **argv)
{
    int linear = !(argc > 1 && !strcmp(argv[1], "default"));
    int scr;
    xcb_connection_t *c = xcb_connect(NULL, &scr);
    xcb_screen_t *s;
    xcb_window_t win, root;
    xcb_pixmap_t pix;
    xcb_dri3_open_reply_t *open_r;
    int fd, drm, bad = 0, y, x;
    struct gbm_device *gbm;
    struct gbm_bo *bo;
    uint32_t stride, *map, *ptr;
    void *map_data = NULL;
    uint32_t colors[4] = { 0xff0000, 0x00ff00, 0x0000ff, 0xffff00 };

    if (xcb_connection_has_error(c)) { fprintf(stderr, "no display\n"); return 2; }
    s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    root = s->root;
    open_r = xcb_dri3_open_reply(c, xcb_dri3_open(c, root, 0), NULL);
    if (!open_r) { fprintf(stderr, "DRI3 not available\n"); return 2; }
    drm = xcb_dri3_open_reply_fds(c, open_r)[0];
    free(open_r);
    gbm = gbm_create_device(drm);
    bo = gbm_bo_create(gbm, W, H, GBM_FORMAT_XRGB8888,
                       GBM_BO_USE_RENDERING | (linear ? GBM_BO_USE_LINEAR : 0));
    if (!bo) { fprintf(stderr, "gbm_bo_create failed\n"); return 2; }
    stride = gbm_bo_get_stride(bo);
    map = gbm_bo_map(bo, 0, 0, W, H, GBM_BO_TRANSFER_WRITE, &stride, &map_data);
    if (!map) { fprintf(stderr, "gbm_bo_map failed (tiled buffers can't be CPU-mapped; use linear)\n"); return 2; }
    for (y = 0; y < H; y++) {
        ptr = (uint32_t *) ((char *) map + (size_t) y * stride);
        for (x = 0; x < W; x++) ptr[x] = colors[(y >= H / 2) * 2 + (x >= W / 2)];
    }
    gbm_bo_unmap(bo, map_data);
    printf("buffer %dx%d stride %u modifier %s\n", W, H, stride, linear ? "linear" : "implicit");
    fd = gbm_bo_get_fd(bo);

    win = xcb_generate_id(c);
    {
        uint32_t ov = 1;
        xcb_create_window(c, XCB_COPY_FROM_PARENT, win, root, 40, 40, W, H, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          s->root_visual, XCB_CW_OVERRIDE_REDIRECT, &ov);
    }
    xcb_map_window(c, win);
    xcb_flush(c);
    usleep(400000);     /* viewable before anything is drawn into it */
    pix = xcb_generate_id(c);
    xcb_dri3_pixmap_from_buffer(c, pix, root, H * stride, W, H, stride, 24, 32, fd);
    xcb_flush(c);
    {
        xcb_get_geometry_reply_t *g = xcb_get_geometry_reply(c, xcb_get_geometry(c, pix), NULL);
        printf("imported pixmap: %s\n", g ? "valid" : "ERROR (import failed)");
        free(g);
    }
    /* (a) core CopyArea pixmap -> window, no Present */
    {
        xcb_gcontext_t gc = xcb_generate_id(c);
        xcb_create_gc(c, gc, win, 0, NULL);
        xcb_copy_area(c, pix, win, gc, 0, 0, 0, 0, W, H);
        xcb_flush(c);
        usleep(200000);
        printf("pixmap itself : TL %06x TR %06x BL %06x BR %06x\n", read_pixel(c, pix, 10, 10), read_pixel(c, pix, 100, 10),
               read_pixel(c, pix, 10, 100), read_pixel(c, pix, 100, 100));
        printf("after CopyArea: TL %06x TR %06x BL %06x BR %06x\n", read_pixel(c, win, 10, 10), read_pixel(c, win, 100, 10),
               read_pixel(c, win, 10, 100), read_pixel(c, win, 100, 100));
        if (read_pixel(c, win, 10, 10) != colors[0] || read_pixel(c, win, 100, 100) != colors[3]) bad++;
    }
    /* (b) Present */
    {
        xcb_clear_area(c, 0, win, 0, 0, 0, 0);
        xcb_present_pixmap(c, win, pix, 1, 0, 0, 0, 0, 0, 0, 0, XCB_PRESENT_OPTION_NONE, 0, 0, 0, 0, NULL);
        xcb_flush(c);
        usleep(300000);
        printf("after Present : TL %06x TR %06x BL %06x BR %06x\n", read_pixel(c, win, 10, 10), read_pixel(c, win, 100, 10),
               read_pixel(c, win, 10, 100), read_pixel(c, win, 100, 100));
        if (read_pixel(c, win, 10, 10) != colors[0] || read_pixel(c, win, 100, 100) != colors[3]) bad++;
    }
    printf(bad ? "FAIL (%d of 2 paths wrong)\n" : "PASS\n", bad);
    return bad ? 1 : 0;
}
