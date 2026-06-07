#!/usr/bin/env python3
# profile.py -- feature-USAGE profiler for v2m files. Reads each patch's
# osc/VCF/dist mode + LFO-polarity values (not just the param COUNT) to ask:
# is there any in-file signal -- beyond the format version -- that reveals the
# engine BUILD that authored a file?
#
#   ./profile.py a.v2m b.v2m ...
#
# Motivation: format version (param-count/globSize) is the only EXPLICIT
# version marker. Feature usage gives a LOWER BOUND on engine capability (a
# song using FM osc / Moog VCF / Aux / LFO polarity can only come from an
# engine that has them). The question for the build-date-vs-format problem
# (NOTES.md): can usage separate early-v5 (fr-022, old core) from late-v5
# (candytron, new core)?
#
# FINDING (NEGATIVE): no. The v5 feature SET is identical across all v5 files,
# and whether a song USES a feature is song-dependent, not build-dependent --
# drumtro3 (2002) uses FM+polarity exactly like josie (2003). The trio that
# actually differs (LCG / osc-freq / box-vs-OSM) is pure renderer internals
# with ZERO file representation. So usage corroborates the format version but
# cannot sub-divide a format version by build. The only build-era signal that
# ever existed is EXTERNAL (the source binary's date). See NOTES.md.
#
# Reference run (v0/v1/v5 spread):
#   fr08      v0 td=480 FM=0 pol=0          (2000, old core)
#   flybye    v1 td=480 FM=0 pol=0          (2001, old core)
#   fr022     v5 td=96  FM=0 pol=0          (2002-08, old core) <- same feature
#   drumtro3  v5 td=96  FM=4 pol=6          (2002,    old?)        set as the
#   invtro    v5 td=480 FM=1 pol=4          (2003)                 late-v5 ones;
#   josie     v5 td=480 FM=2 pol=6          (2003-08, new core)    usage is
#   kkrieger6 v5 td=96  FM=2 pol=12         (new core)             song-driven.
#
# Patch layout: params are stored in v2parms order, only those with
# version<=fver, packed. packoff() maps a full-table (v6) param index to its
# byte offset within a patch at format version fver.

import struct, sys

# kPatchParmVer (v2load.h / sounddef.h v2parms[].version), full v6 order:
VER = [0,2,                          # Voice: Panning, Txpose(v2)
       0,2,0,0,0,0,                  # Osc1: Mode,Ring(v2),Txp,Det,Col,Vol
       0,2,0,0,0,0,                  # Osc2
       0,2,0,0,0,0,                  # Osc3
       0,0,0, 0,0,0,                 # VCF1, VCF2: Mode,Cutoff,Reso
       0,3,                          # Routing, Balance(v3)
       0,0,0,0,                      # Voice Dist: Mode,InGain,P1,P2
       0,0,0,0,0,0, 0,0,0,0,0,0,     # Amp EG, EG2
       0,0,0,0,0,5,0,                # LFO1: ...,Polarity(v5),Amplify
       0,0,0,0,0,5,0,                # LFO2
       0,0,6,6,6,6,0,0,0,1,          # Globals: ...,AuxA/B(v6)...,Boost(v1)
       0,0,0,0,                      # Channel Dist: Mode,InGain,P1,P2
       0,0,0,0,0,0,0,                # Chorus/Flanger
       1,1,1,1,1,1,1,1,1,            # Compressor (all v1)
       0]                            # MaxPoly

def packoff(i, fver): return sum(1 for j in range(i) if VER[j] <= fver)
def u32(d, o): return struct.unpack_from('<I', d, o)[0]

def parse(path):
    d = open(path, 'rb').read()
    timediv = u32(d, 0); maxtime = u32(d, 4); gdnum = u32(d, 8)
    p = 12 + 10 * gdnum
    for ch in range(16):
        nn = u32(d, p); p += 4
        if nn:
            p += 5 * nn
            pcn = u32(d, p); p += 4 + 4 * pcn
            pbn = u32(d, p); p += 4 + 5 * pbn
            for cn in range(7):
                ccn = u32(d, p); p += 4 + 4 * ccn
    gs = u32(d, p); p += 4 + gs
    ps = u32(d, p); p += 4; pm = p
    gsz = {0:12,1:21,2:21,3:21,4:22,5:22,6:23}
    pcz = {0:68,1:78,2:82,3:83,4:83,5:85,6:89}
    maxp = u32(d, pm) // 4
    fver = None
    for v in [v for v in range(7) if gsz[v] == gs]:
        if maxp < 2: fver = v; break
        gap = u32(d, pm + 4) - u32(d, pm)
        dd = gap - (pcz[v] + 1)
        if dd >= 0 and dd % 3 == 0: fver = v
    OSC = [2, 8, 14]; VCF = [20, 23]; DIST = [28, 68]; POL = [49, 56]
    fm = aux = moog_vcf = moog_dist = hidist = polset = 0
    modes = set(); vcfmodes = set(); distmodes = set()
    for pp in range(maxp):
        base = pm + u32(d, pm + 4 * pp)
        def g(idx): return d[base + packoff(idx, fver)]
        for o in OSC:
            m = g(o) & 7; modes.add(m)
            if m == 5: fm += 1
            if m in (6, 7): aux += 1
        for vf in VCF:
            m = g(vf) & 7; vcfmodes.add(m)
            if m in (6, 7): moog_vcf += 1
        for ds in DIST:
            m = g(ds); distmodes.add(m)
            if m == 10: moog_dist += 1
            if m in (5, 6, 7, 8, 9): hidist += 1
        for po in POL:
            if VER[po] <= fver and g(po) != 0: polset += 1
    return dict(v=fver, timediv=timediv, maxp=maxp,
                oscmodes=sorted(modes), vcfmodes=sorted(vcfmodes),
                distmodes=sorted(distmodes), FM=fm, AUX=aux,
                moogVCF=moog_vcf, moogDist=moog_dist, hiDist=hidist, polUsed=polset)

for f in sys.argv[1:]:
    try:
        r = parse(f); name = f.split('/')[-1]
        print(f"{name:28} v{r['v']} td={r['timediv']:<4} maxp={r['maxp']:<3} "
              f"osc={r['oscmodes']} vcf={r['vcfmodes']} dist={r['distmodes']} "
              f"FM={r['FM']} aux={r['AUX']} moogV={r['moogVCF']} "
              f"moogD={r['moogDist']} hiDist={r['hiDist']} pol={r['polUsed']}")
    except Exception as e:
        print(f"{f}: ERR {e}")
