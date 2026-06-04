# Tasks: fix-v2-boost-a0-dist-gain2

## 1. Confirm the gain2 hypothesis before fixing (trace first, like BOOSTTRACE)

- [x] 1.1 Add a `DISTTRACE`-style bit-dump of the OVERDRIVE `gain2` (and `offs`)
      to `V2Dist::set` on the C++ side (hex of the stored sF32, gated by env,
      `V2_VALIDATE` only).
- [x] 1.2 Mirror it on the asm side: emit routine in
      `v2/validate/asm_appendix.asm` + C callback in `asm_stubs.cpp`
      (pattern: `boostdbg_c`), injected by `build.sh` sed after the `.mode1`
      `fstp [gain2]`.
- [x] 1.3 Run both harnesses on pzero, diff the gain2 sequences; confirm the
      first mismatch lands at the 9.677 s ch7 patch event (the `vce_dist`
      onset). If gain2 matches everywhere, STOP and re-localize before touching
      dist (the fix below would be unjustified).

## 2. Boost fixes (synth_core.cpp V2Boost::set)

- [x] 2.1 Reassociate the `a0` sum to the asm order:
      `ia0 = 1.0f / ((bs + cAm1) + Ap1)`, with a comment marking the
      association as load-bearing (cites syBoostSet fadd order,
      synth.asm:2818-2823) so it isn't "cleaned up" later.
- [x] 2.2 Replace `beta = sqrtf(2.0f * A)` with the asm's stepwise form:
      `asq = A*A; beta = sqrtf((asq + 1.0f) - (Am1*Am1))` (each step an sF32
      store or single-rounded op), keeping/inverting the existing comment: the
      binomial simplification is bit-unfaithful for 21/127 amounts.
- [x] 2.3 Re-run BOOSTTRACE on both cores over the full song; require the
      coefficient sequences byte-identical (was: 1-ULP divergence on every
      amount-92 set from ordinal #12184).

## 3. Dist gain2 fix (synth_core.cpp V2Dist::set)

- [x] 3.1 Add `v2_atan` next to `v2_pow2`/`v2_pow`: `V2_X87_FAITHFUL` → inline
      `fld1; fpatan` (operands per asm `.mode1`: atan(gain1) = fpatan with st1 =
      gain1, st0 = 1); portable → libm `atan` as today.
- [x] 3.2 Use it in the OVERDRIVE branch:
      `gain2 = (para->param1 / 128.0f) / v2_atan(gain1);` (128.0f is a power of
      two — exact; no fci128 constant hazard).
- [x] 3.3 Re-run the gain2 trace diff (task 1) — require byte-identical
      sequences over the whole song.

## 4. Oracle: boost amount sweep

- [x] 4.1 Extend the boost leaf oracle (comp_leaves or a new comp_boost case)
      to render a short identical-input buffer through asm and C++ boost for
      EVERY amount 1..127 and compare bit-exact (eps=0), reporting the failing
      amount on mismatch.
- [x] 4.2 Self-test the sweep: temporarily revert the beta fix (or the a0
      association) locally and confirm the sweep fails naming the affected
      amounts (e.g. 92 for a0, the 21-amount set for beta); restore.
- [x] 4.3 Run the full oracle suite (`comp_osc`, `comp_flt`, `comp_leaves`,
      `comp_trisaw`, `comp_fastatan`, `comp_lfo`, `comp_osc_exact`, fixed
      pairs) — no regressions.

## 5. Whole-song re-measure

- [x] 5.1 `CHANSOLO=7` BUSTAP A/B over ≥25 s: require the `ch_boost` tap to
      introduce no divergence at 9.677 s (post-boost diff ≤ post-comp diff) and
      record the new `ch_dist` value.
- [x] 5.2 Full-song (`auto`) A/B: record new max-abs/rms and first-divergence
      sample (baseline: 0.0218729973 / 0.000411, first divergence float
      #1567954 ≈ 17.8 s). Clean /tmp dumps before runs (31G tmpfs!).
- [x] 5.3 Re-run the four per-voice ledgers (freqlog/ctrllog/fltlog/osclog) —
      must stay at 0 divergences.
- [x] 5.4 If a residual remains: re-run the BUSTAP chain to localize the next
      stage (expect `post_compr` candidates); record findings as the
      starting point of the next change — do not scope-creep into fixing here.

## 5b. Measured outcome (recorded per 5.4 — next change starts here)

- Set-paths now bit-exact whole-song: BOOSTTRACE 312,608 sets / 0 mismatches,
  DISTG2TRACE 655,685 sets / 0 mismatches. All four per-voice ledgers still 0.
- ch7-solo 25 s chain: `ch_dist` 6.1e-3 → 1.3e-4 (46x). BUT `ch_boost` STILL
  shocks at the 9.677 s activation (input 1.6e-7 → output 2.0e-5, coeffs
  bit-exact; physically impossible as amplification, biquad L1 ≈ 2). The spec
  scenario "In-context boost tap closes" is therefore NOT fully met — a SECOND
  mechanism exists at the same event class. (Scenario removed from this change's
  delta at archive time; it belongs to the follow-up change's spec.)
- Whole-song: max 0.021984458 / rms 0.000408 — magnitude UNCHANGED vs baseline
  0.0218729973 / 0.000411 (first divergence float #1567954 → #1568074). The
  fixed 1-ULP seeds were real but not the dominant whole-song driver; the
  remaining mechanism fires at (presumably) every channel-activation event.
- UNVERIFIED LEAD for the next change: C++ renderFrame SKIPS channels with no
  active voices (`if (voice == POLY) continue;`, synth_core.cpp ~3919),
  freezing that channel's chain state (boost IIR, chorus delay, dcf). If the
  asm `syChanProcess` path (synth.asm ~5127) runs every channel every frame,
  the idle-channel states evolve there (silence + dcoffset through IIRs) and
  differ at re-activation — matching the observed shock-with-bit-exact-
  inputs-and-coeffs signature exactly. VERIFY the asm channel loop first.

## 6. Docs & bookkeeping

- [x] 6.1 Update `v2/validate/HANDOVER.md`: new LATEST STATE numbers; remove the
      stale "UNCOMMITTED — commit these!" line (fixes are committed: 297b66f);
      add the a0-association + beta + gain2 findings to the fixes list; note
      the sweep oracle; record the exoneration results (BoostCos/Sin, beta for
      pzero, sqrt sites) under falsified/checked hypotheses.
- [x] 6.2 Document the new gain2 trace env var (and BOOSTTRACE if undocumented)
      in `v2/validate/BUILD.md`.
- [x] 6.3 Commit on `v2-port-fidelity` following the established message
      pattern ("v2: N port-fidelity fixes (boost a0/beta, dist fpatan) —
      whole-song residual 0.0219→X").
