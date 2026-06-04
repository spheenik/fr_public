#!/usr/bin/env python3
# step B 1.4/1.5: find code references (disp32) to 2000-pool constants
import struct

img = open('/tmp/fr08/unpacked.bin', 'rb').read()
BASE = 0x400000

TARGETS = [
    ('attackmul_old(-11/128)', 0x40a448),
    ('attackadd(7.0)',         0x40a44c),
    ('susmul(0.0019375)',      0x40a450),
    ('fci256(1/256)',          0x40a41c),
    ('fci128',                 0x40a40c),
    ('gain(0.6)',              0x40a454),
    ('gainh(0.6)',             0x40a458),
    ('unk(3185015.0)',         0x40a45c),
    ('mdlfomul(1973915.5)',    0x40a460),
    ('OscPitchOffs(60.0)',     0x40a444),
    ('fc1023',                 0x40a440),
    ('fc2pi',                  0x40a418),
]

for name, va in TARGETS:
    pat = struct.pack('<I', va)
    hits, i = [], img.find(pat)
    while i >= 0:
        hits.append(hex(BASE + i))
        i = img.find(pat, i + 1)
    print(f'{name:<24} {len(hits):<3} {" ".join(hits)}')
