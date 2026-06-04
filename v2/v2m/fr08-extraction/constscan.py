#!/usr/bin/env python3
# step B 1.2/1.3: extract float constants from 2004 synth.asm, scan the
# depacked fr08 image (PE at 0x400000) for their bit patterns.
import re, struct, sys

ASM = '/home/spheenik/projects/scene/fr_public/v2/synth.asm'
IMG = '/tmp/fr08/unpacked.bin'
BASE = 0x400000

img = open(IMG, 'rb').read()

def scan(pat):
    hits, i = [], img.find(pat)
    while i >= 0:
        hits.append(BASE + i)
        i = img.find(pat, i + 1)
    return hits

consts = []   # (name, kind, value)
for line in open(ASM, encoding='latin-1'):
    m = re.match(r'^(\w+)\s+(dd|dq)\s+(.+?)(;.*)?$', line.strip())
    if m:
        name, kind, vals = m.group(1), m.group(2), m.group(3)
        for j, v in enumerate(v.strip() for v in vals.split(',')):
            try: consts.append((name if j == 0 else f'{name}+{j}', kind, float(v)))
            except ValueError: pass
    m = re.match(r'^(\w+)\s+equ\s+(-?\d+)\s*(;.*)?$', line.strip())
    if m:
        consts.append((m.group(1), 'equ', int(m.group(2))))

print(f'{len(consts)} constants from synth.asm\n')
print(f'{"name":<16}{"kind":<5}{"value":<22}{"hits":<6}VAs')
for name, kind, val in consts:
    if kind == 'dd':
        pat = struct.pack('<f', val)
    elif kind == 'dq':
        pat = struct.pack('<d', val)
    else:
        pat = struct.pack('<i', val) if -2**31 <= val < 2**31 else struct.pack('<I', val)
    hits = scan(pat)
    # distinctive = rare in image; print all, flag the useful ones
    mark = ' ' if len(hits) == 0 else ('*' if len(hits) <= 4 else '+')
    vas = ' '.join(hex(h) for h in hits[:6]) + (' ...' if len(hits) > 6 else '')
    print(f'{mark}{name:<15}{kind:<5}{val:<22}{len(hits):<6}{vas}')
