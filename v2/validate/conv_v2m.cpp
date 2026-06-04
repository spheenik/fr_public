// Minimal CLI around v2mconv's ConvertV2M: upgrade an old-format .v2m to the
// newest patch/global layout (what the "_new" files in v2/v2m were made with).
// The repo's conv2m tool was repurposed for .v2p patch export (its ConvertV2M
// call is commented out), hence this thin wrapper for the validation rig.
//
//   ./conv_v2m <in.v2m> <out.v2m>
//
// Prints the detected source version delta; refuses files CheckV2MVersion
// rejects (-1: unknown synth version / no patches).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../types.h"
#include "tool/file.h"   // shim/tool/file.h (-Ishim): portable file class
#include "../sounddef.h"
#include "../v2mconv.h"

int main(int argc, char **argv)
{
  if (argc != 3)
  {
    fprintf(stderr, "usage: conv_v2m <in.v2m> <out.v2m>\n");
    return 1;
  }

  FILE *f = fopen(argv[1], "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
  fseek(f, 0, SEEK_END);
  long inlen = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *in = (unsigned char*)malloc(inlen);
  if (fread(in, 1, inlen, f) != (size_t)inlen) { fprintf(stderr, "short read\n"); return 1; }
  fclose(f);

  sdInit();   // builds v2version/v2vsizes/v2gsizes from the parameter tables

  int delta = CheckV2MVersion(in, inlen);
  if (delta < 0)
  {
    fprintf(stderr, "%s: REJECTED (%s)\n", argv[1], v2mconv_errors[-delta]);
    return 2;
  }
  printf("%s: source is %d version(s) behind current (format v%d)\n",
         argv[1], delta, v2version - delta);
  if (delta > 0)
    printf("  era compat: render the converted file with V2_SRCVER=%d to gate "
           "period DSP behaviors (see v2m/fr08-extraction/DELTA.md)\n",
           v2version - delta);

  unsigned char *out = 0;
  int outlen = 0;
  ConvertV2M(in, inlen, &out, &outlen);
  if (!out || outlen <= 0)
  {
    fprintf(stderr, "%s: conversion produced nothing\n", argv[1]);
    return 3;
  }

  f = fopen(argv[2], "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
  fwrite(out, 1, outlen, f);
  fclose(f);
  printf("%s: wrote %d bytes (was %ld)\n", argv[2], outlen, inlen);
  return 0;
}
