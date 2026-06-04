import struct,sys
def u32(d,o): return struct.unpack_from('<I',d,o)[0]
def try_parse(d,o):
    # replicate InitBase, return end offset or None
    n=len(d)
    try:
        if o+12>n: return None
        timediv=u32(d,o); maxtime=u32(d,o+4); gdnum=u32(d,o+8)
        if not(1<=timediv<=8000): return None
        if gdnum>100000: return None
        p=o+12+10*gdnum
        for ch in range(16):
            if p+4>n: return None
            notenum=u32(d,p); p+=4
            if notenum:
                if notenum>1000000: return None
                p+=5*notenum
                if p+4>n: return None
                pcnum=u32(d,p); p+=4; 
                if pcnum>1000000:return None
                p+=4*pcnum
                if p+4>n:return None
                pbnum=u32(d,p);p+=4
                if pbnum>1000000:return None
                p+=5*pbnum
                for cn in range(7):
                    if p+4>n:return None
                    ccnum=u32(d,p);p+=4
                    if ccnum>1000000:return None
                    p+=4*ccnum
        if p+4>n:return None
        size=u32(d,p)
        if size>16384: return None
        p+=4+size
        if p+4>n:return None
        size=u32(d,p)
        if size>1048576: return None
        p+=4+size
        if p+4>n:return None
        spsize=u32(d,p); p+=4
        if spsize and spsize<8192:
            p+=spsize
        # sanity: must have consumed something substantial and timediv/maxtime plausible
        if maxtime==0: return None
        return (p, timediv, maxtime, gdnum)
    except Exception:
        return None

d=open(sys.argv[1],'rb').read()
print('scanning',len(d),'bytes')
hits=[]
for o in range(0,len(d)-12,4):
    r=try_parse(d,o)
    if r:
        end,timediv,maxtime,gdnum=r
        sz=end-o
        if sz>=200:  # real songs are KB+
            hits.append((o,end,sz,timediv,maxtime,gdnum))
# filter: keep largest non-overlapping
hits.sort(key=lambda x:-x[2])
print('candidates:',len(hits))
for h in hits[:20]:
    print(f'off={h[0]:#x} end={h[1]:#x} size={h[2]} timediv={h[3]} maxtime={h[4]} gdnum={h[5]}')
