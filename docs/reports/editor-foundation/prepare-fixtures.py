#!/usr/bin/env python3
"""Recreate the exact distinct-path PNG workload used in this report."""
from pathlib import Path
import struct
import zlib

root = Path(__file__).resolve().parents[3]
output = root / 'build/foundation/fixtures'
output.mkdir(parents=True, exist_ok=True)


def chunk(kind, data):
    return (struct.pack('>I', len(data)) + kind + data
            + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff))


raw = (b'\0' + bytes((53, 89, 144, 255)) * 1024) * 1024
png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1024, 1024, 8, 6, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
for index in range(24):
    (output / f'image-{index:02}.png').write_bytes(png)
print(f'Wrote 24 benchmark PNGs to {output}')
