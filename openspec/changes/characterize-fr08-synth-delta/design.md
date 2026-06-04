## Context

Any audio difference between our player and fr08's embedded player decomposes
into three layers:

```
            fr08v101.exe (2000)                  our pipeline
        ┌──────────────────────┐           ┌─────────────────────────┐
 DATA   │ fr08.v2m (version 0) │ ──conv2m─▶ converted/fr08.v2m (v6)  │  heuristic defaults
        ├──────────────────────┤           ├─────────────────────────┤
 PLAYER │ year-2000 sequencer  │    vs     │ v2mplayer.cpp           │  timing, speed evts
        ├──────────────────────┤           ├─────────────────────────┤
 SYNTH  │ year-2000 synth.asm  │    vs     │ synth_core.cpp ≡ 2004asm│  6 versions of DSP
        │ (+ Ronan v0)         │           │ (RONAN compiled out)    │
        └──────────────────────┘           └─────────────────────────┘
```

Our port is already bit-exact against the 2004 asm, so "our player vs fr08's
player" reduces to (conversion semantics ⊕ 2000→2004 synth delta). Whatever we
measure must be *attributable* to a layer, not just detected.

Existing assets this builds on:

- **Extraction artifacts.** `v2m/fr08-extraction/` scripts; `/tmp/fr08/` still
  holds the previous session's intermediates (`depacked.bin` 3.2M, wine memory
  dumps + maps). All re-derivable via `dumpmem.sh` if /tmp is cleaned.
- **validate/ rig.** Two-process A/B harnesses, `compare.py`, ledger/BUSTAP/
  CHANSOLO toolkit; `build.sh` already assembles `synth.asm` into Linux objects
  (`asm_appendix.asm`) — i.e. we have a clean reference binary of the 2004 code
  and a precedent for running foreign x86 synth code natively.

## Goals / Non-Goals

- **Goal:** enumerate the actual behavioral delta (code-level, then per-sample)
  between the fr08-embedded synth and the 2004 core we ported.
- **Goal:** deterministic offline render of the *original* v0 data through the
  *genuine* 2000 code — ground truth nothing else can provide.
- **Non-goal (yet):** committing to bit-exact party-2000 reproduction. That
  decision is deferred until the delta size is known. Phase D is designed but
  not pre-approved.

## Decisions

### D1. Method order: tables → static diff → dynamic ground truth

Cheapest evidence first. The version tables were free (done, see proposal). The
static diff (B) enumerates *what* changed without any execution risk. The
harness (C1) measures *how much it matters* per-sample. Audio capture from the
live demo under Wine (dsound proxy) is demoted to a one-time sanity anchor that
the C1 harness equals what the demo really plays — never the primary diff
method (realtime, resampling, jitter).

### D2. Function matching is anchored on float constants

The original synth is hand-written NASM: no compiler noise, and the constant
pool (fc32bit, fcdcoffset, fastatan coefficient pair, moog 1/6, …) survives
verbatim across years. Matching strategy: extract the constant table from 2004
`synth.asm`, scan the depacked image for the IEEE-754 bit patterns, cluster →
locate the synth's data segment; then walk code references into it (capstone)
to segment and label functions. Constants that *differ* are themselves
first-class findings (coefficient changes).

### D3. Envelope code is the first deep-diff target

`transEnv` proves decay/release scaling changed at v2 and hypothesizes a
square-law relationship. The disassembly either confirms the exact old formula
(then a gated reimplementation can be bit-exact) or reveals kb's translation
was approximating something else entirely (then transEnv is dead on arrival).
Either outcome retires the biggest known audible delta.

### D4. Era flag is general, not envelope-specific

If phase D proceeds: one compat input — the source v2m format version —
plumbed through player→core init, gating *all* period behaviors found by B.
Matches the port's existing philosophy (faithful by default, fixes behind
gates; here: modern by default, period behavior behind a vintage gate). The
core currently never sees the source version (conversion erases it), so the
plumbing is a side-channel through the player/conv layer, designed in phase D
proper.

### D5. Candidate fixes are scored, not argued

Once C1 exists: render fr08 through (a) current core, (b) current core +
re-enabled transEnv data fix, (c) core + gated old envelope code — each diffed
per-sample against C1 ground truth. transEnv is structurally lossy (7-bit
re-quantization, clamping), so (c) is the only bit-exact-capable option; but
the ranking becomes a number, not an aesthetic.

### D6. Validation is matched-seed A/B; historical playback is a non-goal

The 2000 synth seeds osc-noise / LFO-S&H / dist from `rdtsc` at every voice
init (step B, three sites). This is not an obstacle to validation, only to
"reproduce the exact bytes the demo played in 2000" — which is itself
meaningless: the demo seeded from the live TSC, so **no two runs ever produced
the same noise, yet every run sounded the same** (user, from running it many
times). Perceptual correctness never depended on the noise sample values.

Therefore:
- **Validation** = stub `rdtsc` to the *same fixed value* in BOTH the extracted
  2000 code (C1 ground truth) AND our era-gated core. Both LCGs then consume an
  identical seed, so the A/B is bit-exact over the *entire* signal — noise and
  S&H included — and reduces to the familiar max-abs-0 check. This fully proves
  the gated port reproduces the 2000 behavior.
- **Historical playback reproduction** is an explicit **non-goal**. We are not
  trying to match any particular 2000 render; we match the *code's behavior*
  under a pinned seed.

Net: the 7 confirmed deltas (DELTA.md) get gated behind the era flag, and "is
it correct?" is answered by matched-seed A/B against the genuine 2000 code. The
`fsin` sine (delta 4) emits the same `fsin` in the gated path → bit-exact on a
given host (the A/B requires nothing more). Its low-bit host-dependence is
**accepted, not a caveat**: it is the same x87-as-the-host-gives-it stance the
whole port already takes (`V2_X87_FAITHFUL`), and it "has never been different".

## Risks / Open Questions

- **Determinism of the 2000 code.** Known bug class in this port: rdtsc-seeded
  S&H. If the 2000 synth seeds noise/S&H from timer/rdtsc, the harness must pin
  it before any per-sample comparison is meaningful. Check first in B.
- **Balance=64 neutrality** (conv default for v3 param): does the 2004 core
  treat Balance=64 as a true no-op for `single`/`serial` routing? Settle by
  reading `synth_core.cpp` — if not neutral, it is a conversion-layer bug
  affecting every fr08 patch, independent of the synth delta.
- **Ronan v0.** Speech data is copied through conversion (`v2mconv.cpp:383`),
  and the talking is in the demo. C1 runs it for free (it is their code); our
  validation builds compile RONAN out, so whole-song comparisons must either
  include a RONAN build or gate speech channels.
- **Delta size unknown.** Four years of kb. If B finds the 2000 core is
  pervasively different (not just envelopes + additive features), phase D
  scope balloons — that is exactly why D is evidence-gated.
- **API drift.** The 2000 synth's entry points / workspace layout differ from
  2004's; C1 harness must reverse the period ABI from the binary (the demo's
  own call sites document it).
