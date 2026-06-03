#!/bin/sh
# Build the V2 core A/B validation harness (Phase 1).
# Produces two binaries that differ ONLY in which synth core they link:
#   harness_asm  -> synth.asm  (the oracle)
#   harness_cpp  -> synth_core.cpp (the port under test)
# See openspec/changes/validate-v2-synth-core/ and BUILD.md.
set -e
cd "$(dirname "$0")"

CXX="g++"
# Bit-faithful precision: mirror the asm's per-render x87 setup (synth.asm:4868,
# `and ax,0F0FFh` -> PC=24 single, round-nearest) for the WHOLE validation build.
#   -mpc32   : startup x87 control word = PC=24 (24-bit single). Pulls crtprec32.o
#              at LINK time, so it must also be in LDFLAGS below.
#   -mno-sse : keep every float op AND conversion on the x87 (no detour through an
#              8-bit-exponent xmm register), so intermediates keep the asm's
#              24-bit mantissa / 15-bit exponent.
#   -DV2_X87_FAITHFUL : enable the inline-asm f2xm1 transcendentals + fistp-round
#              freq path + float tri/saw box filter in synth_core.cpp.
# The asm oracle (harness_asm) is unaffected: the asm core sets/restores its own
# control word per render, and the shared player/harness objects are byte-identical
# in both binaries, so the A/B comparison stays fair.
CXXFLAGS="-m32 -std=c++03 -O2 -w -include compat.h -I.. -mpc32 -mno-sse -DV2_X87_FAITHFUL"

echo "[1/5] assemble oracle (synth.asm, RONAN off) + validation appendix"
sed 's/^%define\([[:space:]]*\)RONAN[[:space:]]*$/; RONAN disabled for validation: &/' \
    ../synth.asm > synth_noronan.asm
# Append the validation appendix (exposes block routines / SR globals / offsets).
# Concatenated onto the generated copy so the original synth.asm stays untouched.
cat asm_appendix.asm >> synth_noronan.asm
nasm -f elf32 -w-label-orphan synth_noronan.asm -o synth_asm.o
# Fixed-ASM variant: corrects the fastatan table-select ASM bug
# (cmovge edx,ebx -> cmovae edx,ebx, line 235 only; the cmovge at the bitcrusher
# uses different operands). Mirrors the C++ BUG_V2_ATAN_TABLE=0 build so we can
# prove fixed-C++ == fixed-ASM. Original synth.asm stays frozen.
sed 's/cmovge\([[:space:]]*edx, ebx\)/cmovae\1/' synth_noronan.asm > synth_noronan_fixed.asm
nasm -f elf32 -w-label-orphan synth_noronan_fixed.asm -o synth_asm_fixed.o
# The asm exports MSVC-decorated _synth*@N; rename to undecorated names so the
# Linux-compiled player (which references plain `synthInit` via the header) links.
nm synth_asm.o | grep ' T _synth' \
  | sed -E 's/.* T (_synth([A-Za-z]+)@[0-9]+)/\1 synth\2/' > redef.map
objcopy --redefine-syms=redef.map synth_asm.o synth_asm_undec.o

echo "[2/5] compile core under test (synth_core.cpp)"
# -DV2_VALIDATE exposes synthDebugGetBus (read-only bus-tap accessor) for the
# A/B harness; it does not affect any DSP path.
$CXX $CXXFLAGS -DV2_VALIDATE -c ../synth_core.cpp -o synth_cpp.o

echo "[3/5] compile shared player + harness + asm-only stubs"
$CXX $CXXFLAGS -c v2mplayer_port.cpp -o v2mplayer_port.o
$CXX $CXXFLAGS -c harness.cpp -o harness.o
$CXX $CXXFLAGS -c asm_stubs.cpp -o asm_stubs.o

# -no-pie: the asm core is non-PIC 32-bit and uses absolute relocations in .text;
# a PIE link rejects those. Non-PIE executable is fine for a local test rig.
LDFLAGS="-m32 -no-pie -mpc32"  # -mpc32 on the link pulls crtprec32.o (sets PC=24 at startup)

echo "[4/5] link harness_asm"
$CXX $LDFLAGS harness.o v2mplayer_port.o synth_asm_undec.o asm_stubs.o -o harness_asm

echo "[5/5] link harness_cpp"
$CXX $LDFLAGS harness.o v2mplayer_port.o synth_cpp.o -o harness_cpp

echo "[+] component equivalence tests"
nasm -f elf32 tramp.asm -o tramp.o
# comp_osc.cpp #includes ../synth_core.cpp (for V2Osc) and links the RAW asm
# object (decorated _synth*@N coexists with undecorated synthInit -> no clash).
for t in comp_osc comp_flt comp_leaves comp_fastatan comp_trisaw; do
  $CXX $CXXFLAGS -c $t.cpp -o $t.o
  $CXX $LDFLAGS $t.o synth_asm.o tramp.o -o $t
done

# Symmetric fixed-pair validation: FIXED C++ (BUG_V2_ATAN_TABLE=0) linked against
# the FIXED asm (cmovae). These should match just like the faithful pair does.
for t in comp_fastatan comp_leaves; do
  $CXX $CXXFLAGS -DBUG_V2_ATAN_TABLE=0 -c $t.cpp -o ${t}_fixed.o
  $CXX $LDFLAGS ${t}_fixed.o synth_asm_fixed.o tramp.o -o ${t}_fixed
done

echo "done: harness_asm harness_cpp comp_* (+ _fixed pair)"
