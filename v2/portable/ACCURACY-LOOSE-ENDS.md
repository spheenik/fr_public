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
**To close the rest (optional):** v3 and v2 still have no period engine
(fr-022 turned out v5, not the hoped-for v3), so the in-gap flips
(including the 16→32 pool growth) stay proxied at flipsAt 5.

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
whole-song bit-exact against the 2001 binary itself. **v2/v3/v4 still have
no period engine** (fr-022 is v5), so their era-fidelity remains unprovable
for now — those files stay liveness-only until a v2/v3/v4 binary surfaces.
Note kkrieger6's *native-v5* render changed with the era voice pool (it
saturates 32 voices at 102.47 s; josie never does) — evidence-backed but
oracle-less, since no period engine that plays kkrieger6 exists.

## 5. CC1 mod-dest remap re-audit (low priority)

Open lead from the 6.2 hunt: re-verify the v5→v6 mod-dest remap puts these on
the right canonical param — mod3 dest67=boost.amount, mod4 dest65=aux2/delay-
send, mod5 dest13=osc2.vol. No observed divergence; flagged for completeness.

## 6. Cross-host / cross-arch determinism (task 8.2)

The hash contract is implemented and `baselines.sha256` is checked in, but
the equal-bits-on-a-second-host check has only been run on x86_64 this
session. Re-run `test/check.py` on one other arch (e.g. aarch64) and record
the result to close 8.2.

---

### Scrapped / descoped

- **ALSA live playback tool `v2play` (task 9.0b)** — descoped by user
  2026-06-06. `libv2portable` stays dependency-free; offline rendering is
  fully covered by `v2dump` (WAV/f32, whole-song or fixed-length). No
  `v2play.c` was ever committed, so nothing to remove. Out of scope for this
  change; revisit only if an embedding consumer needs live ALSA output.
