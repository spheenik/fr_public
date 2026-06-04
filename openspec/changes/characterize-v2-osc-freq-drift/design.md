## Context

The faithful build (`V2_X87_FAITHFUL`) leaves a ~0.049 whole-song residual on
`pzero_new.v2m`. Every isolatable DSP block bit-matches the asm; the residual is
diffuse, slowly-accumulating osc-frequency phase drift (commit `01ac9bd`). We want to
*prove* that characterization at the source and, where a concrete port divergence is
found, fix it.

Current validation infrastructure (`v2/validate/`) we build on:

- **Two-process A/B.** `harness_asm` and `harness_cpp` are separate 32-bit binaries
  that render the same `.v2m` deterministically; `compare.py` diffs the raw f32 dumps.
- **BUSTAP rig.** Both cores expose `synthDebug*` accessors (C++ under `V2_VALIDATE`,
  asm mirrored in `asm_appendix.asm`). The asm side captures via **sed-injected
  `call <snap>` splices** after DSP stages plus **`pushad`/`popad` integer-copy snap
  routines** that touch no FPU state. The player (`v2mplayer_port.cpp`) polls the
  accessors after each `synthRender` and dumps per-stream files keyed by an env var
  (`BUSTAP=<prefix>`). Both cores get the identical chunk sequence, so streams are
  call-for-call comparable.
- **Byte-identical instance layout.** Allocation and init-zeroing are byte-identical
  across cores (the `sizeof(*this)` fix; confirmed in the handover). An object's
  **offset within the synth instance is a stable cross-core identity.**
- **Determinism.** Both cores process the song in identical order, so the *sequence*
  of `freq` computations is identical in order unless a structural divergence changes
  control flow.

The structural fact that makes this finite: osc and LFO phase are **integer**
accumulators (`cnt += freq`), so phase is bit-exact forever; the only non-exact step
is computing the integer `freq` via x87 `fistp` round-to-nearest (`v2_oscfreq`,
`v2_fistp`). The residual is therefore the integral of a finite set of ±1 integer
tie-flips born at those `fistp` sites.

## Goals / Non-Goals

**Goals:**
- **Prove the residual is freq-borne** via a knockout: cross-feed the asm's `freq`
  into the C++ core; if the whole-song residual collapses to ~0, freq is the entire
  story and nothing structural hides underneath.
- **Attribute it.** Produce a per-core ledger of every `freq` computation —
  `(call_ordinal, kind, instance_offset, pno_bits, preround_bits, freq_int)` —
  comparable call-for-call, and a diff tool that lists every divergent event with its
  `(time, channel, voice, osc/lfo)` coordinate and `Δ`.
- **Chase a fix.** Use per-event ULP genealogy (the captured `pno`/pre-round bits) to
  find the upstream source of each divergence; fix concrete port bugs directly, gate
  genuine asm behaviors behind `BUG_V2_*`, and record the residual reduction.
- **Leave a regression guard.** A cheap permanent assertion over the ledger diff:
  event count ≤ baseline, all `|Δ| == 1`, all at sub-ULP tie margin.

**Non-Goals:**
- Driving the residual to exactly 0 (the tail is expected to be irreducible sub-ULP
  round-to-nearest-even tie-flips).
- Touching the **portable default build** or `synth.asm` (the harness taps a
  generated copy; all hooks are `V2_VALIDATE`-gated).
- Re-opening the floating-point-precision axis on the global mix chain (exhausted; the
  residual is the osc/lfo freq tail, not the mix chain).

## Decisions

### D1. Event ledger, not a per-frame buffer snapshot
BUSTAP snapshots *buffers* per render frame. Freq divergence is an **event** that
occurs at each `chgPitch`/LFO `set` (control-rate, per MIDI frame, per oscillator).
So the ledger is an **append log of records**, one per freq computation, not a buffer
tap. Each core writes its own log; the diff is line-for-line.
- *Alternative considered:* snapshot the per-osc `freq` field every render frame. Rejected:
  it samples state, not events — it would miss intra-frame recomputes and couples the
  record rate to the frame size rather than to the actual cause.

### D2. Identity by instance offset, alignment by call ordinal
Each record carries the object's **offset within the synth instance** (stable and
identical across cores by the byte-identical-layout invariant) as its coordinate, and
a **monotonic call ordinal** (incremented once per freq computation across the whole
synth) as its alignment key. The diff walks both logs by ordinal; a length or
`instance_offset` mismatch at the same ordinal is itself a **structural** finding
(different number/order of freq computations), distinct from a same-coordinate `freq`
value divergence.
- *Why not symbolic (channel, voice, osc) tags?* The asm has no cheap symbolic identity at
  the `fistp` site; the offset is a single `lea`-and-store and the diff tool maps
  offset → (channel, voice, osc) once, using the known `V2Synth`/`V2Voice` layout.

### D3. Timestamp from the player's render position (frame-granular)
The synth core computing `freq` does not know absolute sample time. The **player**
does (it drives render in chunks). After each `synthRender`, the player records the
current ledger high-water mark against the absolute sample position, so the diff tool
buckets each event-ordinal into a `(seconds)` timestamp at frame granularity — enough
to locate events in the 235 s timeline. No per-event clock needed in the core.

### D4. Knockout via file-replay, not cross-linking
The A/B harness is two separate processes; do not link the cores together. Instead:
1. Run `harness_asm` with the ledger enabled → it writes the asm freq log.
2. Run `harness_cpp` under `FREQKNOCKOUT=<asm-log>`: the C++ core, at each freq
   computation, **overrides** its computed `freq` with the asm value indexed by call
   ordinal (and asserts the `instance_offset` matches). Render and compare.
If the residual collapses, freq is confirmed as the sole source.
- *Alternative considered:* snap both cores to a synthetic shared freq. Rejected: changes the
  audio for *both*, so it can't measure the C++-vs-asm gap — it only proves the phase
  path is deterministic, which we already know.
- *Replay alignment* is the same ordinal used by the diff; a desync (different call
  count) surfaces immediately as an ordinal/offset mismatch.

### D5. asm-side capture mirrors the BUSTAP splice pattern, FPU-safe
A `freqlog_emit` routine is **sed-injected** after the `fistp` store in the asm's
`syOscChgPitch` / `syLFOSet` (the same generated-copy injection `build.sh` already
does for `mixtap_snap_*`). The `freq` integer is already in memory post-`fistp`, so
the emit is a **pure integer append** under `pushad`/`popad` — no FPU disturbance, the
established safety contract.
- **Pre-round float capture (`pno`, pre-`fistp` product)** for ULP genealogy *does*
  need the x87 stack. Use **`fst` (non-popping)** to a temp before the `fistp`: it
  rounds the value to a 32-bit store for the record but **leaves `st0` intact**, so
  the subsequent `fistp` is bit-unchanged. The captured single-rounded `pno` is
  exactly the quantity to compare (the asm's constants are single anyway). This is
  gated separately (a heavier flag) and validated against the oracles to confirm zero
  perturbation.

### D6. Record every computation; compress in the diff
Log every freq computation (not just changes) for exact ordinal alignment. Volume is
bounded and comparable to existing dumps (the f32 streams are already 3.4 MB each);
the diff tool collapses runs and reports only divergent events.
- *Alternative considered:* log only on `freq` delta. Rejected: it breaks the simple ordinal
  alignment and complicates desync detection for marginal space savings.

### D7. ULP genealogy → targeted fix, with a defined stopping point
For each divergent event, the captured `pno_bits` localize whether the divergence is
born at the `fistp` tie (same `pno`, `freq` differs → pure round-to-nearest tie) or
upstream (the `pno` float itself differs → trace to the modmatrix/envelope that
produced it; the LFO output is exact `utof23`, so the prime suspect is modmatrix
arithmetic or an envelope multiply). Concrete port divergences are fixed directly;
genuine asm behaviors are gated behind `BUG_V2_*`. Stop when remaining events are
same-`pno` sub-ULP tie-flips — irreducible against x87 round-to-nearest-even.

## Risks / Trade-offs

- **[FPU perturbation from pre-round capture changes the very value being measured]** →
  Use non-popping `fst` to a temp; verify `comp_osc 0/4`, `comp_flt 0/7`,
  `comp_leaves 0` and the whole-song number are byte-identical with the capture on vs
  off before trusting any captured `pno`. Gate float capture behind its own flag so
  the cheap integer ledger never carries this risk.
- **[Knockout replay desync silently corrupts the result]** → Assert `instance_offset`
  matches at each replayed ordinal; abort loudly on mismatch. A desync is then a
  *finding* (structural divergence), not a silent wrong answer.
- **[Ledger volume / IO cost on the 235 s render]** → Fixed-width binary records,
  same dump discipline as the f32 streams; the diff tool streams rather than loading
  whole files. Acceptable per existing dump sizes.
- **[The fix hunt hits diminishing returns against irreducible tie behavior]** → The
  stopping point is defined up front (D7): same-`pno` sub-ULP ties are accepted; the
  deliverable is the proven attribution + guard, not a zero.
- **[Instance-offset identity assumes byte-identical layout]** → Already an
  established, tested invariant (POISON diagnostic, allocation byte-identical). The
  diff's offset-mismatch detection would catch any regression of that invariant too.

## Migration Plan

Validation-only; nothing ships. Rollout is additive within `v2/validate/`:
1. Land the integer ledger (core hook + asm splice + accessor + player dump + diff
   tool); confirm oracles + whole-song number unchanged with the ledger compiled in
   but inactive (env unset).
2. Run the ledger on the full song; record event count + attribution.
3. Run the knockout; record residual collapse (or not).
4. Add pre-round float capture; re-verify zero perturbation; run ULP genealogy.
5. Apply fixes (direct or `BUG_V2_*`); re-run ledger + whole-song; record reduction.
6. Promote the diff into the regression-guard assertion.
Rollback is removal of the `V2_VALIDATE`-gated hooks and the generated-copy splices;
the portable build and `synth.asm` are never touched, so there is nothing to revert
there.

## Open Questions

- **Control-rate granularity:** how many freq computations occur per second (per osc /
  per lfo)? Sets ledger size and timestamp resolution. Resolve by counting in the
  first instrumentation pass.
- **Where the upstream `pno` ULP enters:** is the modulated pitch recomputed per
  frame or per sample, and does the modmatrix sum order differ between cores? Resolve
  by reading the render-frame control path once the genealogy points there.
- **One ledger or two:** osc-freq and lfo-freq events share the record schema but feed
  different downstream paths (pitch vs amp/pitch). Keep them in one stream tagged by
  `kind`, or split? Lean: one stream, `kind` field — simpler alignment.
- **Guard baseline:** the accepted event count / `|Δ|` / tie-margin thresholds for the
  permanent assertion — set from the post-fix ledger.
