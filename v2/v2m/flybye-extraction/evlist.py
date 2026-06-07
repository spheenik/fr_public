#!/usr/bin/env python3
# evlist.py -- list v2m sequencer events with absolute tick + sample + seconds.
# Layout per portable v2seq.cpp InitBase; tick->sample via the same
# (nexttime-time)*usecs/timediv2 64-bit walk the player does (UpdateSampleDelta).
import sys, struct

SR = 44100

def u32(b, o): return struct.unpack_from('<I', b, o)[0]

def parse(path):
    d = open(path, 'rb').read()
    timediv = u32(d, 0); maxtime = u32(d, 4); gdnum = u32(d, 8)
    o = 12
    gptr = o; o += 10 * gdnum
    chans = []
    for ch in range(16):
        notenum = u32(d, o); o += 4
        c = dict(ch=ch, notenum=notenum)
        if notenum:
            c['noteptr'] = o; o += 5 * notenum
            c['pcnum'] = u32(d, o); o += 4
            c['pcptr'] = o; o += 4 * c['pcnum']
            c['pbnum'] = u32(d, o); o += 4
            c['pbptr'] = o; o += 5 * c['pbnum']
            c['cc'] = []
            for cn in range(7):
                ccnum = u32(d, o); o += 4
                c['cc'].append((ccnum, o)); o += 4 * ccnum
        chans.append(c)
    return d, timediv, maxtime, gdnum, gptr, chans

def deltas(d, ptr, n, w3=3):
    # SoA 24-bit time deltas: low[n], mid[n], high[n] -> absolute ticks
    t = 0; out = []
    for i in range(n):
        delta = d[ptr+i] + (d[ptr+n+i] << 8) + (d[ptr+2*n+i] << 16)
        t += delta
        out.append(t)
    return out

def vals(d, ptr, n, stream):
    # delta-coded value stream #stream (after the 3 time streams)
    v = 0; out = []
    for i in range(n):
        v = (v + d[ptr + stream*n + i]) & 0xff
        out.append(v)
    return out

def main():
    path = sys.argv[1]
    lo = float(sys.argv[2]) if len(sys.argv) > 3 else 0.0
    hi = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
    d, timediv, maxtime, gdnum, gptr, chans = parse(path)
    timediv2 = 10000 * timediv
    print(f"timediv={timediv} maxtime={maxtime} gdnum={gdnum}")

    # global (tempo) events
    gt = deltas(d, gptr, gdnum)
    gus = [u32(d, gptr + 3*gdnum + 4*i) for i in range(gdnum)]

    evs = []   # (tick, kind, ch, info)
    for i in range(gdnum):
        evs.append((gt[i], 'TEMPO', -1, f"usecs={gus[i]}"))
    for c in chans:
        if not c['notenum']: continue
        ch = c['ch']; n = c['notenum']
        ts = deltas(d, c['noteptr'], n)
        nts = vals(d, c['noteptr'], n, 3)
        vls = vals(d, c['noteptr'], n, 4)
        for t, nt, vl in zip(ts, nts, vls):
            evs.append((t, 'NOTE', ch, f"note={nt} vel={vl}" + (" (OFF)" if vl == 0 else "")))
        pn = c['pcnum']
        for t, pc in zip(deltas(d, c['pcptr'], pn), vals(d, c['pcptr'], pn, 3)):
            evs.append((t, 'PGM', ch, f"pgm={pc}"))
        bn = c['pbnum']
        if bn:
            for t, b0, b1 in zip(deltas(d, c['pbptr'], bn), vals(d, c['pbptr'], bn, 3), vals(d, c['pbptr'], bn, 4)):
                evs.append((t, 'PB', ch, f"pb0={b0} pb1={b1}"))
        for cn, (ccnum, ccptr) in enumerate(c['cc']):
            for t, v in zip(deltas(d, ccptr, ccnum), vals(d, ccptr, ccnum, 3)):
                evs.append((t, f'CC{cn+1}', ch, f"val={v}"))
    evs.sort(key=lambda e: e[0])

    # tick -> sample walk (same math as UpdateSampleDelta; usecs scaled *(SR/100))
    # state: usecs default 5000*SR until first tempo event
    ticks = sorted(set(e[0] for e in evs))
    usecs = 5000 * SR
    gi = 0
    cur_t = 0; cur_smp = 0; rem = 0
    smp_at = {}
    for t in ticks:
        # advance from cur_t to t using current tempo; tempo events take effect AT their tick
        # (the player applies the global event when m_state.time==gnt, before rendering onward)
        # so segment [cur_t, t) uses tempo set at cur_t.
        prod = (t - cur_t) * usecs
        quot = prod // timediv2
        r = prod % timediv2
        newrem = (rem + r) & 0xffffffff
        carry = 1 if newrem < rem else 0
        rem = newrem
        cur_smp += quot + carry
        cur_t = t
        smp_at[t] = cur_smp
        while gi < gdnum and gt[gi] == t:
            usecs = gus[gi] * (SR // 100)
            gi += 1
    for t, kind, ch, info in evs:
        s = smp_at[t]
        sec = s / SR
        if lo <= sec <= hi:
            print(f"tick {t:8d}  smp {s:9d}  {sec:9.4f}s  ch{ch:2d}  {kind:5s} {info}")

if __name__ == '__main__':
    main()
