# .kkrieger (beta) — v5 own-engine oracle

**Unpack SOLVED (2026-06-10).** The kkrunchy_k7 image reconstructs to coherent code;
the oracle loads and RUNS kkrieger's genuine V2 engine. **The earlier "osc-amplitude
silence" diagnosis was WRONG** — the engine renders all 13 channels correctly; the
near-silent intro is the **ronan speech channel (ch15) vocoding to nothing without
lyrics**, and the intro happens to be ch15-only. A/B now characterised — see §3.

## 1. The unpack blocker — SOLVED

kkrieger-beta is packed with **kkrunchy_k7** (ryg, 2004; source is in this repo at
`kkrunchy_k7/`), a *newer* kkrunchy than candytron's. k7 adds an x86 **split-stream
disassembly filter**: the packer (`exepacker.cpp` → `DisFilter::Filter`) separates
opcode bytes from their operand/immediate/displacement bytes so each stream
compresses better; the stub reverses it with `DisUnFilter` (stage **depack2.asm**).

The generic `era unpack` route stopped at the stub's **import-resolution loop**
(`depacker.asm` ~L466: `mov esi,IMPORTS; mov ebx,LOADLIBRARY; call [ebx]`). With no
Windows loader the IAT slots hold filler (0x17bb7), so `call [ebx]` faulted —
**before** stage1 returns to depack2. The un-filter therefore never ran and the
dumped `.text` stayed in split form: opcodes stripped from operands. Proof: the
address-free fild-loop heart `d91f8d7f0449` was present 7× in candytron/fr024/fr029
and **0×** here, and the noise LCG sat as bare bytes `35 84 b3 0b 6b 63 19 36`
without its `imul`/`add` opcodes. (candytron is an *older* kkrunchy with no such
filter, which is why it always worked — the stub-stop `fetch-unmapped` at a low addr
is the *normal* kkrunchy finish, not the tell.)

**Fix:** stub the two imports so the loop completes → stage1 returns → depack2 runs
`DisUnFilter` → coherent `.text`. `kkr_unpack.py` (this dir) maps a scratch page with
`LoadLibraryA`(ret 4)/`GetProcAddress`(ret 8) stubs returning a fake nonzero value,
patches the IAT slots at the import-loop entry, runs to the post-un-filter OEP fault,
and dumps. Result verified: `fild-heart` and `imul-LCG` both present, fully
disassemblable.

```
unzip ~/downloads/kkrieger-beta.zip pno0001.exe -d /tmp/kk
python3 kkr_unpack.py /tmp/kk/pno0001.exe unpacked_fixed.bin   # base 0x7c0000
```

**Image base is 0x7c0000** (not 0x400000 like the other demos) — the toolkit's
`era disasm/assay` assume 0x400000, so add 0x3c0000 to their reported VAs, or use a
capstone disasm at base 0x7c0000.

## 2. Oracle — runs the genuine engine

Built like fr024/fr029 (`../toolkit/v5_oracle.c`) but at base 0x7c0000. Synth-entry
VAs (found by the same fingerprints, in the now-coherent image):

| fn | VA | note |
| --- | --- | --- |
| synthInit ret8 | **0x809e2e** | zeroes 0x1e2cc8 state @0x8c4f48; 32 voices @0x8c6e5c stride 0x228 |
| synthRender ret16 | **0x809f08** | PC=24; out buf = arg0; mixes internal 0x8c4644 → buf |
| synthProcessMIDI ret4 | **0x80a222** | |
| synthSetGlobals ret4 | **0x80a530** | 22 globals → floats @0xa7ee9c |
| rdtsc | **0x8082ae, 0x808966** | |

The `-DKK_*` flags below enable env-gated kkrieger probes baked into `v5_oracle.c`
(harmless to fr024/fr029, which don't define them): `KK_VOICEBASE`+`KK_CHANMAP`
arm `KK_VTRACE` (per-sec voice dump: chan/gate/curvol/osc mode·gain·freq) and
`KK_MIDITRACE` (emitted midibuf per tick); `KK_RONAN_JNE` arms `KK_NORONAN`
(patch the ch15 `syRonanProcess` guard `jne`→`jmp` so ch15 renders the RAW carrier).
`VA_LYRICS` (generic, not KK-only) makes `ssReset` call the binary's `synthSetLyrics`
after `synthSetGlobals` — mirrors the genuine player's `ssReset` tail and feeds the
v2m's 256 phoneme-string pointers into ronan so **ch15 vocodes real speech** (§3).

```
gcc -m32 -no-pie -O0 ../toolkit/v5_oracle.c -I../toolkit -o oracle \
  -DORACLE_IMG_BASE=0x7c0000u -DIMG_SIZE=0xd87000 \
  -DVA_INIT=0x809e2e -DVA_GLOB=0x80a530 -DVA_MIDI=0x80a222 -DVA_REND=0x809f08 \
  -DRDTSC0=0x8082ae -DRDTSC1=0x808966 -DVA_LYRICS=0x80742f \
  -DKK_VOICEBASE=0x8c6e5c -DKK_CHANMAP=0x8c4f58 -DKK_RONAN_JNE=0x80a0fb
./oracle unpacked_fixed.bin ../embedded/kkrieger.v2m orc.f32 30
```

`synthSetLyrics` @**0x80742f** (stdcall ret4, `const char**`): `w=*wsptr`@0x84286c,
`rep movsd` 64 ptrs → `texts`@0x842a00, `w.ptr`/`w.baseptr`(+0x140/+0x144)=`texts[0]`.
With `VA_LYRICS` defined the engine vocodes speech (peak 0.239); without it ch15 is silent.

Verified working end-to-end: `setSampleRate` produces the canonical oscbase
12740060; `synthProcessMIDI` allocates voices; envelopes generate correctly; and
**all 13 channels render** (peak 0.153, rms 0.019 over 30 s).

## 3. A/B vs the portable — characterised (engine faithful; ronan + razor-phase residual)

The "osc-amplitude silence" of the earlier note was a **misdiagnosis**. The chain of
evidence:

1. **The intro is ch15-only.** The other channels' tick-0/tick-12 events are
   *note-offs* (vel 0); their real notes don't start until tick ~3072 (≈21 s). So the
   first ~21 s is genuinely just channel 15 — the `KK_VTRACE` dump shows the only
   active voices in that window are all ch15 (curvol correct ≈0.96, osc gain 0.99,
   freq correct), yet the mix is ~7e-8.
2. **ch15 is the ronan speech channel.** `RenderBlock` (binary @0x80a0f8) routes
   `cl==15` through `syRonanProcess` (@0x809e24), which vocodes the channel's raw
   oscillator carrier through the speech synth. **Without lyrics, ronan zeroes ch15**
   → the ch15-only intro is ~silent. NOPing the guard (`KK_NORONAN`) makes ch15 render
   the raw carrier and the intro becomes audible (peak 0.27). The collapse is *not* an
   osc-gain global and *not* the brüllwürfel SR-const class — it is the
   harness-doesn't-drive-ronan-lyrics path. **Feeding lyrics (`-DVA_LYRICS=0x80742f`,
   §2) fixes it: the genuine engine then vocodes real speech** — ch15-solo peak 0.21,
   rms 0.038, formant-clustered (326–457 Hz F1/F2), tonal/voiced, syllabic time-varying
   envelope. That is the faithful reference (engine driven exactly as the real player:
   `synthInit→setGlobals→setLyrics→MIDI`).
   **The true speech A/B exposes a PORTABLE ronan divergence.** Built with `V2_RONAN=1`
   the portable also feeds lyrics (`v2seq.cpp:175 synthSetLyrics(m_synth,speechptrs)`,
   gets note-ons at `v2core.cpp:3719`), yet its ronan produces **near-silence** on ch15
   (solo peak 0.015, rms 0.0035, corr 0.05 vs the genuine) — ~11× too quiet, *below*
   even its own raw carrier (0.04–0.11), as if vocoding the silence phoneme. Same
   ronan.cpp source + same lyrics, so the gap is a **ronan version/era difference**
   (kkrieger-beta 2004-04 vs the RG2/v6 ronan the portable ported) or a workspace-init
   delta — a separate speech-synth fidelity gap, NOT the instrument engine.
3. **The instrument engine is faithful.** Excluding ch15 (portable `KK_MUTECH=15` vs
   the oracle's ronan-silenced ch15): whole-mix corr **0.85**, peaks 0.153/0.143, rms
   0.0192/0.0187 (≈3 %). Per-channel **solos** (strip the v2m to one channel by zeroing
   the other channels' velocity columns — `/tmp/strip.py` idiom — and render both):

   | channel | osc1 mode | corr | rms-ratio orc/port | reading |
   | --- | --- | --- | --- | --- |
   | ch7  | 2 (sine)   | **0.998** | 1.03 | tonal; tiny 5th-harmonic (≈326 Hz) timbre residual |
   | ch11 | 4 (FM)     | 0.768 | 0.99 | FM osc: energy matches, **phase decorrelates** |

   ch7 is sample-aligned (lag 0) and tonal (the diff is 92 % <500 Hz, spectrally tonal
   — **not** noise). ch11's FM oscillator matches in energy but razor-phase
   decorrelates — the same continuous-osc-phase / FM razor-tie class as fr024/fr029's
   late-song drift ([[v5-corpus-own-engine-verified]]) and the brüllwürfel noise-seed
   sub-era delta ([[brullwurfel-v5-oracle]] §7), just **heavily exercised** here by
   kkrieger's FM bass + drums + 13-channel mix. The whole-mix 32-sample lag comes from
   those FM/noise channels, not the tonal ones.

**Disposition.** kkrieger's genuine engine is unpacked, runs, and renders every
channel; instrument fidelity is structural/energy-faithful (tonal channels ≈0.998).
Characterised, non-blocking residuals: (a) **ronan/ch15 — lyrics now fed
(`synthSetLyrics`@0x80742f); the genuine engine vocodes real speech, but the portable's
ronan diverges hard (near-silent), a separate speech-synth version/era gap** —
the open follow-up here; (b) FM/noise channels phase-decorrelate (known razor-tie
class); (c) the small ch7 326 Hz harmonic timbre residual. It is **not** bit-exact like
fr024/fr029.
