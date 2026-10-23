"""Scan the generated translation (port-nx/generated/default) per guest function.

    python -I genscan.py <generated dir> <image.bin> pm4         functions building PM4 type-3 headers
    python -I genscan.py <generated dir> <image.bin> strings SUB... strings each function references
    python -I genscan.py <generated dir> <image.bin> calls SUB...   functions each one calls (counts)
    python -I genscan.py <generated dir> <image.bin> size SUB...    instruction counts
"""
import collections
import glob
import os
import re
import struct
import sys

gen, image_path, mode, *subs = sys.argv[1:]
image = open(image_path, 'rb').read()
BASE = 0x82000000

FUNC_RE = re.compile(r'DEFINE_REX_FUNC\((sub_[0-9A-F]+)\) \{(.*?)\n\}', re.S)
LIS_RE = re.compile(r'// lis r(\d+),(-?\d+)')
LO_RE = re.compile(r'// (?:addi|lwz|lfs|lhz|lbz|lfd|ld|stw|stfs|li) r?f?(\d+),(?:r(\d+),(-?\d+)|(-?\d+)\(r(\d+)\))')
ORI_RE = re.compile(r'// ori r(\d+),r(\d+),(\d+)')
CALL_RE = re.compile(r'\b(sub_[0-9A-F]+)\(ctx, base\);')


def functions():
    for path in sorted(glob.glob(os.path.join(gen, 'fear_recomp.*.cpp'))):
        text = open(path, encoding='utf-8', errors='replace').read()
        for m in FUNC_RE.finditer(text):
            yield m.group(1), m.group(2)


def read_string(va):
    o = va - BASE
    if not (0 <= o < len(image)):
        return None
    end = image.find(b'\0', o, o + 200)
    if end <= o + 3:
        return None
    s = image[o:end]
    if all(32 <= c < 127 for c in s):
        return s.decode()
    return None


def addresses(body):
    hi = {}
    out = []
    for line in body.splitlines():
        m = LIS_RE.search(line)
        if m:
            hi[int(m.group(1))] = (int(m.group(2)) & 0xFFFF) << 16
            continue
        m = re.search(r'// (?:addi|lwz|lfs|lhz|lbz|lfd|stw|stfs) [rf](\d+),(?:r(\d+),(-?\d+)|(-?\d+)\(r(\d+)\))', line)
        if m:
            if m.group(2) is not None:
                base_reg, lo = int(m.group(2)), int(m.group(3))
            else:
                base_reg, lo = int(m.group(5)), int(m.group(4))
            if base_reg in hi:
                out.append((hi[base_reg] + lo) & 0xFFFFFFFF)
    return out


if mode == 'pm4':
    counts = collections.Counter()
    for name, body in functions():
        n = 0
        for m in LIS_RE.finditer(body):
            v = int(m.group(2)) & 0xFFFF
            if 0xC000 <= v <= 0xC03F:   # PM4 type 3, count up to 63
                n += 1
        if n:
            counts[name] = n
    by_addr = sorted(counts.items(), key=lambda kv: int(kv[0][4:], 16))
    print(len(by_addr), 'functions with PM4 type-3 header constants')
    for name, n in by_addr:
        print(name, n)
elif mode in ('strings', 'calls', 'size'):
    want = set(subs)
    for name, body in functions():
        if name not in want:
            continue
        want.discard(name)
        if mode == 'strings':
            seen = []
            for a in addresses(body):
                s = read_string(a)
                if s and s not in seen:
                    seen.append(s)
            print(name, '|', ' | '.join(seen[:25]))
        elif mode == 'calls':
            c = collections.Counter(CALL_RE.findall(body))
            print(name, ' '.join(f'{k}x{v}' for k, v in c.most_common(20)))
        else:
            print(name, body.count('\n\t// '))
