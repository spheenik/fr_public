#!/usr/bin/env python3
# disasm.py -- disassemble a flat unpacked V2 image (VA = 0x400000 + offset).
# Used to read the four signature-less player/render rows out of flybye and
# compare to fr08 (v0) / the v5 binaries. See NOTES.md "Disassembly".
#
#   pip install capstone   # tested 5.0.7
#   ./disasm.py flybye_unpacked.bin 0x410455 40
#
# (script name intentionally NOT 'dis.py' -- that shadows the stdlib 'dis'
# module capstone imports, breaking the import.)

import sys, capstone
path = sys.argv[1]; va = int(sys.argv[2], 16)
n = int(sys.argv[3]) if len(sys.argv) > 3 else 60
d = open(path, 'rb').read(); off = va - 0x400000
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for i, ins in enumerate(md.disasm(d[off:off + 400], va)):
    if i >= n: break
    print('%08x  %-20s %s %s' % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
