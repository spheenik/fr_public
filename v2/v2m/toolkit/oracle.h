// oracle.h -- shared C scaffold for period-binary oracle harnesses.
//
// A harness #includes this, supplies its own per-binary config (IMG_SIZE, entry/
// init/render VAs, rdtsc sites) and its own driving strategy (call the binary's
// in-image player like c1_flybye, OR a ported source player + synth-only calls like
// c2_oracle), and gets the process scaffold common to every harness:
//
//   oracle_map_image(path, size)        map the unpacked image at 0x400000, load it
//   oracle_install_faults()             SIGSEGV/SIGBUS/SIGILL reporter (eip + bytes)
//   oracle_pin_rdtsc(sites, n)          patch each `0f31` rdtsc -> `31c0` (seed 0)
//   oracle_write_f32(path, buf, frames) write interleaved stereo float32
//   ORACLE_F32_OPEN / ORACLE_F32_CHUNK  streaming variant for chunked render loops
//
// NATIVE BY DESIGN -- never run under Unicorn. The V2 audio path is x87
// transcendental-heavy (fsin/fpatan/f2xm1); the host hardware x87 reproduces the
// period Pentium's 80-bit results bit-for-bit (max|d|=0), whereas QEMU/Unicorn
// rounds the bottom ~11 mantissa bits (libm-at-double). See change design.md (D3).
//
// Build a harness:  gcc -m32 -no-pie -O0 harness.c -o harness

#ifndef ORACLE_H
#define ORACLE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#define __USE_GNU 1
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef ORACLE_IMG_BASE
#define ORACLE_IMG_BASE 0x400000u
#endif

// --- fault reporter: report eip + the faulting bytes, then exit -------------
// The image range is reported if the harness defines ORACLE_IMG_SIZE.
static void oracle_segv(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *)uc_;
    unsigned eip = uc->uc_mcontext.gregs[REG_EIP];
    unsigned esp = uc->uc_mcontext.gregs[REG_ESP];
    fprintf(stderr, "[oracle] SIG%d fault=%p eip=0x%08x esp=0x%08x\n",
            sig, si->si_addr, eip, esp);
#ifdef ORACLE_IMG_SIZE
    if (eip >= ORACLE_IMG_BASE && eip < ORACLE_IMG_BASE + (unsigned)ORACLE_IMG_SIZE) {
        unsigned char *p = (unsigned char *)(uintptr_t)eip;
        fprintf(stderr, "[oracle]   bytes: %02x %02x %02x %02x %02x %02x\n",
                p[0], p[1], p[2], p[3], p[4], p[5]);
    }
#endif
    _exit(42);
}

static void oracle_install_faults(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = oracle_segv;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
}

// --- map the unpacked image at its native base and load it ------------------
// Returns the number of bytes read. Exits on mmap failure.
static size_t oracle_map_image(const char *path, size_t img_size)
{
    void *p = mmap((void *)(uintptr_t)ORACLE_IMG_BASE, img_size,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void *)(uintptr_t)ORACLE_IMG_BASE) {
        fprintf(stderr, "[oracle] mmap @0x%x failed (got %p)\n", ORACLE_IMG_BASE, p);
        _exit(1);
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[oracle] cannot open %s\n", path);
        _exit(1);
    }
    size_t n = fread((void *)(uintptr_t)ORACLE_IMG_BASE, 1, img_size, f);
    fclose(f);
    fprintf(stderr, "[oracle] mapped %zu bytes from %s at 0x%x\n", n, path, ORACLE_IMG_BASE);
    return n;
}

// --- pin rdtsc seed sites to 0: `0f31` -> `31c0` (xor eax,eax) ---------------
static void oracle_pin_rdtsc(const uint32_t *sites, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        uint8_t *s = (uint8_t *)(uintptr_t)sites[i];
        if (s[0] == 0x0f && s[1] == 0x31) {
            s[0] = 0x31;
            s[1] = 0xc0;
        } else {
            fprintf(stderr, "[oracle] WARN no rdtsc @0x%x (%02x %02x)\n",
                    sites[i], s[0], s[1]);
        }
    }
    if (count)
        fprintf(stderr, "[oracle] %u rdtsc site(s) pinned -> seed 0\n", count);
}

// --- write interleaved stereo float32 ---------------------------------------
static int oracle_write_f32(const char *path, const float *buf, size_t frames)
{
    FILE *out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "[oracle] cannot open %s\n", path);
        return -1;
    }
    size_t w = fwrite(buf, 2 * sizeof(float), frames, out);
    fclose(out);
    fprintf(stderr, "[oracle] wrote %zu frames -> %s\n", w, path);
    return w == frames ? 0 : -1;
}

// streaming variant for chunked render loops:
//   FILE *o = ORACLE_F32_OPEN(path);
//   ... ORACLE_F32_CHUNK(o, buf, chunk); ...
//   fclose(o);
#define ORACLE_F32_OPEN(path)            fopen((path), "wb")
#define ORACLE_F32_CHUNK(o, buf, frames) fwrite((buf), 2 * sizeof(float), (frames), (o))

#endif // ORACLE_H
