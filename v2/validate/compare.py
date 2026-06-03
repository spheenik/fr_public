#!/usr/bin/env python3
# Compare two raw float32 render dumps from the V2 validation harness.
# Reports sample count, signal level (to rule out silence), max abs error,
# and the first sample index exceeding tolerance.
#
#   usage: compare.py <a.f32> <b.f32> [eps]
import sys, struct, math

def load(path):
    with open(path, "rb") as f:
        data = f.read()
    n = len(data) // 4
    return struct.unpack("<%df" % n, data[:n*4])

def main():
    if len(sys.argv) < 3:
        print("usage: compare.py <a.f32> <b.f32> [eps]"); return 2
    a = load(sys.argv[1]); b = load(sys.argv[2])
    eps = float(sys.argv[3]) if len(sys.argv) > 3 else 1e-6

    if len(a) != len(b):
        print("LENGTH MISMATCH: %d vs %d floats" % (len(a), len(b))); return 1

    peak_a = max((abs(x) for x in a), default=0.0)
    peak_b = max((abs(x) for x in b), default=0.0)
    max_err = 0.0
    first_div = -1
    over = 0
    sq = 0.0
    for i in range(len(a)):
        d = abs(a[i] - b[i])
        if d > max_err: max_err = d
        sq += d*d
        if d > eps:
            over += 1
            if first_div < 0: first_div = i
    rms_err = math.sqrt(sq/len(a)) if a else 0.0

    print("floats compared : %d (%d stereo samples)" % (len(a), len(a)//2))
    print("peak |A| / |B|  : %.6f / %.6f %s" % (
        peak_a, peak_b, "  <-- WARNING: near silence" if max(peak_a,peak_b) < 1e-4 else ""))
    print("tolerance (eps) : %g" % eps)
    print("max abs error   : %.9g" % max_err)
    print("rms abs error   : %.9g" % rms_err)
    print("samples > eps   : %d / %d" % (over, len(a)))
    if first_div >= 0:
        print("first divergence: float #%d (stereo sample %d, %s ch)" % (
            first_div, first_div//2, "L" if first_div%2==0 else "R"))
        print("  A=%.9g  B=%.9g" % (a[first_div], b[first_div]))
        print("VERDICT         : DIVERGE")
        return 1
    else:
        print("VERDICT         : MATCH (within eps)")
        return 0

if __name__ == "__main__":
    sys.exit(main())
