import struct
from unicorn import *
from unicorn.x86_const import *

d=open('/tmp/fr08/fr08v101.exe','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]
opt=struct.unpack_from('<H',d,pe+20)[0]
ep=struct.unpack_from('<I',d,pe+40)[0]
ib=struct.unpack_from('<I',d,pe+52)[0]
sizeimg=struct.unpack_from('<I',d,pe+80)[0]
print('entry',hex(ib+ep),'imagebase',hex(ib),'sizeimage',hex(sizeimg))

mu=Uc(UC_ARCH_X86,UC_MODE_32)
# map full image region
base=ib
total=(sizeimg+0xfff)&~0xfff
mu.mem_map(base,total)
# headers
mu.mem_write(base, d[:0x400])
off=pe+24+opt
secs=[]
for i in range(nsec):
    name=d[off:off+8].rstrip(b'\0')
    vsz,va,rsz,ro=struct.unpack_from('<IIII',d,off+8)
    secs.append((name,va,vsz,ro,rsz))
    if rsz:
        mu.mem_write(base+va, d[ro:ro+rsz])
    off+=40
    print('sec',name,hex(va),hex(vsz),'raw',hex(ro),hex(rsz))

# stack
stk=0x200000
mu.mem_map(stk, 0x100000)
mu.reg_write(UC_X86_REG_ESP, stk+0x80000)
mu.reg_write(UC_X86_REG_EBP, stk+0x80000)

# a scratch page for any API thunk region high addresses
def hook_unmapped(mu,access,addr,size,value,ud):
    print('UNMAPPED',hex(access),hex(addr),'eip',hex(mu.reg_read(UC_X86_REG_EIP)))
    # map page and continue so depack loops don't die on edge
    try:
        mu.mem_map(addr & ~0xfff, 0x1000)
        return True
    except: return False
mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED|UC_HOOK_MEM_WRITE_UNMAPPED|UC_HOOK_MEM_FETCH_UNMAPPED, hook_unmapped)

cnt=[0]
def hook_code(mu,addr,size,ud):
    cnt[0]+=1
    # stop if we leave the stub section and enter newly-written code region (depacked entry)
mu.hook_add(UC_HOOK_CODE, hook_code)

try:
    mu.emu_start(ib+ep, 0, count=200000000)
except UcError as e:
    print('stop:',e,'eip',hex(mu.reg_read(UC_X86_REG_EIP)),'instrs',cnt[0])

# dump unpacked image
out=bytearray(total)
for a in range(0,total,0x1000):
    try: out[a:a+0x1000]=mu.mem_read(base+a,0x1000)
    except: pass
open('/tmp/fr08/unpacked.bin','wb').write(out)
print('dumped',len(out))
