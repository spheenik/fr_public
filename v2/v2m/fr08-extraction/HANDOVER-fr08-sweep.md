# Handover: fr08 whole-song bit-exact sweep

**Goal:** make the fr08 whole-song render bit-exact vs the genuine year-2000
binary. **Currently bit-exact for the first 118.056 s** (full mix, incl.
reverb/delay tail). The sweep proceeds divergence by divergence; this doc
carries the current target plus everything solved so far (methods matter — the
solved cases are the playbook).

Branch: `v2-port-fidelity`. Work dir: `/home/spheenik/projects/scene/fr_public/v2/validate`.
`../v2m/fr08-extraction/DELTA.md` = the delta catalogue + era-gating model
(`eraV0()` = srcVersion<1, `eraEnvOld()` = srcVersion<2).

## SOLVED 2026-06-05 (in this order — each moved the bit-exact frontier)

### 1. "ch3 osc phase-inverted at 66.8 s" — FALSE PREMISE (tap misalignment)
The previous handover's claim was an artifact: the C1 VCE tap window was
compared one frame off. Under content-anchored alignment the osc was bit-exact
through the whole note. **Lesson (now a hard rule): never align C1 tap windows
by `pos/256` — anchor on bit-exact content from ≥20 frames BEFORE the suspect
region.** (All-zero frames match anywhere — anchor on NONZERO content.)

### 2. ch3 @66.8 s — CORRUPTED EXTRACTED v2m (data, not synth!)
The voice's release diverged because the port never saw ch3's note-off: the
extracted `../v2m/fr08.v2m` differed from the v2m embedded in the genuine
image (VA 0x415637) by **8 bytes** (2 clusters of 4): file off 0xa149-0xa14c =
ch3 NOTEDELTA stream events 321-324 (`01 01 e7 80` vs embedded `00 00 00 00` —
note-off decoded as n54 against a playing n53, gate never cleared, notes
53,53,53 became 54,55,30,158), and 0x1ef82-0x1ef85 = ch5 CC2 stream bytes
1096-1099 (a second latent divergence). **Fixed by re-extracting fr08.v2m
from unpacked.bin bytes + reconverting** (conv_v2m). Both players decode the
note streams identically (2000 note handler @0x9845-0x985d == modern
`lastnte += noteptr[3*nn]`); the conversion memcpy's MIDI verbatim — the synth
was never wrong. Extraction provenance of the 8 bad bytes: unknown.

### 3. ch2 @67.05 s — env DECAY-clamp era delta (REAL synth delta, now gated)
ch2 (pgm 3, first note) mods **env2 → osc1/osc2 pitch**. The port's global
`val ≤ 2^-13 → OFF` clamp killed env2 in DECAY (gate held, sul < 2^-13); the
2000 env tick (@0x40a759) has the clamp ONLY in SUSTAIN/RELEASE handlers, so
its env2 decays into denormals and keeps feeding a tiny pitch mod
(pitch 41000008 vs 41000000 → integer freq +2 → slow osc phase drift,
surfacing mid-frame ~25 frames into the note at 2e-7). The **2004 asm DOES
clamp in DECAY** (`.state_dec` → `.s4checkrunout`, synth.asm:1178) — the
runout-from-DECAY check was added between 2000 and 2004. **Gated**: skip the
clamp in DECAY/ATTACK under `eraV0()` (`V2Env::tick`, synth_core.cpp ~1623).
Regression gate green (5 songs modern A/B MATCH).

## CURRENT TARGET: ch15 first activation @118.056 s

First mix diff: stereo sample 5,206,273 = **frame 20337 offset 1** (the
volramp-signature offset). ch15: pgm 0x11 set @116.29 s, **first note ever**
(note 0x41 vel 0x50) @ sample **5,206,249** = mid-frame 20336.91, surrounded
by a CC7 ramp (`bf 07 xx` every ~424 smp — sub-chunk boundaries). pgm17: osc1
saw/tri col=1, osc2 pulse col=61, F1 allpass, LFO1 sin→osc1 pitch (vibrato),
mods vel→ampenv, lfo1→osc1pitch, ctl7→chanvol.

**Exonerated (bit-exact over the activation window, all verified):**
- voice chain: osc / flt / dist taps — bit-exact
- envelopes: env1+env2 out/state per tick (C1 ticklog vs port ctrllog) — bit-exact
- osc freq path: all 51 chgPitch records (pitch/freq/nffrq bits) — bit-exact

**The divergence enters at the post-volramp `.chan` tap** (voice→chan
accumulation): first diff frame 20338 offset 1 on the ch15 SOLO (frame 20337
offset 1 in the full mix — note the 1-frame discrepancy, unexplained).
Port-side `cv` (chan/(voice·lvol)) is exactly linear `k·0.00102145` = the
trailing-edge tick2 ramp ✓ self-consistent. C1-side "cv" looks ≈
`A·k(k+1)/2` for small k but turns chaotic (sign flips, |cv|>1) at larger k ⇒
**C1's chan content is NOT a scaled copy of the voice buffer — it is
timing/phase-shifted content**, i.e. the 2000 accumulates the activating
voice into chan with different sub-frame timing than the port (the mid-frame
note-on partial piece [5206249..5206272) and/or the CC7-event sub-chunks).
This is a **5th facet of the sub-frame era-difference family** (after
TICK-before-SET, mid-frame note-on voice phase, mid-frame channel-activation
chorus phase, trailing-edge control tick).

**Where to look next:**
1. The port's mid-frame note-on advance (`processMIDI` note-on, eraV0 scratch
   render) assumes the partial render is INAUDIBLE (curvol=0,ramp=0). Verify
   what the 2000 actually does with the partial piece's chan accumulation for
   THIS case (ch5's verification only covered chorus-state advance with FX
   input exactly 0). Check leftover curvol in slot 0 (FULL→OSC collapse does
   NOT zero curvol; both solo modes event-filter, so slot history should
   match — verify).
2. Dump C1's chan tap per SUB-CHUNK (the .chan stream concatenates variable
   sub-chunks — instrument chunk_pre/chunk_post with explicit (pos,count)
   sidecars to see exact piece boundaries) and find at which piece the
   timing shift enters.
3. Port side: add cur/ramp + chan-accumulation logging per render piece
   (sub-frame path, `renderSubFrame`/`controlTick` ~4256).
4. Note the CC7 ramp: chgain is flat-per-frame in BOTH eras (ch10 63/64 case
   + 2000 chan code @0x40b67b — param store + channel modmatrix, no
   per-sample gain ramp), so chgain VALUES are not the suspect; TIMING of the
   pieces is.

## Ground truth & reproduction

- **C1 ground truth** (genuine 2000 binary, rdtsc pinned to 0):
  `/tmp/fr08/c1_fr08.f32` = 29 241 344 stereo f32 frames (663 s). Re-derivable:
  `./c1_fr08_harness /tmp/fr08/unpacked.bin /tmp/fr08/c1_fr08.f32 4096 700`.
  Image: `/tmp/fr08/unpacked.bin` (md5 `2c1ebe7efac1000ba9a4be86f38c55b1`, base
  0x400000). Synth objdump: `/tmp/fr08/fr08_objdump.txt`; player objdump
  (0x9400-0x9b50): `/tmp/fr08/player_objdump.txt`.
- **The v2m**: `../v2m/fr08.v2m` = bytes [0x15637 .. +183554) of unpacked.bin
  (re-extracted 2026-06-05; the old extraction had the 8 corrupt bytes).
  `../v2m/converted/fr08.v2m` = `./conv_v2m ../v2m/fr08.v2m <out>`.
- **Port render**: `V2_SRCVER=0 ./harness_cpp ../v2m/converted/fr08.v2m OUT.f32 N`
  (N stereo samples; whole song = 29241344). Build: `./build.sh`
  (gcc -m32 -no-pie, `-mpc32 -mno-sse -DV2_X87_FAITHFUL`).
- **Compare**: `python3 compare.py A.f32 B.f32`; whole song via numpy memmap
  chunked compare (233 MB).
- **Event list**: `./c1_timing_probe /tmp/fr08/unpacked.bin CSV SECONDS` →
  (idx, cursmpl, songtick, running-status MIDI hex — parse statefully).
  Current: `/tmp/fr08/c1_timing_full.csv` (through 121 s).

## Tooling reference (validate/; env-gated, never affects audio)

- **Solo**: C1 `C1_SOLO=N ./c1_solo_probe /tmp/fr08/unpacked.bin OUT.f32 SECS`;
  port `CHANSOLO=N V2_SRCVER=0 ./harness_cpp ... OUT.f32 NSAMPLES`. Both
  event-filter via a ProcessMIDI detour.
- **VCE stage taps** (post-osc/-flt/-dist/-volramp/-chanFX/premix):
  C1 `C1_VCEFRAME=<pfx> C1_VCE_LO=<smpl> C1_VCE_HI=<smpl>` (dumps only chunks
  with pos in window; SUB-CHUNK granularity — align by content!);
  port `VCEFRAME=<pfx> VCEFRAME_CH=N` (absolute, every frame).
  Files: `.osc/.flt/.dist` mono, `.chan/.chanpostA/.premix` stereo.
- **Voice tick trace**: C1 `C1_TICKLOG=1 C1_TICK_LO/HI=<smpl>` → per-tick
  env1.out/state, cur, ramp (+ hex: e1.out/val, e2.st/out/val, cur, ramp).
  NB pos labels lag (pos = last processed event, not frame start).
  Port equivalent: `FREQLOG=<pfx>` → `.ctrllog` (FreqLogRec kind=2: e1.out,
  e2.out, lfo1, lfo2 bits + states in r2: e1|e2<<8; port enum OFF=0 REL=1
  ATK=2 DEC=3 SUS=4 vs 2000 OFF=0 ATK=1 DEC=2 SUS=3 REL=4).
- **chgPitch/freq trace** (NEW): C1 `C1_FREQTRACE=1 C1_FREQ_LO/HI=<smpl>` →
  `[chgp] pos osc pitch note freq nffrq` (hex bits) from the genuine
  syOscChgPitch (4 call sites detoured). Port: `FREQLOG=<pfx>` → `.freqlog`
  (kind=0: freq=r[2], pitch=r[3], nffrq=r[4]). Streams align 1:1 in order.
- **Mute global FX**: `MUTEREVERB=1 MUTEDELAY=1` / `C1_MUTE_REVERB=1
  C1_MUTE_DELAY=1`.
- **Param dumps**: `OSCDUMP` `MODDUMP` `NOTETRACE` `ALLOCTRACE` `LFODUMP`
  `ENVTRACE` `DISTTRACE` `CHORUSTRACE` `REVERBTRACE` `CHANTRACE`; C1 `REVDUMP`.
- **Patch/event spelunking offline**: the v2m parses trivially in python
  (header 12B + 10·gdnum, per-channel SoA streams [td0|td1|td2|note|vel]·nn
  delta-coded; patchmap offsets + 89-byte v6 patches + modmatrix triplets).
  Modmatrix dest map (sF32 index in syVV2): 0 pan, 1 transp, 2-7/8-13/14-19
  osc1/2/3(mode,ring,pitch,detune,color,gain), 20-22 flt1(mode,cutoff,reso),
  23-25 flt2, 26 routing, 27 fltbal, 28-31 dist, 32-37 env1(...vol=37),
  38-43 env2, 44-50 lfo1(...amp=50), 51-57 lfo2, 58 oscsync, 59+ channel
  (59=chanvol).
- **C1 unit probes**: `c1_osc_probe`, `c1_env_probe`, `c1_flt_probe`, etc.
  (call genuine routines in the mapped image standalone).

## Key 2000-image addresses

- syEnvSet @0x40a6d1; syEnvTick @0x40a759 (jump table @0x40a745; handlers OFF
  0x40a76f/ATK 0x40a781/DEC 0x40a7a8/SUS 0x40a7cd/REL 0x40a803; LOWEST clamp
  0x39000000 only in SUS+REL)
- syOscChgPitch @0x40a49b (in [ebp+0x44]=pitch [ebp+0x40]=note, out
  [ebp+0x8]=freq [ebp+0x20]=nffrq); call sites 0x40a4f5 (per-tick via
  syOscSet) + 0x40af46/4e/56 (noteOn)
- per-voice tick @0x40ad06 (called @0x40b9c1); syV2Render @0x40ad4d; vcebuf
  @0x714fc4; channel param store/modmatrix @0x40b67b; channel set @0x40b574
- player tick @0x4095a5 (note decode @0x9845-0x985d: 8-bit delta accumulators)
- syV2NoteOn @0x40aef7; render driver @0x40b95c (frame counter 0x715d14=256)

## Port code locations (v2/synth_core.cpp)

- `V2Env::tick` ~1573; the gated DECAY/ATTACK clamp skip ~1623
- `V2Voice::tick` (volramp ~2416); `V2Voice::render` ~2440; `noteOn` ~2564
- `storeV2Values` ~4118; `processMIDI` note-on ~3815 (mid-frame advances)
- `renderSubFrame`/`controlTick` ~4256 (gated sub-frame era render path)
- channel `accumulate(mixbuf, chan, n, chgain)` ~3551

## Regression gate (run after EVERY synth change)

```
for s in kkrieger6 debris_ost josie pzero zeitmaschine; do
  ./harness_asm ../v2m/converted/$s.v2m /tmp/fr08/a.f32 220500 >/dev/null 2>&1
  ./harness_cpp ../v2m/converted/$s.v2m /tmp/fr08/c.f32 220500 >/dev/null 2>&1
  printf "%-14s " $s; python3 compare.py /tmp/fr08/a.f32 /tmp/fr08/c.f32 | grep VERDICT
done
```
All must MATCH. Then whole-song fr08 compare vs `/tmp/fr08/c1_fr08.f32` to
confirm the frontier moved.

## Session log

1. `ea310d4` TICK-before-SET + no master DCF.
2. `75603a4` gated sub-frame rendering (subsumed the scratch-advance hacks).
3. `c04b844` reverb gain precision + SetSourceVersion reorder + low-cut gate
   → 66.8 s bit-exact.
4. `fa3f5cf`/`f5ec77c` status + the (wrong) ch3-osc handover.
5. 2026-06-05: osc claim disproven (alignment artifact); ch3 = corrupted
   extraction (8 bytes, re-extracted); ch2 = env DECAY-clamp era delta
   (gated); **frontier 66.798 s → 118.056 s**. New tools: C1_FREQTRACE,
   hex ticklog. Next: ch15 activation chan-timing facet (above).
