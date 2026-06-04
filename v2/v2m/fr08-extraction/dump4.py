import os,sys,time,re,signal

os.environ['WINEPREFIX']='/tmp/fr08/wp'
os.environ['WINEDEBUG']='-all'
os.environ['DISPLAY']=':77'

def loaders():
    res=[]
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try:
            cl=open('/proc/%s/cmdline'%p,'rb').read().replace(b'\0',b' ').decode()
        except Exception:
            continue
        if 'fr08v101.exe' in cl and 'x86_64-unix/wine' in cl:
            res.append(int(p))
    return res

def has_guest_image(tp):
    try:
        for line in open('/proc/%d/maps'%tp):
            m=re.match(r'0*400000-([0-9a-f]+) ',line)
            if m:
                return True
    except Exception:
        pass
    return False

def dump(tp):
    total=0
    out=open('/tmp/fr08/wine_full.bin','wb')
    idx=open('/tmp/fr08/wine_full.idx','w')
    mem=open('/proc/%d/mem'%tp,'rb')
    for line in open('/proc/%d/maps'%tp).read().splitlines():
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
        idx.write('%#x-%#x %s fileoff=%#x len=%d\n'%(a,b,perm,total,len(data)))
        out.write(data); total+=len(data)
    mem.close(); out.close(); idx.close()
    return total

pid=os.fork()
if pid==0:
    os.chdir('/tmp/fr08')
    os.execvp('wine',['wine','fr08v101.exe'])
    os._exit(127)

stopped=None
t0=time.time()
while time.time()-t0 < 25:
    ls=loaders()
    for tp in ls:
        if has_guest_image(tp):
            # let unpack finish, then freeze
            time.sleep(0.4)
            try: os.kill(tp, signal.SIGSTOP)
            except Exception: pass
            stopped=tp
            break
    if stopped: break
    time.sleep(0.02)

if not stopped:
    print('never saw guest image')
else:
    print('froze pid',stopped,'guest image present')
    n=dump(stopped)
    print('dumped',n,'bytes')
    try: os.kill(stopped, signal.SIGKILL)
    except Exception: pass

try: os.kill(pid,signal.SIGKILL)
except Exception: pass
os.system('pkill -9 -f fr08v101.exe 2>/dev/null')
