# .kkrieger (beta) — v5 own-engine oracle

**Unpack SOLVED (2026-06-10).** The kkrunchy_k7 image now reconstructs to coherent
code; the oracle loads and RUNS kkrieger's genuine V2 engine. One residual wiring
issue (osc-amplitude silence) is open before a clean A/B vs the portable — see §3.

## 1. The unpack blocker — SOLVED

kkrieger-beta is packed with **kkrunchy_k7** (ryg, 2004; source is in this repo at
`kkrunchy_k7/`), a *newer* kkrunchy than candytron's. k7 adds an x86 **split-stream
disassembly filter**: the packer (`exepacker.cpp` → `DisFilter::Filter`) separates
opcode bytes from their operand/immediate/displacement bytes so each stream
compresses better; the stub reverses it with `DisUnFilter` (stage **depack2.asm**).

The generic `era unpack` route stopped at the stub's **import-resolution loop**
(`depacker.asm` ~L466: `mov esi,IMPORTS; mov ebx,LOADLIBRARY; call [ebx]`). With no
Windows loader the IAT slots hold filler (0x17bb7), so `call [ebx]` faulted —
**before** stage1 returns to depack2. The un-filter therefore never ran and the
dumped `.text` stayed in split form: opcodes stripped from operands. Proof: the
address-free fild-loop heart `d91f8d7f0449` was present 7× in candytron/fr024/fr029
and **0×** here, and the noise LCG sat as bare bytes `35 84 b3 0b 6b 63 19 36`
without its `imul`/`add` opcodes. (candytron is an *older* kkrunchy with no such
filter, which is why it always worked — the stub-stop `fetch-unmapped` at a low addr
is the *normal* kkrunchy finish, not the tell.)

**Fix:** stub the two imports so the loop completes → stage1 returns → depack2 runs
`DisUnFilter` → coherent `.text`. `kkr_unpack.py` (this dir) maps a scratch page with
`LoadLibraryA`(ret 4)/`GetProcAddress`(ret 8) stubs returning a fake nonzero value,
patches the IAT slots at the import-loop entry, runs to the post-un-filter OEP fault,
and dumps. Result verified: `fild-heart` and `imul-LCG` both present, fully
disassemblable.

```
unzip ~/downloads/kkrieger-beta.zip pno0001.exe -d /tmp/kk
python3 kkr_unpack.py /tmp/kk/pno0001.exe unpacked_fixed.bin   # base 0x7c0000
```

**Image base is 0x7c0000** (not 0x400000 like the other demos) — the toolkit's
`era disasm/assay` assume 0x400000, so add 0x3c0000 to their reported VAs, or use a
capstone disasm at base 0x7c0000.

## 2. Oracle — runs the genuine engine

Built like fr024/fr029 (`../toolkit/v5_oracle.c`) but at base 0x7c0000. Synth-entry
VAs (found by the same fingerprints, in the now-coherent image):

| fn | VA | note |
| --- | --- | --- |
| synthInit ret8 | **0x809e2e** | zeroes 0x1e2cc8 state @0x8c4f48; 32 voices @0x8c6e5c stride 0x228 |
| synthRender ret16 | **0x809f08** | PC=24; out buf = arg0; mixes internal 0x8c4644 → buf |
| synthProcessMIDI ret4 | **0x80a222** | |
| synthSetGlobals ret4 | **0x80a530** | 22 globals → floats @0xa7ee9c |
| rdtsc | **0x8082ae, 0x808966** | |

```
gcc -m32 -no-pie -O0 ../toolkit/v5_oracle.c -I../toolkit -o oracle \
  -DORACLE_IMG_BASE=0x7c0000u -DIMG_SIZE=0xd87000 \
  -DVA_INIT=0x809e2e -DVA_GLOB=0x80a530 -DVA_MIDI=0x80a222 -DVA_REND=0x809f08 \
  -DRDTSC0=0x8082ae -DRDTSC1=0x808966
./oracle unpacked_fixed.bin ../embedded/kkrieger.v2m orc.f32 30
```

Verified working end-to-end: `setSampleRate` produces the canonical oscbase
12740060; `synthProcessMIDI` allocates voices; the **amplitude envelope generates
correctly** (e.g. a voice sustaining at curvol 0.92, aenv state = sustain). The
genuine engine is executing.

## 3. OPEN: osc-amplitude silence (NOT an unpack issue)

The render comes out near-silent (peak ~8e-8) **despite correct envelopes**. The
osc *phase* advances at the right frequency (≈65 Hz from a correct integer phase
increment) and curvol is right (0.92), but the osc *output amplitude* is ~1e-8 — the
collapse is in the oscillator gain/output path, the same class as the brüllwürfel
"osc collapse" gotcha ([[brullwurfel-v5-oracle]]: a synth-relevant global the demo's
init wrapper sets that a synth-only harness must replay). The osc render (0x808377)
is coherent and dispatches by type; the missing piece is a gain/scale the game's
startup establishes outside synthInit/SetGlobals. This blocks the final A/B vs the
portable but is independent of the (now solved) unpack. song uses ronan (spsize=128);
the harness doesn't set lyrics yet, so ch15 is silent either way.

Disposition: kkrieger's engine is now reachable and running; finishing the A/B is a
bounded follow-up (find the osc-gain global), not a packer problem.
