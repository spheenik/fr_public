#!/usr/bin/env python3
# Convert a raw float32 render dump from the V2 validation harness into a
# playable WAV. The harness/compare flow speaks raw interleaved-stereo float32
# (L,R,L,R,...) at 44.1 kHz; this turns one of those dumps into something you
# can actually listen to without rebuilding anything.
#
#   usage: f32towav.py <in.f32> [out.wav] [--rate HZ]
#
# Output is 16-bit PCM (clamped to [-1,1]) — the most universally playable
# format (aplay, paplay, ffplay, browsers). The .f32 stays the source of truth
# for bit-exact validation; this is purely for ears.
import sys, os, wave, array, struct

def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opts = [a for a in argv[1:] if a.startswith("--")]
    if not args:
        print("usage: f32towav.py <in.f32> [out.wav] [--rate HZ]")
        return 2

    inpath = args[0]
    outpath = args[1] if len(args) > 1 else os.path.splitext(inpath)[0] + ".wav"
    rate = 44100
    for i, o in enumerate(opts):
        if o == "--rate" and i + 1 < len(opts):
            rate = int(opts[i + 1])
        elif o.startswith("--rate="):
            rate = int(o.split("=", 1)[1])

    f = array.array("f")
    with open(inpath, "rb") as fp:
        f.frombytes(fp.read())
    if sys.byteorder == "big":
        f.byteswap()  # the dump is little-endian on disk

    if len(f) % 2:
        print("warning: odd float count %d — truncating to whole stereo frames" % len(f),
              file=sys.stderr)
        f = f[: len(f) - 1]

    peak = max((abs(x) for x in f), default=0.0)
    clipped = 0
    pcm = array.array("h")
    for x in f:
        v = int(x * 32767.0 + (0.5 if x >= 0 else -0.5))
        if v > 32767:
            v = 32767; clipped += 1
        elif v < -32768:
            v = -32768; clipped += 1
        pcm.append(v)
    if sys.byteorder == "big":
        pcm.byteswap()  # wave wants little-endian

    with wave.open(outpath, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())

    frames = len(f) // 2
    print("wrote %s — %d stereo frames, %.2fs @ %d Hz, peak %.4f%s"
          % (outpath, frames, frames / float(rate), rate, peak,
             ", %d samples clipped" % clipped if clipped else ""))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
