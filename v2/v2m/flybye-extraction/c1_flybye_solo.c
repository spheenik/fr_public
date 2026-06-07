// c1_flybye_solo -- flybye (2001, v1) adaptation of validate/c1_solo_probe.c:
// channel-solo + voice-alloc trace on the genuine 2001 engine, for the 66.42s
// divergence hunt (vs the portable's native-v1 render).
//
//   C1_SOLO=N        keep only MIDI channel N (filter the assembled buffer
//                    before ProcessMIDI -- analogue of port V2SEQ_SOLO=N)
//   C1_ALLOCTRACE=1  log "[alloc] pos chan slot note vel" at the genuine
//                    allocator's SET call (curalloc-aligned with the port's
//                    V2_STEAL chanmap transitions)
//
// Addresses (flybye unpacked.bin; structure byte-identical to fr08, only
// data/call addresses differ -- see flybye-extraction/NOTES.md):
//   OpenV2M 0x40d61c  PlayV2M 0x40d74c  RenderProxy 0x40d4d2 (stdcall ret 8)
//   player tick 0x40d024; PM call site 0x40d4b8 -> ProcessMIDI 0x4101ea
//   midibuf 0x478a88; playing flag 0x477318; pos global 0x477d50
//   alloc SET call 0x4103dd -> voice set 0x40f0e8 (edx=slot ecx=chan esi->note,vel)
//   curalloc 0x50337c
//   rdtsc 0x40e604/0x40ea9e/0x40ebcc; Ronan init 0x40d6c5/cb/e9, ch15 process 0x4100d3
//
// Build: gcc -m32 -no-pie -O2 -mstackrealign c1_flybye_solo.c -o c1_flybye_solo
// Run:   C1_ALLOCTRACE=1 ./c1_flybye_solo /tmp/fr013/unpacked.bin out.f32 70

#include <stdint.h>

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x2ef000u

// shared native oracle scaffold (mmap@0x400000 + rdtsc-pin + f32)
#define ORACLE_IMG_SIZE IMG_SIZE
#include "../toolkit/oracle.h"
#define VA_V2M       0x41bd9du
#define VA_OPEN_V2M  0x40d61cu
#define VA_PLAY_V2M  0x40d74cu
#define VA_RENDER    0x40d4d2u
#define VA_PLAYING   0x477318u
#define VA_PMCALL    0x40d4b8u   // e8 2d 2d 00 00 = call 0x4101ea
#define VA_PROCMIDI  0x4101eau
#define VA_POS       0x477d50u   // cumulative rendered samples
#define VA_ALLOC_SET_CALL 0x4103ddu  // e8 06 ed ff ff = call 0x40f0e8
static const uint32_t RDTSC_SITES[] = { 0x40e604u, 0x40ea9eu, 0x40ebccu };

int g_solo = -1;

uint8_t g_fpu[128] __attribute__((aligned(16)));

// keep only events whose channel == g_solo (same as fr08 probe)
void filter_midi(uint8_t *buf)
{
  if (g_solo < 0) return;
  uint8_t tmp[1024]; int o = 0;
  uint8_t *p = buf; uint8_t status = 0;
  while (*p != 0xfd && o < (int)sizeof(tmp)-4) {
    if (*p >= 0x80) status = *p++;
    int cmd = status & 0xf0, ch = status & 0x0f;
    int dlen = (cmd == 0xc0 || cmd == 0xd0) ? 1 : 2;
    if (cmd < 0x80) break;
    if (ch == g_solo) {
      tmp[o++] = status;
      for (int i=0;i<dlen;i++) tmp[o++] = p[i];
    }
    p += dlen;
  }
  tmp[o++] = 0xfd;
  memcpy(buf, tmp, o);
}

extern void hook_pm(void);
__asm__(
  ".text\n.globl hook_pm\nhook_pm:\n"
  "  pushal\n"
  "  movl 0x24(%esp), %eax\n"
  "  pushl %eax\n  call filter_midi\n  addl $4, %esp\n"
  "  popal\n"
  "  movl $0x4101ea, %eax\n"
  "  jmp *%eax\n"
);

// alloc trace: at the SET call edx=slot, ecx=chan, esi->note,vel
const uint32_t g_chanset_real = 0x40f0e8u;
int g_alloctrace_n = 0;
void alloclog(uint32_t edx, uint32_t ecx, uint32_t esi)
{
  if (g_alloctrace_n >= 20000) return;
  g_alloctrace_n++;
  uint32_t pos = *(uint32_t*)(uintptr_t)VA_POS;
  fprintf(stderr, "[alloc] pos=%u chan=%u slot=%u note=%u vel=%u\n",
          pos, ecx, edx, *(uint8_t*)(uintptr_t)esi, *(uint8_t*)(uintptr_t)(esi+1));
}
extern void hook_allocset(void);
__asm__(
  ".text\n.globl hook_allocset\nhook_allocset:\n"
  "  pushal\n  fnsave g_fpu\n"
  "  pushl %esi\n  pushl %ecx\n  pushl %edx\n"
  "  call alloclog\n  addl $12,%esp\n"
  "  frstor g_fpu\n  popal\n"
  "  jmp *g_chanset_real\n"
);

static void install_call(uint32_t site, void *target)
{
  uint8_t *s = (uint8_t*)(uintptr_t)site;
  int32_t rel = (int32_t)((uintptr_t)target - (site + 5));
  s[0]=0xe8; memcpy(s+1,&rel,4);
}

int main(int argc, char **argv)
{
  const char *path = (argc>1)?argv[1]:"/tmp/fr013/unpacked.bin";
  const char *out  = (argc>2)?argv[2]:"/tmp/fr013/c1_solo.f32";
  unsigned secs    = (argc>3)?(unsigned)atoi(argv[3]):70;
  const char *se = getenv("C1_SOLO"); g_solo = se ? atoi(se) : -1;

  oracle_map_image(path, IMG_SIZE);
  oracle_pin_rdtsc(RDTSC_SITES, sizeof(RDTSC_SITES)/sizeof(RDTSC_SITES[0]));

  // Ronan init nops (== c1_flybye_harness)
  { uint8_t*a2=(uint8_t*)0x40d6c5u; uint8_t*a1=(uint8_t*)0x40d6cbu; uint8_t*cl=(uint8_t*)0x40d6e9u;
    if(a2[0]==0xff&&a1[0]==0x68&&cl[0]==0xe8){memset(a2,0x90,4);memset(a1,0x90,5);memset(cl,0x90,5);}
    else { fprintf(stderr,"bad ronan init bytes\n"); return 1; } }
  // Ronan ch15 process call
  if (!getenv("C1_RONAN")) {
    uint8_t *rp = (uint8_t*)0x4100d3u;
    if (rp[0]==0xe8) memset(rp,0x90,5);
    else { fprintf(stderr,"no call at ronan process\n"); return 1; }
  }

  if (getenv("C1_ALLOCTRACE")) {
    uint8_t *site = (uint8_t*)(uintptr_t)VA_ALLOC_SET_CALL;
    if (site[0] != 0xe8) { fprintf(stderr,"no call at alloc-set site (%02x)\n",site[0]); return 1; }
    install_call(VA_ALLOC_SET_CALL, &hook_allocset);
    fprintf(stderr,"[solo] alloc trace armed\n");
  }

  { uint8_t *site = (uint8_t*)(uintptr_t)VA_PMCALL;
    if (site[0] != 0xe8) { fprintf(stderr,"no call at PM site (%02x)\n",site[0]); return 1; }
    install_call(VA_PMCALL, &hook_pm);
    fprintf(stderr,"[solo] C1_SOLO=%d, ProcessMIDI detour installed\n", g_solo);
  }

  uint32_t esp_save = 0;
  __asm__ volatile (
    "movl %%esp, %0\n\t pushl $0\n\t pushl %2\n\t call *%3\n\t movl %0, %%esp\n\t"
    : "+m"(esp_save) : "0"(esp_save), "i"(VA_V2M), "r"(VA_OPEN_V2M)
    : "eax","ecx","edx","esi","cc","memory");
  __asm__ volatile (
    "movl %%esp, %0\n\t call *%2\n\t movl %0, %%esp\n\t"
    : "+m"(esp_save) : "0"(esp_save), "r"(VA_PLAY_V2M)
    : "eax","ecx","edx","esi","cc","memory");

  uint32_t chunk = 4096;
  uint64_t cap = (uint64_t)secs * 44100;
  float *buf = malloc(chunk*2*sizeof(float));
  void (__attribute__((stdcall)) *render)(float*,uint32_t) =
       (void(__attribute__((stdcall))*)(float*,uint32_t))(uintptr_t)VA_RENDER;
  volatile uint32_t *playing = (volatile uint32_t*)(uintptr_t)VA_PLAYING;
  FILE *o = fopen(out,"wb");
  uint64_t total=0; uint32_t tail=0;
  while (total < cap) {
    render(buf, chunk);
    fwrite(buf, 2*sizeof(float), chunk, o);
    total += chunk;
    if (!*playing) { if (tail >= 6*44100) break; tail += chunk; }
  }
  fclose(o);
  fprintf(stderr,"[solo] wrote %.1fs -> %s\n", (double)total/44100.0, out);
  return 0;
}
