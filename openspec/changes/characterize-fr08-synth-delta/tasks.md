## 0. Step A — version-table archaeology (done during exploration)

- [x] 0.1 Recover the param-level changelog from `sounddef.h` version fields +
      `v2mconv.cpp` conversion loop. → table in proposal.md; gap is mostly
      additive with neutral defaults.
- [x] 0.2 Identify admitted behavioral changes in conversion code. → `transEnv`
      (`v2mconv.cpp:243`), disabled envelope Decay/Release translation for
      `vdelta<2`; EQ never shipped (`//FICKEN`); speech copied through (Ronan
      v0 present in fr08).

## 1. Step B — static binary diff (2000 image vs 2004 synth.asm)

- [x] 1.1 Verify the usable live image. → `/tmp/fr08/unpacked.bin` (md5
      `2c1ebe7efac1000ba9a4be86f38c55b1`, base 0x400000; v2m present at VA
      0x415637). Recorded in DELTA.md.
- [x] 1.2 Extract the float-constant table from 2004 `synth.asm`. → `constscan.py`
      (parses dd/dq/equ → f32/f64/int bit patterns).
- [x] 1.3 Scan the image for those bit patterns; cluster → located synth pool
      @0x40a400 + code @0x40a464; flagged 2004-only constants (sin poly, fastatan,
      dcoffset, SR consts) absent in 2000. See DELTA.md.
- [x] 1.4 Disassemble the code region. → `objdump -D -b binary -m i386 -M intel`
      over file off 0xa464–0xc800 (`/tmp/fr08/fr08_objdump.txt`, ~2 s); re-synced
      from constant xrefs. (NOT Ghidra analyzeAll — hangs on the raw blob.)
- [x] 1.5 Deep-diff the **envelope** code. → `syEnvSet` @0x40a6d1 is identical to
      2004 except (a) `fcattackmul` -11/128 vs -12/128, (b) decay/release use
      `calcfreq` (×10) where 2004 uses `calcfreq2` (×11, which didn't exist in
      2000). This IS what transEnv compensates; a gated core fix is bit-exact.
      See `fr08-extraction/DELTA.md`.
- [x] 1.6 Sweep the remaining matched functions. → 51 functions mapped &
      aligned (`fnmap.py`/`asm2004fp.py`); confirmed diffs: osc base-freq const
      3185015.0, osc sine `fsin` vs fastsin poly, noise LCG MSVC 214013/2531011
      vs 2004, param smoother 1/256 vs 1/127, no fcdcoffset anywhere. Full table
      in `DELTA.md`. ~15 functions aligned-by-fingerprint still need line-diff
      (marked F).
- [x] 1.7 Determinism hazards. → **THREE rdtsc seeds** (osc-noise @0x40a494,
      LFO S&H @0x40a93e, dist @0x40aa6c) at every voice init. 2000 synth is
      non-deterministic by design for noise/S&H → C1 must stub rdtsc; bit-exact
      repro of the historical playback is impossible for noise/S&H content.
- [x] 1.8 Identify the period API. → Open = `0x4099e3(v2m_ptr)` cdecl (V2M
      parser; player state GLOBAL @0x592dfc-0x59331c). ProcessMIDI @0x40bb8c;
      synth render chain @0x40ba10/0x40b7ba. "Imports" 0x4093xx are internal
      Ronan, not Win32 → image is self-contained. Loader recipe (map blob
      @0x400000, patch 3 rdtsc sites) in DELTA.md. Render-to-buffer entry to be
      pinned by running, in task 2.2.
- [x] 1.9 Write findings to `v2m/fr08-extraction/DELTA.md`; analysis scripts
      (constscan/refscan/fnmap/asm2004fp/slice/dump_pool2.py) copied into
      `fr08-extraction/` and documented in its README.

## 2. Step C1 — ground-truth harness (run the genuine 2000 code offline)

- [x] 2.1 Loader: map the depacked image at 0x400000 in a 32-bit process, stub
      imports the synth path touches. **Stub `rdtsc` to a fixed constant** (the
      three seed sites @0x40a494/0x40a93e/0x40aa6c) so the render is
      deterministic AND so the seed can be matched on the gated-core side.
      → **MILESTONE 1 DONE**: `validate/c1_fr08_harness.c` maps unpacked.bin
      @0x400000 (gcc -m32 -no-pie), patches the 3 rdtsc sites, gates RONAN init
      (nop its 2 pushes @0x409a8c/0x409a9c + call @0x409aad — it's a 2-arg
      stdcall ret 8; leaving a push corrupts OpenV2M's ret), and calls OpenV2M
      via direct asm. OpenV2M returns cleanly; parsed header verified
      (timediv=480 tpc=4800000 maxtime=683530 gdnum=1). Remaining: set up audio
      buffers + drive render (2.2).
- [x] 2.2 Identify & call the period entry points (init/render/processMIDI/
      player tick — `0x40bb8c` is ProcessMIDI; trace its 0x4093xx imports) on
      the original v0 `fr08.v2m`; render deterministic f32 (44100, chunk
      invariance check). → **MILESTONE 2 DONE**: glue disasm pinned
      PlayV2M @0x409b10, RenderProxy @0x40990e (dsound fill, stdcall),
      Reset @0x40940b (→ synthInit @0x40b872 + synthSetGlobals @0x40be53),
      player tick @0x4095a5 (→ ProcessMIDI @0x40bbac). Harness renders the
      whole song (657.1s + 6s tail, 29 241 344 frames → /tmp/fr08/c1_fr08.f32):
      no NaN/Inf, peak 1.007, rms 0.123; chunk-4096 vs chunk-333 runs
      **bit-exact** over the common length (= determinism + chunk invariance);
      Ronan-gated ProcessMIDI survives the speech events. Listen anchor:
      /tmp/fr08/c1_fr08.wav.
- [ ] 2.3 (optional) dsound-proxy capture from the live demo under Wine — only
      as a coarse "sounds the same" anchor; NOT a bit reference (historical
      seed is unrecoverable, by design — non-goal).

## 3. Step D — era-gated compat (approved: gate the 7 confirmed deltas)

- [x] 3.1 Side-check: Balance=64 is a true no-op — `f1gain/f2gain` apply only in
      FLTR_PARALLEL (`synth_core.cpp:2230-2236`), both 1.0 at 64. Conv default OK.
- [x] 3.2 Era-flag plumbing: carry the source v2m format version from
      conversion → player → `synthInit`/per-voice init (conversion currently
      erases it). One runtime flag, not per-feature. → `V2Instance.srcVersion`
      (modern=6 default, predicates `eraEnvOld()` <2 / `eraV0()` <1) +
      `synthSetSourceVersion()` C-API (synth.h, libv2.h, no-op asm stub);
      `V2MPlayer::SetSourceVersion()` re-applied after every synthInit in
      Reset(); harness env `V2_SRCVER=<n>`; conv_v2m prints the value
      ("format v0 … render with V2_SRCVER=0"). Flag unset ⇒ josie 10s A/B
      asm-vs-cpp still max-abs 0 (no regression).
- [~] 3.3 Gate the confirmed deltas behind the era flag (DELTA.md list):
      envelope attackmul −11/128 + dec/rel calcfreq ×10; osc base const
      3185015.0; osc sine `fsin`; noise LCG 214013/2531011; param smoother
      1/256; drop fcdcoffset. Faithful-by-default; period behavior behind gate.
      → **DONE for all SYNTH deltas; ROOT CAUSE found.** The step-B list was
      reorganized once the deeper disasm landed (DELTA.md "CORRECTION"):
      - **Root cause = control frame 256 (2000) vs 128 (2004)**, not two
        separate "smoother 1/256" + "calcfreq" deltas. `synthSetSourceVersion`
        sets `SRcFrameSize=256`/`SRfciframe=1/256` under `eraEnvOld()`; the
        volramp coeff and env/LFO tick-rate follow for free.
      - **delta 1 envelope**: `V2Env::set` under `eraEnvOld()` → `fcattackmul_v0`
        (−11/128) + dec/rel via `calcfreq` (×10).
      - **delta 2 oscillator** is a FULL reimplementation, not a constant swap
        (2000 = 4×-oversampled numeric box filter, not the 2004 analytic
        convolution). `renderTriSaw_v0/renderPulse_v0/renderSin_v0/
        renderNoise_v0` + `syOscSet` coeffs + freq const `fcoscbase_v0`
        (3185015.0) under `eraV0()`. **VERIFIED BIT-EXACT** against the genuine
        2000 `syOscRender` in isolation — `validate/c1_osc_probe` (calls the
        2000 syOscSet/syOscRender in the mapped image vs `synthTestOscV0`); all
        7 cases (trisaw/pulse/sine/noise × colors/seeds) **max|d| = 0**.
      - **delta 3 sine** = native `fsin` (`v2_sin`); **delta 4 noise** = MSVC LCG
        214013/2531011 + exact 2000 LRC recurrence (both inside the osc, proven
        by the probe); **delta 6 dcoffset** dropped in the SVF render under
        `eraV0()`; **delta 7 moog** modes already unreachable in v0.
      - Seed-zeroing (osc/LFO → 0) under `eraV0()` to match C1's rdtsc=0.
      Modern path untouched throughout; flag-unset A/B asm-vs-cpp still max-abs 0.
- [~] 3.4 Validate per D6: matched-seed A/B (same fixed rdtsc stub both sides)
      gated core vs C1 → expect whole-signal max-abs 0, noise included.
      → **Two layers PROVEN bit-exact; residual localized to the synth voice
      chain.** (a) `c1_osc_probe`: eraV0 oscillator vs the genuine 2000
      syOscSet/syOscRender — all 7 cases **max|d|=0**. (b) `c1_timing_probe`:
      detours the 2000 sequencer tick, logs per-event (sample-pos, MIDI bytes)
      vs `v2mplayer_port` EVTRACE_RAW — over 204 events both the **event sample
      positions AND the MIDI bytes are identical (0 mismatches)**. So the PLAYER
      and CONVERSION layers are bit-exact (the earlier "growing-lag" reading was
      a cross-correlation artifact of phase-shifted quasi-periodic signals, NOT
      a timing drift). Whole-song A/B vs C1: rms 0.0588 (modern) → 0.0483
      (gated); the residual is now **100% synth-internal, downstream of the
      bit-exact oscillator** — voice chain (filter/dist/dcf/channel-mix),
      env/volramp, or per-note osc phase. Next: a voice-buffer tap (vcebuf
      @0x714fc4 / chan buf @0x7153c4) with the same probe methodology. Speech:
      RONAN off both sides (silent), not yet isolated.
- [x] 3.5 Line-diff the fingerprint-only (F) functions. → **DONE.** Filter:
      non-moog SVF bit-identical @44100, moog modes 6/7 absent. Reverb: shared
      Freeverb topology + identical gain table. Chorus: shared modulated delay.
      Channel chain: no comp/boost/aux (v1+ features, no code in v0). syOscSet
      standard (real osc delta is fsin render). Player FPU control word
      **byte-identical** (24-bit single precision) — shared invariant, not a
      delta. Final 7-item era-gate list + "shared" inventory in DELTA.md.
