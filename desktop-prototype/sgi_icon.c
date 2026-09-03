#include "sgi_icon.h"

#include <X11/Xutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct SgiIconImage {
    unsigned int width;
    unsigned int height;
    unsigned char *rgb;
};

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int read_at(FILE *fp, long offset, void *buf, size_t size)
{
    if (fseek(fp, offset, SEEK_SET) != 0)
        return 0;
    return fread(buf, 1, size, fp) == size;
}

static int decode_rle_row(FILE *fp, uint32_t offset, uint32_t length,
                          unsigned char *dst, unsigned int width)
{
    unsigned char *src;
    unsigned int in = 0;
    unsigned int out = 0;

    src = (unsigned char *)malloc(length ? length : 1);
    if (!src)
        return 0;
    if (!read_at(fp, (long)offset, src, length)) {
        free(src);
        return 0;
    }

    while (in < length && out < width) {
        unsigned char control = src[in++];
        unsigned int count = control & 0x7f;

        if (!count)
            break;

        if (control & 0x80) {
            while (count-- && in < length && out < width)
                dst[out++] = src[in++];
        } else {
            unsigned char value;
            if (in >= length)
                break;
            value = src[in++];
            while (count-- && out < width)
                dst[out++] = value;
        }
    }

    free(src);
    return out == width;
}

SgiIconImage *sgi_icon_load(const char *path)
{
    FILE *fp;
    unsigned char header[512];
    unsigned int storage, bpc, dimension, width, height, channels;
    uint32_t *row_start = NULL;
    uint32_t *row_size = NULL;
    unsigned char *rows[4] = { NULL, NULL, NULL, NULL };
    SgiIconImage *image = NULL;
    unsigned int y, z;

    fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fread(header, 1, sizeof(header), fp) != sizeof(header))
        goto fail;
    if (be16(header) != 0x01da)
        goto fail;

    storage = header[2];
    bpc = header[3];
    dimension = be16(header + 4);
    width = be16(header + 6);
    height = be16(header + 8);
    channels = be16(header + 10);

    if (bpc != 1 || width == 0 || height == 0 || channels == 0)
        goto fail;
    if (dimension < 2 || dimension > 3 || channels > 4)
        goto fail;
    if (storage != 0 && storage != 1)
        goto fail;

    image = (SgiIconImage *)calloc(1, sizeof(*image));
    if (!image)
        goto fail;
    image->width = width;
    image->height = height;
    image->rgb = (unsigned char *)malloc((size_t)width * height * 3);
    if (!image->rgb)
        goto fail;

    for (z = 0; z < channels; ++z) {
        rows[z] = (unsigned char *)malloc(width);
        if (!rows[z])
            goto fail;
    }

    if (storage == 1) {
        size_t entries = (size_t)height * channels;
        unsigned char *table = (unsigned char *)malloc(entries * 8);
        size_t i;

        row_start = (uint32_t *)malloc(entries * sizeof(*row_start));
        row_size = (uint32_t *)malloc(entries * sizeof(*row_size));
        if (!table || !row_start || !row_size) {
            free(table);
            goto fail;
        }
        if (!read_at(fp, 512, table, entries * 8)) {
            free(table);
            goto fail;
        }

        for (i = 0; i < entries; ++i) {
            row_start[i] = be32(table + i * 4);
            row_size[i] = be32(table + entries * 4 + i * 4);
        }
        free(table);
    }

    for (y = 0; y < height; ++y) {
        unsigned int x;

        for (z = 0; z < channels; ++z) {
            size_t index = (size_t)z * height + y;

            if (storage == 1) {
                if (!decode_rle_row(fp, row_start[index], row_size[index],
                                    rows[z], width))
                    goto fail;
            } else {
                long offset = 512 + (long)index * (long)width;
                if (!read_at(fp, offset, rows[z], width))
                    goto fail;
            }
        }

        for (x = 0; x < width; ++x) {
            size_t p = ((size_t)y * width + x) * 3;
            unsigned char r, g, b;

            if (channels < 3) {
                r = g = b = rows[0][x];
            } else {
                r = rows[0][x];
                g = rows[1][x];
                b = rows[2][x];
            }

            image->rgb[p + 0] = r;
            image->rgb[p + 1] = g;
            image->rgb[p + 2] = b;
        }
    }

    for (z = 0; z < 4; ++z)
        free(rows[z]);
    free(row_start);
    free(row_size);
    fclose(fp);
    return image;

fail:
    for (z = 0; z < 4; ++z)
        free(rows[z]);
    free(row_start);
    free(row_size);
    sgi_icon_free(image);
    fclose(fp);
    return NULL;
}

void sgi_icon_free(SgiIconImage *image)
{
    if (!image)
        return;
    free(image->rgb);
    free(image);
}

static unsigned long component_to_mask(unsigned char c, unsigned long mask)
{
    unsigned int shift = 0;
    unsigned long max;

    if (!mask)
        return 0;
    while (((mask >> shift) & 1UL) == 0)
        ++shift;
    max = mask >> shift;
    return ((((unsigned long)c * max + 127UL) / 255UL) << shift) & mask;
}

void sgi_icon_draw(Display *dpy, Window win, GC gc,
                   const SgiIconImage *image,
                   int x, int y, unsigned int box_w, unsigned int box_h)
{
    int screen = DefaultScreen(dpy);
    Visual *visual = DefaultVisual(dpy, screen);
    unsigned int draw_w, draw_h;
    XImage *ximage;
    unsigned int dx, dy;

    if (!image || !image->rgb)
        return;
    if (visual->class != TrueColor && visual->class != DirectColor)
        return;

    draw_w = image->width;
    draw_h = image->height;
    if (draw_w > box_w || draw_h > box_h) {
        double sx = (double)box_w / (double)draw_w;
        double sy = (double)box_h / (double)draw_h;
        double scale = sx < sy ? sx : sy;

        draw_w = (unsigned int)(draw_w * scale);
        draw_h = (unsigned int)(draw_h * scale);
        if (!draw_w)
            draw_w = 1;
        if (!draw_h)
            draw_h = 1;
    }

    ximage = XCreateImage(dpy, visual, (unsigned int)DefaultDepth(dpy, screen),
                          ZPixmap, 0, NULL, draw_w, draw_h, 32, 0);
    if (!ximage)
        return;

    ximage->data = (char *)calloc(1, (size_t)ximage->bytes_per_line * draw_h);
    if (!ximage->data) {
        XDestroyImage(ximage);
        return;
    }

    for (dy = 0; dy < draw_h; ++dy) {
        unsigned int source_y = image->height - 1 -
            (unsigned int)(((uint64_t)dy * image->height) / draw_h);

        for (dx = 0; dx < draw_w; ++dx) {
            unsigned int source_x =
                (unsigned int)(((uint64_t)dx * image->width) / draw_w);
            size_t p = ((size_t)source_y * image->width + source_x) * 3;
            unsigned long pixel =
                component_to_mask(image->rgb[p + 0], visual->red_mask) |
                component_to_mask(image->rgb[p + 1], visual->green_mask) |
                component_to_mask(image->rgb[p + 2], visual->blue_mask);

            XPutPixel(ximage, (int)dx, (int)dy, pixel);
        }
    }

    XPutImage(dpy, win, gc, ximage, 0, 0,
              x + ((int)box_w - (int)draw_w) / 2,
              y + ((int)box_h - (int)draw_h) / 2,
              draw_w, draw_h);
    XDestroyImage(ximage);
}
