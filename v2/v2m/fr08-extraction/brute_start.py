import struct,subprocess,os,math,collections
d=open('/tmp/fr08/fr08v101.exe','rb').read()
pad=b'\x00'*(4*1024*1024)
def ent(b):
    h=collections.Counter(b); n=len(b)
    return -sum(c/n*math.log2(c/n) for c in h.values())
best=[]
for start in range(0x1f0,0x260):
    stream=d[start:]+pad
    blob=b'\x00'+struct.pack('<I',0x319000)+stream
    open('/tmp/fr08/t.rk','wb').write(blob)
    try:
        subprocess.run(['/tmp/fr08/rekk/rekkrunchy','-d','/tmp/fr08/t.rk','/tmp/fr08/t.out'],
                       timeout=60,capture_output=True)
        o=open('/tmp/fr08/t.out','rb').read()
        e=ent(o[:4096])
        best.append((e,start))
    except Exception as ex:
        best.append((9.9,start))
best.sort()
for e,s in best[:8]:
    print('start=%#x entropy=%.3f'%(s,e))
