// OSC per-component equivalence test — the trampoline/ABI proof (tasks 1.3-1.5, 2.1).
//
// C++ side: include synth_core.cpp directly to reach V2Osc/V2Instance/syVOsc
// (they have no header). asm side: drive syOscSet/syOscRender via the cdecl
// trampolines, with SR globals established by calcNewSampleRate.
//
// Links against the RAW synth_asm.o: its decorated `_synthInit@12` coexists with
// synth_core.cpp's undecorated `synthInit`, so there is no symbol clash.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

// dead-debug stub (synth_core.cpp declares it; optimizer usually drops it)
extern "C" void __stdcall OutputDebugStringA(const char *) {}

#include "../synth_core.cpp"   // V2Instance, V2Osc, syVOsc (compiled with compat.h, c++03, -m32)

extern "C" {
  void v2x_calcSR(int sr);
  void v2x_oscInit(void *W, int idx);
  void v2x_oscSet(void *W, const void *V);
  void v2x_oscRender(void *W, float *dst, const float *src, int n);
  extern unsigned int v2x_size_syWOsc;
  extern unsigned int v2x_off_syWOsc_note;
  extern float SRfcobasefrq, SRfclinfreq;   // asm SR globals
}

static const float EPS = 1e-4f;

// One OSC test case: a parameter preset + note.
struct OscCase { const char *name; float mode, ring, pitch, detune, color, gain, note; };

static int run_case(const OscCase &c, int N)
{
  // ---- shared parameter blob (identical bytes to both cores) ----
  syVOsc para;
  para.mode = c.mode; para.ring = c.ring; para.pitch = c.pitch;
  para.detune = c.detune; para.color = c.color; para.gain = c.gain;

  // ---- C++ core ----
  static V2Instance inst;            // static: large (buffers); keep off stack
  inst.calcNewSampleRate(44100);
  V2Osc osc;
  osc.init(&inst, 0);
  osc.note = c.note;
  osc.set(&para);
  std::vector<float> cbuf(N, 0.0f);
  osc.render(cbuf.data(), N);

  // ---- asm core ----
  v2x_calcSR(44100);
  std::vector<unsigned char> W(v2x_size_syWOsc, 0);
  v2x_oscInit(W.data(), 0);
  // note lives in the workspace; write it at the exported offset (default 0 here)
  *(float *)(W.data() + v2x_off_syWOsc_note) = c.note;
  v2x_oscSet(W.data(), &para);
  std::vector<float> abuf(N, 0.0f);
  v2x_oscRender(W.data(), abuf.data(), 0, N);

  // ---- diff ----
  float maxerr = 0.0f, peaka = 0.0f; int first = -1;
  for (int i = 0; i < N; i++) {
    float d = fabsf(cbuf[i] - abuf[i]);
    if (d > maxerr) maxerr = d;
    if (fabsf(abuf[i]) > peaka) peaka = fabsf(abuf[i]);
    if (d > EPS && first < 0) first = i;
  }
  bool ok = first < 0;
  printf("  %-14s peak=%.4f maxerr=%.3g  %s",
         c.name, peaka, maxerr, ok ? "MATCH" : "DIVERGE");
  if (!ok) printf(" @ sample %d (cpp=%.6g asm=%.6g)", first, cbuf[first], abuf[first]);
  printf("\n");
  if (!ok && getenv("DUMP")) {
    for (int i = 0; i < 24; i++)
      printf("    [%2d] cpp=%+.8f asm=%+.8f  d=%.2e\n", i, cbuf[i], abuf[i], fabsf(cbuf[i]-abuf[i]));
  }
  return ok ? 0 : 1;
}

int main()
{
  const int N = 256;

  // SR-state sanity: assert the two cores compute the same SR constants first.
  static V2Instance probe; probe.calcNewSampleRate(44100);
  v2x_calcSR(44100);
  printf("SR constants: obasefrq cpp=%.9g asm=%.9g | linfreq cpp=%.9g asm=%.9g\n",
         probe.SRfcobasefrq, SRfcobasefrq, probe.SRfclinfreq, SRfclinfreq);
  bool srok = fabsf(probe.SRfcobasefrq - SRfcobasefrq) < 1e-3f
           && fabsf(probe.SRfclinfreq - SRfclinfreq) < 1e-9f;
  printf("SR match: %s\n\n", srok ? "yes" : "NO -- block tests below are meaningless until fixed");

  OscCase cases[] = {
    { "tri/saw",  1, 0, 64, 64, 64, 128, 0 },
    { "pulse",    2, 0, 64, 64, 64, 128, 0 },
    { "sin",      3, 0, 64, 64, 64, 128, 0 },
    { "noise",    4, 0, 64, 64, 64, 128, 0 },
  };
  printf("OSC equivalence (N=%d samples):\n", N);
  int fails = 0;
  for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++)
    fails += run_case(cases[i], N);

  printf("\n%d/%zu osc cases diverge.\n", fails, sizeof(cases)/sizeof(cases[0]));
  return fails ? 1 : 0;
}
