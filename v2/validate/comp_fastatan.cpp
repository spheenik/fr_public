// Sweep-compare C++ fastatan vs ASM fastatan to pin the overdrive divergence.
#include <cstdio>
#include <cmath>
extern "C" void __stdcall OutputDebugStringA(const char *) {}
#include "../synth_core.cpp"   // brings the static fastatan()
extern "C" float v2x_fastatan(float x);

int main()
{
  printf("   x        cpp          asm         diff      libm atan\n");
  float worst = 0, worstx = 0;
  for (double xd = -8.0; xd <= 8.0; xd += 0.03125) {
    float x = (float)xd;
    float c = fastatan(x), a = v2x_fastatan(x);
    float d = fabsf(c - a);
    if (d > worst) { worst = d; worstx = x; }
    // print a few representative points and any big gap
    if (d > 0.01f || fabsf(fabsf(x) - 1.0f) < 0.02f || fabsf(fabsf(x) - 2.0f) < 0.02f)
      printf("%7.4f  %10.6f  %10.6f  %9.6f  %9.6f\n", x, c, a, d, (float)atan(x));
  }
  printf("\nworst |cpp-asm| = %.6f at x=%.5f\n", worst, worstx);
  return 0;
}
