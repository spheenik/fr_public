#!/bin/bash
export WINEPREFIX=/tmp/fr08/wp WINEDEBUG=-all
cd /tmp/fr08
# launch in background under xvfb
xvfb-run -a wine fr08v101.exe >/tmp/fr08/wine.log 2>&1 &
WPID=$!
# poll for the actual wine child process holding the image
for i in $(seq 1 200); do
  for pid in $(pgrep -f fr08v101.exe); do
    if [ -r /proc/$pid/maps ]; then
      # check for a large committed region around 0x400000
      if grep -qiE '^0*400000-' /proc/$pid/maps 2>/dev/null; then
        echo "FOUND pid=$pid iter=$i"
        cp /proc/$pid/maps /tmp/fr08/maps.$pid
        # dump the image region(s)
        /tmp/fr08/venv/bin/python3 - "$pid" <<'PY'
import sys
pid=sys.argv[1]
out=open('/tmp/fr08/wine_image.bin','wb')
mem=open(f'/proc/{pid}/mem','rb')
start=0x400000; end=0x800000
data=b''
for base in range(start,end,0x1000):
    try:
        mem.seek(base); data+=mem.read(0x1000)
    except: data+=b'\0'*0x1000
out.write(data); out.close()
print('dumped',len(data),'bytes from',pid)
PY
        kill $WPID 2>/dev/null
        pkill -f fr08v101.exe 2>/dev/null
        exit 0
      fi
    fi
  done
done
echo "never found image region"
cat /tmp/fr08/wine.log
pkill -f fr08v101.exe 2>/dev/null
