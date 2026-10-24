/* Streaming extensions for the original xwsh TTY parser. The translated
 * renderer and its SGI terminal sequences remain the implementation. */
#include "IrixGuest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned state, length, title_length;
    unsigned char sequence[64], title[192];
    int title_code;
} XwshStream;

/* emit is synchronous; the adapter never buffers an unbounded OSC payload. */
typedef void (*XwshEmit)(void *, const unsigned char *, size_t);
static void xwsh_stream_feed(XwshStream *s, const unsigned char *in, size_t len,
                             XwshEmit emit, void *opaque)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        unsigned char c = in[i];
        if (s->state && (c == 0x18 || c == 0x1a)) {
            s->state = s->length = s->title_length = 0;
            continue;
        }
        switch (s->state) {
        case 0:
            if (c == 0x1b) s->state = 1;
            else emit(opaque, &c, 1);
            break;
        case 1:
            if (c == ']') {
                s->state = 3; s->length = s->title_length = 0;
                s->title_code = 0;
            } else if (c == '[') {
                s->state = 2; s->length = 0;
            } else if (c != 0x1b) {
                const unsigned char esc = 0x1b;
                emit(opaque, &esc, 1); emit(opaque, &c, 1); s->state = 0;
            }
            break;
        case 2:
            if (c == 0x1b) { s->state = 1; break; }
            if (c >= 0x40 && c <= 0x7e) {
                unsigned char out[80];
                unsigned n = 0, j;
                int numeric = 1;
                for (j = 0; j < s->length; ++j) {
                    if (s->sequence[j] < '0' || s->sequence[j] > '9') numeric = 0;
                    else if (n < 100000) n = n * 10 + s->sequence[j] - '0';
                }
                if (numeric && (c == 'E' || c == 'F' || c == 'A' ||
                                c == 'B' || c == 'C' || c == 'D')) {
                    int count = snprintf((char *)out, sizeof(out), "\033[%u%c",
                                         n ? n : 1, c == 'E' ? 'B' : c == 'F' ? 'A' : c);
                    emit(opaque, out, (size_t)count);
                    if (c == 'E' || c == 'F') emit(opaque, (const unsigned char *)"\r", 1);
                } else if (numeric && c == 'G') {
                    /* CHA: emulate an absolute column with CR + supported CUF. */
                    unsigned column = n ? n : 1;
                    emit(opaque, (const unsigned char *)"\r", 1);
                    if (column > 1) {
                        int count = snprintf((char *)out, sizeof(out), "\033[%uC", column - 1);
                        emit(opaque, out, (size_t)count);
                    }
                } else {
                    emit(opaque, (const unsigned char *)"\033[", 2);
                    emit(opaque, s->sequence, s->length); emit(opaque, &c, 1);
                }
                s->state = 0;
            } else if (s->length < sizeof(s->sequence)) {
                s->sequence[s->length++] = c;
            } else s->state = 6; /* discard overlong CSI through its final byte */
            break;
        case 3: /* OSC numeric selector */
            if (c >= '0' && c <= '9') {
                if (s->title_code < 10000) s->title_code = s->title_code * 10 + c - '0';
                ++s->length;
            } else if (c == ';') {
                s->state = 4;
                if (!s->length || s->title_code > 2) s->title_code = -1;
            } else {
                s->title_code = -1;
                s->state = c == 0x1b ? 5 : c == 7 ? 0 : 4;
            }
            break;
        case 4: /* OSC payload; retain only supported title/icon operations */
            if (c == 7) goto osc_done;
            if (c == 0x1b) s->state = 5;
            else if (s->title_code >= 0 && s->title_length < sizeof(s->title))
                s->title[s->title_length++] = c;
            break;
        case 5:
            if (c == '\\' || c == 7) {
osc_done:
                if (s->title_code >= 0) {
                    unsigned char head[] = {0x1b, ']', '0' + s->title_code, ';'};
                    emit(opaque, head, sizeof(head));
                    emit(opaque, s->title, s->title_length);
                    emit(opaque, (const unsigned char *)"\007", 1);
                }
                s->state = 0;
            } else if (c != 0x1b) s->state = 4;
            break;
        case 6:
            if (c >= 0x40 && c <= 0x7e) s->state = 0;
            else if (c == 0x1b) s->state = 1;
            break;
        }
    }
}

#ifndef XWSH_STREAM_TEST
static IrixGuestFunction original_write;
static IrixGuestAddr original_address;
typedef struct XwshTerminal {
    IrixGuestAddr object, scratch;
    XwshStream stream;
    struct XwshTerminal *next;
} XwshTerminal;
static XwshTerminal *terminals;
typedef struct {
    IrixGuestRuntime *rt;
    IrixGuestCpu *cpu;
    XwshTerminal *terminal;
    unsigned used;
    unsigned char data[4096];
} XwshOutput;

static void flush_output(XwshOutput *o)
{
    void *guest;
    if (!o->used || o->rt->trapped) return;
    guest = irix_guest_ptr(o->rt, o->terminal->scratch, o->used);
    if (!guest) return;
    memcpy(guest, o->data, o->used);
    o->cpu->gpr[4] = o->terminal->object;
    o->cpu->gpr[5] = o->terminal->scratch;
    o->cpu->gpr[6] = o->used;
    o->cpu->gpr[25] = original_address;
    original_write(o->rt, o->cpu);
    o->used = 0;
}
static void emit_output(void *opaque, const unsigned char *p, size_t n)
{
    XwshOutput *o = opaque;
    if (o->used + n > sizeof(o->data)) flush_output(o);
    memcpy(o->data + o->used, p, n); o->used += n;
}
static void compatible_write(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr object = (uint32_t)cpu->gpr[4];
    size_t n = (uint32_t)cpu->gpr[6];
    const unsigned char *in = irix_guest_ptr(rt, (uint32_t)cpu->gpr[5], n);
    XwshTerminal *t;
    XwshOutput output;
    if (!in || !n) { original_write(rt, cpu); return; }
    for (t = terminals; t && t->object != object; t = t->next) {}
    if (!t) {
        t = calloc(1, sizeof(*t));
        if (!t) { original_write(rt, cpu); return; }
        t->scratch = irix_guest_xt_compat_alloc(rt, 4096);
        if (!t->scratch) { free(t); original_write(rt, cpu); return; }
        t->object = object; t->next = terminals; terminals = t;
    }
    memset(&output, 0, sizeof(output));
    output.rt = rt; output.cpu = cpu; output.terminal = t;
    xwsh_stream_feed(&t->stream, in, n, emit_output, &output);
    flush_output(&output);
}
void irix_xwsh_compat_init(IrixGuestRuntime *rt)
{
    size_t i;
    for (i = 0; i < rt->function_count; ++i) {
        IrixGuestFunctionEntry *e = &rt->functions[i];
        if (e->name && !strcmp(e->name, "write__11TTYEmulatorGPCcUi")) {
            original_write = e->function; original_address = e->address;
            e->function = compatible_write;
            return;
        }
    }
}
#endif
