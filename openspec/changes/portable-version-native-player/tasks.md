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

## 6. Ronan + v5 era alignment (candytron oracle)

SCOPE ADDED by user (2026-06-05): align the portable's v5 path and the
Ronan port against the genuine fr-030 candytron final binary (the
compiled period synth+ronan that josie was authored for), per the fr08
C1 playbook. Source: ~/downloads/fr-030_candytron_final.zip (assayed:
fr030-candytron-final-101.exe, 65536 bytes, PE32, kkrunchy-packed,
single "kkrunchy" section 0xf000, 2003-08; working copy /tmp/candytron).
This un-defers 6.1/6.2 by supplying the missing speech oracle.

- [x] 6.0a Unpack the candytron binary: static depack (in-repo kkrunchy
      sources; packer string "kkrunchy5") or runtime memory dump under
      Wine; record provenance (zip sha256, "final 1.01", 2003-08) and
      stash the unpacked image
      (DONE: Wine here is wow64-only and never exposes the guest at
      0x400000, so the fr08 live-dump recipe is out. Instead a
      self-contained 32-bit loader harness `c2_unpack.c` maps the packed
      exe flat at 0x400000, jumps the kkrunchy stub (entry 0x40fdc3),
      lets it decompress in-place + run the E8 call-fixup, and dumps the
      finished image when the post-decompress import resolver faults on
      the unpatched LoadLibraryA placeholder (dword @0x40ffa2 = 0xffba ->
      SIGSEGV). Output `/tmp/candytron/unpacked.bin` = 0xb5f000 bytes;
      verified complete (.text @0x411000 is clean post-fixup x86, ~5.5%
      nonzero over first 2MB = right for a 64k). Provenance + stub
      anatomy in v2/v2m/candytron-extraction/{NOTES.md,c2_unpack.c}.)
- [x] 6.0b Extract the embedded josie v2m (signature/heuristic scan of
      the unpacked image) and cross-check against the in-repo v5
      original (da8e5cb / current tree): byte-equality or documented
      diff
      (DONE: data-store name `josie_.v2m` @0x4084c; v2m payload @dump
      0x24bf6 (VA 0x424bf6) behind a 10-byte tag `*VM\0\0\0\0ryg`, stream
      proper at +10 = `e0 01 00 00` (timediv 480, == repo). NOT
      byte-identical to repo josie.v2m/josie_data.v2m: header diverges
      right after timediv (embedded `3228 0300 01..` vs repo `8004 0300
      02..`) but a large interior body matches (~1.4KB contiguous).
      => candytron ships a DIFFERENT export of the same song; the 6.0d
      oracle and 6.2 portable run must both use candytron's own v2m, not
      the repo copy. Exact payload length + `*VM..ryg` tag semantics
      deferred to 6.0c. 64KB record window: josie_candytron.v2mc.)
- [x] 6.0c Locate the synth + ronan code in the unpacked image and
      build a C2 harness (fr08 C1 playbook): offline render of josie
      WITH SPEECH, deterministic (rdtsc neutralized / seed pinned),
      chunk-invariant
      (RECON DONE, harness TBD -- 2026-06-06. Full entry-point map in
      candytron-extraction/NOTES.md "6.0c reconnaissance". Synth core
      localized 0x41dc00..0x41e800 (calcfreq 0x41dc78, 2 rdtsc seed
      sites 0x41dca8/0x41e32d = determinism patch points, voice init
      0x41e75b). Synth Init/Open @0x41f7e4 (zeroes ~2MB state @0x4c2ccc,
      OpenV2M candidate @0x4144c4, inits 32 voices stride 0x228 @0x4c4be0).
      Control surface: g_player @0xeba32c, g_v2m @0xeba344=0x424c00,
      sound-cmd dispatcher @0x41cfb4 (cmd2=init+play). KEY FINDING:
      unlike fr08's clean RenderProxy(buf,n), candytron is werkkzeug3 --
      the V2 synth is wrapped in operator objects driven by a
      timeline-synced ring buffer + DirectSound thread. ENGINE NOW
      FULLY MAPPED (NOTES.md "6.0c engine map"): synthInit(patch,44100)
      @0x41f7e4, synthSetSampleRate @0x4144c4, synthSetGlobals @0x41fe84,
      and **synthRender @0x41f8c2** -- confirmed by its x87 PC=24 setup
      (and ax,0xf0ff; or ax,0x3f; fldcw) and 32-voice loop; it self-ticks
      the sequencer (@0x41fa2e, countdown @0x4c2e5c). Remaining wiring
      gap: the patch/globals "song descriptor" [0x6a7fd8]/[0x6a7fdc] is
      struct-filled by the high-level *VM-container parse (v2m @0x424c00)
      -- harness must either call that parse or set the ptrs by hand.
      HARNESS BUILT (c2_render.c): maps image, pins rdtsc, stubs malloc
      IAT; ONE call to 0x414140 does the whole synth init (parse +
      synthInit + synthSetGlobals + playerOpen) -- verified correct; then
      loops synthRender 0x41f8c2. Synth + RONAN SPEECH both initialize and
      tick (found the phonemes in the v2m: "!kah_m !fao_r !miy_" =
      "come for me" @v2m+0x9733; 0x6a8900 = the speech sub-player).
      REMAINING BLOCKER: song is silent -- synthRender ticks only the
      speech player; the NOTE sequencer (separate obj, the cmd2/g_player
      path) and the timeline clock (refill 0x413b62 at tempo, which
      gates playback via the player[0x150] wait flag cleared by 0x41487e)
      still need wiring. Detail in NOTES.md "6.0c harness".
      UPDATE 2026-06-06: SOLVED -- josie RENDERS from candytron's synth.
      Note path = ProcessMIDI @0x41fbdc, driven by refill 0x413b62; driver
      per 128-frame: set clock [0x6a7598]=cumulative samples, call refill
      (triggers voices), call synthRender. AUDIO confirmed (rms 0.07..0.35).
      SOLVED via SOURCE (user pointed to genthree/ = Candytron source):
      _viruz2.cpp has the exact player (smpldelta=(nexttime-time)*usecs/
      timediv2, usecs=val*441). c2_oracle.c ports ssInitBase/ssReset/ssTick/
      ssRender VERBATIM, drives the binary synth VAs + synthSetLyrics
      @0x414ab7 for RONAN. Authentic render, no speed hacks. USER CONFIRMED
      embedded josie sounds correct; ronan now wired (render diff 0.738 vs
      no-ronan). c2_embedded_ronan.wav = demo josie + genuine player +
      speech. Note: demo-embedded josie (VA 0x424c00) != source josie
      (gdnum 1 vs 2); render embedded for demo-match. NEXT 6.0d:
      determinism + chunk-invariance.)
- [x] 6.0d Render the whole-song speech-enabled ground truth
      (c2_josie.f32) + determinism/chunk-invariance checks; this is the
      v5/Ronan oracle
      (DONE 2026-06-06: c2_oracle.c renders the demo-embedded josie WITH
      Ronan speech, USER-CONFIRMED correct (music + speech). Determinism
      + chunk-invariance PASS: identical sha256 across 2 runs and I/O
      chunk 333/2048/4096 (rdtsc pinned). This IS the v5/Ronan oracle.
      Use the EMBEDDED josie (josie_embedded.v2m, sha256 6b2b3fc9...) for
      demo-match; source josie.v2m is a different export (gdnum 2 vs 1).)
- [x] 6.0e v5 era assay from the binary: read off every ASSUMED
      v2eras.h row's state at the candytron era (moog? fastsin/fastatan
      vs native fsin/fpatan? dcoffset? crusher gain1 fold? voice/master
      DCFs? keysync? ...) and update thresholds/evidence — a third
      proven anchor (v5) between the v0 binary and v6 asm; cross-check
      against the RG2/ViruzII + RG2/Viewer period sources where they
      disagree
      (DONE 2026-06-06: full assay in candytron-extraction/NOTES.md
      "6.0e". genthree _viruz2a.asm == RG2/ViruzII == RG2/Viewer
      byte-identical (one v5 source, no disagreements); source==binary
      verified by 6 disasm signatures. 10 rows OLD at v5 -> flip pinned
      at exactly v6, EV_PROVEN (env-clamp, fsin, fpatan, dcoffset,
      crusher-split, voice/master DCF, moog, keysync, aux-busses); 7
      rows already NEW at v5 -> stay flipsAt 1 ASSUMED, gap narrowed to
      v1-v4; all 4 anchored rows consistent. Engine edits: sine eval
      decoupled from the 4x-advance convention (renderSin_v0 step from
      OSC_FREQ_CONST); NEW ROW DELTA_NO_FM_OSC -- the 2000 oscjtab maps
      mode 5/6/7 to OFF (no FM at v0; the lab's "FM shared" was
      unexercised conjecture) + renderFMSin_v5 (integer-mod fistp +
      native fsin, the v5 scheme). Regression: 17/17 baseline hashes
      unchanged, all 5 unit tests PASS.)
- [ ] 6.1 Ronan port behind `V2_RONAN`, per-instance state — UN-DEFERRED
      (2026-06-05) now that 6.0c/d provide the oracle. History: a DRAFT
      port of the 2004 ronan exists (portable/ronan.cpp + phonemtab.h;
      __asm -> v2math kernels) and is deterministic / chunk-invariant /
      regression-clean, but the josie voice was audibly off and
      V2_RONAN was defaulted to 0 pending an oracle. First step now:
      diff the period ronan (candytron image + RG2 era sources) against
      the 2004 ronan.cpp to decide whether the draft port is buggy or
      simply the wrong-era voice; port/gate per the evidence
- [ ] 6.2 Align portable vs the C2 oracle: render the ORIGINAL
      josie.v2m (v5, native loader) and compare vs c2_josie.f32;
      localize structural divergence to zero with the lab toolkit
      (CHANSOLO/ledgers/BUSTAP), transcendental-tie residual documented
      as ε (same rules as the fr08 v0 path); re-verify the
      Ronan-disabled build still plays josie speech-silent with no
      phoneme tables in the binary (was verified on the draft: nm
      count 0, -9.3KB). kkrieger6 ch15 rides as informational (no
      kkrieger oracle in this change)
      (IN PROGRESS 2026-06-06: localization started, candytron-extraction/
      NOTES.md "6.2". Method: channel-solo both sides (oracle C2_SOLO,
      portable V2SEQ_SOLO) + per-section asm diff. First onset divergence =
      ch7. BUG FOUND+FIXED: osc-noise/LFO-S&H seed was coupled to
      DELTA_NOISE_LCG_MSVC -> 2004 fixed seed table, but v0 AND v5 seed from
      rdtsc(->0); the table is a v6 add. New row DELTA_RDTSC_SEED {6,PROVEN}
      decouples it. Whole-song josie rms|d| 0.163->0.127 (-22%), max
      1.31->0.79; v0/v6 baselines unchanged, tests PASS.
      2ND FIX: DELTA_CC6_HICUT {6,PROVEN} -- ch15 CC6 -> master hicut
      (sqr((val+1)/128)); v0(fr08 @0x40bded)+v5 have it, 2004 dropped it.
      Correct for v0/v5 but josie sends no ch15 CC6 -> 0 effect on josie's #
      (completeness addition). VOICE DSP + PLAYER PROVEN BIT-EXACT vs the
      candytron binary (direct voice-array tap 0x4c4be0: osc freq/cnt,
      envelope, volramp all identical; v2seq == genthree _viruz2.cpp). So the
      josie residual (rms 0.127) is in the per-channel FX / global mix path
      (CC1-mod aux2/delay sends, master lc/hc EQ, chgain), NOT the voice.
      Full handover + tooling + NEXT STEP (tap the binary's chanbuf/aux/mixbuf
      stages) in candytron-extraction/NOTES.md "RESUME HERE". NOT yet to-eps.)

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
