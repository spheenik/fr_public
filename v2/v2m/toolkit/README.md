# era-extraction toolkit

Shared offline tooling for the period-binary hunt: turn a packed/unpacked period
farbrausch binary into its carved v2m song(s), its era-delta assay, and a reference
oracle render. One canonical copy of each stage, replacing the forked scripts that
used to live in `fr08-extraction/`, `flybye-extraction/`, and `candytron-extraction/`.

## Modules

| file | purpose |
| --- | --- |
| `packers.py` | `detect_packer(exe)` (PE section signature) + `unpack(exe)` → flat image |
| `carve.py`   | `find_v2ms(image)` → embedded v2m spans (the canonical `findv2m3` parser) |
| `eras.py`    | `assay(image)` → era-delta constants/opcodes present in a synth image |
| `disasm.py`  | `disasm(image, va, n)` → decoded instructions (image base `0x400000`) |
| `era`        | CLI front end: `era {detect|unpack|carve|assay|disasm} <file>` |
| `oracle.h`   | shared C oracle scaffold (mmap@`0x400000` + fault reporter + rdtsc-pin + f32) |

## CLI

```
./era detect  <exe>                 # → aplib | kkrunchy | ruletool | none
./era unpack  <exe> <out.bin>       # depack to a flat image
./era carve   <image.bin>           # list embedded v2m spans
./era carve   <image.bin> --extract i --out song.v2m
./era assay   <image.bin>           # era-delta const/opcode report
./era disasm  <image.bin> <va> [n]  # disassemble n insns at a VA
```

## Packer routes (`unpack`)

- **aplib** (`rygs and`/`packer.` stub) and **kkrunchy** (single section): both are
  unpacked by running the depacker stub under Unicorn — map the image, emulate from
  the PE entry, let the stub decompress in place, stop when it jumps to the
  unresolved OEP (`UC_ERR_INSN_INVALID`), dump `0x400000`. The aPLib stub is pure
  integer; the flybye image round-trips byte-identically.
- **none** (conventional `.text/.rdata/...`): not packed — `unpack` returns the file
  image unchanged and `carve` operates on it directly (e.g. zeitmaschine).
- **ruletool** (`ruletool`/`resultat`, fr011): detected but **not** unpacked — raises
  a clear error. Reverse-engineering this earlier packer is out of scope.

## Oracle scaffold (`oracle.h`)

`oracle.h` is **native** (`-m32` on the host CPU), never Unicorn. The V2 audio path
is x87-transcendental-heavy (`fsin`/`fpatan`/`f2xm1`); the host hardware x87
reproduces the period results bit-for-bit (`max|d| = 0`), whereas QEMU/Unicorn rounds
the bottom ~11 mantissa bits (libm-at-double) — tested: `fsin` ~679 ULP, `f2xm1`
~1936, `fpatan` ~837. Unicorn is for unpacking only. See the change `design.md` (D3).

A harness `#include "oracle.h"`, supplies its own `{IMG_SIZE, entry/init/render VAs,
rdtsc sites}` and driving strategy (in-image player or ported player), and gets the
common scaffold helpers. Build: `gcc -m32 -no-pie -O0 harness.c -o harness`.

## Dependencies

`python-unicorn` (2.1.4) and `python-capstone` (5.0.7) — on Arch: `pacman -S
python-unicorn python-capstone`. Only `unpack`/`disasm` need them; `carve`/`assay`
are pure Python.
