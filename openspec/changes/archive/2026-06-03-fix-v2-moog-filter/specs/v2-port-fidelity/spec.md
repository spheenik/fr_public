## ADDED Requirements

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
