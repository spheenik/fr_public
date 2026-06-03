// comp_lfo.cpp — BIT-EXACT LFO oracle.
//
// The general comp_leaves LFO test uses amp=1 and a 1e-4 tolerance, which masks
// ULP-level divergences. But the in-song failure mode is phase drift: a SIN-mode
// LFO modulates osc pitch, and a 1-ULP error in the LFO output can flip the osc
// freq (fistp boundary), then accumulate as phase drift over the whole song
// (showing up as growing spikes at saw edges). A tolerance-based test
// fundamentally cannot catch a 1-ULP-accumulates bug, so this test compares the
// LFO output to the asm BIT-EXACT, using pzero's actual LFO configs (amp up to
// 127, pol=2, various rates), over many ticks to sweep the full phase range.
//
// Built the same way as the other comp_* oracles (links the raw asm object +
// tramp.asm v2x_* trampolines).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

extern "C" void __stdcall OutputDebugStringA(const char *) {}
#include "../synth_core.cpp"

extern "C" {
  void v2x_calcSR(int sr);
  void v2x_lfoInit(void*); void v2x_lfoSet(void*, const void*);
  void v2x_lfoKeyOn(void*); void v2x_lfoTick(void*);
  extern unsigned int v2x_size_syWLFO, v2x_off_syWLFO_out;
  float v2x_fastsin(float);
  float v2x_fastsinrc(float);
}

// fastsin/fastsinrc are static in synth_core.cpp (included above) -- callable here.
static int sweep_sin(const char *name, float lo, float hi, int N, bool rc)
{
  int first=-1; float maxerr=0; float xfirst=0;
  for(int i=0;i<N;i++){
    float x=lo+(hi-lo)*(i/(float)(N-1));
    float cpp = rc ? fastsinrc(x) : fastsin(x);
    float asmv= rc ? v2x_fastsinrc(x) : v2x_fastsin(x);
    float d=fabsf(cpp-asmv); if(d>maxerr)maxerr=d;
    if(cpp!=asmv && first<0){ first=i; xfirst=x; }
  }
  bool ok=(first<0);
  printf("  %-26s N=%d maxerr=%.3g  %s", name, N, maxerr, ok?"EXACT":"DIVERGE");
  if(!ok) printf("  @x=%.9g", xfirst);
  printf("\n");
  return ok?0:1;
}

static V2Instance g_inst;

struct Cfg { int mode, sync, eg, rate, pol, amp, phase; const char *name; };

static int run(const Cfg &cfg, int T)
{
  syVLFO p;
  p.mode=(float)cfg.mode; p.sync=(float)cfg.sync; p.egmode=(float)cfg.eg;
  p.rate=(float)cfg.rate; p.phase=(float)cfg.phase;
  p.pol=(float)cfg.pol;   p.amp=(float)cfg.amp;

  std::vector<float> c(T), a(T);

  V2LFO l; l.init(&g_inst);
  l.nseed = 0x12345678u; // pin the S&H seed so it isn't rand()-dependent
  l.set(&p); l.keyOn();
  for(int i=0;i<T;i++){ l.tick(); c[i]=l.out; }

  std::vector<unsigned char> W(v2x_size_syWLFO,0);
  v2x_lfoInit(W.data());
  // mirror the pinned seed into the asm workspace if its layout exposes it; the
  // asm seed field follows the same struct, but for sin/tri/saw it's unused.
  v2x_lfoSet(W.data(), &p); v2x_lfoKeyOn(W.data());
  for(int i=0;i<T;i++){ v2x_lfoTick(W.data()); a[i]=*(float*)(W.data()+v2x_off_syWLFO_out); }

  int first=-1; float maxerr=0;
  for(int i=0;i<T;i++){
    float d=fabsf(c[i]-a[i]); if(d>maxerr)maxerr=d;
    if(c[i]!=a[i] && first<0) first=i; // EXACT (bit-for-bit) comparison
  }
  bool ok=(first<0);
  printf("  %-26s ticks=%d maxerr=%.3g  %s", cfg.name, T, maxerr, ok?"EXACT":"DIVERGE");
  if(!ok){
    unsigned cb,ab; memcpy(&cb,&c[first],4); memcpy(&ab,&a[first],4);
    printf("  @tick %d cpp=%.9g asm=%.9g (cpp=%08x asm=%08x)", first, c[first], a[first], cb, ab);
  }
  printf("\n");
  return ok?0:1;
}

int main()
{
  g_inst.calcNewSampleRate(44100);
  v2x_calcSR(44100);
  const int T=4096;
  int fails=0;
  printf("LFO bit-exact equivalence (pzero configs + high-amp sweep):\n");

  // pzero's actual LFO configs (captured via LFODUMP), then all modes at amp=127.
  static const Cfg cfgs[] = {
    {3,0,0, 0, 0, 98, 0,  "sin rate=0 amp=98 (pzero)"},
    {3,0,0,14, 2, 49, 61, "sin rate=14 pol=2 amp=49 (pzero)"},
    {3,0,0,27, 2, 127,0,  "sin rate=27 pol=2 amp=127 (pzero)"},
    {0,0,0,30, 0, 127,0,  "saw rate=30 amp=127"},
    {1,0,0,30, 0, 127,0,  "tri rate=30 amp=127"},
    {2,0,0,30, 0, 127,0,  "pulse rate=30 amp=127"},
    {3,0,0,30, 0, 127,0,  "sin rate=30 amp=127"},
    {3,0,0,30, 1, 127,0,  "sin rate=30 pol=1 amp=127"},
    {3,0,0,30, 2, 127,0,  "sin rate=30 pol=2 amp=127"},
  };
  for(unsigned i=0;i<sizeof(cfgs)/sizeof(cfgs[0]);i++)
    fails += run(cfgs[i], T);

  printf("\nDirect sine-kernel sweep (isolates fastsin vs fastsinrc):\n");
  fails += sweep_sin("fastsin [-pi/2,pi/2]", -1.5707963f, 1.5707963f, 20000, false);
  fails += sweep_sin("fastsinrc [0,2pi)",     0.0f,       6.2831853f, 20000, true);

  printf("\n%d LFO config(s) diverge (bit-exact).\n", fails);
  return fails?1:0;
}
