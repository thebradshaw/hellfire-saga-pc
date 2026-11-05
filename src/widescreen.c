#include <stddef.h>
#include <stdint.h>
#include "widescreen.h"
#include "emu.h"

/*
 * Screen geometry
 * ---------------
 * The game's camera X is the left edge of its original 320px view. In wide
 * mode the emulator shows 40 more pixels on each side, so the visible range
 * becomes camera-40 .. camera+360. Level art is drawn in 16px blocks:
 *  - rows start 40px further left and are 26 blocks long instead of 22
 *  - full-screen redraws start 64px left of the camera, 29 columns
 *  - new columns while scrolling appear 48px further out on either side
 * The wider rows would overflow the game's tile-update buffer at $FFD0B6
 * in busy frames, so hook_row empties it first (see there).
 *
 * All addresses below are for HellFire Saga v2.11 (SHA-1 f8f57b72...).
 * Each patch lists the bytes it expects to find, so a different ROM is
 * detected and left alone instead of being corrupted.
 */

typedef struct
{
  uint32_t       addr;
  const uint8_t *expect;
  const uint8_t *replace;
  size_t         len;
  const char    *what;
} Patch;

/* Hook routines live in free ROM space past the end of the game. */
#define HOOK_BASE 0x3F0000u

/* hook_row: entry of the level row drawer. Start rows 40px further left and,
   for screen-width rows (22 blocks), draw 26 blocks instead.
   The rows go into the game's tile-update buffer at $FFD0B6 (a6 points at
   its end), which is sent to VRAM once a frame. In Sin City and Malicious
   Glance one frame can hold a row, a column and a 32-block background row,
   filling it right up to $FFD370, and the wider row pushes that past the
   end into other game RAM (showing up as junk tiles). So before any row,
   anything already in the buffer is sent to VRAM now, using the game's own
   routine, and the buffer starts over. */
static const uint8_t hook_row[] = {
  0x04, 0x44, 0x00, 0x28,             /* subi.w #$28,d4          */
  0x0C, 0x46, 0x00, 0x15,             /* cmpi.w #$15,d6          */
  0x66, 0x02,                         /* bne.s  +2               */
  0x7C, 0x19,                         /* moveq  #$19,d6          */
  0xBC, 0xFC, 0xD0, 0xB6,             /* cmpa.w #$D0B6,a6        */
  0x67, 0x10,                         /* beq.s  done             (buffer empty) */
  0x48, 0xE7, 0x90, 0x84,             /* movem.l d0/d3/a0/a5,-(sp) */
  0x4E, 0xB8, 0x2C, 0xD6,             /* jsr    $2CD6.w          (send buffer to VRAM) */
  0x4C, 0xDF, 0x21, 0x09,             /* movem.l (sp)+,d0/d3/a0/a5 */
  0x4D, 0xF8, 0xD0, 0xB6,             /* lea    ($D0B6).w,a6     */
  0xCA, 0x78, 0xD0, 0x18,             /* done: and.w ($D018).w,d5 (original) */
  0x4E, 0x75                          /* rts                     */
};

/* hook_left_col: new column when scrolling left goes 48px further left. */
static const uint8_t hook_left_col[] = {
  0xD8, 0x40,                         /* add.w  d0,d4            (original) */
  0x04, 0x44, 0x00, 0x30,             /* subi.w #$30,d4          */
  0x04, 0x45, 0x00, 0x10,             /* subi.w #$10,d5          (original) */
  0x4E, 0x75                          /* rts                     */
};

#define HOOK_LEFT_COL  (HOOK_BASE + 0x00)
#define HOOK_ROW_LONG  (HOOK_BASE + 0x20)
#define SURFACE_MAPS   (HOOK_BASE + 0x100)   /* see build_surface_maps */

/* hook_sprite: object X relative to the camera, shifted right by 40 so the
   "is it on screen" test covers the extra columns. */
static const uint8_t hook_sprite[] = {
  0x90, 0x53,                         /* sub.w  (a3),d0          (original) */
  0x06, 0x40, 0x00, 0x28,             /* addi.w #40,d0           */
  0x36, 0x00,                         /* move.w d0,d3            (original) */
  0x4E, 0x75                          /* rts                     */
};
/* hook_sprite and a jump to hook_row live in the 68000's unused exception
   vectors 16-23 ($40-$5F, 32 bytes), so a 4-byte jsr.w can reach them. */
#define HOOK_SPRITE 0x40u
#define HOOK_ROW    0x4Au

#define JSR_L(a) 0x4E, 0xB9, (uint8_t)((a) >> 24), (uint8_t)((a) >> 16), (uint8_t)((a) >> 8), (uint8_t)(a)

static int matches(uint32_t addr, const uint8_t *bytes, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++)
    if (emu_rom_read8(addr + (uint32_t)i) != bytes[i]) return 0;
  return 1;
}

static void write(uint32_t addr, const uint8_t *bytes, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++) emu_rom_write8(addr + (uint32_t)i, bytes[i]);
}

#define P(addr, expect, replace, what) \
  { addr, (const uint8_t[])expect, (const uint8_t[])replace, sizeof((const uint8_t[])expect), what }
#define L(...) { __VA_ARGS__ }

static const Patch patches[] = {
  /* ---- level: full-screen redraws (by columns) start 64px left, 29 columns ---- */
  P(0x2720, L(0x7E, 0x15), L(0x7E, 0x1C), "refresh A: column count"),
  P(0x2728, L(0x04, 0x44, 0x00, 0x10), L(0x04, 0x44, 0x00, 0x40), "refresh A: start X"),
  P(0x274A, L(0x7E, 0x15), L(0x7E, 0x1C), "refresh B: column count"),
  P(0x2752, L(0x04, 0x44, 0x00, 0x10), L(0x04, 0x44, 0x00, 0x40), "refresh B: start X"),
  P(0x27EA, L(0x7C, 0x15), L(0x7C, 0x1C), "refresh C: column count"),
  P(0x27F2, L(0x04, 0x44, 0x00, 0x10), L(0x04, 0x44, 0x00, 0x40), "refresh C: start X"),

  /* ---- level: rows drawn while scrolling up/down ---- */
  P(0x28BE, L(0xCA, 0x78, 0xD0, 0x18), L(0x4E, 0xB8, 0x00, HOOK_ROW), "row drawer: wider rows"),

  /* ---- level: columns drawn while scrolling left/right ---- */
  P(0x299A, L(0xD8, 0x40, 0x04, 0x45, 0x00, 0x10), L(JSR_L(HOOK_LEFT_COL)), "FG left column"),
  P(0x2A90, L(0xD8, 0x40, 0x04, 0x45, 0x00, 0x10), L(JSR_L(HOOK_LEFT_COL)), "BG left column"),
  P(0x2B52, L(0xD8, 0x40, 0x04, 0x45, 0x00, 0x10), L(JSR_L(HOOK_LEFT_COL)), "BG2 left column"),
  P(0x29C4, L(0x06, 0x44, 0x01, 0x40), L(0x06, 0x44, 0x01, 0x70), "FG right column"),
  P(0x2ABA, L(0x06, 0x44, 0x01, 0x40), L(0x06, 0x44, 0x01, 0x70), "BG right column"),
  P(0x2B7C, L(0x06, 0x44, 0x01, 0x40), L(0x06, 0x44, 0x01, 0x70), "BG2 right column"),

  /* ---- sprites: on-screen test covers 400px (camera-relative objects) ---- */
  P(0x1C4E, L(0x90, 0x53, 0x36, 0x00), L(0x4E, 0xB8, 0x00, HOOK_SPRITE), "sprites A: shift X"),
  P(0x1C5A, L(0x0C, 0x43, 0x01, 0x40), L(0x0C, 0x43, 0x01, 0x90), "sprites A: right edge"),
  P(0x1C60, L(0x06, 0x40, 0x00, 0x80), L(0x06, 0x40, 0x00, 0x58), "sprites A: unshift X"),
  P(0x1D4E, L(0x90, 0x53, 0x36, 0x00), L(0x4E, 0xB8, 0x00, HOOK_SPRITE), "sprites B: shift X"),
  P(0x1D5C, L(0x0C, 0x43, 0x01, 0x40), L(0x0C, 0x43, 0x01, 0x90), "sprites B: right edge"),
  P(0x1D64, L(0x06, 0x40, 0x00, 0x80), L(0x06, 0x40, 0x00, 0x58), "sprites B: unshift X"),

  /* ---- sprites: per-piece clipping of multi-piece sprites (VDP coords) ---- */
  P(0x1F42, L(0x0C, 0x42, 0x00, 0x60), L(0x0C, 0x42, 0x00, 0x38), "pieces 1: left"),
  P(0x1F48, L(0x0C, 0x42, 0x01, 0xC0), L(0x0C, 0x42, 0x01, 0xE8), "pieces 1: right"),
  P(0x1F9C, L(0x0C, 0x42, 0x00, 0x60), L(0x0C, 0x42, 0x00, 0x38), "pieces 2: left"),
  P(0x1FA2, L(0x0C, 0x42, 0x01, 0xC0), L(0x0C, 0x42, 0x01, 0xE8), "pieces 2: right"),
  P(0x200C, L(0x0C, 0x42, 0x00, 0x60), L(0x0C, 0x42, 0x00, 0x38), "pieces 3: left"),
  P(0x2012, L(0x0C, 0x42, 0x01, 0xC0), L(0x0C, 0x42, 0x01, 0xE8), "pieces 3: right"),
  P(0x2074, L(0x0C, 0x42, 0x00, 0x60), L(0x0C, 0x42, 0x00, 0x38), "pieces 4: left"),
  P(0x207A, L(0x0C, 0x42, 0x01, 0xC0), L(0x0C, 0x42, 0x01, 0xE8), "pieces 4: right"),

  /* ---- water/lava surface (object at 0x144F6): wider sprite strips ---- */
  P(0x144BA, L(0x21, 0x7C, 0x00, 0x01, 0x45, 0x82, 0x00, 0x0C),
             L(0x21, 0x7C, 0x00, (SURFACE_MAPS >> 16) & 0xFF, (SURFACE_MAPS >> 8) & 0xFF, SURFACE_MAPS & 0xFF, 0x00, 0x0C),
             "surface: wider mappings"),
  P(0x1452C, L(0x06, 0x5A, 0x00, 0xC0), L(0x06, 0x5A, 0x01, 0x00), "surface: second strip 256px on"),
};

/*
 * The water/lava surface is one object drawn twice, 192px apart, each copy
 * 192px wide: 384px in all, a little short of 400. These mappings make each
 * copy 256px (reaching 160px left of the object instead of 96) and the code
 * patch above moves the second copy to 256px, so the strip covers the whole
 * wide screen. The 64px wave pattern keeps its phase. Same 7 frames as the
 * original table at 0x14582: frames 0-3 are sparse wave tops, 4-6 solid.
 */
static size_t build_surface_maps(uint8_t *out)
{
  static const uint16_t tiles[7] = { 0x0000, 0x0000, 0x0008, 0x0800, 0x0000, 0x0008, 0x0800 };
  size_t n = 14, f;
  for (f = 0; f < 7; f++)
  {
    int dense = f >= 4, k, count = dense ? 8 : 4;
    out[f * 2] = (uint8_t)(n >> 8);
    out[f * 2 + 1] = (uint8_t)n;
    out[n++] = 0; out[n++] = (uint8_t)count;
    for (k = 0; k < count; k++)
    {
      int x = -160 + k * (dense ? 32 : 64);
      out[n++] = 0xFD;                      /* y -3 */
      out[n++] = 0x0D;                      /* 4x2 tiles */
      out[n++] = (uint8_t)(tiles[f] >> 8);
      out[n++] = (uint8_t)tiles[f];
      out[n++] = (uint8_t)((x >> 8) & 0xFF);
      out[n++] = (uint8_t)(x & 0xFF);
    }
  }
  return n;
}

int widescreen_apply(void)
{
  size_t i, n = sizeof(patches) / sizeof(patches[0]);

  /* Check everything first: all or nothing. */
  for (i = 0; i < n; i++)
    if (!matches(patches[i].addr, patches[i].expect, patches[i].len)) return -1;
  for (i = 0; i < 0x200; i++)
    if (emu_rom_read8(HOOK_BASE + (uint32_t)i) != 0xFF) return -1;
  for (i = 0x40; i < 0x60; i += 4)                 /* vectors 16-23 unused */
    if (!matches((uint32_t)i, (const uint8_t[]){ 0x00, 0x3A, 0xD1, 0xCA }, 4)) return -1;

  write(HOOK_LEFT_COL, hook_left_col, sizeof(hook_left_col));
  write(HOOK_SPRITE, hook_sprite, sizeof(hook_sprite));
  write(HOOK_ROW_LONG, hook_row, sizeof(hook_row));
  write(HOOK_ROW, (const uint8_t[]){ 0x4E, 0xF9, (HOOK_ROW_LONG >> 24) & 0xFF, (HOOK_ROW_LONG >> 16) & 0xFF,
                                     (HOOK_ROW_LONG >> 8) & 0xFF, HOOK_ROW_LONG & 0xFF }, 6);  /* jmp.l */
  {
    uint8_t maps[320];
    write(SURFACE_MAPS, maps, build_surface_maps(maps));
  }
  for (i = 0; i < n; i++) write(patches[i].addr, patches[i].replace, patches[i].len);
  return (int)n;
}
