import struct
# Faithful port of the fr-08 depacker stub (aPLib variant) at 0x7296b0.
exe=open('/tmp/fr08/fr08v101.exe','rb').read()
src=exe[0x200:]          # packer section onward (esi = 0x71a000 -> file 0x200)
s=0
dl=0x80                  # bit tag

def getbit():
    global dl,s
    dl<<=1
    cf=(dl>>8)&1
    dl&=0xff
    if dl!=0:
        return cf
    dl=src[s]; s+=1
    dl=(dl<<1)|cf          # adc dl,dl  (cf preserved across mov/inc)
    cf2=(dl>>8)&1
    dl&=0xff
    return cf2

def getgamma():
    # interlaced gamma: result=1; do{ result=result*2+bit; }while(bit2)
    r=1
    while True:
        r=r*2+getbit()
        if getbit()==0:
            break
    return r

dst=bytearray()
dst.append(src[s]); s+=1   # first literal (movsb)
R0=0xffffffff              # ebp = -1

while True:
    if getbit()==0:                 # "0" : literal
        dst.append(src[s]); s+=1
        continue
    if getbit()==0:                 # "10" : gamma offset / reuse R0
        G=getgamma()
        if G==2:                    # reuse last offset
            offs=R0
            length=getgamma()
        else:
            high=G-3
            offs=(high<<8)|src[s]; s+=1
            length=getgamma()
            if offs>=0x7d00: length+=2
            elif offs>=0x500: length+=1
            elif offs<0x80: length+=2
            R0=offs
        for _ in range(length):
            dst.append(dst[len(dst)-offs])
        continue
    if getbit()==0:                 # "110" : 7-bit offset + len bit, offs==0 -> end
        b=src[s]; s+=1
        length=2+(b&1)
        offs=b>>1
        if offs==0:
            break                   # end of stream -> e8/e9 fixup + jmp OEP
        R0=offs
        for _ in range(length):
            dst.append(dst[len(dst)-offs])
        continue
    # "111" : 4-bit short offset, single byte (or literal 0)
    offs=0
    for _ in range(4):
        offs=(offs<<1)+getbit()
    if offs:
        dst.append(dst[len(dst)-offs])
    else:
        dst.append(0)

print('decoded %d bytes (target 0x319000 = %d), consumed %d/%d src'%(len(dst),0x319000,s,len(src)))
open('/tmp/fr08/depacked.bin','wb').write(dst)
# sanity: demo strings
for tok in (b'farb-rausch',b'the product',b'Direct3D',b'fr-08',b'.fr-08'):
    print(tok, dst.count(tok))
