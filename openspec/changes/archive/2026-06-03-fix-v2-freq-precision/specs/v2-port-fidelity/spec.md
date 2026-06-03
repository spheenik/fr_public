# v2-port-fidelity (delta)

## ADDED Requirements

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
