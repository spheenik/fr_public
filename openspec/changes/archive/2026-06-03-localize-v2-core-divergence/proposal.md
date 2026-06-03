## Why

Phase 1 (`v2-core-validation`, archived) proved that `synth_core.cpp` diverges from `synth.asm` — but only at the *whole-song* level: max error ~0.1, structural (not a constant gain), ~99% of samples differ. A whole-song diff tells you **that** the port is wrong; it cannot tell you **where**, because every DSP block (oscillator, filter, envelope, LFO, distortion, delay, compressor, reverb, …) feeds the same final mix. To fix the port you must isolate the first block whose output differs.

The two cores are structured for exactly this: the asm exports each block (`_OSC_`, `_ENV_`, `_VCF_`, `_LFO_`, `_DIST_`, `_DCFILTER_`, `_BASS_`, `_MODDEL_`, `_COMPRESSOR_`, `_REVERB_`, plus the `_V2V_` voice and `_V2CHAN_` channel aggregates) with documented `syV*`/`syW*` parameter and working-state struct layouts, and `synth_core.cpp` mirrors each with a struct (`V2Osc`, `V2Env`, `V2Flt`, `V2LFO`, `V2Dist`, `V2DCFilter`, `V2Boost`, `V2ModDel`, `V2Comp`, `V2Reverb`, `V2Voice`, `V2Chan`). Per-component equivalence tests are therefore feasible — and are the only way to actually find the bug.

## What Changes

- Add a **per-component equivalence test harness**: for each DSP block, initialize identical parameter + working state on both cores, feed identical input (control values and/or an input sample buffer), run the same number of samples/ticks, and diff the two output buffers — reporting per-block max abs error and first divergence.
- Test the **leaf DSP blocks in isolation first** (oscillator, envelope, filter, LFO, distortion, DC filter, bass boost, mod-delay, compressor, reverb), then the **aggregates** (voice, channel) that compose them — so the first failing *leaf* is found before its failure is masked by downstream blocks.
- Drive the asm side by exposing each block's `sy*Init`/`sy*Render` routine from the (generated, RONAN-stripped) asm copy and calling it with the documented `syV*`/`syW*` struct ABI; drive the C++ side by calling the corresponding `V2*::init`/`render` method. The whole-song validation rig (`v2/validate/`) stays untouched as the regression baseline.
- Produce a **localization report**: an ordered pass/fail table across all blocks, the first diverging block, and — for that block — the minimal input that reproduces the difference, as the concrete fix target for the subsequent port work.

This change is **diagnosis only**. It does not modify `synth.asm` or `synth_core.cpp` behavior, and does not yet *fix* any divergence — it pinpoints the failing block(s) so a follow-on change can correct the port.

## Capabilities

### New Capabilities
- `v2-component-equivalence`: A harness and procedure that drives each V2 DSP block (and the voice/channel aggregates) in isolation on both the asm and C++ cores with identical state and input, and reports per-block whether the outputs match — localizing the first block where the port diverges.

### Modified Capabilities
<!-- None. v2-core-validation (whole-song A/B) is unchanged and reused as-is. -->

## Impact

- **New code**: per-block test harness + build glue, under `v2/validate/` (alongside the Phase 1 rig), reusing its toolchain (`nasm -f elf32`, `g++ -m32 -std=c++03`, `compat.h`) and conventions.
- **Generated asm copy**: the existing `synth_noronan.asm` generation step is extended to also expose the internal `sy*Init`/`sy*Render` block routines as globals (the original `synth.asm` is still not edited). This is the one new mechanism the design must pin down (exact routine names, register/`this`-pointer calling convention, struct sizes).
- **Reused as-is**: `v2/synth.asm` (block routines + documented `syV*`/`syW*` struct layouts), `v2/synth_core.cpp` (the `V2*` block structs/methods), the Phase 1 `compare`-style diff, and the 32-bit ILP32 build discipline (so struct layouts match).
- **Risk/unknown for design**: per-block ABI — whether each asm block routine can be invoked standalone with a hand-built `syW*` state, or whether some blocks are too entangled with the voice/channel context to isolate cleanly (those fall back to minimal single-block patch configurations through the existing seam).
- **Downstream**: the first diverging block becomes the precise target for the port-correctness change that follows; the test harness becomes a permanent per-block regression guard as the port is fixed.
