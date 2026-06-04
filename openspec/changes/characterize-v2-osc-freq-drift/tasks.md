## 1. Integer ledger — core + asm capture

- [x] 1.1 Define the fixed-width record schema (call ordinal, kind, instance offset,
      freq int, pno bits, preround bits) in a shared validation header.
      → `FreqLogRec` (8×u32 = 32 bytes) in `compat.h`; ordinal is implicit (file order).
- [x] 1.2 Add the `V2_VALIDATE`-gated ledger hook in `V2Osc::chgPitch` (after the
      `v2_oscfreq` fistp) — append a record, incrementing the global call ordinal.
      Capture instance offset as `(char*)this - (char*)inst` (or osc base − synth base).
      → offset = `(char*)this - g_freqlog_synthbase` (synth base set at render() entry,
      mirrors asm `mov [this],ebp`).
- [x] 1.3 Add the matching hook in `V2LFO::set` (after the `v2_fistp` freq store),
      tagged `kind=lfo`.
- [x] 1.4 Add the asm-side `freqlog_emit` routine in `asm_appendix.asm` (pushad/popad
      integer copy, mirrors `mixtap_snap_*` safety) and the accessor that hands the
      ledger buffer + count to the player. → `freqlog_emit_osc/lfo`,
      `_synthDebugGetFreqLog@8` (read-and-clear), `_synthDebugArmFreqKnockout@8` (no-op).
- [x] 1.5 `build.sh`: sed-inject `call freqlog_emit` after the fistp store in the asm
      `syOscChgPitch` and `syLFOSet` of the generated copy (do not edit `synth.asm`).
      → anchored on the unique `fistp [ebp+syW{Osc,LFO}.freq]` stores, count-checked.
- [x] 1.6 `v2mplayer_port.cpp`: when `FREQLOG=<prefix>` is set, dump the per-core
      ledger after the render loop, and record the per-render sample position for
      ordinal→time mapping (D3). → `<pfx>.freqlog` (records) + `<pfx>.freqmap`
      (cumulative-count, sample-pos) per render chunk.
- [x] 1.7 Verify inertness: with `FREQLOG` unset, `comp_osc 0/4`, `comp_flt 0/7`,
      `comp_leaves 0`, and the whole-song A/B number are unchanged vs a build without
      the hook (spec: "Ledger is inert when disabled").
      → oracles all 0; whole-song max-abs = 0.0491175018 = known baseline. PASS.

## 2. Ledger diff tool

- [x] 2.1 New `v2/validate/freqdiff.py` (sibling to `compare.py`): read both ledgers,
      walk by call ordinal.
- [~] 2.2 Resolve instance offset → (channel, voice, osc/lfo index) ... map ordinal →
      seconds via the player's sample-position record. → time mapping done (.freqmap).
      REVISED: raw byte offset is NOT a valid cross-core key — the asm `syWOsc` and
      C++ `V2Osc` have different sizes, so offset-from-synth-base has a different
      per-osc stride per core (deltas in multiples of 24). Alignment is by call
      ordinal; consistency key is `kind`. (voice,osc) resolution deferred — not needed
      for the headline result.
- [x] 2.3 Report each `freq`-value divergence with coordinate, timestamp, the two
      freq values, signed `Δ`, and pure-tie-vs-upstream-`pno` classification.
- [x] 2.4 Detect and report structural desync distinctly (length mismatch, or kind
      mismatch at the same ordinal) with the first diverging ordinal.
- [x] 2.5 Run on a full-song render; record the divergent-event count and the `Δ`
      distribution. → **79,412 / 8,759,985 osc** freq diffs; **0 / 5,829,444 lfo**
      (LFO bit-exact). All isolated (run-length 1; 0 kind-seq mismatches → alignment
      valid). `Δ` NOT ±1 — multiples of 8, median 6e-5 relative, max 3.2e-2 relative
      (freq is a ~10^7 phase increment, so abs Δ of 24–1000s = tiny relative). The
      "±1 tie-flip" magnitude in the proposal was wrong; these are upstream-pitch-ULP-
      driven, not pure fistp ties.

## KEY FINDING (knockout): osc freq is NOT the source of the 0.049 residual

The knockout (cpp replaying the asm's freq by call ordinal) left the cpp output
**byte-identical** to plain cpp (error 0), so the whole-song A/B vs asm stayed at
**0.0491175018** — unchanged. The knockout path is proven live (a +5,000,000 corrupt
ledger diverges by 2.79 at sample 4292). Therefore the 79,412 real osc-freq
differences are inaudible / non-contributing, and the working hypothesis ("residual =
osc-frequency phase drift", commit 01ac9bd) is **FALSIFIED**. Since `freq` is a
bit-exact function of `pitch+note` (comp_osc_exact = 0), the freq diffs imply `pitch`
itself differs upstream (modmatrix/control ULP) — but that upstream diff does not
reach the audio via freq. The same `chgPitch` also computes `nffrq` (noise-filter
freq), which the knockout does NOT override — a prime suspect for the real residual.

## 3. Knockout (cross-feed) probe

- [x] 3.1 C++ core: under `FREQKNOCKOUT=<asm-log>`, load the asm ledger and override
      each computed `freq` by call ordinal; assert kind matches (offset is not
      cross-core comparable — see 2.2), warn-and-fall-through past the ledger tail.
- [x] 3.2 Record the asm ledger (`harness_asm` + `FREQLOG`), then render `harness_cpp`
      under `FREQKNOCKOUT` and compare to asm.
- [x] 3.3 Record the result: does the whole-song residual collapse to ~0?
      → **NO. Residual unchanged (0.0491175018), cpp output byte-identical with vs
      without knockout.** freq is exonerated as the residual source (see KEY FINDING).
- [x] 3.4 Verify the knockout path is live → corrupt ledger (+5,000,000/freq) diverges
      by 2.79 at sample 4292, proving overrides do reach the audio.

## 4. Pitch + nffrq capture + ULP genealogy  (extended per user direction "chase nffrq")

- [x] 4.1 Capture the upstream `pitch` bits and the `nffrq` output bits in the record
      (osc), directly from memory (`[ebp+syWOsc.pitch]`, `[ebp+syWOsc.nffrq]`) — no FPU
      perturbation needed (both already stored). lfo stores its `rate` input.
- [x] 4.2 Extend the knockout to override `nffrq` as well as `freq` (the full chgPitch
      float output), and verify the path is live (corrupt freq → diverge 2.79).
- [x] 4.3 Genealogy: **pitch differs at 80,985 osc records; nffrq at 80,985; freq at
      79,412** (freq diff ⊂ pitch diff). So the upstream control input (pitch) genuinely
      differs by ULPs. **But knockout of freq+nffrq → 0 audio change** → the osc pitch
      ULP is inaudible.

## KEY FINDING 2 (BUSTAP localization): the residual is the VCF FILTER, not the osc

Per-voice-substage BUSTAP at the first divergence:
  vce_osc = 0 (BIT-EXACT)   vce_flt diverges @ sample 90880 (0.0081)   vce_dist/dcf follow
The oscillator output is bit-perfect; the divergence is **born at the filter**. Since
`comp_flt` is bit-exact given the same cutoff/reso (0/7), the diverging quantity is the
**modulated filter cutoff/resonance** — the same upstream modmatrix control ULP that
makes pitch differ, but landing on a recursive IIR (sustains) instead of the osc's
exact integer phase accumulator (washes out). ROOT = modmatrix/control-value
computation; FILTER = the sensitive destination. Next: knockout the filter
cutoff/reso input (same cross-feed technique) to confirm, then trace the modmatrix.

## 5. Targeted fixes

## KEY FINDING 3 (static read of modmatrix + empirical drift shape)

Per user direction, read the modmatrix + env against the asm:
- **modmatrix arithmetic is bit-faithful**: scale `(val-64)*fci128*2` (asm) vs
  `(val-64)/64` (C++) both exact (1/128, 1/64 are exact powers of two); mul-add order
  matches (`source*scale + vparaf[dest]`, 24-bit store); clamp matches except a
  harmless -0.0 edge.
- **env-tick is bit-faithful** per step; one real (minor) asymmetry found: the C++
  underflow check `if (val <= fclowest) {val=0; OFF}` runs after the switch for ALL
  states incl. ATTACK, while the asm only checks it in DECAY/SUSTAIN/RELEASE. Matters
  only for pathologically slow attacks; flag as a candidate `BUG_V2_*`.
- **Empirical drift shape (from the pitch-bit ledger):** the divergent pitch values
  are the SAME osc across CONSECUTIVE ticks, gliding linearly (~-0.0265/tick) with the
  asm↔cpp gap growing by a CONSTANT ~0.00048/tick (LINEAR, not geometric). That is an
  additive-accumulator drift in a modulation SOURCE — the literal "slowly building
  divergence". Per-step logic matches, so the drift is in the source's in-song
  accumulated state, invisible to the per-step oracles (comp_leaves/comp_lfo use
  static/short vectors). DECISIVE NEXT STEP: tap the per-tick source values
  (env.out / lfo.out per voice) into the ledger spare fields and diff to pin which
  source's accumulator drifts (and from which tick).

## KEY FINDING 4 (control-source ledger): the drift is the ENVELOPE sustain

Added a per-voice-per-tick control-source ledger (`ctrllog`, kind=2) capturing the
four accumulating mod sources (aenv/env2 out, lfo1/lfo2 out), mirrored in both cores.
Full-song diff (2,909,449 voice-ticks, 0 kind mismatches):
  lfo1 = 0 (BIT-EXACT)   aenv = 9,857   env2 = 88,842   lfo2 = 88,839
All ignite at ~record 1,609,203; lfo2 follows 40 records later (cascade — `voice->set`
runs every tick, so a drifting env that modulates lfo2's amp drags lfo2 along).

Tracing one voice across its lifetime nails the mechanism:
  before onset:  aenv 120.1322 / 120.1322  (identical)
  at onset:      aenv 120.1322 / 119.9013   → asm HOLDS, cpp DECAYS (~-0.23/tick)
  env2:          asm RISES (suf>1), cpp FALLS (suf<1)  → OPPOSITE directions
The asm env holds dead-constant = SUSTAIN with `suf = v2_exp2(fcsusmul·(sr-64))` =
v2_exp2(0) = **exactly 1.0** (so modulated sustain-rate sr = 64 exactly). The cpp env
has suf just off 1.0. `suf` crosses 1.0 at sr=64, so a modulated sr sitting on 64 that
differs sub-ULP between cores sends suf to opposite sides → the env val walks apart
LINEARLY. MODDUMP confirms env fields are modmatrix destinations and env2 is itself a
mod source (src=9) → env→env/filter feedback. That env drift feeds filter cutoff
(audible 0.049) and osc pitch (washes out — why the freq knockout did nothing).

ROOT chain: a mod SOURCE feeding an env sustain-rate (sr≈64) differs sub-ULP → suf
straddles 1.0 → env val drifts linearly → filter cutoff drifts → residual. The
modmatrix arithmetic + env-tick logic are bit-faithful; the seed is the sub-ULP source
value at the sr≈64 / suf=1.0 knife-edge. FINAL PIN (not done): capture env
suf/sr/dcf/sul/state per voice at record 1,609,203 to name the exact source + param.

## KEY FINDING 5 (the ROOT, and the FIX): note-off released too many voices

The control-source ledger (with corrected enum mapping — C++ V2Env::State is
{OFF,RELEASE,ATTACK,DECAY,SUSTAIN}, a DIFFERENT order than the asm
{OFF,ATTACK,DECAY,SUSTAIN,RELEASE}) showed the first real divergence is a voice whose
envelopes go to RELEASE in cpp while the asm keeps them in SUSTAIN/ATTACK — a GATE/
note-off divergence, NOT an env precision bug (suf was exactly 1.0).

ROOT BUG: `ProcessNoteOff`. The asm (synth.asm:5398-5400) releases the FIRST voice
matching (channel, note, gate) then `jmp .end` — STOPS. The C++ port (synth_core.cpp
note-off loop) looped over ALL POLY voices and released EVERY matching voice. When a
note is held on more than one voice of a channel (overlapping / retriggered notes),
the port released the extra voice the asm keeps sustaining → that voice's modulation
envelope drifts → filter cutoff drifts → the whole-song residual.

FIX (port error, no flag): add `break` after the first `voice->noteOff()`.

RESULT: whole-song max-abs **0.0491175018 → 0.0218729973** (rms 0.00152 → 0.00041);
oracles still 0/0/0. After the fix, osc pitch/freq/nffrq AND all four env/lfo control
sources are **bit-exact over the whole song** (the entire pitch/env drift was this one
bug). The freq/ctrl ledgers all show 0 divergences post-fix.

## REMAINING (separate, smaller): filter-internal precision (~0.022)

`vce_osc` is bit-exact and the filter ledger shows cutoff/reso/mode INPUT bit-exact
(0/0/0 over the song), yet `vce_flt` still diverges (first @ sample 90880). So given
identical input AND identical filter params, the filter OUTPUT diverges → a filter
coefficient / IIR-state precision difference, almost certainly the **moog 4-pole**
path (regular `cfreq = calcfreq·SRfclinfreq` is exact — nffrq proves it). comp_flt is
0/7 in isolation, so it's an accumulation the short oracle doesn't reach. This is the
deep precision tail, distinct from the logic bug above. NEXT: capture the moog
coefficients (moogf/p/q) + filter state to pin it.

- [x] 5.1 note-off early-release port bug fixed directly (no flag) — residual halved,
      all osc/env/lfo control now bit-exact.

## KEY FINDING 7 (remaining 0.022 fully localized): HIGH/BAND filter-chain x87 exponent

Built per-voice osc-output ledger (osclog) + filter ledger (fltlog: input+state+coeff+
mode). Results (whole song, records aligned — 0 cfreq/res mismatches over 7.2M):
- **osc output bit-exact** (osclog 0 divergences) — oscillator fully exonerated.
- filter **coefficients** (cfreq/res) bit-exact; parallel **combine** matches the asm
  arithmetically (commutative add); **0 filters** diverge with exact input (no pure
  single/parallel/vcf1 step divergence).
- Yet vce_flt diverges. Resolution: the diverging filters are fed by **serial HIGH-pass
  vcf1s** (1534 cases) whose OUTPUT `h = in - b·reso - l` (a cancellation) diverges
  while their block-boundary state l/b matches — so it doesn't show in the state tap.
  The transient decays over ~35 ms (stable IIR).
- Mantissa is equalized by PC=24, so the suspect is the **x87 80-bit register (15-bit
  exponent) vs C++ 32-bit float member (8-bit exponent)** for the filter's per-sample
  state/intermediates — the same cancellation-sensitivity the tri/saw uses `double`
  for in the portable build, here exposed in the voice filter.
- NEXT (a fix attempt with tradeoffs, not just a probe): capture the per-sample filter
  OUTPUT to confirm output-diverges-while-state-matches, then try matching the asm's
  x87 register-state retention (e.g. a wider working type for the filter block, like
  the tri/saw) and re-measure. Inaudible (0.022); deep x87-fidelity tail.

## KEY FINDING 6 (remaining 0.022): regular-filter TRANSIENT shock, not moog/precision

- The song uses **no moog filters** (mode histogram: BYPASS/LOW/BAND/HIGH only). The
  moog lead is a dead end.
- Filter ledger (post-block IIR state, mirrored asm/cpp): filter **coefficients
  (cfreq/res) are bit-exact** (0 divergences); the modulated cutoff/reso/mode INPUT is
  bit-exact; the `step_2x` arithmetic + operand order match the asm `.process` exactly;
  FIXDENORMALS=1 in both.
- Yet the filter **state** diverges — and tracing one filter shows a TRANSIENT: state
  matches, then JUMPS ~63k ULP (~0.002) at a single block, then the stable IIR decays
  it back to bit-identical over ~35 ms. A sudden one-block state jump with exact
  coeffs + exact summed osc means the filter got a DIFFERENT INPUT for a moment → a
  discrete per-voice perturbation, NOT a precision tail and NOT the moog.
- HYPOTHESIS: another subtle per-voice event/timing edge (osc retrigger phase, or a
  second note-allocation case) momentarily changes one voice's osc output (masked in
  the summed vce_osc=0). NEXT: capture per-block filter INPUT (first sample) in both
  cores to confirm input-vs-step, then trace that voice's note/alloc event at the
  shock block.
- [ ] 5.2 For each genuine asm behavior: gate behind a new `BUG_V2_*` define (default
      = asm-faithful) per the port-fidelity philosophy.
- [ ] 5.3 After each fix: re-run the ledger + whole-song A/B; record the drop in
      divergent-event count and residual magnitude; confirm no `comp_*` oracle
      regresses.
- [ ] 5.4 Stop when remaining events are irreducible; record them as the accepted
      residual (spec: "Irreducible tail accepted").

## 6. Regression guard

- [ ] 6.1 Set the guard baseline from the post-fix ledger (event count, `|Δ|` bound,
      tie-margin / pure-tie requirement).
- [ ] 6.2 Promote `freqdiff.py` into a pass/fail assertion: fail on count > baseline,
      any `|Δ| > 1`, or any upstream-`pno` divergent event. Wire into the validation
      flow alongside the `comp_*` oracles.
- [ ] 6.3 Self-test the guard: a deliberately injected `|Δ|>1` / upstream divergence
      makes it fail and names the offending event(s).

## 7. Docs

- [ ] 7.1 Update `v2/validate/HANDOVER.md` "What's OPEN" with: the proven attribution
      (event count + knockout result), any fixes landed, the new whole-song number,
      and the accepted irreducible tail.
- [ ] 7.2 Document the `FREQLOG` / `FREQKNOCKOUT` env vars and `freqdiff.py` usage in
      `v2/validate/BUILD.md`.
- [ ] 7.3 Note any new `BUG_V2_*` flag in the handover's ASM-bug list.
