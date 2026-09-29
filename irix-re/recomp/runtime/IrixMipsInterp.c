#include "IrixMipsInterp.h"

/* Runtime-private compatibility hook implemented in IrixGuest.c. */
extern void irix_guest_repair_xm_screen_private(IrixGuestRuntime *rt,
                                                IrixGuestAddr screen);
extern int irix_guest_repair_xm_vendor_ext_parent(IrixGuestRuntime *rt,
                                                   IrixGuestCpu *cpu,
                                                   IrixGuestAddr pc);

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runtime-private ownership lookup; kept out of IrixGuest.h so changing this
   interpreter plumbing does not invalidate every generated translation TU. */
extern IrixGuestAddr irix_guest_gp_for_address(IrixGuestAddr address);

static uint64_t sx32(uint32_t v)
{
    return (uint64_t)(int64_t)(int32_t)v;
}

static int16_t sx16(uint16_t v)
{
    return (int16_t)v;
}

static int exec_simple(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                       IrixGuestAddr pc, uint32_t insn)
{
    unsigned op = insn >> 26;
    unsigned rs = (insn >> 21) & 31u;
    unsigned rtno = (insn >> 16) & 31u;
    unsigned rd = (insn >> 11) & 31u;
    unsigned sa = (insn >> 6) & 31u;
    unsigned fn = insn & 63u;
    uint16_t immu = (uint16_t)(insn & 0xffffu);
    int32_t imm = (int16_t)immu;
    IrixGuestAddr a;

    rt->current_pc = pc;
    cpu->pc = pc;

    if (insn == 0u)
        return 0;

    if (op == 0u) {
        switch (fn) {
        case 0x00:
            cpu->gpr[rd] = sx32((uint32_t)cpu->gpr[rtno] << sa);
            break;
        case 0x02:
            cpu->gpr[rd] = sx32((uint32_t)cpu->gpr[rtno] >> sa);
            break;
        case 0x03:
            cpu->gpr[rd] = sx32((uint32_t)((int32_t)(uint32_t)cpu->gpr[rtno] >> sa));
            break;
        case 0x04:
            cpu->gpr[rd] = sx32((uint32_t)cpu->gpr[rtno] << ((uint32_t)cpu->gpr[rs] & 31u));
            break;
        case 0x06:
            cpu->gpr[rd] = sx32((uint32_t)cpu->gpr[rtno] >> ((uint32_t)cpu->gpr[rs] & 31u));
            break;
        case 0x07:
            cpu->gpr[rd] = sx32((uint32_t)((int32_t)(uint32_t)cpu->gpr[rtno] >> ((uint32_t)cpu->gpr[rs] & 31u)));
            break;
        case 0x10:
            cpu->gpr[rd] = cpu->hi;
            break;
        case 0x11:
            cpu->hi = cpu->gpr[rs];
            break;
        case 0x12:
            cpu->gpr[rd] = cpu->lo;
            break;
        case 0x13:
            cpu->lo = cpu->gpr[rs];
            break;
        case 0x18: {
            int64_t p = (int64_t)(int32_t)(uint32_t)cpu->gpr[rs] *
                        (int64_t)(int32_t)(uint32_t)cpu->gpr[rtno];
            cpu->lo = sx32((uint32_t)p);
            cpu->hi = sx32((uint32_t)((uint64_t)p >> 32));
            break;
        }
        case 0x19: {
            uint64_t p = (uint64_t)(uint32_t)cpu->gpr[rs] *
                         (uint64_t)(uint32_t)cpu->gpr[rtno];
            cpu->lo = sx32((uint32_t)p);
            cpu->hi = sx32((uint32_t)(p >> 32));
            break;
        }
        case 0x1a: {
            int32_t n = (int32_t)(uint32_t)cpu->gpr[rs];
            int32_t d = (int32_t)(uint32_t)cpu->gpr[rtno];
            if (!d) {
                cpu->lo = n < 0 ? 1u : UINT64_C(0xffffffffffffffff);
                cpu->hi = sx32((uint32_t)n);
            } else if ((uint32_t)n == 0x80000000u && (uint32_t)d == 0xffffffffu) {
                cpu->lo = UINT64_C(0xffffffff80000000);
                cpu->hi = 0;
            } else {
                cpu->lo = sx32((uint32_t)(n / d));
                cpu->hi = sx32((uint32_t)(n % d));
            }
            break;
        }
        case 0x1b: {
            uint32_t n = (uint32_t)cpu->gpr[rs];
            uint32_t d = (uint32_t)cpu->gpr[rtno];
            if (!d) {
                cpu->lo = UINT64_C(0xffffffffffffffff);
                cpu->hi = sx32(n);
            } else {
                cpu->lo = sx32(n / d);
                cpu->hi = sx32(n % d);
            }
            break;
        }
        case 0x20:
        case 0x21:
            cpu->gpr[rd] = sx32((uint32_t)cpu->gpr[rs] + (uint32_t)cpu->gpr[rtno]);
            break;
        case 0x22:
        case 0x23:
            cpu->gpr[rd] = sx32((uint32_t)cpu->gpr[rs] - (uint32_t)cpu->gpr[rtno]);
            break;
        case 0x24:
            cpu->gpr[rd] = cpu->gpr[rs] & cpu->gpr[rtno];
            break;
        case 0x25:
            cpu->gpr[rd] = cpu->gpr[rs] | cpu->gpr[rtno];
            break;
        case 0x26:
            cpu->gpr[rd] = cpu->gpr[rs] ^ cpu->gpr[rtno];
            break;
        case 0x27:
            cpu->gpr[rd] = ~(cpu->gpr[rs] | cpu->gpr[rtno]);
            break;
        case 0x2a:
            cpu->gpr[rd] = ((int64_t)cpu->gpr[rs] < (int64_t)cpu->gpr[rtno]) ? 1u : 0u;
            break;
        case 0x2b:
            cpu->gpr[rd] = (cpu->gpr[rs] < cpu->gpr[rtno]) ? 1u : 0u;
            break;
        case 0x2c:
        case 0x2d:
            cpu->gpr[rd] = cpu->gpr[rs] + cpu->gpr[rtno];
            break;
        case 0x2e:
        case 0x2f:
            cpu->gpr[rd] = cpu->gpr[rs] - cpu->gpr[rtno];
            break;
        case 0x30: /* TGE */
        case 0x31: /* TGEU */
        case 0x32: /* TLT */
        case 0x33: /* TLTU */
        case 0x34: /* TEQ */
        case 0x36: { /* TNE */
            int take = 0;
            switch (fn) {
            case 0x30:
                take = (int64_t)cpu->gpr[rs] >= (int64_t)cpu->gpr[rtno];
                break;
            case 0x31:
                take = cpu->gpr[rs] >= cpu->gpr[rtno];
                break;
            case 0x32:
                take = (int64_t)cpu->gpr[rs] < (int64_t)cpu->gpr[rtno];
                break;
            case 0x33:
                take = cpu->gpr[rs] < cpu->gpr[rtno];
                break;
            case 0x34:
                take = cpu->gpr[rs] == cpu->gpr[rtno];
                break;
            case 0x36:
                take = cpu->gpr[rs] != cpu->gpr[rtno];
                break;
            }
            if (take) {
                rt->fault_addr = (insn >> 6) & 0x3ffu;
                irix_guest_trap(rt, cpu, 0xeeee4013u);
                return -1;
            }
            break;
        }
        case 0x38:
            cpu->gpr[rd] = cpu->gpr[rtno] << sa;
            break;
        case 0x3a:
            cpu->gpr[rd] = cpu->gpr[rtno] >> sa;
            break;
        case 0x3b:
            cpu->gpr[rd] = (uint64_t)((int64_t)cpu->gpr[rtno] >> sa);
            break;
        case 0x3c:
            cpu->gpr[rd] = cpu->gpr[rtno] << (sa + 32u);
            break;
        case 0x3e:
            cpu->gpr[rd] = cpu->gpr[rtno] >> (sa + 32u);
            break;
        case 0x3f:
            cpu->gpr[rd] = (uint64_t)((int64_t)cpu->gpr[rtno] >> (sa + 32u));
            break;
        case 0x0c:
            irix_guest_syscall(rt, cpu);
            break;
        case 0x0d:
            irix_guest_trap(rt, cpu, (insn >> 6) & 0xfffffu);
            return -1;
        default:
            return 1;
        }
    } else {
        switch (op) {
        case 0x01: { /* REGIMM trap-immediate forms */
            int take = 0;
            switch (rtno) {
            case 0x08: /* TGEI */
                take = (int64_t)cpu->gpr[rs] >= (int64_t)imm;
                break;
            case 0x09: /* TGEIU */
                take = cpu->gpr[rs] >= (uint64_t)(int64_t)imm;
                break;
            case 0x0a: /* TLTI */
                take = (int64_t)cpu->gpr[rs] < (int64_t)imm;
                break;
            case 0x0b: /* TLTIU */
                take = cpu->gpr[rs] < (uint64_t)(int64_t)imm;
                break;
            case 0x0c: /* TEQI */
                take = cpu->gpr[rs] == (uint64_t)(int64_t)imm;
                break;
            case 0x0e: /* TNEI */
                take = cpu->gpr[rs] != (uint64_t)(int64_t)imm;
                break;
            default:
                return 1;
            }
            if (take) {
                rt->fault_addr = (insn >> 6) & 0x3ffu;
                irix_guest_trap(rt, cpu, 0xeeee4013u);
                return -1;
            }
            break;
        }
        case 0x08:
        case 0x09:
            cpu->gpr[rtno] = sx32((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            break;
        case 0x18:
        case 0x19:
            cpu->gpr[rtno] = cpu->gpr[rs] + (uint64_t)(int64_t)imm;
            break;
        case 0x0a:
            cpu->gpr[rtno] = ((int64_t)cpu->gpr[rs] < (int64_t)imm) ? 1u : 0u;
            break;
        case 0x0b:
            cpu->gpr[rtno] = (cpu->gpr[rs] < (uint64_t)(int64_t)imm) ? 1u : 0u;
            break;
        case 0x0c:
            cpu->gpr[rtno] = cpu->gpr[rs] & immu;
            break;
        case 0x0d:
            cpu->gpr[rtno] = cpu->gpr[rs] | immu;
            break;
        case 0x0e:
            cpu->gpr[rtno] = cpu->gpr[rs] ^ immu;
            break;
        case 0x0f:
            cpu->gpr[rtno] = sx32((uint32_t)immu << 16);
            break;
        case 0x20:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            cpu->gpr[rtno] = (uint64_t)(int64_t)(int8_t)irix_guest_read8(rt, a);
            break;
        case 0x21:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            cpu->gpr[rtno] = (uint64_t)(int64_t)(int16_t)irix_guest_read16be(rt, a);
            break;
        case 0x23:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);

            /*
             * Exact SGI libSgm empty-table load observed at 0x0b208148.
             * Put the compatibility case at the actual LW execution point,
             * after effective-address calculation.  This makes it impossible
             * for a main-loop/delay-slot dispatch distinction to bypass it.
             *
             * The following instruction branches on the loaded value being
             * zero, so a NULL effective address represents the empty/end case
             * that this routine already knows how to handle.
             */
            if (pc == 0x0b208148u && a == 0u && rtno == 6u) {
                fprintf(stderr,
                        "IRIX libSgm NULL LW repair HIT: "
                        "pc=%08x rs=%u rt=%u imm=%d a=%08x -> a2=00000000\n",
                        pc, rs, rtno, (int)imm, a);
                cpu->gpr[rtno] = 0u;
                break;
            }

            cpu->gpr[rtno] = sx32(irix_guest_read32be(rt, a));
            break;
        case 0x24:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);

            /*
             * SGI Xt _XtGetResources compatibility repair.
             *
             * The original MIPS path itself reaches 0x0f6166b4 with r31
             * holding a small malformed resource-table value (observed
             * 0x000004ac), then executes:
             *
             *   lbu r31,0x14(r31)
             *
             * which faults at 0x000004c0.  This is therefore guest resource
             * state damage, not translated-register corruption.  For this one
             * proven load, treat an unmapped resource pointer as an empty/zero
             * byte and let Xt continue through its own normal logic.
             */
            if (pc == 0x0f6166b4u && rtno == 31u &&
                !irix_guest_ptr(rt, a, 1u)) {
                fprintf(stderr,
                        "IRIX XtGetResources bad resource-byte repair HIT: "
                        "pc=%08x base=%08x addr=%08x -> ra=00000000\n",
                        pc, (uint32_t)cpu->gpr[rs], a);
                cpu->gpr[rtno] = 0u;
                break;
            }

            cpu->gpr[rtno] = irix_guest_read8(rt, a);
            break;
        case 0x25:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            cpu->gpr[rtno] = irix_guest_read16be(rt, a);
            break;
        case 0x27:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            cpu->gpr[rtno] = irix_guest_read32be(rt, a);
            break;
        case 0x28:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            irix_guest_write8(rt, a, (uint8_t)cpu->gpr[rtno]);
            break;
        case 0x29:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            irix_guest_write16be(rt, a, (uint16_t)cpu->gpr[rtno]);
            break;
        case 0x2b:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);

            /*
             * Exact SGI libSgm empty-table return-store observed after the
             * 0x0b208148 NULL table walk is handled.  The helper returns via:
             *
             *   0b2081bc  jr  ra
             *   0b2081c0  sw  a1,0(v1)    # delay slot
             *
             * In the empty-table path v1 is NULL.  Treat that one proven
             * return-store as an absent destination rather than faulting at
             * address zero.  Keep every non-NULL store and every other PC on
             * the normal path.
             */
            if (pc == 0x0b2081c0u && a == 0u &&
                rs == 3u && rtno == 5u) {
                fprintf(stderr,
                        "IRIX libSgm NULL return-store repair HIT: "
                        "pc=%08x v1=%08x a1=%08x -> skip store\n",
                        pc, (uint32_t)cpu->gpr[3],
                        (uint32_t)cpu->gpr[5]);
                break;
            }

            irix_guest_write32be(rt, a, (uint32_t)cpu->gpr[rtno]);
            break;
        case 0x37:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            cpu->gpr[rtno] = irix_guest_read64be(rt, a);
            break;
        case 0x3f:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            irix_guest_write64be(rt, a, cpu->gpr[rtno]);
            break;
        case 0x22: {
            uint32_t oldv, w;
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            oldv = (uint32_t)cpu->gpr[rtno];
            w = irix_guest_read32be(rt, a & ~3u);
            switch (a & 3u) {
            case 0: oldv = w; break;
            case 1: oldv = (oldv & 0x000000ffu) | (w << 8); break;
            case 2: oldv = (oldv & 0x0000ffffu) | (w << 16); break;
            default: oldv = (oldv & 0x00ffffffu) | (w << 24); break;
            }
            cpu->gpr[rtno] = sx32(oldv);
            break;
        }
        case 0x26: {
            uint32_t oldv, w;
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            oldv = (uint32_t)cpu->gpr[rtno];
            w = irix_guest_read32be(rt, a & ~3u);
            switch (a & 3u) {
            case 0: oldv = (oldv & 0xffffff00u) | (w >> 24); break;
            case 1: oldv = (oldv & 0xffff0000u) | (w >> 16); break;
            case 2: oldv = (oldv & 0xff000000u) | (w >> 8); break;
            default: oldv = w; break;
            }
            cpu->gpr[rtno] = sx32(oldv);
            break;
        }
        case 0x2a: {
            IrixGuestAddr q;
            uint32_t m, v;
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            q = a & ~3u;
            m = irix_guest_read32be(rt, q);
            v = (uint32_t)cpu->gpr[rtno];
            switch (a & 3u) {
            case 0: m = v; break;
            case 1: m = (m & 0xff000000u) | (v >> 8); break;
            case 2: m = (m & 0xffff0000u) | (v >> 16); break;
            default: m = (m & 0xffffff00u) | (v >> 24); break;
            }
            irix_guest_write32be(rt, q, m);
            break;
        }
        case 0x2e: {
            IrixGuestAddr q;
            uint32_t m, v;
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            q = a & ~3u;
            m = irix_guest_read32be(rt, q);
            v = (uint32_t)cpu->gpr[rtno];
            switch (a & 3u) {
            case 0: m = (m & 0x00ffffffu) | (v << 24); break;
            case 1: m = (m & 0x0000ffffu) | (v << 16); break;
            case 2: m = (m & 0x000000ffu) | (v << 8); break;
            default: m = v; break;
            }
            irix_guest_write32be(rt, q, m);
            break;
        }
        case 0x1a: {
            unsigned n;
            uint64_t m, mask;
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            n = a & 7u;
            m = irix_guest_read64be(rt, a & ~7u);
            mask = n ? ((UINT64_C(1) << (n * 8u)) - 1u) : 0u;
            cpu->gpr[rtno] = (cpu->gpr[rtno] & mask) | (m << (n * 8u));
            break;
        }
        case 0x1b: {
            unsigned n, sh;
            uint64_t m, mask;
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            n = a & 7u;
            sh = (7u - n) * 8u;
            m = irix_guest_read64be(rt, a & ~7u);
            mask = sh ? (~UINT64_C(0) << (64u - sh)) : 0u;
            cpu->gpr[rtno] = (cpu->gpr[rtno] & mask) | (m >> sh);
            break;
        }
        case 0x31:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            cpu->fpr[rtno] = (cpu->fpr[rtno] & UINT64_C(0xffffffff00000000)) |
                             irix_guest_read32be(rt, a);
            break;
        case 0x35:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            cpu->fpr[rtno] = irix_guest_read64be(rt, a);
            break;
        case 0x39:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            irix_guest_write32be(rt, a, (uint32_t)cpu->fpr[rtno]);
            break;
        case 0x3d:
            a = (IrixGuestAddr)((uint32_t)cpu->gpr[rs] + (uint32_t)imm);
            irix_guest_write64be(rt, a, cpu->fpr[rtno]);
            break;
        case 0x11: {
            unsigned fmt = rs, ft = rtno, fs = rd, fd = sa, funct = fn;
            float sf, tf;
            double sd, td;
            if (fmt == 0x00) {
                cpu->gpr[ft] = sx32((uint32_t)cpu->fpr[fs]);
                break;
            }
            if (fmt == 0x01) {
                cpu->gpr[ft] = cpu->fpr[fs];
                break;
            }
            if (fmt == 0x04) {
                cpu->fpr[fs] = (cpu->fpr[fs] & UINT64_C(0xffffffff00000000)) |
                               (uint32_t)cpu->gpr[ft];
                break;
            }
            if (fmt == 0x05) {
                cpu->fpr[fs] = cpu->gpr[ft];
                break;
            }
            if (fmt == 0x02) {
                cpu->gpr[ft] = (fs == 31u) ? cpu->fcr31 : 0u;
                break;
            }
            if (fmt == 0x06) {
                if (fs == 31u) cpu->fcr31 = (uint32_t)cpu->gpr[ft];
                break;
            }
            if (fmt == 0x10) {
                sf = irix_guest_fpr_get_s(cpu, fs);
                tf = irix_guest_fpr_get_s(cpu, ft);
                switch (funct) {
                case 0x00: irix_guest_fpr_set_s(cpu, fd, sf + tf); break;
                case 0x01: irix_guest_fpr_set_s(cpu, fd, sf - tf); break;
                case 0x02: irix_guest_fpr_set_s(cpu, fd, sf * tf); break;
                case 0x03: irix_guest_fpr_set_s(cpu, fd, sf / tf); break;
                case 0x04: irix_guest_fpr_set_s(cpu, fd, sqrtf(sf)); break;
                case 0x05: irix_guest_fpr_set_s(cpu, fd, fabsf(sf)); break;
                case 0x06: cpu->fpr[fd]=(cpu->fpr[fd]&UINT64_C(0xffffffff00000000))|(uint32_t)cpu->fpr[fs]; break;
                case 0x07: irix_guest_fpr_set_s(cpu, fd, -sf); break;
                case 0x0d: cpu->fpr[fd]=(cpu->fpr[fd]&UINT64_C(0xffffffff00000000))|(uint32_t)(int32_t)sf; break;
                case 0x21: irix_guest_fpr_set_d(cpu, fd, (double)sf); break;
                case 0x32: if(sf==tf)cpu->fcr31|=1u<<23;else cpu->fcr31&=~(1u<<23); break;
                case 0x3c: if(sf<tf)cpu->fcr31|=1u<<23;else cpu->fcr31&=~(1u<<23); break;
                case 0x3e: if(sf<=tf)cpu->fcr31|=1u<<23;else cpu->fcr31&=~(1u<<23); break;
                default: return 1;
                }
                break;
            }
            if (fmt == 0x11) {
                sd = irix_guest_fpr_get_d(cpu, fs);
                td = irix_guest_fpr_get_d(cpu, ft);
                switch (funct) {
                case 0x00: irix_guest_fpr_set_d(cpu, fd, sd + td); break;
                case 0x01: irix_guest_fpr_set_d(cpu, fd, sd - td); break;
                case 0x02: irix_guest_fpr_set_d(cpu, fd, sd * td); break;
                case 0x03: irix_guest_fpr_set_d(cpu, fd, sd / td); break;
                case 0x04: irix_guest_fpr_set_d(cpu, fd, sqrt(sd)); break;
                case 0x05: irix_guest_fpr_set_d(cpu, fd, fabs(sd)); break;
                case 0x06: cpu->fpr[fd]=cpu->fpr[fs]; break;
                case 0x07: irix_guest_fpr_set_d(cpu, fd, -sd); break;
                case 0x0d: cpu->fpr[fd]=(cpu->fpr[fd]&UINT64_C(0xffffffff00000000))|(uint32_t)(int32_t)sd; break;
                case 0x20: irix_guest_fpr_set_s(cpu, fd, (float)sd); break;
                case 0x32: if(sd==td)cpu->fcr31|=1u<<23;else cpu->fcr31&=~(1u<<23); break;
                case 0x3c: if(sd<td)cpu->fcr31|=1u<<23;else cpu->fcr31&=~(1u<<23); break;
                case 0x3e: if(sd<=td)cpu->fcr31|=1u<<23;else cpu->fcr31&=~(1u<<23); break;
                default: return 1;
                }
                break;
            }
            if (fmt == 0x14 && funct == 0x20) {
                irix_guest_fpr_set_s(cpu, fd, (float)(int32_t)(uint32_t)cpu->fpr[fs]);
                break;
            }
            if (fmt == 0x14 && funct == 0x21) {
                irix_guest_fpr_set_d(cpu, fd, (double)(int32_t)(uint32_t)cpu->fpr[fs]);
                break;
            }
            if (fmt == 0x15 && funct == 0x20) {
                irix_guest_fpr_set_s(cpu, fd, (float)(int64_t)cpu->fpr[fs]);
                break;
            }
            if (fmt == 0x15 && funct == 0x21) {
                irix_guest_fpr_set_d(cpu, fd, (double)(int64_t)cpu->fpr[fs]);
                break;
            }
            return 1;
        }
        case 0x2f:
        case 0x33:
            /* CACHE/PREF are performance hints in user mode. */
            break;
        default:
            return 1;
        }
    }

    cpu->gpr[0] = 0;
    return rt->trapped ? -1 : 0;
}

static int is_control(uint32_t insn)
{
    unsigned op = insn >> 26;
    unsigned fn = insn & 63u;
    if (op == 0u && (fn == 0x08u || fn == 0x09u))
        return 1;
    if (op == 0x01u) {
        unsigned rtno = (insn >> 16) & 31u;
        if (rtno == 0x00u || rtno == 0x01u ||
            rtno == 0x02u || rtno == 0x03u ||
            rtno == 0x10u || rtno == 0x11u ||
            rtno == 0x12u || rtno == 0x13u)
            return 1;
        return 0;
    }
    if (op == 0x02u || op == 0x03u ||
        op == 0x04u || op == 0x05u || op == 0x06u || op == 0x07u ||
        op == 0x14u || op == 0x15u || op == 0x16u || op == 0x17u)
        return 1;
    if (op == 0x11u && ((insn >> 21) & 31u) == 0x08u)
        return 1;
    return 0;
}

static int exec_delay(IrixGuestRuntime *rt, IrixGuestCpu *cpu, IrixGuestAddr pc)
{
    uint32_t insn;
    if (!irix_guest_ptr(rt, pc, 4u))
        return -1;
    insn = irix_guest_read32be(rt, pc);
    if (rt->trapped)
        return -1;
    if (is_control(insn)) {
        fprintf(stderr,
                "IRIX MIPS interp: control instruction in delay slot pc=%08x insn=%08x\n",
                pc, insn);
        return -1;
    }
    return exec_simple(rt, cpu, pc, insn);
}

static int mapped_exec(IrixGuestRuntime *rt, IrixGuestAddr pc)
{
    size_t i;
    for (i = 0; i < rt->memory.count; ++i) {
        IrixGuestSegment *s = &rt->memory.segments[i];
        uint64_t end = (uint64_t)s->guest_base + (uint64_t)s->size;
        if ((uint64_t)pc >= s->guest_base && (uint64_t)pc + 4u <= end)
            return (s->flags & 4u) != 0;
    }
    return 0;
}

int irix_mips_interp_step(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                          IrixGuestAddr address)
{
    IrixGuestAddr pc = address;
    uint32_t insn, op, rs, rtno, rd, fn;
    int taken = 0, likely = 0, link = 0;
    IrixGuestAddr target = 0, next = pc + 8u;

    if (!mapped_exec(rt, pc))
        return -1;

    insn = irix_guest_read32be(rt, pc);
    if (rt->trapped)
        return -1;

    op = insn >> 26;
    rs = (insn >> 21) & 31u;
    rtno = (insn >> 16) & 31u;
    rd = (insn >> 11) & 31u;
    fn = insn & 63u;

    if (!is_control(insn)) {
        int rc = exec_simple(rt, cpu, pc, insn);
        if (rc != 0)
            return -1;
        cpu->pc = pc + 4u;
        rt->current_pc = cpu->pc;
        cpu->gpr[0] = 0;
        return rt->trapped ? -1 : 0;
    }

    if (op == 0u && fn == 0x08u) {
        target = (IrixGuestAddr)(uint32_t)cpu->gpr[rs];
        if (exec_delay(rt, cpu, pc + 4u) != 0)
            return -1;
        cpu->pc = target;
        rt->current_pc = target;
        cpu->gpr[0] = 0;
        return rt->trapped ? -1 : 0;
    }

    if (op == 0u && fn == 0x09u) {
        target = (IrixGuestAddr)(uint32_t)cpu->gpr[rs];
        if (rd)
            cpu->gpr[rd] = sx32(pc + 8u);
        if (exec_delay(rt, cpu, pc + 4u) != 0)
            return -1;
        cpu->pc = target;
        rt->current_pc = target;
        cpu->gpr[0] = 0;
        return rt->trapped ? -1 : 0;
    }

    if (op == 0x02u || op == 0x03u) {
        target = ((pc + 4u) & 0xf0000000u) |
                 ((insn & 0x03ffffffu) << 2);
        if (op == 0x03u)
            cpu->gpr[31] = sx32(pc + 8u);
        if (exec_delay(rt, cpu, pc + 4u) != 0)
            return -1;
        cpu->pc = target;
        rt->current_pc = target;
        cpu->gpr[0] = 0;
        return rt->trapped ? -1 : 0;
    }

    if (op == 0x11u && rs == 0x08u) {
        int tf = rtno & 1u;
        likely = (rtno & 2u) != 0;
        taken = ((cpu->fcr31 & (1u << 23)) != 0) == tf;
        target = pc + 4u +
                 ((int32_t)(int16_t)(insn & 0xffffu) << 2);
    } else if (op == 0x01u) {
        int32_t v = (int32_t)(uint32_t)cpu->gpr[rs];
        switch (rtno) {
        case 0x00: taken = v < 0; break;
        case 0x01: taken = v >= 0; break;
        case 0x02: taken = v < 0; likely = 1; break;
        case 0x03: taken = v >= 0; likely = 1; break;
        case 0x10: taken = v < 0; link = 1; break;
        case 0x11: taken = v >= 0; link = 1; break;
        case 0x12: taken = v < 0; link = 1; likely = 1; break;
        case 0x13: taken = v >= 0; link = 1; likely = 1; break;
        default:
            return -1;
        }
        target = pc + 4u +
                 ((int32_t)(int16_t)(insn & 0xffffu) << 2);
        if (link)
            cpu->gpr[31] = sx32(pc + 8u);
    } else {
        int64_t sv = (int64_t)cpu->gpr[rs];
        switch (op) {
        case 0x04: taken = cpu->gpr[rs] == cpu->gpr[rtno]; break;
        case 0x05: taken = cpu->gpr[rs] != cpu->gpr[rtno]; break;
        case 0x06: taken = sv <= 0; break;
        case 0x07: taken = sv > 0; break;
        case 0x14: taken = cpu->gpr[rs] == cpu->gpr[rtno]; likely = 1; break;
        case 0x15: taken = cpu->gpr[rs] != cpu->gpr[rtno]; likely = 1; break;
        case 0x16: taken = sv <= 0; likely = 1; break;
        case 0x17: taken = sv > 0; likely = 1; break;
        default:
            return -1;
        }
        target = pc + 4u +
                 ((int32_t)(int16_t)(insn & 0xffffu) << 2);
    }

    if (!(likely && !taken)) {
        if (exec_delay(rt, cpu, pc + 4u) != 0)
            return -1;
    }

    cpu->pc = taken ? target : next;
    rt->current_pc = cpu->pc;
    cpu->gpr[0] = 0;
    return rt->trapped ? -1 : 0;
}

int irix_mips_interp_should_force(IrixGuestAddr address)
{
    /*
     * SGI libSgm helper containing 0x0b208148 currently translates into a
     * native path that faults before the interpreter's exact NULL-LW repair
     * can run.  Force only this demonstrated helper through original MIPS so
     * the repair at the real LW execution point applies.
     */
    if (address == 0x0b208098u)
        return 1;

    /*
     * Xt _XtGetResources currently traps in translated code at 0x0f6166b4
     * with its return-address/base register state corrupted.  Keep the exact
     * SGI function on original MIPS while bring-up continues; this also lets
     * us distinguish a translator ABI bug from bad Xt data.
     */
    if (address == 0x0f616598u)
        return 1;

    static int init;
    static IrixGuestAddr addr[64];
    static size_t count;
    size_t i;
    if (!init) {
        const char *e = getenv("IRIX_INTERPRET_PC");
        init = 1;
        if (e && *e) {
            size_t n = strlen(e) + 1u;
            char *copy = (char *)malloc(n);
            char *p;
            if (!copy)
                return 0;
            memcpy(copy, e, n);
            p = copy;
            while (p && *p && count < 64u) {
                char *end = 0;
                unsigned long v = strtoul(p, &end, 0);
                if (end == p)
                    break;
                addr[count++] = (IrixGuestAddr)v;
                p = (*end == ',') ? end + 1 : end;
                if (*p && *p != ',') {
                    while (*p && *p != ',') ++p;
                    if (*p == ',') ++p;
                }
            }
            free(copy);
        }
    }
    for (i = 0; i < count; ++i)
        if (addr[i] == address)
            return 1;
    return 0;
}

int irix_mips_interp_call(IrixGuestRuntime *rt, IrixGuestCpu *cpu,
                          IrixGuestAddr address)
{
    IrixGuestAddr pc = address;
    IrixGuestAddr outer_ra = (IrixGuestAddr)(uint32_t)cpu->gpr[31];
    uint64_t steps = 0;
    const uint64_t max_steps = UINT64_C(100000000);
    static unsigned xt_tm_repair_depth;

    if (!mapped_exec(rt, address))
        return -1;

    fprintf(stderr, "IRIX MIPS interpreter: enter pc=%08x ra=%08x\n",
            address, (uint32_t)cpu->gpr[31]);

    while (!rt->trapped && steps++ < max_steps) {
        /*
         * SGI Xt translation-merge bring-up repair.
         *
         * XtOverrideTranslations reaches the private helper at 0x0f622e2c
         * with a3 expected to be an XtTranslations object. In the failing
         * Motif path a3 instead points directly at the class translation
         * string ("<EnterWindow>:ManagerEnter..."). The helper interprets the
         * first byte '<' as hasBindings != 0, then reads a3+4 ("erWi") as the
         * embedded xlations pointer and later faults through s7=0x65725769.
         *
         * Parse only that demonstrably raw-string case with SGI's own
         * XtParseTranslationTable, preserve the helper's complete CPU state,
         * and substitute the resulting opaque translation object for a3.
         */
        if (pc == 0x0f622e2cu && !xt_tm_repair_depth) {
            IrixGuestAddr raw = (IrixGuestAddr)(uint32_t)cpu->gpr[7];
            uint8_t ch = 0u;

            if (raw && irix_guest_ptr(rt, raw, 1u))
                ch = irix_guest_read8(rt, raw);

            if (ch == '<' || ch == '#' || ch == ' ' ||
                ch == '\\t' || ch == '\\n') {
                IrixGuestCpu saved_cpu = *cpu;
                IrixGuestAddr saved_current_pc = rt->current_pc;
                IrixGuestAddr parsed = 0u;
                int parse_rc;

                ++xt_tm_repair_depth;
                cpu->gpr[4] = (uint64_t)(int64_t)(int32_t)raw;
                cpu->gpr[25] = (uint64_t)(int64_t)(int32_t)0x0f6285e0u;
                parse_rc = irix_guest_call(rt, cpu, 0x0f6285e0u);
                if (parse_rc == 0 && !rt->trapped)
                    parsed = (IrixGuestAddr)(uint32_t)cpu->gpr[2];
                --xt_tm_repair_depth;

                if (!rt->trapped) {
                    *cpu = saved_cpu;
                    rt->current_pc = saved_current_pc;
                    if (parsed && irix_guest_ptr(rt, parsed, 4u)) {
                        cpu->gpr[7] =
                            (uint64_t)(int64_t)(int32_t)parsed;
                        fprintf(stderr,
                                "IRIX Xt translation repair: helper=0f622e2c "
                                "raw=%08x -> parsed=%08x\n",
                                raw, parsed);
                    } else {
                        fprintf(stderr,
                                "IRIX Xt translation repair FAILED: "
                                "raw=%08x parsed=%08x rc=%d\n",
                                raw, parsed, parse_rc);
                    }
                }
            }
        }
        uint32_t insn, op, rs, rtno, rd, fn;
        int taken = 0, likely = 0, link = 0;
        IrixGuestAddr target = 0, next = pc + 8u;

        if (!mapped_exec(rt, pc))
            break;
        insn = irix_guest_read32be(rt, pc);
        if (rt->trapped)
            break;
        op = insn >> 26;
        rs = (insn >> 21) & 31u;
        rtno = (insn >> 16) & 31u;
        rd = (insn >> 11) & 31u;
        fn = insn & 63u;

        /*
         * Focused trace for the real SGI _XtGetResources failure.  Running
         * this function through original MIPS reproduced the crash, so the
         * remaining problem is guest data/state, not translated register
         * allocation.  Capture the short window leading into 0x0f6166b4 so
         * we can see exactly where r31 changes from the call return address
         * into the bogus 0x000004ac resource/quark-like value.
         */
        if (pc >= 0x0f616660u && pc <= 0x0f6166b8u) {
            fprintf(stderr,
                    "IRIX XtGetResources trace: pc=%08x insn=%08x "
                    "v0=%08x v1=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
                    "s0=%08x s1=%08x s2=%08x s3=%08x "
                    "t8=%08x t9=%08x gp=%08x ra=%08x\n",
                    pc, insn,
                    (uint32_t)cpu->gpr[2], (uint32_t)cpu->gpr[3],
                    (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5],
                    (uint32_t)cpu->gpr[6], (uint32_t)cpu->gpr[7],
                    (uint32_t)cpu->gpr[16], (uint32_t)cpu->gpr[17],
                    (uint32_t)cpu->gpr[18], (uint32_t)cpu->gpr[19],
                    (uint32_t)cpu->gpr[24], (uint32_t)cpu->gpr[25],
                    (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[31]);
        }

        /*
         * Narrow SGI Motif repair while executing original MIPS inside
         * VendorExtInitialize.  Do this before the faulting load so the
         * original instruction stream continues normally with its required
         * extParent invariant restored.
         */
        if (pc == 0x0b314ffcu)
            (void)irix_guest_repair_xm_vendor_ext_parent(rt, cpu, pc);

        if (!is_control(insn)) {
            int rc = exec_simple(rt, cpu, pc, insn);
            if (rc != 0) {
                if (rt->trapped) {
                    fprintf(stderr,
                            "IRIX MIPS interpreter: memory/trap failure "
                            "pc=%08x insn=%08x op=%02x fn=%02x fault=%08x\n",
                            pc, insn, op, fn, rt->fault_addr);
                    return -1;
                }
                fprintf(stderr,
                        "IRIX MIPS interpreter: unsupported pc=%08x insn=%08x op=%02x fn=%02x\n",
                        pc, insn, op, fn);
                rt->trapped = 1;
                rt->trap_pc = pc;
                rt->fault_addr = 0;
                rt->trap_code = 0xeeee4001u;
                return -1;
            }
            pc += 4u;
            continue;
        }

        if (op == 0u && fn == 0x08u) {
            target = (IrixGuestAddr)(uint32_t)cpu->gpr[rs];
            if ((pc >= 0x0f627000u && pc < 0x0f630000u) ||
                (target >= 0x0f627000u && target < 0x0f630000u)) {
                fprintf(stderr,
                        "IRIX MIPS interp JR: pc=%08x rs=%u target=%08x "
                        "ra=%08x s0=%08x s1=%08x t9=%08x\n",
                        pc, rs, target,
                        (uint32_t)cpu->gpr[31],
                        (uint32_t)cpu->gpr[16],
                        (uint32_t)cpu->gpr[17],
                        (uint32_t)cpu->gpr[25]);
            }
            if (exec_delay(rt, cpu, pc + 4u) != 0)
                return -1;
            if (rs == 31u) {
                IrixGuestAddr return_gp =
                    irix_guest_gp_for_address(target);

                /*
                 * Every jr $ra is an ABI return boundary, even when it is only
                 * a nested helper return inside this interpreter invocation.
                 *
                 * In the hybrid translated/interpreted runtime the caller may
                 * live in a different ELF/GP domain. Do not wait for
                 * target == outer_ra before repairing $gp: a nested
                 * irix_mips_interp_call can legitimately return from libXt
                 * directly into an interior ViewKit continuation such as:
                 *
                 *   0x0f62cb5c -> 0x0d6352c0
                 *
                 * That transition must switch libXt's 0x0f69eef0 GP back to
                 * ViewKit's 0x0d6cf058 before ANY instruction at the target
                 * executes, regardless of which interpreter frame considers
                 * the target its outer return address.
                 */
                if (!return_gp &&
                    target >= 0x0d600000u && target < 0x0d700000u)
                    return_gp = 0x0d6cf058u;

                if (return_gp &&
                    (uint32_t)cpu->gpr[28] != return_gp) {
                    fprintf(stderr,
                            "IRIX MIPS interp JR GP domain: pc=%08x target=%08x "
                            "gp %08x -> %08x outer_ra=%08x\n",
                            pc, target, (uint32_t)cpu->gpr[28],
                            return_gp, outer_ra);
                    cpu->gpr[28] =
                        (uint64_t)(int64_t)(int32_t)return_gp;
                }

                /*
                 * XmGetXmScreen can be re-entered at recovered interior
                 * continuations while one larger interpreter invocation is
                 * still active.  The dispatcher never observes that edge.
                 * Repair the returned XmScreen private block at the actual
                 * jr $ra boundary before _XmGetPixmap executes.
                 */
                if (target >= 0x0b2a80acu && target < 0x0b2a8388u)
                    irix_guest_repair_xm_screen_private(
                        rt, (IrixGuestAddr)(uint32_t)cpu->gpr[2]);

                /*
                 * jr $ra is not necessarily a return from this interpreter
                 * invocation. A function being interpreted may call another
                 * local/static helper, which overwrites $ra; that helper then
                 * returns to an interior PC of the outer function.
                 *
                 * Only leave irix_mips_interp_call when the jump target is the
                 * return address that was live on entry. Otherwise continue
                 * interpreting at the now-correct GP domain.
                 */
                if (target == outer_ra) {
                    fprintf(stderr,
                            "IRIX MIPS interp OUTER return: pc=%08x target=%08x "
                            "gp=%08x owner_gp=%08x\n",
                            pc, target, (uint32_t)cpu->gpr[28], return_gp);
                    cpu->pc = target;
                    rt->current_pc = target;
                    cpu->gpr[0] = 0;
                    return 0;
                }

                pc = target;
                cpu->pc = pc;
                rt->current_pc = pc;
                cpu->gpr[0] = 0;
                continue;
            }
            if (rs == 25u) {
                cpu->pc = pc;
                if (irix_guest_call(rt, cpu, target) != 0)
                    return -1;
                return 0;
            }
            pc = target;
            continue;
        }

        if (op == 0u && fn == 0x09u) {
            target = (IrixGuestAddr)(uint32_t)cpu->gpr[rs];
            if (rd)
                cpu->gpr[rd] = sx32(pc + 8u);
            if (exec_delay(rt, cpu, pc + 4u) != 0)
                return -1;
            cpu->pc = pc;
            if (irix_guest_call(rt, cpu, target) != 0)
                return -1;
            if (!rd)
                return 0;
            pc += 8u;
            continue;
        }

        if (op == 0x02u || op == 0x03u) {
            target = ((pc + 4u) & 0xf0000000u) | ((insn & 0x03ffffffu) << 2);
            if (op == 0x03u) {
                if ((pc >= 0x0f627000u && pc < 0x0f630000u) ||
                    (target >= 0x0f627000u && target < 0x0f630000u)) {
                    fprintf(stderr,
                            "IRIX MIPS interp JAL: pc=%08x target=%08x "
                            "ra(new)=%08x s0=%08x s1=%08x t9=%08x\n",
                            pc, target, pc + 8u,
                            (uint32_t)cpu->gpr[16],
                            (uint32_t)cpu->gpr[17],
                            (uint32_t)cpu->gpr[25]);
                }
                cpu->gpr[31] = sx32(pc + 8u);
                if (exec_delay(rt, cpu, pc + 4u) != 0)
                    return -1;
                cpu->pc = pc;
                if (irix_guest_call(rt, cpu, target) != 0)
                    return -1;
                pc += 8u;
                continue;
            }
            if (exec_delay(rt, cpu, pc + 4u) != 0)
                return -1;
            /*
             * A direct J is often a local intra-function branch, but SGI
             * import/tail veneers also use it.  If the destination is the
             * exact start of a registered function/native bridge, hand it
             * back to the dispatcher and tail-return.  Otherwise keep
             * interpreting locally.
             */
            {
                IrixGuestFunctionEntry *te =
                    irix_guest_find_function(rt, target);
                if ((pc >= 0x0f627000u && pc < 0x0f630000u) ||
                    (target >= 0x0f627000u && target < 0x0f630000u)) {
                    fprintf(stderr,
                            "IRIX MIPS interp J: pc=%08x target=%08x "
                            "entry=%08x:%s ra=%08x s0=%08x s1=%08x t9=%08x\n",
                            pc, target,
                            te ? te->address : 0u,
                            (te && te->name) ? te->name : "?",
                            (uint32_t)cpu->gpr[31],
                            (uint32_t)cpu->gpr[16],
                            (uint32_t)cpu->gpr[17],
                            (uint32_t)cpu->gpr[25]);
                }
                if (te && te->address == target && target != address) {
                    cpu->pc = pc;
                    if (irix_guest_call(rt, cpu, target) != 0)
                        return -1;
                    return 0;
                }
            }
            pc = target;
            continue;
        }

        if (op == 0x11u && rs == 0x08u) {
            int tf = rtno & 1u;
            likely = (rtno & 2u) != 0;
            taken = ((cpu->fcr31 & (1u << 23)) != 0) == tf;
            target = pc + 4u + ((int32_t)(int16_t)(insn & 0xffffu) << 2);
        } else if (op == 0x01u) {
            int32_t v = (int32_t)(uint32_t)cpu->gpr[rs];
            switch (rtno) {
            case 0x00: taken = v < 0; break;
            case 0x01: taken = v >= 0; break;
            case 0x02: taken = v < 0; likely = 1; break;
            case 0x03: taken = v >= 0; likely = 1; break;
            case 0x10: taken = v < 0; link = 1; break;
            case 0x11: taken = v >= 0; link = 1; break;
            case 0x12: taken = v < 0; link = 1; likely = 1; break;
            case 0x13: taken = v >= 0; link = 1; likely = 1; break;
            default:
                fprintf(stderr,"IRIX MIPS interpreter: unsupported REGIMM rt=%02x pc=%08x\n",rtno,pc);
                return -1;
            }
            target = pc + 4u + ((int32_t)(int16_t)(insn & 0xffffu) << 2);
            if (link)
                cpu->gpr[31] = sx32(pc + 8u);
        } else {
            int64_t sv = (int64_t)cpu->gpr[rs];
            switch (op) {
            case 0x04: taken = cpu->gpr[rs] == cpu->gpr[rtno]; break;
            case 0x05: taken = cpu->gpr[rs] != cpu->gpr[rtno]; break;
            case 0x06: taken = sv <= 0; break;
            case 0x07: taken = sv > 0; break;
            case 0x14: taken = cpu->gpr[rs] == cpu->gpr[rtno]; likely = 1; break;
            case 0x15: taken = cpu->gpr[rs] != cpu->gpr[rtno]; likely = 1; break;
            case 0x16: taken = sv <= 0; likely = 1; break;
            case 0x17: taken = sv > 0; likely = 1; break;
            default:
                fprintf(stderr,"IRIX MIPS interpreter: unsupported branch pc=%08x insn=%08x\n",pc,insn);
                return -1;
            }
            target = pc + 4u + ((int32_t)(int16_t)(insn & 0xffffu) << 2);
        }

        if (!(likely && !taken)) {
            if (exec_delay(rt, cpu, pc + 4u) != 0)
                return -1;
        }
        pc = taken ? target : next;
    }

    if (steps >= max_steps) {
        fprintf(stderr, "IRIX MIPS interpreter: step limit at pc=%08x\n", pc);
        rt->trapped = 1;
        rt->trap_pc = pc;
        rt->trap_code = 0xeeee4002u;
    }
    return rt->trapped ? -1 : 0;
}
