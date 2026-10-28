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
#ifdef IRIX_AL_ALSA
#include <alsa/asoundlib.h>
#else
#include <pulse/error.h>
#include <pulse/simple.h>
#endif
#ifdef IRIX_HAVE_AUDIOFILE
#include <audiofile.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RET(v) do { cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)(v); if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "%s(%x,%x,%x) -> %d\n", __func__, ARG(0), ARG(1), ARG(2), (int)(int32_t)cpu->gpr[2]); } while (0)
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

/* ---- audio output: PulseAudio by default, ALSA when built with IRIX_AL_ALSA ---- */
typedef struct AlOut AlOut;
enum { AL_OUT_U8, AL_OUT_S16, AL_OUT_S32, AL_OUT_F32 };

#ifdef IRIX_AL_ALSA
/* Plays straight to ALSA (IRIX_AL_ALSA_DEVICE, default "default"), for hosts
 * without a PulseAudio/PipeWire server. */
struct AlOut { snd_pcm_t *pcm; unsigned rate; unsigned frame; };

static AlOut *al_out_open(unsigned rate, unsigned ch, int fmt, const char *name)
{
    static const snd_pcm_format_t map[] = {
        SND_PCM_FORMAT_U8, SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_S32_LE, SND_PCM_FORMAT_FLOAT_LE };
    static const unsigned width[] = { 1, 2, 4, 4 };
    const char *dev = getenv("IRIX_AL_ALSA_DEVICE");
    snd_pcm_t *pcm = 0;
    AlOut *o;
    int err;
    (void)name;
    if (!dev || !dev[0]) dev = "default";
    err = snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "IRIX AL: cannot open ALSA device %s: %s\n", dev, snd_strerror(err));
        return 0;
    }
    err = snd_pcm_set_params(pcm, map[fmt], SND_PCM_ACCESS_RW_INTERLEAVED, ch, rate, 1, 100000);
    if (err < 0) {
        fprintf(stderr, "IRIX AL: ALSA %s rejects %u Hz x %u: %s\n", dev, rate, ch, snd_strerror(err));
        snd_pcm_close(pcm);
        return 0;
    }
    o = calloc(1, sizeof *o);
    if (!o) { snd_pcm_close(pcm); return 0; }
    o->pcm = pcm;
    o->rate = rate;
    o->frame = ch * width[fmt];
    return o;
}

static int al_out_write(AlOut *o, const void *buf, size_t bytes)
{
    const uint8_t *p = buf;
    snd_pcm_uframes_t left = bytes / o->frame;
    while (left) {
        snd_pcm_sframes_t n = snd_pcm_writei(o->pcm, p, left);
        if (n < 0) {
            n = snd_pcm_recover(o->pcm, (int)n, 1);     /* underrun, suspend */
            if (n < 0) return -1;
            continue;
        }
        p += (size_t)n * o->frame;
        left -= (snd_pcm_uframes_t)n;
    }
    return 0;
}

static uint64_t al_out_latency_us(AlOut *o)
{
    snd_pcm_sframes_t d = 0;
    if (snd_pcm_delay(o->pcm, &d) < 0 || d < 0) return 0;
    return (uint64_t)d * 1000000u / (o->rate ? o->rate : 44100u);
}

static void al_out_close(AlOut *o)
{
    snd_pcm_drain(o->pcm);
    snd_pcm_close(o->pcm);
    free(o);
}
#else
struct AlOut { pa_simple *pa; };

static AlOut *al_out_open(unsigned rate, unsigned ch, int fmt, const char *name)
{
    pa_sample_spec ss;
    pa_buffer_attr attr;
    AlOut *o;
    int err = 0;
    memset(&ss, 0, sizeof ss);
    ss.format = fmt == AL_OUT_U8 ? PA_SAMPLE_U8 : fmt == AL_OUT_S16 ? PA_SAMPLE_S16LE
              : fmt == AL_OUT_S32 ? PA_SAMPLE_S32LE : PA_SAMPLE_FLOAT32LE;
    ss.rate = rate;
    ss.channels = (uint8_t)ch;
    attr.maxlength = (uint32_t)-1;
    attr.tlength = pa_usec_to_bytes(100000, &ss);
    attr.prebuf = (uint32_t)-1;
    attr.minreq = (uint32_t)-1;
    attr.fragsize = (uint32_t)-1;
    o = calloc(1, sizeof *o);
    if (!o) return 0;
    o->pa = pa_simple_new(0, "IRIX", PA_STREAM_PLAYBACK, 0, name, &ss, 0, &attr, &err);
    if (!o->pa) {
        fprintf(stderr, "IRIX AL: cannot open PulseAudio stream: %s\n", pa_strerror(err));
        free(o);
        return 0;
    }
    return o;
}

static int al_out_write(AlOut *o, const void *buf, size_t bytes)
{
    return pa_simple_write(o->pa, buf, bytes, 0) < 0 ? -1 : 0;
}

static uint64_t al_out_latency_us(AlOut *o)
{
    return (uint64_t)pa_simple_get_latency(o->pa, 0);
}

static void al_out_close(AlOut *o)
{
    pa_simple_drain(o->pa, 0);
    pa_simple_free(o->pa);
    free(o);
}
#endif

typedef struct {
    int used;
    int output;
    AlConfig cfg;
    AlOut *pa;
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
    if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "AL ALnewconfig a0=%x a1=%x a2=%x a3=%x\n", ARG(0), ARG(1), ARG(2), ARG(3));
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
    if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "AL ALopenport a0=%x a1=%x a2=%x a3=%x\n", ARG(0), ARG(1), ARG(2), ARG(3));
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
    al_out_close(p->pa);
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
    if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "AL ALsetconfig a0=%x a1=%x a2=%x a3=%x\n", ARG(0), ARG(1), ARG(2), ARG(3));
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
    int ch = p->cfg.channels > 0 ? p->cfg.channels : 2;
    int rate = g_param[AL_OUTPUT_RATE] > 0 ? g_param[AL_OUTPUT_RATE] : 44100;
    int fmt;
    if (p->pa && p->pa_rate == rate) return 0;
    pa_close(p);
    fmt = (p->cfg.sampfmt == AL_SAMPFMT_TWOSCOMP && p->cfg.width == 1) ? AL_OUT_U8
        : (p->cfg.sampfmt == AL_SAMPFMT_TWOSCOMP && p->cfg.width == 2) ? AL_OUT_S16
        : (p->cfg.sampfmt == AL_SAMPFMT_TWOSCOMP) ? AL_OUT_S32
        : AL_OUT_F32;
    p->pa = al_out_open((unsigned)rate, (unsigned)ch, fmt, p->name);
    if (!p->pa) return -1;
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
    if (al_out_write(p->pa, dst, (size_t)n * (size_t)out_w) < 0) {
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
    uint64_t us;
    int ch, rate;
    if (!p->pa) return 0;
    us = al_out_latency_us(p->pa);
    ch = p->cfg.channels > 0 ? p->cfg.channels : 2;
    rate = p->pa_rate > 0 ? p->pa_rate : 44100;
    return (int)((uint64_t)us * (uint64_t)rate * (uint64_t)ch / 1000000u);
}

static void b_ALgetfilled(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "AL ALgetfilled a0=%x a1=%x a2=%x a3=%x\n", ARG(0), ARG(1), ARG(2), ARG(3));
    AlPort *p = port_from(ARG(0));
    (void)rt;
    RET(p ? port_filled(p) : -1);
}

static void b_ALgetfillable(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "AL ALgetfillable a0=%x a1=%x a2=%x a3=%x\n", ARG(0), ARG(1), ARG(2), ARG(3));
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
    if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "AL ALsetparams a0=%x a1=%x a2=%x a3=%x\n", ARG(0), ARG(1), ARG(2), ARG(3));
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
    if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "AL ALgetparams a0=%x a1=%x a2=%x a3=%x\n", ARG(0), ARG(1), ARG(2), ARG(3));
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
    if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "AL ALgetdefault a0=%x a1=%x a2=%x a3=%x\n", ARG(0), ARG(1), ARG(2), ARG(3));
    int32_t id = (int32_t)ARG(1);
    (void)rt;
    params_init();
    RET((id >= 0 && id < AL_NPARAMS) ? g_param[id] : 0);
}

static void b_ALgetminmax(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "AL ALgetminmax a0=%x a1=%x a2=%x a3=%x\n", ARG(0), ARG(1), ARG(2), ARG(3));
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

#ifdef IRIX_HAVE_AUDIOFILE
/*
 * SGI Audio File Library (af*) on the host libaudiofile, which is API
 * compatible with it.  Handles are small guest-visible ids; arrays and
 * doubles cross the boundary in guest (big-endian) form.  Only the reading
 * path is bridged; the rest stays with the translated library.
 */
#define AF_BASE 0x7b100000u
#define AF_MAX 32
static AFfilehandle g_af[AF_MAX];

static AFfilehandle af_from(uint32_t h)
{
    uint32_t i;
    if (h < AF_BASE || (h - AF_BASE) % HANDLE_STRIDE) return 0;
    i = (h - AF_BASE) / HANDLE_STRIDE;
    return i < AF_MAX ? g_af[i] : 0;
}

static uint32_t af_register(AFfilehandle f)
{
    uint32_t i;
    if (!f) return 0;
    for (i = 0; i < AF_MAX; ++i)
        if (!g_af[i]) { g_af[i] = f; return AF_BASE + i * HANDLE_STRIDE; }
    afCloseFile(f);
    return 0;
}

static void af_put_ints(IrixGuestRuntime *rt, uint32_t addr, const int *v, int n)
{
    int i;
    for (i = 0; addr && i < n; ++i) irix_guest_write32be(rt, addr + 4u * (uint32_t)i, (uint32_t)v[i]);
}

static void af_put_double(IrixGuestRuntime *rt, uint32_t addr, double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof u);
    if (addr) irix_guest_write64be(rt, addr, u);
}

#define AFS_BASE 0x7b200000u
#define AFS_MAX 16
static AFfilesetup g_afs[AFS_MAX];
static AFfilesetup afs_from(uint32_t h)
{
    uint32_t i;
    if (h < AFS_BASE || (h - AFS_BASE) % HANDLE_STRIDE) return AF_NULL_FILESETUP;
    i = (h - AFS_BASE) / HANDLE_STRIDE;
    return i < AFS_MAX ? g_afs[i] : AF_NULL_FILESETUP;
}
#define AFSET() AFfilesetup st = afs_from(ARG(0))
static int af_read_ints(IrixGuestRuntime *rt, uint32_t addr, int *out, int n)
{
    int i;
    if (!addr || n < 0 || n > 256) return 0;
    for (i = 0; i < n; ++i) out[i] = (int)irix_guest_read32be(rt, addr + 4u * (uint32_t)i);
    return n;
}
#define AFH() AFfilehandle f = af_from(ARG(0))
static void b_afSetErrorHandler(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; RET(0); }

static void b_afOpenFile(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    char name[1024], mode[16];
    guest_string(rt, ARG(0), name, sizeof name);
    guest_string(rt, ARG(1), mode, sizeof mode);
    RET(af_register(afOpenFile(name, mode, afs_from(ARG(2)))));
}
static void b_afOpenFD(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    char mode[16];
    guest_string(rt, ARG(1), mode, sizeof mode);
    RET(af_register(afOpenFD((int)ARG(0), mode, afs_from(ARG(2)))));
}
static void b_afOpenNamedFD(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    char mode[16], name[1024];
    guest_string(rt, ARG(1), mode, sizeof mode);
    guest_string(rt, ARG(3), name, sizeof name);
    RET(af_register(afOpenNamedFD((int)ARG(0), mode, afs_from(ARG(2)), name)));
}
static void b_afIdentifyFD(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; RET(afIdentifyFD((int)ARG(0))); }
static void b_afIdentifyNamedFD(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    char name[1024]; int impl = 0, r;
    guest_string(rt, ARG(1), name, sizeof name);
    r = afIdentifyNamedFD((int)ARG(0), name, &impl);
    if (ARG(2)) irix_guest_write32be(rt, ARG(2), (uint32_t)impl);
    RET(r);
}
static void b_afCloseFile(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); uint32_t i = (ARG(0) - AF_BASE) / HANDLE_STRIDE; (void)rt;
    if (!f) { RET(-1); return; }
    g_af[i] = 0; RET(afCloseFile(f));
}
static void b_afGetFileFormat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); int ver = 0, r = f ? afGetFileFormat(f, &ver) : -1;
    if (ARG(1)) irix_guest_write32be(rt, ARG(1), (uint32_t)ver);
    RET(r);
}
static void b_afGetTrackIDs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); int ids[64] = {0}, n = f ? afGetTrackIDs(f, ids) : -1;
    if (n > 64) n = 64;
    if (n > 0) af_put_ints(rt, ARG(1), ids, n);
    RET(n);
}
static void b_afGetInstIDs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); int ids[256] = {0}, n = f ? afGetInstIDs(f, ids) : -1;
    if (n > 256) n = 256;
    if (n > 0) af_put_ints(rt, ARG(1), ids, n);
    RET(n);
}
static void b_afGetMiscIDs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); int ids[256] = {0}, n = f ? afGetMiscIDs(f, ids) : -1;
    if (n > 256) n = 256;
    if (n > 0) af_put_ints(rt, ARG(1), ids, n);
    RET(n);
}
static void b_afGetMarkIDs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); int ids[256] = {0}, n = f ? afGetMarkIDs(f, (int)ARG(1), ids) : -1;
    if (n > 256) n = 256;
    if (n > 0) af_put_ints(rt, ARG(2), ids, n);
    RET(n);
}
static void b_afGetLoopIDs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); int ids[256] = {0}, n = f ? afGetLoopIDs(f, (int)ARG(1), ids) : -1;
    if (n > 256) n = 256;
    if (n > 0) af_put_ints(rt, ARG(2), ids, n);
    RET(n);
}

#define AF_INT_GETTER(Name) \
static void b_##Name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { AFH(); (void)rt; RET(f ? Name(f, (int)ARG(1)) : -1); }
#define AF_INT_SETTER(Name) \
static void b_##Name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { AFH(); (void)rt; RET(f ? Name(f, (int)ARG(1), (int)ARG(2)) : -1); }
AF_INT_GETTER(afGetByteOrder)
AF_INT_GETTER(afGetVirtualByteOrder)
AF_INT_GETTER(afGetChannels)
AF_INT_GETTER(afGetVirtualChannels)
AF_INT_GETTER(afGetCompression)
AF_INT_SETTER(afSetVirtualByteOrder)
AF_INT_SETTER(afSetVirtualChannels)

static void b_afSetVirtualSampleFormat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFH(); (void)rt; RET(f ? afSetVirtualSampleFormat(f, (int)ARG(1), (int)ARG(2), (int)ARG(3)) : -1); }
static void b_afGetSampleFormat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); int v[2] = {0, 0};
    if (f) afGetSampleFormat(f, (int)ARG(1), &v[0], &v[1]);
    af_put_ints(rt, ARG(2), &v[0], 1); af_put_ints(rt, ARG(3), &v[1], 1);
}
static void b_afGetVirtualSampleFormat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); int v[2] = {0, 0};
    if (f) afGetVirtualSampleFormat(f, (int)ARG(1), &v[0], &v[1]);
    af_put_ints(rt, ARG(2), &v[0], 1); af_put_ints(rt, ARG(3), &v[1], 1);
}
static void b_afGetRate(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFH(); (void)rt; irix_guest_fpr_set_d(cpu, 0, f ? afGetRate(f, (int)ARG(1)) : 0.0); }
static void b_afGetVirtualRate(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFH(); (void)rt; irix_guest_fpr_set_d(cpu, 0, f ? afGetRate(f, (int)ARG(1)) : 0.0); }  /* host lib has no rate converter: virtual rate == file rate */
static void b_afSetVirtualRate(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFH(); (void)rt; RET(f ? 0 : -1); }  /* accepted without conversion; the AL bridge plays at any rate */
static void b_afGetFrameSize(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFH(); (void)rt; irix_guest_fpr_set_s(cpu, 0, f ? afGetFrameSize(f, (int)ARG(1), (int)ARG(2)) : 0.0f); }
static void b_afGetVirtualFrameSize(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFH(); (void)rt; irix_guest_fpr_set_s(cpu, 0, f ? afGetVirtualFrameSize(f, (int)ARG(1), (int)ARG(2)) : 0.0f); }
static void b_afGetFrameCount(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFH(); (void)rt; cpu->gpr[2] = (uint64_t)(int64_t)(f ? afGetFrameCount(f, (int)ARG(1)) : -1); }
static void b_afGetDataOffset(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFH(); (void)rt; cpu->gpr[2] = (uint64_t)(int64_t)(f ? afGetDataOffset(f, (int)ARG(1)) : -1); }
static void b_afSeekFrame(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFH(); (void)rt; cpu->gpr[2] = (uint64_t)(int64_t)(f ? afSeekFrame(f, (int)ARG(1), (AFframecount)(int64_t)cpu->gpr[6]) : -1); }
static void af_get_mapping(IrixGuestRuntime *rt, IrixGuestCpu *cpu, int virt)
{
    AFH(); double m[4] = {1.0, 0.0, 0.0, 0.0};
    if (f) { if (virt) afGetVirtualPCMMapping(f, (int)ARG(1), &m[0], &m[1], &m[2], &m[3]); else afGetPCMMapping(f, (int)ARG(1), &m[0], &m[1], &m[2], &m[3]); }
    af_put_double(rt, ARG(2), m[0]); af_put_double(rt, ARG(3), m[1]);
    af_put_double(rt, ARG(4), m[2]); af_put_double(rt, ARG(5), m[3]);
}
static void b_afGetPCMMapping(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { af_get_mapping(rt, cpu, 0); }
static void b_afGetVirtualPCMMapping(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { af_get_mapping(rt, cpu, 1); }
static void b_afSetVirtualPCMMapping(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); (void)rt;
    RET(f ? afSetVirtualPCMMapping(f, (int)ARG(1), irix_guest_fpr_get_d(cpu, 14), irix_guest_fpr_get_d(cpu, 15),
                                   irix_guest_fpr_get_d(cpu, 16), irix_guest_fpr_get_d(cpu, 17)) : -1);
}
static void b_afReadFrames(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFH(); int track = (int)ARG(1), frames = (int)ARG(3);
    size_t bytes; void *dst;
    if (!f || frames <= 0) { RET(f ? 0 : -1); return; }
    bytes = (size_t)(afGetVirtualFrameSize(f, track, 1) * (float)frames) + 8u;
    dst = irix_guest_ptr(rt, ARG(2), bytes);
    if (!dst) { RET(-1); return; }
    RET(afReadFrames(f, track, dst, frames));
}

static void b_afNewFileSetup(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t i; AFfilesetup st = afNewFileSetup(); (void)rt;
    for (i = 0; st && i < AFS_MAX; ++i)
        if (!g_afs[i]) { g_afs[i] = st; RET(AFS_BASE + i * HANDLE_STRIDE); return; }
    if (st) afFreeFileSetup(st);
    RET(0);
}
static void b_afFreeFileSetup(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFSET(); (void)rt;
    if (st) { g_afs[(ARG(0) - AFS_BASE) / HANDLE_STRIDE] = 0; afFreeFileSetup(st); }
    RET(0);
}
#define AFS_VOID1(Name) static void b_##Name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { AFSET(); (void)rt; if (st) Name(st, (int)ARG(1)); RET(0); }
#define AFS_VOID2(Name) static void b_##Name(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { AFSET(); (void)rt; if (st) Name(st, (int)ARG(1), (int)ARG(2)); RET(0); }
AFS_VOID1(afInitFileFormat)
AFS_VOID2(afInitChannels)
AFS_VOID2(afInitCompression)
AFS_VOID2(afInitByteOrder)
static void b_afInitSampleFormat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); (void)rt; if (st) afInitSampleFormat(st, (int)ARG(1), (int)ARG(2), (int)ARG(3)); RET(0); }
static void b_afInitRate(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); (void)rt; if (st) afInitRate(st, (int)ARG(1), irix_guest_fpr_get_d(cpu, 14)); RET(0); }
static void b_afInitDataOffset(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); (void)rt; if (st) afInitDataOffset(st, (int)ARG(1), (AFfileoffset)(int64_t)cpu->gpr[6]); RET(0); }
static void b_afInitFrameCount(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); (void)rt; if (st) afInitFrameCount(st, (int)ARG(1), (AFframecount)(int64_t)cpu->gpr[6]); RET(0); }
static void b_afInitPCMMapping(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AFSET(); (void)rt;
    if (st) afInitPCMMapping(st, (int)ARG(1), irix_guest_fpr_get_d(cpu, 14), irix_guest_fpr_get_d(cpu, 15),
                             irix_guest_fpr_get_d(cpu, 16), irix_guest_fpr_get_d(cpu, 17));
    RET(0);
}
static void b_afInitTrackIDs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); int ids[256]; int n = af_read_ints(rt, ARG(1), ids, (int)ARG(2)); if (st) afInitTrackIDs(st, ids, n); RET(0); }
static void b_afInitInstIDs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); int ids[256]; int n = af_read_ints(rt, ARG(1), ids, (int)ARG(2)); if (st) afInitInstIDs(st, ids, n); RET(0); }
static void b_afInitMiscIDs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); int ids[256]; int n = af_read_ints(rt, ARG(1), ids, (int)ARG(2)); if (st) afInitMiscIDs(st, ids, n); RET(0); }
static void b_afInitMarkIDs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); int ids[256]; int n = af_read_ints(rt, ARG(2), ids, (int)ARG(3)); if (st) afInitMarkIDs(st, (int)ARG(1), ids, n); RET(0); }
static void b_afInitLoopIDs(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); int ids[256]; int n = af_read_ints(rt, ARG(2), ids, (int)ARG(3)); if (st) afInitLoopIDs(st, (int)ARG(1), ids, n); RET(0); }
static void b_afInitMiscType(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); (void)rt; if (st) afInitMiscType(st, (int)ARG(1), (int)ARG(2)); RET(0); }
static void b_afInitMiscSize(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ AFSET(); (void)rt; if (st) afInitMiscSize(st, (int)ARG(1), (int)ARG(2)); RET(0); }
static void b_af_noop(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; RET(0); }


/* AUpvlist parameter-value lists: kept host-side, values read/written through guest pointers */
#define PV_BASE 0x7b300000u
#define PV_MAX 16
#define PV_ITEMS 32
typedef struct { int used, max; int param[PV_ITEMS], type[PV_ITEMS]; uint64_t val[PV_ITEMS]; } PvList;
static PvList g_pv[PV_MAX];
static PvList *pv_from(uint32_t h)
{
    if (h < PV_BASE || (h - PV_BASE) % HANDLE_STRIDE) return 0;
    h = (h - PV_BASE) / HANDLE_STRIDE;
    return h < PV_MAX && g_pv[h].used ? &g_pv[h] : 0;
}
#define PVI() PvList *pl = pv_from(ARG(0)); int it = (int)ARG(1); int ok = pl && it >= 0 && it < pl->max
static void b_AUpvnew(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t i; (void)rt;
    for (i = 0; i < PV_MAX; ++i)
        if (!g_pv[i].used) {
            memset(&g_pv[i], 0, sizeof g_pv[i]);
            g_pv[i].used = 1; g_pv[i].max = (int)ARG(0) > PV_ITEMS ? PV_ITEMS : (int)ARG(0);
            RET(PV_BASE + i * HANDLE_STRIDE); return;
        }
    RET(0);
}
static void b_AUpvfree(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ PvList *pl = pv_from(ARG(0)); (void)rt; if (pl) pl->used = 0; RET(pl ? 0 : -1); }
static void b_AUpvgetmaxitems(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ PvList *pl = pv_from(ARG(0)); (void)rt; RET(pl ? pl->max : -1); }
static void b_AUpvsetparam(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ PVI(); (void)rt; if (ok) pl->param[it] = (int)ARG(2); RET(ok ? 0 : -1); }
static void b_AUpvsetvaltype(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ PVI(); (void)rt; if (ok) pl->type[it] = (int)ARG(2); RET(ok ? 0 : -1); }
static void b_AUpvgetparam(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ PVI(); if (ok && ARG(2)) irix_guest_write32be(rt, ARG(2), (uint32_t)pl->param[it]); RET(ok ? 0 : -1); }
static void b_AUpvgetvaltype(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ PVI(); if (ok && ARG(2)) irix_guest_write32be(rt, ARG(2), (uint32_t)pl->type[it]); RET(ok ? 0 : -1); }
static void b_AUpvsetval(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    PVI();
    if (ok && ARG(2)) pl->val[it] = pl->type[it] == 2 ? irix_guest_read64be(rt, ARG(2)) : irix_guest_read32be(rt, ARG(2));
    RET(ok ? 0 : -1);
}
static void b_AUpvgetval(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    PVI();
    if (ok && ARG(2)) {
        if (pl->type[it] == 2) irix_guest_write64be(rt, ARG(2), pl->val[it]);
        else irix_guest_write32be(rt, ARG(2), (uint32_t)pl->val[it]);
    }
    RET(ok ? 0 : -1);
}
static void b_afQueryLong(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{ (void)rt; RET((uint32_t)afQueryLong((int)ARG(0), (int)ARG(1), (int)ARG(2), (int)ARG(3), (int)ARG(4))); }
static void b_afQueryPointer(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; RET(0); }
static void b_afGetMarkPosition(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; cpu->gpr[2] = 0; }
static void b_af_fail(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; RET(-1); }

static const AlEntry g_af_entries[] = {
    { "AUpvnew", b_AUpvnew }, { "AUpvfree", b_AUpvfree }, { "AUpvgetmaxitems", b_AUpvgetmaxitems }, { "AUpvsetparam", b_AUpvsetparam },
    { "AUpvsetvaltype", b_AUpvsetvaltype }, { "AUpvgetparam", b_AUpvgetparam }, { "AUpvgetvaltype", b_AUpvgetvaltype },
    { "AUpvsetval", b_AUpvsetval }, { "AUpvgetval", b_AUpvgetval }, { "AUchecklicense", b_af_noop },
    { "afQueryLong", b_afQueryLong }, { "afQueryPointer", b_afQueryPointer }, { "afGetCompressionName", b_af_noop },
    { "afGetCompressionParams", b_af_fail }, { "afGetMarkName", b_af_noop }, { "afGetMarkPosition", b_afGetMarkPosition },
    { "afSetMarkPosition", b_af_noop }, { "afGetInstParamLong", b_af_noop }, { "afSetInstParamLong", b_af_noop },
    { "afGetLoopStart", b_af_noop }, { "afGetLoopEnd", b_af_noop }, { "afGetLoopMode", b_af_noop }, { "afSetLoopStart", b_af_noop },
    { "afSetLoopEnd", b_af_noop }, { "afSetLoopMode", b_af_noop }, { "afGetMiscSize", b_af_noop }, { "afGetMiscType", b_af_noop },
    { "afReadMisc", b_af_fail }, { "afSeekMisc", b_af_fail }, { "afWriteMisc", b_af_fail }, { "afWriteFrames", b_af_fail },
    { "afGetAESChannelData", b_af_fail }, { "afSetAESChannelData", b_af_noop },

    { "afNewFileSetup", b_afNewFileSetup }, { "afFreeFileSetup", b_afFreeFileSetup }, { "afInitFileFormat", b_afInitFileFormat },
    { "afInitChannels", b_afInitChannels }, { "afInitCompression", b_afInitCompression }, { "afInitByteOrder", b_afInitByteOrder },
    { "afInitSampleFormat", b_afInitSampleFormat }, { "afInitRate", b_afInitRate }, { "afInitDataOffset", b_afInitDataOffset },
    { "afInitFrameCount", b_afInitFrameCount }, { "afInitPCMMapping", b_afInitPCMMapping }, { "afInitTrackIDs", b_afInitTrackIDs },
    { "afInitInstIDs", b_afInitInstIDs }, { "afInitMiscIDs", b_afInitMiscIDs }, { "afInitMarkIDs", b_afInitMarkIDs },
    { "afInitLoopIDs", b_afInitLoopIDs }, { "afInitMiscType", b_afInitMiscType }, { "afInitMiscSize", b_afInitMiscSize },
    { "afInitMarkName", b_af_noop }, { "afInitCompressionParams", b_af_noop }, { "afInitAESChannelDataTo", b_af_noop },
    { "afSetChannelMatrix", b_af_noop }, { "afSetTrackPCMMapping", b_af_noop },
    { "afSetErrorHandler", b_afSetErrorHandler }, { "afOpenFile", b_afOpenFile }, { "afOpenFD", b_afOpenFD },
    { "afOpenNamedFD", b_afOpenNamedFD }, { "afIdentifyFD", b_afIdentifyFD }, { "afIdentifyNamedFD", b_afIdentifyNamedFD },
    { "afCloseFile", b_afCloseFile }, { "afGetFileFormat", b_afGetFileFormat }, { "afGetTrackIDs", b_afGetTrackIDs },
    { "afGetInstIDs", b_afGetInstIDs }, { "afGetMiscIDs", b_afGetMiscIDs }, { "afGetMarkIDs", b_afGetMarkIDs },
    { "afGetLoopIDs", b_afGetLoopIDs }, { "afGetByteOrder", b_afGetByteOrder }, { "afGetVirtualByteOrder", b_afGetVirtualByteOrder },
    { "afGetChannels", b_afGetChannels }, { "afGetVirtualChannels", b_afGetVirtualChannels }, { "afGetCompression", b_afGetCompression },
    { "afSetVirtualByteOrder", b_afSetVirtualByteOrder }, { "afSetVirtualChannels", b_afSetVirtualChannels },
    { "afSetVirtualSampleFormat", b_afSetVirtualSampleFormat }, { "afGetSampleFormat", b_afGetSampleFormat },
    { "afGetVirtualSampleFormat", b_afGetVirtualSampleFormat }, { "afGetRate", b_afGetRate },
    { "afGetVirtualRate", b_afGetVirtualRate }, { "afSetVirtualRate", b_afSetVirtualRate },
    { "afGetFrameSize", b_afGetFrameSize }, { "afGetVirtualFrameSize", b_afGetVirtualFrameSize },
    { "afGetFrameCount", b_afGetFrameCount }, { "afGetDataOffset", b_afGetDataOffset }, { "afSeekFrame", b_afSeekFrame },
    { "afGetPCMMapping", b_afGetPCMMapping }, { "afGetVirtualPCMMapping", b_afGetVirtualPCMMapping },
    { "afSetVirtualPCMMapping", b_afSetVirtualPCMMapping }, { "afReadFrames", b_afReadFrames },
};
#endif /* IRIX_HAVE_AUDIOFILE */



/* IRIX 6.3+ lowercase al* API: same objects as the AL* ones, frames instead of samples */
static void b_alWriteFrames(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlPort *p = port_from(ARG(0));
    int ch = p && p->cfg.channels > 0 ? p->cfg.channels : 2;
    cpu->gpr[6] = (uint32_t)(ARG(2) * (uint32_t)ch);
    b_ALwritesamps(rt, cpu);
    if ((int32_t)cpu->gpr[2] >= 0) cpu->gpr[2] = (uint32_t)((int32_t)cpu->gpr[2] / ch);
}
static void b_alGetFilled(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    AlPort *p = port_from(ARG(0));
    int ch = p && p->cfg.channels > 0 ? p->cfg.channels : 2;
    (void)rt;
    RET(p ? port_filled(p) / ch : -1);
}
static void b_alRes(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; RET(1); }
static void b_alGetParams(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t i, n = ARG(2);
    for (i = 0; i < n && i < 64; ++i) irix_guest_write64be(rt, ARG(1) + i * 16u + 8u, 0);
    RET(0);
}
static void b_alZero(IrixGuestRuntime *rt, IrixGuestCpu *cpu) { (void)rt; RET(0); }
static void b_alSetParams(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    uint32_t i, n = ARG(2);
    params_init();
    for (i = 0; i < n && i < 64; ++i) {
        uint32_t param = irix_guest_read32be(rt, ARG(1) + i * 16u);
        uint64_t val = irix_guest_read64be(rt, ARG(1) + i * 16u + 8u);
        if (getenv("IRIX_AL_TRACE")) fprintf(stderr, "alSetParams res=%x param=%x val=%llx\n", ARG(0), param, (unsigned long long)val);
        /* AL_RATE (0x1000000d): alFixed with the integer part in the high word */
        if (param == 0x1000000du && (val >> 32) >= 1000 && (val >> 32) <= 192000)
            g_param[AL_OUTPUT_RATE] = g_param[AL_INPUT_RATE] = (int)(val >> 32);
    }
    RET(0);
}

static const AlEntry g_entries[] = {
    { "alSetWidth", b_ALsetwidth }, { "alGetWidth", b_ALgetwidth }, { "alSetSampFmt", b_ALsetsampfmt }, { "alGetSampFmt", b_ALgetsampfmt },
    { "alGetChannels", b_ALgetchannels }, { "alGetQueueSize", b_ALgetqueuesize }, { "alSetConfig", b_ALsetconfig },
    { "alGetConfig", b_ALgetconfig }, { "alGetFillable", b_ALgetfillable },
    { "alNewConfig", b_ALnewconfig }, { "alFreeConfig", b_ALfreeconfig }, { "alSetChannels", b_ALsetchannels },
    { "alSetQueueSize", b_ALsetqueuesize }, { "alOpenPort", b_ALopenport }, { "alClosePort", b_ALcloseport },
    { "alWriteFrames", b_alWriteFrames }, { "alGetFilled", b_alGetFilled }, { "alGetResourceByName", b_alRes },
    { "alSetParams", b_alSetParams }, { "alGetParams", b_alGetParams }, { "alSetDevice", b_alZero }, { "alSetFillPoint", b_alZero },

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

static const char *const g_af_strong[][2] = {
    {"afGetFrameCount", "AFgetframecnt"},
    {"afReadFrames", "AFreadframes"},
    {"afWriteFrames", "AFwriteframes"},
    {"afTellFrame", "AFtellframe"},
    {"afSeekFrame", "AFseekframe"},
    {"afSyncFile", "AFsyncfile"},
    {"afSetErrorHandler", "AFseterrorhandler"},
    {"afInitFileFormat", "AFinitfilefmt"},
    {"afGetFileFormat", "AFgetfilefmt"},
    {"afInitTrackIDs", "AFinittrackids"},
    {"afGetTrackIDs", "AFgettrackids"},
    {"afInitAESChannelData", "AFinitaeschanneldata"},
    {"afSetAESChannelData", "AFsetaeschanneldata"},
    {"afGetAESChannelData", "AFgetaeschanneldata"},
    {"afInitChannels", "AFinitchannels"},
    {"afGetChannels", "AFgetchannels"},
    {"afInitCompression", "AFinitcompression"},
    {"afInitCompressionParams", "AFinitcompressionparams"},
    {"afGetCompression", "AFgetcompression"},
    {"afGetCompressionParams", "AFgetcompressionparams"},
    {"afGetCompressionName", "AFgetcompressionname"},
    {"afInitRate", "AFinitrate"},
    {"afGetRate", "AFgetrate"},
    {"afInitSampleFormat", "AFinitsampfmt"},
    {"afGetSampleFormat", "AFgetsampfmt"},
    {"afInitInstIDs", "AFinitinstids"},
    {"afGetInstIDs", "AFgetinstids"},
    {"afSetInstParams", "AFsetinstparams"},
    {"afSetInstParamLong", "AFsetinstparamlong"},
    {"afGetInstParams", "AFgetinstparams"},
    {"afGetInstParamLong", "AFgetinstparamlong"},
    {"afInitLoopIDs", "AFinitloopids"},
    {"afGetLoopIDs", "AFgetloopids"},
    {"afSetLoopMode", "AFsetloopmode"},
    {"afGetLoopMode", "AFgetloopmode"},
    {"afSetLoopStart", "AFsetloopstart"},
    {"afGetLoopStart", "AFgetloopstart"},
    {"afSetLoopTrack", "AFsetlooptrack"},
    {"afGetLoopTrack", "AFgetlooptrack"},
    {"afSetLoopEnd", "AFsetloopend"},
    {"afGetLoopEnd", "AFgetloopend"},
    {"afInitMarkIDs", "AFinitmarkids"},
    {"afGetMarkIDs", "AFgetmarkids"},
    {"afInitMarkName", "AFinitmarkname"},
    {"afGetMarkName", "AFgetmarkname"},
    {"afSetMarkPosition", "AFsetmarkpos"},
    {"afGetMarkPosition", "AFgetmarkpos"},
    {"afInitMiscIDs", "AFinitmiscids"},
    {"afGetMiscIDs", "AFgetmiscids"},
    {"afInitMiscSize", "AFinitmiscsize"},
    {"afGetMiscSize", "AFgetmiscsize"},
    {"afInitMiscType", "AFinitmisctype"},
    {"afGetMiscType", "AFgetmisctype"},
    {"afReadMisc", "AFreadmisc"},
    {"afSeekMisc", "AFseekmisc"},
    {"afWriteMisc", "AFwritemisc"},
    {"afGetFD", "AFgetfd"},
    {"afIdentifyFD", "AFidentifyfd"},
    {"afOpenFile", "AFopenfile"},
    {"afOpenFD", "AFopenfd"},
    {"afCloseFile", "AFclosefile"},
    {"afNewFileSetup", "AFnewfilesetup"},
    {"afFreeFileSetup", "AFfreefilesetup"},
};

IrixGuestFunction irix_al_bridge_lookup(const char *name)
{
    size_t i;
    if (getenv("IRIX_AL_BRIDGE") && !strcmp(getenv("IRIX_AL_BRIDGE"), "0")) return 0;
    for (i = 0; i < sizeof g_entries / sizeof g_entries[0]; ++i)
        if (!strcmp(name, g_entries[i].name)) return g_entries[i].fn;
#ifdef IRIX_HAVE_AUDIOFILE
    if (!(getenv("IRIX_AF_BRIDGE") && !strcmp(getenv("IRIX_AF_BRIDGE"), "0")))
        for (i = 0; i < sizeof g_af_entries / sizeof g_af_entries[0]; ++i) {
            const char *n = g_af_entries[i].name;
            if (!strcmp(name, n)) return g_af_entries[i].fn;
            /* the translated functions are recorded under SGI's strong names:
               afOpenFile is the alias of AFopenfile */
            { size_t k;
              for (k = 0; k < sizeof g_af_strong / sizeof g_af_strong[0]; ++k)
                  if (!strcmp(name, g_af_strong[k][1]) && !strcmp(n, g_af_strong[k][0])) return g_af_entries[i].fn; }
            if (name[0] == 'A' && name[1] == 'F' && strlen(name) == strlen(n)) {
                size_t k;
                for (k = 2; n[k] && name[k] == (n[k] >= 'A' && n[k] <= 'Z' ? n[k] + 32 : n[k]); ++k) ;
                if (!n[k]) return g_af_entries[i].fn;
            }
        }
#endif
    return 0;
}
