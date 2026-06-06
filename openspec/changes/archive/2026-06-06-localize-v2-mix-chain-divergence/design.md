# Design: localize the global-mix-chain divergence

## Context

After the freq path was made bit-faithful, the whole-song A/B residual (~0.0876 on
`pzero_new.v2m`) stayed put. The bus-tap rig (committed in `81754c4`) exposes the
live aux1/aux2/`mixbuf` buffers via `synthDebugGetBus` (C++ under `V2_VALIDATE`;
mirrored for the asm core in `v2/validate/asm_appendix.asm`, auto-renamed by the
redef step). The player dumps them per-frame when `BUSTAP=<prefix>` is set, after
every `synthRender`, so both cores produce call-for-call comparable streams.

That tap reads aux1≈0.0027, aux2≈0.0026, **mix≈0.081** — the divergence is born in
the global mix chain, which runs once per frame on `mixbuf` after all channels are
summed (`V2Synth::renderFrame`, `synth_core.cpp:3342-3370`):

```
StereoSample *mix = instance.mixbuf;
reverb.render(mix, nsamples);          // 1. global reverb            (syReverb)
delay.renderAux2Main(mix, nsamples);   // 2. mod-delay aux2 -> main   (syModDel)
dcf.renderStereo(mix, mix, nsamples);  // 3. global DC filter         (syDCFilter)
... inline low-cut / high-cut loop ... // 4. lcbuf/hcbuf one-poles    (inline)
compr.render(mix, nsamples);           // 5. sum compressor           (syCompProcChannel)
```

The current tap snapshots only the *final* `mixbuf` (post-stage-5), so it cannot
attribute the 0.081 to a stage. We need intermediate snapshots.

## Goals / Non-Goals

**Goals:**
- Snapshot `mixbuf` after each of the five global stages in **both** cores, dumped
  per-frame and call-for-call comparable (same mechanism as the existing bus tap).
- Bisect: identify the **first** global stage whose asm-vs-C++ output diverges.
- Fix that stage's port to match the asm (or gate a genuine asm bug behind
  `BUG_V2_*`), and confirm the whole-song A/B magnitude drops for the first time.

**Non-Goals:**
- Touching the portable default build's DSP (the tap is `V2_VALIDATE`-only).
- Editing `synth.asm` (the harness taps a generated copy; asm-side snapshots live
  in `asm_appendix.asm`).
- Pre-committing to which stage is wrong or what the fix is — that is the output
  of the localization phase, not an input.

## Decisions

### Per-stage snapshot buffers, mirrored in both cores

Add (under `V2_VALIDATE`) five `StereoSample` scratch buffers — one per stage —
that `renderFrame` fills by `memcpy`ing `mixbuf` immediately after each global
stage returns. Expose them through a new accessor alongside `synthDebugGetBus`,
e.g.:

```cpp
#ifdef V2_VALIDATE
extern "C" void __stdcall synthDebugGetMixTap(void *pthis,
    float **postReverb, float **postDelay, float **postDcf,
    float **postLcHc, float **postCompr, int *framesize);
#endif
```

Mirror the same five snapshots and accessor in `v2/validate/asm_appendix.asm` for
the asm core, at the equivalent points in the asm's render frame (the redef step
auto-renames so both link). The player dumps each stream to
`<BUSTAP_PREFIX>.post_<stage>` per frame, exactly like aux1/aux2/mix today.

Rationale: copying *after* each stage (rather than tapping inside each FX object)
keeps the DSP path byte-identical and reuses the proven per-frame dump cadence, so
the only new surface is read-only snapshots.

### Bisect rule

Diff the asm vs C++ stream at each tap point in order (post-reverb first). The
first tap whose max-abs diff jumps to the ~0.08 magnitude is the culprit stage;
taps before it should read the small ~0.003 carried in from the channel sends.
Because each stage's input is the previous stage's (already-diverged) output,
attribute the stage that **introduces** the jump, not merely one that carries it.

### Fix phase (scoped after localization)

Once the stage is known, read its asm vs C++ port (`syReverb` / `syModDel` /
`syDCFilter` / the inline lc/hc / `syCompProcChannel`) and apply the
port-fidelity rule from `[[v2-port-fidelity-philosophy]]`: a **C++ porting error**
(asm correct, port wrong) is fixed directly; a **genuine asm bug** is gated behind
a `BUG_V2_*` define (default = asm-faithful). Re-run the whole-song A/B and the
`comp_*` oracles to confirm the magnitude drops with no regression.

## Risks / Trade-offs

- **Non-monotonic downstream perturbation.** The handover warns that neutering a
  block perturbs downstream divergence non-monotonically. We do **not** neuter
  anything here — snapshots are read-only copies — so the bisect stays reliable.
- **Stage carries vs introduces.** A later stage (e.g. the compressor) can amplify
  an upstream diff; the bisect rule (attribute the first *jump*, inspect inputs)
  guards against blaming the amplifier instead of the source.
- **asm snapshot placement.** The asm render frame must be tapped at the exact
  equivalent points; an off-by-one stage boundary would mis-attribute. Mitigate by
  verifying the post-compressor asm snapshot equals the existing final-`mixbuf`
  bus tap (they must be identical).
- **Multiple culprits.** More than one stage may diverge; the change handles them
  iteratively (localize → fix → re-tap) until the magnitude is driven down.
