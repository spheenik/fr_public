# fr-08 (.the .product) music extraction

Provenance trail for `../fr08.v2m`: the V2M music data extracted from the
actual shipping demo, **fr-08: .the .product, final version 1.01**
(farbrausch, 2000) — `fr08v101.exe` (scene.org release; `readme.txt` and
`file_id.diz` are the original distribution files).

## Pipeline

1. `dumpmem.sh` — runs the demo under `xvfb-run wine`, polls `/proc/<pid>/maps`
   for the committed image around `0x400000`, and dumps the process memory
   (the exe is packed; the live image is the depacked program + data).
2. `aplib.py` / `unpack.py` / `loose.py` / `brute_start.py` — aPLib depacker
   and helpers for carving the packed sections offline.
3. `findv2m.py` / `findv2m2.py` / `findv2m3.py` — structural V2M scanners:
   V2M has no magic, so they walk candidate offsets validating the header
   shape (timediv/maxtime/gdnum ranges, 16 channel stream sizes summing
   consistently — see `try_parse` in findv2m3.py).
4. `dump2.py`..`dump5.py` / `disasm.py` — iteration helpers used along the way.

Intermediate artifacts (wine memory dumps, depacked images, rendered wavs)
are reproducible via the scripts and are not committed.

## Results

| file | md5 | what |
|------|-----|------|
| `../fr08.v2m` | `de7f33ae4c27eda5f1e1c57c94898b36` | main music, extracted from the v1.01 image (format: 6 versions behind current) |
| `../converted/fr08.v2m` | `8a054757f5a6f794d9744c501b12094e` | conv_v2m output of the above (byte-verified) |

## Caveats

The whole-song A/B (asm vs C++ core, max-abs 0) validates the **final 2004 V2**
rendering of the **converted** data. It does not reproduce the party-2000
sound: the demo carries its own year-2000 synth, the patch data crosses the
largest conversion gap conv_v2m supports (6 versions), and the validation
build compiles RONAN (speech) out.
