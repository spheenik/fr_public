## Why

The `localize-v2-core-divergence` work (archived) and the subsequent bus-tap
drill-down (see `v2/validate/HANDOVER.md`) traced the residual whole-song A/B
divergence (~0.0876 on `pzero_new.v2m`) to the **oscillator frequency
computation** in `V2Osc::chgPitch` (`synth_core.cpp`). Every isolatable DSP block
already bit-matches the asm; the freq path is the last dominant source.

A fresh-session investigation corrected the prior precision diagnosis, which was
built on a false premise:

- The handover claimed "the C++ core uses SSE (24-bit single)". **It does not.**
  `g++ -m32` on this toolchain defaults to `-mfpmath=387` (x87) and runs at the
  glibc default control word `0x037F` = **64-bit** significand. The asm forces the
  x87 to **24-bit single** (`synth.asm:4868  and ax,0F0FFh` → PC=00) for the whole
  render. So the cores share the x87 unit and the 15-bit exponent range, but the
  C++ core keeps 40 extra mantissa bits the asm throws away.
- An experiment (rebuilding the cpp harness with `-mpc32 -mno-sse`, forcing
  x87/PC=24 with no SSE detours) moved the divergence *onset* (#8764 → #8810) but
  **not the magnitude** (0.08763 unchanged). This proves the pervasive 64→24
  mantissa gap is a *minor* contributor — the synth stores nearly every value to
  an `sF32` member between ops, so 64-bit intermediates rarely survive. The
  *dominant* source is the freq path, which the flags cannot reach:
  1. libm `pow`/`powf` use a different polynomial than the asm's x87 `f2xm1`;
  2. the `(sInt)` cast **truncates** where the asm `fistp` **rounds to nearest**;
  3. C++ `/12.0f` **divides** where the asm does `fmul [fci12]` with
     `fci12 = 0.083333333333f` (a rounded-reciprocal *multiply*).

Fidelity to the asm is fundamentally an x86/x87/PC=24 proposition (inline `f2xm1`,
control-word semantics). A single C++ binary cannot be both bit-faithful and
portable. The plan is to make a **bit-faithful validation build** that proves the
port logic is correct end-to-end (whole-song A/B → 0); once correctness is locked,
the portable build's small, sub-audible precision differences are acceptable.

## What Changes

- **Faithful x87 transcendentals (gated).** Add inline-asm x87 helpers replicating
  the asm `pow2`/`calcfreq`/`calcfreq2` kernels (`fld1/fld/fprem/f2xm1/faddp/
  fscale/fstp`, `synth.asm:355-408`) exactly, and a round-to-nearest `fistp` store.
  Used only when a new `V2_X87_FAITHFUL` define is set (the validation build);
  the portable build keeps libm and the truncating cast unchanged.
- **Oscillator freq path matches the asm** (`V2Osc::chgPitch`), under the faithful
  define: route `nffrq` through `v2_calcfreq`, `freq` through `v2_pow2` + `fistp`
  round, and mirror the `* fci12` reciprocal-multiply (vs `/12.0f`).
- **Revert the tri/saw `double` box-filter fix to `float`** under the faithful
  define: at x87 PC=24, `float` has the 24-bit mantissa AND 15-bit exponent the
  asm has; `double` (53-bit) overshoots. The prior fix was premised on the asm
  being 80-bit, which it never was.
- **Validation build runs x87 at 24-bit single.** Add `-mpc32 -mno-sse` (and
  `-DV2_X87_FAITHFUL`) to the `v2/validate` build (`build.sh`) for the cpp harness
  and the component oracles, mirroring the asm's PC=24 / no-SSE-detour semantics.

This change does NOT alter the portable default build's DSP (no `V2_X87_FAITHFUL`
= current libm/`(sInt)`/`double` behavior). `synth.asm` is not edited.

## Capabilities

### Modified Capabilities
- `v2-port-fidelity`: adds an oscillator-frequency correctness requirement and a
  faithful-build precision requirement to the existing capability.

## Impact

- **Code changed:** `v2/synth_core.cpp` (`V2Osc::chgPitch`, new gated x87 helpers,
  gated tri/saw revert); `v2/validate/build.sh` (faithful build flags + define).
- **Verified by:** `v2/validate/comp_osc` (oscillator oracle) and the whole-song
  A/B (`harness_asm` vs `harness_cpp`) on `pzero_new.v2m` /
  `v2_zeitmaschine_new.v2m`, plus the bus-tap rig for localization.
- **Goal:** whole-song A/B magnitude drops from 0.0876 toward 0 (bit-faithful
  build); no regression in the existing component oracles.
