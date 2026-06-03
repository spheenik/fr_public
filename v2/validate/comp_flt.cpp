// Filter (VCF) per-component equivalence test (task 2.3) — the prime suspect.
// Same pattern as comp_osc.cpp. asm step is a BYTE stride (default 4 = 1 float),
// C++ step is an ELEMENT stride (default 1 = 1 float) — equivalent, no override.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

extern "C" void __stdcall OutputDebugStringA(const char *) {}
#include "../synth_core.cpp"   // V2Instance, V2Flt, syVFlt

extern "C" {
  void v2x_calcSR(int sr);
  void v2x_fltInit(void *W);
  void v2x_fltSet(void *W, const void *V);
  void v2x_fltRender(void *W, float *dst, const float *src, int n);
  extern unsigned int v2x_size_syWFlt;
}

static const float EPS = 1e-4f;

// deterministic test input: fixed-seed white-ish noise in [-1,1]
static void make_input(std::vector<float> &v, int N)
{
  v.resize(N);
  unsigned s = 0x12345678u;
  for (int i = 0; i < N; i++) {
    s = s * 1664525u + 1013904223u;
    v[i] = ((int)(s >> 8) & 0xffff) / 32768.0f - 1.0f;
  }
}

struct FltCase { const char *name; float mode, cutoff, reso; };

static int run_case(const FltCase &c, const std::vector<float> &src, int N)
{
  syVFlt para; para.mode = c.mode; para.cutoff = c.cutoff; para.reso = c.reso;

  static V2Instance inst; inst.calcNewSampleRate(44100);

  // C++ core
  V2Flt flt; flt.init(&inst); flt.set(&para);
  std::vector<float> cbuf(N, 0.0f);
  flt.render(cbuf.data(), src.data(), N, 1);

  // asm core
  v2x_calcSR(44100);
  std::vector<unsigned char> W(v2x_size_syWFlt, 0);
  v2x_fltInit(W.data());
  v2x_fltSet(W.data(), &para);

  if (c.mode >= 6) { // moog: compare the actual coefficients set() produced
    float *aw = (float*)W.data();           // syWFlt: mode,cfreq,res,moogf,moogp,moogq
    printf("    [coeffs] cpp moogf=%.7g moogp=%.7g moogq=%.7g\n", flt.moogf, flt.moogp, flt.moogq);
    printf("    [coeffs] asm moogf=%.7g moogp=%.7g moogq=%.7g\n", aw[3], aw[4], aw[5]);
  }
  std::vector<float> abuf(N, 0.0f);
  v2x_fltRender(W.data(), abuf.data(), src.data(), N);

  float maxerr = 0, peaka = 0; int first = -1;
  for (int i = 0; i < N; i++) {
    float d = fabsf(cbuf[i] - abuf[i]);
    if (d > maxerr) maxerr = d;
    if (fabsf(abuf[i]) > peaka) peaka = fabsf(abuf[i]);
    if (d > EPS && first < 0) first = i;
  }
  bool ok = first < 0;
  printf("  %-8s cut=%.0f res=%.0f  peak=%.4f maxerr=%.3g  %s",
         c.name, c.cutoff, c.reso, peaka, maxerr, ok ? "MATCH" : "DIVERGE");
  if (!ok) printf(" @ %d (cpp=%.6g asm=%.6g)", first, cbuf[first], abuf[first]);
  printf("\n");
  return ok ? 0 : 1;
}

int main()
{
  const int N = 512;
  std::vector<float> src; make_input(src, N);

  FltCase cases[] = {
    { "low",   1, 64, 32 }, { "band",  2, 64, 32 }, { "high",  3, 64, 32 },
    { "notch", 4, 64, 32 }, { "all",   5, 64, 32 },
    { "moogl", 6, 64, 32 }, { "moogh", 7, 64, 32 },
  };
  printf("VCF equivalence (N=%d, fixed-noise input):\n", N);
  int fails = 0;
  for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++)
    fails += run_case(cases[i], src, N);
  printf("\n%d/%zu filter cases diverge.\n", fails, sizeof(cases)/sizeof(cases[0]));
  return fails ? 1 : 0;
}
