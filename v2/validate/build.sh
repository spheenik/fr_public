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
# Inject per-stage mix-chain snapshot calls into the render frame (validation
# localization tap). Splices `call mixtap_snap_<stage>` after each global FX stage
# so the asm core snapshots mixbuf at the same five points as synth_core.cpp's
# MIXTAP_SNAP. Routines/buffers are defined in asm_appendix.asm (appended below);
# anchored on render-frame-unique tokens. The ORIGINAL synth.asm is untouched.
sed -i \
  -e 's/\(call[[:space:]][[:space:]]*syReverbProcess\)/\tcall mixtap_snap_premix\n\1/' \
  -e 's/\(call[[:space:]][[:space:]]*syReverbProcess\)/\1\n\tcall mixtap_snap_reverb/' \
  -e 's/\(call[[:space:]][[:space:]]*syModDelRenderAux2Main\)/\1\n\tcall mixtap_snap_delay/' \
  -e 's/\(;[[:space:]]*lowcut\/highcut\)/\tcall mixtap_snap_dcf\n\1/' \
  -e 's/\(;[[:space:]]*Kompressor\)/\tcall mixtap_snap_lchc\n\1/' \
  -e 's/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp[[:space:]]*-[[:space:]]*SYN\.compr\]\)/\tcall mixtap_snap_compr\n\1/' \
  synth_noronan.asm
# Sanity: premix (before reverb) + the five post-stage snapshots = 6 calls.
n=$(grep -c 'call mixtap_snap_' synth_noronan.asm)
[ "$n" -eq 6 ] || { echo "ERROR: mixtap injection count $n != 6" >&2; exit 1; }
# Inject per-voice-substage snapshot calls into syV2Render (localization level 2).
# Snapshots mono vcebuf after osc/filter/dist/dcf; anchored on unique section
# comments. Routines/buffers defined in asm_appendix.asm.
sed -i \
  -e 's/\(;[[:space:]]*Filter + Routing\)/\tcall vcetap_snap_osc\n\1/' \
  -e 's/\(;[[:space:]]*distortion\)/\tcall vcetap_snap_flt\n\1/' \
  -e 's/\(;[[:space:]]*dc filter\)/\tcall vcetap_snap_dist\n\1/' \
  -e 's/\(;[[:space:]]*vcebuf (mono) nach chanbuf\)/\tcall vcetap_snap_dcf\n\1/' \
  synth_noronan.asm
n=$(grep -c 'call vcetap_snap_' synth_noronan.asm)
[ "$n" -eq 4 ] || { echo "ERROR: vcetap injection count $n != 4" >&2; exit 1; }
# Inject the channel voice-sum snapshot (post-curvol) before syChanProcess in the
# render block. Captures chanbuf after the voice loop, before the channel chain.
sed -i \
  -e 's/\(call[[:space:]][[:space:]]*syChanProcess\)/\tcall chantap_snap\n\1/' \
  -e 's/\(;[[:space:]]*process all channels\)/\tcall chantap_reset\n\1/' \
  synth_noronan.asm
n=$(grep -c 'call chantap_snap' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: chantap injection count $n != 1" >&2; exit 1; }
n=$(grep -c 'call chantap_reset' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: chantap_reset injection count $n != 1" >&2; exit 1; }
# Inject the per-channel-chain SUB-STAGE snapshot calls into syChanProcess. Each
# is spliced BEFORE the unique `lea ebp,[...]` that follows the matching chain
# render call, i.e. right after that block returns (chanbuf holds its output).
# dist/chorus/dcf2 appear in both fxr-routing branches; only the executed branch
# fires at runtime. Scoped to the syChanProcess body (label..storeChanValues) so
# the identical sub-struct `lea` navigation in syChanSet is NOT matched.
# Routines/buffers defined in asm_appendix.asm.
sed -i '/^syChanProcess/,/^storeChanValues:/{
  s/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp + syWChan\.compw - syWChan\.dcf1w\]\)/\tcall chtap_snap_dcf1\n\1/
  s/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp + syWChan\.boostw - syWChan\.compw\]\)/\tcall chtap_snap_comp\n\1/
  s/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp + 0 - syWChan\.boostw\]\)/\tcall chtap_snap_boost\n\1/
  s/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp - syWChan\.distw + syWChan\.dcf2w\]\)/\tcall chtap_snap_dist\n\1/
  s/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp - syWChan\.dcf2w + syWChan\.chrw\]\)/\tcall chtap_snap_dcf2\n\1/
  s/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp + 0 - syWChan\.dcf2w\]\)/\tcall chtap_snap_dcf2\n\1/
  s/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp - syWChan\.chrw + 0\]\)/\tcall chtap_snap_chorus\n\1/
  s/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp + syWChan\.distw - syWChan\.chrw\]\)/\tcall chtap_snap_chorus\n\1/
}' synth_noronan.asm
# Per-stage counts: dcf1/comp/boost once; dist twice (both branches share an
# anchor); dcf2/chorus twice (one anchor per branch). Total = 9.
for st in dcf1:1 comp:1 boost:1 dist:2 dcf2:2 chorus:2; do
  s=${st%:*}; want=${st#*:}
  got=$(grep -c "call chtap_snap_$s\b" synth_noronan.asm)
  [ "$got" -eq "$want" ] || { echo "ERROR: chtap_snap_$s injection count $got != $want" >&2; exit 1; }
done
n=$(grep -c 'call chtap_snap_' synth_noronan.asm)
[ "$n" -eq 9 ] || { echo "ERROR: chtap injection total $n != 9" >&2; exit 1; }
# Inject the asm-side chorus-integer dump at the end of syModDelSet (ebp = the
# syWModDel base, FPU stack empty). Anchored on the unique mphase shift.
sed -i \
  -e 's/\(shl[[:space:]][[:space:]]*dword \[ebp + syWModDel\.mphase\], 1\)/\1\n\tcall chorusdbg_snap/' \
  synth_noronan.asm
n=$(grep -c 'call chorusdbg_snap' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: chorusdbg_snap injection count $n != 1" >&2; exit 1; }
# Inject the asm-side compressor-integer dump at the end of syCompSet (ebp =
# syWComp base, FPU stack empty). Anchored on the unique release store.
sed -i \
  -e 's/\(fstp[[:space:]][[:space:]]*dword \[ebp + syWComp\.release\]\)/\1\n\tcall comptrace_snap/' \
  synth_noronan.asm
n=$(grep -c 'call comptrace_snap' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: comptrace_snap injection count $n != 1" >&2; exit 1; }
# Inject the asm-side boost-coeff dump at the end of syBoostSet's enabled path
# (ebp = syWBoost base, FPU stack empty). Anchored on the unique b2 store.
sed -i \
  -e 's/\(fstp[[:space:]][[:space:]]*dword \[ebp + syWBoost\.b2\]\)/\1\n\tcall boostdbg_snap/' \
  synth_noronan.asm
n=$(grep -c 'call boostdbg_snap' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: boostdbg_snap injection count $n != 1" >&2; exit 1; }
# Inject the asm-side dist OVERDRIVE/CLIP setup dump at the end of syDistSet's
# shared .mode2b tail (ebp = syWDist base, eax = mode&15, FPU stack empty).
# Anchored on the unique offs store. Fires for voice AND channel dist.
sed -i \
  -e 's/\(fstp[[:space:]][[:space:]]*dword \[ebp + syWDist\.offs\]\)/\1\n\tcall distg2dbg_snap/' \
  synth_noronan.asm
n=$(grep -c 'call distg2dbg_snap' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: distg2dbg_snap injection count $n != 1" >&2; exit 1; }
# Inject the asm-side reverb-coeff dump at the end of syReverbSet (ebp = syWReverb
# base, FPU stack empty). Anchored on the unique lowcut store (last coeff written).
sed -i \
  -e 's/\(fstp[[:space:]][[:space:]]*dword \[ebp + syWReverb\.setup + syCReverb\.lowcut\]\)/\1\n\tcall reverbdbg_snap/' \
  synth_noronan.asm
n=$(grep -c 'call reverbdbg_snap' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: reverbdbg_snap injection count $n != 1" >&2; exit 1; }
# Inject the asm-side env-coeff dump at the end of syEnvSet (ebp = syWEnv base,
# FPU stack empty). Anchored on the unique gain store.
sed -i \
  -e 's/\(fstp[[:space:]][[:space:]]*dword \[ebp + syWEnv\.gain\]\)/\1\n\tcall envdbg_snap/' \
  synth_noronan.asm
n=$(grep -c 'call envdbg_snap' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: envdbg_snap injection count $n != 1" >&2; exit 1; }
# Inject the asm-side voice-allocation dump at ProcessNoteOn.donoteon (ecx=chan,
# edx=slot). Anchored on the unique chanmap store right after the .donoteon label.
sed -i \
  -e 's/\(mov \[ebp + SYN\.chanmap  + 4\*edx\], ecx  ; channel\)/\tcall allocdbg_snap\n\t\1/' \
  synth_noronan.asm
n=$(grep -c 'call allocdbg_snap' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: allocdbg_snap injection count $n != 1" >&2; exit 1; }
# Inject the freq-divergence ledger emit calls after the freq fistp stores in
# syOscChgPitch / syLFOSet (ebp = osc/LFO base, freq stored, FPU empty). Routines
# defined in asm_appendix.asm. Anchored on the unique freq-field fistp stores.
sed -i \
  -e 's/\(fistp[[:space:]][[:space:]]*dword \[ebp + syWOsc\.freq\]\)/\1\n\tcall freqlog_emit_osc/' \
  -e 's/\(fistp[[:space:]][[:space:]]*dword \[ebp + syWLFO\.freq\]\)/\1\n\tcall freqlog_emit_lfo/' \
  synth_noronan.asm
n=$(grep -c 'call freqlog_emit_osc' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: freqlog_emit_osc injection count $n != 1" >&2; exit 1; }
n=$(grep -c 'call freqlog_emit_lfo' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: freqlog_emit_lfo injection count $n != 1" >&2; exit 1; }
# Inject the control-source ledger emit in syV2Tick, after both LFOs tick and ebp is
# restored to the voice base (anchor: the unique `lea ebp,[ebp+0-syWV2.lfo2]`), FPU
# empty. Routine defined in asm_appendix.asm.
sed -i \
  -e 's/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp + 0 - syWV2\.lfo2\]\)/\1\n\tcall ctrllog_emit/' \
  synth_noronan.asm
n=$(grep -c 'call ctrllog_emit' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: ctrllog_emit injection count $n != 1" >&2; exit 1; }
# Inject the filter ledger emit in syFltRender after the regular-filter state store
# (b then l; ebp=filter base). Captures post-block IIR state. Routine in appendix.
sed -i \
  -e 's/\(fstp[[:space:]]*dword \[ebp + syWFlt\.l\]\)/\1\n\tcall fltlog_emit/' \
  synth_noronan.asm
# Inject the filter-input capture at the regular path start (esi=source), before the
# coeff/state loads, so the first input sample is saved for the end-of-block emit.
sed -i \
  -e 's/\(fld[[:space:]][[:space:]]*dword \[ebp + syWFlt\.res\]\)/\tcall fltlog_capin\n\1/' \
  synth_noronan.asm
n=$(grep -c 'call fltlog_capin' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: fltlog_capin injection count $n != 1" >&2; exit 1; }
# Inject the per-voice osc-output capture in syV2Render after the 3 oscs render
# (ebp=voice base, ebx=vcebuf). Routine in appendix.
sed -i \
  -e 's/\(lea[[:space:]][[:space:]]*ebp,[[:space:]]*\[ebp - syWV2\.osc3 + 0\]\)/\1\n\tcall osclog_emit/' \
  synth_noronan.asm
n=$(grep -c 'call osclog_emit' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: osclog_emit injection count $n != 1" >&2; exit 1; }
n=$(grep -c 'call fltlog_emit' synth_noronan.asm)
[ "$n" -eq 1 ] || { echo "ERROR: fltlog_emit injection count $n != 1" >&2; exit 1; }
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
for t in comp_osc comp_flt comp_leaves comp_fastatan comp_trisaw comp_lfo comp_osc_exact; do
  $CXX $CXXFLAGS -c $t.cpp -o $t.o
  $CXX $LDFLAGS $t.o synth_asm.o tramp.o -o $t
done

# Symmetric fixed-pair validation: FIXED C++ (BUG_V2_ATAN_TABLE=0) linked against
# the FIXED asm (cmovae). These should match just like the faithful pair does.
for t in comp_fastatan comp_leaves; do
  $CXX $CXXFLAGS -DBUG_V2_ATAN_TABLE=0 -c $t.cpp -o ${t}_fixed.o
  $CXX $LDFLAGS ${t}_fixed.o synth_asm_fixed.o tramp.o -o ${t}_fixed
done

echo "[+] conv_v2m (old-format .v2m -> current; sources of ../v2m/converted/)"
# Plain C++ tool (no x87-faithful flags needed). shim/ provides a portable
# tool/file.h + windows.h so the original sounddef.cpp builds on Linux;
# __declspec(selectany) maps onto gcc's weak attribute.
$CXX -m32 -std=c++03 -O2 -w -include compat.h -I.. -Ishim \
  -D'__declspec(x)=__attribute__((x))' -D'selectany=weak' \
  conv_v2m.cpp ../sounddef.cpp ../v2defs.cpp ../v2mconv.cpp -o conv_v2m

echo "done: harness_asm harness_cpp comp_* (+ _fixed pair) conv_v2m"
