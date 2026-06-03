## ADDED Requirements

### Requirement: Per-block equivalence driver

The system SHALL provide a test harness that exercises a single V2 DSP block in isolation on both cores — initializing identical working state, applying an identical parameter blob, feeding identical input, and running the same number of samples or ticks — then diffs the two outputs and reports per-block max absolute error and the first diverging sample/tick.

#### Scenario: Single block, identical setup

- **WHEN** a block is tested with a given parameter blob, initial state, and input on both the asm and C++ cores
- **THEN** the harness runs the asm block routine and the C++ block method with byte-identical parameter and input data, and compares their outputs

#### Scenario: Per-block verdict

- **WHEN** a block's two output buffers are compared against the stated tolerance
- **THEN** the harness reports that block as MATCH or DIVERGE, with its max absolute error and (on divergence) the first diverging sample/tick index

### Requirement: Shared parameter contract

The harness SHALL feed each block the same `syV*` parameter struct bytes on both cores (the layout both implementations already share), rather than configuring the two sides independently, so that a divergence is attributable to the block's processing and not to differing inputs.

#### Scenario: Identical parameter bytes

- **WHEN** a block is configured for a test
- **THEN** the asm `sy*Set` path and the C++ `V2*::set` path receive the same `syV*` byte buffer

### Requirement: Identical global/sample-rate state

The harness SHALL initialize the sample-rate-derived global/instance state identically on both cores (at a fixed sample rate) before exercising any block, since blocks read sample-rate-dependent constants.

#### Scenario: Sample-rate constants established

- **WHEN** any block test runs
- **THEN** the asm sample-rate globals and the C++ `V2Instance` sample-rate fields have both been initialized at the same fixed sample rate before the block is driven

### Requirement: Leaf-before-aggregate localization

The harness SHALL test the leaf DSP blocks (oscillator, envelope, filter, LFO, distortion, DC filter, bass boost, mod delay, compressor, reverb) before the aggregate blocks (voice, channel), and report the first diverging leaf block before any aggregate result, so the root divergence is identified before it is masked by composition.

#### Scenario: Leaf blocks reported first

- **WHEN** the full block suite is run
- **THEN** the report orders leaf blocks ahead of aggregates and identifies the first diverging leaf block

#### Scenario: Aggregate corroboration

- **WHEN** a leaf block diverges and an aggregate that contains it also diverges
- **THEN** the report attributes the aggregate's divergence to the failing leaf rather than treating it as an independent failure

### Requirement: Asm block routines exposed without editing the original

The harness SHALL invoke the asm block routines (`sy*Init`/`sy*Set`/`sy*Render`/`sy*Tick`) by exposing them as global symbols in the generated asm copy used by the validation build, WITHOUT modifying the original `v2/synth.asm`.

#### Scenario: Routines callable, original untouched

- **WHEN** the harness links against the asm core to drive individual blocks
- **THEN** the required block routines are reachable as globals from the generated asm copy, and `v2/synth.asm` itself is unchanged

### Requirement: Localization report

The system SHALL produce a report containing an ordered pass/fail table across all tested blocks, the identity of the first diverging block, and the minimal input that reproduces that block's divergence.

#### Scenario: Report identifies the fix target

- **WHEN** the block suite completes with at least one diverging block
- **THEN** the report names the first diverging block, its max absolute error, and a minimal reproducing input, as the concrete target for the port-correctness work

#### Scenario: All blocks match

- **WHEN** every individual block matches but the whole-song A/B (Phase 1) still diverges
- **THEN** the report records that no single block is at fault and flags composition/routing (voice/channel mixing, FX ordering) as the remaining suspect
