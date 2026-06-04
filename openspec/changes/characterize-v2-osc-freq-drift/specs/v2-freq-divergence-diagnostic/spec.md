## ADDED Requirements

### Requirement: Freq-computation ledger

The validation build SHALL append one fixed-width record to a per-core ledger every
time an oscillator or LFO computes its integer `freq` (in `V2Osc::chgPitch` and
`V2LFO::set`, and their asm equivalents `syOscChgPitch` / `syLFOSet`). Each record
SHALL contain a monotonic call ordinal, a kind tag (osc or lfo), the object's offset
within the synth instance, the computed `freq` integer, and the pre-round float input
(`pno` for osc; the pre-`fistp` argument for lfo). Recording SHALL be `V2_VALIDATE`-
gated and activated by an environment variable (e.g. `FREQLOG=<prefix>`); when
inactive it SHALL NOT alter any audio output or DSP state.

#### Scenario: Ledger captures every freq computation in order
- **WHEN** the song is rendered with `FREQLOG` set on either core
- **THEN** the ledger contains exactly one record per `chgPitch`/LFO `set` call, in
  call order, each tagged with its call ordinal, kind, instance offset, computed
  `freq`, and pre-round input

#### Scenario: Ledger is inert when disabled
- **WHEN** the song is rendered with `FREQLOG` unset
- **THEN** the whole-song A/B magnitude and every per-block oracle (`comp_osc 0/4`,
  `comp_flt 0/7`, `comp_leaves 0`) are byte-identical to a build without the ledger
  hook compiled in

#### Scenario: asm-side capture does not perturb the fistp result
- **WHEN** the asm core renders with pre-round float capture enabled (non-popping
  `fst` to a temp before the `fistp`)
- **THEN** the computed `freq` integers and the whole-song output are byte-identical
  to the same render with capture disabled

### Requirement: Call-for-call ledger diff

A diff tool SHALL compare the asm and C++ ledgers by call ordinal and report every
divergent event. For each divergence it SHALL emit the event's coordinate (instance
offset resolved to channel / voice / osc-or-lfo index), the timestamp in seconds, the
two `freq` values and their signed `Δ`, and whether the two `pno` inputs are bit-
identical (a pure `fistp` tie-flip) or differ (an upstream ULP). A length or
instance-offset mismatch at the same ordinal SHALL be reported distinctly as a
structural divergence.

#### Scenario: Diffuse residual resolved to discrete events
- **WHEN** the asm and C++ ledgers from a full-song render are diffed
- **THEN** the tool prints a finite, timestamped, coordinate-attributed list of every
  record whose `freq` differs, plus per-event `Δ` and the pure-tie-vs-upstream-ULP
  classification

#### Scenario: Structural desync is flagged distinctly
- **WHEN** the two ledgers differ in length, or carry different instance offsets at
  the same call ordinal
- **THEN** the tool reports a structural divergence (not a `freq`-value divergence)
  and identifies the first ordinal at which the call sequences diverge

#### Scenario: Timestamps locate events in the song timeline
- **WHEN** an event diverges at call ordinal k
- **THEN** the tool maps k to a wall-clock position in seconds using the player's
  per-render sample-position record, at frame granularity

### Requirement: Freq knockout (cross-feed) probe

The C++ validation core SHALL support overriding each computed `freq` with the asm
core's value, replayed by call ordinal from a previously recorded asm ledger, gated by
an environment variable (e.g. `FREQKNOCKOUT=<asm-log>`). The override SHALL assert
that the instance offset at each replayed ordinal matches and abort loudly on
mismatch. This isolates whether the `freq` path is the sole source of the whole-song
residual.

#### Scenario: Knockout collapses the residual when freq is the sole source
- **WHEN** `harness_cpp` renders under `FREQKNOCKOUT` pointed at the asm ledger
- **THEN** the whole-song A/B magnitude against the asm drops to approximately zero,
  confirming the residual is entirely freq-borne

#### Scenario: Knockout replay desync aborts
- **WHEN** the C++ core's freq-computation sequence does not align with the replayed
  asm ledger (different count or offset at an ordinal)
- **THEN** the override aborts loudly at the first mismatching ordinal rather than
  silently producing a misaligned result

### Requirement: Residual attribution and regression guard

The change SHALL record the measured event count, the per-event `Δ` distribution, and
the knockout result as the proven characterization of the residual. It SHALL provide a
permanent assertion over the ledger diff that fails if the divergent-event count
exceeds an established baseline, if any event has `|Δ| > 1`, or if any divergent event
is an upstream-`pno` difference rather than a sub-ULP `fistp` tie-flip.

#### Scenario: Guard tolerates benign tie-flips
- **WHEN** the ledger diff contains only sub-ULP `fistp` tie-flips with `|Δ| == 1` at
  or below the baseline count
- **THEN** the regression guard passes

#### Scenario: Guard catches a structural regression
- **WHEN** a future change introduces a `freq` divergence with `|Δ| > 1`, an upstream-
  `pno` difference, or a divergent-event count above the baseline
- **THEN** the regression guard fails and names the offending event(s)

### Requirement: ULP genealogy and targeted fix

For each divergent event classified as an upstream `pno` difference, the change SHALL
trace the differing float to the modulation source that produced it. A divergence
where the asm is correct and the port is wrong SHALL be fixed directly; a genuine asm
behavior SHALL be gated behind a `BUG_V2_*` define per the port-fidelity philosophy.
After each fix the ledger and whole-song A/B SHALL be re-run and the change in
divergent-event count and residual magnitude recorded. Driving the residual to exactly
zero is NOT required; remaining same-`pno` sub-ULP tie-flips are accepted as
irreducible.

#### Scenario: Upstream port bug fixed and verified
- **WHEN** an upstream-`pno` divergence is traced to a concrete port error and fixed
- **THEN** the corresponding ledger events disappear, the whole-song residual is
  re-measured lower, and no per-block oracle regresses

#### Scenario: Irreducible tail accepted
- **WHEN** all remaining divergent events are same-`pno` `fistp` tie-flips against x87
  round-to-nearest-even
- **THEN** the change records them as the accepted irreducible residual and stops,
  rather than pursuing a zero
