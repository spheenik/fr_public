# Design: portable-version-native-player

## Context

The repo now contains a fully characterized pair of V2 engine endpoints:

- **v0 (year 2000, fr08)**: bit-exact reproduction proven against the
  genuine binary (whole 663 s song, max|d| = 0), via era gates in
  `v2/synth_core.cpp` (`eraV0()`, `eraEnvOld()`) under the
  `V2_X87_FAITHFUL` build (32-bit, x87, `-mpc32`).
- **v6 (2004 `synth.asm`)**: bit-exact across a 5-song corpus.

The behavior deltas between the two are catalogued in
`v2/v2m/fr08-extraction/DELTA.md` (~15 deltas). The v2m **format**
versions 0–6 are mechanically defined by the parameter version
annotations in `sounddef.h` (patch params added at v0,1,2,3,5,6; globals
at v0,1,4,6), and `v2mconv.cpp` detects a file's version by patch-size
fingerprint. The conversion is value-preserving (defaults inserted, MIDI
copied verbatim, `transEnv` disabled) — proven by fr08: converted file +
era gates = bit-exact.

The corpus on hand is bimodal: fr08.v2m is format v0, all other 16 files
are format v6. No v1–v5 material exists locally; period binaries
(fr-013/019/022/025 etc.) could later provide both files and behavioral
evidence.

UPDATE (2026-06-05): both statements above were overturned during
implementation. (a) The *converted/* corpus hides true versions — the
ORIGINALS in-repo (initial checkin da8e5cb + current tree) span v0–v6,
including josie/kkrieger6 at v5 and period synth sources under
RG2/ViruzII + RG2/Viewer. (b) A genuine period binary is now in hand:
fr-030 candytron final (kkrunchy-packed, 2003-08, user-supplied) —
the compiled v5-era synth+ronan. Scope was extended (see proposal):
candytron becomes the v5/Ronan oracle and a third proven era anchor.

Constraints:
- The existing lab (`v2/`, `validate/`, C1 oracle) must remain untouched;
  it is the reference against which the new player is measured.
- The faithful build's x87 tricks (inline asm, PC=24 control word,
  `-m32`) cannot ship in a portable player.

## Goals / Non-Goals

**Goals:**
- A self-contained, portable, embeddable C++ V2M player
  (`v2/portable/`): any platform, 64-bit clean, no asm, no external
  conversion step.
- Native playback of v2m format versions 0–6 with version-correct engine
  semantics, switched per-delta via a data table.
- Cross-host deterministic output: same file + same seed → identical
  bits on every platform.
- Measured, published ε against both oracles (v0: `c1_fr08.f32`; v6:
  `harness_asm` renders).
- Compile-time subsetting: build only the version range (and features,
  e.g. Ronan) you need.

**Non-Goals:**
- Bit-exactness to the historical x87 hardware renders (structural
  fidelity + fixed ε instead).
- Pinning the v1–v5 behavior thresholds with binary evidence (follow-up
  research track; this design only provides the slots and override
  knobs). AMENDED 2026-06-05: the v5 point specifically is now IN scope
  via the candytron binary assay (task 6.0e); v1–v4 remain follow-up.
- Replacing or modifying the validation lab, `tinyplayer`, `in_v2m`, or
  the VSTi.
- Sample-rate flexibility beyond 44100 Hz in the first iteration (the
  2000 engine is hardwired to 44100; the oracles are 44100).

## Decisions

### D1: Fork-and-strip from `synth_core.cpp`, not a clean rewrite

The portable engine starts as a copy of `synth_core.cpp` with the
`V2_X87_FAITHFUL` inline-asm paths replaced by portable equivalents and
the era gating restructured to the table (D3). Rationale: that file *is*
the proven semantics carrier — every PORT FIX and era gate in it encodes
hard-won, oracle-verified knowledge (voice-steal chanmask, allpass
association, moddel left/right tap asymmetry, crusher fold, …). A clean
rewrite would re-roll every one of those dice. Alternative considered:
ground-up rewrite for cleanliness — rejected; the risk/benefit is wildly
lopsided. The sequencer similarly forks `v2mplayer.cpp` (its
timing/decoding logic is already proven event-exact against the 2000
player) adapted to consume the internal canonical form directly.

### D2: Load-time canonicalization to the v6 patch layout + retained `srcVersion`

The loader detects the format version (patch-size fingerprint, same
method as `CheckV2MVersion`), parses that version's layout, and upgrades
patches/globals to the v6 layout by inserting era-correct defaults —
absorbing `v2mconv`'s job at load time. The detected version is kept as
`srcVersion` and drives all behavior gates. Rationale: conversion is
proven value-preserving, and one canonical in-memory format keeps the
render path free of per-version layout branching. Alternative: native
per-version patch structs through the whole engine — 7× layout code for
zero semantic gain (the semantics live in the gates, not the layout).
The loader's version-size tables are generated from the `sounddef.h`
parameter annotations (single source of truth; a comment marks the
provenance and a check in the lab build asserts they match).

### D3: Era behavior deltas as a data table with evidence levels

One visible header (`v2eras.h`) defines every known behavior delta:

```cpp
enum V2Delta { DELTA_ENV_CURVES, DELTA_FRAME256, DELTA_OSC_BOXFILTER,
               DELTA_NOISE_LCG_MSVC, DELTA_NATIVE_FSIN, DELTA_NATIVE_FPATAN,
               DELTA_NO_DCOFFSET, DELTA_CRUSHER_UNFOLDED_GAIN1,
               DELTA_PGMCHANGE_V0, DELTA_NO_VOICE_DCF, DELTA_NO_MASTER_DCF,
               DELTA_NO_COMP_BOOST, DELTA_NO_AUX_BUSSES, DELTA_NO_MOOG,
               DELTA_NO_RVB_LOWCUT, DELTA_TICK_BEFORE_SET,
               DELTA_SUBFRAME_RENDER, /*…*/ };

struct V2DeltaRow { V2Delta id; int flipsAtVersion; V2Evidence evidence; };
// evidence: PROVEN (binary-verified), ANCHORED (param tables imply),
//           ASSUMED (documented guess for the v1..v5 gap)
```

A gate reads `active = srcVersion < row.flipsAtVersion` (i.e. "the old
behavior applies below the flip version"). Anchored rows: comp/boost v1,
ringmod v2, env curves + frame size v2 (kb's `transEnv` hint), reverb
lowcut v4, aux busses v6. Floating rows default to a documented
assumption (initially: flip together with the nearest anchored
neighbor, or at v6 when there is no signal at all). Runtime overrides —
`forceBehaviorVersion` and per-delta forcing (debug builds) — make the
table testable the day mid-era evidence appears. Rationale: evidence
becomes a one-line data edit, never a redesign; the table doubles as the
research ledger. Alternatives: keep ad-hoc `eraV0()` predicates
(unscalable past two eras); strategy objects/vtables per component
(defeats compile-out, overkill).

### D4: Strict float32 arithmetic + project-owned transcendentals

The original engines run x87 with PC=24: every `+−×÷` already rounds to
float32 precision, so portable `float` ops (SSE/NEON) match op-for-op.
Policy, enforced by build flags and code style:

- `float` everywhere in the audio path; no double promotion (literal
  suffixes audited), no `-ffast-math`, `-ffp-contract=off` (FMA skips a
  rounding step → different bits).
- Transcendentals (`exp2`/`sin`/`atan` — the x87 `f2xm1/fyl2x`, `fsin`,
  `fpatan` sites) are **owned fixed implementations** (double-precision
  internals, rounded to float32 once), not libm. Rationale: libm differs
  per OS/version → host-tinted output; owned implementations make the
  renderer bit-identical across hosts, turning "delta vs history" into
  one fixed published constant. Accuracy target: correctly rounded to
  float32 (double internals suffice except astronomically rare ties),
  which matches the x87 80-bit-internal-then-rounded results at the
  points that matter — including the integer `fistp` osc-freq ties that
  the fr08 work proved are the sensitive spots.
- 2004's `fastsin`/`fastatan` are already float polynomial code —
  portable bit-exactly as-is; the owned transcendentals are mainly for
  calcfreq/pow2 paths and the v0-only native `fsin`/`fpatan` gates.

### D5: Compile-time subsetting by constant-foldable version range

```cpp
// build config
#define V2_VER_MIN 0   // e.g. 6,6 for a v6-only build
#define V2_VER_MAX 6

inline bool V2Synth::deltaActive(const V2DeltaRow &r) const {
  if (r.flipsAtVersion <= V2_VER_MIN) return false; // all supported vers past it
  if (r.flipsAtVersion >  V2_VER_MAX) return true;  // none reached it
  return srcVersion < r.flipsAtVersion;             // genuine runtime switch
}
```

With `V2_VER_MIN == V2_VER_MAX` every gate constant-folds and dead
branches are eliminated — single-version builds carry only their code,
from one engine source. Loader version tables outside the range are
likewise excluded (files outside the range are rejected with a clear
error). Ronan is an independent flag (`V2_RONAN`, default on) since the
phoneme tables have real size. Alternative: `template<int VER>`
instantiation — only wins per-version-optimized inner loops in
multi-version builds, which nothing needs; costs compile time and code
bloat.

### D6: Determinism contract

The historical engines seed noise/S&H/dist from `rdtsc` (3 sites) —
non-deterministic by design. The portable player defines playback as
deterministic with seed 0 (matching the C1 oracle's pinned-rdtsc ground
truth); `setSeed()` exists for anyone wanting variation. Denormals are
part of the v0 semantics (gate-held DECAY envelopes decay into
subnormals and audibly modulate — proven by fr08 ch2@67s): the build
documentation forbids FTZ/DAZ (`-ffast-math` and friends) and `open()`
performs a one-time runtime self-check (compute a subnormal, verify
nonzero) so misbuilt hosts fail loudly, not subtly.

### D7: Public API (pull model, header-only surface)

```cpp
namespace v2portable {
struct Player {
  enum class Result { OK, BadFile, UnsupportedVersion, FpEnvBroken /*…*/ };
  Result   open(const void *v2m, size_t len);      // detect + canonicalize
  int      fileVersion() const;                    // 0..6
  void     play(uint32_t fromMs = 0);
  void     render(float *stereoInterleaved, uint32_t frames); // 44100 Hz
  bool     isPlaying() const;
  void     setSeed(uint64_t seed);                 // default 0
  // research knobs:
  Result   open(const void*, size_t, int forceBehaviorVersion);
};
}
```

No callbacks, no allocation after `open()`, no global state (multiple
instances allowed — unlike the historical global singleton). Speech text
access (Ronan) compiled in under `V2_RONAN`.

### D8: Validation methodology

- **v6 path**: render the 16 modern corpus files; compare against
  `harness_asm` renders. Budget: per-song rms and max|d| recorded in the
  change docs; expected magnitude ~1e-6 rms or below (transcendental ties
  only).
- **v0 path**: render fr08.v2m (the *unconverted* original, exercising
  the native loader); compare against `c1_fr08.f32`. Same budget
  discipline.
- **Cross-host determinism**: hash the rendered output; the hash is the
  contract (same on x86_64/aarch64). Initially verified on the machines
  at hand.
- **Subsetting**: v6-only and v0-only builds must reproduce the
  full-build output bit-exactly for in-range files and cleanly reject
  out-of-range files.
- A small `v2/portable/test/` driver reuses `compare.py` and the corpus;
  the lab stays read-only.

## Risks / Trade-offs

- **[Osc-freq integer ties]** A 1-ULP exp2 difference at a `fistp` tie
  flips the integer oscillator frequency by 1 → slow phase drift over
  minutes (the known sensitive spot from the fr08 work). → Owned exp2
  with correct-to-float32 rounding makes this match x87 in all but
  astronomically rare cases; residual cases are part of the published ε
  and identical on every host.
- **[Middle-only behaviors]** A v1–v5 engine may have had behavior that
  exists in *neither* endpoint (appeared and was replaced) — undetectable
  from current evidence. → Accepted; the table + override knobs localize
  the blast radius; a future binary hunt would surface it.
- **[Floating thresholds are guesses]** v1–v5 files (none on hand today)
  may play with wrong-era semantics. → Evidence column makes the
  uncertainty explicit; `forceBehaviorVersion` lets a user/researcher
  correct per file; thresholds are data edits.
- **[Table drift vs `sounddef.h`]** The generated loader tables could
  drift from the source-of-truth param annotations. → Lab-build assert
  comparing generated tables against `sdInit()` output.
- **[Compiler nondeterminism]** A compiler could still reassociate or
  contract FP ops despite flags. → CI-style determinism hash check; keep
  the audio path free of patterns that invite it (no reductions across
  iterations, mirrors the asm structure anyway).
- **[Denormal performance]** v0 envelopes in the subnormal zone are slow
  on some CPUs. → Accepted (matches original behavior); v6 path has the
  dcoffset bias by design.
- **[Fork divergence]** `v2/portable/` forks `synth_core.cpp` knowledge;
  future fixes in the lab don't auto-propagate. → Accepted and
  intentional (lab = research, portable = product); DELTA.md remains the
  shared ledger.

## Migration Plan

Not a deployment migration (new component). Implementation order is
designed so every step is oracle-checkable:

1. Scaffold `v2/portable/` + era table + portable math layer (owned
   transcendentals, unit-checked against the x87 probes in the lab).
2. Fork engine, strip x87, route gates through the table; get the **v6
   path** green vs `harness_asm` (largest corpus, no loader work needed
   yet beyond v6 parsing).
3. Native loader: fingerprint + per-version parse + canonicalize; verify
   v6 files load identically to the old path, then fr08.v2m (v0)
   end-to-end vs the C1 oracle.
4. Subsetting builds + determinism hash + ε publication.

Rollback: delete the directory; nothing else changed.

## Open Questions

- Exact accuracy strategy for owned `exp2`: plain double polynomial
  (simple, ~1e-16) vs. double-double correctly-rounded (heavier,
  eliminates tie risk entirely). Decide after measuring tie frequency on
  the corpus.
- Should per-delta override knobs ship in release builds or debug-only?
  (Leaning: debug-only; `forceBehaviorVersion` ships.)
- WASM as an explicit target platform (denormal + determinism story is
  good there, but untested)?
- How to ship ε results: doc table in the change vs. a checked-in
  baseline file the test driver diffs against (leaning: both — doc for
  humans, baseline hashes for CI).
- Does the canonical patch default set need per-era *defaults* anywhere
  (a v0 file upgraded to v6 layout gets "Off"/neutral values for new
  params — already proven correct for fr08; double-check the v1/v4
  additions have true neutral defaults once mid-era files exist).
