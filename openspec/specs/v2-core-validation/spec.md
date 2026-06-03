# v2-core-validation Specification

## Purpose

Validate that the C++ port of the V2 synthesizer core (`synth_core.cpp`) is sample-accurate against the original assembly core (`synth.asm`), by building both on Linux and comparing their rendered output for a set of `.v2m` test songs.

## Requirements

### Requirement: Dual-core render harness

The system SHALL provide a harness that renders a `.v2m` song to an in-memory buffer of stereo 32-bit float samples, with the V2 synth core selectable at build time between the assembly core (`synth.asm`) and the C++ core (`synth_core.cpp`), using `v2/v2mplayer.cpp` as the shared sequencer for both.

#### Scenario: Render with the assembly core

- **WHEN** the harness is built against the assembly core and run on a given `.v2m` file for a fixed sample count
- **THEN** it produces a deterministic buffer of stereo float samples and exits successfully

#### Scenario: Render with the C++ core

- **WHEN** the harness is built against the C++ core and run on the same `.v2m` file for the same fixed sample count
- **THEN** it produces a deterministic buffer of stereo float samples of identical length

#### Scenario: Identical inputs across cores

- **WHEN** both core builds are run on the same song, sample count, sample rate, and RONAN configuration
- **THEN** the only difference between the two runs is which synth core was compiled in

### Requirement: Linux-buildable assembly oracle

The system SHALL assemble `v2/synth.asm` on Linux as a 32-bit object (via `nasm`/`yasm`, `-f elf32`) and link it into the harness as the reference implementation, without requiring Windows, Wine, or any Win32 runtime.

#### Scenario: Assemble the core

- **WHEN** the build assembles `synth.asm` targeting 32-bit ELF
- **THEN** assembly succeeds and exposes the `synth*` entry points declared in `v2/synth.h`

#### Scenario: Word-size agreement between cores

- **WHEN** both the assembly oracle and the C++ core under test are built for a 32-bit (ILP32) target
- **THEN** `sU32` is 4 bytes in both, and the patch-data and instance struct layouts match between the two cores

### Requirement: Sample-accurate comparison and report

The system SHALL compare the assembly-core and C++-core render buffers sample-by-sample and emit a correctness report containing at least the maximum absolute error and, when the buffers differ beyond a stated tolerance, the index of the first diverging sample.

#### Scenario: Cores match

- **WHEN** the two render buffers are compared and the maximum absolute error is within the stated tolerance
- **THEN** the report states that the C++ core matches the assembly core, including the measured maximum absolute error

#### Scenario: Cores diverge

- **WHEN** the two render buffers differ beyond the stated tolerance
- **THEN** the report identifies the first diverging sample index and the maximum absolute error, so the divergence can be localized

#### Scenario: Multiple test songs

- **WHEN** the comparison is run across the designated set of `.v2m` test songs
- **THEN** the report records a per-song correctness result

### Requirement: Apples-to-apples speech-synth configuration

The system SHALL ensure the RONAN speech-synthesis path is configured identically for both cores in any given comparison run, and the baseline comparison SHALL use songs that do not exercise the speech synth.

#### Scenario: RONAN configured identically

- **WHEN** a comparison run is performed
- **THEN** both the assembly and C++ cores are built with the same RONAN setting (both enabled or both disabled)

#### Scenario: Non-speech baseline songs

- **WHEN** the baseline correctness run is performed
- **THEN** it uses `.v2m` songs that do not trigger the speech synth, so the result is not influenced by RONAN differences
