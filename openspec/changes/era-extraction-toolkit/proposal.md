## Why

The period-binary extraction tooling has been copy-paste-forked across three
`-extraction` dirs (`fr08`, `flybye`, `candytron`): `unpack.py` is byte-identical
between fr08 and flybye, `findv2m` exists in three generations (203 lines for one
~76-line parser), `disasm.py` diverged into two incompatible forms, and the
`erascan`/`constscan` assay was copied forward at each binary. The era-binary hunt
(`v2/v2m/era-hunt-manifest.md`) just queued **11 more** period binaries; under the
current pattern each one means forking a whole dir again. A single shared toolkit
removes that per-binary tax now, while the duplication is still small enough to
factor by diffing against existing committed contracts.

## What Changes

- **New `v2/v2m/toolkit/`** holding one canonical copy of each extraction stage:
  - `packers.py` — `detect_packer(exe)` (PE section-name signature) + `unpack(exe)`
    dispatching the three routes: aPLib `rygs and`/`packer.` stub (unicorn, 10 of
    the queued binaries), `kkrunchy` single-section (the `c2_unpack.c` route), and
    **detect-only** for the unknown `ruletool`/`resultat` packer (fr011) — flagged,
    not unpacked.
  - `carve.py` — the canonical `findv2m3` v2m-span parser (the two older forks deleted).
  - `eras.py` — the constant/opcode era-assay (one copy, replacing erascan/constscan).
  - `disasm.py` — the flat-image VA disassembler (the flybye CLI form; the fr08 form deleted).
  - `era` — one CLI front end: `era {detect|unpack|carve|assay|disasm} <file>`.
  - `oracle.h` — the shared C oracle scaffold (mmap@`0x400000` + SIGSEGV-dump + f32
    output) with per-binary `{entry, init, render}` VAs as config; the bespoke synth
    glue stays per-binary.
- **Migrate the three existing extraction dirs** to consume the toolkit and **delete
  their forked copies**, flybye first (cleanest), verified end-to-end before the others.
- Each migration step is gated on an existing committed contract — re-run
  unpack→byte-compare the flat image, re-run carve→byte-identical `.v2m`,
  recompile oracle→re-render→`max|d| = 0` vs the committed reference render. A
  refactor that breaks anything fails loudly as a non-zero diff.
- No change to the portable player, the v2m format, or any rendered output. This is
  dev-time infrastructure only.

## Capabilities

### New Capabilities
- `era-extraction-toolkit`: the shared offline tooling that turns a packed/unpacked
  period farbrausch binary into its carved v2m song(s), its era-delta assay, and a
  reference oracle render — packer detection/dispatch, v2m carving, era assay,
  disassembly, and the C oracle scaffold, plus the contract-diff verification that
  lets the proven fr08/flybye/candytron dirs migrate onto it safely.

### Modified Capabilities
<!-- None: existing specs cover player/engine behavior; no requirement changes. -->

## Impact

- **New:** `v2/v2m/toolkit/` (Python modules + `era` CLI + `oracle.h`).
- **Modified:** `v2/v2m/fr08-extraction/`, `flybye-extraction/`, `candytron-extraction/`
  — forked scripts deleted, replaced by toolkit imports; carved `.v2m`, `NOTES.md`,
  and binary-specific oracle glue retained.
- **Dependencies:** `unicorn` and `capstone` (already required by the existing scripts;
  not installed in the current shell — the unpack-path re-verify needs a `pip install`,
  or falls back to the already-unpacked `.bin`s in `/tmp`). Carve/assay/oracle paths
  need neither.
- **Contracts relied on (unchanged):** committed carved `.v2m` files and the reference
  `.f32` oracle renders; `portable/v2load` version fingerprinting; `portable/v2dump`.
- **Out of scope:** running the actual 11-binary sweep, and reverse-engineering the
  fr011 `ruletool` packer (detect-only here).
