#!/usr/bin/env python3
# Segment the 2000 synth code into functions (at call targets) and fingerprint
# each by referenced pool constants + called subroutines.
import re

POOL_LO, POOL_HI = 0x40a400, 0x40a464
CODE_LO, CODE_HI = 0x40a464, 0x40c800

POOL = {  # va -> name (from constscan match to 2004 synth.asm)
 0x40a400:'fci12',0x40a404:'fc2',0x40a408:'fc32bit',0x40a40c:'fci128',
 0x40a410:'fci4',0x40a414:'fci2',0x40a418:'fc2pi',0x40a41c:'fci256',
 0x40a420:'fc64',0x40a424:'fc32',0x40a428:'fci16',0x40a42c:'fcipi_2',
 0x40a430:'fc32768',0x40a434:'fc256',0x40a438:'fc10',0x40a43c:'fci32768',
 0x40a440:'fc1023',0x40a444:'fcOscPitchOffs',0x40a448:'fcattackmul11',
 0x40a44c:'fcattackadd',0x40a450:'fcsusmul',0x40a454:'fcgain',
 0x40a458:'fcgainh',0x40a45c:'fcUNK3185015',0x40a460:'fcmdlfomul',
}

insns = []  # (va, mnem, operand_text, raw)
for line in open('/tmp/fr08/fr08_objdump.txt'):
    m = re.match(r'\s*([0-9a-f]{6,8}):\t([0-9a-f ]+)\t(\S+)\s*(.*)', line)
    if not m: continue
    va = int(m.group(1), 16)
    insns.append((va, m.group(3), m.group(4).strip()))

# call targets within window = function entries
entries = set([CODE_LO])
disp_re = re.compile(r'0x([0-9a-f]+)')
for va, mn, op in insns:
    if mn == 'call':
        mm = disp_re.search(op)
        if mm:
            t = int(mm.group(1), 16)
            if CODE_LO <= t < CODE_HI: entries.add(t)
entries = sorted(entries)

def fn_of(va):
    lo = CODE_LO
    for e in entries:
        if e <= va: lo = e
        else: break
    return lo

# fingerprint each function
from collections import defaultdict
consts = defaultdict(list)
calls  = defaultdict(set)
size   = {}
for i, e in enumerate(entries):
    nxt = entries[i+1] if i+1 < len(entries) else CODE_HI
    size[e] = nxt - e
for va, mn, op in insns:
    f = fn_of(va)
    for mm in disp_re.finditer(op):
        t = int(mm.group(1), 16)
        if POOL_LO <= t < POOL_HI:
            nm = POOL.get(t & ~3, hex(t))
            if nm not in consts[f]: consts[f].append(nm)
    if mn == 'call':
        mm = disp_re.search(op)
        if mm: calls[f].add(int(mm.group(1),16))

print(f'{len(entries)} functions in 0x40a464-0x40c800\n')
for e in entries:
    cs = ' '.join(consts[e])
    cl = ' '.join(f'->{c-CODE_LO+0xa464:x}' if False else hex(c) for c in sorted(calls[e]))
    print(f'{e:#08x} sz={size[e]:<5} consts[{cs}]  calls[{cl}]')
