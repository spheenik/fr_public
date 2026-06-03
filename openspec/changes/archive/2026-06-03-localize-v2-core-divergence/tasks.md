## 1. Harness infrastructure

- [x] 1.1 Extend the generated asm copy (`synth_noronan.asm` step in `validate/build.sh`) to also `global`-export the block routines (`syOscInit/Set/NoteOn/Render`, `syEnvInit/Set/Tick`, `syFltInit/Set/Render`, `syLFO*`, `syDist*`, `syDCF*`, `syBoost*`, `syModDel*`, `syComp*`, `syReverb*`, `syV2*`, `syChan*`) — WITHOUT editing the original `synth.asm`. **asm_appendix.asm exposes routines/SR globals/offsets; original untouched.**
- [x] 1.2 Write a register-ABI trampoline (asm or inline-asm) that calls an asm block routine with `ebp`=state ptr and the block's buffer/count registers (e.g. OSC: `edi`=dest, `ecx`=count), preserving the C caller per the routines' `pushad`/`popad`. **tramp.asm cdecl wrappers; validated bit-exact on pulse osc.**
- [x] 1.3 Initialize sample-rate/global state at 44100 Hz on both cores (asm `SRf*` globals via the synthInit/SR-setup path; C++ `V2Instance`), and ASSERT the resulting SR constants match between cores before any block test. **SR constants asserted equal (obasefrq/linfreq match exactly).**
- [x] 1.4 Validate the trampoline + setup on OSC alone against a hand-traced/known output, to confirm the ABI (accumulate vs overwrite, register roles) before trusting the suite. **OSC proof: pulse bit-exact, sin/noise ~1e-7 — ABI correct.**
- [x] 1.5 Add a per-shape diff/report helper (reuse Phase 1 `compare` logic): max abs error, first divergence index, MATCH/DIVERGE at `eps=1e-4`. **Per-sample diff (max err, first divergence) in comp_osc.**

## 2. Leaf block equivalence tests

- [x] 2.1 Oscillator (`syOscRender` vs `V2Osc::render`): each mode (tri/saw, pulse, sin, noise — seed identically), fixed `syVOsc` presets, N samples, diff mono buffer. **3/4 modes match; tri/saw diverges 1.36e-4 (documented C++ rewrite, line 586).**
- [x] 2.2 Envelope (`syEnvTick` vs `V2Env::tick`): drive gate on/off across attack/decay/sustain/release, diff the per-tick scalar sequence (full sequence, not endpoint). **MATCH (bit-exact). Note: asm/C++ env-state enums differ (asm ATTACK=1, C++=2); drove each by its own value.**
- [x] 2.3 Filter / VCF (`syFltRender` vs `V2Flt::render`): each filter type + representative cutoff/reso, deterministic input buffer (impulse + sweep), diff output. **5 std modes match (~1e-7); MOOG modes 6/7 DIVERGE 0.101 — primary culprit. C++ V2Flt::set moog coeffs (ryg's `@@@BUG?` comment).**
- [x] 2.4 LFO (`syLFO*`/tick vs `V2LFO::tick`): each waveform/rate, diff per-tick scalar sequence. **MATCH all 4 deterministic modes (saw/tri/pulse/sin). S&H uses rand() seed (skipped).**
- [x] 2.5 Distortion (`syDist*` vs `V2Dist::render`): each distortion mode, diff buffer. **modes 0/2 MATCH; modes 1 & 3 DIVERGE (0.13, 2.5) — real per-mode distortion port bugs.**
- [x] 2.6 DC filter (`syDCF*` vs `V2DCFilter`): diff buffer. **DC filter MATCH (~2e-7).**
- [x] 2.7 Bass boost (`syBoostRender` vs `V2Boost::render`): diff stereo buffer. **Bass boost MATCH (5.6e-6) after zeroing C++ state (V2Boost::init doesn't zero IIR state).**
- [ ] 2.8 Mod delay (`syModDel*` vs `V2ModDel::render`): identical delay buffer size/zeroing on both sides, diff stereo buffer. **N/A as leaf — V2ModDel is channel-level, needs external delay buffers + instance. Fallback testing required.**
- [ ] 2.9 Compressor (`syCompRender` vs `V2Comp::render`): diff stereo buffer. **N/A as leaf — syCompRender reads SYN.vcebuf (lookahead). Instance-entangled; fallback required.**
- [ ] 2.10 Reverb (`syReverb*` vs `V2Reverb::render`): identical buffers + seed, diff stereo buffer. **N/A as leaf — syReverbProcess reads SYN.aux1buf. Instance-entangled; fallback required.**

## 3. Aggregate block tests — DEFERRED (user chose leaves-only)

- [ ] 3.1 Voice (`syV2Render` vs `V2Voice::render`): single note, minimal patch, diff stereo buffer; attribute any divergence to a failing leaf from group 2. **DEFERRED — user scoped this run to leaf blocks.**
- [ ] 3.2 Channel (`syChan*` vs `V2Chan`): one voice + neutral FX, diff stereo buffer; corroborate against voice/leaf results. **DEFERRED — user scoped this run to leaf blocks.**

## 4. Localization report

- [x] 4.1 Run the full suite and produce an ordered pass/fail table (leaves first, then aggregates) with per-block max abs error and first divergence. **Ordered table in REPORT_LOCALIZE.md (leaves; aggregates noted N/A).**
- [x] 4.2 Identify the first diverging leaf block and extract the minimal reproducing input (smallest param/input that still shows the difference). **Primary = Moog filter (VCF 6/7); minimal repro = moog mode + noise input. Also dist 1/3, tri/saw.**
- [x] 4.3 Handle the all-leaves-match case: if every block matches but the Phase 1 whole-song A/B still diverges, flag composition/routing (voice/channel mixing, FX ordering) as the remaining suspect. **N/A — leaves DO diverge (moog/dist/tri-saw), so composition is not the sole suspect.**
- [x] 4.4 Write the report: first diverging block + minimal repro as the concrete fix target for the follow-on port-correctness change; note any blocks that could only be tested via fallback (minimal-patch-through-seam) isolation. **validate/REPORT_LOCALIZE.md; fix priority: moog coeffs > dist 1/3 > tri/saw. COMP/REVERB/MODDEL need fallback.**
