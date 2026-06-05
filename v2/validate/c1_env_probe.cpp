// Unit-test the eraEnvOld envelope (synthTestEnvV0) vs the genuine 2000
// syEnvSet/syEnvTick (@0x40a6d1/0x40a759) in the mapped image.
// Build: g++ -m32 -no-pie -std=c++03 -mpc32 -mno-sse -O2 -w -include compat.h \
//        -I.. c1_env_probe.cpp synth_cpp.o -o c1_env_probe
// 2000 ABI: syEnvSet ebp=env, esi=syVEnv {ar,dr,sl,sr,rr,vol}; syEnvTick ebp=env,
//   eax=gate(0/1); env: [+4]=state(byte) [+8]=val [+0]=out(=val*gain).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <sys/mman.h>

extern "C" void synthTestEnvV0(const float *p, const unsigned char *gates, float *out, int n);

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x32b000u
extern "C" { volatile uint32_t g_env, g_par, g_gate; }

static void call_set(){ __asm__ volatile(
  "pushal\n\t movl g_env,%%ebp\n\t movl g_par,%%esi\n\t movl $0x40a6d1,%%eax\n\t call *%%eax\n\t popal\n\t" ::: "memory","cc"); }
static float call_tick(){ // gate in g_gate; returns env out [ebp+0]
  __asm__ volatile(
    "pushal\n\t movl g_env,%%ebp\n\t movl g_gate,%%eax\n\t movl $0x40a759,%%ecx\n\t call *%%ecx\n\t popal\n\t" ::: "memory","cc");
  return *(float*)(uintptr_t)g_env; // [ebp+0] = out
}

static int load_image(const char *path){
  void *p=mmap((void*)(uintptr_t)IMG_BASE,IMG_SIZE,PROT_READ|PROT_WRITE|PROT_EXEC,
               MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
  if(p!=(void*)(uintptr_t)IMG_BASE) return 0;
  FILE*f=fopen(path,"rb"); if(!f) return 0;
  size_t n=fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f); fclose(f); return n>0x10000;
}

static int test_case(const char *name, float ar,float dr,float sl,float sr,float rr,float vol,int N,int gateoff){
  unsigned char *gates=(unsigned char*)malloc(N);
  for(int i=0;i<N;i++) gates[i]=(i<gateoff)?1:0;

  static uint8_t env[0x40]; memset(env,0,sizeof env);
  float par[6]={ar,dr,sl,sr,rr,vol};
  g_env=(uint32_t)(uintptr_t)env; g_par=(uint32_t)(uintptr_t)par; call_set();
  float *o2000=(float*)malloc(N*sizeof(float));
  for(int i=0;i<N;i++){ g_gate=gates[i]; o2000[i]=call_tick(); }

  float *ocpp=(float*)malloc(N*sizeof(float));
  synthTestEnvV0(par, gates, ocpp, N);

  double maxd=0; int first=-1;
  for(int i=0;i<N;i++){ double e=fabs((double)o2000[i]-ocpp[i]); if(e>maxd)maxd=e; if(e!=0&&first<0)first=i; }
  printf("%-9s ar=%g dr=%g sl=%g sr=%g rr=%g  max|d|=%.3e first=%d  [2000 %.5f %.5f | cpp %.5f %.5f]\n",
         name,ar,dr,sl,sr,rr,maxd,first,o2000[5],o2000[N/2],ocpp[5],ocpp[N/2]);
  free(gates);free(o2000);free(ocpp); return maxd==0.0;
}

int main(int argc,char**argv){
  const char*path=(argc>1)?argv[1]:"/tmp/fr08/unpacked.bin";
  if(!load_image(path)){ fprintf(stderr,"load failed\n"); return 1; }
  int N=400, off=300, ok=1;
  ok &= test_case("fast",   100,80, 64, 64, 90, 127, N, off);
  ok &= test_case("slow",    40,110,100, 70,110, 127, N, off);
  ok &= test_case("zerosus", 90,100,  0, 50, 80, 100, N, off);
  ok &= test_case("highsus",110, 90,120, 80,100, 120, N, off);
  printf("%s\n", ok?"ALL ENV CASES BIT-EXACT":"ENV DIVERGENCE FOUND");
  return ok?0:1;
}
