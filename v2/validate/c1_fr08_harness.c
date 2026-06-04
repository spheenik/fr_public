// C1 ground-truth harness for fr-08's year-2000 V2 synth.
//
// Loads the depacked demo image (unpacked.bin) at its native base 0x400000 in a
// 32-bit process and calls the genuine 2000 synth/player code directly. The
// image is self-contained (synth @0x40axxx, Ronan @0x4093xx, tables @0x45abxx,
// player globals @0x592xxx, scratch @0x715xxx-0x726xxx all in-image), so no
// import stubbing is needed. The 3 rdtsc seed sites are patched to a fixed seed
// for determinism (see fr08-extraction/DELTA.md).
//
// Build (32-bit, no-pie so 0x400000 is free; mpc32 = x87 24-bit single, matching
// the synth's own control word):
//   gcc -m32 -no-pie -O0 c1_fr08_harness.c -o c1_fr08_harness
// Run:
//   ./c1_fr08_harness /tmp/fr08/unpacked.bin
//
// Milestone 1: load + patch + OpenV2M + dump the parsed v2m header (proves the
// loader mechanics and the self-contained-image premise). Render comes next.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#define __USE_GNU 1
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

static void segv(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *)uc_;
    unsigned eip = uc->uc_mcontext.gregs[REG_EIP];
    unsigned esp = uc->uc_mcontext.gregs[REG_ESP];
    unsigned ecx = uc->uc_mcontext.gregs[REG_ECX];
    unsigned esi = uc->uc_mcontext.gregs[REG_ESI];
    unsigned edi = uc->uc_mcontext.gregs[REG_EDI];
    fprintf(stderr, "[c1] SIGSEGV fault_addr=%p eip=0x%08x esp=0x%08x "
            "ecx=0x%08x esi=0x%08x edi=0x%08x\n",
            si->si_addr, eip, esp, ecx, esi, edi);
    _exit(42);
}

#define IMG_BASE  0x400000u
#define IMG_SIZE  0x32b000u   // unpacked.bin is exactly 811 pages

// Addresses discovered by static analysis (fr08-extraction/DELTA.md):
#define VA_V2M_DATA   0x415637u  // the in-image original v0 fr08.v2m
#define VA_OPEN_V2M   0x4099e3u  // OpenV2M(v2m_ptr) cdecl; player state -> globals
// rdtsc seed sites (osc-noise / LFO-S&H / dist) -- patch 0f31 -> 31c0 (xor eax,eax)
static const uint32_t RDTSC_SITES[] = { 0x40a494u, 0x40a93eu, 0x40aa6cu };
// OpenV2M unconditionally inits RONAN (speech) at 0x409aad, which hits IAT
// thunks (jmp [0x4440xx]) NULL in the static dump. Speech is gated out on both
// A/B sides (the synth/render window makes NO thunk calls -- self-contained).
// The Ronan init is a 2-arg stdcall (ret 8): args pushed at 0x409a8c
// (push [esp+0x10], 4 bytes) and 0x409a9c (push 0x40990e, 5 bytes), then
// call 0x40a092 (0x409aad, 5 bytes). Nop ALL THREE so the stack stays balanced
// (skipping the call means its ret-8 won't clean the args). Leaving any push
// shifts the stack -> OpenV2M's ret-8 returns to a bogus address.
#define VA_RONAN_PUSH_ARG2  0x409a8cu  // push [esp+0x10]  (4 bytes: ff 74 24 10)
#define VA_RONAN_PUSH_ARG1  0x409a9cu  // push 0x40990e    (5 bytes: 68 ..)
#define VA_RONAN_INIT_CALL  0x409aadu  // call 0x40a092    (5 bytes: e8 ..)
// parsed player-state globals written by OpenV2M:
#define VA_G_TIMEDIV  0x592e00u
#define VA_G_TPC      0x592e04u  // timediv * 10000
#define VA_G_MAXTIME  0x592e08u
#define VA_G_GDNUM    0x592e10u

static uint32_t rd32(uint32_t va) { return *(volatile uint32_t *)(uintptr_t)va; }

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/tmp/fr08/unpacked.bin";

    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = segv; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);

    // 1) map a fixed RWX region at the image's native base
    void *p = mmap((void *)(uintptr_t)IMG_BASE, IMG_SIZE,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void *)(uintptr_t)IMG_BASE) {
        fprintf(stderr, "mmap @0x%x failed (got %p) -- 0x400000 not free?\n",
                IMG_BASE, p);
        return 1;
    }

    // 2) load the depacked image into it
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    size_t n = fread((void *)(uintptr_t)IMG_BASE, 1, IMG_SIZE, f);
    fclose(f);
    fprintf(stderr, "[c1] loaded %zu bytes at 0x%x\n", n, IMG_BASE);
    if (n < IMG_SIZE) fprintf(stderr, "[c1] WARNING: short read\n");

    // 3) pin rdtsc -> fixed seed 0 (0f 31 -> 31 c0)
    for (unsigned i = 0; i < sizeof(RDTSC_SITES)/sizeof(RDTSC_SITES[0]); i++) {
        uint8_t *s = (uint8_t *)(uintptr_t)RDTSC_SITES[i];
        if (s[0] == 0x0f && s[1] == 0x31) { s[0] = 0x31; s[1] = 0xc0; }
        else fprintf(stderr, "[c1] WARN: no rdtsc at 0x%x (%02x %02x)\n",
                     RDTSC_SITES[i], s[0], s[1]);
    }
    fprintf(stderr, "[c1] rdtsc sites patched -> deterministic seed 0\n");

    // 3b) skip RONAN (speech) init -- nop both pushes + the call, keeping the
    //     stack balanced (the init is a 2-arg stdcall, ret 8)
    {
        uint8_t *p2 = (uint8_t *)(uintptr_t)VA_RONAN_PUSH_ARG2;
        uint8_t *p1 = (uint8_t *)(uintptr_t)VA_RONAN_PUSH_ARG1;
        uint8_t *cl = (uint8_t *)(uintptr_t)VA_RONAN_INIT_CALL;
        if (p2[0] == 0xff && p1[0] == 0x68 && cl[0] == 0xe8) {
            memset(p2, 0x90, 4); memset(p1, 0x90, 5); memset(cl, 0x90, 5);
            fprintf(stderr, "[c1] RONAN init (2 pushes + call) nop'd (speech gated)\n");
        } else fprintf(stderr, "[c1] WARN: unexpected Ronan bytes (%02x %02x %02x)\n",
                       p2[0], p1[0], cl[0]);
    }

    // 4) OpenV2M(v2m_ptr, arg2) -- STDCALL, 2 args (ends `ret 0x8`); the demo
    //    passes ([0x594328]=0) as arg2. Called via inline asm so the 2-arg
    //    stdcall stack discipline is exact (callee cleans via ret 8).
    fprintf(stderr, "[c1] calling OpenV2M(0x%x, 0)...\n", VA_V2M_DATA);
    if (getenv("C1_BRK")) __asm__ volatile ("int3");  // debug trap (region mapped)
    // OpenV2M saves/restores only edi+ebx (not esi/ebp); mark esi clobbered so
    // gcc keeps nothing live there. Immediate inputs avoid input/clobber clash.
    __asm__ volatile (
        "pushl $0\n\t"            // arg2 = 0
        "pushl %0\n\t"            // arg1 = v2m ptr
        "call  %P1\n\t"          // OpenV2M (direct); ret 8 pops both args
        : : "i"(VA_V2M_DATA), "i"(VA_OPEN_V2M)
        : "eax", "ecx", "edx", "esi", "cc", "memory");
    fprintf(stderr, "[c1] OpenV2M returned.\n");

    // 5) dump the parsed header -- proves the parse ran on our v2m
    fprintf(stderr, "[c1] timediv=%u tpc=%u maxtime=%u gdnum=%u\n",
            rd32(VA_G_TIMEDIV), rd32(VA_G_TPC),
            rd32(VA_G_MAXTIME), rd32(VA_G_GDNUM));
    return 0;
}
