# version-build-subsetting Specification

## Purpose
TBD - created by archiving change portable-version-native-player. Update Purpose after archive.
## Requirements
### Requirement: Compile-time version range selection
The build SHALL accept a supported version range (`V2_VER_MIN`,
`V2_VER_MAX`, defaults 0 and 6) and the engine's behavior gates SHALL be
expressed so that gates outside the range constant-fold, allowing the
compiler to eliminate unsupported versions' code paths from the binary.

#### Scenario: Full-range build
- **WHEN** the player is built with the default range 0–6
- **THEN** all corpus files (v0 and v6) load and render correctly

#### Scenario: Single-version gate folding
- **WHEN** the player is built with `V2_VER_MIN == V2_VER_MAX`
- **THEN** every behavior gate resolves at compile time (verifiable by
  inspecting that era-specific code of other versions is absent from the
  binary, e.g. v0-only symbols/constants absent in a v6-only build)

### Requirement: Subset builds are behavior-identical in range
For files within its compiled range, a subset build SHALL produce output
bit-identical to the full-range build (same seed, same host).

#### Scenario: v6-only build equivalence
- **WHEN** a v6-only build and a full build render the modern corpus
- **THEN** outputs are bit-identical per file

#### Scenario: v0-only build equivalence
- **WHEN** a v0-only build and a full build render `fr08.v2m`
- **THEN** outputs are bit-identical

### Requirement: Out-of-range files rejected cleanly
A subset build SHALL reject files whose detected version is outside its
compiled range with a distinct, queryable error (including which version
was detected), never by mis-rendering them.

#### Scenario: v0 file on a v6-only build
- **WHEN** `fr08.v2m` is opened by a v6-only build
- **THEN** open returns the unsupported-version error and reports the
  detected version 0

### Requirement: Optional feature compile-out
Ronan speech synthesis SHALL be independently compile-selectable
(`V2_RONAN`, default on); a build without it SHALL still play files that
use speech, with the speech channel silent, and SHALL not contain the
phoneme tables.

#### Scenario: Speech-enabled build
- **WHEN** `josie.v2m` is rendered by a Ronan-enabled build
- **THEN** the speech channel is audible and matches the oracle within ε

#### Scenario: Speech-disabled build
- **WHEN** `josie.v2m` is rendered by a Ronan-disabled build
- **THEN** playback succeeds with the speech contribution absent

