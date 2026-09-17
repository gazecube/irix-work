#define irix_clib_extra_bridge_lookup irix_clib_extra_bridge_lookup_base
#include "IrixCLibExtraBridge.c"
#undef irix_clib_extra_bridge_lookup

static int g_guest_oserror;

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

static void bridge_guest_setoserror(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    int old = g_guest_oserror;
    (void)rt;
    g_guest_oserror = (int)(int32_t)cpu->gpr[4];
    cpu->gpr[2] = (uint64_t)(int64_t)old;
}

static void bridge_guest_oserror(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint64_t)(int64_t)g_guest_oserror;
}

IrixGuestFunction irix_clib_extra_bridge_lookup(const char *name)
{
    IrixGuestFunction fn;
    if (!name) return 0;
    if (!strcmp(name, "setoserror")) return bridge_guest_setoserror;
    if (!strcmp(name, "oserror")) return bridge_guest_oserror;
    fn = irix_clib_extra_bridge_lookup_base(name);
    if (fn) return fn;
    if (!strcmp(name, "sscanf")) return bridge_sscanf;
    if (!strcmp(name, "exit")) return bridge_exit;
    return 0;
}
