## Why

The `localize-v2-core-divergence` change (archived) pinpointed the Moog filter as
the primary 0.1-scale divergence between the C++ port (`synth_core.cpp`) and the
assembly reference (`synth.asm`): VCF modes 6/7 (moog-lo/moog-hi) diverged by
0.10 while every other filter mode matched to float-noise. The C++ moog *lowpass*
was effectively dead (near-silent output). This is the first change to actually
*correct* the port — making `V2Moog::step` bit-comparable to the asm ladder.

## What Changes

- Fix two independent bugs in `V2Moog::step` (`synth_core.cpp`), both confirmed
  against the asm `.processmoog` ladder via the component oracle (`comp_flt`):
  1. **Feedback sign flip (primary).** The four ladder stages subtracted the
     feedback coefficient (`(in+b[k-1])*p - b[k]*f`) where the asm *adds* it
     (`+ moogf*b[k]`). With `moogf ≈ +0.972`, the subtraction put each pole near
     −1 (rejecting low frequencies) instead of near +1 (a leaky integrator /
     lowpass) — hence the near-silent moog-lo. Changed the four `- b[k]*f` to
     `+ b[k]*f`.
  2. **Double DC-offset subtraction (secondary).** The fed-back state stored
     `b[4] = b4 - fcdcoffset` (a second un-bias), while the asm stores the
     once-unbiased value. Changed to `b[4] = b4`.
- After both fixes, all 7 VCF modes match the asm to ≤6e-8 (`0/7 diverge`).

This change does NOT touch the filter coefficient math (`V2Flt::set` is already
correct — coefficients matched the asm exactly) or any other DSP block. It is
scoped to the Moog ladder render only.

## Capabilities

### New Capabilities
- `v2-port-fidelity`: Per-block correctness requirements asserting that the C++
  V2 synth core (`synth_core.cpp`) produces output matching the assembly
  reference (`synth.asm`), verified by the `v2-component-equivalence` harness.
  This change establishes the capability with the Moog filter requirement;
  follow-on fixes (tri/saw oscillator, distortion modes 1 & 3) add to it.

### Modified Capabilities
<!-- None. -->

## Impact

- **Code changed:** `v2/synth_core.cpp`, `V2Moog::step` only (4 sign flips + 1
  state-writeback line). No interface/ABI change.
- **Verified by:** `v2/validate/comp_flt` (component oracle) — moog-lo/moog-hi now
  MATCH (≤6e-8); the 5 standard filter modes remain MATCH (no regression);
  `comp_osc`/`comp_leaves` unchanged.
- **Whole-song scope:** the two baseline songs (`pzero_new`, `v2_zeitmaschine`)
  do not exercise the moog filter, so their whole-song A/B is unchanged — their
  residual divergence is from other blocks (distortion modes 1/3; the
  instance-entangled compressor/reverb/mod-delay). Those are separate follow-on
  fixes; this change is verified at the component level, which is the correct
  scope for a per-block correctness fix.
