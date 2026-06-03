// comp_osc_exact.cpp — BIT-EXACT oscillator oracle, swept over notes (freqs).
//
// comp_osc tests color=64 / note=0 / eps=1e-4. But pzero's saw oscillators run at
// color=0 (brpt=0, the "pure saw-down" edge the handover flagged), at real notes,
// and the in-song failure is a 1-ULP-accumulates phase/edge divergence that a
// tolerance test can't see. This oracle drives the osc with pzero's actual osc
// configs across a sweep of notes, and compares the rendered output BIT-EXACT
// (the only kind of comparison that catches drift), over enough samples to hit
// the box-filter "hard" cases at the saw discontinuities.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

extern "C" void __stdcall OutputDebugStringA(const char *) {}
#include "../synth_core.cpp"

extern "C" {
  void v2x_calcSR(int sr);
  void v2x_oscInit(void *W, int idx);
  void v2x_oscSet(void *W, const void *V);
  void v2x_oscRender(void *W, float *dst, const float *src, int n);
  extern unsigned int v2x_size_syWOsc;
  extern unsigned int v2x_off_syWOsc_note;
  extern unsigned int v2x_off_syWOsc_freq;
  extern unsigned int v2x_off_syWOsc_brpt;
  extern float SRfcobasefrq, SRfclinfreq;  // asm SR globals
  float v2x_pow2(float);
}

static V2Instance g_inst;

struct OscCfg { float mode, ring, pitch, detune, color, gain; const char *name; };

// Compare the osc SETUP integers (freq/brpt) C++ vs asm across notes. A freq
// mismatch means a phase-drift source upstream of the waveform formulas.
static int check_setup(const OscCfg &c)
{
  int freqdiff=0, brptdiff=0; int firstn=-1; int cf=0,af=0;
  for(int n=0;n<=120;n++){
    syVOsc para; para.mode=c.mode; para.ring=c.ring; para.pitch=c.pitch;
    para.detune=c.detune; para.color=c.color; para.gain=c.gain;
    V2Osc osc; osc.init(&g_inst,0); osc.note=(float)n; osc.set(&para);
    std::vector<unsigned char> W(v2x_size_syWOsc,0);
    v2x_oscInit(W.data(),0); *(float*)(W.data()+v2x_off_syWOsc_note)=(float)n; v2x_oscSet(W.data(),&para);
    int af_=*(int*)(W.data()+v2x_off_syWOsc_freq);
    unsigned ab_=*(unsigned*)(W.data()+v2x_off_syWOsc_brpt);
    if(osc.freq!=af_){ freqdiff++; if(firstn<0){firstn=n;cf=osc.freq;af=af_;} }
    if(osc.brpt!=(int)ab_) brptdiff++;
  }
  printf("  %-22s freq mismatches=%d brpt mismatches=%d", c.name, freqdiff, brptdiff);
  if(firstn>=0) printf("  (first note=%d cpp.freq=%d asm.freq=%d d=%d)", firstn, cf, af, cf-af);
  printf("\n");
  return freqdiff+brptdiff;
}

// Render `note` through both cores bit-exact; return first diverging sample (-1 ok).
static int run_note(const OscCfg &c, float note, int N, float &maxerr, float &cv, float &av)
{
  syVOsc para;
  para.mode=c.mode; para.ring=c.ring; para.pitch=c.pitch;
  para.detune=c.detune; para.color=c.color; para.gain=c.gain;

  V2Osc osc; osc.init(&g_inst, 0); osc.note=note; osc.set(&para);
  std::vector<float> cb(N,0.0f); osc.render(cb.data(), N);

  std::vector<unsigned char> W(v2x_size_syWOsc,0);
  v2x_oscInit(W.data(),0);
  *(float*)(W.data()+v2x_off_syWOsc_note)=note;
  v2x_oscSet(W.data(), &para);
  std::vector<float> ab(N,0.0f); v2x_oscRender(W.data(), ab.data(), 0, N);

  int first=-1; maxerr=0;
  for(int i=0;i<N;i++){
    float d=fabsf(cb[i]-ab[i]); if(d>maxerr)maxerr=d;
    if(cb[i]!=ab[i] && first<0){ first=i; cv=cb[i]; av=ab[i]; }
  }
  return first;
}

static int run_cfg(const OscCfg &c, int N)
{
  int worstNote=-1, worstFirst=-1; float worstMax=0, wc=0, wa=0;
  int divergeCount=0;
  for(int n=0;n<=120;n++){
    float me; float cv=0,av=0;
    int first=run_note(c, (float)n, N, me, cv, av);
    if(first>=0){ divergeCount++; if(me>worstMax){ worstMax=me; worstNote=n; worstFirst=first; wc=cv; wa=av; } }
  }
  bool ok=(divergeCount==0);
  printf("  %-22s %s", c.name, ok?"EXACT (all notes)":"DIVERGE");
  if(!ok){
    unsigned cb,ab; memcpy(&cb,&wc,4); memcpy(&ab,&wa,4);
    printf("  %d/121 notes; worst note=%d @samp %d maxerr=%.3g cpp=%.9g asm=%.9g (%08x/%08x)",
           divergeCount, worstNote, worstFirst, worstMax, wc, wa, cb, ab);
  }
  printf("\n");
  return ok?0:1;
}

int main()
{
  g_inst.calcNewSampleRate(44100);
  v2x_calcSR(44100);
  const int N=1024;
  int fails=0;
  { unsigned short cw=0; __asm__ volatile("fnstcw %0":"=m"(cw));
    printf("x87 control word = 0x%04x (PC=%d RC=%d)\n", cw, (cw>>8)&3, (cw>>10)&3); }
  { unsigned cb,ab; memcpy(&cb,&g_inst.SRfcobasefrq,4); memcpy(&ab,&SRfcobasefrq,4);
    unsigned cl,al; memcpy(&cl,&g_inst.SRfclinfreq,4); memcpy(&al,&SRfclinfreq,4);
    printf("SRfcobasefrq cpp=%.9g asm=%.9g (%08x/%08x %s) | SRfclinfreq (%08x/%08x %s)\n",
      g_inst.SRfcobasefrq, SRfcobasefrq, cb, ab, cb==ab?"EXACT":"DIFF", cl, al, cl==al?"EXACT":"DIFF"); }
  // direct pow2 sweep over the osc-freq argument range (pitch+note-60)*fci12
  { int pd=0; float wx=0,wc=0,wa=0;
    for(int n=0;n<=120;n++){
      float arg=(0.0f + (float)n - 60.0f) * 0.083333333333f;
      float cp=v2_pow2(arg), ap=v2x_pow2(arg);
      if(cp!=ap){ pd++; if(pd==1){wx=arg;wc=cp;wa=ap;} }
    }
    printf("v2_pow2 vs asm pow2 over osc args: %d/121 differ", pd);
    if(pd){ unsigned a,b; memcpy(&a,&wc,4);memcpy(&b,&wa,4); printf(" (first arg=%.6g cpp=%.9g asm=%.9g %08x/%08x)",wx,wc,wa,a,b);}
    printf("\n");
  }
  // step-by-step freq for note=10 (the first mismatch), to localize within chgPitch
  { float pitch=0.0f, note=10.0f;
    float arg=(pitch + note - 60.0f) * 0.083333333333f;
    float p_cpp=v2_pow2(arg), p_asm=v2x_pow2(arg);
    float prod_cpp = g_inst.SRfcobasefrq * p_cpp;
    float prod_asmpow = g_inst.SRfcobasefrq * p_asm;
    int f_cpp = v2_fistp(prod_cpp);
    int f_asmpow = v2_fistp(prod_asmpow);
    unsigned a,b,c2; memcpy(&a,&p_cpp,4);memcpy(&b,&p_asm,4);memcpy(&c2,&prod_cpp,4);
    double exact = (double)g_inst.SRfcobasefrq * (double)p_cpp;
    printf("note10: arg=%.9g pow2 cpp=%08x asm=%08x %s | prod24=%.9g(%08x) EXACT(dbl)=%.9f fistp(cppPow)=%d fistp(asmPow)=%d\n",
           arg, a, b, a==b?"=":"DIFF", prod_cpp, c2, exact, f_cpp, f_asmpow);
  }
  printf("OSC bit-exact equivalence, swept over notes 0..120 (pzero configs):\n");

  // pzero's osc configs (from OSCDUMP): tri/saw at low color (brpt near 0), sin.
  static const OscCfg cfgs[] = {
    {1,0,64,64,  0,106, "tri/saw color=0 (saw)"},   // brpt=0 pure saw-down
    {1,0,64,64,  1,124, "tri/saw color=1"},
    {1,0,64,74,  6,124, "tri/saw color=6 detune=74"},
    {1,0,64,50,110, 54, "tri/saw color=110 detune=50"},
    {1,0,64,56,  1,124, "tri/saw color=1 detune=56"},
    {1,0,64,64, 32,126, "tri/saw color=32"},
    {2,0,64,64, 64,127, "pulse color=64"},
    {3,0,64,64, 63, 47, "sin color=63"},
  };
  for(unsigned i=0;i<sizeof(cfgs)/sizeof(cfgs[0]);i++)
    fails += run_cfg(cfgs[i], N);

  printf("\nSetup-integer check (freq/brpt per note -- a freq mismatch = phase drift):\n");
  for(unsigned i=0;i<sizeof(cfgs)/sizeof(cfgs[0]);i++)
    check_setup(cfgs[i]);

  printf("\n%d osc config(s) diverge (bit-exact).\n", fails);
  return fails?1:0;
}
