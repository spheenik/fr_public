## 1. Bitcrusher (mode 3)

- [x] 1.1 Confirm asm uses `fistp` (round) vs C++ `(sInt)` (truncate); clamp bounds
  identical. Fix: `(sInt)lrintf(in*crush1)`. **mode 3 matches exactly (0).**

## 2. Overdrive (mode 1) — fastatan

- [x] 2.1 Rule out `set()` (libm `atan` vs asm `fpatan` — both precise); isolate the
  bug to `fastatan` (CLIP matches, differs only by `fastatan`).
- [x] 2.2 Build a `fastatan` oracle: expose asm `fastatan` as a global + `v2x_fastatan`
  trampoline (ABI st0/st1/ax/ebx); sweep −8..8 vs C++ (`comp_fastatan.cpp`).
- [x] 2.3 Fix #1 — restore shared `cxm2` (swap `c[3]`↔`c[4]` in both coeff rows) to
  match the asm rational form.
- [x] 2.4 Fix #2 — replicate the exponent-byte table-select bug:
  `coeffs[x >= 1.0f && x < 2.0f]`.
- [x] 2.5 Verify: `fastatan` sweep worst |cpp−asm| = 0.0; overdrive matches (6e-8).

## 3. Verify

- [x] 3.1 `comp_leaves`: dist modes 0–3 all MATCH (0 diverge).
- [x] 3.2 No regression: `comp_osc` (1/4, pre-existing tri/saw), `comp_flt` (0/7).
