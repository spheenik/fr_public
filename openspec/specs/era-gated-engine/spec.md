# era-gated-engine Specification

## Purpose
TBD - created by archiving change portable-version-native-player. Update Purpose after archive.
## Requirements
### Requirement: Behavior deltas defined as a data table
The engine SHALL express every known behavior delta between the 2000 and
2004 synths as one row of a single, human-readable table
(`{delta id, flips-at-version, evidence level}`), and engine code SHALL
consult gates derived from this table rather than ad-hoc version
comparisons. The evidence level SHALL be one of: PROVEN (verified against
a period binary), ANCHORED (implied by the parameter version tables), or
ASSUMED (documented guess).

#### Scenario: Delta inventory covered
- **WHEN** the table is compared against the delta catalogue
  (`v2m/fr08-extraction/DELTA.md`)
- **THEN** every behavior delta in the catalogue has exactly one row
  (env curves + frame size, osc box-filter, noise LCG, native fsin/
  fpatan, dcoffset absence, crusher gain1 fold, PGM-change semantics,
  voice/master DCF absence, comp/boost absence, aux-bus absence, moog
  absence, reverb low-cut absence, TICK-before-SET, sub-frame rendering)

#### Scenario: Threshold edit changes behavior without code changes
- **WHEN** a delta row's flips-at-version value is edited
- **THEN** files on either side of the new threshold render with the
  corresponding era behavior, with no other code modification

### Requirement: Version-correct rendering at the proven endpoints
The engine SHALL reproduce v0 semantics for version-0 files and v6
semantics for version-6 files, as defined by the existing oracles, within
the published ε of the portable-determinism capability.

#### Scenario: v0 endpoint
- **WHEN** the original `fr08.v2m` is rendered with seed 0 at 44100 Hz
- **THEN** the output matches the C1 ground truth (`c1_fr08.f32`
  derivation) within the published ε budget

#### Scenario: v6 endpoint
- **WHEN** each modern corpus file is rendered with seed 0 at 44100 Hz
- **THEN** the output matches the corresponding 2004-asm harness render
  within the published ε budget

### Requirement: Mid-range versions render with documented threshold semantics
For files of versions 1–5, the engine SHALL apply each delta according to
its table threshold, and the table SHALL document the evidence level of
every such threshold so that unverified (ASSUMED) semantics are explicit,
not implicit.

#### Scenario: Mid-version file plays
- **WHEN** a version-3 file is opened (compiled-in range permitting)
- **THEN** it renders without error, with each behavior delta resolved by
  its table threshold

### Requirement: Behavior-version override
The player SHALL provide a runtime override to force the behavior version
independently of the detected format version (research instrument for
pinning ASSUMED thresholds).

#### Scenario: Forcing period behavior on a modern file
- **WHEN** a v6 file is opened with behavior version forced to 0
- **THEN** the engine renders it with all v0-era gates active

#### Scenario: Override bounded by compiled range
- **WHEN** the override requests a version outside the compiled-in range
- **THEN** open fails with a distinct error

