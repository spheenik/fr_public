## Why

The V2 synthesizer ships two implementations of its DSP core: the original hand-written x86 assembly (`v2/synth.asm`, the shipping truth) and a C++ port (`v2/synth_core.cpp`) that Fabian "ryg" Giesen started in mid-2012 and abandoned in November 2012 — with a commit log that literally says *"C++ ver still doesn't match ASM"*. Any Linux port, maintainability rewrite, or modern plugin build depends on that C++ core being correct, but nobody knows whether it actually matches the assembly today. This change answers one question with a number before any porting work begins: **does `synth_core.cpp` produce bit-comparable output to `synth.asm`?**

## What Changes

- Add a host-independent **A/B render harness** that loads a `.v2m` song, drives it through `V2MPlayer`, and renders a fixed number of audio frames to a raw float buffer — with the synth core selectable at build time (asm core vs C++ core).
- Build the **assembly core as a Linux oracle**: assemble `synth.asm` with `nasm -f elf32` and link it into the harness as the reference implementation (no Windows, no Wine).
- Build the **C++ core under test** (`synth_core.cpp`) with `g++ -m32`, so both cores agree on struct/word sizes (sidestepping the LP64 `sU32 = unsigned long` size mismatch that only bites at 64-bit).
- Add a **diff/report step** that compares the two render buffers sample-by-sample and emits a single correctness metric (max absolute error, and first divergence sample/block).
- Establish RONAN handling so the A/B is apples-to-apples (speech synth compiled identically on both sides, or disabled identically), using non-speech `.v2m` songs for the baseline run.
- Produce a **validation report**: the correctness number per test song, and — if divergence is found — the first DSP block where the two cores split, using the existing `scope.h` debug-scope machinery.

This change is **validation only**. It does not modify either synth core, does not remove the assembly, does not fix `types.h`, and does not add an audio backend. Those belong to the follow-on "port" phase, which this change exists to de-risk.

## Capabilities

### New Capabilities
- `v2-core-validation`: A reproducible, Linux-buildable harness and procedure that renders identical input through both the assembly and C++ V2 synth cores and reports whether their audio output matches, including where they first diverge.

### Modified Capabilities
<!-- None. No existing spec requirements change; the two synth cores are not modified by this change. -->

## Impact

- **New code** (validation harness + build glue), kept isolated under the change's own tooling — does not touch `synth.asm` or `synth_core.cpp` behavior.
- **Reused**: `v2/synth.h`/`v2/libv2.h` (the `extern "C" __stdcall` seam both cores share), `v2/types.h`. **Correction (found during apply):** `v2/v2mplayer.cpp`'s 4 inline `__asm` blocks are **MSVC** syntax (member-name operands, `rep stosd`) which GCC cannot compile — they do NOT "assemble fine under -m32". They are ported to behaviour-identical portable C++ in a validate-local copy (`validate/v2mplayer_port.cpp`); the original is left untouched. This is safe for validation: the player is shared identically by both core builds, so it cancels out of the A/B — the cores under test are unmodified.
- **Toolchain dependency**: requires a 32-bit build environment on Linux — `nasm`/`yasm` and `gcc-multilib`/`g++ -m32` (plus 32-bit libstdc++). x86-only for this phase, by design.
- **Test data**: existing in-tree `.v2m` songs (e.g. `v2/v2m/pzero_new.v2m`, `v2/v2m/v2_zeitmaschine_new.v2m`).
- **Downstream**: the resulting correctness number decides the scope of the subsequent Linux port (finish vs. debug), and the divergence location, if any, becomes the first work item there.
