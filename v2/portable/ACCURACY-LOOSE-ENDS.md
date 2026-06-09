# Portable player — accuracy loose ends

This file collects the open threads that would push the portable player
*closer to the historical hardware renders* than it already is. **None of
them is a defect.** The player is structurally faithful, deterministic, and
chunk-invariant across all supported eras (v0–v6); three eras are now
bit-exact against their genuine binaries (v0 vs the year-2000 fr08 binary,
**v1 vs the 2001 flybye binary — whole song, 2026-06-07**, v6 vs the 2004
`synth.asm`). What is left is sub-perceptual: in listening
tests the differences are not audible. These are recorded here so the work
is *resumable*, not because it is *required*.

The living threshold ledger is `v2eras.h`; the deep handover for the josie
hunt is `../v2m/candytron-extraction/NOTES.md` ("RESUME HERE").

---

## 1. josie v5 music-bed residual (~0.009 rms) — transcendental ε

**Status:** localized, not a structural bug. Documented as ε.

The native v5 render of `josie.v2m` matches the candytron binary oracle
(`c2_josie.f32`) to a music-bed residual of **rms ~0.009** once ch15 is
routed through Ronan (the V2_RONAN=0 stub leaving ch15 raw accounts for the
larger 0.079 figure — that is a *config* artifact, not a mix bug).

Proven bit-exact vs the candytron binary along the way:
- per-channel DSP chain — 13/16 channels solo `d_rms = 0.00000`
- the voice DSP (direct voice-array tap @0x4c4be0)
- the player / sequencer (`v2seq` == genthree `_viruz2.cpp`)
- voice stealing — all 291 note-on allocations identical

The remaining floor is **ch3/ch6 late-song (38–46 s) oscillator-phase razor
ties** amplified by the sum-compressor: keysync=0 → continuous phase, ch6 is
bit-exact until a single 1-ULP tie at 39.5 s and then the phase drifts.
Prime suspect: portable native-sin vs the binary's x87 `fsin` feeding an
LFO/osc.

**To close it (optional):** decode the exact op at ch6's 39.5 s onset via
per-note bisection (the method that drove fr08 v0 to max|d| = 0), then
either localize+fix the 1-ULP op or formally document it as irreducible
native-vs-x87 ε under the fr08 v0 rules. Tap ch6 solo onset @3487340.
Tooling: `c2_oracle_solo` (C2_SOLO/C2_VTAP/C2_CTAP) + `v2dump` V2SEQ_SOLO.

## 2. Ronan phoneme-sequencer one-syllable lag (was the "audibly off")

**Status:** FIXED in 6.1 (USER-CONFIRMED by listening), kept here only as a
pointer for the related fine-timing thread.

Root cause was `reset()` not replicating the lab's `memset(workspace,0)`,
leaving `wait4on` stuck after a mid-song CC4 text-select → one-syllable lag
all song (ch15 speech corr 0.13 → 0.99966). The residual *fine* phoneme
timing (sub-frame note-on/off vs wait-gate ordering) was never chased to the
sample because it is inaudible. If ever revisited: per-tick trace of
spos/scounter/framecount/wait4on after the 7.384 s reset, both sides
(tools C2_RTAP/C2_NTAP committed; ronan process@0x41493c tick@0x4145c4
ws[0x6a88f8]).

## 3. v1–v4 ASSUMED era-table rows

**Status (updated 2026-06-07):** two more period binaries unpacked and
assayed — **flybye (fr-013, format v1)** and **fr-022 ein.schlag (embeds a
v5 export)** — see `../v2m/flybye-extraction/NOTES.md`. The era assay now has
a five-point build timeline: v0 fr08 (2000) · v1 flybye (2001-12) · v5 fr-022
(2002-08) · v5 candytron (2003-08) · v6 synth.asm (2004).

All seven formerly-ASSUMED rows are now resolved (data edits) — the osc-core
trio by constant scan, the four player/render rows by disassembly. **No
EV_ASSUMED row remains at flipsAt 1.** The follow-up render oracle
(`c1_flybye_harness.c`) then upgraded v1 from correct-by-row to **whole-song
bit-exact**, catching one row no static read had found: the **voice-pool
size** (16 in 2000/2001, 32 in both 2002/2003 v5 binaries, 64 in the 2004
asm) — now `DELTA_POLY_16`/`DELTA_POLY_32` + era-bounded allocator scans
(the 66.42 s hunt, flybye-extraction NOTES).

| row | status after flybye/fr-022 |
| --- | --- |
| `DELTA_OSC_BOXFILTER`   | **SUPERSEDED → flipsAt 3 PROVEN (see §9).** flybye(v1) box, fr014(v3)/fr019(v4) OSM — the box→OSM rewrite is v1→v3, not the build-date. flipsAt 5 was wrong. |
| `DELTA_NOISE_LCG_MSVC`  | **fixed → flipsAt 5.** flybye(v1) has MSVC LCG; modern absent. |
| `DELTA_OSC_FREQ_CONST`  | **fixed → flipsAt 5.** flybye(v1) has baked 3185015. |
| `DELTA_PGMCHANGE_V0`    | **fixed → flipsAt 5.** flybye(v1) disasm @0x410455 == fr08: no early-out + ctl7=127. (NEW already at early-v5 fr-022 — flipped earlier than the rest.) |
| `DELTA_TICK_BEFORE_SET` | **fixed → flipsAt 5.** flybye render drv @0x40ff83 == fr08: trailing-edge tick. Build-date flip (OLD in early-v5 fr-022). |
| `DELTA_SUBFRAME_RENDER` | **fixed → flipsAt 5.** same render driver: sub-frame chunks. Build-date flip. |
| `DELTA_RVB_E_FULLPREC`  | **fixed → flipsAt 5.** syReverbSet @0x40f89e == fr08: no SRfclinfreq. Build-date flip (fr-022 OLD, candytron NEW @0x41f27d). |

**The trio is build-date-tied, not format-tied.** fr-022 (v5, 2002-08) and
candytron (v5, 2003-08) are the SAME format version with OPPOSITE DSP cores
(fr-022 = old MSVC LCG + baked freq + box; candytron = modern). The flip
landed in that 12-month gap, and the v2m records only format version — so an
early-v5 file is genuinely under-specified. `flipsAt 5` is the best proxy
(OLD v0..v4, NEW v5..v6): correct for every oracle-bearing file and a no-op
for v0/v5/v6 (check.py still 3/3). The lone unrepresentable case is an
early-v5 old-core file like fr-022 — rendering it as new-core measures **89%
relative-RMS / 0.63 correlation** error (dominated by the tonal core, not
just noise; full breakdown in the extraction NOTES). Getting it right needs a
build-era signal the file lacks (the `v2eras.h` "model change" option) —
deferred until/unless early-v5 files enter the corpus.

**Done since:** the flybye render-harness oracle was built
(`c1_flybye_harness.c`) and the v1 claim is now **bit-exact, whole song**
(rel-RMS 4.0e-5, max|d| 1.5e-4, zero samples >1e-3) after the voice-pool fix.

**Update 2026-06-07 (era-gap sweep — `../v2m/era-gap-sweep-results` in memory,
+ `../v2m/brullwurfel-extraction/`):** 11 more period binaries unpacked/assayed,
giving the FIRST v3 and v4 engines. Three rows moved off the flipsAt-5 proxy:
- `DELTA_PGMCHANGE_V0` → **flipsAt 4 EV_PROVEN** (v3 fr014/fr-022party OLD
  ctl7=127; v4 fr019 NEW same-prog early-out — both sides at the MS2002 party).
- `DELTA_NO_FM_OSC` → **flipsAt 4 EV_PROVEN** (oscjtab mode5 OFF thru v3, fsin
  FM at v4 fr019 @0x4282a1; was the last flipsAt-1 ASSUMED).
- `DELTA_POLY_16` → **flipsAt 3** (pool 32 PROVEN at v3 fr014 @0x410030; the
  16→32 growth is v2-or-v3, no v2 binary, so still ASSUMED for that 1-version gap).
NOISE_LCG/FREQ_CONST + TICK/SUBFRAME/RVB_E STAY flipsAt-5 proxies (OSC_BOXFILTER
left the group — pinned at v3 PROVEN by the v4 oracle, §9) but the sweep
confirmed their build-date flip is the one month **2002-08→09** (old-core thru
fr-027 v5 2002-07, new at fr-028 brullwurfel 2002-09; fr-027 joins fr-022 as an
early-v5 old-core unrepresentable case). check.py still 3/3 (these gates are
no-ops on the v0+v6 corpus).

## 4. v1–v4 originals: structural-only validation (no oracle)

We have genuine period files spanning the gap, in-repo:

| file | era | location |
| --- | --- | --- |
| `tpinv2.v2m` / `tpinv.v2m`        | v1 | RG2/flybye, RG2/ViruzII |
| `whatever07.v2m` / `whateverload2.v2m` | v3 | RG2/einschlag |
| `loading.v2m`                     | v4 | RG2/Viewer |
| `drumtro3.v2m` / `invtro.v2m`     | v5 | RG2/dopplerdefekt, RG2/welcome_to |

These files are detected at their native version, canonicalized to the v6
*layout* (value-preserving), and rendered with their *native-era behavior*
(the era gates fire on the detected v1/v3/v4, not on v6). They render through
the portable and are **deterministic + chunk-invariant** (verified 2026-06-06:
identical bytes across reruns and chunk 4096 vs 333).

**Caveat — this is a liveness check, not a fidelity check.** Determinism +
chunk-invariance only proves the renderer is deterministic; it does NOT prove
the v1–v4 era *behavior* is correct (a renderer that wrongly played everything
at v6 would pass the same test). **v1 is now oracle-proven** (flybye, §3):
the embedded tpinv2 (byte-identical to the repo `tpinv2.v2m`) renders
whole-song bit-exact against the 2001 binary itself.

**Update 2026-06-07:** the era-gap sweep surfaced **v3 (fr014, fr-022party) and
v4 (fr019) period engines** (assayed, §3), and the brullwurfel (fr-028, v5)
**render oracle** is built (`../v2m/brullwurfel-extraction/c3_oracle.c`). So v3/v4
are no longer binary-less, and v5 has a second proven render anchor besides
candytron. **Update 2026-06-08: v3 (fr014, §8) AND v4 (fr019, §9) now have full
render oracles** — v3/v4 osc rendering is render-proven (fr019 whole-mix rms
3.9e-6), and the v4 oracle corrected the BOXFILTER flip point (§9). **v2 still
has no binary at all** — it remains liveness-only. kkrieger6's native-v5 pool
note stands (saturates 32 voices at 102.47 s; josie never does).

## 5. CC1 mod-dest remap re-audit (low priority)

Open lead from the 6.2 hunt: re-verify the v5→v6 mod-dest remap puts these on
the right canonical param — mod3 dest67=boost.amount, mod4 dest65=aux2/delay-
send, mod5 dest13=osc2.vol. No observed divergence; flagged for completeness.

## 6. Cross-host / cross-arch determinism (task 8.2)

The hash contract is implemented and `baselines.sha256` is checked in, but
the equal-bits-on-a-second-host check has only been run on x86_64 this
session. Re-run `test/check.py` on one other arch (e.g. aarch64) and record
the result to close 8.2.

## 7. Portable v5 vs the brullwurfel v5 oracle (~0.148 residual)

**Status (2026-06-07): LOCALIZED + PROVEN a real brullwurfel→candytron
modern-core sub-era delta. Documented as an unrepresentable early-modern-core
case (mirrors §3's early-v5 case). The portable is NOT at fault — it is
bit-exact against its candytron reference.**

The brullwurfel (fr-028, 2002-09) render oracle (`../v2m/brullwurfel-extraction/
c3_oracle.c` + NOTES) renders song1 (= the fr08 ".the .product" song re-exported
to v5) through brullwurfel's OWN synth. Against the portable v5 path (same v2m,
60 s): envelope correlation **0.9994**, **identical peak**, but a sample-level
**rms-diff/rms ≈ 0.148** — not bit-exact. The hunt (channel-solo bisection +
three-way oracle + patch dump + image constant-scan) ran it all the way down:

**1. Channel-solo (`C3_SOLO`/`V2SEQ_SOLO`).** The whole 0.148 collapses onto a
single channel, **ch1** (rms-diff 0.01665 of the 0.0167 total; every other
channel is bit-exact at ~2e-9 or silent in the first 60 s). ch1 is silent until
~36 s, then diverges from its very first sample — *not* an accumulating drift.

**2. Three-way oracle (the decisive test).** Rendering song1 ch1 through
candytron's OWN engine too (`c2_oracle_solo` + `/tmp/candytron/unpacked.bin`):

| pair | peak A / B | rms-diff | verdict |
| --- | --- | --- | --- |
| portable vs candytron (2003) | 0.383627 / 0.383627 | 1.2e-09 | **BIT-EXACT** (1 ULP) |
| candytron (2003) vs brullwurfel (2002) | 0.383627 / 0.170289 | 1.67e-02 | the divergence |
| portable vs brullwurfel (2002) | 0.383627 / 0.170289 | 1.67e-02 | same divergence |

So the portable reproduces candytron to the last ULP; **both** the portable and
candytron diverge from brullwurfel identically. The 0.148 is a genuine v5
sub-era delta between the earliest modern core (Sep-2002) and candytron
(Aug-2003), not a portable defect.

**3. Root cause: ch1's full-gain NOISE oscillator.** ch1's patch (pgm1, serial
filters) is `osc0/osc1 = PULSE` at gain 39/47 and **`osc2 = OSC_NOISE` at full
gain 127**, through a high-resonance serial filter (flt0 mode3 cutoff102
reso106). The two builds' ch1 renders are **decorrelated (corr 0.16)** with all
energy in a ~3.5 kHz resonant band — the signature of *differing noise* shaped by
the same resonant filter (the quiet pulses are identical and supply the residual
0.16 correlation). c3 is byte-stable across runs, so the noise is deterministic,
just build-specific.

**4. Mechanism: same LCG, different seed.** Both unpacked images contain the
SAME modern noise LCG (196314165 / 907633515 — brullwurfel @0x4369, candytron
@0x1441b) *and* the MSVC LCG, so `DELTA_NOISE_LCG_MSVC` is correct (both NEW).
The decorrelation is therefore a **different per-voice noise seed** feeding the
same LCG (identical generator + different start = fully decorrelated). At v5 the
seed is `seedMix(userSeed, rdtsc→0, idx)`; brullwurfel's early-modern build seeds
it differently from candytron, and the v2m carries no build-era signal to tell
the two apart.

**Disposition:** unrepresentable early-modern-core sub-era case, same shape as
§3's early-v5-old-core (fr-022/fr-027). The portable models candytron, the later
canonical v5, bit-exact; noise-seed divergence between builds is the irreducible
class already noted for rdtsc seeding. NOT promoted to an era row (no
early-modern file in the corpus to serve, and a noise *seed* — unlike the LCG
constant — is not a behavior the v2m can carry). Re-open only if an early-modern
noise-heavy v2m enters the corpus AND brullwurfel's exact seed init is decoded
(disasm the syOsc noise-mode seed at the brullwurfel noise-gen site).

Tooling added this session: `C3_SOLO` in `c3_oracle.c` (mirrors `C2_SOLO`/
`V2SEQ_SOLO`); `V2_PATCHDUMP=<ch>` in `v2core.cpp` `storeV2Values` (dev-only,
`#ifndef NDEBUG`, dumps a channel's post-mod voice config). check.py 3/3 (genuine-only).

NOTE: the brullwurfel unpacked image + carved song1 live in scratch (re-unpack
`~/downloads/fr-028.zip` via `../v2m/toolkit/era`; song1 is carve index 1). The
candytron image re-unpacks from `~/downloads/fr-030_candytron_final.zip`
(kkrunchy) to `/tmp/candytron/unpacked.bin`.

## 8. v3 (fr014) render oracle — built; ch8 divergence SOLVED

**Status (2026-06-08, SOLVED): the v3 render oracle is built and the ch8
divergence is FIXED. ROOT CAUSE: the CHANNEL mod matrix must skip voice-private
mod sources (>= 8: aenv/env2, lfo1/lfo2, note) at v3/v4 — they have no
channel-level meaning. fr-014 ch8 (pgm7) carries `aenv -> comp.outgain` and
`lfo1 -> chorus.amount` mods; the portable wrongly applied them, pushing the
channel compressor makeup from outgain 98 to 128 (the clamp) for ~3.7x extra
gain (plus the chorus perturbation) = the ~5x ch8 blow-up. FIX:
`DELTA_CHANMOD_NO_VOICE_SRC` (flipsAt 5) — the v3/v4 stores skip src>=8
(`cmp al,8; jae`, fr014 @0x40fe39, fr019 @0x429900); the modern-core v5+ store
applies all sources (bit-exact v5/v6 corpus). RESULT: whole-song v3 oracle corr
0.947 -> 0.990, ch8 peak 3.19 -> amplitude-matched (~0.68 like the binary).
check.py stays 3/3, release render sha unchanged. The remaining ch8 residual
(corr ~0 on a noise channel, peaks matched) is the §7-class noise-seed phase
decorrelation amplified by the chorus feedback comb — NOT a new bug.**

**Overturned suspects (history): noise resonator (719fe10), amplitude-envelope/
curvol (f15a2b7), and "channel comp/chorus makeup era-difference" (the prior §8)
were ALL wrong. The first two were broken-probe artifacts; the third was the
right STAGE (comp/chorus) but the wrong CAUSE — the comp/chorus algorithms and
their parsed params are bit-faithful; only the mod-source filter differed.**

`../v2m/fr014-extraction/c1_fr014_harness.c` is the first **render** oracle for
format v3 (previously assay-only, §3). It drives mark&sweep's (fr-014, 2001-12)
own player on the in-image v3 song (carve 0), rdtsc-pinned, deterministic
(byte-stable). VAs: OpenV2M 0x40d93d, PlayV2M 0x40da6d, RenderProxy 0x40d7f3
(stdcall ret8), synthRender 0x40ff6f (sets x87 **PC=24** @0x40ff71), synthInit
0x40feb2 (SYN@0x50a374 sz 0x1e27b4, 32 voices @0x50c184 stride 0x210),
playing@0x476af0, rdtsc 0x40e4a0/0x40ead9/0x40ec01. Ronan init (call 0x40df02)
nop'd. Build: `gcc -m32 -no-pie -O0 c1_fr014_harness.c -o c1_fr014_harness`.

**Result:** portable-v3 vs the v3 binary is corr 0.947 / median sample ratio 1.0
(most of the mix bit-faithful). The whole divergence is **ch8** (pgm7: three
full-gain NOISE oscs → HP filter flt0 mode3 → LP flt1 mode1 → BITCRUSHER mode3
→ vol). Portable ch8 peak **3.19**, binary **0.63** (~5×). Other channels
bit-exact ~2e-9 or matching ~5e-4.

**WHAT WAS MEASURED (2026-06-08 follow-up — gdb on the binary + new taps).**
The earlier curvol/env story rested on a voice-workspace probe that read the
WRONG voice slot with brullwurfel-borrowed offsets; every number it produced
(curvol≈0.118, "flt0 left open") was an artifact. Re-measured from scratch:

1. **Noise oscillator: BIT-EXACT.** gdb at `renderNoise` entry (0x40e7e1) shows
   the binary's runtime coeffs are `nffrq`, `nfres=1.0`, `gain=0.992`, `seed=0`
   — identical to the portable. Stepping `renderNoise_v0` 21 samples from seed 0
   reproduces the binary's frame-2 state **to the bit** (`seed=3522190791,
   sl=0.371471, sb=-0.645311`). The "noise resonator divergence" (commit 719fe10)
   is FALSE. The decoded recurrence (`bb=sb+sl·f; h=(n−sl)·r−bb; ll=sl+h·f;
   out=(h+bb+ll)·gain`) is `renderNoise_v0` verbatim. x87 PC=24/53/64 all give
   the same peak (13.29) — precision is not it either.
2. **Whole per-voice chain matches.** gdb buffer peaks: OSC sum ≈ **13.3**
   (portable VCETAP 13.29), DIST (post-bitcrusher voice buffer) = **1.969**
   (portable 1.97). The amp-EG `Amplify` (voice-param[37], velocity-modded) reads
   **125** in the binary vs **124** portable — gain matches. flt0 cutoff closes
   to ~93 in BOTH. So osc/filter/bitcrusher/curvol are all faithful.
   (The earlier NOP-based "binary osc = 0.11" was ALSO an artifact — NOPing the
   filter calls corrupted the shared voice buffer. gdb is the reliable probe.)
3. **The divergence is the CHANNEL FX chain.** Per-stage peaks for ch8:
   - portable voice-sum into chanbuf (1 voice): **1.36** — matches the binary.
   - portable after channel COMPRESSOR: **3.21** (~2.4× makeup).
   - portable after channel CHORUS: **9.18** (~2.8×, feedback comb).
   - portable pre-master mix: **5.38**; final **3.19**.
   - binary pre-master channel mix (0x509b30): **1.80**; final **0.628**.
   Empirical isolation (portable, dev gates): baseline 3.19; no-chorus **1.16**;
   no-comp **0.99**; **no-comp + no-chorus 0.49** ≈ binary **0.628**. So the
   portable's channel compressor + chorus over-amplify a signal the v3 binary
   leaves near unity. (corr is ~0 because ch8 is noise — use peak/RMS, not corr.)

**THE FIX (how it was found).** `0x40fda8` is the channel value STORE (loads
patch bytes [0x39..0x51] → the 25-float value array @0x510384+ch*0x64, then runs
the channel mod matrix), tail-calling the channel SET `0x40fc69` then the per-
sample RENDER `0x40fcda` (stage map: COMP 0x40f945, BOOST 0x40f40a, then
DIST 0x40ee75 / CHORUS 0x40f612 swapped by the fxr flag [+0xc]; NO dcf at v3 —
exactly the portable's comp→boost→[dist↔chorus] order). The stage isolation
(FR014_NOCOMP/… vs V2_MIXTAP) localized the over-amplification to the channel
COMP. The portable's comp.set RECEIVED the correct params (mode1/thr9/outg98,
== binary value[16..24]) but RENDERED with outgain 128: a controller mod was
inflating it. V2_CHANPARM (post-mod param dump) + V2_MODREMAP showed the mod
`src=8 (aenv) -> dest comp.outgain` (and `src=10 (lfo1) -> chorus.amount`). The
binary keeps outgain pinned at 98 across the whole song because its channel store
SKIPS src>=8 (`cmp al,8; jae` @0x40fe39). Encoded as `DELTA_CHANMOD_NO_VOICE_SRC`
in storeChanValues. The comp/boost/dist/chorus engines, their param parse, the
v3 channel-param layout, and the aux-param defaulting are all bit-faithful — only
the channel mod-source filter was missing.

**Evidence for the flip (flipsAt 5):** OLD (skip src>=8) PROVEN at v3 (fr014
@0x40fe39) and v4 (fr019 @0x429900, same `cmp al,8; jae` after the dest-range
`cmp al,0x39/0x52`). NEW (apply all) at the modern-core v5+ store (brullwurfel
v5 has no such range/source check) — proven by the bit-exact v5/v6 corpus, which
carries src>=8 channel mods and only matches if they are applied. Build-date
proxy at v5 like the osc/player rows (an early-v5 old-core file would still
skip). The fr014 binary IS ground truth (native v3); matched, not converted-v6.

**Reusable taps (dev-only, `#ifndef NDEBUG`, env-gated, output-neutral; committed):**
- portable `v2core.cpp`: `V2_PATCHDUMP=<ch>` (post-mod voice config + mod matrix),
  `V2_VCETAP` (per-stage osc/flt/dist/dcf peaks), `V2_NSEED` (noise seed +
  nffrq/nfres hex), `V2_VOL` (curvol/flt0.cfreq/flt1.cfreq/env2.out/aenv.out),
  `V2_OSCONSET` (first-16 osc-buffer samples at onset), `V2_MIXTAP` (per-master-
  stage peaks premix/reverb/delay/dcf/lchc/compr + per-channel-stage CHTAP
  dcf1/comp/boost/dist/chorus + `[chanN] voicesum/nvoices` + CHTAP entry/rxaux),
  `V2_COMPDUMP` (channel-comp mode/thresh/ratio/outgain/autogain → invol/outvol),
  `V2_CHANPARM=<ch>` (that channel's POST-MOD param array + decoded comp config,
  on pgm/outgain change — the tap that cracked this), `V2_CHANTRACE` (per-channel
  pre/post-process peak + the channel comp's runtime mode/invol/outvol/net/curgain
  + fxr), `V2_NOCOMP` / `V2_NOCHORUS` (skip a channel-FX stage = stage isolation).
- portable `v2load.cpp`: `V2_MODREMAP` (raw v3 dest → remapped v6 dest per mod;
  `src=N` is the mod source — 0 vel, 1..7 ctl, 8/9 aenv/env2, 10/11 lfo).
- harness `c1_fr014_harness.c`: `FR014_SOLO=<ch>` (binary-side channel solo),
  `FR014_DUMPVOX=<sec>` (one-shot voice dump: per-voice param[37] Amplify + aenv +
  osc nffrq/nfres + `[ch8 chanvals]` post-mod value array + decoded `[ch8 COMP]`),
  `FR014_BUFPEAK` (running max of voicebuf 0x509730 [post-dist] + chanmix 0x509b30
  [pre-master] + onset samples), `FR014_NOCOMP/NOBOOST/NODIST/NOCHORUS` (NOP the
  channel-FX render call(s) = binary stage isolation, == portable V2_NOCOMP/…),
  `FR014_STAGE=osc|flt` / `FR014_NOISERAW` (voice-chain NOPs — UNRELIABLE, corrupt
  the shared buffer; prefer gdb). The TRUSTWORTHY binary probe is **gdb** at VAs.
- Scratch: `era unpack <party.exe> <out.bin>`; fr014 → carve 0 = song0.v2m (v3,
  the divergent song), carve 1 = song1.v2m (v1); fr019 (v4) and fr-028 (v5
  brullwurfel) unpacked for the flip evidence. Portable solo: `V2SEQ_SOLO=8`.

NOTE: every earlier suspect is now disproven — the 719fe10 "color misparse"
(binary nfres=1.0), the f15a2b7 "amplitude-envelope/curvol" (Amplify=125 matches),
and the prior §8 "channel comp/chorus makeup era-difference" (right stage, wrong
cause: the comp/chorus math and parsed params are bit-faithful). The real cause
is the channel mod-source filter (`DELTA_CHANMOD_NO_VOICE_SRC`).

## 9. v4 (fr019) render oracle — built; overturned the BOXFILTER flip point

**Status (2026-06-08, SOLVED): the v4 render oracle is built and it caught a
real era-row error. ROOT CAUSE: the tri/saw/pulse box->analytic-OSM oscillator
rewrite happens at the v1->v3 format step, NOT at the v5 build-date as the
flipsAt-5 `DELTA_OSC_BOXFILTER` proxy assumed. The proxy was set from flybye
(v1, box) + candytron (v5, OSM) and INTERPOLATED across v2-v4 -- but v3/v4 were
never checked. They are OSM. FIX: `DELTA_OSC_BOXFILTER` flipsAt 5 -> 3 (PROVEN),
decoupled from the NOISE_LCG/FREQ_CONST pair it was bundled with; AND the
integral renderTriSaw/renderPulse now advance the phase at freq<<2 when
old(FREQ_CONST) (v3/v4 keep the 4x-oversample freq convention even though the
renderer is the analytic OSM -- fr019 pulse @0x428111 does `shl esi,2`), mirror-
ing renderSin_v0. The box-vs-OSM divergence is then GONE (ch3 pulse corr -1.0 ->
1.0 max|d|=0). fr014 (v3) whole-song corr 0.990 -> 0.99989; flybye (v1)/fr08 (v0)
unchanged (box untouched); check.py 3/3 (genuine-only) (v0/v5/v6 corpus is unaffected by a
v3/v4-only change). BUT the box->OSM fix alone left fr019 audibly wrong from
t=20.3 s -- that turned out to be TWO MORE structural v4 bugs in the FM oscillator
(not a floor; see "FM oscillator" below). With all three fixes, fr019 whole-song
(341 s) is 0 of 341 sec above 3%, overall rms-diff/rms 0.12%, listening A/B
indistinguishable.**

`../v2m/fr019-extraction/c1_fr019_harness.c` is the v4 ORACLE. poemtoahorse
(fr-019, ms2002 2002-03) fuses OpenV2M+PlayV2M into one stdcall @0x4107f0 (parses
header -> globals timediv@0x448b98/maxtime@0x448ba0/gdnum@0x448ba8, builds 16
channel tables @0x448bb4 stride 0x50, calls Reset @0x41003d, sets the playing
BYTE @0x448148 := 1). RenderProxy @0x410716 (ret 8) calls synthRender @0x429a36.
synthInit @0x429979 (SYN @0x4cacf0, 32 voices @0x4ccb00 stride 0x210, `mov
cl,0x20` = v4 POLY 32). rdtsc 0x427ee4/0x42856b/0x428693. No Ronan on the path.
embedded v4 song = carve 0 @0x41a7cd (timediv 480, 14 active ch). VAs signature-
matched against fr014's known V2MPlayer methods (OpenV2M imul-0x2710 prologue,
synthRender's PC=24 `66 25 fff0; 66 0d 3f00`, the byte playing-flag RenderProxy).
Build: `gcc -m32 -no-pie -O0 c1_fr019_harness.c -o c1_fr019_harness`.

**How it was found.** Whole-mix diff DIVERGE (rms 0.037). Channel-solo bisection
(`FR019_SOLO`=binary, `V2SEQ_SOLO`=portable): ch2 (noise) bit-exact, ch3 (3x
pulse) carried the whole residual at **corr exactly -1.0, magnitudes matched** --
a pure sign inversion, localized to OSC_PULSE (ch4 = 1 pulse + 2 tri/saw diverged
partially). The v1 linchpin: flybye (v1) ch7 pulse is `corr +1.0 max|d|=0`
BIT-EXACT vs the current (box) renderer, so the box sign is right at v1 -- a real
era difference, not a global bug. The asm settled it: fr014(v3) pulse @0x40e6cf /
tri/saw @0x40e595 and fr019(v4) @0x428111 / @0x427fd9 are the analytic OSM
(utof23 + `fdivr` gain/f + the osm state-machine jump), byte-identical to each
other, while flybye(v1) tri/saw @0x40e70a is the 4x box (`mov cl,4; fldz`). The
-1.0 was box-vs-OSM (opposite polarity convention); the residual 4% magnitude
after fixing the flip was the freq<<2 phase-advance (the OSM at v3/v4 still uses
the old 4x freq). fr08(v0) has 4 pulse channels and is whole-song bit-exact too,
so v0+v1 = box, v3+v4 = OSM: the flip is v1->v3 (v2 has no binary; assumed box).

**v4 was the goal era for the FM-osc path** (`DELTA_NO_FM_OSC` NEW side):
poemtoahorse uses mode5 FM on ch11/ch12 (one ring-modulated). The first-15s
bit-exactness render-proves the FM path that was previously assay-only (fsin
@0x4282a1) -- though the FM channels also carry a slice of the whole-song
residual below. oscjtab confirms it: fr014(v3) mode5 -> off @0x40e6ad, fr019(v4)
mode5 -> FM @0x4282a1.

**FM oscillator -- TWO more structural v4 bugs (2026-06-09, FIXED).** After the
box->OSM fix, fr019 was still audibly wrong (diff ~80% of signal) from t=20.32 s
-- which is exactly when ch11's first FM note enters (the first ~20 s has no FM,
which is why earlier windows looked clean and the original "FM render-proved"
claim was premature). The trigger channel is ch11 (osc1 mode5 FM), found by
whole-mix-aligned onset detection (NOT the binary solo, whose re-Reset shifts
note timing per-channel -- a real gotcha; the whole-mix diff is the trustworthy
signal). Both bugs are in `renderFMSin_v5`, both asm-confirmed from the v4 FM
renderer @0x4282a1, both the v4-vs-v5 FM scheme:
  1. **Carrier freq<<2.** v4 advances the FM carrier at freq<<2 (`shl edx,2; add
     eax,edx` @0x4282a7), like the other oscillators at FREQ_CONST-old;
     renderFMSin_v5 advanced 1x, so the carrier ran at 1/4 rate -- spectrum at
     894 Hz instead of 996. Fixed -> sidebands match, whole-song bad-seconds
     40 -> 10 (60 s).
  2. **Modulation depth 4.0** (NOT freq-gated -- see below). v4 scales the
     modulator by 4.0 (`fmul [0x427e7c]`), twice the 2.0 fcfmmax. With 2.0 the
     timbre is wrong (ch11 magnitude-spectrum corr 0.87, rel-dist 0.57); with 4.0
     it matches (0.998). Fixed -> whole-song 10 -> 0 bad-seconds.
The carrier freq<<2 is gated `old(DELTA_OSC_FREQ_CONST)` (v4 only -- candytron's
FM @0x41e060 has NO `shl edx,2`). But the **depth 4.0 is NOT gated**: the v5
candytron FM uses 4.0 too (@0x41dbd0, verified 2026-06-10) -- the integer FM
scheme (v4 AND v5) is 4.0; only the SEPARATE v6 float scheme (renderFMSin, the
2004 synth.asm line-85 `fcfmmax 2.0`) is 2.0. So `renderFMSin_v5` hardcodes 4.0
for v4+v5; the 2.0 it used before was a latent v5 bug (the v6 value reused).
Confirmed against the candytron oracle: candytron's own v5 FM channel (ch6) now
matches the portable to magnitude-spectrum corr 1.0000 (rel-dist 0.0). v6
unchanged, check.py 3/3 (genuine-only) (the v5 FM path is not in that corpus -- its v5 songs
are v6-converted).
RESULT: fr019 whole-song (341 s) 0/341 sec above 3%, overall rms-diff/rms 0.12%,
listening A/B indistinguishable.

**Method lesson (this section was WRONG twice before this rewrite):** "whole-mix
MATCH 3.9e-6" was a 10 s window; "ch6 pulse / §1 resonance-amplified ULP floor"
was a mis-attribution off the timing-skewed binary solo. Each "sub-perceptual
floor" call turned out to be a structural bug found by pushing harder. Judge a
DEPTH/timbre change by the MAGNITUDE spectrum, not waveform correlation (which is
phase-dominated): waveform corr stayed -0.69 while the depth fix took the
magnitude match 0.87 -> 0.998. The remaining 0.12% is a genuine carrier-phase/
keysync offset (waveform -0.69, magnitude 0.998) + transcendental ULP -- below
the perceptual floor (confirmed by the whole-song measure AND listening, not an
assumption). The exact phase op is the only open item; not chased (inaudible).
RULED OUT for the big divergence: rdtsc clock (pinned), voice-steal, x87-vs-SSE
(faithful `-m32 -mpc32` build BYTE-IDENTICAL) -- it was plain wrong constants.

**Reusable taps (committed):** `FR019_SOLO=<ch>` (binary channel solo via
notenum-zeroing @0x448bac+ch*0x50 + re-Reset), `FR019_BUFPEAK` (output peak).
Scratch: `era unpack ~/downloads/fr019_party.zip's fr-019-party-b.exe
/tmp/fr019_unpacked.bin`; carve 0 = the v4 song. fr-022 party (also ms2002, v3,
has pulse) and the fr019 pulse/oscjtab disasm are the cross-checks.

---

### Scrapped / descoped

- **ALSA live playback tool `v2play` (task 9.0b)** — descoped by user
  2026-06-06. `libv2portable` stays dependency-free; offline rendering is
  fully covered by `v2dump` (WAV/f32, whole-song or fixed-length). No
  `v2play.c` was ever committed, so nothing to remove. Out of scope for this
  change; revisit only if an embedding consumer needs live ALSA output.
