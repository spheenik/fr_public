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
   **The true speech A/B exposed a PORTABLE ch15 divergence — and the cause is the
   CARRIER, not ronan (SOLVED 2026-06-11).** Built with `V2_RONAN=1` the portable feeds
   the same lyrics (`v2seq.cpp:175`) + gets note-ons (`v2core.cpp:3719`), yet ch15 is
   near-silent (solo peak 0.015, rms 0.003, corr 0.05). The earlier "ronan version/era
   difference" guess is **DISPROVEN** on three independent grounds:
   - **ronan source is identical.** `werkkzeug3_kkrieger/ronan.cpp` ≡ the lab `v2/ronan.cpp`
     the portable forked (same syls table, phoneme bytes, `db2lin` −70/6, `NOISEGAIN`,
     post-EQ, framerate=3). The only structurally-different variant (RG2/ViruzII: no
     post-EQ, `a_bypass×0.1`) is *quieter*, so no version explains "genuine 11× louder".
   - **ronan is FAITHFUL.** Traced over the intro: the sequencer walks the full marker-
     less lyric `AH27OH40UH30IH20EH80AH` (curp2 60→39→42→25→16→60), `a_voicing≈0.397`
     97% of ticks, per-vowel formant gains correct. ronan vocodes whatever carrier it is
     handed; here it is handed silence. (kkrieger is the ONLY corpus song whose lyrics
     are `!`/`_`-free free-run — candytron/josie are all `!`-gated — but that path is
     also proven correct here.)
   - **The CARRIER is the bug — and the exact cause is the FM-osc PHASE SCHEME
     (ROOT-CAUSED 2026-06-11).** ch15's energy is fine (raw-carrier rms 0.039 ≈ the
     genuine 0.041) but its SPECTRUM is collapsed: portable carrier is ~all <200 Hz
     (sub-200 ≈ 12300 vs ~210 above 500 Hz); the genuine carrier is a rich 65 Hz harmonic
     series. A formant filter is a high-Q bandpass at 500–3000 Hz — with no harmonic
     energy there, ronan can produce no speech. ch15 patch (pgm12) runs **three same-pitch
     oscillators** — osc0 PULSE, osc1 SAW, osc2 FM (mode 5) — all at 65 Hz (detune=64).
     V2's FM osc *replaces* the voice buffer, using the preceding oscs (pulse+saw, ±2) as
     its **modulator**, so the carrier IS the FM output. **The portable renders v5 FM with
     the INTEGER `fistp` phase scheme** (`renderFMSin_v5`: `modi = fistp(mod·4·2³¹)`), which
     **overflows a 32-bit int 100 % of samples** on the ±2 modulator (x87 fistp → constant
     `0x80000000`) → the FM degenerates to a phase-shifted pure sine → carrier collapses.
     **kkrieger-beta's 2004 engine uses the FLOAT phase scheme** (`renderFMSin`:
     `t = (mod·2 + phase_float[1,2))·2π`, `fastsinrc`), which adds the large modulator in
     float with graceful wrap → rich FM. **Proven by disassembly of both binaries:**
     candytron FM @0x41e066 = integer/`fistp`/native-`fsin`, depth 4.0 (@0x41dbd0); kkrieger
     FM @0x8086a2 = float/`fadd`/`fastsinrc` (@0x808178), depth 2.0 (@0x808080). **FIX
     (proven):** dispatch kkrieger's FM to `renderFMSin` (float). The ch15 carrier then
     matches the genuine near-bit-exactly (sub-200/above-500 = 8370/6838 vs genuine
     8372/6839), ch15 speech **corr 1.000** (peak 0.177/0.177, 100 % energy), and the
     **whole 30 s song** matches the own-engine oracle at **corr 0.9995, rms|d| 0.0013,
     max|d| 0.013** — ε-floor, the same late-song razor-tie class as fr024/fr029 (§
     [[v5-corpus-own-engine-verified]]). NOT a ronan/speech-synth gap; ronan is faithful.
     Repro: build the portable, dispatch FM via `renderFMSin`, render `embedded/kkrieger.v2m`,
     compare to `orc_lyrics.f32`.
   - **BLOCKER — build-era delta WITHIN format v5 (cannot be version-gated).** fr029
     (v5, ~2003) uses **integer** FM and is PROVEN faithful with it (flipping it to float
     regresses it by max|d| 0.91); kkrieger (v5, 2004) uses **float** FM. Both fingerprint
     as format v5 (identical `globSize`=22 and patch-param-count), so the loader cannot
     auto-distinguish them, and **no content heuristic works**: candytron (2003) has speech
     + ch15 + integer FM (kills "speech/ch15 ⇒ float"); fr029's FM modulator *also*
     overflows yet wants the degenerate integer result (kills "overflow ⇒ float"). The era
     model had assumed "float FM = v6 (2012)"; kkrieger-beta proves it appeared in the 2004
     build. This is the **2nd build-era ambiguity** after the brüllwürfel noise-seed sub-era
     ([[brullwurfel-v5-oracle]] §7) — a fix needs a *provenance-supplied* era hint, not a
     format gate. Until then the engine default (integer FM) leaves kkrieger ch15 broken.
3. **The instrument engine is faithful — and the FM fix lifts the WHOLE mix to ε-floor.**
   The previously-documented "FM/noise channels phase-decorrelate" residual (below) was
   **largely the same integer-FM bug**: every FM channel ran the overflowing integer scheme.
   With the float FM scheme the **whole 30 s mix** (every channel + ch15) jumps from corr
   **0.44 → 0.9995** (rms|d| 0.0013), per-second 1.000 for 0–21 s then 0.999. So kkrieger is
   not merely "energy-faithful" — with the fix it is **own-engine-proven at ε-floor**,
   joining fr024/fr029 (and actually the tightest of the three, max|d| 0.013). The original
   integer-FM solos were: ch7 **0.998** (tonal sine, sample-aligned, tiny ≈326 Hz harmonic
   residual) and ch11 (FM) **0.768** ("phase decorrelates") — the ch11 figure is the
   pre-fix integer-FM artifact, resolved by the float scheme.

**Disposition.** kkrieger's genuine engine is unpacked, runs, renders every channel, and
— with the FM-scheme fix — matches its own engine whole-song at **corr 0.9995 (ε-floor)**.
ronan is FAITHFUL (sequencer + gains proven). The one open item is the **gating** of the
FM-scheme fix (build-era within v5, above); the engine default still uses integer FM, so
out of the box kkrieger ch15 is silent and the FM channels decorrelate. Other residuals:
(a) the **FM-scheme gating** (the open follow-up); (b) the small ch7 ≈326 Hz 5th-harmonic
timbre residual (the only thing keeping the fixed song off bit-exact). The old "FM/noise
razor-phase" residual is **subsumed by (a)** — same root, resolved by the float scheme.
