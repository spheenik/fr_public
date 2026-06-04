// MSVC -> gcc/Linux build shims for the V2 cores (validation harness, Phase 1).
// Force-included via `g++ -include compat.h` so the original v2/ sources are
// not modified. NONE of this changes synth behavior — it only maps MSVC
// keywords/CRT names onto their gcc/glibc equivalents.
#ifndef VALIDATE_COMPAT_H
#define VALIDATE_COMPAT_H

// MSVC fixed-width 64-bit keyword (used by types.h: signed/unsigned __int64).
#define __int64 long long

// Calling-convention keywords. On 32-bit x86, gcc honors stdcall as an
// attribute (callee cleans the stack) — matching the asm core's ABI.
#ifndef __stdcall
#define __stdcall __attribute__((stdcall))
#endif
#ifndef __cdecl
#define __cdecl
#endif

// MSVC secure-CRT, 3-arg template form used in synth_core.cpp's dprintf().
// `buf` is always a fixed char array there, so sizeof(buf) is its capacity.
#include <cstdio>
#include <cstdarg>
#include <cstring> // memset used by the ported v2mplayer zero-fill
#define vsprintf_s(buf, fmt, arg) vsnprintf((buf), sizeof(buf), (fmt), (arg))

// --- Freq-divergence ledger (validation diagnostic) ------------------------
// One fixed-width record per oscillator/LFO integer-`freq` computation (the only
// non-exact step in the otherwise bit-exact integer phase path). See OpenSpec
// change `characterize-v2-osc-freq-drift`. The C++ core (synth_core.cpp), the asm
// appendix (asm_appendix.asm) and the player (v2mplayer_port.cpp) all agree on this
// 32-byte layout. Records are drained per `synthRender` call (control rate ~344 Hz
// per active voice, so the in-core ring only ever holds one chunk's worth).
#define FREQLOG_CAP 16384   // max records buffered between drains (drain = per render)
typedef struct
{
  unsigned kind;      // 0 = oscillator, 1 = LFO
  unsigned offset;    // byte offset of the osc/LFO object within the V2Synth instance
  unsigned freq;      // computed integer freq, as stored by the fistp (raw bits)
  unsigned pno;       // pre-round float input bits (float-capture pass; else 0)
  unsigned preround;  // pre-fistp product bits     (float-capture pass; else 0)
  unsigned r0, r1, r2;
} FreqLogRec;

#endif // VALIDATE_COMPAT_H
