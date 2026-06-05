// Unit-test the eraV0 LFO (synthTestLfoV0) vs the genuine 2000 syLFOInit/Set/
// KeyOn/Tick (@0x40a935/0x40a945/0x40a98b/0x40a9c2) in the mapped image.
// Build: g++ -m32 -no-pie -std=c++03 -mpc32 -mno-sse -O2 -w -include compat.h \
//        -I.. c1_lfo_probe.cpp synth_cpp.o -o c1_lfo_probe
// 2000 LFO: out=+0 mode=+4 sync=+8 eg=+c freq=+10 cntr=+14 cphase=+18 amp=+1c
//   nseed=+20 last=+24. syVLFO esi = {mode,sync,eg,rate,phase,amp} (NO pol).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <sys/mman.h>

extern "C" void synthTestLfoV0(const float *p, float *out, int n);

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x32b000u
extern "C" { volatile uint32_t g_lfo, g_par; }

static void call_init(){ __asm__ volatile(
  "pushal\n\t movl g_lfo,%%ebp\n\t movl $0x40a935,%%eax\n\t call *%%eax\n\t popal\n\t" ::: "memory","cc"); }
static void call_set(){ __asm__ volatile(
  "pushal\n\t movl g_lfo,%%ebp\n\t movl g_par,%%esi\n\t movl $0x40a945,%%eax\n\t call *%%eax\n\t popal\n\t" ::: "memory","cc"); }
static void call_keyon(){ __asm__ volatile(
  "pushal\n\t movl g_lfo,%%ebp\n\t movl $0x40a98b,%%eax\n\t call *%%eax\n\t popal\n\t" ::: "memory","cc"); }
static float call_tick(){ __asm__ volatile(
  "pushal\n\t movl g_lfo,%%ebp\n\t movl $0x40a9c2,%%eax\n\t call *%%eax\n\t popal\n\t" ::: "memory","cc");
  return *(float*)(uintptr_t)g_lfo; }

static int load_image(const char *path){
  void *p=mmap((void*)(uintptr_t)IMG_BASE,IMG_SIZE,PROT_READ|PROT_WRITE|PROT_EXEC,
               MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
  if(p!=(void*)(uintptr_t)IMG_BASE) return 0;
  FILE*f=fopen(path,"rb"); if(!f) return 0;
  size_t n=fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f); fclose(f); return n>0x10000;
}

static int test_case(const char *name,float mode,float sync,float eg,float rate,float phase,float amp,int N){
  static uint8_t lfo[0x40]; memset(lfo,0,sizeof lfo);
  float par[6]={mode,sync,eg,rate,phase,amp};
  g_lfo=(uint32_t)(uintptr_t)lfo; g_par=(uint32_t)(uintptr_t)par;
  call_init();
  *(uint32_t*)(lfo+0x20)=0; // match eraV0 S&H seed=0 (init seeded rdtsc, unpatched here)
  call_set(); call_keyon();
  float *o2000=(float*)malloc(N*sizeof(float));
  for(int i=0;i<N;i++) o2000[i]=call_tick();

  float *ocpp=(float*)malloc(N*sizeof(float));
  synthTestLfoV0(par, ocpp, N);

  double maxd=0; int first=-1;
  for(int i=0;i<N;i++){ double e=fabs((double)o2000[i]-ocpp[i]); if(e>maxd)maxd=e; if(e!=0&&first<0)first=i; }
  printf("%-7s mode=%g rate=%g amp=%g  max|d|=%.3e first=%d  [2000 %.5f %.5f | cpp %.5f %.5f]\n",
         name,mode,rate,amp,maxd,first,o2000[1],o2000[N/2],ocpp[1],ocpp[N/2]);
  free(o2000);free(ocpp); return maxd==0.0;
}

int main(int argc,char**argv){
  const char*path=(argc>1)?argv[1]:"/tmp/fr08/unpacked.bin";
  if(!load_image(path)){ fprintf(stderr,"load failed\n"); return 1; }
  int N=600, ok=1;
  ok &= test_case("saw",  0,0,0, 80, 40, 100, N);
  ok &= test_case("tri",  1,0,0, 70, 64, 100, N);
  ok &= test_case("pulse",2,0,0, 90, 30, 110, N);
  ok &= test_case("sin",  3,0,0, 75, 64, 100, N);
  ok &= test_case("s&h",  4,0,0, 85, 64, 100, N);
  ok &= test_case("sync", 1,1,0, 60, 96, 100, N);
  printf("%s\n", ok?"ALL LFO CASES BIT-EXACT":"LFO DIVERGENCE FOUND");
  return ok?0:1;
}
