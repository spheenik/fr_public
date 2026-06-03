# V2 core divergence — component localization report (Phase 2a)

**Goal:** Phase 1 found the C++ port (`synth_core.cpp`) diverges from the asm
(`synth.asm`) at the whole-song level (~0.1 max error). This phase tests each DSP
block *in isolation* — identical params/state/input on both cores — to find which
block(s) are actually wrong.

**Method:** the asm block routines (`sy*Set`/`sy*Render`/`sy*Tick`) are exposed as
globals in the generated asm copy and called through cdecl register-ABI
trampolines (`tramp.asm`); the C++ blocks are driven directly (harness `#include`s
`synth_core.cpp`). Sample-rate globals are initialized identically and asserted
equal first. Build: `./build.sh` → `comp_osc`, `comp_flt`, `comp_leaves`.

## Per-block results (eps = 1e-4)

| Block | Modes/cases | Result |
|-------|-------------|--------|
| **Oscillator** | pulse, sin, noise | MATCH (≤1.2e-7) |
| | **tri/saw** | **DIVERGE 1.4e-4** (small) |
| **Filter (VCF)** | low, band, high, notch, all | MATCH (≤1.2e-7) |
| | **moog-lo, moog-hi (modes 6/7)** | **DIVERGE 0.10** ← primary |
| **Envelope** | attack/decay/sustain | MATCH (bit-exact) |
| **LFO** | saw, tri, pulse, sin | MATCH (≤1.2e-7) |
| **DC filter** | — | MATCH (2.5e-7) |
| **Bass boost** | — | MATCH (5.6e-6) |
| **Distortion** | mode 0 (off), mode 2 | MATCH |
| | **mode 1, mode 3** | **DIVERGE 0.13 / 2.5** |
| Compressor | — | not isolatable* |
| Reverb | — | not isolatable* |
| Mod-delay | — | not isolatable* |

\* These three read the global synth instance buffers (`SYN.vcebuf` lookahead,
`SYN.aux1buf` reverb send, channel delay buffers) and cannot be driven with a
standalone workspace. They require full-instance ("fallback") testing — deferred.

## Confirmed divergences, ranked

1. **Moog filter (VCF modes 6 & 7), max err 0.10 — PRIMARY.** Matches the
   whole-song error magnitude. The C++ moog *lowpass* is near-silent vs the asm;
   the *highpass* is off by 0.1. Root cause is the moog coefficient math in
   `V2Flt::set` — ryg left a literal `// @@@BUG? V2 code for this part looks
   suspicious.` comment on this exact code. **This is the main fix target.**
2. **Distortion modes 1 & 3, max err 0.13 / 2.5.** Real per-mode port bugs in
   `V2Dist::renderMono` (modes 0/2 match exactly, so the ABI/setup is sound).
3. **Oscillator tri/saw, max err 1.4e-4 — minor.** `V2Osc::renderTriSaw` was
   deliberately rewritten with "a different bit ordering from the ASM version"
   (comment, line 586); the rewrite is slightly inexact.

## Verified-correct blocks

Oscillator pulse/sin/noise, Filter low/band/high/notch/all, Envelope, LFO
(all 4 deterministic modes), DC filter, Bass boost. These need no porting work.

## Notes / gotchas found while building the harness

- **Env state enums differ between cores** (asm `ATTACK=1`, C++ `ATTACK=2`) — the
  C++ source even says "slightly different state ordering". Drive each by its own
  enum value.
- **`V2Boost::init` does not zero its IIR state** (relies on the enclosing
  instance being zero-allocated). A fair standalone test must zero it, else the
  filter blows up from garbage state — this was a harness artifact, not a synth
  bug.
- LFO sample&hold uses `rand()` for its seed in C++ (`init`), so it is not
  bit-comparable without forcing equal seeds; the 4 deterministic modes match.

## Consequence for the fix

The follow-on port-correctness change should target, in priority order:
**(1) the moog filter coefficients in `V2Flt::set`** (the 0.1-scale, song-level
bug), **(2) distortion modes 1 & 3**, **(3) the tri/saw oscillator** (cosmetic).
Compressor/reverb/mod-delay still need a fallback (full-instance) test before
they can be cleared or implicated.
