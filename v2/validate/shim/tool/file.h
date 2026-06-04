// Portable stdio stand-in for v2/tool/File.h (which is Win32 HANDLE-based and
// uses an MSVC-only ##-pasting macro). Just enough for sounddef.cpp to build
// on Linux for the conv_v2m converter; include after types.h (sounddef.cpp
// does). Picked up via -Ishim because the original include "tool/file.h"
// misses the case-sensitive filename (tool/File.h).
#ifndef __file_h_
#define __file_h_

#include <stdio.h>
#include <string.h>

#define GETPUTMETHODS(T) T get##T() { T t; read(&t,(sS32)sizeof(T)); return t; } \
                         sS32 put##T(T t) { return write(&t,(sS32)sizeof(T)); }

class file
{
public:
  FILE *fp;

  file() : fp(0)  { }
  virtual ~file() { close(); }

  bool open(const char *name, bool wr=false) { close(); fp = fopen(name, wr ? "wb" : "rb"); return fp != 0; }
  virtual void close() { if (fp) { fclose(fp); fp = 0; } }

  virtual sS32 read(void *buf, sS32 cnt)        { return fp ? (sS32)fread(buf, 1, cnt, fp) : 0; }
  virtual sS32 write(const void *buf, sS32 cnt) { return fp ? (sS32)fwrite(buf, 1, cnt, fp) : 0; }

  virtual sS32 seek(sS32 pos=0)   { if (fp) fseek(fp, pos, SEEK_SET); return tell(); }
  virtual sS32 seekcur(sS32 pos)  { if (fp) fseek(fp, pos, SEEK_CUR); return tell(); }
  virtual sS32 seekend(sS32 pos=0){ if (fp) fseek(fp, pos, SEEK_END); return tell(); }

  virtual sS32 tell() { return fp ? (sS32)ftell(fp) : 0; }
  virtual sS32 size()
  {
    if (!fp) return 0;
    long cur = ftell(fp); fseek(fp, 0, SEEK_END);
    long s = ftell(fp); fseek(fp, cur, SEEK_SET);
    return (sS32)s;
  }

  sBool eof() { return tell() == size(); }

  sBool eread(void *buf, sS32 cnt)        { return read(buf, cnt) == cnt; }
  sBool ewrite(const void *buf, sS32 cnt) { return write(buf, cnt) == cnt; }

  template<class T> sS32 read(T& buf)  { return read(&buf, (sS32)sizeof(T)); }
  template<class T> sS32 write(T& buf) { return write(&buf, (sS32)sizeof(T)); }

  GETPUTMETHODS(sInt)
  GETPUTMETHODS(sS8)
  GETPUTMETHODS(sS16)
  GETPUTMETHODS(sS32)
  GETPUTMETHODS(sU8)
  GETPUTMETHODS(sU16)
  GETPUTMETHODS(sU32)
  GETPUTMETHODS(sF32)
  GETPUTMETHODS(sF64)

  sS32 puts(const char *t) { return t ? write((const void*)t, (sS32)strlen(t)) : 0; }
};

// Memory file: just enough for sdImportV2MPatches (open-from-file / size /
// detach). Reads the source file fully into a heap buffer.
class fileM : public file
{
public:
  unsigned char *buf;
  sS32 len;

  fileM() : buf(0), len(0) { }
  ~fileM() { if (buf) delete[] buf; }

  bool open(file &src)
  {
    len = src.size();
    buf = new unsigned char[len];
    src.seek(0);
    return src.read(buf, len) == len;
  }

  void *detach() { void *p = buf; buf = 0; return p; }

  void close() { if (buf) { delete[] buf; buf = 0; } len = 0; }
  sS32 read(void *, sS32)        { return 0; }   // unused on this path
  sS32 write(const void *, sS32) { return 0; }
  sS32 seek(sS32)                { return 0; }
  sS32 tell()                    { return 0; }
  sS32 size()                    { return len; }
};

#endif
