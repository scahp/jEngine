#!/usr/bin/env python3
"""Compare actual RGB pixels in the center scene region, excluding the ImGui panels."""
import json
import struct
import sys
import zlib
from pathlib import Path


def read_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    offset, compressed = 8, bytearray()
    while offset < len(data):
        size = struct.unpack_from('>I', data, offset)[0]
        kind, chunk = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + size]
        offset += size + 12
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', chunk)
        elif kind == b'IDAT':
            compressed.extend(chunk)
    assert depth == 8 and color in (2, 6) and interlace == 0
    channels = 3 if color == 2 else 4
    stride = width * channels
    raw = zlib.decompress(compressed)
    previous, pixels = bytearray(stride), bytearray()
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            a = row[i - channels] if i >= channels else 0
            b = previous[i]
            c = previous[i - channels] if i >= channels else 0
            estimate = a + b - c
            distances = (abs(estimate - a), abs(estimate - b), abs(estimate - c))
            paeth = (a, b, c)[distances.index(min(distances))]
            prediction = (0, a, b, (a + b) // 2, paeth)[filter_type]
            row[i] = (row[i] + prediction) & 255
        for x in range(width):
            pixels.extend(row[x * channels:x * channels + 3])
            if channels == 4:
                assert row[x * channels + 3] == 255, 'Screenshot has unintended transparency'
        previous = row
    return width, height, pixels


w, h, enabled = read_png(sys.argv[1])
w2, h2, disabled = read_png(sys.argv[2])
assert (w, h) == (w2, h2)
changed, total, difference = 0, 0, 0
for y in range(40, h - 10):
    for x in range(305, w - 380):
        index = (y * w + x) * 3
        delta = sum(abs(enabled[index + c] - disabled[index + c]) for c in range(3))
        changed += delta > 0
        total += 1
        difference += delta
result = {'scene_pixels': total, 'changed_pixels': changed, 'mean_absolute_rgb_difference': difference / (total * 3)}
print(json.dumps(result, indent=2))
assert changed > 0, 'No visible difference in the scene region'
