// Dump the osc params the GENUINE 2000 synth receives (hook syOscSet @0x40a4d2
// call sites in syV2Set), to compare vs the port's OSCDUMP. Settles whether the
// converted v6 patch feeds the synth the same osc settings as the original v0
// (conversion check) -- if identical, the channel-10 voice-chain divergence is
// downstream of the osc params (filter / channel), not a patch difference.
//
// Build: gcc -m32 -no-pie -O2 c1_param_probe.c -o c1_param_probe
// Run:   ./c1_param_probe /tmp/fr08/unpacked.bin

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x32b000u
#define VA_V2M_DATA  0x415637u
#define VA_OPEN_V2M  0x4099e3u
#define VA_PLAY_V2M  0x409b10u
#define VA_RENDER    0x40990eu
#define VA_F_PLAYING 0x5923b0u
#define VA_OSCSET    0x40a4d2u
static const uint32_t OSCSET_CALLS[] = { 0xae8du, 0xae98u, 0xaea3u }; // file offs in image? -> VA = 0x400000+...
static const uint32_t RDTSC_SITES[] = { 0x40a494u, 0x40a93eu, 0x40aa6cu };
#define VA_RONAN_PUSH_ARG2 0x409a8cu
#define VA_RONAN_PUSH_ARG1 0x409a9cu
#define VA_RONAN_INIT_CALL 0x409aadu

volatile int g_logleft = 9; // log the first 9 osc-set calls (3 voices)

void log_osc(const float *p)
{
  if (g_logleft <= 0) return;
  g_logleft--;
  // syOscSet reads [esi+0]=mode [+4]=pitch [+8]=detune [+0xc]=color [+0x10]=gain
  fprintf(stderr, "[2000 oscset] mode=%g pitch=%g detune=%g color=%g gain=%g\n",
          p[0], p[1], p[2], p[3], p[4]);
}

extern void hook_os(void);
__asm__(
  ".text\n.globl hook_os\n"
  "hook_os:\n"
  "  pushal\n"
  "  movl 4(%esp), %eax\n"   // saved esi = osc params ptr
  "  pushl %eax\n"
  "  call log_osc\n"
  "  addl $4, %esp\n"
  "  popal\n"
  "  movl $0x40a4d2, %eax\n" // real syOscSet (rets to original call site)
  "  jmp *%eax\n"
);

int main(int argc, char **argv)
{
  const char *path = (argc>1)?argv[1]:"/tmp/fr08/unpacked.bin";
  void *p = mmap((void*)(uintptr_t)IMG_BASE, IMG_SIZE,
                 PROT_READ|PROT_WRITE|PROT_EXEC,
                 MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
  if (p != (void*)(uintptr_t)IMG_BASE) { fprintf(stderr,"mmap failed\n"); return 1; }
  FILE *f = fopen(path,"rb"); if (!f) return 1;
  fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f); fclose(f);
  for (unsigned i=0;i<3;i++){ uint8_t*s=(uint8_t*)(uintptr_t)RDTSC_SITES[i];
    if(s[0]==0x0f&&s[1]==0x31){s[0]=0x31;s[1]=0xc0;} }
  { uint8_t*p2=(uint8_t*)(uintptr_t)VA_RONAN_PUSH_ARG2;
    uint8_t*p1=(uint8_t*)(uintptr_t)VA_RONAN_PUSH_ARG1;
    uint8_t*cl=(uint8_t*)(uintptr_t)VA_RONAN_INIT_CALL;
    if(p2[0]==0xff&&p1[0]==0x68&&cl[0]==0xe8){memset(p2,0x90,4);memset(p1,0x90,5);memset(cl,0x90,5);} }

  // detour the 3 syOscSet call sites (VA = 0x400000 + offset)
  for (unsigned i=0;i<3;i++){
    uint8_t *site = (uint8_t*)(uintptr_t)(IMG_BASE + OSCSET_CALLS[i]);
    if (site[0]!=0xe8) { fprintf(stderr,"no call @%x (%02x)\n", IMG_BASE+OSCSET_CALLS[i], site[0]); return 1; }
    int32_t rel = (int32_t)((uintptr_t)&hook_os - (IMG_BASE + OSCSET_CALLS[i] + 5));
    memcpy(site+1,&rel,4);
  }

  __asm__ volatile ("pushl $0\n\t pushl %0\n\t call %P1\n\t"
    : : "i"(VA_V2M_DATA),"i"(VA_OPEN_V2M) : "eax","ecx","edx","esi","cc","memory");
  ((void(*)(void))(uintptr_t)VA_PLAY_V2M)();

  float buf[4096*2];
  void (__attribute__((stdcall)) *render)(float*,uint32_t) =
       (void(__attribute__((stdcall))*)(float*,uint32_t))(uintptr_t)VA_RENDER;
  // render ~0.2s -- enough for the first notes' voice setups
  for (int i=0;i<3 && g_logleft>0;i++) render(buf, 4096);
  return 0;
}
