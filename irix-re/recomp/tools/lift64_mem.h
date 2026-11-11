/* Memory access for big-endian code lifted to a little-endian machine (lift_n64.py).
 *
 * Lifted code uses native memory: a field written with one width and read with the same width is simply the
 * native value, so native code sees the structures it expects.  Big-endian code also reads and writes the same
 * bytes at different widths: the low half of a long through a 32-bit load of its last four bytes, a bit field
 * through the 32-bit word that also holds a short and a char, a structure copied eight bytes at a time.  Those
 * give different results in little-endian memory.
 *
 * One shadow byte per 8-byte cell records how lifted code last wrote the cell:
 *
 *     bits 0-1   state of bytes 0-3 (the first half)        0 unknown   1 one native 32-bit value
 *     bits 2-3   state of bytes 4-7 (the second half)       2 narrower units   3 (both) one native 64-bit value
 *     bits 4-7   for each 16-bit quarter of a "narrower" half: 1 = one native 16-bit value, 0 = single bytes
 *
 * An access that matches the state is a plain native access.  One that does not is answered as the big-endian
 * machine would: a narrow load out of a wider value takes the bytes at the mirrored position, a wide load over
 * narrower units composes them most significant first, and a narrow store into a wider value first rewrites that
 * value as narrower units in place.  "Unknown" (memory lifted code never wrote: native structures, fresh blocks)
 * is always accessed natively.
 *
 * A loaded value carries a tag saying what it was composed from, so that storing it unchanged, or after only bit
 * operations, puts every unit back where it was: a structure copy keeps the native layout, and a bit field update
 * leaves its neighbours alone.
 */
#ifndef LIFT64_MEM_H
#define LIFT64_MEM_H
#include <stdint.h>

#if __SIZEOF_POINTER__ == 8
#define SH_BASE ((uint8_t *)0x200000000000ull)
#else
extern uint8_t *lift_sh_base;                 /* 32-bit process: the libc layer maps 512 MB for it where there is room */
#define SH_BASE lift_sh_base
#endif
#define SH(a) (SH_BASE[(uint64_t)(uintptr_t)(a) >> 3])

#define ST_U 0u
#define ST_W 1u
#define ST_N 2u
#define ST_Q 0x0fu                       /* both halves 3 */
#define T8 0x100u                        /* tag: composed by an 8-byte load; low byte = the cell's shadow */
#define T4 0x200u                        /* by a 4-byte load; bits 0-1 half state, bits 2-3 its quarter bits */
#define T2 0x400u                        /* by a 2-byte load; bit 0: 1 native 16-bit value, 0 bytes; bit 1: unknown */

typedef uint16_t lift_tag;

static inline uint16_t lm_bs16(uint16_t v) { return __builtin_bswap16(v); }
#define LM16(p) (*(uint16_t *)(p))
#define LM32(p) (*(uint32_t *)(p))
#define LM64(p) (*(uint64_t *)(p))

/* value of one half (big-endian composition) given its state and quarter bits */
static inline uint32_t lm_half_get(uintptr_t p, unsigned code, unsigned qb)
{
    if (code != ST_N)
        return LM32(p);
    return ((uint32_t)((qb & 1) ? LM16(p) : lm_bs16(LM16(p))) << 16) | ((qb & 2) ? LM16(p + 2) : lm_bs16(LM16(p + 2)));
}
static inline void lm_half_put(uintptr_t p, unsigned code, unsigned qb, uint32_t v)
{
    if (code != ST_N) {
        LM32(p) = v;
        return;
    }
    LM16(p) = (qb & 1) ? (uint16_t)(v >> 16) : lm_bs16((uint16_t)(v >> 16));
    LM16(p + 2) = (qb & 2) ? (uint16_t)v : lm_bs16((uint16_t)v);
}
/* a cell that holds one 64-bit value becomes two 32-bit values, most significant first */
static inline void lm_split_q(uintptr_t c)
{
    uint64_t v = LM64(c);
    LM32(c) = (uint32_t)(v >> 32);
    LM32(c + 4) = (uint32_t)v;
    SH(c) = ST_W | (ST_W << 2);
}
/* a half that holds one 32-bit value becomes two 16-bit values */
static inline void lm_split_w(uintptr_t c, unsigned h)
{
    uint32_t w = LM32(c + 4 * h);
    LM16(c + 4 * h) = (uint16_t)(w >> 16);
    LM16(c + 4 * h + 2) = (uint16_t)w;
    SH(c) = (uint8_t)((SH(c) & ~(3u << (2 * h))) | (ST_N << (2 * h)) | (3u << (4 + 2 * h)));
}

static inline uint64_t LD8(uintptr_t a, lift_tag *t)
{
    unsigned s;
    if (a & 7) { *t = 0; return LM64(a); }
    s = SH(a);
    *t = (lift_tag)(T8 | s);
    if (s == 0 || (s & 0x0f) == ST_Q)
        return LM64(a);
    return ((uint64_t)lm_half_get(a, s & 3, (s >> 4) & 3) << 32) | lm_half_get(a + 4, (s >> 2) & 3, (s >> 6) & 3);
}
static inline void ST8(uintptr_t a, uint64_t v, lift_tag t)
{
    unsigned s;
    if (a & 7) { LM64(a) = v; return; }
    s = t & 0xff;
    if (!(t & T8) || (s & 0x0f) == ST_Q) {              /* a computed value, or a copy of a 64-bit value */
        LM64(a) = v;
        SH(a) = ST_Q;
    } else if (s == 0) {                                 /* a copy of memory lifted code never wrote */
        LM64(a) = v;
        SH(a) = 0;
    } else {                                             /* a copy of narrower units: each back in its place */
        lm_half_put(a, s & 3, (s >> 4) & 3, (uint32_t)(v >> 32));
        lm_half_put(a + 4, (s >> 2) & 3, (s >> 6) & 3, (uint32_t)v);
        SH(a) = (uint8_t)s;
    }
}

static inline uint32_t LD4(uintptr_t a, lift_tag *t)
{
    uintptr_t c = a & ~(uintptr_t)7;
    unsigned s, h, code, qb;
    if (a & 3) { *t = 0; return LM32(a); }
    s = SH(c);
    h = (a >> 2) & 1;
    if ((s & 0x0f) == ST_Q) {
        *t = T4 | ST_W;
        return h ? (uint32_t)LM64(c) : (uint32_t)(LM64(c) >> 32);
    }
    code = (s >> (2 * h)) & 3;
    qb = (s >> (4 + 2 * h)) & 3;
    *t = (lift_tag)(T4 | code | (qb << 2));
    return lm_half_get(a, code, qb);
}
static inline void ST4(uintptr_t a, uint32_t v, lift_tag t)
{
    uintptr_t c = a & ~(uintptr_t)7;
    unsigned h, code = ST_W, qb = 0;
    if (a & 3) { LM32(a) = v; return; }
    if ((SH(c) & 0x0f) == ST_Q)
        lm_split_q(c);
    h = (a >> 2) & 1;
    if ((t & T4) && (t & 3) == ST_N) {                   /* units read as one word go back as they were */
        code = ST_N;
        qb = (t >> 2) & 3;
    }
    lm_half_put(a, code, qb, v);
    SH(c) = (uint8_t)((SH(c) & ~((3u << (2 * h)) | (3u << (4 + 2 * h)))) | (code << (2 * h)) | (qb << (4 + 2 * h)));
}

static inline uint16_t LD2(uintptr_t a, lift_tag *t)
{
    uintptr_t c = a & ~(uintptr_t)7;
    unsigned s, q, h, code;
    if (a & 1) { *t = 0; return LM16(a); }
    s = SH(c);
    q = (a >> 1) & 3;
    h = q >> 1;
    *t = T2 | 1;
    if ((s & 0x0f) == ST_Q)
        return (uint16_t)(LM64(c) >> (16 * (3 - q)));
    code = (s >> (2 * h)) & 3;
    if (code == ST_W)
        return (uint16_t)(LM32(c + 4 * h) >> (16 * (1 - (q & 1))));
    if (code == ST_U) { *t = T2 | 2; return LM16(a); }
    if (s & (0x10u << q))
        return LM16(a);
    *t = T2;
    return lm_bs16(LM16(a));
}
static inline void ST2(uintptr_t a, uint16_t v, lift_tag t)
{
    uintptr_t c = a & ~(uintptr_t)7;
    unsigned q, h, native = 1;
    if (a & 1) { LM16(a) = v; return; }
    if ((SH(c) & 0x0f) == ST_Q)
        lm_split_q(c);
    q = (a >> 1) & 3;
    h = q >> 1;
    if (((SH(c) >> (2 * h)) & 3) == ST_W)
        lm_split_w(c, h);
    if ((t & T2) && !(t & 3))                            /* two bytes read as one unit go back as bytes */
        native = 0;
    LM16(a) = native ? v : lm_bs16(v);
    SH(c) = (uint8_t)((SH(c) & ~((3u << (2 * h)) | (0x10u << q))) | (ST_N << (2 * h)) | (native ? (0x10u << q) : 0));
}

static inline uint8_t LD1(uintptr_t a)
{
    uintptr_t c = a & ~(uintptr_t)7;
    unsigned s = SH(c), h, q, code;
    if (s == 0)
        return *(uint8_t *)a;
    if ((s & 0x0f) == ST_Q)
        return (uint8_t)(LM64(c) >> (8 * (7 - (a & 7))));
    h = (a >> 2) & 1;
    code = (s >> (2 * h)) & 3;
    if (code == ST_W)
        return (uint8_t)(LM32(c + 4 * h) >> (8 * (3 - (a & 3))));
    q = (a >> 1) & 3;
    if (code == ST_N && (s & (0x10u << q)))
        return (uint8_t)(LM16(a & ~(uintptr_t)1) >> (8 * (1 - (a & 1))));
    return *(uint8_t *)a;
}
static inline void ST1(uintptr_t a, uint8_t v)
{
    uintptr_t c = a & ~(uintptr_t)7;
    unsigned s = SH(c), h, q;
    if ((s & 0x0f) == ST_Q)
        lm_split_q(c);
    h = (a >> 2) & 1;
    if (((SH(c) >> (2 * h)) & 3) == ST_W)
        lm_split_w(c, h);
    q = (a >> 1) & 3;
    s = SH(c);
    if (s & (0x10u << q)) {                              /* a 16-bit value becomes two bytes */
        uintptr_t p = a & ~(uintptr_t)1;
        LM16(p) = lm_bs16(LM16(p));
        s &= ~(0x10u << q);
    }
    *(uint8_t *)a = v;
    SH(c) = (uint8_t)((s & ~(3u << (2 * h))) | (ST_N << (2 * h)));
}

/* result tag of a bit operation: the layout of the operand that came from memory */
static inline lift_tag lm_bits(lift_tag a, lift_tag b) { return (a & (T4 | T2)) ? a : (b & (T4 | T2)) ? b : 0; }

/* Before a native function gets [a, a+n) through a typed pointer: it will read and write there with native widths
 * and knows nothing of the states.  Cells it covers whole become "unknown" as they are (lifted code wrote their fields
 * with the widths of the type).  In a cell it covers in part, the covered piece is rewritten as one native value of
 * its size and the rest keeps its value. */
static inline void lm_native(uintptr_t a, uint64_t n)
{
    uintptr_t c, e = a + n;
    if (!a) return;
    for (c = a & ~(uintptr_t)7; c < e; c += 8) {
        unsigned s = SH(c), h, q;
        uintptr_t lo = a > c ? a : c, hi = e < c + 8 ? e : c + 8;
        if (!s) continue;
        if (lo == c && hi == c + 8) { SH(c) = 0; continue; }
        if ((s & 0x0f) == ST_Q) lm_split_q(c);
        for (h = 0; h < 2; h++) {
            uintptr_t p = c + 4 * h;
            unsigned code;
            if (hi <= p || lo >= p + 4) continue;
            s = SH(c);
            code = (s >> (2 * h)) & 3;
            if (code == ST_U) continue;
            if (lo <= p && hi >= p + 4) {                    /* the whole half: one native 32-bit value */
                LM32(p) = lm_half_get(p, code, (s >> (4 + 2 * h)) & 3);
                SH(c) = (uint8_t)(s & ~((3u << (2 * h)) | (3u << (4 + 2 * h))));
                continue;
            }
            if (code == ST_W) lm_split_w(c, h);
            for (q = 2 * h; q < 2 * h + 2; q++) {
                uintptr_t w = c + 2 * q;
                int whole = lo <= w && hi >= w + 2, isw;
                if (hi <= w || lo >= w + 2) continue;
                isw = (SH(c) >> (4 + q)) & 1;
                if (whole != isw) {                          /* 16-bit value <-> two bytes */
                    LM16(w) = lm_bs16(LM16(w));
                    SH(c) ^= (uint8_t)(0x10u << q);
                }
            }
        }
    }
}

/* Objects handed to a native function through a typed pointer.  f lists the scalar fields of one element (pairs of
 * offset and size, ended by 0xffff); there are n elements of `size` bytes.
 *
 * lm_conv, before the call: every field is read the way lifted code would read it and stored as one native value
 * of its type, so the callee finds the layout its compiler expects whatever widths lifted code used to fill it in
 * (two shorts written by one 32-bit store, a structure cleared eight bytes at a time).
 * lm_mark, after the call: the same widths are recorded again without touching memory, for what the callee wrote. */
static inline void lm_conv(uintptr_t a, const uint16_t *f0, unsigned size, uint64_t n)
{
    const uint16_t *f;
    lift_tag t;
    if (!a) return;
    if (n > 65536) n = 65536;
    for (; n; n--, a += size)
        for (f = f0; f[0] != 0xffff; f += 2) {
            uintptr_t p = a + f[0];
            if (!SH(p)) continue;                            /* never written by lifted code: native already */
            if (f[1] == 8) { if (!(p & 7)) ST8(p, LD8(p, &t), 0); }
            else if (f[1] == 4) { if (!(p & 3)) ST4(p, LD4(p, &t), 0); }
            else if (f[1] == 2) { if (!(p & 1)) ST2(p, LD2(p, &t), 0); }
            else ST1(p, LD1(p));
        }
}
static inline void lm_mark(uintptr_t a, const uint16_t *f0, unsigned size, uint64_t n)
{
    const uint16_t *f;
    if (!a) return;
    if (n > 65536) n = 65536;
    for (; n; n--, a += size)
        for (f = f0; f[0] != 0xffff; f += 2) {
            uintptr_t p = a + f[0], c = p & ~(uintptr_t)7;
            unsigned h = (p >> 2) & 1, q = (p >> 1) & 3, s = SH(c);
            if (f[1] == 8 && !(p & 7))
                SH(c) = ST_Q;
            else if (f[1] == 4 && !(p & 3) && (s & 0x0f) != ST_Q)
                SH(c) = (uint8_t)((s & ~((3u << (2 * h)) | (3u << (4 + 2 * h)))) | (ST_W << (2 * h)));
            else if (f[1] == 2 && !(p & 1) && (s & 0x0f) != ST_Q && ((s >> (2 * h)) & 3) != ST_W)
                SH(c) = (uint8_t)((s & ~(3u << (2 * h))) | (ST_N << (2 * h)) | (0x10u << q));
        }
}

/* A typed class record of a lifted library, and the counted tables it points to.  A class record that native code
 * defines starts with the layout of its superclass's record; lift_native_class (libc layer) gives it those widths. */
struct lift_follow { uint16_t ptr, count, count_size, elem_size; const uint16_t *fields; };
struct lift_class { const char *rec; uint32_t size; const struct lift_follow *follow; uint32_t nfollow; };

/* the frame of a lifted function: its states are cleared when the function returns */
struct lift_fr { void *p; unsigned long cells; };
static inline void lift_fr_done(struct lift_fr *f) { __builtin_memset(&SH(f->p), 0, f->cells); }

/* dmult / dmultu: the low half of the 128-bit product is returned, the high half stored */
static inline uint64_t lift_mul64(uint64_t a, uint64_t b, int sign, uint64_t *hi)
{
#ifdef __SIZEOF_INT128__
    if (sign) { __int128 p = (__int128)(int64_t)a * (int64_t)b; *hi = (uint64_t)(p >> 64); return (uint64_t)p; }
    { unsigned __int128 p = (unsigned __int128)a * b; *hi = (uint64_t)(p >> 64); return (uint64_t)p; }
#else
    uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    uint64_t h = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    if (sign) { if ((int64_t)a < 0) h -= b; if ((int64_t)b < 0) h -= a; }
    *hi = h;
    return (mid << 32) | (uint32_t)ll;
#endif
}
#endif
