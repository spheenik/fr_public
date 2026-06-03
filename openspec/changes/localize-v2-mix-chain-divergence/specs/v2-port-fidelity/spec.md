# v2-port-fidelity (delta)

## ADDED Requirements

### Requirement: Global mix chain matches the assembly reference

The C++ global mix chain that runs on `mixbuf` after all channels are summed
(`V2Synth::renderFrame`) — global reverb (`syReverb`), mod-delay aux2-to-main
(`syModDel`), DC filter (`syDCFilter`), the inline low-cut/high-cut one-poles, and
the sum compressor (`syCompProcChannel`) — SHALL, in the bit-faithful validation
build, produce `mixbuf` output that matches the assembly `synth.asm` after each
stage, within the component-equivalence tolerance, for identical channel-bus input
and global parameters.

#### Scenario: Each global stage matches when bus-tapped

- **WHEN** both the asm and C++ cores render the same song and `mixbuf` is
  snapshotted after each global stage (post-reverb, post-delay, post-dcf,
  post-lowcut/highcut, post-compressor)
- **THEN** every per-stage snapshot stream matches the asm within tolerance,
  identifying no stage as a divergence source

#### Scenario: Post-compressor snapshot equals the final mix bus tap

- **WHEN** the per-stage tap snapshots `mixbuf` after the sum compressor (the last
  global stage)
- **THEN** that snapshot is identical to the existing final-`mixbuf` bus tap on the
  same core (confirming the per-stage taps are placed at the correct boundaries)

#### Scenario: Whole-song magnitude drops after the responsible stage is fixed

- **WHEN** the global stage that introduces the residual divergence is corrected
  (port fix, or asm bug gated behind a `BUG_V2_*` define) and the whole-song A/B
  (`harness_asm` vs `harness_cpp`) is re-run on `pzero_new.v2m`
- **THEN** the max-abs divergence magnitude drops below the prior 0.0876, with no
  regression in the component oracles (`comp_osc` 0/4, `comp_flt` 0/7,
  `comp_leaves` 0)
