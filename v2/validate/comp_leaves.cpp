// Remaining isolatable leaf blocks: ENV, LFO, DIST, DCF, BOOST (tasks 2.2,2.4-2.7).
// COMP/REVERB/MODDEL are NOT here: they read the global synth instance buffers
// (SYN.vcebuf / SYN.aux1buf / channel delay bufs) and cannot be isolated with a
// standalone workspace — they need full-instance (fallback) testing. Documented
// in REPORT.md.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

extern "C" void __stdcall OutputDebugStringA(const char *) {}
#include "../synth_core.cpp"

extern "C" {
  void v2x_calcSR(int sr);
  // env
  void v2x_envInit(void*); void v2x_envSet(void*, const void*); void v2x_envTick(void*);
  extern unsigned int v2x_size_syWEnv, v2x_off_syWEnv_state, v2x_off_syWEnv_out;
  // lfo
  void v2x_lfoInit(void*); void v2x_lfoSet(void*, const void*);
  void v2x_lfoKeyOn(void*); void v2x_lfoTick(void*);
  extern unsigned int v2x_size_syWLFO, v2x_off_syWLFO_out;
  // dist / dcf / boost
  void v2x_distInit(void*); void v2x_distSet(void*, const void*);
  void v2x_distRenderMono(void*, float*, const float*, int);
  extern unsigned int v2x_size_syWDist;
  void v2x_dcfInit(void*); void v2x_dcfRenderMono(void*, float*, const float*, int);
  extern unsigned int v2x_size_syWDCF;
  void v2x_boostInit(void*); void v2x_boostSet(void*, const void*);
  void v2x_boostRender(void*, float*, int);
  extern unsigned int v2x_size_syWBoost, v2x_off_syWBoost_a1; // a1,a2,b0,b1,b2
}

static const float EPS = 1e-4f;
static V2Instance g_inst;

static void noise(std::vector<float> &v, int n, unsigned seed=0x12345678u)
{
  v.resize(n);
  for (int i=0;i<n;i++){ seed=seed*1664525u+1013904223u; v[i]=((int)(seed>>8)&0xffff)/32768.0f-1.0f; }
}

static int report(const char *name, const float *a, const float *b, int n)
{
  float maxerr=0,peak=0; int first=-1;
  for(int i=0;i<n;i++){ float d=fabsf(a[i]-b[i]); if(d>maxerr)maxerr=d; if(fabsf(b[i])>peak)peak=fabsf(b[i]); if(d>EPS&&first<0)first=i; }
  bool ok=first<0;
  printf("  %-16s peak=%.4f maxerr=%.3g  %s", name, peak, maxerr, ok?"MATCH":"DIVERGE");
  if(!ok) printf(" @ %d (cpp=%.6g asm=%.6g)", first, a[first], b[first]);
  printf("\n");
  return ok?0:1;
}

// ---- ENV: tick attack/decay/sustain (gate held on), compare out per tick ----
static int test_env(int T)
{
  syVEnv p; p.ar=80; p.dr=80; p.sl=100; p.sr=64; p.rr=64; p.vol=128;
  std::vector<float> c(T), a(T);

  V2Env e; e.init(&g_inst); e.set(&p); e.val=0.0f; e.state=V2Env::ATTACK;
  for(int i=0;i<T;i++){ e.tick(true); c[i]=e.out; }

  std::vector<unsigned char> W(v2x_size_syWEnv,0);
  v2x_envInit(W.data()); v2x_envSet(W.data(), &p);
  *(int*)(W.data()+v2x_off_syWEnv_state)=1; // asm ATTACK=1
  for(int i=0;i<T;i++){ v2x_envTick(W.data()); a[i]=*(float*)(W.data()+v2x_off_syWEnv_out); }
  return report("env atk/dec/sus", c.data(), a.data(), T);
}

// ---- LFO: deterministic modes (saw/tri/pulse/sin), sync=0, compare out/tick ----
static int test_lfo(int T)
{
  const char *nm[]={"lfo saw","lfo tri","lfo pulse","lfo sin"};
  int fails=0;
  for(int m=0;m<4;m++){
    syVLFO p; p.mode=(float)m; p.sync=0; p.egmode=0; p.rate=64; p.phase=0; p.pol=0; p.amp=1;
    std::vector<float> c(T), a(T);
    V2LFO l; l.init(&g_inst); l.set(&p); l.keyOn();
    for(int i=0;i<T;i++){ l.tick(); c[i]=l.out; }
    std::vector<unsigned char> W(v2x_size_syWLFO,0);
    v2x_lfoInit(W.data()); v2x_lfoSet(W.data(), &p); v2x_lfoKeyOn(W.data());
    for(int i=0;i<T;i++){ v2x_lfoTick(W.data()); a[i]=*(float*)(W.data()+v2x_off_syWLFO_out); }
    fails += report(nm[m], c.data(), a.data(), T);
  }
  return fails;
}

// ---- DIST: a few modes, mono render ----
static int test_dist(const std::vector<float>&src,int N)
{
  int fails=0;
  for(int m=0;m<4;m++){
    syVDist p; p.mode=(float)m; p.ingain=72; p.param1=64; p.param2=64;
    std::vector<float> c(N,0), a(N,0);
    V2Dist d; d.init(&g_inst); d.set(&p); d.renderMono(c.data(), src.data(), N);
    std::vector<unsigned char> W(v2x_size_syWDist,0);
    v2x_distInit(W.data()); v2x_distSet(W.data(), &p);
    v2x_distRenderMono(W.data(), a.data(), src.data(), N);
    char nm[24]; snprintf(nm,sizeof nm,"dist mode%d",m);
    fails += report(nm, c.data(), a.data(), N);
  }
  return fails;
}

// ---- DCF: no params, mono render ----
static int test_dcf(const std::vector<float>&src,int N)
{
  std::vector<float> c(N,0), a(N,0);
  V2DCFilter d; d.init(&g_inst); d.renderMono(c.data(), src.data(), N);
  std::vector<unsigned char> W(v2x_size_syWDCF,0);
  v2x_dcfInit(W.data()); v2x_dcfRenderMono(W.data(), a.data(), src.data(), N);
  return report("dcf", c.data(), a.data(), N);
}

// ---- BOOST: stereo in-place render — bit-exact sweep over ALL amounts ----
// A single amount can pass by luck: the boost set-path rounding is data-
// dependent (the a0 association bug only fired at amount 92 — pzero's 9.677s
// patch; the sqrt(2A) beta simplification differed on 21/127 amounts). So
// sweep the full domain of the set input at eps=0; tolerance- or single-point
// oracles HIDE 1-ULP set-path bugs that shock the biquad state in context.
static int test_boost(int N)
{
  std::vector<float> src; noise(src, 2*N, 0xC0FFEEu); // interleaved L,R
  int bad=0, firstbad=-1;
  for (int amt=1; amt<128; amt++) {
    syVBoost p; p.amount=(float)amt;
    std::vector<float> c(src), a(src);
    // V2Boost::init does NOT zero its IIR state (the real synth relies on the
    // instance being zero-allocated); the asm syBoostInit zeros it. Mirror that.
    V2Boost b; memset(&b, 0, sizeof b); b.init(&g_inst); b.set(&p);
    b.render((StereoSample*)c.data(), N);
    std::vector<unsigned char> W(v2x_size_syWBoost,0);
    v2x_boostInit(W.data()); v2x_boostSet(W.data(), &p);
    v2x_boostRender(W.data(), a.data(), N);
    // coeff-level check: attributes a divergence to the SET path (coeffs
    // differ) vs the RENDER path (coeffs equal, output differs).
    const float *acf = (const float*)(W.data()+v2x_off_syWBoost_a1); // a1,a2,b0,b1,b2
    float ccf[5] = { b.a1, b.a2, b.b0, b.b1, b.b2 };
    int cbad = memcmp(ccf, acf, sizeof ccf) != 0;
    if (memcmp(c.data(), a.data(), 2*N*sizeof(float)) != 0 || cbad) {
      bad++; if (firstbad<0) firstbad=amt;
      int fi=-1; for (int i=0;i<2*N;i++) if (c[i]!=a[i]) { fi=i; break; }
      union { float f; unsigned u; } uc, ua; uc.f=fi>=0?c[fi]:0; ua.f=fi>=0?a[fi]:0;
      printf("  boost amount %3d: DIVERGES (eps=0) first@%d cpp=%08x asm=%08x coeffs=%s\n",
             amt, fi, uc.u, ua.u, cbad?"DIFFER":"equal");
      if (cbad) {
        const char *cn[5]={"a1","a2","b0","b1","b2"};
        for (int k=0;k<5;k++) if (ccf[k]!=acf[k]) {
          union { float f; unsigned u; } x,y; x.f=ccf[k]; y.f=acf[k];
          printf("    %s cpp=%08x asm=%08x\n", cn[k], x.u, y.u);
        }
      }
    }
  }
  printf("  %-16s maxerr=%s  %s (127 amounts, eps=0)\n", "boost sweep",
         bad? ">0" : "0", bad? "DIVERGE" : "MATCH");
  return bad?1:0;
}

int main()
{
  // volatile: keep sr a RUNTIME value. With a literal 44100, gcc const-folds
  // calcNewSampleRate's coefficient chain at compile time in full (MPFR)
  // precision -- SRfcBoostSin comes out 1 ULP off the asm's runtime x87 PC=24
  // value (3caf0f9f vs 3caf0f9e), which made the bit-exact boost sweep flag 12
  // amounts that are fine in the real harness (where sr arrives at runtime).
  volatile int sr = 44100;
  g_inst.calcNewSampleRate(sr);
  v2x_calcSR(sr);
  const int N=512, T=200;
  std::vector<float> src; noise(src, N);

  int fails=0;
  printf("Leaf-block equivalence (isolatable leaves only):\n");
  fails += test_env(T);
  fails += test_lfo(T);
  fails += test_dist(src, N);
  fails += test_dcf(src, N);
  fails += test_boost(N);
  printf("\n%d leaf case(s) diverge.\n", fails);
  printf("(COMP/REVERB/MODDEL excluded: instance-entangled, need fallback testing.)\n");
  return fails?1:0;
}
