import struct, capstone
d=open('/tmp/fr08/fr08v101.exe','rb').read()
IB=0x400000
# packer section: va 0x31a000, raw 0x200
SEC_VA=0x31a000; SEC_RAW=0x200; SEC_SZ=0xf800
def fo2va(fo): return IB+SEC_VA+(fo-SEC_RAW)
def va2fo(va): return SEC_RAW+(va-IB-SEC_VA)
ep_va=0x7296b0
ep_fo=va2fo(ep_va)
print('entry va=%#x file_off=%#x  (section ends at file %#x)'%(ep_va,ep_fo,SEC_RAW+SEC_SZ))
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
md.detail=True
# disassemble from entry to end of section
start=ep_fo
code=d[start:SEC_RAW+SEC_SZ]
for ins in md.disasm(code, fo2va(start)):
    print('%08x  %-22s %s %s'%(ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
