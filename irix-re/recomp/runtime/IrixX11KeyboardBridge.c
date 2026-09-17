#include "IrixX11KeyboardBridge.h"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>

#define IRIX_X11_KBD_TMP_BASE 0x72700000u
#define IRIX_X11_KBD_TMP_SIZE 0x00100000u
#define IRIX_X11_DISPLAY_BASE 0x72400000u
#define IRIX_X11_DISPLAY_STRIDE 0x00001000u
#define IRIX_X11_MAX_DISPLAYS 16u

typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestAddr tmp_cur;
    Display *displays[IRIX_X11_MAX_DISPLAYS];
    size_t display_count;
} IrixX11KeyboardState;

static IrixX11KeyboardState g_kbd;

/* The broad Xlib bridge owns Display creation. We recover the host Display by
   opening the same display name lazily for keyboard-only calls. This module is
   intentionally self-contained; it never exposes host pointers to the guest. */
static Display *display_from_guest(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    uint32_t off, i;
    IrixGuestAddr name_addr;
    const char *name;
    if (addr < IRIX_X11_DISPLAY_BASE || addr >= IRIX_X11_DISPLAY_BASE + IRIX_X11_MAX_DISPLAYS * IRIX_X11_DISPLAY_STRIDE)
        return 0;
    off = addr - IRIX_X11_DISPLAY_BASE;
    if (off % IRIX_X11_DISPLAY_STRIDE) return 0;
    i = off / IRIX_X11_DISPLAY_STRIDE;
    if (i >= IRIX_X11_MAX_DISPLAYS) return 0;
    if (g_kbd.displays[i]) return g_kbd.displays[i];
    name_addr = irix_guest_read32be(rt, addr + 128u);
    name = name_addr ? (const char *)irix_guest_ptr(rt, name_addr, 1) : 0;
    g_kbd.displays[i] = XOpenDisplay(name);
    if (g_kbd.displays[i] && i >= g_kbd.display_count) g_kbd.display_count = i + 1u;
    return g_kbd.displays[i];
}

static IrixGuestAddr tmp_alloc(uint32_t n)
{
    IrixGuestAddr a;
    n = (n + 7u) & ~7u;
    if (!n || g_kbd.tmp_cur + n < g_kbd.tmp_cur ||
        g_kbd.tmp_cur + n > IRIX_X11_KBD_TMP_BASE + IRIX_X11_KBD_TMP_SIZE)
        return 0;
    a = g_kbd.tmp_cur;
    g_kbd.tmp_cur += n;
    return a;
}

static void wr32(IrixGuestRuntime *rt, IrixGuestAddr p, uint32_t v)
{
    if (p) irix_guest_write32be(rt, p, v);
}

static void bridge_XDisplayKeycodes(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(rt, (uint32_t)cpu->gpr[4]);
    int minc = 0, maxc = 0;
    if (d) XDisplayKeycodes(d, &minc, &maxc);
    wr32(rt, (uint32_t)cpu->gpr[5], (uint32_t)minc);
    wr32(rt, (uint32_t)cpu->gpr[6], (uint32_t)maxc);
    cpu->gpr[2] = 0;
}

static void bridge_XGetKeyboardMapping(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(rt, (uint32_t)cpu->gpr[4]);
    int per = 0;
    int count = (int)(uint32_t)cpu->gpr[6];
    KeySym *map = d ? XGetKeyboardMapping(d, (KeyCode)(uint32_t)cpu->gpr[5], count, &per) : 0;
    IrixGuestAddr out = 0;
    int i, n = (count > 0 && per > 0) ? count * per : 0;
    wr32(rt, (uint32_t)cpu->gpr[7], (uint32_t)per);
    if (map && n > 0 && (uint64_t)n * 4u <= UINT32_MAX) {
        out = tmp_alloc((uint32_t)n * 4u);
        if (out) for (i = 0; i < n; ++i) wr32(rt, out + (IrixGuestAddr)i * 4u, (uint32_t)map[i]);
    }
    if (map) XFree(map);
    cpu->gpr[2] = out;
}

static void bridge_XGetModifierMapping(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(rt, (uint32_t)cpu->gpr[4]);
    XModifierKeymap *m = d ? XGetModifierMapping(d) : 0;
    IrixGuestAddr out = 0, keys = 0;
    int n, i;
    if (m) {
        n = 8 * m->max_keypermod;
        keys = tmp_alloc((uint32_t)(n > 0 ? n : 1));
        out = tmp_alloc(8u);
        if (keys && out) {
            unsigned char *p = (unsigned char *)irix_guest_ptr(rt, keys, (size_t)(n > 0 ? n : 1));
            if (p && n > 0) for (i = 0; i < n; ++i) p[i] = m->modifiermap[i];
            wr32(rt, out + 0, (uint32_t)m->max_keypermod);
            wr32(rt, out + 4, keys);
        } else out = 0;
        XFreeModifiermap(m);
    }
    cpu->gpr[2] = out;
}

static void bridge_XFreeModifiermap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr map = (uint32_t)cpu->gpr[4];
    (void)rt;

    /* XGetModifierMapping above returns a guest-side copy and frees the host
       XModifierKeymap immediately.  The guest keyboard scratch arena is
       monotonic, so there is no individual allocation to release here. */
    (void)map;
    cpu->gpr[2] = 1;
}

static void bridge_XkbLookupKeySym(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(rt, (uint32_t)cpu->gpr[4]);
    KeyCode keycode = (KeyCode)(uint32_t)cpu->gpr[5];
    unsigned int state = (unsigned int)(uint32_t)cpu->gpr[6];
    IrixGuestAddr mods_out = (uint32_t)cpu->gpr[7];
    IrixGuestAddr sym_out = (uint32_t)cpu->gpr[8];
    unsigned int mods = 0;
    KeySym sym = NoSymbol;
    Bool ok = False;

    if (d)
        ok = XkbLookupKeySym(d, keycode, state, &mods, &sym);
    wr32(rt, mods_out, (uint32_t)mods);
    wr32(rt, sym_out, (uint32_t)sym);
    cpu->gpr[2] = ok ? 1u : 0u;
}

static void bridge_XQueryBestCursor(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(rt, (uint32_t)cpu->gpr[4]);
    unsigned int width = 0, height = 0;
    Status ok = 0;

    if (d)
        ok = XQueryBestCursor(d, (Drawable)(uint32_t)cpu->gpr[5],
                              (unsigned int)(uint32_t)cpu->gpr[6],
                              (unsigned int)(uint32_t)cpu->gpr[7],
                              &width, &height);
    wr32(rt, (uint32_t)cpu->gpr[8], width);
    wr32(rt, (uint32_t)cpu->gpr[9], height);
    cpu->gpr[2] = (uint32_t)ok;
}

static void bridge_XKeycodeToKeysym(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = d ? (uint32_t)XKeycodeToKeysym(d, (KeyCode)(uint32_t)cpu->gpr[5], (int)cpu->gpr[6]) : 0;
}

static void bridge_XKeysymToKeycode(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    Display *d = display_from_guest(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = d ? (uint32_t)XKeysymToKeycode(d, (KeySym)(uint32_t)cpu->gpr[5]) : 0;
}

static void bridge_XKeysymToString(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = XKeysymToString((KeySym)(uint32_t)cpu->gpr[4]);
    IrixGuestAddr out = 0;
    size_t n;
    void *p;
    if (s) {
        n = strlen(s) + 1u;
        if (n <= UINT32_MAX) {
            out = tmp_alloc((uint32_t)n);
            p = out ? irix_guest_ptr(rt, out, n) : 0;
            if (p) memcpy(p, s, n); else out = 0;
        }
    }
    cpu->gpr[2] = out;
}

static void bridge_XStringToKeysym(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = (const char *)irix_guest_ptr(rt, (uint32_t)cpu->gpr[4], 1);
    cpu->gpr[2] = s ? (uint32_t)XStringToKeysym(s) : 0;
}

void irix_x11_keyboard_bridge_init(IrixGuestRuntime *rt)
{
    memset(&g_kbd, 0, sizeof(g_kbd));
    g_kbd.rt = rt;
    if (irix_guest_map(rt, IRIX_X11_KBD_TMP_BASE, IRIX_X11_KBD_TMP_SIZE, 3u) == 0 ||
        irix_guest_ptr(rt, IRIX_X11_KBD_TMP_BASE, 1))
        g_kbd.tmp_cur = IRIX_X11_KBD_TMP_BASE;
}

IrixGuestFunction irix_x11_keyboard_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "XDisplayKeycodes")) return bridge_XDisplayKeycodes;
    if (!strcmp(name, "XGetKeyboardMapping")) return bridge_XGetKeyboardMapping;
    if (!strcmp(name, "XGetModifierMapping")) return bridge_XGetModifierMapping;
    if (!strcmp(name, "XFreeModifiermap")) return bridge_XFreeModifiermap;
    if (!strcmp(name, "XkbLookupKeySym")) return bridge_XkbLookupKeySym;
    if (!strcmp(name, "XQueryBestCursor")) return bridge_XQueryBestCursor;
    if (!strcmp(name, "XKeycodeToKeysym")) return bridge_XKeycodeToKeysym;
    if (!strcmp(name, "XKeysymToKeycode")) return bridge_XKeysymToKeycode;
    if (!strcmp(name, "XKeysymToString")) return bridge_XKeysymToString;
    if (!strcmp(name, "XStringToKeysym")) return bridge_XStringToKeysym;
    return 0;
}
