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
| `DELTA_OSC_BOXFILTER`   | **fixed → flipsAt 5.** flybye(v1) is OLD (box) — old `flipsAt 1` was WRONG. |
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
for v0/v5/v6 (check.py still 17/17). The lone unrepresentable case is an
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
The osc trio + TICK/SUBFRAME/RVB_E STAY flipsAt-5 proxies but the sweep
confirmed their build-date flip is the one month **2002-08→09** (old-core thru
fr-027 v5 2002-07, new at fr-028 brullwurfel 2002-09; fr-027 joins fr-022 as an
early-v5 old-core unrepresentable case). check.py still 17/17 (these gates are
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
candytron. v3/v4 still lack a *render* oracle (assay-only), and **v2 still has no
binary at all** — those remain liveness-only. kkrieger6's native-v5 pool note
stands (saturates 32 voices at 102.47 s; josie never does).

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
`#ifndef NDEBUG`, dumps a channel's post-mod voice config). check.py 17/17.

NOTE: the brullwurfel unpacked image + carved song1 live in scratch (re-unpack
`~/downloads/fr-028.zip` via `../v2m/toolkit/era`; song1 is carve index 1). The
candytron image re-unpacks from `~/downloads/fr-030_candytron_final.zip`
(kkrunchy) to `/tmp/candytron/unpacked.bin`.

## 8. v3 (fr014) render oracle — built; one channel (ch8) diverges, OPEN

**Status (2026-06-08): v3 render oracle BUILT & working; v3 is render-validated
EXCEPT one channel. Root cause NOT yet pinned — engine math proven bit-faithful,
divergence localized to ch8's amplitude-envelope/voice-volume path. RESUMABLE.**

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

**What is RULED OUT (all measured, not theorized):**
- *Loader color misparse* — binary ch8 osc nfres = **1.0**, identical to portable.
- *Noise seed* — both **0** (rdtsc→0). *nffrq/nfres* — **bit-identical**
  (`3f790e5f`/`3f800000`, all 3 oscs). *Recurrence* — decoded @0x40e7e1,
  bit-identical to `renderNoise_v0`. *x87 precision* — binary runs **PC=24** =
  float32; `double`-precision noise had **ZERO effect** (peak identical 3.1879);
  the recurrence is intrinsically **bounded** (sim: 1 osc maxes ~4.5, 3
  correlated oscs sum to ~13 in BOTH). **So this is NOT an x87/precision/
  marginal-stability problem** — mimicking x87 would not help.
- *Keysync / noise-filter accumulation* — resetting `nf` on note-on = zero effect.
- *Master compressor* — bass (<500 Hz, other channels) NOT ducked at 7–8 s.
- *Static volume* — lvol/rvol = **0.7071** both.
- *env2→flt0.cutoff mod* — IS mis-applied (portable closes flt0 cfreq→0.143,
  binary leaves it open ~0.98) but skipping it only moved 3.19→**2.57** (~20%);
  a contributor, NOT the main cause. (NB the v2load mod-dest remap @v2load.cpp:344
  produces dest=21=flt0.cutoff for ch8's env2 mod, and that matches both the
  `kPatchParmVer` table and `../sounddef.h` — so the remap is "correct" by the
  table yet the binary doesn't close flt0; see open question below. This is §5.)

**LEADING (unconfirmed) hypothesis — amplitude envelope / curvol.** At the ch8
peak (~7.249 s) the binary's voice `curvol` ≈ **0.118** vs the portable's ≈
**0.8–0.97** (~7×) — the dominant ~5× factor (osc/filters/bitcrusher just scale
this). Points to a v3 **amplitude-envelope** behavior (attack/decay curve, or the
vel→aenv.amplify mod: ch8 mod0 src=0 dest=37) differing from the portable's model.
`DELTA_ENV_CURVES` is **flipsAt 2 EV_ANCHORED** (an unproven guess) — a candidate
the oracle now contradicts for ch8.

**TENSION to resolve:** other channels (incl. their envelopes) match at ~5e-4, so
it is NOT a blanket env-curve error — it is specific to ch8's env params / vel-mod
regime. **CAVEAT:** the binary voice-field offsets used for the curvol read
(curvol@+0xc, lvol@+0x1c, rvol@+0x20) were *borrowed from brullwurfel's v5 layout*
and are NOT verified for fr014 — good enough to see a ~7× gap, NOT to draft a fix.

**HOW TO RESUME (the genuinely-final step):**
1. Build a **verified fr014 voice-field map**: disasm the env setup (env init
   0x40e9c2 @ voice +0xe4 (aenv) / +0xfc (env2); filters 0x40e85f @ +0x114/+0x138)
   to pin the real `curvol`/`val`/`out`/`atd`/`dcf` offsets — same method that
   pinned the osc fields (nffrq@osc+0x14, nfres@+0x18; osc bases +0x30/+0x6c/+0xa8).
2. With verified offsets, dump the binary's ch8 **aenv trajectory** (out/val/state
   + atd/dcf/sul/suf) over 7.2–7.5 s and compare to the portable's `env[0]`
   (V2_VOL tap already prints curvol/flt0.cfreq/env2.out/aenv.out). Decide:
   attack-rate, decay-rate, sustain, or the vel→amplify mod.
3. If the v3 env curve differs: it is a `DELTA_ENV_CURVES`/`DELTA_ENV_CLAMP_SUSREL`
   era-threshold correction (both currently ANCHORED/PROVEN-at-v5, never v3-proven).
   Test by adjusting the gate and re-running the oracle; the matching-channels
   tension MUST stay satisfied (don't regress 0–7 s) and check.py stays 17/17.
4. The fr014 binary IS the ground truth (the v3 patch played natively); the
   portable must match it, not the converted-v6 interpretation.

**Reusable taps (dev-only, `#ifndef NDEBUG`, env-gated, output-neutral; committed):**
- portable `v2core.cpp`: `V2_PATCHDUMP=<ch>` (post-mod voice config + mod matrix),
  `V2_VCETAP` (per-stage osc/flt/dist/dcf peaks), `V2_NSEED` (noise seed +
  nffrq/nfres hex), `V2_VOL` (curvol/flt0.cfreq/flt1.cfreq/env2.out/aenv.out).
- portable `v2load.cpp`: `V2_MODREMAP` (raw v3 dest → remapped v6 dest per mod).
- harness `c1_fr014_harness.c`: `FR014_DUMPVOX=<sec>` (one-shot voice-workspace
  float dump for v6), `FR014_SOLO=<ch>` (zero other channels' notenum in the
  parsed table = binary-side channel solo).
- Scratch: `era unpack ~/downloads/fr014.zip`'s `mark&sweep.exe` →
  `/tmp/erascan_recon/fr014/unpacked.bin`; carve 0 = song0.v2m (v3, the divergent
  song); carve 1 = song1.v2m (v1). Portable solo: `V2SEQ_SOLO=8`.

NOTE: the v3 oracle's first commit message (719fe10) names a "v3 patch param
parsing (color misparse)" prime suspect — that was **DISPROVEN** afterward
(binary nfres=1.0); this section supersedes it.

---

### Scrapped / descoped

- **ALSA live playback tool `v2play` (task 9.0b)** — descoped by user
  2026-06-06. `libv2portable` stays dependency-free; offline rendering is
  fully covered by `v2dump` (WAV/f32, whole-song or fixed-length). No
  `v2play.c` was ever committed, so nothing to remove. Out of scope for this
  change; revisit only if an embedding consumer needs live ALSA output.
