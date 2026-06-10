# .kkrieger (beta) — v5 own-engine oracle BLOCKED (kkrieger image won't reconstruct)

Goal was the same own-engine verification done for fr-024 / fr-029. It is
**blocked**: for *this* image the unpack recovers the *data* but not coherent
*code*, so there is no runnable synth to oracle against. NOT a portable-player
question — a packer/toolchain limitation, and **specific to kkrieger-beta**:
candytron is *also* kkrunchy-packed and reconstructs perfectly (its c2 oracle is
bit-exact, and the `d91f8d7f0449` fild-loop heart below is present 7×). So the
kkrunchy route works in general; kkrieger-beta's image is the exception.

## What works

- `~/downloads/kkrieger-beta.zip` → `pno0001.exe` (kkrunchy-packed, 95 KB).
- `era unpack` runs the stub under Unicorn and dumps a 14 MB image
  (`unpacked.bin`). The **data** decompressed correctly:
  - `era carve` finds 2 valid v2m songs (span 0 = `v2m/embedded/kkrieger.v2m`,
    27031 bytes, 13 ch, **spsize=128 → uses ronan speech**).
  - `era assay` reads real float constants: modern noise LCG present, no baked
    oscfreq, no v6 oscseeds. (The "v6 fcdcoffset 2^-18" hits at 0x46d5d5/0x46fb03/
    0x4785bd are in the **game-code** region 0x46–0x47, not the synth at 0x40–0x43
    — coincidental 0x39800000 float matches in physics/render code, not the synth.)
  - Data-signature alone ⇒ kkrieger is *consistent with* modern-core v5 (like
    fr024/fr029/candytron) — but a data signature is **not** an engine render, so
    this stays UNVERIFIED, not proven.

## Why the code is unusable (decisive proof)

Two independent proofs the .text is not coherent code:

1. **The address-free fild-loop heart is absent.** The v5 synth is byte-identical
   across releases, so `d9 1f 8d 7f 04 49` (`fstp [edi]; lea edi,[edi+4]; dec ecx`,
   the address-free core of `synthSetGlobals` and the param fild loops) appears
   **7× in candytron, 7× in fr024, 7× in fr029 — and 0× in kkrieger.**

2. **The noise LCG is bare data, not an instruction.** In candytron (@0x414419)
   the modern LCG is `imul eax,eax,0xbb38435 ; add eax,0x3619636b`
   (`69 c0 35 84 b3 0b  05 6b 63 19 36`). In kkrieger the same constants appear as
   bare adjacent bytes `35 84 b3 0b 6b 63 19 36` with the `69 c0`/`05` opcodes
   **stripped**, sitting in a table-like layout amid null padding and
   absolute-VA-looking dwords (0x842878, 0x842b00…). Disassembly of the whole
   synth region is incoherent (`jmp 0xac3624ba`, missing displacement operands).

This looks like kkrunchy's **disassembly-based code filter** (ryg's packer
segregates opcode vs. operand/relocation streams for better compression; the
decompressor un-filters them as its final stage). candytron's earlier (2003)
kkrunchy build either omits the filter or the toolkit emulation reverses it;
kkrieger-beta (2004-04) is a later build whose filtered `.text` the current
`era unpack` route does **not** reconstruct for this image. The stub-stop
(`fetch-unmapped eip=0x17bb7`) is the *normal* kkrunchy finish signal — candytron
stops the same way at 0xffba — so the stop is not the tell; the recovered bytes are.

## To unblock (separate, substantial task — not attempted here)

Diagnose why kkrieger-beta's stub leaves `.text` un-reconstructed where
candytron's doesn't (compare the two kkrunchy stubs; likely the newer build's
x86 un-filter pass is the gap), then either run it to true completion under
Unicorn or implement the un-filter over the dumped stream. This is RE on the
packer, orthogonal to the synth port. Every *other* carvable demo in the corpus
is aPLib (clean round-trip) or kkrunchy-that-works (candytron); kkrieger-beta is
the lone holdout. Until then, kkrieger's song renders deterministically in the
portable player and is era-*plausible* (modern-core data signature), but cannot
be called *faithful*.
