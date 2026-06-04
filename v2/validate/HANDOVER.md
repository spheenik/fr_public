# V2 synth — C++ port fidelity: handover

Start-here note for picking this up in a fresh session.

## ⚡ LATEST STATE (2026-06-04, session 3) — READ THIS FIRST

Whole-song A/B on `pzero_new.v2m` (faithful build, `auto` length = 235.3s):
**max-abs 0.0218729973** (was 0.0491175018 at session start), rms 0.000411.
**Bit-exact for the first ~17.8 s** (first divergence float #1567954, stereo sample
783977 — exactly the ch7 noise/FM-channel onset). Older sections below are
historical; trust this section where they conflict.

### Fixes landed this session (in working tree, UNCOMMITTED — commit these!)

1. **note-off over-release** (`synth_core.cpp`, MIDI note-off loop): the asm
   (`ProcessNoteOff`, synth.asm:5398-5400) releases the FIRST voice matching
   (chan, note, gate) then `jmp .end` — STOPS. The port released EVERY matching
   voice, so overlapping/retriggered notes killed a voice the asm keeps sustaining
   → its mod envelope drifted → filter cutoff → the dominant residual.
   Fix: `break;` after the first `voice->noteOff()`. **0.0491 → 0.0219.**
2. **parallel filter-balance branch** (`V2Voice::set`): the asm (syV2Set,
   synth.asm:2468-2481) picks the f1gain/f2gain branch on the sign of
   `fist(fltbal-64)` — the ROUND-TO-NEAREST INTEGER — not the float sign of x.
   For modulated fltbal ∈ (63.5, 64) the asm takes the ≥0 branch → f1gain = 1-x
   **> 1** (slight boost); the port attenuated f2gain instead. Fix: branch on
   `v2_fistp(fltbal-64) >= 0` (portable: `lrintf`). **First divergence 7.6s → 17.8s;
   the ENTIRE voice path became bit-exact** (see ledgers below).
3. `flcalc` long-double VCF working state (V2LRC/lrc_step_2x + V2Flt::render):
   **proven byte-identical no-op** (the x87-exponent hypothesis was falsified) —
   keep as documentation-of-equivalence or revert freely.

### Falsified hypotheses (do NOT re-chase these)

- **"Residual = osc-freq phase drift" — DISPROVEN by knockout.** Cross-feeding the
  asm's freq stream into the C++ (`FREQKNOCKOUT`) changed the output by ZERO (path
  proven live via corrupt-ledger control). The osc pitch/freq drift was a SYMPTOM
  of fix #1.
- **"VCF state diverges from x87 exponent range" — falsified** (fix #3 = no-op).
- The earlier "vce_flt diverges / filter-internal" reads were CONFOUNDED: offset
  20312-style records in the fltlog are the **distortion's embedded V2Flt**
  (V2Dist filter modes), processed AFTER the vce_flt tap; voice vcf pairs sit at
  {base+168?, +56} with voice stride 660 (cpp). The real birth point was fix #2's
  combine gains.

### Where the remaining 0.0219 lives (current, precise)

ALL per-voice ledgers are now **0 divergences over the whole song**: osc outputs +
combine gains (osclog), env/lfo outs + suf + states (ctrllog), VCF input/state/
coeffs/mode (fltlog), osc freq/pitch/nffrq/cnt/mode/gain/brpt/nfb (freqlog).
BUSTAP chain (whole song): `vce_osc 0`, `vce_flt 0` →
`vce_dist 3.8e-6` (seed: voice DIST; known fpatan-vs-libm-atan overdrive suspect)
→ `chan 4.8e-7` → `ch_dcf1 4.8e-7` → `ch_comp 1.9e-6` → **`ch_boost 9.3e-4`** →
**`ch_dist 6.1e-3`** → `ch_chorus/dcf2 6.1e-3` → premix 5.1e-4 → reverb 5.1e-4 →
**`post_compr 2.0e-2`** (sum compressor amplifies) → final 0.0219.
**NEXT:** (a) faithful x87 `fpatan` for the dist overdrive (the 3.8e-6 voice-dist
seed AND the ch_dist jump — both dist stages); (b) re-examine ch_boost's 500x jump
(boost was made bit-exact in isolation last session — in-context input is now
~1.9e-6-noisy, so check whether boost amplifies or adds); start at the ch7 onset
(17.8s), `CHANSOLO=7` isolates it.

### Diagnostic infrastructure built this session (all V2_VALIDATE, reusable)

Four per-event ledgers, 32-byte records (`FreqLogRec` in `compat.h`), drained per
`synthRender` by the player, dumped under `FREQLOG=<prefix>`:
- `<pfx>.freqlog` kind=0/1: osc/lfo freq computations — [kind, offset, freq, pitch,
  nffrq, cnt, mode, nf.b] (+ `.freqmap` ordinal→sample sidecar).
- `<pfx>.ctrllog` kind=2: per-voice-per-tick [aenv.out, env2.out, lfo1.out,
  lfo2.out, env2.suf, states]. ⚠ C++ `V2Env::State` order is {OFF,RELEASE,ATTACK,
  DECAY,SUSTAIN} ≠ asm {OFF,ATK,DEC,SUS,REL} — map before comparing.
- `<pfx>.fltlog` kind=3: per-V2Flt-render [lrc.l, lrc.b, cfreq, res, first-input,
  mode]. ⚠ includes the DIST's embedded filters, not just vcf1/vcf2.
- `<pfx>.osclog` kind=4: per-voice [vcebuf[0..2], f1gain, f2gain, fmode].
`FREQKNOCKOUT=<asm.freqlog>` replays asm freq+nffrq into the C++ core by call
ordinal (kind-asserted; warns+falls-through past the tail). `freqdiff.py` diffs
freqlogs. ⚠ raw `offset` fields are PER-CORE (asm/cpp struct sizes differ) — align
by record ordinal (validated: counts equal + coeffs match), never by offset.
⚠ asm-side emit routines live in `asm_appendix.asm`; injected by `build.sh` sed
(anchors: osc/lfo freq fistp, syV2Tick post-LFO lea, syFltRender l-store + regular
path start, syV2Render post-osc lea). Inert when env unset (integer-only, own
buffers; verified byte-identical).

### Operational gotchas

- **The dumps are GB-scale; /tmp is a 31G tmpfs.** A full /tmp breaks EVERYTHING
  cryptically (rtk hook SIGABRT/134, harness exit 1, empty logs, EDQUOT on write).
  `rm /tmp/*.f32 /tmp/*.freqlog ...` between runs.
- Renders: `./harness_{asm,cpp} ../v2m/pzero_new.v2m out.f32 auto` ≈ 30-60s each.
- Uncommitted working-tree files: `synth_core.cpp` (2 fixes + flcalc + ledger
  hooks), `compat.h` (FreqLogRec), `asm_appendix.asm`, `build.sh`,
  `v2mplayer_port.cpp`, `freqdiff.py` (new). OpenSpec change
  `characterize-v2-osc-freq-drift` documents the full investigation (KEY FINDINGS
  1-7 in tasks.md).

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

Whole-song A/B on `pzero_new.v2m` (faithful build, `V2_X87_FAITHFUL`):
**max 0.0876298994** (was 0.0876300689 before the freq-path fix). The bit-faithful
freq + all-transcendentals path moved only the 7th significant figure — confirming
the freq path is NOT the dominant residual (see "What's OPEN").

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
- *bit-faithful freq path, gated behind `V2_X87_FAITHFUL` (this session):*
  inline-asm x87 `v2_pow2`/`v2_calcfreq`/`v2_calcfreq2` (the asm `f2xm1` kernel)
  + round-to-nearest `v2_fistp` store + `* fci12` reciprocal-multiply in
  `V2Osc::chgPitch`, replacing libm `pow`/`powf`, the truncating `(sInt)` cast,
  and `/12.0f`. Tri/saw box-filter reverted to `float` under the faithful build
  (the asm is 24-bit single, not 80-bit; `double` overshot). Build runs x87 at
  PC=24 / no-SSE via `-mpc32 -mno-sse -DV2_X87_FAITHFUL`. The osc now bit-matches
  the asm at sample 0; whole-song magnitude **unchanged** (0.0876) — the freq path
  was necessary but not sufficient. See `openspec` change `fix-v2-freq-precision`.
- *ASM bug, flagged:* `BUG_V2_ATAN_TABLE` — fastatan picks the wrong rational table
  for |x|>=2 (cmovge vs cmovae). Build also generates a fixed-ASM variant so the
  flag=0 build validates symmetrically.

## What's OPEN (next step for "combined output")

The whole-song A/B (`harness_asm` vs `harness_cpp`) diverges **~0.0838** on
`pzero_new.v2m` (current measured; was 0.108534 — see progress below).

### ✅ RESOLVED / DEAD END (2026-06-03): the residual is STRUCTURAL, not precision

**Stop pulling floating-point precision levers — the axis is exhausted.** Four
independent precision interventions now leave the whole-song magnitude unmoved:

| intervention | whole-song max-abs vs asm |
|--------------|---------------------------|
| x87 PC=24 faithful (current build) | 0.0837996621 |
| `+ -fexcess-precision=standard`    | 0.0837996621 (codegen + binary + audio BYTE-IDENTICAL) |
| `-mpc32 -mno-sse` (24-bit mantissa)| onset shift only, magnitude unchanged |
| **full SSE math** (`-msse2 -mfpmath=sse`, FLT_EVAL_METHOD=0) | 0.0838358328 |

**On the GCC excess-precision question (settled empirically on GCC 16.1.1):**
- The "GCC promotes float literals to 80-bit `fldt`" bug is **REAL** but
  **context-dependent**: a *non-exactly-representable* literal (`0.8f`, `5.6f`,
  `1/12` …) that feeds a *live / excess-precision* subexpression is promoted to an
  ~exact 80-bit long double (`fldt`); the SAME literal stored to a `static const
  sF32` (or used where the result is immediately stored to float) loads as 32-bit
  (`flds`/`fmuls`). Exactly-representable literals (integers, powers of two,
  halves/quarters) are immune either way. Confirmed: the moog coeffs (`0.8f`,
  `5.6f` in `V2Flt::set`) promote to `fldt`; `fci12` was the first instance found.
- **The cure that DOES work:** `-mfpmath=sse -msse2` (sets `FLT_EVAL_METHOD=0` →
  no x87 excess precision at all → all literals 32-bit, globally, no source edits).
  Verified: moogA `fldt` count 2 → 0 under SSE.
- **The cure that does NOT work:** `-fexcess-precision=standard` is **inert on x87
  here** — leaves `FLT_EVAL_METHOD=2` and still promotes the constants, in **C and
  C++**, at every `-std` (c++03/17/20, c99/c11). The manual implies C++ support but
  on this target it changes nothing for constant promotion. Do not re-try it.
- **BUT NONE OF THIS MATTERS for the residual:** killing the promotion *globally*
  via SSE moved the whole-song by 0.00004 (0.0838 → 0.0838). So the constant bug,
  though real, is **not** the source of the 0.084. Fixing the moog/literal
  promotion is at most a correctness nicety, not the residual.

**→ NEXT STEP IS STRUCTURAL, NOT PRECISION.** The first divergence crosses
threshold at a *fixed sample* (~stereo 4401, ~0.1 s in) **regardless of FP mode** —
the hallmark of a control-flow / logic difference (a note trigger, filter-mode
switch, env/LFO phase boundary the port sequences differently), not a ULP drift.
Investigate *what happens at that sample* in the voice path (the queued voice-chain
tap, §8/§9) by comparing **logic**, not bits. See the change
`localize-v2-mix-chain-divergence` (re-pointed at the voice-event investigation).

### ⚠ SYSTEMIC: GCC x87 EXCESS PRECISION on float constants (REAL but NOT the residual — see RESOLVED block above)

The single most important thing to fix next, because it is almost certainly
biting in **many** places (filter cutoff, env/lfo coeffs, every non-power-of-two
constant), not just the one we patched.

**The bug:** the asm defines constants as real 32-bit floats (`fci12 dd
0.083333333333` → `0x3daaaaab` = 0.0833333358) and loads them with `fmul dword`
(24-bit). The C port writes the same value as `0.083333333333f` — BUT under
`-mfpmath=387` (forced by `-mno-sse`) and WITHOUT `-fexcess-precision=standard`,
GCC uses `FLT_EVAL_METHOD=2`: it emits the literal as an **80-bit long double**
and loads it with `fldt`. So the C constant is ~exact 1/12, a DIFFERENT value than
the asm's 24-bit float. `-mpc32` (PC=24) only rounds arithmetic *results*, NOT
constant *loads* — so it does not help here. Confirmed by disassembly (`fldt` vs
`fmuls`) + gdb (osc-freq arg differed by 1 ULP → freq `fistp` flipped on tie
products → osc phase drift; was the dominant osc divergence, vce_osc 0.15→1.3e-6
once fixed).

**Patched so far (surgically, ONE site):** `v2_oscfreq` does the `*fci12` in inline
asm with a `static const sF32 v2_fci12` fed as an `"m"` operand (forces a 4-byte
`fmul dword`). See `synth_core.cpp` V2Osc::chgPitch.

**WANT: the global cure** — make GCC define/load ALL float constants at 24-bit like
the asm, so we don't whack-a-mole each site. Candidates to try (in the validation
build only):
  - `-fexcess-precision=standard` (the textbook fix; sets FLT_EVAL_METHOD=0,
    rounds every op AND constant to declared type). **Tried once, did NOT change
    the osc-freq arg in a -O0 probe — investigate why (—std interaction? needs
    -fno-fast-math? GCC version?). This is the highest-leverage thing to get
    working.**
  - `-ffloat-store` (forces spills; helps live intermediates, may not fix constant
    loads).
  - `-fsingle-precision-constant` (forces FP constants to single — but may wrongly
    narrow the few places that legitimately need double, e.g. the `long double` pi
    in `fastsinrc` and the `double` tri/saw box filter; audit before using).
If a flag works, re-verify the new bit-exact oracles stay green and the osc inline
helpers can likely be simplified back to plain C.

**New bit-exact test oracles (built this session — keep & extend):**
  - `comp_lfo`  — LFO in pzero configs, `eps=0`. Found `fastsinrc` float-pi vs
    `fldpi` (80-bit). Also has a direct `fastsin`/`fastsinrc`/`pow2` sweep.
  - `comp_osc_exact` — osc swept over all notes, `eps=0`, plus a freq/brpt
    setup-integer check. Found the tri/saw hard-case association, the
    `calcNewSampleRate` divide-vs-reciprocal, and the `fci12` excess-precision bug.
  Both link the asm via new `v2x_fastsin/fastsinrc/pow2` trampolines (tramp.asm)
  and `v2x_off_syWOsc_freq/brpt` offsets (asm_appendix.asm).
  Lesson: tolerance-based oracles (eps=1e-4) HIDE 1-ULP bugs that accumulate into
  audible phase drift. Use EXACT comparison for anything that feeds a phase/freq.

### STATUS UPDATE (2026-06-03): freq path closed, residual is the GLOBAL MIX CHAIN
NOTE: the analysis below is SUPERSEDED in part — the dominant per-voice source was
the OSCILLATOR (excess-precision freq drift, now fixed). After the osc fix the
voice-level source is the **VCF filter** (vce_flt ~6.6e-3), amplified by the
per-channel + global compressors into the whole-song ~0.082. Fixes landed this
session: ModDel/chorus rounding (0.0876→0.0838), fastsinrc pi, tri/saw hard cases,
calcNewSampleRate recip-multiply, fci12 excess precision (osc now bit-exact).

### STATUS UPDATE (2026-06-03): freq path closed, residual is the GLOBAL MIX CHAIN

The `fix-v2-freq-precision` change made the oscillator frequency path bit-faithful
to the asm (inline `f2xm1`, `fistp` round, `fci12` multiply; build at x87 PC=24 /
no-SSE). Result: the osc no longer diverges at sample 0, but the **whole-song
magnitude did not move** (0.0876300689 → 0.0876298994, only the 7th sig-fig).

Re-running the bus-tap on the faithful build **re-localized** the dominant source:

    aux1 = 0.00267   aux2 = 0.00258   mix = 0.08116

The ~0.081 is introduced in the **GLOBAL MIX CHAIN** on `mixbuf` — reverb / mod-
delay / dc-filter / lowcut-highcut / sum-compressor (`synth_core.cpp:3321-3349`) —
NOT in the voice / oscillator / transcendentals. A NEGATIVE RESULT confirms this:
converting *every* transcendental in the synth (calcfreq/calcfreq2, all `powf`,
reverb `base^e`) to the faithful x87 kernel did not move the magnitude. The
residual is **structural**, not a precision gap — the handover's previously-noted
"instance-entangled compressor / reverb / mod-delay" state.

**FAITHFUL-vs-PORTABLE SPLIT (decided):** a single C++ binary cannot be both
bit-faithful to the asm and portable — fidelity is fundamentally x86/x87/PC=24
(inline `f2xm1`, control-word semantics). The faithful path is gated behind
`V2_X87_FAITHFUL` (validation build: `-mpc32 -mno-sse -DV2_X87_FAITHFUL`); the
portable default build is untouched (libm / `(sInt)` / `double`, SSE-ok). Goal:
drive the *faithful* build's whole-song A/B to 0 to prove port logic is correct;
then the portable build's small sub-audible precision differences are acceptable.

**NEXT CHANGE:** localize the global-mix-chain divergence — tap `mixbuf` between
the reverb / mod-delay / dc-filter / lowcut-highcut / sum-compressor stages
(`synth_core.cpp:3321-3349`) in both cores and diff stage-by-stage to find which
global effect introduces the 0.081. (The osc/voice/freq path is exonerated.)

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
