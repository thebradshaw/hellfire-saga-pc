/*
 * smoketest.c - boots a ROM without a window and saves screenshots.
 *
 *   hellfire_smoketest <rom> <out_prefix> [boot_frames] [extra_columns] [script]
 *
 * Taps Start every 2 seconds for boot_frames to get into the game, then runs
 * the optional script: comma-separated steps like R120 (hold right for 120
 * frames), L60, U30, D30, J10 (jump), W60 (wait). A screenshot is written
 * after the boot and after every step: <out_prefix>_00.ppm, _01.ppm, ...
 * With extra_columns > 0 the widescreen patches are applied too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu.h"
#include "romcheck.h"
#include "widescreen.h"

static int16_t audio[4096 * 2];

static void save(const char *prefix, int index)
{
  char path[512];
  int w, h, pitch, x, y;
  const uint32_t *px = emu_frame(&w, &h, &pitch);
  FILE *out;

  snprintf(path, sizeof(path), "%s_%02d.ppm", prefix, index);
  out = fopen(path, "wb");
  if (!out) return;
  fprintf(out, "P6\n%d %d\n255\n", w, h);
  for (y = 0; y < h; y++)
  {
    const uint32_t *row = (const uint32_t *)((const uint8_t *)px + (size_t)y * pitch);
    for (x = 0; x < w; x++)
    {
      unsigned char rgb[3] = { (row[x] >> 16) & 255, (row[x] >> 8) & 255, row[x] & 255 };
      fwrite(rgb, 1, 3, out);
    }
  }
  fclose(out);
  printf("wrote %s (%dx%d)\n", path, w, h);
}

int main(int argc, char **argv)
{
  int boot, extra, i, shot = 0;
  RomCheck rc;

  if (argc < 3)
  {
    fprintf(stderr, "usage: %s <rom> <out_prefix> [boot_frames] [extra_columns] [script]\n", argv[0]);
    return 2;
  }
  boot  = argc > 3 ? atoi(argv[3]) : 600;
  extra = argc > 4 ? atoi(argv[4]) : 0;

  romcheck_file(argv[1], &rc);
  printf("rom check: %s (%s)\n", romcheck_status_name(rc.status), rc.sha1_hex);

  if (!emu_load(argv[1], NULL, extra))
  {
    fprintf(stderr, "failed to load %s\n", argv[1]);
    return 1;
  }
  if (extra > 0) printf("widescreen patches: %d\n", widescreen_apply());

  for (i = 0; i < boot; i++)
  {
    emu_set_pad((i % 120) < 4 ? EMU_BTN_START : 0);
    emu_run_frame(audio);
  }
  save(argv[2], shot++);

  if (argc > 5)
  {
    char script[1024], *step;
    snprintf(script, sizeof(script), "%s", argv[5]);
    for (step = strtok(script, ","); step; step = strtok(NULL, ","))
    {
      uint16_t pad = 0;
      int n = atoi(step + 1);
      switch (step[0])
      {
        case 'R': pad = EMU_BTN_RIGHT; break;
        case 'L': pad = EMU_BTN_LEFT;  break;
        case 'U': pad = EMU_BTN_UP;    break;
        case 'D': pad = EMU_BTN_DOWN;  break;
        case 'J': pad = EMU_BTN_C;     break;
        default:  pad = 0;             break;
      }
      for (i = 0; i < n; i++)
      {
        emu_set_pad(pad);
        emu_run_frame(audio);
      }
      save(argv[2], shot++);
    }
  }

  emu_unload();
  return 0;
}
