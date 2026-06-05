# Handover: chase the ch3 (pgm 5) oscillator divergence at t=66.8 s

**Goal:** make the fr08 whole-song render bit-exact vs the genuine year-2000
binary. It is currently **bit-exact for the first 66.8 s** (full mix, incl.
reverb/delay tail); the first divergence is a **structural** oscillator delta on
**ch3 (program 5)**. Chase it (it is NOT float-precision noise — the osc output
is grossly different, a phase-inverted pulse). Then keep sweeping later sections.

Branch: `v2-port-fidelity`. Work dir: `/home/spheenik/projects/scene/fr_public/v2/validate`.
Read `../v2m/fr08-extraction/DELTA.md` first — it is the full delta catalogue and
the era-gating model (`eraV0()` = srcVersion<1, `eraEnvOld()` = srcVersion<2).

## The era model (how period behavior is gated)

The port (`v2/synth_core.cpp`) is faithful-to-2004 by default. Period (year-2000,
fr08) behavior is gated behind `instance.eraV0()` / `instance.eraEnvOld()`. The
harness sets the era with env `V2_SRCVER=0`; the player applies it via
`synthSetSourceVersion` (now **before** `synthSetGlobals` — see
`validate/v2mplayer_port.cpp` ~line 372, important for set-time DSP like reverb
gains). Modern (`V2_SRCVER` unset) must stay bit-exact vs the 2004 asm core —
**always re-check modern after any change** (see "regression" below).

## Ground truth & reproduction

- **C1 ground truth** (genuine 2000 binary, rdtsc pinned to 0):
  `/tmp/fr08/c1_fr08.f32` = 29 241 344 stereo f32 frames (663 s). Re-derivable by
  `./c1_fr08_harness /tmp/fr08/unpacked.bin /tmp/fr08/c1_fr08.f32 4096 700`.
  Image: `/tmp/fr08/unpacked.bin` (md5 `2c1ebe7efac1000ba9a4be86f38c55b1`, base
  0x400000). Objdump of the synth window: `/tmp/fr08/fr08_objdump.txt`.
- **Port render**: `V2_SRCVER=0 ./harness_cpp ../v2m/converted/fr08.v2m OUT.f32 N`
  (N = stereo sample count, or `auto`). Build everything with `./build.sh`
  (gcc -m32 -no-pie, `-mpc32 -mno-sse -DV2_X87_FAITHFUL`).
- **Compare**: `python3 compare.py A.f32 B.f32` (or numpy memmap for big files;
  the whole song is 233 MB).

**First divergence (the target):** float 5 891 586 = **stereo sample 2 945 793 =
t ≈ 66.798 s**. C1=−0.12132220, port=−0.12132196 at that first sample, then it
grows. Localized to **ch3** by soloing (ch4, ch12 at that moment are bit-exact).

## What is already known about the ch3 divergence

- **It is the oscillator.** Per-frame voice sub-stage tap (see "Tooling") shows
  **post-osc** already diverges by **max|d| ≈ 3.5** at frame 11503 (t=66.7748 s);
  flt/dist just carry it. So the osc output itself is wrong, not downstream DSP.
- **The pulse is phase-INVERTED.** At the diverging frame the osc tap is a steady
  value: **C1 = +1.758**, **port = −1.758** (same magnitude, opposite sign) —
  i.e. the two are in OPPOSITE pulse segments (one in the "high" half, the other
  in the "low" half). osc mode is **2 = pulse** (verified via `OSCDUMP`).
- **ch3 is MONOPHONIC** (`maxpoly=1`): every note reuses physical slot 0. So this
  is **NOT a voice-allocation/steal slot difference** (both eras use slot 0).
  `NOTETRACE`/`ALLOCTRACE` confirm slot=0 for all ch3 notes.
- **Timing:** the relevant ch3 note (note 53 = 0x35, vel 80) starts at **66.679 s**
  (event @ stereo ~2 940 531, MIDI `93 35 50`), and the osc first diverges
  **~100 ms later** (66.774 s). So either (a) a constant half-period phase offset
  set at the note-on (keysync / osc.cnt), or (b) a small/!constant **frequency
  difference** that drifts the pulse into the opposite segment over ~100 ms.
  **Decide (a) vs (b) first** by cross-correlating the osc tap over a stable
  window: constant lag ⇒ (a) onset phase; growing lag ⇒ (b) frequency.
- **ch3 patch (pgm 5) modmatrix** (`MODDUMP`): src0(vel)→dest37(ampenv),
  src1(ctl1)→dest50, **src10(LFO1)→dest21**, src7(ctl7)→dest59(chanvol),
  **src9(env2)→dest21**. Two mods (LFO1 + env2) hit **dest 21** — find what
  voice-param index 21 is (count sF32 fields in `syVV2`; if it is osc pitch/detune
  that is a prime suspect for a frequency drift). dest50 is another voice param.

## Hypotheses to chase (in order)

1. **Onset osc phase / keysync.** ch3 pgm5's `oscsync` (keysync) value: 0=NONE,
   1=OSC (cnt=0 on noteOn), 2=FULL. The 2000 noteOn @0x40aef7 has only ONE
   keysync branch (zeros the 3 osc cnt fields = SYNC_OSC for any keysync!=0; it
   has no FULL re-init). The port `V2Voice::noteOn` (`synth_core.cpp` ~2564) has
   SYNC_FULL/OSC/NONE. If ch3 is SYNC_NONE, osc.cnt resumes from slot 0's frozen
   value; if the 2000 instead zeroes it (or vice-versa) the pulse starts a
   half-period off → steady inversion. **Check ch3's oscsync and compare the
   noteOn cnt handling to @0x40aef7.** (Earlier a SYNC_FULL→SYNC_OSC eraV0 collapse
   was tried and reverted because ch5 wasn't FULL; revisit for ch3.)
2. **Frequency / pitch.** dest21 mod (LFO1+env2). If dest21 is osc pitch, verify
   the modulated pitch → `chgPitch` freq matches the 2000. The eraV0 osc freq is
   `v2_oscfreq(pno, fcoscbase_v0=3185015.0)` (one x87 seq, `synth_core.cpp` ~286);
   the 2000 is `syOscChgPitch` @0x40a49b (freq = round(pow2((pitch+note-60)/12)·
   3185015)). A pitch-bend (`eX` MIDI) or portamento path could differ.
3. **A pulse-specific osc-set coeff** (`syOscSet` @0x40a4d2 vs the eraV0
   `renderPulse_v0` set, `synth_core.cpp`). The osc was probe-verified bit-exact
   (`c1_osc_probe`) for the cases tested — but pgm5's exact color/pitch/detune may
   hit an untested combination. Re-run `c1_osc_probe` with ch3 pgm5's params.

## Tooling reference

All in `validate/`. Env vars gate diagnostics; they never affect the audio path.

- **Solo a channel** (both sides, byte-comparable):
  C1: `C1_SOLO=3 ./c1_solo_probe /tmp/fr08/unpacked.bin OUT.f32 SECS`
  port: `CHANSOLO=3 V2_SRCVER=0 ./harness_cpp ../v2m/converted/fr08.v2m OUT.f32 N`
- **Per-frame voice sub-stage tap** (post-osc / post-flt / post-dist /
  post-volramp voice-sum / post-channel-FX / premix-EQ-input), absolute frames:
  C1: `C1_SOLO=3 C1_VCEFRAME=/tmp/fr08/c1v3 C1_VCE_LO=<smpl> C1_VCE_HI=<smpl> ./c1_solo_probe ...`
  port: `CHANSOLO=3 VCEFRAME=/tmp/fr08/pv3 VCEFRAME_CH=3 V2_SRCVER=0 ./harness_cpp ...`
  Files: `<pfx>.osc/.flt/.dist` (mono, summed across voices), `.chan` (pre-FX,
  stereo, ALL frames absolute on port), `.chanpostA` (post-FX, absolute),
  `.premix` (EQ input). C1 dumps only chunks with pos in [LO,HI]; align by frame =
  pos/256. (Port `.chan`/`.chanpostA`/`.premix` are every-frame absolute; C1 side
  starts at the first chunk in the window.)
- **Mute global FX** (split dry vs reverb/delay tail): `MUTEREVERB=1 MUTEDELAY=1`
  on port; `C1_MUTE_REVERB=1 C1_MUTE_DELAY=1` on the probe.
- **Param dumps**: `OSCDUMP` (osc mode/pitch/detune/color/gain), `MODDUMP`
  (modmatrix, first patch only), `NOTETRACE`/`ALLOCTRACE` (note-on + slot),
  `LFODUMP`, `ENVTRACE`, `DISTTRACE`, `CHORUSTRACE`, `REVERBTRACE`, `CHANTRACE`,
  `NOTETRACE`. C1 side: `C1_TICKLOG` (+`C1_TICK_LO/HI`) voice tick trace,
  `REVDUMP` (reverb gains @0x7194e0).
- **Event list**: `./c1_timing_probe /tmp/fr08/unpacked.bin /tmp/fr08/c1_timing_full.csv 68`
  → CSV of (idx, cursmpl, songtick, assembled-MIDI-hex) for the whole window.
  Decode: status byte `9x`=noteon, `8x`/vel0=noteoff, `bx`=CC, `cx`=pgm, `ex`=PB.
- **C1 standalone unit-test of the 2000 osc** in the mapped image: `c1_osc_probe`
  (calls genuine `syOscSet`/`syOscRender` @0x40a4d2/0x40a585 vs `synthTestOscV0`).
  Use it to A/B pgm5's exact osc params bit-for-bit.

## Key 2000-image addresses (objdump in /tmp/fr08/fr08_objdump.txt)

- syOscInit @0x40a485 (seeds nseed = rdtsc; patched to 0 by the harness)
- syOscChgPitch @0x40a49b (freq = pow2((n−60)/12)·3185015.0)
- syOscSet @0x40a4d2 ; syOscRender @0x40a585 (mode jumptable; pulse @0xa5ed)
- syV2NoteOn @0x40aef7 (the keysync branch: zeros osc cnt for keysync!=0; NO full
  re-init); note-on handler / alloc @0x40bc5f ; per-voice tick @0x40ad06
- syV2Render @0x40ad4d (osc→flt→dist→volramp; vcebuf @0x714fc4)

## Port code locations (v2/synth_core.cpp)

- `V2Voice::noteOn` ~2564 (keysync switch); `V2Voice::set` ~2511 (keysync=oscsync)
- `V2Osc` set/render: eraV0 `renderPulse_v0` + `syOscSet` coeffs; freq via
  `v2_oscfreq`/`fcoscbase_v0`=3185015.0 (~283-300)
- `storeV2Values` ~4118 (modmatrix application) ; `processMIDI` note-on/alloc ~3815
- `renderSubFrame`/`controlTick` ~4256 (the sub-frame era render path)
- reverb `set` (PC=64 gain) ~3264, render low-cut gate ~3371

## Regression gate (run after EVERY change)

```
for s in kkrieger6 debris_ost josie; do
  ./harness_asm ../v2m/converted/$s.v2m /tmp/fr08/a.f32 220500 >/dev/null 2>&1
  ./harness_cpp ../v2m/converted/$s.v2m /tmp/fr08/c.f32 220500 >/dev/null 2>&1
  printf "%-12s " $s; python3 compare.py /tmp/fr08/a.f32 /tmp/fr08/c.f32 | grep VERDICT
done
```
All must MATCH (modern path bit-exact vs the asm oracle). Then re-check the
period target: render ch3 solo 67 s and compare to `c1_s3.f32`, and re-render the
first ~70 s full-mix vs `/tmp/fr08/c1_fr08.f32` to confirm the bit-exact range
moved past 66.8 s.

## Session log (what got it to 66.8 s — committed on v2-port-fidelity)

1. `ea310d4` TICK-before-SET + no master DCF (ch10 voice).
2. `fe03dac` mid-frame note-on osc phase-advance (ch5, later subsumed).
3. `a63c34b` channel-FX partial-frame advance (ch5 chorus, later subsumed).
4. `75603a4` **gated sub-frame rendering** under eraV0 (`renderSubFrame`/
   `controlTick`; removed the two scratch-advance hacks). Both channels dry
   bit-exact; fixed the frame-aligned control-tick edge (the 63/64 chgain case).
5. `c04b844` **reverb gain PC=64 precision** + SetSourceVersion-before-SetGlobals
   reorder + **reverb low-cut gate** → first 66.8 s whole-mix bit-exact.
6. `fa3f5cf` status correction (this handover's premise).

The remaining work is a section-by-section delta sweep of the rest of the song;
ch3 pgm5's osc is the first one. Expect a small number of further structural
era-deltas in later sections (the song reuses a limited patch set).
