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
 *  - rows start 64px further left and are 29 blocks long instead of 22
 *  - full-screen redraws start 64px left of the camera, 29 columns
 *  - new columns while scrolling appear 48px (left) and 64px (right)
 *    further out, 24px past each visible edge,
 *    and rows reach from the left column to the right one
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

/* hook_row: entry of the level row drawer. Start rows 64px further left and,
   for screen-width rows (22 blocks), draw 29 blocks instead. That covers
   exactly the blocks from the newest left column (64px left of the camera)
   to the newest right column (384px right of it). Rows any shorter leave
   gaps: a block the right column drew earlier, while it was off the top or
   bottom of the screen, would never be drawn and show stale tiles.
   Only screen rows are moved. Other rows, like the 32-block background
   rows that already span the whole plane, stay as they are: moved left of
   x=0 they read garbage from the level layout (stone blocks floating in
   Malicious Glance's background). For the same reason a screen row never
   starts left of the level.
   The rows go into the game's tile-update buffer at $FFD0B6 (a6 points at
   its end), which is sent to VRAM once a frame. In Sin City and Malicious
   Glance one frame can hold a row, a column and a 32-block background row,
   filling it right up to $FFD370, and the wider row pushes that past the
   end into other game RAM (showing up as junk tiles). So before any row,
   anything already in the buffer is sent to VRAM now, using the game's own
   routine, and the buffer starts over. */
static const uint8_t hook_row[] = {
  0x0C, 0x46, 0x00, 0x15,             /* cmpi.w #$15,d6          */
  0x66, 0x0A,                         /* bne.s  flush            (other rows as they are) */
  0x7C, 0x1C,                         /* moveq  #$1C,d6          */
  0x04, 0x44, 0x00, 0x40,             /* subi.w #$40,d4          */
  0x6A, 0x02,                         /* bpl.s  flush            */
  0x78, 0x00,                         /* moveq  #0,d4            (never left of the level) */
  0xBC, 0xFC, 0xD0, 0xB6,             /* flush: cmpa.w #$D0B6,a6 */
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
#define HOOK_WRAP_X    (HOOK_BASE + 0xE0)

/* hook_wrap_x: the game's unclipped sprite loops keep only the low 9 bits of
   a piece's X, so a piece far left of the screen wraps around to the right.
   At 320 that lands off screen; in a wider picture it shows up in the right
   margin (stray pieces of objects that are behind the player). Pieces outside
   the picture get X = 1 instead, which is always off screen. The limits are
   set by set_margin. */
static const uint8_t hook_wrap_x[] = {
  0x0C, 0x42, 0x00, 0x60,             /* cmpi.w #left,d2         */
  0x6D, 0x0A,                         /* blt.s  off              */
  0x0C, 0x42, 0x01, 0xC0,             /* cmpi.w #right,d2        */
  0x6C, 0x04,                         /* bge.s  off              */
  0x4E, 0x75,                         /* rts                     */
  0x4E, 0x71,                         /* nop                     */
  0x74, 0x01,                         /* off: moveq #1,d2        */
  0x4E, 0x75                          /* rts                     */
};

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
  P(0x29C4, L(0x06, 0x44, 0x01, 0x40), L(0x06, 0x44, 0x01, 0x80), "FG right column"),
  P(0x2ABA, L(0x06, 0x44, 0x01, 0x40), L(0x06, 0x44, 0x01, 0x80), "BG right column"),
  P(0x2B7C, L(0x06, 0x44, 0x01, 0x40), L(0x06, 0x44, 0x01, 0x80), "BG2 right column"),

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

  /* ---- sprites: unclipped pieces no longer wrap from the left edge to the right ---- */
  P(0x1E32, L(0x02, 0x42, 0x01, 0xFF, 0x66, 0x02, 0x52, 0x42), L(JSR_L(HOOK_WRAP_X), 0x4E, 0x71), "pieces A: no wrap"),
  P(0x1E6C, L(0x02, 0x42, 0x01, 0xFF, 0x66, 0x02, 0x52, 0x42), L(JSR_L(HOOK_WRAP_X), 0x4E, 0x71), "pieces B: no wrap"),
  P(0x1EBC, L(0x02, 0x42, 0x01, 0xFF, 0x66, 0x02, 0x52, 0x42), L(JSR_L(HOOK_WRAP_X), 0x4E, 0x71), "pieces C: no wrap"),
  P(0x1F04, L(0x02, 0x42, 0x01, 0xFF, 0x66, 0x02, 0x52, 0x42), L(JSR_L(HOOK_WRAP_X), 0x4E, 0x71), "pieces D: no wrap"),

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

/*
 * HUD (tall mode): the health meter's X goes through a hook that adds a
 * word the frontend sets (RAM $FFDF10, never used by the game), so the
 * meter can sit at the left edge of the wide picture and move back in
 * with the boss bars.
 */
static const uint8_t hook_hud[] = {
  0x30, 0x38, 0xEB, 0x06,             /* move.w ($EB06).w,d0   (original: meter X) */
  0xD0, 0x78, 0xDF, 0x10,             /* add.w  ($DF10).w,d0   */
  0x59, 0x47,                         /* subq.w #4,d7          (original) */
  0x4E, 0x75                          /* rts                   */
};
#define HOOK_HUD (HOOK_BASE + 0xC0)

/*
 * Tall mode: 240 lines instead of 224
 * -----------------------------------
 * The emulator draws 8 more lines above and below the game's 224, so the
 * view is T .. T+239 with T = camera Y - 8. The planes are only 256 pixels
 * (16 blocks) tall and the view now touches all 16 rows of blocks, so
 * there is no spare row left: a row has to be replaced in the very frame
 * it leaves the top and enters the bottom (or the other way round). The
 * game draws a new row when the camera crosses a 16-pixel line; these
 * patches make it test T instead, and draw
 *   scrolling up:   the row holding T          (was camera - 16)
 *   scrolling down: the row holding T + 240    (was camera + 224)
 *   columns and full redraws: 16 blocks from T (was from camera - 16)
 * At the top of a level T stops at 0 (nothing is above it); the frontend
 * then shows 0..239 by drawing the 16 extra lines all below.
 */

/* tall_up ($50): d5 = max(d5 - 8, 0). tall_down ($58): the same + 240. */
static const uint8_t hook_tall[] = {
  0x51, 0x45,                         /* $50: subq.w #8,d5     */
  0x6A, 0x02,                         /*      bpl.s  +2        */
  0x7A, 0x00,                         /*      moveq  #0,d5     */
  0x4E, 0x75,                         /*      rts              */
  0x61, 0xF6,                         /* $58: bsr.s  $50       */
  0x06, 0x45, 0x00, 0xF0,             /*      addi.w #240,d5   */
  0x4E, 0x75                          /*      rts              */
};
#define HOOK_TALL_UP   0x50u
#define HOOK_TALL_DOWN 0x58u

/* Which half of a 16-pixel step T is in (bit 4), for the layers' "crossed
   a block line" tests. Each replaces just the game's "andi.w #$10,dN":
   the zones' own background routines jump straight to that instruction
   (jsr $32CA), so nothing before it may change. The hooks live in the
   reserved 68000 vectors 48-53 ($C0-$D7), within reach of a 4-byte jsr.w. */
static const uint8_t hook_phase[] = {
  0x51, 0x41,                         /* $C0: subq.w #8,d1     (background layers: Y in d1) */
  0x6A, 0x02,                         /*      bpl.s  +2        */
  0x72, 0x00,                         /*      moveq  #0,d1     */
  0x02, 0x41, 0x00, 0x10,             /*      andi.w #$10,d1   (original) */
  0x4E, 0x75,                         /*      rts              */
  0x51, 0x40,                         /* $CC: subq.w #8,d0     (level: Y in d0) */
  0x6A, 0x02,                         /*      bpl.s  +2        */
  0x70, 0x00,                         /*      moveq  #0,d0     */
  0x02, 0x40, 0x00, 0x10,             /*      andi.w #$10,d0   (original) */
  0x4E, 0x75                          /*      rts              */
};
#define HOOK_PHASE_BG 0xC0u
#define HOOK_PHASE_FG 0xCCu
#define TALL_PHASE_FG_SITE 0x3238u    /* andi.w #$10,d0 */

/* Sprites: an object is drawn when it overlaps lines -8..239 (was 0..223),
   and its pieces are kept down to 40 lines above and 240 below. */
static const uint8_t hook_sprite_y[] = {
  0x14, 0x28, 0x00, 0x06,             /* move.b 6(a0),d2       (original) */
  0xD2, 0x42,                         /* add.w  d2,d1          (original) */
  0x50, 0x41,                         /* addq.w #8,d1          */
  0x4E, 0x75                          /* rts                   */
};
#define HOOK_SPRITE_Y (HOOK_BASE + 0xA0)
static const uint32_t tall_sprite_sites[] = { 0x1C68, 0x1D6C };
static const uint32_t tall_piece_sites[] = { 0x1F26, 0x1F72, 0x1FE2, 0x2056 };

static const uint32_t tall_up_sites[] = {     /* subi.w #$10,d5 */
  0x272C, 0x2756, 0x277E, 0x27A4, 0x27F6,     /* full redraws */
  0x294C, 0x2A42, 0x2B04,                     /* rows when scrolling up */
  0x29C8, 0x2ABE, 0x2B80                      /* right columns */
};
static const uint32_t tall_down_sites[] = { 0x2974, 0x2A6A, 0x2B2C };   /* addi.w #$E0,d5 */
static const uint32_t tall_phase_bg_sites[] = { 0x32CA, 0x335E, 0x33EC };   /* andi.w #$10,d1 */

static int tall_check(void)
{
  size_t i;
  for (i = 0; i < sizeof(tall_up_sites) / sizeof(tall_up_sites[0]); i++)
    if (!matches(tall_up_sites[i], (const uint8_t[]){ 0x04, 0x45, 0x00, 0x10 }, 4)) return 0;
  for (i = 0; i < 3; i++)
    if (!matches(tall_down_sites[i], (const uint8_t[]){ 0x06, 0x45, 0x00, 0xE0 }, 4)) return 0;
  for (i = 0; i < 3; i++)
    if (!matches(tall_phase_bg_sites[i], (const uint8_t[]){ 0x02, 0x41, 0x00, 0x10 }, 4)) return 0;
  for (i = HOOK_PHASE_BG; i < HOOK_PHASE_BG + sizeof(hook_phase); i += 4)   /* vectors 48-53 unused */
    if (!matches((uint32_t)i, (const uint8_t[]){ 0x00, 0x3A, 0xD1, 0xE2 }, 4)) return 0;
  for (i = 0; i < 2; i++)
  {
    uint32_t a = tall_sprite_sites[i];
    if (!matches(a, hook_sprite_y, 6)) return 0;
    if (!matches(a + 14, (const uint8_t[]){ 0x06, 0x42, 0x00, 0xE0 }, 4)) return 0;
    if (!matches(a + (i ? 24 : 22), (const uint8_t[]){ 0x06, 0x41, 0x00, 0x80 }, 4)) return 0;
  }
  for (i = 0; i < 4; i++)
    if (!matches(tall_piece_sites[i], (const uint8_t[]){ 0x0C, 0x42, 0x00, 0x60 }, 4)
        || !matches(tall_piece_sites[i] + 6, (const uint8_t[]){ 0x0C, 0x42, 0x01, 0x60 }, 4)) return 0;
  if (!matches(0x1B88, hook_hud, 4) || !matches(0x1B8C, hook_hud + 8, 2)) return 0;
  return matches(TALL_PHASE_FG_SITE, (const uint8_t[]){ 0x02, 0x40, 0x00, 0x10 }, 4);
}

static void tall_apply(void)
{
  size_t i;
  write(HOOK_TALL_UP, hook_tall, sizeof(hook_tall));
  write(HOOK_PHASE_BG, hook_phase, sizeof(hook_phase));
  for (i = 0; i < sizeof(tall_up_sites) / sizeof(tall_up_sites[0]); i++)
    write(tall_up_sites[i], (const uint8_t[]){ 0x4E, 0xB8, 0x00, HOOK_TALL_UP }, 4);
  for (i = 0; i < 3; i++)
    write(tall_down_sites[i], (const uint8_t[]){ 0x4E, 0xB8, 0x00, HOOK_TALL_DOWN }, 4);
  for (i = 0; i < 3; i++)
    write(tall_phase_bg_sites[i], (const uint8_t[]){ 0x4E, 0xB8, 0x00, HOOK_PHASE_BG }, 4);
  write(TALL_PHASE_FG_SITE, (const uint8_t[]){ 0x4E, 0xB8, 0x00, HOOK_PHASE_FG }, 4);
  write(HOOK_SPRITE_Y, hook_sprite_y, sizeof(hook_sprite_y));
  for (i = 0; i < 2; i++)
  {
    uint32_t a = tall_sprite_sites[i];
    write(a, (const uint8_t[]){ JSR_L(HOOK_SPRITE_Y) }, 6);
    write(a + 14, (const uint8_t[]){ 0x06, 0x42, 0x00, 0xF8 }, 4);              /* height: 8 + 224 + 16 */
    write(a + (i ? 24 : 22), (const uint8_t[]){ 0x06, 0x41, 0x00, 0x78 }, 4);   /* take the 8 off again */
  }
  for (i = 0; i < 4; i++)
  {
    write(tall_piece_sites[i], (const uint8_t[]){ 0x0C, 0x42, 0x00, 0x58 }, 4);
    write(tall_piece_sites[i] + 6, (const uint8_t[]){ 0x0C, 0x42, 0x01, 0x70 }, 4);
  }
  write(HOOK_HUD, hook_hud, sizeof(hook_hud));
  write(0x1B88, (const uint8_t[]){ JSR_L(HOOK_HUD) }, 6);
  emu_write_ram8(0xDF10, 0);
  emu_write_ram8(0xDF11, 0);
  /* the left-column hook has its own "subi.w #$10,d5": make it a call too */
  write(HOOK_LEFT_COL + 6, (const uint8_t[]){ 0x4E, 0xB8, 0x00, HOOK_TALL_UP }, 4);
}


/*
 * Edges: things the game only keeps for its own 320x224 screen.
 *  - Rings: the ring manager holds the rings from 8px left of the camera
 *    to 328px right of it, so rings popped in and out inside the wider
 *    picture. Its window grows by the margin on each side.
 *  - Objects: a routine at 0x564E ("is it on screen?") is used by
 *    projectiles, debris and some enemies to delete themselves once they
 *    leave 320x224, so they vanished while still in view. It now tests
 *    the whole picture.
 *  - Tall mode: rings are drawn for 8 more lines above and below, and the
 *    on-screen test covers the extra lines.
 * The hooks live in the reserved 68000 vectors 54-63 ($D8-$FF).
 */
#define HOOK_RING_X     0xD8u   /* move.w ($CFCC).w,d4; subi.w #8+m,d4; rts */
#define HOOK_ONSCREEN_X 0xE2u   /* sub.w ($CFCC).w,d0; addi.w #m,d0; rts    */
#define HOOK_ONSCREEN_Y 0xECu   /* sub.w ($CFD0).w,d1; addq.w #8,d1; rts    */
#define HOOK_RING_Y     0xF4u   /* move.w 4(a3),d4; subq.w #8,d4; rts       */
static const uint32_t ring_window_sites[] = { 0x2484, 0x24BC };   /* move.w ($CFCC).w,d4; subq.w #8,d4 */
static const uint32_t ring_width_sites[]  = { 0x24A0, 0x24E8 };   /* addi.w #$150,d4 */

static int edges_check(void)
{
  size_t i;
  for (i = HOOK_RING_X; i < 0x100; i += 4)                         /* vectors 54-63 unused */
    if (!matches((uint32_t)i, (const uint8_t[]){ 0x00, 0x3A, 0xD1, 0xE2 }, 4)) return 0;
  for (i = 0; i < 2; i++)
    if (!matches(ring_window_sites[i], (const uint8_t[]){ 0x38, 0x38, 0xCF, 0xCC, 0x51, 0x44 }, 6)
        || !matches(ring_width_sites[i], (const uint8_t[]){ 0x06, 0x44, 0x01, 0x50 }, 4)) return 0;
  return matches(0x2636, (const uint8_t[]){ 0x38, 0x2B, 0x00, 0x04, 0x3A, 0x3C, 0x00, 0xF0 }, 8)
      && matches(0x2654, (const uint8_t[]){ 0x06, 0x41, 0x00, 0x78 }, 4)
      && matches(0x5652, (const uint8_t[]){ 0x90, 0x78, 0xCF, 0xCC, 0x6B, 0x1A, 0x0C, 0x40, 0x01, 0x40 }, 10)
      && matches(0x5662, (const uint8_t[]){ 0x92, 0x78, 0xCF, 0xD0, 0x6B, 0x0A, 0x0C, 0x41, 0x00, 0xE0 }, 10);
}

static void edges_apply(int m, int tall)
{
  size_t i;
  write(HOOK_RING_X, (const uint8_t[]){ 0x38, 0x38, 0xCF, 0xCC, 0x04, 0x44, (uint8_t)((8 + m) >> 8), (uint8_t)(8 + m), 0x4E, 0x75 }, 10);
  write(HOOK_ONSCREEN_X, (const uint8_t[]){ 0x90, 0x78, 0xCF, 0xCC, 0x06, 0x40, (uint8_t)(m >> 8), (uint8_t)m, 0x4E, 0x75 }, 10);
  for (i = 0; i < 2; i++)
  {
    /* the flags of the subtraction still decide the "bhi" that follows */
    write(ring_window_sites[i], (const uint8_t[]){ 0x4E, 0xB8, 0x00, HOOK_RING_X, 0x4E, 0x71 }, 6);
    write(ring_width_sites[i] + 2, (const uint8_t[]){ (uint8_t)((0x150 + 2 * m) >> 8), (uint8_t)(0x150 + 2 * m) }, 2);
  }
  write(0x5652, (const uint8_t[]){ 0x4E, 0xB8, 0x00, HOOK_ONSCREEN_X }, 4);
  write(0x5658 + 2, (const uint8_t[]){ (uint8_t)((320 + 2 * m) >> 8), (uint8_t)(320 + 2 * m) }, 2);
  if (!tall) return;
  write(HOOK_ONSCREEN_Y, (const uint8_t[]){ 0x92, 0x78, 0xCF, 0xD0, 0x50, 0x41, 0x4E, 0x75 }, 8);
  write(HOOK_RING_Y, (const uint8_t[]){ 0x38, 0x2B, 0x00, 0x04, 0x51, 0x44, 0x4E, 0x75 }, 8);
  write(0x5662, (const uint8_t[]){ 0x4E, 0xB8, 0x00, HOOK_ONSCREEN_Y }, 4);
  write(0x5668 + 2, (const uint8_t[]){ 0x00, 0xF8 }, 2);           /* -8 .. 240 */
  write(0x2636, (const uint8_t[]){ 0x4E, 0xB8, 0x00, HOOK_RING_Y }, 4);
  write(0x263A + 2, (const uint8_t[]){ 0x01, 0x10 }, 2);           /* rings from 16 above to 256 below the camera */
  write(0x2654 + 2, (const uint8_t[]){ 0x00, 0x70 }, 2);           /* and their sprite Y follows the 8 */
}


/*
 * Wider than 400: the newest left column is drawn 16px further out (80px
 * left of the camera, 12px past the picture's edge), so the rows and full
 * redraws start there too and are one block longer.
 */
static void far_left(void)
{
  static const uint32_t refresh_count[] = { 0x2720, 0x274A, 0x27EA };
  static const uint32_t refresh_start[] = { 0x2728, 0x2752, 0x27F2 };
  size_t i;
  emu_rom_write8(HOOK_LEFT_COL + 5, 0x40);                         /* subi.w #$40,d4 */
  emu_rom_write8(HOOK_ROW_LONG + 7, 0x1D);                         /* moveq  #$1D,d6 */
  emu_rom_write8(HOOK_ROW_LONG + 11, 0x50);                        /* subi.w #$50,d4 */
  for (i = 0; i < 3; i++)
  {
    emu_rom_write8(refresh_count[i] + 1, 0x1D);
    emu_rom_write8(refresh_start[i] + 3, 0x50);
  }
}

/* The sprite patches above are written for a 40px margin (400px wide).
   For another width these bytes are rewritten: margin = pixels added on
   each side. */
static void write16(uint32_t addr, int v)
{
  emu_rom_write8(addr, (uint8_t)(v >> 8));
  emu_rom_write8(addr + 1, (uint8_t)v);
}

static void set_margin(int m)
{
  static const uint32_t shift[]   = { 0x1C4E, 0x1D4E };
  static const uint32_t right[]   = { 0x1C5A, 0x1D5C };
  static const uint32_t unshift[] = { 0x1C60, 0x1D64 };
  static const uint32_t pieces[]  = { 0x1F42, 0x1F9C, 0x200C, 0x2074 };
  size_t i;
  (void)shift;
  write16(HOOK_SPRITE + 4, m);                                   /* addi.w #m,d0 */
  for (i = 0; i < 2; i++) write16(right[i] + 2, 320 + 2 * m);
  for (i = 0; i < 2; i++) write16(unshift[i] + 2, 0x80 - m);
  write16(HOOK_WRAP_X + 2, 96 - m);
  write16(HOOK_WRAP_X + 8, 448 + m);
  for (i = 0; i < 4; i++)
  {
    write16(pieces[i] + 2, 96 - m);
    write16(pieces[i] + 8, 448 + m);
  }
}

int widescreen_apply(int extra_columns, int tall)
{
  size_t i, n = sizeof(patches) / sizeof(patches[0]);

  /* Check everything first: all or nothing. */
  for (i = 0; i < n; i++)
    if (!matches(patches[i].addr, patches[i].expect, patches[i].len)) return -1;
  for (i = 0; i < 0x200; i++)
    if (emu_rom_read8(HOOK_BASE + (uint32_t)i) != 0xFF) return -1;
  for (i = 0x40; i < 0x60; i += 4)                 /* vectors 16-23 unused */
    if (!matches((uint32_t)i, (const uint8_t[]){ 0x00, 0x3A, 0xD1, 0xCA }, 4)) return -1;
  if (tall && !tall_check()) return -1;
  if (!edges_check()) return -1;

  write(HOOK_LEFT_COL, hook_left_col, sizeof(hook_left_col));
  write(HOOK_SPRITE, hook_sprite, sizeof(hook_sprite));
  write(HOOK_WRAP_X, hook_wrap_x, sizeof(hook_wrap_x));
  write(HOOK_ROW_LONG, hook_row, sizeof(hook_row));
  write(HOOK_ROW, (const uint8_t[]){ 0x4E, 0xF9, (HOOK_ROW_LONG >> 24) & 0xFF, (HOOK_ROW_LONG >> 16) & 0xFF,
                                     (HOOK_ROW_LONG >> 8) & 0xFF, HOOK_ROW_LONG & 0xFF }, 6);  /* jmp.l */
  {
    uint8_t maps[320];
    write(SURFACE_MAPS, maps, build_surface_maps(maps));
  }
  for (i = 0; i < n; i++) write(patches[i].addr, patches[i].replace, patches[i].len);
  set_margin(extra_columns * 4);
  if (extra_columns > 10) far_left();
  edges_apply(extra_columns * 4, tall);
  if (tall) tall_apply();
  return (int)n;
}
