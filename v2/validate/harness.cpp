// V2 core validation harness (Phase 1).
//
// Renders a .v2m song to a raw interleaved-stereo float32 buffer and dumps it
// to disk. The synth core (asm vs C++) is chosen at LINK time, not here: this
// one source is linked twice, against synth_asm.o or synth_cpp.o. The only
// variable between the two resulting binaries is which core they call.
//
//   usage: harness <song.v2m> <out.f32> <num_stereo_samples>
//
// Output format: <num_stereo_samples>*2 little-endian float32 (L,R,L,R,...).
//
// See openspec/changes/validate-v2-synth-core/.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "types.h"
#include "v2mplayer.h"
#include "wav.h"

// Safety stub: synth_core.cpp declares OutputDebugStringA for its (dead) debug
// printf path. The optimizer normally drops it; provide a no-op so any build
// links regardless of optimization level. Harmless — never called in practice.
extern "C" void __stdcall OutputDebugStringA(const char *) {}

// Render in fixed-size blocks. Block size is identical for both core builds, so
// it cancels out of the A/B comparison; V2MPlayer is sample-accurate w.r.t.
// event timing, so the choice does not affect output.
// Stereo samples per Render call. Kept <= MAX_FRAME_SIZE (280) so that each
// Render maps to a single synth-internal frame and the BUSTAP per-frame dumps
// capture every sample (bustap_dump snapshots only the last internal sub-frame
// per Render call). Output is sample-accurate regardless of block size.
static const sU32 BLOCK = 256;

static sU8 *load_file(const char *path, sU32 *len_out)
{
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "harness: cannot open '%s'\n", path); return 0; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  sU8 *buf = (sU8 *)malloc(len);
  if (!buf || fread(buf, 1, len, f) != (size_t)len) {
    fprintf(stderr, "harness: read failed for '%s'\n", path);
    fclose(f); free(buf); return 0;
  }
  fclose(f);
  *len_out = (sU32)len;
  return buf;
}

int main(int argc, char **argv)
{
  if (argc != 4) {
    fprintf(stderr, "usage: %s <song.v2m> <out.f32|.wav> <num_stereo_samples|auto>\n", argv[0]);
    fprintf(stderr, "  num_stereo_samples : fixed render length (deterministic; use for A/B validation)\n");
    fprintf(stderr, "  auto               : render the WHOLE song + reverb/delay tail until it decays\n");
    return 2;
  }
  const char *songpath = argv[1];
  const char *outpath  = argv[2];
  const bool  auto_len = (strcmp(argv[3], "auto") == 0);
  sU32 total = auto_len ? 0 : (sU32)strtoul(argv[3], 0, 10);

  // The v2m memory block must remain valid for as long as the player is open.
  sU32 songlen = 0;
  sU8 *song = load_file(songpath, &songlen);
  if (!song) return 1;

  // V2MPlayer embeds the synth instance (m_synth[3MB]) and drives the synth.h
  // seam internally — no soundsys.cpp / DirectSound involvement.
  static V2MPlayer player; // static: 3MB instance, keep off the stack
  player.Init();
  // Era compat (env V2_SRCVER=<n>): declare the song's ORIGINAL v2m format
  // version (0 = year-2000/fr08) when rendering a v2mconv-upgraded file, so
  // the core gates the period DSP behaviors (fr08-extraction/DELTA.md).
  // conv_v2m prints the right value. Unset = modern rendering (default).
  if (const char *sv = getenv("V2_SRCVER")) {
    player.SetSourceVersion(atoi(sv));
    fprintf(stderr, "harness: era compat: source v2m version = %d\n", atoi(sv));
  }
  if (!player.Open(song, 44100)) {
    fprintf(stderr, "harness: V2MPlayer::Open failed (song may need v2mconv upgrade)\n");
    free(song);
    return 3;
  }
  player.Play(0);

  sF32 *out = 0;
  std::vector<sF32> autobuf; // backing store for auto mode

  if (auto_len) {
    // Render the whole song, then let the tail ring out.
    //
    // Lifecycle (see v2mplayer_port.cpp): while song events remain, state is
    // PLAYING and IsPlaying() is true. When the last event is consumed, Tick()
    // flips state to STOPPED and Render() then keeps producing the reverb/delay
    // tail indefinitely — it never stops on its own. So: render blocks while
    // playing, then keep rendering until the output decays to silence.
    //
    // NOTE: this is for *listening*, not A/B validation — the silence-trimmed
    // length depends on the tail and could differ by a few samples between
    // cores. For bit-exact comparison pass a fixed <num_stereo_samples>.
    const sF32 SILENCE = 3.0e-5f;        // ~ -90 dBFS
    const sU32 SILENT_BLOCKS_NEEDED = 44100 / BLOCK + 1; // ~1s of quiet to call it done
    const sU32 SONG_CAP   = 20u * 60u * 44100u; // 20 min hard cap (runaway/looping songs)
    const sU32 TAIL_CAP   = 60u * 44100u;        // 60s max tail after events end

    sF32 block[BLOCK * 2];
    sU32 song_samples = 0;

    // 1. events
    while (player.IsPlaying() && song_samples < SONG_CAP) {
      player.Render(block, BLOCK);
      autobuf.insert(autobuf.end(), block, block + BLOCK * 2);
      song_samples += BLOCK;
    }

    // 2. tail until silence (or cap)
    sU32 silent = 0, tail = 0;
    while (silent < SILENT_BLOCKS_NEEDED && tail < TAIL_CAP) {
      player.Render(block, BLOCK);
      autobuf.insert(autobuf.end(), block, block + BLOCK * 2);
      sF32 peak = 0.0f;
      for (sU32 i = 0; i < BLOCK * 2; i++) {
        sF32 a = block[i] < 0 ? -block[i] : block[i];
        if (a > peak) peak = a;
      }
      if (peak < SILENCE) silent++; else silent = 0;
      tail += BLOCK;
    }
    // Trim the trailing detected-silence so the file ends right as it decays.
    if (silent >= SILENT_BLOCKS_NEEDED) {
      size_t trim = (size_t)silent * BLOCK * 2;
      if (trim < autobuf.size()) autobuf.resize(autobuf.size() - trim);
    }

    total = (sU32)(autobuf.size() / 2);
    out = autobuf.empty() ? 0 : &autobuf[0];
    fprintf(stderr, "harness: auto length — song %.2fs + tail = %.2fs total (%u frames)\n",
            song_samples / 44100.0, total / 44100.0, total);
  } else {
    // Fixed length: render into one big interleaved-stereo float buffer.
    out = (sF32 *)malloc((size_t)total * 2 * sizeof(sF32));
    if (!out) { fprintf(stderr, "harness: OOM (%u samples)\n", total); free(song); return 4; }

    for (sU32 done = 0; done < total; ) {
      sU32 n = total - done;
      if (n > BLOCK) n = BLOCK;
      player.Render(out + (size_t)done * 2, n);
      done += n;
    }
  }

  player.Close();

  // If the output path ends in .wav, emit a playable 16-bit PCM WAV instead of
  // the raw float32 dump. The .f32 path is unchanged so the A/B validation flow
  // (compare.py) keeps working bit-for-bit.
  size_t pathlen = strlen(outpath);
  bool as_wav = pathlen >= 4 &&
                (outpath[pathlen-4] == '.') &&
                (outpath[pathlen-3] == 'w' || outpath[pathlen-3] == 'W') &&
                (outpath[pathlen-2] == 'a' || outpath[pathlen-2] == 'A') &&
                (outpath[pathlen-1] == 'v' || outpath[pathlen-1] == 'V');

  if (as_wav) {
    if (wav_write_pcm16(outpath, out, total, 44100)) {
      fprintf(stderr, "harness: cannot write '%s'\n", outpath);
      if (!auto_len) free(out);
      free(song); return 5;
    }
    fprintf(stderr, "harness: wrote %u stereo frames (%.2fs) to %s\n",
            total, total / 44100.0, outpath);
  } else {
    FILE *of = fopen(outpath, "wb");
    if (!of) { fprintf(stderr, "harness: cannot write '%s'\n", outpath); if (!auto_len) free(out); free(song); return 5; }
    fwrite(out, sizeof(sF32), (size_t)total * 2, of);
    fclose(of);

    fprintf(stderr, "harness: wrote %u stereo samples (%u floats) to %s\n",
            total, total * 2, outpath);
  }

  if (!auto_len) free(out); // auto mode: out points into autobuf (vector-owned)
  free(song);
  return 0;
}
