// Minimal single-call probe: set up note=10 color=0 osc and call v2x_oscSet once,
// so gdb can break inside syOscChgPitch and read st0 right before fistp.
#include <cstdio>
#include <cstring>
#include <vector>
extern "C" void __stdcall OutputDebugStringA(const char*){}
#include "../synth_core.cpp"
extern "C" {
  void v2x_calcSR(int);
  void v2x_oscInit(void*,int);
  void v2x_oscSet(void*,const void*);
  extern unsigned int v2x_size_syWOsc, v2x_off_syWOsc_note, v2x_off_syWOsc_freq;
}
static V2Instance g;
int main(){
  g.calcNewSampleRate(44100); v2x_calcSR(44100);
  syVOsc p; p.mode=1;p.ring=0;p.pitch=64;p.detune=64;p.color=0;p.gain=106;
  std::vector<unsigned char> W(v2x_size_syWOsc,0);
  v2x_oscInit(W.data(),0);
  *(float*)(W.data()+v2x_off_syWOsc_note)=10.0f;
  v2x_oscSet(W.data(),&p);
  printf("asm freq=%d\n", *(int*)(W.data()+v2x_off_syWOsc_freq));

  V2Osc osc; osc.init(&g,0); osc.note=10.0f; osc.set(&p);
  printf("cpp freq=%d  (arg printed via gdb)\n", osc.freq);
  return 0;
}
