## Why

`fr08.v2m` (extracted from the shipping **fr-08: .the .product v1.01** binary,
see `v2/v2m/fr08-extraction/`) renders whole-song BIT-EXACT asm-vs-C++ under the
**final 2004** V2 core. But per the extraction README caveat, that validates the
modern rendering of the *converted* data — it does not reproduce the party-2000
sound. The demo carries its own year-2000 synth, and the patch data crosses the
largest conversion gap conv_v2m supports: format **version 0 → 6**.

Exploration of the version tables (`sounddef.h:84-299`, `v2mconv.cpp`) already
recovered kb's changelog for free — every `V2PARAM.version > 0` is a post-fr08
addition:

| ver | added | conv2m default | neutral? |
|-----|-------|----------------|----------|
| 1 | channel + sum compressor (9+9 params), Boost | Off / 0 | yes |
| 2 | Voice Txpose, Osc1/2/3 RingMod | 64=center / Off | yes |
| 3 | filter routing Balance | 64=mid | **unverified** |
| 4 | Reverb LowCut | 0 | yes |
| 5 | LFO1/2 Polarity | 0="+" | probably |
| 6 | AuxA/AuxB busses (recv/send), osc AuxA/AuxB modes | 0=off | yes |

So the param-level gap is mostly **additive with neutral defaults** — with one
admitted exception: `transEnv` (`v2mconv.cpp:243`), a **disabled** translation of
envelope Decay/Release values (params 33/36/39/42) for `vdelta<2` files, mapping
old→new through `v' = -log2(1-sqrt(1-2^(-v·11/128)))·128/11`. kb is saying, in
source: *the envelope math changed between v0/v1 and v2, conversion can only
approximate it, and the approximation is commented out*. Our bit-exact-2004 render
plays fr08's old envelope values through new envelope math, raw — affecting the
decay/release shape of essentially every note in the song.

What the param tables cannot see: DSP changes to *existing* features (reverb,
filters, dist curves, noise gen, sequencer timing) between the 2000 and 2004
synth. Those differences exist only in the fr08 binary itself. The original is
hand-written NASM, so disassembly ≈ source — and V2 is saturated with magic float
constants that survive compilation verbatim, giving us function-level anchors.

## What Changes

Three phases, evidence-gated (later phases depend on what earlier ones find):

- **B — static binary diff.** Locate the year-2000 synth code in the depacked
  fr08 image (constant-anchored), disassemble, and diff function-by-function
  against the assembled 2004 `synth.asm`. Envelope code first (transEnv tells us
  *something* changed there; the binary tells us *what*). Output: a catalogued
  behavioral delta list in `v2m/fr08-extraction/`.
- **C1 — ground-truth harness.** Call the extracted 2000 synth+player code
  offline (precedent: `validate/asm_stubs.cpp` already runs foreign x86 synth
  code natively) and render the *original* v0 `fr08.v2m` deterministically.
  Output: the genuine party-2000 waveform as a per-sample reference, pluggable
  into the existing `compare.py` / ledger toolkit.
- **D — era-gated compat.** B found a small, finite delta (7 confirmed items,
  DELTA.md), so this proceeds: plumb a general "era" flag (source format
  version) through to the core and gate the period behaviors behind it — same
  pattern as the existing `#define`d ASM-bug gates, runtime-selected by file
  vintage. **Validation is matched-seed A/B** (rdtsc stubbed to the same fixed
  value in both the extracted 2000 code and the gated core → bit-exact over the
  whole signal incl. noise; see design D6). The gated old-envelope path is
  bit-exact and supersedes the lossy data-side transEnv.

  **Non-goal:** reproducing any particular *historical* 2000 playback. The demo
  seeded noise/S&H from the live TSC — no two runs were ever sample-identical,
  yet all sounded the same. Correctness is "gated core == 2000 code under a
  pinned seed", not "matches a 2000 recording".

## Impact

- `v2/v2m/fr08-extraction/` — new analysis scripts + `DELTA.md` findings doc
- `v2/validate/` — (phase C1) loader/harness for the extracted 2000 code
- `v2/synth_core.cpp`, player/conv plumbing — (phase D only, evidence-gated)
- No behavior change to current rendering until phase D is approved on evidence
