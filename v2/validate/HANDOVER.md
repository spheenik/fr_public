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

### Where the residual is — drilled down to the OSCILLATOR (precision)

Localized with three tap levels (all built as throwaway sed-injected harnesses;
the bus-tap accessor is the only committed part):
1. **bus tap** (committed): channel sends (aux1/aux2) diverge at frame 1, mix at
   frame 7. Channel chain (comp/chorus/dist-stereo/dcf/boost) ALL verified vs ASM
   by reading — exonerated.
2. **voice tap** (chanbuf after voice loop, before `chansw.process`, both cores
   via a sed-injected `call`): VOICE output diverges 0.0266 (masked in chanbuf
   until ~#4596 because the amp env starts near 0). Root is in the voice.
3. **per-stage / per-osc tap** (vcebuf after each sub-stage / each osc in
   `syV2Render`): the **oscillator output diverges at sample #0** — an immediate
   per-sample divergence, traced to **osc[0]**.

ROOT CAUSE = oscillator **frequency precision/rounding**. `chgPitch` computes
`freq = (sInt)(SRfcobasefrq * pow(2.0f, (pitch+note-60)/12))`:
- ASM `syOscChgPitch` uses `fistp` = **round-to-nearest**; the port's `(sInt)`
  cast **truncates** → off by 1 whenever the frac >= 0.5. (leaf tests passed only
  because their test notes happened to have frac < 0.5.)
- ASM uses `pow2` (x87 `f2xm1`/`fscale`) at **24-bit single precision**; the port
  uses libm `pow` in **double** → lands on a different integer for some notes.
A 1-unit freq error → slow phase drift over the song (~0.0012 steady diff) plus
0.088 spikes at tri/saw wave transitions (box-filter cancellation, scaled by
rcpf=1/f). Sample 0 of tri/saw depends on freq directly. The first tri/saw also
has **brpt=0** (col=0 → c1=gain/0=inf, an untested edge; unused for pure
saw-down but still a sharp edge).

PRECISION CONTEXT (important — CORRECTED 2026-06-03, prior note was wrong):

The ASM forces the x87 to **24-bit single precision** for the whole render
(`synth.asm:4868  and ax, 0F0FFh` → PC=00, RC=00 round-to-nearest, exceptions
masked; preceded by `finit`, set on every `_synthRender` entry and restored from
`oldfpcw` at exit).

The earlier claim that "the C++ core uses SSE" is **FALSE on this toolchain.**
`g++ -m32` here defaults to `-mfpmath=387` (verified: `g++ -m32 -Q --help=target`
shows `-mfpmath= 387`; a probe TU compiles `a*b+a/b` to `fmul/fdivp/faddp`). So
the C++ core runs its float math on the **same x87 unit as the ASM** — but at the
**glibc default control word 0x037F = PC=11 = 64-bit significand**, never touched.

So the real difference is NOT "SSE vs x87" and NOT confined to `double`/`pow`. It
is **pervasive**: every multi-op float expression kept live in an x87 register
runs at 64-bit mantissa in the C++ core vs 24-bit in the ASM. The C++ core is
*more* precise than the ASM almost everywhere; faithfulness means deliberately
rounding to 24-bit like the ASM does. Both already share the **15-bit exponent**
range (x87 regardless of PC), so the tri/saw "double" box-filter fix was a
mis-diagnosis: the cancellation needed x87's wide exponent (which plain `float`
on x87 already has), NOT `double`'s 53-bit mantissa. The ASM is 24-bit single,
never 80-bit, so that fix *overshoots* and should revert to `float`.

Why the per-block leaf tests still read 0.0: values that get spilled to `float`
storage between ops are truncated to 24-bit anyway and happen to match; the
divergence lives in expressions kept live in 80-bit registers across several ops
(an `-O2` register-allocation decision — see probe notes below).

Why "setting the C++ CW had no effect" in the prior session: it was measured on
the **osc freq**, which is dominated by **libm `powf`** — and libm transcendentals
run their own internal precision and ignore the caller's control word. The CW
change was real but invisible on that particular test; the conclusion "C++ is SSE"
was wrong.

### NEXT STEP: unified theory — run the C++ core as x87 at PC=24

One dominant lever was never actually pulled (it was dismissed as a no-op under
the false SSE premise):

1. **Set x87 CW to PC=24 in the C++ core**, mirroring `synth.asm:4868`
   (`fstcw`/`and 0F0FFh`/`or 3Fh`/`finit`/`fldcw` on render entry, restore on
   exit). Makes every compiler-emitted float op round to 24-bit bit-for-bit with
   the ASM, across the entire synth. The CW can't reach the two point sources
   below, so all three are needed:
2. **Replace libm `powf`/`pow`/`calcfreq`/`calcfreq2` with inline-asm x87** that
   replicates the ASM `pow2` kernel exactly (`synth.asm:388` —
   `fld1/fld/fprem/f2xm1/faddp/fscale/fstp`; `calcfreq`/`calcfreq2` are the same
   kernel with a different pre-scale `fc10`/`fccfframe`). libm won't honor PC=24
   and uses a different polynomial than `f2xm1`.
3. **Round the freq cast like `fistp`**: `freq = (sInt)(...)` truncates; ASM uses
   round-to-nearest. Do the store as `fistp` (or `lrintf` with x87 round-nearest).
4. **Revert the tri/saw `double` box-filter fix to `float`** — at PC=24 on x87,
   `float` has the 24-bit mantissa AND 15-bit exponent the ASM has; `double`
   (53-bit) overshoots.

Tried in a prior session (all reverted; none closed the whole-song 0.0876 — now
understood as each touching only one minor source while the pervasive 64-vs-24-bit
arithmetic row stayed untouched because it was believed already-matched via SSE):
- `(sInt)` → `lrint` (round): osc 0.095→0.088, whole-song unchanged. (= point
  source #3 only)
- double `pow` → 80-bit `long double exp2l`: osc 0.088→0.0585, whole-song
  unchanged. (wrong direction — ASM is 24-bit, not 80-bit)
- "forcing C++ x87 CW to 24-bit single: no effect" — measured on libm-dominated
  freq; see above.

EXPERIMENT RUN (2026-06-03): lever #1 can be applied with ZERO source edits via
compiler flags — `-mpc32` (sets startup x87 CW to PC=24, must be on the LINK line
too: it pulls `crtprec32.o`/`set_precision`) plus `-mno-sse` (forces every float
op AND conversion onto x87, so no intermediate detours through an 8-bit-exponent
xmm). Rebuilt a throwaway `harness_cpp` with both and ran the whole-song A/B:

    baseline (PC=64, SSE convs):  max 0.0876300689  first divergence #8764
    -mpc32 -mno-sse (all x87/24): max 0.087630054   first divergence #8810

→ The divergence ONSET moves ~46 samples later, but the MAGNITUDE is unchanged.
  So the pervasive 64→24 arithmetic is a REAL but MINOR contributor — NOT the
  dominant source. (Why minor: the synth stores almost every value to an `sF32`
  member between ops, so 64-bit intermediates rarely survive to affect output;
  both cores are already truncated to 32-bit at every store.)

→ The DOMINANT 0.0876 is the freq point-sources (#2 libm powf ≠ f2xm1, #3 cast
  truncate ≠ fistp round) — neither touched by flags. REORDERED PRIORITY:
    HEAVY HITTERS:  #2 inline-asm f2xm1, #3 fistp-round the freq cast.
    CLEANUP TAIL:   #1 -mpc32 / -mno-sse (closes the residual to true 0 after
                    the freq path matches; cheap, no source edits).
  Next experiment: patch the two freq lines (powf→inline f2xm1 pow2; (sInt)→fistp
  round) and re-measure — expect the magnitude to drop for the first time.

### Probe findings (2026-06-03 — all empirically verified on this toolchain)

- **Default runtime x87 CW = `0x037F`** (PC=11 → 64-bit significand, RC=00
  round-nearest). Confirmed with `fstcw` at runtime.
- **PC=24 vs PC=64 demonstrably changes register-kept float results** — e.g. a
  cancellation expr gave `0.666666667` (PC64) vs `0.666666687` (PC24). The lever
  is real, not a no-op.
- **libm `powf` ignores the control word** (identical result at PC=24 and PC=64).
  Confirms the transcendentals must be replaced with inline-asm `f2xm1` to match
  the ASM, not just re-rounded.
- **gcc emits a MIX of int-conversion strategies** under `-mfpmath=387`:
  - The freq cast `(sInt)(SRfcobasefrq * powf(...))` is the truncate dance:
    value live on x87 → `fnstcw`/`or $0xc,%ah` (RC=11 = round-toward-zero) →
    `fistp` → restore. So it **truncates**, confirming point source #3. The
    faithful store is `fistp` at the ambient RC=nearest (i.e. do NOT set RC=chop).
  - Other sites use `cvttss2si` (SSE truncate) or bare `fistpl` (round). When
    building the faithful path, verify each int store individually rather than
    assuming a uniform rule.
- **gcc preserves live x87 values across the `powf` call as 80-bit** (`fstpt`/
  `fldt` spills, `fldt 0x38(%esp)` observed). So even call-surviving intermediates
  keep 64-bit precision in the C++ core — reinforcing that the precision gap vs
  the ASM's 24-bit is carried everywhere, not just within a single expression.
- `SRfcobasefrq` etc. are computed at runtime in `calcNewSampleRate` (depends on
  `sr`), so they are NOT compile-time folded away from the control word — though
  literal-only subexpressions inside them may be folded by `-O2`/MPFR. Re-check if
  a constant-derived value ever diverges.

### STRATEGY: bit-faithful first, then split precision for portability

A C++ port cannot be **both** bit-faithful to the ASM **and** portable: fidelity
is fundamentally an x86/x87/PC=24 proposition (inline f2xm1, control-word
twiddling). Plan:
1. Build the **bit-faithful** validation build (x87 PC=24 + inline transcendentals,
   `V2_VALIDATE`-gated) and drive the whole-song A/B to 0 — this *proves the port
   logic is correct*, independent of precision.
2. Once correctness is locked, the portable build is free to use SSE / libm /
   whatever; the residual precision difference vs the ASM will be small and should
   not be audibly noticeable. Keep the faithful x87 path behind `V2_VALIDATE` (or a
   dedicated define) so the portable path stays clean.

NOTE: "neuter a block in both cores and re-bus-tap" is UNRELIABLE — removing a
correct block changes the signal and perturbs downstream divergence
non-monotonically (neutering dist made it *worse*). Prefer reading, per-mode leaf
isolation, or the per-stage taps above.

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
