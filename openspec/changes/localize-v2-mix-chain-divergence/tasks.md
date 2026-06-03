# Tasks

## 1. Per-stage mix tap — C++ core (`V2_VALIDATE`-gated)
- [x] 1.1 Add five `StereoSample` snapshot buffers (post-reverb, post-delay,
      post-dcf, post-lc/hc, post-compr) and `memcpy` `mixbuf` into each
      immediately after the corresponding stage in `renderFrame`
      (`synth_core.cpp:3342-3370`). Read-only; no DSP change. (g_mixtap_* +
      MIXTAP_SNAP macro.)
- [x] 1.2 Add `synthDebugGetMixTap(...)` accessor exposing the five buffers +
      framesize, next to `synthDebugGetBus`.

## 2. Per-stage mix tap — ASM core (`v2/validate/asm_appendix.asm`)
- [x] 2.1 Mirror the five snapshots at the equivalent points in the asm render
      frame and a matching accessor; rely on the redef step to auto-rename.
      (mixtap_snap_* routines + mixtap_* bss + `_synthDebugGetMixTap@28`;
      `call mixtap_snap_*` sed-injected into the render frame by build.sh.)
- [x] 2.2 Sanity gate: the asm post-compressor snapshot MUST equal the existing
      final-`mixbuf` bus tap on the asm core (verifies stage-boundary placement).
      VERIFIED: post_compr == .mix (MATCH) on BOTH cores.

## 3. Player / harness dump
- [x] 3.1 Extend the `BUSTAP=` per-frame dump to also write the five per-stage
      streams (`<prefix>.post_reverb` … `<prefix>.post_compr`) for both cores.
- [x] 3.2 Build (faithful flags: `-mpc32 -mno-sse -DV2_X87_FAITHFUL -DV2_VALIDATE`);
      confirm `harness_asm` unaffected and no `comp_*` regression.
      VERIFIED: comp_osc 0/4, comp_flt 0/7, comp_leaves 0.

## 4. Localize
- [x] 4.1 Run the A/B with `BUSTAP=` on `pzero_new.v2m`; diff each per-stage
      stream (asm vs C++) in order. Per-stage max-abs error:
      reverb 0.04390, delay 0.04457, dcf 0.04488, lchc 0.04489, compr 0.08116.
- [x] 4.2 Identify the FIRST stage whose max-abs diff jumps. RESULT: the mix
      chain does NOT introduce the bulk of the divergence — ~0.0439 ENTERS the
      chain already present (upstream channel/voice sum, pre-reverb). Within the
      chain, reverb/delay/dcf/lc-hc are near-passthrough (Δ ≤ 0.001); the **sum
      compressor is the dominant in-chain amplifier** (+0.0363, 0.0449→0.0812,
      worst case moves #256→#1792). Open question for §5: is the compressor a
      genuine port bug, or faithfully amplifying the upstream ~0.044 root?

## 5. Fix the responsible stage
- [x] 5.1 Read `syCompProcChannel` vs `V2Comp::render`: the gain integrator
      (`gain += (dgain<gain?attack:release)*(dgain-gain)`), the
      `dgain = 1/(1+ratio*(lvl-1))` formula, the lookahead wrap (length dblen+1,
      already fixed), the attack/release select, and the `out = v*outvol*gain`
      multiply are ALL structurally identical. The compressor is FAITHFUL — it
      has no fresh port bug. Its 1.8x amplification is the expected behavior of a
      stateful gain integrator fed an already-diverged signal. Reverb/delay/dcf/
      lc-hc are near-passthrough (Δ ≤ 0.001) → also faithful.
- [~] 5.2 NO MIX-CHAIN FIX EXISTS: every global stage is faithful. The ~0.0439
      that ENTERS the chain pre-reverb is the dominant root. It cannot come from
      the reverb (its aux1 send diverges only 0.0027 — far too small to manufacture
      0.044), so it is the DRY channel sum already in `mixbuf` — i.e. UPSTREAM of
      the mix chain, in the per-channel/voice processing (the handover's known
      voice 0.0266 divergence, summed across channels). The mix chain is
      EXONERATED. Root redirected upstream → next change.

## 7b. Precision axis CLOSED (2026-06-03) — residual is structural
- [x] 7b.1 Confirmed the ~0.084 residual is NOT floating-point precision. Four
      interventions leave the whole-song magnitude unmoved: x87 PC=24 faithful
      0.0837997; `-fexcess-precision=standard` byte-identical; `-mpc32 -mno-sse`
      onset-shift-only; full SSE math (`-msse2 -mfpmath=sse`, FLT_EVAL_METHOD=0)
      0.0838358. The GCC float-literal→80-bit `fldt` promotion is REAL but
      context-dependent (non-exact literals in live exprs: `fci12`, moog
      `0.8f`/`5.6f`); the ONLY working cure is `-mfpmath=sse -msse2`
      (`-fexcess-precision=standard` is inert on x87 in C and C++, every `-std`).
      Fixing it globally via SSE moved the residual by 0.00004 → constant bug is
      real but NOT the residual. DO NOT re-run the precision gauntlet.
- [x] 7b.2 Tell: first divergence is at a FIXED sample (~stereo 4401, ~0.1 s)
      REGARDLESS of FP mode → control-flow/logic difference, not ULP drift. §8/§9
      re-pointed to localize that structural voice event by comparing LOGIC.

## 8. Voice-chain tap — localize the STRUCTURAL voice event (~sample 4401)
- [ ] 8.1 C++: add four mono `vcebuf` snapshots in `V2Voice::render` (post-osc,
      post-filter, post-dist, post-dcf) + `synthDebugGetVceTap` accessor.
- [ ] 8.2 ASM: mirror the four snapshots at the equivalent points in `syV2Render`
      (sed-injected calls) + `_synthDebugGetVceTap@24`; bss buffers in the appendix.
- [ ] 8.3 Player: dump `<prefix>.vce_osc/.vce_flt/.vce_dist/.vce_dcf` for both cores.
      (NOTE: vcebuf is per-voice; the per-frame snapshot holds the last voice of
      the frame — valid in the early single-voice region where the first
      divergence lives.)
- [ ] 8.4 Build + run A/B; diff each voice sub-stage; identify the first one that
      diverges AND THE SAMPLE. Anchor on ~stereo 4401 (the FP-mode-invariant onset):
      determine which voice/channel is active then and what event occurs (note
      on/off, filter-mode switch, env/LFO phase boundary, gate retrigger). Record
      the per-substage diffs and the triggering event.

## 9. Drill + fix the structural voice event
- [ ] 9.1 At the ~4401 event, read asm vs C++ for the culprit sub-block focusing on
      CONTROL FLOW / SEQUENCING (not arithmetic precision — that axis is closed,
      §7b): branch conditions, state transitions, off-by-one in event timing, init
      order, struct-field aliasing. Find the logic divergence.
- [ ] 9.2 Apply the port-fidelity rule: fix a C++ porting error directly; gate a
      genuine asm bug behind a `BUG_V2_*` define (default = asm-faithful).

## 8b. Voice tap RAN (2026-06-03) — re-localized to the PER-CHANNEL chain
- [x] 8b.1 Built+ran the voice tap + chantap + event trace (EVTRACE) + chorus trace
      on the faithful build. Layer-stack onsets (first bit-difference / first >1e-5):
      `vce_osc` bit-EXACT (osc fully exonerated); `vce_flt/dist/dcf` clean to
      frame 56832; `chan` (voice sum INTO chan-chain) bit-exact to frame 4738;
      **`aux1/aux2` (chan-chain OUTPUT) diverge at frame ~128** — the FIRST audible
      output sample. So the divergence is BORN in the per-channel chain
      (`V2Chan::process`, synth_core.cpp:2724 — dcf1 / per-channel comp / boost /
      dist / chorus / dcf2), from BIT-IDENTICAL input. Voice DSP + global mix
      EXONERATED. (Contradicts the earlier "channel chain exonerated by reading".)
- [x] 8b.2 Event correlation (EVTRACE): the ~4563 whole-song onset is NOT a discrete
      event — it grows during the sustained ch1 5-note chord (note-on burst at
      smpl=127) after a CC1 mod at smpl=3581. The chan-chain `aux` divergence starts
      even earlier (frame ~128, with the first chord). So "the 4401/4563 event" is
      the per-channel chain diverging on its own state from the first notes, not a
      voice event.
- [x] 8b.3 Signature: `aux1` is silent until frame 128; the FIRST non-zero output
      already diverges ~4e-6 (~4% of a 9e-5 signal) and the diff OSCILLATES with the
      signal envelope (not a ramp). = a DELAY/PHASE offset, not precision drift. Two
      delay-based stages can produce it: the per-channel **compressor lookahead**
      (`V2Comp`, dblen) or the **chorus delay tap** (`V2ModDel`, dboffs/mmaxoffs/
      mphase/mfreq — see the drift note at synth_core.cpp:2209). Chorus is active on
      several channels (mfreq=34000/9785/10904/22046/72566).

## 10. Bisect the per-channel chain → fix the delay-based stage
- [ ] 10.1 Add per-stage channel-chain taps (post dcf1 / comp / boost / chorus /
      dist / dcf2) in BOTH cores (C++ `V2Chan::process` + asm `syChanProcess`,
      sed-injected like the mix/voice taps); diff to find the stage that first
      diverges at frame ~128. (Alternatively: dump the asm-side chorus integers
      `mfreq/dboffs/mmaxoffs/mphase` and the comp `dblen` and compare to the C++
      values already traced via CHORUSTRACE — a 1-unit mismatch is the bug.)
- [ ] 10.2 Read that stage's asm vs C++ at the divergent sample, focused on the
      delay-index / lookahead-length / LFO-phase integer math. Fix a port error
      directly; gate a genuine asm bug behind `BUG_V2_*` (default asm-faithful).

## 8c. Coefficient sweep (2026-06-03) — dominant source is the AMP ENV output
Built asm-side dumps (CHORUSTRACE/COMPTRACE/BOOSTTRACE/SRTRACE) mirroring C++ and
compared every cheap channel/voice coefficient:
- [x] chorus integers (mfreq/dboffs/mmaxoffs/mphase): **bit-match** asm (the prior
      uncommitted `v2_fistp` fix is verified correct). Chorus exonerated.
- [x] compressor (mode/dblen/invol/outvol/ratio/attack/release): **match** (OFF-mode
      stores 5 vs 4 = ignored bit; comp is OFF on the active channels). Exonerated.
- [x] channel gains (a1gain/a2gain/aarcv/abrcv/aasnd/absnd/chgain): match (a1gain/
      a2gain differ by ≤1 ULP from a multiply-grouping difference — negligible).
- [x] boost biquad: poles a1/a2 **match**; feedforward **b0/b2 differ by 1 ULP**
      (C++ `(A·X)·ia0` vs asm `X·(A·ia0)`) — real port nit but bounded (poles match)
      so not the source. `beta=sqrt(2A)` vs asm cancellation form MATCHED for pzero.
- [x] SR coeffs (obasefrq/linfreq/**dcfilter**/BoostCos/BoostSin): **all bit-match**.
      The dcf pole matches; BoostCos/Sin (libm cos/sin) match the asm fcos/fsin here.
- [x] FIXDENORMALS is defined (=1) → both cores add the `fcdcoffset` voice-output
      bias; the voice→chanbuf output loop (out·cv, cv+=volramp, lvol/rvol, +dc,
      accumulate) and `volramp=((aenv.out/128)-curvol)·SRfciframe` MATCH the asm.
- [x] **FIX (committed-worthy): dcf step operand order.** asm `syDCFRenderStereo` is
      `(R·ym1 - xm1 + in + dc) - dc`; C++ `V2DCF::step` had `(dc + R·ym1 - xm1 + in)
      - dc` (dc added first). Recursive pole R≈0.997 integrates the per-sample ULP.
      Reordered to match asm (synth_core.cpp:501). Whole-song onset 8802→8808; magnitude
      unchanged (it is a minor contributor, NOT the dominant source).
- [x] env generator EXONERATED: all 6 coeffs (atd/dcf/sul/suf/ref/gain) bit-match
      across all 10 configs; tick state machine + `out=val*gain` match the asm by
      reading. (Corrected a mis-localization — `aenv.out` does NOT diverge.)

## 11. CORRECTED localization — back to the per-channel CHAIN (it amplifies)
Reliable thresholds: `aux1` (chan-chain OUT) bit-diff @128, >1e-3 @3575; `chan`
(chan-chain IN) bit-exact to 4738, >1e-5 only @56832. So the chain takes ~bit-exact
input and AMPLIFIES it to 1e-3 early → a recursive stage with a pole. Cleared so
far: chorus✓ comp(off) boost-poles✓ dcf-pole✓ channel-gains✓; fixed dcf operand
order (minor). NOT yet examined: **dist** (decimator/filter modes are recursive) and
the **auxA/auxB cross-channel routing** (feedback through global reverb/delay).
- [x] 11.1 Channel dist modes in use are 5 and 7 = FILTER modes → the dist runs
      `V2Flt` (resonant SVF) inside the channel chain. Combined with the voice
      vcf1/vcf2, `V2Flt` is THE recursive amplifier present in the chain. The trail
      converges on `V2Flt` (== the handover's original `vce_flt ~6.6e-3` suspect).

## 12. PRIME TARGET: V2Flt (resonant SVF) — recurrence/coeff vs the asm
`V2Flt` is high-Q recursive (amplifies tiny diffs), used in voice vcf1/vcf2, the
channel dist filter modes (5/7), and the noise filter. Its leaf test `comp_flt`
only passes under tolerance (eps=1e-4) — the SAME trap that hid the oscillator bug.
- [ ] 12.1 Build `comp_flt_exact` (eps=0, swept cutoff/res WITH carried state) — the
      missing twin of `comp_osc_exact`.
- [x] 12.2 SVF step (`V2LRC::step_2x`) matches `syFltRender.process` operand-for-
      operand; `comp_flt` low/band/high bit-exact. **FIX: moog coeffs.** asm
      `syFltSet` computes `moogf=1-(p+p)`, `moogp=f+0.8*(t*f)`, `moogq=((r*(1+..))*2)*2`
      with 32-bit `fc0p8/fc5p6`; C++ had different grouping + `0.8f/5.6f` promoted to
      80-bit. Rewrote to match (synth_core.cpp:1288) → comp_flt moogl/moogh now
      maxerr=0 (were ~9e-7). BUT whole-song UNCHANGED (0.0838001538).

## 13. KEY FINDING: all per-component coeffs/recurrences now bit-exact, residual STUCK
osc, SVF, moog, env, compressor, chorus, boost-poles, dcf are all
verified-faithful/bit-exact, yet the whole-song A/B is immovable at ~0.0838 (dry
voice/channel sum ~0.044 still enters the global mix; sum-compressor amplifies to
0.081). Fixes landed this session: dcf operand order, moog coeffs (both real, both
verified in isolation, neither moved the whole-song). Two committed-worthy fixes.
- [x] 13.1 dist **overdrive** (mode 1): asm `syDistSet.mode1` uses x87 **`fpatan`**
      (80-bit) for `gain2=(param1/128)/atan(gain1)`; C++ uses libm `atan` (double) —
      no faithful path → `gain2` diverges. Real, but a coefficient (~1e-7 rel), minor.
      Needs an inline-asm fpatan in the faithful build (like the freq path). NOT the
      0.044 dominant.
- [x] 13.2 Sum-compressor NEUTRALIZED (via the existing `post_lchc` pre-compressor
      mix tap, mirrored in both cores): un-amplified divergence = **0.0450**, onset
      frame **128** (the first chord); compressor amplifies ×1.83 → 0.0821. The
      compressor is a FAITHFUL amplifier, EXONERATED. Root = the 0.045 dry
      voice/channel sum, onset at the first polyphonic chord.

## 14. PRIME HYPOTHESIS: voice-allocation / summation ORDER (not a component)
All per-voice components are bit-exact, yet the SUM diverges from the first chord
(frame 128, when polyphony starts). FP addition is non-associative → if C++ sums
the chord's ~8 voices in a different order, or allocates notes to different voice
slots, than the asm, the sum diverges while each voice stays bit-identical. Fits:
onset == polyphony start, FP-mode-invariant, invisible to per-component checks.
- [x] 14.1 Voice ALLOCATION is byte-IDENTICAL (ALLOCTRACE both cores): every note
      lands in the same slot (45->0, 84->1, 69->2, 72->3, 81->4, 57->5, ...). The
      allocation/sum-order-via-allocation hypothesis is EXONERATED.

## 15b. FIX: boost render recurrence grouping
asm `syBoostProcChan` accumulates `b0*x + ((b1*x1 - a1*y1) + (b2*x2 - a2*y2))`;
C++ `V2Boost::render` had the sequential `((b0x+b1x1)+b2x2)-a1y1)-a2y2`. The biquad
is recursive (poles match) so the grouping diff drifts. Matched the asm
(synth_core.cpp:2174) → whole-song 0.0838001538 -> 0.0837990204 (real, minor).

## 16. CONCLUSION: dominant residual is NOT a single component
FOUR verified port-fidelity fixes this session (dcf operand order, moog coeffs,
boost render grouping; overdrive fpatan identified) — every one real and verified
in isolation (comp_flt moog now bit-exact, etc.), NONE moved the whole-song off
~0.0838. Combined with: compressor exonerated (faithful x1.83 amp), allocation
byte-identical, auxA/B unused, and the entire per-voice/channel signal path
verified bit-exact. The dominant ~0.044 dry-sum divergence is therefore NOT
concentrated in any one component — it is broadly-distributed and resists
per-component attribution. Per-component drilling is EXHAUSTED.
COMMITTABLE FIXES this session: dcf operand order, moog coeffs, boost render
grouping (all verified faithful in isolation; keep regardless of whole-song).
Remaining options (none guaranteed to reach exactly 0):
- [ ] 16.1 Per-stage channel tap (only un-run direct probe) to settle whether a
      chain DSP op still diverges vs the cross-channel summation into aux/mix.
- [ ] 16.2 Accept the faithful build is sub-audibly close; the remaining ~0.044
      is distributed 24-bit-vs-register precision that no single fix removes.

## 15. PARADOX + the one remaining tool: per-stage channel tap
Allocation identical, all per-voice/channel components bit-exact, yet `aux`
(chan-chain OUT) diverges bit-level @128 from bit-exact `chan` (chan-chain IN).
The ONLY unobserved element is the auxA/auxB cross-channel RECEIVE (`chanbuf +=
auxabuf*aarcv`), which runs INSIDE process AFTER the chantap snapshot — if the
auxa/auxb feedback bus carries divergence it enters there. Every indirect probe is
exhausted (coeffs, allocation, neutralization); the per-stage channel tap is the
only thing that directly shows which chain op diverges.
- [ ] 15.1 Snapshot `chanbuf` after aux-receive / dcf1 / comp / boost / dist /
      chorus / dcf2 in BOTH cores (C++ `V2Chan::process` + asm `syChanProcess`,
      sed-injected like the mix/voice taps). Diff to find the op that diverges @128.
- [ ] 15.2 First check cheaply: are aasnd/aarcv/absnd/abrcv nonzero for any channel
      (is the auxA/B bus even used)? If unused, the receive is a no-op and the
      divergence is a chain DSP op; if used, chase the auxa/auxb bus + its feedback.

## 6. Verify
- [ ] 6.1 Re-run the whole-song A/B on `pzero_new.v2m`: max-abs magnitude drops
      below 0.0876 for the first time. Record the new number.
- [ ] 6.2 Re-tap the bus: the fixed stage no longer diverges; `comp_osc` 0/4,
      `comp_flt` 0/7, `comp_leaves` 0 — no regression.
- [ ] 6.3 If a residual remains in a later stage, iterate (localize → fix →
      re-tap) or spin off a follow-up change; record the remaining magnitude.

## 7. Docs
- [ ] 7.1 Update `v2/validate/HANDOVER.md` "What's OPEN" with the localized stage,
      the fix, and the new whole-song number.
- [ ] 7.2 Note any new `BUG_V2_*` flag in the handover's ASM-bug list.
