#!/usr/bin/env python3
"""Audit sized functions in one or more IRIX N32 ELFs for decoder coverage."""
import argparse, collections, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import translate_n32_regimm as t


def classify(msg):
    m = re.search(r'unsupported (.+?) at 0x[0-9a-fA-F]+ word=0x([0-9a-fA-F]+)', msg)
    if m:
        return m.group(1), int(m.group(2), 16)
    return msg, None


def audit(path):
    elf = t.Elf32BE(str(path))
    failures = []
    functions = 0
    words = 0
    seen = set()
    for name, start, size, shndx in sorted(elf.symbols(), key=lambda x: (x[1], x[0])):
        if not size or (name, start) in seen:
            continue
        seen.add((name, start))
        functions += 1
        try:
            code = elf.words(start, size, shndx=shndx, section_base=0) if elf.etype == 1 else elf.words(start, size)
        except Exception as exc:
            failures.append((name, start, None, str(exc)))
            continue
        for i, word in enumerate(code):
            addr = (start + i * 4) & 0xffffffff
            words += 1
            try:
                t.decode(addr, word)
            except Exception as exc:
                failures.append((name, addr, word, str(exc)))
    return functions, words, failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary', nargs='+', type=Path)
    a = ap.parse_args()
    total_functions = total_words = 0
    all_failures = []
    for path in a.binary:
        fns, words, failures = audit(path)
        total_functions += fns
        total_words += words
        print(f'{path}: functions={fns} words={words} unsupported={len(failures)}')
        all_failures.extend((path, *x) for x in failures)
    if all_failures:
        grouped = collections.defaultdict(list)
        for path, name, addr, word, msg in all_failures:
            key, decoded_word = classify(msg)
            grouped[key].append((path, name, addr, word if word is not None else decoded_word, msg))
        print('\nUnsupported decoder coverage:', file=sys.stderr)
        for key in sorted(grouped):
            rows = grouped[key]
            print(f'  {key}: {len(rows)} occurrence(s)', file=sys.stderr)
            for path, name, addr, word, msg in rows[:12]:
                w = '????????' if word is None else f'{word:08x}'
                print(f'    {path.name}:{name} 0x{addr:08x} word=0x{w}', file=sys.stderr)
            if len(rows) > 12:
                print(f'    ... {len(rows)-12} more', file=sys.stderr)
        print(f'\nFAIL: {len(all_failures)} unsupported instruction occurrence(s) across {total_functions} functions / {total_words} words', file=sys.stderr)
        return 1
    print(f'PASS: decoder covers {total_functions} functions / {total_words} words')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
