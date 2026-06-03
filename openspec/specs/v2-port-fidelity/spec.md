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

### Requirement: Oscillator frequency matches the assembly reference

The C++ oscillator pitch-to-frequency computation (`V2Osc::chgPitch`) SHALL, in
the bit-faithful validation build, produce the same `freq` and `nffrq` as the
assembly `syOscChgPitch` (`synth.asm`), for identical pitch, note, and sample
rate — by reproducing the asm's x87 `f2xm1`-based `pow2`/`calcfreq`, its
round-to-nearest `fistp` store, and its reciprocal-multiply by the 32-bit
constant `fci12` (rather than a libm `pow`, a truncating cast, or a divide).

#### Scenario: Oscillator output matches across notes

- **WHEN** the oscillator is driven with identical parameters and input on both
  the asm and C++ cores, across notes whose pitch fraction lands on either side
  of an integer frequency boundary
- **THEN** the per-sample oscillator outputs match within the component
  tolerance, including sample 0 of tri/saw waveforms

#### Scenario: Frequency rounds to nearest, not toward zero

- **WHEN** `SRfcobasefrq * pow2((pitch+note-60)*fci12)` has a fractional part ≥ 0.5
- **THEN** the stored integer `freq` rounds up (matching the asm `fistp`), not
  down (the previous `(sInt)` truncation)

### Requirement: Bit-faithful validation build uses x87 24-bit single precision

The validation build (`v2/validate`) SHALL compile and link the C++ core so its
floating-point arithmetic runs on the x87 unit at 24-bit single precision with
round-to-nearest, mirroring the asm's per-render control-word setup
(`synth.asm:4868  and ax,0F0FFh`). The portable default build (without the
faithful define) is unaffected and retains libm/SSE precision.

#### Scenario: Validation build runs at PC=24 with no SSE detour

- **WHEN** the validation cpp harness and component oracles are built
- **THEN** they are compiled and linked with x87 24-bit single precision and no
  SSE float detours (so intermediates keep the asm's 24-bit mantissa / 15-bit
  exponent), and the faithful frequency/transcendental path is enabled

#### Scenario: Portable build behavior unchanged

- **WHEN** the C++ core is built without the faithful define
- **THEN** the oscillator frequency path uses the existing libm/`(sInt)`/`double`
  behavior (no DSP change to the portable build)
