// c1_fr019_harness -- ground-truth harness for fr-019 "poem to a horse"'s
// 2002-03 V2 synth (a format-v4 engine). Direct analogue of
// ../fr014-extraction/c1_fr014_harness.c, but poemtoahorse's player layout
// differs: OpenV2M and PlayV2M are FUSED into one Open+Play function that
// parses the header, builds the channel tables, runs Reset, and leaves the
// sequencer playing (playing flag := 1) in a single stdcall. So the harness
// calls that one function, then loops RenderProxy. This is the v4 ORACLE: its
// output, diffed against the portable's native-v4 render, upgrades the v4 era
// rows (esp. DELTA_NO_FM_OSC's NEW side -- poemtoahorse uses mode5 FM on ch11/
// ch12) from assay-only to render-proven.
//
// Build (32-bit, no-pie so 0x400000 is free; the synth sets its own x87 CW):
//   gcc -m32 -no-pie -O0 c1_fr019_harness.c -o c1_fr019_harness
// Run:
//   ../toolkit/era unpack 'fr-019-party-b.exe' /tmp/fr019_unpacked.bin
//   ./c1_fr019_harness /tmp/fr019_unpacked.bin out.f32 4096 120
//
// Entry points (located by disasm; signature-matched against fr014's known
// V2MPlayer methods -- see the extraction notes / git log):
//   Open+Play   @0x4107f0  (v2m ptr, samplerate) stdcall ret 8 -- parses header
//                          -> player globals (timediv@0x448b98, maxtime@0x448ba0,
//                          gdnum@0x448ba8), builds 16 channel tables @0x448bb4
//                          stride 0x50, calls Reset @0x41003d, sets playing := 1.
//   RenderProxy @0x410716  (f32 *buf, u32 n) stdcall ret 8 -- fills; checks the
//                          playing BYTE @0x448148, calls synthRender @0x429a36.
//   synthInit   @0x429979  (lazy, from the tick path) -- zeroes SYN@0x4cacf0
//                          size 0x1e27c4, 32 voices @0x4ccb00 stride 0x210 (v4
//                          POLY=32, `mov cl,0x20`).
//   embedded v4 v2m @0x41a7cd (carve 0: timediv 480, maxtime 345600, 14 active ch)
//   playing flag @0x448148 (BYTE: 1 while sequencer runs, cleared at song end)
//   rdtsc seeds  @0x427ee4 / 0x42856b / 0x428693  (0f31 -> 31c0, seed=0)
//
// No Ronan (speech) init lies on the Open+Play path (poemtoahorse's carve 0 has
// no speech), so -- unlike fr014/flybye -- there is no Ronan call to nop out.

#include <stdint.h>

#define IMG_BASE 0x400000u
#define IMG_SIZE 0x2b2200u            // fr019 unpacked.bin = 2826240 bytes

// shared native oracle scaffold (mmap@0x400000 + fault reporter + rdtsc-pin + f32)
#define ORACLE_IMG_SIZE IMG_SIZE
#include "../toolkit/oracle.h"

#define VA_V2M       0x41a7cdu        // embedded v4 song0 (timediv 480)
#define VA_OPEN_PLAY 0x4107f0u        // fused Open+Play, stdcall ret 8
#define VA_RENDER    0x410716u        // RenderProxy, stdcall ret 8
#define VA_PLAYING   0x448148u        // BYTE: sequencer running
#define VA_TIMEDIV   0x448b98u
#define VA_MAXTIME   0x448ba0u
#define VA_GDNUM     0x448ba8u
#define VA_RESET     0x41003du        // sequencer Reset (re-seek to song start)
#define VA_NOTENUM   0x448bacu        // channel event-count table base; +ch*0x50
static const uint32_t RDTSC_SITES[] = { 0x427ee4u, 0x42856bu, 0x428693u };

#define SAMPLE_RATE  44100u
#define TAIL_SECONDS 6u

typedef void (__attribute__((stdcall)) *render_fn)(float *buf, uint32_t n);
static uint32_t rd32(uint32_t va) { return *(volatile uint32_t *)(uintptr_t)va; }
static uint8_t  rd8 (uint32_t va) { return *(volatile uint8_t  *)(uintptr_t)va; }

int main(int argc, char **argv)
{
    const char *path    = (argc > 1) ? argv[1] : "/tmp/fr019_unpacked.bin";
    const char *outpath = (argc > 2) ? argv[2] : "/tmp/fr019-extraction/c1_fr019.f32";
    uint32_t chunk      = (argc > 3) ? (uint32_t)atoi(argv[3]) : 4096u;
    uint64_t max_smp    = ((argc > 4) ? (uint64_t)atoi(argv[4]) : 120u) * SAMPLE_RATE;
    if (!chunk) chunk = 4096u;

    oracle_install_faults();
    oracle_map_image(path, IMG_SIZE);
    oracle_pin_rdtsc(RDTSC_SITES, sizeof(RDTSC_SITES) / sizeof(RDTSC_SITES[0]));
    fprintf(stderr, "[c1019] timediv@v2m=%u\n", rd32(VA_V2M));

    // Open+Play(v2m, samplerate) -- convention-agnostic: save esp and restore
    // after, so the ret 8 (or any other) balances regardless. stdcall arg order:
    // push samplerate (arg2) then v2m (arg1); the callee reads [esp+4]=v2m.
    fprintf(stderr, "[c1019] OpenPlay(0x%x, %u)...\n", VA_V2M, SAMPLE_RATE);
    uint32_t esp_save = 0;
    __asm__ volatile (
        "movl %%esp, %0\n\t"
        "pushl %4\n\t"
        "pushl %3\n\t"
        "call  *%2\n\t"
        "movl  %0, %%esp\n\t"
        : "+m"(esp_save)
        : "0"(esp_save), "r"(VA_OPEN_PLAY), "i"(VA_V2M), "i"(SAMPLE_RATE)
        : "eax","ecx","edx","esi","edi","cc","memory");
    fprintf(stderr, "[c1019] OpenPlay ok: timediv=%u maxtime=%u gdnum=%u playing=%u\n",
            rd32(VA_TIMEDIV), rd32(VA_MAXTIME), rd32(VA_GDNUM), rd8(VA_PLAYING));

    // FR019_SOLO=<ch>: zero the event count for every other channel, then re-run
    // Reset so the sequencer re-seeks the song start with only `ch` audible.
    // (Open+Play already ran Reset; we zero notenum AFTER it and re-Reset.)
    const char *soloenv = getenv("FR019_SOLO");
    if (soloenv) {
        int solo = atoi(soloenv);
        fprintf(stderr, "[c1019] notenum table:");
        for (int ch = 0; ch < 16; ch++) {
            volatile uint32_t *nn = (volatile uint32_t *)(uintptr_t)(VA_NOTENUM + (uint32_t)ch * 0x50u);
            fprintf(stderr, " ch%d=%u", ch, *nn);
            if (ch != solo) *nn = 0;
        }
        fprintf(stderr, "\n[c1019] soloed ch%d; re-Reset\n", solo);
        __asm__ volatile (
            "movl %%esp, %0\n\t"
            "call *%2\n\t"
            "movl %0, %%esp\n\t"
            : "+m"(esp_save)
            : "0"(esp_save), "r"(VA_RESET)
            : "eax","ecx","edx","esi","edi","cc","memory");
        *(volatile uint8_t *)(uintptr_t)VA_PLAYING = 1; // Reset doesn't touch it, belt-and-braces
    }

    FILE *out = fopen(outpath, "wb");
    if (!out) { fprintf(stderr, "cannot write %s\n", outpath); return 1; }
    float *buf = malloc((size_t)chunk * 2 * sizeof(float));
    render_fn render = (render_fn)(uintptr_t)VA_RENDER;

    // FR019_BUFPEAK: track running max of the final stereo output (sanity: is the
    // FM path producing the expected amplitude?). Cheap, output-neutral.
    const int bufpeak_on = getenv("FR019_BUFPEAK") ? 1 : 0;
    float bufpeak = 0.0f; double bufpeak_t = 0.0;
    uint64_t total = 0, tail_left = ~0ull, next_log = 0;
    while (total < max_smp) {
        render(buf, chunk);
        if (bufpeak_on) {
            for (uint32_t k = 0; k < chunk * 2; k++) {
                float a = buf[k] < 0 ? -buf[k] : buf[k];
                if (a > bufpeak && a < 1e4f) { bufpeak = a; bufpeak_t = (double)total / SAMPLE_RATE; }
            }
        }
        if (fwrite(buf, 2 * sizeof(float), chunk, out) != chunk) {
            fprintf(stderr, "short write\n"); return 1; }
        total += chunk;
        uint8_t playing = rd8(VA_PLAYING);
        if (total >= next_log) {
            fprintf(stderr, "[c1019] %6.1fs (playing=%u)\r", (double)total / SAMPLE_RATE, playing);
            next_log += 30u * SAMPLE_RATE;
        }
        if (!playing && tail_left == ~0ull) {
            fprintf(stderr, "\n[c1019] song end @%.1fs -- %us tail\n",
                    (double)total / SAMPLE_RATE, TAIL_SECONDS);
            tail_left = (uint64_t)TAIL_SECONDS * SAMPLE_RATE;
        }
        if (tail_left != ~0ull) { if (tail_left <= chunk) break; tail_left -= chunk; }
    }
    fclose(out);
    if (bufpeak_on)
        fprintf(stderr, "\n[c1019] output peak=%.5f @%.3fs\n", bufpeak, bufpeak_t);
    fprintf(stderr, "\n[c1019] wrote %llu samples (%.1fs) -> %s%s\n",
            (unsigned long long)total, (double)total / SAMPLE_RATE, outpath,
            (total >= max_smp) ? " [max cap]" : "");
    return 0;
}
