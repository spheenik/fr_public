# V2 core validation — build notes (Phase 1)

A/B harness that renders the same `.v2m` through both V2 synth cores and diffs
the output. See `openspec/changes/validate-v2-synth-core/`.

## Toolchain prerequisites (32-bit / ILP32, x86)

Both cores are built 32-bit so `sU32` (defined as `unsigned long` in `types.h`)
is 4 bytes on both sides and struct/patch-data layouts match. See design.md D2.

Verified present on the dev machine (2026-06-03):

| Tool | Required | Found |
|------|----------|-------|
| Assembler | `nasm` (or `yasm`), `-f elf32` | NASM 3.01 |
| C/C++ compiler | `gcc`/`g++` with `-m32` | GCC 16.1.1 |
| 32-bit libstdc++ | needed to link the C++ core harness | present |

Quick check:

```sh
nasm --version
gcc -m32 -xc /dev/null -c -o /dev/null   # 32-bit cc works
echo 'int main(){return 0;}' | g++ -m32 -xc++ - -o /tmp/t && /tmp/t  # 32-bit libstdc++ links
```

On a fresh distro you may need: `nasm`, `gcc-multilib`/`g++-multilib` (Debian/Ubuntu)
or `lib32-gcc-libs`/`lib32-glibc` (Arch).

## Build (filled in as tasks land)

### Oracle: `synth_asm.o` (from `synth.asm`)

`synth.asm` assembles **as-is** on NASM 3.01 — zero dialect fixes needed (only
harmless `label-orphan` style warnings, suppressed with `-w-label-orphan`).

RONAN is disabled by commenting the single `%define RONAN` line. We do NOT edit
the original `synth.asm`; instead we generate a copy in this dir:

```sh
sed 's/^%define\([[:space:]]*\)RONAN[[:space:]]*$/; RONAN disabled for validation: &/' \
    ../synth.asm > synth_noronan.asm
nasm -f elf32 -w-label-orphan synth_noronan.asm -o synth_asm.o
```

With RONAN off the object is **fully self-contained** (no undefined symbols — not
even libc) and exports the 12 `_synth*@N` entry points (MSVC stdcall-decorated).

> Note the symbol decoration: the asm exports `_synthInit@12`, `_synthRender@20`,
> etc. On Linux ELF the harness must bind to those exact names (e.g. via a gcc
> `__asm__("_synthInit@12")` label alias) since g++ emits undecorated `synthInit`.

### Core under test: `synth_cpp.o` (from `synth_core.cpp`)

```sh
g++ -m32 -std=c++03 -O2 -include compat.h -I.. -c ../synth_core.cpp -o synth_cpp.o
```

- `-std=c++03`: the code is from 2012. Modern libstdc++ adds `std::lerp` (C++17),
  which collides with the core's own `lerp()`. C++03 avoids importing that name —
  no source change, no behavior change.
- `-include compat.h`: maps `__int64`, `__stdcall`, `vsprintf_s` (see compat.h).
  RONAN is off by default (`#ifdef RONAN` guards, never defined here);
  `DEBUGSCOPES`/`COVERAGE` are 0 in-source. The dead `dprintf`/`OutputDebugStringA`
  path is dropped by the optimizer; a stub in the harness covers any -O0 build.

Exports the **undecorated** `synthInit`, `synthRender`, ... (Linux ELF). The
harness binds these directly for the cpp build, and binds the `_synth*@N`
aliases for the asm build.

- `harness_asm` / `harness_cpp` — one harness source, two link targets
