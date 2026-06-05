# Spec: portable-player-api

## ADDED Requirements

### Requirement: Self-contained embeddable component
The portable player SHALL live in a self-contained directory
(`v2/portable/`) with no build or include dependency on the existing
`v2/` sources, `validate/` lab, or any external library beyond the C/C++
standard library, and SHALL NOT modify any existing component.

#### Scenario: Standalone compilation
- **WHEN** `v2/portable/` is compiled in isolation (only its own files)
- **THEN** it builds into a usable player library/objects

### Requirement: Open–query–render lifecycle
The API SHALL provide: `open(data, len)` returning a typed result;
`fileVersion()` reporting the detected format version after a successful
open; `play(fromMs)`; `render(float* stereoInterleaved, frames)` pulling
44100 Hz stereo float32; `isPlaying()`; and `setSeed(seed)`.

#### Scenario: Normal playback
- **WHEN** a valid corpus file is opened, `play(0)` is called, and render
  is pulled repeatedly
- **THEN** the file's audio is produced from the beginning, and
  `isPlaying()` turns false after song end (tail rendering still
  permitted)

#### Scenario: Version query
- **WHEN** `fr08.v2m` is opened successfully
- **THEN** `fileVersion()` returns 0

#### Scenario: Render before open
- **WHEN** render is called on a player with no successful open
- **THEN** it outputs silence and does not crash

### Requirement: Typed error results
`open()` SHALL distinguish at minimum: success, structurally invalid
data, version outside the compiled range (with detected version
queryable), and broken floating-point environment.

#### Scenario: Distinct errors
- **WHEN** each failure class is provoked (garbage data; out-of-range
  version on a subset build; FTZ environment)
- **THEN** open returns the corresponding distinct result code

### Requirement: Multi-instance, allocation-bounded operation
Player instances SHALL hold no shared mutable global state (multiple
independent instances in one process SHALL be supported), and after a
successful `open()` the render path SHALL perform no heap allocation.

#### Scenario: Two concurrent instances
- **WHEN** two player instances render different files in one process
- **THEN** both produce the same output as they would alone

### Requirement: Chunk-size invariance
Rendering SHALL be invariant to the pull chunk size: any partition of N
frames into render calls SHALL produce identical output bits.

#### Scenario: Different chunkings agree
- **WHEN** the same file is rendered in 4096-frame chunks and in
  333-frame chunks
- **THEN** the outputs are bit-identical over the common length
