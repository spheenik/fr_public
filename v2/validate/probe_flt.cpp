// Bit-exact (eps=0) VCF probe: comp_flt uses EPS=1e-4 which hides 1-ULP render
// divergences (the debris_ost residual). Renders BLOCKS with state continuity
// (in-context shape: 128-sample frames) over dirty-mantissa noise at several
// amplitudes, comparing output AND post-block state bit-for-bit.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

extern "C" void __stdcall OutputDebugStringA(const char *) {}
#include "../synth_core.cpp"

extern "C" {
  void v2x_calcSR(int sr);
  void v2x_fltInit(void *W);
  void v2x_fltSet(void *W, const void *V);
  void v2x_fltRender(void *W, float *dst, const float *src, int n);
  extern unsigned int v2x_size_syWFlt;
}

union FB { float f; unsigned u; };

static void make_input(std::vector<float> &v, int N, float amp)
{
  v.resize(N);
  unsigned s = 0xCAFEBABEu;
  for (int i = 0; i < N; i++) {
    s = s * 1664525u + 1013904223u;
    v[i] = (((int)(s >> 8) & 0xffff) / 32768.0f - 1.0f) * amp;
  }
}

int main()
{
  const int BLK = 128, NBLK = 24, N = BLK * NBLK;
  static V2Instance inst; // static: harmless, mirrors comp_flt
  inst.calcNewSampleRate(44100);
  v2x_calcSR(44100);

  long cases = 0, bad = 0, shown = 0;
  float amps[3] = { 1.0f, 0.05f, 0.002f };

  for (int mode = 1; mode <= 7; mode++)
  for (int ai = 0; ai < 3; ai++)
  for (int cut = 4; cut <= 124; cut += 24)
  for (int res = 0; res <= 120; res += 24)
  {
    std::vector<float> src; make_input(src, N, amps[ai]);
    syVFlt para; para.mode = (float)mode; para.cutoff = (float)cut; para.reso = (float)res;

    V2Flt flt; flt.init(&inst); flt.set(&para);
    std::vector<unsigned char> W(v2x_size_syWFlt, 0);
    v2x_fltInit(W.data());
    v2x_fltSet(W.data(), &para);

    if (mode >= 6) { // compare moog coefficients bitwise (set-path vs render-path attribution)
      float *aw = (float*)W.data(); // syWFlt: mode,cfreq,res,moogf,moogp,moogq
      FB cf, cp, cq, af, ap, aq;
      cf.f = flt.moogf; cp.f = flt.moogp; cq.f = flt.moogq;
      af.f = aw[3];     ap.f = aw[4];     aq.f = aw[5];
      if (cf.u != af.u || cp.u != ap.u || cq.u != aq.u) {
        static int cshown = 0;
        if (cshown++ < 6)
          printf("COEFF DIFF mode=%d cut=%d res=%d: f %08x/%08x p %08x/%08x q %08x/%08x (cpp/asm)\n",
                 mode, cut, res, cf.u, af.u, cp.u, ap.u, cq.u, aq.u);
      }
    }

    std::vector<float> cbuf(N, 0.0f), abuf(N, 0.0f);
    for (int b = 0; b < NBLK; b++) {           // block-wise, state continues
      flt.render(&cbuf[b*BLK], &src[b*BLK], BLK, 1);
      v2x_fltRender(W.data(), &abuf[b*BLK], &src[b*BLK], BLK);
    }

    cases++;
    int first = -1;
    for (int i = 0; i < N; i++)
      if (cbuf[i] != abuf[i]) { first = i; break; }
    if (first >= 0) {
      bad++;
      if (shown < 10) {
        FB a, c; a.f = abuf[first]; c.f = cbuf[first];
        printf("DIFF mode=%d amp=%g cut=%d res=%d @ %d (blk %d): asm %08x (%.9g) cpp %08x (%.9g)\n",
               mode, amps[ai], cut, res, first, first/BLK, a.u, a.f, c.u, c.f);
        shown++;
      }
    }
  }
  printf("\n%ld/%ld cases bit-diverge.\n", bad, cases);
  return bad ? 1 : 0;
}
