# Fix V2 boost a0 association + dist gain2 fpatan

## Why

The whole-song A/B residual (0.0219 on `pzero_new.v2m`) is now fully attributed to
two set-path 1-ULP porting errors, both born at the same ch7 patch-change event at
9.677 s:

1. **Boost `a0` summation order** (`V2Boost::set`): the port computes
   `(Ap1 + cAm1) + bs`; the asm (`syBoostSet`) computes `(bs + cAm1) + Ap1`.
   FP addition is not associative — for boost amount 92 (which a third boost
   channel switches to at 9.677 s) the orderings round differently, `ia0` shifts
   1 ULP, and **all five biquad coefficients** come out 1 ULP off. The coefficient
   step shocks the IIR state by 1.13e-5 and rings down for seconds; downstream
   dist + sum-compressor amplify it into the dominant residual. Evidence is
   bit-perfect: a 24-bit simulation of both orderings reproduces the exact
   observed values (`b0` cpp `3f80fa30` vs asm `3f80fa31`) and the exact
   agreement at amounts 54/66.
2. **Dist overdrive `gain2`** (`V2Dist::set`, mode OVERDRIVE): the port computes
   `(param1/128)/atan(gain1)` with libm double `atan`; the asm uses x87 `fpatan`
   (synth.asm:1835). `vce_dist` first diverges at exactly the same 9.677 s patch
   event (2.98e-8 — a 1-ULP `gain2`), and `ch_dist` jumps to 6.1e-3 through the
   overdrive nonlinearity. Both dist stages (voice + channel) share this site.

A third, latent bug found in the same audit: the port computes boost
`beta = sqrtf(2*A)` (porter's algebraic simplification) where the asm computes
`sqrt((A²+1) − (A−1)²)` stepwise. For pzero's amounts (54/66/92) the results
coincide, but a 24-bit sweep shows **21 of 127** possible amounts produce a
1-ULP-different beta. Fix it now so the next song doesn't re-open this hunt.

## What Changes

- `V2Boost::set`: reassociate the `a0` sum to the asm's order
  `(bs + cAm1) + Ap1` (portable-safe pure-C reassociation; porting error → fixed
  directly, no flag).
- `V2Boost::set`: restore the asm's stepwise beta formula
  `sqrt((A²+1) − (A−1)²)` (portable-safe; porting error → fixed directly).
- `V2Dist::set` (OVERDRIVE): compute `gain2`'s `atan(gain1)` with the x87
  `fpatan` under `V2_X87_FAITHFUL` (libm `atan` stays in the portable build,
  matching the existing transcendental-gating pattern).
- Extend the boost leaf oracle to sweep **all 127 boost amounts** bit-exact
  (eps=0), so data-dependent set-path rounding can never hide behind a single
  lucky test value again.
- Re-measure the whole-song A/B; update `v2/validate/HANDOVER.md` (which also
  still claims the session-3 fixes are uncommitted — they are committed as
  297b66f).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `v2-port-fidelity`: add requirements that (a) the boost shelving-EQ coefficient
  computation matches the asm's operand association and beta formula bit-exactly
  for every amount, and (b) the faithful build's distortion overdrive `gain2`
  matches the asm `fpatan` setup bit-exactly.

## Impact

- `v2/synth_core.cpp`: `V2Boost::set` (two-line reassociation + beta formula),
  `V2Dist::set` (gated `fpatan` helper, likely a new `v2_atan` inline next to
  `v2_pow`/`v2_pow2`).
- `v2/validate/` leaf oracle (boost amount sweep; `comp_leaves` or a new case).
- `v2/validate/HANDOVER.md` refresh.
- Expected result: `ch_boost` tap → 0; both dist stages drop or zero out; the
  whole-song residual falls well below 0.0219 (possibly to bit-exact past the
  17.8 s mark). No portable-build DSP change except the two portable-safe
  reassociations (which move the portable build *closer* to the asm).
