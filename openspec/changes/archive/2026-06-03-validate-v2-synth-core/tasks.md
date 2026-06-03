## 1. Toolchain & feasibility

- [x] 1.1 Verify/install 32-bit toolchain: `nasm` (or `yasm`), `gcc-multilib`/`g++ -m32`, and 32-bit `libstdc++`; record exact prerequisites in a build note.
- [x] 1.2 Assemble `v2/synth.asm` with `nasm -f elf32` (RONAN disabled); fix only concrete assembler/dialect errors, change no synth logic. Output: `synth_asm.o`. **Assembles as-is, zero dialect fixes; RONAN-off object is fully self-contained.**
- [x] 1.3 Compile `v2/synth_core.cpp` with `g++ -m32 -c` (RONAN disabled, `DEBUGSCOPES 0`); fix only compile errors from MSVC-isms (`__int64`, `__stdcall`, `OutputDebugStringA`) via local shims — no behavior changes. Output: `synth_cpp.o`. **Needed `-std=c++03` (C++17 `std::lerp` clash) + `compat.h` shim; compiles clean.**
- [x] 1.4 Resolve open question: does instantiating/sizing a `V2Instance` need glue from `soundsys.cpp`? Document the minimal init sequence the harness must call (must NOT pull in DirectSound). **No glue needed — `V2MPlayer` embeds the synth (m_synth[3MB]) and drives the seam; harness = Init→Open→Play→Render. Asm build needs objcopy symbol rename.**

## 2. Render harness

- [x] 2.1 Write a host-independent harness that loads a `.v2m` file into memory and drives it through `V2MPlayer` (`v2/v2mplayer.cpp`) at a fixed sample rate (44100) for a fixed sample count. **harness.cpp: Init→Open→Play→Render, blockwise.**
- [x] 2.2 Render via the `synth.h` seam (`synthInit`/`synthRender`/...) into a stereo 32-bit float buffer and dump it to a raw `.f32` file (no WAV encoding). **Renders interleaved stereo f32 to raw .f32 dump.**
- [x] 2.3 Build two link targets from the one harness source: `harness_asm` (links `synth_asm.o`) and `harness_cpp` (links `synth_cpp.o`); confirm both are deterministic across repeated runs. **build.sh: two link targets; both built & verified bit-identical across repeated runs.**
- [x] 2.4 If a chosen baseline `.v2m` fails to load, run it through `v2mconv` first and use the upgraded file (do not modify `v2mconv`). **Not needed — baseline songs load directly (Open succeeds, real signal).**

## 3. Comparison & report

- [x] 3.1 Write a diff step that loads two `.f32` dumps and reports sample count, max absolute error, and the index of the first sample exceeding tolerance. **compare.py: count, peak (silence guard), max/rms err, first-divergence index.**
- [x] 3.2 Define the "match" epsilon empirically from the first clean run; document it in the report rather than guessing up front. **eps=1e-4 as float-noise threshold; actual error ~0.1 is ~1000x above it — verdict robust.**
- [x] 3.3 Run the A/B over the baseline non-speech songs (`v2/v2m/pzero_new.v2m`, `v2/v2m/v2_zeitmaschine_new.v2m`) and record a per-song correctness result. **pzero: maxerr 0.109; zeitmaschine: maxerr 0.137. Both DIVERGE, both non-silent.**

## 4. Localize divergence (only if cores differ) — DEFERRED TO PHASE 2 (user decision)

> Phase 1 answered the validation question (DIVERGE, structural). Block-level
> localization is consciously deferred to Phase 2: the intended tool (`scope.h`)
> is Windows+OpenGL only and single-sided, so it needs new headless dual-core
> instrumentation — Phase-2-scale work, and the natural first Phase 2 task.

- [~] 4.1 ~~enable `DEBUGSCOPES` / use `scope.h` to bisect which DSP block first splits~~ — DEFERRED to Phase 2 (becomes its first task). See REPORT.md "Consequence for Phase 2".
- [~] 4.2 ~~Record the first diverging DSP block per failing song~~ — DEFERRED to Phase 2.

## 5. Validation report

- [x] 5.1 Write the validation report: per-song max abs error, first-divergence sample/block (if any), toolchain prerequisites, and the exact reproduce commands. **`validate/REPORT.md`.**
- [x] 5.2 State the verdict and its consequence for Phase 2: match ⇒ "finish the port" (audio backend + LP64 fix + drop asm); divergence ⇒ "debug from block X" with the located entry points. **Verdict: DIVERGE (structural, not gain). Phase 2 = debug branch; report lists concrete next steps.**
