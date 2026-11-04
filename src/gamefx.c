#include <stddef.h>
#include <string.h>
#include "gamefx.h"
#include "emu.h"

/* ---- game RAM (HellFire Saga v2.11) ------------------------------------- */

#define RAM_GAME_MODE   0xE6B8   /* byte: main loop dispatch, see 0x3D4    */
#define MODE_LEVEL      0x04
#define MODE_TITLE      0x0C     /* the notices and the title screen        */
#define MODE_SEGA       0x28     /* SEGA and Red Miso Studios logos         */
#define RAM_CAM_MIN_X   0xCF52   /* word: camera left limit                 */
#define RAM_CAM_MAX_X   0xCF54   /* word: camera right limit                */
#define RAM_CTRL_LOCK   0xE6ED   /* byte: player input ignored (cutscenes)  */
#define RAM_MISO_SLIDE  0xB646   /* word: Miso logo offset, 0x300 -> 0      */

/* ---- bars ----------------------------------------------------------------- */

#define BAR_IN_FRAMES   12       /* a lock must hold this long to bring bars in */
#define BAR_OUT_FRAMES  40       /* and be gone this long to send them away     */
#define BAR_SECONDS     0.55     /* slide duration                              */

static int    want_run, unwant_run;   /* consecutive game frames */
static int    bars_target;            /* 0 = wide, 1 = bars in   */
static double bars_pos;               /* 0..1, animated          */
static int    hide_miso_wrap;
static int    miso_waiting_seen;      /* the Miso logo has been parked off-screen */
static int    widen_miso;

void gamefx_reset(void)
{
  want_run = unwant_run = 0;
  bars_target = 0;
  bars_pos = 0.0;
  hide_miso_wrap = 0;
  miso_waiting_seen = 0;
  widen_miso = 0;
}

void gamefx_frame(void)
{
  int mode = emu_read_ram8(RAM_GAME_MODE);
  int want = 0, now = 0;

  if (mode == MODE_TITLE)
  {
    /* Notices and title screen: the art is 320px wide. */
    want = now = 1;
  }
  else if (mode == MODE_LEVEL)
  {
    /* Boss arenas pin the camera: its left and right limits meet. */
    int min_x = emu_read_ram16(RAM_CAM_MIN_X);
    int max_x = emu_read_ram16(RAM_CAM_MAX_X);
    int boss  = max_x - min_x < 16;
    /* Cutscenes and text boxes take control away from the player. */
    int scene = emu_read_ram8(RAM_CTRL_LOCK) != 0;
    want = boss || scene;
  }

  if (want) { want_run++; unwant_run = 0; }
  else      { unwant_run++; want_run = 0; }
  if (now || want_run >= BAR_IN_FRAMES) bars_target = 1;
  if (unwant_run >= BAR_OUT_FRAMES)     bars_target = 0;

  /* On the SEGA screen the Miso logo waits just past the right edge; at
     400px it wraps around into the left edge until it slides in. */
  {
    int slide = emu_read_ram16(RAM_MISO_SLIDE);
    hide_miso_wrap = mode == MODE_SEGA && slide >= 0x180;
    if (mode != MODE_SEGA) miso_waiting_seen = 0;
    else if (slide >= 0x180) miso_waiting_seen = 1;
    /* Once the Miso logo has settled, its background art stops 40px short
       of the right edge: stretch the art right of the logo to fill it. */
    widen_miso = mode == MODE_SEGA && miso_waiting_seen && slide == 0;
  }
}

static double ease(double t)   /* smooth start and stop */
{
  return t * t * (3.0 - 2.0 * t);
}

static void shade_column(uint32_t *px, int h, int pitch, int x, unsigned keep)
{
  /* keep = 0..256: how much of the original pixel stays. */
  int y;
  for (y = 0; y < h; y++)
  {
    uint32_t c = px[(size_t)y * pitch + x];
    uint32_t r = ((c >> 16) & 255) * keep >> 8;
    uint32_t g = ((c >> 8) & 255) * keep >> 8;
    uint32_t b = (c & 255) * keep >> 8;
    px[(size_t)y * pitch + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
  }
}

void gamefx_draw(uint32_t *px, int w, int h, int pitch, double dt)
{
  int side = (w - 320) / 2, bar, x, y;
  double step;

  if (side <= 0) return;

  if (hide_miso_wrap)
  {
    /* Carry the background's light stripes out to the edge instead. */
    for (y = 0; y < h; y++)
    {
      uint32_t *row = px + (size_t)y * pitch;
      for (x = 0; x < side; x++) row[x] = row[side];
    }
  }

  if (widen_miso && w == 400)
  {
    /* Stretch columns 292..359 (beam + curved stripes, right of the logo)
       across 292..399. The beam is horizontal, so it simply runs on. */
    enum { SRC0 = 292, SRC1 = 360, DST1 = 400 };
    for (y = 0; y < h; y++)
    {
      uint32_t *row = px + (size_t)y * pitch;
      uint32_t src[SRC1 - SRC0];
      memcpy(src, row + SRC0, sizeof(src));
      for (x = SRC0; x < DST1; x++)
        row[x] = src[(x - SRC0) * (SRC1 - SRC0) / (DST1 - SRC0)];
    }
  }

  step = dt / BAR_SECONDS;
  if (step > 0.25) step = 0.25;   /* after a stall, don't jump */
  if (bars_target && bars_pos < 1.0) { bars_pos += step; if (bars_pos > 1.0) bars_pos = 1.0; }
  if (!bars_target && bars_pos > 0.0) { bars_pos -= step; if (bars_pos < 0.0) bars_pos = 0.0; }

  bar = (int)(ease(bars_pos) * side + 0.5);
  if (bar <= 0) return;
  for (y = 0; y < h; y++)
  {
    uint32_t *row = px + (size_t)y * pitch;
    for (x = 0; x < bar; x++) { row[x] = 0xFF000000u; row[w - 1 - x] = 0xFF000000u; }
  }
  /* A soft inner edge, so the bars read as a frame and not a cut. */
  {
    static const unsigned keep[] = { 110, 170, 220 };
    int i;
    for (i = 0; i < 3 && bar + i < w / 2; i++)
    {
      shade_column(px, h, pitch, bar + i, keep[i]);
      shade_column(px, h, pitch, w - 1 - bar - i, keep[i]);
    }
  }
}

/* ---- invincibility ------------------------------------------------------- */

typedef struct
{
  uint32_t      addr;
  uint8_t       off[6], on[6];
  int           len;
} Toggle;

static const Toggle invincible_patches[] = {
  /* HurtCharacter (0x7EF8): "tst.w $EBC2.w" -> "moveq #1,d0; nop", so its
     next "bne" always takes the no-damage exit. Covers enemies, spikes,
     projectiles and hazards. */
  { 0x7EFE, { 0x4A, 0x78, 0xEB, 0xC2 }, { 0x70, 0x01, 0x4E, 0x71 }, 4 },
  /* KillCharacter (0x7FF0): same trick on its debug-mode check, so being
     crushed and other instant deaths are skipped... */
  { 0x7FF0, { 0x4A, 0x78, 0xE7, 0x2A }, { 0x70, 0x01, 0x4E, 0x71 }, 4 },
  /* ...except falling out of the level, which now jumps just past it. */
  { 0xDE16, { 0x4E, 0xF9, 0x00, 0x00, 0x7F, 0xF0 }, { 0x4E, 0xF9, 0x00, 0x00, 0x7F, 0xF6 }, 6 },
  /* Underwater HP drain (0xD618): "tst.b $EAF8.w" -> "bra.s" to its rts. */
  { 0xD628, { 0x4A, 0x38 }, { 0x60, 0x58 }, 2 },
};

#define TOGGLE_COUNT (sizeof(invincible_patches) / sizeof(invincible_patches[0]))

int gamefx_set_invincible(int on)
{
  size_t i;
  int k;

  /* Every site must hold either its original or its patched bytes. */
  for (i = 0; i < TOGGLE_COUNT; i++)
  {
    const Toggle *t = &invincible_patches[i];
    int is_off = 1, is_on = 1;
    for (k = 0; k < t->len; k++)
    {
      uint8_t b = emu_rom_read8(t->addr + k);
      if (b != t->off[k]) is_off = 0;
      if (b != t->on[k])  is_on = 0;
    }
    if (!is_off && !is_on) return 0;
  }
  for (i = 0; i < TOGGLE_COUNT; i++)
  {
    const Toggle *t = &invincible_patches[i];
    for (k = 0; k < t->len; k++)
      emu_rom_write8(t->addr + k, on ? t->on[k] : t->off[k]);
  }
  return 1;
}
