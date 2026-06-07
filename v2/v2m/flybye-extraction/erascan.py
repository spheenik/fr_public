#!/usr/bin/env python3
# erascan.py -- read era-delta behavior straight out of an unpacked V2 synth
# image by scanning for the constants/opcodes that distinguish the 2000-core
# from the 2004-core. This is the candytron-assay method (../candytron-
# extraction/NOTES.md) applied to the v1/v5 binaries.
#
#   ./erascan.py flybye_unpacked.bin
#
# Interpreting a hit: presence of a constant/opcode IN THE SYNTH REGION means
# that code path exists in that binary. Absence of the modern variant anywhere
# (these 64ks have only the player, no editor) means the old path is the only
# one. Cross-check the address against the synth region (the rdtsc cluster +
# fpatan locate it).

import struct, re, sys

d = open(sys.argv[1], 'rb').read()
BASE = 0x400000
print(f"image {sys.argv[1]}: {len(d)} bytes, nonzero {100*sum(1 for b in d if b)/len(d):.2f}%")

CONSTS = {
    # --- DELTA_NOISE_LCG_MSVC: 2000 = MSVC rand() LCG; 2004 = different LCG ---
    "MSVC LCG mul 214013 (old)":       struct.pack('<I', 214013),
    "MSVC LCG add 2531011 (old)":      struct.pack('<I', 2531011),
    "modern LCG 196314165 (new)":      struct.pack('<I', 196314165),
    "modern LCG 907633515 (new)":      struct.pack('<I', 907633515),
    # --- DELTA_OSC_FREQ_CONST: 2000 = baked const; 2004 = runtime fcoscbase ---
    "baked oscfreq 3185015.0 (old)":   struct.pack('<f', 3185015.0),
    # --- DELTA_RDTSC_SEED: 2000/v5 = rdtsc; v6 = fixed seed table ------------
    "fixed oscseed 0xdeadbeef (v6)":   struct.pack('<I', 0xdeadbeef),
    "fixed oscseed 0xbaadf00d (v6)":   struct.pack('<I', 0xbaadf00d),
    "fixed oscseed 0xd3adc0de (v6)":   struct.pack('<I', 0xd3adc0de),
    # --- DELTA_NO_DCOFFSET: 2004 adds the 2^-18 denormal bias ---------------
    "fcdcoffset 2^-18 (v6 only)":      struct.pack('<f', 1.0 / 262144.0),
}
OPCODES = {
    "rdtsc (0f31)":   b'\x0f\x31',   # osc-noise / S&H / dist seeds
    "f2xm1 (d9f0)":   b'\xd9\xf0',   # calcfreq pow2
    "fscale (d9fd)":  b'\xd9\xfd',
    "fpatan (d9f3)":  b'\xd9\xf3',   # overdrive native atan (old until v6)
    "fsin (d9fe)":    b'\xd9\xfe',   # osc/LFO native sine (old until v6)
}

print("\nconstants:")
for name, pat in CONSTS.items():
    locs = [hex(BASE + m.start()) for m in re.finditer(re.escape(pat), d)]
    print(f"  {name:34} {'FOUND ' + str(locs[:4]) if locs else 'absent'}")
print("\nopcodes:")
for name, pat in OPCODES.items():
    locs = [hex(BASE + m.start()) for m in re.finditer(re.escape(pat), d)]
    print(f"  {name:18} count={len(locs):3} {locs[:6]}")
