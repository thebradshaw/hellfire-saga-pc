#include "hf_file.h"

#ifdef _WIN32
#include <windows.h>

FILE *hf_fopen(const char *utf8_path, const char *mode)
{
  wchar_t wpath[1024], wmode[16];
  if (!MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wpath, 1024)) return NULL;
  if (!MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 16)) return NULL;
  return _wfopen(wpath, wmode);
}
#else
FILE *hf_fopen(const char *utf8_path, const char *mode)
{
  return fopen(utf8_path, mode);
}
#endif
