"""Unpack an InstallShield 6+ cabinet set (data1.hdr + dataN.cab), checking every file's MD5.

The F.E.A.R. public tools installer is an InstallScript setup with no extract-only mode; its
self-extracting stub does take /extract_all:<dir>, which leaves Disk1/data1.hdr and the
cabinets. This unpacks those. The format follows unshield's description of it.

Usage: python -I isextract.py <Disk1 dir> <out dir> [--list]
Exit code 1 if any file other than the installer's own engine files fails its MD5.
"""
import hashlib, os, struct, sys, zlib

FILE_SPLIT, FILE_OBFUSCATED, FILE_COMPRESSED, FILE_INVALID = 1, 2, 4, 8


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u64(b, o): return struct.unpack_from('<Q', b, o)[0]
def cstr(b, o): return b[o:b.index(b'\0', o)].decode('latin-1')


class Volume:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(20 + 0x50)
        assert h[:4] == b'ISc(', path
        p = 20
        self.data_offset = u32(h, p)
        self.first_file_index = u32(h, p + 8)
        self.last_file_index = u32(h, p + 12)
        def lohi(o): return u32(h, p + o) | (u32(h, p + o + 4) << 32)
        self.first_file_offset = lohi(16)
        self.first_file_size_expanded = lohi(24)
        self.first_file_size_compressed = lohi(32)
        self.last_file_offset = lohi(40)
        self.last_file_size_expanded = lohi(48)
        self.last_file_size_compressed = lohi(56)

    def read(self, off, n):
        self.f.seek(off)
        d = self.f.read(n)
        assert len(d) == n
        return d


def main():
    disk, out = sys.argv[1], sys.argv[2]
    listing = '--list' in sys.argv
    hdr = open(os.path.join(disk, 'data1.hdr'), 'rb').read()
    assert hdr[:4] == b'ISc('
    version = u32(hdr, 4)
    cdo = u32(hdr, 12)
    ft_off = u32(hdr, cdo + 0x0c)
    dir_count = u32(hdr, cdo + 0x1c)
    file_count = u32(hdr, cdo + 0x28)
    ft_off2 = u32(hdr, cdo + 0x2c)
    ft = cdo + ft_off
    dirs = [cstr(hdr, ft + u32(hdr, ft + 4 * i)) for i in range(dir_count)]
    vols = {}

    def vol(n):
        if n not in vols:
            vols[n] = Volume(os.path.join(disk, f'data{n}.cab'))
        return vols[n]

    ok = bad = skipped = 0
    for i in range(file_count):
        p = ft + ft_off2 + i * 0x57
        flags = u16(hdr, p)
        exp = u64(hdr, p + 2)
        comp = u64(hdr, p + 10)
        doff = u64(hdr, p + 18)
        md5 = hdr[p + 26:p + 42]
        name = cstr(hdr, ft + u32(hdr, p + 58))
        d = u16(hdr, p + 62)
        volume = u16(hdr, p + 85)
        if flags & FILE_INVALID or not name:
            skipped += 1
            continue
        rel = os.path.join(dirs[d], name) if dirs[d] else name
        if listing:
            print(f'{exp:>12} {flags:02x} v{volume} {rel}')
            continue
        # Gather the stored bytes, following splits across volumes.
        raw, n, remaining = bytearray(), volume, comp
        while remaining:
            v = vol(n)
            if not flags & FILE_SPLIT:
                off, take = doff, comp
            elif v.last_file_index == i and v.last_file_offset != 0x7FFFFFFF:
                off, take = v.last_file_offset, v.last_file_size_compressed
            elif v.first_file_index == i:
                off, take = v.first_file_offset, v.first_file_size_compressed
            else:
                raise RuntimeError(f'{rel}: split file not in volume {n}')
            take = min(take, remaining)
            raw += v.read(off, take)
            remaining -= take
            n += 1
        assert not flags & FILE_OBFUSCATED, rel
        if flags & FILE_COMPRESSED:
            data, q = bytearray(), 0
            while q < len(raw):
                ln = u16(raw, q); q += 2
                data += zlib.decompressobj(-15).decompress(bytes(raw[q:q + ln]))
                q += ln
        else:
            data = raw
        good = len(data) == exp and hashlib.md5(data).digest() == md5
        dst = os.path.join(out, rel.replace('\\', os.sep))
        os.makedirs(os.path.dirname(dst) or out, exist_ok=True)
        with open(dst, 'wb') as fo:
            fo.write(data)
        if good:
            ok += 1
        elif rel.lower() in INSTALLER_FILES:
            skipped += 1
        else:
            bad += 1
            print('MISMATCH', rel, len(data), exp)
    if not listing:
        print(f'version {version:#x}: {ok} ok, {bad} mismatched, {skipped} skipped, {file_count} entries')
    return bad


# setup.exe, setup.ini and setup.inx are stored twice, the copy in the cabinet with an MD5 of
# the other; they are the installer's own files, never used here.
INSTALLER_FILES = {'setup.exe', 'setup.ini', 'setup.inx'}

if __name__ == '__main__':
    sys.exit(1 if main() else 0)
