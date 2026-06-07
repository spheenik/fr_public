# brullwurfel (fr-028) v5 oracle — extraction notes

Goal: render the carved brullwurfel song1 (= the fr08 ".the .product" song
re-exported to format v5) through brullwurfel's OWN V2 synth, giving song1 a
true v5 oracle. Since song1 is the same music as `fr08.v2m` (v0, bit-exact C1
oracle), this would let us measure the 2002 engine rewrite on identical input
with no portable player in the loop.

Provenance: `~/downloads/fr-028.zip` (sha256 2dfa53e5…ddb977f09, see
`../era-hunt-manifest.md`). aPLib-packed; unpacked via the toolkit
(`era unpack`) to `/tmp/erascan_recon/fr-028/unpacked.bin` (0x376000 bytes,
imagebase 0x400000). 12 embedded v5 songs carved; song1 (off 0x11930,
183870 B, timediv 480 / maxtime 683530 / 15 active ch) is the fr08 twin.

## Engine map (base 0x400000, single-instance — SYN state at fixed .bss)

All four synth API funcs use the period asm `_synth*` shapes but DROP the
`this` arg (single-instance build): the SYN block is baked at **0x55eeb9**
(synthInit zeroes SYN.size = **0x1e2dac** from there). Conventions read off
the image + the demo's own call sites:

| func | VA | args (stdcall) | notes |
| --- | --- | --- | --- |
| synthInit | 0x4057b9 | (patchmap, samplerate) ret8 | zeroes 0x55eeb9, calls calcNewSampleRate 0x403b0c |
| synthRender | 0x40588f | (outbuf, count, 0, 0) ret16 | demo call site @0x403288 pushes ebx,ebx,esi(count),edi(outbuf); renders if count≠0 |
| synthProcessMIDI | 0x405c6c | (midiptr) ret4 | 0xfd-terminated, running-status latch @0x55eebd |
| synthSetGlobals | 0x405f14 | (globals) ret4 | 22 globals fild'd to 0x718e0d, then sub-setup 0x40517e |
| (demo audio-init) | 0x402d8c→ | — | demo's own setup (globals 0x76f8xx, seq state); NOT used by the oracle |

rdtsc seeds (osc/LFO determinism): **0x403bb2, 0x40423f** → pinned to
`xor eax,eax`. No malloc IAT needed (synth state is static .bss). No Ronan
wired (musicdisk; song1 instrumental — though it DOES carry 56576 B of speech
data, currently ignored).

Voice workspace: base **0x560dcd**, stride **0x228** (POLY=32). Per-voice
offsets used for debugging: note +0x0, gate +0x8, curvol +0xc, lvol +0x1c,
rvol +0x20; aenv {out +0x114, state +0x118, val +0x11c}; fenv {state +0x13c,
val +0x140}. chanmap @0x55eec9 (free if MSB set).

## Harness: `c3_oracle.c`

Ports the genuine genthree `_viruz2.cpp` player (ssInitBase/ssReset/ssTick/
ssRender — identical to candytron's `c2_oracle.c`) and drives the binary's 4
synth VAs. Build `gcc -m32 -no-pie -O0 c3_oracle.c -o c3_oracle -lm`.
Run `./c3_oracle <unpacked.bin> <song.v2m> <out.f32> <seconds>`
(`C3_VOICES=1` dumps per-voice state per second).

**Status: WORKS — faithful v5 render. The "sustain decay" bug is SOLVED.**

The bug: the demo's audio-init wrapper (@0x402686, behind the Win32/DSound
imports the oracle doesn't run) sets a base-scaling const **[0x55ee71] = 0.25**
(`0x3e800000`) BEFORE synthInit. Without it, calcNewSampleRate (@0x403b0c)
computes the osc-base/SR const [0x408030] = C·(1/SR)·[0x55ee71] = **0**, so the
oscillator base frequency collapses and voices decay to silence under constant
envelope. **Fix: the harness writes [0x55ee71]=0.25 before ssReset.** Then
[0x408030] = **3185015.0** (= the canonical `SRfcobasefrq`/oscfreq constant)
and the render is healthy. (Symptom that localized it: healthy aenv+fenv+volume
but signal decaying => not envelope/routing => SR/osc-freq const; [0x408030]==0
was the smoking gun. Generalizable: a harness that skips the demo's DSound
wrapper must replay any synth-relevant globals that wrapper set.)

### Verification (60 s, vs the bit-exact fr08 v0 oracle `c1_fr08.f32`)
- **c3 (binary v5) vs portable-v5 render**: env corr **0.9994**, identical peak
  (0.598) — the portable v5 player is VALIDATED against the genuine brullwurfel
  engine. Sample residual rms-diff/rms = **0.148** (close, not bit-exact — the
  earliest-modern-core brullwurfel may expose v5 deltas the candytron-derived
  portable reference doesn't model; worth a follow-up).
- **c3 (genuine v5) vs fr08 v0 oracle (genuine v0), SAME song** — the clean
  cross-era measurement, no portable in the loop: time-domain corr −0.10
  (decorrelated), magnitude-spectrum corr **0.897**, band balance within 8%
  (v5 slightly darker, 0.92 at 8 kHz+, = the box→analytic OSM anti-aliasing),
  peak v5/v0 = 0.98 (same loudness). => same music + same spectral character,
  wholly different sample-level waveform. The 2002 rewrite changed phase/timbre
  microstructure, preserved the musical/spectral envelope.

### Follow-ups
1. Localize the 0.148 c3-vs-portable residual (TAP per-voice signal chain via
   toolkit `oracle.h` `oracle_chan_tap`; brullwurfel = earliest modern-core).
2. song1 carries 56576 B speech data — Ronan path not wired (not needed for the
   instrumental render; wire if a speech song is targeted).
