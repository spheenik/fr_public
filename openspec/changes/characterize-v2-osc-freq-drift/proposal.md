## Why

The faithful V2 port (`V2_X87_FAITHFUL` build) is down to a whole-song A/B residual
of ~0.049 on `pzero_new.v2m` (commit `01ac9bd`, after the noise-filter-state and
FM-sine-phase fixes). Every isolatable DSP block bit-matches the asm; the remainder
is **diffuse, gradually-accumulating oscillator-frequency phase drift** — inaudible,
but unexplained at the bit level. We currently *assert* it is benign freq drift; we
have not *proven* it, and we cannot rule out a small structural divergence hiding
under the diffuse output. The residual must be characterized at its source.

The structure of the synth makes this tractable. Both the oscillator and the LFO
advance phase with an **integer** accumulator (`sU32 cnt; cnt += freq;` —
`synth_core.cpp:665,794,1451,1559`). Integer accumulation is bit-exact forever and
cannot drift. The **only** non-exact step in the entire phase path is the moment
`freq` is computed, via an x87 `fistp` round-to-nearest into an integer
(`v2_oscfreq`, `v2_fistp`). So the residual can only be born at a small set of
`float → int` rounding boundaries, each producing at most a **±1 integer** tie-flip,
which the integer phase counter then integrates forever. Per the contractive-vs-
integrating argument, no contractive float state (envelopes, IIR filters) can
produce *monotonic* buildup — only the marginally-stable integer phase counters can,
and they only diverge when a freq recompute tips a tie.

Therefore the diffuse output residual is the double-integral of a handful of
discrete, timestamped, attributable events. Instrumenting the **source** (freq) once
converts an un-bisectable 235-second fog into a finite list of `(time, channel,
voice, osc, freq)` records — and that ledger doubles as a permanent regression guard
that distinguishes benign sub-ULP tie-flips from real structural regressions.

## What Changes

- **Knockout experiment (validation-only).** Add a mechanism to cross-feed the
  computed integer `freq` between the two cores (C++ core reads the asm's `freq`, or
  both snap to a shared value) at every `chgPitch` / LFO `set`. If the whole-song
  residual collapses to ~0, freq is proven to be the entire story and nothing
  structural hides underneath. This is the decisive go/no-go probe.
- **Freq-divergence ledger (validation-only).** Instrument every `freq` computation
  in both cores to append `(sample_time, channel, voice, osc_index, pno_bits,
  freq_int)` to a per-core log, mirrored across the asm and C++ cores exactly like
  the existing BUSTAP rig so the streams are call-for-call comparable. A diff tool
  reports every record where `freq_int` differs: the discrete, attributed events
  that constitute the residual. Records the count, the per-event `|Δ|`, and the
  `(time, channel, voice, osc)` coordinates.
- **(Optional, gated by ledger findings) Phase-difference staircase.** Log `cnt` per
  oscillator at block boundaries in both cores; emit `signed(asm_cnt − cpp_cnt)` over
  time to visualize the buildup as a monotone staircase and attribute each riser to a
  ledger event.
- **(Optional) Tie-margin histogram.** For each `fistp`, record the distance of the
  pre-round value from the nearest half-integer tie; histogram it to quantify how
  much of the population rides the knife-edge and whether the residual is even
  reducible.
- **ULP genealogy + fix attempt.** For each divergent ledger event, capture the
  `fistp` input bit-for-bit (`pno = pitch+note-60`, the pre-round product) and walk
  it upstream to the modulation source that introduced the ULP — the LFO output is
  exact (`utof23` of an integer counter), so the ULP is most likely born in the
  modmatrix arithmetic or an envelope multiply (the one contractive-but-iterated
  float in the pitch path). Where a concrete port divergence is found (asm correct,
  port wrong), fix it directly; where it is a genuine asm behavior, gate it behind a
  `BUG_V2_*` define per the port-fidelity philosophy. Re-run the ledger and whole-
  song A/B to record the reduction. Stop when the remaining events are irreducible
  sub-ULP tie-flips against x87 round-to-nearest-even (diminishing returns).
- **Regression guard.** Promote the ledger diff into a cheap permanent assertion:
  divergent-event count ≤ a known baseline, all `|Δ| == 1`, all at sub-ULP tie
  margin — so a future refactor introducing a structural bug (wrong mode, sequencing
  error) spikes the count or produces `|Δ| > 1` and is caught.

The portable default build is untouched; all instrumentation is `V2_VALIDATE`-gated
like the existing bus tap. `synth.asm` is never edited (the harness taps a generated
copy). This change is **diagnostic-first**: it proves what the residual is and guards
against regressions. It then **uses the ledger to chase a fix** — tracing each
divergent event's upstream ULP (modmatrix/envelope feeding a `fistp` tie) and
correcting concrete port divergences — but with a defined stopping point: driving the
residual to exactly 0 is **not** required, because the tail is expected to be
irreducible sub-ULP tie-flips against x87 round-to-nearest semantics. Success is a
proven attribution, any genuine port bug fixed, and a measured residual reduction.

## Capabilities

### New Capabilities
- `v2-freq-divergence-diagnostic`: A validation-only diagnostic that instruments the
  integer `freq` computation (osc + LFO) in both the asm and C++ cores, produces a
  call-for-call-comparable per-core ledger of `(time, channel, voice, osc, pno,
  freq)`, and a diff/report tool that attributes the whole-song residual to discrete
  ±1 tie-flip events. Includes the knockout cross-feed probe and the regression-guard
  assertion over the ledger diff. Optional phase-staircase and tie-margin sub-probes.

### Modified Capabilities
<!-- No spec-level requirement change to the existing harness/equivalence capabilities;
     the new tap is additive and validation-gated. The whole-song A/B and bus-tap
     contracts in v2-core-validation are unchanged. -->

## Impact

- **Code:** `v2/synth_core.cpp` (validation-gated ledger hook in `V2Osc::chgPitch`
  and `V2LFO::set`, behind `V2_VALIDATE`); `v2/validate/asm_appendix.asm` (mirrored
  asm-side freq tap, like `synthDebugGetBus`); `v2/validate/v2mplayer_port.cpp` (dump
  the ledger when an env var is set, e.g. `FREQLOG=<prefix>`); a new diff/report
  script in `v2/validate/` (sibling to `compare.py`); optionally `build.sh` for the
  knockout/cross-feed variant.
- **Build:** validation build only; no change to the portable default build or to
  `synth.asm`.
- **Docs:** `v2/validate/HANDOVER.md` "What's OPEN" updated with the ledger findings
  (event count, attribution, knockout result) and the residual verdict.
- **No runtime/audio impact**: instrumentation is compiled out of the shipping build.
