#include "IrixRuntimeLibcBridge.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static IrixGuestRuntime *g_rt;
static IrixGuestAddr g_strtok_state;
static IrixGuestAddr g_setcat_name;
static IrixGuestAddr g_locale_name;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_cstr(rt, addr, 65536u);
}

static void ret_i32(IrixGuestCpu *cpu, int v)
{
    cpu->gpr[2] = (uint64_t)(int64_t)(int32_t)v;
}

static IrixGuestAddr guest_tokenize(IrixGuestRuntime *rt, IrixGuestAddr start_addr,
                                    const char *delim, IrixGuestAddr *state)
{
    char *s;
    size_t skip, take;
    IrixGuestAddr token;

    if (!delim || !state) return 0;
    if (start_addr) *state = start_addr;
    if (!*state) return 0;

    s = (char *)irix_guest_cstr(rt, *state, 65536u);
    if (!s) return 0;

    skip = strspn(s, delim);
    *state += (IrixGuestAddr)skip;
    s += skip;
    if (!*s) {
        *state = 0;
        return 0;
    }

    token = *state;
    take = strcspn(s, delim);
    if (s[take]) {
        s[take] = '\0';
        *state += (IrixGuestAddr)take + 1u;
    } else {
        *state = 0;
    }
    return token;
}

static void bridge_bzero(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    uint32_t n = (uint32_t)cpu->gpr[5];
    void *dst = n ? irix_guest_ptr(rt, dst_addr, n) : 0;
    if (dst && n) memset(dst, 0, n);
    cpu->gpr[2] = 0;
}

static void bridge_gettext(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr msgid = (uint32_t)cpu->gpr[4];
    cpu->gpr[2] = guest_cstr(rt, msgid) ? msgid : 0;
}

/*
 * System V / IRIX message catalog compatibility.
 *
 * These APIs return char pointers. Never return a host libc pointer here:
 * translated code can only dereference guest addresses.
 */
static void bridge_setcat(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr name = (uint32_t)cpu->gpr[4];

    if (name && !guest_cstr(rt, name)) {
        cpu->gpr[2] = 0;
        return;
    }
    g_setcat_name = name;
    cpu->gpr[2] = name;
}

static void bridge_gettxt(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr msgid = (uint32_t)cpu->gpr[4];
    IrixGuestAddr fallback = (uint32_t)cpu->gpr[5];

    if (msgid && !guest_cstr(rt, msgid)) {
        cpu->gpr[2] = 0;
        return;
    }

    /* Catalog loading comes later; preserve gettxt() fallback semantics. */
    cpu->gpr[2] = guest_cstr(rt, fallback) ? fallback : 0;
}

static void bridge_setlocale(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr locale = (uint32_t)cpu->gpr[5];

    (void)cpu->gpr[4]; /* locale category */
    if (!locale) {
        cpu->gpr[2] = g_locale_name;
        return;
    }
    if (!guest_cstr(rt, locale)) {
        cpu->gpr[2] = 0;
        return;
    }

    /*
     * Accept the guest request without exposing a host locale pointer.
     * Remember the guest-owned string for later setlocale(category, NULL).
     */
    g_locale_name = locale;
    cpu->gpr[2] = locale;
}

static void bridge_strcasecmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    int r = (a && b) ? strcasecmp(a, b) : (a ? 1 : (b ? -1 : 0));
    ret_i32(cpu, r);
}

static void bridge_strncasecmp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    size_t n = (size_t)(uint32_t)cpu->gpr[6];
    int r = (a && b) ? strncasecmp(a, b, n) : (a ? 1 : (b ? -1 : 0));
    ret_i32(cpu, r);
}

static void bridge_strcoll(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *a = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *b = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    int r = (a && b) ? strcoll(a, b) : (a ? 1 : (b ? -1 : 0));
    ret_i32(cpu, r);
}

static void bridge_strcspn(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    const char *reject = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    cpu->gpr[2] = (s && reject) ? (uint32_t)strcspn(s, reject) : 0;
}

static void bridge_strpbrk(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr base = (uint32_t)cpu->gpr[4];
    const char *s = guest_cstr(rt, base);
    const char *accept = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    const char *p = (s && accept) ? strpbrk(s, accept) : 0;
    cpu->gpr[2] = p ? base + (IrixGuestAddr)(p - s) : 0;
}

static void bridge_strtok(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr start = (uint32_t)cpu->gpr[4];
    const char *delim = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    cpu->gpr[2] = guest_tokenize(rt, start, delim, &g_strtok_state);
}

static void bridge_strtok_r(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr start = (uint32_t)cpu->gpr[4];
    const char *delim = guest_cstr(rt, (uint32_t)cpu->gpr[5]);
    IrixGuestAddr savep = (uint32_t)cpu->gpr[6];
    IrixGuestAddr state = 0;
    IrixGuestAddr out;

    if (!savep || !delim) { cpu->gpr[2] = 0; return; }
    if (!start) state = irix_guest_read32be(rt, savep);
    out = guest_tokenize(rt, start, delim, &state);
    irix_guest_write32be(rt, savep, state);
    cpu->gpr[2] = out;
}

static void bridge_gettimeofday(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    struct timeval tv;
    struct timezone tz;
    IrixGuestAddr tvp = (uint32_t)cpu->gpr[4];
    IrixGuestAddr tzp = (uint32_t)cpu->gpr[5];
    int rc = gettimeofday(&tv, tzp ? &tz : 0);

    if (rc == 0 && tvp) {
        irix_guest_write32be(rt, tvp + 0u, (uint32_t)tv.tv_sec);
        irix_guest_write32be(rt, tvp + 4u, (uint32_t)tv.tv_usec);
    }
    if (rc == 0 && tzp) {
        irix_guest_write32be(rt, tzp + 0u, (uint32_t)tz.tz_minuteswest);
        irix_guest_write32be(rt, tzp + 4u, (uint32_t)tz.tz_dsttime);
    }
    ret_i32(cpu, rc);
}

static void bridge_getdomainname(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst_addr = (uint32_t)cpu->gpr[4];
    uint32_t len = (uint32_t)cpu->gpr[5];
    char *dst = len ? (char *)irix_guest_ptr(rt, dst_addr, len) : 0;
    int rc = (dst && len) ? getdomainname(dst, (size_t)len) : -1;
    if (rc == 0 && len) dst[len - 1u] = '\0';
    ret_i32(cpu, rc);
}

static void bridge_getgroups(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int count = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr out = (uint32_t)cpu->gpr[5];
    gid_t *groups = 0;
    int rc, i;

    if (count > 0) {
        groups = (gid_t *)calloc((size_t)count, sizeof(*groups));
        if (!groups) { ret_i32(cpu, -1); return; }
    }
    rc = getgroups(count, groups);
    if (rc >= 0 && out && groups) {
        for (i = 0; i < rc; ++i)
            irix_guest_write32be(rt, out + (IrixGuestAddr)i * 4u, (uint32_t)groups[i]);
    }
    free(groups);
    ret_i32(cpu, rc);
}

static void bridge_getdtablehi(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    long n;
    (void)rt;
#ifdef _SC_OPEN_MAX
    n = sysconf(_SC_OPEN_MAX);
#else
    n = 256;
#endif
    if (n < 0) n = 256;
    if (n > INT32_MAX) n = INT32_MAX;
    cpu->gpr[2] = (uint32_t)n;
}

static void bridge_setpgrp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    ret_i32(cpu, setpgrp());
}

static uint32_t rpc_word(const unsigned char *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return ntohl(v);
}

static void bridge_pmap_getport(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr sa_addr = (uint32_t)cpu->gpr[4];
    uint32_t prog = (uint32_t)cpu->gpr[5];
    uint32_t vers = (uint32_t)cpu->gpr[6];
    uint32_t proto = (uint32_t)cpu->gpr[7];
    struct sockaddr_in dst;
    struct pollfd pfd;
    uint32_t req[14];
    unsigned char reply[256];
    uint32_t xid;
    ssize_t n;
    size_t off;
    int fd;

    cpu->gpr[2] = 0;
    if (!sa_addr || !irix_guest_ptr(rt, sa_addr, 16u)) return;

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(111u);
    dst.sin_addr.s_addr = htonl(irix_guest_read32be(rt, sa_addr + 4u));

    xid = ((uint32_t)getpid() << 16) ^ (uint32_t)cpu->pc ^ prog ^ vers ^ proto;
    req[0]  = htonl(xid);
    req[1]  = htonl(0u);
    req[2]  = htonl(2u);
    req[3]  = htonl(100000u);
    req[4]  = htonl(2u);
    req[5]  = htonl(3u);
    req[6]  = htonl(0u);
    req[7]  = htonl(0u);
    req[8]  = htonl(0u);
    req[9]  = htonl(0u);
    req[10] = htonl(prog);
    req[11] = htonl(vers);
    req[12] = htonl(proto);
    req[13] = htonl(0u);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    if (sendto(fd, req, sizeof(req), 0, (struct sockaddr *)&dst, sizeof(dst)) != (ssize_t)sizeof(req)) {
        close(fd);
        return;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 300) <= 0 || !(pfd.revents & POLLIN)) {
        close(fd);
        return;
    }

    n = recv(fd, reply, sizeof(reply), 0);
    close(fd);
    if (n < 28) return;
    if (rpc_word(reply + 0) != xid || rpc_word(reply + 4) != 1u || rpc_word(reply + 8) != 0u)
        return;

    off = 12u;
    if (off + 8u > (size_t)n) return;
    off += 4u;
    {
        uint32_t verf_len = rpc_word(reply + off);
        off += 4u;
        off += (size_t)((verf_len + 3u) & ~3u);
    }
    if (off + 8u > (size_t)n) return;
    if (rpc_word(reply + off) != 0u) return;
    off += 4u;
    cpu->gpr[2] = (uint32_t)(rpc_word(reply + off) & 0xffffu);
}

static void dump_vec_words(IrixGuestRuntime *rt, const char *label, IrixGuestAddr addr)
{
    if (addr && irix_guest_ptr(rt, addr, 20u)) {
        fprintf(stderr, " %s=%08x,%08x,%08x,%08x,%08x",
                label,
                irix_guest_read32be(rt, addr + 0u),
                irix_guest_read32be(rt, addr + 4u),
                irix_guest_read32be(rt, addr + 8u),
                irix_guest_read32be(rt, addr + 12u),
                irix_guest_read32be(rt, addr + 16u));
    }
}

static void bridge___vec_delete2(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    fprintf(stderr,
            "VEC_DELETE2 ABI a0-a7=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
            (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
            (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
            (uint32_t)cpu->gpr[8], (uint32_t)cpu->gpr[9],
            (uint32_t)cpu->gpr[10], (uint32_t)cpu->gpr[11]);
    dump_vec_words(rt, "a0_words", (uint32_t)cpu->gpr[4]);
    dump_vec_words(rt, "a3_words", (uint32_t)cpu->gpr[7]);
    dump_vec_words(rt, "a4_words", (uint32_t)cpu->gpr[8]);
    dump_vec_words(rt, "a5_words", (uint32_t)cpu->gpr[9]);
    fputc('\n', stderr);
    irix_guest_trap(rt, cpu, 0xeeee2102u);
}

void irix_runtime_libc_bridge_init(IrixGuestRuntime *rt)
{
    g_rt = rt;
    g_strtok_state = 0;
    g_setcat_name = 0;
    g_locale_name = 0;
    (void)g_rt;
}

IrixGuestFunction irix_runtime_libc_bridge_lookup(const char *name)
{
    if (!name) return 0;
    if (!strcmp(name, "__vec_delete2")) return bridge___vec_delete2;
#define MAP(n) if (!strcmp(name, #n)) return bridge_##n
    MAP(bzero);
    MAP(gettext); MAP(setcat); MAP(gettxt); MAP(setlocale);
    MAP(strcasecmp); MAP(strncasecmp); MAP(strcoll); MAP(strcspn); MAP(strpbrk);
    MAP(strtok); MAP(strtok_r);
    MAP(gettimeofday); MAP(getdomainname); MAP(getgroups); MAP(getdtablehi); MAP(setpgrp);
    MAP(pmap_getport);
#undef MAP
    return 0;
}
