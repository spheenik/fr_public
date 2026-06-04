// Unit-test the eraV0 channel chorus (synthTestChorusV0) vs the GENUINE 2000
// syChorusInit/Set/render (@0x40b0a6/0x40b0c1/0x40b268) in the mapped image.
// The 2000 render hardcodes the channel buffer 0x7153c4 for I/O; the delay
// buffers are separate (we allocate two 2048-float arrays). State (delay line,
// dbptr, mod counter) persists across render calls, so we drive several chunks.
//
// Build: g++ -m32 -no-pie -std=c++03 -mpc32 -mno-sse -O2 -w -include compat.h \
//        -I.. c1_chorus_probe.cpp synth_cpp.o -o c1_chorus_probe

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <sys/mman.h>

extern "C" void synthTestChorusV0(const float *p, float *io, int n);

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x32b000u
#define VA_CHANBUF 0x7153c4u   // 2000 channel buffer (chorus render I/O)
extern "C" { volatile uint32_t g_ch, g_par, g_n; }

static void call_init(){ __asm__ volatile(
  "pushal\n\t movl g_ch,%%ebp\n\t movl $0x40b0a6,%%eax\n\t call *%%eax\n\t popal\n\t" ::: "memory","cc"); }
static void call_set(){ __asm__ volatile(
  "pushal\n\t movl g_ch,%%ebp\n\t movl g_par,%%esi\n\t movl $0x40b0c1,%%eax\n\t call *%%eax\n\t popal\n\t" ::: "memory","cc"); }
static void call_render(){ __asm__ volatile(
  "pushal\n\t movl g_ch,%%ebp\n\t movl g_n,%%ecx\n\t movl $0x40b268,%%eax\n\t call *%%eax\n\t popal\n\t" ::: "memory","cc"); }

static int load_image(const char *path){
  void *p=mmap((void*)(uintptr_t)IMG_BASE,IMG_SIZE,PROT_READ|PROT_WRITE|PROT_EXEC,
               MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
  if(p!=(void*)(uintptr_t)IMG_BASE) return 0;
  FILE*f=fopen(path,"rb"); if(!f) return 0;
  size_t n=fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f); fclose(f); return n>0x10000;
}

int main(int argc,char**argv){
  const char*path=(argc>1)?argv[1]:"/tmp/fr08/unpacked.bin";
  if(!load_image(path)){ fprintf(stderr,"load failed\n"); return 1; }

  // chorus params: amount(wet), fb, llen, rlen, mrate, mdepth, mphase
  float par[7] = { 90, 80, 60, 70, 40, 50, 80 };
  const int CH=256, CHUNKS=16, N=CH*CHUNKS;

  // ---- 2000 side ----
  static uint8_t chs[0x40]; memset(chs,0,sizeof chs);
  static float L2000[2048], R2000[2048];
  *(uint32_t*)(chs+0x00)=(uint32_t)(uintptr_t)L2000;
  *(uint32_t*)(chs+0x04)=(uint32_t)(uintptr_t)R2000;
  *(uint32_t*)(chs+0x08)=2047u; // mask
  g_ch=(uint32_t)(uintptr_t)chs;
  call_init();
  g_par=(uint32_t)(uintptr_t)par; call_set();
  float *out2000=(float*)malloc(N*2*sizeof(float));
  uint32_t s=0x55aa1234u; float *cbuf=(float*)(uintptr_t)VA_CHANBUF;
  for(int c=0;c<CHUNKS;c++){
    for(int i=0;i<CH*2;i++){ s=s*1664525u+1013904223u; cbuf[i]=((s>>9)*(1.0f/4194304.0f))-1.0f; }
    g_n=(uint32_t)CH; call_render();
    memcpy(out2000+c*CH*2, cbuf, CH*2*sizeof(float));
  }

  // ---- C++ side: same input, but synthTestChorusV0 re-inits per call, so to
  // persist state we must replay all prior chunks each call. Cheap at this size.
  float *outcpp=(float*)malloc(N*2*sizeof(float));
  float *allin=(float*)malloc(N*2*sizeof(float));
  s=0x55aa1234u;
  for(int i=0;i<N*2;i++){ s=s*1664525u+1013904223u; allin[i]=((s>>9)*(1.0f/4194304.0f))-1.0f; }
  // single call over the whole length (state internal, matches 2000's persistence)
  memcpy(outcpp, allin, N*2*sizeof(float));
  synthTestChorusV0(par, outcpp, N);

  double maxd=0; int first=-1;
  for(int i=0;i<N*2;i++){ double e=fabs((double)out2000[i]-outcpp[i]); if(e>maxd)maxd=e; if(e!=0&&first<0)first=i; }
  printf("chorus  max|d|=%.3e first=%d (frame %d)  [2000 %.6f %.6f | cpp %.6f %.6f]\n",
         maxd, first, first/2, out2000[200],out2000[201], outcpp[200],outcpp[201]);
  printf("%s\n", maxd==0.0?"CHORUS BIT-EXACT":"CHORUS DIVERGENCE");
  return maxd==0.0?0:1;
}
