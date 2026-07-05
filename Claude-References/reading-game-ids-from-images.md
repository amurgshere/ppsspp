# Reading PSP disc IDs from ISO/CSO files

When adding compat.ini entries for a game, don't guess serials from memory or partial redump.org matches — read the real `DISC_ID` straight out of the image's `PSP_GAME/PARAM.SFO`. Guessed IDs are a common source of silently-inactive compat.ini entries (wrong ID = flag never applies, no error).

No external tools (7z, PowerISO, etc.) are needed — a PSP ISO is plain ISO9660, and PARAM.SFO is a small documented key/value format. A CSO is just ISO9660 data split into fixed-size blocks, individually zlib- (or lz4-, for ZSO) compressed, with an index table up front — also parseable without extracting.

## Script

Self-contained, stdlib only (zlib is built in; only `ziso`/`ZISO`-magic LZ4 variants need the `lz4` package, which is rare in the wild — most CSOs are zlib `CISO`). Save as e.g. `read_discid.py` and run `python read_discid.py file1.iso file2.cso ...`.

```python
import sys, struct, os, zlib

class CSOFile:
    """Minimal random-access reader for CISO/CSO v1/v2 compressed ISO images."""
    def __init__(self, path):
        self._raw = open(path, 'rb')
        header = self._raw.read(24)
        magic, header_size, total_bytes, block_size, ver, align, reserved = struct.unpack('<4sIQIBBH', header)
        if magic not in (b'CISO', b'ziso', b'ZISO'):
            raise ValueError(f"not a recognized CSO magic: {magic!r}")
        self.block_size = block_size
        self.total_bytes = total_bytes
        self.align = align
        self.lz4 = (magic in (b'ziso', b'ZISO'))
        num_blocks = total_bytes // block_size
        idx_bytes = self._raw.read((num_blocks + 1) * 4)
        self.index = struct.unpack(f'<{num_blocks+1}I', idx_bytes)
        self.pos = 0
        self._cache_block = None
        self._cache_data = None

    def _read_block(self, block_no):
        if self._cache_block == block_no:
            return self._cache_data
        entry = self.index[block_no]
        next_entry = self.index[block_no + 1]
        compressed = not (entry & 0x80000000)
        offset = (entry & 0x7FFFFFFF) << self.align
        next_offset = (next_entry & 0x7FFFFFFF) << self.align
        length = next_offset - offset
        self._raw.seek(offset)
        raw = self._raw.read(length)
        if compressed:
            if self.lz4:
                import lz4.block
                data = lz4.block.decompress(raw, uncompressed_size=self.block_size)
            else:
                data = zlib.decompressobj(-15).decompress(raw)
                data = (data + b'\x00' * self.block_size)[:self.block_size]
        else:
            data = raw
        self._cache_block = block_no
        self._cache_data = data
        return data

    def seek(self, pos, whence=0):
        if whence == 0: self.pos = pos
        elif whence == 1: self.pos += pos
        elif whence == 2: self.pos = self.total_bytes + pos

    def read(self, size):
        out = bytearray()
        remaining = size
        pos = self.pos
        while remaining > 0 and pos < self.total_bytes:
            block_no = pos // self.block_size
            block_off = pos % self.block_size
            data = self._read_block(block_no)
            chunk = data[block_off:block_off + remaining]
            out += chunk
            got = len(chunk)
            if got == 0:
                break
            pos += got
            remaining -= got
        self.pos = pos
        return bytes(out)

def open_disc_image(path):
    if path.lower().endswith('.cso'):
        return CSOFile(path)
    return open(path, 'rb')

def read_sector(f, lba, size=2048):
    f.seek(lba * 2048)
    return f.read(size)

def find_file_lba(f, dir_lba, dir_size):
    remaining = dir_size
    lba = dir_lba
    entries = {}
    while remaining > 0:
        data = read_sector(f, lba)
        pos = 0
        while pos < len(data):
            length = data[pos]
            if length == 0:
                break
            rec = data[pos:pos+length]
            id_len = rec[32]
            extent = struct.unpack('<I', rec[2:6])[0]
            data_len = struct.unpack('<I', rec[10:14])[0]
            name = rec[33:33+id_len]
            name_str = name.decode('ascii', errors='ignore').split(';')[0]
            if name_str not in ('', '\x00', '\x01'):
                entries[name_str.upper()] = (extent, data_len)
            pos += length
        remaining -= 2048
        lba += 1
    return entries

def get_param_sfo(imgpath):
    f = open_disc_image(imgpath)
    pvd = read_sector(f, 16)
    assert pvd[1:6] == b'CD001', "not ISO9660 PVD"
    root_rec = pvd[156:156+34]
    root_extent = struct.unpack('<I', root_rec[2:6])[0]
    root_size = struct.unpack('<I', root_rec[10:14])[0]
    root_entries = find_file_lba(f, root_extent, root_size)
    if 'PSP_GAME' not in root_entries:
        return None
    pg_extent, pg_size = root_entries['PSP_GAME']
    pg_entries = find_file_lba(f, pg_extent, pg_size)
    if 'PARAM.SFO' not in pg_entries:
        return None
    sfo_extent, sfo_size = pg_entries['PARAM.SFO']
    f.seek(sfo_extent * 2048)
    return f.read(sfo_size)

def parse_sfo(data):
    if data[0:4] != b'\x00PSF':
        return {}
    key_table_start = struct.unpack('<I', data[8:12])[0]
    data_table_start = struct.unpack('<I', data[12:16])[0]
    nentries = struct.unpack('<I', data[16:20])[0]
    result = {}
    for i in range(nentries):
        off = 20 + i*16
        key_off, data_fmt, data_len, _, data_off = struct.unpack('<HHIII', data[off:off+16])
        key_start = key_table_start + key_off
        key_end = data.index(b'\x00', key_start)
        key = data[key_start:key_end].decode('ascii', errors='ignore')
        val_bytes = data[data_table_start+data_off:data_table_start+data_off+data_len]
        if data_fmt == 0x0204:
            val = val_bytes.split(b'\x00')[0].decode('ascii', errors='ignore')
        elif data_fmt == 0x0404:
            val = struct.unpack('<I', val_bytes[:4])[0] if len(val_bytes) >= 4 else 0
        else:
            val = val_bytes
        result[key] = val
    return result

if __name__ == '__main__':
    for path in sys.argv[1:]:
        sfo = get_param_sfo(path)
        info = parse_sfo(sfo) if sfo else {}
        print(f"{os.path.basename(path)}: DISC_ID={info.get('DISC_ID')} TITLE={info.get('TITLE')}")
```

## Verified working

- **ISO**: walked PVD → root dir → `PSP_GAME/` → `PARAM.SFO`, confirmed against known serials (e.g. LocoRoco USA ISO → `UCUS98662`, matches redump.org).
- **CSO (zlib `CISO` v1)**: confirmed against three real dumps — `Burnout Legends.cso` → `ULES00125`, `Bubble Bobble Evolution.cso` → `ULES00303`, `Ace Combat X - Skies of Deception.cso` → `ULUS10176`. All matched their known titles.
- **Not tested**: `ziso`/`ZISO`-magic (LZ4) CSO variant — the code path exists but no sample file was available to verify. If it errors, check whether `lz4` (pip package) is installed.

## Why this matters

PSN digital release serials (`NPxxNNNNN`) are often completely different from the UMD retail disc serials for the same game (both are valid compat.ini targets, but distinct) — e.g. LocoRoco Midnight Carnival JP retail vs PSN digital have different `DISC_ID`s entirely, and PSN releases usually aren't in redump.org (disc-dump only database) at all. When a game has both retail and digital releases, get the ID from an actual image of each release rather than assuming one covers both.
