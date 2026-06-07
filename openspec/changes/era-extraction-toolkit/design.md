## Context

Three period-binary `-extraction` dirs (`fr08`, `flybye`, `candytron`) each grew a
near-identical set of offline tools. Concretely today:

- `unpack.py` — **byte-identical** between fr08 and flybye (aPLib stub via unicorn).
- `findv2m*.py` — **three generations** in fr08 alone (63 + 64 + 76 lines); the v2m
  span parser is the same idea re-derived each time.
- `disasm.py` — **diverged** into a clean CLI form (flybye) and a hard-coded
  one-binary form (fr08).
- `erascan.py` / `constscan.py` — the const/opcode era-assay, copied forward.
- Oracle harnesses — `c2_oracle.c` (198), `c2_render.c` (165), `c2_oracle_solo.c`
  (258), `c1_flybye_harness.c` (183), `c1_flybye_solo.c` (175) ≈ **1086 lines of C**
  sharing an mmap + SIGSEGV-dump + rdtsc-pin + f32-writer scaffold.

The era-hunt manifest just queued 11 more binaries, 10 of which ride the single
proven aPLib route. Under the current pattern each new binary forks a whole dir.
The proven results (fr08 663 s bit-exact, flybye whole-song, candytron C2 oracle)
are captured as **committed artifacts** — carved `.v2m` files and reference `.f32`
renders — which is what makes refactoring the *tooling* safe: the artifacts are the
contract, and the tools must keep reproducing them byte-for-byte.

A critical finding from reading the two oracle harnesses: they drive the binary
**differently**. `c1_flybye` calls the binary's *own* in-image player
(`OpenV2M`/`PlayV2M`/`RenderProxy` at fixed VAs). `c2` (candytron) instead ports the
genthree `_viruz2.cpp` source player into the harness and calls only the binary's
*synth* entry points, because candytron's werkkzeug MIDI driver was hard to
replicate. So the player-driving strategy is genuinely per-binary; only the process
scaffold is common.

## Goals / Non-Goals

**Goals:**
- One canonical copy of each offline stage in `v2/v2m/toolkit/`: packer
  detect/dispatch, v2m carve, era assay, disasm, plus a shared C oracle *scaffold*.
- A single `era` CLI front end so a new binary is `era {detect|unpack|carve|assay} <f>`.
- Migrate fr08/flybye/candytron onto the toolkit and delete their forked scripts,
  each step proven by diffing against its committed artifact.
- Cut the per-binary cost for the 11 queued binaries from "fork a dir" to "add a
  config + bespoke driving glue."

**Non-Goals:**
- No change to the portable player, the v2m format, or any rendered audio.
- Not running the 11-binary sweep (this only builds the tooling it will use).
- Not reverse-engineering the fr011 `ruletool`/`resultat` packer — **detect-only**.
- Not unifying the oracle *driving* strategy (in-image player vs ported player); only
  the scaffold is shared.

## Decisions

### D1 — Packer dispatch by PE section-name signature
`detect_packer(exe)` reads PE section names: `rygs and`+`packer.` → `aplib`,
single `kkrunchy` → `kkrunchy`, `ruletool`+`resultat` → `ruletool`, normal
`.text/.rdata/...` → `none`. `unpack(exe)` dispatches: `aplib` → the proven
unicorn stub-emulation (today's `unpack.py`, unchanged logic), `kkrunchy` → the
`c2_unpack.c` mmap+SIGSEGV route, `none` → identity (carve the file directly,
e.g. zeitmaschine), `ruletool` → **raise NotImplemented** with a clear message.
*Alternative considered:* entropy/heuristic detection — rejected; section names
are an exact, zero-ambiguity signature here and already verified across all 13.

### D2 — One Python package, one CLI, thin modules
`toolkit/{packers,carve,eras,disasm}.py` importable as functions, fronted by an
`era` CLI. `carve.py` is the `findv2m3` parser verbatim (newest, strictest); the
two older forks are deleted, not merged. `disasm.py` is the flybye CLI form; the
fr08 hard-coded form is deleted. *Alternative:* keep scripts standalone and just
de-dup by symlink — rejected; symlinks hide the canonical source and don't give a
single import surface for the sweep.

### D3 — Share only the oracle *scaffold*, keep driving glue per-binary
`oracle.h` provides the common C scaffold as small inline helpers/macros:
`oracle_map_image(base,size)`, the SIGSEGV/SIGBUS/SIGILL reporter, an rdtsc
seed-pinning helper (patch `0f31`→`31c0` at given VAs), and an f32 stereo writer.
Per-binary config = `{IMG_BASE, IMG_SIZE, entry/init/render VAs, rdtsc sites}`.
The harness keeps its own `main()` and its own driving strategy (in-image player
*or* ported player). *Alternative:* a fully generic oracle driver parameterized by
ABI — rejected as premature; the flybye/candytron split shows the driving layer
resists generalization and bespoke glue was discovered per-binary via disasm.

The scaffold runs the synth **natively** (`-m32` on the host CPU), never under
Unicorn — and this is load-bearing, not incidental. The V2 audio path is x87
transcendental-heavy (`fsin` osc, `fpatan` overdrive, `f2xm1` calcfreq), and the
native host x87 reproduces the period Pentium's 80-bit results bit-for-bit, which is
*why* the flybye/fr08/candytron oracles hit `max|d| = 0`. A direct test on this host
(identical 80-bit inputs, default CW `0x037f`) showed Unicorn/QEMU diverges from
hardware on exactly these ops — agreeing on the exponent and top ~52 mantissa bits
but rounding the bottom ~11 (libm-at-double signature): `fsin` ~679 ULP, `f2xm1`
~1936 ULP, `fpatan` ~837 ULP. Emulating the render would inject that error millions
of times per song. Unicorn is therefore confined to **unpacking** (the aPLib stub is
pure integer — reproduced the flybye image byte-identically) and, if ever needed, a
*non*-bit-exact instruction/memory **trace** mode; it is not a path to a bit-exact
oracle. Do not re-litigate this without re-running the test.

### D4 — Migration gated on committed contracts, flybye first
Each migrated stage is verified against an existing artifact, not trusted:
re-run unpack → byte-compare flat image; re-run carve → byte-identical `.v2m`;
recompile oracle on `oracle.h` → re-render → `max|d| = 0` vs the committed
reference `.f32`. Flybye migrates end-to-end first (cleanest, in-image player);
only when it is green do candytron and fr08 follow. *Alternative:* big-bang
migrate all three — rejected; a scaffold bug would then fire against three
contracts at once, obscuring which assumption broke.

### D5 — Location: `v2/v2m/toolkit/` as a sibling
Keeps all period-binary work under `v2/v2m/`. Each `-extraction` dir retains only
its carved `.v2m`, `NOTES.md`, and binary-specific oracle glue, and imports the
toolkit (relative path / `sys.path` insert, matching existing script conventions).

## Risks / Trade-offs

- **Touching proven-working code** → every change is diffed against a committed
  artifact (D4); a regression surfaces as a non-zero diff, never a silent change.
  Migrate one dir at a time so a fault maps to one contract.
- **unicorn/capstone absent in the current shell** → the unpack/disasm re-verify
  needs a `pip install unicorn capstone`; carve/assay/oracle paths don't. The
  already-unpacked `.bin`s in `/tmp` are a fallback for byte-comparing carve.
- **Reference `.f32` renders live in `/tmp`, not the repo** (large) → if a `/tmp`
  reference is gone, regenerate it from the *pre-migration* harness first to
  re-establish the contract before refactoring that harness.
- **Over-abstracting the oracle** → mitigated by D3: share only the scaffold,
  leave driving glue bespoke. If a future binary needs a third driving strategy,
  it costs glue, not a scaffold rewrite.
- **`ruletool` left detect-only** → fr011 stays known-but-blocked; deliberate, and
  it is lowest-priority corroboration, not on any gap-pinning critical path.

## Migration Plan

1. Build `toolkit/` modules + `era` CLI + `oracle.h` (no dir touched yet).
2. **flybye**: repoint to toolkit, delete forks, verify unpack/carve/assay byte-
   exact and `c1_flybye` re-render `max|d|=0`. Commit.
3. **candytron**, then **fr08**: same, each gated on its own contracts.
4. Rollback strategy: each dir migrates in its own commit; revert that commit to
   restore the forked scripts. Toolkit addition (step 1) is inert until imported.

## Open Questions

- Should `eras.py` emit a machine-readable assay (JSON) to feed `v2eras.h`
  evidence rows directly, or stay human-readable as today? (Lean: add `--json`,
  keep text default — defer unless the sweep wants it.)
- Does fr08's harness live in `v2/validate/` (`c1_fr08_harness.c`) rather than the
  extraction dir? If so its migration is a fourth touch-point to confirm in tasks.
