#!/usr/bin/env python3
# inspect the second constant cluster around 0x444100-0x444360
import struct

img = open('/tmp/fr08/unpacked.bin', 'rb').read()
BASE = 0x400000

for va in range(0x444100, 0x444368, 4):
    off = va - BASE
    (f,) = struct.unpack_from('<f', img, off)
    (i,) = struct.unpack_from('<I', img, off)
    d = ''
    if va % 8 == 0 and off + 8 <= len(img):
        (dv,) = struct.unpack_from('<d', img, off)
        if 1e-12 < abs(dv) < 1e12:
            d = f'   dq {dv!r}'
    print(f'{va:#x}  {i:08x}  {f!r}{d}')
