# Tasks

## 1. Faithful x87 transcendental helpers (gated)
- [x] 1.1 Add `#ifdef V2_X87_FAITHFUL` block with inline-asm `v2_pow2`,
      `v2_calcfreq`, `v2_calcfreq2` replicating the asm kernel (synth.asm:355-408)
- [x] 1.2 Add inline-asm `v2_fistp(sF32)->sInt` (round-to-nearest store)
- [x] 1.3 Sanity-check each helper against the asm constants
      (`fc10=10`, `fccfframe=11`) and ranges

## 2. Oscillator freq path (`V2Osc::chgPitch`)
- [x] 2.1 Gate the faithful path: `v2_calcfreq` for `nffrq`, `v2_pow2`+`v2_fistp`
      for `freq`, with `* 0.083333333333f` (fci12) not `/12.0f`
- [x] 2.2 Keep the existing libm/`(sInt)` path under `#else`

## 3. Tri/saw box-filter revert (gated)
- [x] 3.1 Under `V2_X87_FAITHFUL`, compute the hard-case box filter in `float`;
      keep `double` in the non-faithful build

## 4. Validation build flags (`v2/validate/build.sh`)
- [x] 4.1 Add `-mpc32 -mno-sse -DV2_X87_FAITHFUL` to cpp core, player, harness,
      and component oracle compiles
- [x] 4.2 Add `-mpc32` to the link lines (pulls `crtprec32.o`)
- [x] 4.3 Confirm `harness_asm` build is unaffected (asm core, no flags needed)

## 5. Verify
- [x] 5.1 `comp_osc` 0/4, `comp_flt` 0/7, `comp_leaves` 0 — all bit-match, no
      regression. 0 `powf`/`pow` calls left in the faithful object.
- [x] 5.2 Whole-song A/B on `pzero_new.v2m`: 0.0876298994 (was 0.0876300689).
      The faithful freq + all-transcendentals path moves only the 7th significant
      figure — NOT the magnitude.
- [x] 5.3 Bus-tap re-localize (faithful build): aux1=0.00267, aux2=0.00258,
      **mix=0.08116**. The dominant divergence is introduced in the GLOBAL mix
      chain (reverb / delay / dcf / lowcut-highcut / sum-compressor on `mixbuf`,
      synth_core.cpp:3321-3349), NOT in the voice/osc/transcendentals.
- [x] 5.4 NEGATIVE RESULT: converting ALL transcendentals (calcfreq/calcfreq2,
      every `powf(2,x)`, reverb `base^e`) to faithful x87 did not move the
      whole-song magnitude. The residual is STRUCTURAL in the global mix chain —
      a separate problem (the handover's "instance-entangled compressor/reverb/
      mod-delay"). Spin off as its own change.

## 7. Conclusion / handoff
- [x] 7.1 This change is complete at its scope: the bit-faithful build's
      transcendentals + osc frequency + tri/saw box filter all match the asm
      (component-verified). It is necessary infrastructure for whole-song zero
      but not sufficient.
- [x] 7.2 NEXT CHANGE: localize the global-mix-chain divergence (tap between
      reverb/delay/dcf/lc-hc/compr stages on `mixbuf`). Documented as the next
      step in `v2/validate/HANDOVER.md` ("What's OPEN" status update).

## 6. Docs
- [x] 6.1 Update `v2/validate/HANDOVER.md` "Current results" with the new number
      (0.0876298994; faithful freq path + bus-tap re-localization to mix=0.08116).
- [x] 6.2 Note the faithful-vs-portable split decision in the handover/spec
      (HANDOVER.md "What's OPEN"; delta spec "Bit-faithful validation build" req).
