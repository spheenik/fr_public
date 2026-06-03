## Context

`v2/` contains two implementations of the V2 synth core that are meant to produce identical audio:

- `synth.asm` — original hand-written x86 (NASM dialect, 32-bit), the shipping truth. Self-contained per kb's README (the core has no `win32.inc` include; `%define RONAN/VUMETER/FULLMIDI` at the top).
- `synth_core.cpp` — ryg's C++ port, begun 2012-06-30, last touched 2012-11-27. Commit `49549db` (2012-07-22) states the C++ version "still doesn't match ASM." Line 588 carries a comment mapping the C++ bit layout to the ASM version — evidence the port was written to be bit-comparable.

Both expose the same seam: `extern "C" __stdcall synthInit/synthRender/synthProcessMIDI/...` declared in `synth.h`. The `.v2m` song format is driven by `V2MPlayer` in `v2mplayer.cpp`, which calls that seam. The player contains 3 inline `__asm` blocks doing 64-bit mul/div — these assemble under a 32-bit x86 build.

The decisive risk this phase removes: nobody knows if `synth_core.cpp` is correct. Everything downstream (Linux port, 64-bit, plugin) is wasted effort if it silently diverges.

## Goals / Non-Goals

**Goals:**
- Render the same `.v2m` through both cores on Linux and produce a single correctness number (max abs error) plus first-divergence location if they differ.
- Make the assembly core the oracle, built natively on Linux (`nasm -f elf32`) — no Windows/Wine.
- Keep the comparison deterministic and reproducible across runs.
- If divergence exists, localize it to the first failing DSP block.

**Non-Goals:**
- No modification of `synth.asm` or `synth_core.cpp` behavior.
- No removal of the assembly; no `types.h` LP64 fix; no 64-bit build.
- No audio output backend (ALSA/Pipewire), no WAV playback UX beyond what the diff needs.
- No ARM / non-x86 support (the inline asm in `v2mplayer.cpp` is x86-locked this phase).
- No plugin (CLAP/LV2/VST) work.

## Decisions

**D1 — The assembly core is the oracle, not a third-party render.**
Build `synth.asm` with `nasm -f elf32` and link it as the reference. *Why:* it is the literal shipping implementation; any external player (JS ports, Wine-run binaries) introduces its own correctness question. *Alternatives rejected:* (a) render reference WAVs under Wine — adds a Win32 + DirectSound dependency and trust gap; (b) trust a community port — circular, that's also unvalidated against this asm.

**D2 — Build both cores 32-bit (`-m32` / `-f elf32`).**
*Why:* `types.h` defines `sU32 = unsigned long`, which is 8 bytes on LP64 Linux — confirmed. At ILP32 it is 4 bytes, so asm and C++ agree on every struct/patch-data layout for free, with zero edits to shared headers. *Trade-off:* requires `gcc-multilib`/32-bit libstdc++; result is x86-32 only. Acceptable — this is a validation rig, not the shipping port. The LP64 fix is explicitly Phase 2.

**D3 — Core selection is a build-time switch, harness code is shared.**
One harness source, two link targets: `harness+synth.o(asm)` and `harness+synth_core.o(cpp)`. *Why:* guarantees identical sequencer, song loading, sample count, and sample rate across both runs — the only variable is the core. *Alternative rejected:* one binary linking both cores under renamed symbols — both define the same `extern "C"` names, forcing symbol surgery and risking subtle divergence in the harness path.

**D4 — Compare raw float buffers, not WAV files.**
The harness renders to a `float` buffer and writes it (e.g. raw `.f32` dump) per core; a small diff step loads both and reports max abs error + first divergence index. *Why:* avoids quantization/encoding noise masking real differences. Tolerance is near-zero for a true match; a documented small epsilon accounts for any legitimate float-eval-order differences, with any larger gap treated as real divergence.

**D5 — RONAN off for the baseline; identical on both sides always.**
*Why:* the speech synth is a separate concern; disabling it on both cores keeps the first result clean and removes `ronan.cpp`/`phonemtab.h` from the critical path. Baseline songs (`v2m/pzero_new.v2m`, `v2m/v2_zeitmaschine_new.v2m`) are fr-authored "new format" and least likely to need a `v2mconv` upgrade pass. RONAN-on comparison is a later, optional run once the no-speech path matches.

**D6 — Localize divergence with the existing `scope.h` machinery.**
`synth_core.cpp` already has `DEBUGSCOPES`/`COVERAGE` hooks built for exactly this A/B work. If buffers diverge, enable scopes to bisect which DSP block (osc/env/filter/dist/delay/...) splits first. *Why:* reuse the author's intended debug path instead of inventing one.

## Risks / Trade-offs

- **`synth.asm` won't assemble cleanly on NASM/YASM (macro/dialect drift)** → Mitigation: it's already NASM-syntax (`%define`, `section .bss`, `resd`, `global`); fix only concrete assembler errors, change no logic. If a Win32-specific stub is referenced, stub it locally in the harness, not in the asm.
- **`.v2m` needs format upgrade before the current synth accepts it** → Mitigation: prefer the in-tree "new" songs; if a song fails to load, run it through `v2mconv` first (in scope as a pre-step, out of scope to modify).
- **32-bit toolchain not installed** → Mitigation: documented prerequisite (`nasm`/`yasm`, `gcc-multilib`, 32-bit libstdc++); the harness is the only thing needing it this phase.
- **Float eval-order differences cause tiny nonzero error even when "correct"** → Mitigation: report the actual max abs error and judge against a stated epsilon; "match" is a claim about magnitude, not bit-identity, and the number is recorded either way.
- **The C++ core needs glue from `soundsys.cpp` to instantiate `V2Instance`** (open) → Mitigation: confirm during task 1; if so, lift only the minimal init into the harness, do not pull in DirectSound.

## Open Questions

- ~~Is `synth_core.cpp` self-sufficient as a render target via the `synth.h` seam, or does instantiating/sizing a `V2Instance` require helper code from `soundsys.cpp`?~~ **RESOLVED (task 1.4):** No `soundsys.cpp` glue needed. `V2MPlayer` (v2mplayer.h:233) embeds the synth as a fixed `sU8 m_synth[3*1024*1024]` buffer and calls `synthInit/synthRender/synthProcessMIDI` internally. The harness only does `Init → Open(v2m, 44100) → Play(0) → Render(buf, N)`. `soundsys.cpp` is purely the DirectSound output thread and is not linked. Sanity check at runtime: both cores' instance size (`synthGetSize()`) must be ≤ 3 MB.
- **Symbol-decoration bridge (discovered while resolving 1.4):** `v2mplayer.cpp` references the *undecorated* `synth*` names (via `synth.h`), which the cpp core provides directly. The asm core exports MSVC-decorated `_synth*@N`, so the asm build must rename them to undecorated (`objcopy --redefine-syms`) before linking. stdcall convention matches on both sides (asm `ret N` ↔ gcc `__attribute__((stdcall))`).
- Does the current synth load the chosen baseline `.v2m` directly, or is a `v2mconv` upgrade pass required first? (Resolve at task 2.3/2.4 — first actual run.)
- ~~What epsilon counts as "match"?~~ **RESOLVED (task 3.2):** moot — the measured error (~0.1, ~25–40% of peak) is ~1000× any plausible float-noise epsilon (`1e-4`). The DIVERGE verdict is robust to epsilon choice.

## Outcome (Phase 1 complete)

**Verdict: DIVERGE.** `synth_core.cpp` does not match `synth.asm` (see `v2/validate/REPORT.md`). Systematic across both baseline songs; both deterministic and non-silent; not a constant-gain difference (structural). Group 4 (block-level localization) is rescoped to Phase 2 because the intended tool (`scope.h`) is Windows-only and single-sided — it requires building new headless dual-core instrumentation.
