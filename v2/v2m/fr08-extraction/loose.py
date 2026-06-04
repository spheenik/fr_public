import struct,sys
def u32(d,o): return struct.unpack_from('<I',d,o)[0]
d=open(sys.argv[1],'rb').read()
n=len(d); hits=[]
for o in range(0,n-12):
    timediv=u32(d,o)
    if not(32<=timediv<=2000): continue
    maxtime=u32(d,o+4)
    if not(2000<=maxtime<=40_000_000): continue
    gdnum=u32(d,o+8)
    if not(1<=gdnum<=8000): continue
    # try walking channels loosely
    p=o+12+10*gdnum; ok=True; active=0
    if p+4>n: continue
    for ch in range(16):
        if p+4>n: ok=False;break
        nn=u32(d,p);p+=4
        if nn>500000: ok=False;break
        if nn: active+=1; p+=5*nn
        if nn:
            for fld,mul in [(4,1)]:  # pcnum
                if p+4>n: ok=False;break
                v=u32(d,p);p+=4
                if v>500000: ok=False;break
                p+=4*v
            if not ok: break
            if p+4>n: ok=False;break
            v=u32(d,p);p+=4
            if v>500000: ok=False;break
            p+=5*v
            for cn in range(7):
                if p+4>n: ok=False;break
                v=u32(d,p);p+=4
                if v>500000: ok=False;break
                p+=4*v
            if not ok: break
    if ok and active>=4:
        hits.append((o,timediv,maxtime,gdnum,active,p-o))
hits.sort(key=lambda x:-x[5])
print('loose channel-walk hits:',len(hits))
for h in hits[:10]:
    print('off=%#x timediv=%d maxtime=%d gdnum=%d active=%d body=%d'%h)
