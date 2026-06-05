// v2eras -- THE behavior-delta ledger of the portable V2 player.
//
// Every known engine-behavior difference between the year-2000 synth (v2m
// format v0, fr08) and the 2004 synth (format v6, synth.asm) is one row
// here: at which format version the OLD behavior flipped to the NEW one, and
// how we know. Engine code never compares versions directly; it asks
// oldBehavior(DELTA_X, version).
//
// Evidence levels:
//   PROVEN   -- verified against a period binary (currently: both endpoints;
//               the fr08 reconstruction proved the v0 side of every row, the
//               5-song 2004-asm A/B proved the v6 side).
//   ANCHORED -- the parameter version tables (sounddef.h) imply the feature's
//               introduction version (params for it appear there).
//   ASSUMED  -- documented guess for the v1..v5 gap. Default policy: flip at
//               v1 unless coupled to an anchored row. When a mid-era binary
//               is analyzed (the follow-up research track), these become data
//               edits here -- never engine redesigns.
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
  // --- feature-introduction deltas (anchored by the param tables) ----------
  DELTA_NO_COMP_BOOST,     // channel dcf1/comp/boost/dcf2 + global sum comp
                           // absent (no code in the v0 binary)
  DELTA_NO_AUX_BUSSES,     // AuxA/B busses absent (default-handled: rcv/send
                           // gains canonicalize to 0; lab port verified
                           // bit-exact without an explicit gate)
  DELTA_NO_RVB_LOWCUT,     // reverb low-cut stage absent (default-handled:
                           // lowcut=0 makes the 2004 stage an exact no-op)

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
//    (srcVersion < 2) encoded the same judgment.
//  - flipsAt 1 + ANCHORED on comp/boost: their parameters appear at format
//    v1 (sounddef.h: +10 patch/+9 global params), and the v0 binary has no
//    code for them.
//  - flipsAt 4 + ANCHORED on reverb low-cut: its global param appears at v4.
//  - flipsAt 6 + ANCHORED on aux busses: their params appear at v6.
//  - flipsAt 1 + ASSUMED on everything else: only the endpoints are proven;
//    v1 is the conservative default ("the 2004 behavior existed by v1 unless
//    evidence says otherwise"). Override with Player::open(...,
//    forceBehaviorVersion) when researching.
inline constexpr V2DeltaRow kDeltas[DELTA_COUNT] = {
  /* DELTA_ENV_CURVES         */ { 2, EV_ANCHORED },
  /* DELTA_FRAME256           */ { 2, EV_ANCHORED },
  /* DELTA_ENV_CLAMP_SUSREL   */ { 1, EV_ASSUMED  },
  /* DELTA_OSC_BOXFILTER      */ { 1, EV_ASSUMED  },
  /* DELTA_NOISE_LCG_MSVC     */ { 1, EV_ASSUMED  },
  /* DELTA_NATIVE_FSIN        */ { 1, EV_ASSUMED  },
  /* DELTA_NATIVE_FPATAN      */ { 1, EV_ASSUMED  },
  /* DELTA_OSC_FREQ_CONST     */ { 1, EV_ASSUMED  },
  /* DELTA_NO_DCOFFSET        */ { 1, EV_ASSUMED  },
  /* DELTA_CRUSHER_SPLIT_GAIN1*/ { 1, EV_ASSUMED  },
  /* DELTA_PGMCHANGE_V0       */ { 1, EV_ASSUMED  },
  /* DELTA_TICK_BEFORE_SET    */ { 1, EV_ASSUMED  },
  /* DELTA_SUBFRAME_RENDER    */ { 1, EV_ASSUMED  },
  /* DELTA_NO_VOICE_DCF       */ { 1, EV_ASSUMED  },
  /* DELTA_NO_MASTER_DCF      */ { 1, EV_ASSUMED  },
  /* DELTA_NO_MOOG            */ { 1, EV_ASSUMED  },
  /* DELTA_NO_COMP_BOOST      */ { 1, EV_ANCHORED },
  /* DELTA_NO_AUX_BUSSES      */ { 6, EV_ANCHORED },
  /* DELTA_NO_RVB_LOWCUT      */ { 4, EV_ANCHORED },
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

#if V2_VER_MIN == 0 && V2_VER_MAX == 6
// ledger sanity for the default full-range build
static_assert(oldBehavior(DELTA_ENV_CURVES, 0), "v0 must get old env curves");
static_assert(oldBehavior(DELTA_ENV_CURVES, 1), "v1 assumed old env curves");
static_assert(!oldBehavior(DELTA_ENV_CURVES, 2), "v2+ gets modern env curves");
static_assert(oldBehavior(DELTA_NO_AUX_BUSSES, 5), "aux busses are v6");
static_assert(!oldBehavior(DELTA_NO_AUX_BUSSES, 6), "v6 has aux busses");
static_assert(!oldBehavior(DELTA_CRUSHER_SPLIT_GAIN1, 6), "v6 folds gain1");
static_assert(oldBehavior(DELTA_CRUSHER_SPLIT_GAIN1, 0), "v0 splits gain1");
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
