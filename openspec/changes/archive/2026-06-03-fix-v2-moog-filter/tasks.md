## 1. Diagnose

- [x] 1.1 Confirm the bug is in render, not coefficients: dump `moogf/moogp/moogq`
  from both cores after `set()` — identical, so `V2Flt::set` is correct.
- [x] 1.2 Compare `V2Moog::step` against the asm `.processmoog` ladder line-by-line
  to locate the discrepancies.

## 2. Fix

- [x] 2.1 Fix feedback sign: change the four ladder stages from `- b[k]*f` to
  `+ b[k]*f` (match the asm `faddp`). **Moves divergence 0.10 → 1e-4.**
- [x] 2.2 Fix double DC-offset: change `b[4] = b4 - fcdcoffset` to `b[4] = b4`
  (asm stores the once-unbiased value). **Moves 1e-4 → 6e-8.**

## 3. Verify

- [x] 3.1 `comp_flt`: moog-lo and moog-hi MATCH (≤6e-8); 0/7 filter modes diverge.
- [x] 3.2 No regression: standard filter modes still match; `comp_osc`/`comp_leaves`
  unchanged; both fixes confirmed independently real (revert-in-isolation).
- [x] 3.3 Whole-song A/B unchanged for baseline songs (they don't use moog) —
  documents that remaining song-level divergence is from other blocks.
