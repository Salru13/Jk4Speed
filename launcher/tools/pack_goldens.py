# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Compress the previews test_golden writes (build/preview/*.png, stored
deflate) into launcher/golden/<name>.png for every name in golden/goldens.txt.

    python3 -I launcher/tools/pack_goldens.py

Stdlib only. The pixels are unchanged; tests/test_goldens_png.py checks that
each committed PNG decodes to the hash in goldens.txt.
"""
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_png(path):
    """(width, height, rgb bytes) of an 8-bit RGB, non-interlaced PNG."""
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('%s: not a PNG' % path)
    pos, idat, w = 8, b'', None
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            if (depth, ctype, inter) != (8, 2, 0):
                raise ValueError('%s: only 8-bit RGB without interlace' % path)
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    row = w * 3
    out = bytearray(row * h)
    prev = bytearray(row)
    for y in range(h):
        f = raw[y * (row + 1)]
        line = bytearray(raw[y * (row + 1) + 1:(y + 1) * (row + 1)])
        for x in (range(row) if f else ()):
            a = line[x - 3] if x >= 3 else 0
            b = prev[x]
            c = prev[x - 3] if x >= 3 else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * row:(y + 1) * row] = line
        prev = line
    return w, h, bytes(out)


def bgrx(rgb):
    """The frame's memory layout (B, G, R, 0 per pixel), as test_golden hashes it."""
    out = bytearray(len(rgb) // 3 * 4)
    out[0::4] = rgb[2::3]
    out[1::4] = rgb[1::3]
    out[2::4] = rgb[0::3]
    return bytes(out)


def write_png(path, w, h, rgb):
    row = w * 3
    raw = b''.join(b'\x00' + rgb[y * row:(y + 1) * row] for y in range(h))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)

    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    tmp = path + '.tmp'
    open(tmp, 'wb').write(png)
    os.replace(tmp, path)


def goldens():
    with open(os.path.join(ROOT, 'golden', 'goldens.txt')) as f:
        return [line.split() for line in f if line.strip()]


def main():
    import hashlib
    for name, sha in goldens():
        src = os.path.join(ROOT, 'build', 'preview', name + '.png')
        w, h, rgb = read_png(src)
        if hashlib.sha256(bgrx(rgb)).hexdigest() != sha:
            sys.exit('%s: preview does not match goldens.txt; run test_golden first' % name)
        write_png(os.path.join(ROOT, 'golden', name + '.png'), w, h, rgb)
        print('packed', name)


if __name__ == '__main__':
    main()
