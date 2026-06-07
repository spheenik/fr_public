# flybye (fr-013) + ein.schlag (fr-022) extraction & v1/v5 era assay

The v1 oracle hunt (and an unexpected v5 cross-check). Analogue of the fr08
(v0) and candytron (v5) extractions, aimed at the `v2eras.h` v1..v4 gap —
the seven `EV_ASSUMED` rows and the two `EV_ANCHORED` env/frame bets that no
period engine had ever pinned (see `../../portable/ACCURACY-LOOSE-ENDS.md`
§3–4).

## Provenance

| file | what | sha256 | dist date (file mtime) |
| --- | --- | --- | --- |
| `fr013_unoff.zip` | fr-013 flybye, unofficial release | `60b20306…b9ff852e` | — |
| `flybye.exe`      | PE32, 65536 B | `e3450e40…a4ecc7e0` | **2001-12-01** |
| `fr-022_final.zip`| fr-022 ein.schlag, final | `2774a44e…6f8addac` | — |
| `fr-022.exe`      | PE32, 65024 B | `5f547c0b…08d6c196` | **2002-08-03** |

Source zips: scene.org releases, in the user's `~/downloads/`. The exes are
**not** committed (copyrighted demo binaries); reproduce the analysis with
`unpack.py` against your own copies. flybye plays a v1 song; ein.schlag's
final embeds a v5 song. With the fr08 (v0, 2000) and candytron (v5, 2003-08)
anchors this gives a five-point build timeline: 2000 → 2001-12 → 2002-08 →
2003-08 → 2004.

## Unpacking (`unpack.py`)

Both exes use ryg's `rygs and packer.` aPLib stub (3 sections; section[0]
"rygs and" is the zero-rawsize in-place decompression target at RVA 0x1000,
section[1] "packer." holds the packed stream). This host's
`ptrace_scope=1` blocks the fr08 live-wine `/proc/pid/mem` recipe (the dumper
is not an ancestor of the wineserver-reparented guest → `EACCES`), so we use
the **static** route: map the image, emulate from the entry point under
unicorn, let the stub decompress in place. It halts with
`UC_ERR_INSN_INVALID` when it jumps to the unresolved OEP — decompression is
already complete (imports are never patched, we only want the image).

    pip install unicorn            # 2.1.4
    ./unpack.py flybye.exe  flybye_unpacked.bin   # 0x2ef000, ~5.6M instrs, 5.0% nonzero
    ./unpack.py fr-022.exe  fr022_unpacked.bin    # 0x340000, ~5.7M instrs, 3.3% nonzero

Both images disassemble as clean post-OEP x86 in the synth region.

## Embedded V2Ms (`../fr08-extraction/findv2m3.py`)

Carved with the structural scanner, version detected by the portable loader
(`v2dump`), and confirmed by parsing globSize + the patch offset table:

| committed file | from | off (VA) | size | **format** | parse |
| --- | --- | --- | --- | --- | --- |
| `flybye_embedded.v2m` | flybye | 0x41bd9d | 26080 | **v1** | globSize 21, gap consistent only with 78 patch parms |
| `fr022_embedded.v2m`  | fr-022 | 0x42243c | 31022 | **v5** | globSize 22, gap consistent only with 85 patch parms |

Both are committed here: they are the *binary's own export* and are the
correct oracle inputs (the candytron lesson — the shipped export need not
match a repo copy). Notes:

- `flybye_embedded.v2m` is byte-identical to the repo `RG2/flybye/tpinv2.v2m`
  for all 26076 of tpinv2's bytes (the carve is 4 bytes longer). So flybye is
  a clean **v1** oracle for tpinv2.
- `fr022_embedded.v2m` is **v5**, NOT the v3 of the repo standalones
  `RG2/einschlag/whatever07.v2m` / `whateverload2.v2m`. It shares
  `whateverload2`'s MIDI stream (same song) but carries the v5 LFO-Polarity
  patch param and globSize 22 in the tail. **fr-022 ships a v5 export** — so
  it is a second v5 binary, not the v3 oracle originally hoped for. (The v3
  era therefore still has NO period engine.)

## Era assay (`erascan.py`)

Read each constant/opcode-decidable row straight out of the synth region.
Synth region located by the rdtsc cluster + native fpatan:
flybye ≈ 0x40e000–0x40f000, fr-022 ≈ 0x40d000–0x40e000.

| row | flybye **v1** | fr-022 **v5** | candytron **v5** | fr08 **v0** |
| --- | --- | --- | --- | --- |
| `NOISE_LCG_MSVC` | **OLD** MSVC LCG @0x40e7dc; modern absent | **OLD** MSVC @0x40da0a; modern absent | NEW modern @0x41dff5 (synth); MSVC only in player @0x412ba5 | OLD MSVC @0x409fbc |
| `OSC_FREQ_CONST` | **OLD** baked 3185015 @0x40e5c0 | **OLD** baked @0x40d668 | NEW runtime (baked absent) | OLD baked @0x40a45c |
| `OSC_BOXFILTER` | OLD (coupled to freq) | OLD (coupled) | NEW OSM | OLD box |
| `RDTSC_SEED` | OLD 3× rdtsc | OLD 2× rdtsc | OLD rdtsc | OLD rdtsc |
| `NATIVE_FPATAN` | OLD fpatan @0x40ec2f | OLD @0x40df13 | OLD @0x41e60b | OLD |
| `NATIVE_FSIN` | OLD fsin present | OLD present | OLD present | OLD |
| `NO_DCOFFSET` | OLD (2^-18 absent) | OLD (absent) | OLD (absent) | OLD |
| fixed oscseed table | absent | absent (lone stray 0xdeadbeef @0x4056f3, other two absent → not the table) | absent | absent |

### Headline finding: the osc-core trio is build-date-tied, not format-tied

`NOISE_LCG_MSVC`, `OSC_FREQ_CONST`, `OSC_BOXFILTER` (one coupled convention —
the 4× phase advance + baked freq, see the `static_assert` in `v2eras.h`):

```
                          build      core
  v0  fr08      2000      OLD  (MSVC LCG, baked 3185015, box filter)
  v1  flybye    2001-12   OLD
  v5  fr-022    2002-08   OLD   ┐ SAME format version, OPPOSITE DSP core:
  v5  candytron 2003-08   NEW   ┘ the modernization landed in this 12-month gap
  v6  synth.asm 2004      NEW
```

Two v5 binaries one year apart disagree. The flip is on the engine
**build-date** timeline and format v5 straddles it; a v2m records only its
format version, never its build, so the file is genuinely under-specified for
these three rows. This was previously **`flipsAt 1, EV_ASSUMED`** with a
guess of "v5 = modern" — flybye proves v1 is OLD (the guess was wrong), and
fr-022 proves the proxy can never be exact.

**Fix applied:** trio → `flipsAt 5` (`v2eras.h`). OLD for v0..v4, NEW for
v5..v6. This is the best format proxy: correct for every oracle-bearing file
(v0 fr08, late-v5 josie/kkrieger6 stay NEW exactly as before, v6 corpus), and
a no-op for v0/v5/v6 so `test/check.py` is still 17/17. The single
unrepresentable case is an early-v5 old-core file like fr-022 itself.

### Cost of the unrepresentable case — fr-022 old-core vs new-core

How wrong is fr-022 rendered under `flipsAt 5` (i.e. as new-core when it is
actually old-core)? Measured on `fr022_embedded.v2m`, 40 s, force v4
(old-core, correct) vs force v5 (new-core, what the model gives it) — v4↔v5
differ *only* by the trio, so this is exactly the error:

| component isolated | relative RMS error | correlation | perceptual read |
| --- | --- | --- | --- |
| noise LCG only | 46.4% | 0.904 | benign — a different but statistically-equivalent noise sequence |
| osc-freq + box-filter only | 70.7% | 0.750 | **real** — altered oscillator timbre/tuning |
| **all three (the actual error)** | **89.4%** | **0.633** | clearly, audibly a different render (peak 1.83 → 2.83) |

So the error is large and **dominated by the tonal core**, not just noise
reshuffling. fr-022 genuinely cannot be rendered faithfully by a format-only
model. (Method note: isolations require rebuilding BOTH `libv2portable.a`
*and* any statically-linked harness between each row toggle — a stale
harness silently reuses the prior core. The numbers above are post-fix.)

Rendering early-v5 files correctly would need a build-era signal the v2m
lacks (the `v2eras.h` "model change" option). The measured prize for that
work: recovering this 89% / 0.63 error — worth it only if early-v5 files ever
enter the corpus.

### Resolution: the fr-022 song is NOT actually lost (the v3 twin)

The fr-022 binary embeds exactly ONE v2m — the "whateverload2" song, at v5.
The repo `RG2/einschlag/whateverload2.v2m` is the SAME song at **v3** (its
MIDI stream is byte-identical to the embedded copy through all 30166 MIDI
bytes; they diverge only in the param tail, v3 vs v5 layout). (The other
einschlag standalone `whatever07.v2m` is a *different*, main tune, also v3,
NOT embedded in the exe.) So we hold the same music in two formats — and the
v3 one renders correctly, because after the trio fix v3 < 5 selects the OLD
core, which is what fr-022's binary used.

Three measurements (30 s; `forceBehaviorVersion` via the eradump harness;
"truth" = the embedded v5 file forced to v4 = old core + the v4 reverb-lowcut,
which fr-022 being v5-format also has):

| test | rel-RMS | corr | meaning |
| --- | --- | --- | --- |
| v3 repo vs v5 embedded, **both forced v4** | **0.000%** | **1.000000** | bit-identical — provably the same song; v3↔v5 is pure format/layout |
| v3 repo **native (v3)** vs truth | 1.98% | 0.9998 | the v3 copy renders ~correctly; off only by the v4 reverb-lowcut (v3 < 4) |
| v5 embedded **native (v5)** vs truth | 77.6% | 0.68 | the wrong default — new core, badly off |

**Proof the v5 is a value-preserving up-conversion of the v3** (`canon.cpp`):
canonicalize both copies to the common v6 layout and diff the bytes —

    ./canon ../../../RG2/einschlag/whateverload2.v2m fr022_embedded.v2m
    whateverload2.v2m: detected v3 -> canon 31055 bytes
    fr022_embedded.v2m: detected v5 -> canon 31059 bytes
    canonicalized-to-v6 byte-identical: NO (differing bytes 0 / 31055 shared)

**Zero** differing bytes across all 31055 shared bytes; the v5 copy is 4 bytes
longer (non-semantic trailing tail — test [1]'s bit-identical *audio* proves
those 4 bytes don't render). So the param values + MIDI are provably the same
data: the embedded v5 is `ConvertV2M(v3)`, audio-neutral. The format step
v3→v5 changed layout, not behavior. (Mechanism: the editor saves at its
current format version; the Aug-2002 build's exporter up-converted the v3
project to v5 at embed time, exactly as the loader's canonicalizer does.)

Conclusions:
- There was never "a v5 that lacked the modern changes." v5 is a single
  *format*, written by TWO engine builds a year apart: Aug-2002 (old core)
  and Aug-2003 (candytron, new core). The conversion picks the **format**; the
  build picks the **core**. fr-022 sounds old because its *build* was old-core.
- The format-vs-build collision has an **accidental escape hatch for this
  song**: the v3 representation aligns format-era with build-era (a 2002
  v3-format file → old core; the 2002 engine *was* old core). The v5
  representation is the "lie" — a v5-format wrapper around an old-core song,
  more dishonest about fr-022's true sound than the v3 copy it was made from.
- Neither single native copy is exact: v5-native has lowcut but the wrong
  (new) core; v3-native has the right (old) core but no lowcut (v3 < 4).
  **fr-022's exact truth = old core WITH lowcut = `forceBehaviorVersion=4` on
  EITHER copy** (test 1 confirms both give the identical result).
- So the "model change" override we'd need **already exists** —
  `forceBehaviorVersion`. fr-022 renders bit-exact-to-old-core at forced v4;
  the only thing the file can't supply is the *auto-detection* that a v5 file
  wants behavior-4. With known provenance (as for our corpus) the knob
  suffices; auto-detection from the bare file remains impossible.

## Disassembly: the four player/render rows (no constant signature)

These four had no constant/opcode tell, so they were read by disassembling
flybye's player/render code (capstone over the flat unpacked image,
VA = 0x400000 + offset) and comparing to the known v0 (fr08) and v6
(`../../synth.asm`) shapes. flybye's player/render code is **byte-for-byte
structurally identical to fr08** (same opcodes; only data/call addresses
differ) — so all four are **OLD at v1**:

| row | flybye routine | == fr08 | the OLD tell flybye has |
| --- | --- | --- | --- |
| `PGMCHANGE_V0`    | `@0x410455` | `@0x40be15` | no same-program early-out; writes `ctl7=127` (`f3aab07faa` @0x410482) |
| `SUBFRAME_RENDER` | render drv `@0x40ff83` | `@0x40b95c` | `ecx=min(todo,framectr)` sub-frame chunks |
| `TICK_BEFORE_SET` | render drv `@0x40ff83` | `@0x40b95c` | control TICK (`call @0x40ee66`) at the chunk's trailing edge |
| `RVB_E_FULLPREC`  | `syReverbSet @0x40f89e` | `@0x40b2d0` | `e=sqr(64/(t+1))` (`def1dcc8` @0x40f8ab, const 0x40e574=64.0); NO `SRfclinfreq`, no PC change |

Threshold (where each flips), from the early-v5 (fr-022) / late-v5 (candytron)
binaries — **two distinct patterns**:

- **TICK_BEFORE_SET, SUBFRAME_RENDER, RVB_E_FULLPREC** — still OLD in early-v5
  fr-022 (render driver sub-frame struct `@0x40f2ae`; reverb has no
  `SRfclinfreq`, `fmul st0,st0` @0x40ec88 goes straight to the pow loop), NEW
  only in late-v5 candytron (reverb `fmul [SRfclinfreq]` @0x41f27d). Same
  **build-date flip as the trio** → `flipsAt 5` proxy; early-v5 fr-022 is the
  unrepresentable case for these too.
- **PGMCHANGE_V0** — already NEW in early-v5 fr-022 (`ctl7=127` signature
  absent), so it flipped *earlier* (v2..v5, by 2002), before the render-driver
  rewrite. `flipsAt 5` is still correct at every known point (v0/v1 OLD, v5
  NEW) but the exact gap flip is unpinned.

All four moved `flipsAt 1 → 5`. They keep v5 NEW, so the oracle corpus is
unchanged (`check.py` 17/17). The engine already gated on all four
(`v2core.cpp` 3044/3520/3803/3819/4002) — pure data edits.

## Status against the v1..v4 gap

- **Pinned by this assay (data edits in `v2eras.h`):** the osc-core trio AND
  the four player/render rows (PGMCHANGE/TICK/SUBFRAME/RVB_E) → `flipsAt 5`
  (all were a wrong `flipsAt 1`). **No `EV_ASSUMED` row remains at flipsAt 1.**
- **Confirmed already-correct:** `RDTSC_SEED`, `NATIVE_FSIN/FPATAN`,
  `NO_DCOFFSET` all OLD at v1 & v5 (consistent with their `flipsAt 6`).
  `ENV_CURVES`/`FRAME256` (`flipsAt 2`) and the comp/lowcut/aux feature rows
  independently corroborated by the converter (`../../v2mconv.cpp` disabled
  `transEnv` targets format<2; `../../sounddef.h` param-version tables).
- **Not yet done:** a flybye/fr-022 *render-harness oracle* (c1/c2-style) to
  prove the v1/v5 native render is bit-exact, not merely correct-by-row.
- **Still no period engine for v3 or v2** (v2 has no period file anywhere
  either) — so the exact in-gap flip of the build-date rows stays unpinned.
