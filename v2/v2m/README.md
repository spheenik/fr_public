# v2m — period-binary extraction & oracle work

This tree is organized **by stage**, not by demo. A single demo's work therefore
spans several directories (its extraction dir here, its oracle harness in
`../validate/`). That is intentional — read the stages, then follow a demo
across them.

NOTE (USER RULING 2026-06-10): there are **no v6-converted song files**. The
portable plays the original embedded V2M at its native era; ground truth is
always the song carved from the demo binary, never a converted copy. The former
`converted/` corpus has been removed.

## Stages

```
 ../downloads (not committed)         the period .exe/.zip releases (copyright policy)
        │  unpack + carve + per-demo reverse engineering
        ▼
 <demo>-extraction/                   ONE dir per demo, the extraction stage:
   fr08-extraction/                     carved .v2m, NOTES/DELTA/HANDOVER docs,
   flybye-extraction/                   the demo's oracle-driving glue, and any
   candytron-extraction/                demo-specific RE scratch scripts
        │  shared offline tooling (no per-demo copies)
        ▼
 toolkit/                             the `era` CLI + oracle.h scaffold used by ALL
                                        extraction dirs (detect/unpack/carve/assay/
                                        disasm/tap/compare; the native render
                                        scaffold + live/signal-chain taps). See its
                                        README. Replaced the forked per-dir scripts.
        │  carve the EMBEDDED song from each demo binary -> the ground-truth corpus
        ▼
 embedded/                           the genuine songs carved from the demo binaries
                                        (fr08 v0, flybye v1, fr014 v3, fr019 v4,
                                        candytron v5), each at its NATIVE format
                                        version + PROVENANCE.md. THE corpus the
                                        portable is validated against; replaced the
                                        old v6-converted set (no converted files).
        │  render the original at its native era through the period synth + the
        │  portable player, diff (no v6 conversion)
        ▼
 ../validate/                        the c1_* oracle lab (cross-cutting): the period
                                        synth harnesses (c1_fr08_harness, c1_solo_probe)
                                        live here WITH their sibling probes
                                        (c1_osc/flt/lfo/… ) and comparators
                                        (compare.py, freqdiff.py, comp_*). A demo's
                                        oracle harness belongs to this lab, not its
                                        extraction dir.
```

## Following a demo across the stages (e.g. fr-08)

| stage | fr-08 artifact |
| --- | --- |
| extraction | `fr08-extraction/` (binary, RE scripts, docs) + raw carve `fr08.v2m` |
| tooling | `toolkit/era` (unpack/carve/assay) + `toolkit/oracle.h` |
| oracle lab | `../validate/c1_fr08_harness.c`, `../validate/c1_solo_probe.c` |

`flybye-extraction/` is the most self-contained (its carved `flybye_embedded.v2m` /
`fr022_embedded.v2m` and oracle glue sit together); `candytron-extraction/` drives a
repo source song (`../../genthree/data/josie.v2m`) rather than a committed carve.

## Provenance

Period binaries are **not committed** (copyright); `era-hunt-manifest.md` records
exactly what was fetched (sha256 + FTP path) so the sweep is reproducible. Local
copies live in `~/downloads/`.
