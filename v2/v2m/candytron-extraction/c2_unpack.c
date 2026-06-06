// C2 unpack harness for fr-030 candytron (kkrunchy5-packed 64k).
//
// Maps the PACKED exe at its native base 0x400000 in a 32-bit process and runs
// only the kkrunchy depacker stub (entry @0x40fdc3). kkrunchy decompresses the
// whole image in-place (mem->mem) and then jumps to the demo OEP, which
// immediately touches Win32 imports / the PEB that don't exist in this bare
// process -> SIGSEGV. By that point decompression is finished, so the SIGSEGV
// handler dumps the unpacked image [0x400000 .. 0x400000+IMG_SIZE) to disk.
//
// This mirrors the fr08 c1_fr08_harness.c mapping+segv recipe, but for the
// UNPACK stage rather than the render stage.
//
// Build:  gcc -m32 -no-pie -O0 c2_unpack.c -o c2_unpack
// Run:    ./c2_unpack [packed.exe] [out.bin]
//
//   PE: 1 section "kkrunchy" RVA 0x1000 (file off 0x1000), vsize 0xb5d835,
//       imagebase 0x400000, imagesize 0xb5f000, entry RVA 0xfdc3. File is
//       exactly 0x10000 bytes (headers 0x1000 + packed section 0xf000), so a
//       flat copy of the whole file to 0x400000 lands the section raw at its
//       RVA 0x1000.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#define __USE_GNU 1
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#define IMG_BASE   0x400000u
#define IMG_SIZE   0xb5f000u   // PE SizeOfImage
#define ENTRY_VA   0x40fdc3u   // imagebase + entry RVA 0xfdc3

static const char *g_out = "/tmp/candytron/unpacked.bin";
static volatile int g_dumped = 0;

static void dump_image(const char *why, unsigned eip)
{
    if (g_dumped) _exit(43);
    g_dumped = 1;
    fprintf(stderr, "[c2] %s (eip=0x%08x) -- dumping image\n", why, eip);
    FILE *o = fopen(g_out, "wb");
    if (!o) { fprintf(stderr, "[c2] cannot open %s\n", g_out); _exit(1); }
    size_t w = fwrite((void *)(uintptr_t)IMG_BASE, 1, IMG_SIZE, o);
    fclose(o);
    fprintf(stderr, "[c2] wrote %zu bytes -> %s\n", w, g_out);
    _exit(0);
}

static void segv(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *)uc_;
    unsigned eip = uc->uc_mcontext.gregs[REG_EIP];
    fprintf(stderr, "[c2] SIGSEGV fault_addr=%p eip=0x%08x\n", si->si_addr, eip);
    dump_image("segv after depack", eip);
}

static void alarm_dump(int sig) { dump_image("watchdog timeout", 0); }

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "fr030-candytron-final-101.exe";
    if (argc > 2) g_out = argv[2];

    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = segv; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    signal(SIGALRM, alarm_dump);

    // 1) map the image space at its native base
    void *p = mmap((void *)(uintptr_t)IMG_BASE, IMG_SIZE,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void *)(uintptr_t)IMG_BASE) {
        fprintf(stderr, "[c2] mmap @0x%x failed (got %p)\n", IMG_BASE, p);
        return 1;
    }

    // 2) flat-load the packed file (headers + packed section) at 0x400000
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[c2] cannot open %s\n", path); return 1; }
    size_t n = fread((void *)(uintptr_t)IMG_BASE, 1, 0x10000, f);
    fclose(f);
    fprintf(stderr, "[c2] loaded %zu packed bytes at 0x%x; entry 0x%x\n",
            n, IMG_BASE, ENTRY_VA);

    // 3) give the stub a watchdog (in case it loops instead of faulting)
    alarm(20);

    // 4) jump into the depacker. Give it a fresh-ish register state; the stub
    //    sets up its own ebp/pointers (mov ebp,0x410000 ...). Use a private
    //    scratch stack area inside the image tail so a deep stub stack can't
    //    clobber our harness stack.
    fprintf(stderr, "[c2] entering depacker...\n");
    __asm__ volatile (
        "mov %0, %%eax\n\t"
        "jmp *%%eax\n\t"
        : : "r"(ENTRY_VA) : "eax");

    // unreachable
    dump_image("returned from stub", 0);
    return 0;
}
