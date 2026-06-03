## Context

`localize-v2-core-divergence` found VCF moog modes 6/7 diverge 0.10 (C++ moog-lo
near-silent). This change roots-causes and fixes it, using the `comp_flt`
component oracle (asm vs C++, identical params/state/input) as the verifier.

## Goals / Non-Goals

**Goals:** make `V2Moog::step` match the asm `.processmoog` ladder; verify via
`comp_flt` (0/7 filter modes diverge); no regression in standard filter modes.

**Non-Goals:** no change to `V2Flt::set` coefficients (already correct); no other
block (tri/saw osc, distortion, etc. are separate follow-ons); no whole-song
guarantee (baseline songs don't use moog).

## Decisions

**D1 — Diagnose set() vs render() before editing.** Dumped the moog coefficients
(`moogf/moogp/moogq`) from both cores after `set()`: identical
(`0.9719726 / 0.0140137 / 3.738762`). So `set()` is correct and the bug is purely
in the render ladder. This avoided "fixing" the coefficient math (which is right).

**D2 — Fix #1: ladder feedback sign.** asm stage: `b1' = p*(in'+b0) + moogf*b1`
(an `faddp`). C++ had `b[1] = (in+b[0])*p - b[1]*f`. With `moogf = 1-2p ≈ +0.972`,
subtraction places the pole near −1 (a near-Nyquist rejecter) instead of near +1
(leaky-integrator lowpass). Fix: the four stages use `+ b[k]*f`. This alone moves
the divergence from 0.10 to 1e-4.

**D3 — Fix #2: single DC-offset un-bias for the fed-back state.** asm stores the
once-unbiased `clip - dco` as the feedback state; C++ stored `b4 - fcdcoffset`
*after already* subtracting `fcdcoffset` (i.e. `clip - 2·dco`). Fix: `b[4] = b4`.
With Fix #1 present, this moves 1e-4 → 6e-8 (full match). Verified by reverting it
in isolation (divergence returns to 1e-4), so both fixes are independently real.

**D4 — Keep the asm storage convention.** `moogf` stays `1 - 2p` (matches the asm
stored value and the dumped coefficient); only the *usage* sign in the ladder
changes. This keeps the coefficient comparison clean and the diff minimal.

## Risks / Trade-offs

- **[Other blocks still diverge]** → out of scope; the baseline songs don't use
  moog so this fix doesn't move their whole-song A/B. Verified at component level,
  which is the correct granularity for a per-block fix.
- **[Regression in standard filter modes]** → guarded: `comp_flt` confirms
  low/band/high/notch/all still match after the change.
