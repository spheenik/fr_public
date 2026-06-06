# portable-determinism Specification

## Purpose
TBD - created by archiving change portable-version-native-player. Update Purpose after archive.
## Requirements
### Requirement: Portable build with no assembly or x87 dependence
The portable player SHALL build and run as plain C++ on 64-bit and 32-bit
targets without inline assembly, x87 control-word manipulation, or
architecture-specific intrinsics in the audio path.

#### Scenario: Native 64-bit build
- **WHEN** the portable player is built on x86_64 and aarch64 hosts
- **THEN** it compiles without architecture-conditional source changes
  and renders the corpus successfully

### Requirement: Strict float32 arithmetic policy
All ordinary arithmetic in the audio path SHALL be IEEE-754 binary32
(`float`) operations — matching the original engines' x87 PC=24
rounding op-for-op — with compiler reassociation, FMA contraction
(`-ffp-contract=off`), fast-math, and silent double promotion excluded
by build flags and code convention.

#### Scenario: Contraction-sensitive result
- **WHEN** the corpus is rendered by builds at different optimization
  levels of the same compiler with the mandated flags
- **THEN** the rendered output is bit-identical

### Requirement: Owned transcendental implementations
The audio path SHALL NOT call libm for transcendentals; `exp2`-family
(calcfreq/pow2), `sin` (v0 native-fsin gates), and `atan` (v0 waveshaper
and overdrive setup) SHALL be project-owned fixed implementations with
double-precision internals rounded once to float32, accurate to
correctly-rounded float32 (osc-frequency `fistp` tie sites are the
binding accuracy constraint).

#### Scenario: Transcendental accuracy at the freq-tie sites
- **WHEN** the owned exp2 path computes integer oscillator frequencies
  over the corpus note set
- **THEN** the resulting integers equal the x87-reference values
  (validated against the existing lab probes) for all corpus notes

### Requirement: Cross-host bit-identical output
The rendered output SHALL be bit-identical across supported hosts and
platforms given the same file, seed, and compiled version range; a
render hash SHALL serve as the verifiable contract.

#### Scenario: Two hosts, one hash
- **WHEN** the same corpus file is rendered with seed 0 on two different
  supported hosts
- **THEN** the output hashes are equal

### Requirement: Deterministic seeding
The engine SHALL seed the noise, sample-and-hold, and distortion state
that the historical engines seeded from `rdtsc` from a player seed with
default 0 (the C1-oracle reference convention); identical seeds SHALL
yield identical renders.

#### Scenario: Repeat render
- **WHEN** the same file is opened and rendered twice with the default
  seed
- **THEN** both renders are bit-identical

### Requirement: Subnormal arithmetic preserved
The player SHALL rely on IEEE-correct subnormal (denormal) arithmetic
(required by v0 envelope semantics) and SHALL verify at `open()` that the
floating-point environment does not flush subnormals, failing with a
distinct error if it does.

#### Scenario: Broken FP environment detected
- **WHEN** `open()` runs in an environment with flush-to-zero enabled
- **THEN** it returns the FP-environment error instead of producing
  silently wrong audio

### Requirement: Published ε against the historical oracles
The change SHALL record, per corpus file, the measured deviation (rms and
max|d|) of the portable render against the corresponding oracle render
(v0: C1 ground truth; v6: 2004-asm harness), and these measurements SHALL
be reproducible by a checked-in test driver.

#### Scenario: ε reproducibility
- **WHEN** the test driver is run against the corpus and oracles
- **THEN** it reports per-file rms/max|d| matching the published table

