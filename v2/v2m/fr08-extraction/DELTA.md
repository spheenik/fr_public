# fr-08 (year-2000) synth vs final (2004) V2 — behavioral delta

Working catalogue for the `characterize-fr08-synth-delta` change. Evidence
source: the depacked v1.01 image (`unpacked.bin`, md5
`2c1ebe7efac1000ba9a4be86f38c55b1`, minimal-PE memory image at base
0x400000; extracted v2m verified present at VA 0x415637), diffed against
2004 `v2/synth.asm`.

## Layout of the 2000 image

| VA | what |
|----|------|
| 0x40a400–0x40a460 | synth float-constant pool (25 entries, near-2004 order) |
| 0x40a464– ~0x40c000 | synth + player code (x87-dense, refs into the pool) |
| 0x40b2a2 | `syRvDefs` reverb gain table — **identical to 2004** |
| 0x415637 | the v2m music data (extracted → `../fr08.v2m`) |
| ~0x444100–0x444360 | **Ronan v0 data**: double-precision π family, formant tables, phoneme name strings (`ae`,`oe`,`mm`,`nn`,…) — ancestor of `phonemtab.h`. NOT a second synth (earlier const hits here were coincidence) |

## Method note

Disassembly is **`objdump -D -b binary -m i386 -M intel`** over the synth code
window only (`fr08_objdump.txt`, file off 0xa464–0xc800, ~2 s). Linear sweep,
re-synced from the constant xrefs below. Do NOT run Ghidra `analyzeAll()` on the
raw image — it treats the whole 3.2 MB blob as candidate code and hangs for
minutes. The region is dense hand-written x87; linear disasm is sufficient.

## RESOLVED: envelope (syEnvSet) — bit-exact gateable

2000 `syEnvSet` @ **0x40a6d1** is instruction-for-instruction identical to 2004
`syEnvSet` (`synth.asm:1071-1117`). **Exactly two** value differences:

1. **Attack coefficient** `fcattackmul`: 2000 = **-11/128** (-0.0859375, ref @
   0x40a6d4→0x40a448); 2004 = **-12/128** (-0.09375). kb left the old value as a
   comment in `synth.asm:86` (`fcattackmul dd -0.09375 ;-0.0859375`).
   `atd = 2^(7 − ar·11/128)` then, `·12/128` now.
2. **Decay/Release shaping**: 2000 decay (@0x40a6f5) and release (@0x40a72b)
   both `call 0x40a464` = **calcfreq** (×`fc10`, range 2⁻¹⁰‥1). 2004 routes both
   through **calcfreq2** (×`fc11`=`fccfframe`, range 2⁻¹¹‥1). `calcfreq2` did NOT
   exist in 2000 — `11.0` is absent from the synth pool (only image hit is inside
   Ronan data, coincidental). kb's `synth.asm:82` comment: `fccfframe dd 11.0 ;
   for calcfreq2`. **This is exactly what `transEnv` (v2mconv.cpp:243, disabled)
   tries to compensate** — remap old decay/release *values* onto the new ×11
   curve. A version-gated core fix (restore -11/128; route dec/rel via ×10) is
   **bit-exact** with no quantization, strictly better than the data-side
   transEnv. Sustain (pow2 @0x40a46e) and gain paths are byte-identical.

The pow2 helper @0x40a46e is `fld 2.0; fyl2x; <pow2 tail>` ≡ pow2 (the
2.0/fyl2x is an identity multiply by log2(2)=1).
2. **Noise LCG constants absent**: `icnoisemul`/`icnoiseadd`
   (196314165 / 907633515) appear nowhere in the image → the 2000 noise
   generator is a different algorithm (or differently seeded). Determinism
   hazard for the harness (task 1.7) until identified.
3. **No `fcdcoffset`** (2^-18 denormal-prevention offset) → the dcoffset trick
   is post-2000.
4. **No fastatan / sin polynomial coefficients** (`fcatan*`, `fcsinx*` doubles
   absent) → overdrive waveshaper and sine approximation differ structurally.
5. **No SR-flexibility constants** (`fcoscbase`, `fcsrbase`, `fcboostfreq`,
   `fcsamplesperms` absent) → 2000 synth is hardwired 44100.
6. **`fci256` (1/256) in pool where 2004 has `fci127`**: 2004 uses fci127 only
   in the decimator-dist coefficients (`synth.asm:1875,1879`); single 2000 code
   ref at 0x40ad44 — suspected ÷256 → ÷127 coefficient change (verify in
   listing).
7. **Unknown constant 3185015.0** at 0x40a45c (next to `fcmdlfomul`), gone by
   2004. One code ref at 0x40a4ca. Unidentified.
8. **transEnv** (`v2mconv.cpp:243`, disabled): kb's own old→new envelope
   decay/release translation, `curve_new = sqrt(curve_old)` in multiplier
   terms — consistent with the envelope tick rate doubling (e.g. frame length
   halving) at format v2. To be confirmed against the 2000 `syEnvSet`/
   `syEnvTick` disassembly.

## Full function sweep (2000 image → 2004 synth.asm)

51 functions in 0x40a464–0x40c800, segmented at call targets and aligned to
2004 by constant fingerprint (`fnmap.py` / `asm2004fp.py`). C = confirmed by
reading the disasm; F = aligned by fingerprint, body not yet line-diffed.

| 2000 VA | function | vs 2004 | conf |
|---------|----------|---------|------|
| 0x40a464 | calcfreq (×fc10) | identical | C |
| 0x40a46e/74 | pow / pow2 (fyl2x;2^x tail) | identical (eq. form) | C |
| — | **calcfreq2 (×11) does NOT exist** | added post-2000 | C |
| 0x40a485 | syOscInit | **seeds nseed = `rdtsc`** | C |
| 0x40a49b | syOscChgPitch | **freq = pow2((n−60)/12)·`3185015.0`** (baked 261.6256·2³²/44100/8); 2004 computes from fcoscbase at runtime | C |
| 0x40a4d2 | syOscSet | extra fci4/fc2 refs vs 2004 — verify | F |
| 0x40a585 | syOscRender | **sine = native x87 `fsin`** (2004 = fastsin poly fcsinx3/5/7); **noise LCG = MSVC rand `·214013+2531011`** (2004 = ·196314165+907633515); saw/tri/pulse phase trick identical | C |
| 0x40a6c9 | syEnvInit (zero state) | identical | C |
| 0x40a6d1 | syEnvSet | **attackmul −11/128; dec/rel via calcfreq ×10 not calcfreq2 ×11** | C |
| 0x40a759 | syEnvTick (state machine) | no float consts; verify immediates | F |
| 0x40a837 | syLFOSet (fci128, calcfreq) | verify | F |
| 0x40a935 | syLFOInit | **seeds S&H = `rdtsc`** | C |
| 0x40a945 | syLFO render / S&H (fci128 fc32bit) | uses MSVC-rand noise too — verify | F |
| 0x40a9f1 | sine/range-reduce helper (fc2pi fci2) | self-recursive; verify role | F |
| 0x40aa60 | syDistInit | **seeds rand = `rdtsc`** | C |
| 0x40aa93 | syDistSet | fp matches 2004 **minus fci127** | F |
| 0x40ab71 | syDistRenderMono (fci32768) | identical fp | F |
| 0x40acb3 | syV2Init | calls the 3 rdtsc inits | C |
| 0x40ad06 | param smoother | **coeff fci256 (1/256) where 2004 uses fci127 (1/127)** | C |
| 0x40ad4d | syV2Render (osc→flt→dist) | calls oscrender/distrender | F |
| 0x40ae5f | syV2Set (sets sub-objects) | calls oscset/envset/lfoset/distset | F |
| 0x40aef7 | syV2NoteOn/Tick (chgpitch) | verify | F |
| 0x40af88 | channel set | verify | F |
| 0x40b0c1 | chorus/flanger (fcmdlfomul fc1023) | no fc2/fcdcoffset vs 2004 | F |
| 0x40b157 | reverb comb/allpass (fc2) | verify | F |
| 0x40b317 | reverb/delay buffer render (sz586) | verify | F |
| 0x40b574 | compressor render (fcgain fcgainh) | no fc64/fcdcoffset vs 2004 | F |
| 0x40b67b | channel render wrapper | verify | F |
| 0x40b7ba | mix / soundsystem render (sz598) | verify | F |
| 0x40ba10 | outer render (sz380) | verify | F |
| 0x40bb8c | **player / ProcessMIDI** (sz3188) | sequencer; calls 0x4093xx (imports) | F |

### Bit-exact-gateable differences (confirmed)
1. Envelope: attackmul −11/128, dec/rel calcfreq ×10. (RESOLVED above.)
2. Osc base-freq constant 3185015.0 (vs SR-computed) — pin the constant.
3. Osc sine: `fsin` vs fastsin polynomial — emit `fsin` in gated path (host-FPU
   FSIN low bits are CPU-vendor-dependent; "bit-exact" is host-relative).
4. Noise LCG: MSVC `·214013+2531011` vs 2004 `·196314165+907633515`.
5. Param smoother coeff 1/256 vs 1/127.
6. No `fcdcoffset` (2⁻¹⁸ denormal offset) anywhere — added post-2000.

### Filter (VCF) — line-diffed, mostly SHARED
- `syFltSet` @0x40a837: `cfreq = calcfreq(cut/128)`, `res = 1−reso/128`. The 2004
  non-moog path is identical except `·SRfclinfreq` (= 44100/SR = **1.0** at
  native rate) → **bit-identical at 44100**. The `fc0p8/fc5p6/fci2` in the 2004
  fingerprint live ONLY in the `.moogflt` branch.
- `syFltRender` @0x40a880 + SVF core @0x40a8a6: **Chamberlin SVF, 2× oversampled**
  (`low += f·band; high = in − low − q·band; band += f·high`, twice/sample) —
  same algorithm as 2004. No constants (coeffs precomputed in set).
- **Mode jump table @0x40a860** (8 entries): off/low/band/high/notch/all =
  modes 0–5 present; **modes 6,7 (MoogL/MoogH) both alias to 0x40a8da = mode 0
  (passthrough)** → the moog ladder did NOT exist in 2000.
- Era-gated diffs: (a) moog modes absent, (b) render injects no `fcdcoffset`
  (2004 does). Otherwise filter is bit-identical at 44100.

### Reverb — line-diffed, SHARED topology
- `syReverbSet` @0x40b2d0: builds **6 feedback gains = `syRvDefs[0..5]` ^ decay**
  (`pow2` loop, `cmp cl,6`) where decay = f(64/(time+1)); plus highcut damping
  (`·fci128`). Same Freeverb-style **4-comb + 2-allpass** structure as 2004
  (lencl0-3/lenal0-1/lencr0-3/lenar0-1), **gain table byte-identical**.
- Diffusion @0x40b157: modulated/interpolated delay (own per-sample LCG → 0..1
  fraction, two-tap lerp) — present in both eras.
- Only param-level delta: **LowCut is v4-added** (absent in v0 fr08); conv2m
  default (0) handles it. No structural reverb divergence found.

### Chorus / channel chain / player — line-diffed
- `syChorusSet` @0x40b0c1: amount/feedback, delay L/R (`·fc1023`, ≤1023 samples),
  mod rate (`calcfreq·fcmdlfomul`), depth, phase. Standard modulated-delay
  chorus/flanger — shared with 2004.
- Channel set @0x40b574: chanvol (`·fcgain` 0.6), two FX sends (reverb+delay,
  `·fcgainh·chanvol`), fxroute, then channel-dist + chorus. **No compressor, no
  boost, no AuxA/B** — those are v1/v6 features and literally have **no code** in
  the v0 binary. Era-gate for v0 = leave them disabled (conv2m already defaults
  them Off); nothing to bit-match.
- `syOscSet` @0x40a4d2: mode, pitch (note+detune), gain `·fci4` (0.25), color→brpt
  `·fc32bit`, tri/pulse slope coeffs (fsqrt/reciprocal). Structurally standard;
  real osc delta is the `fsin` render, not set.
- **Player/ProcessMIDI @0x40bb8c**: 8-way MIDI status jump table + event loop
  (standard V2M sequencer; calls 0x4093xx imports). First action sets the x87
  control word: `and 0xf0ff; or 0x3f; finit; fldcw` → **PC=24-bit single
  precision, round-nearest, exceptions masked** — **byte-identical to 2004**
  (`synth.asm:4869-4875,5327-5333`). Not a delta, but a hard invariant the C1
  harness AND the gated core must honor (the faithful build already does).

### Net after filter+reverb sweep
The era-behavior surface is SMALLER than feared: filter & reverb are essentially
shared. Confirmed era-gated deltas remain the original list (envelope, osc
freq-const + `fsin` sine, noise LCG, smoother 1/256, global no-dcoffset) **plus
one**: VCF moog modes absent in 2000 (fr08 patches can't select them anyway).

### Determinism hazard (decisive for the whole goal)
**Three `rdtsc` seeds** (osc-noise, LFO S&H, dist) at every voice init
(0x40a494, 0x40a93e, 0x40aa6c). The 2000 synth is **non-deterministic by
design** for all noise/S&H content. Consequences:
- C1 harness MUST stub rdtsc to a fixed sequence to render reproducibly.
- Bit-exact reproduction of *the shipped fr08 playback* is **impossible** for
  noise/S&H (the original consumed the live TSC). Only deterministic content
  (tonal oscillators, envelopes, filters) can ever be bit-exact; noise/S&H can
  only match *a* deterministic re-seed, not the historical one.
- This is the 2000 ancestor of the port's known "rdtsc S&H seed" bug-class.

## Identical (binary evidence)

- `syRvDefs` reverb gain table (all 6 floats, contiguous, same order).
- Core pool constants & order largely match 2004 (fci12, fc2, fc32bit, fci128,
  fci4, fci2, fc2pi, fc64, fc32, fci16, fcipi_2, fc32768, fc256, fc10,
  fci32768, fc1023, fcOscPitchOffs, fcattackadd, fcsusmul, fcgain, fcgainh,
  fcmdlfomul).

## Conversion-layer checks (2004 core source)

- **Balance=64 default is a true no-op**: `f1gain/f2gain` only apply in
  `FLTR_PARALLEL` (`synth_core.cpp:2230-2236`) and both equal 1.0 at 64 —
  unweighted f1+f2 sum. Remaining check: 2000 parallel combine unweighted?
  (expected yes).

## Key code anchors in the 2000 image (from constant xrefs)

| VA | meaning |
|----|---------|
| 0x40a4b9 | ref `fcOscPitchOffs` — pitch calc |
| 0x40a6d6 / 0x40a6dc | refs `fcattackmul_OLD` / `fcattackadd` — **syEnvSet (2000)** |
| 0x40a712 | ref `fcsusmul` — syEnvSet sustain |
| 0x40a4ca | ref unknown 3185015.0 |
| 0x40ad44 | ref `fci256` — suspected decimator coeffs |
| 0x40b121 | ref `fcmdlfomul` — mod/LFO delay scaling |
| 0x40b57f / 0x40b591 0x40b5a5 | refs `fcgain`/`fcgainh` — main mix gains |

## Final delta list (era-gate surface for v0 fr08)

Faithful-modern by default; gate these to period behavior when source v2m is v0:
1. Envelope: attackmul −11/128 (not −12/128); dec/rel via calcfreq ×10 (calcfreq2
   ×11 didn't exist). [bit-exact]
2. Osc base-freq constant 3185015.0 (not SR-computed). [bit-exact]
3. Osc sine: native `fsin` (not fastsin polynomial). [host-FPU-relative]
4. Noise LCG: MSVC `·214013+2531011` (not 2004 ·196314165+907633515). [bit-exact]
5. Control-rate param smoother coeff 1/256 (not 1/127). [bit-exact]
6. No `fcdcoffset` (2⁻¹⁸ denormal offset) anywhere. [bit-exact]
7. VCF: moog modes 6,7 absent (alias to passthrough). [feature-gate; fr08 can't
   select them anyway]
Disabled-by-feature in v0 (no code; conv2m defaults Off): channel/sum compressor,
boost, AuxA/B busses.
Shared (verified identical): non-moog SVF filter @44100, Freeverb reverb + gain
table, chorus, x87 24-bit single-precision control word, calcfreq/pow2, osc
saw/tri/pulse phase gen, reverb syRvDefs.

## C1 harness recipe (period API — task 1.8)

The audio path is **self-contained within the image** — the only "imports" the
player hits (0x4093c5/d6/e7) are internal **Ronan** speech helpers (parse
phoneme markers `!`=0x21 / `_`=0x5f, index table @0x45ab90), NOT Win32. All
absolute refs (synth @0x40axxx, Ronan @0x4093xx, tables @0x45abxx, player
globals @0x592xxx, scratch @0x715xxx) lie inside `unpacked.bin` (0x400000–
0x72b000). So the loader is simple:

1. **Map** the whole `unpacked.bin` at base **0x400000**, one RWX region (32-bit
   process; on x86-32 Linux the host loads ~0x08048000 so 0x400000 is free —
   precedent: the `validate/` harnesses are already 32-bit). No per-import stubs.
2. **Pin rdtsc**: patch the 3 seed sites (0x40a494 / 0x40a93e / 0x40aa6c) `0f 31`
   → `31 c0` (xor eax,eax) → all noise/S&H/dist seeds init to 0, deterministic.
   Match the same fixed seed (0) on the gated-core side for the A/B.
3. **Open**: call `0x4099e3(v2m_ptr)` (cdecl) — the V2M parser. It reads
   timediv→0x592e00 (×10000=tpc→0x592e04), maxtime→0x592e08, gdnum→0x592e10,
   then the 16 channel streams; player state is a GLOBAL singleton at
   0x592dfc–0x59331c (mirrors `V2MBase`/`InitBase`). Feed it the **original v0**
   `fr08.v2m` (../fr08.v2m, the extracted one — NOT the converted file).
4. **Render**: drive the per-block render (calls ProcessMIDI @0x40bb8c for due
   events + the synth render chain @0x40ba10/0x40b7ba via relative calls).
   *Render entry not yet pinned* — it's the dsound mixing callback; locate by
   running the loader and stepping the open→play path (finalize in task 2.2).
   The synth render writes f32; the player FPU CW (24-bit single, set in
   ProcessMIDI) is established on entry.
5. **Determinism**: chunk-size invariance check; the FPU CW must be active
   (ProcessMIDI sets it) before any render.

Open question for the build: the dumped IAT holds stale Wine addresses — verify
the audio render path makes NO real Win32 call (initial evidence: it doesn't;
the only cross-refs are internal Ronan). If any surface, stub at those sites.

**MILESTONE 1 (DONE)** — `v2/validate/c1_fr08_harness.c` (gcc -m32 -no-pie):
maps unpacked.bin @0x400000, patches the 3 rdtsc sites, gates RONAN, calls
OpenV2M. Returns clean; header verified: timediv=480 tpc=4800000 maxtime=683530
gdnum=1. Two gotchas the build pinned down:
- **OpenV2M is a 2-arg stdcall** (`ret 8`); demo passes (v2m_ptr, 0). Call with
  exactly 2 args (inline asm) or the ret-8 corrupts the stack.
- **RONAN init is a 2-arg stdcall** at 0x409aad, args pushed @0x409a8c
  (`push [esp+0x10]`) and @0x409a9c (`push 0x40990e`). To skip it nop ALL THREE
  (both pushes + the call); nopping only the call leaves the args on the stack
  and OpenV2M's `ret 8` returns into the v2m data.

**MILESTONE 2 (DONE)** — full-song deterministic render. The player glue
(0x4094xx–0x409bxx, outside the original objdump window) resolved the whole
driver chain; no manual buffer setup needed (Reset does it all):

| VA | function | notes |
|----|----------|-------|
| 0x40940b | **Reset** | resets stream cursors (0x5923f0 area) + timing (cur 0x5923b4=0, next-event 0x5923b8=-1, tempo 0x5923c8=44100·5000, sig 4/4), then `synthInit(patch=[0x592df8])` @0x40b872 (`ret 4`; zeroes 0x11388 bytes @0x715c08, inits 16 voices/channels + reverb/delay/chorus) and `synthSetGlobals([0x592dfc])` @0x40be53. Called by both OpenV2M and PlayV2M. |
| 0x409b10 | **PlayV2M** () | stop → Reset → playing(0x5923b0)=1, paused(0x5923b1)=0 |
| 0x409b36 | StopV2M () | playing=0, paused=1 |
| 0x40990e | **RenderProxy(f32 \*buf, u32 n)** | stdcall `ret 8` — the dsound fill. Loops: render min(n, samples-to-next-event 0x592de8) via synthRender @0x40b923, then player tick @0x4095a5; next-event distance = (next−cur)·tempo(0x5923c8)/tpc(0x592e04) with remainder accumulation @0x592dec. Song end clears playing but leaves paused=0 → further calls render the reverb/delay tail. Full callee-save → plain stdcall fn-ptr call works. |
| 0x4095a5 | player tick | walks the 16 chans × (note/ctl×7/pgm/pitch) streams, assembles a MIDI buffer @0x593314 (running status, 0xfd terminator), calls **ProcessMIDI @0x40bbac**; advances song position 0x5923b4, handles tempo events (0x5923c8 = v·441), clears playing when next-event stays −1. |
| 0x409ae3/0x409ac2 | dsound position/latency helpers | not needed offline |

Results (`c1_fr08_harness [image] [out.f32] [chunk] [max_s]`): whole song
renders offline — **657.1 s + 6 s tail = 29 241 344 stereo f32 frames**, no
NaN/Inf, peak 1.007, rms 0.123. Chunk-4096 vs chunk-333 (independent
processes) **bit-exact over the common length** → run-to-run determinism AND
chunk invariance proven. ProcessMIDI survives the speech events with RONAN
init nop'd (speech silently absent — acceptable: our validation builds compile
RONAN out anyway). Ground truth: `/tmp/fr08/c1_fr08.f32` (+`.wav` listen
anchor), re-derivable any time by re-running the harness.

## Status

- [x] image verified, pool located & matched (objdump method; Ghidra not needed)
- [x] envelope set/tick deep-diff (transEnv hypothesis confirmed)
- [x] function-by-function sweep (51 fns; osc/env/flt/lfo/dist/chorus/chan/reverb
      line-diffed; player FPU + dispatch checked)
- [x] noise generator identification (MSVC rand LCG)
- [x] determinism hazard (3× rdtsc seeds)
- [x] period API surface (entry points) for the C1 harness — fully mapped
      (PlayV2M/RenderProxy/Reset/tick, table above); whole-song deterministic
      render verified (MILESTONE 2)
