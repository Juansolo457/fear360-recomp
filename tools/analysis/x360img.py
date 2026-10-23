"""The dumped 360 image (base 0x82000000, big-endian): strings, pointers, words, PPC disassembly.

    python -P x360img.py <image> str <regex>
    python -P x360img.py <image> ptrs <hex VA>...
    python -P x360img.py <image> word <hex VA> [count]
    python -P x360img.py <image> dis <hex VA> [count]
    python -P x360img.py <image> lis <hex VA>...      code that builds these addresses (lis+addi/ori/lwz...)
"""
import re
import struct
import sys

import capstone

BASE = 0x82000000
path, cmd, *args = sys.argv[1:]
data = open(path, 'rb').read()


def word(v):
    return struct.unpack_from('>I', data, v - BASE)[0]


if cmd == 'str':
    for m in re.finditer(rb'[\x20-\x7e]{3,}\x00', data):
        s = m.group()[:-1].decode()
        if re.search(args[0], s):
            print(hex(BASE + m.start()), s)
elif cmd == 'ptrs':
    targets = {int(a, 16) for a in args}
    for o in range(0, len(data) - 3, 4):
        w = struct.unpack_from('>I', data, o)[0]
        if w in targets:
            print(hex(BASE + o), 'holds', hex(w))
elif cmd == 'word':
    v = int(args[0], 16)
    for k in range(int(args[1]) if len(args) > 1 else 8):
        print(hex(v + 4 * k), hex(word(v + 4 * k)))
elif cmd == 'dis':
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    v = int(args[0], 16)
    n = int(args[1]) if len(args) > 1 else 60
    o = v - BASE
    for ins in md.disasm(data[o:o + n * 4], v):
        print(f'{ins.address:08x}  {ins.mnemonic:8s} {ins.op_str}')
elif cmd == 'lis':
    # Scan for lis rX, hi followed within 8 instructions by addi/ori/lwz/lfs/... rY, rX, lo.
    targets = [int(a, 16) for a in args]
    want = {}
    for t in targets:
        hi = (t + 0x8000) >> 16 & 0xFFFF
        lo = t & 0xFFFF
        want.setdefault(hi, []).append((t, lo))
    n = len(data) // 4
    for i in range(n):
        w = struct.unpack_from('>I', data, i * 4)[0]
        if w >> 26 != 15 or (w >> 16 & 0x1F) != 0:  # addis rD, 0, imm = lis
            continue
        hi = w & 0xFFFF
        if hi not in want:
            continue
        rd = w >> 21 & 0x1F
        for k in range(1, 12):
            if i + k >= n:
                break
            w2 = struct.unpack_from('>I', data, (i + k) * 4)[0]
            ra = w2 >> 16 & 0x1F
            if ra != rd:
                continue
            lo = w2 & 0xFFFF
            for t, tlo in want[hi]:
                if lo == tlo:
                    print(f'{BASE + (i + k) * 4:08x} builds {t:#x}')
