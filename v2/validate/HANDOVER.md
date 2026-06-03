# V2 synth — C++ port fidelity: handover

Start-here note for picking this up in a fresh session.

## Goal & philosophy

Make the C++ V2 synth (`v2/synth_core.cpp`) reproduce the original assembly
(`v2/synth.asm`) bit-for-bit, validated by a 32-bit Linux harness in `v2/validate/`.

- **Default build = 100% faithful to the shipping ASM** (reproduces the demo/intro
  audio, bugs and all).
- **Genuine ASM bugs** are gated behind `BUG_V2_*` defines (default = bug on =
  ASM-faithful; set to 0 for the corrected behavior). Pattern established by ryg's
  existing `BUG_V2_FM_RANGE`.
- **C++ porting errors** (where the ASM was correct and the port got it wrong) are
  just fixed directly — no flag.
- `v2/synth.asm` is **never edited**. The harness assembles a generated copy.

## Branch / git state

- Branch: **`v2-port-fidelity`** (2 commits ahead of `master`, not pushed).
  - `5f604d6` validation harness + moog/distortion fixes
  - `2936134` tri/saw oscillator fix
- Working tree clean. Nothing committed to `master`.

## Build & test

Prereqs (32-bit toolchain): `nasm`, `gcc`/`g++` with `-m32` + 32-bit libstdc++
(Arch: `lib32-gcc-libs`; Debian/Ubuntu: `g++-multilib`).

```sh
cd v2/validate
./build.sh                 # builds everything; ASM original stays frozen
```

Per-block oracles (each renders identical input through ASM core vs C++ core and diffs):

```sh
./comp_osc        # oscillators (tri/saw, pulse, sin, noise)
./comp_flt        # filter: low/band/high/notch/all/moog-lo/moog-hi
./comp_leaves     # env, lfo, distortion, dc filter, bass boost
./comp_trisaw     # tri/saw startup sweep (notes x breakpoints) + per-case forensic
./comp_fastatan   # fastatan sweep vs ASM
```

Symmetric "fixed pair" (corrected C++ flag=0 vs a corrected ASM variant):

```sh
./comp_fastatan_fixed   # C++ BUG_V2_ATAN_TABLE=0 vs ASM cmovae
./comp_leaves_fixed
```

Whole-song A/B (the "combined output" test):

```sh
N=441000   # 10s @ 44.1kHz
./harness_asm ../v2m/pzero_new.v2m asm.f32 $N
./harness_cpp ../v2m/pzero_new.v2m cpp.f32 $N
python3 compare.py asm.f32 cpp.f32
# .f32 = raw interleaved stereo float32; convert/listen if desired.
```

## Current results

Every **isolatable DSP block bit-matches the ASM** (default build):

| block | status |
|-------|--------|
| oscillators (4) | MATCH (0/4) |
| filter (7 modes incl. moog) | MATCH (0/7) |
| env, lfo, dist (4 modes), dcf, boost | MATCH (0/leaves) |
| fastatan sweep | worst 0.0 |
| fixed pair (flag=0 C++ vs fixed ASM) | worst 0.0 |

Fixes applied (`v2/synth_core.cpp`):
- *porting errors, no flag:* moog ladder feedback sign; moog double dc-offset;
  dist bitcrusher round (lrintf vs truncate); fastatan shared-`cxm2` coeff; tri/saw
  hard cases in `double` (was float `sqr()`, lost precision in a cancellation
  amplified by 1/freq — only showed at sub-audio notes).
- *ASM bug, flagged:* `BUG_V2_ATAN_TABLE` — fastatan picks the wrong rational table
  for |x|>=2 (cmovge vs cmovae). Build also generates a fixed-ASM variant so the
  flag=0 build validates symmetrically.

## What's OPEN (next step for "combined output")

The whole-song A/B (`harness_asm` vs `harness_cpp`) **still diverges ~0.108** on
`pzero_new.v2m`. Cause: three channel/global FX blocks could NOT be isolated as
leaves because they read the global synth-instance buffers:
- **compressor** (`syCompRender` reads `SYN.vcebuf` lookahead)
- **reverb** (`syReverbProcess` reads `SYN.aux1buf`)
- **mod-delay** (channel-level, needs external delay buffers)

To localize/fix these, build a **full-instance fallback harness**: set up a minimal
one-voice patch (e.g. one oscillator, neutral elsewhere, then enable one FX at a
time) and drive it through the real `synthRender` seam, comparing whole-instance
output between cores. That isolates each FX in its real context without needing a
standalone workspace. Then fix each like the leaves above. Once these match, the
whole-song A/B should drop to float-noise.

## Backlog / ideas

- **tri/saw cancellation reformulation** (discussed): the box-filter "hard" cases
  have a catastrophic cancellation; the faithful build needs `double` to match the
  ASM's 80-bit. A *future* `BUG_V2_*` improvement flag could algebraically
  reformulate it cancellation-free so the *fixed* build is accurate in single
  precision on both C++ and ASM (the first flag whose fixed side is simpler/faster
  than faithful). Needs deriving the stable closed form.
- The `BUG_V2_*` defines use `#ifndef` guards so the build can override via
  `-DBUG_V2_ATAN_TABLE=0` without editing source.

## Reference docs

- `v2/validate/REPORT.md` — Phase 1 (whole-song divergence proof)
- `v2/validate/REPORT_LOCALIZE.md` — Phase 2 (per-block localization)
- `v2/validate/BUILD.md` — toolchain/build details
- `openspec/changes/archive/2026-06-03-*` — the staged methodology (validate →
  localize → fix-moog → fix-distortion), with proposals/designs/tasks
- `openspec/specs/v2-port-fidelity/spec.md` — the accumulating correctness spec
