// Unit-test the eraV0 SVF filter (synthTestFltV0) against the GENUINE year-2000
// syFltSet/syFltRender called directly in the mapped fr08 image. This is the
// last untested voice-chain stage (osc already proven bit-exact via
// c1_osc_probe; player+conversion proven bit-exact via c1_timing_probe). If the
// filter matches, the whole-song residual is osc-phase-in-context (keysync);
// if not, the filter is the divergence.
//
// Build (after build.sh produced synth_cpp.o):
//   g++ -m32 -no-pie -std=c++03 -mpc32 -mno-sse -O2 -w -include compat.h -I.. \
//       c1_flt_probe.cpp synth_cpp.o -o c1_flt_probe
// Run: ./c1_flt_probe /tmp/fr08/unpacked.bin
//
// 2000 ABI (DELTA.md disasm):
//   syFltSet    @0x40a837  ebp=flt struct, esi=syVFlt {mode,cutoff,reso}
//     -> [ebp+0]=mode(int) [ebp+4]=cfreq=calcfreq(cut/128) [ebp+8]=res=1-reso/128
//   syFltRender @0x40a880  ebp=flt, esi=src, edi=dst, ecx=n; SVF state @[ebp+0xc]
//     (b) / [ebp+0x10] (l); 2x-oversampled; SETS dst.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <sys/mman.h>

extern "C" void synthTestFltV0(const float *p, const float *src,
                               float *dst, int nsamples);

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x32b000u
#define VA_FLTSET    0x40a837u
#define VA_FLTRENDER 0x40a880u

extern "C" { volatile uint32_t g_flt, g_par, g_src, g_dst, g_n; }

static void call_fltset()
{
  __asm__ volatile(
    "pushal\n\t"
    "movl g_flt,%%ebp\n\t"
    "movl g_par,%%esi\n\t"
    "movl $0x40a837,%%eax\n\t"
    "call *%%eax\n\t"
    "popal\n\t" ::: "memory","cc");
}
static void call_fltrender()
{
  __asm__ volatile(
    "pushal\n\t"
    "movl g_flt,%%ebp\n\t"
    "movl g_src,%%esi\n\t"
    "movl g_dst,%%edi\n\t"
    "movl g_n,%%ecx\n\t"
    "movl $0x40a880,%%eax\n\t"
    "call *%%eax\n\t"
    "popal\n\t" ::: "memory","cc");
}

static int load_image(const char *path)
{
  void *p = mmap((void*)(uintptr_t)IMG_BASE, IMG_SIZE,
                 PROT_READ|PROT_WRITE|PROT_EXEC,
                 MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
  if (p != (void*)(uintptr_t)IMG_BASE) return 0;
  FILE *f = fopen(path,"rb"); if (!f) return 0;
  size_t n = fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f); fclose(f);
  return n > 0x10000;
}

static int test_case(const char *name, float mode, float cutoff, float reso, int N)
{
  // input: a fixed pseudo-random-ish signal (deterministic), [-1,1)
  float *in = (float*)malloc(N*sizeof(float));
  uint32_t s = 0x1234567u;
  for (int i=0;i<N;i++){ s=s*1664525u+1013904223u; in[i]=((s>>9)*(1.0f/4194304.0f))-1.0f; }

  static uint8_t flt[0x40]; memset(flt,0,sizeof flt);
  float par[3] = { mode, cutoff, reso };
  float *o2000 = (float*)calloc(N,sizeof(float));
  g_flt=(uint32_t)(uintptr_t)flt; g_par=(uint32_t)(uintptr_t)par;
  call_fltset();
  g_src=(uint32_t)(uintptr_t)in; g_dst=(uint32_t)(uintptr_t)o2000; g_n=(uint32_t)N;
  call_fltrender();

  float *ocpp = (float*)calloc(N,sizeof(float));
  float pc[3] = { mode, cutoff, reso };
  synthTestFltV0(pc, in, ocpp, N);

  double maxd=0; int first=-1;
  for (int i=0;i<N;i++){ double d=fabs((double)o2000[i]-ocpp[i]); if(d>maxd)maxd=d; if(d!=0&&first<0)first=i; }
  printf("%-8s mode=%g cut=%g reso=%g  max|d|=%.3e first=%d  [2000 %.5f %.5f | cpp %.5f %.5f]\n",
         name, mode, cutoff, reso, maxd, first, o2000[1],o2000[2], ocpp[1],ocpp[2]);
  free(in); free(o2000); free(ocpp);
  return maxd==0.0;
}

int main(int argc, char **argv)
{
  const char *path=(argc>1)?argv[1]:"/tmp/fr08/unpacked.bin";
  if (!load_image(path)) { fprintf(stderr,"load failed\n"); return 1; }
  int N=512, ok=1;
  ok &= test_case("low",   1, 90, 40, N);
  ok &= test_case("low-hi", 1, 30, 100, N);
  ok &= test_case("band",  2, 80, 64, N);
  ok &= test_case("high",  3, 70, 50, N);
  ok &= test_case("notch", 4, 64, 30, N);
  ok &= test_case("all",   5, 100, 80, N);
  printf("%s\n", ok ? "ALL FILTER CASES BIT-EXACT" : "FILTER DIVERGENCE FOUND");
  return ok?0:1;
}
