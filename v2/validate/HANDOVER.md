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

- Branch: **`v2-port-fidelity`** (5 commits ahead of `master`, not pushed).
  - `5f604d6` validation harness + moog/distortion fixes
  - `2936134` tri/saw oscillator fix
  - `5468351` this handover note
  - `c5d24c6` init zeroing fix (sizeof(this) -> sizeof(*this)); poison diagnostic
  - `81754c4` bus-tap rig + compressor lookahead off-by-one fix
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
- *porting errors, no flag (this session):*
  - **synth init zeroing**: `memset(this,0,sizeof(this))` cleared only 4 bytes
    (a pointer); ASM `_synthInit` zeroes the whole instance (`rep stosb SYN.size`).
    Fixed to `sizeof(*this)`. Latent in the harness (m_synth is BSS), so it did
    NOT change the whole-song result — proven with the POISON diagnostic.
  - **compressor lookahead off-by-one**: ASM `syCompProcChannel` wraps the
    lookahead ring with `inc/cmp dblen/jbe` (ring length dblen+1); port used
    `>= dblen` (length dblen). Fixed to `> dblen`. pzero 0.108534 -> 0.087630.
- *ASM bug, flagged:* `BUG_V2_ATAN_TABLE` — fastatan picks the wrong rational table
  for |x|>=2 (cmovge vs cmovae). Build also generates a fixed-ASM variant so the
  flag=0 build validates symmetrically.

## What's OPEN (next step for "combined output")

The whole-song A/B (`harness_asm` vs `harness_cpp`) diverges **~0.0876** on
`pzero_new.v2m` (was 0.108534 — see progress below).

### Bus-tap rig (built — use this to localize)

`synthDebugGetBus()` (in both cores: C++ under `-DV2_VALIDATE`, ASM in
`asm_appendix.asm`, auto-renamed by the redef step) returns the live per-frame
bus buffers (aux1/aux2 channel sends, mix). The player dumps them when
`BUSTAP=<prefix>` is set, after every `synthRender` call — both cores get the
identical chunk sequence, so the streams are call-for-call comparable.

```sh
BUSTAP=/tmp/a ./harness_asm ../v2m/pzero_new.v2m /tmp/a.f32 441000
BUSTAP=/tmp/c ./harness_cpp ../v2m/pzero_new.v2m /tmp/c.f32 441000
for s in aux1 aux2 mix; do python3 compare.py /tmp/a.$s /tmp/c.$s; done
```

Also: `POISON=1` fills the instance with 0xCC before synthInit (proves the
init-zeroing contract — ASM unaffected, a non-zeroing C++ core would diverge).

### Where the residual is (and isn't)

Bus-tap result: divergence ORIGINATES in the channel sends (aux1/aux2 diverge
at **frame 1**; mix only at frame 7), in a **stateful** block (frame 0 matches).
The per-channel chain blocks have ALL been verified against the ASM by reading:
- **comp** (PEAK/stereo, what pzero channels use): LD peak/RMS, gain smoothing,
  lookahead, init/reset, mode calc — all match (after the off-by-one fix below).
- **chorus / mod-delay**: bit-faithful (an asm-exact reformulation produced
  identical output; the original lerp form already matches).
- **dist renderStereo** (filter modes 5/6/7, which pzero uses): `syDistSet.modeF`
  and `syDistRenderStereo.modeF` both match the C++ (cutoff=param1, reso=param2,
  mode=dist_mode-4; stride-8 stereo filter render via the validated syFltRender).
- dcf renderStereo ≈ mono path; boost is leaf-tested (stereo).

So the channel chain checks out yet aux still diverges → the residual is most
likely **upstream in the VOICE output** (the frame-boundary tap can't separate
voice from channel) or a voice/integration path the leaf tests miss:
voice dist filter/decimator `renderMono` (test_dist only covered modes 0-3),
FM/sync osc combos, or the voice-assembly routing.

### NEXT TOOL: intra-frame voice-vs-channel tap

The frame-boundary tap can't see voice output (chanbuf is overwritten per
channel within a frame). Add a tap INSIDE the channel loop — after the voice
render loop, before `chansw.process()` — in BOTH cores. C++ side is trivial;
the ASM side needs a hook in the asm renderFrame (the hard part — the channel
loop is inline, so the appendix can't reach it; may need a sed-injected `call`
to a dumper, or restrict to frames where a single channel is active so the
boundary chanbuf == that channel). If voice output matches but aux diverges,
the bug is in the channel send/receive/routing; if voice diverges, drill into
the voice (osc/flt/env/lfo all leaf-MATCH, so suspect dist filter modes, FM,
sync, or voice assembly).

NOTE: "neuter a block in both cores and re-bus-tap" is UNRELIABLE — removing a
correct block changes the signal and perturbs downstream divergence
non-monotonically (neutering dist made it *worse*). Prefer reading or per-mode
leaf isolation.

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
