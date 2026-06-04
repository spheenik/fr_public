// Exact-state replay of the debris_ost noise divergence (tick 69978, voice 0):
// inject the BIT-IDENTICAL pre-tick state captured from both cores' ledgers and
// render one 128-sample block. Expected: asm vce[1]=3e3ec985 cpp vce[1]=3e3ec986.
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" void __stdcall OutputDebugStringA(const char *) {}
#include "../synth_core.cpp"

extern "C" {
  void v2x_calcSR(int sr);
  void v2x_oscInit(void *W, int idx);
  void v2x_oscRender(void *W, float *dst, const float *src, int n);
  extern unsigned int v2x_size_syWOsc;
}

union FB { float f; unsigned u; };
static sF32 uf(unsigned u) { FB x; x.u = u; return x.f; }

// syWOsc dword offsets (synth.asm struc): mode ring cnt freq brpt nffrq nfres
// nseed gain tmp nfl nfb note pitch
enum { W_MODE, W_RING, W_CNT, W_FREQ, W_BRPT, W_NFFRQ, W_NFRES, W_NSEED, W_GAIN, W_TMP, W_NFL, W_NFB };

int main()
{
  static V2Instance inst;
  inst.calcNewSampleRate(44100);
  v2x_calcSR(44100);

  // captured pre-tick state (bit-identical in both cores)
  const unsigned NSEED = 0xfa2bb1de, NFL = 0x3eb3048a, NFB = 0xbd60531f;
  const unsigned NFFRQ = 0x3cfdd7d2, NFRES = 0x3de4c738, GAIN = 0x3d000000;
  const int N = 128;

  // ---- C++ core: poke fields directly ----
  V2Osc osc; osc.init(&inst, 0);
  osc.mode = 4; osc.ring = false;
  osc.nffrq = uf(NFFRQ); osc.nfres = uf(NFRES);
  osc.nseed = NSEED; osc.gain = uf(GAIN);
  osc.nf.l = uf(NFL); osc.nf.b = uf(NFB);
  std::vector<float> cbuf(N, 0.0f);
  osc.render(cbuf.data(), N);

  // ---- asm core ----
  std::vector<unsigned char> Wb(v2x_size_syWOsc, 0);
  v2x_oscInit(Wb.data(), 0);
  unsigned *W = (unsigned *)Wb.data();
  W[W_MODE] = 4; W[W_RING] = 0;
  W[W_NFFRQ] = NFFRQ; W[W_NFRES] = NFRES;
  W[W_NSEED] = NSEED; W[W_GAIN] = GAIN;
  W[W_NFL] = NFL; W[W_NFB] = NFB;
  std::vector<float> abuf(N, 0.0f);
  v2x_oscRender(Wb.data(), abuf.data(), NULL, N);

  int diffs = 0;
  for (int i = 0; i < N; i++) {
    FB a, c; a.f = abuf[i]; c.f = cbuf[i];
    if (a.u != c.u) {
      if (diffs < 6)
        printf("DIFF @ %3d: asm %08x (%.9g)  cpp %08x (%.9g)\n", i, a.u, a.f, c.u, c.f);
      diffs++;
    }
  }
  FB a0, c0, a1, c1; a0.f=abuf[0]; c0.f=cbuf[0]; a1.f=abuf[1]; c1.f=cbuf[1];
  printf("sample0: asm %08x cpp %08x   sample1: asm %08x cpp %08x\n", a0.u, c0.u, a1.u, c1.u);
  printf("%d/%d samples differ. %s\n", diffs, N,
         diffs ? "REPRODUCED" : "NOT reproduced (difference needs more context)");
  return 0;
}
