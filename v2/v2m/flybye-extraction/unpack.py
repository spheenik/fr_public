#!/usr/bin/env python3
# unpack.py -- static aPLib depacker for the "rygs and packer." stub used by
# the year-2001/2002 farbrausch 64k releases (fr-013 flybye, fr-022 ein.schlag).
#
# Both exes have the same 3-section PE layout (section[0] "rygs and", a huge
# zero-rawsize CODE section that is the in-place decompression target; the
# real packed stream lives in section[1] "packer."). We map the file image,
# emulate from the entry point, and let the stub decompress in place; it stops
# with UC_ERR_INSN_INVALID when it jumps to the unresolved OEP (imports are
# never patched -- decompression is already complete by then), exactly like
# the fr08 recipe (../fr08-extraction/unpack.py). Then we dump 0x400000.
#
# This host's ptrace_scope=1 blocks the fr08 live-wine /proc/pid/mem dump
# (the dumper is not an ancestor of the wineserver-reparented guest), so the
# static route is the only one that works here.
#
#   pip install unicorn   # tested with unicorn 2.1.4
#   ./unpack.py flybye.exe flybye_unpacked.bin
#   ./unpack.py fr-022.exe fr022_unpacked.bin

import struct, sys
from unicorn import *
from unicorn.x86_const import *

PATH = sys.argv[1] if len(sys.argv) > 1 else "flybye.exe"
OUT  = sys.argv[2] if len(sys.argv) > 2 else "unpacked.bin"

d = open(PATH, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
ep = struct.unpack_from('<I', d, pe + 40)[0]
ib = struct.unpack_from('<I', d, pe + 52)[0]
sizeimg = struct.unpack_from('<I', d, pe + 80)[0]
print('entry', hex(ib + ep), 'imagebase', hex(ib), 'sizeimage', hex(sizeimg))

mu = Uc(UC_ARCH_X86, UC_MODE_32)
base = ib
total = (sizeimg + 0xfff) & ~0xfff
mu.mem_map(base, total)
mu.mem_write(base, d[:0x400])
off = pe + 24 + opt
for i in range(nsec):
    name = d[off:off + 8].rstrip(b'\0')
    vsz, va, rsz, ro = struct.unpack_from('<IIII', d, off + 8)
    if rsz:
        mu.mem_write(base + va, d[ro:ro + rsz])
    off += 40
    print('sec', name, hex(va), hex(vsz), 'raw', hex(ro), hex(rsz))

stk = 0x200000
mu.mem_map(stk, 0x100000)
mu.reg_write(UC_X86_REG_ESP, stk + 0x80000)
mu.reg_write(UC_X86_REG_EBP, stk + 0x80000)

def hook_unmapped(mu, access, addr, size, value, ud):
    try:
        mu.mem_map(addr & ~0xfff, 0x1000)
        return True
    except Exception:
        return False
mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED |
            UC_HOOK_MEM_FETCH_UNMAPPED, hook_unmapped)

cnt = [0]
def hook_code(mu, addr, size, ud):
    cnt[0] += 1
mu.hook_add(UC_HOOK_CODE, hook_code)

try:
    mu.emu_start(ib + ep, 0, count=200000000)
except UcError as e:
    print('stop:', e, 'eip', hex(mu.reg_read(UC_X86_REG_EIP)), 'instrs', cnt[0])

out = bytearray(total)
for a in range(0, total, 0x1000):
    try:
        out[a:a + 0x1000] = mu.mem_read(base + a, 0x1000)
    except Exception:
        pass
open(OUT, 'wb').write(out)
print('dumped', len(out), '->', OUT)
