#include "IrixX11ContextBridge.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    IrixGuestAddr display;
    uint32_t rid;
    int32_t context;
    uint32_t data;
} IrixX11ContextEntry;

typedef struct {
    IrixGuestRuntime *rt;
    IrixX11ContextEntry *entries;
    size_t count;
    size_t cap;
} IrixX11ContextState;

static IrixX11ContextState g_ctx;

static IrixX11ContextEntry *find_entry(IrixGuestAddr display, uint32_t rid, int32_t context)
{
    size_t i;
    for (i = 0; i < g_ctx.count; ++i) {
        IrixX11ContextEntry *e = &g_ctx.entries[i];
        if (e->display == display && e->rid == rid && e->context == context)
            return e;
    }
    return 0;
}

static int ensure_capacity(void)
{
    IrixX11ContextEntry *p;
    size_t cap;
    if (g_ctx.count < g_ctx.cap) return 0;
    cap = g_ctx.cap ? g_ctx.cap * 2u : 64u;
    if (cap < g_ctx.cap || cap > SIZE_MAX / sizeof(*p)) return -1;
    p = (IrixX11ContextEntry *)realloc(g_ctx.entries, cap * sizeof(*p));
    if (!p) return -1;
    g_ctx.entries = p;
    g_ctx.cap = cap;
    return 0;
}

static void bridge_XSaveContext(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr display = (uint32_t)cpu->gpr[4];
    uint32_t rid = (uint32_t)cpu->gpr[5];
    int32_t context = (int32_t)cpu->gpr[6];
    uint32_t data = (uint32_t)cpu->gpr[7];
    IrixX11ContextEntry *e = find_entry(display, rid, context);
    (void)rt;

    fprintf(stderr,
            "IRIX XSaveContext: display=%08x rid=%08x context=%08x data=%08x existing=%s\n",
            display, rid, (uint32_t)context, data, e ? "yes" : "no");

    if (e) {
        e->data = data;
        cpu->gpr[2] = 0;
        return;
    }
    if (ensure_capacity() != 0) {
        cpu->gpr[2] = (uint32_t)XCNOMEM;
        return;
    }
    e = &g_ctx.entries[g_ctx.count++];
    e->display = display;
    e->rid = rid;
    e->context = context;
    e->data = data;
    cpu->gpr[2] = 0;
}

static void bridge_XFindContext(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr display = (uint32_t)cpu->gpr[4];
    uint32_t rid = (uint32_t)cpu->gpr[5];
    int32_t context = (int32_t)cpu->gpr[6];
    IrixGuestAddr out = (uint32_t)cpu->gpr[7];
    IrixX11ContextEntry *e = find_entry(display, rid, context);

    fprintf(stderr,
            "IRIX XFindContext: display=%08x rid=%08x context=%08x out=%08x hit=%s data=%08x\n",
            display, rid, (uint32_t)context, out,
            e ? "yes" : "no", e ? e->data : 0u);

    if (!e) {
        /*
         * Emergency Motif XmDisplay bootstrap compatibility.
         *
         * SGI Motif repeatedly probes context -3 while constructing the
         * per-Display XmDisplay shell.  In the translated path the original
         * initializer never reaches XSaveContext before recursively asking
         * for XmDisplay again, so each miss creates another shell until the
         * stack is exhausted.
         *
         * The resource-id argument observed on this path is the freshly
         * created XmDisplay widget itself.  Seed the missing association on
         * the first probe, then satisfy the lookup immediately.  Scope this
         * narrowly to Motif's private context -3 and a plausible guest widget
         * pointer so ordinary Xlib contexts keep normal semantics.
         */
        if (context == -3 && rid >= 0x30000000u && rid < 0x40000000u) {
            if (ensure_capacity() == 0) {
                e = &g_ctx.entries[g_ctx.count++];
                e->display = display;
                e->rid = rid;
                e->context = context;
                e->data = rid;
                fprintf(stderr,
                        "IRIX XFindContext: seed XmDisplay context display=%08x rid=%08x context=%08x data=%08x\n",
                        display, rid, (uint32_t)context, rid);
                if (out)
                    irix_guest_write32be(rt, out, rid);
                cpu->gpr[2] = rt->trapped ? (uint32_t)XCNOENT : 0u;
                return;
            }
        }
        cpu->gpr[2] = (uint32_t)XCNOENT;
        return;
    }
    if (out)
        irix_guest_write32be(rt, out, e->data);
    cpu->gpr[2] = rt->trapped ? (uint32_t)XCNOENT : 0u;
}

static void bridge_XDeleteContext(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr display = (uint32_t)cpu->gpr[4];
    uint32_t rid = (uint32_t)cpu->gpr[5];
    int32_t context = (int32_t)cpu->gpr[6];
    size_t i;
    (void)rt;

    for (i = 0; i < g_ctx.count; ++i) {
        IrixX11ContextEntry *e = &g_ctx.entries[i];
        if (e->display == display && e->rid == rid && e->context == context) {
            g_ctx.entries[i] = g_ctx.entries[g_ctx.count - 1u];
            --g_ctx.count;
            cpu->gpr[2] = 0;
            return;
        }
    }
    cpu->gpr[2] = (uint32_t)XCNOENT;
}

void irix_x11_context_bridge_init(IrixGuestRuntime *rt)
{
    free(g_ctx.entries);
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.rt = rt;
}

IrixGuestFunction irix_x11_context_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "XSaveContext")) return bridge_XSaveContext;
    if (!strcmp(name, "XFindContext")) return bridge_XFindContext;
    if (!strcmp(name, "XDeleteContext")) return bridge_XDeleteContext;
    return 0;
}
