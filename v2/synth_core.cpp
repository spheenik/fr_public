#include "types.h"
#include "synth.h"
#include <math.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

// TODO:
// - VU meters?

// Ye olde original V2 bugs you can turn on and off :)
// Each is 1 = ASM-faithful (reproduces the shipping demo audio), 0 = fixed.
//   BUG_V2_FM_RANGE   - broken sine range reduction for FM oscis
//   BUG_V2_ATAN_TABLE - fastatan picks the wrong rational table for |x|>=2
//                       (cmovge vs cmovae on the exponent byte); affects the
//                       distortion "overdrive" mode
//   BUG_V2_COMP_OLDMODE - syCompInit only sets mode=2 and leaves oldmode at 0
//                       (zeroed instance) — but PEAK|MONO|ON also encodes to
//                       0, so a compressor that is peak/mono/on from its very
//                       first set() never takes the mode-change reset and
//                       starts with curgain = 0.0 instead of 1.0 (the
//                       channel's first note fades in over ~25ms instead of
//                       starting at full gain).
#define BUG_V2_FM_RANGE   1
#ifndef BUG_V2_ATAN_TABLE     // overridable from the build (-DBUG_V2_ATAN_TABLE=0)
#define BUG_V2_ATAN_TABLE 1
#endif
#ifndef BUG_V2_COMP_OLDMODE   // overridable from the build (-DBUG_V2_COMP_OLDMODE=0)
#define BUG_V2_COMP_OLDMODE 1
#endif

// Debugging tools
#define DEBUGSCOPES 0
#define COVERAGE    0

// --------------------------------------------------------------------------
// Debug scopes
// --------------------------------------------------------------------------

#if DEBUGSCOPES
#include "scope.h"
#define DEBUG_PLOT_OPEN(which, title, rate, w, h) scopeOpen((which), (title), (rate), (w), (h))
#define DEBUG_PLOT_VAL(which, value) do { float t=value; scopeSubmit((which), &t, 1); } while(0)
#define DEBUG_PLOT(which, data, nsamples) scopeSubmit((which), (data), (nsamples))
#define DEBUG_PLOT_STRIDED(which, data, stride, nsamples) scopeSubmitStrided((which), (data), (stride), (nsamples))
#define DEBUG_PLOT_UPDATE() scopeUpdateAll()
#else
#define DEBUG_PLOT_OPEN(which, title, rate, w, h)
#define DEBUG_PLOT_VAL(which, value)
#define DEBUG_PLOT(which, data, nsamples)
#define DEBUG_PLOT_STRIDED(which, data, stride, nsamples)
#define DEBUG_PLOT_UPDATE()
#endif

#define DEBUG_PLOT_CHAN(which, ch) ((unsigned char *)(which)+(ch))
#define DEBUG_PLOT_STEREO(which, data, nsamples) \
  DEBUG_PLOT_STRIDED(DEBUG_PLOT_CHAN(which, 0), &(data)->l, 2, (nsamples)); \
  DEBUG_PLOT_STRIDED(DEBUG_PLOT_CHAN(which, 1), &(data)->r, 2, (nsamples))

// --------------------------------------------------------------------------
// Code coverage
// --------------------------------------------------------------------------

#if COVERAGE
#include <stdio.h>

static const char *code_coverage[500];
#define COVER(desc) code_coverage[__COUNTER__] = (desc)

static sInt synthGetNumCoverage();
#else
#define COVER(desc)
#endif

// --------------------------------------------------------------------------
// Constants.
// --------------------------------------------------------------------------

// Natural constants
static const sF32 fclowest = 1.220703125e-4f; // 2^(-13) - clamp EGs to 0 below this (their nominal range is 0..128) 
static const sF32 fcpi_2  = 1.5707963267948966192313216916398f;
static const sF32 fcpi    = 3.1415926535897932384626433832795f;
static const sF32 fc1p5pi = 4.7123889803846898576939650749193f;
static const sF32 fc2pi   = 6.28318530717958647692528676655901f;
static const sF32 fc32bit = 2147483648.0f; // 2^31 (original code has (2^31)-1, but this ends up rounding up to 2^31 anyway)

// Synth constants
static const sF32 fcoscbase   = 261.6255653f; // Oscillator base freq
// era <v1 (fr08): the 2000 osc multiplies the pitch-pow2 by this baked constant
// (0x4a4265dc) instead of the SR-computed SRfcobasefrq. = fcoscbase·2²⁹/44100;
// ≈ SRfcobasefrq/4 (the 2000 render advances cnt 4× per sample). DELTA.md d2.
static const sF32 fcoscbase_v0 = 3185015.0f;
static const sF32 fcsrbase    = 44100.0f;     // Base sampling rate
static const sF32 fcboostfreq = 150.0f;       // Bass boost cut-off freq
static const sF32 fcframebase = 128.0f;       // size of a frame in samples
static const sF32 fcdcflt     = 126.0f;
static const sF32 fccfframe   = 11.0f;

static const sF32 fcfmmax     = 2.0f;
static const sF32 fcattackmul = -0.09375f; // -0.0859375
static const sF32 fcattackadd = 7.0f;
// era <v2 (fr08): the original attack multiplier -- kb's comment above IS this
// value; the change to -12/128 came with format v2 (fr08-extraction/DELTA.md).
static const sF32 fcattackmul_v0 = -0.0859375f;
static const sF32 fcsusmul    = 0.0019375f;
static const sF32 fcgain      = 0.6f;
static const sF32 fcgainh     = 0.6f;
static const sF32 fcmdlfomul  = 1973915.49f;
static const sF32 fcrms8192 = 0.0110485434560398050687631931578883f; // asm fci8192 = 1/sqrt(8192), 24-bit
static const sF32 fcmoogsixth = 1.0f/6.0f; // asm fci6 (24-bit float). As a BARE
  // literal inside the moog clip expression gcc/x87 promotes it to an ~exact
  // 80-bit fldt (FLT_EVAL_METHOD=2 constant-promotion bug) — 1 ULP off the
  // asm's fmul dword [fci6] for ~25% of inputs. A static const sF32 loads flds.
static const sF32 fccpdfalloff = 0.9998f; // @@@BUG this should probably depend on sampling rate.

static const sF32 fcdcoffset  = 3.814697265625e-6f; // 2^-18

// --------------------------------------------------------------------------
// General helper functions. 
// --------------------------------------------------------------------------

#define COUNTOF(x)    (sizeof(x)/sizeof(*(x)))

// Float bitcasts. Union-based type punning to maximize compiler
// compatibility.
union FloatBits
{
  sF32 f;
  sU32 u;
};

static sF32 bits2float(sU32 u)
{
  FloatBits x;
  x.u = u;
  return x.f;
}

// Fast arctangent
static sF32 fastatan(sF32 x)
{
  // extract sign
  sF32 sign = 1.0f;
  if (x < 0.0f)
  {
    sign = -1.0f;
    x = -x;
  }

  // we have two rational approximations: one for |x| < 1.0 and one for
  // |x| >= 1.0, both of the general form
  //   r(x) = (cx1*x + cx3*x^3) / (cxm0 + cxm2*x^2 + cxm4*x^4) + bias
  // PORTING FIX (not an ASM bug): the ASM loads these coefficients as DOUBLES
  // (fmul/fadd qword [fcatan*], synth.asm:108-113) — narrowing them to sF32
  // flips the 24-bit rounding of the products/sums for ~28% of inputs (1 ULP).
  // In particular the |x|>=1 bias is the full double pi/2, not 1.57079633f.
  // Values copied verbatim from synth.asm.
  // PORTING FIX (not an ASM bug): cxm2 (x^2 denom coeff) is SHARED across both
  // tables in the ASM (0.76443945); only cxm0/cxm4 are per-table. The original
  // C++ port made cxm2 per-table and swapped it with cxm4. Corrected here.
  static const sF64 coeffs[2][6] = {
    //         cx1          cx3        cxm0        cxm2        cxm4   bias
    {          1.0, 0.43157974,        1.0, 0.76443945, 0.05831938,   0.0 },
    { -0.431597974,       -1.0, 0.05831938, 0.76443945,        1.0,
      1.5707963267948966192313216916398 },
  };
#if BUG_V2_ATAN_TABLE
  // ASM bug: the |x|>=1 table is selected only when the biased exponent == 0x7f
  // (cmovge vs cmovae), i.e. x in [1,2); for x >= 2 it uses the |x|<1 table.
  const sF64 *c = coeffs[x >= 1.0f && x < 2.0f];
#else
  const sF64 *c = coeffs[x >= 1.0f]; // corrected: |x|>=1 table for all x >= 1
#endif
  sF32 x2 = x*x;
  sF32 r = (sF32)((c[1]*x2 + c[0])*x / ((c[4]*x2 + c[3])*x2 + c[2]) + c[5]);
  return r * sign;
}

// Fast sine for x in [-pi/2, pi/2]
// This is a single-precision odd Minimax polynomial, not a Taylor series!
// Coefficients courtesy of Robin Green.
static sF32 fastsin(sF32 x)
{
  sF32 x2 = x*x;
#ifdef V2_X87_FAITHFUL
  // asm fastsin loads the polynomial coeffs as qword DOUBLES (fcsinx3/5/7 = `dq`)
  // and evaluates on the x87. A `float` literal of e.g. -0.16666 is a DIFFERENT
  // value than the double, even at PC=24, so the whole sine (and thus every
  // SIN-mode LFO / sin / FM-sin osc) diverges. Use double constants to match.
  return (sF32)((((-0.00018542*x2 + 0.0083143)*x2 - 0.16666)*x2 + 1.0) * x);
#else
  return (((-0.00018542f*x2 + 0.0083143f)*x2 - 0.16666f)*x2 + 1.0f) * x;
#endif
}

// Fast sine with range check (for x >= 0)
// Applies symmetries, then funnels into fastsin.
static sF32 fastsinrc(sF32 x)
{
  // @@@BUG
  // NB this range reduction really only works for values >=0,
  // yet FM-sine oscillators will also pass in negative values.
  // This is not a good idea. At all. But it's what the original
  // V2 code does. :)

  // first range reduction: mod with 2pi
  x = fmodf(x, fc2pi);
  // now x in [-2pi,2pi]

#if !BUG_V2_FM_RANGE
  if (x < 0.0f)
    x += fc2pi;
#endif

  // need to reduce to [-pi/2, pi/2] to call fastsin
  if (x > fc1p5pi) // x in (3pi/2,2pi]
    x -= fc2pi; // sin(x) = sin(x-2pi)
  else if (x > fcpi_2) // x in (pi/2,3pi/2]
  {
#ifdef V2_X87_FAITHFUL
    // asm fastsinrc reflects about `fldpi` -- the x87's 80-bit pi -- NOT a 32-bit
    // float `fcpi`. At PC=24 the float-pi result rounds to a different 24-bit
    // value (1 ULP), and that ULP, via SIN-LFO -> osc pitch -> freq fistp
    // boundary, accumulates into audible osc phase drift over the song. Use the
    // x87 pi to match. (long double is 80-bit x87 under -mno-sse.)
    x = (sF32)((long double)3.14159265358979323846264L - (long double)x);
#else
    x = fcpi - x; // sin(x) = -sin(x-pi) = sin(-(x-pi)) = sin(pi-x)
#endif
  }

  return fastsin(x);
}

#ifdef V2_X87_FAITHFUL
// Bit-faithful x87 transcendentals for the validation build (x86 only).
// These replicate the asm pow2/pow kernel (synth.asm:355-408) instruction for
// instruction: 2^y via f2xm1/fscale (and base^e via fyl2x). Built with -mpc32
// the ambient x87 control word is PC=24 (24-bit single) / RC=00 (round-nearest),
// matching the asm's per-render `and ax,0F0FFh` setup, so results match
// bit-for-bit. libm pow/powf cannot be used here: they use a different
// polynomial and ignore the caller's control word.
static inline sF32 v2_pow2(sF32 y)              // 2^y   (asm pow2, synth.asm:388)
{
  sF32 r;
  __asm__ ("fld1\n\t"               // 1 y
           "fld %%st(1)\n\t"        // y 1 y
           "fprem\n\t"              // frac 1 y   (frac = y mod 1, |frac|<1)
           "f2xm1\n\t"              // (2^frac-1) 1 y
           "faddp %%st,%%st(1)\n\t" // 2^frac y
           "fscale\n\t"             // 2^frac*2^trunc(y)=2^y , y
           "fstp %%st(1)\n\t"       // 2^y
           : "=t"(r) : "0"(y));
  return r;
}
static inline sF32 v2_pow(sF32 base, sF32 e)    // base^e (asm pow, synth.asm:399)
{
  sF32 r;
  __asm__ ("fyl2x\n\t"              // e*log2(base)  (consumes base=st0, e=st1)
           "fld1\n\t fld %%st(1)\n\t fprem\n\t f2xm1\n\t"
           "faddp %%st,%%st(1)\n\t fscale\n\t fstp %%st(1)\n\t"
           : "=t"(r) : "0"(base), "u"(e) : "st(1)");
  return r;
}
// store st0 as 32-bit int at the ambient rounding mode (round-to-nearest),
// mirroring the asm `fistp` — not the C `(sInt)` cast, which truncates.
static inline sInt v2_fistp(sF32 v)
{
  sInt r;
  __asm__ ("fistpl %0" : "=m"(r) : "t"(v) : "st");
  return r;
}
// freq = round_to_nearest(pow2(arg) * base), computed as ONE x87 sequence exactly
// like the asm syOscChgPitch (pow2 / fmul [SRfcobasefrq] / fistp). Doing the mul
// and store while pow2's result is still live in the register -- rather than
// returning it as an sF32 first -- matters on exact-tie products where the early
// 24-bit round flips the fistp (and thus the integer osc freq) by 1.
// fci12 = rounded 1/12 as a 24-bit float (asm `fci12 dd 0.083333333333` = 0x3daaaaab).
// MUST be a real in-memory float so the asm below loads it with `fmul dword` (24-bit)
// like the asm core. Writing the bare literal `0.083333333333f` inline makes GCC
// load it via `fldt` at 80-bit excess precision (~exact 1/12), which is a DIFFERENT
// value -> the freq arg differs by 1 ULP -> the freq fistp flips on tie products.
static const sF32 v2_fci12 = 0.083333333333f;
// freq = round( pow2( (pitch+note-60) * fci12 ) * base ), the whole syOscChgPitch
// freq tail as ONE x87 sequence with a 24-bit fci12 -- bit-exact with the asm.
static inline sInt v2_oscfreq(sF32 pno, sF32 base) // pno = pitch+note-60
{
  sInt r;
  __asm__ ("fmul %3\n\t"            // pno * fci12 (24-bit memory float)
           "fld1\n\t"
           "fld %%st(1)\n\t"
           "fprem\n\t"
           "f2xm1\n\t"
           "faddp %%st,%%st(1)\n\t"
           "fscale\n\t"
           "fstp %%st(1)\n\t"       // pow2(pno*fci12)
           "fmul %2\n\t"            // * base
           "fistpl %0\n\t"          // round-to-nearest -> int
           : "=m"(r) : "t"(pno), "m"(base), "m"(v2_fci12) : "st");
  return r;
}
// gain2 = (param1/128) / atan(gain1), the whole dist OVERDRIVE setup tail as
// ONE x87 sequence like asm .mode1 (synth.asm:1831-1838: fld gain1 / fld1 /
// fpatan / fdivp). fpatan computes at full internal precision regardless of
// PC=24 (precision control does not apply to x87 transcendentals); only the
// divide rounds. Rounding atan() to sF32 before dividing -- as a plain
// v2_atan() wrapper would -- double-rounds and misses the asm by 1 ULP
// (libm double atan was 1 ULP off on 102k of 107k pzero set calls).
static inline sF32 v2_overdrive_gain2(sF32 p1g, sF32 gain1) // p1g = param1/128
{
  sF32 r;
  __asm__ ("fld1\n\t"
           "fpatan\n\t"                  // atan(gain1), full precision; pops
           "fdivrp %%st, %%st(1)\n\t"    // p1g / atan -> st0, rounds at PC=24
                                         // (gas swaps fdivp/fdivrp p-form
                                         // mnemonics vs Intel; verified by the
                                         // DISTG2TRACE oracle: fdivp gave the
                                         // reciprocal)
           : "=t"(r) : "0"(gain1), "u"(p1g) : "st(1)");
  return r;
}
// native x87 fsin, for the year-2000 osc sine (syOscRender @0x40a624 `fsin`).
// The host x87 FSIN low bits are CPU-vendor-dependent; this is host-relative
// "bit-exact" by design (DELTA.md delta 3, same stance as V2_X87_FAITHFUL).
static inline sF32 v2_fsin(sF32 x)
{
  sF32 r;
  __asm__ ("fsin" : "=t"(r) : "0"(x));
  return r;
}
// native x87 atan (= fpatan(x, 1)), for the year-2000 overdrive waveshaper
// (syDistRenderMono @0x40ab88 `fld1; fpatan`). 2004 uses the fastatan
// polynomial instead (DELTA.md: "no fastatan ... overdrive differs").
static inline sF32 v2_atan(sF32 x)
{
  sF32 r;
  __asm__ ("fld1\n\t fpatan\n\t" : "=t"(r) : "0"(x));
  return r;
}
#endif

// 2^x and base^e: faithful x87 kernels in the validation build, libm otherwise.
// Routing ALL transcendentals through these makes every coefficient (env, lfo,
// filter, distortion, reverb, mod-delay, compressor) match the asm in the
// faithful build, not just the oscillator frequency.
static inline sF32 v2_exp2(sF32 x)
{
#ifdef V2_X87_FAITHFUL
  return v2_pow2(x);
#else
  return powf(2.0f, x);
#endif
}
static inline sF32 v2_sin(sF32 x)
{
#ifdef V2_X87_FAITHFUL
  return v2_fsin(x);
#else
  return sinf(x);
#endif
}
static inline sF32 v2_atanf(sF32 x)  // native atan for the eraV0 overdrive
{
#ifdef V2_X87_FAITHFUL
  return v2_atan(x);
#else
  return atanf(x);
#endif
}
static inline sF32 v2_powf(sF32 base, sF32 e)
{
#ifdef V2_X87_FAITHFUL
  return v2_pow(base, e);
#else
  return powf(base, e);
#endif
}

static sF32 calcfreq(sF32 x)  { return v2_exp2((x - 1.0f) * 10.0f); }
static sF32 calcfreq2(sF32 x) { return v2_exp2((x - 1.0f) * fccfframe); }

// tri/saw box-filter working type. The "hard" cases (b/d/e/f) have a
// catastrophic cancellation amplified by rcpf=1/f. The asm computes this on the
// x87 at 24-bit mantissa / 15-bit exponent. The faithful build runs x87 at
// PC=24 (-mpc32) so plain `float` reproduces that exactly. The portable build
// uses SSE single (8-bit exponent), which would overflow the cancellation at
// low frequencies, so it uses `double` as an approximation of the asm.
#ifdef V2_X87_FAITHFUL
typedef sF32 trisaw_flt;
#else
typedef double trisaw_flt;
#endif

// square
static inline sF32 sqr(sF32 x)
{
  return x*x;
}

template<typename T>
static inline T min(T a, T b)
{
  return a < b ? a : b;
}

template<typename T>
static inline T max(T a, T b)
{
  return a > b ? a : b;
}

template<typename T>
static inline T clamp(T x, T min, T max)
{
  return (x < min) ? min : (x > max) ? max : x;
}

// uniform randon number generator
// just a linear congruential generator, nothing fancy.
static inline sU32 urandom(sU32 *seed)
{
  *seed = *seed * 196314165 + 907633515;
  return *seed;
}

// uniform random float in [-1,1)
static inline sF32 frandom(sU32 *seed)
{
  sU32 bits = urandom(seed); // random 32-bit value
  sF32 f = bits2float((bits >> 9) | 0x40000000); // random float in [2,4)
  return f - 3.0f; // uniform random float in [-1,1)
}

// 32-bit value into float with 23 bits percision
static inline sF32 utof23(sU32 x)
{
  sF32 f = bits2float((x >> 9) | 0x3f800000); // 1 + x/(2^32)
  return f - 1.0f;
}

// float from [0,1) into 0.32 unsigned fixed-point
// this loses a bit, but that's what V2 does.
static inline sU32 ftou32(sF32 v)
{
  // asm computes these as `fistp; shl x,1` = 2*round_to_nearest(v*fc32bit). The
  // port truncated via (sInt); that off-by-one (when frac>=0.5) desyncs every
  // ftou32-derived integer phase/breakpoint (osc brpt, lfo cphase, dist dfreq,
  // moddel mphase). Match the asm round in the faithful build.
#ifdef V2_X87_FAITHFUL
  return 2u * (sU32)v2_fistp(v * fc32bit);
#else
  return 2u * (sInt)(v * fc32bit);
#endif
}

// linear interpolation between a and b using t.
static inline sF32 lerp(sF32 a, sF32 b, sF32 t)
{
  return a + t * (b-a);
}

// DEBUG
#include <stdarg.h>
#include <stdio.h>
extern "C" void __stdcall OutputDebugStringA(const char *what);
static void dprintf(const char *fmt, ...)
{
  char buf[256];
  va_list arg;
  va_start(arg, fmt);
  vsprintf_s(buf, fmt, arg);
  va_end(arg);
  OutputDebugStringA(buf);
}

// --------------------------------------------------------------------------
// Building blocks
// --------------------------------------------------------------------------

union StereoSample
{
  struct
  {
    sF32 l, r;
  };
  sF32 ch[2];
};

// LRC filter.
// The state variables are 'l' and 'b'. The time series for l and b
// correspond to a resonant low-pass and band-pass respectively, hence
// the name. 'step' returns 'h', which is just the "missing" resonant
// high-pass.
//
// Note that 'freq' here isn't actually a frequency at all, it's actually
// 2*(1 - cos(2pi*freq/SR)), but V2 calls this "frequency" anyway :)
struct V2LRC
{
  sF32 l, b;

  void init()
  {
    l = b = 0.0f;
  }

  // Single step
  sF32 step(sF32 in, sF32 freq, sF32 reso)
  {
    l += freq * b;
    sF32 h = in - b*reso - l;
    b += freq * h;
    return h;
  }

  // 2x oversampled step (the good stuff)
  sF32 step_2x(sF32 in, sF32 freq, sF32 reso)
  {
    // the filters get slightly biased inputs to avoid the state variables
    // getting too close to 0 for prolonged periods of time (which would
    // cause denormals to appear)
    in += fcdcoffset;

    // step 1
    l += freq * b - fcdcoffset; // undo bias here (1 sample delay)
    b += freq * (in - b*reso - l);

    // step 2
    l += freq * b;
    sF32 h = in - b*reso - l;
    b += freq * h;

    return h;
  }
};

// VCF block-working type. The asm (syFltRender) keeps the SVF state l,b (and coeffs
// f,r) on the x87 stack across the WHOLE block -- 15-bit exponent, mantissa at PC=24
// (24-bit). The port's V2LRC.l/b are 32-bit floats (8-bit exponent), stored back each
// sample; the high-pass output h = in - b*reso - l is a cancellation, so the narrower
// exponent of an intermediate diverges in HIGH/BAND serial chains. Mirror the asm:
// keep the block state at register width (long double under PC=24 == the asm's
// register), narrowing to 32-bit only on store between blocks (the asm's fstp dword).
#ifdef V2_X87_FAITHFUL
typedef long double flcalc;
#else
typedef sF32 flcalc;
#endif

// 2x-oversampled SVF step on register-width working state (mirrors V2LRC::step_2x /
// the asm .process). Updates l,b in place; returns the high-pass output h.
// dco = the denormal-prevention DC bias (fcdcoffset normally; 0 for era <v1,
// where the 2000 syFltRender injects no bias -- DELTA.md delta 6).
static inline flcalc lrc_step_2x(flcalc &l, flcalc &b, flcalc in, flcalc freq, flcalc reso, flcalc dco)
{
  in += dco;
  l += freq * b - dco;
  b += freq * (in - b*reso - l);
  l += freq * b;
  flcalc h = in - b*reso - l;
  b += freq * h;
  return h;
}

// Moog filter state
struct V2Moog
{
  sF32 b[5]; // filter state

  void init()
  {
    b[0] = b[1] = b[2] = b[3] = b[4] = 0.0f;
  }

  sF32 step(sF32 realin, sF32 f, sF32 p, sF32 q)
  {
    sF32 in = realin + fcdcoffset; // again, biased in
    sF32 t1, t2, t3, b4;

    in -= q * b[4]; // feedback
    t1 = b[1]; b[1] = (in + b[0]) * p + b[1] * f;
    t2 = b[2]; b[2] = (t1 + b[1]) * p + b[2] * f;
    t3 = b[3]; b[3] = (t2 + b[2]) * p + b[3] * f;
               b4   = (t3 + b[3]) * p + b[4] * f;

    b4 -= b4*b4*b4 * fcmoogsixth; // clipping (see fcmoogsixth: do NOT inline the literal)
    b4 -= fcdcoffset; // un-bias
    b[4] = b4; // feedback state keeps the once-unbiased value (matches ASM)
    b[0] = realin;

    return b4;
  }
};

// DC filter state. Just a highpass used with a very low cut-off
// to remove DC offsets from a signal.
struct V2DCF
{
  sF32 xm1; // x(n-1)
  sF32 ym1; // y(n-1)

  void init()
  {
    xm1 = ym1 = 0.0f;
  }

  sF32 step(sF32 in, sF32 R)
  {
    // y(n) = x(n) - x(n-1) + R*y(n-1)
    // asm syDCFRenderStereo order: ((R*ym1 - xm1 + in) + dc) - dc -- the denormal-
    // avoidance bias `dc` is added AFTER the main sum, not before. Writing it as
    // `(dc + R*ym1 - xm1 + in) - dc` rounds the recurrence differently each sample;
    // with the pole R~0.997 that ~1 ULP error integrates (1/(1-R)~333x) into audible
    // channel drift. Match the asm operand order exactly.
    sF32 y = (R*ym1 - xm1 + in + fcdcoffset) - fcdcoffset;
    xm1 = in;
    ym1 = y;
    return y;
  }
};

// Constant-length delay line
class V2Delay
{
  sU32 pos, len;
  sF32 *buf;

public:
  V2Delay()
    : pos(0), len(0), buf(0)
  {
  }

  void init(sF32 *buf, sU32 len)
  {
    this->buf = buf;
    this->len = len;
    reset();
  }

  template<sU32 N>
  void init(sF32 (&buf)[N])
  {
    init(buf, N);
  }

  void reset()
  {
    memset(buf, 0, sizeof(*buf)*len);
    pos = 0;
  }

  inline sF32 fetch() const
  {
    return buf[pos];
  }

  inline void feed(sF32 v)
  {
    buf[pos] = v;
    if (++pos == len)
      pos = 0;
  }
};

// debug stuff
static void checkRange(const sF32 *src, sInt nsamples)
{
  for (sInt i=0; i < nsamples; i++)
    assert(src[i] >= -1.5f && src[i] < 1.5f);
}

static void checkRange(const StereoSample *src, sInt nsamples)
{
  checkRange(&src[0].l, nsamples * 2);
}

// --------------------------------------------------------------------------
// V2 Instance
// --------------------------------------------------------------------------

struct V2Instance
{
  static const int MAX_FRAME_SIZE = 280; // in samples

  // Era compat (fr08-extraction/DELTA.md): the v2m format version the song
  // was ORIGINALLY authored as, before any v2mconv upgrade. SRCVER_MODERN
  // (= the 2004 core, format v6) by default; synthSetSourceVersion() lowers it
  // for period files, gating the confirmed period DSP behaviors. The envelope
  // delta is dated to format v2 by kb's own transEnv (v2mconv.cpp); the other
  // deltas have binary evidence for v0 (fr08) only.
  static const sInt SRCVER_MODERN = 6;
  sInt srcVersion;
  bool eraEnvOld()  const { return srcVersion < 2; } // env attack/dec/rel scaling
  bool eraV0()      const { return srcVersion < 1; } // all other fr08-era deltas

  // Stuff that depends on the sample rate
  sF32 SRfcsamplesperms;
  sF32 SRfcobasefrq;
  sF32 SRfclinfreq;
  sF32 SRfcBoostCos, SRfcBoostSin;
  sF32 SRfcdcfilter;

  sInt SRcFrameSize;
  sF32 SRfciframe;

  // buffers
  sF32 vcebuf[MAX_FRAME_SIZE];
  sF32 vcebuf2[MAX_FRAME_SIZE];
  StereoSample levelbuf[MAX_FRAME_SIZE]; // original V2 overlaps level buffer with voice buffers
  StereoSample chanbuf[MAX_FRAME_SIZE];
  sF32 aux1buf[MAX_FRAME_SIZE];
  sF32 aux2buf[MAX_FRAME_SIZE];
  StereoSample mixbuf[MAX_FRAME_SIZE];
  StereoSample auxabuf[MAX_FRAME_SIZE];
  StereoSample auxbbuf[MAX_FRAME_SIZE];

  void calcNewSampleRate(sInt samplerate)
  {
    sF32 sr = (sF32)samplerate;

    // asm calcNewSampleRate computes the reciprocal 1/sr ONCE (fld1/fdiv) and
    // MULTIPLIES by it; `a/sr` and `a*(1/sr)` round differently (1 ULP). That ULP
    // in SRfcobasefrq flips the osc freq fistp on some notes -> phase drift. Match
    // the asm: a single reciprocal, multiply, in the asm's operand order.
    sF32 recip = 1.0f / sr;

    SRfcsamplesperms = sr / 1000.0f; // (not computed by the asm; sr/1000 is fine)
    SRfcobasefrq = (fcoscbase * fc32bit) * recip;
    SRfclinfreq = fcsrbase * recip;
    SRfcdcfilter = 1.0f - fcdcflt * recip;

    // frame size
    SRcFrameSize = (sInt)(fcframebase * sr / fcsrbase + 0.5f);
    SRfciframe = 1.0f / (sF32)SRcFrameSize;

    assert(SRcFrameSize <= MAX_FRAME_SIZE);

    // low shelving EQ (asm order: (1/sr)*fc2pi*fcboostfreq)
    sF32 boost = recip * fc2pi * fcboostfreq;
    SRfcBoostCos = cos(boost);
    SRfcBoostSin = sin(boost);
#ifdef V2_VALIDATE
    if (getenv("SRTRACE")) {
      union { sF32 f; sU32 u; } a,b,c,d,e,g;
      a.f=SRfcsamplesperms; b.f=SRfcobasefrq; c.f=SRfclinfreq;
      d.f=SRfcdcfilter; e.f=SRfcBoostCos; g.f=SRfcBoostSin;
      fprintf(stderr, "[C++ SR] samplesperms=%08x obasefrq=%08x linfreq=%08x dcfilter=%08x BoostCos=%08x BoostSin=%08x\n",
              a.u, b.u, c.u, d.u, e.u, g.u);
    }
#endif
  }
};

#ifdef V2_VALIDATE
// --------------------------------------------------------------------------
// Freq-divergence ledger (validation diagnostic, OpenSpec change
// `characterize-v2-osc-freq-drift`). Osc/LFO phase are exact integer
// accumulators; the only non-exact step is computing the integer `freq` via the
// x87 fistp. We append one record per freq computation in BOTH cores and diff
// them call-for-call: the diffuse whole-song residual becomes a finite list of
// discrete ±1 tie-flip events. Mirrored in asm_appendix.asm. Drained per
// synthRender. Pure bookkeeping into a separate buffer -- never touches a DSP
// value or the FPU, so it is inert w.r.t. the audio output.
static FreqLogRec g_freqlog_buf[FREQLOG_CAP];
static unsigned   g_freqlog_count = 0;
static char      *g_freqlog_synthbase = 0;  // set at render() entry; mirrors asm [this]

// Control-source ledger: one record per voice per tick, capturing the four
// accumulating modulation sources (aenv/env2 out, lfo1/lfo2 out) after they tick.
// Diffing this pins WHICH source's accumulated state drifts (the linear pitch drift
// feeds osc pitch -> washes out, and filter cutoff -> the audible residual).
static FreqLogRec g_ctrllog_buf[FREQLOG_CAP];
static unsigned   g_ctrllog_count = 0;

// Filter ledger: one record per V2Flt::set, capturing the modulated cutoff/reso
// INPUT and the computed coefficient OUTPUT, to localize a filter-path divergence
// to upstream (modulated input differs) vs filter-internal (input matches, coeff/
// state differs).
static FreqLogRec g_fltlog_buf[FREQLOG_CAP];
static unsigned   g_fltlog_count = 0;

// Osc-output ledger: per-voice osc output (vcebuf[0..2], before any filter), to
// settle whether a voice's osc output diverges (vs the filter/serial chain).
static FreqLogRec g_osclog_buf[FREQLOG_CAP];
static unsigned   g_osclog_count = 0;

// Knockout: when armed, each freq computation is overridden with the asm core's
// value, replayed in call order from a previously recorded asm ledger. If freq is
// the sole source of the residual, the whole-song A/B then collapses to ~0.
static const FreqLogRec *g_freqknockout    = 0;  // replay table (asm ledger), or 0
static unsigned          g_freqknockout_n  = 0;
static unsigned          g_freqknockout_pos = 0;

static inline unsigned f2u(sF32 f) { union { sF32 f; unsigned u; } c; c.f = f; return c.u; }
static inline sF32     u2f(unsigned u) { union { sF32 f; unsigned u; } c; c.u = u; return c.f; }

// Record one freq computation. For osc, pitch_bits = the upstream pitch input and
// nffrq_bits = the noise-filter freq computed alongside (the chgPitch output the
// freq knockout originally ignored). For lfo, pitch_bits = the rate input, nffrq=0.
static inline void freqlog_emit(unsigned kind, const void *obj, unsigned freq,
                                unsigned pitch_bits, unsigned nffrq_bits,
                                unsigned cnt, unsigned gain_bits, unsigned brpt)
{
  if (g_freqlog_count < FREQLOG_CAP)
  {
    FreqLogRec &r = g_freqlog_buf[g_freqlog_count++];
    r.kind = kind;
    r.offset = (unsigned)((char *)obj - g_freqlog_synthbase);
    r.freq = freq;
    r.pno = pitch_bits;     // upstream input (pitch for osc, rate for lfo)
    r.preround = nffrq_bits; // noise-filter freq (osc only)
    r.r0 = cnt;             // osc phase accumulator (block-start)
    r.r1 = gain_bits;       // osc MODE (reused; gain confirmed exact)
    r.r2 = brpt;            // osc noise-filter band state nf.b (reused; brpt exact)
  }
}

// Record one voice's four modulation-source outputs (kind=2). Fields reused:
// freq=aenv.out, pno=env2.out, preround=lfo1.out, r0=lfo2.out (all float bits).
static inline void ctrllog_emit(const void *voiceobj, sF32 e0, sF32 e1, sF32 l0, sF32 l1,
                                unsigned env2suf, unsigned states)
{
  if (g_ctrllog_count < FREQLOG_CAP)
  {
    FreqLogRec &r = g_ctrllog_buf[g_ctrllog_count++];
    r.kind = 2;
    r.offset = (unsigned)((char *)voiceobj - g_freqlog_synthbase);
    r.freq = f2u(e0); r.pno = f2u(e1); r.preround = f2u(l0); r.r0 = f2u(l1);
    r.r1 = env2suf;     // env2 sustain factor (suf) bits
    r.r2 = states;      // aenv.state | (env2.state<<8)
  }
}

// Knockout: replay the asm record for this call ordinal. Returns the asm freq, and
// (osc only, nffrq_io != 0) also overrides *nffrq_io with the asm nffrq -- so the
// WHOLE chgPitch float output is cross-fed, not just the integer freq. Aligned by
// call ordinal; asserts kind only (raw offsets are not cross-core comparable).
static inline sInt freqlog_knockout(unsigned kind, const void *obj, sInt freq, sF32 *nffrq_io)
{
  if (!g_freqknockout) return freq;
  if (g_freqknockout_pos >= g_freqknockout_n)
  {
    // Past the recorded ledger (e.g. a slightly longer auto decay tail): fall
    // through to the core's own values rather than aborting. Warn once.
    static int warned = 0;
    if (!warned) { warned = 1;
      fprintf(stderr, "[FREQKNOCKOUT] past asm ledger at ordinal %u; using cpp values for the tail\n",
              g_freqknockout_pos); }
    return freq;
  }
  const FreqLogRec &r = g_freqknockout[g_freqknockout_pos++];
  (void)obj;
  if (r.kind != kind)
  {
    fprintf(stderr, "[FREQKNOCKOUT] kind desync at ordinal %u: asm=%u vs cpp=%u\n",
            g_freqknockout_pos - 1, r.kind, kind);
    abort();
  }
  if (nffrq_io) *nffrq_io = u2f(r.preround);
  return (sInt)r.freq;
}
#endif // V2_VALIDATE

// --------------------------------------------------------------------------
// Oscillator
// --------------------------------------------------------------------------

struct syVOsc
{
  sF32 mode;    // OSC_* (as float. it's all floats in here)
  sF32 ring;
  sF32 pitch;
  sF32 detune;
  sF32 color;
  sF32 gain;
};

struct V2Osc
{
  enum Mode
  {
    OSC_OFF     = 0,
    OSC_TRI_SAW = 1,
    OSC_PULSE   = 2,
    OSC_SIN     = 3,
    OSC_NOISE   = 4,
    OSC_FM_SIN  = 5,
    OSC_AUXA    = 6,
    OSC_AUXB    = 7,
  };

  sInt mode;          // OSC_*
  bool ring;          // ring modulation on/off
  sU32 cnt;           // wave counter
  sInt freq;          // wave counter inc (8x/sample)
  sU32 brpt;          // break point for tri/pulse wave
  sF32 nffrq, nfres;  // noise filter freq/resonance
  sU32 nseed;         // noise random seed
  sF32 gain;          // output gain
  V2LRC nf;           // noise filter
  sF32 note;
  sF32 pitch;

  // era <v1 (fr08): the 2000 osc renders tri/saw + pulse as a 4x linearly-
  // oversampled numeric box filter (syOscRender @0x40a585), not the 2004
  // analytic convolution. These are the per-segment line coeffs syOscSet
  // @0x40a4d2 precomputes; see fr08-extraction/DELTA.md.
  sF32 v0_dn_k, v0_dn_o;  // cnt<brpt segment: val = p*dn_k + dn_o
  sF32 v0_up_k, v0_up_o;  // cnt>=brpt segment: val = p*up_k + up_o

  V2Instance *inst;   // V2 instance we belong to.

  void init(V2Instance *instance, sInt idx)
  {
    static const sU32 seeds[] = { 0xdeadbeefu, 0xbaadf00du, 0xd3adc0deu };
    assert(idx < COUNTOF(seeds));

    cnt = 0;
    nf.init();
    // era <v1 (fr08): the 2000 syOscInit seeds nseed from rdtsc; the C1 ground
    // truth pins rdtsc=0, so the matched-seed A/B (DELTA.md D6) needs nseed=0
    // here too. (At the first synthInit srcVersion is still MODERN; the era
    // setter re-seeds those voices. keysync noteOns re-init and hit this path
    // with the real srcVersion.)
    nseed = instance->eraV0() ? 0u : seeds[idx];
    inst = instance;
  }

  void noteOn()
  {
    chgPitch();
  }

  void chgPitch()
  {
#ifdef V2_X87_FAITHFUL
    // asm syOscChgPitch (synth.asm:488-502): x87 at PC=24. fci128=0.0078125
    // (=1/128, exact), fci12=0.083333333333 (rounded 1/12, a MULTIPLY not a
    // divide), pow2 via f2xm1, freq stored with fistp (round-to-nearest).
    nffrq = inst->SRfclinfreq * calcfreq((pitch + 64.0f) * 0.0078125f);
    // whole freq tail (·fci12, pow2, ·base, fistp) as one x87 sequence with a
    // 24-bit fci12 -- bit-exact with the asm (no 80-bit constant / excess precision).
    // era <v1 (fr08): the 2000 synth multiplies by a baked constant 3185015.0
    // (= fcoscbase·2²⁹/44100 ≈ SRfcobasefrq/4) and advances cnt 4× per sample
    // in the oversampled render; 2004 computes SRfcobasefrq at runtime and
    // advances once. Same pitch, but the baked constant rounds differently.
    freq = v2_oscfreq(pitch + note - 60.0f,
                      inst->eraV0() ? fcoscbase_v0 : inst->SRfcobasefrq);
#else
    nffrq = inst->SRfclinfreq * calcfreq((pitch + 64.0f) / 128.0f);
    freq = (sInt)((inst->eraV0() ? fcoscbase_v0 : inst->SRfcobasefrq)
                  * pow(2.0f, (pitch + note - 60.0f) / 12.0f));
#endif
#ifdef V2_VALIDATE
    freqlog_emit(0 /*osc*/, this, (unsigned)freq, f2u(pitch), f2u(nffrq), cnt, (unsigned)mode, f2u(nf.b));
    freq = freqlog_knockout(0 /*osc*/, this, freq, &nffrq);
#endif
  }

  void set(const syVOsc *para)
  {
    mode = (sInt)para->mode;
    ring = (((sInt)para->ring) & 1) != 0;
#ifdef V2_VALIDATE
    if (getenv("OSCDUMP")) { static int n=0; if(n++<24)
      fprintf(stderr,"[OSCDUMP] mode=%d ring=%d pitch=%.3f detune=%.1f color=%.1f gain=%.1f\n",
              mode,(int)ring,pitch,para->detune,para->color,para->gain); }
#endif

    pitch = (para->pitch - 64.0f) + (para->detune - 64.0f) / 128.0f;
    chgPitch();
    gain = para->gain / 128.0f;

    sF32 col = para->color / 128.0f;
    brpt = ftou32(col);
    nfres = 1.0f - sqrtf(col);

    if (inst->eraV0())
    {
      // 2000 syOscSet @0x40a4d2 box-segment coeffs (x87 PC=24, faithful build).
      // down (cnt<brpt, ramp -1->+1):  val = p*(2/col) + (-1-2/col)
      // up   (cnt>=brpt, ramp +1->-1): val = p*(-2/(1-col)) + (4/(1-col)-1)
      v0_dn_k = 2.0f / col;
      v0_dn_o = -1.0f - 2.0f / col;
      v0_up_k = -2.0f / (1.0f - col);
      v0_up_o = 4.0f / (1.0f - col) - 1.0f;
    }
  }

  void render(sF32 *dest, sInt nsamples)
  {
    if (inst->eraV0())
    {
      // 2000 syOscRender @0x40a585: tri/saw + pulse are 4x-oversampled box
      // filters, sine uses native fsin, noise uses the MSVC LCG. AUXA/AUXB and
      // ring don't exist in v0 (modes 6/7 unused; conv2m defaults them off).
      switch (mode & 7)
      {
      case OSC_OFF:     break;
      case OSC_TRI_SAW: renderTriSaw_v0(dest, nsamples); break;
      case OSC_PULSE:   renderPulse_v0(dest, nsamples); break;
      case OSC_SIN:     renderSin_v0(dest, nsamples); break;
      case OSC_NOISE:   renderNoise_v0(dest, nsamples); break;
      case OSC_FM_SIN:  renderFMSin(dest, nsamples); break;  // FM shared
      default:          break;
      }
      DEBUG_PLOT(this, dest, nsamples);
      return;
    }

    switch (mode & 7)
    {
    case OSC_OFF:     break;
    case OSC_TRI_SAW: renderTriSaw(dest, nsamples); break;
    case OSC_PULSE:   renderPulse(dest, nsamples); break;
    case OSC_SIN:     renderSin(dest, nsamples); break;
    case OSC_NOISE:   renderNoise(dest, nsamples); break;
    case OSC_FM_SIN:  renderFMSin(dest, nsamples); break;
    case OSC_AUXA:    renderAux(dest, inst->auxabuf, nsamples); break;
    case OSC_AUXB:    renderAux(dest, inst->auxbbuf, nsamples); break;
    }

    DEBUG_PLOT(this, dest, nsamples);
  }

private:
  // ---- year-2000 (era <v1) oscillator renderers (DELTA.md delta 2-4) --------
  // Faithful ports of syOscRender @0x40a585. The faithful build runs x87 at
  // PC=24 (-mpc32), so plain float arithmetic reproduces the 2000 rounding;
  // only the [1,2)-from-counter bit trick, the fistp, and fsin are special.

  // counter top bits -> float in [1,2): asm `shr eax,9; or 0x3f800000`.
  static inline sF32 v0_cnt2f(sU32 c)
  {
    return bits2float((c >> 9) | 0x3f800000);
  }

  void renderTriSaw_v0(sF32 *dest, sInt nsamples)
  {
    COVER("Osc v0 trisaw");
    sU32 c = cnt;
    sF32 bg = gain * 0.25f; // box gain = (gain/128)*0.25  (gain already /128)
    for (sInt i=0; i < nsamples; i++)
    {
      sF32 acc = 0.0f;
      for (sInt s=0; s < 4; s++) // 4x oversample + box average
      {
        sF32 p = v0_cnt2f(c);
        acc += (c < brpt) ? (p * v0_dn_k + v0_dn_o) : (p * v0_up_k + v0_up_o);
        c += (sU32)freq;
      }
      output(dest + i, acc * bg);
    }
    cnt = c;
  }

  void renderPulse_v0(sF32 *dest, sInt nsamples)
  {
    COVER("Osc v0 pulse");
    sU32 c = cnt;
    sF32 bg = gain * 0.25f;
    for (sInt i=0; i < nsamples; i++)
    {
      sF32 acc = 0.0f;
      for (sInt s=0; s < 4; s++)
      {
        if (c >= brpt) acc += 2.0f; // each high sub-sample contributes +2
        c += (sU32)freq;
      }
      output(dest + i, (acc - 4.0f) * bg); // {-4..+4} * box gain
    }
    cnt = c;
  }

  void renderSin_v0(sF32 *dest, sInt nsamples)
  {
    COVER("Osc v0 sin");
    sU32 c = cnt;
    sU32 step = (sU32)freq << 2; // sine advances 4*freq per output sample
    for (sInt i=0; i < nsamples; i++)
    {
      sF32 p = v0_cnt2f(c);
      c += step;
      output(dest + i, v2_sin(p * fc2pi) * gain); // native fsin, gain=gain/128
    }
    cnt = c;
  }

  void renderNoise_v0(sF32 *dest, sInt nsamples)
  {
    COVER("Osc v0 noise");
    // Exact 2000 noise + resonant LRC, traced from syOscRender @0x40a659. Note
    // this is a DIFFERENT recurrence/output from the 2004 V2LRC.step (and uses
    // the MSVC LCG + a 16-bit float gen). nf.{l,b} hold the two filter states.
    sF32 sl = nf.l, sb = nf.b, f = nffrq, r = nfres;
    sU32 seed = nseed;
    for (sInt i=0; i < nsamples; i++)
    {
      seed = seed * 214013 + 2531011;          // MSVC LCG (0x343fd/0x269ec3)
      // 2000 float gen: (seed&0xffff)<<7 | 0x40000000 -> [2,4); n = that - 3
      sF32 n = bits2float(((seed & 0xffff) << 7) | 0x40000000) - 3.0f;
      sF32 bb = sb + sl * f;                    // b' = b + l*nffrq
      sF32 h  = (n - sl) * r - bb;              // h  = (n-l)*nfres - b'
      sF32 ll = sl + h * f;                     // l' = l + h*nffrq
      output(dest + i, gain * (h + bb + ll));   // out = (h + b' + l') * gain
      sl = ll; sb = bb;
    }
    nf.l = sl; nf.b = sb;
    nseed = seed;
  }

  inline void output(sF32 *dest, sF32 x)
  {
    if (ring)
      *dest *= x;
    else
      *dest += x;
  }

  // Oscillator state machine (read description of renderTriSaw for context)
  //
  // We keep track of whether the current sample is in the up or down phase,
  // whether the previous sample was, and if the waveform counter wrapped
  // around on the transition. This allows us to figure out which of
  // the cases above we fall into. Note this code uses a different bit ordering
  // from the ASM version that is hopefully a bit easier to understand.
  //
  // For reference: our bits map to the ASM version as follows (MSB->LSB order)
  //   (o)ld_up
  //   (c)arry
  //   (n)ew_up

  enum OSMTransitionCode    // carry:old_up:new_up
  {
    OSMTC_DOWN = 0,         // old=down, new=down, no carry
    // 1 is an invalid configuration
    OSMTC_UP_DOWN = 2,      // old=up, new=down, no carry
    OSMTC_UP = 3,           // old=up, new=up, no carry
    OSMTC_DOWN_UP_DOWN = 4, // old=down, new=down, carry
    OSMTC_DOWN_UP = 5,      // old=down, new=up, carry
    // 6 is an invalid configuration
    OSMTC_UP_DOWN_UP = 7    // old=up, new=up, carry
  };

  inline sU32 osm_init() // our state field: old_up:new_up
  {
    return (cnt - freq) < brpt ? 3 : 0;
  }

  inline sU32 osm_tick(sU32 &state) // returns transition code
  {
    // old_up = new_up, new_up = (cnt < brpt)
    state = ((state << 1) | (cnt < brpt)) & 3;

    // we added freq to cnt going from the previous sample to the current one.
    // so if cnt is less than freq, we carried.
    sU32 transition_code = state | (cnt < (sU32)freq ? 4 : 0); 

    // finally, tick the oscillator
    cnt += freq;

    return transition_code;
  }

  void renderTriSaw(sF32 *dest, sInt nsamples)
  {
    // Okay, so here's the general idea: instead of the classical sawtooth
    // or triangle waves, V2 uses a generalized triangle wave that looks like
    // this:
    //
    //       /\                  /\
    //      /   \               /   \
    //     /      \            /      \
    // ---/---------\---------/---------\> t
    //   /            \      /
    //  /               \   /
    // /                  \/
    // [-----1 period-----]
    // [-----] "break point" (brpt)
    //
    // If brpt=1/2 (ignoring fixed-point scaling), you get a regular triangle
    // wave. The example shows brpt=1/3, which gives an asymmetrical triangle
    // wave. At the extremes, brpt=0 gives a pure saw-down wave, and brpt=1
    // (if that was a possible value, which it isn't) gives a pure saw-up wave.
    //
    // Purely point-sampling this (or any other) waveform would cause notable
    // aliasing. The standard ways to avoid this issue are to either:
    // 1) Over-sample by a certain amount and then use a low-pass filter to
    //    (hopefully) get rid of the frequencies that would alias, or
    // 2) Generate waveforms from a Fourier series that's cut off below the
    //    Nyquist frequency, ensuring there's no aliasing to begin with.
    // V2 does neither. Instead it computes the convolution of the continuous
    // waveform with an analytical low-pass filter. The ideal low-pass in
    // terms of frequency response would be a sinc filter, which unfortunately
    // has infinite support. Instead, V2 just uses a simple box filter. This
    // doesn't exactly have favorable frequency-domain characteristics, but
    // it's still much better than point sampling and has the advantage that
    // it's fairly simple analytically. It boils down to computing the average
    // value of the waveform over the interval [t,t+h], where t is the current
    // time and h = 1/SR (SR=sampling rate), which is in turn:
    //
    //    f_box(t) = 1/h * (integrate(x=t..t+h) f(x) dx)
    //
    // Now there's a bunch of cases for these intervals [t,t+h] that we need to
    // consider. Bringing up the diagram again, and adding some intervals at the
    // bottom:
    //
    //       /\                  /\
    //      /   \               /   \
    //     /      \            /      \
    // ---/---------\---------/---------\> t
    //   /            \      /
    //  /               \   /
    // /                  \/
    // [-a-]      [-c]
    //     [--b--]       [-d--]
    //   [-----------e-----------]
    //          [-----------f-----------]
    //
    // a) is purely in the saw-up region,
    // b) starts in the saw-up region and ends in saw-down,
    // c) is purely in the saw-down region,
    // d) starts during saw-down and ends in saw-up.
    // e) starts during saw-up and ends in saw-up, but passes through saw-down
    // f) starts saw-down, ends saw-down, passes through saw-up.
    //
    // For simplicity here, I draw different-sized intervals sampling a fixed-
    // frequency wave, even though in practice it's the other way round, but
    // this way it's easier to put it all into a single picture.
    //
    // The original assembly code goes through a few gyrations to encode all
    // these possible cases into a bitmask and then does a single switch.
    // In practice, for all but very high-frequency waves, we're hitting the
    // "easy" cases a) and c) almost all the time.
    COVER("Osc tri/saw");

    // calc helper values.
    // PORTING NOTE: the "hard" cases below (b/d/e/f) have a catastrophic
    // cancellation amplified by rcpf=1/f. The asm computes this on the x87 at
    // 24-bit mantissa / 15-bit exponent. `trisaw_flt` is `float` in the faithful
    // build (x87 PC=24 reproduces the asm exactly) and `double` in the portable
    // build (SSE single's 8-bit exponent would overflow at low frequencies).
    trisaw_flt f = utof23(freq);
    trisaw_flt omf = 1.0 - f;
    trisaw_flt rcpf = 1.0 / f;
    trisaw_flt col = utof23(brpt);

    // m1 = 2/col = slope of saw-up wave
    // m2 = -2/(1-col) = slope of saw-down wave
    // c1 = gain/2*m1 = gain/col = scaled integration constant
    // c2 = gain/2*m2 = -gain/(1-col) = scaled integration constant
    trisaw_flt c1 = gain / col;
    trisaw_flt c2 = -gain / (1.0 - col);

    sU32 state = osm_init();

    for (sInt i=0; i < nsamples; i++)
    {
      trisaw_flt p = utof23(cnt) - col;
      trisaw_flt y = 0.0;

      // state machine action
      switch (osm_tick(state))
      {
      case OSMTC_UP: // case a)
        // average of linear function = just sample in the middle
        y = c1 * (p + p - f);
        break;

      case OSMTC_DOWN: // case c)
        // again, average of a linear function
        y = c2 * (p + p - f);
        break;
        
      case OSMTC_UP_DOWN: // case b)
        y = rcpf * (c2 * (p*p) - c1 * ((p-f)*(p-f))); // trisaw_flt, not float sqr()
        break;

      case OSMTC_DOWN_UP: // case d)
        // asm m0c21: y = -((c2*(p+1-f)^2 - c1*p^2) + g) * (1/f). The asm forms
        // the square base as (p+1)-f INLINE (fld1/fadd/fsub), not p+(1-f); at
        // PC=24 those round differently. Also the 3-term sum is (c2.. - c1..)+g,
        // not g + c2.. - c1.. . Match the asm op order exactly (1-ULP fidelity).
        {
          trisaw_flt t = p + (trisaw_flt)1.0 - f; // (p+1) - f, in the asm association
          y = -((c2*(t*t) - c1*(p*p)) + gain) * rcpf;
        }
        break;

      case OSMTC_UP_DOWN_UP: // case e)
        // asm m0c121: y = -((c1*(omf*(2p+omf))) + g) * (1/f). The asm multiplies
        // omf*(2p+omf) FIRST, then by c1 (c1*(omf*X)), not (c1*omf)*X.
        y = -((c1*(omf*(p + p + omf))) + gain) * rcpf;
        break;

      case OSMTC_DOWN_UP_DOWN: // case f)
        // asm m0c212: same as case e with c2: y = -((c2*(omf*(2p+omf))) + g)*(1/f).
        y = -((c2*(omf*(p + p + omf))) + gain) * rcpf;
        break;

      // INVALID CASES
      default:
        assert(false);
        break;
      }

      output(dest + i, y + gain);
    }
  }

  void renderPulse(sF32 *dest, sInt nsamples)
  {
    // This follows the same general pattern as renderTriSaw above, except
    // this time the waveform is a pulse wave with variable pulse width,
    // which means we get very simple integrals. The state machine works
    // the exact same way, see above for description.
    COVER("Osc pulse");

    // calc helper values
    sF32 f = utof23(freq);
    sF32 gdf = gain / f;
    sF32 col = utof23(brpt);

    sF32 cc121 = gdf * 2.0f * (col - 1.0f) + gain;
    sF32 cc212 = gdf * 2.0f * col - gain;

    sU32 state = osm_init();

    for (sInt i=0; i < nsamples; i++)
    {
      sF32 p = utof23(cnt);
      sF32 out = 0.0f;

      switch (osm_tick(state))
      {
      case OSMTC_UP:
        out = gain;
        break;

      case OSMTC_DOWN:
        out = -gain;
        break;

      case OSMTC_UP_DOWN:
        out = gdf * 2.0f * (col - p) + gain;
        break;

      case OSMTC_DOWN_UP:
        out = gdf * 2.0f * p - gain;
        break;

      case OSMTC_UP_DOWN_UP:
        out = cc121;
        break;

      case OSMTC_DOWN_UP_DOWN:
        out = cc212;
        break;

      // INVALID CASES
      default:
        assert(false);
        break;
      }

      output(dest + i, out);
    }
  }

  void renderSin(sF32 *dest, sInt nsamples)
  {
    COVER("Osc sin");

    // Sine is already a perfectly bandlimited waveform, so we needn't
    // worry about aliasing here.
    for (sInt i=0; i < nsamples; i++)
    {
      // Brace yourselves: The name is a lie! It's actually a cosine wave!
      sU32 phase = cnt + 0x40000000; // quarter-turn (pi/2) phase offset
      cnt += freq; // step the oscillator

      // range reduce to [0,pi]
      if (phase & 0x80000000) // Symmetry: cos(x) = cos(-x)
        phase = ~phase; // V2 uses ~ not - which is slightly off but who cares

      // convert to t in [1,2)
      sF32 t = bits2float((phase >> 8) | 0x3f800000); // 1.0f + (phase / (2^31))

      // and then to t in [-pi/2,pi/2)
      // i know the V2 ASM code says "scale/move to (-pi/4 .. pi/4)".
      // trust me, it's lying.
      t = t * fcpi - fc1p5pi;

      output(dest + i, gain * fastsin(t));
    }
  }

  void renderNoise(sF32 *dest, sInt nsamples)
  {
    COVER("Osc noise");

    V2LRC flt = nf;
    sU32 seed = nseed;

    for (sInt i=0; i < nsamples; i++)
    {
      // uniform random value (noise)
      sF32 n = frandom(&seed);

      // filter
      sF32 h = flt.step(n, nffrq, nfres);
      sF32 x = nfres*(flt.l + h) + flt.b;

      output(dest + i, gain * x);
    }

    // Persist the working filter state + seed back to the members. The asm
    // (syOsc .mode3, synth.asm:964-968) stores nfb/nfl/nseed every block so the
    // resonant noise filter rings continuously. The original port wrote `flt =
    // nf` here (backwards) — nf never updated, so the LRC restarted from l=b=0
    // each 1024-sample block, decorrelating the filtered noise from the asm.
    nf = flt;
    nseed = seed;
  }

  void renderFMSin(sF32 *dest, sInt nsamples)
  {
    COVER("Osc FM");

    // V2's take on FM is a bit unconventional but fairly slick and flexible.
    // The carrier wave is always a sine, but the modulator is whatever happens
    // to be in the voice buffer at that point - which is the output of the
    // previous oscillators. So you can use all the oscillator waveforms
    // (including noise, other FMs and the aux bus oscillators!) as modulator
    // if you are so inclined.
    //
    // And it's very little code too :)
    for (sInt i=0; i < nsamples; i++)
    {
      sF32 mod = dest[i] * fcfmmax;
#ifdef V2_X87_FAITHFUL
      // The asm (syOsc .mode4, synth.asm:981-986) builds the phase float as
      // (cnt>>9)|0x3f800000 -- a value in [1,2) -- and does NOT subtract 1.0 the
      // way utof23() does. For a real sine the extra 1.0 (= 2pi after the *fc2pi
      // scale) would be irrelevant, but V2's fastsinrc range reduction is
      // deliberately broken (see its @@@BUG), so fastsinrc(t) != fastsinrc(t+2pi):
      // with a negative modulator the offset moves t across the pi/2 / 3pi/2
      // reflection boundaries and the minimax poly is evaluated out of range.
      // Match the asm's [1,2) phase (and its `mod + phase` operand order). This
      // path is FM-sine only (ch7) -- never exercised before 17.8s.
      sF32 t = (mod + bits2float((cnt >> 9) | 0x3f800000)) * fc2pi;
#else
      sF32 t = (utof23(cnt) + mod) * fc2pi;
#endif
      cnt += freq;

      sF32 out = gain * fastsinrc(t);
      if (ring)
        dest[i] *= out;
      else
        dest[i] = out;
    }
  }

  void renderAux(sF32 *dest, const StereoSample *src, sInt nsamples)
  {
    COVER("Osc aux");

    // PORTING FIX (not an ASM bug): the asm (.auxa/.auxb, synth.asm) computes
    // ((l+r) * gain) * fcgain per sample; pre-folding gain*fcgain rounds
    // differently (1 ULP on a fraction of samples). The aux oscs read the
    // cross-channel feedback busses, so the noise only appears in full-mix
    // context — invisible to leaf tests and channel-solo bisection
    // (debris_ost residual).
    for (sInt i=0; i < nsamples; i++)
    {
      sF32 aux = (src[i].l + src[i].r) * gain * fcgain;
      if (ring)
        aux *= dest[i];
      dest[i] = aux;
    }
  }
};

// --------------------------------------------------------------------------
// Envelope Generator
// --------------------------------------------------------------------------

struct syVEnv
{
  sF32 ar;  // attack rate
  sF32 dr;  // decay rate
  sF32 sl;  // sustain level
  sF32 sr;  // sustain rate
  sF32 rr;  // release rate
  sF32 vol; // volume
};

struct V2Env
{
  // Slightly different state ordering here than in V2 code, to make
  // things simpler.
  enum State
  {
    OFF,
    RELEASE,

    ATTACK,
    DECAY,
    SUSTAIN,
  };

  sF32 out;
  State state;
  sF32 val;   // output value (0.0-128.0)
  sF32 atd;   // attack delta (added every frame in stAttack, transition ->stDecay at 128.0)
  sF32 dcf;   // decay factor (mul'd every frame in stDecay, transition ->stSustain at sul)
  sF32 sul;   // sustain level (defines stDecay->stSustain transition point)
  sF32 suf;   // sustain factor (mul'd every frame in stSustain, transition ->stRelease at gate off or ->stOff at 0.0)
  sF32 ref;   // release factor (mul'd every frame in stRelease, transition ->stOff at 0.0)
  sF32 gain;

  V2Instance *inst;

  void init(V2Instance *instance)
  {
    state = OFF;
    inst = instance;
  }

  void set(const syVEnv *para)
  {
    if (inst->eraEnvOld())
    {
      // era <v2 (fr08): attack = 2^(7 - ar*11/128), and decay/release go
      // through calcfreq (x10 range) -- calcfreq2 (x11) didn't exist yet.
      // This is exactly the delta kb's disabled transEnv (v2mconv.cpp:243)
      // tries to approximate on the data side; gating the code is bit-exact.
      // Evidence: 2000 syEnvSet @0x40a6d1 (fr08-extraction/DELTA.md).
      atd = v2_exp2(para->ar * fcattackmul_v0 + fcattackadd);
      dcf = 1.0f - calcfreq(1.0f - para->dr / 128.0f);
      ref = 1.0f - calcfreq(1.0f - para->rr / 128.0f);
    }
    else
    {
      // ar: 2^7 (128) to 2^-4 (0.03, ca. 10 secs at 344frames/sec)
      atd = v2_exp2(para->ar * fcattackmul + fcattackadd);

      // dcf: 0 (5msecs thanks to volramping) up to almost 1
      dcf = 1.0f - calcfreq2(1.0f - para->dr / 128.0f);

      // ref: 0 (5ms thanks to volramping) up to almost 1
      ref = 1.0f - calcfreq2(1.0f - para->rr / 128.0f);
    }

    // sul: 0..127 is fine already
    sul = para->sl;

    // suf: 1/128 (15ms till it's gone) up to 128 (15ms till it's fully there)
    suf = v2_exp2(fcsusmul * (para->sr - 64.0f));

    gain = para->vol / 128.0f;
#ifdef V2_VALIDATE
    if (getenv("ENVTRACE")) {
      union { sF32 f; sU32 u; } A,D,S,U,R,G;
      A.f=atd; D.f=dcf; S.f=sul; U.f=suf; R.f=ref; G.f=gain;
      fprintf(stderr, "[env.set] atd=%08x dcf=%08x sul=%08x suf=%08x ref=%08x gain=%08x\n",
              A.u, D.u, S.u, U.u, R.u, G.u);
    }
#endif
  }

  void tick(bool gate)
  {
    // process immediate gate transition
    if (state <= RELEASE && gate) // gate on
      state = ATTACK;
    else if (state >= ATTACK && !gate) // gate off
      state = RELEASE;

    // process current state
    switch (state)
    {
    case OFF:
      COVER("EG off");
      val = 0.0f;
      break;

    case ATTACK:
      COVER("EG atk");
      val += atd;
      if (val >= 128.0f)
      {
        val = 128.0f;
        state = DECAY;
      }
      break;

    case DECAY:
      COVER("EG dcy");
      val *= dcf;
      if (val <= sul)
      {
        val = sul;
        state = SUSTAIN;
      }
      break;

    case SUSTAIN:
      COVER("EG sus");
      val *= suf;
      if (val > 128.0f)
        val = 128.0f;
      break;

    case RELEASE:
      COVER("EG rel");
      val *= ref;
      break;
    }

    // avoid underflow to denormals
    if (val <= fclowest)
    {
      val = 0.0f;
      state = OFF;
    }

    out = val * gain;
    DEBUG_PLOT_VAL(this, out / 128.0f);
  }
};

// --------------------------------------------------------------------------
// Filter
// --------------------------------------------------------------------------

struct syVFlt
{
  sF32 mode;
  sF32 cutoff;
  sF32 reso;
};

struct V2Flt
{
  enum Mode
  {
    BYPASS,
    LOW,
    BAND,
    HIGH,
    NOTCH,
    ALL,
    MOOGL,
    MOOGH
  };

  sInt mode;
  sF32 cfreq;
  sF32 res;
  sF32 moogf, moogp, moogq; // moog filter coeffs
  V2LRC lrc;
  V2Moog moog;

  V2Instance *inst;

  void init(V2Instance *instance)
  {
    lrc.init();
    moog.init();
    inst = instance;
  }

  void set(const syVFlt *para)
  {
    mode = (sInt)para->mode;
    sF32 f = calcfreq(para->cutoff / 128.0f) * inst->SRfclinfreq;
    sF32 r = para->reso / 128.0f;

    if (mode < MOOGL)
    {
      COVER("VCF set regular");
      res = 1.0f - r;
      cfreq = f;
    }
    else
    {
      COVER("VCF set moog");

      // @@@BUG? V2 code for this part looks suspicious.
      // Match syFltSet's moog operand order EXACTLY: the moog is a 4-pole
      // recursive filter, so a 1-ULP coeff error drifts/amplifies over the song.
      // 0.8/5.6 as static const sF32 -> loaded 32-bit like the asm fc0p8/fc5p6
      // (a bare literal in this live x87 expression promotes to 80-bit, != asm).
      static const sF32 fc0p8 = 0.8f, fc5p6 = 5.6f;
      f *= 0.25f;
      sF32 t = 1.0f - f;

      moogp = f + fc0p8 * (f * t);              // asm: f + 0.8*(t*f)
      moogf = 1.0f - (moogp + moogp);           // asm: 1 - (p+p), not (1-p)-p
      sF32 qx = fc5p6 * (t * t) + (1.0f - t);   // asm: 5.6*t^2 + (1-t)
      sF32 qb = r * (1.0f + 0.5f * (t * qx));   // asm: r' * (1 + 0.5*(t*qx))
      sF32 q2 = qb + qb;                        // asm: *2
      moogq = q2 + q2;                          // asm: *2 again (= *4)
    }
  }

  void render(sF32 *dest, const sF32 *src, sInt nsamples, sInt step=1)
  {
    V2LRC flt;
    V2Moog m;
    // era <v1 (fr08): the 2000 SVF render injects no denormal DC bias.
    const flcalc dco = inst->eraV0() ? (flcalc)0 : (flcalc)fcdcoffset;

#ifdef V2_VALIDATE
    sF32 fltlog_in = src[0]; // first input sample (before in-place overwrite)
#endif

    switch (mode & 7)
    {
    case BYPASS:
      COVER("VCF bypass");
      // @@@BUG ignores step? this is wrong but I suppose this
      // never gets hit for stereo case.
      if (dest != src)
        memmove(dest, src, nsamples * sizeof(sF32));
      break;

    case LOW:
      COVER("VCF low");
      { flcalc l = lrc.l, b = lrc.b;
        for (sInt i=0; i < nsamples; i++)
        {
          lrc_step_2x(l, b, src[i*step], cfreq, res, dco);
          dest[i*step] = (sF32)l;
        }
        lrc.l = (sF32)l; lrc.b = (sF32)b; }
      break;

    case BAND:
      COVER("VCF band");
      { flcalc l = lrc.l, b = lrc.b;
        for (sInt i=0; i < nsamples; i++)
        {
          lrc_step_2x(l, b, src[i*step], cfreq, res, dco);
          dest[i*step] = (sF32)b;
        }
        lrc.l = (sF32)l; lrc.b = (sF32)b; }
      break;

    case HIGH:
      COVER("VCF high");
      { flcalc l = lrc.l, b = lrc.b;
        for (sInt i=0; i < nsamples; i++)
        {
          flcalc h = lrc_step_2x(l, b, src[i*step], cfreq, res, dco);
          dest[i*step] = (sF32)h;
        }
        lrc.l = (sF32)l; lrc.b = (sF32)b; }
      break;

    case NOTCH:
      COVER("VCF notch");
      { flcalc l = lrc.l, b = lrc.b;
        for (sInt i=0; i < nsamples; i++)
        {
          flcalc h = lrc_step_2x(l, b, src[i*step], cfreq, res, dco);
          dest[i*step] = (sF32)(l + h);
        }
        lrc.l = (sF32)l; lrc.b = (sF32)b; }
      break;

    case ALL:
      COVER("VCF all");
      // PORTING FIX (not an ASM bug): the asm (.mode5, synth.asm) sums the
      // allpass output as (h + b) + l; (l + b) + h rounds differently for ~a
      // ULP on some samples. The allpass output never feeds the l/b state, so
      // the filter's own ledger state stays exact — the 1-ULP output noise
      // only surfaces in DOWNSTREAM stateful blocks (kkrieger6: voice ALL vcf
      // -> channel dist's embedded allpass accumulated it).
      { flcalc l = lrc.l, b = lrc.b;
        for (sInt i=0; i < nsamples; i++)
        {
          flcalc h = lrc_step_2x(l, b, src[i*step], cfreq, res, dco);
          dest[i*step] = (sF32)(h + b + l);
        }
        lrc.l = (sF32)l; lrc.b = (sF32)b; }
      break;

    case MOOGL:
      COVER("VCF moog low");
      // Moog filters are 2x oversampled, so run filter twice.
      m = moog;
      for (sInt i=0; i < nsamples; i++)
      {
        sF32 in = src[i*step];
        m.step(in, moogf, moogp, moogq);
        dest[i*step] = m.step(in, moogf, moogp, moogq);
      }
      moog = m;
      break;

    case MOOGH:
      COVER("VCF moog high");
      m = moog;
      for (sInt i=0; i < nsamples; i++)
      {
        sF32 in = src[i*step];
        m.step(in, moogf, moogp, moogq);
        dest[i*step] = in - m.step(in, moogf, moogp, moogq);
      }
      moog = m;
      break;
    }

#ifdef V2_VALIDATE
    // Post-block filter STATE (regular LRC modes), to localize the filter-internal
    // divergence: freq=lrc.l, pno=lrc.b, preround=cfreq, r0=res.
    if ((mode & 7) <= ALL && g_fltlog_count < FREQLOG_CAP) {  // bypass+LRC (asm regular path)
      FreqLogRec &rec = g_fltlog_buf[g_fltlog_count++];
      rec.kind = 3;
      rec.offset = (unsigned)((char *)this - g_freqlog_synthbase);
      rec.freq = f2u(lrc.l); rec.pno = f2u(lrc.b);
      rec.preround = f2u(cfreq); rec.r0 = f2u(res);
      rec.r1 = f2u(fltlog_in);          // first input sample
      rec.r2 = (unsigned)(mode & 7);    // filter mode (0=byp,1=low,2=band,3=high,4=notch,5=all)
    }
#endif

    DEBUG_PLOT_STRIDED(this, dest, step, nsamples);
  }
};

// --------------------------------------------------------------------------
// Low Frequency Oscillator
// --------------------------------------------------------------------------

struct syVLFO
{
  sF32 mode;    // 0=saw, 1=tri, 2=pulse, 3=sin, 4=s&h
  sF32 sync;    // 0=free, 1=in sync with keyon
  sF32 egmode;  // 0=continuous 1=one-shot (EG mode)
  sF32 rate;    // rate (0Hz..~43Hz)
  sF32 phase;   // start phase shift
  sF32 pol;     // polarity: +, -, +/-
  sF32 amp;     // amplification (0..1)
};

struct V2LFO
{
  enum Mode
  {
    SAW,
    TRI,
    PULSE,
    SIN,
    S_H
  };

  sF32 out;
  sInt mode;    // mode
  bool sync;    // sync mode
  bool eg;      // envelope generator mode
  sInt freq;    // frequency
  sU32 cntr;    // counter
  sU32 cphase;  // counter sync phase
  sF32 gain;    // output gain
  sF32 dc;      // output dc
  sU32 nseed;   // random seed
  sU32 last;    // last counter value (for s&h transition)

  void init(V2Instance *instance)
  {
    cntr = last = 0;
    // era <v1 (fr08): C1 pins rdtsc=0, so the S&H seed must be 0 for the
    // matched-seed A/B (DELTA.md D6). (Re-seeded by the era setter for the
    // synth-init-time voices, as srcVersion isn't set yet at first init.)
    if (instance->eraV0()) { nseed = 0u; return; }
#ifdef V2_VALIDATE
    // The asm (syLFOInit, synth.asm:1595) seeds the S&H generator with RDTSC —
    // the shipping synth is deliberately nondeterministic per run. For the
    // bit-exact asm-vs-cpp A/B the validation build pins the same constant in
    // BOTH cores (build.sh rewrites the asm's rdtsc to this value).
    nseed = 0x2BAD5EED;
#else
    nseed = rand(); // not really, but close enough...
#endif
  }

  void set(const syVLFO *para)
  {
    mode = (sInt)para->mode;
    sync = (sInt)para->sync != 0;
    eg = (sInt)para->egmode != 0;
#ifdef V2_VALIDATE
    if (getenv("LFODUMP")) { static int n=0; if(n++<12)
      fprintf(stderr,"[LFODUMP] mode=%d sync=%d eg=%d rate=%.1f pol=%.0f amp=%.1f phase=%.1f\n",
              mode,(int)sync,(int)eg,para->rate,para->pol,para->amp,para->phase); }
#endif
#ifdef V2_X87_FAITHFUL
    // asm syLFOSet (synth.asm): freq stored with fistp (round-to-nearest), NOT a
    // truncating cast. fci128=0.0078125 (=1/128, exact), order matches the asm
    // (calcfreq * fc32bit * 0.5). The truncating (sInt) below is off-by-one
    // whenever the frac >= 0.5, which slowly desyncs the integer phase counter
    // and (via LFO->amp-env modulation) drifts curvol over the whole song.
    freq = v2_fistp(calcfreq(para->rate * 0.0078125f) * fc32bit * 0.5f);
#else
    freq = (sInt)(0.5f * fc32bit * calcfreq(para->rate / 128.0f));
#endif
#ifdef V2_VALIDATE
    freqlog_emit(1 /*lfo*/, this, (unsigned)freq, f2u(para->rate), 0, 0, 0, 0);
    freq = freqlog_knockout(1 /*lfo*/, this, freq, 0);
#endif
    cphase = ftou32(para->phase / 128.0f);

    switch ((sInt)para->pol)
    {
    case 0: // +
      COVER("LFO pol +");
      gain = para->amp;
      dc = 0.0f;
      break;

    case 1: // -
      COVER("LFO pol -");
      gain = -para->amp;
      dc = 0.0f;
      break;

    case 2: // +/-
      COVER("LFO pol +/-");
      gain = para->amp;
      dc = -0.5f * para->amp;
      break;
    }
  }

  void keyOn()
  {
    if (sync)
    {
      COVER("LFO sync");
      cntr = cphase;
      last = ~0u;
    }
  }

  void tick()
  {
    sF32 v;
    sU32 x;

    switch (mode & 7)
    {
    case SAW:
    default:
      COVER("LFO saw");
      v = utof23(cntr);
      break;

    case TRI:
      COVER("LFO tri");
      x = (cntr << 1) ^ (sS32(cntr) >> 31);
      v = utof23(x);
      break;

    case PULSE:
      COVER("LFO pulse");
      x = sS32(cntr) >> 31;
      v = utof23(x);
      break;

    case SIN:
      COVER("LFO sin");
      v = utof23(cntr);
      v = fastsinrc(v * fc2pi) * 0.5f + 0.5f;
      break;

    case S_H:
      COVER("LFO sample+hold");
      if (cntr < last)
        nseed = urandom(&nseed);
      last = cntr;
      v = utof23(nseed);
      break;
    }

    out = v * gain + dc;
    cntr += freq;
    if (cntr < (sU32)freq && eg) // in one-shot mode, clamp at wrap-around
      cntr = ~0u;

    DEBUG_PLOT_VAL(this, out / 128.0f);
  }
};

// --------------------------------------------------------------------------
// Distortion
// --------------------------------------------------------------------------

struct syVDist
{
  sF32 mode;    // see below
  sF32 ingain;  // -12dB .. 36dB
  sF32 param1;  // outgain / crush / outfreq / cutoff
  sF32 param2;  // offset / xor / jitter / reso
};

struct V2Dist
{
  enum Mode
  {
    OFF = 0,
    OVERDRIVE,
    CLIP,
    BITCRUSHER,
    DECIMATOR,

    FLT_BASE  = DECIMATOR,
    FLT_LOW   = FLT_BASE + V2Flt::LOW,
    FLT_BAND  = FLT_BASE + V2Flt::BAND,
    FLT_HIGH  = FLT_BASE + V2Flt::HIGH,
    FLT_NOTCH = FLT_BASE + V2Flt::NOTCH,
    FLT_ALL   = FLT_BASE + V2Flt::ALL,
    FLT_MOOGL = FLT_BASE + V2Flt::MOOGL,
    FLT_MOOGH = FLT_BASE + V2Flt::MOOGH,
  };

  sInt mode;
  sF32 gain1;     // input gain for all fx
  sF32 gain2;     // output gain for od/clip
  sF32 offs;      // offs for od/clip
  sF32 crush1;    // 1/crush_factor
  sInt crush2;    // crush_factor
  sInt crxor;     // xor value for crush
  sU32 dcount;    // decimator counter
  sU32 dfreq;     // decimator frequency
  sF32 dvall;     // last decimator value (mono/left)
  sF32 dvalr;     // last decimator value (mono/right)
  V2Flt fltl;     // filter mono/left
  V2Flt fltr;     // filter right
  V2Instance *inst;

  void init(V2Instance *instance)
  {
    dcount = 0;
    dvall = dvalr = 0.0f;
    fltl.init(instance);
    fltr.init(instance);
    inst = instance;
  }

  void set(const syVDist *para)
  {
    sF32 x;

    mode = (sInt)para->mode;
    gain1 = v2_exp2((para->ingain - 32.0f) / 16.0f);
#ifdef V2_VALIDATE
    if (getenv("DISTTRACE")) { static int n=0; if (n++<40)
      fprintf(stderr, "[dist.set] mode=%d ingain=%.1f p1=%.1f p2=%.1f\n",
              mode, para->ingain, para->param1, para->param2); }
#endif

    switch (mode)
    {
    case OFF:
      break;

    case OVERDRIVE:
#ifdef V2_X87_FAITHFUL
      // /128.0f is a power of two (exact), same value as the asm's fmul fci128.
      gain2 = v2_overdrive_gain2(para->param1 / 128.0f, gain1);
#else
      gain2 = (para->param1 / 128.0f) / atan(gain1);
#endif
      offs = gain1 * 2.0f * ((para->param2 / 128.0f) - 0.5f);
      break;

    case CLIP:
      gain2 = para->param1 / 128.0f;
      offs = gain1 * 2.0f * ((para->param2 / 128.0f) - 0.5f);
      break;

    case BITCRUSHER:
      x = para->param1 * 256.0f + 1.0f;
      crush2 = (sInt)x;
      crush1 = gain1 * (32768.0f / x);
      crxor = ((sInt)para->param2) << 9;
      break;

    case DECIMATOR:
      dfreq = ftou32(calcfreq(para->param1 / 128.0f));
      break;

    default: // filters
      {
        syVFlt setup;
        setup.cutoff = para->param1;
        setup.reso = para->param2;
        setup.mode = (sF32)(mode - FLT_BASE);
        fltl.set(&setup);
        fltr.set(&setup);
      }
      break;
    }
#ifdef V2_VALIDATE
    // Bit-dump of the OVERDRIVE/CLIP setup values (the asm stores these in the
    // shared .mode2b tail, synth.asm:1848). Mirrored on the asm side by
    // distg2dbg_snap (asm_appendix.asm) so the sequences diff call-for-call.
    // gain2 is the fpatan-vs-libm-atan suspect for the vce_dist/ch_dist seeds.
    if ((mode == OVERDRIVE || mode == CLIP) && getenv("DISTG2TRACE")) {
      union { sF32 f; sU32 u; } g1, g2, of;
      g1.f = gain1; g2.f = gain2; of.f = offs;
      fprintf(stderr, "[dist.g2] mode=%d gain1=%08x gain2=%08x offs=%08x\n",
              mode, g1.u, g2.u, of.u);
    }
#endif
  }

  void renderMono(sF32 *dest, const sF32 *src, sInt nsamples)
  {
    switch (mode)
    {
    case OFF:
      if (dest != src)
        memmove(dest, src, nsamples * sizeof(sF32));
      break;

    case OVERDRIVE:
      COVER("DIST overdrive");
      for (sInt i=0; i < nsamples; i++)
        dest[i] = overdrive(src[i]);
      break;

    case CLIP:
      COVER("DIST clip");
      for (sInt i=0; i < nsamples; i++)
        dest[i] = clip(src[i]);
      break;

    case BITCRUSHER:
      COVER("DIST bitcrusher");
      for (sInt i=0; i < nsamples; i++)
        dest[i] = bitcrusher(src[i]);
      break;

    case DECIMATOR:
      COVER("DIST mono decimator");
      for (sInt i=0; i < nsamples; i++)
      {
        decimator_tick(src[i], 0.0f);
        dest[i] = dvall;
      }
      break;

    default: // filters
      COVER("DIST mono filter");
      fltl.render(dest, src, nsamples);
      break;
    }

    DEBUG_PLOT(this, dest, nsamples);
  }

  void renderStereo(StereoSample *dest, const StereoSample *src, sInt nsamples)
  {
    // @@@BUG this matches the original V2 code, but frankly I have my doubts
    // that always running the Moog filters in Mono mode is intentional... 
    switch (mode)
    {
    case DECIMATOR:
      COVER("DIST stereo decimator");
      for (sInt i=0; i < nsamples; i++)
      {
        decimator_tick(src[i].l, src[i].r);
        dest[i].l = dvall;
        dest[i].r = dvalr;
      }
      break;

    case FLT_LOW:
    case FLT_BAND:
    case FLT_HIGH:
    case FLT_NOTCH:
    case FLT_ALL:
      COVER("DIST stereo filter");
      fltl.render(&dest[0].l, &src[0].l, nsamples, 2);
      fltr.render(&dest[0].r, &src[0].r, nsamples, 2);
      break;

    default:
      // everything else we presume to be stateless and just pass through the
      // mono version.
      renderMono(&dest[0].l, &src[0].l, nsamples*2);
    }

    DEBUG_PLOT_STEREO(this, dest, nsamples);
  }

private:
  inline sF32 overdrive(sF32 in)
  {
    // era <v1 (fr08): native x87 atan (fpatan), not the fastatan polynomial
    // (syDistRenderMono @0x40ab88; DELTA.md delta -- fastatan is post-2000).
    if (inst->eraV0())
      return gain2 * v2_atanf(in * gain1 + offs);
    return gain2 * fastatan(in * gain1 + offs);
  }

  inline sF32 clip(sF32 in)
  {
    return gain2 * clamp(in * gain1 + offs, -1.0f, 1.0f);
  }

  inline sF32 bitcrusher(sF32 in)
  {
    sInt t = (sInt)lrintf(in * crush1); // ASM uses fistp (round-to-nearest), not truncation
    t = clamp(t * crush2, -0x7fff, 0x7fff) ^ crxor;
    return (sF32)t / 32768.0f;
  }

  inline void decimator_tick(sF32 l, sF32 r)
  {
    dcount += dfreq;
    if (dcount < dfreq) // carry
    {
      dvall = l;
      dvalr = r;
    }
  }
};

// --------------------------------------------------------------------------
// DC filter
// --------------------------------------------------------------------------

// This is just a high-pass with very low cut-off to remove DC offsets from
// the signal.
struct V2DCFilter
{
  V2DCF fl;         // left/mono filter state
  V2DCF fr;         // right filter state
  V2Instance *inst;

  void init(V2Instance *instance)
  {
    inst = instance;
    fl.init();
    fr.init();
  }

  void renderMono(sF32 *dest, const sF32 *src, sInt nsamples)
  {
    sF32 R = inst->SRfcdcfilter;

    V2DCF l = fl;
    for (sInt i=0; i < nsamples; i++)
      dest[i] = l.step(src[i], R);
    fl = l;
  }

  void renderStereo(StereoSample *dest, const StereoSample *src, sInt nsamples)
  {
    sF32 R = inst->SRfcdcfilter;

    V2DCF l = fl;
    V2DCF r = fr;
    for (sInt i=0; i < nsamples; i++)
    {
      dest[i].l = l.step(src[i].l, R);
      dest[i].r = r.step(src[i].r, R);
    }
    fl = l;
    fr = r;
  }
};

// --------------------------------------------------------------------------
// V2 Voice
// --------------------------------------------------------------------------

struct syVV2
{
  // Voice parameters.
  // changing any of these *will* invalidate all V2M files!
  static const sInt NOSC = 3; // number of oscillators
  static const sInt NFLT = 2; // number of filters (NB: changing this would complicate routing too)
  static const sInt NENV = 2; // number of envelope generators
  static const sInt NLFO = 2; // number of LFOs

  sF32 panning;
  sF32 transp;   // transpose
  syVOsc osc[NOSC];
  syVFlt flt[NFLT];
  sF32 routing; // 0: single 1: serial 2: parallel
  sF32 fltbal;  // parallel filter balance
  syVDist dist;
  syVEnv env[NENV];
  syVLFO lfo[NLFO];
  sF32 oscsync; // 0: none 1: osc 2: full
};

#ifdef V2_VALIDATE
// Voice-chain sub-stage snapshots (validation localization, level 2). V2Voice::
// render copies the mono vcebuf after each sub-stage so the A/B harness can find
// which voice block first diverges. vcebuf is per-voice; the per-frame snapshot
// holds the last voice rendered that frame -- valid in the early single-voice
// region where the first divergence lives. Mirrored in asm_appendix.asm
// (vcetap_* / vcetap_snap_*). Exposed via synthDebugGetVceTap.
static sF32 g_vcetap_osc [V2Instance::MAX_FRAME_SIZE]; // after oscillators
static sF32 g_vcetap_flt [V2Instance::MAX_FRAME_SIZE]; // after filters
static sF32 g_vcetap_dist[V2Instance::MAX_FRAME_SIZE]; // after distortion
static sF32 g_vcetap_dcf [V2Instance::MAX_FRAME_SIZE]; // after voice dc filter
// Accumulate across ALL voices (reset per frame in renderFrame), so the tap is
// unmasked -- the per-voice last-writer snapshot hid divergence in non-last voices.
#define VCETAP_SNAP(stage, buf, n) \
    do { for (sInt _i=0; _i<(n); _i++) g_vcetap_##stage[_i] += (buf)[_i]; } while(0)
#else
#define VCETAP_SNAP(stage, buf, n) ((void)0)
#endif

struct V2Voice
{
  enum FilterRouting
  {
    FLTR_SINGLE = 0,
    FLTR_SERIAL,
    FLTR_PARALLEL
  };

  enum KeySync
  {
    SYNC_NONE = 0,
    SYNC_OSC,
    SYNC_FULL
  };

  sInt note;
  sF32 velo;
  bool gate;

  sF32 curvol;
  sF32 volramp;

  sF32 xpose;     // transpose
  sInt fmode;     // FLTR_*
  sF32 lvol;      // left volume
  sF32 rvol;      // right volume
  sF32 f1gain;    // filter 1 gain
  sF32 f2gain;    // filter 2 gain

  sInt keysync;

  V2Osc osc[syVV2::NOSC];
  V2Flt vcf[syVV2::NFLT];
  V2Env env[syVV2::NENV];
  V2LFO lfo[syVV2::NLFO];
  V2Dist dist;    // distorter
  V2DCFilter dcf; // post DC filter

  V2Instance *inst;

  void init(V2Instance *instance)
  {
    for (sInt i=0; i < syVV2::NOSC; i++)
      osc[i].init(instance, i);
    for (sInt i=0; i < syVV2::NFLT; i++)
      vcf[i].init(instance);
    for (sInt i=0; i < syVV2::NENV; i++)
      env[i].init(instance);
    for (sInt i=0; i < syVV2::NLFO; i++)
      lfo[i].init(instance);
    dist.init(instance);
    dcf.init(instance);
    inst = instance;
  }

  void tick()
  {
    for (sInt i=0; i < syVV2::NENV; i++)
      env[i].tick(gate);

    for (sInt i=0; i < syVV2::NLFO; i++)
      lfo[i].tick();

#ifdef V2_VALIDATE
    ctrllog_emit(this, env[0].out, env[1].out, lfo[0].out, lfo[1].out,
                 f2u(env[1].suf), (unsigned)env[0].state | ((unsigned)env[1].state << 8));
#endif

    // volume ramping slope
    volramp = (env[0].out / 128.0f - curvol) * inst->SRfciframe;
    DEBUG_PLOT_VAL(&curvol, curvol);
  }

  void render(StereoSample *dest, sInt nsamples)
  {
    assert(nsamples <= V2Instance::MAX_FRAME_SIZE);

    // clear voice buffer
    sF32 *voice = inst->vcebuf;
    sF32 *voice2 = inst->vcebuf2;
    memset(voice, 0, nsamples * sizeof(*voice));

    // oscillators -> voice buffer
    for (sInt i=0; i < syVV2::NOSC; i++)
      osc[i].render(voice, nsamples);
    VCETAP_SNAP(osc, voice, nsamples);
#ifdef V2_VALIDATE
    // Per-voice osc output (before any filter): freq=voice[0], pno=voice[1], preround=voice[2].
    if (g_osclog_count < FREQLOG_CAP) {
      FreqLogRec &r = g_osclog_buf[g_osclog_count++];
      r.kind = 4;
      r.offset = (unsigned)((char *)this - g_freqlog_synthbase);
      r.freq = f2u(voice[0]); r.pno = f2u(nsamples > 1 ? voice[1] : 0.0f);
      r.preround = f2u(nsamples > 2 ? voice[2] : 0.0f);
      r.r0 = f2u(f1gain); r.r1 = f2u(f2gain); // parallel combine gains
      // r2: filter routing in bits 0-3 + per-osc modes (bits 4-15) and ring
      // flags (bits 16-18) — the osc-mode context proved decisive for the
      // debris AUXA hunt; never infer modes from freqlog ordinals (noteOn
      // ticks emit extra freq records and shift per-offset indexing).
      r.r2 = (unsigned)(fmode & 15)
           | ((unsigned)(osc[0].mode & 7) << 4) | ((unsigned)(osc[1].mode & 7) << 8)
           | ((unsigned)(osc[2].mode & 7) << 12)
           | ((osc[0].ring ? 1u : 0u) << 16) | ((osc[1].ring ? 1u : 0u) << 17)
           | ((osc[2].ring ? 1u : 0u) << 18);
    }
#endif

    // voice buffer -> filters -> voice buffer
    switch (fmode)
    {
    case FLTR_SINGLE:
      COVER("VOICE filter single");
      vcf[0].render(voice, voice, nsamples);
      break;

    case FLTR_SERIAL:
    default:
      COVER("VOICE filter serial");
      vcf[0].render(voice, voice, nsamples);
      vcf[1].render(voice, voice, nsamples);
      break;

    case FLTR_PARALLEL:
      COVER("VOICE filter parallel");
      vcf[1].render(voice2, voice, nsamples);
      vcf[0].render(voice, voice, nsamples);
      for (sInt i=0; i < nsamples; i++)
        voice[i] = voice[i]*f1gain + voice2[i]*f2gain;
      break;
    }
    VCETAP_SNAP(flt, voice, nsamples);

    // voice buffer -> distortion -> voice buffer
    dist.renderMono(voice, voice, nsamples);
    VCETAP_SNAP(dist, voice, nsamples);

    // voice buffer -> dc filter -> voice buffer
    // era <v1 (fr08): syV2Render @0x40ad4d has NO per-voice DC filter (osc ->
    // flt -> dist -> volramp); the post-voice dcf is post-2000. (DELTA.md)
    if (!inst->eraV0())
    {
      dcf.renderMono(voice, voice, nsamples);
      VCETAP_SNAP(dcf, voice, nsamples);
    }

    DEBUG_PLOT(this, voice, nsamples);

    // voice buffer (mono) -> +=output buffer (stereo)
    // original ASM code has chan buffer hardwired as output here. era <v1
    // injects no fcdcoffset in the voice->channel mix (the 2000 loop adds none).
    const sF32 dco = inst->eraV0() ? 0.0f : fcdcoffset;
    sF32 cv = curvol;
    for (sInt i=0; i < nsamples; i++)
    {
      sF32 out = voice[i] * cv;
      cv += volramp;

      dest[i].l += lvol * out + dco;
      dest[i].r += rvol * out + dco;
    }

    curvol = cv;
  }

  void set(const syVV2 *para)
  {
    xpose = para->transp - 64.0f;
    updateNote();

    fmode = (sInt)para->routing;
    keysync = (sInt)para->oscsync;

    // equal power panning
    sF32 p = para->panning / 128.0f;
    lvol = sqrtf(1.0f - p);
    rvol = sqrtf(p);

    // filter balance for parallel
    sF32 x = (para->fltbal - 64.0f) / 64.0f;
    // The asm (syV2Set, synth.asm:2468-2481) picks the branch on the sign of
    // `fist(fltbal-64)` -- the ROUND-TO-NEAREST INTEGER -- not the float sign of x.
    // For a modulated fltbal in (63.5, 64) the rounded int is 0, so the asm takes
    // the >=0 branch and computes f1gain = 1-x > 1 (a slight BOOST of filter 1),
    // where the float-sign test wrongly attenuated filter 2 instead. This was the
    // last whole-song divergence source (via the parallel filter combine).
#ifdef V2_X87_FAITHFUL
    sInt xi = v2_fistp(para->fltbal - 64.0f);
#else
    sInt xi = (sInt)lrintf(para->fltbal - 64.0f);
#endif
    if (xi >= 0)
    {
      f2gain = 1.0f;
      f1gain = 1.0f - x;
    }
    else
    {
      f1gain = 1.0f;
      f2gain = 1.0f + x;
    }

    // subsections
    for (sInt i=0; i < syVV2::NOSC; i++)
      osc[i].set(&para->osc[i]);

    for (sInt i=0; i < syVV2::NENV; i++)
      env[i].set(&para->env[i]);

    for (sInt i=0; i < syVV2::NFLT; i++)
      vcf[i].set(&para->flt[i]);

    for (sInt i=0; i < syVV2::NLFO; i++)
      lfo[i].set(&para->lfo[i]);

    dist.set(&para->dist);
  }

  void noteOn(sInt note, sInt vel)
  {
    this->note = note;
    updateNote();

    velo = (sF32)vel;
    gate = true;

    // reset EGs
    for (sInt i=0; i < syVV2::NENV; i++)
      env[i].state = V2Env::ATTACK;

    // process sync
    switch (keysync)
    {
    case SYNC_FULL:
      COVER("VOICE noteOn sync full");
      for (sInt i=0; i < syVV2::NENV; i++)
        env[i].val = 0.0f;
      curvol = 0.0f;

      for (sInt i=0; i < syVV2::NOSC; i++)
        osc[i].init(inst, i);

      for (sInt i=0; i < syVV2::NFLT; i++)
        vcf[i].init(inst);

      dist.init(inst);
      // fall-through

    case SYNC_OSC:
      COVER("VOICE noteOn sync osc");
      for (sInt i=0; i < syVV2::NOSC; i++)
        osc[i].cnt = 0;
      // fall-through

    case SYNC_NONE:
    default:
      break;
    }

    for (sInt i=0; i < syVV2::NOSC; i++)
      osc[i].chgPitch();

    for (sInt i=0; i < syVV2::NLFO; i++)
      lfo[i].keyOn();

    dcf.init(inst);
  }

  void noteOff()
  {
    gate = false;
  }

private:
  void updateNote()
  {
    sF32 n = xpose + (sF32)note;
    for (sInt i=0; i < syVV2::NOSC; i++)
      osc[i].note = n;
  }
};

// --------------------------------------------------------------------------
// Bass boost (fixed low shelving EQ)
// --------------------------------------------------------------------------

struct syVBoost
{
  sF32 amount;    // boost in dB (0..18)
};

struct V2Boost
{
  bool enabled;
  sF32 a1, a2;      // normalized filter coeffs
  sF32 b0, b1, b2;
  sF32 x1[2];       // state variables
  sF32 x2[2];
  sF32 y1[2];
  sF32 y2[2];

  V2Instance *inst;

  void init(V2Instance *instance)
  {
    inst = instance;
  }

  void set(const syVBoost *para)
  {
    enabled = ((sInt)para->amount) != 0;
    if (!enabled)
      return;

    COVER("BOOST set");

    // A = 10^(dBgain/40), or a rough approximation anyway
    sF32 A = v2_exp2(para->amount / 128.0f);

    // V2 code computes beta = sqrt((A^2 + 1) - (A-1)^2), which is algebraically
    // just sqrt(2A) — but NOT bit-equal: the asm (syBoostSet, synth.asm:2790-
    // 2800) rounds A*A, A*A+1, (A-1)^2 and the difference to single precision
    // step by step, and for 21 of the 127 possible amounts the result differs
    // from sqrt(2A) by 1 ULP. Keep the asm's stepwise form; the "redundant"
    // shape is load-bearing for bit-fidelity.
    sF32 beta = sqrtf((A * A + 1.0f) - (A - 1.0f) * (A - 1.0f));

    // temp vars
    sF32 bs = beta * inst->SRfcBoostSin;
    sF32 Am1 = A - 1.0f;
    sF32 Ap1 = A + 1.0f;
    sF32 cAm1 = Am1 * inst->SRfcBoostCos;
    sF32 cAp1 = Ap1 * inst->SRfcBoostCos;

    // a0 = (A+1) + (A-1)*cos + beta*sin, summed in the asm's ORDER:
    // (bs + cAm1) + Ap1 (syBoostSet, synth.asm:2818-2823 — fadd st1, fadd st3).
    // FP addition is not associative: (Ap1 + cAm1) + bs rounds differently for
    // some amounts (e.g. 92 — pzero's 9.677s ch7 patch), shifting ia0 and ALL
    // FIVE coefficients by 1 ULP and shocking the biquad state. The association
    // below is load-bearing; do not "clean it up".
    sF32 ia0 = 1.0f / ((bs + cAm1) + Ap1);

    b1 = 2.0f * A * (Am1 - cAp1) * ia0;
    a1 = -2.0f * (Am1 + cAp1) * ia0;
    a2 = (Ap1 + cAm1 - bs) * ia0;
    // PORT FIX: syBoostSet forms A*ia0 ONCE, then b0/b2 = (Ap1-cAm1 ± bs)*(A*ia0)
    // (synth.asm:2854-2862). The port wrote A*(...)*ia0, i.e. (A*(...))* ia0 --
    // FP multiply isn't associative, so b0/b2 came out 1 ULP off the asm, and the
    // chorus feedback comb + sum compressor amplified that into the whole-song
    // residual. Group to match the asm exactly.
    sF32 Aia0 = A * ia0;
    b0 = (Ap1 - cAm1 + bs) * Aia0;
    b2 = (Ap1 - cAm1 - bs) * Aia0;
#ifdef V2_VALIDATE
    if (getenv("BOOSTTRACE")) {
      union { sF32 f; sU32 u; } B0,B1,B2,A1,A2;
      B0.f=b0; B1.f=b1; B2.f=b2; A1.f=a1; A2.f=a2;
      fprintf(stderr, "[boost.set] ena=%d b0=%08x b1=%08x b2=%08x a1=%08x a2=%08x\n",
              enabled, B0.u, B1.u, B2.u, A1.u, A2.u);
    }
#endif
  }

  void render(StereoSample *buf, sInt nsamples)
  {
    if (!enabled)
      return;

    COVER("BOOST render");

    for (sInt ch=0; ch < 2; ch++)
    {
      sF32 xm1 = x1[ch], xm2 = x2[ch];
      sF32 ym1 = y1[ch], ym2 = y2[ch];

      for (sInt i=0; i < nsamples; i++)
      {
        sF32 x = buf[i].ch[ch] + fcdcoffset;

        // Second-order IIR filter. Match syBoostProcChan's accumulation grouping
        // EXACTLY: b0*x + ((b1*x1 - a1*y1) + (b2*x2 - a2*y2)). The biquad is
        // recursive, so a different operand order rounds each sample differently
        // and the poles integrate it into drift (the sequential form below drifts).
        sF32 y = b0*x + ((b1*xm1 - a1*ym1) + (b2*xm2 - a2*ym2));
        ym2 = ym1; ym1 = y;
        xm2 = xm1; xm1 = x;

        buf[i].ch[ch] = y;
      }

      x1[ch] = xm1; x2[ch] = xm2;
      y1[ch] = ym1; y2[ch] = ym2;
    }
  }
};

// --------------------------------------------------------------------------
// Modulating delay
// --------------------------------------------------------------------------

struct syVModDel
{
  sF32 amount;    // dry/wet value (0=-wet, 64=dry, 127=wet)
  sF32 fb;        // feedback (0=-100%, 64=0%, 127=~100%)
  sF32 llength;   // length of left delay
  sF32 rlength;   // length of right delay
  sF32 mrate;     // modulation rate
  sF32 mdepth;    // modulation depth
  sF32 mphase;    // modulation stereo phase (0=-180deg, 64=0deg, 127=180deg)
};

struct V2ModDel
{
  sF32 *db[2];    // left/right delay buffer
  sU32 dbufmask;  // delay buffer mask (size-1, must be pow2)

  sU32 dbptr;     // buffer write pos
  sU32 dboffs[2]; // buffer read offset
  
  sU32 mcnt;      // mod counter
  sInt mfreq;     // mod freq
  sU32 mphase;    // mod phase
  sU32 mmaxoffs;  // mod max offs (2048samples*depth)

  sF32 fbval;     // feedback val
  sF32 dryout;
  sF32 wetout;

  V2Instance *inst;

  void init(V2Instance *instance, sF32 *buf1, sF32 *buf2, sInt buflen)
  {
    assert(buflen != 0 && (buflen & (buflen - 1)) == 0);
    db[0] = buf1;
    db[1] = buf2;
    dbufmask = buflen - 1;
    inst = instance;

    reset();
  }

  void reset()
  {
    dbptr = 0;
    mcnt = 0;

    memset(db[0], 0, (dbufmask + 1) * sizeof(sF32));
    memset(db[1], 0, (dbufmask + 1) * sizeof(sF32));
  }

  void set(const syVModDel *para)
  {
    wetout = (para->amount - 64.0f) / 64.0f;
    dryout = 1.0f - fabsf(wetout);
    fbval = (para->fb - 64.0f) / 64.0f;

#ifdef V2_X87_FAITHFUL
    // asm syModDelSet: every int conversion is fistp (round-to-nearest), NOT a
    // truncating cast. fci128=0.0078125 (=1/128, exact); operation order matches
    // the asm. The truncating (sInt)/ftou32 below are off-by-one when the frac
    // >= 0.5; mfreq drives the integer mod-counter (mcnt += mfreq) so its error
    // slowly drifts the chorus, diverging the channel output over the song.
    sF32 lenscale = ((sF32)dbufmask - 1023.0f) * 0.0078125f;
    dboffs[0] = v2_fistp(para->llength * lenscale);
    dboffs[1] = v2_fistp(para->rlength * lenscale);

    mfreq = v2_fistp(calcfreq(para->mrate * 0.0078125f) * fcmdlfomul * inst->SRfclinfreq);
    mmaxoffs = v2_fistp(para->mdepth * 0.0078125f * 1023.0f);
    mphase = 2u * (sU32)v2_fistp((para->mphase - 64.0f) * 0.0078125f * fc32bit);
#ifdef V2_VALIDATE
    if (getenv("CHORUSTRACE"))
      fprintf(stderr, "[chorus.set] wetout=%.6f mfreq=%d dboffs=%d,%d mmaxoffs=%d mphase=%u "
              "(mrate=%.1f mdepth=%.1f amount=%.1f)\n",
              wetout, mfreq, dboffs[0], dboffs[1], mmaxoffs, mphase,
              para->mrate, para->mdepth, para->amount);
#endif
#else
    sF32 lenscale = ((sF32)dbufmask - 1023.0f) / 128.0f;
    dboffs[0] = (sInt)(para->llength * lenscale);
    dboffs[1] = (sInt)(para->rlength * lenscale);

    mfreq = (sInt)(inst->SRfclinfreq * fcmdlfomul * calcfreq(para->mrate / 128.0f));
    mmaxoffs = (sInt)(para->mdepth * 1023.0f / 128.0f);
    mphase = ftou32((para->mphase - 64.0f) / 128.0f);
#endif
  }

  void renderAux2Main(StereoSample *dest, sInt nsamples)
  {
    if (!wetout)
      return;

    COVER("MODDEL aux->main");

    for (sInt i=0; i < nsamples; i++)
    {
      StereoSample x;

      sF32 in = inst->aux2buf[i] + fcdcoffset;
      processSample(&x, in, in, 0.0f);

      dest[i].l += x.l;
      dest[i].r += x.r;
    }
  }

  void renderChan(StereoSample *chanbuf, sInt nsamples)
  {
    if (!wetout)
      return;

    COVER("MODDEL chan");

    // era <v1 (fr08): the 2000 chorus render @0x40b268 feeds the channel buffer
    // straight in (fld [esi]/[esi+4]) with NO denormal bias; 2004 adds
    // fcdcoffset. Through the feedback comb this accumulates, so it must be
    // gated for the matched A/B. (DELTA.md delta 6, channel level.)
    const sF32 dco = inst->eraV0() ? 0.0f : fcdcoffset;
    sF32 dry = dryout;
    for (sInt i=0; i < nsamples; i++)
      processSample(&chanbuf[i], chanbuf[i].l + dco, chanbuf[i].r + dco, dry);
  }

private:
  inline sF32 processChanSample(sF32 in, sInt ch, sF32 dry)
  {
    // modulation is a triangle wave
    sU32 counter = mcnt + (ch ? mphase : 0);
    counter = (counter < 0x80000000u) ? counter*2 : 0xffffffffu - counter*2;
    
    // determine effective offset
    sU64 offs32_32 = (sU64)counter * mmaxoffs; // 32.32 fixed point
    sU32 offs_int = sU32(offs32_32 >> 32) + dboffs[ch];
    // PORT FIX: asm syModDelProcessSample reads in1=db[idx], in2=db[idx+1] and
    // interpolates in1 + (1-frac)*(in2-in1) (asm x = fc2-(1+frac) = 1-frac). The
    // RIGHT channel (ch=1, the "rechtes dingens") has an extra `dec ebx`
    // (synth.asm:3067) so idx = dbptr-offs-1; the LEFT channel does not. The port
    // read {index, index-1} for BOTH channels -- correct for the right, but the
    // LEFT channel then interpolated the wrong neighbor pair (a one-sample tap
    // error the feedback comb amplified into the dominant chain divergence).
    sU32 idx = dbptr - offs_int - (ch ? 1u : 0u);

    // linear interpolation using low-order bits of offs32_32.
    sF32 *delaybuf = db[ch];
    sF32 x = utof23((sU32)(offs32_32 & 0xffffffffu));
    sF32 in1 = delaybuf[idx & dbufmask];
    sF32 in2 = delaybuf[(idx + 1) & dbufmask];
    sF32 delayed = in1 + (1.0f - x) * (in2 - in1);

    // mix and output
    delaybuf[dbptr] = in + delayed*fbval;
    return in*dry + delayed*wetout;
  }

  inline void processSample(StereoSample *out, sF32 l, sF32 r, sF32 dry)
  {
    out->l = processChanSample(l, 0, dry);
    out->r = processChanSample(r, 1, dry);

    // tick
    mcnt += mfreq;
    dbptr = (dbptr + 1) & dbufmask;
  }
};

// --------------------------------------------------------------------------
// Stereo Compressor
// --------------------------------------------------------------------------

struct syVComp
{
  sF32 mode;      // 0=off, 1=Peak, 2=RMS
  sF32 stereo;    // 0=mono, 1=stereo
  sF32 autogain;  // 0=off, 1=on
  sF32 lookahead; // lookahead in ms
  sF32 threshold; // threshold (-54dB .. 6dB)
  sF32 ratio;     // (0=1:1 .. 127=1:inf)
  sF32 attack;    // attack value
  sF32 release;   // release value
  sF32 outgain;   // output gain
};

struct V2Comp
{
  static const int COMPDLEN = 5700;
  static const int RMSLEN = 8192; // must be a power of 2

  enum Mode
  {
    MODE_OFF = 0,
    MODE_PEAK,
    MODE_RMS,
  };

  enum ModeBits
  {
    MODE_BIT_PEAK   = 0,
    MODE_BIT_RMS    = 1,
    MODE_BIT_MONO   = 0,
    MODE_BIT_STEREO = 2,
    MODE_BIT_ON     = 0,
    MODE_BIT_OFF    = 4,
  };

  sInt mode;      // bit 0: Peak/RMS, bit 1: Stereo, bit 2: off
  sInt oldmode;   // last mode set() saw (asm syWComp.oldmode; drives the reset)

  sF32 invol;     // input gain (1/threshold, internal threshold is always 0dB)
  sF32 ratio;
  sF32 outvol;    // output gain (outgain * threshold)
  sF32 attack;    // attack (lpf coeff, 0..1)
  sF32 release;   // release (lpf coeff, 0..1)

  sU32 dblen;     // lookahead buffer length
  sU32 dbcnt;     // lookahead buffer offset

  sF32 curgain[2]; // current gain
  sF32 peakval[2]; // peak value
  sF32 rmsval[2];  // rms current value
  sU32 rmscnt;     // rms counter

  StereoSample dbuf[COMPDLEN]; // lookahead delay buffer
  StereoSample rmsbuf[RMSLEN]; // RMS ring buffer

  V2Instance *inst;

  void init(V2Instance *instance)
  {
    mode = MODE_BIT_STEREO;
    memset(dbuf, 0, sizeof(dbuf));
    inst = instance;

#if BUG_V2_COMP_OLDMODE
    // ASM-faithful: syCompInit only sets mode=2; oldmode/curgain/peak/rms stay
    // at the zeroed-instance values. curgain=1 only ever arrives via the
    // mode-change reset in set() — which a comp whose first set() computes
    // mode==0 (peak|mono|on) never takes, so it starts at curgain=0.
    oldmode = 0;
    for (sInt i=0; i < 2; i++)
    {
      peakval[i] = 0.0f;
      rmsval[i] = 0.0f;
      curgain[i] = 0.0f;
    }
    memset(rmsbuf, 0, sizeof(rmsbuf));
    rmscnt = 0;
#else
    oldmode = mode;
    reset();
#endif
  }

  void reset()
  {
    for (sInt i=0; i < 2; i++)
    {
      peakval[i] = 0.0f;
      rmsval[i] = 0.0f;
      curgain[i] = 1.0f;
    }
    memset(rmsbuf, 0, sizeof(rmsbuf));
    rmscnt = 0;
  }

  void set(const syVComp *para)
  {
    switch ((sInt)para->mode)
    {
    case MODE_OFF:  mode = MODE_BIT_OFF; break;
    case MODE_PEAK: mode = MODE_BIT_PEAK | MODE_BIT_ON; break;
    case MODE_RMS:  mode = MODE_BIT_RMS | MODE_BIT_ON; break;
    default:        assert(false);
    }

    if (para->stereo != 0.0f)
      mode |= MODE_BIT_STEREO;

    // asm syCompSet compares against a PERSISTENT oldmode field (not last
    // frame's mode): the reset fires only on a transition set() itself sees.
    // (Encoding note: asm encodes OFF as 5/7 vs our 4/6 — both injective and
    // both map peak|mono|on to 0, so transition decisions are identical.)
    if (mode != oldmode)
    {
      oldmode = mode;
      reset();
    }

    // @@@BUG: original V2 code uses "fcsamplesperms" here which is
    // hard-coded to 44.1kHz
    // asm syCompSet stores dblen via fistp (round-to-nearest); the port truncated.
#ifdef V2_X87_FAITHFUL
    dblen = (sU32)v2_fistp(para->lookahead * inst->SRfcsamplesperms);
#else
    dblen = (sInt)(para->lookahead * inst->SRfcsamplesperms);
#endif

    sF32 thresh = 8.0f * calcfreq(para->threshold / 128.0f);
    invol = 1.0f / thresh;
    if (para->autogain != 0.0f)
      thresh = 1.0f;
    outvol = thresh * v2_exp2((para->outgain - 64.0f) / 16.0f);
    ratio = para->ratio / 128.0f;
    
    // attack: 0 (!) ... 200ms (5Hz)
    attack = v2_exp2(-para->attack * 12.0f / 128.0f);
    // release: 5ms .. 5s
    release = v2_exp2(-para->release * 16.0f / 128.0f);
#ifdef V2_VALIDATE
    if (getenv("COMPTRACE")) {
      union { sF32 f; sU32 u; } iv, ov, rt, at, rl;
      iv.f=invol; ov.f=outvol; rt.f=ratio; at.f=attack; rl.f=release;
      fprintf(stderr, "[comp.set] mode=%d dblen=%u invol=%08x outvol=%08x ratio=%08x attack=%08x release=%08x\n",
              mode, dblen, iv.u, ov.u, rt.u, at.u, rl.u);
    }
#endif
  }

  void render(StereoSample *buf, sInt nsamples)
  {
    if (mode & MODE_BIT_OFF)
      return;

    COVER("COMP render");

    // Step 1: level detect (fills LD buffers)
    StereoSample *levels = inst->levelbuf;
    switch (mode & (MODE_BIT_RMS | MODE_BIT_STEREO))
    {
    case MODE_BIT_PEAK | MODE_BIT_MONO:
      COVER("COMP level peak mono");
      for (sInt i=0; i < nsamples; i++)
        levels[i].l = levels[i].r = invol * doPeak(0.5f * (buf[i].l + buf[i].r), 0);
      break;

    case MODE_BIT_RMS | MODE_BIT_MONO:
      COVER("COMP level rms mono");
      for (sInt i=0; i < nsamples; i++)
      {
        // mono: dcoffset on the input (asm syCompLDMonoRMS), not the accumulator
        levels[i].l = levels[i].r = invol * doRMS(0.5f * (buf[i].l + buf[i].r) + fcdcoffset, 0, 0.0f);
        rmscnt = (rmscnt + 1) & (RMSLEN - 1);
      }
      break;

    case MODE_BIT_PEAK | MODE_BIT_STEREO:
      COVER("COMP level peak stereo");
      for (sInt i=0; i < nsamples; i++)
      {
        levels[i].l = invol * doPeak(buf[i].l, 0);
        levels[i].r = invol * doPeak(buf[i].r, 1);
      }
      break;

    case MODE_BIT_RMS | MODE_BIT_STEREO:
      COVER("COMP level rms stereo");
      for (sInt i=0; i < nsamples; i++)
      {
        levels[i].l = invol * doRMS(buf[i].l, 0, fcdcoffset);
        levels[i].r = invol * doRMS(buf[i].r, 1, fcdcoffset);
        rmscnt = (rmscnt + 1) & (RMSLEN - 1);
      }
      break;
    }

    // Step 2: compress!
    for (sInt ch=0; ch < 2; ch++)
    {
      sF32 gain = curgain[ch];
      sU32 dbind = dbcnt;

      for (sInt i=0; i < nsamples; i++)
      {
        // lookahead delay line
        sF32 v = outvol * dbuf[dbind].ch[ch];
        dbuf[dbind].ch[ch] = invol * buf[i].ch[ch];
        // PORT FIX: ASM (syCompProcChannel) wraps with "inc/cmp dblen/jbe" i.e.
        // resets only when the index EXCEEDS dblen -> ring length dblen+1. The
        // port used ">= dblen" (ring length dblen), making the lookahead one
        // sample short. Match the ASM.
        if (++dbind > dblen)
          dbind = 0;

        // determine dest gain
        sF32 dgain = 1.0f;
        sF32 lvl = levels[i].ch[ch];
        if (lvl >= 1.0f)
          dgain = 1.0f / (1.0f + ratio * (lvl - 1.0f));

        // and compress
        gain += (dgain < gain ? attack : release) * (dgain - gain);
        buf[i].ch[ch] = v * gain;
      }

      curgain[ch] = gain;
      if (ch == 1)
        dbcnt = dbind;
    }
  }
  
private:
  // level detection variants
  inline sF32 doPeak(sF32 in, sInt ch)
  {
    peakval[ch] = max(peakval[ch] * fccpdfalloff + fcdcoffset, fabsf(in));
    return peakval[ch];
  }

  // PORTING FIX (not an ASM bug) — op-for-op port of syCompLD{Mono,Stereo}RMS:
  // - the oldest sample is subtracted from the accumulator FIRST (its own
  //   rounding), not fused into "+= insq - oldest";
  // - the FIXDENORMALS dcoffset goes on the ACCUMULATOR in stereo mode but on
  //   the INPUT in mono mode (asm inconsistency, replicated via the callers);
  // - the output is sqrt(rv) * fci8192 (the asm's 24-bit 1/sqrt(8192)
  //   constant), NOT sqrt(rv/8192) — a different rounding sequence (1 ULP on
  //   most samples; was the whole-song residual driver via the sum compressor).
  inline sF32 doRMS(sF32 in, sInt ch, sF32 accumdc)
  {
    rmsval[ch] -= rmsbuf[rmscnt].ch[ch]; // remove oldest
    rmsval[ch] += accumdc;               // denormal fix (stereo: on accumulator)
    sF32 insq = sqr(in);
    rmsval[ch] += insq;                  // add new sample
    rmsbuf[rmscnt].ch[ch] = insq;        // keep track of value we added
    return sqrtf(rmsval[ch]) * fcrms8192;
  }
};

// --------------------------------------------------------------------------
// Stereo reverb
// --------------------------------------------------------------------------

struct syVReverb
{
  sF32 revtime;
  sF32 highcut;
  sF32 lowcut;
  sF32 vol;
};

struct V2Reverb
{
  sF32 gainc[4];  // feedback gain for comb filter delays 0-3
  sF32 gaina[2];  // feedback gain for allpas delays 0-1
  sF32 gainin;    // input gain
  sF32 damp;      // high cut (1-val^2)
  sF32 lowcut;    // low cut (val^2)

  V2Delay combd[2][4];  // left/right comb filter delay lines
  sF32 combl[2][4];     // left/right comb delay filter buffers
  V2Delay alld[2][2];   // left/right allpass filters
  sF32 hpf[2];          // memory for low cut filters

  V2Instance *inst;

  // delay line buffers
  sF32 bcombl0[1309];
  sF32 bcombl1[1635];
  sF32 bcombl2[1811];
  sF32 bcombl3[1926];
  sF32 balll0[220];
  sF32 balll1[74];
  sF32 bcombr0[1327];
  sF32 bcombr1[1631];
  sF32 bcombr2[1833];
  sF32 bcombr3[1901];
  sF32 ballr0[205];
  sF32 ballr1[77];

  void init(V2Instance *instance)
  {
    // init filters
    combd[0][0].init(bcombl0);
    combd[0][1].init(bcombl1);
    combd[0][2].init(bcombl2);
    combd[0][3].init(bcombl3);
    alld[0][0].init(balll0);
    alld[0][1].init(balll1);
    combd[1][0].init(bcombr0);
    combd[1][1].init(bcombr1);
    combd[1][2].init(bcombr2);
    combd[1][3].init(bcombr3);
    alld[1][0].init(ballr0);
    alld[1][1].init(ballr1);

    inst = instance;
    reset();
  }

  void reset()
  {
    for (sInt ch=0; ch < 2; ch++)
    {
      // comb
      for (sInt i=0; i < 4; i++)
      {
        combd[ch][i].reset();
        combl[ch][i] = 0.0f;
      }

      // allpass
      for (sInt i=0; i < 2; i++)
        alld[ch][i].reset();

      // low cut
      hpf[ch] = 0.0f;
    }
  }

  void set(const syVReverb *para)
  {
    static const sF32 gaincdef[4] = {
      0.966384599f, 0.958186359f, 0.953783929f, 0.950933178f
    };
    static const sF32 gainadef[2] = {
      0.994260075f, 0.998044717f
    };

    sF32 e = inst->SRfclinfreq * sqr(64.0f / (para->revtime + 1.0f));
    for (sInt i=0; i < 4; i++)
      gainc[i] = v2_powf(gaincdef[i], e);

    for (sInt i=0; i < 2; i++)
      gaina[i] = v2_powf(gainadef[i], e);

    damp = inst->SRfclinfreq * (para->highcut / 128.0f);
    gainin = para->vol / 128.0f;
    lowcut = inst->SRfclinfreq * sqr(sqr(para->lowcut / 128.0f));
#ifdef V2_VALIDATE
    if (getenv("REVERBTRACE")) {
      union { sF32 f; sU32 u; } gc[4], ga[2], dm, gi, lc;
      for (sInt i=0;i<4;i++) gc[i].f=gainc[i];
      for (sInt i=0;i<2;i++) ga[i].f=gaina[i];
      dm.f=damp; gi.f=gainin; lc.f=lowcut;
      fprintf(stderr, "[reverb.set] gainc=%08x,%08x,%08x,%08x gaina=%08x,%08x "
              "damp=%08x gainin=%08x lowcut=%08x\n",
              gc[0].u,gc[1].u,gc[2].u,gc[3].u, ga[0].u,ga[1].u, dm.u, gi.u, lc.u);
    }
#endif
  }

  void render(StereoSample *dest, sInt nsamples)
  {
    const sF32 *inbuf = inst->aux1buf;

    COVER("RVB render");

    for (sInt i=0; i < nsamples; i++)
    {
      sF32 in = inbuf[i] * gainin + fcdcoffset;

      for (sInt ch=0; ch < 2; ch++)
      {
        // parallel comb filters
        sF32 cur = 0.0f;
        for (sInt j=0; j < 4; j++)
        {
          sF32 dv = gainc[j] * combd[ch][j].fetch();
          sF32 nv = (j & 1) ? (dv - in) : (dv + in); // alternate phase on combs
          sF32 lp = combl[ch][j] + damp * (nv - combl[ch][j]);
          // PORT FIX: persist the comb lowpass state. The asm stores the filtered
          // result to BOTH the lowpass memory and the delay line (synth.asm:3883
          // `fst lpfcl0` / `fst linecl0`); the port fed only the delay line and
          // never wrote combl back, so it stayed 0 and the one-pole lowpass
          // degenerated into a fixed `damp*nv` scale -- losing each comb's HF
          // damping, which diverged through the comb feedback (reverb-only; this
          // block is excluded from the leaf oracle, so it went uncaught).
          combl[ch][j] = lp;
          combd[ch][j].feed(lp);
          cur += lp;
        }

        // serial allpass filters
        for (sInt j=0; j < 2; j++)
        {
          sF32 dv = alld[ch][j].fetch();
          sF32 dz = cur + gaina[j] * dv;
          alld[ch][j].feed(dz);
          cur = dv - gaina[j] * dz;
        }

        // low cut and output
        hpf[ch] += lowcut * (cur - hpf[ch]);
        dest[i].ch[ch] += cur - hpf[ch];
      }
    }
  }
};

// --------------------------------------------------------------------------
// Channel
// --------------------------------------------------------------------------

struct syVChan
{
  sF32 chanvol;
  sF32 auxarcv; // aux a receive
  sF32 auxbrcv; // aux b receive
  sF32 auxasnd; // aux a send
  sF32 auxbsnd; // aux b send
  sF32 aux1;
  sF32 aux2;
  sF32 fxroute;
  syVBoost boost;
  syVDist dist;
  syVModDel chorus;
  syVComp comp;
};

#ifdef V2_VALIDATE
// Per-channel-chain SUB-STAGE taps (validation localization, one level below
// g_chantap). renderFrame resets them; V2Chan::process ACCUMULATES chanbuf into
// the matching buffer after each chain block (dcf1->comp->boost->dist/chorus/
// dcf2), summed across all channels like g_chantap. Lets the A/B harness pin
// WHICH channel-FX block first turns a bit-exact input into a divergent output.
// Stereo, one frame each. Mirrored in v2/validate/asm_appendix.asm (chtap_* +
// chtap_snap_*). Exposed via synthDebugGetChainTap. Read-only; no DSP effect.
static StereoSample g_chtap_dcf1  [V2Instance::MAX_FRAME_SIZE];
static StereoSample g_chtap_comp  [V2Instance::MAX_FRAME_SIZE];
static StereoSample g_chtap_boost [V2Instance::MAX_FRAME_SIZE];
static StereoSample g_chtap_dist  [V2Instance::MAX_FRAME_SIZE];
static StereoSample g_chtap_chorus[V2Instance::MAX_FRAME_SIZE];
static StereoSample g_chtap_dcf2  [V2Instance::MAX_FRAME_SIZE];
static inline void chtap_acc(StereoSample *dst, const StereoSample *src, sInt n)
{
  for (sInt i=0; i < n; i++) { dst[i].l += src[i].l; dst[i].r += src[i].r; }
}
#define CHTAP_SNAP(stage, chan, n) chtap_acc(g_chtap_##stage, (chan), (n))
#else
#define CHTAP_SNAP(stage, chan, n) ((void)0)
#endif

struct V2Chan
{
  enum FXRouting
  {
    FXR_DIST_THEN_CHORUS = 0,
    FXR_CHORUS_THEN_DIST,
  };

  sF32 chgain;    // channel gain
  sF32 a1gain;    // aux1 gain
  sF32 a2gain;    // aux2 gain
  sF32 aasnd;     // aux a send gain
  sF32 absnd;     // aux b send gain
  sF32 aarcv;     // aux a receive gain
  sF32 abrcv;     // aux b receive gain
  sInt fxr;
  V2DCFilter dcf1;
  V2Boost boost;
  V2Dist dist;
  V2DCFilter dcf2;
  V2ModDel chorus;
  V2Comp comp;

  V2Instance *inst;

  void init(V2Instance *instance, sF32 *delbuf1, sF32 *delbuf2, sInt buflen)
  {
    inst = instance;
    dcf1.init(inst);
    boost.init(inst);
    dist.init(inst);
    dcf2.init(inst);
    chorus.init(inst, delbuf1, delbuf2, buflen);
    comp.init(inst);
  }

  void set(const syVChan *para)
  {
    aarcv = para->auxarcv / 128.0f;
    abrcv = para->auxbrcv / 128.0f;
    aasnd = fcgain * (para->auxasnd / 128.0f);
    absnd = fcgain * (para->auxbsnd / 128.0f);
    chgain = fcgain * (para->chanvol / 128.0f);
    // PORTING FIX (not an ASM bug): the ASM (syChanSet, synth.asm:4198-4209)
    // computes ((aux/128) * fcgainh) * chgain — aux first, chgain LAST.
    // Multiplication is non-associative: grouping chgain*fcgainh first came out
    // 1 ULP off for some volumes, seeding the aux sends (reverb input).
    a1gain = (para->aux1 / 128.0f) * fcgainh * chgain;
    a2gain = (para->aux2 / 128.0f) * fcgainh * chgain;
    fxr = (sInt)para->fxroute;
#ifdef V2_VALIDATE
    if (getenv("CHANTRACE"))
      fprintf(stderr, "[chan.set] aarcv=%.5f abrcv=%.5f aasnd=%.5f absnd=%.5f a1gain=%.5f a2gain=%.5f fxr=%d\n",
              aarcv, abrcv, aasnd, absnd, a1gain, a2gain, fxr);
#endif
    dist.set(&para->dist);
    chorus.set(&para->chorus);
    comp.set(&para->comp);
    boost.set(&para->boost);
  }

  void process(sInt nsamples)
  {
    StereoSample *chan = inst->chanbuf;

    // AuxA/B receive (stereo)
    accumulate(chan, inst->auxabuf, nsamples, aarcv);
    accumulate(chan, inst->auxbbuf, nsamples, abrcv);

    // Filters
    dcf1.renderStereo(chan, chan, nsamples);
    CHTAP_SNAP(dcf1, chan, nsamples);
    DEBUG_PLOT_STEREO(&dcf1, chan, nsamples);
    comp.render(chan, nsamples);
    CHTAP_SNAP(comp, chan, nsamples);
    boost.render(chan, nsamples);
    CHTAP_SNAP(boost, chan, nsamples);
    if (fxr == FXR_DIST_THEN_CHORUS)
    {
      dist.renderStereo(chan, chan, nsamples);
      CHTAP_SNAP(dist, chan, nsamples);
      dcf2.renderStereo(chan, chan, nsamples);
      CHTAP_SNAP(dcf2, chan, nsamples);
      chorus.renderChan(chan, nsamples);
      CHTAP_SNAP(chorus, chan, nsamples);
    }
    else // FXR_CHORUS_THEN_DIST
    {
      chorus.renderChan(chan, nsamples);
      CHTAP_SNAP(chorus, chan, nsamples);
      dist.renderStereo(chan, chan, nsamples);
      CHTAP_SNAP(dist, chan, nsamples);
      dcf2.renderStereo(chan, chan, nsamples);
      CHTAP_SNAP(dcf2, chan, nsamples);
    }

    // Aux1/2 send (mono)
    accumulateMonoMix(inst->aux1buf, chan, nsamples, a1gain);
    accumulateMonoMix(inst->aux2buf, chan, nsamples, a2gain);

    // AuxA/B send (stereo)
    accumulate(inst->auxabuf, chan, nsamples, aasnd);
    accumulate(inst->auxbbuf, chan, nsamples, absnd);

    // Channel buffer to mix buffer (stereo)
    accumulate(inst->mixbuf, chan, nsamples, chgain);

    DEBUG_PLOT_STEREO(this, chan, nsamples);
  }

private:
  void accumulate(StereoSample *dest, const StereoSample *src, sInt nsamples, sF32 gain)
  {
    if (gain == 0.0f)
      return;

    for (sInt i=0; i < nsamples; i++)
    {
      dest[i].l += gain * src[i].l;
      dest[i].r += gain * src[i].r;
    }
  }

  void accumulateMonoMix(sF32 *dest, const StereoSample *src, sInt nsamples, sF32 gain)
  {
    if (gain == 0.0f)
      return;

    for (sInt i=0; i < nsamples; i++)
      dest[i] += gain * (src[i].l + src[i].r);
  }
};

// --------------------------------------------------------------------------
// Sound definitions
// --------------------------------------------------------------------------

struct V2Mod
{
  sU8 source;   // source: vel/ctl1-7/aenv/env2/lfo1/lfo2
  sU8 val;      // 0=-1 .. 128=1
  sU8 dest;     // destination (index into V2Sound)
};

struct V2Sound
{
  sU8 voice[sizeof(syVV2) / sizeof(sF32)];
  sU8 chan[sizeof(syVChan) / sizeof(sF32)];
  sU8 maxpoly;
  sU8 modnum;
  V2Mod modmatrix[1]; // actually modnum entries!
};

union V2PatchMap
{
  sU32 offsets[128];    // offsets into raw_data[]
  sU8 raw_data[1];      // variable size
};

// --------------------------------------------------------------------------
// Ronan
// --------------------------------------------------------------------------

struct syWRonan
{
  sU8 mem[64*1024]; // "that should be enough" --synth.asm. :)
};

#ifdef RONAN

extern "C"
{
  void __stdcall ronanCBInit(syWRonan *pthis);
  void __stdcall ronanCBTick(syWRonan *pthis);
  void __stdcall ronanCBNoteOn(syWRonan *pthis);
  void __stdcall ronanCBNoteOff(syWRonan *pthis);
  void __stdcall ronanCBSetCtl(syWRonan *pthis, sU32 ctl, sU32 val);
  void __stdcall ronanCBProcess(syWRonan *pthis, sF32 *buf, sU32 len);
  void __stdcall ronanCBSetSR(syWRonan *pthis, sInt samplerate);
}

#else

static inline void ronanCBInit(syWRonan *) {}
static inline void ronanCBTick(syWRonan *) {}
static inline void ronanCBNoteOn(syWRonan *) {}
static inline void ronanCBNoteOff(syWRonan *) {}
static inline void ronanCBSetCtl(syWRonan *, sU32, sU32) {}
static inline void ronanCBProcess(syWRonan *, sF32 *, sU32) {}
static inline void ronanCBSetSR(syWRonan *, sInt) {}

extern "C" void __stdcall synthSetLyrics(void *, const char **) {}

#endif

// --------------------------------------------------------------------------
// Synth
// --------------------------------------------------------------------------

struct V2ChanInfo
{
  sU8 pgm;    // program
  sU8 ctl[7]; // controllers
};

// V2Synth holds a V2Instance.
// In the original code these are one and the same struct (SYN) but that
// would turn out fairly awkward in this C++ version, hence the split.
#ifdef V2_VALIDATE
// Per-stage mix-chain snapshots (validation-only localization tap). renderFrame
// copies `mixbuf` into one of these after each global FX stage so the A/B harness
// can bisect which stage introduces the divergence. Read-only; no DSP effect.
// Mirrored in v2/validate/asm_appendix.asm (mixtap_* + mixtap_snap_*) for the asm
// core. Exposed via synthDebugGetMixTap below.
static StereoSample g_mixtap_premix[V2Instance::MAX_FRAME_SIZE]; // dry mix, before reverb
static StereoSample g_mixtap_reverb[V2Instance::MAX_FRAME_SIZE]; // after reverb
static StereoSample g_mixtap_delay [V2Instance::MAX_FRAME_SIZE]; // after mod-delay
static StereoSample g_mixtap_dcf   [V2Instance::MAX_FRAME_SIZE]; // after dc filter
static StereoSample g_mixtap_lchc  [V2Instance::MAX_FRAME_SIZE]; // after low-cut/high-cut
static StereoSample g_mixtap_compr [V2Instance::MAX_FRAME_SIZE]; // after sum compressor
// chanbuf snapshot: the post-curvol voice SUM for a channel, taken after the
// voice loop (before the channel chain). Bridges the gap between the per-voice
// SIGNAL taps (g_vcetap_*, pre amp-envelope) and the global mix. Per-channel;
// the per-frame snapshot holds the last channel -- valid in the early
// single-channel region where the first divergence lives.
static StereoSample g_chantap      [V2Instance::MAX_FRAME_SIZE];
#define MIXTAP_SNAP(stage, mix, n) \
    memcpy(g_mixtap_##stage, (mix), (n) * sizeof(StereoSample))
#else
#define MIXTAP_SNAP(stage, mix, n) ((void)0)
#endif

struct V2Synth
{
  static const sInt POLY = 64;
  static const sInt CHANS = 16;

  const V2PatchMap *patchmap;
  sU32 mrstat;          // running status in MIDI decoding
  sU32 curalloc;
  sInt samplerate;
  sInt chanmap[POLY];   // voice -> chan
  sU32 allocpos[POLY];
  sInt voicemap[CHANS]; // chan -> choice
  sInt tickd;           // number of finished samples left in mix buffer

  V2ChanInfo chans[CHANS];
  syVV2 voicesv[POLY];
  V2Voice voicesw[POLY];
  syVChan chansv[CHANS];
  V2Chan chansw[CHANS];

  struct Globals
  {
    syVReverb rvbparm;
    syVModDel delparm;
    sF32 vlowcut;
    sF32 vhighcut;
    syVComp cprparm;
    sU8 guicolor;
    sU8 _pad[3];
  } globals;

  V2Reverb reverb;
  V2ModDel delay;
  V2DCFilter dcf;
  V2Comp compr;
  sF32 lcfreq;    // low cut freq
  sF32 lcbuf[2];  // low cut buf l/r
  sF32 hcfreq;    // high cut freq
  sF32 hcbuf[2];  // high cut buf l/r

  bool initialized;

  // delay buffers
  sF32 maindelbuf[2][32768];
  sF32 chandelbuf[CHANS][2][2048];

  V2Instance instance;

  syWRonan ronan;

  void init(const void *patchmap, sInt samplerate)
  {
    // Ahem, so this is somewhat dubious, but we don't use
    // virtual functions or anything so it should be fine. Ahem.
    // Look away please :)
    //
    // PORT FIX: the original used sizeof(this) (== 4, a pointer!) so this
    // memset cleared only 4 bytes and the port silently relied on the caller
    // handing it zeroed memory. The ASM _synthInit zeroes the WHOLE instance
    // ("mov ecx, SYN.size / rep stosb"). Match it: sizeof(*this).
    memset(this, 0, sizeof(*this));

    // era compat: the memset above would leave srcVersion == 0 (= fr08 era!);
    // default to modern. A period file must opt in via synthSetSourceVersion
    // AFTER synthInit (mirrors the synthSetGlobals call order in the player).
    instance.srcVersion = V2Instance::SRCVER_MODERN;

    // set sampling rate
    this->samplerate = samplerate;
    instance.calcNewSampleRate(samplerate);
    ronanCBSetSR(&ronan, samplerate);

    // patch map
    this->patchmap = (const V2PatchMap*)patchmap;

    // init voices
    for (sInt i=0; i < POLY; i++)
    {
      chanmap[i] = -1;
      voicesw[i].init(&instance);
    }

    // init channels
    for (sInt i=0; i < CHANS; i++)
    {
      chans[i].ctl[6] = 0x7f;
      chansw[i].init(&instance, chandelbuf[i][0], chandelbuf[i][1], COUNTOF(chandelbuf[i][0]));
    }

    // global filters
    reverb.init(&instance);
    delay.init(&instance, maindelbuf[0], maindelbuf[1], COUNTOF(maindelbuf[0]));
    ronanCBInit(&ronan);
    compr.init(&instance);
    dcf.init(&instance);

    // debug plots (uncomment the ones you want)
    sInt sr_plot = 44100/10; // plot rate
    sInt sr_lfo = 800;
    sInt w = 800, h = 150;

    //DEBUG_PLOT_OPEN(&voicesw[1].osc[0], "Voice 1 VCO 0", sr_plot, w, h);
    //DEBUG_PLOT_OPEN(&voicesw[1].osc[1], "Voice 1 VCO 1", sr_plot, w, h);
    //DEBUG_PLOT_OPEN(&voicesw[1].vcf[0], "Voice 1 VCF 0", sr_plot, w, h);
    DEBUG_PLOT_OPEN(&voicesw[1].env[0], "Voice 1 Env 0", sr_lfo, w, h);
    //DEBUG_PLOT_OPEN(&voicesw[1].lfo[0], "Voice 1 LFO 0", sr_lfo, w, h);
    //DEBUG_PLOT_OPEN(&voicesw[1].dist, "Voice 1 Dist", sr_plot, w, h);
    //DEBUG_PLOT_OPEN(&voicesw[1], "Voice 1 final", sr_plot, w, h);
    //DEBUG_PLOT_OPEN(DEBUG_PLOT_CHAN(&chansw[0].dcf1, 0), "Chan 0 DCF1 L", sr_plot, w, h);
    //DEBUG_PLOT_OPEN(DEBUG_PLOT_CHAN(&chansw[0].dcf1, 1), "Chan 0 DCF1 R", sr_plot, w, h);
    //DEBUG_PLOT_OPEN(DEBUG_PLOT_CHAN(&chansw[0], 0), "Channel 0 L", sr_plot, w, h);
    //DEBUG_PLOT_OPEN(DEBUG_PLOT_CHAN(&chansw[0], 1), "Channel 0 R", sr_plot, w, h);
    //DEBUG_PLOT_OPEN(DEBUG_PLOT_CHAN(&instance.mixbuf, 0), "Mix L", sr_plot, w, h);
    //DEBUG_PLOT_OPEN(DEBUG_PLOT_CHAN(&instance.mixbuf, 1), "Mix R", sr_plot, w, h);

    initialized = true;
  }

  void render(sF32 *buf, sInt nsamples, sF32 *buf2, bool add)
  {
    sInt todo = nsamples;

#ifdef V2_VALIDATE
    // Reference base for freq-ledger object offsets; mirrors the asm `mov [this],ebp`
    // at the top of synthRender (synth.asm:4773). Same logical base in both cores.
    g_freqlog_synthbase = (char *)this;
#endif

    // fragment loop - chunk everything into frames.
    while (todo)
    {
      // do we need to render a new frame?
      if (!tickd)
        tick();

      // copy to dest buffer(s)
      const StereoSample *src = &instance.mixbuf[instance.SRcFrameSize - tickd];
      sInt nread = min(todo, tickd);
      if (!buf2) // interleaved samples
      {
        if (!add)
        {
          COVER("OUT interleaved set");
          memcpy(buf, src, nread * sizeof(StereoSample));
        }
        else
        {
          COVER("OUT interleaved add");
          for (sInt i=0; i < nread; i++)
          {
            buf[i*2+0] += src[i].l;
            buf[i*2+1] += src[i].r;
          }
        }

        buf += 2*nread;
      }
      else // buf = left, buf2 = right
      {
        if (!add)
        {
          COVER("OUT separate set");
          for (sInt i=0; i < nread; i++)
          {
            buf[i] = src[i].l;
            buf2[i] = src[i].r;
          }
        }
        else
        {
          COVER("OUT separate add");
          for (sInt i=0; i < nread; i++)
          {
            buf[i] += src[i].l;
            buf2[i] += src[i].r;
          }
        }

        buf += nread;
        buf2 += nread;
      }

      todo -= nread;
      tickd -= nread;
    }

    DEBUG_PLOT_UPDATE();
  }

  void processMIDI(const sU8 *cmd)
  {
    while (*cmd != 0xfd) // until end of stream
    {
      if (*cmd & 0x80) // start of message
        mrstat = *cmd++;

      if (mrstat < 0x80) // we don't have a current message? uhm...
        break;

      sInt chan = mrstat & 0xf;
      switch ((mrstat >> 4) & 7)
      {
      case 1: // Note on
        if (cmd[1] != 0) // velocity==0 is actually a note off
        {
          COVER("MIDI note on");
          if (chan == CHANS-1)
            ronanCBNoteOn(&ronan);

          // calculate current polyphony for this channel
          const V2Sound *sound = getpatch(chans[chan].pgm);
          sInt npoly = 0;
          for (sInt i=0; i < POLY; i++)
            npoly += (chanmap[i] == chan);

#ifdef V2_VALIDATE
          if (getenv("NOTETRACE"))
            fprintf(stderr, "[noteon] chan=%d note=%d vel=%d pgm=%d npoly=%d maxpoly=%d\n",
                    chan, cmd[0], cmd[1], chans[chan].pgm, npoly, sound->maxpoly);
#endif

          // voice allocation. this is equivalent to the original V2 code,
          // but hopefully simpler to follow.
          sInt usevoice = -1;
          sInt chanmask, chanfind;

          if (!npoly || npoly < sound->maxpoly) // even if maxpoly is 0, allow at least 1.
          {
            // if we haven't reached polyphony limit yet, try to find a free voice
            // first.
            for (sInt i=0; i < POLY; i++)
            {
              if (chanmap[i] < 0)
              {
                usevoice = i;
                break;
              }
            }

            // okay, need to find a free voice. we'll take any channel.
            COVER("SYN find voice any");
            chanmask = 0;
            chanfind = 0;
          }
          else
          {
            // if we're at polyphony limit, we know there's at least one voice
            // used by this channel, so we can limit ourselves to killing
            // voices from our own chan.
            // PORTING FIX (not an ASM bug): the asm steal loops (.killvoice,
            // synth.asm:5512/5535) compare the FULL chanmap entry against the
            // channel. Masking with 0xf made FREE voices (chanmap == -1,
            // -1 & 0xf == 15) eligible as "channel 15's own voices", so at the
            // poly limit on the Ronan channel the port ALLOCATED fresh voices
            // instead of stealing — polyphony grew unbounded (kkrieger6's
            // velocity-ramp swells on chan 15).
            COVER("SYN find voice channel");
            chanmask = ~0;
            chanfind = chan;
          }

          // don't have a voice yet? kill oldest eligible one with gate off.
          if (usevoice < 0)
          {
            COVER("SYN replace voice gate off");
            sU32 oldest = curalloc;
            for (sInt i=0; i < POLY; i++)
            {
              if ((chanmap[i] & chanmask) == chanfind && !voicesw[i].gate && allocpos[i] < oldest)
              {
                oldest = allocpos[i];
                usevoice = i;
              }
            }
          }

          // still no voice? okay, just take the oldest one we can find, period.
          if (usevoice < 0)
          {
            COVER("SYN replace voice oldest");
            sU32 oldest = curalloc;
            for (sInt i=0; i < POLY; i++)
            {
              if ((chanmap[i] & chanmask) == chanfind && allocpos[i] < oldest)
              {
                oldest = allocpos[i];
                usevoice = i;
              }
            }
          }

          // we have our voice - assign it!
          assert(usevoice >= 0);
          chanmap[usevoice] = chan;
          voicemap[chan] = usevoice;
          allocpos[usevoice] = curalloc++;

          // and note on!
#ifdef V2_VALIDATE
          if (getenv("ALLOCTRACE"))
            fprintf(stderr, "[C++ alloc] note=%d vel=%d chan=%d -> slot=%d (npoly=%d)\n",
                    cmd[0], cmd[1], chan, usevoice, npoly);
#endif
          storeV2Values(usevoice);
          voicesw[usevoice].noteOn(cmd[0], cmd[1]);
          cmd += 2;
          break;
        }
        // fall-through (for when we had a note off)

      case 0: // Note off
        COVER("MIDI note off");
        if (chan == CHANS-1)
          ronanCBNoteOff(&ronan);

        for (sInt i=0; i < POLY; i++)
        {
          if (chanmap[i] != chan)
            continue;

          V2Voice *voice = &voicesw[i];
          if (voice->note == cmd[0] && voice->gate)
          {
            voice->noteOff();
            // The asm (ProcessNoteOff, synth.asm:5398-5400) stops at the FIRST
            // matching voice (`jmp .end`). The port looped over all POLY voices and
            // released EVERY voice holding this note, so when a note is held on more
            // than one voice of a channel (overlapping/retriggered notes) the port
            // released the extra voice(s) the asm keeps sustaining -- a slowly
            // accumulating whole-song divergence via the modulation envelopes.
            break;
          }
        }
        cmd += 2;
        break;

      case 2: // Aftertouch
        COVER("MIDI aftertouch");
        cmd++; // ignored
        break;

      case 3: // Control change
        COVER("MIDI controller change");
        {
          sInt ctrl = cmd[0];
          sU8 val = cmd[1];
          if (ctrl >= 1 && ctrl <= 7)
          {
            chans[chan].ctl[ctrl - 1] = val;
            if (chan == CHANS-1)
              ronanCBSetCtl(&ronan, ctrl, val);
          }
          else if (ctrl == 120) // CC #120: all sound off
          {
            COVER("MIDI CC all sound off");
            for (sInt i=0; i < POLY; i++)
            {
              if (chanmap[i] != chan)
                continue;

              voicesw[i].init(&instance);
              chanmap[i] = -1;
            }
          }
          else if (ctrl == 123) // CC #123: all notes off
          {
            COVER("MIDI CC all notes off");
            if (chan == CHANS-1)
              ronanCBNoteOff(&ronan);

            for (sInt i=0; i < POLY; i++)
            {
              if (chanmap[i] == chan)
                voicesw[i].noteOff();
            }
          }
        }
        cmd += 2;
        break;

      case 4: // Program change
        COVER("MIDI program change");
        {
          sU8 pgm = *cmd++ & 0x7f;
          // did the program actually change?
          if (chans[chan].pgm != pgm)
          {
            COVER("MIDI program change real");
            chans[chan].pgm = pgm;

            // need to turn all voices on this channel off.
            for (sInt i=0; i < POLY; i++)
            {
              if (chanmap[i] == chan)
                chanmap[i] = -1;
            }
          }

          // either way, reset controllers
          for (sInt i=0; i < 6; i++)
            chans[chan].ctl[i] = 0;
        }
        break;

      case 5: // Pitch bend
        COVER("MIDI pitch bend");
        cmd += 2; // ignored
        break;

      case 6: // Poly Aftertouch
        COVER("MIDI poly aftertouch");
        cmd += 2; // ignored
        break;

      case 7: // System
        COVER("MIDI system exclusive");
        if (chan == 0xf) // Reset
          init(patchmap, samplerate);
        break; // rest ignored
      }
    }
  }

  void setGlobals(const sU8 *para)
  {
    if (!initialized)
      return;

    // copy over
    sF32 *globf = (sF32 *)&globals;
    for (sInt i=0; i < sizeof(globals)/sizeof(sF32); i++)
      globf[i] = para[i];

    // set
    reverb.set(&globals.rvbparm);
    delay.set(&globals.delparm);
    compr.set(&globals.cprparm);
    lcfreq = sqr((globals.vlowcut + 1.0f) / 128.0f);
    hcfreq = sqr((globals.vhighcut + 1.0f) / 128.0f);
  }

  void getPoly(sInt *dest)
  {
    for (sInt i=0; i <= CHANS; i++)
      dest[i] = 0;

    for (sInt i=0; i < POLY; i++)
    {
      sInt chan = chanmap[i];
      if (chan < 0)
        continue;

      dest[chan]++;
      dest[CHANS]++;
    }
  }

  void getPgm(sInt *dest)
  {
    for (sInt i=0; i < CHANS; i++)
      dest[i] = chans[i].pgm;
  }

private:
  const V2Sound *getpatch(sInt pgm) const
  {
    assert(pgm >= 0 && pgm < 128);
    return (const V2Sound *)&patchmap->raw_data[patchmap->offsets[pgm]];
  }

  sF32 getmodsource(const V2Voice *voice, sInt chan, sInt source) const
  {
    sF32 in = 0.0f;

    switch (source)
    {
    case 0: // velocity
      COVER("MOD src vel");
      in = voice->velo;
      break;

    case 1: case 2: case 3: case 4: case 5: case 6: case 7: // controller value
      COVER("MOD src ctl");
      in = chans[chan].ctl[source-1];
      break;

    case 8: case 9: // EG output
      COVER("MOD src EG");
      in = voice->env[source-8].out;
      break;

    case 10: case 11: // LFO output
      COVER("MOD src LFO");
      in = voice->lfo[source-10].out;
      break;

    default: // note
      COVER("MOD src note");
      in = 2.0f * (voice->note - 48.0f);
      break;
    }

    return in;
  }

  void storeV2Values(sInt vind)
  {
    assert(vind >= 0 && vind < POLY);
    sInt chan = chanmap[vind];
    if (chan < 0)
      return;

    // get patch definition
    const V2Sound *patch = getpatch(chans[chan].pgm);

    // voice data
    syVV2 *vpara = &voicesv[vind];
    sF32 *vparaf = (sF32 *)vpara;
    V2Voice *voice = &voicesw[vind];
    
    // copy voice dependent data
    for (sInt i=0; i < COUNTOF(patch->voice); i++)
      vparaf[i] = (sF32)patch->voice[i];

#ifdef V2_VALIDATE
    if (getenv("MODDUMP")) {
      static int done = 0;
      if (!done++) {
        // env[0] (aenv) param index range, to flag amp-env-targeting mods.
        sInt aenvbase = (sInt)((sF32*)&vpara->env[0] - (sF32*)vpara);
        sInt aenvend  = aenvbase + (sInt)(sizeof(syVEnv)/sizeof(sF32));
        fprintf(stderr,"[MODDUMP] voiceparams=%d aenv idx [%d..%d) modnum=%d\n",
                (int)COUNTOF(patch->voice), aenvbase, aenvend, patch->modnum);
        for (sInt i=0;i<patch->modnum;i++){
          const V2Mod*m=&patch->modmatrix[i];
          const char*tag=(m->dest>=aenvbase&&m->dest<aenvend)?"  <-- AMP ENV":"";
          fprintf(stderr,"[MODDUMP]  src=%d val=%d dest=%d%s\n",m->source,m->val,m->dest,tag);
        }
      }
    }
#endif
    // modulation matrix
    for (sInt i=0; i < patch->modnum; i++)
    {
      const V2Mod *mod = &patch->modmatrix[i];
      if (mod->dest >= COUNTOF(patch->voice))
        continue;

      sF32 scale = (mod->val - 64.0f) / 64.0f;
      vparaf[mod->dest] = clamp(vparaf[mod->dest] + scale*getmodsource(voice, chan, mod->source), 0.0f, 128.0f);
    }

    voice->set(vpara);
  }

  void storeChanValues(sInt chan)
  {
    assert(chan >= 0 && chan < CHANS);

    // get patch definition
    const V2Sound *patch = getpatch(chans[chan].pgm);

    // chan data
    syVChan *cpara = &chansv[chan];
    sF32 *cparaf = (sF32 *)cpara;
    V2Chan *cwork = &chansw[chan];
    V2Voice *voice = &voicesw[voicemap[chan]];

    // copy channel dependent data
    for (sInt i=0; i < COUNTOF(patch->chan); i++)
      cparaf[i] = (sF32)patch->chan[i];

    // modulation matrix
    for (sInt i=0; i < patch->modnum; i++)
    {
      const V2Mod *mod = &patch->modmatrix[i];
      sInt dest = mod->dest - COUNTOF(patch->voice);
      if (dest < 0 || dest >= COUNTOF(patch->chan))
        continue;

      sF32 scale = (mod->val - 64.0f) / 64.0f;
      cparaf[dest] = clamp(cparaf[dest] + scale*getmodsource(voice, chan, mod->source), 0.0f, 128.0f);
    }

    cwork->set(cpara);
  }

  void tick()
  {
    // voices
    for (sInt i=0; i < POLY; i++)
    {
      if (chanmap[i] < 0)
        continue;

      storeV2Values(i);
      voicesw[i].tick();

      // if EG1 finished, turn off voice
      if (voicesw[i].env[0].state == V2Env::OFF)
        chanmap[i] = -1;
    }

    // chans
    for (sInt i=0; i < CHANS; i++)
      storeChanValues(i);

    ronanCBTick(&ronan);
    tickd = instance.SRcFrameSize;
    renderFrame();

#if COVERAGE
    // print coverage updates as they happen
    static int cur_frame;
    static const char *old_coverage[COUNTOF(code_coverage)];
    sInt ccount = synthGetNumCoverage();
    for (sInt i=0; i < ccount; i++)
    {
      if (old_coverage[i] != code_coverage[i])
      {
        old_coverage[i] = code_coverage[i];
        printf("[%5d,%3d] %s\n", cur_frame, i, code_coverage[i]);
      }
    }
    cur_frame++;
#endif
  }

  void renderFrame()
  {
    sInt nsamples = instance.SRcFrameSize;

    // clear output buffer
    memset(instance.mixbuf, 0, nsamples * sizeof(StereoSample));

    // clear aux buffers
    memset(instance.aux1buf, 0, nsamples * sizeof(sF32));
    memset(instance.aux2buf, 0, nsamples * sizeof(sF32));
    memset(instance.auxabuf, 0, nsamples * sizeof(StereoSample));
    memset(instance.auxbbuf, 0, nsamples * sizeof(StereoSample));

#ifdef V2_VALIDATE
    // chantap/vcetap are UNMASKED accumulators across all voices/channels: reset
    // here, accumulate below.
    memset(g_chantap, 0, nsamples * sizeof(StereoSample));
    memset(g_vcetap_osc,  0, nsamples * sizeof(sF32));
    memset(g_vcetap_flt,  0, nsamples * sizeof(sF32));
    memset(g_vcetap_dist, 0, nsamples * sizeof(sF32));
    memset(g_vcetap_dcf,  0, nsamples * sizeof(sF32));
    // per-channel-chain sub-stage taps (accumulated across channels below)
    memset(g_chtap_dcf1,   0, nsamples * sizeof(StereoSample));
    memset(g_chtap_comp,   0, nsamples * sizeof(StereoSample));
    memset(g_chtap_boost,  0, nsamples * sizeof(StereoSample));
    memset(g_chtap_dist,   0, nsamples * sizeof(StereoSample));
    memset(g_chtap_chorus, 0, nsamples * sizeof(StereoSample));
    memset(g_chtap_dcf2,   0, nsamples * sizeof(StereoSample));
#endif

    // process all channels
    for (sInt chan=0; chan < CHANS; chan++)
    {
      // check if any voices are active on this channel
      sInt voice = 0;
      while (voice < POLY && chanmap[voice] != chan)
        voice++;

      if (voice == POLY)
        continue;

      // clear channel buffer
      memset(instance.chanbuf, 0, nsamples * sizeof(StereoSample));

      // render all voices on this channel
      for (; voice < POLY; voice++)
      {
        if (chanmap[voice] != chan)
          continue;

        voicesw[voice].render(instance.chanbuf, nsamples);
      }
#ifdef V2_VALIDATE
      for (sInt i=0; i < nsamples; i++) {
        g_chantap[i].l += instance.chanbuf[i].l;
        g_chantap[i].r += instance.chanbuf[i].r;
      }
#endif

      // channel 15 -> Ronan
      if (chan == CHANS-1)
        ronanCBProcess(&ronan, &instance.chanbuf[0].l, nsamples);

      chansw[chan].process(nsamples); 
    }

    // global filters
    StereoSample *mix = instance.mixbuf;
    MIXTAP_SNAP(premix, mix, nsamples);   // dry channel sum, before any global FX
    reverb.render(mix, nsamples);
    MIXTAP_SNAP(reverb, mix, nsamples);
    delay.renderAux2Main(mix, nsamples);
    MIXTAP_SNAP(delay, mix, nsamples);
    dcf.renderStereo(mix, mix, nsamples);
    MIXTAP_SNAP(dcf, mix, nsamples);

    // low cut/high cut
    sF32 lcf = lcfreq, hcf = hcfreq;
    for (sInt i=0; i < nsamples; i++)
    {
      for (sInt ch=0; ch < 2; ch++)
      {
        // low cut
        sF32 x = mix[i].ch[ch] - lcbuf[ch];
        lcbuf[ch] += lcf * x;

        // high cut
        if (hcf != 1.0f)
        {
          hcbuf[ch] += hcf * (x - hcbuf[ch]);
          x = hcbuf[ch];
        }

        mix[i].ch[ch] = x;
      }
    }

    MIXTAP_SNAP(lchc, mix, nsamples);

    // sum compressor
    compr.render(mix, nsamples);
    MIXTAP_SNAP(compr, mix, nsamples);

    DEBUG_PLOT_STEREO(mix, mix, nsamples);
  }
};

// --------------------------------------------------------------------------
// C-style interface
// --------------------------------------------------------------------------

unsigned int __stdcall synthGetSize()
{
  return sizeof(V2Synth);
}

void __stdcall synthInit(void *pthis, const void *patchmap, int samplerate)
{
  ((V2Synth *)pthis)->init(patchmap, samplerate);
}

void __stdcall synthRender(void *pthis, void *buf, int smp, void *buf2, int add)
{
  ((V2Synth *)pthis)->render((sF32 *)buf, smp, (sF32 *)buf2, add != 0);
}

void __stdcall synthProcessMIDI(void *pthis, const void *ptr)
{
  ((V2Synth *)pthis)->processMIDI((const sU8 *)ptr);
}

void __stdcall synthSetGlobals(void *pthis, const void *ptr)
{
  ((V2Synth *)pthis)->setGlobals((const sU8 *)ptr);
}

#ifdef V2_VALIDATE
// Validation: render ONE year-2000 (eraV0) oscillator in isolation so it can be
// unit-tested against the genuine 2000 syOscRender called in the C1 image
// (validate/c1_osc_probe.c). p = {mode,pitch,detune,color,gain} in the 2000
// syVOsc field order (NO ring). dest = nsamples mono floats (caller zeroes).
extern "C" void synthTestOscV0(const float *p, unsigned cnt0, unsigned nseed0,
                               float *dest, int nsamples)
{
  static V2Instance inst;
  inst.calcNewSampleRate(44100);
  inst.srcVersion = 0; // eraV0 + eraEnvOld
  static V2Osc osc;
  osc.init(&inst, 0);
  syVOsc para;
  para.mode = p[0]; para.ring = 0.0f; para.pitch = p[1];
  para.detune = p[2]; para.color = p[3]; para.gain = p[4];
  osc.set(&para);
  osc.cnt = cnt0;
  osc.nseed = nseed0;
  osc.render(dest, nsamples);
}

// Validation: render ONE eraV0 filter in isolation, to unit-test against the
// genuine 2000 syFltSet/syFltRender (@0x40a837/0x40a880) in c1_flt_probe.
// p = {mode,cutoff,reso} (2000 syVFlt order). src/dst = nsamples mono floats.
extern "C" void synthTestFltV0(const float *p, const float *src,
                               float *dst, int nsamples)
{
  static V2Instance inst;
  inst.calcNewSampleRate(44100);
  inst.srcVersion = 0; // eraV0 (dco=0 in the SVF render)
  static V2Flt flt;
  flt.init(&inst);
  syVFlt para;
  para.mode = p[0]; para.cutoff = p[1]; para.reso = p[2];
  flt.set(&para);
  flt.render(dst, src, nsamples);
}

// Validation: render ONE eraV0 distortion in isolation, vs the genuine 2000
// syDistSet/syDistRenderMono (@0x40aa93/0x40ab71) in c1_dist_probe.
// p = {mode,ingain,param1,param2} (syVDist order). src/dst = nsamples mono.
extern "C" void synthTestDistV0(const float *p, const float *src,
                                float *dst, int nsamples)
{
  static V2Instance inst;
  inst.calcNewSampleRate(44100);
  inst.srcVersion = 0;
  static V2Dist dist;
  dist.init(&inst);
  syVDist para;
  para.mode = p[0]; para.ingain = p[1]; para.param1 = p[2]; para.param2 = p[3];
  dist.set(&para);
  dist.renderMono(dst, src, nsamples);
}

// Validation: render the eraV0 channel chorus (V2ModDel) in isolation vs the
// genuine 2000 syChorusSet/render (@0x40b0c1/0x40b268) in c1_chorus_probe.
// p = syVModDel {amount,fb,llength,rlength,mrate,mdepth,mphase}; io = n
// interleaved-stereo frames, processed in place. Delay buffer = 2048 (= V2Chan).
extern "C" void synthTestChorusV0(const float *p, float *io, int nsamples)
{
  static V2Instance inst;
  inst.calcNewSampleRate(44100);
  inst.srcVersion = 0;
  static sF32 b0[2048], b1[2048];
  static V2ModDel md;
  md.init(&inst, b0, b1, 2048);
  syVModDel para;
  para.amount = p[0]; para.fb = p[1]; para.llength = p[2]; para.rlength = p[3];
  para.mrate = p[4]; para.mdepth = p[5]; para.mphase = p[6];
  md.set(&para);
  md.renderChan((StereoSample *)io, nsamples);
}
#endif

void __stdcall synthSetSourceVersion(void *pthis, int srcver)
{
  // era compat (see V2Instance::srcVersion). Call after synthInit (which sets
  // the modern 128-sample frame); we recompute the control-frame size here.
  V2Instance &inst = ((V2Synth *)pthis)->instance;
  inst.srcVersion = srcver;
  // ROOT delta (fr08-extraction/DELTA.md): the year-2000 synth ran the control
  // rate at a 256-sample frame (driver resets the frame counter to 0x100), vs
  // 128 in 2004. This single change is the root of the envelope decay/release
  // shaping (calcfreq x10 over 256 == ~calcfreq2 x11 over 128, kb's transEnv
  // sqrt relationship) AND the volume-ramp coeff (1/256 vs 1/128 = 1/frame).
  // Voices read SRcFrameSize / SRfciframe live, so overriding here (post-init,
  // pre-render) retimes env/LFO/volramp without touching the SR constants.
  if (inst.eraEnvOld())
  {
    inst.SRcFrameSize = 256;
    inst.SRfciframe   = 1.0f / 256.0f;
  }
  // Matched-seed A/B (DELTA.md D6): the C1 ground truth pins rdtsc=0, so all
  // osc-noise / LFO-S&H seeds start at 0. The voices were init'd during
  // synthInit (before srcVersion was set), so re-seed them here; keysync
  // noteOns re-init through the eraV0() path and seed 0 on their own.
  V2Synth *syn = (V2Synth *)pthis;
  if (inst.eraV0())
    for (sInt v=0; v < V2Synth::POLY; v++)
    {
      for (sInt o=0; o < syVV2::NOSC; o++) syn->voicesw[v].osc[o].nseed = 0u;
      for (sInt l=0; l < syVV2::NLFO; l++) syn->voicesw[v].lfo[l].nseed = 0u;
    }
}

void __stdcall synthGetPoly(void *pthis, void *dest)
{
  ((V2Synth *)pthis)->getPoly((sInt*)dest);
}

void __stdcall synthGetPgm(void *pthis, void *dest)
{
  ((V2Synth *)pthis)->getPgm((sInt*)dest);
}

void __stdcall synthSetVUMode(void *, int)
{
  // nyi
}

void __stdcall synthGetChannelVU(void *, int, float *, float *)
{
  // nyi
}

void __stdcall synthGetMainVU(void *, float *, float *)
{
  // nyi
}

long __stdcall synthGetFrameSize(void *pthis)
{
  return ((V2Synth *)pthis)->instance.SRcFrameSize;
}

#ifdef V2_VALIDATE
// Validation-only: expose the live per-frame "bus" buffers (channel sends to
// the global FX, and the final mix) so the A/B harness can bus-tap and bisect
// where the whole-song divergence first appears. Read-only; never affects DSP.
// Mirrored in v2/validate/asm_appendix.asm for the ASM core.
extern "C" void __stdcall synthDebugGetBus(void *pthis, float **a1, float **a2,
                                           float **mix, int *framesize)
{
  V2Synth *s = (V2Synth *)pthis;
  *a1        = s->instance.aux1buf;
  *a2        = s->instance.aux2buf;
  *mix       = &s->instance.mixbuf[0].l;
  *framesize = s->instance.SRcFrameSize;
}

// Validation-only: expose the five per-stage mix-chain snapshots (see
// g_mixtap_* above) so the A/B harness can bisect WHICH global FX stage
// introduces the divergence. Read-only. Mirrored in asm_appendix.asm.
extern "C" void __stdcall synthDebugGetMixTap(void *pthis,
    float **postReverb, float **postDelay, float **postDcf,
    float **postLcHc, float **postCompr, int *framesize)
{
  V2Synth *s = (V2Synth *)pthis;
  *postReverb = &g_mixtap_reverb[0].l;
  *postDelay  = &g_mixtap_delay [0].l;
  *postDcf    = &g_mixtap_dcf   [0].l;
  *postLcHc   = &g_mixtap_lchc  [0].l;
  *postCompr  = &g_mixtap_compr [0].l;
  *framesize  = s->instance.SRcFrameSize;
}

// Validation-only: expose the four per-voice-substage snapshots (see g_vcetap_*
// above), one level deeper than the mix tap, to find which voice block first
// diverges. Mono (vcebuf). Read-only. Mirrored in asm_appendix.asm.
extern "C" void __stdcall synthDebugGetVceTap(void *pthis,
    float **postOsc, float **postFlt, float **postDist, float **postDcf,
    int *framesize)
{
  V2Synth *s = (V2Synth *)pthis;
  *postOsc  = g_vcetap_osc;
  *postFlt  = g_vcetap_flt;
  *postDist = g_vcetap_dist;
  *postDcf  = g_vcetap_dcf;
  *framesize = s->instance.SRcFrameSize;
}

// Validation-only: hand the player the freq-divergence ledger accumulated during
// the just-finished synthRender, then clear it (read-and-clear drain). Mirrored in
// asm_appendix.asm. The record layout is FreqLogRec (compat.h).
extern "C" void __stdcall synthDebugGetFreqLog(void **recs, int *count)
{
  *recs = g_freqlog_buf;
  *count = (int)g_freqlog_count;
  g_freqlog_count = 0;
}

// Validation-only: drain the control-source ledger (read-and-clear). Mirrored in asm.
extern "C" void __stdcall synthDebugGetCtrlLog(void **recs, int *count)
{
  *recs = g_ctrllog_buf;
  *count = (int)g_ctrllog_count;
  g_ctrllog_count = 0;
}

// Validation-only: drain the filter ledger (read-and-clear). Mirrored in asm.
extern "C" void __stdcall synthDebugGetFltLog(void **recs, int *count)
{
  *recs = g_fltlog_buf;
  *count = (int)g_fltlog_count;
  g_fltlog_count = 0;
}

// Validation-only: drain the osc-output ledger (read-and-clear). Mirrored in asm.
extern "C" void __stdcall synthDebugGetOscLog(void **recs, int *count)
{
  *recs = g_osclog_buf;
  *count = (int)g_osclog_count;
  g_osclog_count = 0;
}

// Validation-only: arm the freq knockout. `recs`/`count` is an asm-recorded ledger
// (FreqLogRec[]); subsequent freq computations replay these values by call ordinal.
// Pass count=0 to disarm. The C++ core only; the asm oracle is never overridden.
extern "C" void __stdcall synthDebugArmFreqKnockout(void *recs, int count)
{
  g_freqknockout     = (const FreqLogRec *)recs;
  g_freqknockout_n   = (unsigned)count;
  g_freqknockout_pos = 0;
}

// Validation-only: the dry mix (mixbuf before any global FX) -- bridges the clean
// channel chain and post_reverb to isolate dry-mix vs reverb as the source.
extern "C" void __stdcall synthDebugGetPreMix(void *pthis, float **premix, int *framesize)
{
  V2Synth *s = (V2Synth *)pthis;
  *premix = &g_mixtap_premix[0].l;
  *framesize = s->instance.SRcFrameSize;
}

// Validation-only: the post-curvol channel voice-sum snapshot (see g_chantap).
extern "C" void __stdcall synthDebugGetChanTap(void *pthis, float **chan, int *framesize)
{
  V2Synth *s = (V2Synth *)pthis;
  *chan = &g_chantap[0].l;
  *framesize = s->instance.SRcFrameSize;
}

// Validation-only: expose the six per-channel-chain sub-stage snapshots (see
// g_chtap_* above), one level below the chan tap, to pin WHICH channel-FX block
// introduces the divergence. Stereo. Read-only. Mirrored in asm_appendix.asm.
extern "C" void __stdcall synthDebugGetChainTap(void *pthis,
    float **postDcf1, float **postComp, float **postBoost,
    float **postDist, float **postChorus, float **postDcf2, int *framesize)
{
  V2Synth *s = (V2Synth *)pthis;
  *postDcf1   = &g_chtap_dcf1  [0].l;
  *postComp   = &g_chtap_comp  [0].l;
  *postBoost  = &g_chtap_boost [0].l;
  *postDist   = &g_chtap_dist  [0].l;
  *postChorus = &g_chtap_chorus[0].l;
  *postDcf2   = &g_chtap_dcf2  [0].l;
  *framesize  = s->instance.SRcFrameSize;
}
#endif

extern "C" void * __stdcall synthGetSpeechMem(void *pthis)
{
  return &((V2Synth *)pthis)->ronan;
}

#if COVERAGE

void synthPrintCoverage()
{
  int end = synthGetNumCoverage();
  printf("synth coverage:\n");
  for (int i=0; i < end; i++)
    printf("[%3d] %s\n", i, code_coverage[i] ? code_coverage[i] : "<not hit>");
}

static int synthGetNumCoverage()
{
  return __COUNTER__;
}

#endif

// vim: sw=2:sts=2:et:cino=\:0l1g0(0
