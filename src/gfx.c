#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "../third_party/font8x8/font8x8_basic.h"

static uint32_t blend(uint32_t dst, uint32_t src)
{
  uint32_t a = src >> 24, ia = 255 - a;
  uint32_t r, g, b;
  if (a == 255) return src;
  if (a == 0)   return dst;
  r = (((src >> 16) & 255) * a + ((dst >> 16) & 255) * ia) / 255;
  g = (((src >> 8)  & 255) * a + ((dst >> 8)  & 255) * ia) / 255;
  b = (( src        & 255) * a + ( dst        & 255) * ia) / 255;
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void gfx_clear(Canvas *c, uint32_t color)
{
  int x, y;
  for (y = 0; y < c->h; y++)
    for (x = 0; x < c->w; x++)
      c->px[y * c->pitch + x] = color;
}

void gfx_fill(Canvas *c, int x, int y, int w, int h, uint32_t color)
{
  int i, j, x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > c->w ? c->w : x + w, y1 = y + h > c->h ? c->h : y + h;
  for (j = y0; j < y1; j++)
  {
    uint32_t *row = c->px + j * c->pitch;
    for (i = x0; i < x1; i++) row[i] = blend(row[i], color);
  }
}

static uint32_t lerp_color(uint32_t a, uint32_t b, int t, int n)
{
  int k;
  uint32_t out = 0;
  for (k = 0; k < 32; k += 8)
  {
    int ca = (a >> k) & 255, cb = (b >> k) & 255;
    out |= (uint32_t)(ca + (cb - ca) * t / (n ? n : 1)) << k;
  }
  return out;
}

void gfx_vgradient(Canvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
  int j;
  for (j = 0; j < h; j++)
    gfx_fill(c, x, y + j, w, 1, lerp_color(top, bottom, j, h - 1));
}

void gfx_blit(Canvas *c, const Image *img, int x, int y)
{
  int j, i;
  if (!img->px) return;
  for (j = 0; j < img->h; j++)
  {
    int dy = y + j;
    if (dy < 0 || dy >= c->h) continue;
    for (i = 0; i < img->w; i++)
    {
      int dx = x + i;
      if (dx < 0 || dx >= c->w) continue;
      c->px[dy * c->pitch + dx] = blend(c->px[dy * c->pitch + dx], img->px[j * img->w + i]);
    }
  }
}

/* ---- text ----------------------------------------------------------------- */

int gfx_text_width(const char *s, int scale)
{
  return (int)strlen(s) * 8 * scale;
}

static void draw_glyph(Canvas *c, int x, int y, unsigned char ch, int scale, uint32_t color)
{
  int row, col, sy, sx;
  const char *g = font8x8_basic[ch & 127];
  for (row = 0; row < 8; row++)
    for (col = 0; col < 8; col++)
      if ((g[row] >> col) & 1)
        for (sy = 0; sy < scale; sy++)
          for (sx = 0; sx < scale; sx++)
          {
            int px = x + col * scale + sx, py = y + row * scale + sy;
            if (px >= 0 && py >= 0 && px < c->w && py < c->h)
              c->px[py * c->pitch + px] = blend(c->px[py * c->pitch + px], color);
          }
}

void gfx_text(Canvas *c, int x, int y, const char *s, int scale, uint32_t color)
{
  for (; *s; s++, x += 8 * scale) draw_glyph(c, x, y, (unsigned char)*s, scale, color);
}

void gfx_text_shadow(Canvas *c, int x, int y, const char *s, int scale, uint32_t color)
{
  gfx_text(c, x + scale, y + scale, s, scale, RGBA(0, 0, 0, 200));
  gfx_text(c, x, y, s, scale, color);
}

void gfx_text_centered(Canvas *c, int cx, int y, const char *s, int scale, uint32_t color)
{
  gfx_text_shadow(c, cx - gfx_text_width(s, scale) / 2, y, s, scale, color);
}

int gfx_text_wrapped(Canvas *c, int x, int y, int max_w, const char *s, int scale, uint32_t color)
{
  char line[256];
  int max_chars = max_w / (8 * scale);
  if (max_chars > 255) max_chars = 255;
  if (max_chars < 1) return y;

  while (*s)
  {
    int n = 0, last_space = -1;
    while (s[n] && s[n] != '\n' && n < max_chars)
    {
      if (s[n] == ' ') last_space = n;
      n++;
    }
    if (s[n] && s[n] != '\n' && last_space > 0) n = last_space;
    memcpy(line, s, (size_t)n);
    line[n] = 0;
    gfx_text_shadow(c, x, y, line, scale, color);
    y += 10 * scale;
    s += n;
    while (*s == ' ') s++;
    if (*s == '\n') s++;
  }
  return y;
}

/* ---- images ---------------------------------------------------------------- */

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO_FAIL_REASON
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../third_party/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

int image_load(Image *img, const char *path)
{
  int w, h, n, i;
  unsigned char *data = stbi_load(path, &w, &h, &n, 4);
  memset(img, 0, sizeof(*img));
  if (!data) return 0;
  img->px = (uint32_t *)malloc((size_t)w * h * 4);
  if (!img->px) { stbi_image_free(data); return 0; }
  for (i = 0; i < w * h; i++)
    img->px[i] = RGBA(data[i*4], data[i*4+1], data[i*4+2], data[i*4+3]);
  img->w = w;
  img->h = h;
  stbi_image_free(data);
  return 1;
}

int image_resize(Image *dst, const Image *src, int w, int h)
{
  int x, y;
  memset(dst, 0, sizeof(*dst));
  if (!src->px || w <= 0 || h <= 0) return 0;
  dst->px = (uint32_t *)malloc((size_t)w * h * 4);
  if (!dst->px) return 0;
  dst->w = w;
  dst->h = h;

  for (y = 0; y < h; y++)
  {
    int sy0 = y * src->h / h, sy1 = (y + 1) * src->h / h;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    for (x = 0; x < w; x++)
    {
      int sx0 = x * src->w / w, sx1 = (x + 1) * src->w / w, sx, sy;
      uint32_t acc[4] = {0, 0, 0, 0}, count = 0;
      if (sx1 <= sx0) sx1 = sx0 + 1;
      for (sy = sy0; sy < sy1; sy++)
        for (sx = sx0; sx < sx1; sx++)
        {
          uint32_t p = src->px[sy * src->w + sx];
          acc[0] += p >> 24; acc[1] += (p >> 16) & 255; acc[2] += (p >> 8) & 255; acc[3] += p & 255;
          count++;
        }
      dst->px[y * w + x] = RGBA(acc[1] / count, acc[2] / count, acc[3] / count, acc[0] / count);
    }
  }
  return 1;
}

void image_fade(Image *img, int percent, int fade_px)
{
  int x, y;
  for (y = 0; y < img->h; y++)
  {
    int k = percent;
    int from_bottom = img->h - 1 - y;
    if (fade_px > 0 && from_bottom < fade_px) k = k * from_bottom / fade_px;
    for (x = 0; x < img->w; x++)
    {
      uint32_t p = img->px[y * img->w + x];
      uint32_t a = (p >> 24) * (uint32_t)k / 100;
      img->px[y * img->w + x] = (a << 24) | (p & 0x00FFFFFF);
    }
  }
}

void image_free(Image *img)
{
  free(img->px);
  memset(img, 0, sizeof(*img));
}
