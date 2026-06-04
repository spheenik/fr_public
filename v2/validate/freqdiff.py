#!/usr/bin/env python3
# Diff two freq-divergence ledgers (asm vs C++) produced by the validation harness
# under FREQLOG=<prefix>. Each ledger is a stream of 32-byte FreqLogRec (compat.h):
#   u32 kind(0=osc,1=lfo), offset, freq, pno, preround, r0, r1, r2   (little-endian)
# Records are call-for-call comparable: both cores compute freq in identical order.
#
# Reports, for the whole song:
#   - a structural desync (different record count, or kind/offset mismatch at the
#     same ordinal) -- a control-flow difference, distinct from a value divergence
#   - every freq-value divergence: ordinal, time (s), coordinate, the two freqs,
#     signed delta, and (once float capture is on) pure-tie vs upstream-pno
#
#   usage: freqdiff.py <asm-prefix> <cpp-prefix>
#     reads <prefix>.freqlog (records) and <prefix>.freqmap (ordinal->sample map)
import sys, struct

RECSZ = 32
SR = 44100.0

def load_log(path):
    with open(path, "rb") as f:
        data = f.read()
    n = len(data) // RECSZ
    recs = []
    for i in range(n):
        kind, off, freq, pno, pre, _, _, _ = struct.unpack_from("<8I", data, i*RECSZ)
        recs.append((kind, off, freq, pno, pre))
    return recs

def load_map(path):
    # sidecar: pairs of (cumulative_record_total, sample_pos) per render chunk.
    try:
        with open(path, "rb") as f:
            data = f.read()
    except IOError:
        return []
    out = []
    for i in range(len(data) // 8):
        total, smpl = struct.unpack_from("<2I", data, i*8)
        out.append((total, smpl))
    return out

def time_of(ordinal, fmap):
    # first chunk whose cumulative total exceeds this ordinal owns the event.
    for total, smpl in fmap:
        if ordinal < total:
            return smpl / SR
    return fmap[-1][1] / SR if fmap else -1.0

KIND = {0: "osc", 1: "lfo"}

def main():
    if len(sys.argv) < 3:
        print("usage: freqdiff.py <asm-prefix> <cpp-prefix>"); return 2
    a = load_log(sys.argv[1] + ".freqlog")
    b = load_log(sys.argv[2] + ".freqlog")
    fmap = load_map(sys.argv[1] + ".freqmap")

    print("asm records : %d" % len(a))
    print("cpp records : %d" % len(b))

    n = min(len(a), len(b))
    # Structural desync = the call SEQUENCES differ. Aligned by call ordinal; the
    # consistency key is `kind` (osc/lfo) only. Raw byte offsets are NOT comparable
    # across cores (asm syWOsc and C++ V2Osc differ in size, so offset-from-base has
    # a different per-osc stride per core), so offset is reported only as a per-core
    # attribution hint, never used for the structural check.
    struct_at = -1
    for i in range(n):
        if a[i][0] != b[i][0]:
            struct_at = i; break
    if len(a) != len(b):
        print("STRUCTURAL: record count differs (%d vs %d)" % (len(a), len(b)))
    if struct_at >= 0:
        ai, bi = a[struct_at], b[struct_at]
        print("STRUCTURAL: kind desync at ordinal %d (t=%.3fs): asm kind=%s vs cpp kind=%s" % (
              struct_at, time_of(struct_at, fmap),
              KIND.get(ai[0], ai[0]), KIND.get(bi[0], bi[0])))
        print("VERDICT: STRUCTURAL DIVERGENCE (control flow differs) -- stop here")
        return 1

    # value divergence: same coordinate, different freq
    events = []
    for i in range(n):
        ak, aoff, afreq, apno, apre = a[i]
        bk, boff, bfreq, bpno, bpre = b[i]
        if afreq != bfreq:
            delta = (bfreq - afreq)
            # signed interpretation of the 32-bit freq delta
            if delta >= (1 << 31): delta -= (1 << 32)
            if delta < -(1 << 31): delta += (1 << 32)
            # classification: pure fistp tie-flip (same pno) vs upstream pno diff.
            # pno/preround are 0 until the float-capture pass; mark "n/a" then.
            if apno == 0 and bpno == 0:
                cls = "?"
            elif apno == bpno:
                cls = "tie"     # same input, fistp rounded differently
            else:
                cls = "upstream"  # the float input itself differs
            events.append((i, time_of(i, fmap), KIND.get(ak, ak), aoff,
                           afreq, bfreq, delta, cls))

    print("freq-value divergences : %d / %d records compared" % (len(events), n))
    if not events:
        print("VERDICT: MATCH (every freq computation bit-identical)")
        return 0

    # distribution of |delta| and class
    from collections import Counter
    dd = Counter(abs(e[6]) for e in events)
    cc = Counter(e[7] for e in events)
    print("  |delta| histogram :", dict(sorted(dd.items())))
    print("  class histogram   :", dict(cc))
    print("  first 40 events (ordinal  time  kind  offset  asmFreq  cppFreq  d  class):")
    for e in events[:40]:
        print("    %8d  %8.3fs  %3s  off=%-8d  %10d  %10d  %+d  %s" % (
            e[0], e[1], e[2], e[3], e[4], e[5], e[6], e[7]))
    if len(events) > 40:
        print("    ... %d more" % (len(events) - 40))
    print("VERDICT: %d freq divergences (see histograms above)" % len(events))
    return 1

if __name__ == "__main__":
    sys.exit(main())
