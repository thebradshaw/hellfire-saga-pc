/*
 * romcheck.h - identifies the ROM the player gives us.
 *
 * We never ship the game. The player supplies an already patched
 * HellFire Saga ROM and we confirm it is the version we were built for.
 */
#ifndef HF_ROMCHECK_H
#define HF_ROMCHECK_H

typedef enum
{
  ROM_OK = 0,          /* HellFire Saga v2.11, exactly what we expect */
  ROM_MISSING,         /* file could not be opened */
  ROM_NOT_GENESIS,     /* no "SEGA" header: not a Genesis ROM at all */
  ROM_UNPATCHED_S3,    /* a plain Sonic 3 / Sonic & Knuckles ROM */
  ROM_SMD_FORMAT,      /* interleaved .smd dump; needs converting to .bin/.gen */
  ROM_UNKNOWN          /* a Genesis ROM, but not the version we expect */
} RomStatus;

typedef struct
{
  RomStatus status;
  long      size;
  char      sha1_hex[41];
  char      title[49];  /* header "overseas name", trimmed */
} RomCheck;

#define HELLFIRE_V211_SHA1 "f8f57b7295c93ee30fc02340051d081a060f8e17"
#define HELLFIRE_V211_SIZE 4087160L

void        romcheck_file(const char *path, RomCheck *out);
const char *romcheck_status_name(RomStatus s);

/* A sentence or two telling the player what to do about this result. */
const char *romcheck_advice(RomStatus s);

#endif
