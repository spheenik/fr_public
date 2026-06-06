# candytron (fr-030) extraction notes

Oracle source for the v5 / Ronan alignment track (OpenSpec change
`portable-version-native-player`, group 6). Analogue of the fr08
`c1_fr08_harness` work, but the binary is **kkrunchy5-packed**, so step one
is unpacking.

## Provenance

- Source zip: `~/downloads/fr-030_candytron_final.zip`
  sha256 `3e1b7ae4577c24449f71cb1d2585c09135f4768c69510842c7a99fa1d4522184`
- Packed exe: `fr030-candytron-final-101.exe` (65536 bytes, PE32, 2003-08)
  sha256 `2b437a3629f2c815be92df0765af8acd8a185eb3a59cd1fe95a11dce2a99cdc4`
- PE: single section `kkrunchy` RVA 0x1000 / file off 0x1000, rawsize 0xf000,
  SizeOfImage 0xb5f000, ImageBase 0x400000, entry RVA 0xfdc3. Packer marker
  string `kkrunchy5` + `MZfarbrauschPE`.

## Unpacking (c2_unpack.c)

Wine on this host is wow64-only (no pure 32-bit prefix) and never exposes the
guest at 0x400000, so the fr08 live-wine dump recipe (`dump5.py`) does not
apply. Instead `c2_unpack.c` is a self-contained 32-bit loader: it maps the
packed exe flat at 0x400000, jumps to the depacker stub (0x40fdc3), and lets
kkrunchy decompress in-place. The stub layout (objdump-confirmed):

- `mov ebp,0x410000` work area; range-coder source ptr `[ebp]=0x4000d4`
  (packed stream begins at file off 0xd4, right after the minimal headers).
- output ptr `edi=0x410dcb` (pushed at 0x40fe02 = the saved OEP);
  range-coder helpers at 0x40ff37 / 0x40ff7b / 0x40ff91 / 0x40fee1 / 0x40fefb.
- after the decode loop: an E8/E9 call-displacement fixup pass over
  0x411000..0x420000 (the ~60 KB `.text`), then an **import resolver** at
  0x40feaf doing `call DWORD PTR ds:0x40ffa2` / `ds:0x40ffa6`.

The dword at 0x40ffa2 is the unpatched LoadLibraryA placeholder `0x0000ffba`;
in a real process the loader fills it, here `call ds:0x40ffa2` jumps to 0xffba
→ SIGSEGV. **Decompression + the E8 fixup are already complete by then**, so
the harness's SIGSEGV handler dumps the finished image. No PEB/import faking
needed — we only ever wanted the decompressed image, not a running demo.

    gcc -m32 -no-pie -O0 c2_unpack.c -o c2_unpack
    ./c2_unpack fr030-candytron-final-101.exe unpacked.bin   # -> 0xb5f000 bytes

Verified complete: `.text` @0x411000 disassembles as clean post-fixup x86
(standard prologues/`call`/`ret`); nonzero density ~5.5% over the first 2 MB
(expected for a 64k: ~60 KB code + data, rest BSS).

## Embedded josie v2m (task 6.0b cross-check)

- Data-store directory name `josie_.v2m` at dump file off 0x4084c, preceded by
  a werkkzeug-ish `.oO3o.` signature.
- The v2m payload is at dump file off **0x24bf6** (VA 0x424bf6), prefixed by a
  10-byte tag `2a 56 4d 00 00 00 00 72 79 67` = `*VM\0\0\0\0ryg`; the v2m
  stream proper begins at +10 with `e0 01 00 00` (timediv 480, == repo josie).
- **NOT byte-identical to the in-repo `josie.v2m` / `josie_data.v2m`.** The
  stream header diverges immediately after timediv (embedded `32 28 03 00 01..`
  vs repo `80 04 03 00 02..`) yet a large interior body region matches
  (~1.4 KB contiguous around the anchor at repo off 0x1000). Conclusion: the
  demo ships a *different export* of the same song than the repo copy.
  ⇒ The 6.0d oracle must render **candytron's own** v2m, and 6.2 must feed the
  portable that same v2m — not the repo josie. Exact payload length + the
  `*VM..ryg` tag semantics are TODO for the harness step (6.0c).
- Window dump for the record: `/tmp/candytron/josie_candytron.v2mc` (64 KB from
  0x24bf6; not the exact-length file yet).

## Next (6.0c+)

Locate synth / player / Ronan entry points in `unpacked.bin` (pattern-match
against the known v2 synth structure + the fr08 player glue), build a C2
render harness (map at 0x400000, pin rdtsc, RONAN **on**), render the
speech-enabled josie ground truth, then the v5 era assay (6.0e).

## 6.0c reconnaissance — entry-point map (in unpacked.bin / VAs at base 0x400000)

Code (.text) is the E8-fixed region 0x411000..0x420000; data 0x420000..~0x440984.

### V2 synth core — LOCALIZED to ~0x41dc00..0x41e800
- `calcfreq`/pow2 (x87 `fyl2x;fld1;fprem;f2xm1;faddp;fscale`) @ **0x41dc78**.
- **rdtsc seed routine @0x41dc99** (`pusha; clear [ebp+8/2c/30]; rdtsc;
  mov [ebp+0x1c],eax; popa; ret`) — ebp = unit state, seed at [ebp+0x1c].
  Raw rdtsc opcodes (0f31) at **0x41dca8** and **0x41e32d** = the 2 determinism
  patch points (0f31 -> 31c0, like the fr08 C1 harness).
- **synth voice/unit init @~0x41e75b** (`pusha; lea ebp,[ebp+0x30]; call
  0x41dc99` ×3 to seed 3 noise/S&H/dist units; then `call 0x41e0ad` ×2,
  `call 0x41e210`, ... walking sub-units by ebp offset = classic V2 layout).
- More calcfreq callers (osc setup): 0x41dcc0, 0x41e22f, 0x41e34f, 0x41e558,
  0x41ed47, 0x41ef47.

### Control surface (werkkzeug3 sound system)
- **g_player @0xeba32c** (a 0x888-byte operator object; ctor @0x41d19a),
  **g_v2m @0xeba344** (set to **0x424c00** = the v2m stream after the *VM tag).
- **Sound-command dispatcher @0x41cfb4** (cmd in ecx): cmd **2** = init+play
  (body @0x41d05b: malloc 0x888 -> ctor 0x41d19a -> g_player; table loop
  @0x42083c `call 0x41d2cf` ×3; set g_v2m=0x424c00; `call 0x414140`),
  cmd **6** @0x41cff0, cmd **7** = stop @0x41cfd4 (clears g_player).
- Open wrapper @0x41cf9e (`mov edi,&g_v2m; call 0x41415d`).
- Audio output driver: pull/fill callback @0x41400b (flags 0x6a7590/91, ring
  count 0x6a7fc8, copy helper 0x41f8c2); ring **refill/timeline** @0x413b62
  (tempo/beat globals 0x6a75xx); render+clamp-to-±1 @0x41415d (blocks of
  0x1000, ±1.0 consts @0x420d28/0x420d58); audio-engine reset @0x4140df,
  start @0x41410b, wrapper alloc(0x10000 work buf) @0x414140.

### Harness implication (the hard part of 6.0c)
fr08 exposed a clean `RenderProxy(buf,n)`; candytron does NOT. The V2 synth is
wrapped in werkkzeug operator objects and driven by a timeline-synced ring
buffer + DirectSound thread — and the refill path (0x413b62) makes **no direct
call into the synth region**, so the synth is rendered through the operator
graph, not a simple player method. Options for the deterministic offline
render (TBD):
  (a) find the wrapped V2MPlayer::Render inside the operator graph and call it
      directly with our own buffer (preferred — avoids Win32), or
  (b) drive the demo's own init+render path, stubbing the Win32/DirectSound
      imports it needs (heavier; pulls in threads/timers).
Both still pin the 2 rdtsc sites and feed g_v2m=0x424c00. RONAN: not yet
located; the speech filter + phoneme tables are a separate hunt (search the
data region 0x420000..0x440984 for the phoneme table; find the ch15 speech
process call analogous to fr08's 0x40baac).

### 6.0c update — synth Init/Open pinned
- **synth Init/Open @0x41f7e4** (called from the refill @0x413b43): zeroes a
  ~2MB synth state blob (`rep stosb` 0x1e2cc8 bytes @**0x4c2ccc**, in BSS),
  `call 0x41dc08` (synth global init), opens the v2m via `push [esp+0x28];
  **call 0x4144c4**` (= OpenV2M candidate), then inits **32 voices** (loop
  `mov ebp,0x4c4be0; call 0x41e75b; lea ebp,[ebp+0x228]` ×0x20, voice stride
  0x228). Args: [esp+0x24], [esp+0x28] (v2m ptr / globals).
- v2m parser candidate **0x4144c4**; synth state base **0x4c2ccc**; voice
  array base **0x4c4be0** (32 × 0x228).
- STILL TBD for a runnable harness: the per-chunk "render N samples to a
  buffer" entry (synth appears to render via the operator graph into an
  internal buffer that the ring-buffer drains, not a clean RenderProxy(buf,n));
  the play/tick trigger; the exact OpenV2M(0x4144c4) signature. These + writing
  and debugging the C2 harness against the real calling conventions are the
  remaining 6.0c work — materially larger than fr08 because of the werkkzeug3
  operator/timeline wrapping.

### 6.0c BREAKTHROUGH — synth render found @0x41f8c2
`0x41f8c2` = **synthRender** (stdcall, ret 0x10 = 4 args), called from the
DirectSound fill @0x41400b as `(edi=buf, esi=count, ebx, ebx)`.
Unmistakable V2 fingerprint: `fstcw [0x4c2cc8]; and ax,0xf0ff; or ax,0x3f;
finit; fldcw` = **sets x87 PC=24 single precision** (the whole lab's premise),
then loops the 32 voices (active flags @0x4c2cdc, voice stride 0x228). Args:
arg0=[esp+0x24]->0x6a599c (buf), arg1=[esp+0x28]->0x6a5994 (count; if 0 ->
restore CW + ret 0x10), arg2/arg3 -> 0x6a59a0/0x6a59a4.
Load/init sequence (one-time, fn ending @0x413b61): synthInit 0x41f7e4
(args incl. push 0x4, push 0xac44, push [0x6a7fd8]) -> synthSetGlobals
0x41fe84 (arg [0x6a7fdc]) -> 0x414ab7. Per-chunk: synthRender 0x41f8c2.
TODO: confirm whether synthRender self-ticks the v2m sequencer or a separate
tick advances the song; nail synthInit's full arg convention; then build the
C2 harness (map 0x400000, pin rdtsc 0x41dca8/0x41e32d, init, render loop).

### 6.0c engine map — COMPLETE (harness build remains)
Confirmed V2 synth engine entry points (all VAs at base 0x400000):
- **synthInit(patchData, 44100) @0x41f7e4** — zeroes ~2MB state @0x4c2ccc,
  stores patchData ptr @0x4c2ccc, samplerate->0x4c2cd8, calls synthSetSampleRate
  0x4144c4(rate), inits 32 voices (0x41e75b, stride 0x228 @0x4c4be0). 2 args:
  arg0=[0x6a7fd8]=patchData, arg1=0xac44=44100.
- **synthSetSampleRate @0x4144c4** — V2 SRfclinfreq setup (fld1;fld1;fpatan;
  fmul 0x420d5c; ... -> 0x6a9f00/04/08).
- **synthSetGlobals @0x41fe84** — fild's 0x16 (22) global bytes from [esp+0x24]
  to floats @0x67cc20, calls operator setups 0x41f26c/0x41ecee/0x41eedd.
  arg = [0x6a7fdc].
- **synthRender(buf, count, a, b) @0x41f8c2** — stdcall ret 0x10; sets x87
  PC=24 (and ax,0xf0ff; or ax,0x3f; fldcw); self-ticks the sequencer via
  0x41fa2e using sample-countdown @0x4c2e5c; renders 32 voices; calls
  0x41f6b0/0x41f7d2/0x41fa2e. From the fill @0x41400b: (edi=buf, esi=count).
- rdtsc determinism patches: **0x41dca8, 0x41e32d** (0f31 -> 31c0).

The ONE remaining wiring gap: [0x6a7fd8] (patchData) and [0x6a7fdc] (globals)
are a "current-song descriptor" filled by the high-level *VM-container parse
(0x424c00) -- not by a direct mov I can grep (struct-copied). Harness options:
  (i) call the demo's high-level v2m load (in the cmd2 path 0x41d05b chain)
      to populate the descriptor, then synthInit/SetGlobals/renderloop; or
  (ii) reverse the *VM container layout and set the descriptor ptrs by hand
      (patch blob + 22 globals + MIDI stream offsets), feeding 0x424c00.
Next concrete step: build c2_render.c (map unpacked.bin @0x400000, patch the
2 rdtsc, wire the descriptor via (i) or (ii), loop synthRender, write f32),
then iterate on faults -- the fr08 c1_fr08_harness.c is the template.

### 6.0c harness (c2_render.c) — runs synth+speech, note-sequencer still TBD
c2_render.c (in this dir): maps unpacked.bin @0x400000, pins the 2 rdtsc,
stubs the malloc IAT (slots 0x42003c=alloc,0x420050=ident,0x420038=free with
stdcall stubs), sets g_v2m=0x424c00, then:
- **one call to 0x414140** does the WHOLE synth init: parse (0x41387d fills the
  song descriptor @0x6a7fd8: [+0]=patch 0x42d970, [+4]=globals 0x42d956,
  [+8]=timediv 480), synthInit (0x41f7e4: state @0x4c2ccc, 44100 @0x4c2cd8, 32
  voices), synthSetGlobals, AND playerOpen (0x414505, called from inside
  synthInit @0x41f8ae -> sets [0x6a88f8]=0x6a8900 player obj, note table).
- render loop calls synthRender 0x41f8c2(buf,count,0,0).
Init is verified correct (state ptrs right). Frame size [0x422010]=0x80=128.

**BLOCKER: song frozen / silent.** synthRender's tick (0x41f7d2->seq 0x4145c4)
reads player obj [0x6a8900]; its stream ptr [+0x140/144]=0x42e333 decodes as
ASCII **"!kah_m !fao_r !miy_"** = RONAN SPEECH PHONEMES ("come for me"). So
0x6a8900 is the SPEECH/Ronan sub-player and 0x4145c4 is the speech tick. It
advances 1 byte then sets player[0x150]=1 (=0x6a8a50, "wait") and every later
tick branches to 0x41487b without progressing. 0x41487e clears [0x150] (the
per-beat "advance", driven at tempo by the demo timeline/refill 0x413b62).
Clearing it manually each chunk only crawls the SPEECH stream, still silent.
=> The NOTE sequencer that triggers the 32 synth voices is a SEPARATE object
(almost certainly g_player @0xeba32c, set up by the cmd2 path 0x41d05b which I
bypassed to dodge DirectSound). NEXT: init/drive the note sequencer (find what
parses the v2m NOTE streams + ticks the voices), and drive the timeline clock
(advance 0x6a7598 + call refill 0x413b62) so playback runs at tempo.

### 6.0c SOLVED (mostly) — josie renders from candytron's synth
Found the note path: **ProcessMIDI @0x41fbdc** (sets PC=24, reads MIDI stream
[esp+0x24], dispatches via jump table @0x41fbbc) is called by the **refill
0x413b62** (void cdecl: collects this slice's events into buf @0x6a64e8,
terminates 0xfd, calls ProcessMIDI). Driver per 128-sample control frame:
  1. set master clock [0x6a7598] = CUMULATIVE samples (NOT +=, refill zeroes it
     each call but keeps last @0x6a7594, so elapsed = cumulative - last = nf)
  2. call refill 0x413b62  (advances song, triggers the 32 voices)
  3. call synthRender 0x41f8c2(buf,128,0,0)
Result: **AUDIO** -- real music, rms 0.07..0.35, peaks ~1.0 (one 1.29 transient).
c2_josie.f32 / .wav rendered. 0x41400b (the demo's own driver) HANGS without the
DirectSound ring set up -- bypassed by calling refill+synthRender directly.
OPEN: tempo -- using samples/tick=[0x6a7fe0]=480 (timediv) directly; song may
run too fast (no intro silence) -> ProcessMIDI tempo (set-tempo) handling /
the real samples-per-tick needs checking for a sample-accurate oracle. Also:
speech (Ronan) audibility to confirm; then determinism/chunk-invariance (6.0d).

### 6.0c TEMPO FIX — silent intro + music now present
The master clock [0x6a7598] is NOT in samples -- it runs ~960Hz. The demo's
driver 0x41400b computes avail = clk_elapsed*[0x6a75a8]/[0x6a7fe4] (SIGFPE'd
when driven externally, internal clock conflict). The conversion:
**samples = clk * [0x6a75a8]/[0x6a7fe4]; ratio = 0xd249020/0x493e00 = 45.9375
samples per clock-unit.** So advancing the clock by sample count ran the song
45.9x too fast (dense garbage). Fix: per FRAME=256 samples, set
[0x6a7598] = (cumulative_samples * [0x6a7fe4]) / [0x6a75a8], then call refill
0x413b62 + synthRender. RESULT: **first ~6s SILENT then music kicks in**
(rms 0->0.13, peak ~0.6) -- matches josie's structure (user: "silence at the
start, started by 10s"). c2_josie.wav (45s) for listening verification.
OPEN: confirm it's the correct josie by ear; speech audibility; then exactify
the event timing (per-256-frame vs the demo's per-event avail granularity) and
determinism/chunk-invariance for the 6.0d oracle.

### 6.0c BREAKTHROUGH 2 — genthree SOURCE found; player ported
User pointed to genthree/ = the Candytron SOURCE (_viruz2a.asm = period V2 synth,
_viruz2.cpp = the V2M player, ronan.cpp, josie.v2m). genthree/josie.v2m is
BYTE-IDENTICAL to the in-repo josie. The player's exact MIDI timing (the thing
the binary's werkkzeug driver made impossible to replicate):
  smpldelta = (nexttime-time) * usecs / timediv2   [_viruz2.cpp ssRender]
  usecs default = 500000*441 (THE 441/0x1b9 seen in the binary); tempo events
  set usecs = stored*441; timediv2 = 10000*timediv. 3-byte column-stored deltas
  (GETDELTA). => c2_oracle.c: ports ssInitBase/ssReset/ssTick/ssRender VERBATIM
  from _viruz2.cpp, driving the binary's synth VAs (synthInit 0x41f7e4,
  synthSetGlobals 0x41fe84, synthProcessMIDI 0x41fbdc, synthRender 0x41f8c2).
RESULT: clean authentic render, NO hand-tuned speed. Source josie: silent 0-2s,
music 4s+. **Embedded josie (demo's actual song, VA 0x424c00) DIFFERS from source**
(gdnum=1 vs 2, maxtime 206898 vs 197760) -- the demo ships a different export.
Embedded render: quiet pad 0s, full music ~3.5s. Both WAVs for A/B vs the demo.
OPEN: wire RONAN (synthSetLyrics, base.speechptrs) for speech; confirm embedded
matches the demo by ear; then determinism/chunk-invariance for the oracle (6.0d).

### 6.0c RONAN wired -> COMPLETE ORACLE
synthSetLyrics = **0x414ab7** (eax = ptr to 64 lyric char*; rep movsd 0x40 to
texts@0xefa370; sets ronan baseptr/ptr = player[0x140/0x144]). Source: ronan.cpp
synthSetLyrics does memcpy(texts,a_ptr,64*4)+wsptr->baseptr=ptr=texts[0]; note
value selects texts[val] in ronanCBNoteOn. Synth auto-inits ronan (syRonanInit in
synthInit, SR via ronanCBSetSR), ticks it (syRonanTick in synthRender), note-ons
on the speech channel drive it. c2_oracle.c calls synthSetLyrics(base.speechptrs)
in ssReset (gate C2_NORONAN). VERIFIED: ronan vs no-ronan render max|d|=0.738 =>
speech CONTRIBUTES. => c2_embedded_ronan.wav = full demo josie + genuine player +
speech. 6.0c effectively DONE (authentic offline render of josie WITH speech).
NEXT (6.0d): determinism + chunk-invariance checks; then this is the v5/Ronan
oracle to align the portable v5 path + ronan against (6.0e era assay, 6.1/6.2).

### 6.0c/6.0d DONE — speech-enabled oracle COMPLETE + verified
synthSetLyrics = 0x414ab7 IS correct: ronanCBSetCtl (0x41488e, the ch15 ctl
handler reached from synthProcessMIDI jumptable[3] @0x41fdf6) reads texts[] from
**0xefa370** = exactly what 0x414ab7 writes; wsptr = 0x6a8900 (baseptr +0x140,
ptr +0x144). RONAN routing: ch15 note-on -> _ronanCBNoteOn (0x... triggers),
ch15 ctl4 -> _ronanCBSetCtl(4,val) selects texts[val]. Parse verified: ch15
notenum 312(src)/334(emb), ctl[3](=ctl4) 53/56, texts[0..5] = real phonemes
"!kah_m !fao_" = "come for me". USER CONFIRMED (2026-06-06): music AND ronan
both sound correct (isolated ronan-only diff = intelligible speech in bursts
8-12s/16-20s/22-26s/30-34s).
**DETERMINISM + CHUNK-INVARIANCE: PASS** -- identical sha256 across 2 runs and
I/O chunk 333/2048/4096 (rdtsc pinned, no other nondeterminism in this path).
=> c2_oracle.c is the v5/Ronan ORACLE: renders the demo-embedded josie (VA
0x424c00, extract = josie_embedded.v2m) WITH speech, deterministic. Use the
EMBEDDED v2m for demo-match (source josie.v2m differs: gdnum 2 vs 1).
NEXT: 6.0e v5 era assay (read the synth's behavior for v2eras.h), then align
the portable v5 path + ronan (6.1/6.2) against this oracle.

### The two josie files (resolved)
The demo-embedded josie is BYTE-IDENTICAL to **genthree/data/josie.v2m**
(39470 B, gdnum=1, sha256 5bf17fb8...). genthree/data/ is the demo's packed
asset dir = the FINAL export. genthree/josie.v2m (37458 B, gdnum=2) is the DEV
export (== the in-repo josie.v2m). So:
  - ORACLE input = genthree/data/josie.v2m  (what the demo plays; c2_oracle.c
    now defaults to it). Renders IDENTICALLY to the binary-extracted embedded
    v2m (same sha256) -- no extraction needed, it was in-repo all along.
  - genthree/josie.v2m = the dev variant (different tempo map).

## 6.0e — v5 era assay (2026-06-06): every ledger row read off the binary

Method: the genthree `_viruz2a.asm` (candytron's synth source) is
**byte-identical** to `RG2/ViruzII/synth.asm` and `RG2/Viewer/synth.asm`
(modulo the license header line), so the three in-repo "period sources"
collapse to ONE v5-era synth — no v1–v4 anchors from them, and no
disagreements to reconcile. Source↔binary correspondence verified by 6
independent signatures in the unpacked image (objdump of unpacked.bin,
adjust-vma 0x400000): modern LCG `imul 0xbb38435` ×3, native `fsin` osc
@0x41dfc3 / FM @0x41e094 / LFO @0x41e43b, native `fpatan` dist render
@0x41e60b, crusher render two-mul `fmuls gain1; fmuls crush1` @0x41e665,
env decay WITHOUT the LOWEST(0x39000000) runout clamp @0x41e199, modern
attackmul -0.09375 @0x41dbd4, and NO 2^-18 dcoffset float anywhere in the
11.4MB image.

### OLD behavior at v5 → row flips pinned at exactly v6 (now EV_PROVEN)
ENV_CLAMP_SUSREL, NATIVE_FSIN, NATIVE_FPATAN, NO_DCOFFSET,
CRUSHER_SPLIT_GAIN1, NO_VOICE_DCF, NO_MASTER_DCF (master chain = moddel →
lc/hc EQ → comp, no DCF stage), NO_MOOG (syFRTab modes 6/7 = bypass; dist
has only the 5 SVF filter modes), KEYSYNC_OSC_ONLY (no HARDSYNC path, no
DCF init in noteOn), NO_AUX_BUSSES (osc jtab 6/7 = off; was ANCHORED).

### NEW behavior already at v5 → threshold stays 1/ASSUMED, gap now v1–v4
OSC_BOXFILTER (OSM convolution, identical casetab structure to 2004),
NOISE_LCG_MSVC (modern constants + modern shr9|0x40000000 float gen),
OSC_FREQ_CONST (runtime fcoscbase 261.625..., 1× advance),
PGMCHANGE_V0 (byte-identical handler to 2004), TICK_BEFORE_SET (SET→TICK),
SUBFRAME_RENDER (full-frame render + tickd doling), RVB_E_FULLPREC
(SRfclinfreq factor present; set runs inside synthRender at PC=24).

### Anchored rows cross-checked, all consistent
ENV_CURVES(2): modern curves/calcfreq2 at v5. FRAME256(2): fcframebase=128.
NO_COMP_BOOST(1): compressor + bass boost both present. NO_RVB_LOWCUT(4):
lowcut param + hpf stage present.

### Two structural discoveries (engine edits, not just data)
1. **Sine eval / phase advance decoupled.** v5 sine = the v0 native-fsin
   evaluation ([1,2)·2π → fsin) but with 1× advance on the runtime freq.
   The v2eras static_assert no longer couples NATIVE_FSIN to
   OSC_FREQ_CONST/BOXFILTER; v2core renderSin_v0 derives its step (freq<<2
   vs freq) from DELTA_OSC_FREQ_CONST.
2. **FM osc didn't exist at v0** — NEW ROW DELTA_NO_FM_OSC. The 2000
   oscjtab (@0x40a565, fr08 depacked.bin, file+0x401000 mapping) maps
   modes 5/6/7 → OFF; the lab port's "FM shared across eras" was
   unexercised conjecture. v5 FM (.mode4) = integer phase modulation
   (mod·fcfmmax·fc32bit → fistp → 32-bit wraparound add) + native fsin —
   structurally different from 2004's float-add + fastsinrc. Ported as
   renderFMSin_v5, gated NO_FM_OSC (off) → NATIVE_FSIN (v5 impl) → modern.

Regression after the rewiring: all 17 baselines.sha256 hashes unchanged
(v6 corpus + fr08-v0-native), mathcheck / twoinstance / tablecheck /
loadcheck / forcecheck PASS. The edits only change v1–v5 behavior — i.e.
exactly what 6.2 will validate against c2_josie (native v5 render).

## 6.2 — josie v5 localization (started 2026-06-06)

Method: channel-solo both sides (oracle `C2_SOLO` env in c2_oracle_solo.c;
portable `V2SEQ_SOLO` already exists, NDEBUG-guarded) + section-by-section
normalized diff of genthree/_viruz2a.asm (== the v5 binary) vs v2/synth.asm
(2004). At the very first note onset global FX are ~passthrough (no history),
so a solo'd channel's early divergence is its own voice/channel path.

### Divergence map
- First/only divergence at onset is **ch7** (pgm7, first note tick 240 ≈
  0.235s); ch7 solo reproduces the full-mix early divergence exactly.
- ch7 pgm7 path: osc1=tri/saw(OSM), osc2=**noise**, VCF band(mode2,resonant),
  dist=clip(≈passthrough here), channel **chorus** on; boost/comp off.

### BUG FOUND + FIXED: osc/LFO seed source mis-coupled (real v5 era bug)
The portable keyed the osc-noise + LFO-S&H **seed** choice on
DELTA_NOISE_LCG_MSVC: old→seed 0, else→the 2004 fixed table {0xdeadbeef,
0xbaadf00d,0xd3adc0de} (osc) / libc-rand (LFO). But the fixed table is a **v6
addition** (synth.asm `_OSC_` oscseeds; absent in genthree). v0 AND v5 both
seed from **rdtsc** (fr08 syOscInit @0x40a + candytron syOscInit @0x41dc99 /
syLFOInit rdtsc @0x41dca8,0x41e32d) → 0 under the pinned-rdtsc convention.
At v5 the LCG constants are modern (196314165) but the SEED is still rdtsc(0).
Coupling broke exactly in v1..v5.
FIX: new row **DELTA_RDTSC_SEED** (flipsAt 6, EV_PROVEN); 3 seed sites
(syVOsc::init, V2LFO::init, the era-setter re-seed loop) routed through it.
Effect: ch7 solo rms|d| 0.00376→0.00236 (-37%); whole-song josie full-mix
rms|d| 0.163→0.127 (-22%), max|d| 1.31→0.79. v0/v6 baselines UNCHANGED
(delta only differs from NOISE_LCG in v1..v5), all unit tests PASS.

### Remaining residual (open)
After the seed fix, ch7 still has a **smooth, monotonic, saturating** diff
(~+0.043 over ~3 frames ≈ an 18 Hz step response), NOT jagged → not the noise.
Every section (OSC OSM, VCF SVF, ENV, MODDEL chorus, COMP, BOOST) is identical
v5↔2004 in source EXCEPT: native fsin/fpatan (gated), moog/aux (v6, gated),
the seed table (now fixed), env decay-runout (gated), and `%if FIXDENORMALS`
dcoffset adds — and the candytron build has **no** 2^-18 constant in the image,
so FIXDENORMALS was OFF (dcoffset correctly gated off at v5). So the residual
is NOT explained by any source-level section diff found so far. Whole-song
residual is bounded + section-correlated (rms 0.07..0.16, no growth) → looks
like accumulated per-voice/channel state/precision (chorus-state or a resonant-
VCF coefficient razor-tie), each channel likely contributing its own. Next:
isolate ch7's chorus vs voice (needs editing the v5-LAYOUT chan bytes in the
v2m — NOT the v6-canonical offsets; v5 chan omits the 4 auxa/b rcv/snd params),
then sweep the other channels. Tooling left in place: c2_oracle_solo (C2_SOLO),
V2SEQ_SOLO. (Debug taps were temporary and have been removed; tree is clean.)

### 6.2 ch7 deep-dive (2026-06-06 cont.) — narrowed to per-note osc phase/state
Stripped ch7 to bare voice in the v2m (both sides; CORRECT v5 layout offsets:
voice 0-58, chan: chanvol59/reverb60/delay61/fxroute62/boost63/chandist64-67/
chorus68-74/comp75-83 -- v5 OMITS the 4 v6 auxa/b rcv/snd, so v6-canonical
offsets are wrong for editing the FILE). Tooling: /tmp/candytron/c2_oracle_solo
(C2_SOLO env) + V2SEQ_SOLO. Isolation results (ch7 solo, vs c2 oracle):
- canonicalization VERIFIED correct: raw v5 chan bytes (dist/chorus/comp) ==
  portable's in-memory v6-canonical values, byte-for-byte. Not the loader.
- chorus stripped -> divergence UNCHANGED. Not the chorus.
- osc2(noise) off -> UNCHANGED. Not the noise (for ch7).
- VCF bypassed -> divergence GREW (band filter was masking it) -> it's the osc.
- bare osc1->volramp->chanvol (no flt/dist/chorus/comp) -> still diverges,
  rms 0.0186. Pure voice path.
- trisaw_flt float->double: ch7 UNCHANGED (and double breaks v6 baselines) ->
  NOT the OSM coefficient precision.
- freq formula == v5 asm (pow2((pitch+note-60)/12)*SRfcobasefrq, fistp);
  SRfcobasefrq=(fcoscbase*fc32bit)*(1/sr)=12740060 == v5 calcNewSampleRate
  EXACTLY. v2_oscfreq mathcheck-validated vs x87. So integer freq matches.
- voice-allocation/steal logic: IDENTICAL v5 asm vs 2004 (only data->ebp
  per-instance addressing differs). Same voice choices.
- sequencer event timing: v2seq UpdateSampleDelta == c2 oracle EXACTLY
  (usecs=5000*SR==500000*441, td2=10000*timediv, remainder-carry). Matches.
KEY OBSERVATION: the FIRST note matches for ~9 periods; a LATER note (new
onset from silence @0.28s) diverges from its 2nd sample, growing quadratically
(=linear osc-slope error integrated). oscsync(keysync)=0 for pgm7 -> osc phase
(cnt) is CONTINUOUS across notes/steals, never reset. So a note's osc phase
depends on the entire prior voice history; once any prior render differs by a
hair, the stolen-voice phase diverges and every subsequent note on that voice
is off. This is a per-note osc-phase/state divergence, bounded + non-growing
(matches whole-song rms 0.07..0.16, section-correlated) -- the same FAMILY as
[[portable-seq-timing-bug]]'s stolen-voice observation, now on a true-era v5
oracle. NOT yet root-caused: needs tapping the BINARY's internal per-voice osc
buffer (instrument c2_oracle to dump synth voice state @0x4c4be0 stride 0x228)
to compare osc-for-osc and find which note/voice first desyncs and why (phase
vs a 1-ULP something in a specific note's render). That binary-tap is the
clean next step. Banked this session: the DELTA_RDTSC_SEED fix (-22%).

### 6.2 cont. (2026-06-06) — voice DSP EXONERATED via binary tap; +CC6 hicut delta
User prompt: use the genthree player SOURCE, not a binary reverse. Done:
- genthree _viruz2.cpp PLAYER is byte-structurally identical to the portable
  v2seq: same event dispatch (pc/cc/pb/notes, running status), same sub-frame
  smpldelta render-chunking (ssRender == V2MPlayer::Render), same UpdateSampleDelta
  (usecs=5000*SR==500000*441, td2=10000*timediv, remainder carry). Player RULED OUT.
- Direct per-voice tap of the BINARY (voice array 0x4c4be0 stride 0x228;
  osc1.cnt@+0x38, freq@+0x3c, curvol@+0x0c, volramp@+0x10) vs the portable
  (voicesw[0]), MATCHED chunk size (C2_CHUNK=4096): osc freq BIT-IDENTICAL
  (4772130), osc phase cnt IDENTICAL, curvol/volramp IDENTICAL (0.245854303,
  -6.868e-9). => the VOICE DSP (oscillator + envelope + volramp) is bit-exact
  vs candytron. The residual is NOT in the voice; it's downstream in the
  channel/global path (CC1-modulated aux2/delay sends, master lc/hc EQ, chgain).
  The earlier "bare ch7" test was not truly bare -- the global delay (ch7's
  CC1->aux2 send, 279 CC1 events) + master EQ were still active.

REAL BEHAVIOR FOUND + FIXED (2nd of the session): **DELTA_CC6_HICUT**
("FAKE 2: Lowcut!"). v0 (fr08 ProcessControlChange @0x40bded) AND v5 (candytron
/ all three byte-identical RG2 _viruz2a.asm) map **MIDI CC6 on channel 15** to
the master high-cut: hcfreq = sqr((val+1)/128), in addition to storing the
controller. 2004 dropped it. Gated {6, EV_PROVEN}; engine fix in processMIDI
case 3 (chan==15 && ctrl==6). SAFE: v0/v6 baselines unchanged (fr08 sends no
ch15/CC6), all tests PASS. NOTE josie's ch15 sends CC4(56)/CC5(24)/CC7(1) but
**no CC6**, so josie itself doesn't trigger it (whole-song rms unchanged at
0.127) -- it's a correctness/completeness addition, proven, for v0/v5 files
that do use it; NOT the josie residual.

NET this session: 2 proven era-table corrections (DELTA_RDTSC_SEED -22% on
josie; DELTA_CC6_HICUT). Voice DSP proven bit-exact vs candytron. josie residual
(rms 0.127) now localized to the per-channel FX / global mix path (aux/delay
sends, master EQ, chgain), NOT the synth voice -- the clean next thread is to
tap the binary's chanbuf/aux/mix stages (the channel-process + RenderBlock
path) the same way the voice was tapped.

## ========== RESUME HERE (6.2 handover for a fresh context) ==========
Goal of group 6.2: drive the portable's native v5 render of josie to (near)
zero vs the candytron oracle, documenting the transcendental-tie residual as ε.

### Current state (2026-06-06)
- 6.0e DONE (era assay; v2eras.h has the v5 PROVEN rows + DELTA_NO_FM_OSC).
- 6.2 IN PROGRESS. josie whole-mix residual vs c2_emb.f32: rms 0.127, max 0.79.
- TWO proven era-table fixes committed (both: v0/v6 baselines unchanged, tests
  PASS, only differ in v1..v5):
  1. DELTA_RDTSC_SEED {6,PROVEN}: osc-noise + LFO-S&H seed is rdtsc(->0) pre-v6,
     not the 2004 fixed table. -22% on josie (0.163->0.127).
  2. DELTA_CC6_HICUT {6,PROVEN}: ch15 CC6 -> master hicut sqr((val+1)/128).
     Correct for v0/v5 but josie sends no ch15 CC6, so 0 effect on josie's #.
- PROVEN bit-exact vs candytron (direct binary tap, matched chunk size):
  the VOICE DSP (osc freq/phase cnt, envelope, volramp) AND the PLAYER
  (v2seq == genthree _viruz2.cpp). So the residual is NOT voice/player.
- => RESIDUAL IS IN THE PER-CHANNEL FX / GLOBAL MIX PATH: CC1-modulated
  aux2/delay sends, the channel chain (dist/chorus/comp/boost), master lc/hc
  EQ, chgain, reverb. NEXT STEP below.

### NEXT STEP
Tap the BINARY's mix-path stages the same way the voice was tapped, and find
the first stage that diverges from the portable:
  - chanbuf (per-channel buffer, post voice-mix, pre channel-FX)
  - aux1buf/aux2buf (reverb/delay sends), mixbuf (master accumulate)
  - master lc/hc EQ output (RenderBlock hcfreq/lcfreq one-pole)
Binary addresses (base 0x400000, from "6.0c engine map"): synth state
@0x4c2ccc; the mix/aux/chan buffers are in the synth BSS near there -- locate
chanbuf/aux1buf/aux2buf/mixbuf by disassembling syChanProcess (_V2CHAN_) and
.RenderBlock in the unpacked image. Compare per-frame against the portable's
inst->chanbuf/aux1buf/aux2buf/mixbuf (v2core V2Instance). Likely suspects given
"everything matches by source": a CC1-modulated send/param remap, or a
master-EQ coefficient. Also re-audit the mod-dest remap for the CC1 mods
(mod3 dest67=boost.amount, mod4 dest65=aux2/delay-send, mod5 dest13=osc2.vol)
-- verify the v5->v6 dest remap puts them on the right canonical param.

### TOOLING (all in /tmp/candytron unless noted; rebuild from repo if cleared)
- Oracle (candytron binary synth + genthree-ported player), with solo + taps:
  SRC: v2/v2m/candytron-extraction/c2_oracle_solo.c  (committed)
  BUILD: gcc -m32 -no-pie -O0 c2_oracle_solo.c -o c2_oracle_solo
  RUN: ./c2_oracle_solo /tmp/candytron/unpacked.bin <v2m> <out.f32> <secs>
  ENV: C2_SOLO=<ch> (keep only that channel's MIDI), C2_CHUNK=<n> (match the
       portable's outer chunk, use 4096), C2_VTAP=1 (per-note osc freq/cnt),
       C2_CTAP=1 (voice0 cnt/curvol/volramp per render chunk), C2_NORONAN=1.
  Voice array @0x4c4be0 stride 0x228; osc1.cnt@+0x38 freq@+0x3c (syWOsc:
  mode0/ring4/cnt8/freq12...), curvol@+0x0c volramp@+0x10 (syWV2 head).
- unpacked.bin: re-derive with c2_unpack.c if /tmp cleared (see "Unpacking").
- Portable: v2/portable/v2dump <v2m> <out.f32> <secs> <chunk>; env V2SEQ_SOLO=<ch>
  (NDEBUG-off builds). Detects v5 automatically for genthree/data/josie.v2m.
- Oracle reference renders: c2_emb.f32 (no-ronan), c2_emb_ronan.f32 (with).
  Regenerate: C2_NORONAN=1 ./c2_oracle_solo unpacked.bin genthree/data/josie.v2m c2_emb.f32 47
- v2m INPUT for both = genthree/data/josie.v2m (v5, == demo-embedded, sha
  5bf17fb8...). NOT genthree/josie.v2m (dev variant, gdnum 2).
- v5 FILE chan-byte edit offsets (patch7=37016 for pgm7; v5 layout, NOT v6 --
  v5 OMITS the 4 auxa/b rcv/snd): voice 0-58; chanvol59, reverb/aux1 60,
  delay/aux2 61, fxroute62, boost63, chandist64-67, chorus68-74, comp75-83,
  maxpoly84, modnum85, modmatrix 86+ (3 bytes/mod: source,val,dest).
  osc2.mode@patch7+8, vcf1.mode@patch7+20. (Use to strip stages in BOTH sides.)
- Compare: python3 numpy rms/max on the two .f32 (interleaved stereo float32).
