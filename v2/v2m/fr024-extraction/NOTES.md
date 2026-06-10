# fr-024 — v5 own-engine oracle

Verifies the portable v5 player against **fr-024's own genuine V2 synth**, carved
from the demo binary (oracle-first, per the lab's ground-truth rule: render the
embedded song through the binary's OWN engine, never a converted file).

## Result — PROVEN faithful (ε-floor)

Whole song (206.3s), oracle vs `portable v2dump` (both native x87):

| window | rms\|d\| | per-sec corr |
| --- | --- | --- |
| 0–60s  | ~1e-6 (≈ bit-exact) | 1.00000 |
| 80–180s | ~0.001 | 0.99999 |
| whole song | rms 0.00100 | **corr 0.999991** |

peak |oracle|/|portable| = 1.1400 / 1.1297 (within 1%). max\|d\| = 0.0261 at
182.1s — a single late-song osc-phase **razor-tie** (continuous-phase voice
nudged by a 1-ULP transcendental tie), the same irreducible native-vs-x87 floor
documented for candytron (v5) and fr019 (v4). No structural divergence: bit-exact
for the first 60s, corr stays ≥0.99999 throughout.

→ The portable modern-core v5 model reproduces fr-024 to the sub-perceptual floor.

## Era assay (data-section signature)

`era assay` matches candytron's modern-core v5 exactly: modern noise LCG present
(196314165/907633515), **no** baked oscfreq 3185015.0, **no** v6 oscseeds, **no**
v6 fcdcoffset. (The MSVC LCG 214013/2531011 hit is the C-runtime `rand`, also
present in candytron — not the synth.) song: spsize=0 → no ronan/speech.

## Binary

- source release: `~/downloads/fr024.zip` → `fr024.exe` (aPLib-packed)
- `era unpack fr024.exe unpacked.bin` (3399680 bytes; aPLib route round-trips clean)
- song = `v2m/embedded/fr024.v2m` (carve span 0; timediv 480, maxtime 165079, 8 ch)

## Engine VAs (base 0x400000), found by code-fingerprint vs candytron's

The v5 synth source is byte-identical across releases, so each entry was located
by its address-free code fingerprint:

| fn | VA | how |
| --- | --- | --- |
| synthInit(patchmap, sr) ret8 | **0x40da9a** | `60 31 c0 b9` prologue + `rep stosb` state-zero, the one just before synthRender; state base 0x5525f8, voices 0x55450c |
| synthRender(buf,cnt,a,b) ret16 | **0x40db65** | `60 9b d9 3d` (pushal;wait;fnstcw) + `mov ecx,[esp+0x28]` |
| synthProcessMIDI(buf) ret4 | **0x40de62** | `60 9b d9 3d` + `mov esi,[esp+0x24]` |
| synthSetGlobals(g) ret4 | **0x40e0e9** | unique `60 31 c0 31 c9` + fild-22-globals loop |
| rdtsc seed sites | **0x40bf88, 0x40c60d** | from `era assay` (pinned → seed 0) |

No IAT alloc/free stubs needed (synth state is static BSS; renders fault-free).

## Reproduce

```
era unpack ~/downloads/fr024.zip:fr024.exe unpacked.bin   # (unzip first)
gcc -m32 -no-pie -O0 ../toolkit/v5_oracle.c -I../toolkit -o oracle \
  -DIMG_SIZE=0x800000 \
  -DVA_INIT=0x40da9a -DVA_GLOB=0x40e0e9 -DVA_MIDI=0x40de62 -DVA_REND=0x40db65 \
  -DRDTSC0=0x40bf88 -DRDTSC1=0x40c60d
./oracle unpacked.bin ../embedded/fr024.v2m orc.f32 210
../../portable/v2dump ../embedded/fr024.v2m port.f32 210 2048
era compare orc.f32 port.f32            # (truncate port to oracle length first)
```
