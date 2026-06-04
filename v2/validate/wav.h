// Minimal WAV writer for the V2 harness. Header-only, no deps.
//
// Writes a 16-bit PCM stereo WAV from an interleaved float buffer (L,R,L,R,...)
// so a render can be listened to directly. Floats are clamped to [-1,1] and
// scaled to 16-bit — the most universally playable format. The raw .f32 dump
// stays the source of truth for bit-exact validation; this is purely for ears.
#ifndef V2_VALIDATE_WAV_H
#define V2_VALIDATE_WAV_H

#include <cstdio>
#include <cstring>

// Returns 0 on success, nonzero on write failure.
static int wav_write_pcm16(const char *path, const float *interleaved,
                           unsigned long stereo_frames, unsigned int rate)
{
  FILE *f = fopen(path, "wb");
  if (!f) return 1;

  const unsigned short channels = 2;
  const unsigned short bits = 16;
  const unsigned int block_align = channels * (bits / 8);          // 4
  const unsigned int byte_rate = rate * block_align;
  const unsigned int data_bytes = (unsigned int)stereo_frames * block_align;
  const unsigned int riff_bytes = 36 + data_bytes;

  // Little-endian header (matches x86 host; writes byte-by-byte to stay portable).
  unsigned char h[44];
  memcpy(h + 0,  "RIFF", 4);
  h[4] = riff_bytes;       h[5] = riff_bytes >> 8;  h[6] = riff_bytes >> 16;  h[7] = riff_bytes >> 24;
  memcpy(h + 8,  "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0;       // fmt chunk size = 16
  h[20] = 1;  h[21] = 0;                              // PCM
  h[22] = channels;        h[23] = 0;
  h[24] = rate;            h[25] = rate >> 8;         h[26] = rate >> 16;       h[27] = rate >> 24;
  h[28] = byte_rate;       h[29] = byte_rate >> 8;    h[30] = byte_rate >> 16;  h[31] = byte_rate >> 24;
  h[32] = block_align;     h[33] = 0;
  h[34] = bits;            h[35] = 0;
  memcpy(h + 36, "data", 4);
  h[40] = data_bytes;      h[41] = data_bytes >> 8;   h[42] = data_bytes >> 16; h[43] = data_bytes >> 24;
  fwrite(h, 1, 44, f);

  const unsigned long n = stereo_frames * 2;
  for (unsigned long i = 0; i < n; i++) {
    float x = interleaved[i];
    int v = (int)(x * 32767.0f + (x >= 0.0f ? 0.5f : -0.5f));
    if (v > 32767) v = 32767;
    else if (v < -32768) v = -32768;
    unsigned char s[2] = { (unsigned char)(v & 0xff), (unsigned char)((v >> 8) & 0xff) };
    fwrite(s, 1, 2, f);
  }
  fclose(f);
  return 0;
}

#endif // V2_VALIDATE_WAV_H
