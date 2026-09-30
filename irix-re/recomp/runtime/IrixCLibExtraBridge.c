#include "IrixCLibExtraBridge.h"
#include "IrixLibcBridge.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IrixGuestRuntime *g_rt;

typedef struct GuestArgs {
    IrixGuestRuntime *rt;
    IrixGuestCpu *cpu;
    unsigned reg;
    IrixGuestAddr stack;
} GuestArgs;

static const char *guest_cstr(IrixGuestRuntime *rt, IrixGuestAddr addr)
{
    return irix_guest_cstr(rt, addr, 65536u);
}

static uint64_t next_arg(GuestArgs *a)
{
    uint64_t v;
    if (a->reg <= 11u)
        return a->cpu->gpr[a->reg++];
    v = irix_guest_read64be(a->rt, a->stack);
    a->stack += 8u;
    return v;
}

static void append_bytes(char *dst, size_t cap, size_t *len, const char *src, size_t n)
{
    size_t room;
    if (!dst || !cap || !len || !src) return;
    room = (*len < cap) ? cap - *len - 1u : 0u;
    if (room) {
        size_t take = n < room ? n : room;
        memcpy(dst + *len, src, take);
        dst[*len + take] = 0;
    }
    *len += n;
}

static int format_guest(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                        IrixGuestAddr fmt_addr, unsigned first_reg,
                        IrixGuestAddr stack_addr,
                        char *out, size_t out_cap)
{
    const char *fmt = guest_cstr(rt, fmt_addr);
    GuestArgs args;
    size_t total = 0;
    const char *p;
    if (!fmt) return -1;
    if (out && out_cap) out[0] = 0;
    args.rt = rt;
    args.cpu = cpu;
    args.reg = first_reg;
    args.stack = stack_addr;

    for (p = fmt; *p; ) {
        char spec[64];
        char tmp[512];
        size_t si = 0;
        int width_star = 0, prec_star = 0;
        int width = 0, prec = 0;
        int long_count = 0, short_mod = 0;
        char conv;
        int n;

        if (*p != '%') {
            append_bytes(out, out_cap, &total, p, 1u);
            ++p;
            continue;
        }
        if (p[1] == '%') {
            append_bytes(out, out_cap, &total, "%", 1u);
            p += 2;
            continue;
        }

        spec[si++] = *p++;
        while (*p && strchr("-+ #0'", *p) && si + 1u < sizeof(spec)) spec[si++] = *p++;
        if (*p == '*') { width_star = 1; spec[si++] = *p++; }
        else while (isdigit((unsigned char)*p) && si + 1u < sizeof(spec)) spec[si++] = *p++;
        if (*p == '.') {
            spec[si++] = *p++;
            if (*p == '*') { prec_star = 1; spec[si++] = *p++; }
            else while (isdigit((unsigned char)*p) && si + 1u < sizeof(spec)) spec[si++] = *p++;
        }
        if (*p == 'h') { short_mod = 1; spec[si++] = *p++; if (*p == 'h') spec[si++] = *p++; }
        else if (*p == 'l') { long_count = 1; spec[si++] = *p++; if (*p == 'l') { long_count = 2; spec[si++] = *p++; } }
        else if (*p == 'L' || *p == 'j' || *p == 'z' || *p == 't') spec[si++] = *p++;
        if (!*p) break;
        conv = *p++;
        spec[si++] = conv;
        spec[si] = 0;
        if (width_star) width = (int)(int32_t)next_arg(&args);
        if (prec_star) prec = (int)(int32_t)next_arg(&args);

#define FMT1(value) do { \
            if (width_star && prec_star) n = snprintf(tmp, sizeof(tmp), spec, width, prec, (value)); \
            else if (width_star) n = snprintf(tmp, sizeof(tmp), spec, width, (value)); \
            else if (prec_star) n = snprintf(tmp, sizeof(tmp), spec, prec, (value)); \
            else n = snprintf(tmp, sizeof(tmp), spec, (value)); \
        } while (0)

        switch (conv) {
        case 'd': case 'i': {
            uint64_t raw = next_arg(&args);
            if (long_count >= 1) FMT1((long long)(int64_t)raw);
            else if (short_mod) FMT1((int)(short)(int32_t)raw);
            else FMT1((int)(int32_t)raw);
            break;
        }
        case 'u': case 'o': case 'x': case 'X': {
            uint64_t raw = next_arg(&args);
            if (long_count >= 1) FMT1((unsigned long long)raw);
            else if (short_mod) FMT1((unsigned int)(unsigned short)raw);
            else FMT1((unsigned int)(uint32_t)raw);
            break;
        }
        case 'c':
            FMT1((int)(uint32_t)next_arg(&args));
            break;
        case 's': {
            const char *s = guest_cstr(rt, (uint32_t)next_arg(&args));
            if (!s) s = "(null)";
            FMT1(s);
            break;
        }
        case 'p': {
            uintptr_t pv = (uintptr_t)(uint32_t)next_arg(&args);
            FMT1((void *)pv);
            break;
        }
        case 'n': {
            IrixGuestAddr a = (uint32_t)next_arg(&args);
            if (a) irix_guest_write32be(rt, a, (uint32_t)total);
            n = 0;
            tmp[0] = 0;
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
            union { uint64_t u; double d; } x;
            x.u = next_arg(&args);
            FMT1(x.d);
            break;
        }
        default:
            tmp[0] = '%'; tmp[1] = conv; tmp[2] = 0; n = 2;
            break;
        }
#undef FMT1
        if (n < 0) return -1;
        append_bytes(out, out_cap, &total, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : strlen(tmp));
    }
    return total > INT_MAX ? INT_MAX : (int)total;
}

static void bridge_sprintf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst = (uint32_t)cpu->gpr[4];
    char *p = (char *)irix_guest_ptr(rt, dst, 1);
    int n;
    if (!p) { cpu->gpr[2] = (uint64_t)(int64_t)-1; return; }
    n = format_guest(rt, cpu, (uint32_t)cpu->gpr[5], 6u, (uint32_t)cpu->gpr[29], p, 65536u);
    cpu->gpr[2] = (uint64_t)(int64_t)n;
}

static void bridge_snprintf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr dst = (uint32_t)cpu->gpr[4];
    uint32_t cap = (uint32_t)cpu->gpr[5];
    char *p = cap ? (char *)irix_guest_ptr(rt, dst, cap) : 0;
    int n = format_guest(rt, cpu, (uint32_t)cpu->gpr[6], 7u, (uint32_t)cpu->gpr[29], p, cap);
    cpu->gpr[2] = (uint64_t)(int64_t)n;
}

static void bridge_printf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    char buf[8192];
    int n = format_guest(rt, cpu, (uint32_t)cpu->gpr[4], 5u, (uint32_t)cpu->gpr[29], buf, sizeof(buf));
    if (n >= 0) fputs(buf, stdout);
    cpu->gpr[2] = (uint64_t)(int64_t)n;
}

static void bridge_fprintf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    char buf[8192];
    int n;
    (void)cpu->gpr[4];
    n = format_guest(rt, cpu, (uint32_t)cpu->gpr[5], 6u,
                     (uint32_t)cpu->gpr[29], buf, sizeof(buf));
    if (n >= 0) fputs(buf, stderr);
    cpu->gpr[2] = (uint64_t)(int64_t)n;
}

static void bridge_vfprintf(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    char buf[8192];
    IrixGuestAddr fmt_addr = (uint32_t)cpu->gpr[5];
    IrixGuestAddr va_addr = (uint32_t)cpu->gpr[6];
    int n;

    /*
     * IRIX N32 va_list is a pointer into the 8-byte argument save area.
     * Reuse the normal formatter with register consumption disabled so
     * next_arg() reads successive 64-bit big-endian slots from va_addr.
     *
     * The current stdio bridge does not yet expose arbitrary guest FILE *
     * objects here; match bridge_fprintf() and direct this path to stderr.
     */
    n = format_guest(rt, cpu, fmt_addr, 12u, va_addr, buf, sizeof(buf));
    if (n >= 0)
        fputs(buf, stderr);
    cpu->gpr[2] = (uint64_t)(int64_t)n;
}

static void bridge_puts(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    const char *s = guest_cstr(rt, (uint32_t)cpu->gpr[4]);
    cpu->gpr[2] = (uint64_t)(int64_t)(s ? puts(s) : EOF);
}

static void bridge_putchar(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    (void)rt;
    cpu->gpr[2] = (uint64_t)(int64_t)putchar((int)(uint32_t)cpu->gpr[4]);
}

static void bridge_atoi(IrixGuestRuntime *rt, IrixGuestCpu *cpu){const char*s=guest_cstr(rt,(uint32_t)cpu->gpr[4]);cpu->gpr[2]=(uint64_t)(int64_t)(s?atoi(s):0);}
static void bridge_atol(IrixGuestRuntime *rt, IrixGuestCpu *cpu){const char*s=guest_cstr(rt,(uint32_t)cpu->gpr[4]);cpu->gpr[2]=(uint64_t)(int64_t)(s?atol(s):0);}
static void bridge_atof(IrixGuestRuntime *rt, IrixGuestCpu *cpu){const char*s=guest_cstr(rt,(uint32_t)cpu->gpr[4]);irix_guest_fpr_set_s(cpu,0u,s?(float)atof(s):0.0f);}
static void bridge_strtol(IrixGuestRuntime *rt, IrixGuestCpu *cpu){IrixGuestAddr a=(uint32_t)cpu->gpr[4],ep=(uint32_t)cpu->gpr[5];const char*s=guest_cstr(rt,a),*e=0;long v=s?strtol(s,(char**)&e,(int)cpu->gpr[6]):0;if(ep)irix_guest_write32be(rt,ep,e? a+(uint32_t)(e-s):0);cpu->gpr[2]=(uint64_t)(int64_t)v;}
static void bridge_strtoul(IrixGuestRuntime *rt, IrixGuestCpu *cpu){IrixGuestAddr a=(uint32_t)cpu->gpr[4],ep=(uint32_t)cpu->gpr[5];const char*s=guest_cstr(rt,a),*e=0;unsigned long v=s?strtoul(s,(char**)&e,(int)cpu->gpr[6]):0;if(ep)irix_guest_write32be(rt,ep,e? a+(uint32_t)(e-s):0);cpu->gpr[2]=(uint64_t)v;}
static void bridge_strtod(IrixGuestRuntime *rt, IrixGuestCpu *cpu){IrixGuestAddr a=(uint32_t)cpu->gpr[4],ep=(uint32_t)cpu->gpr[5];const char*s=guest_cstr(rt,a),*e=0;double v=s?strtod(s,(char**)&e):0.0;if(ep)irix_guest_write32be(rt,ep,e? a+(uint32_t)(e-s):0);irix_guest_fpr_set_d(cpu,0,v);}
static void bridge_setoserror(IrixGuestRuntime *rt,IrixGuestCpu *cpu){int old=errno;(void)rt;errno=(int)(int32_t)cpu->gpr[4];cpu->gpr[2]=(uint64_t)(int64_t)old;}
static void bridge_oserror(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=(uint64_t)(int64_t)errno;}
static void bridge_tolower(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=(uint64_t)(int64_t)tolower((int)(uint32_t)cpu->gpr[4]);}
static void bridge_toupper(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=(uint64_t)(int64_t)toupper((int)(uint32_t)cpu->gpr[4]);}
static void bridge_isalpha(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=isalpha((int)(uint32_t)cpu->gpr[4])?1:0;}
static void bridge_isdigit(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=isdigit((int)(uint32_t)cpu->gpr[4])?1:0;}
static void bridge_isspace(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=isspace((int)(uint32_t)cpu->gpr[4])?1:0;}
static void bridge_isalnum(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=isalnum((int)(uint32_t)cpu->gpr[4])?1:0;}
static void bridge_isupper(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=isupper((int)(uint32_t)cpu->gpr[4])?1:0;}
static void bridge_islower(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=islower((int)(uint32_t)cpu->gpr[4])?1:0;}
static void bridge_isxdigit(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=isxdigit((int)(uint32_t)cpu->gpr[4])?1:0;}
static void bridge_ispunct(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=ispunct((int)(uint32_t)cpu->gpr[4])?1:0;}
static void bridge_isprint(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=isprint((int)(uint32_t)cpu->gpr[4])?1:0;}
static void bridge_iscntrl(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=iscntrl((int)(uint32_t)cpu->gpr[4])?1:0;}
static void bridge_abs(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=(uint64_t)(int64_t)abs((int)(int32_t)cpu->gpr[4]);}
static void bridge_labs(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=(uint64_t)(int64_t)labs((long)(int64_t)cpu->gpr[4]);}
static void bridge_rand(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;cpu->gpr[2]=(uint32_t)rand();}
static void bridge_srand(IrixGuestRuntime *rt,IrixGuestCpu *cpu){(void)rt;srand((unsigned)cpu->gpr[4]);cpu->gpr[2]=0;}

/*
 * IRIX N32 hard-float calls pass scalar FP arguments in the matching FPRs.
 * Keep these wrappers explicit: the guest register file is not host ABI
 * state, so directly exposing libm symbols would read the wrong arguments.
 */
#define BRIDGE_MATH_D1(n) static void bridge_##n(IrixGuestRuntime *rt,IrixGuestCpu *cpu){double x=irix_guest_fpr_get_d(cpu,12u);(void)rt;irix_guest_fpr_set_d(cpu,0u,n(x));}
#define BRIDGE_MATH_D2(n) static void bridge_##n(IrixGuestRuntime *rt,IrixGuestCpu *cpu){double x=irix_guest_fpr_get_d(cpu,12u),y=irix_guest_fpr_get_d(cpu,13u);(void)rt;irix_guest_fpr_set_d(cpu,0u,n(x,y));}
#define BRIDGE_MATH_F1(n) static void bridge_##n(IrixGuestRuntime *rt,IrixGuestCpu *cpu){float x=irix_guest_fpr_get_s(cpu,12u);(void)rt;irix_guest_fpr_set_s(cpu,0u,n(x));}
#define BRIDGE_MATH_F2(n) static void bridge_##n(IrixGuestRuntime *rt,IrixGuestCpu *cpu){float x=irix_guest_fpr_get_s(cpu,12u),y=irix_guest_fpr_get_s(cpu,13u);(void)rt;irix_guest_fpr_set_s(cpu,0u,n(x,y));}

BRIDGE_MATH_D1(ceil)
BRIDGE_MATH_D1(floor)
BRIDGE_MATH_D1(sqrt)
BRIDGE_MATH_D1(rint)
BRIDGE_MATH_D1(trunc)
BRIDGE_MATH_D1(cos)
BRIDGE_MATH_D1(exp)
BRIDGE_MATH_D2(hypot)
BRIDGE_MATH_D1(log)
BRIDGE_MATH_D1(log10)
BRIDGE_MATH_D1(sin)
BRIDGE_MATH_D1(sinh)
BRIDGE_MATH_D1(cosh)
BRIDGE_MATH_D2(atan2)
BRIDGE_MATH_D2(pow)

BRIDGE_MATH_F1(fabsf)
BRIDGE_MATH_F1(ceilf)
BRIDGE_MATH_F1(floorf)
BRIDGE_MATH_F1(sqrtf)
BRIDGE_MATH_F1(cosf)
BRIDGE_MATH_F1(expf)
BRIDGE_MATH_F2(hypotf)
BRIDGE_MATH_F1(logf)
BRIDGE_MATH_F1(log10f)
BRIDGE_MATH_F1(sinf)
BRIDGE_MATH_F1(sinhf)
BRIDGE_MATH_F1(coshf)
BRIDGE_MATH_F2(atan2f)
BRIDGE_MATH_F2(fmodf)
BRIDGE_MATH_F2(powf)

#undef BRIDGE_MATH_D1
#undef BRIDGE_MATH_D2
#undef BRIDGE_MATH_F1
#undef BRIDGE_MATH_F2

static IrixGuestAddr alloc_guest_for_vec(IrixGuestRuntime *rt, IrixGuestCpu *cpu, uint32_t size)
{
    IrixGuestFunction malloc_fn = irix_libc_bridge_lookup("malloc");
    IrixGuestCpu call;
    if (!malloc_fn) return 0;
    call = *cpu;
    call.gpr[4] = size;
    malloc_fn(rt, &call);
    return (uint32_t)call.gpr[2];
}

static int call_guest_vec_ctor(IrixGuestRuntime *rt, const IrixGuestCpu *caller,
                               IrixGuestAddr ctor, IrixGuestAddr object)
{
    IrixGuestCpu call = *caller;
    IrixGuestAddr saved_pc = rt->current_pc;
    call.gpr[4] = object;
    if (irix_guest_call(rt, &call, ctor) != 0) {
        rt->current_pc = saved_pc;
        return -1;
    }
    rt->current_pc = saved_pc;
    return 0;
}

static void bridge___vec_new_eh2(IrixGuestRuntime *rt, IrixGuestCpu *cpu)
{
    IrixGuestAddr array = (uint32_t)cpu->gpr[4];
    uint32_t count = (uint32_t)cpu->gpr[5];
    uint32_t element_size = (uint32_t)cpu->gpr[6];
    IrixGuestAddr ctor = (uint32_t)cpu->gpr[7];
    IrixGuestAddr dtor = (uint32_t)cpu->gpr[8];
    uint64_t total = (uint64_t)count * (uint64_t)element_size;
    uint32_t i;

    (void)dtor;
    if (total > UINT32_MAX) {
        cpu->gpr[2] = 0;
        return;
    }
    if (!array) {
        array = alloc_guest_for_vec(rt, cpu, (uint32_t)(total ? total : 1u));
        if (!array) {
            cpu->gpr[2] = 0;
            return;
        }
    }
    if (ctor && element_size) {
        for (i = 0; i < count && !rt->trapped; ++i) {
            if (call_guest_vec_ctor(rt, cpu, ctor, array + i * element_size) != 0)
                break;
        }
    }
    cpu->gpr[2] = array;
}

void irix_clib_extra_bridge_init(IrixGuestRuntime *rt){g_rt=rt;(void)g_rt;}

IrixGuestFunction irix_clib_extra_bridge_lookup(const char *name)
{
    if(!name)return 0;
    if(!strcmp(name,"__vec_new_eh2"))return bridge___vec_new_eh2;
#define M(n) if(!strcmp(name,#n))return bridge_##n
    M(sprintf); M(snprintf); M(printf); M(fprintf); M(vfprintf); M(puts); M(putchar);
    M(atoi); M(atol); M(atof); M(strtol); M(strtoul); M(strtod); M(setoserror); M(oserror);
    M(tolower); M(toupper); M(isalpha); M(isdigit); M(isspace); M(isalnum);
    M(isupper); M(islower); M(isxdigit); M(ispunct); M(isprint); M(iscntrl);
    M(abs); M(labs); M(rand); M(srand);
    M(ceil); M(floor); M(sqrt); M(rint); M(trunc); M(atan2); M(cos); M(exp);
    M(hypot); M(log); M(log10); M(pow); M(sin); M(sinh); M(cosh);
    M(fabsf); M(ceilf); M(floorf); M(sqrtf); M(atan2f); M(cosf); M(expf);
    M(hypotf); M(logf); M(log10f); M(fmodf); M(powf); M(sinf); M(sinhf); M(coshf);
#undef M
    return 0;
}
