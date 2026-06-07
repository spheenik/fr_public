// canon.cpp -- prove two v2m files are the SAME song by canonicalizing both
// to the common v6 layout and comparing bytes. Used to show the fr-022 v5
// embedded song is a value-preserving up-conversion of the repo v3 copy
// (RG2/einschlag/whateverload2.v2m) -- i.e. format v3->v5 changed layout, not
// audio. See NOTES.md "Resolution: the fr-022 song is NOT actually lost".
//
//   g++ -O2 -ffp-contract=off -std=c++17 -I../../portable canon.cpp \
//       ../../portable/libv2portable.a -o canon
//   ./canon ../../../RG2/einschlag/whateverload2.v2m fr022_embedded.v2m

#include "v2load.h"
#include <cstdio>
#include <vector>
#include <cstring>
using namespace v2portable;

static std::vector<unsigned char> rd(const char *p)
{
  FILE *f = fopen(p, "rb");
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<unsigned char> d(n);
  if (fread(d.data(), 1, n, f) != (size_t)n) d.clear();
  fclose(f);
  return d;
}

int main(int c, char **v)
{
  if (c != 3) { printf("usage: %s <a.v2m> <b.v2m>\n", v[0]); return 2; }
  auto a = rd(v[1]), b = rd(v[2]);
  auto ra = v2loadCanonicalize(a.data(), a.size());
  auto rb = v2loadCanonicalize(b.data(), b.size());
  printf("%s: detected v%d -> canon %zu bytes\n", v[1], ra.version, ra.size);
  printf("%s: detected v%d -> canon %zu bytes\n", v[2], rb.version, rb.size);
  if (ra.result != Result::OK || rb.result != Result::OK) { printf("load fail\n"); return 1; }
  size_t n = ra.size < rb.size ? ra.size : rb.size, diff = 0;
  for (size_t i = 0; i < n; i++) if (ra.data[i] != rb.data[i]) diff++;
  bool same = ra.size == rb.size && memcmp(ra.data, rb.data, ra.size) == 0;
  printf("canonicalized-to-v6 byte-identical: %s  (differing bytes %zu / %zu shared)\n",
         same ? "YES" : "NO", diff, n);
  return 0;
}
