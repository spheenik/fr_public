## Why

The `v2eras.h` ledger had three PROVEN anchors — v0 (`fr08v101.exe`), v5
(candytron), v6 (`synth.asm`) — but the v1..v4 gap rested on seven
`EV_ASSUMED` rows (pinned `flipsAt 1` by default policy) and two
`EV_ANCHORED` env/frame bets. No period engine existed for that gap, so the
mid-era behavior was deterministic but **unprovable** (see
`v2/portable/ACCURACY-LOOSE-ENDS.md` §3–4).

Two period binaries were obtained and statically unpacked, and the result was
not what was expected:

- **`flybye.exe`** (fr-013, 2001-12) — ships `tpinv2.v2m`, detected **v1**.
  This is the real prize: a genuine v1 oracle.
- **`fr-022.exe`** (ein.schlag, 2002-08) — was *expected* to be the v3 oracle
  (its repo standalone `whatever07.v2m` is v3). But the **binary embeds a v5
  export** (globSize 22, the v5 LFO-Polarity param). So fr-022 is a *second
  v5 binary*, not a v3 oracle — **the v3 era still has no period engine.**

That accident turned out more valuable than a v3 confirmation: fr-022 (v5,
2002-08) and candytron (v5, 2003-08) are the **same format version with
opposite DSP cores**, which exposed that a whole class of behavior deltas
tracks the engine **build date**, not the format version — something the
format-version era model could not represent. Concretely, the assay found the
ledger's `flipsAt 1` for the osc-core trio was simply **wrong** (flybye proves
v1 is OLD), and proved no single format threshold can ever be exact for it.

## What Changes

- **Unpack + assay** both binaries with the proven candytron methodology
  (locate synth routines by x87/constant signatures, read each era row). Tools
  + provenance + the full write-up are preserved under
  `v2/v2m/flybye-extraction/` (`unpack.py`, `erascan.py`, `NOTES.md`, the two
  embedded v2ms), mirroring `fr08-extraction/` and `candytron-extraction/`.
- **Fix the osc-core trio** (`DELTA_OSC_BOXFILTER`, `DELTA_NOISE_LCG_MSVC`,
  `DELTA_OSC_FREQ_CONST` — one coupled convention): `flipsAt 1 → 5`. flybye
  proves they are OLD at v1 (MSVC LCG, baked 3185015, box filter; modern
  variants absent). `flipsAt 5` is the best format proxy — OLD v0..v4, NEW
  v5..v6 — chosen because it is a **no-op for every oracle-bearing file**
  (v0 fr08, late-v5 josie/kkrieger6, v6 corpus): `test/check.py` stays 17/17.
- **Fix the four player/render rows** (`DELTA_PGMCHANGE_V0`,
  `DELTA_TICK_BEFORE_SET`, `DELTA_SUBFRAME_RENDER`, `DELTA_RVB_E_FULLPREC`):
  `flipsAt 1 → 5`. These have no constant signature, so they were settled by
  disassembling flybye's player/render code — byte-for-byte structurally
  identical to fr08 (v0), hence OLD at v1. Three (TICK/SUBFRAME/RVB_E) share
  the trio's build-date flip (still OLD in early-v5 fr-022, NEW in late-v5
  candytron); PGMCHANGE flipped earlier (already NEW in fr-022). Also a no-op
  for the oracle corpus (v5 stays NEW). **No `EV_ASSUMED` row remains at
  flipsAt 1.**
- **Document the model limitation, with measurements.** The trio flip is
  build-date-tied; an early-v5 old-core file like fr-022 is unrepresentable.
  Rendering fr-022 under `flipsAt 5` (new-core) vs its true old-core measures
  **89% relative-RMS / 0.63 correlation** error (dominated by the tonal core,
  not just noise — full breakdown in the extraction NOTES). A faithful render
  would need a build-era signal the v2m does not carry; deferred as the
  `v2eras.h` "model change" option unless early-v5 files enter the corpus.
- **Corroborate the anchored rows from kb's own converter** (no binary
  needed): `v2mconv.cpp`'s disabled `transEnv` (targets format<2) confirms the
  `ENV_CURVES`/`FRAME256` flip at v2; the `sounddef.h` param-version tables
  confirm comp/boost@v1, reverb-lowcut@v4, aux@v6.
- **Update** `ACCURACY-LOOSE-ENDS.md` §3–4 to the new status.

## Capabilities

### New Capabilities
<!-- none: adds evidence + a threshold fix to an existing capability -->

### Modified Capabilities
- `era-gated-engine`: the "Mid-range versions render with documented threshold
  semantics" requirement gains v1 period-binary evidence (the osc-core trio
  corrected from a wrong `flipsAt 1` to `flipsAt 5`, EV upgraded from a wrong
  guess to binary-read). The capability also gains an explicit statement that
  a subset of deltas is build-date-tied and therefore only *proxied* by format
  version, with one documented unrepresentable case (early-v5 old-core).

## Impact

- **Data:** `v2/portable/v2eras.h` — trio thresholds `1 → 5` + build-date
  rationale comment. No engine code, no API changes. v5/v6/v0 renders
  bit-identical; only v1..v4 native renders change (and only for the better —
  v1 now proven-correct). `test/check.py` 17/17 unchanged.
- **Docs:** `v2/portable/ACCURACY-LOOSE-ENDS.md` (§3–4), new
  `v2/v2m/flybye-extraction/` (NOTES.md + tooling + embedded v2ms). Source
  exes are **not** committed (copyrighted); reproduce via `unpack.py`.
- **Scope explicitly NOT closed by this change** (documented, not silently
  dropped):
  - No flybye/fr-022 *render-harness oracle* yet — the v1/v5 claim is
    correct-by-row (disassembly), not bit-exact. A c1/c2-style harness is the
    follow-up that would upgrade it.
  - **v3 and v2 still have no period engine** (v2 has no period file at all) —
    so the exact in-gap flip of the build-date rows stays unpinned; `flipsAt 5`
    is a documented proxy, not a measured boundary.
