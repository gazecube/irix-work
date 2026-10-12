#define irix_clib_extra_bridge_lookup irix_clib_extra_bridge_lookup_base
#include "IrixCLibExtraBridge.c"
#undef irix_clib_extra_bridge_lookup

#include <sys/file.h>
#include <unistd.h>

#define IRIX_GUEST_MMAP_BASE 0x74000000u
#define IRIX_GUEST_MMAP_END  0x7f000000u
#define IRIX_GUEST_PAGE_SIZE 0x1000u

static int g_guest_oserror;
static IrixGuestAddr g_guest_mmap_next = IRIX_GUEST_MMAP_BASE;
static unsigned long g_guest_mktemp_counter;

static void scan_write_integer(IrixGuestRuntime *rt, IrixGuestAddr dst,
                               uint64_t value, int length, int is_signed)
{
    (void)is_signed;
    if (!dst) return;
    if (length == -2) {
        irix_guest_write8(rt, dst, (uint8_t)value);
    } else if (length == -1) {
        irix_guest_write16be(rt, dst, (uint16_t)value);
    } else if (length >= 2) {
        irix_guest_write64be(rt, dst, value);
    } else {
        /* IRIX N32 int and long are both 32-bit. */
        irix_guest_write32be(rt, dst, (uint32_t)value);
    }
}

static int scan_in_set(unsigned char c, const char *beg, const char *end,
                       int invert)
{
    const char *p = beg;
    int hit = 0;
    while (p < end) {
        unsigned char lo = (unsigned char)*p++;
        if (p + 1 < end && *p == '-') {
            unsigned char hi;
            ++p;
            hi = (unsigned char)*p++;
            if (lo <= c && c <= hi) hit = 1;
        } else if (c == lo) {
            hit = 1;
        }
    }
    return invert ? !hit : hit;
}

static int scan_guest(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                      IrixGuestAddr input_addr, IrixGuestAddr fmt_addr)
{
    const char *input = guest_cstr(rt, input_addr);
    const char *fmt = guest_cstr(rt, fmt_addr);
    const char *s, *f;
    GuestArgs args;
    int assigned = 0;

    if (!input || !fmt) return EOF;
    s = input;
    f = fmt;
    args.rt = rt;
    args.cpu = cpu;
    args.reg = 6u;
    args.stack = (uint32_t)cpu->gpr[29];

    while (*f) {
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*f)) ++f;
            while (isspace((unsigned char)*s)) ++s;
            continue;
        }
        if (*f != '%') {
            if (*s != *f) break;
            ++s;
            ++f;
            continue;
        }

        ++f;
        if (*f == '%') {
            if (*s != '%') break;
            ++s;
            ++f;
            continue;
        }

        {
            int suppress = 0;
            int width = 0;
            int length = 0;
            char conv;
            IrixGuestAddr dst = 0;

            if (*f == '*') { suppress = 1; ++f; }
            while (isdigit((unsigned char)*f)) {
                if (width < INT_MAX / 10)
                    width = width * 10 + (*f - '0');
                ++f;
            }
            if (*f == 'h') {
                length = -1;
                ++f;
                if (*f == 'h') { length = -2; ++f; }
            } else if (*f == 'l') {
                length = 1;
                ++f;
                if (*f == 'l') { length = 2; ++f; }
            } else if (*f == 'L') {
                length = 2;
                ++f;
            }

            conv = *f;
            if (!conv) break;
            ++f;
            if (!suppress && conv != '%') dst = (uint32_t)next_arg(&args);

            if (conv != 'c' && conv != '[' && conv != 'n')
                while (isspace((unsigned char)*s)) ++s;

            if (conv == 'd' || conv == 'i' || conv == 'u' ||
                conv == 'o' || conv == 'x' || conv == 'X' || conv == 'p') {
                char buf[256];
                size_t n = 0, lim = width > 0 ? (size_t)width : sizeof(buf) - 1u;
                char *endp;
                int base = 10;
                uint64_t value;
                if (lim >= sizeof(buf)) lim = sizeof(buf) - 1u;
                while (s[n] && !isspace((unsigned char)s[n]) && n < lim) {
                    buf[n] = s[n];
                    ++n;
                }
                buf[n] = 0;
                if (!n) break;
                if (conv == 'i') base = 0;
                else if (conv == 'o') base = 8;
                else if (conv == 'x' || conv == 'X' || conv == 'p') base = 16;
                errno = 0;
                if (conv == 'd' || conv == 'i')
                    value = (uint64_t)(int64_t)strtoll(buf, &endp, base);
                else
                    value = (uint64_t)strtoull(buf, &endp, base);
                if (endp == buf) break;
                s += (size_t)(endp - buf);
                if (!suppress) {
                    scan_write_integer(rt, dst, value, conv == 'p' ? 0 : length,
                                       conv == 'd' || conv == 'i');
                    if (rt->trapped) return assigned ? assigned : EOF;
                    ++assigned;
                }
                continue;
            }

            if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
                conv == 'g' || conv == 'G' || conv == 'a' || conv == 'A') {
                char buf[256];
                size_t n = 0, lim = width > 0 ? (size_t)width : sizeof(buf) - 1u;
                char *endp;
                double value;
                if (lim >= sizeof(buf)) lim = sizeof(buf) - 1u;
                while (s[n] && !isspace((unsigned char)s[n]) && n < lim) {
                    buf[n] = s[n];
                    ++n;
                }
                buf[n] = 0;
                if (!n) break;
                value = strtod(buf, &endp);
                if (endp == buf) break;
                s += (size_t)(endp - buf);
                if (!suppress) {
                    if (length >= 1) {
                        union { double d; uint64_t u; } x;
                        x.d = value;
                        irix_guest_write64be(rt, dst, x.u);
                    } else {
                        union { float f; uint32_t u; } x;
                        x.f = (float)value;
                        irix_guest_write32be(rt, dst, x.u);
                    }
                    if (rt->trapped) return assigned ? assigned : EOF;
                    ++assigned;
                }
                continue;
            }

            if (conv == 's') {
                size_t n = 0;
                size_t lim = width > 0 ? (size_t)width : SIZE_MAX;
                while (s[n] && !isspace((unsigned char)s[n]) && n < lim) ++n;
                if (!n) break;
                if (!suppress) {
                    char *out = (char *)irix_guest_ptr(rt, dst, n + 1u);
                    if (!out) return assigned ? assigned : EOF;
                    memcpy(out, s, n);
                    out[n] = 0;
                    ++assigned;
                }
                s += n;
                continue;
            }

            if (conv == 'c') {
                size_t n = width > 0 ? (size_t)width : 1u;
                size_t i;
                for (i = 0; i < n; ++i) if (!s[i]) break;
                if (i != n) break;
                if (!suppress) {
                    char *out = (char *)irix_guest_ptr(rt, dst, n);
                    if (!out) return assigned ? assigned : EOF;
                    memcpy(out, s, n);
                    ++assigned;
                }
                s += n;
                continue;
            }

            if (conv == '[') {
                const char *beg = f;
                const char *end;
                int invert = 0;
                size_t n = 0;
                size_t lim = width > 0 ? (size_t)width : SIZE_MAX;
                if (*beg == '^') { invert = 1; ++beg; }
                end = beg;
                if (*end == ']') ++end;
                while (*end && *end != ']') ++end;
                if (*end != ']') break;
                f = end + 1;
                while (s[n] && n < lim &&
                       scan_in_set((unsigned char)s[n], beg, end, invert)) ++n;
                if (!n) break;
                if (!suppress) {
                    char *out = (char *)irix_guest_ptr(rt, dst, n + 1u);
                    if (!out) return assigned ? assigned : EOF;
                    memcpy(out, s, n);
                    out[n] = 0;
                    ++assigned;
                }
                s += n;
                continue;
            }

            if (conv == 'n') {
                if (!suppress) {
                    uint64_t count = (uint64_t)(s - input);
                    scan_write_integer(rt, dst, count, length, 1);
                    if (rt->trapped) return assigned ? assigned : EOF;
                }
                continue;
            }

            break;
        }
    }

    if (assigned == 0 && *s == 0 && s == input) return EOF;
    return assigned;
}

static void bridge_sscanf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int rc = scan_guest(rt, cpu, (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5]);
    cpu->gpr[2] = (uint64_t)(int64_t)rc;
}

static void bridge_exit(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int status = (int)(int32_t)cpu->gpr[4];
    (void)rt;
    fflush(NULL);
    exit(status);
}

static void bridge_guest_flock(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int fd = (int)(int32_t)cpu->gpr[4];
    int operation = (int)(int32_t)cpu->gpr[5];
    int rc;
    (void)rt;
    errno = 0;
    rc = flock(fd, operation);
    if (rc < 0)
        g_guest_oserror = errno;
    else
        g_guest_oserror = 0;
    cpu->gpr[2] = (uint64_t)(int64_t)rc;
}

static void bridge_guest_mktemp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    char *tmpl = (char *)irix_guest_cstr(rt, addr, 65536u);
    size_t len, xcount, attempt;

    if (!tmpl) {
        g_guest_oserror = EFAULT;
        cpu->gpr[2] = 0;
        return;
    }

    len = strlen(tmpl);
    xcount = 0;
    while (xcount < len && tmpl[len - xcount - 1u] == 'X')
        ++xcount;

    if (xcount < 6u) {
        tmpl[0] = '\0';
        g_guest_oserror = EINVAL;
        cpu->gpr[2] = (uint64_t)(uint32_t)addr;
        return;
    }

    for (attempt = 0; attempt < 10000u; ++attempt) {
        uint64_t value = ((uint64_t)(uint32_t)getpid() << 32) ^
                         (uint64_t)++g_guest_mktemp_counter ^
                         (uint64_t)attempt;
        size_t i;
        struct stat st;

        for (i = 0; i < xcount; ++i) {
            tmpl[len - 1u - i] = alphabet[value % (sizeof(alphabet) - 1u)];
            value = value / (sizeof(alphabet) - 1u) + UINT64_C(0x9e3779b97f4a7c15);
        }

        errno = 0;
        if (lstat(tmpl, &st) != 0 && errno == ENOENT) {
            g_guest_oserror = 0;
            cpu->gpr[2] = (uint64_t)(uint32_t)addr;
            return;
        }
        if (errno != 0 && errno != EEXIST) {
            int err = errno;
            tmpl[0] = '\0';
            g_guest_oserror = err;
            cpu->gpr[2] = (uint64_t)(uint32_t)addr;
            return;
        }
    }

    tmpl[0] = '\0';
    g_guest_oserror = EEXIST;
    cpu->gpr[2] = (uint64_t)(uint32_t)addr;
}

static void bridge_guest_mkstemp(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr addr = (uint32_t)cpu->gpr[4];
    char *guest_template = (char *)irix_guest_cstr(rt, addr, 65536u);
    char host_template[4096];
    const char *resolved;
    size_t length, xcount = 0;
    int fd;

    if (!guest_template) {
        g_guest_oserror = EFAULT;
        cpu->gpr[2] = (uint64_t)(int64_t)-1;
        return;
    }

    length = strlen(guest_template);
    while (xcount < length && guest_template[length - xcount - 1u] == 'X')
        ++xcount;
    if (xcount < 6u) {
        g_guest_oserror = EINVAL;
        cpu->gpr[2] = (uint64_t)(int64_t)-1;
        return;
    }

    resolved = irix_guest_resolve_path(rt, guest_template, host_template,
                                       sizeof(host_template), "mkstemp", 0);
    if (!resolved) {
        g_guest_oserror = EFAULT;
        cpu->gpr[2] = (uint64_t)(int64_t)-1;
        return;
    }

    fd = mkstemp((char *)resolved);
    if (fd >= 0) {
        /* If the guest path was staged under IRIX-TREE, return the generated
           suffix to the guest's original template buffer as POSIX requires. */
        if (resolved != guest_template)
            memcpy(guest_template + length - xcount,
                   resolved + strlen(resolved) - xcount, xcount);
        g_guest_oserror = 0;
    } else {
        g_guest_oserror = errno;
    }
    cpu->gpr[2] = (uint64_t)(int64_t)fd;
}

static uint32_t guest_page_round(uint32_t n)
{
    if (!n || n > UINT32_MAX - (IRIX_GUEST_PAGE_SIZE - 1u)) return 0;
    return (n + IRIX_GUEST_PAGE_SIZE - 1u) & ~(IRIX_GUEST_PAGE_SIZE - 1u);
}

static int guest_range_free(IrixGuestRuntime *rt, IrixGuestAddr base, uint32_t size)
{
    uint64_t end = (uint64_t)base + size;
    size_t i;
    if (!size || end > UINT32_MAX + UINT64_C(1)) return 0;
    for (i = 0; i < rt->memory.count; ++i) {
        IrixGuestSegment *seg = &rt->memory.segments[i];
        uint64_t seg_end = (uint64_t)seg->guest_base + seg->size;
        if ((uint64_t)base < seg_end && end > seg->guest_base)
            return 0;
    }
    return 1;
}

static IrixGuestAddr guest_mmap_alloc(IrixGuestRuntime *rt, uint32_t size)
{
    IrixGuestAddr base = g_guest_mmap_next;
    while ((uint64_t)base + size <= IRIX_GUEST_MMAP_END) {
        if (guest_range_free(rt, base, size)) {
            g_guest_mmap_next = base + size;
            return base;
        }
        base += IRIX_GUEST_PAGE_SIZE;
    }
    return 0;
}

static void guest_drop_segment(IrixGuestRuntime *rt, size_t index)
{
    free(rt->memory.segments[index].host_base);
    if (index + 1u < rt->memory.count)
        memmove(&rt->memory.segments[index], &rt->memory.segments[index + 1u],
                (rt->memory.count - index - 1u) * sizeof(rt->memory.segments[0]));
    --rt->memory.count;
}

static void bridge_guest_mmap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr requested = (uint32_t)cpu->gpr[4];
    uint32_t length = (uint32_t)cpu->gpr[5];
    unsigned prot = (unsigned)(uint32_t)cpu->gpr[6];
    int fd = (int)(int32_t)cpu->gpr[8];
    off_t offset = (off_t)(int64_t)cpu->gpr[9];
    uint32_t map_size = guest_page_round(length);
    IrixGuestAddr base;
    unsigned char *dst;
    size_t done = 0;

    if (!map_size) {
        g_guest_oserror = EINVAL;
        cpu->gpr[2] = UINT32_MAX;
        return;
    }

    if (requested) {
        base = requested & ~(IRIX_GUEST_PAGE_SIZE - 1u);
        if (base != requested || !guest_range_free(rt, base, map_size)) {
            g_guest_oserror = EINVAL;
            cpu->gpr[2] = UINT32_MAX;
            return;
        }
    } else {
        base = guest_mmap_alloc(rt, map_size);
        if (!base) {
            g_guest_oserror = ENOMEM;
            cpu->gpr[2] = UINT32_MAX;
            return;
        }
    }

    if (irix_guest_map(rt, base, map_size, prot) != 0) {
        g_guest_oserror = ENOMEM;
        cpu->gpr[2] = UINT32_MAX;
        return;
    }

    dst = (unsigned char *)irix_guest_ptr(rt, base, map_size);
    if (!dst) {
        g_guest_oserror = EFAULT;
        cpu->gpr[2] = UINT32_MAX;
        return;
    }

    if (fd >= 0) {
        errno = 0;
        while (done < length) {
            ssize_t n = pread(fd, dst + done, (size_t)length - done,
                              offset + (off_t)done);
            if (n > 0) {
                done += (size_t)n;
                continue;
            }
            if (n == 0) break;
            if (errno == EINTR) continue;
            g_guest_oserror = errno ? errno : EIO;
            guest_drop_segment(rt, rt->memory.count - 1u);
            cpu->gpr[2] = UINT32_MAX;
            return;
        }
    }

    g_guest_oserror = 0;
    cpu->gpr[2] = (uint64_t)(uint32_t)base;
}

static void bridge_guest_munmap(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr base = (uint32_t)cpu->gpr[4];
    uint32_t length = (uint32_t)cpu->gpr[5];
    uint32_t map_size = guest_page_round(length);
    size_t i;

    for (i = 0; i < rt->memory.count; ++i) {
        IrixGuestSegment *seg = &rt->memory.segments[i];
        if (seg->guest_base == base && (!map_size || seg->size == map_size)) {
            guest_drop_segment(rt, i);
            g_guest_oserror = 0;
            cpu->gpr[2] = 0;
            return;
        }
    }

    g_guest_oserror = EINVAL;
    cpu->gpr[2] = (uint64_t)(int64_t)-1;
}

static void bridge_guest_setoserror(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int old = g_guest_oserror;
    int value = (int)(int32_t)cpu->gpr[4];
    IrixGuestAddr ra = (IrixGuestAddr)(uint32_t)cpu->gpr[31];
    IrixGuestFunctionEntry *caller = irix_guest_find_function(rt, ra);
    const char *caller_name =
        caller && caller->name ? caller->name : "?";

    /*
     * Bring-up containment for the desktop feature database.
     *
     * PanelComponent constructs the "Directory" ImdFtrIconX during startup.
     * libimdFtrDB::_imdFtrGetType currently reports EUCLEAN (135), which
     * causes desktop to post noOtrErrorMessage and terminate the session.
     *
     * Suppress only this exact feature-lookup error so the desktop can
     * continue far enough to expose the next real dependency/state failure.
     * Do not globally mask EUCLEAN or other IRIX error state.
     */
    if (value == 135 &&
        caller_name &&
        !strcmp(caller_name, "_imdFtrGetType")) {
        
        value = 0;
    }

    
    g_guest_oserror = value;
    cpu->gpr[2] = (uint64_t)(int64_t)old;
}

static void bridge_guest_oserror(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr ra = (IrixGuestAddr)(uint32_t)cpu->gpr[31];
    IrixGuestFunctionEntry *caller = irix_guest_find_function(rt, ra);
    
    cpu->gpr[2] = (uint64_t)(int64_t)g_guest_oserror;
}

IrixGuestFunction irix_clib_more_bridge_lookup(const char *name);

IrixGuestFunction irix_clib_extra_bridge_lookup(const char *name)
{
    IrixGuestFunction fn;
    if (!name) return 0;
    if (!strcmp(name, "setoserror")) return bridge_guest_setoserror;
    if (!strcmp(name, "oserror")) return bridge_guest_oserror;
    if (!strcmp(name, "flock")) return bridge_guest_flock;
    if (!strcmp(name, "mktemp")) return bridge_guest_mktemp;
    if (!strcmp(name, "mkstemp")) return bridge_guest_mkstemp;
    if (!strcmp(name, "mmap")) return bridge_guest_mmap;
    if (!strcmp(name, "munmap")) return bridge_guest_munmap;
    fn = irix_clib_extra_bridge_lookup_base(name);
    if (fn) return fn;
    if (!strcmp(name, "sscanf")) return bridge_sscanf;
    if (!strcmp(name, "exit")) return bridge_exit;
    return irix_clib_more_bridge_lookup(name);
}
