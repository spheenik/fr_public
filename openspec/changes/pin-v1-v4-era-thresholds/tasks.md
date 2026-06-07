# Tasks — pin-v1-v4-era-thresholds

## 1. Assay + data edits (landed in 8f47cd4)

- [x] 1.1 Unpack flybye + fr-022 statically (unicorn emulation of the aPLib
      stub); commit `unpack.py`, `erascan.py`, the two embedded v2ms and
      provenance in `v2/v2m/flybye-extraction/NOTES.md`
- [x] 1.2 Assay the constant/opcode rows (osc trio, rdtsc, fsin/fpatan,
      dcoffset) in both binaries; move the trio `flipsAt 1 → 5`
- [x] 1.3 Disassemble the four signature-less player/render rows
      (PGMCHANGE/TICK/SUBFRAME/RVB_E); move `flipsAt 1 → 5`
- [x] 1.4 Document the build-date model limitation + fr-022 measurements
      (89% rel-RMS unrepresentable case, v3-twin resolution) in NOTES.md
- [x] 1.5 Corroborate anchored rows from `v2mconv.cpp` transEnv +
      `sounddef.h` param tables

## 2. v1 render oracle (landed in e9ce38e)

- [x] 2.1 Build `c1_flybye_harness.c` (image map, rdtsc pins, Ronan gates,
      2001 OpenV2M/PlayV2M/RenderProxy) rendering the whole embedded song
- [x] 2.2 First whole-song diff vs portable native-v1: 0–66.42 s bit-exact,
      abrupt divergence at 66.42 s recorded in NOTES.md

## 3. The 66.42 s hunt → voice-pool era row

- [x] 3.1 Localize the divergence (alloc-trace probe on the flybye image vs
      the portable `V2_STEAL` tap): first diverging allocation = alloc 785
      at sample 2929102; 2001 engine steals (16-voice pool), portable opens
      voice 16
- [x] 3.2 Pin the pool size across all period binaries: 16 (fr08 2000,
      flybye 2001) / 32 (fr-022 2002-08, candytron 2003-08) / 64 (2004 asm)
- [x] 3.3 Verify hypothesis: POLY=16 build renders whole flybye song
      bit-exact (rel-RMS 4.0e-5, max|d| 1.5e-4); josie (v5) bit-identical
      at 32 vs 64
- [x] 3.4 Add `DELTA_POLY_16` (flipsAt 5, ASSUMED proxy) and
      `DELTA_POLY_32` (flipsAt 6, PROVEN) rows to `v2eras.h` with evidence
      comments + monotonicity/endpoint static_asserts
- [x] 3.5 Bound the four allocator scans in `v2core.cpp` processMIDI
      note-on (npoly count, find-free, gate-off steal, oldest steal) by the
      era pool size via a constant-foldable `poolSize()` helper
- [x] 3.6 Re-render flybye embedded v1 with the gated build and verify the
      whole-song oracle diff (no sample >1e-3; rel-RMS ~4e-5)
- [x] 3.7 Run `test/check.py` — 17/17 with baselines unchanged; A/B the
      in-repo v5 originals stock vs gated: josie bit-identical (never
      saturates 32); kkrieger6 native-v5 changes from 102.47 s where it
      saturates 32 voices — evidence-backed (both v5 binaries are 32),
      oracle-less, documented in NOTES + loose-ends
- [x] 3.8 Commit the hunt probe `c1_flybye_solo.c` (+ `evlist.py`) into
      `v2/v2m/flybye-extraction/`

## 4. Documentation

- [x] 4.1 Update `flybye-extraction/NOTES.md`: replace the "open follow-up"
      paragraph with the solved hunt (method, the 16/32/64 timeline with
      addresses, verification numbers)
- [x] 4.2 Update `v2/portable/ACCURACY-LOOSE-ENDS.md` §3–4 to the new
      status (v1 oracle-proven; pool row; remaining v2/v3 gap)
