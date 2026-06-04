// Voice-chain localization: render the year-2000 synth with a SINGLE MIDI
// channel soloed, to byte-compare against the port's CHANSOLO render. Since the
// player timing + MIDI stream are already proven bit-exact (c1_timing_probe)
// and the oscillator is bit-exact (c1_osc_probe), any per-channel divergence is
// isolated to that channel's voice chain (osc-in-context / filter / dist / dcf /
// channel-mix + channel FX).
//
// Method: detour ProcessMIDI's call inside the sequencer tick (@0x4098b1) to a
// hook that FILTERS the assembled MIDI buffer (@0x593314) down to one channel
// before the real ProcessMIDI runs -- the 2000 analogue of the port's CHANSOLO
// (which discards other channels' emitted bytes). Timing/allocation bookkeeping
// is unchanged; only that channel sounds.
//
// Build: gcc -m32 -no-pie -O2 c1_solo_probe.c -o c1_solo_probe
// Run:   C1_SOLO=10 ./c1_solo_probe /tmp/fr08/unpacked.bin /tmp/fr08/c1_solo10.f32 [secs]
//   port: CHANSOLO=10 V2_SRCVER=0 ./harness_cpp ../v2m/converted/fr08.v2m out.f32 N

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
#define VA_PROCMIDI  0x40bbacu   // ProcessMIDI(buf) stdcall ret 4
#define VA_PMCALL    0x4098b1u   // `e8 f6 22 00 00` = call 0x40bbac in the tick

static const uint32_t RDTSC_SITES[] = { 0x40a494u, 0x40a93eu, 0x40aa6cu };
#define VA_RONAN_PUSH_ARG2 0x409a8cu
#define VA_RONAN_PUSH_ARG1 0x409a9cu
#define VA_RONAN_INIT_CALL 0x409aadu

int g_solo = -1; // channel to keep (0..15); -1 = all

// Filter the assembled 2000 MIDI buffer in place: keep only events whose
// channel == g_solo. Source uses running status; we emit explicit status per
// kept event (ProcessMIDI handles explicit status). 0xfd terminates.
void filter_midi(uint8_t *buf)
{
  if (g_solo < 0) return;
  uint8_t tmp[1024]; int o = 0;
  uint8_t *p = buf; uint8_t status = 0;
  while (*p != 0xfd && o < (int)sizeof(tmp)-4) {
    if (*p >= 0x80) status = *p++;
    int cmd = status & 0xf0, ch = status & 0x0f;
    int dlen = (cmd == 0xc0 || cmd == 0xd0) ? 1 : 2;
    if (cmd < 0x80) break; // safety
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
  ".text\n"
  ".globl hook_pm\n"
  "hook_pm:\n"            // entry: [esp]=retaddr, [esp+4]=midi buf ptr
  "  pushal\n"           // +32
  "  movl 0x24(%esp), %eax\n"  // buf ptr = [esp + 32 + 4]
  "  pushl %eax\n"
  "  call filter_midi\n"
  "  addl $4, %esp\n"
  "  popal\n"
  "  movl $0x40bbac, %eax\n"   // tail-call real ProcessMIDI (ret 4 -> tick)
  "  jmp *%eax\n"
);

int main(int argc, char **argv)
{
  const char *path = (argc>1)?argv[1]:"/tmp/fr08/unpacked.bin";
  const char *out  = (argc>2)?argv[2]:"/tmp/fr08/c1_solo.f32";
  unsigned secs    = (argc>3)?(unsigned)atoi(argv[3]):60;
  const char *se = getenv("C1_SOLO"); g_solo = se ? atoi(se) : -1;

  void *p = mmap((void*)(uintptr_t)IMG_BASE, IMG_SIZE,
                 PROT_READ|PROT_WRITE|PROT_EXEC,
                 MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
  if (p != (void*)(uintptr_t)IMG_BASE) { fprintf(stderr,"mmap failed\n"); return 1; }
  FILE *f = fopen(path,"rb"); if (!f) { fprintf(stderr,"open %s\n",path); return 1; }
  fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f); fclose(f);

  for (unsigned i=0;i<3;i++){ uint8_t*s=(uint8_t*)(uintptr_t)RDTSC_SITES[i];
    if(s[0]==0x0f&&s[1]==0x31){s[0]=0x31;s[1]=0xc0;} }
  { uint8_t*p2=(uint8_t*)(uintptr_t)VA_RONAN_PUSH_ARG2;
    uint8_t*p1=(uint8_t*)(uintptr_t)VA_RONAN_PUSH_ARG1;
    uint8_t*cl=(uint8_t*)(uintptr_t)VA_RONAN_INIT_CALL;
    if(p2[0]==0xff&&p1[0]==0x68&&cl[0]==0xe8){memset(p2,0x90,4);memset(p1,0x90,5);memset(cl,0x90,5);} }

  // detour ProcessMIDI call in the tick -> hook_pm (filters, then real PM)
  {
    uint8_t *site = (uint8_t*)(uintptr_t)VA_PMCALL;
    if (site[0] != 0xe8) { fprintf(stderr,"no call at PM site (%02x)\n",site[0]); return 1; }
    int32_t rel = (int32_t)((uintptr_t)&hook_pm - (VA_PMCALL + 5));
    memcpy(site+1, &rel, 4);
    fprintf(stderr,"[solo] C1_SOLO=%d, ProcessMIDI detour installed\n", g_solo);
  }

  __asm__ volatile ("pushl $0\n\t pushl %0\n\t call %P1\n\t"
    : : "i"(VA_V2M_DATA), "i"(VA_OPEN_V2M)
    : "eax","ecx","edx","esi","cc","memory");
  ((void(*)(void))(uintptr_t)VA_PLAY_V2M)();

  uint32_t chunk = 4096;
  uint64_t cap = (uint64_t)secs * 44100;
  float *buf = malloc(chunk*2*sizeof(float));
  void (__attribute__((stdcall)) *render)(float*,uint32_t) =
       (void(__attribute__((stdcall))*)(float*,uint32_t))(uintptr_t)VA_RENDER;
  volatile uint8_t *playing = (volatile uint8_t*)(uintptr_t)VA_F_PLAYING;
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
