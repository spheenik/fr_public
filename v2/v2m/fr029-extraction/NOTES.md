# fr-029 — v5 own-engine oracle

Verifies the portable v5 player against **fr-029's own genuine V2 synth**, carved
from the demo binary (oracle-first; render the embedded song through the binary's
OWN engine).

## Result — PROVEN faithful (ε-floor)

Whole song (173.3s), oracle vs `portable v2dump` (both native x87):

| window | rms\|d\| | per-sec corr |
| --- | --- | --- |
| 20–120s | ~0.001 | 0.99997–1.0 |
| 140–160s | ~0.009 | 0.998–0.999 |
| whole song | rms 0.00514 | **corr 0.999557** |

peak |oracle|/|portable| = 1.1615 / 1.1601. max\|d\| = 0.350 at 145.0s — a
single-sample late-song osc-phase **razor-tie** spike (per-second corr in that
window stays ≥0.998). Same irreducible native-vs-x87 floor as candytron (the
late-song 0.009 band matches candytron's documented music-bed floor). No
structural divergence.

→ The portable modern-core v5 model reproduces fr-029 to the sub-perceptual floor.

## Era assay (data-section signature)

Matches candytron's modern-core v5 exactly: modern noise LCG present, no baked
oscfreq, no v6 oscseeds, no v6 fcdcoffset. song: spsize=0 → no ronan/speech.

## Binary

- source release: `~/downloads/fr029.zip` → `fr029.exe` (aPLib-packed)
- `era unpack` → unpacked.bin (3399680 bytes; clean aPLib round-trip)
- song = `v2m/embedded/fr029.v2m` (carve span 0; timediv 96, maxtime 49908, 9 ch)

## Engine VAs (base 0x400000), found by code-fingerprint vs candytron's

| fn | VA |
| --- | --- |
| synthInit ret8 | **0x40d96a** |
| synthRender ret16 | **0x40da35** |
| synthProcessMIDI ret4 | **0x40dd32** |
| synthSetGlobals ret4 | **0x40dfb9** |
| rdtsc seed sites | **0x40be58, 0x40c4dd** |

Same fingerprints as fr024 (see that NOTES). No IAT stubs needed.

## Reproduce

```
gcc -m32 -no-pie -O0 ../toolkit/v5_oracle.c -I../toolkit -o oracle \
  -DIMG_SIZE=0x800000 \
  -DVA_INIT=0x40d96a -DVA_GLOB=0x40dfb9 -DVA_MIDI=0x40dd32 -DVA_REND=0x40da35 \
  -DRDTSC0=0x40be58 -DRDTSC1=0x40c4dd
./oracle unpacked.bin ../embedded/fr029.v2m orc.f32 174
../../portable/v2dump ../embedded/fr029.v2m port.f32 174 2048
era compare orc.f32 port.f32     # (truncate port to oracle length first)
```
