import struct,sys
def u32(d,o): return struct.unpack_from('<I',d,o)[0]

def try_parse(d,o):
    n=len(d)
    try:
        if o+12>n: return None
        timediv=u32(d,o); maxtime=u32(d,o+4); gdnum=u32(d,o+8)
        if not(32<=timediv<=2000): return None
        if not(2000<=maxtime<=40_000_000): return None
        if not(1<=gdnum<=8000): return None
        p=o+12+10*gdnum
        active=0
        for ch in range(16):
            if p+4>n: return None
            notenum=u32(d,p); p+=4
            if notenum:
                if notenum>500000: return None
                active+=1; p+=5*notenum
                if p+4>n: return None
                pcnum=u32(d,p); p+=4
                if pcnum>500000:return None
                p+=4*pcnum
                if p+4>n:return None
                pbnum=u32(d,p);p+=4
                if pbnum>500000:return None
                p+=5*pbnum
                for cn in range(7):
                    if p+4>n:return None
                    ccnum=u32(d,p);p+=4
                    if ccnum>500000:return None
                    p+=4*ccnum
        if active<2: return None
        if p+4>n:return None
        gsize=u32(d,p)
        if not(0<gsize<=16384): return None
        p+=4+gsize
        if p+4>n:return None
        psize=u32(d,p)
        if not(50<=psize<=1048576): return None
        p+=4+psize
        if p+4>n:return None
        spsize=u32(d,p); p+=4
        # full InitBase speech validation
        if spsize and spsize<8192:
            if p+spsize>n: return None
            speech=p
            nptr=u32(d,speech)
            if nptr>256: return None              # would overflow speechptrs[256]
            for i in range(nptr):
                if speech+4+i*4+4>n: return None
                off=u32(d,speech+4+i*4)
                if off>=spsize: return None       # pointer must stay inside speech blob
            p+=spsize
        return dict(start=o,end=p,size=p-o,timediv=timediv,maxtime=maxtime,
                    gdnum=gdnum,active=active,gsize=gsize,psize=psize,spsize=spsize)
    except Exception:
        return None

d=open(sys.argv[1],'rb').read()
print('scanning',len(d),'bytes')
hits=[]
for o in range(0,len(d)-12,1):
    r=try_parse(d,o)
    if r and r['size']>=2000:
        hits.append(r)
# keep non-overlapping, earliest-start wins, then largest
hits.sort(key=lambda h:(h['start'],-h['size']))
chosen=[]; covered=0
for h in hits:
    if h['start']>=covered:
        chosen.append(h); covered=h['end']
print('valid non-overlapping songs:',len(chosen),'(raw hits %d)'%len(hits))
for h in chosen:
    print('off=%#x end=%#x size=%d timediv=%d maxtime=%d active=%d psize=%d spsize=%d'%(
        h['start'],h['end'],h['size'],h['timediv'],h['maxtime'],h['active'],h['psize'],h['spsize']))
