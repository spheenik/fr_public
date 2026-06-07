## ADDED Requirements

### Requirement: Packer detection by PE section signature

The toolkit SHALL identify the packer of a period farbrausch binary from its PE
section names, returning one of `aplib`, `kkrunchy`, `ruletool`, or `none`, without
unpacking.

#### Scenario: aPLib stub binary
- **WHEN** `detect_packer` is given an exe whose sections include `rygs and` and `packer.`
- **THEN** it returns `aplib`

#### Scenario: kkrunchy binary
- **WHEN** `detect_packer` is given an exe with a single `kkrunchy` section
- **THEN** it returns `kkrunchy`

#### Scenario: unknown ruletool packer
- **WHEN** `detect_packer` is given an exe whose sections include `ruletool` and `resultat`
- **THEN** it returns `ruletool`

#### Scenario: unpacked binary
- **WHEN** `detect_packer` is given an exe with conventional sections (`.text`, `.rdata`, `.data`, `.rsrc`) and no known packer signature
- **THEN** it returns `none`

### Requirement: Unpacking dispatch by route

The toolkit SHALL produce the flat in-memory image of a binary by dispatching on the
detected packer, and SHALL refuse — with an explicit, named error — to unpack a route
it does not implement.

#### Scenario: aPLib route reproduces the known image
- **WHEN** an `aplib`-packed binary with a previously-captured unpacked `.bin` is unpacked
- **THEN** the produced flat image is byte-identical to that captured `.bin`

#### Scenario: kkrunchy route
- **WHEN** a `kkrunchy`-packed binary is unpacked
- **THEN** the toolkit uses the SIGSEGV-dump depack route and returns the decompressed image

#### Scenario: unpacked binary passes through
- **WHEN** a binary detected as `none` is unpacked
- **THEN** the toolkit returns the file image unchanged (carve operates directly)

#### Scenario: unsupported ruletool route fails loudly
- **WHEN** unpacking is attempted on a `ruletool`-detected binary
- **THEN** the toolkit raises a clear not-implemented error naming the `ruletool` packer, and does not return a partial image

### Requirement: V2M carving from a flat image

The toolkit SHALL locate embedded v2m song spans in a flat image using a single
canonical parser, replacing the forked `findv2m` generations.

#### Scenario: carve reproduces a committed song
- **WHEN** the carver scans an unpacked image for which a carved `.v2m` is committed in the repo
- **THEN** the carved bytes for that span are byte-identical to the committed `.v2m`

#### Scenario: multiple embedded songs
- **WHEN** an image contains more than one valid v2m span
- **THEN** the carver returns all non-overlapping spans (earliest-start, then largest)

### Requirement: Era assay of a synth image

The toolkit SHALL report the era-delta constants and opcodes present in a synth image
(the `erascan`/`constscan` assay), as a single module.

#### Scenario: assay reports known rows
- **WHEN** the assay runs on an unpacked image
- **THEN** it reports, per known era-delta constant and opcode, whether it is present and at which addresses

### Requirement: Flat-image disassembly

The toolkit SHALL disassemble a flat image at a given virtual address (image base
`0x400000`), as a single canonical CLI, replacing the diverged `disasm` forks.

#### Scenario: disassemble at a VA
- **WHEN** the disassembler is given an image, a virtual address, and an instruction count
- **THEN** it prints that many decoded instructions starting at the file offset for that VA

### Requirement: Buffer comparison contract primitive

The toolkit SHALL provide the oracle-contract verification primitive — compare two
interleaved-stereo float32 render buffers and report sample-count match, signal
level, `max|d|`, rms, count over tolerance, and first divergence — fast enough for
whole-song renders, replacing the inline diffs and the pure-Python lab `compare.py`.

#### Scenario: bit-exact buffers
- **WHEN** two byte-identical f32 buffers are compared with eps 0
- **THEN** it reports `max|d| = 0` and a MATCH verdict (exit 0)

#### Scenario: divergent or mismatched buffers
- **WHEN** two buffers differ, or differ in length
- **THEN** it reports the first divergence point (or a length mismatch) and a DIVERGE verdict (exit non-zero)

#### Scenario: whole-song scale
- **WHEN** comparing a multi-minute render (tens of millions of floats)
- **THEN** the comparison completes in well under a second (numpy-accelerated, array fallback)

### Requirement: Unified extraction CLI

The toolkit SHALL expose a single command-line front end covering the offline stages
so a new binary is handled without copying a script.

#### Scenario: subcommands cover the pipeline
- **WHEN** a user invokes the `era` CLI
- **THEN** it offers `detect`, `unpack`, `carve`, `assay`, `disasm`, `tap`, and `compare` subcommands operating on a binary, flat image, or render buffers

#### Scenario: static data tap
- **WHEN** a user runs `era tap` with an image, a virtual address, and a type
- **THEN** it decodes the data at that VA in the static image (u32/i32/f32/f64/u16/u8/hex), distinct from runtime taps which require the `oracle.h` helpers in a render harness

### Requirement: Shared C oracle scaffold

The toolkit SHALL provide a shared C oracle scaffold (`oracle.h`) covering the process
concerns common to every oracle harness — mapping the image at `0x400000`, a fault
reporter, rdtsc seed pinning, and f32 stereo output — while leaving the per-binary
driving strategy (in-image player vs ported player) and synth ABI in bespoke glue.

#### Scenario: scaffold provides common helpers
- **WHEN** an oracle harness includes `oracle.h`
- **THEN** it obtains the image-map, fault-reporter, rdtsc-pin, and f32-writer helpers without redefining them

#### Scenario: per-binary glue stays bespoke
- **WHEN** a harness drives the binary
- **THEN** the entry/init/render VAs, rdtsc sites, and player-driving strategy are supplied by that harness, not fixed by the scaffold

### Requirement: Oracle tapping

The scaffold SHALL provide the shared mechanism for tapping a loaded oracle —
reading live typed data at chosen virtual addresses (including pointer-indirect
field reads) and streaming it to env-gated tap sinks — so a harness expresses only
the per-binary positions, not the read/sink boilerplate.

#### Scenario: typed live read at a position
- **WHEN** a harness reads a value at a virtual address in the mapped image (directly or via a workspace pointer)
- **THEN** the scaffold returns the typed value (u32/i32/f32/buffer pointer), yielding 0 for a null workspace pointer

#### Scenario: env-gated tap sink
- **WHEN** a harness opens a tap bound to an environment variable
- **THEN** writes to that tap stream to its file only if the variable is set, and are a no-op otherwise

#### Scenario: signal-chain tap-table lifecycle
- **WHEN** a harness declares a table of channel taps and runs the open/reset/accumulate/dump/close lifecycle
- **THEN** the scaffold opens each tap prefix-gated, accumulates the per-binary buffers, and dumps mono/stereo frames per chunk — leaving only the accumulate point (where the live buffer feeds each tap) in the harness

### Requirement: Migration preserves committed contracts

Migrating an existing extraction dir onto the toolkit SHALL preserve every committed
contract, verified by diff, and SHALL be reversible per dir.

#### Scenario: carve contract holds after migration
- **WHEN** a migrated dir re-runs unpack and carve
- **THEN** the flat image and carved `.v2m` are byte-identical to before migration

#### Scenario: oracle render contract holds after migration
- **WHEN** a migrated oracle harness is recompiled against `oracle.h` and re-rendered
- **THEN** its output `.f32` is bit-exact (`max|d| = 0`) against the committed reference render

#### Scenario: forked scripts removed
- **WHEN** a dir's migration is complete and green
- **THEN** that dir's forked copies of the toolkit scripts are deleted and it imports the toolkit instead
