/*
 * SGI video control client (libXsgivc, "XSGIvc") on the host X connection.
 *
 * libXsgivc builds its requests directly in the Xlib output buffer of the
 * Display it is given.  The Display a recompiled program sees is a shadow
 * object without such a buffer, so the XSGIvc entry points are provided
 * natively: each one sends the same SGI-VIDEO-CONTROL request over the host
 * connection (libxcb, raw) and writes the answer into guest memory in the
 * layout of the SGI structures (big-endian, N32).
 *
 * Calling convention: N32, args in gpr[4..11], result in gpr[2].
 */
#include "IrixGuest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <string.h>
#include <sys/uio.h>

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#define ARG(n) ((uint32_t)cpu->gpr[4 + (n)])
#define RET(v) do { cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)(v); if (getenv("IRIX_VC_TRACE")) fprintf(stderr, "%s -> %d\n", __func__, (int)(int32_t)cpu->gpr[2]); } while (0)

extern IrixGuestAddr irix_libc_guest_malloc(uint32_t size);
Display *irix_x11_primary_display_from_guest(IrixGuestAddr guest);

#define X_ListVideoFormats 2
#define X_ListVideoFormatCombinations 3
#define X_ListFormatsInCombination 4
#define X_LoadVideoFormat 5
#define X_LoadVideoCombination 6
#define X_NVListVideoFormats 30
#define X_NVListVideoFormatCombinations 31
#define X_NVLoadVideoFormat 32
#define X_NVLoadVideoCombination 33
#define X_LoadFormatByName 34
#define X_QueryVersion 0
#define X_QueryVideoScreenInfo 1
#define X_QueryChannelInfo 8
#define X_SetControl 10
#define X_QueryControl 11
#define X_QueryMonitorName 9
#define X_QueryScreenGammaMaps 18
#define X_QueryGammaMap 19
#define X_StoreGammaColors8 20
#define X_StoreGammaColors16 21
#define X_QueryGammaColors 22

/* --- raw requests ------------------------------------------------------ */

static int vc_major(Display *d)
{
    static Display *cached_d;
    static int cached;
    int op, ev, er;

    if (d == cached_d && cached)
        return cached;
    if (!XQueryExtension(d, "SGI-VIDEO-CONTROL", &op, &ev, &er))
        return 0;
    cached_d = d;
    cached = op;
    return op;
}

/* Sends minor + words and returns the malloc'd reply (32-byte header plus data), or NULL. */
static uint8_t *vc_call(Display *d, int minor, const uint32_t *words, int nwords)
{
    xcb_connection_t *c;
    uint8_t req[64];
    struct iovec parts[3];
    xcb_protocol_request_t xr = { 1, 0, 0, 0 };
    xcb_generic_error_t *err = NULL;
    unsigned int seq;
    uint8_t *rep;
    int major, i;

    if (!d || nwords > 12 || !(major = vc_major(d)))
        return 0;
    c = XGetXCBConnection(d);
    if (!c)
        return 0;
    XFlush(d);
    req[0] = (uint8_t)major;
    req[1] = (uint8_t)minor;
    req[2] = (uint8_t)(1 + nwords);
    req[3] = 0;
    for (i = 0; i < nwords; i++)
        memcpy(req + 4 + 4 * i, &words[i], 4);
    parts[2].iov_base = req;
    parts[2].iov_len = 4 + 4 * (size_t)nwords;
    parts[0].iov_base = 0; parts[0].iov_len = 0;
    parts[1].iov_base = 0; parts[1].iov_len = 0;
    seq = xcb_send_request(c, XCB_REQUEST_RAW, parts + 2, &xr);
    xcb_flush(c);
    rep = xcb_wait_for_reply(c, seq, &err);
    if (err) {
        free(err);
        return 0;
    }
    return rep;
}

/* a request with a payload, sent complete; returns the malloc'd reply or NULL (errors count as failure) */
static uint8_t *vc_call_buf(Display *d, uint8_t *req, size_t len, int want_reply)
{
    xcb_connection_t *c;
    struct iovec parts[3];
    xcb_protocol_request_t xr = { 1, 0, 0, want_reply ? 0 : 1 };
    xcb_generic_error_t *err = NULL;
    unsigned int seq;
    uint8_t *rep;
    int major;

    if (!d || !(major = vc_major(d)) || !(c = XGetXCBConnection(d)))
        return 0;
    req[0] = (uint8_t)major;
    req[2] = (uint8_t)((len / 4) & 0xff);
    req[3] = (uint8_t)((len / 4) >> 8);
    XFlush(d);
    parts[0].iov_base = 0; parts[0].iov_len = 0;
    parts[1].iov_base = 0; parts[1].iov_len = 0;
    parts[2].iov_base = req; parts[2].iov_len = len;
    seq = xcb_send_request(c, XCB_REQUEST_RAW, parts + 2, &xr);
    xcb_flush(c);
    if (!want_reply)
        return 0;
    rep = xcb_wait_for_reply(c, seq, &err);
    if (err) {
        free(err);
        return 0;
    }
    return rep;
}

static uint32_t rd32(const uint8_t *p, size_t off) { uint32_t v; memcpy(&v, p + off, 4); return v; }
static uint16_t rd16(const uint8_t *p, size_t off) { uint16_t v; memcpy(&v, p + off, 2); return v; }

static void wrf(IrixGuestRuntime *rt, IrixGuestAddr a, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    irix_guest_write32be(rt, a, v);
}

static void wr_str(IrixGuestRuntime *rt, IrixGuestAddr a, const char *s, size_t max)
{
    size_t i;

    for (i = 0; i + 1 < max && s[i]; i++)
        irix_guest_write8(rt, a + (uint32_t)i, (uint8_t)s[i]);
    irix_guest_write8(rt, a + (uint32_t)i, 0);
}

/* --- entry points ------------------------------------------------------ */

static void b_XSGIvcQueryVersion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t w = 1 | (2u << 16);
    uint8_t *r = vc_call(d, X_QueryVersion, &w, 1);

    if (!r) { RET(0); return; }
    if (ARG(1)) irix_guest_write32be(rt, ARG(1), rd16(r, 8));
    if (ARG(2)) irix_guest_write32be(rt, ARG(2), rd16(r, 10));
    free(r);
    RET(1);
}

static void b_XSGIvcQueryVideoScreenInfo(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t w = ARG(1);
    uint8_t *r = vc_call(d, X_QueryVideoScreenInfo, &w, 1);
    IrixGuestAddr s = ARG(2);

    if (!r || !r[1]) { free(r); RET(0); return; }
    /* XSGIvcScreenInfo: numChannels, flags, lockOp, graphicsType[32], numExternalSync */
    irix_guest_write32be(rt, s + 0, rd32(r, 8));
    irix_guest_write32be(rt, s + 4, rd32(r, 12));
    irix_guest_write32be(rt, s + 8, r[16]);
    wr_str(rt, s + 12, "Xsgi", 32);
    irix_guest_write32be(rt, s + 44, rd32(r, 20));
    free(r);
    RET(1);
}

static void b_XSGIvcQueryScreenGammaMaps(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t w = ARG(1);
    uint8_t *r = vc_call(d, X_QueryScreenGammaMaps, &w, 1);

    if (!r || !r[1]) { free(r); RET(0); return; }
    if (ARG(2)) irix_guest_write32be(rt, ARG(2), rd32(r, 8));
    free(r);
    RET(1);
}

static void b_XSGIvcQueryMonitorName(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t w[2] = { ARG(1), ARG(2) };
    uint8_t *r = vc_call(d, X_QueryMonitorName, w, 2);
    uint32_t len;
    IrixGuestAddr g;

    if (!r || !r[1]) { free(r); RET(0); return; }
    len = rd32(r, 8);
    g = irix_libc_guest_malloc(len + 1);
    if (!g) { free(r); RET(0); return; }
    wr_str(rt, g, (const char *)r + 32, len + 1);
    if (ARG(3)) irix_guest_write32be(rt, ARG(3), g);
    free(r);
    RET(1);
}

/* XSGIvcChannelInfo (0x198 bytes) followed by its arrays, laid out like the SGI library does */
static void b_XSGIvcQueryChannelInfo(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t w[2] = { ARG(1), ARG(2) };
    uint8_t *r = vc_call(d, X_QueryChannelInfo, w, 2);
    uint32_t namelen, fields, gammas, ports, syncbytes, total, i;
    IrixGuestAddr g, fa, ga, sa, ta;
    const uint8_t *p;

    if (!r) { RET(0); return; }
    namelen = rd32(r, 116);
    fields = rd32(r, 120);
    gammas = rd32(r, 28);
    ports = rd32(r, 68);
    syncbytes = rd32(r, 72);
    total = 0x198 + fields * 36 + gammas * 4 + ports * 12 + syncbytes;
    g = irix_libc_guest_malloc(total);
    if (!g) { free(r); RET(0); return; }
    for (i = 0; i < 0x198; i += 4)
        irix_guest_write32be(rt, g + i, 0);
    fa = g + 0x198;
    ga = fa + fields * 36;
    sa = ga + gammas * 4;
    ta = sa + ports * 12;

    irix_guest_write32be(rt, g + 0x00, r[1]);                      /* active */
    wrf(rt, g + 0x04, (float)(int32_t)rd32(r, 8) / 4096.0f);        /* source.x */
    wrf(rt, g + 0x08, (float)(int32_t)rd32(r, 12) / 4096.0f);       /* y */
    wrf(rt, g + 0x0c, (float)(int32_t)rd32(r, 20) / 4096.0f);       /* height */
    wrf(rt, g + 0x10, (float)(int32_t)rd32(r, 16) / 4096.0f);       /* width */
    if (namelen)
        wr_str(rt, g + 0x14, (const char *)r + 124, 255);
    irix_guest_write32be(rt, g + 0x114, rd16(r, 78));              /* height */
    irix_guest_write32be(rt, g + 0x118, rd16(r, 76));              /* width */
    irix_guest_write32be(rt, g + 0x11c, rd16(r, 82));              /* totalHeight */
    irix_guest_write32be(rt, g + 0x120, rd16(r, 80));              /* totalWidth */
    wrf(rt, g + 0x124, (float)rd32(r, 84) / 1000.0f);
    wrf(rt, g + 0x128, (float)rd32(r, 88) / 1000.0f);
    irix_guest_write32be(rt, g + 0x12c, rd32(r, 92));              /* pixelClock */
    irix_guest_write32be(rt, g + 0x130, rd32(r, 96));              /* back porch */
    irix_guest_write32be(rt, g + 0x134, rd32(r, 100));             /* sync */
    irix_guest_write32be(rt, g + 0x138, rd32(r, 104));             /* front porch */
    irix_guest_write32be(rt, g + 0x13c, rd32(r, 108));             /* active */
    irix_guest_write32be(rt, g + 0x140, fields);
    irix_guest_write32be(rt, g + 0x144, fields ? fa : 0);
    irix_guest_write32be(rt, g + 0x148, rd32(r, 112));             /* formatFlags */
    irix_guest_write32be(rt, g + 0x14c, rd32(r, 24));              /* channelFlags */
    irix_guest_write32be(rt, g + 0x150, gammas ? ga : 0);
    irix_guest_write32be(rt, g + 0x154, gammas);
    irix_guest_write32be(rt, g + 0x158, r[60]);                    /* blankingOp */
    irix_guest_write32be(rt, g + 0x15c, r[61]);                    /* gainOp */
    irix_guest_write32be(rt, g + 0x160, r[62]);                    /* pedestalOp */
    irix_guest_write32be(rt, g + 0x164, r[63]);                    /* phaseHOp */
    irix_guest_write32be(rt, g + 0x168, rd32(r, 32));
    irix_guest_write32be(rt, g + 0x16c, rd32(r, 36));
    irix_guest_write32be(rt, g + 0x170, r[64]);                    /* phaseVOp */
    irix_guest_write32be(rt, g + 0x174, rd32(r, 40));
    irix_guest_write32be(rt, g + 0x178, rd32(r, 44));
    irix_guest_write32be(rt, g + 0x17c, r[65]);                    /* phaseSCHOp */
    irix_guest_write32be(rt, g + 0x180, rd32(r, 48));
    irix_guest_write32be(rt, g + 0x184, rd32(r, 52));
    irix_guest_write32be(rt, g + 0x188, r[66]);                    /* syncOp */
    irix_guest_write32be(rt, g + 0x18c, ports);
    irix_guest_write32be(rt, g + 0x190, ports ? sa : 0);
    irix_guest_write32be(rt, g + 0x194, rd32(r, 56));              /* physicalID */

    /* trailing data: name, fields, gamma maps, sync ports */
    p = r + 124 + ((namelen + 3) & ~3u);
    for (i = 0; i < fields; i++, p += 36) {
        uint32_t k;
        for (k = 0; k < 9; k++)
            irix_guest_write32be(rt, fa + i * 36 + k * 4, rd32(p, k * 4));
    }
    for (i = 0; i < gammas; i++, p += 4)
        irix_guest_write32be(rt, ga + i * 4, rd32(p, 0));
    for (i = 0; i < ports; i++) {
        uint32_t nt = rd32(p, 4), k;

        irix_guest_write32be(rt, sa + i * 12, rd32(p, 0));
        irix_guest_write32be(rt, sa + i * 12 + 4, nt ? ta : 0);
        irix_guest_write32be(rt, sa + i * 12 + 8, nt);
        for (k = 0; k < nt; k++, ta += 4)
            irix_guest_write32be(rt, ta, rd32(p, 8 + k * 4));
        p += 8 + nt * 4;
    }
    if (ARG(3)) irix_guest_write32be(rt, ARG(3), g);
    free(r);
    RET(1);
}

static void b_XSGIvcQueryGammaMap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t w[2] = { ARG(1), ARG(2) };
    uint8_t *r = vc_call(d, X_QueryGammaMap, w, 2);

    if (!r || !r[1]) { free(r); RET(0); return; }
    if (ARG(3)) irix_guest_write32be(rt, ARG(3), rd32(r, 8));      /* size */
    if (ARG(4)) irix_guest_write32be(rt, ARG(4), rd32(r, 12));     /* precision */
    if (ARG(5)) irix_guest_write32be(rt, ARG(5), rd32(r, 16));     /* attributes */
    free(r);
    RET(1);
}

/* the per-channel adjustments are SetControl/QueryControl requests with the SGI library's control ids */
enum { C_GAIN_ALL = 5, C_PEDESTAL = 6, C_PHASE_H = 7, C_PHASE_SCH = 8, C_PHASE_V = 9, C_GAMMA_MAP = 10, C_BLANKING = 11 };

static int vc_set_control(IrixGuestCpu *cpu, uint32_t control, uint32_t value)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t w[4] = { ARG(1), ARG(2), control, value };
    uint8_t *r;
    xcb_connection_t *c;

    if (!d || !vc_major(d))
        return 0;
    c = XGetXCBConnection(d);
    if (!c)
        return 0;
    /* a request without a reply; reuse vc_call's sender but expect none */
    {
        uint8_t req[24];
        struct iovec parts[3];
        xcb_protocol_request_t xr = { 1, 0, 0, 1 };
        int i;

        XFlush(d);
        req[0] = (uint8_t)vc_major(d); req[1] = X_SetControl; req[2] = 5; req[3] = 0;
        for (i = 0; i < 4; i++)
            memcpy(req + 4 + 4 * i, &w[i], 4);
        parts[0].iov_base = 0; parts[0].iov_len = 0;
        parts[1].iov_base = 0; parts[1].iov_len = 0;
        parts[2].iov_base = req; parts[2].iov_len = 20;
        xcb_send_request(c, XCB_REQUEST_RAW, parts + 2, &xr);
        xcb_flush(c);
    }
    (void)r;
    return 1;
}

static int vc_query_control(IrixGuestCpu *cpu, uint32_t control, uint32_t *value)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t w[3] = { ARG(1), ARG(2), control };
    uint8_t *r = vc_call(d, X_QueryControl, w, 3);

    if (!r || !r[1]) { free(r); return 0; }
    *value = rd32(r, 8);
    free(r);
    return 1;
}

static void b_vc_set_blanking(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; vc_set_control(cpu, C_BLANKING, ARG(3) ? 1 : 0); RET(0); }
static void b_vc_set_pedestal(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; vc_set_control(cpu, C_PEDESTAL, ARG(3) ? 1 : 0); RET(0); }
static void b_vc_set_phase_h(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; vc_set_control(cpu, C_PHASE_H, ARG(3)); RET(0); }
static void b_vc_set_phase_v(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; vc_set_control(cpu, C_PHASE_V, ARG(3)); RET(0); }
static void b_vc_set_phase_sch(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; vc_set_control(cpu, C_PHASE_SCH, ARG(3)); RET(0); }
static void b_vc_set_gamma_map(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; vc_set_control(cpu, C_GAMMA_MAP, ARG(3)); RET(0); }

static void vc_query_int(IrixGuestRuntime *rt, IrixGuestCpu *cpu, uint32_t control)
{
    uint32_t v;

    if (!vc_query_control(cpu, control, &v)) { RET(0); return; }
    if (ARG(3)) irix_guest_write32be(rt, ARG(3), v);
    RET(1);
}
static void b_vc_query_blanking(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_query_int(rt, cpu, C_BLANKING); }
static void b_vc_query_pedestal(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_query_int(rt, cpu, C_PEDESTAL); }
static void b_vc_query_phase_h(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_query_int(rt, cpu, C_PHASE_H); }
static void b_vc_query_phase_v(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_query_int(rt, cpu, C_PHASE_V); }
static void b_vc_query_phase_sch(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_query_int(rt, cpu, C_PHASE_SCH); }
static void b_vc_query_gamma_map(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_query_int(rt, cpu, C_GAMMA_MAP); }

/* gain: the component id picks the control (1..5); on the wire it is the gain in 1/512 units */
static void b_vc_set_gain(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    float g = irix_guest_fpr_get_s(cpu, 16);

    (void)rt;
    if (ARG(3) >= 1 && ARG(3) <= C_GAIN_ALL)
        vc_set_control(cpu, ARG(3), (uint32_t)(int32_t)(g * 512.0 + 0.5));
    RET(0);
}
static void b_vc_query_gain(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t v;
    float g;
    uint32_t bits;

    if (ARG(3) < 1 || ARG(3) > C_GAIN_ALL || !vc_query_control(cpu, ARG(3), &v)) { RET(0); return; }
    g = (float)(int32_t)v / 512.0f;
    memcpy(&bits, &g, 4);
    if (ARG(4)) irix_guest_write32be(rt, ARG(4), bits);
    RET(1);
}

/* gamma tables: values go over the wire little-endian as 16-bit (or bytes); the guest sees big-endian shorts */
static void b_XSGIvcQueryGammaColors(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t w[3] = { ARG(1), ARG(2), ARG(3) };
    uint8_t *r = vc_call(d, X_QueryGammaColors, w, 3);
    uint32_t n, i;
    IrixGuestAddr g;

    if (!r || !r[1]) { free(r); RET(0); return; }
    n = rd32(r, 8);
    g = irix_libc_guest_malloc(n * 2 + 4);
    if (!g) { free(r); RET(0); return; }
    for (i = 0; i < n; i++) {
        uint16_t v = rd16(r, 32 + 2 * i);

        irix_guest_write8(rt, g + 2 * i, (uint8_t)(v >> 8));
        irix_guest_write8(rt, g + 2 * i + 1, (uint8_t)v);
    }
    if (getenv("IRIX_VC_TRACE"))
        fprintf(stderr, "QueryGammaColors map=%u component=%u -> n=%u first=%u mid=%u last=%u\n", ARG(2), ARG(3), n, rd16(r, 32), rd16(r, 32 + n), rd16(r, 32 + 2 * (n - 1)));
    if (ARG(4)) irix_guest_write32be(rt, ARG(4), n);
    if (ARG(5)) irix_guest_write32be(rt, ARG(5), g);
    free(r);
    RET(1);
}

/* StoreGammaColors8/16(dpy, screen, map, itemCount, loadTables, values): a request with a payload */
static void vc_store_gamma(IrixGuestRuntime *rt, IrixGuestCpu *cpu, int minor, int sixteen)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t count = ARG(3), mask = ARG(4), values = ARG(5);
    int ncomp = ((mask & 1) != 0) + ((mask & 2) != 0) + ((mask & 4) != 0);   /* loadTables: 1 R, 2 G, 4 B */
    size_t bytes = (size_t)count * ncomp * (sixteen ? 2 : 1), padded = (bytes + 3) & ~3u, i;
    xcb_connection_t *c;
    uint8_t *req;
    struct iovec parts[3];
    xcb_protocol_request_t xr = { 1, 0, 0, 1 };
    int major;
    uint32_t hdr[4];

    if (!d || !count || count > 65536 || !ncomp || !values || !(major = vc_major(d))) { RET(0); return; }
    c = XGetXCBConnection(d);
    req = calloc(1, 20 + padded);
    if (!c || !req) { free(req); RET(0); return; }
    hdr[0] = ARG(1); hdr[1] = ARG(2); hdr[2] = count; hdr[3] = mask;
    if (getenv("IRIX_VC_TRACE")) {
        uint32_t k;
        fprintf(stderr, "StoreGammaColors%d map=%u component=%u count=%u values:", sixteen ? 16 : 8, ARG(2), mask, count);
        for (k = 0; k < 6 && k < count; k++)
            fprintf(stderr, " %u", sixteen ? (irix_guest_read8(rt, values + 2 * k) << 8 | irix_guest_read8(rt, values + 2 * k + 1)) : irix_guest_read8(rt, values + k));
        fprintf(stderr, " ... last=%u\n", sixteen ? (irix_guest_read8(rt, values + 2 * (count - 1)) << 8 | irix_guest_read8(rt, values + 2 * (count - 1) + 1)) : irix_guest_read8(rt, values + count - 1));
    }
    req[0] = (uint8_t)major; req[1] = (uint8_t)minor;
    req[2] = (uint8_t)((5 + padded / 4) & 0xff); req[3] = (uint8_t)((5 + padded / 4) >> 8);
    memcpy(req + 4, hdr, 16);
    for (i = 0; i < bytes; i += sixteen ? 2 : 1) {
        if (sixteen) {
            req[20 + i] = irix_guest_read8(rt, values + i + 1);   /* BE guest short -> LE wire */
            req[20 + i + 1] = irix_guest_read8(rt, values + i);
        } else {
            req[20 + i] = irix_guest_read8(rt, values + i);
        }
    }
    XFlush(d);
    parts[0].iov_base = 0; parts[0].iov_len = 0;
    parts[1].iov_base = 0; parts[1].iov_len = 0;
    parts[2].iov_base = req; parts[2].iov_len = 20 + padded;
    xcb_send_request(c, XCB_REQUEST_RAW, parts + 2, &xr);
    xcb_flush(c);
    free(req);
    RET(0);
}
static void b_XSGIvcStoreGammaColors8(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_store_gamma(rt, cpu, X_StoreGammaColors8, 0); }
static void b_XSGIvcStoreGammaColors16(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_store_gamma(rt, cpu, X_StoreGammaColors16, 1); }


/* --- video formats ----------------------------------------------------- */

static void put_le32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* the pattern ints of a request, in bit order, taken from a guest XSGIvcVideoFormatInfo */
static size_t pattern_words(IrixGuestRuntime *rt, IrixGuestAddr pat, uint32_t mask, uint32_t *out)
{
    size_t n = 0;

    if (!pat) return 0;
    if (mask & 0x2) out[n++] = irix_guest_read32be(rt, pat + 0x100);
    if (mask & 0x4) out[n++] = irix_guest_read32be(rt, pat + 0x104);
    if (mask & 0x8) out[n++] = irix_guest_read32be(rt, pat + 0x108);
    if (mask & 0x10) out[n++] = irix_guest_read32be(rt, pat + 0x10c);
    if (mask & 0x20) { uint32_t b = irix_guest_read32be(rt, pat + 0x110); float f; memcpy(&f, &b, 4); out[n++] = (uint32_t)(int32_t)(f * 1000.0f + 0.5f); }
    if (mask & 0x40) { uint32_t b = irix_guest_read32be(rt, pat + 0x114); float f; memcpy(&f, &b, 4); out[n++] = (uint32_t)(int32_t)(f * 1000.0f + 0.5f); }
    if (mask & 0x80) out[n++] = irix_guest_read32be(rt, pat + 0x118);
    if (mask & 0x100) out[n++] = irix_guest_read32be(rt, pat + 0x134);
    return n;
}

/* write one format (48-byte description, padded name, fields) starting at wire pointer p into a
 * guest XSGIvcVideoFormatInfo at g; field infos go to *fa. Returns the new wire pointer. */
static const uint8_t *parse_format(IrixGuestRuntime *rt, const uint8_t *p, IrixGuestAddr g, IrixGuestAddr *fa)
{
    uint32_t i, k, namelen = rd32(p, 40), fields = rd32(p, 44);
    float f;

    for (i = 0; i < 0x138; i += 4)
        irix_guest_write32be(rt, g + i, 0);
    irix_guest_write32be(rt, g + 0x100, rd16(p, 2));            /* height */
    irix_guest_write32be(rt, g + 0x104, rd16(p, 0));            /* width */
    irix_guest_write32be(rt, g + 0x108, rd16(p, 6));            /* totalHeight */
    irix_guest_write32be(rt, g + 0x10c, rd16(p, 4));            /* totalWidth */
    f = (float)rd32(p, 8) / 1000.0f; { uint32_t b; memcpy(&b, &f, 4); irix_guest_write32be(rt, g + 0x110, b); }
    f = (float)rd32(p, 12) / 1000.0f; { uint32_t b; memcpy(&b, &f, 4); irix_guest_write32be(rt, g + 0x114, b); }
    irix_guest_write32be(rt, g + 0x118, rd32(p, 16));           /* pixelClock */
    irix_guest_write32be(rt, g + 0x11c, rd32(p, 20));
    irix_guest_write32be(rt, g + 0x120, rd32(p, 24));
    irix_guest_write32be(rt, g + 0x124, rd32(p, 28));
    irix_guest_write32be(rt, g + 0x128, rd32(p, 32));
    irix_guest_write32be(rt, g + 0x12c, fields);
    irix_guest_write32be(rt, g + 0x130, fields ? *fa : 0);
    irix_guest_write32be(rt, g + 0x134, rd32(p, 36));
    if (namelen)
        wr_str(rt, g + 0, (const char *)p + 48, 255);
    p += 48 + ((namelen + 3) & ~3u);
    for (i = 0; i < fields; i++, p += 36, *fa += 36)
        for (k = 0; k < 9; k++)
            irix_guest_write32be(rt, *fa + k * 4, rd32(p, k * 4));
    return p;
}

/* turns a reply carrying a format list into the guest array the SGI library would return */
static IrixGuestAddr build_format_array(IrixGuestRuntime *rt, uint8_t *r, uint32_t *count_out)
{
    uint32_t count = rd32(r, 8), fields = rd32(r, 12), i;
    IrixGuestAddr arr, fa;
    const uint8_t *p = r + 32;

    *count_out = count;
    if (!count) return 0;
    arr = irix_libc_guest_malloc(count * 0x138 + fields * 36);
    if (!arr) return 0;
    fa = arr + count * 0x138;
    for (i = 0; i < count; i++)
        p = parse_format(rt, p, arr + i * 0x138, &fa);
    return arr;
}

/* ListVideoFormats / NVListVideoFormats(dpy, screen, channel, pattern, mask, matchMonitor, [matchOnlyNV,] max, count*) */
static void vc_list_formats(IrixGuestRuntime *rt, IrixGuestCpu *cpu, int nv)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t mask = ARG(4), max, words[8];
    uint8_t req[24 + 32];
    size_t n, len;
    uint8_t *r;
    IrixGuestAddr counts, arr;
    uint32_t count = 0;

    max = nv ? ARG(7) : ARG(6);
    counts = nv ? (uint32_t)irix_guest_read64be(rt, (uint32_t)cpu->gpr[29]) : ARG(7);
    memset(req, 0, sizeof req);
    req[1] = nv ? X_NVListVideoFormats : X_ListVideoFormats;
    put_le32(req + 4, ARG(1));
    put_le32(req + 8, ARG(2));
    req[12] = ARG(5) ? 1 : 0;
    if (nv) req[13] = ARG(6) ? 1 : 0;
    put_le32(req + 16, max);
    put_le32(req + 20, mask);
    n = pattern_words(rt, ARG(3), mask, words);
    memcpy(req + 24, words, n * 4);
    len = 24 + n * 4;
    r = vc_call_buf(d, req, len, 1);
    if (!r || !r[1]) { free(r); if (counts) irix_guest_write32be(rt, counts, 0); RET(0); return; }
    arr = build_format_array(rt, r, &count);
    if (counts) irix_guest_write32be(rt, counts, count);
    free(r);
    RET(arr);
}
static void b_XSGIvcListVideoFormats(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_list_formats(rt, cpu, 0); }
static void b_XSGIvcNVListVideoFormats(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_list_formats(rt, cpu, 1); }

/* a name request: header words, then nbytes of name */
static uint8_t *vc_name_request(IrixGuestRuntime *rt, Display *d, int minor, uint32_t screen, int has_channel,
                                uint32_t channel, uint32_t nonvolatile, IrixGuestAddr name, int want_reply)
{
    char buf[256];
    size_t nb, hdr = has_channel ? 20 : 12, total;
    uint8_t *req, *r;

    if (!name) return 0;
    {
        size_t i;
        for (i = 0; i + 1 < sizeof buf; i++) {
            buf[i] = (char)irix_guest_read8(rt, name + (uint32_t)i);
            if (!buf[i]) break;
        }
        buf[i < sizeof buf ? i : sizeof buf - 1] = 0;
    }
    {   /* a path is accepted: the server matches on the file name without directory and extension */
        char *slash = strrchr(buf, '/'), *dot;

        if (slash) memmove(buf, slash + 1, strlen(slash));
        dot = strrchr(buf, '.');
        if (dot && has_channel) *dot = 0;
    }
    nb = strlen(buf) + 1;
    total = hdr + ((nb + 3) & ~3u);
    req = calloc(1, total);
    if (!req) return 0;
    req[1] = (uint8_t)minor;
    put_le32(req + 4, screen);
    if (has_channel) {
        put_le32(req + 8, channel);
        put_le32(req + 12, nonvolatile);
        put_le32(req + 16, (uint32_t)nb);
    } else {
        put_le32(req + 8, (uint32_t)nb);
    }
    memcpy(req + hdr, buf, nb);
    r = vc_call_buf(d, req, total, want_reply);
    free(req);
    return r;
}

static void b_XSGIvcListVideoFormatsInCombination(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint8_t *r = vc_name_request(rt, d, X_ListFormatsInCombination, ARG(1), 0, 0, 0, ARG(2), 1);
    uint32_t count = 0;
    IrixGuestAddr arr = 0;

    if (r && r[1])
        arr = build_format_array(rt, r, &count);
    free(r);
    if (ARG(3)) irix_guest_write32be(rt, ARG(3), count);
    RET(arr);
}

/* the combination names come back as NUL-terminated strings; the library returns a char ** */
static void vc_list_combinations(IrixGuestRuntime *rt, IrixGuestCpu *cpu, int nv)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint8_t req[16];
    uint8_t *r;
    char pat[128];
    uint32_t i, count, kept = 0, maxn = nv ? ARG(4) : ARG(3);
    IrixGuestAddr counts = nv ? ARG(5) : ARG(4), arr, sp;
    const char *s;
    size_t total = 0;
    const char *names[256];

    memset(req, 0, sizeof req);
    req[1] = nv ? X_NVListVideoFormatCombinations : X_ListVideoFormatCombinations;
    put_le32(req + 4, ARG(1));
    put_le32(req + 8, maxn);
    if (nv) req[12] = ARG(3) ? 1 : 0;
    r = vc_call_buf(d, req, nv ? 16 : 12, 1);
    if (counts) irix_guest_write32be(rt, counts, 0);
    if (!r || !r[1]) { free(r); RET(0); return; }
    pat[0] = 0;
    if (ARG(2)) {
        uint32_t k;
        for (k = 0; k + 1 < sizeof pat; k++) {
            pat[k] = (char)irix_guest_read8(rt, ARG(2) + k);
            if (!pat[k]) break;
        }
        pat[k < sizeof pat ? k : sizeof pat - 1] = 0;
    }
    count = rd32(r, 8);
    s = (const char *)r + 32;
    for (i = 0; i < count && kept < 256; i++, s += strlen(s) + 1)
        if (!pat[0] || !fnmatch(pat, s, 0)) {
            names[kept++] = s;
            total += strlen(s) + 1;
        }
    if (!kept) { free(r); RET(0); return; }
    arr = irix_libc_guest_malloc(kept * 4 + (uint32_t)total + 4);
    if (!arr) { free(r); RET(0); return; }
    sp = arr + kept * 4;
    for (i = 0; i < kept; i++) {
        irix_guest_write32be(rt, arr + i * 4, sp);
        wr_str(rt, sp, names[i], strlen(names[i]) + 1);
        sp += (uint32_t)strlen(names[i]) + 1;
    }
    if (counts) irix_guest_write32be(rt, counts, kept);
    free(r);
    RET(arr);
}
static void b_XSGIvcListVideoFormatCombinations(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_list_combinations(rt, cpu, 0); }
static void b_XSGIvcNVListVideoFormatCombinations(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_list_combinations(rt, cpu, 1); }

/* Load*: the reply says whether it worked; errors count as failure */
static void vc_load_result(uint8_t *r, IrixGuestCpu *cpu)
{
    int ok = r && r[1];

    free(r);
    RET(ok ? 1 : 0);
}

static void vc_load_format(IrixGuestRuntime *rt, IrixGuestCpu *cpu, int nv)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));
    uint32_t mask = ARG(4), words[8];
    uint8_t req[28 + 32];
    size_t n, fixed = nv ? 28 : 20;
    uint8_t *r;

    memset(req, 0, sizeof req);
    req[1] = nv ? X_NVLoadVideoFormat : X_LoadVideoFormat;
    put_le32(req + 4, ARG(1));
    put_le32(req + 8, ARG(2));
    req[12] = ARG(5) ? 1 : 0;
    put_le32(req + 16, mask);
    if (nv) { put_le32(req + 20, ARG(6)); put_le32(req + 24, ARG(7)); }
    n = pattern_words(rt, ARG(3), mask, words);
    memcpy(req + fixed, words, n * 4);
    r = vc_call_buf(d, req, fixed + n * 4, 1);
    vc_load_result(r, cpu);
}
static void b_XSGIvcLoadVideoFormat(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_load_format(rt, cpu, 0); }
static void b_XSGIvcNVLoadVideoFormat(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { vc_load_format(rt, cpu, 1); }

static void b_XSGIvcLoadVideoFormatCombination(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));

    vc_load_result(vc_name_request(rt, d, X_LoadVideoCombination, ARG(1), 0, 0, 0, ARG(2), 1), cpu);
}
static void b_XSGIvcLoadVideoFormatByName(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));

    vc_load_result(vc_name_request(rt, d, X_LoadFormatByName, ARG(1), 1, ARG(2), 0, ARG(3), 1), cpu);
}
static void b_XSGIvcNVLoadVideoFormatByName(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = irix_x11_primary_display_from_guest(ARG(0));

    vc_load_result(vc_name_request(rt, d, X_LoadFormatByName, ARG(1), 1, ARG(2), 1, ARG(3), 1), cpu);
}

/* not implemented yet: queries fail, setters do nothing, lists are empty */
static void b_vc_fail(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; RET(0); }
static void b_vc_empty3(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    if (ARG(3)) irix_guest_write32be(rt, ARG(3), 0);
    RET(0);
}
static void b_vc_empty4(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    if (ARG(4)) irix_guest_write32be(rt, ARG(4), 0);
    RET(0);
}
static void b_vc_empty5(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    if (ARG(5)) irix_guest_write32be(rt, ARG(5), 0);
    RET(0);
}
/* the ninth argument (the count pointer of NVListVideoFormats) is on the stack */
static void b_vc_empty_stack8(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t p = (uint32_t)irix_guest_read64be(rt, (uint32_t)cpu->gpr[29]);

    if (p) irix_guest_write32be(rt, p, 0);
    RET(0);
}
static void b_vc_empty7(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    if (ARG(7)) irix_guest_write32be(rt, ARG(7), 0);
    RET(0);
}

typedef struct { const char *name; IrixGuestFunction fn; } VcEntry;

static const VcEntry g_entries[] = {
    { "XSGIvcQueryVersion", b_XSGIvcQueryVersion },
    { "XSGIvcQueryVideoScreenInfo", b_XSGIvcQueryVideoScreenInfo },
    { "XSGIvcQueryScreenGammaMaps", b_XSGIvcQueryScreenGammaMaps },
    { "XSGIvcQueryChannelInfo", b_XSGIvcQueryChannelInfo },
    { "XSGIvcQueryMonitorName", b_XSGIvcQueryMonitorName },
    { "XSGIvcQueryGammaMap", b_XSGIvcQueryGammaMap },
    { "XSGIvcListVideoFormats", b_XSGIvcListVideoFormats },
    { "XSGIvcNVListVideoFormats", b_XSGIvcNVListVideoFormats },
    { "XSGIvcListVideoFormatCombinations", b_XSGIvcListVideoFormatCombinations },
    { "XSGIvcNVListVideoFormatCombinations", b_XSGIvcNVListVideoFormatCombinations },
    { "XSGIvcListVideoFormatsInCombination", b_XSGIvcListVideoFormatsInCombination },
    { "XSGIvcLoadVideoFormat", b_XSGIvcLoadVideoFormat }, { "XSGIvcNVLoadVideoFormat", b_XSGIvcNVLoadVideoFormat },
    { "XSGIvcLoadVideoFormatCombination", b_XSGIvcLoadVideoFormatCombination }, { "XSGIvcNVLoadVideoFormatCombination", b_XSGIvcLoadVideoFormatCombination },
    { "XSGIvcLoadVideoFormatByName", b_XSGIvcLoadVideoFormatByName }, { "XSGIvcNVLoadVideoFormatByName", b_XSGIvcNVLoadVideoFormatByName },
    { "XSGIvcDisableChannel", b_vc_fail },
    { "XSGIvcSetOutputGain", b_vc_set_gain }, { "XSGIvcSetOutputPedestal", b_vc_set_pedestal },
    { "XSGIvcSetOutputPhaseH", b_vc_set_phase_h }, { "XSGIvcSetOutputPhaseSCH", b_vc_set_phase_sch },
    { "XSGIvcSetOutputPhaseV", b_vc_set_phase_v }, { "XSGIvcSetChannelGammaMap", b_vc_set_gamma_map },
    { "XSGIvcSetOutputBlanking", b_vc_set_blanking },
    { "XSGIvcQueryOutputGain", b_vc_query_gain }, { "XSGIvcQueryOutputPedestal", b_vc_query_pedestal },
    { "XSGIvcQueryOutputPhaseH", b_vc_query_phase_h }, { "XSGIvcQueryOutputPhaseSCH", b_vc_query_phase_sch },
    { "XSGIvcQueryOutputPhaseV", b_vc_query_phase_v }, { "XSGIvcQueryChannelGammaMap", b_vc_query_gamma_map },
    { "XSGIvcQueryOutputBlanking", b_vc_query_blanking },
    { "XSGIvcSetOutputSync", b_vc_fail }, { "XSGIvcQueryOutputSync", b_vc_fail },
    { "XSGIvcSetScreenInputSyncSource", b_vc_fail }, { "XSGIvcQueryScreenInputSyncSource", b_vc_fail },
    { "XSGIvcSetPlatformParameter", b_vc_fail }, { "XSGIvcQueryPlatformParameter", b_vc_fail },
    { "XSGIvcStoreGammaColors8", b_XSGIvcStoreGammaColors8 }, { "XSGIvcStoreGammaColors16", b_XSGIvcStoreGammaColors16 },
    { "XSGIvcQueryGammaColors", b_XSGIvcQueryGammaColors }, { "XSGIvcSelectInput", b_vc_fail },
    { "XSGIvcFreeVideoFormatInfo", b_vc_fail },
    { "XSGIvcSetChannelInputRectangle", b_vc_fail }, { "XSGIvcQueryBestChannelRectangle", b_vc_fail },
    { "XSGIvcSendMonitorCommand", b_vc_fail }, { "XSGIvcSendMonitorQuery", b_vc_fail },
    { "XSGIvcQueryMonitorBaseProtocol", b_vc_fail }, { "XSGIvcInitMonitorBaseProtocol", b_vc_fail },
    { "XSGIvcSetScreenExternalSyncSource", b_vc_fail }, { "XSGIvcQueryScreenExternalSyncSource", b_vc_fail },
    { "XSGIvcQueryExternalSyncSourceName", b_vc_fail },
};

IrixGuestFunction irix_xsgivc_bridge_lookup(const char *name)
{
    size_t i;

    if (getenv("IRIX_VC_BRIDGE") && !strcmp(getenv("IRIX_VC_BRIDGE"), "0"))
        return 0;
    for (i = 0; i < sizeof g_entries / sizeof g_entries[0]; ++i)
        if (!strcmp(name, g_entries[i].name))
            return g_entries[i].fn;
    return 0;
}
