# Proposal: portable-version-native-player

## Why

The faithful V2 port is now bit-exact against both known oracles (the 2004
`synth.asm` for modern files and the genuine year-2000 fr08 binary for v0
files), but it is unusable outside the validation lab: it requires 32-bit
x86, x87 inline assembly, `-mpc32`, and an external conversion step
(`conv_v2m`) plus an environment variable (`V2_SRCVER`) to play period
files with period semantics. There is no player a normal program on a
normal machine can embed. Now that the engine semantics of both endpoint
eras are fully characterized (DELTA.md), we can build one portable,
self-contained player that plays any-version v2m files natively and
deterministically.

## What Changes

- **New self-contained portable player** under `v2/portable/`: plain C++,
  no inline asm, no x87/SSE assumptions, 32/64-bit clean, builds on any
  platform. The existing `v2/` sources, `validate/` harness, and oracles
  are untouched (they remain the reference lab).
- **Version-native playback, no external conversion**: the player detects
  the v2m format version (0–6) by structural fingerprint, parses that
  version's layout, canonicalizes to one internal patch representation at
  load time, and retains the source version to drive engine behavior.
- **Era behavior switching as data**: every known engine-behavior delta
  between the 2000 and 2004 synths (~15 deltas: env curves/frame size, osc
  algorithm, noise LCG, sin/atan, dcoffset, crusher gain1 fold, PGM-change
  semantics, DCFs, comp/boost/aux, moog modes, …) gets one row in a
  visible threshold table `{delta, flips_at_version, evidence}` with
  evidence levels: **proven** (binary-verified: v0, v6), **anchored**
  (parameter tables imply it), **assumed** (documented guess for v1–v5).
  Runtime overrides (force behavior version, per-delta override) serve as
  the research instrument for future evidence.
- **Cross-host determinism**: strict float32 arithmetic (matching the
  original x87 PC=24 rounding op-for-op) plus project-owned fixed
  transcendental implementations (exp2/sin/atan; no libm in the audio
  path) → identical output bits on every platform. The delta vs. the
  historical hardware renders becomes one fixed, measured ε per oracle,
  published in the docs.
- **Compile-time version subsetting**: `V2_VER_MIN`/`V2_VER_MAX` build
  configuration; gates constant-fold so a single-version build contains
  only that version's code. Ronan speech synthesis included behind an
  independent compile flag (josie/kkrieger use it).
- **New public API** (small, embeddable): open(data) → auto-detect /
  report version, play, pull-render float32 stereo, seed control
  (deterministic default 0 replacing the historical rdtsc seeds).

- **v5/Ronan oracle from the candytron binary** (scope added 2026-06-05):
  the fr-030 candytron final executable (user-supplied) carries the
  compiled period synth+ronan that josie (format v5) was authored for.
  Unpack it, extract+cross-check the embedded josie v2m, build a C2
  harness (fr08 C1 playbook) rendering josie *with speech* as ground
  truth, assay the binary against every ASSUMED era-table row (a third
  proven anchor at v5), and align the portable's v5 path + Ronan port
  against that oracle.

Explicit non-goals (follow-up work, separate changes): hunting the
*remaining* period binaries (fr-013/019/022/025, kkrieger betas) to pin
the rest of the v1–v5 behavior thresholds and extract a mid-era corpus
(candytron is now in scope, see above); bit-exactness to the historical
x87 renders (structural fidelity with published ε instead); replacing
the validation lab.

## Capabilities

### New Capabilities
- `v2m-version-native-loading`: detect a v2m file's format version by
  structural fingerprint, parse any supported version's layout natively,
  and canonicalize value-preservingly to the internal patch
  representation — no external conversion tool.
- `era-gated-engine`: a single engine whose behavior deltas switch on the
  source version via a data table with per-delta thresholds and evidence
  levels; includes runtime override knobs for research.
- `portable-determinism`: strict float32 arithmetic policy and owned
  transcendentals yielding bit-identical renders across hosts/platforms,
  with a measured, documented ε against the v0 and v6 oracles.
- `version-build-subsetting`: compile-time selection of the supported
  version range (and Ronan on/off) such that unsupported versions' code
  is eliminated from the binary.
- `portable-player-api`: the embeddable public interface (open/version
  query/play/render/seed) for the portable player.

### Modified Capabilities
<!-- none: the existing faithful port, validation harness, and oracles are
     unchanged; this change adds a new component beside them -->

## Impact

- **New code**: `v2/portable/` (engine, loader, era table, sequencer,
  Ronan port, public header). No modifications to existing `v2/` sources
  or `validate/`.
- **Build**: a new standalone build (CMake or a plain makefile/script)
  independent of the 32-bit validation build; documented build-flag
  policy (no `-ffast-math`, `-ffp-contract=off`, denormals must remain
  enabled for v0 semantics).
- **Validation dependency (read-only)**: renders are compared against the
  existing oracles — `harness_asm` output for v6 semantics and
  `/tmp/fr08/c1_fr08.f32` (re-derivable via `c1_fr08_harness`) for v0 —
  to measure and publish ε. The 17-file corpus (`v2m/`, `v2m/converted/`)
  is the test set; it is bimodal (v0 + v6), so v1–v5 paths ship
  structurally supported but empirically unverified.
- **Interface consumers**: none yet (new API); `tinyplayer`/`in_v2m`
  remain on the old path until a separate adoption change.
