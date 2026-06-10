#!/usr/bin/env python3
"""kkr_unpack.py -- depack kkrieger-beta (kkrunchy_k7) to a COHERENT flat image.

The generic `era unpack` kkrunchy route stops at the import-resolution loop
(LoadLibraryA/GetProcAddress can't resolve with no Windows loader) and dumps the
image while the code section is still in kkrunchy_k7's split-stream *disasm-filtered*
form -- opcodes separated from their operands, so nothing disassembles. See NOTES.md.

kkrunchy_k7's stub order is: stage1 decompress -> resolve imports -> depack2
(DisUnFilter) -> jump OEP. The un-filter runs AFTER imports. So we STUB the two
imports (return fake nonzero handles/addresses) -> the import loop completes ->
depack2 un-filters the .text -> coherent code. We then dump.

This reproduces `unpacked_fixed.bin` (base 0x7c0000). The synth-entry VAs and the
oracle build live in NOTES.md.

Per-binary constants below were read from the kkrieger-beta stub (see NOTES.md):
  IMPORT_LOOP   the import-resolution loop entry (after decompression)
  LOADLIB_SLOT  the [LoadLibraryA, GetProcAddress] IAT slot pair the loop calls
Reproducible from ~/downloads/kkrieger-beta.zip -> pno0001.exe.
"""
import sys, struct, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "toolkit"))
import packers
from unicorn import *
from unicorn.x86_const import *

EXE = sys.argv[1] if len(sys.argv) > 1 else "/tmp/oracle_v5/kkrieger/pno0001.exe"
OUT = sys.argv[2] if len(sys.argv) > 2 else "unpacked_fixed.bin"

# kkrieger-beta stub constants (base 0x7c0000)
IMPORT_LOOP  = 0x7d7ab4   # `mov esi,IMPORTS; mov ebx,LOADLIBRARY` -- loop entry
LOADLIB_SLOT = 0x7d7b9f   # [+0]=LoadLibraryA slot, [+4]=GetProcAddress slot
STUBP        = 0x10000000 # scratch page for the import stubs

data = open(EXE, "rb").read()
pe = packers.PE(data)
base = pe.imagebase
total = (pe.sizeofimage + 0xFFF) & ~0xFFF
mu = Uc(UC_ARCH_X86, UC_MODE_32)
mu.mem_map(base, total); mu.mem_write(base, data)
stk = 0x200000; mu.mem_map(stk, 0x100000)
mu.reg_write(UC_X86_REG_ESP, stk + 0x80000); mu.reg_write(UC_X86_REG_EBP, stk + 0x80000)

# import stubs: LoadLibraryA (stdcall ret 4) and GetProcAddress (stdcall ret 8),
# each returns a fake nonzero value so the loop's `test eax,eax` passes.
mu.mem_map(STUBP, 0x1000)
mu.mem_write(STUBP + 0x00, bytes.fromhex("B820000010") + bytes.fromhex("C20400"))  # mov eax,0x10000020; ret 4
mu.mem_write(STUBP + 0x10, bytes.fromhex("B820000010") + bytes.fromhex("C20800"))  # mov eax,0x10000020; ret 8

BLK = 0x100000; mapped = set()
patched = [False]

def on_code(mu_, addr, size, ud):
    if addr == IMPORT_LOOP and not patched[0]:
        mu_.mem_write(LOADLIB_SLOT, struct.pack('<II', STUBP + 0x00, STUBP + 0x10))
        patched[0] = True

def on_fetch(mu_, access, addr, size, value, ud):
    mu_.emu_stop(); return False  # the OEP/API fault after un-filter = our stop

def on_data(mu_, access, addr, size, value, ud):
    blk = addr & ~(BLK - 1)
    if blk in mapped: return True
    try: mu_.mem_map(blk, BLK); mapped.add(blk); return True
    except Exception:
        try: mu_.mem_map(addr & ~0xFFF, 0x1000); return True
        except Exception: return False

mu.hook_add(UC_HOOK_CODE, on_code)
mu.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED, on_fetch)
mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED, on_data)
try:
    mu.emu_start(pe.entry, 0, count=300000000)
except UcError:
    pass

out = bytearray(total)
for a in range(0, total, 0x1000):
    try: out[a:a + 0x1000] = mu.mem_read(base + a, 0x1000)
    except Exception: pass
open(OUT, "wb").write(out)

# sanity: the un-filtered .text must contain the address-free fild-loop heart
heart = out.find(bytes.fromhex("d91f8d7f0449"))
imul  = out.find(bytes.fromhex("69c03584b30b"))
print("imports patched: %s" % patched[0])
print("wrote %d bytes -> %s (base %#x)" % (len(out), OUT, base))
print("coherence: fild-heart %s, imul-LCG %s" %
      ("OK @%#x" % (heart + base) if heart >= 0 else "ABSENT",
       "OK @%#x" % (imul + base) if imul >= 0 else "ABSENT"))
sys.exit(0 if heart >= 0 else 1)
