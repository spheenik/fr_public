// c1_flybye_harness -- ground-truth harness for fr-013 flybye's year-2001
// V2 synth (format v1). Direct analogue of v2/validate/c1_fr08_harness.c:
// map the depacked image at 0x400000, pin the 3 rdtsc seed sites for
// determinism, call the genuine 2001 OpenV2M + PlayV2M + RenderProxy on the
// in-image embedded v1 song, and write interleaved stereo f32. This is the
// v1 ORACLE: diff its output against the portable's native-v1 render to turn
// the flybye era-row verdicts from correct-by-row into bit-exact proven.
//
// Build (32-bit, no-pie so 0x400000 is free; the synth sets its own x87 CW):
//   gcc -m32 -no-pie -O0 c1_flybye_harness.c -o c1_flybye_harness
// Run:
//   ../toolkit/era unpack flybye.exe /tmp/fr013/unpacked.bin   # once
//   ./c1_flybye_harness /tmp/fr013/unpacked.bin out.f32 4096 200
//
// Entry points (flybye-extraction/NOTES.md; addresses are byte-identical in
// structure to fr08, only data/call targets differ):
//   OpenV2M     @0x40d61c   (v2m ptr) -- parses header -> player globals
//   PlayV2M     @0x40d74c   ()        -- stop + Reset(synthInit/Globals) + play
//   RenderProxy @0x40d4d2   (f32 *buf, u32 n) stdcall ret 8 -- fill; calls
//                           synthRender @0x40ff4a + player tick @0x40d024
//   embedded v1 v2m @0x41bd9d (timediv 480; == repo tpinv2.v2m's bytes)
//   playing flag @0x477318, paused @0x47731c
//   rdtsc seeds  @0x40e604 / 0x40ea9e / 0x40ebcc  (0f31 -> 31c0, seed=0)

#include <stdint.h>

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x2ef000u            // flybye unpacked.bin = 3076096 bytes

// shared native oracle scaffold (mmap@0x400000 + fault reporter + rdtsc-pin + f32)
#define ORACLE_IMG_SIZE IMG_SIZE
#include "../toolkit/oracle.h"

#define VA_V2M       0x41bd9du        // embedded v1 song (timediv 480)
#define VA_OPEN_V2M  0x40d61cu
#define VA_PLAY_V2M  0x40d74cu
#define VA_RENDER    0x40d4d2u        // stdcall ret 8
#define VA_PLAYING   0x477318u        // dword: sequencer running
#define VA_TIMEDIV   0x478568u
#define VA_TPC       0x47856cu
#define VA_MAXTIME   0x478570u
#define VA_GDNUM     0x478578u
static const uint32_t RDTSC_SITES[] = { 0x40e604u, 0x40ea9eu, 0x40ebccu };

#define SAMPLE_RATE  44100u
#define TAIL_SECONDS 6u

typedef void (__attribute__((stdcall)) *render_fn)(float *buf, uint32_t n);
static uint32_t rd32(uint32_t va) { return *(volatile uint32_t *)(uintptr_t)va; }

int main(int argc, char **argv)
{
    const char *path    = (argc > 1) ? argv[1] : "/tmp/fr013/unpacked.bin";
    const char *outpath = (argc > 2) ? argv[2] : "/tmp/fr013/c1_flybye.f32";
    uint32_t chunk      = (argc > 3) ? (uint32_t)atoi(argv[3]) : 4096u;
    uint64_t max_smp    = ((argc > 4) ? (uint64_t)atoi(argv[4]) : 200u) * SAMPLE_RATE;
    if (!chunk) chunk = 4096u;

    oracle_install_faults();
    oracle_map_image(path, IMG_SIZE);
    oracle_pin_rdtsc(RDTSC_SITES, sizeof(RDTSC_SITES) / sizeof(RDTSC_SITES[0]));
    fprintf(stderr, "[c1fb] timediv@v2m=%u\n", rd32(VA_V2M));

    // Skip the RONAN (speech) init inside OpenV2M -- a 2-arg stdcall whose body
    // hits null IAT thunks (-> call to 0). nop the two arg pushes + the call,
    // keeping the stack balanced; the interleaved state-init movs are left
    // intact. (== c1_fr08_harness's Ronan-init skip.)
    {
        uint8_t *a2 = (uint8_t *)0x40d6c5u; // push [esp+0x10]  ff 74 24 10 (4)
        uint8_t *a1 = (uint8_t *)0x40d6cbu; // push 0x40d4d2    68 ..       (5)
        uint8_t *cl = (uint8_t *)0x40d6e9u; // call 0x40e072    e8 ..       (5)
        if (a2[0]==0xff && a1[0]==0x68 && cl[0]==0xe8) {
            memset(a2,0x90,4); memset(a1,0x90,5); memset(cl,0x90,5);
            fprintf(stderr, "[c1fb] Ronan init nop'd (speech gated)\n");
        } else fprintf(stderr, "[c1fb] WARN unexpected Ronan bytes %02x %02x %02x\n",
                       a2[0], a1[0], cl[0]);
    }
    // Ronan PROCESS call in the render driver (ch15 only, cmp cl,0xf @0x4100bc
    // -> call 0x40fdcd @0x4100d3): runs the speech filter on ch15 even with
    // init nop'd (== fr08 @0x40baac). The song has no speech text (spsize=0);
    // gate it off so ch15 is the raw voice chain on both sides. C1_RONAN=1 keeps.
    if (!getenv("C1_RONAN")) {
        uint8_t *rp = (uint8_t *)0x4100d3u;
        if (rp[0]==0xe8) { memset(rp,0x90,5);
            fprintf(stderr, "[c1fb] Ronan ch15 process call nop'd\n"); }
        else fprintf(stderr, "[c1fb] WARN no call at Ronan process (%02x)\n", rp[0]);
    }

    // OpenV2M(v2m) -- call convention-agnostic: save esp to a memory slot and
    // restore after, so cdecl (ret 0) or stdcall (ret 8) both balance. Push a
    // spare 2nd arg in case it is ret 8.
    fprintf(stderr, "[c1fb] OpenV2M(0x%x)...\n", VA_V2M);
    uint32_t esp_save = 0;
    __asm__ volatile (
        "movl %%esp, %0\n\t"
        "pushl $0\n\t"
        "pushl %2\n\t"
        "call  *%3\n\t"
        "movl  %0, %%esp\n\t"
        : "+m"(esp_save)
        : "0"(esp_save), "i"(VA_V2M), "r"(VA_OPEN_V2M)
        : "eax","ecx","edx","esi","cc","memory");
    fprintf(stderr, "[c1fb] OpenV2M ok: timediv=%u tpc=%u maxtime=%u gdnum=%u\n",
            rd32(VA_TIMEDIV), rd32(VA_TPC), rd32(VA_MAXTIME), rd32(VA_GDNUM));

    // PlayV2M() -- void; esp-guarded for safety
    __asm__ volatile (
        "movl %%esp, %0\n\t"
        "call *%2\n\t"
        "movl %0, %%esp\n\t"
        : "+m"(esp_save)
        : "0"(esp_save), "r"(VA_PLAY_V2M)
        : "eax","ecx","edx","esi","cc","memory");
    fprintf(stderr, "[c1fb] PlayV2M: playing=%u\n", rd32(VA_PLAYING));

    FILE *out = fopen(outpath, "wb");
    if (!out) { fprintf(stderr, "cannot write %s\n", outpath); return 1; }
    float *buf = malloc((size_t)chunk * 2 * sizeof(float));
    render_fn render = (render_fn)(uintptr_t)VA_RENDER;
    volatile uint32_t *playing = (volatile uint32_t *)(uintptr_t)VA_PLAYING;
    uint64_t total = 0, tail_left = ~0ull, next_log = 0;
    while (total < max_smp) {
        render(buf, chunk);
        if (fwrite(buf, 2 * sizeof(float), chunk, out) != chunk) {
            fprintf(stderr, "short write\n"); return 1; }
        total += chunk;
        if (total >= next_log) {
            fprintf(stderr, "[c1fb] %6.1fs (playing=%u)\r",
                    (double)total / SAMPLE_RATE, *playing);
            next_log += 30u * SAMPLE_RATE;
        }
        if (!*playing && tail_left == ~0ull) {
            fprintf(stderr, "\n[c1fb] song end @%.1fs -- %us tail\n",
                    (double)total / SAMPLE_RATE, TAIL_SECONDS);
            tail_left = (uint64_t)TAIL_SECONDS * SAMPLE_RATE;
        }
        if (tail_left != ~0ull) { if (tail_left <= chunk) break; tail_left -= chunk; }
    }
    fclose(out);
    fprintf(stderr, "\n[c1fb] wrote %llu samples (%.1fs) -> %s%s\n",
            (unsigned long long)total, (double)total / SAMPLE_RATE, outpath,
            (total >= max_smp) ? " [max cap]" : "");
    return 0;
}
