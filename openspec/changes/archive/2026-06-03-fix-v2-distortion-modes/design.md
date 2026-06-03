## Context

`localize-v2-core-divergence` flagged distortion modes 1 (overdrive) and 3
(bitcrusher) as divergent. This change roots-causes and fixes both, verified
against the asm with the component oracle (`comp_leaves`) plus a new dedicated
`fastatan` sweep oracle (`comp_fastatan`).

## Goals / Non-Goals

**Goals:** make `V2Dist` overdrive and bitcrusher match the asm; add a `fastatan`
sweep oracle; no regression in the other distortion modes or other blocks.

**Non-Goals:** no change to OFF/CLIP/DECIMATOR/filter dist modes (already match);
no other block; no whole-song guarantee (component-level is the right scope).

## Decisions

**D1 — Bitcrusher: round, don't truncate.** The asm quantizes with `fistp`
(round-to-nearest-even, default FPU mode). C++ used `(sInt)` (truncate toward
zero). Fix: `(sInt)lrintf(...)` — `lrintf` honors the current rounding mode and
matches `fistp`. → mode 3 matches exactly.

**D2 — Diagnose overdrive empirically.** `set()` uses precise atan (libm `atan`
vs asm `fpatan`) → not the cause. CLIP matches and differs from OVERDRIVE only by
`fastatan`, so the bug is in `fastatan`. Exposed the asm `fastatan` as a global +
trampoline (`v2x_fastatan`, ABI: st0=val, st1=−1, ax=hi16(val), ebx=8) and swept
−8..8 against the C++ version to pinpoint the differences.

**D3 — Overdrive fix #1: restore the shared cxm2.** The asm `fcatanxm2` is a
single `dq` (0.76443945) used for *both* tables; `fcatanxm0`/`fcatanxm4` are
per-table `dq[2]`. The C++ coefficient table made `cxm2` per-table and swapped it
with `cxm4`. Fix: swap `c[3]`↔`c[4]` in both rows so the denominator becomes
`cxm0 + 0.76443945·x² + cxm4·x⁴` matching the asm.

**D4 — Overdrive fix #2: replicate the table-select bug.** The asm selects the
|x|≥1 table via `cmp ah,7fh; cmovge` on the exponent byte — which fires only when
the biased exponent == 0x7f, i.e. x ∈ [1,2). For x ≥ 2 it uses the |x|<1 table
(the documented `cmovge`-vs-`cmovae` bug). Fix: `coeffs[x >= 1.0f && x < 2.0f]`.
Both fixes together make `fastatan` match the asm to **0.0** across the sweep
(float precision suffices; no double needed).

**D5 — Order of discovery mattered.** Fix #2 alone (attempted first) made overdrive
*worse*, because the coefficients (#1) were still wrong. Both are required and were
confirmed jointly via the sweep oracle reaching 0.0.

## Risks / Trade-offs

- **[`fastatan` used elsewhere?]** → No: grep confirms `fastatan` is called only by
  `V2Dist::overdrive`, so the change is contained.
- **[`lrintf` rounding mode]** → matches x87 `fistp` default (round-to-nearest-even);
  verified by mode-3 exact match.
- **[Other dist modes]** → guarded: `comp_leaves` shows modes 0/2 still MATCH.
