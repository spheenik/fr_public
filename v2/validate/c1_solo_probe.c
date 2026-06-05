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
// Build: gcc -m32 -no-pie -O2 -mstackrealign c1_solo_probe.c -o c1_solo_probe
// (-mstackrealign is REQUIRED: the hook trampolines call C functions on the
//  2000 code's stack, which is not 16-byte aligned; without it gcc's SSE
//  spills (movaps) in any hook-reachable function segfault.)
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
#define VA_RVB_CALL  0x40bae8u   // `e8 2a f8 ff ff` = call reverb render 0x40b317
#define VA_DEL_CALL  0x40baf3u   // `e8 30 f7 ff ff` = call delay  render 0x40b228
#define VA_CHAN_CALL 0x40bac5u   // `e8 02 fb ff ff` = call chan FX render 0x40b5cc
#define VA_CHAN_FX   0x40b5ccu   // channel FX chain (dist+chorus, in-place on chanbuf)
#define VA_CHANBUF   0x7153c4u   // stereo channel buffer (frame-sized)
#define VA_CHUNK_N   0x726f94u   // current inner-chunk sample count
#define VA_CHANOBJ0  0x718cd8u   // channel object array base (0x78 apart)
#define VA_RONAN_PUSH_ARG2 0x409a8cu
#define VA_RONAN_PUSH_ARG1 0x409a9cu
#define VA_RONAN_INIT_CALL 0x409aadu

// Per-frame voice sub-stage tap (C1_VCEFRAME=<pfx>, optional C1_VCE_LO/HI):
// accumulate vcebuf @0x714fc4 post-osc / post-flt / post-dist across the voices
// in each render-chunk and dump per chunk -- the C1 counterpart of the port's
// VCEFRAME dump. Detours: the chunk render call @0x40b982 (pos/reset/dump),
// the per-voice syV2Render call @0x40ba88 (post-dist on return), and inside
// syV2Render at 0x40ad76 (post-osc) and 0x40adfe (post-flt).
#define VA_VCEBUF      0x714fc4u
#define VA_CHUNK_CALL  0x40b982u   // `e8 89 00 00 00` call render-chunk 0x40ba10
#define VA_CHUNK_REAL  0x40ba10u
#define VA_V2R_CALL    0x40ba88u   // `e8 c0 f2 ff ff` call syV2Render 0x40ad4d
#define VA_V2R_REAL    0x40ad4du
#define VA_POSTOSC     0x40ad76u   // `8d ad 4c ff ff ff` lea ebp,[ebp-0xb4]
#define VA_POSTFLT     0x40adfeu   // `be c4 4f 71 00` mov esi,0x714fc4
#define VA_PREMIX_EQ   0x40baf8u   // `8b 0d 94 6f 72 00` mov ecx,[0x726f94] = lc/hc EQ entry
#define VA_OUTPTR      0x726f98u   // ptr to current output chunk (= channel sum pre-EQ)

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

// Chan-stream tap (pair of the port's CHANSTREAM=<prefix>): dump chanbuf for
// one channel BEFORE (.pre) and AFTER (.post) the 2000 channel-FX chain
// @0x40b5cc. Each inner chunk renders n FRESH samples, so concatenation gives
// the contiguous per-channel stream, byte-comparable with the port's dump.
int g_cs_ch = -1;
FILE *g_cs_pre = 0, *g_cs_post = 0;
const uint32_t g_chanfx_real = VA_CHAN_FX;

extern FILE *g_fvc, *g_fvcp; extern int g_vce_chan; extern uint32_t g_vce_count;
extern float g_acc_chan[640], g_acc_chanpost[640];
// chunk sidecar (written by chunk_post when armed): per in-window chunk,
// {pos, count, chanfx_n, chanfx_fired} -- audits the .chan stream construction
// (whether the soloed channel's FX ran this chunk and with which sample count,
// vs the chunk count the voice taps use). Catches stale-chanbuf-tail reads.
extern FILE *g_fvchunks;
uint32_t g_chanfx_fired = 0, g_chanfx_n = 0;

static void cs_dump(uint32_t ebp, FILE *f)
{
  if (!f) return;
  int ch = (int)((ebp - VA_CHANOBJ0) / 0x78u);
  if (ch != g_cs_ch) return;
  uint32_t n = *(uint32_t*)(uintptr_t)VA_CHUNK_N;
  fwrite((void*)(uintptr_t)VA_CHANBUF, 2*sizeof(float), n, f);
}
void chanstream_pre(uint32_t ebp)
{
  cs_dump(ebp, g_cs_pre);
  // vce tap: accumulate this channel's post-volramp voice sum (chanbuf, pre
  // channel-FX) for the per-frame comparison against the port's g_chantap.
  if (g_fvc) {
    int ch = (int)((ebp - VA_CHANOBJ0) / 0x78u);
    if (ch == g_vce_chan) {
      const float *cb = (const float*)(uintptr_t)VA_CHANBUF;
      uint32_t n = g_vce_count; if (n > 320) n = 320;
      for (uint32_t i = 0; i < 2*n; i++) g_acc_chan[i] += cb[i];
      g_chanfx_fired = 1;
      g_chanfx_n = *(uint32_t*)(uintptr_t)VA_CHUNK_N; // the 2000's own count
    }
  }
}
void chanstream_post(uint32_t ebp)
{
  cs_dump(ebp, g_cs_post);
  if (g_fvcp) {
    int ch = (int)((ebp - VA_CHANOBJ0) / 0x78u);
    if (ch == g_vce_chan) {
      const float *cb = (const float*)(uintptr_t)VA_CHANBUF;
      uint32_t n = g_vce_count; if (n > 320) n = 320;
      for (uint32_t i = 0; i < 2*n; i++) g_acc_chanpost[i] += cb[i];
    }
  }
}

// Tick log (C1_TICKLOG=1): trace the first voice ticks -- env1 out/state,
// volramp cur/ramp -- to pin the control-timeline question empirically.
#define VA_TICK_CALL 0x40b9c1u   // `e8 40 f3 ff ff` = call voice tick 0x40ad06
#define VA_TICK_REAL 0x40ad06u
const uint32_t g_tick_real = VA_TICK_REAL;
int g_ticklog_n = 0;

void ticklog(uint32_t ebp)
{
  uint32_t pos = *(uint32_t*)(uintptr_t)0x592de4;
  const char *lo = getenv("C1_TICK_LO"), *hi = getenv("C1_TICK_HI");
  if (lo && pos < (uint32_t)strtoul(lo,0,10)) return;
  if (hi && pos > (uint32_t)strtoul(hi,0,10)) return;
  if (g_ticklog_n >= 400) return;
  g_ticklog_n++;
  int vc = (int)((ebp - 0x716a18u) / 0x1f0u);
  float out   = *(float*)(uintptr_t)(ebp + 0x124);
  uint32_t st = *(uint32_t*)(uintptr_t)(ebp + 0x128);
  float cur   = *(float*)(uintptr_t)(ebp + 0x0c);
  float ramp  = *(float*)(uintptr_t)(ebp + 0x10);
  // env struct = {out,state,val,atd,dcf,sul,suf,ref,gain} (9 dwords, 0x24);
  // env1 @ voice+0x124, env2 @ voice+0x148 (see syEnvSet/Tick layout).
  uint32_t e1v = *(uint32_t*)(uintptr_t)(ebp + 0x12c);  // env1.val bits
  uint32_t e2o = *(uint32_t*)(uintptr_t)(ebp + 0x148);  // env2.out bits
  uint32_t e2s = *(uint32_t*)(uintptr_t)(ebp + 0x14c);  // env2.state
  uint32_t e2v = *(uint32_t*)(uintptr_t)(ebp + 0x150);  // env2.val bits
  fprintf(stderr, "[tick] pos=%u vc=%d env1.out=%g st=%u cur=%g ramp=%g "
          "| e1.out=%08x e1.val=%08x e2.st=%u e2.out=%08x e2.val=%08x cur=%08x ramp=%08x\n",
          pos, vc, out, st, cur, ramp,
          *(uint32_t*)(uintptr_t)(ebp + 0x124), e1v, e2s, e2o, e2v,
          *(uint32_t*)(uintptr_t)(ebp + 0x0c), *(uint32_t*)(uintptr_t)(ebp + 0x10));
}

extern void hook_tick(void);
__asm__(
  ".text\n"
  ".globl hook_tick\n"
  "hook_tick:\n"
  "  call *g_tick_real\n"      // real voice tick first (env/lfo ticks + volramp)
  "  pushal\n"
  "  pushl %ebp\n"
  "  call ticklog\n"
  "  addl $4, %esp\n"
  "  popal\n"
  "  ret\n"
);

// Alloc trace (C1_ALLOCTRACE=1): log every voice allocation -- the genuine
// allocator's choice (@0x40bc5f..0x40bd99) just before the SET call @0x40bd9f,
// where edx=slot, ecx=channel, esi->note,vel. Byte-comparable with the port's
// ALLOCTRACE to find the FIRST diverging steal choice (which can long predate
// the first audible divergence when the disputed voices are silent).
#define VA_ALLOC_SET_CALL 0x40bd9fu
const uint32_t g_chanset_real = 0x40af88u;
int g_alloctrace_n = 0;
void alloclog(uint32_t edx, uint32_t ecx, uint32_t esi)
{
  if (g_alloctrace_n >= 20000) return;
  g_alloctrace_n++;
  uint32_t pos = *(uint32_t*)(uintptr_t)0x592de4;
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
  "  jmp *g_chanset_real\n"  // tail-jmp: the real SET's ret returns to site+5
);

// Chorus per-sample state ledger (C1_CHORLOG=<ch>, wide window C1_CHOR_LO/HI on
// abs OUTPUT sample, tight per-sample window C1_CHOR_SLO/SHI): detours the
// CHANNEL chorus's call into the ModDel per-sample core (@0x40b287 ->
// 0x40b157). At the call site ebp = ModDel obj, edx = dbptr. 2000 obj layout
// (from the core disasm): +0x00 dbL +0x04 dbR +0x08 mask +0x0c dbptr(store)
// +0x10 dboffsL +0x14 dboffsR +0x18 mcnt +0x1c mfreq +0x20 mphase
// +0x24 mmaxoffs. The chorus obj lives inside the channel block
// @0x718cd8+ch*0x78. Counterpart of the port's CHORLOG=<ch>; lines
// byte-comparable ([chorK] per chunk, [chorS] per sample, both channels).
// NB needs C1_VCEFRAME armed for the chunk-pos clock (g_vce_pos).
#define VA_CHORUS_CALL 0x40b287u
extern uint32_t g_vce_pos;
const uint32_t g_moddel_real = 0x40b157u;
int g_chor_ch = -1;
uint32_t g_chor_lo = 0, g_chor_hi = 0, g_chor_slo = 0, g_chor_shi = 0;
uint32_t g_chor_calls = 0;   // per-chunk sample counter for the target chorus

static void chor_detail(uint32_t ebp, uint32_t dbptr, int ch, uint32_t pos)
{
  uint32_t mcnt  = *(uint32_t*)(uintptr_t)(ebp+0x18);
  uint32_t mph   = *(uint32_t*)(uintptr_t)(ebp+0x20);
  uint32_t mmax  = *(uint32_t*)(uintptr_t)(ebp+0x24);
  uint32_t mask  = *(uint32_t*)(uintptr_t)(ebp+0x08);
  uint32_t dboff = *(uint32_t*)(uintptr_t)(ebp + (ch ? 0x14 : 0x10));
  const float *db = *(const float**)(uintptr_t)(ebp + (ch ? 0x04 : 0x00));
  uint32_t counter = mcnt + (ch ? mph : 0);
  counter = (counter < 0x80000000u) ? counter*2u : 0xffffffffu - counter*2u;
  uint64_t offs64 = (uint64_t)counter * mmax;
  uint32_t offs_int = (uint32_t)(offs64 >> 32) + dboff;
  uint32_t idx = dbptr - offs_int - (ch ? 1u : 0u);
  uint32_t in1 = *(uint32_t*)&db[idx & mask];
  uint32_t in2 = *(uint32_t*)&db[(idx+1u) & mask];
  fprintf(stderr, "[chorS] pos=%u ch=%d dbptr=%u mcnt=%08x cnt=%08x offs=%u "
          "idx=%u frac=%08x in1=%08x in2=%08x\n",
          pos, ch, dbptr, mcnt, counter, offs_int, idx & mask,
          (uint32_t)(offs64 & 0xffffffffu), in1, in2);
}
void chorlog(uint32_t ebp, uint32_t edx)
{
  int ch = (int)((ebp - VA_CHANOBJ0) / 0x78u);
  if (ch != g_chor_ch) return;
  uint32_t pos = g_vce_pos + g_chor_calls;
  g_chor_calls++;
  if (pos < g_chor_lo || pos > g_chor_hi) return;
  if (g_chor_calls == 1) {
    fprintf(stderr, "[chorK] pos=%u dbptr=%u mcnt=%08x\n",
            pos, edx, *(uint32_t*)(uintptr_t)(ebp+0x18));
    // channel obj = chorus obj - 0x44 (2000 chain @0x40b5cc); dist obj at
    // chan+0x10: +0x0 mode (&7, jump table @0x40ac57), decimator +0x1c dcount
    // +0x20 dfreq +0x24 dvall +0x28 dvalr. fxr = [chan+0xc].
    uint32_t chan = ebp - 0x44u, dist = chan + 0x10u;
    fprintf(stderr, "[distK] pos=%u fxr=%u mode=%u dcount=%08x dfreq=%08x dvall=%08x dvalr=%08x\n",
            pos, *(uint32_t*)(uintptr_t)(chan+0xc),
            *(uint32_t*)(uintptr_t)(dist+0x0) & 7u,
            *(uint32_t*)(uintptr_t)(dist+0x1c), *(uint32_t*)(uintptr_t)(dist+0x20),
            *(uint32_t*)(uintptr_t)(dist+0x24), *(uint32_t*)(uintptr_t)(dist+0x28));
  }
  if (pos >= g_chor_slo && pos <= g_chor_shi) {
    chor_detail(ebp, edx, 0, pos);
    chor_detail(ebp, edx, 1, pos);
  }
}
extern void hook_chorus(void);
__asm__(
  ".text\n.globl hook_chorus\nhook_chorus:\n"
  "  pushal\n  fnsave g_fpu\n"
  "  pushl %edx\n  pushl %ebp\n"
  "  call chorlog\n  addl $8,%esp\n"
  "  frstor g_fpu\n  popal\n"
  "  jmp *g_moddel_real\n"  // tail-jmp: the real core's ret returns to site+5
);

// chgPitch trace (C1_FREQTRACE=1, window C1_FREQ_LO/HI on sample pos): log every
// genuine syOscChgPitch @0x40a49b -- (pos, osc obj, pitch/note INPUT bits, integer
// freq + nffrq OUTPUT bits) -- to byte-compare per-tick freq vs the port's FREQLOG
// ledger (the env2->osc-pitch mod path, ch2 pgm3 chase). Detours all 4 call sites
// (syOscSet per-tick @0x40a4f5 + the 3 noteOn calls @0x40af46/4e/56); ebp = osc.
#define VA_CHGPITCH 0x40a49bu
static const uint32_t CHGPITCH_CALLS[] = { 0x40a4f5u, 0x40af46u, 0x40af4eu, 0x40af56u };
const uint32_t g_chgpitch_real = VA_CHGPITCH;
int g_freqtrace_n = 0;

void freqtrace(uint32_t ebp)
{
  uint32_t pos = *(uint32_t*)(uintptr_t)0x592de4;
  const char *lo = getenv("C1_FREQ_LO"), *hi = getenv("C1_FREQ_HI");
  if (lo && pos < (uint32_t)strtoul(lo,0,10)) return;
  if (hi && pos > (uint32_t)strtoul(hi,0,10)) return;
  if (g_freqtrace_n >= 4000) return;
  g_freqtrace_n++;
  fprintf(stderr, "[chgp] pos=%u osc=%05x pitch=%08x note=%08x freq=%08x nffrq=%08x\n",
          pos, ebp - 0x716a18u,
          *(uint32_t*)(uintptr_t)(ebp+0x44), *(uint32_t*)(uintptr_t)(ebp+0x40),
          *(uint32_t*)(uintptr_t)(ebp+0x8),  *(uint32_t*)(uintptr_t)(ebp+0x20));
}

extern uint8_t g_fpu[128];   // FPU save area (defined with the premix hook below)
extern void hook_chgp(void);
__asm__(
  ".text\n.globl hook_chgp\nhook_chgp:\n"
  "  call *g_chgpitch_real\n"  // real chgPitch first (outputs land in the osc obj)
  "  pushal\n  fnsave g_fpu\n" // printf may touch x87; preserve stack AND ctrl word
  "  pushl %ebp\n  call freqtrace\n  addl $4, %esp\n"
  "  frstor g_fpu\n  popal\n"
  "  ret\n"
);

// ---- per-frame voice sub-stage tap ------------------------------------------
int   g_vce = 0;
uint32_t g_vce_lo = 0, g_vce_hi = 0xffffffffu;
uint32_t g_vce_pos = 0, g_vce_count = 0;
FILE *g_fvo = 0, *g_fvf = 0, *g_fvd = 0;
float g_acc_osc[320], g_acc_flt[320], g_acc_dist[320];
float g_acc_chan[640]; // stereo, post-volramp voice sum (pre channel-FX)
float g_acc_chanpost[640]; // stereo, chanbuf AFTER channel-FX chain
float g_acc_premix[640];   // stereo, channel-sum output buffer just before the lc/hc EQ
FILE *g_fvc = 0, *g_fvcp = 0, *g_fvpm = 0;
FILE *g_fvchunks = 0;  // chunk sidecar: {pos,count,chanfx_n,chanfx_fired} per chunk
FILE *g_fvcpre = 0;    // chanbuf BEFORE the voice render (stale-vs-zeroed audit)
FILE *g_fvcv = 0;      // chanbuf right AFTER the voice render (volramp output)
float g_acc_chanpre[640], g_acc_chanv[640];
int g_vce_chan = -1;   // channel to tap chanbuf for (= solo channel)
const uint32_t g_chunk_real = VA_CHUNK_REAL;
const uint32_t g_v2r_real   = VA_V2R_REAL;
uint8_t g_fpu[128] __attribute__((aligned(16)));

static void vce_snap(float *acc)
{
  const float *vb = (const float*)(uintptr_t)VA_VCEBUF;
  uint32_t n = g_vce_count; if (n > 320) n = 320;
  for (uint32_t i = 0; i < n; i++) acc[i] += vb[i];
}
void snap_osc(void)  { vce_snap(g_acc_osc); }
void snap_flt(void)  { vce_snap(g_acc_flt); }
void snap_dist(void) { vce_snap(g_acc_dist); }
void snap_premix(void)
{
  if (!g_fvpm) return;
  const float *ob = *(const float**)(uintptr_t)VA_OUTPTR; // current chunk output (channel sum, pre-EQ)
  uint32_t n = g_vce_count; if (n > 320) n = 320;
  for (uint32_t i = 0; i < 2*n; i++) g_acc_premix[i] += ob[i];
}
extern void hook_premix(void);
__asm__(
  ".text\n.globl hook_premix\nhook_premix:\n"
  "  pushal\n  fnsave g_fpu\n  call snap_premix\n  frstor g_fpu\n  popal\n"
  "  movl 0x726f94,%ecx\n"        // the overwritten `mov ecx,[0x726f94]`
  "  jmp *g_jmp_premix\n"
);
const uint32_t g_jmp_premix = VA_PREMIX_EQ + 6;

void chunk_pre(uint32_t cnt)
{
  g_vce_count = cnt;
  uint32_t n = cnt > 320 ? 320 : cnt;
  memset(g_acc_osc, 0, n*4); memset(g_acc_flt, 0, n*4); memset(g_acc_dist, 0, n*4);
  memset(g_acc_chan, 0, n*8);
  memset(g_acc_chanpost, 0, n*8);
  memset(g_acc_premix, 0, n*8);
  memset(g_acc_chanpre, 0, n*8);
  memset(g_acc_chanv, 0, n*8);
  g_chanfx_fired = 0; g_chanfx_n = 0;
  g_chor_calls = 0;   // chorus ledger: per-chunk sample counter
}
void chunk_post(void)
{
  // C1_CHANVTRACE=<ch>: log the modulated chanvol float (chan param store
  // @0x718918+ch*0x3c, first param = chanvol) + raw ctl7 byte whenever the
  // value changes. Counterpart of the port's CHANVTRACE.
  static int cvch = -2;
  if (cvch == -2) { const char *e = getenv("C1_CHANVTRACE"); cvch = e ? atoi(e) : -1; }
  if (cvch >= 0) {
    static uint32_t last = 0xffffffffu;
    uint32_t cv = *(uint32_t*)(uintptr_t)(0x718918u + 0x3cu*cvch);
    if (cv != last) {
      last = cv;
      fprintf(stderr, "[chanv] pos=%u ch%d chanvol=%08x ctl7=%u\n",
              g_vce_pos, cvch, cv, *(uint8_t*)(uintptr_t)(0x715c94u + 8u*cvch + 7u));
    }
  }
  uint32_t n = g_vce_count > 320 ? 320 : g_vce_count;
  if (g_vce_pos >= g_vce_lo && g_vce_pos <= g_vce_hi) {
    if (g_fvo) fwrite(g_acc_osc,  4, n, g_fvo);
    if (g_fvf) fwrite(g_acc_flt,  4, n, g_fvf);
    if (g_fvd) fwrite(g_acc_dist, 4, n, g_fvd);
    if (g_fvc) fwrite(g_acc_chan, 8, n, g_fvc);
    if (g_fvcp) fwrite(g_acc_chanpost, 8, n, g_fvcp);
    if (g_fvpm) fwrite(g_acc_premix, 8, n, g_fvpm);
    if (g_fvchunks) {
      uint32_t rec[4] = { g_vce_pos, g_vce_count, g_chanfx_n, g_chanfx_fired };
      fwrite(rec, sizeof rec, 1, g_fvchunks);
    }
    if (g_fvcpre) fwrite(g_acc_chanpre, 8, n, g_fvcpre);
    if (g_fvcv) fwrite(g_acc_chanv, 8, n, g_fvcv);
  }
  g_vce_pos += g_vce_count;
}

// detour @0x40b982: ecx = chunk sample count on entry
extern void hook_chunk(void);
__asm__(
  ".text\n.globl hook_chunk\nhook_chunk:\n"
  "  pushal\n  pushl %ecx\n  call chunk_pre\n  addl $4,%esp\n  popal\n"
  "  call *g_chunk_real\n"
  "  pushal\n  call chunk_post\n  popal\n  ret\n"
);
// wrap @0x40ba88: syV2Render, snapshot post-dist on return
// Pre-voice-render snapshot of the chanbuf (the volramp loop's accumulation
// TARGET, @0x7153c4) + per-piece voice cur/ramp log. Answers two questions the
// ch15 chase needs: (a) is the chanbuf zeroed or stale when the voice
// accumulates into it, (b) what cur/ramp does each sub-frame PIECE really use
// (no inference from output division). ebp = voice obj at the call site.
extern FILE *g_fvcpre, *g_fvcv;
extern float g_acc_chanpre[640], g_acc_chanv[640];
void v2r_pre(uint32_t ebp)
{
  if (g_vce_pos < g_vce_lo || g_vce_pos > g_vce_hi) return;
  if (g_fvcpre) {
    const float *cb = (const float*)(uintptr_t)VA_CHANBUF;
    uint32_t n = g_vce_count; if (n > 320) n = 320;
    for (uint32_t i = 0; i < 2*n; i++) g_acc_chanpre[i] += cb[i];
  }
  static int nlog = 0;
  if (nlog < 200) { nlog++;
    fprintf(stderr, "[v2r] pos=%u n=%u vc=%05x cur=%08x ramp=%08x\n",
            g_vce_pos, g_vce_count, ebp - 0x716a18u,
            *(uint32_t*)(uintptr_t)(ebp+0xc), *(uint32_t*)(uintptr_t)(ebp+0x10));
  }
}
void v2r_post(void)
{
  // chanbuf IMMEDIATELY after the voice render (volramp loop output) --
  // before any other writer can touch it. If this matches voice*cv*lvol but
  // the FX-call-time .chan tap does not, something rewrites chanbuf between.
  if (g_vce_pos < g_vce_lo || g_vce_pos > g_vce_hi) return;
  if (g_fvcv) {
    const float *cb = (const float*)(uintptr_t)VA_CHANBUF;
    uint32_t n = g_vce_count; if (n > 320) n = 320;
    for (uint32_t i = 0; i < 2*n; i++) g_acc_chanv[i] += cb[i];
  }
}
extern void hook_v2r(void);
__asm__(
  ".text\n.globl hook_v2r\nhook_v2r:\n"
  "  pushal\n  fnsave g_fpu\n"
  "  pushl %ebp\n  call v2r_pre\n  addl $4,%esp\n"
  "  frstor g_fpu\n  popal\n"
  "  call *g_v2r_real\n"
  "  pushal\n  fnsave g_fpu\n  call snap_dist\n  call v2r_post\n  frstor g_fpu\n  popal\n  ret\n"
);
// detour @0x40ad76: post-osc, then the overwritten `lea ebp,[ebp-0xb4]`
extern void hook_postosc(void);
__asm__(
  ".text\n.globl hook_postosc\nhook_postosc:\n"
  "  pushal\n  fnsave g_fpu\n  call snap_osc\n  frstor g_fpu\n  popal\n"
  "  lea -0xb4(%ebp),%ebp\n"
  "  jmp *g_jmp_postosc\n"
);
// detour @0x40adfe: post-flt, then the overwritten `mov esi,0x714fc4`
extern void hook_postflt(void);
__asm__(
  ".text\n.globl hook_postflt\nhook_postflt:\n"
  "  pushal\n  fnsave g_fpu\n  call snap_flt\n  frstor g_fpu\n  popal\n"
  "  movl $0x714fc4,%esi\n"
  "  jmp *g_jmp_postflt\n"
);
const uint32_t g_jmp_postosc = VA_POSTOSC + 6;  // resume after the 6-byte lea
const uint32_t g_jmp_postflt = VA_POSTFLT + 5;  // resume after the 5-byte mov

static void install_detour(uint32_t site, void *target, int len)
{
  uint8_t *s = (uint8_t*)(uintptr_t)site;
  s[0] = 0xe9; // jmp rel32
  int32_t rel = (int32_t)((uintptr_t)target - (site + 5));
  memcpy(s+1, &rel, 4);
  for (int i=5;i<len;i++) s[i]=0x90;
}
static void install_call(uint32_t site, void *target)
{
  uint8_t *s = (uint8_t*)(uintptr_t)site;
  int32_t rel = (int32_t)((uintptr_t)target - (site + 5));
  s[0]=0xe8; memcpy(s+1,&rel,4);
}

extern void hook_chan(void);
__asm__(
  ".text\n"
  ".globl hook_chan\n"
  "hook_chan:\n"
  "  pushal\n"
  "  pushl %ebp\n"
  "  call chanstream_pre\n"
  "  addl $4, %esp\n"
  "  popal\n"
  "  call *g_chanfx_real\n"   // real chan FX (pusha/popa inside; preserves regs)
  "  pushal\n"
  "  pushl %ebp\n"
  "  call chanstream_post\n"
  "  addl $4, %esp\n"
  "  popal\n"
  "  ret\n"
);

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

  // Ronan speech PROCESS call: the render driver runs the speech filter on
  // ch15's chanbuf (cmp cl,0xf @0x40ba95; call 0x40b7ba @0x40baac) between the
  // voice render and the channel FX -- EVEN with Ronan init nop'd above (the
  // old "speech silently absent" assumption was wrong: it processes with
  // uninitialized state). The port has no Ronan; nop the process call so ch15
  // is the raw voice chain on both sides. Override with C1_RONAN=1 to keep it.
  if (!getenv("C1_RONAN")) {
    uint8_t *rp = (uint8_t*)(uintptr_t)0x40baacu;
    if (rp[0]==0xe8) memset(rp, 0x90, 5);
    else fprintf(stderr,"[solo] WARNING: no call at ronan process site (%02x)\n",rp[0]);
  }

  // optional FX-return mutes (pair with port-side MUTEREVERB/MUTEDELAY)
  if (getenv("C1_MUTE_REVERB")) {
    uint8_t *s = (uint8_t*)(uintptr_t)VA_RVB_CALL;
    if (s[0] != 0xe8) { fprintf(stderr,"no call at rvb site (%02x)\n",s[0]); return 1; }
    memset(s, 0x90, 5); fprintf(stderr,"[solo] reverb render muted\n");
  }
  if (getenv("C1_MUTE_DELAY")) {
    uint8_t *s = (uint8_t*)(uintptr_t)VA_DEL_CALL;
    if (s[0] != 0xe8) { fprintf(stderr,"no call at del site (%02x)\n",s[0]); return 1; }
    memset(s, 0x90, 5); fprintf(stderr,"[solo] delay render muted\n");
  }

  // optional chan-stream tap (pair with port-side CHANSTREAM=<prefix>[:ch])
  const char *cse = getenv("C1_CHANSTREAM");
  if (cse) {
    char pfx[512]; int ch = 10;
    const char *c = strrchr(cse, ':');
    if (c) { snprintf(pfx, sizeof pfx, "%.*s", (int)(c-cse), cse); ch = atoi(c+1); }
    else    snprintf(pfx, sizeof pfx, "%s", cse);
    char pp[600];
    snprintf(pp, sizeof pp, "%s.pre",  pfx); g_cs_pre  = fopen(pp, "wb");
    snprintf(pp, sizeof pp, "%s.post", pfx); g_cs_post = fopen(pp, "wb");
    g_cs_ch = ch;
    uint8_t *site = (uint8_t*)(uintptr_t)VA_CHAN_CALL;
    if (site[0] != 0xe8) { fprintf(stderr,"no call at chanFX site (%02x)\n",site[0]); return 1; }
    int32_t rel = (int32_t)((uintptr_t)&hook_chan - (VA_CHAN_CALL + 5));
    memcpy(site+1, &rel, 4);
    fprintf(stderr,"[solo] chanstream tap on ch%d -> %s.{pre,post}\n", ch, pfx);
  }

  // optional per-frame voice sub-stage tap
  if (getenv("C1_VCEFRAME")) {
    const char *pfx = getenv("C1_VCEFRAME");
    const char *lo = getenv("C1_VCE_LO"), *hi = getenv("C1_VCE_HI");
    if (lo) g_vce_lo = (uint32_t)strtoul(lo,0,10);
    if (hi) g_vce_hi = (uint32_t)strtoul(hi,0,10);
    char p[600];
    snprintf(p,sizeof p,"%s.osc",pfx);  g_fvo=fopen(p,"wb");
    snprintf(p,sizeof p,"%s.flt",pfx);  g_fvf=fopen(p,"wb");
    snprintf(p,sizeof p,"%s.dist",pfx); g_fvd=fopen(p,"wb");
    snprintf(p,sizeof p,"%s.chan",pfx); g_fvc=fopen(p,"wb");
    snprintf(p,sizeof p,"%s.chanpost",pfx); g_fvcp=fopen(p,"wb");
    snprintf(p,sizeof p,"%s.premix",pfx); g_fvpm=fopen(p,"wb");
    snprintf(p,sizeof p,"%s.chunks",pfx); g_fvchunks=fopen(p,"wb");
    snprintf(p,sizeof p,"%s.chanpre",pfx); g_fvcpre=fopen(p,"wb");
    snprintf(p,sizeof p,"%s.chanv",pfx); g_fvcv=fopen(p,"wb");
    g_vce_chan = g_solo;  // tap chanbuf for the soloed channel...
    // ...or any channel in a FULL-MIX render (C1_VCE_CH=N without C1_SOLO):
    if (getenv("C1_VCE_CH")) g_vce_chan = atoi(getenv("C1_VCE_CH"));
    // verify opcodes before patching
    uint8_t *c1=(uint8_t*)(uintptr_t)VA_CHUNK_CALL, *c2=(uint8_t*)(uintptr_t)VA_V2R_CALL;
    uint8_t *po=(uint8_t*)(uintptr_t)VA_POSTOSC,   *pf=(uint8_t*)(uintptr_t)VA_POSTFLT;
    if (c1[0]!=0xe8||c2[0]!=0xe8||po[0]!=0x8d||pf[0]!=0xbe) {
      fprintf(stderr,"[vce] opcode check failed %02x %02x %02x %02x\n",c1[0],c2[0],po[0],pf[0]); return 1; }
    install_call(VA_CHUNK_CALL, &hook_chunk);
    install_call(VA_V2R_CALL,   &hook_v2r);
    install_detour(VA_POSTOSC, &hook_postosc, 6);
    install_detour(VA_POSTFLT, &hook_postflt, 5);
    { uint8_t *pe=(uint8_t*)(uintptr_t)VA_PREMIX_EQ;
      if (pe[0]==0x8b) install_detour(VA_PREMIX_EQ, &hook_premix, 6);
      else fprintf(stderr,"[vce] premix EQ opcode %02x (skipped)\n",pe[0]); }
    // chanbuf (post-volramp voice sum) tap needs the channel-FX detour too
    { uint8_t *cc=(uint8_t*)(uintptr_t)VA_CHAN_CALL;
      if (cc[0]==0xe8) install_call(VA_CHAN_CALL, &hook_chan); }
    g_vce = 1;
    fprintf(stderr,"[vce] frame tap armed [%u..%u] -> %s.{osc,flt,dist}\n",g_vce_lo,g_vce_hi,pfx);
  }

  // optional chorus state ledger (needs C1_VCEFRAME for the chunk-pos clock)
  if (getenv("C1_CHORLOG")) {
    g_chor_ch = atoi(getenv("C1_CHORLOG"));
    const char *a;
    if ((a = getenv("C1_CHOR_LO")))  g_chor_lo  = (uint32_t)strtoul(a,0,10);
    if ((a = getenv("C1_CHOR_HI")))  g_chor_hi  = (uint32_t)strtoul(a,0,10);
    if ((a = getenv("C1_CHOR_SLO"))) g_chor_slo = (uint32_t)strtoul(a,0,10);
    if ((a = getenv("C1_CHOR_SHI"))) g_chor_shi = (uint32_t)strtoul(a,0,10);
    uint8_t *site = (uint8_t*)(uintptr_t)VA_CHORUS_CALL;
    if (site[0] != 0xe8) { fprintf(stderr,"no call at chorus site (%02x)\n",site[0]); return 1; }
    int32_t rel = (int32_t)((uintptr_t)&hook_chorus - (VA_CHORUS_CALL + 5));
    memcpy(site+1, &rel, 4);
    if (!getenv("C1_VCEFRAME"))
      fprintf(stderr,"[chorlog] WARNING: C1_VCEFRAME not set -- pos clock dead\n");
    fprintf(stderr,"[chorlog] ch%d armed [%u..%u] detail [%u..%u]\n",
            g_chor_ch, g_chor_lo, g_chor_hi, g_chor_slo, g_chor_shi);
  }

  // optional voice-tick trace
  if (getenv("C1_TICKLOG")) {
    uint8_t *site = (uint8_t*)(uintptr_t)VA_TICK_CALL;
    if (site[0] != 0xe8) { fprintf(stderr,"no call at tick site (%02x)\n",site[0]); return 1; }
    int32_t rel = (int32_t)((uintptr_t)&hook_tick - (VA_TICK_CALL + 5));
    memcpy(site+1, &rel, 4);
    fprintf(stderr,"[solo] tick log armed\n");
  }

  // optional alloc trace
  if (getenv("C1_ALLOCTRACE")) {
    uint8_t *site = (uint8_t*)(uintptr_t)VA_ALLOC_SET_CALL;
    if (site[0] != 0xe8) { fprintf(stderr,"no call at alloc-set site (%02x)\n",site[0]); return 1; }
    int32_t rel = (int32_t)((uintptr_t)&hook_allocset - (VA_ALLOC_SET_CALL + 5));
    memcpy(site+1, &rel, 4);
    fprintf(stderr,"[solo] alloc trace armed\n");
  }

  // optional chgPitch trace (all 4 call sites)
  if (getenv("C1_FREQTRACE")) {
    for (unsigned i = 0; i < 4; i++) {
      uint8_t *site = (uint8_t*)(uintptr_t)CHGPITCH_CALLS[i];
      if (site[0] != 0xe8) { fprintf(stderr,"no call at chgp site %x (%02x)\n",CHGPITCH_CALLS[i],site[0]); return 1; }
      int32_t rel = (int32_t)((uintptr_t)&hook_chgp - (CHGPITCH_CALLS[i] + 5));
      memcpy(site+1, &rel, 4);
    }
    fprintf(stderr,"[solo] chgPitch trace armed\n");
  }

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
  if (getenv("REVDUMP")) {
    uint32_t *rv = (uint32_t*)(uintptr_t)0x7194e0u; // reverb obj: gainc[0..3], gaina[0..1] at +0x10/0x14
    fprintf(stderr,"[revdump] gainc=%08x,%08x,%08x,%08x gaina=%08x,%08x damp=%08x gainin=%08x\n",
            rv[0],rv[1],rv[2],rv[3], rv[4],rv[5], rv[7], rv[6]);
  }
  fprintf(stderr,"[solo] wrote %.1fs -> %s\n", (double)total/44100.0, out);
  return 0;
}
