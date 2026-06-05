# Tasks: portable-version-native-player

## 1. Scaffold and portable math layer

- [x] 1.1 Create `v2/portable/` skeleton (headers, namespace, build script/
      CMake for the standalone library + a minimal CLI render driver) with
      the mandated FP flags (`-ffp-contract=off`, no fast-math) and a
      subnormal self-check helper
- [x] 1.2 Implement owned transcendentals (`v2math`): exp2/pow2 (calcfreq
      tail), sin, atan — double internals, single rounding to float32
- [x] 1.3 Verify transcendental accuracy against the lab oracles: exp2
      through the osc-freq fistp path vs `probe_freq`/x87 reference over
      the full corpus note/pitch set (integer freqs must match); atan vs
      `probe_atan`; document any residual tie cases
- [x] 1.4 Port `lrintf`-based fistp/fist equivalents and the float32
      arithmetic conventions doc (literal suffix rules, no-double policy)

## 2. Era delta table

- [x] 2.1 Write `v2eras.h`: delta enum + `{id, flipsAtVersion, evidence}`
      rows covering the complete DELTA.md inventory (env curves+frame256,
      osc box-filter, noise LCG, native fsin, native fpatan, no-dcoffset,
      crusher gain1 unfold, PGM-change v0, no voice DCF, no master DCF,
      no comp/boost, no aux busses, no moog, no reverb low-cut,
      TICK-before-SET, sub-frame render), with provenance comments
- [x] 2.2 Implement `deltaActive()` gating with `V2_VER_MIN`/`V2_VER_MAX`
      constant-fold semantics + the runtime `forceBehaviorVersion`
      override hook

## 3. Engine fork — v6 path green first

- [x] 3.1 Fork `synth_core.cpp` → `v2/portable/v2core.cpp`: strip
      `V2_X87_FAITHFUL` inline asm (route through v2math), strip
      VALIDATE-only taps, convert global instance state to per-instance
      struct (multi-instance requirement)
- [ ] 3.2 Re-route all existing `eraV0()`/`eraEnvOld()` call sites through
      the era-table gates (one delta id per site; audit against 2.1 list)
- [x] 3.3 Fork the sequencer (`v2mplayer.cpp` → `v2seq.cpp`): consume the
      internal canonical form, per-instance state, deterministic seed
      plumbing replacing rdtsc
- [x] 3.4 v6 milestone: render the 16 modern corpus files (loading
      via temporary direct-v6 parse) and record per-file rms/max|d| vs
      `harness_asm` renders; investigate anything above the ε expectation
      before proceeding
      (DONE: all 16 v6 files max|d| = 0 at 5s vs harness_cpp [proven
      bit-exact to the asm]; 60s spot checks: 2x exact, kkrieger6 1 ULP.
      Fixes en route: fastsin/fastatan per-op PC=24 rounding, V2Rand
      replacing libc rand() in LFO S&H, fistp 0x80000000 out-of-range.
      Formal per-file table vs harness_asm directly = task 8.1/8.4.)
- [x] 3.5 Chunk-size invariance test (4096 vs 333 frames, bit-identical)
      and render-before-open / two-instance safety checks

## 4. Native loader and canonicalization

- [ ] 4.1 Generate per-version patch/global size+offset tables from the
      `sounddef.h` parameter annotations (generator or carefully derived
      static tables + provenance comment); add the lab-build assert
      comparing them against `sdInit()` output
- [ ] 4.2 Implement `v2load`: structural fingerprint version detection +
      typed error results (BadFile / UnsupportedVersion with detected
      version)
- [ ] 4.3 Implement per-version parse + value-preserving canonicalization
      to the v6-layout internal representation (neutral defaults for
      missing params; MIDI verbatim)
- [ ] 4.4 Loader equivalence tests: v6 file canonicalization is identity;
      `fr08.v2m` (v0) canonicalization byte-equals `conv_v2m` output;
      corrupt-data rejection

## 5. v0 path end-to-end

- [ ] 5.1 Render the original `fr08.v2m` through loader+engine with
      seed 0; compare whole-song vs the C1 ground truth; record ε; chase
      any structural (non-transcendental) divergence to zero using the
      lab's localization tooling
- [ ] 5.2 Verify `forceBehaviorVersion` works both ways (v6 file at
      behavior 0; out-of-range request errors)

## 6. Ronan

- [ ] 6.1 Port `ronan.cpp` into `v2/portable/` behind `V2_RONAN`
      (default on), per-instance state
- [ ] 6.2 Verify `josie.v2m` (and kkrieger6 ch15) speech against the
      oracle within ε; verify the Ronan-disabled build plays josie with
      speech silent and contains no phoneme tables

## 7. Subsetting builds

- [ ] 7.1 Build matrix: full (0–6), v6-only, v0-only; verify in-range
      renders are bit-identical to the full build and out-of-range files
      are rejected with the detected version reported
- [ ] 7.2 Verify code elimination in single-version builds (era-specific
      symbols/constants of excluded versions absent from the binary)

## 8. Determinism and ε publication

- [ ] 8.1 Test driver (`v2/portable/test/`): renders corpus, computes
      per-file rms/max|d| vs oracle baselines and output hashes; checked-in
      baseline file
- [ ] 8.2 Cross-host determinism: render hashes equal on at least two
      hosts/arches (x86_64 + one other available target); document the
      hash contract
- [ ] 8.3 Optimization-level invariance check (-O0 vs -O2 bit-identical)
- [ ] 8.4 Publish the ε table (per-file rms/max|d| for v0 and v6 paths) in
      the change docs / portable README

## 9. CLI tools (user-added scope)

- [x] 9.0a `v2dump <in.v2m> <out.{wav,f32}> [secs] [chunk]`: offline render
      tool (renamed from the scaffold's v2play); .wav = IEEE-float32 RIFF
      with payload bit-identical to the raw .f32 compare format; mp3 out of
      scope by design (external encoder)
- [ ] 9.0b live playback tool `v2play` against ALSA (separate CLI linking
      libasound; libv2portable stays dependency-free) -- DEFERRED by user

## 10. Documentation

- [ ] 10.1 `v2/portable/README.md`: API usage, build flags policy
      (fast-math/FTZ prohibition), version support matrix, evidence
      legend for the era table, ε table, and the follow-up era-evidence
      research track (period-binary hunts) as future work
- [ ] 10.2 Update DELTA.md / handover cross-references to point at
      `v2eras.h` as the living threshold ledger
