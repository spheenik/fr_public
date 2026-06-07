// C2 render harness for fr-030 candytron's V2 synth (v5-era + Ronan).
// Phase 1: map the unpacked image, pin rdtsc, stub the malloc IAT, run the
// in-image v2m parser, and dump the parsed song descriptor to prove it works.
//
// Build: gcc -m32 -no-pie -O0 c2_render.c -o c2_render
// Run:   ./c2_render [unpacked.bin]
//
// VAs (see v2/v2m/candytron-extraction/NOTES.md):
//   v2m stream         0x424c00   (after the *VM..ryg tag)
//   g_v2m global       0xeba344
//   audio open/parse   0x414140 (esi=&g_v2m) -> 0x4140df -> parser 0x41387d
//   song descriptor    0x6a7fc8.. (timediv@+0x10? filled by 0x41387d at edx=0x6a7fd8)
//   malloc 0x411013 via IAT [0x42003c](1,size)->ptr , [0x420050](ptr)->ptr
//   rdtsc seeds        0x41dca8, 0x41e32d   (0f31 -> 31c0)

#include <stdint.h>

#define IMG_BASE 0x400000u
#define IMG_SIZE 0xb5f000u

// shared native oracle scaffold (mmap@0x400000 + fault reporter + rdtsc-pin + f32)
#define ORACLE_IMG_SIZE IMG_SIZE
#include "../toolkit/oracle.h"

#define VA_V2M       0x424c00u
#define VA_G_V2M     0xeba344u
#define VA_AUDIO_OPEN 0x414140u
#define VA_DESC      0x6a7fd8u
#define IAT_ALLOC    0x42003cu
#define IAT_ALLOC2   0x420050u
#define IAT_FREE     0x420038u
static const uint32_t RDTSC_SITES[] = { 0x41dca8u, 0x41e32du };

// stdcall stubs for the demo's malloc IAT
static void * __attribute__((stdcall)) stub_alloc(int one, int size){ (void)one; return calloc(1, size?size:1); }
static void * __attribute__((stdcall)) stub_ident(void *p){ return p; }
static void   __attribute__((stdcall)) stub_free(void *p){ free(p); }

static uint32_t rd32(uint32_t va){ return *(volatile uint32_t*)(uintptr_t)va; }

int main(int argc, char **argv){
    const char *path = argc>1?argv[1]:"/tmp/candytron/unpacked.bin";
    oracle_install_faults();
    { struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=oracle_segv;
      sa.sa_flags=SA_SIGINFO; sigaction(SIGFPE,&sa,NULL); }  // c2 also traps SIGFPE
    oracle_map_image(path, IMG_SIZE);
    oracle_pin_rdtsc(RDTSC_SITES, sizeof(RDTSC_SITES)/sizeof(RDTSC_SITES[0]));

    // populate malloc IAT slots
    *(uint32_t*)(uintptr_t)IAT_ALLOC  = (uint32_t)(uintptr_t)&stub_alloc;
    *(uint32_t*)(uintptr_t)IAT_ALLOC2 = (uint32_t)(uintptr_t)&stub_ident;
    *(uint32_t*)(uintptr_t)IAT_FREE   = (uint32_t)(uintptr_t)&stub_free;
    fprintf(stderr,"[c2] malloc IAT stubbed\n");

    // g_v2m = the embedded v2m stream
    *(uint32_t*)(uintptr_t)VA_G_V2M = VA_V2M;

    // call the audio-open/parse: esi = &g_v2m, function reads [esi]=v2m, parses
    fprintf(stderr,"[c2] calling audio-open/parse 0x%x ...\n", VA_AUDIO_OPEN);
    __asm__ volatile(
        "mov $0xeba344, %%esi\n\t"
        "call *%0\n\t"
        : : "r"((uint32_t)VA_AUDIO_OPEN)
        : "eax","ecx","edx","esi","edi","cc","memory");
    fprintf(stderr,"[c2] parse returned.\n");

    // dump the descriptor region
    fprintf(stderr,"[c2] descriptor @0x6a7fc8:\n");
    for (uint32_t a=0x6a7fc8; a<=0x6a7fe4; a+=4)
        fprintf(stderr,"   [0x%x] = 0x%08x\n", a, rd32(a));
    // synth state sanity: 0x4c2ccc should = patch ptr, 0x4c2cd8 = 44100
    fprintf(stderr,"[c2] synth state: [0x4c2ccc]=0x%08x  [0x4c2cd8]=0x%08x (44100=0x%x)\n",
            rd32(0x4c2ccc), rd32(0x4c2cd8), 44100);

    fprintf(stderr,"[c2] [0x6a88f8]=0x%08x (player obj)\n", rd32(0x6a88f8));
    // player MIDI stream pointers (set by playerOpen/load): [0x6a8900+0x140/144]
    fprintf(stderr,"[c2] player[0x140]=0x%08x [0x144]=0x%08x [0x128]=0x%08x [0x150]=0x%08x\n",
            rd32(0x6a8900+0x140), rd32(0x6a8900+0x144), rd32(0x6a8900+0x128), rd32(0x6a8900+0x150));

    // ---- Phase 2: render loop. synthRender(buf,count,0,0) stdcall ret 0x10 ----
    const uint32_t VA_RENDER = 0x41f8c2u;
    uint32_t secs = (argc>2)?(uint32_t)atoi(argv[2]):10u;
    uint32_t CHUNK = 4096;
    uint64_t total = (uint64_t)secs*44100;
    float *buf = malloc((size_t)CHUNK*2*sizeof(float));
    FILE *out = fopen("/tmp/candytron/c2_josie.f32","wb");
    fprintf(stderr,"[c2] rendering %u s (%llu frames) -> c2_josie.f32\n", secs,(unsigned long long)total);
    // --- debug: render 1 chunk, inspect sequencer/voice state ---
    fprintf(stderr,"[c2] DBG pre : cursor[0x144]=0x%08x countdown[0x4c2e5c]=0x%08x tickval[0x6a9eec]=0x%08x\n",
            rd32(0x6a8900+0x144), rd32(0x4c2e5c), rd32(0x6a9eec));
    {
        uint32_t nf=CHUNK;
        __asm__ volatile("push $0\n\t push $0\n\t push %1\n\t push %0\n\t mov %2,%%eax\n\t call *%%eax\n\t"
            : : "r"(buf),"r"(nf),"r"(VA_RENDER) : "eax","ecx","edx","cc","memory");
    }
    { int act=0; for(int v=0;v<32;v++) if(rd32(0x4c2cdc+v*4)) act++;
      double pk=0; for(uint32_t i=0;i<CHUNK*2;i++){double a=buf[i];if(a<0)a=-a;if(a>pk)pk=a;}
      fprintf(stderr,"[c2] DBG post: cursor[0x144]=0x%08x countdown=0x%08x active_voices=%d chunkpeak=%.5f\n",
              rd32(0x6a8900+0x144), rd32(0x4c2e5c), act, pk); }

    fprintf(stderr,"[c2] player object pointer fields (0x6a8900+off):\n");
    for (uint32_t o=0x100;o<=0x164;o+=4){ uint32_t v=rd32(0x6a8900+o);
        if (v>=0x424c00 && v<0x440000) fprintf(stderr,"   +0x%x = 0x%08x  (into v2m, off +0x%x)\n",o,v,v-0x424c00);
        else fprintf(stderr,"   +0x%x = 0x%08x\n",o,v); }
    fprintf(stderr,"[c2] MIDI bytes @cursor: ");
    { uint32_t c=rd32(0x6a8900+0x140); unsigned char*m=(unsigned char*)(uintptr_t)c;
      for(int i=0;i<24;i++) fprintf(stderr,"%02x ",m[i]); fprintf(stderr,"\n"); }
    const uint32_t VA_REFILL = 0x413b62u; // refill: advance song + ProcessMIDI (void cdecl)
    const uint32_t FRAME = 256;
    // master clock 0x6a7598 is in ~960Hz units: samples = clock*[0x6a75a8]/[0x6a7fe4]
    // => clock = samples*[0x6a7fe4]/[0x6a75a8]  (ratio ~45.938 samples/clock-unit)
    const uint64_t RNUM = rd32(0x6a7fe4), RDEN = rd32(0x6a75a8);
    fprintf(stderr,"[c2] clock ratio = %.4f samples/unit\n",(double)RDEN/RNUM);
    *(uint32_t*)(uintptr_t)0x6a7594 = 0;
    *(uint32_t*)(uintptr_t)0x6a7598 = 0;
    uint64_t done=0; double peak=0; uint64_t nextlog=0;
    while (done<total){
        if (done>=nextlog){ fprintf(stderr,"[c2] t=%llus clk=%u peak=%.5f\n",
            (unsigned long long)(done/44100), rd32(0x6a7598), peak); nextlog+=44100; }
        uint32_t nf = FRAME;
        // clock in 960Hz units for the cumulative sample total
        *(uint32_t*)(uintptr_t)0x6a7598 = (uint32_t)(((done+nf)*RNUM)/RDEN);
        __asm__ volatile("mov %0,%%eax\n\t call *%%eax\n\t" : : "r"(VA_REFILL)
            : "eax","ecx","edx","esi","edi","cc","memory");
        __asm__ volatile(
            "push $0\n\t" "push $0\n\t" "push %1\n\t" "push %0\n\t"
            "mov %2, %%eax\n\t" "call *%%eax\n\t"
            : : "r"(buf), "r"(nf), "r"(VA_RENDER) : "eax","ecx","edx","cc","memory");
        for (uint32_t i=0;i<nf*2;i++){ double a=buf[i]; if(a<0)a=-a; if(a>peak)peak=a; }
        fwrite(buf,2*sizeof(float),nf,out);
        done+=nf;
    }
    fclose(out);
    fprintf(stderr,"[c2] rendered %llu frames, peak=%.4f\n",(unsigned long long)done,peak);
    return 0;
}
