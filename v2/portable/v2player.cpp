// v2portable Player implementation: API facade over the native loader
// (v2load) + forked engine (v2core) + sequencer (v2seq).

#include "v2portable.h"
#include "v2eras.h"
#include "v2load.h"
#include "v2seq.h"

#include <string.h>
#include <stdlib.h>

namespace v2portable {

// ---------------------------------------------------------------------------
// FP environment self-check
// ---------------------------------------------------------------------------

// The v0 engine semantics depend on IEEE-correct subnormal arithmetic
// (gate-held DECAY envelopes decay multiplicatively into the subnormal range
// and keep modulating -- proven audible in the fr08 reconstruction). A host
// built/linked with fast-math style flags sets FTZ/DAZ and silently breaks
// that, so we detect it at runtime and fail loudly instead.
bool fpEnvironmentOk()
{
  // volatile defeats constant folding; the product is subnormal for binary32.
  volatile float a = 1.0e-30f;
  volatile float b = 1.0e-12f;
  volatile float sub = a * b;          // 1e-42, subnormal
  if (sub == 0.0f)
    return false;                      // FTZ: flushed on write
  volatile float back = sub * 1.0e12f; // DAZ: treated as zero on read
  return back != 0.0f;
}

// ---------------------------------------------------------------------------
// PlayerImpl
// ---------------------------------------------------------------------------

struct PlayerImpl {
  V2MPlayer seq;          // embeds the 3MB synth instance; heap via Player
  uint8_t *song = nullptr; // our copy of the (canonicalized) v2m data
  size_t songLen = 0;
  uint64_t seed = 0;
  int behaviorVersion = V2_VER_MAX;
  bool opened = false;

  ~PlayerImpl() { free(song); }
};

Player::Player() : impl_(new PlayerImpl), detectedVersion_(-1) {}
Player::~Player() { delete impl_; }

Result Player::open(const void *v2mData, size_t length, int forceBehaviorVersion)
{
  PlayerImpl *im = impl_;
  if (im->opened) { im->seq.Stop(); im->opened = false; }
  free(im->song); im->song = nullptr; im->songLen = 0;
  detectedVersion_ = -1;

  if (!fpEnvironmentOk())
    return Result::FpEnvBroken;
  if (!v2mData || length < 16)
    return Result::BadFile;

  // research override (era-gated-engine spec): force the behavior version
  // regardless of the detected format version; must lie in the compiled range
  if (forceBehaviorVersion >= 0 && !behaviorVersionValid(forceBehaviorVersion))
    return Result::UnsupportedVersion;

  // native loading: fingerprint the format version and canonicalize to the
  // v6 layout (v2load); the detected version drives the engine's era gates.
  V2LoadResult lr = v2loadCanonicalize(v2mData, length);
  detectedVersion_ = lr.version; // reported even on UnsupportedVersion
  if (lr.result != Result::OK)
    return lr.result;

  im->behaviorVersion = forceBehaviorVersion >= 0 ? forceBehaviorVersion
                                                  : detectedVersion_;

  // the loader's canonical copy is ours; V2MPlayer keeps pointers into it
  // for the song's lifetime
  im->song = lr.data;
  im->songLen = lr.size;

  im->seq.Init(1000);
  im->seq.SetSourceVersion(im->behaviorVersion);
  if (!im->seq.Open(im->song, 44100)) {
    free(im->song); im->song = nullptr;
    return Result::BadFile;
  }
  im->opened = true;
  return Result::OK;
}

int Player::fileVersion() const { return detectedVersion_; }

void Player::play(uint32_t fromMs)
{
  if (impl_->opened)
    impl_->seq.Play(fromMs);
}

bool Player::isPlaying()
const {
  return impl_->opened && const_cast<V2MPlayer &>(impl_->seq).IsPlaying();
}

void Player::render(float *stereoInterleaved, uint32_t frames)
{
  if (!impl_->opened) {
    memset(stereoInterleaved, 0, sizeof(float) * 2u * frames);
    return;
  }
  // V2MPlayer::Render outputs silence when stopped and the synth tail after
  // song end -- matching the API contract.
  impl_->seq.Render(stereoInterleaved, frames);
}

void Player::setSeed(uint64_t seed)
{
  impl_->seed = seed;
  impl_->seq.SetSeed(seed); // applied at the next play() (Reset re-inits)
}

} // namespace v2portable
