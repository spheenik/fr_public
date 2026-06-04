// Empty stand-in for <windows.h> so the original v2 tool sources
// (sounddef.cpp) build on Linux for conv_v2m. Nothing from the real Win32 API
// is actually used on this path (UI/file calls are compiled but never made,
// or are covered by shim/tool/file.h).
#ifndef VALIDATE_SHIM_WINDOWS_H
#define VALIDATE_SHIM_WINDOWS_H

#include <stdio.h>

// sounddef.cpp's load-warning popup — print to stderr instead.
#define MB_ICONEXCLAMATION 0
static inline int MessageBox(void *, const char *text, const char *caption, int)
{
  fprintf(stderr, "[%s] %s\n", caption, text);
  return 0;
}

#endif
