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

    uint64_t total = 0, tail_left = ~0ull, next_log = 0;
    while (total < max_smp) {
        render(buf, chunk);
        if (fwrite(buf, 2 * sizeof(float), chunk, out) != chunk) {
            fprintf(stderr, "short write\n"); return 1; }
        total += chunk;
        if (!dumpvox_done && total >= dumpvox_at) {
            dumpvox_done = 1;
            fprintf(stderr, "\n[c1014] voice-workspace dump @%.2fs:\n",
                    (double)total / SAMPLE_RATE);
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
    fprintf(stderr, "\n[c1014] wrote %llu samples (%.1fs) -> %s%s\n",
            (unsigned long long)total, (double)total / SAMPLE_RATE, outpath,
            (total >= max_smp) ? " [max cap]" : "");
    return 0;
}
