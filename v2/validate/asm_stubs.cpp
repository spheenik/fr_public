#include <cstdio>
#include <cstdlib>
// Stubs linked ONLY into harness_asm.
//
// The shared player calls synthSetLyrics() unconditionally (Reset()). The C++
// core defines it as a no-op (synth_core.cpp), so harness_cpp resolves it
// there. The RONAN-disabled asm core does not export it, so we supply the same
// no-op here. Result: lyrics are a no-op on BOTH sides — identical behavior,
// correct for the RONAN-off / non-speech baseline.
// The asm core's SR coefficients are GLOBAL symbols (set by calcNewSampleRate
// during synthInit). synthSetLyrics runs right after synthInit in Reset(), so by
// then they're valid — dump them here (SRTRACE) to compare with the C++ side.
extern "C" { extern float SRfcobasefrq, SRfclinfreq,
                          SRfcdcfilter, SRfcBoostCos, SRfcBoostSin; }
extern "C" void __attribute__((stdcall)) synthSetLyrics(void *, const char **)
{
  if (getenv("SRTRACE")) {
    union { float f; unsigned u; } b,c,d,e,g;
    b.f=SRfcobasefrq; c.f=SRfclinfreq;
    d.f=SRfcdcfilter; e.f=SRfcBoostCos; g.f=SRfcBoostSin;
    fprintf(stderr, "[ASM SR] obasefrq=%08x linfreq=%08x dcfilter=%08x BoostCos=%08x BoostSin=%08x\n",
            b.u, c.u, d.u, e.u, g.u);
  }
}

// Era compat no-op for harness_asm: the player calls synthSetSourceVersion()
// after synthInit when a source format version is declared (see v2mplayer.h).
// The 2004 ASM core has no era concept -- it IS the modern behavior the era
// flag defaults to -- so the correct asm-side implementation is "ignore".
// (Period gating is C++-core-only; the period A/B reference is the genuine
// 2000 code via validate/c1_fr08_harness, not the 2004 asm.)
extern "C" void __attribute__((stdcall)) synthSetSourceVersion(void *, int)
{
}

// Validation: asm-side chorus-integer dump. asm_appendix.asm's chorusdbg_snap
// (sed-injected into syModDelSet) reads the just-computed syWModDel integers and
// calls this (cdecl). Mirrors synth_core.cpp's CHORUSTRACE so harness_asm and
// harness_cpp emit comparable lines. Gated by env CHORUSTRACE.
#include <cstdio>
#include <cstdlib>
extern "C" void chorusdbg_c(int mfreq, int d0, int d1, int mmaxoffs, unsigned mphase)
{
  if (getenv("CHORUSTRACE"))
    fprintf(stderr, "[ASM chorus.set] mfreq=%d dboffs=%d,%d mmaxoffs=%d mphase=%u\n",
            mfreq, d0, d1, mmaxoffs, mphase);
}

// Validation: asm-side compressor-integer dump (mirrors synth_core.cpp COMPTRACE).
// Floats are passed as their raw 32-bit patterns (printed %08x) for bit-exact diff.
extern "C" void compdbg_c(int mode, unsigned dblen, unsigned invol, unsigned outvol,
                          unsigned ratio, unsigned attack, unsigned release)
{
  if (getenv("COMPTRACE"))
    fprintf(stderr, "[ASM comp.set] mode=%d dblen=%u invol=%08x outvol=%08x ratio=%08x attack=%08x release=%08x\n",
            mode, dblen, invol, outvol, ratio, attack, release);
}

// Validation: asm-side voice-allocation dump (mirrors synth_core.cpp ALLOCTRACE).
extern "C" void allocdbg_c(int slot, int chan, int note, int vel)
{
  if (getenv("ALLOCTRACE"))
    fprintf(stderr, "[ASM alloc] note=%d vel=%d chan=%d -> slot=%d\n",
            note, vel, chan, slot);
}

// Validation: asm-side envelope coeff dump (mirrors synth_core.cpp ENVTRACE).
extern "C" void envdbg_c(unsigned atd, unsigned dcf, unsigned sul, unsigned suf,
                         unsigned ref, unsigned gain)
{
  if (getenv("ENVTRACE"))
    fprintf(stderr, "[ASM env.set] atd=%08x dcf=%08x sul=%08x suf=%08x ref=%08x gain=%08x\n",
            atd, dcf, sul, suf, ref, gain);
}

// Validation: asm-side boost biquad coeff dump (mirrors synth_core.cpp BOOSTTRACE).
extern "C" void boostdbg_c(int ena, unsigned b0, unsigned b1, unsigned b2,
                           unsigned a1, unsigned a2)
{
  if (getenv("BOOSTTRACE"))
    fprintf(stderr, "[ASM boost.set] ena=%d b0=%08x b1=%08x b2=%08x a1=%08x a2=%08x\n",
            ena, b0, b1, b2, a1, a2);
}

// Validation: asm-side dist OVERDRIVE/CLIP setup dump (mirrors synth_core.cpp
// DISTG2TRACE). Identical line format so the two streams diff directly.
// gain2 = (param1/128)/atan(gain1) is the fpatan-vs-libm-atan suspect.
extern "C" void distg2dbg_c(int mode, unsigned gain1, unsigned gain2, unsigned offs)
{
  if (getenv("DISTG2TRACE"))
    fprintf(stderr, "[dist.g2] mode=%d gain1=%08x gain2=%08x offs=%08x\n",
            mode, gain1, gain2, offs);
}

// Validation: asm-side reverb coeff dump (mirrors synth_core.cpp REVERBTRACE).
// Raw 32-bit patterns for bit-exact diff. Field order matches syCReverb.
extern "C" void reverbdbg_c(unsigned g0, unsigned g1, unsigned g2, unsigned g3,
                            unsigned a0, unsigned a1, unsigned damp,
                            unsigned gainin, unsigned lowcut)
{
  if (getenv("REVERBTRACE"))
    fprintf(stderr, "[ASM reverb.set] gainc=%08x,%08x,%08x,%08x gaina=%08x,%08x "
            "damp=%08x gainin=%08x lowcut=%08x\n",
            g0, g1, g2, g3, a0, a1, damp, gainin, lowcut);
}
