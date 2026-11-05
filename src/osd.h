/*
 * osd.h - glue between the Genesis Plus GX (Wide) core and our frontend.
 *
 * The core includes "osd.h" from shared.h and expects it to provide the
 * global config struct, a few file paths, and some callbacks. This is our
 * replacement for the libretro version, with no libretro dependencies.
 */
#ifndef HF_OSD_H
#define HF_OSD_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#endif

#define MAX_INPUTS 8
#define MAX_KEYS   8
#define MAXPATHLEN 1024

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Allow more than the hardware's 20 sprites per line in the extra
   widescreen columns, matching the libretro build of the wide core. */
#define HAVE_NO_SPRITE_LIMIT
#define MAX_SPRITES_PER_LINE 80
#define TMS_MAX_SPRITES_PER_LINE   (config.no_sprite_limit ? MAX_SPRITES_PER_LINE : 4)
#define MODE4_MAX_SPRITES_PER_LINE (config.no_sprite_limit ? MAX_SPRITES_PER_LINE : 8)
#define MODE5_MAX_SPRITES_PER_LINE (config.no_sprite_limit ? MAX_SPRITES_PER_LINE : (bitmap.viewport.w >> 4))
#define MODE5_MAX_SPRITE_PIXELS    (config.no_sprite_limit ? MAX_SPRITES_PER_LINE * 32 : max_sprite_pixels)

typedef struct
{
  int8_t  device;
  uint8_t port;
  uint8_t padtype;
} t_input_config;

/* Field list must match what the core reads (copied from the wide core's
   libretro osd.h). Unused fields are harmless. */
typedef struct
{
  char version[16];
  uint8_t hq_fm;
  uint8_t fm_enhance;
  uint8_t fm_voice_recovery;
  uint8_t filter;
  uint8_t hq_psg;
  uint8_t ym2612;
  uint8_t ym2413;
  uint8_t mono;
  int16_t psg_preamp;
  int16_t fm_preamp;
  int16_t cdda_volume;
  int16_t pcm_volume;
  uint16_t lp_range;
  int16_t low_freq;
  int16_t high_freq;
  int16_t lg;
  int16_t mg;
  int16_t hg;
  uint8_t system;
  uint8_t region_detect;
  uint8_t master_clock;
  uint8_t vdp_mode;
  uint8_t force_dtack;
  uint8_t addr_error;
  uint8_t bios;
  uint8_t lock_on;
  uint8_t add_on;
  uint8_t overscan;
  uint8_t aspect_ratio;
  uint8_t ntsc;
  uint8_t lcd;
  uint8_t gg_extra;
  uint8_t left_border;
  uint8_t h40_extra_columns;        /* 10 extra 8px columns = 400px wide */
  uint8_t h40_extra_columns_alpha;
  uint8_t vdp_fix_dma_boundary_bug;
  uint8_t render;
  t_input_config input[MAX_INPUTS];
  uint8_t invert_mouse;
  uint8_t gun_cursor;
  uint32_t overclock;
  uint8_t no_sprite_limit;
  uint8_t enhanced_vscroll;
  uint8_t enhanced_vscroll_limit;
  uint8_t cd_latency;
  bool cd_precache;
} t_config;

extern t_config config;

/* Optional BIOS / lock-on files. We never use them, so they point nowhere. */
#define GG_ROM      ""
#define AR_ROM      ""
#define SK_ROM      ""
#define SK_UPMEM    ""
#define GG_BIOS     ""
#define MD_BIOS     ""
#define CD_BIOS_EU  ""
#define CD_BIOS_US  ""
#define CD_BIOS_JP  ""
#define MS_BIOS_US  ""
#define MS_BIOS_EU  ""
#define MS_BIOS_JP  ""

/* Called by the core once per frame to latch controller state. */
extern void osd_input_update(void);

/* Called by the core to read a file into memory. */
extern int load_archive(char *filename, unsigned char *buffer, int maxsize, char *extension);

/* Mega CD streaming (unused, but cdd.h needs the types). */
#define cdStream            FILE
#define cdStreamOpen(fname) fopen(fname, "rb")
#define cdStreamClose       fclose
#define cdStreamRead        fread
#define cdStreamSeek        fseek
#define cdStreamTell        ftell
#define cdStreamGets        fgets

/* cdd.c logs through the libretro logger; route it to a no-op. */
#define RETRO_LOG_INFO 1
#define log_cb(level, ...) ((void)0)

#endif /* HF_OSD_H */
