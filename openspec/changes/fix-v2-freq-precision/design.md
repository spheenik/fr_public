# Design: faithful oscillator frequency

## The asm reference (`syOscChgPitch`, synth.asm:488-502)

All x87, control word forced to PC=24 (24-bit single) / RC=00 (round-nearest) for
the whole render (`synth.asm:4868`). Constants are 32-bit floats:
`fc64=64.0`, `fci128=0.0078125` (=1/128 exact), `fci12=0.083333333333` (rounded
1/12), `fcOscPitchOffs=60.0`, `fc10=10.0`, `fccfframe=11.0`.

```
nffrq = SRfclinfreq * calcfreq( (pitch + 64) * (1/128) )
freq  = round_to_nearest( SRfcobasefrq * pow2( (pitch + note - 60) * fci12 ) )
```

`pow2(y)` and `calcfreq(x)` are the same x87 kernel (synth.asm:374-397):
```
; pow2(y):  2^y
fld1            ; 1 y
fld  st1        ; y 1 y
fprem           ; (y mod 1) 1 y        (truncating remainder; |frac|<1)
f2xm1           ; (2^frac - 1) 1 y
faddp st1,st0   ; 2^frac y
fscale          ; 2^frac * 2^trunc(y) = 2^y , y
fstp st1        ; 2^y
; calcfreq(x):  prescale then the same kernel
fld1 / fsubp / fmul [fc10]   ; y = (x-1)*10   ; then pow2 kernel  -> 2^((x-1)*10)
; calcfreq2: identical but fmul [fccfframe] (=11) instead of fc10
```

## The C++ port today (`V2Osc::chgPitch`, synth_core.cpp:551-555)

```cpp
nffrq = inst->SRfclinfreq * calcfreq((pitch + 64.0f) / 128.0f);
freq  = (sInt)(inst->SRfcobasefrq * pow(2.0f, (pitch + note - 60.0f) / 12.0f));
```
with `static calcfreq(x){ return powf(2.0f,(x-1.0f)*10.0f); }`.

Divergences from the asm:
| # | site | asm | C++ port | fix by |
|---|------|-----|----------|--------|
| 1 | `pow2`/`calcfreq` | x87 `f2xm1` kernel @ PC=24 | libm `pow`/`powf` (diff. polynomial, libm ignores CW) | inline-asm kernel |
| 2 | freq store | `fistp` (round-nearest) | `(sInt)` (truncate) | inline-asm `fistp` |
| 3 | `/12` | `fmul [fci12]` (mul by rounded 1/12) | `/12.0f` (divide) | mul by `0.083333333333f` |
| 4 | freq pow precision | single (PC=24) | `pow` is **double** | inline-asm kernel (single) |

(`/128` is exact in float, so divide==multiply there; no change needed.)

## Approach: gated x87 helpers

Add, under `#ifdef V2_X87_FAITHFUL` (x86-only, validation build), three inline-asm
helpers that replicate the asm exactly:

```cpp
static inline sF32 v2_pow2(sF32 y);        // f2xm1 kernel, PC=24 ambient
static inline sF32 v2_calcfreq(sF32 x);    // (x-1)*10 then kernel
static inline sF32 v2_calcfreq2(sF32 x);   // (x-1)*11 then kernel
static inline sInt v2_fistp(sF32 v);       // store-int at ambient RC (round-nearest)
```

Then `chgPitch` becomes:
```cpp
#ifdef V2_X87_FAITHFUL
  nffrq = inst->SRfclinfreq * v2_calcfreq((pitch + 64.0f) * 0.0078125f);
  freq  = v2_fistp(inst->SRfcobasefrq * v2_pow2((pitch + note - 60.0f) * 0.083333333333f));
#else
  nffrq = inst->SRfclinfreq * calcfreq((pitch + 64.0f) / 128.0f);
  freq  = (sInt)(inst->SRfcobasefrq * pow(2.0f, (pitch + note - 60.0f) / 12.0f));
#endif
```

The helpers run on x87; with the validation build's `-mpc32` the ambient CW is
PC=24 and the kernel's f2xm1/fscale match the asm bit-for-bit. `-mno-sse`
guarantees no intermediate detours through an 8-bit-exponent xmm register.

## Build flags (`v2/validate/build.sh`)

Add `-mpc32 -mno-sse -DV2_X87_FAITHFUL` to the cpp core, player, harness, and the
component oracles. `-mpc32` must also be on the **link** line (it pulls
`crtprec32.o` / `set_precision`, which sets the startup CW to 0x007F).

Caveat: `-mpc32` sets PC=24 globally at startup, whereas the asm sets it per
`_synthRender` and restores `oldfpcw` at exit. They agree for everything computed
inside render (where note-triggered `chgPitch` runs). If a measurable divergence
remains from out-of-render computation, switch to an explicit
`fstcw`/`fldcw(PC=24)` on render entry + restore on exit, mirroring the asm.

## Tri/saw revert

The committed "double box-filter" fix (`synth_core.cpp`, tri/saw hard cases) was
premised on the asm being 80-bit. It is 24-bit single. Under `V2_X87_FAITHFUL`,
compute the box filter in `float` (x87 PC=24 supplies the 24-bit mantissa + 15-bit
exponent the cancellation needs). Keep `double` only in the non-faithful build
(where SSE single's 8-bit exponent would otherwise overflow the cancellation).

## Verification plan

1. Build faithful: `comp_osc` must stay MATCH (0/4); `comp_leaves`/`comp_flt`
   unchanged.
2. Whole-song A/B on `pzero_new.v2m`: expect magnitude to drop below 0.0876 for
   the first time. Re-tap the bus (`BUSTAP=`) to confirm the osc no longer
   diverges at sample 0.
3. If a residual remains, bus-tap to localize the next source (likely the other
   `calcfreq`/`pow` sites: env dr/rr, lfo rate, filter cutoff, dist param1,
   reverb gains) and extend the gated helpers to those sites.

## Risks / open questions

- `-mpc32`'s global-vs-per-render scope (see caveat) — may need the explicit
  entry/exit CW dance for exactness.
- gcc may still constant-fold literal-only subexpressions at compile time
  (MPFR, not PC=24); freq inputs are runtime (pitch/note), so low risk.
- Whether matching the freq path alone zeroes the whole song, or whether the
  other `calcfreq`/`pow` sites must also be converted. Treated as iterative:
  measure after the freq fix, extend if needed.
