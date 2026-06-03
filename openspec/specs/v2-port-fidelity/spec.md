# v2-port-fidelity Specification

## Purpose

Ensure the C++ port of the V2 synthesizer reproduces the behavior of the original
assembly reference (`synth.asm`) within component-equivalence tolerance. This
capability tracks fidelity requirements for individual synth components, beginning
with the Moog filter.

## Requirements

### Requirement: Moog filter matches the assembly reference

The C++ Moog filter (`V2Flt` modes MOOGL/MOOGH, driven by `V2Moog::step`) SHALL
produce output that matches the assembly `synth.asm` Moog ladder within the
component-equivalence tolerance (eps = 1e-4), for identical coefficients, state,
and input.

#### Scenario: Moog lowpass matches

- **WHEN** the Moog lowpass (VCF mode 6) is driven with identical parameters and
  input on both the asm and C++ cores
- **THEN** the two outputs match within tolerance (the C++ output is no longer
  near-silent)

#### Scenario: Moog highpass matches

- **WHEN** the Moog highpass (VCF mode 7) is driven with identical parameters and
  input on both cores
- **THEN** the two outputs match within tolerance

#### Scenario: Ladder feedback sign and state writeback

- **WHEN** `V2Moog::step` updates each ladder stage and writes back the fed-back
  state
- **THEN** it adds the feedback coefficient term (matching the asm `.processmoog`
  ladder) and stores the once-unbiased value as the feedback state

### Requirement: Standard filter modes unaffected

The Moog fix SHALL NOT change the behavior of the non-Moog filter modes, which
already matched the assembly reference.

#### Scenario: Standard modes still match

- **WHEN** VCF modes low/band/high/notch/all are tested after the Moog fix
- **THEN** all five still match the assembly reference within tolerance

### Requirement: Distortion bitcrusher matches the assembly reference

The C++ distortion BITCRUSHER mode (`V2Dist::bitcrusher`) SHALL quantize the input
by rounding to nearest (matching the asm `fistp`), not by truncation, so its
output matches the assembly reference within the component-equivalence tolerance.

#### Scenario: Bitcrusher matches

- **WHEN** the BITCRUSHER distortion mode is driven with identical parameters and
  input on both the asm and C++ cores
- **THEN** the two outputs match within tolerance

### Requirement: Distortion overdrive matches the assembly reference

The C++ distortion OVERDRIVE mode (`V2Dist::overdrive` via `fastatan`) SHALL
produce output matching the assembly reference within tolerance, by reproducing
the asm `fastatan` rational approximation exactly — including the shared x²
denominator coefficient and the exponent-byte table-select behavior of the
shipping asm.

#### Scenario: Overdrive matches

- **WHEN** the OVERDRIVE distortion mode is driven with identical parameters and
  input on both cores
- **THEN** the two outputs match within tolerance

#### Scenario: fastatan matches across its input range

- **WHEN** the C++ `fastatan` and the asm `fastatan` are evaluated over a sweep of
  inputs spanning both the |x|<1 and |x|≥1 regions
- **THEN** their results match within tolerance across the whole range, including
  the |x|≥2 region where the asm uses the |x|<1 table
