/*
 * gfx.h - tiny software renderer for the launcher and pause menu.
 *
 * Everything is drawn by the CPU into a plain 32-bit pixel buffer, which is
 * then uploaded to one GPU texture. No GUI toolkit, no font library.
 */
#ifndef HF_GFX_H
#define HF_GFX_H

#include <stdint.h>

typedef struct
{
  uint32_t *px;   /* 0xAARRGGBB */
  int w, h;
  int pitch;      /* in pixels */
} Canvas;

typedef struct
{
  uint32_t *px;   /* 0xAARRGGBB, tightly packed */
  int w, h;
} Image;

#define RGB(r, g, b)     (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

void gfx_clear(Canvas *c, uint32_t color);
void gfx_fill(Canvas *c, int x, int y, int w, int h, uint32_t color);      /* alpha-blended */
void gfx_vgradient(Canvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_blit(Canvas *c, const Image *img, int x, int y);                  /* opaque copy */

/* Text uses the public-domain font8x8, scaled by an integer factor. */
int  gfx_text_width(const char *s, int scale);
void gfx_text(Canvas *c, int x, int y, const char *s, int scale, uint32_t color);
void gfx_text_shadow(Canvas *c, int x, int y, const char *s, int scale, uint32_t color);
void gfx_text_centered(Canvas *c, int cx, int y, const char *s, int scale, uint32_t color);
/* Word-wraps to max_w pixels. Returns the y just below the last line. */
int  gfx_text_wrapped(Canvas *c, int x, int y, int max_w, const char *s, int scale, uint32_t color);

/* Loads a PNG/JPEG from disk. Returns 0 on failure (img is zeroed). */
int  image_load(Image *img, const char *path);
/* Resizes with a box filter, for a one-off downscale of launcher art. */
int  image_resize(Image *dst, const Image *src, int w, int h);
/* Multiplies alpha by percent (0-100) and fades the bottom fade_px rows
   out to transparent, for art that sits behind the menus. */
void image_fade(Image *img, int percent, int fade_px);
void image_free(Image *img);

#endif
