# v2-port-fidelity (delta)

## ADDED Requirements

### Requirement: Boost shelving-EQ coefficients match the assembly reference

`V2Boost::set` SHALL compute the five biquad coefficients (`b0`, `b1`, `b2`,
`a1`, `a2`) bit-identically to the assembly `syBoostSet` (synth.asm) for every
boost amount value 1..127, in both the faithful and portable builds — by
reproducing the asm's operand association for the `a0` normalizer
(`(beta·sin + (A−1)·cos) + (A+1)`, not `((A+1) + (A−1)·cos) + beta·sin`) and
the asm's stepwise beta computation (`sqrt((A²+1) − (A−1)²)`, each intermediate
rounded to single precision, not the algebraically equal `sqrt(2A)`).

#### Scenario: Amount 92 produces bit-identical coefficients

- **WHEN** the boost is set with amount 92 (the value pzero_new.v2m switches a
  third boost channel to at 9.677 s, where the orderings previously rounded
  differently) on both the asm and C++ cores
- **THEN** all five coefficients match bit-exactly (previously all five were
  1 ULP low in the C++ core: `b0=3f80fa30` vs asm `3f80fa31`, etc.)

#### Scenario: All amounts produce bit-identical coefficients

- **WHEN** the boost set/render path is exercised for every amount 1..127 with
  identical input on both cores
- **THEN** the outputs match bit-exactly (eps = 0) for every amount, including
  the 21 amounts where the `sqrt(2A)` beta simplification rounds differently
  from the asm's stepwise form

#### Scenario: In-context boost tap closes

- **WHEN** the whole-song A/B runs with the channel-chain taps
  (`BUSTAP`, `CHANSOLO=7`)
- **THEN** the `ch_boost` tap no longer introduces divergence at the 9.677 s
  patch event (the post-boost diff equals the post-comp input diff, instead of
  jumping ~750x above it)

### Requirement: Distortion overdrive gain2 matches the assembly fpatan setup

In the bit-faithful validation build (`V2_X87_FAITHFUL`), `V2Dist::set` SHALL
compute the OVERDRIVE mode's `gain2 = (param1/128) / atan(gain1)` using the x87
`fpatan` instruction (mirroring asm `.mode1`, synth.asm:1833-1838), not libm
`atan`, so the stored single-precision `gain2` is bit-identical to the asm for
identical parameters. The portable build SHALL keep libm `atan` (no DSP change).

#### Scenario: gain2 bit-identical in the faithful build

- **WHEN** an OVERDRIVE distortion patch is set with identical parameters on
  both the asm core and the faithful C++ core, and the stored `gain2` is dumped
  from both sides
- **THEN** the two `gain2` bit patterns are identical, including for the ch7
  patch set at 9.677 s in pzero_new.v2m where `vce_dist` previously diverged
  by 2.98e-8

#### Scenario: Portable build unchanged

- **WHEN** the C++ core is built without `V2_X87_FAITHFUL`
- **THEN** the overdrive `gain2` computation uses libm `atan` exactly as before

### Requirement: Set-path coefficient oracles sweep their full input domain

Leaf oracles for blocks whose `set()` routines exhibit data-dependent rounding
SHALL sweep the full domain of the set-path input rather than a single value;
specifically the boost oracle SHALL compare asm-vs-C++ output bit-exactly
(eps = 0) for every amount 1..127.

#### Scenario: Sweep catches a single-amount regression

- **WHEN** a boost set-path regression is introduced that only affects a subset
  of amount values (e.g. reverting the beta formula, which affects 21/127)
- **THEN** the sweep oracle fails and names the offending amount(s), even though
  a single-amount test could pass
