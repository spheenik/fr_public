# Design: fix-v2-boost-a0-dist-gain2

## Context

Whole-song A/B (`pzero_new.v2m`, faithful build, 235.3 s) currently diverges
max-abs 0.0219, bit-exact for the first 17.8 s. The entire per-voice path is
bit-exact (all four ledgers report 0 divergences over the whole song); the
BUSTAP chain showed the residual entering at `ch_boost` (9.3e-4) and `ch_dist`
(6.1e-3), amplified by `post_compr` (2.0e-2).

This session's diagnostics (all read-only, evidence in full):

- `CHANSOLO=7` + BUSTAP over 25 s: `ch_comp` diff 1.6e-7 vs `ch_boost` diff
  1.2e-4 — but the boost biquad's peak gain AND impulse-response L1 norm are
  both ~2x (computed from the dumped coefficients), so the boost stage cannot
  *amplify* its input diff by 750x. The divergence is *born* in boost.
- `ch_boost` first diverges at stereo sample 426752 (9.677 s) **while its input
  is still bit-exact at that sample** (onset diff 1.13e-5, then decaying over
  seconds — an IIR transient relaxing, i.e. a one-shot state/coefficient shock).
- `BOOSTTRACE` full-sequence diff: set-call ordinals before ~12184 are
  bit-identical (amounts 54 and 66, two channels). From #12184 a **third**
  boost channel switches on with **amount 92**, and from then on every amount-92
  set differs by exactly 1 ULP in **all five** coefficients
  (asm `b0=3f80fa31 b1=bffcf45b b2=3f782503 a1=bffcfc08 a2=3f7a0a09`,
  cpp `b0=3f80fa30 b1=bffcf45a b2=3f782501 a1=bffcfc07 a2=3f7a0a08`).
- All five sharing the shift implicates their common factor `ia0 = 1/a0`.
  Reading `syBoostSet` (synth.asm:2818-2823): the asm sums
  `a0 = (bs + cAm1) + Ap1`; the port (`synth_core.cpp:2347`) sums
  `(Ap1 + cAm1) + bs`. A 24-bit float simulation of both orderings with the
  dumped `SRfcBoostSin/Cos` reproduces the observed values **bit-exactly**,
  including the agreement at amounts 54/66 and the disagreement at 92.
- `vce_dist` first diverges at exactly the same 9.677 s patch event (2.98e-8),
  implicating the other set-path site: `gain2 = (param1/128)/atan(gain1)`
  (libm double `atan`) vs asm `fpatan` (synth.asm:1833-1838, `.mode1`).
- Exonerated by the same audit: `SRfcBoostCos/Sin` (SRTRACE bit-identical:
  `3f7ff109`/`3caf0f9e`), the beta formula *for pzero's amounts* (54/66/92 all
  coincide), and all `sqrtf` sites (IEEE correctly-rounded on both sides).

Constraints (project philosophy, see HANDOVER.md):
- Default build = 100% faithful to the shipping asm.
- C++ porting errors are fixed directly, no flag.
- x87-fundamental fidelity (instruction-level transcendentals) is gated behind
  `V2_X87_FAITHFUL`; the portable build keeps libm.
- `v2/synth.asm` is never edited.

## Goals / Non-Goals

**Goals:**
- Make `V2Boost::set` produce bit-identical coefficients to `syBoostSet` for
  **every** amount value (not just pzero's three), in both builds.
- Make the faithful build's `V2Dist::set` OVERDRIVE `gain2` bit-identical to the
  asm `fpatan` setup.
- Extend the leaf-oracle coverage so set-path data-dependent rounding is swept
  exhaustively (boost: all 127 amounts).
- Re-measure and document the new whole-song baseline.

**Non-Goals:**
- No change to the boost/dist *render* loops (already matched).
- No whole-song bit-exactness guarantee — `post_compr` may reveal a next-layer
  source once boost/dist collapse; that's a follow-up investigation.
- No portable-build replacement of libm `atan` (gated only).
- Not promoting `freqdiff.py` to a regression guard here (open task 6.x of
  `characterize-v2-osc-freq-drift`).

## Decisions

1. **Reassociate `a0` as `(bs + cAm1) + Ap1`, unconditionally (no flag).**
   This is a porting error (the asm was always this way); per philosophy it's
   fixed directly. Pure-C reassociation is portable and deterministic under
   any FP model that rounds each operation (x87 PC=24, SSE, ARM).
   *Alternative considered*: gating under `V2_X87_FAITHFUL` — rejected; nothing
   x87-specific about operand association, and the portable build moves closer
   to the asm for free. Guard the written form with a comment so a future
   "cleanup" doesn't re-normalize the order (same trap as the fixed `Aia0`
   grouping ten lines below).

2. **Restore the asm's stepwise beta: `sqrt((A²+1) − (A−1)²)`, unconditionally.**
   Same class: porter's algebraic simplification (`sqrt(2A)`) is a porting
   error against the bit-fidelity goal. 21/127 amounts differ. Cheap (set-path,
   runs per patch set, not per sample). Keep the explanatory comment but invert
   it: the "redundant" form is load-bearing for bit-fidelity.
   *Alternative*: keep `sqrt(2A)` since pzero doesn't care — rejected; the next
   v2m would re-open the exact hunt this change closes, and the oracle sweep
   (decision 4) would fail for 21 amounts.

3. **`gain2` via a new `v2_atan` helper following the existing
   `v2_exp2`/`v2_powf` pattern**: `V2_X87_FAITHFUL` → inline
   `fld1; fpatan` (one instruction pair, mirroring asm `.mode1`'s
   `fld gain1; fld1; fpatan` then `fdivp`); portable → libm `atan` exactly as
   today. Compute the quotient + 24-bit store in the same shape as the asm
   (load param1·fci128, divide by atan result, store to `sF32`) — at PC=24 the
   simple C expression `(para->param1 / 128.0f) / v2_atan(gain1)` rounds each
   op to single, matching the asm's `fmul dword [fci128] / fdivp / fstp dword`.
   128.0f is a power of two (exact divide = exact multiply), so no `fci128`
   constant-promotion hazard exists at this site (unlike `fci12`).
   *Alternative*: replicating the whole `.mode1` tail in inline asm — rejected
   unless measurement shows the C expression still off; start minimal.

4. **Boost oracle: sweep amounts 1..127 with eps=0 on the coefficients (via
   BOOSTTRACE-style readback or output comparison).** The existing leaf test
   passed because its single amount was lucky. Sweeping the full domain of the
   set-path input is the general lesson (cf. `comp_osc_exact` sweeping all
   notes). Compare rendered output bit-exact over a short buffer per amount —
   output comparison subsumes coefficient comparison and reuses the existing
   harness pattern.

5. **Order of verification**: leaf oracles first (`comp_leaves` + new sweep),
   then `CHANSOLO=7` BUSTAP (expect `ch_boost` → 0 at 9.677 s), then whole-song
   A/B + the four ledgers (expect first-divergence to move past 17.8 s and/or
   magnitude well below 0.0219). Record numbers in HANDOVER.

## Risks / Trade-offs

- [GCC may re-associate or constant-fold the rewritten `a0`/beta expressions]
  → The build runs `-O2` without `-ffast-math`; IEEE C++ forbids reassociation
  of explicit parenthesized float ops. Verify by BOOSTTRACE diff (sequences must
  be byte-identical), which is part of the task list anyway.
- [The beta rewrite changes coefficients for amounts pzero *doesn't* use, so
  whole-song A/B can't validate it] → the 127-amount sweep oracle validates it
  exhaustively against the real asm.
- [`fpatan` fix may not fully close `vce_dist`/`ch_dist` (hypothesis is strong
  but unverified — gain2 was never dumped from the asm side)] → add a
  DISTTRACE-style gain2 bit-dump on both cores (asm side via `asm_appendix.asm`
  like `boostdbg_c`) BEFORE fixing, to confirm; keep the trace for regression.
- [Excess-precision hazard in the new `v2_atan` call site: GCC could keep
  `atan`'s result or `param1/128` live at 80-bit] → faithful build is
  `-mpc32 -mno-sse`; arithmetic rounds to 24-bit at PC=24, and the result is
  stored to `sF32` members immediately. Verify bit-exactness via the gain2
  trace, the same standard as every other fix.
- [Removing the leftover misplaced `v2/validate/openspec/` scaffold] — already
  cleaned up during exploration; noted here in case a stray copy reappears in
  `git status`.

## Migration Plan

Working-tree change on branch `v2-port-fidelity`; commit follows the established
message pattern ("v2: N port-fidelity fixes (...) — whole-song residual X→Y").
Rollback = revert the commit; no data or build-system migration.

## Open Questions

- Does `post_compr` still amplify a residual after boost/dist are fixed (i.e.
  is there a fourth source)? Measured at the end of this change; if yes, that
  becomes the next localization change, not scope creep here.
- The `ena=54/66/92` raw-amount byte in the asm trace vs C++ `enabled` bool is
  cosmetic, but if a future song uses fractional amounts (0 < amount < 1), the
  C++ `(sInt)para->amount != 0` truncation could disagree with the asm's
  enable test — worth a 5-minute read of `syChanSet`'s ena derivation while in
  the file (out of scope to fix unless trivially confirmed).
