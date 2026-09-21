#!/usr/bin/env python3
"""Translate callable functions in an IRIX N32 executable or DSO."""
import argparse, contextlib, io, json, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import translate_n32_regimm as t

SHT_SYMTAB=2; SHT_DYNSYM=11; SHT_NOBITS=8; STT_FUNC=2
SHF_WRITE=0x1; SHF_ALLOC=0x2; SHF_EXECINSTR=0x4

def ident(s): return re.sub(r'[^A-Za-z0-9_]', '_', s)

BATCH_FUNCTIONS=128
BATCH_PREAMBLE='#include "IrixGuest.h"\n#include <math.h>\n#include <stdio.h>\n\nenum { IRIX_PENDING_NONE, IRIX_PENDING_JUMP, IRIX_PENDING_CALL, IRIX_PENDING_RETURN, IRIX_PENDING_LINK };\n\n'

def function_body(text):
    marker='void irix_recomp_'
    pos=text.find(marker)
    if pos<0:
        raise ValueError('generated function body marker not found')
    return text[pos:].rstrip()+'\n\n'

def write_if_changed(path, text):
    path=Path(path)
    if path.is_file() and path.read_text()==text:
        return False
    path.write_text(text)
    return True

def raw_function_symbols(path):
    p=Path(path).read_bytes(); h=struct.unpack_from('>16sHHIIIIIHHHHHH',p,0)
    shoff,esz,num=h[6],h[11],h[12]
    secs=[struct.unpack_from('>IIIIIIIIII',p,shoff+i*esz) for i in range(num)]
    rows=[]
    for s in secs:
        if s[1] not in (SHT_SYMTAB,SHT_DYNSYM) or not s[9]: continue
        ss=secs[s[6]]; strings=p[ss[4]:ss[4]+ss[5]]
        for i in range(s[5]//s[9]):
            n,v,z,info,other,sh=struct.unpack_from('>IIIBBH',p,s[4]+i*s[9])
            if (info&15)!=STT_FUNC: continue
            e=strings.find(b'\0',n); name=strings[n:e].decode('ascii','replace') if n else ''
            if name: rows.append(dict(name=name,address=v,size=z,shndx=sh,bind=info>>4))
    return rows

def emit_explicit(path, name, start, size, c_prefix):
    """Emit one desktop/runtime function from an explicit address range.

    This intentionally lives here instead of changing translate_n32.py so
    metadata recovery cannot invalidate the static core translator.
    """
    elf=t.Elf32BE(str(path))
    words=elf.words(start,size)
    decoded=[]
    for i,word in enumerate(words):
        addr=start+i*4
        lines,control=t.decode(addr,word)
        decoded.append((addr,word,lines,control))
    fn='irix_recomp_%s%s'%(t.c_name(c_prefix),t.c_name(name))
    print('/* Generated from %s: %s @ 0x%08x, 0x%x bytes (explicit range). */'%(path,name,start,size))
    print('#include "IrixGuest.h"'); print('#include <math.h>'); print('')
    print('enum { IRIX_PENDING_NONE, IRIX_PENDING_JUMP, IRIX_PENDING_CALL, IRIX_PENDING_RETURN, IRIX_PENDING_LINK };'); print('')
    print('void %s(IrixGuestRuntime *rt, IrixGuestCpu *cpu)'%fn); print('{')
    print('    IrixGuestAddr pc = cpu->pc;')
    print('    IrixGuestAddr pending_target = 0;')
    print('    IrixGuestAddr pending_return = 0;')
    print('    int pending_kind = IRIX_PENDING_NONE;')
    print('    int old_pending;')
    print('    IrixGuestAddr old_target;')
    print('    IrixGuestAddr old_return;'); print('')
    print('    for (;;) {')
    print('        old_pending = pending_kind;')
    print('        old_target = pending_target;')
    print('        old_return = pending_return;')
    print('        pending_kind = IRIX_PENDING_NONE;')
    print('        cpu->pc = pc;')
    print('        rt->current_pc = pc;'); print('')
    print('        switch (pc) {')
    for addr,word,lines,_ in decoded:
        print('        case 0x%08xu: /* 0x%08x */'%(addr,word))
        if addr in (0x0983931c, 0x098402b4):
            tag = 'DT_INIT' if addr == 0x0983931c else '__cplinit'
            print('            fprintf(stderr, "IRIX libimdUtil init probe: %s pc=0x%08x sp=%%08x gp=%%08x ra=%%08x kNull.master=%%08x kEmpty.master=%%08x\\n", (uint32_t)cpu->gpr[29], (uint32_t)cpu->gpr[28], (uint32_t)cpu->gpr[31], irix_guest_read32be(rt, 0x09852dccu), irix_guest_read32be(rt, 0x09852dd4u));' % (tag, addr))
        if addr in (0x098402d8, 0x098402fc, 0x09840310):
            print('            fprintf(stderr, "IRIX libimdUtil cplinit step: pc=0x%08x a0=%%08x a1=%%08x t9=%%08x kNull.master=%%08x kEmpty.master=%%08x\\n", (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[25], irix_guest_read32be(rt, 0x09852dccu), irix_guest_read32be(rt, 0x09852dd4u));' % addr)
        if addr in (0x098402ec, 0x098402f4, 0x098402f8):
            print('            fprintf(stderr, "IRIX libimdUtil kEmpty call probe: pc=0x%08x a0=%%08x a1=%%08x t9=%%08x gp=%%08x pending target context\\n", (uint32_t)cpu->gpr[4], (uint32_t)cpu->gpr[5], (uint32_t)cpu->gpr[25], (uint32_t)cpu->gpr[28]);' % addr)
        for line in lines: print('            '+line)
        print('            break;')
    print('        default:'); print('            return;'); print('        }'); print('')
    print('        if (rt->trapped)'); print('            return;'); print('        cpu->gpr[0] = 0;')
    print('        if (old_pending == IRIX_PENDING_RETURN)')
    print('            return;')
    print('        if (old_pending == IRIX_PENDING_CALL) {')
    print('            if (irix_guest_call(rt, cpu, old_target) != 0)')
    print('                return;')
    print('            pc = old_return;')
    print('            continue;')
    print('        }')
    print('        if (old_pending == IRIX_PENDING_LINK) {')
    print('            if (old_target >= 0x%08xu && old_target < 0x%08xu) {'%(start,start+size))
    print('                pc = old_target;')
    print('                continue;')
    print('            }')
    print('            if (irix_guest_call(rt, cpu, old_target) != 0)')
    print('                return;')
    print('            pc = old_return;')
    print('            continue;')
    print('        }')
    print('        if (old_pending == IRIX_PENDING_JUMP) {')
    print('            if (old_target < 0x%08xu || old_target >= 0x%08xu) {'%(start,start+size))
    print('                if (irix_guest_call(rt, cpu, old_target) != 0)')
    print('                    return;')
    print('                return;')
    print('            }')
    print('            pc = old_target;')
    print('            continue;')
    print('        }')
    print('        pc += 4;')
    print('    }')
    print('}')

def zero_size_function_ranges(elf, symbol_rows):
    """Recover named zero-sized STT_FUNC symbols conservatively."""
    out=[]
    defined=[s for s in symbol_rows if s['shndx']!=0 and s['address']]
    for sec_index,sec in enumerate(elf.sections):
        if not (sec['flags']&SHF_EXECINSTR) or not sec['size']:
            continue
        lo=sec['addr']; hi=lo+sec['size']
        rows=[s for s in defined if s['shndx']==sec_index and lo<=s['address']<hi]
        if not rows: continue
        starts=sorted(set(s['address'] for s in rows) | {hi})
        next_boundary={starts[i]:starts[i+1] for i in range(len(starts)-1)}
        for s in rows:
            if s['size']!=0: continue
            end=next_boundary.get(s['address'])
            if end is None: continue
            size=end-s['address']
            if size<=0 or size%4: continue
            out.append((s['name'],s['address'],size,sec_index))
    return out

def debug_frame_function_ranges(elf, path, symbol_rows):
    """Recover exact procedure ranges from DWARF .debug_frame FDEs.

    IRIX debug rld carries useful FDEs even when local function symbols are
    absent.  Unlike heuristic boundary recovery, an FDE explicitly describes
    the complete procedure extent.  The N32 files handled here use the normal
    32-bit DWARF2 .debug_frame layout: length, CIE pointer, initial PC, range.
    """
    p=Path(path).read_bytes()
    sec=next((s for s in elf.sections if s.get('name')=='.debug_frame'),None)
    if not sec or sec['size']<12:
        return []

    exec_sections=[]
    for i,s in enumerate(elf.sections):
        if (s['flags']&SHF_EXECINSTR) and s['size']:
            exec_sections.append((i,s['addr'],s['addr']+s['size']))

    named_by_addr={}
    for s in symbol_rows:
        if s['shndx']!=0 and s['address']:
            named_by_addr.setdefault(s['address'],s['name'])

    out=[]; off=sec['offset']; end=off+sec['size']
    while off+4<=end:
        length=struct.unpack_from('>I',p,off)[0]
        if length==0:
            off+=4
            continue
        entry_end=off+4+length
        if length==0xffffffff or entry_end>end or off+12>entry_end:
            break
        cie=struct.unpack_from('>I',p,off+4)[0]
        if cie!=0xffffffff:
            start,size=struct.unpack_from('>II',p,off+8)
            if size and size%4==0:
                for sec_index,lo,hi in exec_sections:
                    if lo<=start<hi and start+size<=hi:
                        name=named_by_addr.get(start,'__debug_frame_%08x'%start)
                        out.append((name,start,size,sec_index))
                        break
        off=entry_end
    return out

def pdr_function_ranges(elf, path, symbol_rows):
    """Recover procedure starts/ranges from the MIPS ELF .pdr section."""
    p=Path(path).read_bytes()
    pdr=next((s for s in elf.sections if s.get('name')=='.pdr'),None)
    if not pdr or not pdr['size'] or pdr['size']%32:
        return []

    starts=[]
    for off in range(pdr['offset'],pdr['offset']+pdr['size'],32):
        if off+32>len(p): break
        addr=struct.unpack_from('>I',p,off)[0]
        if addr and addr!=0xffffffff:
            starts.append(addr)
    starts=sorted(set(starts))

    named_by_addr={}
    known_starts=set()
    for s in symbol_rows:
        if s['shndx']!=0 and s['address']:
            named_by_addr.setdefault(s['address'],s['name'])
            known_starts.add(s['address'])

    out=[]
    for sec_index,sec in enumerate(elf.sections):
        if not (sec['flags']&SHF_EXECINSTR) or not sec['size']:
            continue
        lo=sec['addr']; hi=lo+sec['size']
        pstarts=[a for a in starts if lo<=a<hi]
        if not pstarts: continue
        boundaries=sorted(set(pstarts+[a for a in known_starts if lo<=a<hi]+[hi]))
        next_boundary={boundaries[i]:boundaries[i+1] for i in range(len(boundaries)-1)}
        for addr in pstarts:
            end=next_boundary.get(addr,hi)
            size=end-addr
            if size<=0 or size%4: continue
            name=named_by_addr.get(addr,'__pdr_%08x'%addr)
            out.append((name,addr,size,sec_index))
    return out

def plausible_mips_entry(elf, addr):
    """Require a strong function-entry signature for pointer-derived starts.

    Besides the ordinary addiu/daddiu $sp,$sp,-N prologue, IRIX rld contains
    callback helpers that perform a small read before their stack adjustment.
    Observed forms include:

        lbu     $at,off($gp)
        beq/bne $at,$zero,...
        addiu   $sp,$sp,-N       # branch delay slot

    and:

        lw      $at,off($a0)
        addiu   $sp,$sp,-N

    These routines are passed indirectly (for example through foreach_obj), so
    direct-link recovery cannot discover them.  Accept only these tightly
    constrained entry shapes rather than treating arbitrary interior loads as
    callable starts.
    """
    try:
        words=elf.words(addr,12)
    except Exception:
        try:
            words=elf.words(addr,4)
        except Exception:
            return False

    w=words[0]
    op=(w>>26)&0x3f; rs=(w>>21)&0x1f; rt=(w>>16)&0x1f; imm=w&0xffff
    if op in (0x09,0x19) and rs==29 and rt==29 and (imm&0x8000)!=0 and imm!=0xffff:
        return True

    if len(words) >= 2:
        w0,w1=words[:2]
        op0=(w0>>26)&0x3f; rs0=(w0>>21)&0x1f; rt0=(w0>>16)&0x1f
        op1=(w1>>26)&0x3f; rs1=(w1>>21)&0x1f; rt1=(w1>>16)&0x1f
        imm1=w1&0xffff
        arg_load=(op0==0x23 and 4<=rs0<=11 and rt0!=0)
        stack_after_load=(op1 in (0x09,0x19) and rs1==29 and rt1==29 and
                          (imm1&0x8000)!=0 and imm1!=0xffff)
        if arg_load and stack_after_load:
            return True

    if len(words) >= 3:
        w0,w1,w2=words[:3]
        op0=(w0>>26)&0x3f; rs0=(w0>>21)&0x1f; rt0=(w0>>16)&0x1f
        op1=(w1>>26)&0x3f; rs1=(w1>>21)&0x1f; rt1=(w1>>16)&0x1f
        op2=(w2>>26)&0x3f; rs2=(w2>>21)&0x1f; rt2=(w2>>16)&0x1f
        imm2=w2&0xffff
        gp_guard=(op0 in (0x20,0x24) and rs0==28 and rt0!=0)
        guard_branch=(op1 in (0x04,0x05) and
                      ((rs1==rt0 and rt1==0) or (rt1==rt0 and rs1==0)))
        stack_delay=(op2 in (0x09,0x19) and rs2==29 and rt2==29 and
                     (imm2&0x8000)!=0 and imm2!=0xffff)
        if gp_guard and guard_branch and stack_delay:
            return True

    return False

def validated_recovered_size(elf, start, provisional_size):
    """Validate or trim a metadata-recovered function extent.

    Recovered metadata can prove a callable start without proving its exact
    size. Decode the provisional range. If decoding encounters non-code,
    accept only a complete function ending at the last `jr ra` plus its delay
    slot before the failure. Otherwise reject the recovered candidate.
    """
    try:
        words=elf.words(start,provisional_size)
    except Exception:
        return 0

    last_return_end=0
    for i,word in enumerate(words):
        addr=start+i*4
        try:
            t.decode(addr,word)
        except Exception:
            if last_return_end and last_return_end<=addr:
                size=last_return_end-start
                if size>0 and size%4==0:
                    try:
                        trimmed=elf.words(start,size)
                        for j,w in enumerate(trimmed):
                            t.decode(start+j*4,w)
                        return size
                    except Exception:
                        return 0
            return 0
        if word==0x03e00008 and i+1<len(words):
            last_return_end=addr+8

    return provisional_size

def data_pointer_function_ranges(elf, path, known_starts):
    """Recover hidden callable starts materialized as data/GOT pointers."""
    p=Path(path).read_bytes()
    candidates=set()
    exec_sections=[]
    for i,sec in enumerate(elf.sections):
        if (sec['flags']&SHF_EXECINSTR) and sec['size']:
            exec_sections.append((i,sec['addr'],sec['addr']+sec['size']))

    def exec_section_for(addr):
        for i,lo,hi in exec_sections:
            if lo<=addr<hi: return i,lo,hi
        return None

    for sec in elf.sections:
        if not (sec['flags']&SHF_ALLOC) or (sec['flags']&SHF_EXECINSTR):
            continue
        if sec['type']==SHT_NOBITS or sec['size']<4:
            continue
        begin=sec['offset']; end=min(begin+sec['size'],len(p))
        for off in range(begin,end-3,4):
            addr=struct.unpack_from('>I',p,off)[0]
            if not addr or addr in known_starts:
                continue
            if exec_section_for(addr) is None:
                continue
            # A word in allocated non-code data that exactly names an
            # executable address is itself strong entry evidence.  Do not
            # require a conventional prologue here: SGI callback veneers and
            # ABI stubs commonly begin with gp-relative loads or an immediate
            # jalr rather than stack setup.
            candidates.add(addr)

    out=[]
    for sec_index,lo,hi in exec_sections:
        local=sorted(a for a in candidates if lo<=a<hi)
        if not local: continue
        boundaries=sorted(set(local+[a for a in known_starts if lo<=a<hi]+[hi]))
        next_boundary={boundaries[i]:boundaries[i+1] for i in range(len(boundaries)-1)}
        for addr in local:
            end=next_boundary.get(addr,hi); size=end-addr
            if size<=0 or size%4: continue
            size=validated_recovered_size(elf,addr,size)
            if not size: continue
            out.append(('__ptr_%08x'%addr,addr,size,sec_index))
    return out

def _direct_link_target(addr, word):
    op=(word>>26)&0x3f
    if op==0x03:
        return (((addr+4)&0xf0000000)|((word&0x03ffffff)<<2))&0xffffffff
    if op==0x01:
        rt=(word>>16)&0x1f
        if rt in (0x10,0x11,0x12,0x13):
            imm=word&0xffff
            if imm&0x8000: imm-=0x10000
            return (addr+4+(imm<<2))&0xffffffff
    return None

def _reachable_code_extent(elf, start, sec_lo, sec_hi, barriers):
    """Find a conservative contiguous extent for one proven callable start.

    Walk direct intra-function control flow, include every MIPS delay slot, and
    treat direct/REGIMM link instructions as calls that resume at pc+8. Reaching
    another proven function start is treated as a tail-call boundary.
    """
    todo=[start]; seen=set(); max_end=start
    while todo:
        addr=todo.pop()
        if addr in seen or addr<sec_lo or addr>=sec_hi:
            continue
        if addr!=start and addr in barriers:
            continue
        try:
            word=elf.words(addr,4)[0]
        except Exception:
            return 0
        seen.add(addr); max_end=max(max_end,addr+4)
        op=(word>>26)&0x3f; rs=(word>>21)&0x1f; rt=(word>>16)&0x1f; fn=word&0x3f
        imm=word&0xffff
        if imm&0x8000: imm-=0x10000

        def include_delay():
            nonlocal max_end
            d=addr+4
            if sec_lo<=d<sec_hi:
                seen.add(d); max_end=max(max_end,d+4)

        if op==0x00 and fn in (0x08,0x09):
            include_delay()
            if fn==0x09:
                todo.append(addr+8)
            continue
        if op==0x02:
            include_delay()
            dst=(((addr+4)&0xf0000000)|((word&0x03ffffff)<<2))&0xffffffff
            todo.append(dst)
            continue
        if op==0x03:
            include_delay(); todo.append(addr+8); continue
        if op==0x01:
            include_delay()
            dst=(addr+4+(imm<<2))&0xffffffff
            if rt in (0x10,0x11,0x12,0x13):
                todo.append(addr+8)
            else:
                todo.append(dst); todo.append(addr+8)
            continue
        if op in (0x04,0x05,0x06,0x07,0x14,0x15):
            include_delay()
            dst=(addr+4+(imm<<2))&0xffffffff
            unconditional=(op==0x04 and rs==0 and rt==0)
            todo.append(dst)
            if not unconditional:
                todo.append(addr+8)
            continue
        if op==0x11 and rs==0x08:
            include_delay()
            dst=(addr+4+(imm<<2))&0xffffffff
            todo.append(dst); todo.append(addr+8)
            continue
        todo.append(addr+4)
    size=max_end-start
    return size if size>0 and size%4==0 else 0

def code_link_function_ranges(elf, known_starts):
    """Recover hidden callees referenced by direct link instructions."""
    exec_sections=[]
    for i,sec in enumerate(elf.sections):
        if (sec['flags']&SHF_EXECINSTR) and sec['size']:
            exec_sections.append((i,sec['addr'],sec['addr']+sec['size']))

    def exec_section_for(addr):
        for item in exec_sections:
            if item[1]<=addr<item[2]: return item
        return None

    candidates=set()
    for sec_index,lo,hi in exec_sections:
        try:
            words=elf.words(lo,hi-lo)
        except Exception:
            continue
        for i,word in enumerate(words):
            addr=lo+i*4
            target=_direct_link_target(addr,word)
            if target is None or target in known_starts:
                continue
            if exec_section_for(target) is not None:
                candidates.add(target)

    barriers=set(known_starts)|candidates
    out=[]
    for addr in sorted(candidates):
        sec=exec_section_for(addr)
        if sec is None: continue
        sec_index,lo,hi=sec
        size=_reachable_code_extent(elf,addr,lo,hi,barriers)
        if not size: continue
        size=validated_recovered_size(elf,addr,size)
        if not size: continue
        out.append(('__link_%08x'%addr,addr,size,sec_index))
    return out

def _stack_frame_setup(word):
    op=(word>>26)&0x3f; rs=(word>>21)&0x1f; rt=(word>>16)&0x1f
    imm=word&0xffff
    if imm&0x8000: imm-=0x10000
    return op in (0x09,0x19) and rs==29 and rt==29 and imm<0

def _simple_entry_prefix(word):
    op=(word>>26)&0x3f; rs=(word>>21)&0x1f; rt=(word>>16)&0x1f
    return op in (0x09,0x0d) and rs==0 and 4<=rt<=11

def _nonfallthrough_control(word):
    op=(word>>26)&0x3f; rs=(word>>21)&0x1f; rt=(word>>16)&0x1f; fn=word&0x3f
    if op==0x02:
        return True
    if op==0x00 and fn==0x08:
        return True
    return op==0x04 and rs==0 and rt==0

def _return_control(word):
    return word==0x03e00008

def _extent_has_return(elf, start, size):
    try:
        return 0x03e00008 in elf.words(start,size)
    except Exception:
        return False

def boundary_entry_function_ranges(elf, known_starts):
    """Recover callback-only hidden entries at strong code boundaries.

    SGI/EDG sometimes places separately callable helpers inside a larger
    symbol's nominal address range and passes their addresses indirectly to Xt.
    Require a non-fallthrough predecessor plus its delay slot. Normal helpers
    still require a real negative stack-frame setup in the first two
    instructions. A block immediately following an exact `jr ra` may also be
    accepted as a leaf helper, provided its recovered extent contains its own
    return. This covers callback comparators that legitimately need no frame.
    """
    exec_sections=[]
    for i,sec in enumerate(elf.sections):
        if (sec['flags']&SHF_EXECINSTR) and sec['size']:
            exec_sections.append((i,sec['addr'],sec['addr']+sec['size']))

    def exec_section_for(addr):
        for item in exec_sections:
            if item[1]<=addr<item[2]: return item
        return None

    candidates=set()
    for sec_index,lo,hi in exec_sections:
        if hi-lo<16: continue
        try:
            words=elf.words(lo,hi-lo)
        except Exception:
            continue
        for i in range(2,len(words)-1):
            addr=lo+i*4
            if addr in known_starts:
                continue
            predecessor=words[i-2]
            if not _nonfallthrough_control(predecessor):
                continue
            w0=words[i]; w1=words[i+1]
            framed=_stack_frame_setup(w0) or (_simple_entry_prefix(w0) and _stack_frame_setup(w1))
            guarded=plausible_mips_entry(elf,addr)
            leaf_after_return=_return_control(predecessor) and w0!=0
            if framed or guarded or leaf_after_return:
                candidates.add(addr)

    barriers=set(known_starts)|candidates
    out=[]
    for addr in sorted(candidates):
        sec=exec_section_for(addr)
        if sec is None: continue
        sec_index,lo,hi=sec
        size=_reachable_code_extent(elf,addr,lo,hi,barriers)
        if not size: continue

        # Some rld helpers branch to a tiny shared epilogue immediately before
        # the next recovered function boundary.  The reachability walk may stop
        # at the epilogue entry when that address is also discovered as a
        # callable boundary elsewhere.  If the gap to the next barrier is tiny,
        # decode it as part of this function and keep it when it ends in a
        # normal jr $ra + delay-slot return sequence.
        # Do not use every tentative boundary candidate here. A weak
        # candidate can later be rejected, yet still split a valid predecessor
        # during reachability. That is exactly what happened with rld:
        # 0x0fb66878 is a shared return epilogue, was tentatively considered a
        # boundary, then dropped, leaving 0x0fb6685c permanently truncated.
        # For this tiny-epilogue repair, only a previously proven start is a
        # hard boundary.
        provisional_end=addr+size
        next_barrier=min((b for b in known_starts if b>provisional_end),default=hi)
        if provisional_end<next_barrier and next_barrier-provisional_end<=16:
            try:
                extra=elf.words(provisional_end,next_barrier-provisional_end)
                # A tiny suffix ending in jr $ra plus its delay slot is an
                # epilogue, not a separate function.  Extend first and let the
                # normal recovered-range validator below decide whether the
                # complete joined range is decodable.  Requiring the validator
                # to return the full size here was circular and left rld's
                # 0x0fb66878 epilogue outside the 0x0fb6685c callback.
                if len(extra)>=2 and extra[-2]==0x03e00008:
                    size=next_barrier-addr
            except Exception:
                pass

        size=validated_recovered_size(elf,addr,size)
        if not size: continue
        # Strong prologues can legitimately tail-transfer into the next known
        # entry and therefore contain no local jr $ra.  The guarded rld
        # callbacks are one such case.  Keep the return requirement only for
        # the weaker "block after a return" heuristic.
        strong_entry=plausible_mips_entry(elf,addr)
        if not strong_entry and not _extent_has_return(elf,addr,size): continue
        out.append(('__boundary_%08x'%addr,addr,size,sec_index))
    return out

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('binary'); ap.add_argument('-o','--output',required=True); ap.add_argument('--prefix')
    a=ap.parse_args(); path=Path(a.binary); out=Path(a.output); prefix=a.prefix or path.stem; out.mkdir(parents=True,exist_ok=True)
    elf=t.Elf32BE(str(path)); defs=[]
    for n,addr,size,sh in elf.symbols():
        if size:
            # Some SGI dynamic symbols have a nominal st_size that ends before
            # the function's real sequential body.  __exec_rld is one example:
            # its symbol range stops after two instructions, immediately before
            # an ordinary stack-frame prologue.  Feeding that truncated range
            # to the translator turns fallthrough into a fake guest call.
            #
            # Only repair the very strong form: the symbol ends in executable
            # code with no control transfer, and the next instruction begins a
            # conventional negative stack-frame adjustment.  Extend from that
            # point with the same conservative CFG walker used for recovered
            # hidden entries.  Real returns/jumps at the nominal end are left
            # untouched.
            repaired_size=size
            end=(addr+size)&0xffffffff
            try:
                last=elf.words(end-4,4)[0]
                nxt=elf.words(end,4)[0]
                op=(last>>26)&0x3f; fn=last&0x3f
                terminal=(op in (0x02,0x03) or
                          (op==0x00 and fn in (0x08,0x09)))
                if not terminal and _stack_frame_setup(nxt):
                    sec=next((s for s in elf.sections
                              if (s['flags']&SHF_EXECINSTR) and
                                 s['addr']<=addr<s['addr']+s['size'] and
                                 end<s['addr']+s['size']),None)
                    if sec is not None:
                        tail=_reachable_code_extent(
                            elf,end,sec['addr'],sec['addr']+sec['size'],set())
                        if tail:
                            repaired_size=size+tail
            except Exception:
                pass
            defs.append((n,addr,repaired_size,sh,'symbol'))

    raw_syms=raw_function_symbols(path)
    symbol_addrs={addr for _,addr,_,_,_ in defs}
    # A real nonzero-size ELF function symbol owns its interior.  Exact
    # executable words materialized in data can prove hidden callable entries,
    # but they must not split an ordinary named function merely because some
    # data happens to contain the address of one of its internal instructions.
    # This matters for libc routines such as memcpy, whose internal labels may
    # be present in prebound/GOT data while execution must still enter through
    # the enclosing function and preserve its setup state.
    symbol_ranges=[(addr,addr+size) for _,addr,size,_,kind in defs
                   if kind=='symbol' and size>0]

    def covered_by_symbol(addr):
        return any(lo < addr < hi for lo,hi in symbol_ranges)

    zero_defs=[]
    for n,addr,size,sh in zero_size_function_ranges(elf,raw_syms):
        if addr not in symbol_addrs:
            zero_defs.append((n,addr,size,sh,'zero_size'))

    # DWARF FDEs are authoritative for otherwise-unrecovered procedures.
    # Do not replace a real ELF function symbol at the same start: some IRIX
    # DSOs have broad FDEs around prebound/linker entry regions, and replacing
    # those symbol ranges changes the import-stub ownership seen by the binding
    # generator.  The FDE still suppresses heuristic interior entries when its
    # start is not already represented by a symbol.
    recovered_before_frames=symbol_addrs|{addr for _,addr,_,_,_ in zero_defs}
    frame_defs=[]
    for n,addr,size,sh in debug_frame_function_ranges(elf,path,raw_syms):
        if addr not in recovered_before_frames:
            frame_defs.append((n,addr,size,sh,'debug_frame'))
    frame_ranges=[(addr,addr+size) for _,addr,size,_,_ in frame_defs]

    def covered_by_frame(addr):
        return any(lo < addr < hi for lo,hi in frame_ranges)

    recovered_addrs={addr for _,addr,_,_,_ in defs+zero_defs+frame_defs}

    # Direct link targets are hard callable-entry evidence and must be
    # recovered before weaker metadata passes.  In particular SGI's
    # __exec_rld uses a branch-and-link into an adjacent hidden body; treating
    # that target as an ordinary continuation leaves the BAL target absent
    # from the registry and traps immediately at process startup.
    early_link_defs=[]
    for n,addr,size,sh in code_link_function_ranges(elf,recovered_addrs):
        if addr not in recovered_addrs:
            # A direct JAL/BAL target is hard executable-entry evidence.  Do
            # not let a broad DWARF FDE suppress it merely because the target
            # lies inside the FDE's nominal procedure range.  rld's
            # __exec_rld links directly to such an adjacent hidden entry.
            early_link_defs.append((n,addr,size,sh,'code_link'))
    recovered_addrs|={addr for _,addr,_,_,_ in early_link_defs}

    pdr_defs=[]
    for n,addr,size,sh in pdr_function_ranges(elf,path,raw_syms):
        if addr not in recovered_addrs and not covered_by_frame(addr):
            pdr_defs.append((n,addr,size,sh,'pdr'))

    recovered_addrs|={addr for _,addr,_,_,_ in pdr_defs}
    ptr_defs=[]
    for n,addr,size,sh in data_pointer_function_ranges(elf,path,recovered_addrs):
        if addr not in recovered_addrs and not covered_by_symbol(addr):
            # A materialized executable address in allocated non-code data is
            # hard callable-entry evidence just like a direct JAL/BAL target.
            # SGI widget/class tables and GOT data use these pointers for
            # indirect jalr calls, and broad FDEs can span many such entries.
            # Do not let FDE coverage suppress an independently materialized
            # function pointer.  A real nonzero-size ELF symbol is stronger
            # ownership evidence, however, so never manufacture an entry in
            # the middle of one.
            ptr_defs.append((n,addr,size,sh,'data_ptr'))

    recovered_addrs|={addr for _,addr,_,_,_ in ptr_defs}
    link_defs=early_link_defs

    recovered_addrs|={addr for _,addr,_,_,_ in link_defs}
    boundary_defs=[]
    for n,addr,size,sh in boundary_entry_function_ranges(elf,recovered_addrs):
        if addr not in recovered_addrs and not covered_by_frame(addr):
            boundary_defs.append((n,addr,size,sh,'boundary_entry'))

    seen_addr=set(); functions=[]; frame_count=0; pdr_count=0; zero_count=0; ptr_count=0; link_count=0; boundary_count=0; dropped_count=0
    all_defs=sorted(defs+zero_defs+frame_defs+pdr_defs+ptr_defs+link_defs+boundary_defs,key=lambda x:(x[1],x[0]))
    batches=[]; batch_bodies=[]; batch_rows=[]
    def flush_batch():
        nonlocal batch_bodies,batch_rows
        if not batch_rows: return
        batch_no=len(batches)
        dst=out/f'batch_{batch_no:04d}.c'
        write_if_changed(dst,BATCH_PREAMBLE+''.join(batch_bodies))
        for row in batch_rows: row['source']=str(dst)
        batches.append(dst)
        batch_bodies=[]; batch_rows=[]

    for i,(n,addr,size,sh,source_kind) in enumerate(all_defs):
        if addr in seen_addr: continue
        if source_kind in ('debug_frame','pdr','zero_size','data_ptr','code_link','boundary_entry'):
            checked_size=validated_recovered_size(elf,addr,size)
            if not checked_size:
                dropped_count+=1
                continue
            size=checked_size
        seen_addr.add(addr)
        generated=io.StringIO()
        with contextlib.redirect_stdout(generated):
            if source_kind in ('debug_frame','pdr','zero_size','data_ptr','code_link','boundary_entry'):
                emit_explicit(str(path),n,addr,size,prefix+'_')
            else:
                t.emit(str(path),n,c_prefix=prefix+'_')
        text=generated.getvalue()
        if '        rt->current_pc = pc;\n' not in text:
            text=text.replace('        cpu->pc = pc;\n',
                              '        cpu->pc = pc;\n        rt->current_pc = pc;\n')
        row=dict(name=n,address=addr,size=size,c_name='irix_recomp_'+ident(prefix+'_')+ident(n),source_kind=source_kind)
        functions.append(row); batch_rows.append(row); batch_bodies.append(function_body(text))
        if len(batch_rows)>=BATCH_FUNCTIONS: flush_batch()
        if source_kind=='debug_frame': frame_count+=1
        if source_kind=='pdr': pdr_count+=1
        if source_kind=='zero_size': zero_count+=1
        if source_kind=='data_ptr': ptr_count+=1
        if source_kind=='code_link': link_count+=1
        if source_kind=='boundary_entry': boundary_count+=1
    flush_batch()

    imports=[s for s in raw_syms if s['shndx']==0]
    meta={'path':str(path),'prefix':prefix,'functions':functions,'imports':imports}
    write_if_changed(out/'bundle.json',json.dumps(meta,indent=2)+'\n')
    registry=io.StringIO()
    registry.write('#include "IrixGuest.h"\n')
    for r in functions: registry.write(f'extern void {r["c_name"]}(IrixGuestRuntime*, IrixGuestCpu*);\n')
    registry.write(f'void register_{ident(prefix)}(IrixGuestRuntime *rt){{\n')
    registry.write('  (void)rt;\n')
    for r in functions:
        # Recovered ranges are translation coverage, not guest symbols.  Keep
        # their generated C entry points private to the recompiler and never
        # publish invented __debug_frame/__pdr/__ptr/__link names into the
        # runtime symbol table.  Only names originating in the ELF symbol
        # table (including recovered zero-sized STT_FUNC extents) are real.
        if r["source_kind"] in ("symbol","zero_size"):
            runtime_name='"'+r["name"].replace('\\','\\\\').replace('"','\\"')+'"'
        else:
            runtime_name='0'
        registry.write(f'  irix_guest_register_function_range(rt,0x{r["address"]:08x}u,0x{r["size"]:x}u,{r["c_name"]},{runtime_name});\n')
    registry.write('}\n')
    write_if_changed(out/'registry.c',registry.getvalue())
    print(f'{path}: functions={len(functions)} batches={len(batches)} zero_size_recovered={zero_count} debug_frame_recovered={frame_count} pdr_recovered={pdr_count} data_ptr_recovered={ptr_count} code_link_recovered={link_count} boundary_entry_recovered={boundary_count} recovered_dropped={dropped_count} imports={len(imports)}')
if __name__=='__main__': main()
