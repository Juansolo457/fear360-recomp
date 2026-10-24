"""Names the profiler's host-code samples in a log from the build's PDB (#40).

    python -I symbolize_profile.py LOG EXE [--symbolizer PATH]

The profiler puts a sample in the exe's own host code (our renderer, the SDK's static code) under
the function holding it, as "fear.exe+0x<RVA>" (port-nx/src/profiler.h). This rewrites each one
to "fear.exe!<function>" with llvm-symbolizer (one run for all of them; it reads the .pdb next to
EXE) and prints the log. EXE must be the build that made the log: the Deck package's fear.exe is
the PC build's, so its fear.pdb matches. Default symbolizer: .tools/llvm/bin/llvm-symbolizer.exe.
"""
import json
import os
import re
import struct
import subprocess
import sys


def image_base(exe):
    data = open(exe, 'rb').read(4096)
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    magic = struct.unpack_from('<H', data, pe + 24)[0]
    if magic != 0x20B:
        sys.exit(f'{exe}: not a 64-bit PE')
    return struct.unpack_from('<Q', data, pe + 24 + 24)[0]


def main():
    args = sys.argv[1:]
    symbolizer = os.path.join(os.path.dirname(__file__), '..', '..', '.tools', 'llvm', 'bin',
                              'llvm-symbolizer.exe')
    if '--symbolizer' in args:
        i = args.index('--symbolizer')
        symbolizer = args[i + 1]
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    log, exe = args
    name = re.escape(os.path.basename(exe))
    pattern = re.compile(name + r'\+0x([0-9A-Fa-f]+)', re.I)
    text = open(log, encoding='utf-8', errors='replace').read()
    rvas = sorted({int(m.group(1), 16) for m in pattern.finditer(text)})
    if not rvas:
        sys.stdout.write(text)
        print(f'(no {os.path.basename(exe)}+0x... samples in {log})', file=sys.stderr)
        return
    base = image_base(exe)
    query = ''.join(f'0x{base + rva:X}\n' for rva in rvas)
    out = subprocess.run([symbolizer, f'--obj={exe}', '--functions=short', '--inlining=false',
                          '--output-style=JSON'],
                         input=query, capture_output=True, text=True, check=True).stdout
    names = {}
    for line in out.splitlines():
        if not line.strip():
            continue
        entry = json.loads(line)
        symbols = entry.get('Symbol') or [{}]
        function = symbols[0].get('FunctionName', '')
        if function and function != '??':
            names[int(entry['Address'], 16) - base] = function
    sys.stdout.write(pattern.sub(
        lambda m: f'{os.path.basename(exe)}!{names[int(m.group(1), 16)]}'
        if int(m.group(1), 16) in names else m.group(0), text))
    print(f'named {len(names)} of {len(rvas)} host functions', file=sys.stderr)


if __name__ == '__main__':
    main()
