"""Minimal XDVDFS reader for Xbox 360 ISOs: list files, extract selected ones.
    python -I xdvdfs.py <iso> list
    python -I xdvdfs.py <iso> extract <outdir> [name-substring ...]
It takes a command, so double-clicking it does nothing, and the game never uses it: a release's
README has the command to run, from the folder this file came in.
"""
import hashlib, os, struct, sys

MAGIC = b"MICROSOFT*XBOX*MEDIA"
SECTOR = 2048
# default.xex of the discs this port supports.
SUPPORTED_XEX = {
    "e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792": "F.E.A.R.",
    "a51deed1dc0842af32a3a2b811674552f4ae5b6339ce8b9abe0eabe14ba6c7eb": "F.E.A.R. Files",
}


def find_partition(f):
    for base in (0x0, 0xFD90000, 0x2080000, 0x18300000):
        f.seek(base + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return base
    raise SystemExit("no XDVDFS partition found")


def walk(f, base, sector, size, path=""):
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    stack = [0]
    seen = set()
    while stack:
        off = stack.pop()
        if off in seen or off * 4 + 14 > len(data):
            continue
        seen.add(off)
        p = off * 4
        left, right, start, length, attr, nlen = struct.unpack_from("<HHIIBB", data, p)
        if left == 0xFFFF:
            continue
        name = data[p + 14:p + 14 + nlen].decode("latin-1")
        full = f"{path}/{name}"
        if attr & 0x10:
            if length:
                yield from walk(f, base, start, length, full)
        else:
            yield full, base + start * SECTOR, length
        if left:
            stack.append(left)
        if right:
            stack.append(right)


def check_output(out):
    """A relative output naming the current folder lands inside itself: fear/assets, run from
    fear/assets, is fear/assets/fear/assets, where the game won't look."""
    if os.path.isabs(out):
        return
    here = os.path.basename(os.getcwd()).lower()
    if here in os.path.normpath(out).lower().split(os.sep):
        raise SystemExit(f"{out} from here would be {os.path.abspath(out)}, a folder inside itself.\n"
                         "Run this from the folder xdvdfs.py came in, or give the full path.")


def report_xex(xex):
    with open(xex, "rb") as f:
        title = SUPPORTED_XEX.get(hashlib.sha256(f.read()).hexdigest())
    if title:
        print(f"Done: {title}, in {os.path.dirname(os.path.abspath(xex))}")
    else:
        print(f"Warning: {xex} isn't from a supported disc, so the game may not run (see the README).")


def main():
    if len(sys.argv) < 3 or sys.argv[2] not in ("list", "extract") or (
            sys.argv[2] == "extract" and len(sys.argv) < 4):
        print(__doc__)
        if len(sys.argv) == 1 and os.name == "nt":  # a double-click's window would close at once
            input("Press Enter to close.")
        raise SystemExit(2)
    iso, cmd = sys.argv[1], sys.argv[2]
    if not os.path.isfile(iso):
        raise SystemExit(f"No disc image at {iso}")
    if cmd == "extract":
        check_output(sys.argv[3])
    with open(iso, "rb") as f:
        base = find_partition(f)
        f.seek(base + 32 * SECTOR + 20)
        root_sector, root_size = struct.unpack("<II", f.read(8))
        files = sorted(walk(f, base, root_sector, root_size))
        if cmd == "list":
            print(f"partition base 0x{base:X}, {len(files)} files, "
                  f"{sum(l for _, _, l in files) / 2**30:.2f} GiB")
            for name, off, length in files:
                print(f"{length:>12}  {name}")
        elif cmd == "extract":
            out, pats = sys.argv[3], [p.lower() for p in sys.argv[4:]]
            xex = None
            for name, off, length in files:
                if pats and not any(p in name.lower() for p in pats):
                    continue
                dst = os.path.join(out, name.lstrip("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                f.seek(off)
                with open(dst, "wb") as o:
                    left = length
                    while left:
                        chunk = f.read(min(left, 1 << 24))
                        o.write(chunk)
                        left -= len(chunk)
                print("extracted", name, length)
                if name.lower() == "/default.xex":
                    xex = dst
            if xex:
                report_xex(xex)


if __name__ == "__main__":
    main()
