#include "IrixX11ContextBridge.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
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

static void diagnose_save(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                          IrixGuestAddr display, uint32_t rid,
                          int32_t context, IrixGuestAddr data)
{
    IrixGuestAddr rec = 0;
    IrixGuestAddr widget = 0;
    IrixGuestAddr req = 0;
    IrixGuestAddr old = 0;
    IrixGuestAddr push_caller_ra = 0;
    IrixGuestAddr sp = (IrixGuestAddr)(uint32_t)cpu->gpr[29];
    uint32_t klass = 0;
    uint32_t logical_parent = 0;
    uint8_t extension_type = 0;
    uint8_t raw_offset_target = 0;
    int have_raw_offset_target = 0;

    /* _XmPushWidgetExtData saves its incoming $ra with `sd ra,8(sp)`. */
    if (sp && irix_guest_ptr(rt, sp + 8u, 8u))
        push_caller_ra = (IrixGuestAddr)(uint32_t)irix_guest_read64be(rt, sp + 8u);

    if (data && irix_guest_ptr(rt, data, 8u)) {
        rec = irix_guest_read32be(rt, data + 0u);
        if (!rt->trapped && rec && irix_guest_ptr(rt, rec, 12u)) {
            widget = irix_guest_read32be(rt, rec + 0u);
            req = irix_guest_read32be(rt, rec + 4u);
            old = irix_guest_read32be(rt, rec + 8u);
            if (!rt->trapped && widget && irix_guest_ptr(rt, widget, 33u)) {
                klass = irix_guest_read32be(rt, widget + 4u);
                logical_parent = irix_guest_read32be(rt, widget + 28u);
                extension_type = irix_guest_read8(rt, widget + 32u);
            }
            if (!rt->trapped && widget >= 33u && irix_guest_ptr(rt, widget - 33u, 1u)) {
                raw_offset_target = irix_guest_read8(rt, widget - 33u);
                have_raw_offset_target = !rt->trapped;
            }
        }
    }

    fprintf(stderr,
            "XSaveContext display=0x%08x rid=0x%08x context=0x%08x data=0x%08x "
            "pushCallerRA=0x%08x rec=0x%08x widget=0x%08x class=0x%08x logicalParent=0x%08x "
            "extensionType=%u rawOffsetTarget[-0x21]=%s%u req=0x%08x old=0x%08x\n",
            (uint32_t)display, rid, (uint32_t)context, (uint32_t)data,
            (uint32_t)push_caller_ra,
            (uint32_t)rec, (uint32_t)widget, klass, logical_parent,
            (unsigned)extension_type,
            have_raw_offset_target ? "" : "?",
            (unsigned)raw_offset_target,
            (uint32_t)req, (uint32_t)old);
}

static void bridge_XSaveContext(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr display = (uint32_t)cpu->gpr[4];
    uint32_t rid = (uint32_t)cpu->gpr[5];
    int32_t context = (int32_t)cpu->gpr[6];
    uint32_t data = (uint32_t)cpu->gpr[7];
    IrixX11ContextEntry *e = find_entry(display, rid, context);

    diagnose_save(rt, cpu, display, rid, context, data);
    if (rt->trapped) {
        cpu->gpr[2] = (uint32_t)XCNOENT;
        return;
    }

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

static void diagnose_find_miss(IrixGuestAddr display, uint32_t rid, int32_t context)
{
    size_t i;
    size_t related = 0;

    fprintf(stderr,
            "XFindContext MISS display=0x%08x rid=0x%08x context=0x%08x entries=%zu\n",
            (uint32_t)display, rid, (uint32_t)context, g_ctx.count);

    for (i = 0; i < g_ctx.count; ++i) {
        const IrixX11ContextEntry *e = &g_ctx.entries[i];
        if (e->display != display) continue;
        if (e->rid != rid && e->context != context) continue;
        fprintf(stderr,
                "  related[%zu] rid=0x%08x context=0x%08x data=0x%08x%s%s\n",
                i, e->rid, (uint32_t)e->context, e->data,
                e->rid == rid ? " same-rid" : "",
                e->context == context ? " same-context" : "");
        ++related;
    }
    if (!related)
        fprintf(stderr, "  no entries share this display+rid or display+context\n");
}

static void bridge_XFindContext(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr display = (uint32_t)cpu->gpr[4];
    uint32_t rid = (uint32_t)cpu->gpr[5];
    int32_t context = (int32_t)cpu->gpr[6];
    IrixGuestAddr out = (uint32_t)cpu->gpr[7];
    IrixX11ContextEntry *e = find_entry(display, rid, context);

    if (!e) {
        diagnose_find_miss(display, rid, context);
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
