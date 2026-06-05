// Minimal CLI render driver for the portable player:
//   v2play <file.v2m> <out.f32> [seconds]
// Renders interleaved stereo float32 at 44100 Hz (the lab's compare.py
// format) so portable renders diff directly against the oracles.

#include "v2portable.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: %s <file.v2m> <out.f32> [seconds]\n", argv[0]);
    return 1;
  }
  const char *inPath = argv[1], *outPath = argv[2];
  double seconds = (argc > 3) ? atof(argv[3]) : 60.0;
  uint32_t chunk = (argc > 4) ? (uint32_t)atoi(argv[4]) : 4096; // render chunk (invariance per spec)

  FILE *f = fopen(inPath, "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", inPath); return 1; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  void *data = malloc((size_t)len);
  if (fread(data, 1, (size_t)len, f) != (size_t)len) {
    fprintf(stderr, "short read\n");
    return 1;
  }
  fclose(f);

  v2portable::Player p;
  v2portable::Result r = p.open(data, (size_t)len);
  free(data);
  switch (r) {
  case v2portable::Result::OK:
    break;
  case v2portable::Result::BadFile:
    fprintf(stderr, "%s: not a (supported) v2m file\n", inPath);
    return 2;
  case v2portable::Result::UnsupportedVersion:
    fprintf(stderr, "%s: format v%d outside compiled range [%d..%d]\n",
            inPath, p.fileVersion(), V2_VER_MIN, V2_VER_MAX);
    return 3;
  case v2portable::Result::FpEnvBroken:
    fprintf(stderr, "FP environment flushes subnormals (fast-math build?)\n");
    return 4;
  }
  fprintf(stderr, "%s: format v%d\n", inPath, p.fileVersion());

  FILE *o = fopen(outPath, "wb");
  if (!o) { fprintf(stderr, "cannot write %s\n", outPath); return 1; }

  p.play(0);
  enum { CHUNKMAX = 4096 };
  static float buf[2 * CHUNKMAX];
  if (chunk < 1 || chunk > CHUNKMAX) chunk = 4096;
  uint64_t total = (uint64_t)(seconds * 44100.0);
  for (uint64_t done = 0; done < total; ) {
    uint32_t n = (uint32_t)((total - done < (uint64_t)chunk) ? (total - done)
                                                             : (uint64_t)chunk);
    p.render(buf, n);
    fwrite(buf, 2 * sizeof(float), n, o);
    done += n;
  }
  fclose(o);
  fprintf(stderr, "wrote %.1fs -> %s\n", (double)total / 44100.0, outPath);
  return 0;
}
