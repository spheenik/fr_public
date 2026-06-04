// Unit-test the eraV0 oscillator (synth_core.cpp synthTestOscV0) against the
// GENUINE year-2000 syOscRender, called directly in the mapped fr08 image.
// This validates delta 2-4 (osc algorithm / freq const / sine fsin / noise LCG)
// in ISOLATION -- the whole-song A/B can't, because C1 is a foreign binary with
// no internal taps.
//
// Build (after build.sh has produced synth_cpp.o):
//   g++ -m32 -no-pie -std=c++03 -mpc32 -mno-sse -O2 -w -include compat.h -I.. \
//       c1_osc_probe.cpp synth_cpp.o -o c1_osc_probe
// Run:
//   ./c1_osc_probe /tmp/fr08/unpacked.bin
//
// 2000 ABI (from fr08-extraction/DELTA.md disasm):
//   syOscSet    @0x40a4d2  ebp=osc struct, esi=syVOsc params (mode,pitch,
//                          detune,color,gain -- 5 floats, NO ring)
//   syOscRender @0x40a585  ebp=osc, edi=dest (mono f32), ecx=nsamples; ADDS.
//   osc struct: [+4]=cnt, [+0x28]=nseed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <math.h>

extern "C" void synthTestOscV0(const float *p, unsigned cnt0, unsigned nseed0,
                               float *dest, int nsamples);

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x32b000u
#define VA_OSCSET    0x40a4d2u
#define VA_OSCRENDER 0x40a585u

// the call args, passed via absolute-addressed globals (external linkage so the
// inline-asm symbol refs resolve; -no-pie makes the absolute ref legal) -- this
// avoids fighting gcc over ebp/esi/edi and survives pushal shifting esp.
extern "C" { volatile uint32_t g_osc, g_par, g_buf, g_n; }

static void call_oscset()
{
  __asm__ volatile(
    "pushal\n\t"
    "movl g_osc,%%ebp\n\t"
    "movl g_par,%%esi\n\t"
    "movl $0x40a4d2,%%eax\n\t"
    "call *%%eax\n\t"
    "popal\n\t"
    ::: "memory","cc");
}

static void call_oscrender()
{
  __asm__ volatile(
    "pushal\n\t"
    "movl g_osc,%%ebp\n\t"
    "movl g_buf,%%edi\n\t"
    "movl g_n,%%ecx\n\t"
    "movl $0x40a585,%%eax\n\t"
    "call *%%eax\n\t"
    "popal\n\t"
    ::: "memory","cc");
}

static int load_image(const char *path)
{
  void *p = mmap((void*)(uintptr_t)IMG_BASE, IMG_SIZE,
                 PROT_READ|PROT_WRITE|PROT_EXEC,
                 MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
  if (p != (void*)(uintptr_t)IMG_BASE) { fprintf(stderr,"mmap failed\n"); return 0; }
  FILE *f = fopen(path,"rb");
  if (!f) { fprintf(stderr,"open %s failed\n",path); return 0; }
  size_t n = fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f);
  fclose(f);
  return n > 0x10000;
}

// one test case: render N samples both ways, return max abs diff (and dump).
static int test_case(const char *name, float mode, float pitch, float detune,
                     float color, float gain, unsigned cnt0, unsigned nseed0,
                     int N)
{
  // ---- 2000 side ----
  static uint8_t osc[0x80];
  memset(osc, 0, sizeof osc);
  float par2000[5] = { mode, pitch, detune, color, gain };
  float *buf2000 = (float*)calloc(N, sizeof(float));

  g_osc = (uint32_t)(uintptr_t)osc;
  g_par = (uint32_t)(uintptr_t)par2000;
  call_oscset();
  *(uint32_t*)(osc + 0x04) = cnt0;   // cnt
  *(uint32_t*)(osc + 0x28) = nseed0; // nseed
  g_buf = (uint32_t)(uintptr_t)buf2000;
  g_n   = (uint32_t)N;
  call_oscrender();

  // ---- C++ eraV0 side ----
  float *bufcpp = (float*)calloc(N, sizeof(float));
  float pcpp[5] = { mode, pitch, detune, color, gain };
  synthTestOscV0(pcpp, cnt0, nseed0, bufcpp, N);

  // ---- compare ----
  double maxd = 0; int firstdiff = -1;
  for (int i=0;i<N;i++) {
    double d = fabs((double)buf2000[i] - (double)bufcpp[i]);
    if (d > maxd) maxd = d;
    if (d != 0.0 && firstdiff < 0) firstdiff = i;
  }
  printf("%-10s mode=%g col=%g gain=%g  max|d|=%.3e  firstdiff=%d  [2000 %.5f %.5f | cpp %.5f %.5f]\n",
         name, mode, color, gain, maxd, firstdiff,
         buf2000[0], buf2000[1], bufcpp[0], bufcpp[1]);
  free(buf2000); free(bufcpp);
  return maxd == 0.0;
}

int main(int argc, char **argv)
{
  const char *path = (argc>1)?argv[1]:"/tmp/fr08/unpacked.bin";
  if (!load_image(path)) return 1;
  fprintf(stderr,"[probe] image mapped @0x%x\n", IMG_BASE);

  int N = 512;
  int ok = 1;
  // pitch 64 = note as-is; vary mode/color/gain. cnt0 nonzero to exercise phase.
  ok &= test_case("trisaw",   1, 80, 64, 64, 100, 0x12345678u, 0, N);
  ok &= test_case("trisaw-c", 1, 76, 64, 32, 110, 0x40000000u, 0, N);
  ok &= test_case("pulse",    2, 84, 64, 64, 100, 0x10000000u, 0, N);
  ok &= test_case("pulse-c",  2, 72, 64, 96, 100, 0x00000000u, 0, N);
  ok &= test_case("sine",     3, 69, 64, 64, 100, 0x20000000u, 0, N);
  ok &= test_case("noise",    4, 64, 64, 64, 100, 0,           0, N);
  ok &= test_case("noise-c",  4, 64, 64, 40, 100, 0,  0x12345678u, N);
  printf("%s\n", ok ? "ALL OSC CASES BIT-EXACT" : "DIVERGENCE(S) FOUND");
  return ok ? 0 : 1;
}
