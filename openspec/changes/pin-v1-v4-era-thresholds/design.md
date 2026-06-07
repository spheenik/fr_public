# Design — pin-v1-v4-era-thresholds

Most of this change is data edits + documentation (already landed in commits
`8f47cd4` and `e9ce38e`). The one real design decision is how to represent
the **voice-pool size** (16 → 32 → 64) in the era machinery, and how to
apply it in the engine with provable equivalence to the period binaries.

## D1: Pool size as two composed boolean rows (not a value-typed row)

The ledger (`v2eras.h`) is a table of boolean deltas consumed via
`oldBehavior(DELTA_X, version)`, with `V2_VER_MIN==MAX` builds folding every
gate to a compile-time constant. The pool size is the first *value-typed*
era difference (16/32/64). Options:

- **(a) Two boolean rows** — `DELTA_POLY_16` ("pool is 16, not bigger") and
  `DELTA_POLY_32` ("pool is 32, not 64"), composed as
  `old(POLY_16) ? 16 : old(POLY_32) ? 32 : 64`. **Chosen.** One mechanism,
  one evidence column per flip, constant-folding preserved for free, and a
  `static_assert` enforces monotonicity (`POLY_16.flipsAt <=
  POLY_32.flipsAt`). Each flip also genuinely carries *different evidence*
  (16→32 is an in-gap proxy; 32→64 is PROVEN at both endpoints), which a
  single value row could not express.
- (b) A separate `constexpr int voicePoolSize(int)` table — cleaner to read
  but introduces a second ledger mechanism and a second evidence convention
  for one row. Rejected.

Thresholds:

| row | flipsAt | evidence | basis |
| --- | --- | --- | --- |
| `DELTA_POLY_16` | 5 | ASSUMED | 16 proven at v0 (fr08 `cmp dl,0x10` @0x40b9e4) and v1 (flybye @0x41000b, steal scan @0x410393); already 32 at early-v5 fr-022 (2002-08, @0x40f322). The in-gap flip (v2..v4) is unpinned — same shape as `DELTA_PGMCHANGE_V0`, same flipsAt-5 proxy convention. |
| `DELTA_POLY_32` | 6 | PROVEN | 32 at BOTH v5 binaries (fr-022 @0x40f322, candytron @0x41f96a — the two v5 builds agree, unlike the osc trio); 64 in 2004 `synth.asm` (`%define POLY 64`). |

## D2: Bound only the allocation-eligibility scans; arrays stay at 64

`POLY` sizes arrays (`chanmap`, `allocpos`, `voicesv/w`) and bounds ~10
loops. Shrinking everything is unnecessary and invasive. Equivalence
argument: **a voice with index >= pool size is never allocated if and only
if the allocator never scans it** — and every other voice loop (tick, frame
render, note-off, CC120/123, PGM kill) is a no-op for voices with
`chanmap[i] == -1`. So bounding exactly these four allocator loops in
`processMIDI` note-on is *provably equivalent* to a smaller pool:

1. the per-channel `npoly` count,
2. the find-free scan (`chanmap[i] < 0`),
3. the gate-off steal scan,
4. the oldest steal scan.

Implemented as a small `poolSize()` helper on `V2Synth` reading the two
gates via `instance.old(...)` (the same accessor every other gate uses), so
it constant-folds in single-version builds exactly like boolean gates.

Empirical confirmation (the hunt, /tmp/fr013): a build with POLY=16
wholesale rendered the entire 144.5 s flybye song bit-exact vs the 2001
binary; bounding only the allocator scans must produce the same allocations
by the argument above (and is re-verified against the oracle in tasks).

## D3: Verification matrix

| render | expectation | why |
| --- | --- | --- |
| flybye embedded v1, whole song vs `c1_flybye.f32` | rel-RMS ~4e-5, max\|d\| ~1.5e-4, **no sample >1e-3** | the divergence at 66.42 s disappears; only the ambient ULP wobble remains |
| `test/check.py` | 17/17, **baselines unchanged** | corpus = v6-converted + native v6 + native v0 fr08; v6 pool is 64 (no-op), fr08 never saturates 16 (its 2640 allocations were already identical vs the 64-voice port) |
| josie original (v5) 32 vs 64 | bit-identical | measured during the hunt: pool never saturates; oracle corpus untouched |

## D4: The hunt probe is preserved as a committed tool

`c1_flybye_solo.c` (channel-solo + alloc-trace detours on the flybye image,
the fr08 `c1_solo_probe.c` methodology re-addressed) goes into
`v2/v2m/flybye-extraction/` next to the harness: it is the instrument that
found the row and the one that re-verifies it; per the extraction-dir
convention (fr08/candytron precedents) tools are committed, demo binaries
are not.
