# fr-08 (year-2000) synth vs final (2004) V2 — behavioral delta

> **STATUS 2026-06-05 (eod): fr08 WHOLE SONG (663 s) is BIT-EXACT vs the
> genuine year-2000 binary EXCEPT a single 7.4 s decaying transient at
> 271.64–279.03 s** (max|d| 0.0025, rings out to 0; the last 384 s and
> everything before 271.6 s are bit-exact, reverb/delay tail included). The
> day's findings, in order: (1) the 66.8 s "ch3 osc inversion" was a
> tap-window alignment artifact AND **8 corrupted bytes in the EXTRACTED v2m**
> (re-extracted from the image-embedded original — data, not synth);
> (2) ch2 @67.05 s = **env DECAY-clamp placement** era delta (syEnvTick
> section below), gated; (3) ch15 @118.06 s = the C1 harness/probe were
> running Ronan's speech PROCESS on ch15 with uninitialized state (now nop'd —
> the port has no Ronan); (4) ch1 @192.09 s = **PGM-change era delta** (no
> same-pgm early-out + ctl7→127 reset; PC handler section below), gated.
> The remaining 271.6 s transient is the **mid-frame channel-activation
> chorus-phase facet** (ch1 staccato-through-chorus; dry input bit-exact,
> chorus mod/delay phase drifts then flushes) — see `HANDOVER-fr08-sweep.md`.
> Modern (≥v1) stays bit-exact vs the 2004 asm (5-song gate green). Earlier
> fixes: gated **sub-frame rendering** (4 facets), **reverb gain PC=64
> precision** + SetSourceVersion reorder, **reverb low-cut gate**.

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

## syEnvTick (2000) decoded — clamp placement delta (2026-06-05, GATED)

`syEnvTick` @0x40a759: 5-state jump table @0x40a745 → handlers OFF 0x40a76f /
ATTACK 0x40a781 / DECAY 0x40a7a8 / SUSTAIN 0x40a7cd / RELEASE 0x40a803. All
transitions are operation-identical to the port's `V2Env::tick`
(synth_core.cpp ~1573): gate-off from ATTACK/DECAY/SUSTAIN jumps to the
RELEASE handler and applies `val *= ref` the SAME tick; gate-on from
OFF/RELEASE applies `val += atd` the same tick; attack clamps at 128→DECAY;
decay floors at sul→SUSTAIN; sustain ceilings at 128.

**The one structural difference: the `val ≤ 2^-13 (0x39000000) → val=0,
state=OFF` clamp exists ONLY in the SUSTAIN (@0x40a7e2) and RELEASE
(@0x40a819) handlers.** The port applies it globally after every state
(synth_core.cpp:1623), including DECAY and ATTACK. Consequence: with
`sl < 2^-13` and the gate held, the 2000 env NEVER leaves DECAY — val decays
multiplicatively into denormals, the voice stays allocated and its osc/filter
state keeps advancing — while the port kills the env (→OFF) and the voice.
(ATTACK can't underflow in practice: atd ≥ 2^-4 over the whole ar range.)

Status: **GATED 2026-06-05** — `V2Env::tick` skips the clamp in DECAY/ATTACK
under `eraV0()` (synth_core.cpp ~1623). The 2004 asm DOES reach the clamp from
DECAY (`.state_dec` → `.s4checkrunout`, synth.asm:1178), so the modern path is
already faithful-2004; the runout-from-DECAY check was added between 2000 and
2004. Proven culprit of the fr08 ch2 (pgm 3) divergence @67.05 s: gate-held
env2 with sul<2^-13 feeds env2→osc-pitch mods; the 2000's denormal-decaying
env2 keeps a residual pitch offset (chgPitch trace: pitch 41000008 vs
41000000 → integer freq +2 → slow phase drift). Note `c1_env_probe`'s
"bit-exact" verdict did NOT exercise this edge (gate-held sub-clamp DECAY).
(ATTACK leg of the gate is belt-and-braces: atd ≥ 2^-4 > 2^-13, unreachable.)

## ProcessProgramChange (2000) — two deltas, GATED (2026-06-05)

2000 PC handler @**0x40be15** vs 2004 `ProcessProgramChange` (synth.asm:5685):

1. **No same-program early-out.** 2004 does `cmp al,[edi]; jz .sameprg` and
   skips the voice-kill when the program is unchanged. The 2000 has NO such
   check — EVERY PC event (even a reload of the current pgm) runs the
   not-aus loop (`chanmap[v]==chan → −1` for all voices).
2. **Controller reset writes ctl7 = 127.** Both zero ctl1–6
   (`mov cl,6; rep stosb`), but the 2000 then does `mov al,0x7f; stosb`
   (@0x40be46) — ctl7 (channel volume) snaps back to MAX. 2004 dropped that
   store (ctl7 preserved).

Proven by fr08 ch1: pgm2 reload @190.12 s with ctl7 sitting at 109; the 2000
resets ctl7→127, and ch1's `ctl7→chanvol` mod (src7→dest59) then yields a
different chgain → a **constant DC offset** in the dry mix from the next ch1
note-on (192.086 s). Gate (eraV0, `processMIDI` case 4): drop the same-pgm
early-out and set `chans[chan].ctl[6]=127` after the ctl1–6 reset.

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
| 0x40a759 | syEnvTick (state machine) | **clamp only in SUS+REL** (see syEnvTick section) | C |
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
| 0x40b157 | ModDel per-sample core (chorus+delay; triangle mod, lerp-frac mantissa trick — NOT an LCG) | identical; covered by c1_chorus_probe | C |
| 0x40b317 | reverb render (combs+allpasses) | **line-diffed identical** (see reverb section) | C |
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
- ~~Diffusion @0x40b157: own per-sample LCG~~ **CORRECTED (line-diff)**:
  0x40b157 is the shared **ModDel per-sample core** (called by the channel
  chorus @0x40b287 AND the global delay @0x40b248 — NOT part of the reverb).
  Its modulator is the deterministic triangle trick (`shl/sbb/xor` on
  mcnt+mphase, == 2004 V2ModDel), and the `shr eax,9; or 0x3f800000` is the
  **interpolation fraction** of the modulated offset built via the
  mantissa-compose idiom (same idiom frandom uses — hence the misread). There
  is NO LCG and NO diffusion stage anywhere in the 2000 reverb or delay.
- Only param-level delta: **LowCut is v4-added** (absent in v0 fr08); conv2m
  default (0) handles it. No structural reverb divergence found.
- **Render @0x40b317 fully line-diffed (2026-06-05)**: input bus @0x7147c4
  (no dcoffset — gate already in place), per comb `lp = lpf + damp·((dv·gainc
  ± in) − lpf)` with alternate phase (+,−,+,−) and the **persisted lowpass**
  (`fst lpf` AND `fst line` — the 2004-asm behavior the port's PORT FIX
  restored), classic 2-allpass tail, output `+=` into dest. **All 12 delay
  lengths byte-match 2004**: L combs 1309/1635/1811/1926 (cmp 0x51d/0x663/
  0x713/0x786), L allpass 220/74 (0xdc/0x4a); R combs 1327/1631/1833/1901
  (0x52f/0x65f/0x729/0x76d), R allpass 205/77 (0xcd/0x4d). No lowcut hpf
  stage (lowcut=0 makes the 2004 stage an exact no-op). ⇒ reverb render is
  algorithm-identical; remaining tail suspects are only the FX-send glue
  (@0x40b67b) and set-time params.

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

## CORRECTION (phase D, deeper disasm): root cause + osc is an algorithm change

Implementing the gates surfaced two findings that **correct/deepen** the
step-B list above (which under-characterized the osc and split one root cause
into two symptoms):

### ROOT CAUSE: control-frame size 256 (2000) vs 128 (2004)
The render driver @0x40b95c resets the control-frame counter `[0x715d14]` to
**0x100 = 256** (`mov DWORD PTR ds:0x715d14,0x100` @0x40ba01) and renders
`min(remaining, frame_left)` samples per block; when `frame_left` hits 0 it
ticks the per-channel control update @0x40ad06 (env/LFO tick + volramp) and
reloads 256. The 2004 core uses `SRcFrameSize = round(128·SR/44100) = 128`.
**This single halving is the root of TWO step-B "deltas":**
- the volume-ramp / "param smoother" coeff: 2000 `volramp = (target/128 −
  cur)·(1/256)` @0x40ad33 == `(Δ)·(1/frame)`; 2004 uses `·SRfciframe` = 1/128.
  So delta 5 is **not** "1/256 vs 1/127" — it's `1/frame`, and follows for free
  once the frame size is 256.
- the envelope decay/release shaping: env ticks **once per 256-sample frame**
  (half as often as 2004's 128). `calcfreq ×10` per 256-frame ≈ `calcfreq2 ×11`
  per 128-frame — which is exactly kb's `transEnv` `sqrt`/frame-doubling
  hypothesis, now confirmed structurally. Delta 1's constants are necessary but
  only correct **together with** frame=256.

Gate (this change): `synthSetSourceVersion` sets `SRcFrameSize=256`,
`SRfciframe=1/256` under `eraEnvOld()` (post-init, pre-render; voices read both
live). Measured: env+frame gate moves whole-song rms-vs-C1 from 0.0588 → 0.0561
(12 s window) — real but small, because the oscillator dominates (below).

### OSC IS A DIFFERENT ALGORITHM (delta 2 is NOT a constant swap)
2000 `syOscRender` @0x40a585 tri/saw @0xa59a and pulse @0xa5ed are a **4×
linearly-oversampled numeric box filter** (inner `mov cl,4` loop: 4 sub-samples
of `cnt += freq`, piecewise-linear up/down segment, accumulate, `·0.25`). The
2004 C++ uses the **analytic box-filter convolution** state machine
(`osm_tick`, transition codes). Different algorithm → different output (esp.
anti-aliasing on high notes); the step-B "saw/tri/pulse phase trick identical"
was wrong. The per-sample phase advance matches (2000 `4·freq` with
`freq = round(pow2((pitch+note−60)·fci12)·3185015.0)`; `3185015 ≈ fcoscbase·
2²⁹/44100 = SRfcobasefrq/4`, modulo the baked-constant rounding), so **pitch**
agrees but the waveform does not. Sine @0xa624 = native `fsin` (delta 3); noise
@0xa659 = MSVC LCG `·0x343fd + 0x269ec3` (delta 4 — note the constants differ
from step-B's "214013/2531011"; the real values are **0x343fd=214013 /
0x269ec3=2531011**, confirmed). syOscSet @0xa4d2 computes the 4 box-segment
coeffs ([ebp+0x10..0x1c]) the 2004 set never needs.

Consequence: gating the oscillator faithfully requires **reimplementing the
2000 osc set+render** (4 wave variants) under `eraV0()`, not pinning a constant.
Validation plan: unit-test the C++ reimplementation against the genuine 2000
`syOscRender` **called directly in the C1 image** (bit-exact, in isolation) —
the whole-song A/B can't localize osc vs filter vs mix because C1 is a foreign
binary with no internal taps.

## LAYER LOCALIZATION (phase D, via probes) — where the residual lives

Two component-isolation probes (the methodology: call the genuine 2000 routine
in the mapped image, byte-compare against our port) pinned the whole-song
A/B residual to a single layer:

- **`c1_osc_probe`** — calls the 2000 `syOscSet`/`syOscRender` (@0x40a4d2/
  0x40a585) vs `synthTestOscV0`. All 7 cases (trisaw/pulse/sine/noise ×
  colors/seeds) **max|d| = 0**. The eraV0 oscillator is bit-exact.
- **`c1_timing_probe`** — detours the 2000 sequencer-tick call (@0x40996f) to
  log per-event (cursmpl, songtick, MIDI bytes @0x593314); compared against
  `v2mplayer_port` `EVTRACE_RAW`. Over fr08/12 s/204 events: **event sample
  positions identical (0 mismatches)** AND **MIDI bytes identical (0
  mismatches)**.

⇒ The PLAYER layer (sequencer timing) and the CONVERSION layer (conv_v2m MIDI
stream) are **bit-exact**: tempo·441 == usecs, tpc == timediv2, and conv_v2m
preserves the note/CC/PB/PGM stream. The synth gets identical input at identical
samples. So the remaining whole-song A/B residual (rms 0.0483, peaks track) is
**100% synth-internal, downstream of the (bit-exact) oscillator** — i.e. the
voice chain (filter / dist / dcf / channel-mix), env/volramp, or per-note osc
phase. (The earlier "growing-lag" cross-correlation was spurious: phase-shifted
quasi-periodic signals correlate at a drifting lag even when perfectly aligned
in time.) Next probe: a voice-buffer tap (vcebuf @0x714fc4 / channel buf
@0x7153c4) to localize within the voice chain, same methodology.

## IMPLEMENTED era-gate surface + component probes (phase D, current)

The era gate (`eraV0()` = srcVersion<1, `eraEnvOld()` = srcVersion<2) now covers
the full delta inventory below, each VERIFIED bit-exact against the genuine 2000
routine called in the mapped image (`validate/c1_*_probe`, `synthTest*V0`
exports). Whole-song A/B vs C1 (converted fr08, V2_SRCVER=0): rms **0.0588
(modern) → 0.0185** (3.2×). ch10-solo 0.0457 → 0.0118.

**Root cause: control frame 256 vs 128** (`synthSetSourceVersion` sets
`SRcFrameSize=256`/`SRfciframe=1/256` under eraEnvOld). From it follow the
volramp coeff (1/frame) and the env/LFO tick rate.

**Components proven bit-exact (max|d|=0) vs the 2000 binary:**
| component | probe | 2000 routine |
|-----------|-------|--------------|
| player timing | c1_timing_probe | seq tick @0x4095a5 |
| conversion/MIDI | c1_timing_probe | (conv_v2m stream) |
| oscillator (5 modes) | c1_osc_probe | syOscSet/Render @0x40a4d2/0x40a585 |
| SVF filter (6 modes) | c1_flt_probe | syFltSet/Render @0x40a837/0x40a880 |
| distortion (5 modes) | c1_dist_probe | syDistSet/Render @0x40aa93/0x40ab71 |
| chorus | c1_chorus_probe | @0x40b0c1/0x40b268 |
| envelope | c1_env_probe | syEnvSet/Tick @0x40a6d1/0x40a759 |
| LFO (6 modes) | c1_lfo_probe | syLFOSet/Tick @0x40a945/0x40a9c2 |

**Era deltas gated (synth_core.cpp, all behind eraV0/eraEnvOld):**
1. Envelope: attackmul −11/128; dec/rel calcfreq ×10 + the 256-frame tick.
2. Osc: 4×-oversampled box renderer (NOT 2004 analytic convolution) + freq const
   `fcoscbase_v0`=3185015.0; sine native `fsin`; noise MSVC-LCG `·214013+2531011`
   (16-bit float gen) + the exact 2000 LRC recurrence.
3. **LFO freq**: drop the modern `·0.5` (a 128-frame compensation) — under the
   256-frame eraV0 the LFO ran at half speed (the largest single residual: this
   one fix took whole-song 0.0375→0.0185). Plus LFO sine `fsin`, S&H MSVC-LCG.
4. Param/volramp smoother = 1/frame (follows from frame=256).
5. **No `fcdcoffset` anywhere** — gated in: SVF render, voice→channel mix,
   channel chorus input, global delay + reverb input. (The chorus/reverb/delay
   feedback combs ACCUMULATE the bias, so these mattered.)
6. Per-voice DC filter ABSENT (2000 voice = osc→flt→dist→volramp, no dcf).
7. Channel chain = **dist + chorus only** (2000 @0x40b5cc) — dcf1/comp/boost/dcf2
   are all post-2000; gated off.
8. Osc overdrive uses native `fpatan` (not fastatan polynomial).
9. Seed-zeroing (osc-noise + LFO-S&H seeds → 0) to match C1's rdtsc=0.
10. VCF moog modes 6,7 absent (fr08 can't select them).

**Remaining residual (ch10 0.0118):** all 8 components are bit-exact in
isolation, so it is in how they COMBINE — the per-frame modulation matrix
(LFO/env → osc pitch / filter cutoff routing) or the global reverb/delay tail.
Next: tap the dry pre-reverb mix vs a 2000 tap, or probe the modmatrix.

## ROOT CAUSE of the combine-residual (2026-06-05): frame TICK precedes SET

The 12 s ch10-solo A/B (C1 vs port, V2_SRCVER=0) was driven to **rms 0.0118 →
7.5e-5 (157×)**, whole-song 12 s window 0.0185 → 0.0142, by two findings:

1. **No master DC filter in v0.** `fcdcflt` (126.0f) appears NOWHERE in the
   2000 image, and the 2000 mix @0x40b7ba flows straight into the parametric
   lc/hc EQ. The modern `dcf.renderStereo` on the global mix is a ~20 Hz
   one-pole that phase-shifted the 88 Hz bass fundamental by +13°/−0.22 dB —
   the *dominant* ch10 residual. Gated behind `!eraV0()` (synth_core.cpp,
   global render). [bit-exact-gateable]

2. **Frame control order: TICK then SET (the real root cause).** The 2000
   render driver @0x40b9a8 loop does, per voice: `call 0x40ad06` (TICK =
   env/lfo step + volramp) → test env1.state → `call 0x40af88` (SET = param
   store + modmatrix). The port's `V2Synth::tick` did SET then TICK. So in
   the 2000 synth **every tick steps the sub-objects with the PREVIOUS frame's
   params**, and a modsource change (velocity at noteon, ctl, env/lfo-sourced
   mods) reaches osc/flt/env one frame LATER than modern. For fr08 ch10
   (env-gain = velocity-mod, base 0) this makes the **note's first frame
   silent** in the 2000 binary (C1 tick log: out=0/st=1 at tick#1). Gated:
   `V2Synth::tick` runs `voicesw[i].tick()` then `storeV2Values(i)` under
   `eraV0()`. **Verified by the chanstream tap** (`CHANSTREAM=` port /
   `C1_CHANSTREAM=` C1 solo probe, dumping chanbuf pre/post the channel-FX
   chain): ch10 PRE and POST are now **max|d| = 0** (bit-exact voice sum).
   The voice-tick code itself (`V2Voice::tick`) already matched 0x40ad06
   (env1,env2,lfo1,lfo2,volramp) instruction-for-instruction; only the outer
   order was wrong. [bit-exact-gateable; confirmed against disasm, not curve-fit]

Modern path untouched: kkrieger6 / debris_ost asm-vs-cpp A/B still max|d| = 0.

## ch5 SOLVED to phase-exact (2026-06-05): mid-frame note-on osc deferral

The ch5 residual was a **constant 39-sample lag** (cross-corr 0.99, zero-lag
corr ~0) at a chord that note-ons mid-frame at sample 325593. 39 = distance to
the next 256-control-frame boundary (325632). (Decisive ruling-out first: ch5's
oscs are pulse+tri/saw, NO noise osc — so the equal-power-uncorrelated look was
a pure time-shift, not noise/seed.) Root cause:

The 2000 render driver @0x40b95c renders in **sub-frame chunks**
(`min(remaining, frame_left)`) and ticks control only at frame boundaries. A
voice note-on'd MID-frame therefore renders through the **partial remainder** of
the current control frame — its osc/filter phase advances over those samples
(output muted, the volramp curvol is still 0). The port (like 2004) computes
whole frames atomically, deferring the new voice to the next boundary, so its
oscillator starts `(next_boundary − noteOn)` samples late → a fixed lag.
Frame-aligned note-ons (opening chord @ sample 0) have lag 0 — why ch10 and
ch5's first 7 s were already bit-exact.

Gate (eraV0, in `processMIDI` note-on after `noteOn`): advance the new voice
over the `tickd` unfinished samples of the current frame into a scratch buffer
(`voicesw[i].render(scratch, tickd)`), replaying the exact osc/flt/dist state
advance. Confirmed by the 2000 noteon order @0x40bd9f (SET 0x40af88 then NoteOn
0x40aef7 = the port's order) and the C1 tick log (env/curvol identical).
**Result: ch5 lag → 0 (corr 0.9999); max|d| 0.136 → 0.0033, rms 0.0142 → 1.9e-4
(76×). Whole-song 12 s full-mix vs C1: rms 0.0185 → 2.0e-4 (92×), rel 0.46%.**
Modern A/B (kkrieger6/debris_ost) still max|d| 0.

(Also tried+reverted: collapsing SYNC_FULL→SYNC_OSC under eraV0 — the 2000
noteon's only keysync branch zeros the 3 osc cnt fields, never reseeds — but
ch5 isn't SYNC_FULL so it had no effect; kept out to avoid dead gating until a
SYNC_FULL v0 patch is found.)

**Remaining ch5 residual FULLY TRACED (2026-06-05) → channel-FX sub-frame
phase.** Built a C1 per-frame voice-substage tap (`C1_VCEFRAME=`/`C1_VCE_LO/HI`
in the solo probe: post-osc @0x40ad76, post-flt @0x40adfe, post-dist on
syV2Render return, post-volramp voice-sum via the channel-FX detour) vs the
port's `VCEFRAME=` per-frame dump (`g_vcetap_*` + `g_chantap`). Result at the
first divergent frame (1276, the note's ~5th frame):
- post-osc, post-flt, post-dist (voice mono sum): **max|d| = 0**
- post-volramp voice sum (chanbuf, pre channel-FX): **max|d| = 0**
- dry output (post channel-FX): **diverges**

So the whole dry VOICE is bit-exact; the residual enters in the **channel FX
chain (dist + chorus)**. The chorus is a feedback modulated-delay: a divergence
recirculates (fb≈0.42, dboffs≈296) and grows over ~3–4 loops, which is why it
first crosses 1e-6 ~4 frames *after* the note onset (frames 1272–1275 of the
output are bit-exact) even though the cause is at the onset.

**Root cause + FIX (same sub-frame class as the voice fix, one level up):**
both eras SKIP silent channels in the render loop (2004 `.chanloop`→`.chanend`;
2000 @0x40ba51), so a channel's chorus freezes while the channel is silent. But
when a note-on ACTIVATES a previously-silent channel MID-frame, the 2000 renders
that channel's FX over the partial sub-frame remainder (channel render @0x40b5cc
runs per chunk @0x40ba10), advancing the chorus mod-counter / write-pointer; the
port (=2004) defers the whole channel to the next 256-boundary, leaving the
chorus `tickd` samples out of phase. The voice-scratch gate fixed the VOICE
phase; the channel chain needed the same.

Gate (eraV0, `processMIDI` note-on, when `npoly == 0` — the channel was
silent): advance the channel's dist+chorus over the `tickd` partial-frame
samples on a zero buffer (the activating voice is muted, curvol=0, so the FX
input is exactly 0 — only the chorus state advance matters). Channel-level
analogue of the voice-scratch advance, NOT a full sub-frame refactor.

**RESULT: ch5 lag 0 / corr 1.00000; chorus post-FX chanbuf now max|d|=0; ch5
DRY (reverb+delay muted) is BIT-EXACT (max|d|=0).** Whole-song 12 s full-mix vs
C1: rms **0.0185 → 7.8e-5 (237×)**, rel 0.18 %. Modern A/B (kkrieger6/
debris_ost/josie/pzero) still max|d|=0.

### Status of the two active channels (12 s window, after all fixes)
- **ch5 (chords + chorus fb): DRY bit-exact.** Its only remaining full-mix
  contribution (~2.3e-5 rms) is the global reverb/delay TAIL (deterministic send
  of the bit-exact dry signal; tap aux1/aux2→reverb for the last 1-ULP).
- **ch10 (mono, no chorus): DRY ~8.25e-4 max** — a single-frame transient that
  decays to bit-exact by ~6.5 s. FULLY PINNED via the premix tap
  (`C1_VCEFRAME .premix` detour @0x40baf8 vs port `VCEFRAME .premix`): the EQ
  input (channel sum) is bit-exact at every frame EXCEPT frame 445 (sample
  113920), where it differs by a **constant ratio 0.984375 = 63/64** (std
  5.8e-8) = a single-frame `chgain` error. Cause: a ctl7 CC ramps 63→64 at
  113920 (`ba 07 40`), which is frame-ALIGNED (113920 = 445·256); ch10 has a
  ctl7→chanvol mod (src=7→dest 59). The 2000 fires the frame-445 control-tick at
  the **trailing edge** (end of the render filling frame 444, BEFORE the
  boundary-aligned ProcessMIDI updates ctl7 → uses ctl7=63); the port (=2004) at
  the **leading edge** (start of the next render, AFTER ProcessMIDI → ctl7=64).
  One frame later both agree, so only frame 445 differs; the one-frame chgain
  step kicks the global lc/hc low-cut EQ, which rings down. **4th facet of the
  same sub-frame era-difference** (after within-frame TICK-before-SET; mid-frame
  note-on voice phase; mid-frame channel-activation chorus phase). Clean fix for
  all four = gated sub-frame rendering under eraV0 (decouple the trailing-edge
  pre-event control-tick from the per-chunk render). Deferred: a single-frame,
  decaying, −90 dB transient; a targeted eager-tick risks the frame-aligned
  note-on case, so it waits for the full refactor.

The per-voice and per-channel DSP is now bit-exact for both channels; the
sub-0.2 % that remains is in the GLOBAL mix/FX tail. Next-pass tooling:
`C1_VCEFRAME`/`C1_VCE_LO/HI` (post-osc/flt/dist + post-volramp + post-channel-FX
+ premix/EQ-input) and port `VCEFRAME`/`VCEFRAME_CH`; `MUTEREVERB`/`MUTEDELAY` to
split dry vs tail.

(The reverb/delay tail is exonerated as a *primary* suspect: ch10 full-vs-dry
split showed the tail contributes only at the 7.5e-5 level.)

## Final delta list (legacy step-B summary, superseded by the table above)

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
