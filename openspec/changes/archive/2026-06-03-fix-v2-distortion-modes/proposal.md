## Why

The `localize-v2-core-divergence` change found that distortion modes 1
(OVERDRIVE) and 3 (BITCRUSHER) diverge from the assembly reference (0.13 and 2.5
max error), while OFF/CLIP/DECIMATOR/filter modes match. These are real port bugs
in `V2Dist`. This change corrects them, verified bit-comparable to the asm via the
`comp_leaves`/`comp_fastatan` component oracles.

## What Changes

- **BITCRUSHER (`V2Dist::bitcrusher`)**: the input quantization used a C cast
  `(sInt)(in*crush1)` which *truncates toward zero*; the asm uses `fistp`
  (*round to nearest*). Changed to `(sInt)lrintf(in*crush1)`. → mode 3 matches
  exactly (max err 0).
- **OVERDRIVE (`fastatan`, used only by `V2Dist::overdrive`)**: the C++ `fastatan`
  was an unfaithful reimplementation of the asm rational approximation. Two bugs:
  1. **Coefficient structure.** The asm shares the x² denominator coefficient
     `cxm2 = 0.76443945` across both approximation tables (only `cxm0`/`cxm4` are
     per-table). The C++ table made `cxm2` per-table and swapped it with `cxm4`.
     Fixed by restoring the shared `cxm2` (swap `c[3]`↔`c[4]` in both rows).
  2. **Table-select bug replication.** The asm selects the |x|≥1 table only when
     the biased exponent byte == 0x7f (a `cmovge`-vs-`cmovae` bug), i.e. only for
     x ∈ [1,2); for x ≥ 2 it falls back to the |x|<1 table. To match the shipping
     asm we replicate this with `coeffs[x >= 1.0f && x < 2.0f]`.
  After both, `fastatan` matches the asm to 0.0 across a −8..8 sweep, and
  overdrive matches to 6e-8.

This change does NOT touch other distortion modes (they already matched) or any
other block. The `fastatan` change is safe because `fastatan` is used *only* by
distortion overdrive.

## Capabilities

### New Capabilities
<!-- None. -->

### Modified Capabilities
- `v2-port-fidelity`: add per-block correctness requirements for distortion
  overdrive and bitcrusher (the capability already holds the Moog filter
  requirement from `fix-v2-moog-filter`).

## Impact

- **Code changed:** `v2/synth_core.cpp` — `V2Dist::bitcrusher` (1 line) and the
  static `fastatan` (coefficient table + table-select). No interface change.
- **Verified by:** `comp_leaves` (dist modes 0–3 all MATCH), `comp_fastatan`
  (new sweep oracle: worst |cpp−asm| = 0.0). No regression: osc/filter suites
  unchanged.
- **Whole-song:** like the Moog fix, the two baseline songs' whole-song A/B is
  governed by whichever blocks they actually use; this fix is verified at the
  component level (the correct granularity for per-block correctness).
- **Harness additions:** `fastatan` exposed as a global in the generated asm copy
  + a `v2x_fastatan` trampoline + `comp_fastatan.cpp` sweep test (kept as a
  permanent diagnostic).
