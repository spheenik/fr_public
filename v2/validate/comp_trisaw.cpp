// Characterize the tri/saw startup divergence across many note/breakpoint
// conditions: where is the first diverging sample, and does asm[0]==asm[1]?
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
extern "C" void __stdcall OutputDebugStringA(const char *) {}
#include "../synth_core.cpp"
static inline sF32 SQ(sF32 x){return x*x;}
extern "C" {
  void v2x_calcSR(int sr);
  void v2x_oscInit(void*, int); void v2x_oscSet(void*, const void*);
  void v2x_oscRender(void*, float*, const float*, int);
  extern unsigned int v2x_size_syWOsc, v2x_off_syWOsc_note;
}

static void run(float note, float color, float detune)
{
  const int N = 8;
  syVOsc p; p.mode=1; p.ring=0; p.pitch=64; p.detune=detune; p.color=color; p.gain=128;

  static V2Instance inst; inst.calcNewSampleRate(44100);
  V2Osc o; o.init(&inst,0); o.note=note; o.set(&p);
  std::vector<float> c(N,0); o.render(c.data(), N);

  v2x_calcSR(44100);
  std::vector<unsigned char> W(v2x_size_syWOsc,0);
  v2x_oscInit(W.data(),0);
  *(float*)(W.data()+v2x_off_syWOsc_note)=note;
  v2x_oscSet(W.data(), &p);
  std::vector<float> a(N,0); v2x_oscRender(W.data(), a.data(), 0, N);

  int first=-1; for(int i=0;i<N;i++) if(fabsf(c[i]-a[i])>1e-4f){first=i;break;}
  bool a01 = fabsf(a[0]-a[1])<1e-7f;
  bool c01 = fabsf(c[0]-c[1])<1e-7f;
  printf("note=%3.0f col=%3.0f det=%3.0f | firstdiv=%2d | asm[0]==asm[1]:%d cpp[0]==cpp[1]:%d | cpp[0]=%+.6f asm[0]=%+.6f\n",
         note, color, detune, first, a01, c01, c[0], a[0]);
}



// Forensic: compute every case-formula at sample 0 (cnt=0) using the C++'s own
// intermediates, then see which matches cpp[0] (the C++'s chosen case) and which
// matches asm[0] (the ASM's actual case).
static void forensic(float note, float color)
{
  syVOsc pv; pv.mode=1; pv.ring=0; pv.pitch=64; pv.detune=64; pv.color=color; pv.gain=128;
  static V2Instance inst; inst.calcNewSampleRate(44100);
  V2Osc o; o.init(&inst,0); o.note=note; o.set(&pv);

  sF32 f = utof23(o.freq), omf = 1.0f - f, rcpf = 1.0f/f, col = utof23(o.brpt);
  sF32 g = o.gain, c1 = g/col, c2 = -g/(1.0f-col);
  sF32 p = utof23(0) - col;                 // cnt = 0
  sF32 G = g;                               // output adds +gain
  printf("note=%.0f col=%.0f  f=%.3e rcpf=%.1f p=%.5f\n", note, color, f, rcpf, p);
  printf("  case a (UP)        : %+.6f\n", c1*(p+p-f) + G);
  printf("  case c (DOWN)      : %+.6f\n", c2*(p+p-f) + G);
  printf("  case b (UP_DOWN)   : %+.6f\n", rcpf*(c2*SQ(p) - c1*SQ(p-f)) + G);
  printf("  case d (DOWN_UP)   : %+.6f\n", -rcpf*(g + c2*SQ(p+omf) - c1*SQ(p)) + G);
  printf("  case e (UP_DOWN_UP): %+.6f\n", -rcpf*(g + c1*omf*(p+p+omf)) + G);
  printf("  case f (DN_UP_DN)  : %+.6f\n", -rcpf*(g + c2*omf*(p+p+omf)) + G);
  // case d in genuine DOUBLE (no float-truncating sqr) — does it match asm[0]?
  {
    double fd=utof23(o.freq), omfd=1.0-fd, rcpfd=1.0/fd, cold=utof23(o.brpt);
    double gd=o.gain, c1d=gd/cold, c2d=-gd/(1.0-cold), pd=(double)(utof23(0))-cold;
    double yd = -rcpfd*(gd + c2d*(pd+omfd)*(pd+omfd) - c1d*pd*pd) + gd;
    printf("  case d DOUBLE      : %+.6f   <-- compare to asm[0]\n", (float)yd);
  }

  // actual outputs
  std::vector<float> c(2,0); o.cnt=0; { V2Osc o2; o2.init(&inst,0); o2.note=note; o2.set(&pv); o2.render(c.data(),2); }
  v2x_calcSR(44100);
  std::vector<unsigned char> W(v2x_size_syWOsc,0); v2x_oscInit(W.data(),0);
  *(float*)(W.data()+v2x_off_syWOsc_note)=note; v2x_oscSet(W.data(), &pv);
  std::vector<float> a(2,0); v2x_oscRender(W.data(), a.data(), 0, 2);
  printf("  >> cpp[0]=%+.6f  asm[0]=%+.6f\n\n", c[0], a[0]);
}

int main()
{
  float notes[] = {12.f, 36.f, 48.f, 60.f, 72.f};
  float cols[]  = {32.f, 64.f, 96.f};
  for (int i=0;i<5;i++) for (int j=0;j<3;j++) run(notes[i], cols[j], 64.f);
  printf("\n=== forensic (sample 0, all case formulas) ===\n");
  forensic(12, 32);
  forensic(12, 96);
  return 0;
}
