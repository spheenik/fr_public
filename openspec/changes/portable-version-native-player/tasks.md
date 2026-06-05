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
- [x] 3.2 Re-route all existing `eraV0()`/`eraEnvOld()` call sites through
      the era-table gates (one delta id per site; audit against 2.1 list)
      (DONE: ~30 sites now `inst->old(DELTA_X)`; predicates removed. Audit
      found+fixed: 2 unledgered gates -> new rows DELTA_KEYSYNC_OSC_ONLY +
      DELTA_RVB_E_FULLPREC; NO_RVB_LOWCUT was mislabeled default-handled
      (engine-gated, canonical lowcut default is nonzero) and now flips at
      its anchored v4; NO_MOOG had a row but no gate -> moog aliases to
      passthrough; NO_AUX_BUSSES now also gates the AUXA/AUXB osc modes
      (was the v0 dispatch default). Osc dispatch split per-mode
      (boxfilter/fsin/LCG); static_assert ties OSC_FREQ_CONST to the
      4x-advance renderers. Verified: 16/16 v6 corpus files bit-identical
      to pre-refactor build @30s, twoinstance+mathcheck PASS, v6-only and
      v0-only subset builds compile.)
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

- [x] 4.1 Generate per-version patch/global size+offset tables from the
      `sounddef.h` parameter annotations (generator or carefully derived
      static tables + provenance comment); add the lab-build assert
      comparing them against `sdInit()` output
      (DONE: v2load.h -- per-param version annotations + canonical defaults
      transcribed with provenance; sizes computed constexpr the same way
      sdInit does (patchParmCount/patchSize/globalSize), static_asserts pin
      v0..v6 = 68/78/82/83/83/85/89 params + 12/21/21/21/22/22/23 globals
      (v3/v4 patch-size tie broken by globsize). test/tablecheck.cpp
      includes the lab sounddef.h read-only and verifies every row +
      sdInit-recomputed sizes: PASS. Note: reverb LowCut default is 32 --
      documents why DELTA_NO_RVB_LOWCUT must be engine-gated.)
- [x] 4.2 Implement `v2load`: structural fingerprint version detection +
      typed error results (BadFile / UnsupportedVersion with detected
      version)
      (DONE: v2load.cpp -- bounds-checked container parse (the lab's
      readfile with rejection instead of trust), used-patch walk +
      globsize/patch-size fingerprint (CheckV2MVersion method);
      UnsupportedVersion carries the detected version; Player::open now
      uses the native loader, temporary v6-only block removed.)
- [x] 4.3 Implement per-version parse + value-preserving canonicalization
      to the v6-layout internal representation (neutral defaults for
      missing params; MIDI verbatim)
      (DONE: ConvertV2M byte-replicated incl. quirks -- offsets[0]/4 patch
      count override, growing-bound mod-dest remap loop, always-written
      speech length, 4-byte zero tail. Canonical form IS the v6 file
      image; seq consumes it unchanged.)
- [x] 4.4 Loader equivalence tests: v6 file canonicalization is identity;
      `fr08.v2m` (v0) canonicalization byte-equals `conv_v2m` output;
      corrupt-data rejection
      (DONE: test/loadcheck.cpp -- all three PASS first run: fr08 v0->v6
      byte-equals v2m/converted/fr08.v2m, v6 identity (+zero tail) on 3
      files, truncation/garbage rejected. Pipeline smoke: v2dump on the
      ORIGINAL fr08.v2m detects v0 and renders -- first 14.77s BIT-EXACT
      vs c1_fr08.f32, 30s max|d| = 1 ULP @ full scale (1.19e-7, rms
      5.4e-9) = the expected transcendental-tie eps class, task 5.1's
      formal scope. 16/16 v6 corpus re-verified bit-identical vs pristine
      HEAD baseline through the new loader path.)

## 5. v0 path end-to-end

- [x] 5.1 Render the original `fr08.v2m` through loader+engine with
      seed 0; compare whole-song vs the C1 ground truth; record ε; chase
      any structural (non-transcendental) divergence to zero using the
      lab's localization tooling
      (DONE: whole 663s native render (v2dump, detected v0) vs
      c1_fr08.f32: first 14.774s BIT-EXACT; eps = max|d| 8.53e-7 (~3 ULP
      at full scale), rms 1.66e-8 -- under the ~1e-6 design budget.
      Structural-divergence verdict: NONE -- per-60s max|d| is bounded
      and section-correlated (1.2e-7..8.5e-7, no growth), and the final
      block (660-663s) is EXACTLY 0, i.e. all stateful units re-converge
      (an integer osc-freq flip would never re-converge); integer freqs
      already proven x87-matching in 1.3. Residual = the documented
      portable-vs-x87 transcendental razor-tie class.)
- [x] 5.2 Verify `forceBehaviorVersion` works both ways (v6 file at
      behavior 0; out-of-range request errors)
      (DONE: test/forcecheck.cpp -- force=6 on v6 bit-identical to
      normal; force=0 on v6 opens OK, renders differently (era gates
      flip), deterministic across reopens, detected version unchanged;
      force=V2_VER_MAX+1 and 99 -> UnsupportedVersion. PASS.)

## 6. Ronan

- [ ] 6.1 Port `ronan.cpp` into `v2/portable/` behind `V2_RONAN`
      (default on), per-instance state
      -- DEFERRED by user (2026-06-05): a DRAFT port exists
      (portable/ronan.cpp + phonemtab.h; __asm -> v2math kernels,
      per-instance state, V2_RONAN gate) and is deterministic /
      chunk-invariant / regression-clean (only josie+kkrieger6 ch15
      affected; fr08 v0 path untouched), BUT a listening test showed the
      josie voice is audibly off vs the original compiled ronan, and no
      speech-enabled oracle exists in-repo (validation harness assembles
      synth.asm RONAN-off; lab ronan.cpp is MSVC __asm). V2_RONAN now
      DEFAULTS TO 0 (the oracle-proven config). Resume by first building
      a reference rendering of josie's speech ("its compiled form"),
      then localizing the port's error against it.
- [ ] 6.2 Verify `josie.v2m` (and kkrieger6 ch15) speech against the
      oracle within ε; verify the Ronan-disabled build plays josie with
      speech silent and contains no phoneme tables
      -- DEFERRED with 6.1 (needs the speech oracle). Already verified
      on the disabled build: josie plays bit-exact to the speech-off
      oracle baseline and the binary contains no phoneme tables / ronan
      symbols (nm count 0, -9.3KB).

## 7. Subsetting builds

- [x] 7.1 Build matrix: full (0–6), v6-only, v0-only; verify in-range
      renders are bit-identical to the full build and out-of-range files
      are rejected with the detected version reported
      (DONE: v6-only == full on all 16 v6 files (10s); v0-only == full on
      native fr08; v6-only rejects fr08 "format v0 outside [6..6]" and
      v0-only rejects josie "format v6 outside [0..0]", both exit 3.)
- [x] 7.2 Verify code elimination in single-version builds (era-specific
      symbols/constants of excluded versions absent from the binary)
      (DONE: v6-only binary lacks fcoscbase_v0/fcattackmul_v0/MSVC-LCG
      constants; v0-only lacks 0xdeadbeef seeds + modern attackmul.
      Audit found comp/boost/sum-comp set() called unconditionally,
      keeping V2Comp::set alive in v0-only -- now gated on
      DELTA_NO_COMP_BOOST (unobservable: that state is only read by the
      gated renders; full-build A/B 16/16 + fr08 v0 re-verified exact).
      After the fix: 0 V2Comp/V2Boost symbols in v0-only.)

## 8. Determinism and ε publication

- [ ] 8.1 Test driver (`v2/portable/test/`): renders corpus, computes
      per-file rms/max|d| vs oracle baselines and output hashes; checked-in
      baseline file
      (driver DONE: test/check.py -- hash layer (anywhere) + eps layer
      (--oracle-dir, lab); baselines.sha256 checked in (60s, seed 0,
      chunk 4096). SCOPE RULING (user, 2026-06-05): converted/ files
      played at v6 behavior carry NO oracle-identity contract -- they
      are a lab construct for validating the engine fork, and most are
      not genuinely v6 anyway (originals span v0..v6). The portable's
      oracle eps covers ORIGINALS with their true era only: fr08.v2m
      (v0) vs C1 and pzero_new/v2_zeitmaschine_new (v6) vs harness_asm.
      Converted files remain in the hash baseline (determinism
      contract only). 60s results: fr08-v0-native rms 5.7e-9 / max
      1 ULP; pzero_new and v2_zeitmaschine_new EXACT ZERO. Informational
      only: 11/14 converted files also came out exact at v6 behavior;
      debris_ost/drumtro3/fr08conv differ from the asm after long
      renders around stolen-voice noteons (localization notes in the
      portable-seq-timing-bug memory) -- explicitly NOT a defect per
      the scope ruling.)
- [ ] 8.2 Cross-host determinism: render hashes equal on at least two
      hosts/arches (x86_64 + one other available target); document the
      hash contract
      (hash contract implemented + baselines.sha256 checked in; second
      host/arch run pending -- only x86_64 available this session)
- [x] 8.3 Optimization-level invariance check (-O0 vs -O2 bit-identical)
      (DONE: all 16 v6 files + native-v0 fr08, 10s renders, -O0 ==
      -O2 bit-exact)
- [ ] 8.4 Publish the ε table (per-file rms/max|d| for v0 and v6 paths) in
      the change docs / portable README
      (UNBLOCKED by the 8.1 scope ruling; table content ready: v0 path
      fr08 vs C1 rms 5.7e-9 / max 1 ULP @60s (whole-song in 5.1 note);
      v6 path pzero_new + v2_zeitmaschine_new vs harness_asm exact 0.
      Publish with the README in 10.1.)

## 9. CLI tools (user-added scope)

- [x] 9.0a `v2dump <in.v2m> <out.{wav,f32}> [secs] [chunk]`: offline render
      tool (renamed from the scaffold's v2play); .wav = IEEE-float32 RIFF
      with payload bit-identical to the raw .f32 compare format; mp3 out of
      scope by design (external encoder)
      (UPDATE 2026-06-05, user request: secs omitted (or "auto") now
      renders the WHOLE song + reverb/delay tail rung out to ~-90 dBFS
      and trimmed (same policy/caps as the lab harness "auto"); fixed
      secs stays the deterministic A/B mode. Verified: drumtro3 auto =
      173.3s + 9.5s tail, WAV length patched, prefix bit-identical to
      fixed-length renders.)
- [ ] 9.0b live playback tool `v2play` against ALSA (separate CLI linking
      libasound; libv2portable stays dependency-free) -- DEFERRED by user

## 10. Documentation

- [ ] 10.1 `v2/portable/README.md`: API usage, build flags policy
      (fast-math/FTZ prohibition), version support matrix, evidence
      legend for the era table, ε table, and the follow-up era-evidence
      research track (period-binary hunts) as future work
- [ ] 10.2 Update DELTA.md / handover cross-references to point at
      `v2eras.h` as the living threshold ledger
