/*
 * OpenGL and GLX for translated IRIX programs.
 *
 * SGI's IRIS GL library (IGLOO32/libgl.so) is implemented on top of OpenGL.
 * It imports its OpenGL and GLX entry points from libGL, which on IRIX is a
 * GLX client of SGI's software GLcore.  Here those imports are resolved to
 * host Mesa instead: GLX goes to the host libGL on the program's host X
 * connection, and every OpenGL entry point converts its big-endian guest
 * arguments and arrays and calls the host function.  The IRIS GL layer
 * itself keeps running as translated code.
 *
 * Calling convention (N32): integer and pointer arguments in a0..a7, floats
 * and doubles in $f12..$f19 by argument slot, further arguments on the stack
 * in 8-byte slots.  Guest memory is big-endian.
 */
#include "IrixGuest.h"
#include "IrixX11BridgeExtraAccess.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_TRACE_ENV "IRIX_TRACE_GL"

static int gl_trace_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv(GL_TRACE_ENV) != NULL;
    return on;
}

static uint32_t arg_n(IrixGuestRuntime *rt, IrixGuestCpu *cpu, unsigned n)
{
    if (n < 8u) return (uint32_t)cpu->gpr[4u + n];
    return (uint32_t)irix_guest_read64be(
        rt, (IrixGuestAddr)(uint32_t)cpu->gpr[29] + (IrixGuestAddr)((n - 8u) * 8u));
}

#define ARG(n) arg_n(rt, cpu, (n))
#define I(n) ((n) < 8 ? (uint32_t)cpu->gpr[4 + (n)] : ARG(n))
#define F(n) irix_guest_fpr_get_s(cpu, 12 + (n))
#define D(n) irix_guest_fpr_get_d(cpu, 12 + (n))

static uint8_t gl_rd8(IrixGuestRuntime *rt, IrixGuestAddr a) { return irix_guest_read8(rt, a); }
static uint16_t gl_rd16(IrixGuestRuntime *rt, IrixGuestAddr a) { return irix_guest_read16be(rt, a); }
static uint32_t gl_rd32(IrixGuestRuntime *rt, IrixGuestAddr a) { return irix_guest_read32be(rt, a); }
static float gl_rdf(IrixGuestRuntime *rt, IrixGuestAddr a)
{
    uint32_t v = irix_guest_read32be(rt, a);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static double gl_rdd(IrixGuestRuntime *rt, IrixGuestAddr a)
{
    uint64_t v = irix_guest_read64be(rt, a);
    double d;
    memcpy(&d, &v, 8);
    return d;
}
static void gl_wrf(IrixGuestRuntime *rt, IrixGuestAddr a, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    irix_guest_write32be(rt, a, v);
}

typedef struct { const char *name; void *fn; } GlProc;
static GlProc gl_procs[512];
static size_t gl_proc_count;

static void *gl_proc(const char *name)
{
    size_t i;
    void *fn;
    for (i = 0; i < gl_proc_count; ++i)
        if (gl_procs[i].name == name) return gl_procs[i].fn;
    fn = (void *)glXGetProcAddressARB((const GLubyte *)name);
    if (gl_proc_count < sizeof(gl_procs) / sizeof(gl_procs[0])) {
        gl_procs[gl_proc_count].name = name;
        gl_procs[gl_proc_count++].fn = fn;
    }
    if (!fn && gl_trace_on())
        fprintf(stderr, "IRIX GL: host has no %s\n", name);
    return fn;
}

#include "IrixGlBridgeScalar.inc"

/* ---- arrays ---- */

static int gl_read_floats(IrixGuestRuntime *rt, IrixGuestAddr p, unsigned n, float *out)
{
    unsigned i;
    if (!p) return 0;
    for (i = 0; i < n; ++i) out[i] = gl_rdf(rt, p + i * 4u);
    return !rt->trapped;
}

static unsigned light_count(uint32_t pname)
{
    switch (pname) {
    case 0x1200: case 0x1201: case 0x1202: case 0x1203: return 4;
    case 0x1204: return 3;
    default: return 1;
    }
}

static unsigned material_count(uint32_t pname)
{
    switch (pname) {
    case 0x1200: case 0x1201: case 0x1202: case 0x1600: case 0x1602: return 4;
    case 0x1603: return 3;
    default: return 1;
    }
}

/* glLightfv(light, pname, params) and friends sharing the (a, pname, ptr) shape. */
#define VEC_FV(NAME, COUNT)                                                   \
static void gl_b_##NAME(IrixGuestRuntime *rt, IrixGuestCpu *cpu)             \
{                                                                             \
    typedef void (*fn_t)(GLenum, GLenum, const GLfloat *);                    \
    fn_t f = (fn_t)gl_proc(#NAME);                                            \
    float v[16];                                                              \
    unsigned n = COUNT(I(1));                                                 \
    if (f && gl_read_floats(rt, I(2), n, v)) f(I(0), I(1), v);                \
    cpu->gpr[2] = 0;                                                          \
}
static unsigned count_one_or_four_color(uint32_t pname) { return pname == 0x2201 ? 4 : 1; }
static unsigned count_texgen(uint32_t pname) { return (pname == 0x2501 || pname == 0x2502) ? 4 : 1; }
VEC_FV(glLightfv, light_count)
VEC_FV(glMaterialfv, material_count)
VEC_FV(glTexEnvfv, count_one_or_four_color)
VEC_FV(glTexGenfv, count_texgen)
#undef VEC_FV

static void gl_b_glFogfv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    typedef void (*fn_t)(GLenum, const GLfloat *);
    fn_t f = (fn_t)gl_proc("glFogfv");
    float v[4];
    if (f && gl_read_floats(rt, I(1), I(0) == 0x0B66 ? 4u : 1u, v)) f(I(0), v);
    cpu->gpr[2] = 0;
}

static void gl_b_glLightModelfv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    typedef void (*fn_t)(GLenum, const GLfloat *);
    fn_t f = (fn_t)gl_proc("glLightModelfv");
    float v[4];
    if (f && gl_read_floats(rt, I(1), I(0) == 0x0B53 ? 4u : 1u, v)) f(I(0), v);
    cpu->gpr[2] = 0;
}

static void gl_b_glLoadMatrixf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    float m[16];
    if (gl_read_floats(rt, I(0), 16, m)) glLoadMatrixf(m);
    cpu->gpr[2] = 0;
}

static void gl_b_glMultMatrixf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    float m[16];
    if (gl_read_floats(rt, I(0), 16, m)) glMultMatrixf(m);
    cpu->gpr[2] = 0;
}

static void gl_b_glMultMatrixd(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    double m[16];
    unsigned i;
    for (i = 0; i < 16; ++i) m[i] = gl_rdd(rt, I(0) + i * 8u);
    if (!rt->trapped) glMultMatrixd(m);
    cpu->gpr[2] = 0;
}

static void gl_b_glClipPlane(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    double e[4];
    unsigned i;
    for (i = 0; i < 4; ++i) e[i] = gl_rdd(rt, I(1) + i * 8u);
    if (!rt->trapped) glClipPlane(I(0), e);
    cpu->gpr[2] = 0;
}

static void gl_b_glTexFilterFuncSGIS(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    typedef void (*fn_t)(GLenum, GLenum, GLsizei, const GLfloat *);
    fn_t f = (fn_t)gl_proc("glTexFilterFuncSGIS");
    uint32_t n = I(2);
    float *w;
    if (f && n <= 4096 && (w = (float *)malloc((n ? n : 1) * 4u))) {
        if (gl_read_floats(rt, I(3), n, w)) f(I(0), I(1), (GLsizei)n, w);
        free(w);
    }
    cpu->gpr[2] = 0;
}

/* ---- queries returning arrays ---- */

static unsigned get_count(uint32_t pname)
{
    switch (pname) {
    case 0x0BA6: case 0x0BA7: case 0x0BA8: return 16;
    case 0x0BA2: case 0x0C10: case 0x0C22: case 0x0C23: case 0x0B00:
    case 0x0B07: case 0x0B04: case 0x0B03: case 0x0B53: case 0x0B66:
    case 0x0B80: case 0x0DD2: return 4;
    case 0x0B02: return 3;
    case 0x0B70: case 0x0B22: case 0x0B12: case 0x0B40: case 0x0D3A:
    case 0x0DD0: case 0x0DD3: case 0x0DD1: return 2;
    default: return 1;
    }
}

static void gl_b_glGetFloatv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    float v[16] = {0};
    unsigned i, n = get_count(I(0));
    glGetFloatv(I(0), v);
    for (i = 0; i < n; ++i) gl_wrf(rt, I(1) + i * 4u, v[i]);
    cpu->gpr[2] = 0;
}

static void gl_b_glGetIntegerv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    GLint v[16] = {0};
    unsigned i, n = get_count(I(0));
    glGetIntegerv(I(0), v);
    for (i = 0; i < n; ++i) irix_guest_write32be(rt, I(1) + i * 4u, (uint32_t)v[i]);
    cpu->gpr[2] = 0;
}

static void gl_b_glGetBooleanv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    GLboolean v[16] = {0};
    unsigned i, n = get_count(I(0));
    glGetBooleanv(I(0), v);
    for (i = 0; i < n; ++i) irix_guest_write8(rt, I(1) + i, v[i]);
    cpu->gpr[2] = 0;
}

static void gl_b_glGetMaterialfv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    float v[4] = {0};
    unsigned i, n = material_count(I(1));
    glGetMaterialfv(I(0), I(1), v);
    for (i = 0; i < n; ++i) gl_wrf(rt, I(2) + i * 4u, v[i]);
    cpu->gpr[2] = 0;
}

static void gl_b_glGetTexLevelParameteriv(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    GLint v = 0;
    glGetTexLevelParameteriv(I(0), (GLint)I(1), I(2), &v);
    irix_guest_write32be(rt, I(3), (uint32_t)v);
    cpu->gpr[2] = 0;
}

static IrixGuestAddr gl_guest_string(IrixGuestRuntime *rt, const char *s)
{
    static struct { const char *host; IrixGuestAddr guest; } cache[16];
    size_t i, n;
    IrixGuestAddr a;
    void *p;
    if (!s) return 0;
    for (i = 0; i < 16 && cache[i].host; ++i)
        if (cache[i].host == s) return cache[i].guest;
    n = strlen(s) + 1u;
    a = irix_guest_xt_compat_alloc(rt, (uint32_t)n);
    p = a ? irix_guest_ptr(rt, a, n) : 0;
    if (!p) return 0;
    memcpy(p, s, n);
    if (i < 16) {
        cache[i].host = s;
        cache[i].guest = a;
    }
    return a;
}

static void gl_b_glGetString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *over = 0;
    switch (I(0)) {
    case GL_VENDOR: over = getenv("IRIX_GL_VENDOR"); break;
    case GL_RENDERER: over = getenv("IRIX_GL_RENDERER"); break;
    case GL_VERSION: over = getenv("IRIX_GL_VERSION"); break;
    case GL_EXTENSIONS: over = getenv("IRIX_GL_EXTENSIONS"); break;
    default: break;
    }
    cpu->gpr[2] = gl_guest_string(rt, over ? over : (const char *)glGetString(I(0)));
}

/* ---- display lists, textures, selection ---- */

static void gl_b_glCallLists(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t n = I(0), type = I(1);
    unsigned size;
    unsigned char *buf;
    uint32_t i;
    switch (type) {
    case 0x1400: case 0x1401: case 0x1407: size = 1; break;
    case 0x1402: case 0x1403: size = 2; break;
    case 0x1408: size = 3; break;
    default: size = 4; break;
    }
    if (n > (1u << 20) || !(buf = (unsigned char *)malloc((size_t)n * size + 4u))) {
        cpu->gpr[2] = 0;
        return;
    }
    for (i = 0; i < n; ++i) {
        IrixGuestAddr a = I(2) + i * size;
        if (size == 1 || size == 3) {
            unsigned k;
            for (k = 0; k < size; ++k) buf[i * size + k] = irix_guest_read8(rt, a + k);
        } else if (size == 2) {
            uint16_t v = irix_guest_read16be(rt, a);
            memcpy(buf + i * 2u, &v, 2);
        } else {
            uint32_t v = irix_guest_read32be(rt, a);
            memcpy(buf + i * 4u, &v, 4);
        }
    }
    if (!rt->trapped) glCallLists((GLsizei)n, type, buf);
    free(buf);
    cpu->gpr[2] = 0;
}

static void gl_b_glGenTexturesEXT(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    typedef void (*fn_t)(GLsizei, GLuint *);
    fn_t f = (fn_t)gl_proc("glGenTexturesEXT");
    uint32_t n = I(0), i;
    GLuint *ids;
    if (!f) f = (fn_t)gl_proc("glGenTextures");
    if (f && n <= 65536 && (ids = (GLuint *)calloc(n ? n : 1, 4))) {
        f((GLsizei)n, ids);
        for (i = 0; i < n; ++i) irix_guest_write32be(rt, I(1) + i * 4u, ids[i]);
        free(ids);
    }
    cpu->gpr[2] = 0;
}

static void gl_b_glDeleteTexturesEXT(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    typedef void (*fn_t)(GLsizei, const GLuint *);
    fn_t f = (fn_t)gl_proc("glDeleteTexturesEXT");
    uint32_t n = I(0), i;
    GLuint *ids;
    if (!f) f = (fn_t)gl_proc("glDeleteTextures");
    if (f && n <= 65536 && (ids = (GLuint *)calloc(n ? n : 1, 4))) {
        for (i = 0; i < n; ++i) ids[i] = irix_guest_read32be(rt, I(1) + i * 4u);
        f((GLsizei)n, ids);
        free(ids);
    }
    cpu->gpr[2] = 0;
}

static void gl_b_glAreTexturesResidentEXT(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    typedef GLboolean (*fn_t)(GLsizei, const GLuint *, GLboolean *);
    fn_t f = (fn_t)gl_proc("glAreTexturesResidentEXT");
    uint32_t n = I(0), i;
    GLuint *ids;
    GLboolean *res;
    GLboolean r = GL_TRUE;
    if (!f) f = (fn_t)gl_proc("glAreTexturesResident");
    if (f && n <= 65536 && (ids = (GLuint *)calloc(n ? n : 1, 4)) &&
        (res = (GLboolean *)calloc(n ? n : 1, 1))) {
        for (i = 0; i < n; ++i) ids[i] = irix_guest_read32be(rt, I(1) + i * 4u);
        r = f((GLsizei)n, ids, res);
        for (i = 0; i < n; ++i) irix_guest_write8(rt, I(2) + i, res[i]);
        free(ids);
        free(res);
    }
    cpu->gpr[2] = r;
}

static struct {
    IrixGuestAddr guest;
    uint32_t size, type;
    GLuint *host;
    int active;
} gl_buf[2]; /* 0 = feedback, 1 = selection */

static void gl_b_glFeedbackBuffer(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t size = I(0);
    (void)rt;
    free(gl_buf[0].host);
    gl_buf[0].host = size <= (1u << 20) ? (GLuint *)calloc(size ? size : 1, 4) : 0;
    gl_buf[0].guest = I(2);
    gl_buf[0].size = gl_buf[0].host ? size : 0;
    gl_buf[0].type = I(1);
    if (gl_buf[0].host) glFeedbackBuffer((GLsizei)size, I(1), (GLfloat *)gl_buf[0].host);
    cpu->gpr[2] = 0;
}

static void gl_b_glSelectBuffer(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t size = I(0);
    (void)rt;
    free(gl_buf[1].host);
    gl_buf[1].host = size <= (1u << 20) ? (GLuint *)calloc(size ? size : 1, 4) : 0;
    gl_buf[1].guest = I(1);
    gl_buf[1].size = gl_buf[1].host ? size : 0;
    if (gl_buf[1].host) glSelectBuffer((GLsizei)size, gl_buf[1].host);
    cpu->gpr[2] = 0;
}

static void gl_b_glRenderMode(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    GLint prev = 0, r;
    uint32_t i;
    glGetIntegerv(GL_RENDER_MODE, &prev);
    r = glRenderMode(I(0));
    if (prev == GL_FEEDBACK && gl_buf[0].host)
        for (i = 0; i < gl_buf[0].size; ++i)
            irix_guest_write32be(rt, gl_buf[0].guest + i * 4u, gl_buf[0].host[i]);
    else if (prev == GL_SELECT && gl_buf[1].host)
        for (i = 0; i < gl_buf[1].size; ++i)
            irix_guest_write32be(rt, gl_buf[1].guest + i * 4u, gl_buf[1].host[i]);
    cpu->gpr[2] = (uint32_t)r;
}

/* ---- pixel data ---- */

static unsigned pixel_components(uint32_t format)
{
    switch (format) {
    case 0x1904: case 0x1905: case 0x1906: case 0x1907: case 0x1900:
    case 0x1901: case 0x1902: case 0x1903: case 0x8049: return 1;
    case 0x190A: return 2;
    case 0x1908: case 0x80E0: return 3;
    case 0x1909: case 0x80E1: case 0x8000: return 4;
    default: return 4;
    }
}

/* Bytes per pixel and element size for a pixel type; bitmaps are bytes. */
static void pixel_type(uint32_t type, unsigned comps, unsigned *pixbytes, unsigned *elem)
{
    switch (type) {
    case 0x1400: case 0x1401: *elem = 1; *pixbytes = comps; break;
    case 0x1402: case 0x1403: *elem = 2; *pixbytes = 2u * comps; break;
    case 0x1404: case 0x1405: case 0x1406: *elem = 4; *pixbytes = 4u * comps; break;
    case 0x8032: case 0x8362: *elem = 1; *pixbytes = 1; break;
    case 0x8033: case 0x8034: case 0x8363: case 0x8364: case 0x8365:
    case 0x8366: *elem = 2; *pixbytes = 2; break;
    case 0x8035: case 0x8036: case 0x8367: case 0x8368: *elem = 4; *pixbytes = 4; break;
    default: *elem = 1; *pixbytes = comps; break;
    }
}

static size_t pixel_buffer_size(uint32_t w, uint32_t h, uint32_t d, uint32_t format,
                                uint32_t type, int pack, unsigned *elem_out)
{
    GLint align = 4, rowlen = 0, skipp = 0, skipr = 0;
    unsigned comps = pixel_components(format), pixbytes, elem;
    size_t rowbytes, stride;
    if (pack) {
        glGetIntegerv(GL_PACK_ALIGNMENT, &align);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &rowlen);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipp);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &skipr);
    } else {
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowlen);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipp);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipr);
    }
    if (type == GL_BITMAP) {
        elem = 1;
        rowbytes = ((rowlen > 0 ? (uint32_t)rowlen : w) + 7u) / 8u;
        stride = (rowbytes + align - 1u) / align * align;
        *elem_out = 1;
        return (size_t)(skipr + (long)h * d) * stride + (size_t)skipp / 8u + (w + 7u) / 8u;
    }
    pixel_type(type, comps, &pixbytes, &elem);
    rowbytes = (size_t)(rowlen > 0 ? (uint32_t)rowlen : w) * pixbytes;
    stride = (rowbytes + align - 1u) / align * align;
    *elem_out = elem;
    return (size_t)(skipr + (long)h * d) * stride + (size_t)skipp * pixbytes + (size_t)w * pixbytes;
}

static void swap_elems(unsigned char *p, size_t bytes, unsigned elem)
{
    size_t i;
    if (elem == 2) {
        for (i = 0; i + 1 < bytes; i += 2) { unsigned char t = p[i]; p[i] = p[i + 1]; p[i + 1] = t; }
    } else if (elem == 4) {
        for (i = 0; i + 3 < bytes; i += 4) {
            unsigned char t0 = p[i], t1 = p[i + 1];
            p[i] = p[i + 3]; p[i + 1] = p[i + 2]; p[i + 2] = t1; p[i + 3] = t0;
        }
    }
}

/* Copy guest pixels (host-native element order) into a malloc'd buffer. */
static unsigned char *pixels_in(IrixGuestRuntime *rt, IrixGuestAddr src, size_t bytes, unsigned elem)
{
    unsigned char *buf;
    void *p;
    if (!src) return 0;
    if (bytes > (256u << 20)) return 0;
    p = irix_guest_ptr(rt, src, bytes ? bytes : 1);
    buf = (unsigned char *)malloc(bytes ? bytes : 1);
    if (!p || !buf) { free(buf); return 0; }
    memcpy(buf, p, bytes);
    swap_elems(buf, bytes, elem);
    return buf;
}

static void gl_b_glDrawPixels(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    unsigned elem;
    size_t bytes = pixel_buffer_size(I(0), I(1), 1, I(2), I(3), 0, &elem);
    unsigned char *buf = pixels_in(rt, I(4), bytes, elem);
    if (buf) glDrawPixels((GLsizei)I(0), (GLsizei)I(1), I(2), I(3), buf);
    free(buf);
    cpu->gpr[2] = 0;
}

static void gl_b_glReadPixels(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t w = I(2), h = I(3), format = I(4), type = I(5);
    unsigned elem;
    size_t bytes = pixel_buffer_size(w, h, 1, format, type, 1, &elem);
    unsigned char *buf = bytes <= (256u << 20) ? (unsigned char *)calloc(bytes ? bytes : 1, 1) : 0;
    void *dst = I(6) ? irix_guest_ptr(rt, I(6), bytes ? bytes : 1) : 0;
    if (buf && dst) {
        glReadPixels((GLint)I(0), (GLint)I(1), (GLsizei)w, (GLsizei)h, format, type, buf);
        swap_elems(buf, bytes, elem);
        memcpy(dst, buf, bytes);
    }
    free(buf);
    cpu->gpr[2] = 0;
}

static void gl_b_glBitmap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    unsigned elem;
    size_t bytes = pixel_buffer_size(I(0), I(1), 1, GL_COLOR_INDEX, GL_BITMAP, 0, &elem);
    unsigned char *buf = I(6) ? pixels_in(rt, I(6), bytes, 1) : 0;
    glBitmap((GLsizei)I(0), (GLsizei)I(1), F(2), F(3), F(4), F(5), buf);
    free(buf);
    cpu->gpr[2] = 0;
}

static void gl_b_glPolygonStipple(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    unsigned char m[128];
    unsigned i;
    for (i = 0; i < 128; ++i) m[i] = irix_guest_read8(rt, I(0) + i);
    if (!rt->trapped) glPolygonStipple(m);
    cpu->gpr[2] = 0;
}

static void gl_b_glTexImage1D(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    unsigned elem;
    size_t bytes = pixel_buffer_size(I(3), 1, 1, I(5), I(6), 0, &elem);
    unsigned char *buf = pixels_in(rt, I(7), bytes, elem);
    glTexImage1D(I(0), (GLint)I(1), (GLint)I(2), (GLsizei)I(3), (GLint)I(4), I(5), I(6), buf);
    free(buf);
    cpu->gpr[2] = 0;
}

static void gl_b_glTexImage2D(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    unsigned elem;
    size_t bytes = pixel_buffer_size(I(3), I(4), 1, I(6), I(7), 0, &elem);
    unsigned char *buf = pixels_in(rt, I(8), bytes, elem);
    glTexImage2D(I(0), (GLint)I(1), (GLint)I(2), (GLsizei)I(3), (GLsizei)I(4),
                 (GLint)I(5), I(6), I(7), buf);
    free(buf);
    cpu->gpr[2] = 0;
}

static void gl_b_glTexImage3DEXT(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    typedef void (*fn_t)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint,
                         GLenum, GLenum, const GLvoid *);
    fn_t f = (fn_t)gl_proc("glTexImage3DEXT");
    unsigned elem;
    size_t bytes = pixel_buffer_size(I(3), I(4), I(5), I(7), I(8), 0, &elem);
    unsigned char *buf = pixels_in(rt, I(9), bytes, elem);
    if (!f) f = (fn_t)gl_proc("glTexImage3D");
    if (f) f(I(0), (GLint)I(1), I(2), (GLsizei)I(3), (GLsizei)I(4), (GLsizei)I(5),
             (GLint)I(6), I(7), I(8), buf);
    free(buf);
    cpu->gpr[2] = 0;
}

static void gl_b_glTexSubImage2DEXT(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    unsigned elem;
    size_t bytes = pixel_buffer_size(I(4), I(5), 1, I(6), I(7), 0, &elem);
    unsigned char *buf = pixels_in(rt, I(8), bytes, elem);
    glTexSubImage2D(I(0), (GLint)I(1), (GLint)I(2), (GLint)I(3), (GLsizei)I(4),
                    (GLsizei)I(5), I(6), I(7), buf);
    free(buf);
    cpu->gpr[2] = 0;
}

static void gl_b_glColorTableSGI(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    typedef void (*fn_t)(GLenum, GLenum, GLsizei, GLenum, GLenum, const GLvoid *);
    fn_t f = (fn_t)gl_proc("glColorTableSGI");
    unsigned elem;
    size_t bytes = pixel_buffer_size(I(2), 1, 1, I(3), I(4), 0, &elem);
    unsigned char *buf = pixels_in(rt, I(5), bytes, elem);
    if (f) f(I(0), I(1), (GLsizei)I(2), I(3), I(4), buf);
    free(buf);
    cpu->gpr[2] = 0;
}

/* ---- evaluators ---- */

static unsigned map_dims(uint32_t target)
{
    switch (target) {
    case 0x0D90: case 0x0DB0: case 0x0D91: case 0x0DB1: return 3; /* vertex3 / normal */
    case 0x0D92: case 0x0DB2: case 0x0D93: case 0x0DB3: return 4; /* vertex4 / color4 */
    case 0x0D94: case 0x0DB4: return 1;                         /* index */
    case 0x0D95: case 0x0DB5: return 3;                         /* normal */
    case 0x0D96: case 0x0DB6: return 1;                         /* texcoord1 */
    case 0x0D97: case 0x0DB7: return 2;
    case 0x0D98: case 0x0DB8: return 3;
    case 0x0D99: case 0x0DB9: return 4;
    default: return 4;
    }
}

static void gl_b_glMap1f(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t stride = I(3), order = I(4);
    unsigned dims = map_dims(I(0));
    size_t n = (size_t)(order ? order - 1u : 0u) * stride + dims, i;
    float *pts;
    if (n > (1u << 20) || !(pts = (float *)malloc(n * 4u))) { cpu->gpr[2] = 0; return; }
    for (i = 0; i < n; ++i) pts[i] = gl_rdf(rt, I(5) + (IrixGuestAddr)i * 4u);
    if (!rt->trapped) glMap1f(I(0), F(1), F(2), (GLint)stride, (GLint)order, pts);
    free(pts);
    cpu->gpr[2] = 0;
}

static void gl_b_glMap2f(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t ustride = I(3), uorder = I(4), vstride = I(7), vorder = I(8);
    unsigned dims = map_dims(I(0));
    size_t n = (size_t)(uorder ? uorder - 1u : 0u) * ustride +
               (size_t)(vorder ? vorder - 1u : 0u) * vstride + dims, i;
    float *pts;
    if (n > (1u << 20) || !(pts = (float *)malloc(n * 4u))) { cpu->gpr[2] = 0; return; }
    for (i = 0; i < n; ++i) pts[i] = gl_rdf(rt, I(9) + (IrixGuestAddr)i * 4u);
    if (!rt->trapped)
        glMap2f(I(0), F(1), F(2), (GLint)ustride, (GLint)uorder, F(5), F(6),
                (GLint)vstride, (GLint)vorder, pts);
    free(pts);
    cpu->gpr[2] = 0;
}

/* SGI's IGLOO fast-path interface into GLcore.  Reporting it unavailable
 * leaves IRIS GL on the ordinary OpenGL entry points. */
static void gl_b_glIglooInterfaceSGIX(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    if (gl_trace_on())
        fprintf(stderr, "IRIX GL: glIglooInterfaceSGIX(%08x, %08x) -> unavailable\n",
                (unsigned)cpu->gpr[4], (unsigned)cpu->gpr[5]);
    cpu->gpr[2] = 0;
}

/* ---- GLX ---- */

#define GLX_MAX_CONTEXTS 64
static GLXContext glx_ctx[GLX_MAX_CONTEXTS];
static IrixGuestAddr glx_cur_display;
static uint32_t glx_cur_handle;

static uint32_t glx_ctx_handle(GLXContext c)
{
    uint32_t i;
    if (!c) return 0;
    for (i = 0; i < GLX_MAX_CONTEXTS; ++i)
        if (glx_ctx[i] == c) return 0x0a000000u + i + 1u;
    for (i = 0; i < GLX_MAX_CONTEXTS; ++i)
        if (!glx_ctx[i]) { glx_ctx[i] = c; return 0x0a000000u + i + 1u; }
    return 0;
}

static GLXContext glx_ctx_from(uint32_t h)
{
    if (h <= 0x0a000000u || h > 0x0a000000u + GLX_MAX_CONTEXTS) return 0;
    return glx_ctx[h - 0x0a000001u];
}

static Display *glx_display(IrixGuestCpu *cpu)
{
    return irix_x11_primary_display_from_guest((uint32_t)cpu->gpr[4]);
}

static XVisualInfo *host_visualinfo(Display *d, IrixGuestRuntime *rt, IrixGuestAddr gvi)
{
    XVisualInfo t, *v;
    int n = 0;
    if (!gvi || !irix_guest_ptr(rt, gvi, 40u)) return 0;
    memset(&t, 0, sizeof(t));
    t.visualid = (VisualID)irix_guest_read32be(rt, gvi + 4u);
    t.screen = (int)irix_guest_read32be(rt, gvi + 8u);
    v = XGetVisualInfo(d, VisualIDMask | VisualScreenMask, &t, &n);
    if ((!v || n < 1) && !(v = XGetVisualInfo(d, VisualIDMask, &t, &n))) return 0;
    return v;
}

static IrixGuestAddr guest_visualinfo(IrixGuestRuntime *rt, IrixGuestCpu *cpu, XVisualInfo *v)
{
    IrixGuestAddr out = irix_guest_xt_compat_alloc(rt, 40u);
    IrixGuestAddr gv;
    if (!out) return 0;
    gv = irix_x11_guest_visual_by_id(rt, (uint32_t)cpu->gpr[4], v->screen, v->visualid);
    irix_guest_write32be(rt, out + 0u, gv);
    irix_guest_write32be(rt, out + 4u, (uint32_t)v->visualid);
    irix_guest_write32be(rt, out + 8u, (uint32_t)v->screen);
    irix_guest_write32be(rt, out + 12u, (uint32_t)v->depth);
    irix_guest_write32be(rt, out + 16u, (uint32_t)v->class);
    irix_guest_write32be(rt, out + 20u, (uint32_t)v->red_mask);
    irix_guest_write32be(rt, out + 24u, (uint32_t)v->green_mask);
    irix_guest_write32be(rt, out + 28u, (uint32_t)v->blue_mask);
    irix_guest_write32be(rt, out + 32u, (uint32_t)v->colormap_size);
    irix_guest_write32be(rt, out + 36u, (uint32_t)v->bits_per_rgb);
    return out;
}

static void gl_b_glXQueryExtension(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    int eb = 0, vb = 0, ok = d ? glXQueryExtension(d, &eb, &vb) : 0;
    if (I(1)) irix_guest_write32be(rt, I(1), (uint32_t)eb);
    if (I(2)) irix_guest_write32be(rt, I(2), (uint32_t)vb);
    cpu->gpr[2] = (uint32_t)ok;
}

static void gl_b_glXQueryVersion(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    int ma = 0, mi = 0, ok = d ? glXQueryVersion(d, &ma, &mi) : 0;
    if (I(1)) irix_guest_write32be(rt, I(1), (uint32_t)ma);
    if (I(2)) irix_guest_write32be(rt, I(2), (uint32_t)mi);
    cpu->gpr[2] = (uint32_t)ok;
}

static void gl_b_glXChooseVisual(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    int attr[64], n = 0;
    IrixGuestAddr p = I(2);
    XVisualInfo *v = 0;
    cpu->gpr[2] = 0;
    if (!d) return;
    for (; n < 60;) {
        uint32_t k = p ? irix_guest_read32be(rt, p) : 0;
        p += 4u;
        attr[n++] = (int)k;
        if (!k) break;
        if (k == GLX_USE_GL || k == GLX_RGBA || k == GLX_DOUBLEBUFFER || k == GLX_STEREO)
            continue;
        attr[n++] = (int)irix_guest_read32be(rt, p);
        p += 4u;
    }
    attr[n < 64 ? n : 63] = 0;
    attr[63] = 0;
    if (gl_trace_on()) {
        int k;
        fprintf(stderr, "IRIX GL: glXChooseVisual screen=%d attrs:", (int)I(1));
        for (k = 0; k < n; ++k) fprintf(stderr, " %d", attr[k]);
        fputc('\n', stderr);
    }
    v = glXChooseVisual(d, (int)I(1), attr);
    if (v) {
        cpu->gpr[2] = guest_visualinfo(rt, cpu, v);
        XFree(v);
    }
    if (gl_trace_on())
        fprintf(stderr, "IRIX GL: glXChooseVisual -> %08x\n", (unsigned)cpu->gpr[2]);
}

static void gl_b_glXGetConfig(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    XVisualInfo *v = d ? host_visualinfo(d, rt, I(1)) : 0;
    int val = 0, rc = 1; /* GLX_BAD_SCREEN-like failure */
    if (v) {
        rc = glXGetConfig(d, v, (int)I(2), &val);
        XFree(v);
    }
    if (I(3)) irix_guest_write32be(rt, I(3), (uint32_t)val);
    if (gl_trace_on())
        fprintf(stderr, "IRIX GL: glXGetConfig vis=%08x attr=%d -> rc=%d val=%d\n",
                (unsigned)I(1), (int)I(2), rc, val);
    cpu->gpr[2] = (uint32_t)rc;
}

static void gl_b_glXCreateContext(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    XVisualInfo *v = d ? host_visualinfo(d, rt, I(1)) : 0;
    GLXContext c = 0;
    if (v) {
        c = glXCreateContext(d, v, glx_ctx_from(I(2)), I(3) ? True : False);
        XFree(v);
    }
    cpu->gpr[2] = glx_ctx_handle(c);
}

static void gl_b_glXDestroyContext(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    GLXContext c = glx_ctx_from(I(1));
    (void)rt;
    if (d && c) {
        glXDestroyContext(d, c);
        glx_ctx[I(1) - 0x0a000001u] = 0;
        if (glx_cur_handle == I(1)) glx_cur_handle = 0;
    }
    cpu->gpr[2] = 0;
}

static void gl_b_glXMakeCurrent(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    GLXContext c = glx_ctx_from(I(2));
    Bool ok = d ? glXMakeCurrent(d, (GLXDrawable)I(1), c) : False;
    if (gl_trace_on())
        fprintf(stderr, "IRIX GL: glXMakeCurrent drawable=%08x ctx=%08x ra=%08x -> %d\n",
                (unsigned)I(1), (unsigned)I(2), (unsigned)cpu->gpr[31], (int)ok);
    if (ok) {
        glx_cur_display = (uint32_t)cpu->gpr[4];
        glx_cur_handle = c ? I(2) : 0;
    }
    cpu->gpr[2] = (uint32_t)ok;
}

static void gl_b_glXGetCurrentContext(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = glXGetCurrentContext() ? glx_cur_handle : 0;
}

static void gl_b_glXGetCurrentDisplay(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = glx_cur_display;
}

static void gl_b_glXGetCurrentDrawable(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint32_t)glXGetCurrentDrawable();
}

static void gl_b_glXSwapBuffers(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    (void)rt;
    if (d) glXSwapBuffers(d, (GLXDrawable)I(1));
    cpu->gpr[2] = 0;
}

static void gl_b_glXWaitGL(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    glXWaitGL();
    cpu->gpr[2] = 0;
}

static void gl_b_glXWaitX(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    glXWaitX();
    cpu->gpr[2] = 0;
}

static void gl_b_glXIsDirect(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    GLXContext c = glx_ctx_from(I(1));
    (void)rt;
    cpu->gpr[2] = (d && c) ? (uint32_t)glXIsDirect(d, c) : 0u;
}

static void gl_b_glXCopyContext(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    GLXContext s = glx_ctx_from(I(1)), t = glx_ctx_from(I(2));
    (void)rt;
    if (d && s && t) glXCopyContext(d, s, t, I(3));
    cpu->gpr[2] = 0;
}

static void gl_b_glXUseXFont(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    glXUseXFont((Font)I(0), (int)I(1), (int)I(2), (int)I(3));
    cpu->gpr[2] = 0;
}

static void gl_b_glXQueryExtensionsString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    cpu->gpr[2] = d ? gl_guest_string(rt, glXQueryExtensionsString(d, (int)I(1))) : 0u;
}

static void gl_b_glXGetClientString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    cpu->gpr[2] = d ? gl_guest_string(rt, glXGetClientString(d, (int)I(1))) : 0u;
}

static void gl_b_glXQueryServerString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = glx_display(cpu);
    cpu->gpr[2] = d ? gl_guest_string(rt, glXQueryServerString(d, (int)I(1), (int)I(2))) : 0u;
}

static void gl_b_glXSwapIntervalSGI(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    typedef int (*fn_t)(int);
    fn_t f = (fn_t)gl_proc("glXSwapIntervalSGI");
    (void)rt;
    cpu->gpr[2] = f ? (uint32_t)f((int)I(0)) : 0u;
}

/* Video sync, FBConfig and pbuffer entry points: report them unavailable. */
static void gl_b_glx_unsupported(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = 0;
}

/*
 * IRIS GL asks the X Input extension about valuator devices (dials, tablets)
 * at start-up.  The program drives the pointer and keyboard through core
 * events, so report that no extension devices exist.
 */
static void gl_b_XListInputDevices(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    if (I(1)) irix_guest_write32be(rt, I(1), 0u);
    cpu->gpr[2] = 0;
}

static void gl_b_xi_none(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = 0;
}

static void gl_b_xi_noop(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    (void)cpu;
}

typedef struct { const char *name; IrixGuestFunction fn; } GlNamed;

static const GlNamed gl_named[] = {
#define N(n) { #n, gl_b_##n }
    N(glLightfv), N(glMaterialfv), N(glTexEnvfv), N(glTexGenfv), N(glFogfv),
    N(glLightModelfv), N(glLoadMatrixf), N(glMultMatrixf), N(glMultMatrixd),
    N(glClipPlane), N(glTexFilterFuncSGIS), N(glGetFloatv), N(glGetIntegerv),
    N(glGetBooleanv), N(glGetMaterialfv), N(glGetTexLevelParameteriv),
    N(glGetString), N(glCallLists), N(glGenTexturesEXT), N(glDeleteTexturesEXT),
    N(glAreTexturesResidentEXT), N(glFeedbackBuffer), N(glSelectBuffer),
    N(glRenderMode), N(glDrawPixels), N(glReadPixels), N(glBitmap),
    N(glPolygonStipple), N(glTexImage1D), N(glTexImage2D), N(glTexImage3DEXT),
    N(glTexSubImage2DEXT), N(glColorTableSGI), N(glMap1f), N(glMap2f),
    N(glIglooInterfaceSGIX),
    N(glXQueryExtension), N(glXQueryVersion), N(glXChooseVisual), N(glXGetConfig),
    N(glXCreateContext), N(glXDestroyContext), N(glXMakeCurrent),
    N(glXGetCurrentContext), N(glXGetCurrentDisplay), N(glXGetCurrentDrawable),
    N(glXSwapBuffers), N(glXWaitGL), N(glXWaitX), N(glXIsDirect),
    N(glXCopyContext), N(glXUseXFont), N(glXQueryExtensionsString),
    N(glXGetClientString), N(glXQueryServerString), N(glXSwapIntervalSGI),
#undef N
    { 0, 0 }
};

/*
 * IGLOO keeps its per-process graphics state pointer in a word at a fixed low
 * address (it stores to 0x100c(zero)); on IRIX that page is part of the
 * process image the GL library maps.  Give it a zeroed page.
 */
void irix_gl_bridge_init(IrixGuestRuntime *rt)
{
    (void)irix_guest_map(rt, 0x1000u, 0x1000u, 3u);
}

IrixGuestFunction irix_gl_bridge_lookup(const char *name)
{
    size_t i;
    if (name && name[0] == 'X') {
        if (!strcmp(name, "XListInputDevices")) return gl_b_XListInputDevices;
        if (!strcmp(name, "XOpenDevice") || !strcmp(name, "XQueryDeviceState") ||
            !strcmp(name, "XGetFeedbackControl") || !strcmp(name, "XCloseDevice") ||
            !strcmp(name, "XSelectExtensionEvent") ||
            !strcmp(name, "XChangeFeedbackControl") ||
            !strcmp(name, "XChangePointerDevice"))
            return gl_b_xi_none;
        if (!strcmp(name, "XFreeDeviceState") || !strcmp(name, "XFreeDeviceList"))
            return gl_b_xi_noop;
        return 0;
    }
    if (!name || name[0] != 'g' || name[1] != 'l') return 0;
    for (i = 0; gl_named[i].name; ++i)
        if (!strcmp(name, gl_named[i].name)) return gl_named[i].fn;
    for (i = 0; gl_scalar_table[i].name; ++i)
        if (!strcmp(name, gl_scalar_table[i].name)) return gl_scalar_table[i].fn;
    if (!strncmp(name, "glX", 3) && (strstr(name, "SGI") || strstr(name, "EXT")))
        return gl_b_glx_unsupported;
    return 0;
}
