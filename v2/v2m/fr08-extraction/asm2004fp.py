#!/usr/bin/env python3
# Fingerprint 2004 synth.asm functions by the float-constants they reference,
# in source order, to align against the 2000 function map.
import re
ASM = '/home/spheenik/projects/scene/fr_public/v2/synth.asm'

# normalize 2000 vs 2004 const names that we know are renamed/equiv
ALIAS = {'fcattackmul':'fcattackmul11'}  # 2004 -12/128 vs 2000 -11/128 (same slot)

cur = None
fns = []  # (name, [consts in order])
order = []
for line in open(ASM, encoding='latin-1'):
    s = line.rstrip('\n')
    # function label: a bare label at col 0 (Word:) that isn't a .local or const
    m = re.match(r'^(\w+):', s)
    if m and not re.match(r'^\w+\s+(dd|dq|equ|resd|resb)\b', s):
        cur = m.group(1); fns.append((cur, []));
    mm = re.findall(r'\[(fc\w+|fci\w+)\]', s)
    if cur and mm:
        for c in mm:
            fns[-1][1].append(c)

for name, cs in fns:
    seen=[];
    for c in cs:
        if c not in seen: seen.append(c)
    if seen:
        print(f'{name:<18} {" ".join(seen)}')
