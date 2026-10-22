/*
 * SGI Audio Library (libaudio, "AL") on the host sound server.
 *
 * IRIX programs open an output port, set device parameters (sample rate,
 * speaker gain) and write big-endian sample buffers.  The original library is
 * a thin layer over the audio kernel device, which does not exist here, so
 * the AL entry points are provided natively and play through PulseAudio
 * (PipeWire's compatible server).  Only the original AL calls the desktop and
 * the sound tools use are implemented; the rest stay with the translated
 * library.
 *
 * Calling convention: N32, args in gpr[4..11], result in gpr[2].
 */
#include "IrixGuest.h"

#include <errno.h>
#include <fcntl.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RET(v) do { cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)(v); } while (0)
#define ARG(n) ((uint32_t)cpu->gpr[4 + (n)])

/* <dmedia/audio.h> */
#define AL_SAMPFMT_TWOSCOMP 1
#define AL_SAMPFMT_FLOAT 32
#define AL_SAMPFMT_DOUBLE 64
#define AL_INPUT_SOURCE 0
#define AL_LEFT_INPUT_ATTEN 1
#define AL_RIGHT_INPUT_ATTEN 2
#define AL_INPUT_RATE 3
#define AL_OUTPUT_RATE 4
#define AL_LEFT_SPEAKER_GAIN 5
#define AL_RIGHT_SPEAKER_GAIN 6
#define AL_INPUT_COUNT 7
#define AL_OUTPUT_COUNT 8
#define AL_MONITOR_CTL 12
#define AL_CHANNEL_MODE 15
#define AL_SPEAKER_MUTE_CTL 16
#define AL_MIC_MODE 17
#define AL_NPARAMS 32

#define CFG_BASE 0x7a100000u
#define PORT_BASE 0x7a200000u
#define HANDLE_STRIDE 0x10u
#define MAX_CFG 64
#define MAX_PORT 32
#define DEFAULT_QUEUE 100000

typedef struct {
    int used;
    int width;      /* AL_SAMPLE_8/16/24 */
    int sampfmt;
    int channels;
    int queuesize;  /* in samples */
} AlConfig;

typedef struct {
    int used;
    int output;
    AlConfig cfg;
    pa_simple *pa;
    int pa_rate;
    int fd;
    char name[32];
} AlPort;

static AlConfig g_cfg[MAX_CFG];
static AlPort g_port[MAX_PORT];
static int32_t g_param[AL_NPARAMS];
static int g_params_ready;

static void params_init(void)
{
    if (g_params_ready) return;
    g_params_ready = 1;
    g_param[AL_INPUT_RATE] = 44100;
    g_param[AL_OUTPUT_RATE] = 44100;
    g_param[AL_LEFT_SPEAKER_GAIN] = 255;
    g_param[AL_RIGHT_SPEAKER_GAIN] = 255;
    g_param[AL_INPUT_COUNT] = 2;
    g_param[AL_OUTPUT_COUNT] = 2;
    g_param[AL_CHANNEL_MODE] = 2;
}

static AlConfig *cfg_from(uint32_t h)
{
    uint32_t i;
    if (h < CFG_BASE || (h - CFG_BASE) % HANDLE_STRIDE) return 0;
    i = (h - CFG_BASE) / HANDLE_STRIDE;
    return i < MAX_CFG && g_cfg[i].used ? &g_cfg[i] : 0;
}

static AlPort *port_from(uint32_t h)
{
    uint32_t i;
    if (h < PORT_BASE || (h - PORT_BASE) % HANDLE_STRIDE) return 0;
    i = (h - PORT_BASE) / HANDLE_STRIDE;
    return i < MAX_PORT && g_port[i].used ? &g_port[i] : 0;
}

static void cfg_defaults(AlConfig *c)
{
    c->used = 1;
    c->width = 2;
    c->sampfmt = AL_SAMPFMT_TWOSCOMP;
    c->channels = 2;
    c->queuesize = DEFAULT_QUEUE;
}

static void guest_string(IrixGuestRuntime *rt, uint32_t addr, char *out, size_t n)
{
    size_t i;
    for (i = 0; addr && i + 1 < n; ++i) {
        char c = (char)irix_guest_read8(rt, addr + (uint32_t)i);
        if (!c) break;
        out[i] = c;
    }
    out[i < n ? i : n - 1] = 0;
}

static void b_ALnewconfig(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t i;
    (void)rt;
    for (i = 0; i < MAX_CFG; ++i) {
        if (!g_cfg[i].used) {
            cfg_defaults(&g_cfg[i]);
            RET(CFG_BASE + i * HANDLE_STRIDE);
            return;
        }
    }
    RET(0);
}

static void b_ALfreeconfig(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlConfig *c = cfg_from(ARG(0));
    (void)rt;
    if (c) c->used = 0;
    RET(0);
}

#define CFG_ACCESSORS(Name, field) \
static void b_ALset##Name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) \
{ AlConfig *c = cfg_from(ARG(0)); (void)rt; if (!c) { RET(-1); return; } c->field = (int)ARG(1); RET(0); } \
static void b_ALget##Name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) \
{ AlConfig *c = cfg_from(ARG(0)); (void)rt; RET(c ? c->field : -1); }

CFG_ACCESSORS(width, width)
CFG_ACCESSORS(sampfmt, sampfmt)
CFG_ACCESSORS(channels, channels)
CFG_ACCESSORS(queuesize, queuesize)

static void b_ALopenport(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlConfig *c = ARG(2) ? cfg_from(ARG(2)) : 0;
    char name[32], dir[8];
    uint32_t i;
    guest_string(rt, ARG(0), name, sizeof name);
    guest_string(rt, ARG(1), dir, sizeof dir);
    params_init();
    for (i = 0; i < MAX_PORT; ++i) {
        if (!g_port[i].used) {
            AlPort *p = &g_port[i];
            memset(p, 0, sizeof *p);
            p->used = 1;
            p->output = dir[0] != 'r';
            if (c) p->cfg = *c; else cfg_defaults(&p->cfg);
            p->fd = open("/dev/null", O_RDWR);
            snprintf(p->name, sizeof p->name, "%s", name[0] ? name : "IRIX audio");
            RET(PORT_BASE + i * HANDLE_STRIDE);
            return;
        }
    }
    RET(0);
}

static void pa_close(AlPort *p)
{
    if (!p->pa) return;
    pa_simple_drain(p->pa, 0);
    pa_simple_free(p->pa);
    p->pa = 0;
}

static void b_ALcloseport(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlPort *p = port_from(ARG(0));
    (void)rt;
    if (!p) { RET(-1); return; }
    pa_close(p);
    if (p->fd >= 0) close(p->fd);
    p->used = 0;
    RET(0);
}

static void b_ALsetconfig(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlPort *p = port_from(ARG(0));
    AlConfig *c = cfg_from(ARG(1));
    (void)rt;
    if (!p || !c) { RET(-1); return; }
    pa_close(p);
    p->cfg = *c;
    RET(0);
}

static void b_ALgetconfig(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlPort *p = port_from(ARG(0));
    uint32_t i;
    (void)rt;
    if (!p) { RET(0); return; }
    for (i = 0; i < MAX_CFG; ++i) {
        if (!g_cfg[i].used) {
            g_cfg[i] = p->cfg;
            g_cfg[i].used = 1;
            RET(CFG_BASE + i * HANDLE_STRIDE);
            return;
        }
    }
    RET(0);
}

static int pa_start(AlPort *p)
{
    pa_sample_spec ss;
    pa_buffer_attr attr;
    int err = 0, ch = p->cfg.channels > 0 ? p->cfg.channels : 2;
    int rate = g_param[AL_OUTPUT_RATE] > 0 ? g_param[AL_OUTPUT_RATE] : 44100;
    if (p->pa && p->pa_rate == rate) return 0;
    pa_close(p);
    memset(&ss, 0, sizeof ss);
    ss.format = (p->cfg.sampfmt == AL_SAMPFMT_TWOSCOMP && p->cfg.width == 1) ? PA_SAMPLE_U8
              : (p->cfg.sampfmt == AL_SAMPFMT_TWOSCOMP && p->cfg.width == 2) ? PA_SAMPLE_S16LE
              : (p->cfg.sampfmt == AL_SAMPFMT_TWOSCOMP) ? PA_SAMPLE_S32LE
              : PA_SAMPLE_FLOAT32LE;
    ss.rate = (uint32_t)rate;
    ss.channels = (uint8_t)ch;
    attr.maxlength = (uint32_t)-1;
    attr.tlength = pa_usec_to_bytes(100000, &ss);
    attr.prebuf = (uint32_t)-1;
    attr.minreq = (uint32_t)-1;
    attr.fragsize = (uint32_t)-1;
    p->pa = pa_simple_new(0, "IRIX", PA_STREAM_PLAYBACK, 0, p->name, &ss, 0, &attr, &err);
    if (!p->pa) {
        fprintf(stderr, "IRIX AL: cannot open PulseAudio stream: %s\n", pa_strerror(err));
        return -1;
    }
    p->pa_rate = rate;
    return 0;
}

static int bytes_per_sample_out(const AlPort *p)
{
    if (p->cfg.sampfmt == AL_SAMPFMT_TWOSCOMP && p->cfg.width == 1) return 1;
    if (p->cfg.sampfmt == AL_SAMPFMT_TWOSCOMP && p->cfg.width == 2) return 2;
    return 4;
}

static uint32_t be32(const uint8_t *s) { return (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3]; }

static void b_ALwritesamps(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlPort *p = port_from(ARG(0));
    uint32_t n = ARG(2);
    int in_w, out_w;
    size_t in_bytes;
    const uint8_t *src;
    uint8_t *dst;
    uint32_t i;
    if (!p || !p->output || !n) { RET(p ? 0 : -1); return; }
    params_init();
    if (pa_start(p)) { RET(-1); return; }
    if (p->cfg.sampfmt == AL_SAMPFMT_TWOSCOMP)
        in_w = p->cfg.width == 1 ? 1 : p->cfg.width == 2 ? 2 : 4;
    else
        in_w = p->cfg.sampfmt == AL_SAMPFMT_DOUBLE ? 8 : 4;
    out_w = bytes_per_sample_out(p);
    in_bytes = (size_t)n * (size_t)in_w;
    src = (const uint8_t *)irix_guest_ptr(rt, ARG(1), in_bytes);
    if (!src) { RET(-1); return; }
    dst = (uint8_t *)malloc((size_t)n * (size_t)out_w);
    if (!dst) { RET(-1); return; }
    for (i = 0; i < n; ++i) {
        const uint8_t *s = src + (size_t)i * (size_t)in_w;
        uint8_t *d = dst + (size_t)i * (size_t)out_w;
        if (p->cfg.sampfmt == AL_SAMPFMT_TWOSCOMP) {
            if (in_w == 1) {
                d[0] = (uint8_t)(s[0] + 128);
            } else if (in_w == 2) {
                d[0] = s[1]; d[1] = s[0];
            } else {
                /* 24-bit samples sit right-aligned in a 32-bit word */
                int32_t v = (int32_t)(be32(s) << 8);
                memcpy(d, &v, 4);
            }
        } else {
            float f;
            if (in_w == 4) {
                uint32_t u = be32(s);
                memcpy(&f, &u, 4);
            } else {
                uint64_t u = (uint64_t)be32(s) << 32 | be32(s + 4);
                double dv;
                memcpy(&dv, &u, 8);
                f = (float)dv;
            }
            memcpy(d, &f, 4);
        }
    }
    if (pa_simple_write(p->pa, dst, (size_t)n * (size_t)out_w, 0) < 0) {
        free(dst);
        RET(-1);
        return;
    }
    free(dst);
    RET(0);
}

static void b_ALreadsamps(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlPort *p = port_from(ARG(0));
    uint32_t n = ARG(2);
    int w = p ? (p->cfg.width == 1 ? 1 : p->cfg.width == 2 ? 2 : 4) : 2;
    void *dst;
    if (!p || p->output) { RET(-1); return; }
    dst = irix_guest_ptr(rt, ARG(1), (size_t)n * (size_t)w);
    if (!dst) { RET(-1); return; }
    memset(dst, 0, (size_t)n * (size_t)w);
    {
        int ch = p->cfg.channels > 0 ? p->cfg.channels : 2;
        int rate = g_param[AL_INPUT_RATE] > 0 ? g_param[AL_INPUT_RATE] : 44100;
        useconds_t us = (useconds_t)((uint64_t)n * 1000000u / ((uint64_t)rate * (uint64_t)ch));
        usleep(us);
    }
    RET(0);
}

static int port_filled(AlPort *p)
{
    pa_usec_t us;
    int ch, rate;
    if (!p->pa) return 0;
    us = pa_simple_get_latency(p->pa, 0);
    ch = p->cfg.channels > 0 ? p->cfg.channels : 2;
    rate = p->pa_rate > 0 ? p->pa_rate : 44100;
    return (int)((uint64_t)us * (uint64_t)rate * (uint64_t)ch / 1000000u);
}

static void b_ALgetfilled(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlPort *p = port_from(ARG(0));
    (void)rt;
    RET(p ? port_filled(p) : -1);
}

static void b_ALgetfillable(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlPort *p = port_from(ARG(0));
    int free_samples;
    (void)rt;
    if (!p) { RET(-1); return; }
    free_samples = p->cfg.queuesize - port_filled(p);
    RET(free_samples > 0 ? free_samples : 0);
}

static void b_ALgetfd(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlPort *p = port_from(ARG(0));
    (void)rt;
    RET(p ? p->fd : -1);
}

static void b_ALseterrorhandler(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    RET(0);
}

static void b_ALsetparams(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t buf = ARG(1);
    uint32_t n = ARG(2), i;
    params_init();
    for (i = 0; i + 1 < n; i += 2) {
        int32_t id = (int32_t)irix_guest_read32be(rt, buf + i * 4u);
        int32_t val = (int32_t)irix_guest_read32be(rt, buf + (i + 1u) * 4u);
        if (id >= 0 && id < AL_NPARAMS) g_param[id] = val;
    }
    RET(0);
}

static void b_ALgetparams(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t buf = ARG(1);
    uint32_t n = ARG(2), i;
    params_init();
    for (i = 0; i + 1 < n; i += 2) {
        int32_t id = (int32_t)irix_guest_read32be(rt, buf + i * 4u);
        int32_t val = (id >= 0 && id < AL_NPARAMS) ? g_param[id] : 0;
        irix_guest_write32be(rt, buf + (i + 1u) * 4u, (uint32_t)val);
    }
    RET(0);
}

static void b_ALgetdefault(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int32_t id = (int32_t)ARG(1);
    (void)rt;
    params_init();
    RET((id >= 0 && id < AL_NPARAMS) ? g_param[id] : 0);
}

static void b_ALgetminmax(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int32_t id = (int32_t)ARG(1);
    int32_t lo = 0, hi = 255;
    if (id == AL_OUTPUT_RATE || id == AL_INPUT_RATE) { lo = 4000; hi = 48000; }
    else if (id == AL_INPUT_COUNT || id == AL_OUTPUT_COUNT || id == AL_CHANNEL_MODE) { lo = 1; hi = 4; }
    else if (id == AL_SPEAKER_MUTE_CTL || id == AL_MONITOR_CTL) { lo = 0; hi = 1; }
    if (ARG(2)) irix_guest_write32be(rt, ARG(2), (uint32_t)lo);
    if (ARG(3)) irix_guest_write32be(rt, ARG(3), (uint32_t)hi);
    RET(0);
}

typedef struct { const char *name; IrixGuestFunction fn; } AlEntry;

static const AlEntry g_entries[] = {
    { "ALnewconfig", b_ALnewconfig }, { "ALfreeconfig", b_ALfreeconfig },
    { "ALsetwidth", b_ALsetwidth }, { "ALgetwidth", b_ALgetwidth },
    { "ALsetsampfmt", b_ALsetsampfmt }, { "ALgetsampfmt", b_ALgetsampfmt },
    { "ALsetchannels", b_ALsetchannels }, { "ALgetchannels", b_ALgetchannels },
    { "ALsetqueuesize", b_ALsetqueuesize }, { "ALgetqueuesize", b_ALgetqueuesize },
    { "ALopenport", b_ALopenport }, { "ALcloseport", b_ALcloseport },
    { "ALsetconfig", b_ALsetconfig }, { "ALgetconfig", b_ALgetconfig },
    { "ALwritesamps", b_ALwritesamps }, { "ALreadsamps", b_ALreadsamps },
    { "ALgetfilled", b_ALgetfilled }, { "ALgetfillable", b_ALgetfillable },
    { "ALgetfd", b_ALgetfd }, { "ALseterrorhandler", b_ALseterrorhandler },
    { "ALsetparams", b_ALsetparams }, { "ALgetparams", b_ALgetparams },
    { "ALgetdefault", b_ALgetdefault }, { "ALgetminmax", b_ALgetminmax },
};

void irix_al_bridge_init(IrixGuestRuntime *rt)
{
    (void)rt;
    params_init();
}

IrixGuestFunction irix_al_bridge_lookup(const char *name)
{
    size_t i;
    if (getenv("IRIX_AL_BRIDGE") && !strcmp(getenv("IRIX_AL_BRIDGE"), "0")) return 0;
    for (i = 0; i < sizeof g_entries / sizeof g_entries[0]; ++i)
        if (!strcmp(name, g_entries[i].name)) return g_entries[i].fn;
    return 0;
}
