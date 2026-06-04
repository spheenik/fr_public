#!/usr/bin/env python3
# print objdump lines whose VA falls in [lo,hi)
import sys, re
lo = int(sys.argv[1], 16); hi = int(sys.argv[2], 16)
for line in open('/tmp/fr08/fr08_objdump.txt'):
    m = re.match(r'\s*([0-9a-f]{6,8}):', line)
    if m and lo <= int(m.group(1), 16) < hi:
        sys.stdout.write(line)
