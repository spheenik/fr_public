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

#endif // VALIDATE_COMPAT_H
