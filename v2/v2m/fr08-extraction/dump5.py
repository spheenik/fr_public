import os,sys,time,re,signal

os.environ['WINEPREFIX']='/tmp/fr08/wp'
os.environ['WINEDEBUG']='-all'
os.environ['DISPLAY']=':77'

def wine_loader():
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try:
            cl=open('/proc/%s/cmdline'%p,'rb').read().replace(b'\0',b' ').decode()
        except Exception:
            continue
        if 'x86_64-unix/wine' in cl and 'fr08v101.exe' in cl:
            return int(p)
    return None

def dump(tp):
    total=0
    out=open('/tmp/fr08/guest.bin','wb')
    idx=open('/tmp/fr08/guest.idx','w')
    mem=open('/proc/%d/mem'%tp,'rb')
    for line in open('/proc/%d/maps'%tp).read().splitlines():
        m=re.match(r'([0-9a-f]+)-([0-9a-f]+) (....) ',line)
        if not m: continue
        a=int(m.group(1),16); b=int(m.group(2),16); perm=m.group(3)
        if 'r' not in perm: continue
        if b-a>256*1024*1024: continue
        # skip file-backed mappings (shared libs); keep anonymous (guest image + heap)
        rest=line[m.end():]
        if '/usr/' in rest or '.so' in rest:
            continue
        try:
            mem.seek(a); data=mem.read(b-a)
        except Exception:
            continue
        if not data or not any(data): continue
        idx.write('%#x-%#x %s len=%d fileoff=%#x %s\n'%(a,b,perm,len(data),total,rest.strip()))
        out.write(data); total+=len(data)
    mem.close(); out.close(); idx.close()
    return total

pid=os.fork()
if pid==0:
    os.chdir('/tmp/fr08')
    os.execvp('wine',['wine','fr08v101.exe'])
    os._exit(127)

tp=None
t0=time.time()
while time.time()-t0 < 15:
    tp=wine_loader()
    if tp: break
    time.sleep(0.02)

if not tp:
    print('loader never appeared'); os._exit(1)
print('loader pid',tp,'-- waiting 2.0s for unpack+guest load')
# freeze repeatedly so it can't exit while we wait
deadline=time.time()+2.0
while time.time()<deadline:
    try: os.kill(tp,0)
    except Exception:
        print('loader exited before we could freeze!'); break
    time.sleep(0.1)
try:
    os.kill(tp, signal.SIGSTOP)
    print('froze',tp)
except Exception as e:
    print('freeze failed',e)
time.sleep(0.2)
n=dump(tp)
print('dumped',n,'bytes')
try: os.kill(tp, signal.SIGKILL)
except Exception: pass
try: os.kill(pid,signal.SIGKILL)
except Exception: pass
os.system('pkill -9 -f fr08v101.exe 2>/dev/null')
