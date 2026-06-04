// Unit-test the eraV0 distortion (synthTestDistV0) vs the GENUINE year-2000
// syDistSet/syDistRenderMono (@0x40aa93/0x40ab71) in the mapped fr08 image.
// Build: g++ -m32 -no-pie -std=c++03 -mpc32 -mno-sse -O2 -w -include compat.h \
//        -I.. c1_dist_probe.cpp synth_cpp.o -o c1_dist_probe
// 2000 ABI: syDistSet ebp=dist, esi=syVDist {mode,ingain,param1,param2};
//           syDistRenderMono ebp=dist, esi=src, edi=dst, ecx=n (SETS dst).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <sys/mman.h>

extern "C" void synthTestDistV0(const float *p, const float *src, float *dst, int n);

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x32b000u
extern "C" { volatile uint32_t g_d, g_par, g_src, g_dst, g_n; }

static void call_set(){ __asm__ volatile(
  "pushal\n\t movl g_d,%%ebp\n\t movl g_par,%%esi\n\t"
  "movl $0x40aa93,%%eax\n\t call *%%eax\n\t popal\n\t" ::: "memory","cc"); }
static void call_render(){ __asm__ volatile(
  "pushal\n\t movl g_d,%%ebp\n\t movl g_src,%%esi\n\t movl g_dst,%%edi\n\t movl g_n,%%ecx\n\t"
  "movl $0x40ab71,%%eax\n\t call *%%eax\n\t popal\n\t" ::: "memory","cc"); }

static int load_image(const char *path){
  void *p=mmap((void*)(uintptr_t)IMG_BASE,IMG_SIZE,PROT_READ|PROT_WRITE|PROT_EXEC,
               MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
  if(p!=(void*)(uintptr_t)IMG_BASE) return 0;
  FILE*f=fopen(path,"rb"); if(!f) return 0;
  size_t n=fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f); fclose(f); return n>0x10000;
}

static int test_case(const char *name, float mode, float ingain, float p1, float p2, int N){
  float *in=(float*)malloc(N*sizeof(float)); uint32_t s=0x1234567u;
  for(int i=0;i<N;i++){ s=s*1664525u+1013904223u; in[i]=((s>>9)*(1.0f/4194304.0f))-1.0f; }
  static uint8_t d[0x40]; memset(d,0,sizeof d);
  float par[4]={mode,ingain,p1,p2};
  float *o2000=(float*)calloc(N,sizeof(float));
  g_d=(uint32_t)(uintptr_t)d; g_par=(uint32_t)(uintptr_t)par; call_set();
  g_src=(uint32_t)(uintptr_t)in; g_dst=(uint32_t)(uintptr_t)o2000; g_n=(uint32_t)N; call_render();
  float *ocpp=(float*)calloc(N,sizeof(float));
  synthTestDistV0(par,in,ocpp,N);
  double maxd=0; int first=-1;
  for(int i=0;i<N;i++){ double e=fabs((double)o2000[i]-ocpp[i]); if(e>maxd)maxd=e; if(e!=0&&first<0)first=i; }
  printf("%-10s mode=%g ingain=%g p1=%g p2=%g  max|d|=%.3e first=%d  [2000 %.6f | cpp %.6f]\n",
         name,mode,ingain,p1,p2,maxd,first,o2000[2],ocpp[2]);
  free(in);free(o2000);free(ocpp); return maxd==0.0;
}

int main(int argc,char**argv){
  const char*path=(argc>1)?argv[1]:"/tmp/fr08/unpacked.bin";
  if(!load_image(path)){ fprintf(stderr,"load failed\n"); return 1; }
  int N=512, ok=1;
  ok &= test_case("overdrive", 1, 40, 90, 70, N);
  ok &= test_case("overdrive2",1, 64, 30, 64, N);
  ok &= test_case("clip",      2, 50, 80, 60, N);
  ok &= test_case("bitcrush",  3, 45, 40, 50, N);
  ok &= test_case("decimate",  4, 40, 70, 64, N);
  printf("%s\n", ok?"ALL DIST CASES BIT-EXACT":"DIST DIVERGENCE FOUND");
  return ok?0:1;
}
