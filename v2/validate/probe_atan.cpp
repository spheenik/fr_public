// Dense full-mantissa sweep: C++ fastatan vs ASM fastatan, bit-exact.
// comp_fastatan's 1/32-step grid (round mantissas) was a false negative for the
// double-vs-float coefficient hypothesis; this sweeps dirty mantissas.
#include <cstdio>
#include <cmath>
extern "C" void __stdcall OutputDebugStringA(const char *) {}
#include "../synth_core.cpp"   // brings the static fastatan()
extern "C" float v2x_fastatan(float x);

union FB { float f; unsigned u; };

int main()
{
  unsigned lcg = 0x12345678u;
  long total = 0, mism = 0, shown = 0;
  long mism_lo = 0, mism_hi = 0, n_lo = 0, n_hi = 0; // |x|<1 vs |x|>=1 table
  for (double xd = -8.0; xd <= 8.0; xd += 1.0/4096) {
    // dirty the low mantissa bits so products actually exercise rounding
    lcg = lcg * 1664525u + 1013904223u;
    FB b; b.f = (float)xd;
    if (b.f != 0.0f) b.u ^= (lcg & 0x3FF);
    float x = b.f;
    float c = fastatan(x), a = v2x_fastatan(x);
    total++;
    int hi = (fabsf(x) >= 1.0f);
    if (hi) n_hi++; else n_lo++;
    if (c != a) {
      mism++;
      if (hi) mism_hi++; else mism_lo++;
      if (shown < 8) {
        FB cc, aa; cc.f = c; aa.f = a;
        printf("x=%.9g  cpp %.9g (%08x)  asm %.9g (%08x)\n", x, c, cc.u, a, aa.u);
        shown++;
      }
    }
  }
  printf("\ntotal %ld  mismatches %ld (%.1f%%)\n", total, mism, 100.0*mism/total);
  printf("  |x|<1 : %ld/%ld (%.1f%%)\n", mism_lo, n_lo, n_lo ? 100.0*mism_lo/n_lo : 0.0);
  printf("  |x|>=1: %ld/%ld (%.1f%%)\n", mism_hi, n_hi, n_hi ? 100.0*mism_hi/n_hi : 0.0);
  return 0;
}
