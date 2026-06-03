## Context

Phase 1 left a whole-song DIVERGE verdict and a working 32-bit Linux A/B rig (`v2/validate/`: NASM oracle, `g++ -m32 -std=c++03` C++ core, `compat.h`, symbol-decoration bridge). This change drills from "the song differs" down to "this block differs."

The two cores expose a near-perfect 1:1 block correspondence, confirmed by inspection:

| Block | asm routines | C++ (`synth_core.cpp`) | param struct | output |
|-------|--------------|------------------------|--------------|--------|
| OSC | syOscInit/Set/NoteOn/Render | `V2Osc::init/set/noteOn/render(sF32*,n)` | `syVOsc` | mono, accumulates |
| ENV | syEnvInit/Set/Tick | `V2Env::init/set/tick(gate)` | `syVEnv` | scalar/tick |
| VCF | syFltInit/Set/Render | `V2Flt::render(dst,src,n,step)` | `syVFlt` | mono |
| LFO | syLFO…/Set/Tick | `V2LFO::set/tick` | `syVLFO` | scalar/tick |
| DIST | syDistInit/Set/(render) | `V2Dist::set/render` | `syVDist` | buffer |
| DCF | syDCFInit/… | `V2DCFilter` | — | stereo |
| BASS | syBoostInit/Set/Render | `V2Boost::render(StereoSample*,n)` | `syVBoost` | stereo |
| MODDEL | …/Set | `V2ModDel::render` | `syVModDel` | stereo |
| COMP | syCompInit/Set/Render | `V2Comp::render(StereoSample*,n)` | `syVComp` | stereo |
| REVERB | …/Set | `V2Reverb::render(StereoSample*,n)` | `syVReverb` | stereo |
| VOICE | syV2Init/Set/Tick/Render | `V2Voice::render(StereoSample*,n)` | `syVV2` | stereo |
| CHAN | …/Set | `V2Chan` | `syVChan` | stereo |

Crucially the `syV*` **parameter structs are shared** — the C++ `set(const syVOsc*)` consumes the same bytes the asm `syOscSet` reads. That is the identical-input contract.

ABI of the asm block routines (confirmed on OSC/FLT): each is `pushad`/`popad`-wrapped (preserves all caller registers) and takes `ebp` = pointer to its `syW*` working-state struct, with buffer/length in registers (OSC: `edi`=dest, `ecx`=count; FLT: dst/src/count similarly). They are *internal* labels — not currently `global`.

## Goals / Non-Goals

**Goals:**
- For each DSP block, drive asm and C++ with identical param bytes + state + input and diff the output, isolating the first block that diverges.
- Test leaf blocks before aggregates; produce an ordered pass/fail table + minimal repro for the first failure.
- Reuse the Phase 1 toolchain unchanged; do not edit the original `synth.asm`/`synth_core.cpp`.

**Non-Goals:**
- Do NOT fix any divergence (that's the follow-on port-correctness change).
- No 64-bit, no audio backend, no ARM. Still x86-32 ILP32.
- Not a bit-exactness proof of the whole synth — just block localization sufficient to find the bug.

## Decisions

**D1 — Drive asm block routines via a thin asm shim, exposed as globals in the generated copy.**
Extend the existing `synth_noronan.asm` generation to append `global sy*Render`/`sy*Set`/`sy*Init`/`sy*Tick` (and the jump-table-internal entry points needed). Provide a tiny hand-written asm shim (or `__attribute__((stdcall))` + inline-asm trampoline) that loads `ebp`/`edi`/`ecx` from C arguments and `call`s the routine, since the routines use a register ABI, not cdecl/stdcall. *Why:* the routines are register-convention and `pushad`-safe, so a 3-instruction trampoline per call shape suffices. *Alternative rejected:* rewriting call sites — that would modify the synth.

**D2 — The `syV*` param struct is the single source of input truth.**
Build one `syV*` blob per test and feed the same bytes to both `sy*Set` (asm) and `V2*::set` (C++). *Why:* removes "configured differently" as a divergence cause; a difference is then provably in the block's processing. *Alternative rejected:* setting fields independently on each side — reintroduces input skew.

**D3 — Initialize sample-rate/global state on both cores before any block test.**
The C++ blocks take a `V2Instance*` for SR-derived tables; the asm reads global `SRf*` constants set during `synthInit`/`calcNewSampleRate`. The harness runs the global init once at 44100 Hz on each side (allocate a `V2Instance` / call the asm SR-setup) before driving blocks. *Why:* blocks read SR constants; uninitialized state would manufacture false divergence. This is the main entanglement risk (see Risks).

**D4 — Per-block driver pattern, three output shapes.**
Blocks fall into: (a) buffer renderers (OSC, VCF, DIST, BASS, MODDEL, COMP, REVERB, VOICE, CHAN) → run N samples, diff the float/stereo buffer; (b) tick scalars (ENV, LFO) → run N ticks, diff the per-tick scalar sequence; (c) state-only (DCF init). The harness has one driver per shape, parameterized per block. *Why:* matches the actual routine signatures rather than forcing a uniform interface.

**D5 — Deterministic, fixed test inputs.**
Each block gets a small fixed battery: representative param presets (e.g. each osc mode; filter types/cutoffs; env stages) and a deterministic input buffer (e.g. a known sweep / impulse / the upstream block's reference output). Noise-seeded blocks (osc noise mode, reverb) seed identically. *Why:* reproducibility and minimal repro extraction.

**D6 — Tolerance reused from Phase 1.**
`eps = 1e-4` as the float-noise threshold; report exact max error regardless. A block "matches" if within eps. *Why:* consistency with the Phase 1 verdict scale.

## Risks / Trade-offs

- **Block can't be isolated (needs live voice/channel context)** → Mitigation: for entangled blocks, fall back to a minimal single-block patch driven through the full `synthRender` seam (one voice, one osc, all else neutral), accepting weaker isolation. Flag such blocks in the report.
- **Register-ABI trampoline gets the convention wrong (ebp/edi/ecx, accumulate-vs-overwrite)** → Mitigation: validate the trampoline first on OSC against a known hand-traced output before trusting the suite; `pushad` means a wrong scratch reg won't corrupt the caller.
- **`syW*` working-state struct layout mismatch between asm and C++** is itself a possible bug source → that's a *finding*, not just a risk: if the C++ struct field order/size differs, the equivalence test surfaces it. Keep ILP32 so field sizes line up.
- **SR-global init differs subtly between cores** → could masquerade as every block diverging. Mitigation: first assert the SR constants themselves match between cores before running blocks.
- **Tick-based blocks (ENV/LFO) accumulate internal phase** → compare full tick sequences, not just an endpoint, so a phase/rate drift is caught early.

## Open Questions

- Exact internal entry points to export for jump-table routines (OSC mode dispatch via `oscjtab`) — does exporting `syOscRender` suffice, or are per-mode labels needed? (Resolve at first harness task.)
- Do any leaf blocks require an allocated delay/reverb buffer (`V2ModDel`/`V2Reverb` take `buf,len`) that must be sized/zeroed identically on both sides? (Likely yes — handle in the per-block driver.)
- Will the `V2Instance`-based C++ init pull in anything beyond SR constants (tables) that the asm sets differently? (Verify the SR-state assertion in D3.)
