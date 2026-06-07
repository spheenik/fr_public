# Era-binary hunt manifest (2026-06-07)

Provenance record for the period-binary sweep that targets the two blind
windows left after the flybye/fr-022/candytron assays
(`flybye-extraction/NOTES.md`, `v2/portable/v2eras.h`):

- **Gap 1 (2001-12 → 2002-08)**: pins the 16→32 voice-pool growth
  (`DELTA_POLY_16`), the `DELTA_PGMCHANGE_V0` flip, and tests
  `ENV_CURVES`/`FRAME256` at the build level.
- **Gap 2 (2002-08 → 2003-08)**: pins the osc-core trio + render-driver
  modernization (the 89%-error build-date flip).
- **v5→v6 bracket (2003-08 → 2004)**: when did the flipsAt-6 rows
  (dcoffset, fastsin/fastatan, pool 64, DCFs, …) actually land?

Per repo policy the demo zips/binaries are **NOT committed** (copyrighted);
this manifest records exactly what was fetched, from where, and its sha256,
so the sweep is reproducible. Local copies live in the user's
**`~/downloads/`** (same convention as the fr08/flybye/fr-022/candytron
zips already there).

Source: `ftp://ftp.scene.org` (fetched 2026-06-07 via curl). Release dates
from pouet.net (group 322); FTP mtimes given as corroboration.

## Downloaded 2026-06-07

| file (~/downloads/) | bytes | sha256 | prod | release | FTP path (pub/...) | window |
| --- | --- | --- | --- | --- | --- | --- |
| `fr011_party.zip` | 42840 | `3397ed45…1d3d066b` | fr-011 ms2001 invitation (64k) | 2001-03 | demos/groups/farbrausch/ | pre-flybye corroboration |
| `fr014.zip` | 53487 | `ea7f2d3a…e33fabf99e5` | fr-014 garbage collection (64k) | 2001-12 | demos/groups/farbrausch/ | flybye-month corroboration |
| `fr019_party.zip` | 66018 | `1fa7e27a…e7e97f818` | fr-019 poemtoahorse PARTY (64k) | 2002-03 | parties/2002/mekkasymposium02/in64/ | **gap 1** |
| `fr-022.zip` | 67134 | `54ca9808…26932d6e` | fr-022 ein.schlag **PARTY** (64k) | 2002-03 | parties/2002/mekkasymposium02/in64/ | **gap 1** — vs the assayed final (2002-08); may embed the v3 export |
| `fr-020.zip` | 6483011 | `b7d8da35…a03ace0045` | fr-020 in control (demo) | 2002-04 | demos/groups/farbrausch/ | **gap 1** |
| `fr-027_final.zip` | 47232 | `43f24097…05711f563` | fr-027 out of the blue (64k) | 2002-07 | demos/groups/farbrausch/ | **gap 1** — latest pre-fr-022-final point |
| `fr-028.zip` | 65353 | `2dfa53e5…ddb977f09` | fr-028 brullwurfel (64k MUSICDISK) | 2002-09 | demos/groups/farbrausch/ | **gap 2 start** — multiple embedded songs |
| `fr029.zip` | 48973 | `4d6584d6…059a266e` | fr-029 dopplerdefekt (64k) | 2002-12 | demos/groups/farbrausch/ | **gap 2** (repo `RG2/dopplerdefekt/drumtro3.v2m` v5) |
| `fr-minus-03-2.zip` | 56065 | `bbbbe30f…c2c414ec` | fr-minus-03.2 rausch-o-mat (64k) | 2002-12 | demos/groups/farbrausch/ | **gap 2** |
| `fr024.zip` | 56582 | `eeffa806…b35b0c3c4e` | fr-024 welcome to... (64k invite) | 2003-01 | demos/groups/farbrausch/ | **gap 2** (repo `RG2/welcome_to/invtro.v2m` v5) |
| `fr-030_candytron_party.zip` | 66291 | `1cd0e5e0…dcc3a37e0e` | fr-030 candytron **PARTY** (64k) | 2003-04 | demos/groups/farbrausch/ | **gap 2 bisect** — vs the assayed final-101 (2003-08) |
| `fr-036-zeitmaschine.zip` | 9951108 | `a321cc0d…ea3288244` | fr-036 zeitmaschine (demo) | 2003-12 | demos/groups/farbrausch/ | **v5→v6 bracket** — repo holds its song as a v6 oracle file |
| `kkrieger-beta.zip` | 102042 | `0de0b9ab…0eda0a654c9f` | kkrieger beta (96k game, .theprodukkt) | 2004-04 | parties/2004/breakpoint04/96kgame/ | **v5→v6 bracket** + the only period engine that played `kkrieger6.v2m` (v5) |

Full sha256 values:

```
3397ed4509f4013092c3d14dbbc5b89360c75cdcca8a3ec68edc59051d3d066b  fr011_party.zip
ea7f2d3a506e447bd4b5b2e777a5c80691ca80939dbe4ffb2b0e33d1fabf99e5  fr014.zip
1fa7e27a89894df24bfaa404c4a72f8332d6e2df4470b44aee80cbce7e97f818  fr019_party.zip
b7d8da3588ef51cbaf2cdbcf64471050d3a9d79c36372e4ddbc34aa03ace0045  fr-020.zip
54ca9808bc26c6613a811fdfac65b1a02d391e429084542ed325217026932d6e  fr-022.zip
eeffa806d154d20c43e5a9524b2d7ecc6cdd368e31601e5e9db950b35b0c3c4e  fr024.zip
43f24097262f4bc91b080e036e84d9451b19120df0204e68dc3694305711f563  fr-027_final.zip
2dfa53e5c4704cff4d38fcaf43be895605b126e77e9ec22376bc869ddb977f09  fr-028.zip
4d6584d61ebb12079bd3b017ef85c9ce9506b4d2280680e3cad543de059a266e  fr029.zip
1cd0e5e0321bffa5ff2601920df1bd119b958e20c90dae1f2ddd28dcc3a37e0e  fr-030_candytron_party.zip
a321cc0dc88fa6c88007281d27f77b5f862d1880f5acb3173e28060ea3288244  fr-036-zeitmaschine.zip
bbbbe30ff04b09e5e559b5dc78fef11d5320c30c0a21b2af95ea111ca2c414ec  fr-minus-03-2.zip
0de0b9abafd78cf7f57fb264c7136ace52b2653f5c675574d0f10eda0a654c9f  kkrieger-beta.zip
```

## Already in ~/downloads/ from previous extractions

| file | prod | used by |
| --- | --- | --- |
| `fr08_final.zip` | fr-08 .the .product (2000-12) | `fr08-extraction/` (v0 anchor, C1 oracle) |
| `fr013_unoff.zip` | fr-013 flybye (2001-12) | `flybye-extraction/` (v1 anchor + oracle) |
| `fr-022_final.zip` | fr-022 ein.schlag final (2002-08) | `flybye-extraction/` (early-v5 assay) |
| `fr-030_candytron_final.zip` | fr-030 candytron final-101 (2003-08) | `candytron-extraction/` (v5 anchor, C2 oracle) |

## Not fetched (known, lower priority)

- `fr-025.zip` / `fr-025-final*.zip` (the.popular.demo, 2003-04, ~8 MB) —
  same-month redundancy with candytron-party; fetch if that build resists
  unpacking. demos/groups/farbrausch/.
- `fr-minus-06_party.zip` (ghettorocker, 2003-04, 6 MB) — ditto.
- `fr-031` faded memories / `fr-032` e.t. (2003-08) — same month as
  candytron final; confirmation only.
- `fr-034` time index (2003-11, w/ Haujobb) — superseded by zeitmaschine
  (2003-12) for the v6 bracket; under parties/2003/simulaatio2 presumably.
- fortnight (2002-02, w/ mfx) — gap 1 has four points already.
- `libv2_1.0.zip` / `farbrausch_v2_plugins_1.0.zip` (2004-12, the public V2
  release) — the v6 endpoint in shipped form; demos/groups/farbrausch/.

## Sweep plan (per binary)

1. Unpack: ryg aPLib stub → `flybye-extraction/unpack.py` (unicorn static
   route); kkrunchy → `candytron-extraction/c2_unpack.c` route.
2. Carve embedded v2m(s): `fr08-extraction/findv2m3.py`; format-detect with
   `portable/v2dump`.
3. Era assay: `flybye-extraction/erascan.py` (constant/opcode rows) + the
   voice-pool loop bound (`cmp dl,0x10/0x20/0x40` after the per-voice tick
   call — see flybye-extraction NOTES "pool size" table for the four known
   shapes).
4. Where a row flips inside a gap: disassemble the signature-less rows
   (PGMCHANGE/TICK/SUBFRAME/RVB_E patterns, NOTES "disassembly" section).
5. New-format embedded songs (v2! v3! v4!) → c1-style render harness → new
   oracle; update `v2eras.h` thresholds + evidence.
