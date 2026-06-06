# v2portable — portable, version-native V2M player

A self-contained C++ player for Farbrausch **V2M** files (the V2 synthesizer
music format). It plays any format version (0–6) *natively* — no external
conversion step — and produces **bit-identical** float32 output on every
supported host. It is the embeddable counterpart to the x87-faithful
validation lab in `v2/` (which stays the reference oracle and is untouched).

- **No dependencies** beyond the C/C++ standard library.
- **No inline asm, no x87/SSE assumptions, 32/64-bit clean.**
- **Deterministic across hosts**: project-owned transcendentals + strict
  IEEE-754 binary32 arithmetic matching the original x87 PC=24 rounding
  op-for-op.

> Scope note: this player aims for **structural fidelity with a measured,
> published ε**, not universal bit-exactness to the historical hardware. The
> two endpoint eras (v0, v6) *are* bit-exact against their genuine binaries;
> see [ε table](#-table) below.

---

## Build

```sh
./build.sh                                   # full build (versions 0–6, Ronan on)
OPT=-O0 ./build.sh                            # -O0 and -O2 are bit-identical (verified)
V2DEFS="-DV2_VER_MIN=6 -DV2_VER_MAX=6" ./build.sh   # v6-only subset build
V2DEFS="-DV2_VER_MIN=0 -DV2_VER_MAX=0" ./build.sh   # v0-only subset build
V2DEFS="-DV2_RONAN=0" ./build.sh             # drop the Ronan speech synth
```

Outputs: `libv2portable.a` (the library), `v2dump` (offline render CLI), and
the test binaries under `test/`.

### Build-flag policy (mandatory for determinism)

The float32 arithmetic *is* the spec; flags that change rounding change the
output bits and break the ε guarantee.

| flag | requirement | why |
| --- | --- | --- |
| `-ffast-math` | **never** | enables FTZ/DAZ (via crtfastmath) → flushes subnormals → breaks v0 envelope semantics; also relaxes rounding |
| `-ffp-contract=off` | **always** | FMA contraction skips a rounding step → different bits |
| FTZ/DAZ (any source) | **off** | subnormal arithmetic must round normally; `open()` self-checks this and returns `FpEnvBroken` otherwise |
| `-O0` vs `-O2` | either | optimization level must not change output (verified bit-identical) |

`build.sh` sets `-ffp-contract=off` and never enables fast-math.

## API

The public interface is `v2portable.h` (namespace `v2portable`). One song per
`Player`; instances are independent (multiple may run concurrently).

```cpp
#include "v2portable.h"
using namespace v2portable;

Player p;
Result r = p.open(data, length);          // detect version, canonicalize, prepare
if (r != Result::OK) { /* BadFile | UnsupportedVersion | FpEnvBroken */ }
int v = p.fileVersion();                   // detected format version 0..6

p.setSeed(0);                              // deterministic default (replaces rdtsc)
p.play();                                  // from the start (full synth reset)

float buf[2 * 4096];
while (p.isPlaying()) {
    p.render(buf, 4096);                   // interleaved stereo f32 @ 44100 Hz
    // ... consume buf ...                 // chunk size is irrelevant to the bits
}
```

Key contracts:
- `render()` is **chunk-size invariant** — any partition of N frames yields
  identical bits — and never allocates.
- `setSeed()` replaces the historical `rdtsc` noise/S&H/dist seeding;
  `0` is the reference seed (matches the pinned-rdtsc oracle convention).
- `open(data, len, forceBehaviorVersion)` — the third arg is a **research
  knob**: `-1` (default) uses the detected version; `0..6` renders with that
  era's engine semantics (must lie inside the compiled range).

### `v2dump` CLI

```sh
./v2dump <in.v2m> <out.{wav,f32}> [secs] [chunk]
```

`.wav` = IEEE-float32 RIFF whose payload is bit-identical to the raw `.f32`
compare format. With `secs` omitted (or `auto`) it renders the whole song plus
the reverb/delay tail rung out to ~−90 dBFS and trimmed; a fixed `secs` is the
deterministic A/B mode. (MP3 is out of scope — pipe to an external encoder.)

## Version support matrix

The loader detects the format version by structural fingerprint, parses that
version's layout, and canonicalizes value-preservingly to the v6 *layout*
internal representation — while the engine switches its behavior on the
**detected source version** (not v6). The era thresholds live in `v2eras.h`.

| version | era | behavior fidelity | evidence |
| --- | --- | --- | --- |
| **v0** | year-2000 (fr08) | **bit-exact** vs the genuine 2000 binary | PROVEN |
| v1–v4 | mid-era | renders at native-era behavior; **unverified** (no oracle) | ASSUMED rows in `v2eras.h` |
| **v5** | 2003 (candytron/josie) | per-channel DSP, voice, player, voice-steal **bit-exact** vs the candytron binary | PROVEN |
| **v6** | 2004 (`synth.asm`) | **bit-exact** vs the 2004 asm | PROVEN |

v1–v4 ship structurally supported and deterministic, but their era-correctness
is currently **unprovable** — no period binary for those eras exists to serve
as an oracle. See [ACCURACY-LOOSE-ENDS.md](ACCURACY-LOOSE-ENDS.md) §3–§4.

### Era-table evidence legend (`v2eras.h`)

- **PROVEN** — verified against a period binary. Three anchors: fr08 (v0),
  candytron final (v5), `synth.asm` (v6).
- **ANCHORED** — the `sounddef.h` parameter version tables imply the feature's
  introduction version.
- **ASSUMED** — documented guess for the v1–v4 gap; default flip at v1 unless
  coupled to an anchored row. A future mid-era binary turns these into data
  edits in `v2eras.h`, never engine redesigns.

## <a name="-table"></a>ε table (measured divergence vs the oracles)

ε is the residual between this portable player and the genuine period
binaries. It is a fixed, measured quantity — the documented portable-vs-x87
transcendental-tie class (native `sin`/`atan` vs x87 `fsin`/`fpatan` at razor
ties), not drift.

| path | oracle | window | max\|d\| | rms\|d\| | notes |
| --- | --- | --- | --- | --- | --- |
| **v0** `fr08.v2m` | C1 (year-2000 binary) | whole 663 s | 8.53e-7 (~3 ULP @ full scale) | 1.66e-8 | first 14.774 s bit-exact; bounded, re-converges to 0 at the tail ⇒ no structural desync |
| **v0** `fr08.v2m` | C1 | 60 s | 1 ULP | 5.7e-9 | |
| **v6** `pzero_new.v2m` | `harness_asm` (2004) | exact | **0** | **0** | bit-exact |
| **v6** `v2_zeitmaschine_new.v2m` | `harness_asm` | exact | **0** | **0** | bit-exact |
| **v5** `josie.v2m` (Ronan on) | candytron final binary | whole song | — | ~0.009 (music-bed) | per-channel DSP / voice / player / voice-steal **bit-exact**; residual = ch3/ch6 late-song osc-phase razor ties (§1 loose ends) |

Scope ruling (2026-06-05): `converted/` files played at v6 behavior carry **no
oracle-identity contract** — they are a lab construct and most are not
genuinely v6 (originals span v0–v6). The ε contract covers **originals at their
true era** only (fr08 v0 vs C1; `pzero_new`/`v2_zeitmaschine_new` v6 vs the
asm; josie v5 vs the candytron binary). Converted files remain in the
*determinism* (hash) baseline only.

## Determinism / regression test suite

```sh
./build.sh && \
  test/mathcheck && test/twoinstance && test/tablecheck && \
  test/loadcheck ../v2m/fr08.v2m ../v2m/converted/fr08.v2m ../v2m/pzero_new.v2m ../v2m/v2_zeitmaschine_new.v2m && \
  test/forcecheck ../v2m/pzero_new.v2m && \
  python3 test/check.py            # corpus hash baseline (+ --oracle-dir for ε)
```

- `mathcheck` — owned transcendentals vs genuine x87 at PC=24 (0 mismatches in 25M+ points).
- `twoinstance` — two concurrent players are independent.
- `tablecheck` — portable per-version size/offset tables vs the lab `sounddef.h`.
- `loadcheck` — v0→v6 canonicalization byte-equals `conv_v2m`; v6 identity; corrupt-data rejection.
- `forcecheck` — `forceBehaviorVersion` both ways + out-of-range rejection.
- `check.py` — renders the corpus, checks per-file hashes against the checked-in
  `baselines.sha256` (determinism contract); `--oracle-dir` adds the ε layer.

## Follow-up research track (future work, separate changes)

The era table has three proven anchors (v0/v5/v6); the v1–v4 thresholds are
documented guesses. Pinning them is a **period-binary hunt**, not engine work:
unpack the remaining era executables (fr-013/019/022/025, kkrieger betas) to
supply a mid-era oracle, then the in-repo v1–v4 originals
(`tpinv2` v1, `whatever07` v3, `loading` v4 …) become the comparison corpus and
the ASSUMED rows become data edits in `v2eras.h`. Cross-host determinism on a
second arch (aarch64) is the other open verification. Full list:
[ACCURACY-LOOSE-ENDS.md](ACCURACY-LOOSE-ENDS.md).

---

Living threshold ledger: [`v2eras.h`](v2eras.h). v0-side characterization:
[`../v2m/fr08-extraction/DELTA.md`](../v2m/fr08-extraction/DELTA.md). v5 assay
+ candytron extraction: `../v2m/candytron-extraction/NOTES.md`.
