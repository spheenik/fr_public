// Player-timing probe: extract the year-2000 sequencer's per-event sample
// positions, to compare against v2mplayer_port's EVTRACE and localize the
// whole-song timing desync (growing lag vs C1; the synth osc is already proven
// bit-exact -- see c1_osc_probe).
//
// Method: load the fr08 image like c1_fr08_harness, then DETOUR the sequencer
// tick call inside RenderProxy (@0x40996f: `call 0x4095a5`) to a logging hook.
// The hook runs the real tick (0x4095a5), then records (cursmpl, songtick):
//   cursmpl  = [0x592de4]  cumulative samples played (RenderProxy advances it)
//   songtick = [0x5923b4]  current song position in ticks (tick sets it)
// matching what the port logs at the end of V2MPlayer::Tick (cursmpl, time).
//
// Build:  gcc -m32 -no-pie -O2 c1_timing_probe.c -o c1_timing_probe
// Run:    ./c1_timing_probe /tmp/fr08/unpacked.bin [out.csv] [seconds]
//
//   harness_cpp EVTRACE side (for comparison):
//     EVTRACE=1 V2_SRCVER=0 ./harness_cpp ../v2m/converted/fr08.v2m /tmp/x.f32 N

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
#define VA_TICK      0x4095a5u   // sequencer tick (== port V2MPlayer::Tick)
#define VA_TICKCALL  0x40996fu   // `e8 31 fc ff ff` = call 0x4095a5 in RenderProxy
#define VA_G_CURSMPL 0x592de4u   // cumulative samples played
#define VA_G_SONGTCK 0x5923b4u   // current song tick

static const uint32_t RDTSC_SITES[] = { 0x40a494u, 0x40a93eu, 0x40aa6cu };
#define VA_RONAN_PUSH_ARG2 0x409a8cu
#define VA_RONAN_PUSH_ARG1 0x409a9cu
#define VA_RONAN_INIT_CALL 0x409aadu

// event log (cursmpl, songtick) pairs
// external linkage so the inline-asm symbol refs resolve (-no-pie: absolute ok)
#define MAX_EVT (1u<<21)
#define MIDI_MAX 64
uint32_t g_evt[MAX_EVT*2];
uint8_t  g_midi[MAX_EVT][MIDI_MAX];
uint8_t  g_midilen[MAX_EVT];
volatile uint32_t g_nevt;

#define VA_G_MIDIBUF 0x593314u   // the 2000 tick's assembled MIDI buffer (0xfd-term)

// Called from hook_tick AFTER the real sequencer tick ran (so the globals +
// MIDI buffer are up to date). Records (cursmpl, songtick, MIDI bytes).
void log_event(void)
{
  uint32_t i = g_nevt;
  if (i >= MAX_EVT-1) return;
  g_evt[i*2]   = *(volatile uint32_t*)(uintptr_t)VA_G_CURSMPL;
  g_evt[i*2+1] = *(volatile uint32_t*)(uintptr_t)VA_G_SONGTCK;
  const uint8_t *src = (const uint8_t*)(uintptr_t)VA_G_MIDIBUF;
  int k = 0;
  while (src[k] != 0xfd && k < MIDI_MAX-1) { g_midi[i][k] = src[k]; k++; }
  g_midilen[i] = (uint8_t)k;
  g_nevt = i + 1;
}

// Detour target. Called via `call hook_tick` patched over the original
// `call 0x4095a5`. Runs the real tick, then logs. Must preserve every register
// the surrounding RenderProxy relies on -- pushal/popal bracket the logging;
// the real tick (0x4095a5) preserves ebx/esi/edi/ebp itself.
extern void hook_tick(void);
__asm__(
  ".text\n"
  ".globl hook_tick\n"
  "hook_tick:\n"
  "  movl $0x4095a5, %eax\n"     // run the genuine sequencer tick
  "  call *%eax\n"
  "  pushal\n"                   // preserve the player's register state
  "  call log_event\n"           // record cursmpl/songtick/MIDI (cdecl, no args)
  "  popal\n"
  "  ret\n"
);

int main(int argc, char **argv)
{
  const char *path = (argc>1)?argv[1]:"/tmp/fr08/unpacked.bin";
  const char *out  = (argc>2)?argv[2]:"/tmp/fr08/c1_timing.csv";
  unsigned secs    = (argc>3)?(unsigned)atoi(argv[3]):60;

  void *p = mmap((void*)(uintptr_t)IMG_BASE, IMG_SIZE,
                 PROT_READ|PROT_WRITE|PROT_EXEC,
                 MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
  if (p != (void*)(uintptr_t)IMG_BASE) { fprintf(stderr,"mmap failed\n"); return 1; }
  FILE *f = fopen(path,"rb");
  if (!f) { fprintf(stderr,"open %s\n",path); return 1; }
  fread((void*)(uintptr_t)IMG_BASE,1,IMG_SIZE,f); fclose(f);

  for (unsigned i=0;i<3;i++){ uint8_t*s=(uint8_t*)(uintptr_t)RDTSC_SITES[i];
    if(s[0]==0x0f&&s[1]==0x31){s[0]=0x31;s[1]=0xc0;} }
  { uint8_t*p2=(uint8_t*)(uintptr_t)VA_RONAN_PUSH_ARG2;
    uint8_t*p1=(uint8_t*)(uintptr_t)VA_RONAN_PUSH_ARG1;
    uint8_t*cl=(uint8_t*)(uintptr_t)VA_RONAN_INIT_CALL;
    if(p2[0]==0xff&&p1[0]==0x68&&cl[0]==0xe8){memset(p2,0x90,4);memset(p1,0x90,5);memset(cl,0x90,5);} }

  // detour the tick call: overwrite rel32 of `call 0x4095a5` @0x40996f so it
  // targets hook_tick instead (hook then calls the real 0x4095a5).
  {
    uint8_t *site = (uint8_t*)(uintptr_t)VA_TICKCALL;
    if (site[0] != 0xe8) { fprintf(stderr,"no call at tick site (%02x)\n",site[0]); return 1; }
    int32_t rel = (int32_t)((uintptr_t)&hook_tick - (VA_TICKCALL + 5));
    memcpy(site+1, &rel, 4);
    fprintf(stderr,"[probe] tick detour -> hook_tick (rel=%d)\n", rel);
  }

  __asm__ volatile (
    "pushl $0\n\t pushl %0\n\t call %P1\n\t"
    : : "i"(VA_V2M_DATA), "i"(VA_OPEN_V2M)
    : "eax","ecx","edx","esi","cc","memory");

  ((void(*)(void))(uintptr_t)VA_PLAY_V2M)();

  // render in chunks until song end + small tail (or secs cap)
  uint32_t chunk = 4096;
  uint64_t cap = (uint64_t)secs * 44100;
  float *buf = malloc(chunk*2*sizeof(float));
  void (__attribute__((stdcall)) *render)(float*,uint32_t) =
       (void(__attribute__((stdcall))*)(float*,uint32_t))(uintptr_t)VA_RENDER;
  volatile uint8_t *playing = (volatile uint8_t*)(uintptr_t)VA_F_PLAYING;
  uint64_t total=0;
  while (total < cap) {
    render(buf, chunk);
    total += chunk;
    if (!*playing) break;
  }

  FILE *o = fopen(out,"w");
  fprintf(o, "idx,cursmpl,songtick,midi\n");
  for (uint32_t i=0;i<g_nevt;i++) {
    fprintf(o, "%u,%u,%u,", i, g_evt[i*2], g_evt[i*2+1]);
    for (int k=0;k<g_midilen[i];k++) fprintf(o, "%02x", g_midi[i][k]);
    fprintf(o, "\n");
  }
  fclose(o);
  fprintf(stderr,"[probe] %u events over %.1fs -> %s\n",
          g_nevt, (double)total/44100.0, out);
  return 0;
}
