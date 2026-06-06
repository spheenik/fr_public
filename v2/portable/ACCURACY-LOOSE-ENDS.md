# Portable player — accuracy loose ends

This file collects the open threads that would push the portable player
*closer to the historical hardware renders* than it already is. **None of
them is a defect.** The player is structurally faithful, deterministic, and
chunk-invariant across all supported eras (v0–v6); the two endpoint eras are
bit-exact against their genuine binaries (v0 vs the year-2000 fr08 binary,
v6 vs the 2004 `synth.asm`). What is left is sub-perceptual: in listening
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

**Status:** structurally supported and *exercised* (see §4), but the exact
flip version inside the v1..v4 gap is a documented guess for these rows.

The era assay has three PROVEN anchors — v0 (fr08 binary), v5 (candytron
binary), v6 (`synth.asm`). Ten rows are PROVEN to flip at exactly v6; seven
rows are NEW already at v5 (so they flip somewhere in v1..v4 — currently
pinned to flipsAt 1, EV_ASSUMED). The remaining `EV_ASSUMED` rows in
`v2eras.h`:

| row | current guess |
| --- | --- |
| `DELTA_OSC_BOXFILTER`   | v5 = OSM (new) |
| `DELTA_NOISE_LCG_MSVC`  | v5 = modern LCG + floatgen |
| `DELTA_OSC_FREQ_CONST`  | v5 = runtime fcoscbase |
| `DELTA_PGMCHANGE_V0`    | v5 == 2004 byte-for-byte |
| `DELTA_TICK_BEFORE_SET` | v5 = SET then TICK |
| `DELTA_SUBFRAME_RENDER` | v5 = full-frame render |
| `DELTA_RVB_E_FULLPREC`  | v5 = SRfclinfreq factor |

**To close it (optional):** the proposal's explicit follow-up — hunt the
remaining period binaries (fr-013/019/022/025, kkrieger betas) to pin the
exact v1–v4 thresholds and supply a mid-era oracle. Separate change.

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
at v6 would pass the same test). There is no period binary for v1–v4, so
era-fidelity for those eras is currently **unprovable** — no bit-exact ε can be
measured. If a mid-era binary is found (§3), these files become the comparison
corpus and the fidelity claim can finally be made.

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
