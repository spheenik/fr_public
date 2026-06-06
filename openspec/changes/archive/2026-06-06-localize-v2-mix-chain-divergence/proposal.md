## Why

The archived `fix-v2-freq-precision` change made the oscillator frequency path and
all transcendentals bit-faithful to the asm, yet the whole-song A/B on
`pzero_new.v2m` did **not** move (`harness_asm` vs `harness_cpp`:
0.0876300689 → 0.0876298994 — only the 7th significant figure). Re-running the
bus-tap rig on the faithful build re-localized the dominant source:

    aux1 = 0.00267   aux2 = 0.00258   mix = 0.08116

The ~0.081 is introduced in the **global mix chain** that runs on `mixbuf` after
all per-channel processing (`V2Synth::renderFrame`, `synth_core.cpp:3342-3370`):

    reverb.render → delay.renderAux2Main → dcf.renderStereo
      → inline low-cut/high-cut → compr.render

The channel sends (aux1/aux2) and the voice/osc/transcendental path are
**exonerated** — they diverge two orders of magnitude less than the mix. A
negative result confirms the residual is **structural**, not a precision gap:
converting every transcendental in the synth to the faithful x87 kernel left the
magnitude untouched.

The existing bus-tap only snapshots the **final** `mixbuf` (after all five global
stages), so it cannot say *which* stage injects the 0.081. This change adds a
per-stage tap so we can bisect the chain, identify the first diverging global
effect, and fix that stage's port to match the asm — the next concrete step toward
driving the bit-faithful whole-song A/B to zero.

## What Changes

- **Per-stage mix tap (validation-only).** Extend the validation bus-tap rig
  (`synthDebugGetBus`, mirrored in `asm_appendix.asm`) so both cores snapshot
  `mixbuf` after **each** global stage (post-reverb, post-delay, post-dcf,
  post-lowcut/highcut, post-compressor). The player dumps the per-stage streams
  per frame, exactly like the existing aux1/aux2/mix dump, so the streams are
  call-for-call comparable between the asm and C++ cores.
- **Localize.** Diff each tap point (asm vs C++) to find the first global stage
  whose output diverges — that stage owns the 0.081.
- **Fix the responsible stage.** Read that stage's asm (`syReverb`, `syModDel`,
  `syDCFilter`, the global low-cut/high-cut, or `syCompProcChannel`) against its
  C++ port and correct the port bug (or gate a genuine asm bug behind a
  `BUG_V2_*` define, per the port-fidelity philosophy).
- **Re-verify.** Confirm the whole-song A/B magnitude drops for the first time,
  and that no per-block oracle (`comp_*`) regresses.

This is primarily a **localization + targeted-fix** change. The exact stage and
fix are unknown until the tap is built and run; the design and tasks structure the
work as a localization phase that gates a fix phase. The portable default build is
not touched; the per-stage tap is `V2_VALIDATE`-gated like the existing bus tap.
`synth.asm` is never edited (the harness taps a generated copy).

### SCOPE RE-POINT (2026-06-03): precision axis exhausted → STRUCTURAL voice event

A follow-up investigation **closed the entire floating-point-precision axis** as the
source of the ~0.084 residual. Four independent precision interventions leave the
whole-song magnitude unmoved: x87 PC=24 faithful = 0.0837997;
`-fexcess-precision=standard` = byte-identical (codegen + binary + audio);
`-mpc32 -mno-sse` = onset-shift-only; **full SSE math** (`-msse2 -mfpmath=sse`,
`FLT_EVAL_METHOD=0`) = 0.0838358. The GCC float-literal→80-bit promotion bug is
**real but context-dependent** (non-exactly-representable literals in live
subexpressions — `fci12`, the moog `0.8f`/`5.6f`); the only cure that works is
`-mfpmath=sse -msse2` (NOT `-fexcess-precision=standard`, which is inert on x87 in
both C and C++ here, at every `-std`). Yet fixing the promotion **globally** via SSE
moved the residual by 0.00004 — **the constant bug is real but is NOT the residual.**

Decisive tell: the first divergence crosses threshold at a **fixed sample**
(~stereo 4401, ~0.1 s) **regardless of FP mode** — the signature of a control-flow /
logic difference, not ULP drift. This change is therefore re-pointed: the work is no
longer "fix a precision gap" but **localize the structural voice event at ~sample
4401 and correct the port logic** (§8 voice tap → §9 drill+fix), comparing *logic*,
not bits. The mix-chain tap and per-stage results stay as committed infra and
exoneration evidence.

### SCOPE EXTENSION (after the mix-chain result)

The per-stage mix tap was built and run: the **global mix chain is faithful** —
reverb/delay/dcf/lc-hc are passthrough (Δ ≤ 0.001) and the sum compressor was
read line-for-line vs `syCompProcChannel` (identical math; it merely amplifies
~1.8× as a stateful gain integrator fed an already-diverged signal). The dominant
~0.044 **enters the chain pre-reverb** as the dry voice/channel sum. Rather than
spin off a separate change per layer, this change is **extended one level deeper**:
add the same tap technique inside `V2Voice::render`/`syV2Render` (post-osc,
post-filter, post-dist, post-dcf) to localize and then **fix** the voice sub-block
that introduces the divergence. The mix-chain tap stays as committed infra.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `v2-port-fidelity`: adds a global-mix-chain fidelity requirement (each global FX
  stage on `mixbuf` matches the asm reference) to the existing capability.

## Impact

- **Code changed:** `v2/synth_core.cpp` (per-stage `mixbuf` snapshot + accessor,
  `V2_VALIDATE`-gated; plus the eventual fix to whichever global stage is found
  responsible); `v2/validate/asm_appendix.asm` (mirror the accessor for the asm
  core); `v2/validate/` player/harness (dump the per-stage streams).
- **Verified by:** the per-stage mix tap (new), the whole-song A/B
  (`harness_asm` vs `harness_cpp`) on `pzero_new.v2m`, and the existing component
  oracles (`comp_osc`/`comp_flt`/`comp_leaves`) for no regression.
- **Goal:** identify the global stage owning the ~0.081 residual and reduce the
  bit-faithful whole-song A/B magnitude below 0.0876 for the first time.
