# V2 synth core validation — Phase 1 report

**Question:** does the C++ port `synth_core.cpp` produce output matching the
original assembly `synth.asm`?

**Verdict: NO — the two cores DIVERGE.** The 2012 commit note *"C++ ver still
doesn't match ASM"* (49549db) is still accurate in 2026. The port was abandoned
in a non-matching state and has not been corrected since.

## Method

One harness source linked against each core (only the core differs):

```
.v2m ──▶ V2MPlayer (shared, ported) ──▶ synthRender ──▶ raw stereo f32 dump
                                          ▲
                         synth.asm (oracle) │ synth_core.cpp (under test)
```

Both built 32-bit (ILP32) so `sU32` (`unsigned long`) is 4 bytes on each side and
all struct/patch layouts match. RONAN (speech) disabled identically on both sides;
baseline songs do not use speech. See `BUILD.md`.

## Results

| Song | samples | peak \|asm\| | peak \|cpp\| | max abs err | rms err | first divergence |
|------|--------:|------:|------:|------:|------:|------|
| `pzero_new.v2m`           | 441000 | 0.416 | 0.396 | **0.109** | 0.0178 | stereo sample 4273 |
| `v2_zeitmaschine_new.v2m` | 441000 | 0.323 | 0.273 | **0.137** | 0.0196 | stereo sample 218  |

- Both cores are **fully deterministic** (re-render is bit-identical).
- Both produce **real signal** (not silence), so this is a true mismatch, not a
  dead/empty render on one side.
- ~99% of samples differ. Max error is ~25–40% of peak amplitude.

### Tolerance

A float eval-order difference would sit near `1e-6` relative and stay there.
Using a generous float-noise threshold `eps = 1e-4`, the measured error (~0.1) is
**~1000× larger** — the verdict does not depend on the exact epsilon.

### It is NOT a simple gain bug

The C++ output is consistently *quieter*. Best-fit constant gain `k = B/A`:
`pzero k≈0.921`, `zeit k≈0.884`. But removing that gain barely reduces the
residual (`rms(B−A)=0.0178` → `rms(B−kA)=0.0171`), and `k` differs per song.
⇒ the divergence is **structural / content-dependent in the signal path**, not a
single output scalar. Phase 2 must localize it inside the DSP chain.

## Reproduce

Prerequisites: `nasm`, `gcc`/`g++` with `-m32` + 32-bit `libstdc++`
(Arch: `lib32-gcc-libs`; Debian/Ubuntu: `g++-multilib`). Then:

```sh
cd v2/validate
./build.sh                                   # -> harness_asm, harness_cpp
N=441000
./harness_asm ../v2m/pzero_new.v2m asm.f32 $N
./harness_cpp ../v2m/pzero_new.v2m cpp.f32 $N
python3 compare.py asm.f32 cpp.f32
```

## Consequence for Phase 2

The fork is resolved to the **debug** branch: ryg's C++ port is close but
incorrect, so a Linux/maintainable port cannot simply adopt `synth_core.cpp`
as-is — it must first be made bit-(or audibly-)correct against the asm.

Concrete next steps (Phase 2):

1. **Block-level bisection.** `scope.h`/`scope.cpp` (the intended A/B tool) is
   Windows+OpenGL only and instruments only the C++ side, so it does **not**
   give a headless asm-vs-cpp per-block diff. Phase 2 needs a headless probe that
   dumps matching intermediate signals from *both* cores (e.g. render a single
   note through one voice and compare after osc → filter → env → dist → channel
   FX → global FX) to find the first stage that splits.
2. Likely suspects given "close, quieter, structural": a filter coefficient /
   gain stage, the boost/EQ, the compressor, or a denormal/rounding path —
   `synth_core.cpp` also exposes `BUG_V2_*` compatibility toggles worth checking
   against the asm's actual behavior.
3. Only after the core matches: the real port (drop asm, fix `types.h` LP64,
   64-bit native, audio backend) per the original Phase-2 plan.
