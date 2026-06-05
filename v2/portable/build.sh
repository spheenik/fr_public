#!/bin/sh
# Build the portable V2M player: static lib + CLI render driver + tests.
#
# FP policy (portable-determinism spec): strict IEEE float32 in the audio
# path. -ffp-contract=off forbids FMA contraction (an FMA skips one rounding
# step -> different bits); fast-math is never used (it would also enable
# FTZ/DAZ via crtfastmath, breaking the v0 subnormal-envelope semantics).
# Optimization level must not change output bits (checked by test driver).
set -e
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
OPT="${OPT:--O2}"
FPFLAGS="-ffp-contract=off"
CXXFLAGS="${OPT} ${FPFLAGS} -Wall -Wextra -std=c++17 ${CXXFLAGS_EXTRA}"

# version-range / feature configuration, e.g.:
#   V2DEFS="-DV2_VER_MIN=6 -DV2_VER_MAX=6 -DV2_RONAN=0" ./build.sh
V2DEFS="${V2DEFS:-}"

echo "[1/3] lib"
for src in v2player v2load v2core v2seq ronan; do
  $CXX $CXXFLAGS $V2DEFS -c $src.cpp -o $src.o
done
ar rcs libv2portable.a v2player.o v2load.o v2core.o v2seq.o ronan.o

echo "[2/3] cli tools"
$CXX $CXXFLAGS $V2DEFS v2dump.cpp libv2portable.a -o v2dump
# (live ALSA player tool: planned, separate from the dependency-free lib)

echo "[3/3] tests"
if [ -f test/mathcheck.cpp ]; then
  $CXX $CXXFLAGS $V2DEFS test/mathcheck.cpp -o test/mathcheck
fi
if [ -f test/twoinstance.cpp ]; then
  $CXX $CXXFLAGS $V2DEFS test/twoinstance.cpp libv2portable.a -o test/twoinstance
fi
if [ -f test/tablecheck.cpp ]; then
  # the lab-build assert (task 4.1): portable format tables vs sounddef.h
  $CXX $CXXFLAGS $V2DEFS test/tablecheck.cpp -o test/tablecheck
fi
if [ -f test/loadcheck.cpp ]; then
  # loader equivalence (task 4.4): identity, conv_v2m byte-equality, rejection
  $CXX $CXXFLAGS $V2DEFS test/loadcheck.cpp libv2portable.a -o test/loadcheck
fi
if [ -f test/forcecheck.cpp ]; then
  # forceBehaviorVersion research knob (task 5.2)
  $CXX $CXXFLAGS $V2DEFS test/forcecheck.cpp libv2portable.a -o test/forcecheck
fi

echo "done: libv2portable.a v2dump"
