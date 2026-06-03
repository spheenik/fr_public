## ADDED Requirements

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
