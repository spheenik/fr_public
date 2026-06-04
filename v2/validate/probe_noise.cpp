// Bit-exact (eps=0) NOISE-osc probe with block continuity: the debris_ost
// residual shows a 1-ULP output diff (state clean) in the noise osc render.
// Sweeps color (nf cutoff/reso) x gain over many continuing blocks of varying
// split sizes, comparing every sample bit-for-bit.
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
}

union FB { float f; unsigned u; };

int main()
{
  static V2Instance inst;
  inst.calcNewSampleRate(44100);
  v2x_calcSR(44100);

  const int N = 4096;
  long cases = 0, bad = 0, shown = 0;

  for (int color = 8; color <= 120; color += 16)
  for (int gain = 32; gain <= 128; gain += 32)
  for (int note = 24; note <= 96; note += 24)
  {
    syVOsc para;
    para.mode = 4.0f;  // noise
    para.ring = 0.0f;
    para.pitch = 64.0f; para.detune = 64.0f;
    para.color = (float)color;
    para.gain = (float)gain;

    V2Osc osc; osc.init(&inst, 0); osc.note = (sF32)note; osc.set(&para);
    std::vector<unsigned char> W(v2x_size_syWOsc, 0);
    v2x_oscInit(W.data(), 0);
    *(float *)(W.data() + v2x_off_syWOsc_note) = (float)note;
    v2x_oscSet(W.data(), &para);

    std::vector<float> cbuf(N, 0.0f), abuf(N, 0.0f);
    // uneven block splits to exercise state round-trips
    static const int splits[] = { 128, 5, 123, 64, 64, 128, 17, 111, 128 };
    int pos = 0, si = 0;
    while (pos < N) {
      int blk = splits[si++ % 9];
      if (pos + blk > N) blk = N - pos;
      memset(&cbuf[pos], 0, blk*sizeof(float)); // osc ADDS into buffer; keep zeros
      memset(&abuf[pos], 0, blk*sizeof(float));
      osc.render(&cbuf[pos], blk);
      v2x_oscRender(W.data(), &abuf[pos], NULL, blk);
      pos += blk;
    }

    cases++;
    for (int i = 0; i < N; i++)
      if (cbuf[i] != abuf[i]) {
        bad++;
        if (shown < 8) {
          FB a, c; a.f = abuf[i]; c.f = cbuf[i];
          printf("DIFF color=%d gain=%d note=%d @ %d: asm %08x (%.9g) cpp %08x (%.9g)\n",
                 color, gain, note, i, a.u, a.f, c.u, c.f);
          shown++;
        }
        break;
      }
  }
  printf("\n%ld/%ld noise cases bit-diverge.\n", bad, cases);
  return bad ? 1 : 0;
}
