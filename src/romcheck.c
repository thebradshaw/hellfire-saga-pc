#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "romcheck.h"
#include "sha1.h"
#include "hf_file.h"

static void trim_copy(char *dst, const unsigned char *src, int n)
{
  int i, end = 0;
  for (i = 0; i < n; i++)
  {
    unsigned char c = src[i];
    dst[i] = (c >= 32 && c < 127) ? (char)c : ' ';
    if (dst[i] != ' ') end = i + 1;
  }
  dst[end] = 0;
}

void romcheck_file(const char *path, RomCheck *out)
{
  FILE *f;
  unsigned char *data;
  uint8_t digest[20];
  Sha1 s;

  memset(out, 0, sizeof(*out));
  out->status = ROM_MISSING;

  f = path ? hf_fopen(path, "rb") : NULL;
  if (!f) return;
  fseek(f, 0, SEEK_END);
  out->size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (out->size <= 0x200 || out->size > 16L * 1024 * 1024) { fclose(f); out->status = ROM_NOT_GENESIS; return; }

  data = (unsigned char *)malloc((size_t)out->size);
  if (!data) { fclose(f); return; }
  if (fread(data, 1, (size_t)out->size, f) != (size_t)out->size) { fclose(f); free(data); return; }
  fclose(f);

  sha1_init(&s);
  sha1_update(&s, data, (size_t)out->size);
  sha1_final(&s, digest);
  sha1_hex(digest, out->sha1_hex);

  if (memcmp(data + 0x100, "SEGA", 4) != 0 && memcmp(data + 0x101, "SEGA", 4) != 0)
  {
    /* .smd dumps start with a 512-byte header and interleaved halves */
    out->status = ((out->size & 0x3FFF) == 0x200 && data[8] == 0xAA && data[9] == 0xBB)
                  ? ROM_SMD_FORMAT : ROM_NOT_GENESIS;
    free(data);
    return;
  }

  trim_copy(out->title, data + 0x150, 48);

  if (strcmp(out->sha1_hex, HELLFIRE_V211_SHA1) == 0)
    out->status = ROM_OK;
  else if (out->size <= 0x400000 &&
           (strstr(out->title, "SONIC THE") || strstr(out->title, "SONIC & KNUCKLES")) &&
           !strstr(out->title, "HELL"))
    out->status = ROM_UNPATCHED_S3;
  else
    out->status = ROM_UNKNOWN;

  free(data);
}

const char *romcheck_status_name(RomStatus s)
{
  switch (s)
  {
    case ROM_OK:           return "HellFire Saga v2.11";
    case ROM_MISSING:      return "file not found";
    case ROM_NOT_GENESIS:  return "not a Genesis ROM";
    case ROM_UNPATCHED_S3: return "unpatched Sonic ROM";
    case ROM_SMD_FORMAT:   return "SMD format ROM";
    default:               return "unknown version";
  }
}

const char *romcheck_advice(RomStatus s)
{
  switch (s)
  {
    case ROM_OK:
      return "All set. This is HellFire Saga v2.11.";
    case ROM_MISSING:
      return "That file could not be opened. Drag your ROM onto this window.";
    case ROM_NOT_GENESIS:
      return "That isn't a Sega Genesis ROM. Use your patched .gen or .bin file.";
    case ROM_UNPATCHED_S3:
      return "This looks like an unpatched Sonic ROM. Apply the HellFire Saga v2.11 "
             "patch to it first, then drag the patched file here.";
    case ROM_SMD_FORMAT:
      return "This is an .smd dump. Convert it to .bin/.gen, then patch it.";
    default:
      return "This Genesis ROM isn't HellFire Saga v2.11. Other versions may not work "
             "with widescreen and boss bars.";
  }
}
