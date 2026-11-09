#!/usr/bin/env python3
"""Typed endianness swap of the big-endian N32 data image.

Field widths come from the DWARF of a clang-built N32 corpus library that was
compiled from the same sources (llvm-dwarfdump text output is parsed). Bytes
without a typed variable fall back to section-based rules.
"""
import re
import struct
import subprocess
import sys

TAG = re.compile(r'^(0x[0-9a-f]+):(\s*)DW_TAG_(\w+)')
ATTR = re.compile(r'^\s+DW_AT_(\w+)\t\((.*)\)\s*$')


class Dwarf:
    def __init__(self, path):
        txt = subprocess.run(['llvm-dwarfdump', '--debug-info', path],
                             capture_output=True, text=True, check=True).stdout
        self.nodes = {}
        self.vars = []          # (cu_stem, addr, type_off)
        stack = []
        cur = None
        for line in txt.splitlines():
            m = TAG.match(line)
            if m:
                depth = len(m.group(2))
                node = {'off': int(m.group(1), 16), 'tag': m.group(3), 'a': {}, 'kids': [], 'depth': depth}
                while stack and stack[-1]['depth'] >= depth:
                    stack.pop()
                node['parent'] = stack[-1] if stack else None
                if stack:
                    stack[-1]['kids'].append(node)
                stack.append(node)
                self.nodes[node['off']] = node
                cur = node
                continue
            m = ATTR.match(line)
            if m and cur is not None:
                cur['a'].setdefault(m.group(1), m.group(2))
        for n in self.nodes.values():
            if n['tag'] == 'variable' and 'location' in n['a'] and 'type' in n['a']:
                m = re.search(r'DW_OP_addr (0x[0-9a-f]+)', n['a']['location'])
                if not m:
                    continue
                cu = n
                while cu['parent'] is not None:
                    cu = cu['parent']
                nm = re.search(r'"([^"]*)"', cu['a'].get('name', ''))
                stem = re.sub(r'\.[^.]*$', '', (nm.group(1) if nm else '').rsplit('/', 1)[-1])
                self.vars.append((stem, int(m.group(1), 16), self.ref(n['a']['type'])))

    @staticmethod
    def ref(v):
        return int(v.split()[0], 16)

    @staticmethod
    def num(n, key, default=None):
        v = n['a'].get(key)
        if v is None:
            return default
        m = re.match(r'(-?(?:0x[0-9a-fA-F]+|\d+))', v)
        return int(m.group(1), 0) if m else default

    def strip(self, t):
        while t is not None and t in self.nodes and self.nodes[t]['tag'] in (
                'typedef', 'const_type', 'volatile_type', 'restrict_type', 'atomic_type'):
            n = self.nodes[t]
            t = self.ref(n['a']['type']) if 'type' in n['a'] else None
        return t

    def size(self, t):
        t = self.strip(t)
        if t is None:
            return 4
        n = self.nodes[t]
        tag = n['tag']
        if tag in ('pointer_type', 'reference_type'):
            return 4
        bs = self.num(n, 'byte_size')
        if bs is not None:
            return bs
        if tag == 'array_type':
            return self.count(n) * self.size(self.ref(n['a']['type']))
        return 4

    def count(self, n):
        total = 1
        for k in n['kids']:
            if k['tag'] == 'subrange_type':
                c = self.num(k, 'count')
                if c is None:
                    ub = self.num(k, 'upper_bound')
                    c = (ub + 1) if ub is not None else 0
                total *= c
        return total

    def leaves(self, t, base, out, stats):
        t = self.strip(t)
        if t is None:
            return
        n = self.nodes[t]
        tag = n['tag']
        if tag in ('pointer_type', 'reference_type'):
            out.add((base, 4))
        elif tag in ('base_type', 'enumeration_type'):
            s = self.size(t)
            is_float = 'float' in n['a'].get('encoding', '')
            if s == 8 and not is_float:
                # 64-bit integers keep big-endian word order (see IrixGuest.h)
                out.add((base, 4))
                out.add((base + 4, 4))
            elif s > 1:
                out.add((base, s))
        elif tag in ('structure_type', 'class_type'):
            for k in n['kids']:
                if k['tag'] != 'member':
                    continue
                off = self.num(k, 'data_member_location', 0)
                if 'bit_size' in k['a']:
                    s = self.num(k, 'byte_size', self.size(self.ref(k['a']['type'])))
                    if s > 1:
                        out.add((base + off, s))
                    stats['bitfields'] += 1
                else:
                    self.leaves(self.ref(k['a']['type']), base + off, out, stats)
        elif tag == 'union_type':
            stats['unions'] += 1
            best = None
            for k in n['kids']:
                if k['tag'] == 'member':
                    sz = self.size(self.ref(k['a']['type']))
                    if best is None or sz > best[0]:
                        best = (sz, k)
            if best:
                self.leaves(self.ref(best[1]['a']['type']), base, out, stats)
        elif tag == 'array_type':
            et = self.ref(n['a']['type'])
            es = self.size(et)
            cnt = self.count(n)
            e = self.strip(et)
            if es > 1 or (e is not None and self.nodes[e]['tag'] in ('structure_type', 'class_type', 'union_type', 'array_type')):
                if cnt * es > 1 << 22:
                    stats['big_arrays'] += 1
                for i in range(min(cnt, 1 << 20)):
                    self.leaves(et, base + i * es, out, stats)


def string_mask(img, aligned_start=False):
    """Mark bytes of NUL-terminated printable runs (>= 2 chars, or a lone character filling its word). MIPSpro puts string
    literals in plain .rodata, where the word-swap default would reverse them."""
    mask = bytearray(len(img))
    n = len(img)
    i = 0
    while i < n:
        j = i
        while j < n and (0x20 <= img[j] < 0x7f or img[j] in (9, 10, 13)):
            j += 1
        # >= 2 printable characters ("OK"), or one character that fills a word on its own ("/", ":", "\n"):
        # shorter literals are word-swapped to garbage otherwise
        short_ok = (j - i == 1 and i % 4 == 0 and img[i + 1:i + 4] == b'\0\0\0')
        # executables pack string literals right after other data: an aligned start of >= 4 printable characters counts too
        start_ok = i == 0 or img[i - 1] == 0 or (aligned_start and i % 4 == 0 and j - i >= 4)
        if (j - i >= 2 or short_ok) and j < n and img[j] == 0 and start_ok:
            for k in range(i, min(j + 1, n)):
                mask[k] = 1
        i = max(j, i) + 1
    return mask


def typed_byte_arrays(obj, declarations):
    """Recover explicitly audited char-array types missing from corpus DWARF.

    Match the actual member, section, range and original bytes. Fail closed
    when a profile describes another binary; never guess that writable words
    containing printable characters are strings.
    """
    import hashlib
    spans = []
    for declaration in declarations:
        if declaration['member'] != obj.name:
            continue
        if declaration['type'] not in ('char[]', 'short[]'):
            raise ValueError('Unsupported explicit data type')
        matches = [section for section in obj.sec
                   if section['name'] == declaration['section']]
        if len(matches) != 1:
            raise ValueError('Byte-array section missing or ambiguous')
        section = matches[0]
        no = section['no']
        offset, size = declaration['offset'], declaration['size']
        if no not in obj.image or offset < 0 or size <= 0 or offset+size > len(obj.image[no]):
            raise ValueError('Byte-array range outside allocated section')
        value = bytes(obj.image[no][offset:offset+size])
        if hashlib.sha256(value).hexdigest() != declaration['sha256']:
            raise ValueError('Byte-array profile does not match original data')
        if declaration['type'] == 'short[]':
            if size % 2:
                raise ValueError('short[] declaration is not a whole number of halfwords')
            value = b''.join(value[i:i+2][::-1] for i in range(0, size, 2))
        spans.append((no, offset, value))
    return spans


def swap_range(buf, off, size):
    if off < 0 or off + size > len(buf):
        return False
    buf[off:off + size] = buf[off:off + size][::-1]
    return True


def apply(linker, so_path, log=sys.stderr, byte_arrays=()):
    """Swap every section image of a laid-out StackLink in place."""
    import link_stack_n32 as L
    dw = Dwarf(so_path)
    with open(so_path, 'rb') as corpus:
        so = L.Obj(so_path, 'corpus', corpus.read())
    addr2name = {}
    for s in so.syms:
        if s['type'] == L.STT_OBJECT and s['name']:
            addr2name.setdefault(s['value'], s['name'])
    typed = {}
    for stem, addr, t in dw.vars:
        nm = addr2name.get(addr)
        if nm:
            typed[(stem, nm)] = t
    stats = {'bitfields': 0, 'unions': 0, 'big_arrays': 0, 'typed': 0, 'untyped_syms': 0, 'default_words': 0}
    members = {obj.name for obj in linker.objs}
    for declaration in byte_arrays:
        if declaration['member'] not in members:
            raise ValueError('Byte-array profile member missing: ' + declaration['member'])
    for o in linker.objs:
        stem = re.sub(r'\.[^.]*$', '', o.name)
        byte_spans = typed_byte_arrays(o, byte_arrays)
        # Relocations describe the actual SGI object, whereas the source corpus
        # can have different class-record fields. Preserve resolved pointer
        # values before applying the corpus layout and restore them as native
        # words afterwards, even when that layout covered the slot as bytes.
        pointers = {}
        for rs in o.sec:
            if rs['type'] not in (L.SHT_REL, L.SHT_RELA):
                continue
            no = rs['info']
            if no not in o.image or o.sec[no]['flags'] & 4:
                continue
            ent = rs['entsz'] or (12 if rs['type'] == L.SHT_RELA else 8)
            for q in range(rs['off'], rs['off'] + rs['size'], ent):
                off, info = struct.unpack_from('>II', o.data, q)
                if info & 255 == L.R_MIPS_32:
                    pointers[(no, off)] = bytes(o.image[no][off:off + 4])[::-1]
        covered = {no: bytearray(len(img)) for no, img in o.image.items()}
        for s in o.syms:
            if s['type'] != L.STT_OBJECT or not s['name'] or s['shndx'] not in o.image:
                continue
            sec = o.sec[s['shndx']]
            if sec['type'] == L.SHT_NOBITS:
                continue
            t = typed.get((stem, s['name']))
            if t is None:
                stats['untyped_syms'] += 1
                continue
            out = set()
            dw.leaves(t, 0, out, stats)
            img = o.image[s['shndx']]
            for off, size in out:
                swap_range(img, s['value'] + off, size)
            sz = max(s['size'], dw.size(t))
            for i in range(s['value'], min(s['value'] + sz, len(img))):
                covered[s['shndx']][i] = 1
            stats['typed'] += 1
        for no, img in o.image.items():
            sec = o.sec[no]
            if sec['type'] == L.SHT_NOBITS or (sec['flags'] & 4):   # SHF_EXECINSTR
                continue
            name = sec['name']
            if name.startswith('.rodata.str') or (sec['flags'] & 0x20):
                continue
            unit = 8 if (name.startswith('.rodata.cst8') or name == '.lit8') else 4
            cov = covered[no]
            strs = string_mask(img) if unit == 4 and not (sec['flags'] & 1) else bytearray(len(img))
            stats['string_bytes'] = stats.get('string_bytes', 0) + sum(strs)
            i = 0
            while i + unit <= len(img):
                if any(strs[i:i + unit]):
                    pass
                elif not any(cov[i:i + unit]):
                    swap_range(img, i, unit)
                    stats['default_words'] += 1
                    for j in range(i, i + unit):
                        cov[j] = 1
                i += unit
        for no, off, value in byte_spans:
            if any(pno == no and poff < off+len(value) and off < poff+4
                   for pno, poff in pointers):
                raise ValueError('Byte-array declaration overlaps a pointer relocation')
            o.image[no][off:off+len(value)] = value
        for (no, off), value in pointers.items():
            o.image[no][off:off + 4] = value
    print('nm_swap:', stats, file=log)
    return stats


def swap_got(linker, small_path):
    data = bytearray(open(small_path, 'rb').read())
    for _, (a, _v) in linker.got.items():
        swap_range(data, a - linker.small_base, 4)
    open(small_path, 'wb').write(data)


def swap_synthetic(linker, main_path):
    """Swap the linker's synthetic data (the SGI C-locale ctype table and __libc_attr)."""
    data = bytearray(open(main_path, 'rb').read())
    for addr, blob in linker.synthetic_data.items():
        off = addr - linker.base
        if len(blob) == 2064:          # __ctype_t: 257 uint32, 257 int16, 257 int16, 8 bytes
            for i in range(257):
                swap_range(data, off + 4 * i, 4)
            for i in range(514):
                swap_range(data, off + 1028 + 2 * i, 2)
        else:                           # __libc_attr: word 0 is the ctype table pointer
            swap_range(data, off, 4)
    open(main_path, 'wb').write(data)
