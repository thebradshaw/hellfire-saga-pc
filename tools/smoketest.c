/*
 * smoketest.c - boots a ROM without a window, presses Start a few times,
 * and writes the last frame to a PPM image. Used to check the core works.
 *
 *   hellfire_smoketest <rom> <out.ppm> [frames] [extra_columns]
 */
#include <stdio.h>
#include <stdlib.h>
#include "emu.h"
#include "romcheck.h"

int main(int argc, char **argv)
{
  static int16_t audio[4096 * 2];
  int frames, extra, i, w, h, pitch, x, y;
  long samples = 0;
  const uint32_t *px;
  FILE *out;
  RomCheck rc;

  if (argc < 3)
  {
    fprintf(stderr, "usage: %s <rom> <out.ppm> [frames] [extra_columns]\n", argv[0]);
    return 2;
  }
  frames = argc > 3 ? atoi(argv[3]) : 600;
  extra  = argc > 4 ? atoi(argv[4]) : 0;

  romcheck_file(argv[1], &rc);
  printf("rom check: %s (%s)\n", romcheck_status_name(rc.status), rc.sha1_hex);

  if (!emu_load(argv[1], NULL, extra))
  {
    fprintf(stderr, "failed to load %s\n", argv[1]);
    return 1;
  }

  for (i = 0; i < frames; i++)
  {
    /* tap Start every 2 seconds to get past the title screens */
    emu_set_pad((i % 120) < 4 ? EMU_BTN_START : 0);
    samples += emu_run_frame(audio);
  }

  px = emu_frame(&w, &h, &pitch);
  printf("frame %dx%d, %ld audio samples over %d frames (%.1f per frame), %.2f fps\n",
         w, h, samples, frames, (double)samples / frames, emu_fps());

  out = fopen(argv[2], "wb");
  if (!out) return 1;
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
  emu_unload();
  return 0;
}
