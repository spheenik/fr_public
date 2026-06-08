// c1_fr014_harness -- ground-truth harness for fr-014 "mark&sweep"'s 2001-12
// V2 synth (the FIRST format-v3 engine). Direct analogue of
// ../flybye-extraction/c1_flybye_harness.c: map the depacked image at
// 0x400000, pin the rdtsc seed sites for determinism, call the demo's own
// OpenV2M + PlayV2M + RenderProxy on the in-image embedded v3 song (carve
// index 0), and write interleaved stereo f32. This is the v3 ORACLE: diff its
// output against the portable's native-v3 render to turn the era-gap-sweep v3
// row verdicts from assayed into bit-exact proven.
//
// Build (32-bit, no-pie so 0x400000 is free; the synth sets its own x87 CW):
//   gcc -m32 -no-pie -O0 c1_fr014_harness.c -o c1_fr014_harness
// Run:
//   ../toolkit/era unpack 'mark&sweep.exe' /tmp/erascan_recon/fr014/unpacked.bin
//   ./c1_fr014_harness /tmp/erascan_recon/fr014/unpacked.bin out.f32 4096 40
//
// Entry points (located by disasm; structurally fr08/flybye-like):
//   OpenV2M     @0x40d93d  (v2m ptr) -- parses header -> player globals
//                          (timediv@0x477d40, [+4]@0x477d48, [+8]@0x477d50)
//   PlayV2M     @0x40da6d  ()        -- stop(0x40daa3) + Reset(0x40d13b) + play
//   RenderProxy @0x40d7f3  (f32 *buf, u32 n) stdcall ret 8 -- fill; calls
//                          synthRender @0x40ff6f + PlayTick @0x40d345
//   synthInit   @0x40feb2  -- zeroes SYN@0x50a374 size 0x1e27b4, 32 voices
//                          @0x50c184 stride 0x210 (v3 POLY=32)
//   embedded v3 v2m @0x41618b (carve 0: timediv 96, maxtime 26904, 9 active ch)
//   playing flag @0x476af0
//   rdtsc seeds  @0x40e4a0 / 0x40ead9 / 0x40ec01  (0f31 -> 31c0, seed=0)

#include <stdint.h>

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x2f0000u            // fr014 unpacked.bin = 3080192 bytes

// shared native oracle scaffold (mmap@0x400000 + fault reporter + rdtsc-pin + f32)
#define ORACLE_IMG_SIZE IMG_SIZE
#include "../toolkit/oracle.h"

#define VA_V2M       0x41618bu        // embedded v3 song0 (timediv 96)
#define VA_OPEN_V2M  0x40d93du
#define VA_PLAY_V2M  0x40da6du
#define VA_RENDER    0x40d7f3u        // stdcall ret 8
#define VA_PLAYING   0x476af0u        // dword: sequencer running
#define VA_TIMEDIV   0x477d40u
#define VA_MAXTIME   0x477d48u
#define VA_GDNUM     0x477d50u
static const uint32_t RDTSC_SITES[] = { 0x40e4a0u, 0x40ead9u, 0x40ec01u };

#define SAMPLE_RATE  44100u
#define TAIL_SECONDS 6u

typedef void (__attribute__((stdcall)) *render_fn)(float *buf, uint32_t n);
static uint32_t rd32(uint32_t va) { return *(volatile uint32_t *)(uintptr_t)va; }

int main(int argc, char **argv)
{
    const char *path    = (argc > 1) ? argv[1] : "/tmp/erascan_recon/fr014/unpacked.bin";
    const char *outpath = (argc > 2) ? argv[2] : "/tmp/erascan_recon/fr014/c1_fr014.f32";
    uint32_t chunk      = (argc > 3) ? (uint32_t)atoi(argv[3]) : 4096u;
    uint64_t max_smp    = ((argc > 4) ? (uint64_t)atoi(argv[4]) : 40u) * SAMPLE_RATE;
    if (!chunk) chunk = 4096u;

    oracle_install_faults();
    oracle_map_image(path, IMG_SIZE);
    oracle_pin_rdtsc(RDTSC_SITES, sizeof(RDTSC_SITES) / sizeof(RDTSC_SITES[0]));
    fprintf(stderr, "[c1014] timediv@v2m=%u\n", rd32(VA_V2M));

    // Skip the RONAN (speech) init inside OpenV2M -- a 2-arg stdcall (call
    // 0x40df02) whose body hits null IAT thunks (-> call to 0). nop the two arg
    // pushes + the call, keeping the stack balanced (stdcall: callee would pop
    // the 8 bytes; removing both pushes + call is neutral). The interleaved
    // state-init movs (clear playing/counters) are left intact. (== c1_flybye's
    // Ronan-init skip; song0 has spsize=0 so no speech is lost.)
    {
        uint8_t *a2 = (uint8_t *)0x40d9e6u; // push [esp+0x10]  ff 74 24 10 (4)
        uint8_t *a1 = (uint8_t *)0x40d9ecu; // push 0x40d7f3    68 f3 d7 40 00 (5)
        uint8_t *cl = (uint8_t *)0x40da0au; // call 0x40df02    e8 f3 04 00 00 (5)
        if (a2[0]==0xff && a1[0]==0x68 && cl[0]==0xe8) {
            memset(a2,0x90,4); memset(a1,0x90,5); memset(cl,0x90,5);
            fprintf(stderr, "[c1014] Ronan init nop'd (speech gated)\n");
        } else fprintf(stderr, "[c1014] WARN unexpected Ronan bytes %02x %02x %02x\n",
                       a2[0], a1[0], cl[0]);
    }

    // OpenV2M(v2m) -- convention-agnostic: save esp and restore after, so cdecl
    // (ret 0) or stdcall (ret N) both balance. Push a spare 2nd arg too.
    fprintf(stderr, "[c1014] OpenV2M(0x%x)...\n", VA_V2M);
    uint32_t esp_save = 0;
    __asm__ volatile (
        "movl %%esp, %0\n\t"
        "pushl $0\n\t"
        "pushl %2\n\t"
        "call  *%3\n\t"
        "movl  %0, %%esp\n\t"
        : "+m"(esp_save)
        : "0"(esp_save), "i"(VA_V2M), "r"(VA_OPEN_V2M)
        : "eax","ecx","edx","esi","edi","cc","memory");
    fprintf(stderr, "[c1014] OpenV2M ok: timediv=%u maxtime=%u gdnum=%u\n",
            rd32(VA_TIMEDIV), rd32(VA_MAXTIME), rd32(VA_GDNUM));

    // channel solo (FR014_SOLO=<ch>): zero notenum for every other channel in
    // the parsed channel table (notenum[ch] = *(0x477d54 + ch*0x50), from
    // OpenV2M's build loop). Must run AFTER OpenV2M, BEFORE PlayV2M.
    const char *soloenv = getenv("FR014_SOLO");
    if (soloenv) {
        int solo = atoi(soloenv);
        fprintf(stderr, "[c1014] notenum table:");
        for (int ch = 0; ch < 16; ch++) {
            volatile uint32_t *nn = (volatile uint32_t *)(uintptr_t)(0x477d54u + (uint32_t)ch * 0x50u);
            fprintf(stderr, " ch%d=%u", ch, *nn);
            if (ch != solo) *nn = 0;
        }
        fprintf(stderr, "\n[c1014] soloed ch%d\n", solo);
    }

    // FR014_STAGE: isolate the voice chain to measure per-stage output in the
    // voice buffer 0x509730. NOP the 5-byte `call` at the stage boundaries
    // (serial routing: vcf0@0x40efcc, vcf1@0x40f020, dist@0x40f03b).
    //   STAGE=osc -> NOP vcf0,vcf1,dist (buffer = osc sum)
    //   STAGE=flt -> NOP dist           (buffer = post-filter)
    {
        const char *st = getenv("FR014_STAGE");
        if (st) {
            uint8_t *vcf0 = (uint8_t *)0x40efccu, *vcf1 = (uint8_t *)0x40f020u, *dst = (uint8_t *)0x40f03bu;
            int nop_dist = 1, nop_flt = (strcmp(st,"osc")==0);
            if (nop_dist && dst[0]==0xe8) { memset(dst,0x90,5); fprintf(stderr,"[c1014] NOP dist\n"); }
            if (nop_flt) {
                if (vcf0[0]==0xe8){ memset(vcf0,0x90,5); fprintf(stderr,"[c1014] NOP vcf0\n"); }
                if (vcf1[0]==0xe8){ memset(vcf1,0x90,5); fprintf(stderr,"[c1014] NOP vcf1\n"); }
            }
        }
    }

    // FR014_NOCOMP / FR014_NOBOOST / FR014_NODIST / FR014_NOCHORUS: stage
    // isolation for the per-channel FX chain (syChanRender @0x40fcda). The render
    // order is COMP(0x40f945) -> BOOST(0x40f40a) -> {DIST(0x40ee75),
    // CHORUS(0x40f612)} swapped by the fxr flag [chanstruct+0xc]. NOP the 5-byte
    // `call` at each stage's call site(s); skipping a stage leaves the in-place
    // channel buffer 0x509b30 untouched (== portable V2_NOCOMP/V2_NOCHORUS).
    // Use with FR014_SOLO=8 FR014_BUFPEAK to build the binary isolation table.
    {
        struct { const char *env; uint32_t site[2]; } stg[4] = {
            { "FR014_NOCOMP",   { 0x40fce8u, 0u } },        // call COMP   0x40f945
            { "FR014_NOBOOST",  { 0x40fcf3u, 0u } },        // call BOOST  0x40f40a
            { "FR014_NODIST",   { 0x40fd05u, 0x40fd28u } }, // call DIST   0x40ee75 (both fxr paths)
            { "FR014_NOCHORUS", { 0x40fd0du, 0x40fd20u } }, // call CHORUS 0x40f612 (both fxr paths)
        };
        for (int s = 0; s < 4; s++) {
            if (!getenv(stg[s].env)) continue;
            for (int k = 0; k < 2; k++) {
                uint32_t va = stg[s].site[k];
                if (!va) continue;
                uint8_t *p = (uint8_t *)(uintptr_t)va;
                if (p[0] == 0xe8) { memset(p, 0x90, 5); fprintf(stderr, "[c1014] %s: NOP call @0x%x\n", stg[s].env, va); }
                else fprintf(stderr, "[c1014] WARN %s: @0x%x not a call (%02x)\n", stg[s].env, va, p[0]);
            }
        }
    }

    // FR014_NOISERAW: bypass the noise resonator so the buffer = raw noise n*gain
    // (NOP the recurrence math 0x40e81a..0x40e835; stack stays balanced: n stays
    // in st0 -> 0x40e836 fmul gain -> output). Directly measures the noise input
    // range fed to the resonator.
    if (getenv("FR014_NOISERAW")) {
        uint8_t *p = (uint8_t *)0x40e81au; size_t n = 0x40e836u - 0x40e81au;
        memset(p, 0x90, n);
        fprintf(stderr, "[c1014] noise resonator NOP'd (raw noise mode, %zu bytes)\n", n);
    }

    // PlayV2M() -- void; esp-guarded for safety
    __asm__ volatile (
        "movl %%esp, %0\n\t"
        "call *%2\n\t"
        "movl %0, %%esp\n\t"
        : "+m"(esp_save)
        : "0"(esp_save), "r"(VA_PLAY_V2M)
        : "eax","ecx","edx","esi","edi","cc","memory");
    fprintf(stderr, "[c1014] PlayV2M: playing=%u\n", rd32(VA_PLAYING));

    FILE *out = fopen(outpath, "wb");
    if (!out) { fprintf(stderr, "cannot write %s\n", outpath); return 1; }
    float *buf = malloc((size_t)chunk * 2 * sizeof(float));
    render_fn render = (render_fn)(uintptr_t)VA_RENDER;
    volatile uint32_t *playing = (volatile uint32_t *)(uintptr_t)VA_PLAYING;
    // one-shot voice-workspace dump (FR014_DUMPVOX=<seconds>): read each voice's
    // 3 oscs (mode@+0, nffrq@+0x14, nfres@+0x18) to compare the binary's parsed
    // resonance against the portable's. voice base 0x50c184, stride 0x210;
    // oscs at voice+0x30 / +0x6c / +0xa8.
    const char *dvenv = getenv("FR014_DUMPVOX");
    uint64_t dumpvox_at = dvenv ? (uint64_t)(atof(dvenv) * SAMPLE_RATE) : ~0ull;
    int dumpvox_done = 0;

    // FR014_BUFPEAK: track the running max of the voice buffer 0x509730 (for v3
    // this holds the post-bitcrusher voice signal after each sub-render, since
    // there is no per-voice DCF). Use with chunk=128 to sample every sub-frame.
    const int bufpeak_on = getenv("FR014_BUFPEAK") ? 1 : 0;
    float bufpeak = 0.0f; double bufpeak_t = 0.0;
    float chanpeak = 0.0f; double chanpeak_t = 0.0;  // pre-master channel mix 0x509b30
    uint64_t total = 0, tail_left = ~0ull, next_log = 0;
    while (total < max_smp) {
        render(buf, chunk);
        if (bufpeak_on) {
            const volatile float *vb = (const volatile float *)(uintptr_t)0x509730u;
            for (int k = 0; k < 256; k++) {
                float a = vb[k] < 0 ? -vb[k] : vb[k];
                if (a > bufpeak && a < 1e4f) { bufpeak = a; bufpeak_t = (double)total / SAMPLE_RATE; }
            }
            const volatile float *cb = (const volatile float *)(uintptr_t)0x509b30u;
            for (int k = 0; k < 256; k++) {
                float a = cb[k] < 0 ? -cb[k] : cb[k];
                if (a > chanpeak && a < 1e4f) { chanpeak = a; chanpeak_t = (double)total / SAMPLE_RATE; }
            }
            // one-shot: print first 16 buffer samples once any are nonzero (onset)
            static int shown_onset = 0;
            if (!shown_onset) {
                int any = 0; for (int k=0;k<16;k++) if (vb[k]!=0.0f) any=1;
                if (any) { shown_onset = 1;
                    fprintf(stderr, "[c1014] onset buf @%.3fs:", (double)total/SAMPLE_RATE);
                    for (int k=0;k<24;k++) fprintf(stderr, " %.7f", vb[k]);
                    fprintf(stderr, "\n");
                    // osc0 coeffs of the voice on slot used (scan voices for active noise)
                    for (int v=0; v<32; v++) {
                        uint32_t ob = 0x50c184u + (uint32_t)v*0x210u + 0x30u;
                        float f=*(volatile float*)(uintptr_t)(ob+0x14u);
                        float r=*(volatile float*)(uintptr_t)(ob+0x18u);
                        float g=*(volatile float*)(uintptr_t)(ob+0x20u);
                        float s1=*(volatile float*)(uintptr_t)(ob+0x30u);
                        float s2=*(volatile float*)(uintptr_t)(ob+0x2cu);
                        uint32_t sd=*(volatile uint32_t*)(uintptr_t)(ob+0x1cu);
                        if (f!=0.0f) {
                            fprintf(stderr,"   v%d osc0 nffrq=%.6f nfres=%.6f gain=%.6f st1=%.4f st2=%.4f seed=%u\n",v,f,r,g,s1,s2,sd);
                            // step the LCG from this seed to show the next n values
                            uint32_t s=sd; fprintf(stderr,"     n[]:");
                            for(int q=0;q<6;q++){ s=s*214013u+2531011u; uint32_t bits=((s&0xffff)<<7)|0x40000000u; float nf; memcpy(&nf,&bits,4); fprintf(stderr," %.4f", nf-3.0f);} fprintf(stderr,"\n");
                        }
                    }
                }
            }
        }
        if (fwrite(buf, 2 * sizeof(float), chunk, out) != chunk) {
            fprintf(stderr, "short write\n"); return 1; }
        total += chunk;
        if (!dumpvox_done && total >= dumpvox_at) {
            dumpvox_done = 1;
            fprintf(stderr, "\n[c1014] voice-workspace dump @%.2fs:\n",
                    (double)total / SAMPLE_RATE);
            // per-voice param FLOAT array (storeV2Values writes 0x39 floats to
            // 0x50a504 + v*0xe4; idx37 = Amp-EG "Amplify" at +0x94). Print it for
            // any voice with active noise oscs so we see the binary's post-mod
            // gain (curvol source) without guessing voice-workspace offsets.
            for (int v = 0; v < 32; v++) {
                uint32_t pb = 0x50a504u + (uint32_t)v * 0xe4u;
                float ampl = *(volatile float *)(uintptr_t)(pb + 37u*4u);
                float aenv_ar = *(volatile float *)(uintptr_t)(pb + 32u*4u);
                float aenv_dr = *(volatile float *)(uintptr_t)(pb + 33u*4u);
                float aenv_sl = *(volatile float *)(uintptr_t)(pb + 34u*4u);
                float f0cut  = *(volatile float *)(uintptr_t)(pb + 21u*4u);
                if (ampl==0.0f && aenv_ar==0.0f && f0cut==0.0f) continue;
                fprintf(stderr, "  v%-2d PARAM[37 Amplify]=%.4f aenv(ar=%.2f dr=%.2f sl=%.2f) flt0.cut=%.4f\n",
                        v, ampl, aenv_ar, aenv_dr, aenv_sl, f0cut);
            }
            const uint32_t OSCOFF[3] = { 0x30u, 0x6cu, 0xa8u };
            for (int v = 0; v < 32; v++) {
                uint32_t vb = 0x50c184u + (uint32_t)v * 0x210u;
                // whole voice (0x0..0x210) -- print nonzero floats in [1e-4,1e3]
                // to find curvol/lvol/rvol (env-driven volumes) for the noise voice
                if (v == 6) {
                    fprintf(stderr, "  v%-2d nonzero floats:", v);
                    for (int k = 0; k < 0x210; k += 4) {
                        float fv = *(volatile float *)(uintptr_t)(vb + (uint32_t)k);
                        float a = fv < 0 ? -fv : fv;
                        if (a > 1e-4f && a < 1e3f)
                            fprintf(stderr, " +%02x=%.4g", k, fv);
                    }
                    fprintf(stderr, "\n");
                }
                for (int o = 0; o < 3; o++) {
                    uint32_t ob = vb + OSCOFF[o];
                    int   mode  = (int)rd32(ob + 0x0u);
                    float nffrq = *(volatile float *)(uintptr_t)(ob + 0x14u);
                    float nfres = *(volatile float *)(uintptr_t)(ob + 0x18u);
                    if (nffrq != 0.0f || nfres != 0.0f) {
                        uint32_t fb, rb; memcpy(&fb,&nffrq,4); memcpy(&rb,&nfres,4);
                        fprintf(stderr, "  v%-2d osc%d mode=%d nffrq=%.9g[%08x] nfres=%.9g[%08x]\n",
                                v, o, mode, nffrq, fb, nfres, rb);
                    }
                }
            }
            // CHANNEL post-mod value array (storeV2Values writes 25 floats per
            // channel @0x510384 + ch*0x64). Layout == syVChan: [0]chanvol [1]aarcv
            // [2]abrcv [3]aasnd [4]absnd [5]aux1 [6]aux2 [7]fxroute, then FX. The
            // per-channel COMP params occupy values[16..24] (offset 0x40; comp set
            // 0x40f654 reads esi[0..8] = mode/stereo/autogain/lookahead/threshold/
            // ratio/attack/release/outgain). Dump ch8 to compare vs the portable.
            {
                uint32_t cv = 0x510384u + 8u * 0x64u;   // ch8 value base
                fprintf(stderr, "  [ch8 chanvals]");
                for (int k = 0; k < 25; k++)
                    fprintf(stderr, " [%d]=%.3f", k, *(volatile float *)(uintptr_t)(cv + (uint32_t)k*4u));
                fprintf(stderr, "\n");
                float *C = (float *)(uintptr_t)(cv + 0x40u);
                fprintf(stderr, "  [ch8 COMP] mode=%.0f stereo=%.0f autogain=%.0f lookahd=%.2f thresh=%.0f ratio=%.0f attack=%.0f release=%.0f outgain=%.0f\n",
                        C[0], C[1], C[2], C[3], C[4], C[5], C[6], C[7], C[8]);
            }
        }
        if (total >= next_log) {
            fprintf(stderr, "[c1014] %6.1fs (playing=%u)\r",
                    (double)total / SAMPLE_RATE, *playing);
            next_log += 30u * SAMPLE_RATE;
        }
        if (!*playing && tail_left == ~0ull) {
            fprintf(stderr, "\n[c1014] song end @%.1fs -- %us tail\n",
                    (double)total / SAMPLE_RATE, TAIL_SECONDS);
            tail_left = (uint64_t)TAIL_SECONDS * SAMPLE_RATE;
        }
        if (tail_left != ~0ull) { if (tail_left <= chunk) break; tail_left -= chunk; }
    }
    fclose(out);
    if (bufpeak_on)
        fprintf(stderr, "\n[c1014] voicebuf(0x509730) peak=%.5f @%.3fs ; chanmix(0x509b30) peak=%.5f @%.3fs\n",
                bufpeak, bufpeak_t, chanpeak, chanpeak_t);
    fprintf(stderr, "\n[c1014] wrote %llu samples (%.1fs) -> %s%s\n",
            (unsigned long long)total, (double)total / SAMPLE_RATE, outpath,
            (total >= max_smp) ? " [max cap]" : "");
    return 0;
}
