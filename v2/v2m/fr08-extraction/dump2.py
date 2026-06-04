import os,sys,time,re,signal

os.environ['WINEPREFIX']='/tmp/fr08/wp'
os.environ['WINEDEBUG']='-all'
os.environ['DISPLAY']=':77'

pid=os.fork()
if pid==0:
    os.chdir('/tmp/fr08')
    os.execvp('wine',['wine','fr08v101.exe'])
    os._exit(127)

# parent: wait for unpack, then find loader descendants and dump their memory
time.sleep(7)

def loaders():
    res=[]
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try:
            cl=open('/proc/%s/cmdline'%p,'rb').read().replace(b'\0',b' ').decode()
        except Exception:
            continue
        if 'fr08v101.exe' in cl and 'x86_64-unix/wine' in cl:
            res.append(p)
    return res

targets=loaders()
print('loader pids:',targets)
total=0
out=open('/tmp/fr08/wine_full.bin','wb')
idx=open('/tmp/fr08/wine_full.idx','w')
for tp in targets:
    try:
        mem=open('/proc/%s/mem'%tp,'rb')
    except Exception as e:
        print('open mem fail',tp,e); continue
    for line in open('/proc/%s/maps'%tp):
        m=re.match(r'([0-9a-f]+)-([0-9a-f]+) (....) ',line)
        if not m: continue
        a=int(m.group(1),16); b=int(m.group(2),16); perm=m.group(3)
        if 'r' not in perm: continue
        if b-a>128*1024*1024: continue
        try:
            mem.seek(a); data=mem.read(b-a)
        except Exception:
            continue
        if not data or not any(data): continue
        idx.write('pid=%s %#x-%#x %s fileoff=%#x len=%d\n'%(tp,a,b,perm,total,len(data)))
        out.write(data); total+=len(data)
    mem.close()
out.close(); idx.close()
print('dumped',total,'bytes')
try: os.kill(pid,signal.SIGKILL)
except Exception: pass
os.system('pkill -9 -f fr08v101.exe 2>/dev/null')
