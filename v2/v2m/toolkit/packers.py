#!/usr/bin/env python3
"""packers -- detect the packer of a period farbrausch binary and unpack it.

detect_packer() reads PE section names (an exact, zero-ambiguity signature across
all the era 64ks). unpack() depacks aPLib- and kkrunchy-packed binaries by running
their depacker stub under Unicorn: map the image, emulate from the PE entry, let the
stub decompress in place, stop when it jumps to the unresolved OEP
(UC_ERR_INSN_INVALID -- imports are never patched, decompression is already done),
and dump the image. This is the proven flybye/fr08 recipe (the aPLib stub is pure
integer; the flybye image round-trips byte-identically), generalized to the kkrunchy
single-section stub via the same generic PE-stub emulation.

  detect_packer('fr-022.exe')           -> 'aplib'
  unpack('fr-022.exe', 'out.bin')       -> bytes (also writes out.bin if given)
"""

import struct

IMG_BASE = 0x400000


class Section:
    __slots__ = ("name", "va", "vsize", "raw", "rawsize")

    def __init__(self, name, va, vsize, raw, rawsize):
        self.name, self.va, self.vsize, self.raw, self.rawsize = name, va, vsize, raw, rawsize


class PE:
    """Minimal 32-bit PE view: entry, imagebase, sizeofimage, sections."""

    def __init__(self, data):
        if data[:2] != b"MZ":
            raise ValueError("not an MZ/PE image")
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        self.nsec = struct.unpack_from("<H", data, pe + 6)[0]
        opt = struct.unpack_from("<H", data, pe + 20)[0]
        self.entry_rva = struct.unpack_from("<I", data, pe + 40)[0]
        self.imagebase = struct.unpack_from("<I", data, pe + 52)[0]
        self.sizeofimage = struct.unpack_from("<I", data, pe + 80)[0]
        self.sections = []
        off = pe + 24 + opt
        for _ in range(self.nsec):
            # strip NUL padding and ryg's trailing-space padding ("packer. ")
            name = data[off:off + 8].rstrip(b"\0 ").decode("latin1")
            vsz, va, rsz, ro = struct.unpack_from("<IIII", data, off + 8)
            self.sections.append(Section(name, va, vsz, ro, rsz))
            off += 40

    @property
    def entry(self):
        return self.imagebase + self.entry_rva

    def section_names(self):
        return [s.name for s in self.sections]


# packer signatures keyed on the set of section names present
def detect_packer(arg):
    """Return 'aplib' | 'kkrunchy' | 'ruletool' | 'none' for a PE (path or bytes)."""
    data = arg if isinstance(arg, (bytes, bytearray)) else open(arg, "rb").read()
    names = set(PE(data).section_names())
    if {"rygs and", "packer."} & names == {"rygs and", "packer."}:
        return "aplib"
    if "kkrunchy" in names:
        return "kkrunchy"
    if {"ruletool", "resultat"} & names == {"ruletool", "resultat"}:
        return "ruletool"
    return "none"


def _emulate_stub(data, pe):
    """Run a self-decompressing depacker stub under Unicorn; return the dumped image.

    Used for both aplib and kkrunchy: the route is identical generic PE-stub
    emulation. Imports are never resolved -- the stub faults on its jump to the OEP
    once decompression has finished, which is our stop signal.
    """
    from unicorn import (
        Uc, UcError, UC_ARCH_X86, UC_MODE_32,
        UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED, UC_HOOK_MEM_FETCH_UNMAPPED,
    )
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EIP

    base = pe.imagebase
    total = (pe.sizeofimage + 0xFFF) & ~0xFFF
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(base, total)
    mu.mem_write(base, data[:0x400])  # headers
    for s in pe.sections:
        if s.rawsize:
            mu.mem_write(base + s.va, data[s.raw:s.raw + s.rawsize])

    stk = 0x200000
    mu.mem_map(stk, 0x100000)
    mu.reg_write(UC_X86_REG_ESP, stk + 0x80000)
    mu.reg_write(UC_X86_REG_EBP, stk + 0x80000)

    def on_unmapped(mu_, access, addr, size, value, ud):
        try:
            mu_.mem_map(addr & ~0xFFF, 0x1000)
            return True
        except Exception:
            return False

    mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED |
                UC_HOOK_MEM_FETCH_UNMAPPED, on_unmapped)

    try:
        mu.emu_start(pe.entry, 0, count=200000000)
    except UcError as e:
        # expected: stub jumps to unresolved OEP -> invalid instruction
        eip = mu.reg_read(UC_X86_REG_EIP)
        _emulate_stub.last_stop = (str(e), eip)

    out = bytearray(total)
    for a in range(0, total, 0x1000):
        try:
            out[a:a + 0x1000] = mu.mem_read(base + a, 0x1000)
        except Exception:
            pass
    return bytes(out)


def unpack(path, out=None):
    """Depack `path` to its flat image (bytes). Writes `out` too if given.

    aplib/kkrunchy  -> Unicorn stub emulation
    none            -> the file image is returned unchanged (carve directly)
    ruletool        -> NotImplementedError (earlier ryg packer, out of scope)
    """
    data = open(path, "rb").read()
    packer = detect_packer(data)
    if packer == "ruletool":
        raise NotImplementedError(
            "ruletool/resultat packer (e.g. fr011) is detect-only; "
            "reverse-engineering it is out of scope")
    pe = PE(data)
    if packer == "none":
        image = data
    else:
        image = _emulate_stub(data, pe)
    if out:
        open(out, "wb").write(image)
    return image
