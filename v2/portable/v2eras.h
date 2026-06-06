// v2eras -- THE behavior-delta ledger of the portable V2 player.
//
// Every known engine-behavior difference between the year-2000 synth (v2m
// format v0, fr08) and the 2004 synth (format v6, synth.asm) is one row
// here: at which format version the OLD behavior flipped to the NEW one, and
// how we know. Engine code never compares versions directly; it asks
// oldBehavior(DELTA_X, version).
//
// Evidence levels:
//   PROVEN   -- verified against a period binary. Three anchors now: the fr08
//               reconstruction proved the v0 side of every row, the 5-song
//               2004-asm A/B proved the v6 side, and the fr-030 candytron
//               final binary (2003-08, kkrunchy-unpacked + the byte-identical
//               genthree/_viruz2a.asm == RG2/ViruzII == RG2/Viewer sources)
//               proved the v5 state of every row (task 6.0e assay,
//               v2m/candytron-extraction/NOTES.md).
//   ANCHORED -- the parameter version tables (sounddef.h) imply the feature's
//               introduction version (params for it appear there).
//   ASSUMED  -- documented guess for the remaining v1..v4 gap. Default
//               policy: flip at v1 unless coupled to an anchored row. When a
//               mid-era binary is analyzed (the follow-up research track),
//               these become data edits here -- never engine redesigns.
//
// The full v0-side characterization lives in v2m/fr08-extraction/DELTA.md;
// row comments cite it. THIS FILE is the living threshold ledger.
//
// NOTE some rows need no engine gate because the loader's canonical defaults
// already make the modern code an exact no-op for old files (marked
// "default-handled"). They are still listed: the ledger must be complete, and
// future evidence may prove a default NOT exactly null on some path.

#ifndef V2ERAS_H_
#define V2ERAS_H_

#include "v2portable.h" // V2_VER_MIN / V2_VER_MAX

namespace v2portable {

enum V2Delta {
  // --- engine-structure deltas (all proven at the endpoints) ---------------
  DELTA_ENV_CURVES = 0,    // env attackmul -11/128 + dec/rel via calcfreq x10
                           // (calcfreq2/x11 did not exist in 2000)
  DELTA_FRAME256,          // control frame 256 samples (2004: 128); implies
                           // volramp coeff 1/256 and env/LFO tick rate; LFO
                           // freq without the modern 0.5x compensation
  DELTA_ENV_CLAMP_SUSREL,  // the val<=2^-13 ->OFF clamp only in SUSTAIN/
                           // RELEASE (2004 added the DECAY runout check);
                           // -> v0 envs decay into subnormals while gated
  DELTA_OSC_BOXFILTER,     // tri/saw/pulse = 4x-oversampled numeric box
                           // filter (2004: analytic OSM convolution)
  DELTA_NOISE_LCG_MSVC,    // noise LCG x214013+2531011 (2004: x196314165+
                           // 907633515) + the 2000 16-bit float recurrence
  DELTA_NATIVE_FSIN,       // osc/LFO sine = native fsin (2004: fastsin poly)
  DELTA_NATIVE_FPATAN,     // overdrive waveshaper = native atan (2004:
                           // fastatan rational poly)
  DELTA_OSC_FREQ_CONST,    // osc base freq = baked 3185015.0 (2004: computed
                           // from fcoscbase at runtime; SR-flexible)
  DELTA_NO_DCOFFSET,       // fcdcoffset (2^-18 denormal bias) absent
                           // everywhere (SVF, voice mix, chorus in, delay/
                           // reverb in)
  DELTA_CRUSHER_SPLIT_GAIN1,// bitcrusher renders (in*gain1)*(32768/x) as two
                           // muls; 2004 folds gain1 into crush1 at set
  DELTA_PGMCHANGE_V0,      // PGM change: no same-program early-out + resets
                           // ctl7 (chan volume) to 127
  DELTA_TICK_BEFORE_SET,   // frame control order TICK then SET: sub-objects
                           // step with the PREVIOUS frame's params
  DELTA_SUBFRAME_RENDER,   // render in sub-frame chunks with trailing-edge
                           // control tick (mid-frame note-on voice/chorus
                           // phase + control-edge facets)
  DELTA_NO_VOICE_DCF,      // per-voice DC filter absent (voice = osc->flt->
                           // dist->volramp only)
  DELTA_NO_MASTER_DCF,     // master DC filter absent (mix -> lc/hc EQ direct)
  DELTA_NO_MOOG,           // VCF modes 6/7 (MoogL/H) alias to passthrough
  DELTA_KEYSYNC_OSC_ONLY,  // noteOn keysync has no full-resync path: ANY
                           // keysync != 0 only zeros the osc phase counters
                           // (SYNC_FULL collapses to SYNC_OSC; noise/LFO/
                           // filter state RESUMES on re-trigger)
  DELTA_RVB_E_FULLPREC,    // reverb set: e = sqr(64/(revtime+1)) with the
                           // quotient kept full-precision through the square
                           // (the 2000 set path ran at x87 PC=64) and no
                           // SRfclinfreq factor (2004 SR-flexibility, =1.0
                           // at 44100)
  // --- feature-introduction deltas (anchored by the param tables) ----------
  DELTA_NO_COMP_BOOST,     // channel dcf1/comp/boost/dcf2 + global sum comp
                           // absent (no code in the v0 binary)
  DELTA_NO_AUX_BUSSES,     // AuxA/B busses absent (default-handled: rcv/send
                           // gains canonicalize to 0; lab port verified
                           // bit-exact without an explicit gate)
  DELTA_NO_RVB_LOWCUT,     // reverb low-cut stage absent (ENGINE-GATED, not
                           // default-handled: the canonical global defaults
                           // give pre-v4 files a NONZERO lowcut param, so the
                           // 2004 hpf stage would be a real extra filter --
                           // proven by fr08's reverb tail, DELTA.md)
  DELTA_CC6_HICUT,         // "FAKE 2: Lowcut!" -- a ch15(speech)-only control
                           // hack: MIDI CC6 on channel 15 ALSO sets the master
                           // high-cut freq hcfreq = sqr((val+1)/128), on top of
                           // storing the controller. PROVEN present at v0 (fr08
                           // ProcessControlChange @0x40bded) AND v5 (candytron/
                           // RG2 _viruz2a.asm ".FAKE 2 : Lowcut!"); REMOVED in
                           // 2004 (synth.asm just stores the CC). Drives the
                           // whole-mix master EQ in josie (ch15 sweeps CC6).
  DELTA_RDTSC_SEED,        // osc-noise + LFO-S&H seed from rdtsc (== 0 under the
                           // deterministic pinned-rdtsc convention) -- NOT the
                           // 2004 fixed osc-seed table {0xdeadbeef,0xbaadf00d,
                           // 0xd3adc0de} / libc-rand LFO sequence. PROVEN old at
                           // v0 (fr08 syOscInit/syLFOInit rdtsc @0x40a..) AND v5
                           // (candytron syOscInit @0x41dc99 + syLFOInit rdtsc);
                           // the fixed table is a v6 addition (oscseeds in
                           // synth.asm _OSC_). Decoupled from DELTA_NOISE_LCG_MSVC
                           // (which only picks the LCG constants): at v5 the LCG
                           // is modern but the SEED is still rdtsc->0.
  DELTA_NO_FM_OSC,         // osc mode 5 (FM sine) absent: the 2000 oscjtab
                           // (@0x40a565 in the fr08 image) maps modes 5/6/7
                           // to OFF -- no FM renderer exists in that binary.
                           // Present by v5 (candytron .mode4). Coupled to
                           // DELTA_NATIVE_FSIN for the implementation choice
                           // once present (see v2core renderFMSin_v5).
  DELTA_NO_CHAN_DCF,       // channel dcf1/dcf2 DC filters absent: the v5
                           // syChanProcess (genthree _viruz2a.asm == candytron
                           // binary) is comp -> boost -> dist/chorus -> sends,
                           // with NO DC filter stage; the dcf1 (pre-comp) and
                           // dcf2 (post-dist) one-poles are 2004 additions.
                           // Previously mis-bundled into DELTA_NO_COMP_BOOST
                           // (whose v1 param anchor only covers comp/boost).
                           // Root cause of the josie whole-mix DC-decay
                           // residual (every channel, 2nd sample of first
                           // note).

  DELTA_COUNT
};

enum V2Evidence : unsigned char {
  EV_PROVEN = 0,  // period-binary verified threshold
  EV_ANCHORED,    // parameter tables imply it
  EV_ASSUMED      // documented guess (v1..v5 gap)
};

struct V2DeltaRow {
  unsigned char flipsAt;   // OLD behavior applies to versions < flipsAt
  V2Evidence evidence;
};

// The ledger. Order must match the V2Delta enum.
//
// Threshold rationale:
//  - flipsAt 2 + ANCHORED on env/frame rows: kb's own transEnv conversion
//    (v2mconv.cpp:243, disabled) remaps envelopes exactly at the <v2 -> v2
//    boundary, and the frame-size halving is the structural root of that
//    remap (DELTA.md "control-frame size" section). The lab's eraEnvOld()
//    (srcVersion < 2) encoded the same judgment. The candytron binary (v5)
//    has the modern side of both (attackmul -0.09375 @0x41dbd4, frame 128)
//    -- consistent.
//  - flipsAt 1 + ANCHORED on comp/boost: their parameters appear at format
//    v1 (sounddef.h: +10 patch/+9 global params), and the v0 binary has no
//    code for them. Present in the v5 binary -- consistent.
//  - flipsAt 4 + ANCHORED on reverb low-cut: its global param appears at v4.
//    Present in the v5 binary (syReverbSet lowcut + hpf stage) -- consistent.
//  - flipsAt 6 + PROVEN (the 6.0e candytron assay): these rows still show
//    the OLD behavior in the v5 binary and the new one in the 2004 asm, so
//    the flip is pinned at exactly v6. Citations are genthree/_viruz2a.asm
//    (== the shipped binary, 6 signatures spot-verified in the unpacked
//    image) vs v2/synth.asm.
//  - flipsAt 1 + ASSUMED on the rest: the v5 binary already shows the NEW
//    behavior, so the flip lies in v1..v4; v1 stays the conservative
//    default ("existed by v1 unless evidence says otherwise"). Override
//    with Player::open(..., forceBehaviorVersion) when researching.
inline constexpr V2DeltaRow kDeltas[DELTA_COUNT] = {
  /* DELTA_ENV_CURVES         */ { 2, EV_ANCHORED },
  /* DELTA_FRAME256           */ { 2, EV_ANCHORED },
  /* DELTA_ENV_CLAMP_SUSREL   */ { 6, EV_PROVEN   }, // v5 state_dec: no LOWEST
                                                     // runout (binary @0x41e199)
  /* DELTA_OSC_BOXFILTER      */ { 1, EV_ASSUMED  }, // v5 = OSM (new)
  /* DELTA_NOISE_LCG_MSVC     */ { 1, EV_ASSUMED  }, // v5 = modern LCG+floatgen
  /* DELTA_NATIVE_FSIN        */ { 6, EV_PROVEN   }, // v5 osc/LFO/FM = native
                                                     // fsin (binary @0x41dfc3/
                                                     // 0x41e43b/0x41e094)
  /* DELTA_NATIVE_FPATAN      */ { 6, EV_PROVEN   }, // v5 overdrive render =
                                                     // native fpatan @0x41e60b
  /* DELTA_OSC_FREQ_CONST     */ { 1, EV_ASSUMED  }, // v5 = runtime fcoscbase
  /* DELTA_NO_DCOFFSET        */ { 6, EV_PROVEN   }, // no 2^-18 constant in the
                                                     // whole v5 image/source
  /* DELTA_CRUSHER_SPLIT_GAIN1*/ { 6, EV_PROVEN   }, // v5 render = two muls
                                                     // @0x41e665; set unfolded
  /* DELTA_PGMCHANGE_V0       */ { 1, EV_ASSUMED  }, // v5 == 2004 byte-for-byte
  /* DELTA_TICK_BEFORE_SET    */ { 1, EV_ASSUMED  }, // v5 = SET then TICK
  /* DELTA_SUBFRAME_RENDER    */ { 1, EV_ASSUMED  }, // v5 = full-frame render
  /* DELTA_NO_VOICE_DCF       */ { 6, EV_PROVEN   }, // no DCF code in v5 at all
  /* DELTA_NO_MASTER_DCF      */ { 6, EV_PROVEN   }, // v5 master: moddel -> EQ
                                                     // -> comp, no DCF stage
  /* DELTA_NO_MOOG            */ { 6, EV_PROVEN   }, // v5 syFRTab modes 6/7 =
                                                     // bypass; no moog dist
  /* DELTA_KEYSYNC_OSC_ONLY   */ { 6, EV_PROVEN   }, // v5 noteOn: oks!=0 only
                                                     // zeros osc counters, no
                                                     // HARDSYNC path
  /* DELTA_RVB_E_FULLPREC     */ { 1, EV_ASSUMED  }, // v5 = SRfclinfreq factor,
                                                     // set at PC=24
  /* DELTA_NO_COMP_BOOST      */ { 1, EV_ANCHORED },
  /* DELTA_NO_AUX_BUSSES      */ { 6, EV_PROVEN   }, // was ANCHORED; v5 osc
                                                     // jtab modes 6/7 = off, no
                                                     // aux bus code
  /* DELTA_NO_RVB_LOWCUT      */ { 4, EV_ANCHORED },
  /* DELTA_CC6_HICUT          */ { 6, EV_PROVEN   }, // v0 & v5 ch15-CC6 -> master
                                                     // hicut; 2004 dropped it
  /* DELTA_RDTSC_SEED         */ { 6, EV_PROVEN   }, // v0 & v5 rdtsc(->0); the
                                                     // fixed seed table is v6
  /* DELTA_NO_FM_OSC          */ { 1, EV_ASSUMED  }, // absent at v0 (PROVEN:
                                                     // oscjtab mode5 = off),
                                                     // present at v5; flip in
                                                     // v1..v4 unknown
  /* DELTA_NO_CHAN_DCF        */ { 6, EV_PROVEN   }, // v5 syChanProcess has no
                                                     // DC filters (source ==
                                                     // binary); dcf1/dcf2 are
                                                     // 2004/v6 additions
};

// Does the OLD (pre-flip) behavior apply at this behavior version?
//
// The V2_VER_MIN/MAX clamps are what make single-version builds fold every
// gate to a compile-time constant: with MIN==MAX the engine's
// behaviorVersion is itself a constexpr (see v2core), so this whole function
// evaluates at compile time and dead branches vanish.
constexpr bool oldBehavior(V2Delta d, int behaviorVersion)
{
  return (kDeltas[d].flipsAt <= V2_VER_MIN) ? false
       : (kDeltas[d].flipsAt > V2_VER_MAX)  ? true
       : behaviorVersion < (int)kDeltas[d].flipsAt;
}

// COUPLING: the 2000 oscillator block is one convention, not independent
// rows. The baked freq constant is ~SRfcobasefrq/4 BECAUSE the v0 renderers
// advance the phase counter 4x per output sample (tri/saw/pulse box loop,
// sine freq<<2). If these rows flipped at different versions, pitch would
// shift by two octaves in the gap. Future evidence must move them together
// (or decouple them with code, not just data).
//
// NOTE the sine EVALUATION is decoupled from this convention since the 6.0e
// assay: candytron (v5) evaluates the sine with native fsin (DELTA_NATIVE_FSIN
// old until v6) but advances 1x on the runtime freq. v2core's renderSin_v0
// therefore derives its step from DELTA_OSC_FREQ_CONST, not from the fsin row.
static_assert(kDeltas[DELTA_OSC_FREQ_CONST].flipsAt == kDeltas[DELTA_OSC_BOXFILTER].flipsAt,
              "osc freq-constant and the 4x-advance v0 renderers are one convention");

#if V2_VER_MIN == 0 && V2_VER_MAX == 6
// ledger sanity for the default full-range build
static_assert(oldBehavior(DELTA_ENV_CURVES, 0), "v0 must get old env curves");
static_assert(oldBehavior(DELTA_ENV_CURVES, 1), "v1 assumed old env curves");
static_assert(!oldBehavior(DELTA_ENV_CURVES, 2), "v2+ gets modern env curves");
static_assert(oldBehavior(DELTA_NO_AUX_BUSSES, 5), "aux busses are v6");
static_assert(!oldBehavior(DELTA_NO_AUX_BUSSES, 6), "v6 has aux busses");
static_assert(!oldBehavior(DELTA_CRUSHER_SPLIT_GAIN1, 6), "v6 folds gain1");
static_assert(oldBehavior(DELTA_CRUSHER_SPLIT_GAIN1, 0), "v0 splits gain1");
// candytron (v5) anchors from the 6.0e assay
static_assert(oldBehavior(DELTA_CRUSHER_SPLIT_GAIN1, 5), "v5 splits gain1");
static_assert(oldBehavior(DELTA_NATIVE_FSIN, 5), "v5 uses native fsin");
static_assert(!oldBehavior(DELTA_NATIVE_FSIN, 6), "v6 uses fastsin");
static_assert(oldBehavior(DELTA_NO_MOOG, 5), "v5 has no moog modes");
static_assert(oldBehavior(DELTA_NO_DCOFFSET, 5), "v5 has no dcoffset");
static_assert(!oldBehavior(DELTA_OSC_BOXFILTER, 5), "v5 osc is OSM already");
static_assert(oldBehavior(DELTA_NO_FM_OSC, 0), "v0 has no FM osc mode");
static_assert(!oldBehavior(DELTA_NO_FM_OSC, 5), "v5 has the FM osc mode");
#endif

// behavior version a Player resolves to: forced override (research knob) or
// the detected file version; rejects out-of-compiled-range requests.
// With V2_VER_MIN == V2_VER_MAX the only accepted value is that constant,
// which is what lets engine gates constant-fold (see v2core).
inline bool behaviorVersionValid(int v)
{
  return v >= V2_VER_MIN && v <= V2_VER_MAX;
}

} // namespace v2portable

#endif // V2ERAS_H_
