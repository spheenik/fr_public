# v2m-version-native-loading Specification

## Purpose
TBD - created by archiving change portable-version-native-player. Update Purpose after archive.
## Requirements
### Requirement: Format version detection by structural fingerprint
The loader SHALL detect a v2m file's format version (0–6) from the file
contents alone, using the patch-size structural fingerprint (patch-offset
spacing matched against each version's parameter-table-derived patch
size), without filename hints, sidecar metadata, or user input.

#### Scenario: v0 file detected
- **WHEN** the original (unconverted) `fr08.v2m` is opened
- **THEN** the loader reports format version 0

#### Scenario: v6 file detected
- **WHEN** any file of the modern corpus (e.g. `pzero_new.v2m`) is opened
- **THEN** the loader reports format version 6

#### Scenario: Corrupt or non-v2m data rejected
- **WHEN** data that fails structural validation is opened
- **THEN** the loader returns a distinct error result and the player
  remains in a safe, idle state (no partial load)

### Requirement: Native parsing of every supported format version
The loader SHALL parse the patch bank, globals, and MIDI streams of any
format version within the compiled-in version range directly, with the
per-version layout derived from the parameter version annotations (the
single source of truth in `sounddef.h`).

#### Scenario: Old layout parsed without external tools
- **WHEN** a v0 file is opened
- **THEN** playback proceeds without any prior conversion step, external
  tool invocation, or environment variable

### Requirement: Value-preserving canonicalization
The loader SHALL canonicalize parsed patches and globals to the single
internal (v6-layout) representation by inserting neutral defaults for
parameters that did not exist in the source version, and SHALL NOT remap,
rescale, or otherwise alter any parameter value present in the source
file. MIDI data SHALL be taken verbatim.

#### Scenario: Canonicalized v6 file is the identity
- **WHEN** a v6 file is loaded
- **THEN** the internal patch representation is byte-identical to the
  file's patch data

#### Scenario: v0 canonicalization matches the proven conversion
- **WHEN** the original `fr08.v2m` (v0) is loaded
- **THEN** the internal representation equals the output of the proven
  `conv_v2m` conversion of the same file

### Requirement: Source version retained for behavior gating
The loader SHALL retain the detected format version as the engine's
`srcVersion` so that engine behavior gates switch on it; canonicalization
SHALL NOT erase version identity.

#### Scenario: Version reaches the engine
- **WHEN** a v0 file is loaded and rendered
- **THEN** the engine renders with v0-era behavior gates active (not
  modern behavior)

