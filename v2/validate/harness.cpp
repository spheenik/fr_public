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

#include "types.h"
#include "v2mplayer.h"

// Safety stub: synth_core.cpp declares OutputDebugStringA for its (dead) debug
// printf path. The optimizer normally drops it; provide a no-op so any build
// links regardless of optimization level. Harmless — never called in practice.
extern "C" void __stdcall OutputDebugStringA(const char *) {}

// Render in fixed-size blocks. Block size is identical for both core builds, so
// it cancels out of the A/B comparison; V2MPlayer is sample-accurate w.r.t.
// event timing, so the choice does not affect output.
static const sU32 BLOCK = 1024; // stereo samples per Render call

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
    fprintf(stderr, "usage: %s <song.v2m> <out.f32> <num_stereo_samples>\n", argv[0]);
    return 2;
  }
  const char *songpath = argv[1];
  const char *outpath  = argv[2];
  sU32 total = (sU32)strtoul(argv[3], 0, 10);

  // The v2m memory block must remain valid for as long as the player is open.
  sU32 songlen = 0;
  sU8 *song = load_file(songpath, &songlen);
  if (!song) return 1;

  // V2MPlayer embeds the synth instance (m_synth[3MB]) and drives the synth.h
  // seam internally — no soundsys.cpp / DirectSound involvement.
  static V2MPlayer player; // static: 3MB instance, keep off the stack
  player.Init();
  if (!player.Open(song, 44100)) {
    fprintf(stderr, "harness: V2MPlayer::Open failed (song may need v2mconv upgrade)\n");
    free(song);
    return 3;
  }
  player.Play(0);

  // Render into one big interleaved-stereo float buffer, block by block.
  sF32 *out = (sF32 *)malloc((size_t)total * 2 * sizeof(sF32));
  if (!out) { fprintf(stderr, "harness: OOM (%u samples)\n", total); free(song); return 4; }

  for (sU32 done = 0; done < total; ) {
    sU32 n = total - done;
    if (n > BLOCK) n = BLOCK;
    player.Render(out + (size_t)done * 2, n);
    done += n;
  }

  player.Close();

  FILE *of = fopen(outpath, "wb");
  if (!of) { fprintf(stderr, "harness: cannot write '%s'\n", outpath); free(out); free(song); return 5; }
  fwrite(out, sizeof(sF32), (size_t)total * 2, of);
  fclose(of);

  fprintf(stderr, "harness: wrote %u stereo samples (%u floats) to %s\n",
          total, total * 2, outpath);

  free(out);
  free(song);
  return 0;
}
