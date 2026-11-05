#include "video.h"
#include "settings.h"

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;
static int tex_w, tex_h;
static int vsync_on;
static SDL_Texture  *crt_tex;          /* CRT overlay, multiplied over the game */
static int crt_w, crt_h, crt_mode;

static int create_renderer(int vsync)
{
  Uint32 flags = SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
  if (texture)  { SDL_DestroyTexture(texture);   texture = NULL; tex_w = tex_h = 0; }
  if (crt_tex)  { SDL_DestroyTexture(crt_tex);   crt_tex = NULL; crt_mode = 0; }
  if (renderer) { SDL_DestroyRenderer(renderer); renderer = NULL; }
  renderer = SDL_CreateRenderer(window, -1, flags);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);   /* software fallback */
  vsync_on = vsync;
  return renderer != NULL;
}

int video_open(const char *title, int fullscreen, int vsync)
{
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) return 0;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* nearest neighbour */

  /* 3x of 400x224 fits a 1366x768 laptop screen. */
  window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            1200, 672, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) return 0;
  SDL_SetWindowMinimumSize(window, 400, 224);
  if (!create_renderer(vsync)) return 0;
  video_set_fullscreen(fullscreen);
  return 1;
}

void video_close(void)
{
  if (texture)  SDL_DestroyTexture(texture);
  if (crt_tex)  SDL_DestroyTexture(crt_tex);
  crt_tex = NULL;
  if (renderer) SDL_DestroyRenderer(renderer);
  if (window)   SDL_DestroyWindow(window);
  texture = NULL; renderer = NULL; window = NULL;
}

void video_set_fullscreen(int on)
{
  SDL_SetWindowFullscreen(window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

void video_set_vsync(int on)
{
  if (on == vsync_on) return;
#if SDL_VERSION_ATLEAST(2, 0, 18)
  if (SDL_RenderSetVSync(renderer, on) == 0) { vsync_on = on; return; }
#endif
  create_renderer(on);
}

int video_refresh_rate(void)
{
  SDL_DisplayMode mode;
  int idx = SDL_GetWindowDisplayIndex(window);
  if (idx < 0 || SDL_GetCurrentDisplayMode(idx, &mode) != 0) return 0;
  return mode.refresh_rate;
}

SDL_Window *video_window(void) { return window; }

void video_set_icon(const uint32_t *px, int w, int h)
{
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom((void *)px, w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return;
  SDL_SetWindowIcon(window, s);
  SDL_FreeSurface(s);
}

/* Builds the overlay for a w x h game image. It has two rows per game line
   (bright, then dark) and, for the full CRT look, three columns per game
   pixel tinted red, green and blue. Drawn with linear filtering and
   multiply blending, the rows become soft scanlines at any window size. */
static void build_crt(int w, int h, int mode)
{
  int ow = mode == CRT_FULL ? w * 3 : 1, oh = h * 2, x, y;
  uint32_t *buf;
  if (crt_tex && crt_w == w && crt_h == h && crt_mode == mode) return;
  if (crt_tex) SDL_DestroyTexture(crt_tex);
  crt_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, ow, oh);
  crt_w = w; crt_h = h; crt_mode = mode;
  if (!crt_tex) return;
  buf = (uint32_t *)SDL_malloc((size_t)ow * oh * 4);
  if (!buf) return;
  for (y = 0; y < oh; y++)
    for (x = 0; x < ow; x++)
    {
      int line = (y & 1) ? 120 : 255;                  /* scanline depth */
      int r = line, g = line, b = line;
      if (mode == CRT_FULL)
      {
        int k = x % 3;                                  /* aperture grille */
        r = r * (k == 0 ? 255 : 180) / 255;
        g = g * (k == 1 ? 255 : 180) / 255;
        b = b * (k == 2 ? 255 : 180) / 255;
      }
      buf[(size_t)y * ow + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
  SDL_UpdateTexture(crt_tex, NULL, buf, ow * 4);
  SDL_free(buf);
  SDL_SetTextureBlendMode(crt_tex, SDL_BLENDMODE_MOD);
#if SDL_VERSION_ATLEAST(2, 0, 12)
  SDL_SetTextureScaleMode(crt_tex, SDL_ScaleModeLinear);
#endif
}

void video_present(const uint32_t *px, int w, int h, int pitch_bytes, int scale_mode)
{
  video_present_crt(px, w, h, pitch_bytes, scale_mode, CRT_OFF);
}

static SDL_Rect last_dst;            /* where the last image was drawn */
static int      last_w, last_h;      /* and its size in pixels         */

int video_to_canvas(int wx, int wy, int *cx, int *cy)
{
  int ww, wh, ow, oh;
  double px, py;
  if (!last_w || !window || !renderer) return 0;
  SDL_GetWindowSize(window, &ww, &wh);
  SDL_GetRendererOutputSize(renderer, &ow, &oh);
  if (ww <= 0 || wh <= 0) return 0;
  px = wx * (double)ow / ww;          /* window points -> output pixels (HiDPI) */
  py = wy * (double)oh / wh;
  if (px < last_dst.x || py < last_dst.y || px >= last_dst.x + last_dst.w || py >= last_dst.y + last_dst.h)
    return 0;
  *cx = (int)((px - last_dst.x) * last_w / last_dst.w);
  *cy = (int)((py - last_dst.y) * last_h / last_dst.h);
  return 1;
}

void video_present_crt(const uint32_t *px, int w, int h, int pitch_bytes, int scale_mode, int crt)
{
  int out_w, out_h, scale;
  SDL_Rect dst;

  if (!texture || w > tex_w || h > tex_h)
  {
    if (texture) SDL_DestroyTexture(texture);
    tex_w = w > 800 ? w : 800;      /* big enough for the launcher canvas too */
    tex_h = h > 448 ? h : 448;
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);
  }

  {
    SDL_Rect src = { 0, 0, w, h };
    SDL_UpdateTexture(texture, &src, px, pitch_bytes);
  }

  SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
  if (scale_mode == SCALE_INTEGER && out_w >= w && out_h >= h)
  {
    scale = SDL_min(out_w / w, out_h / h);
    dst.w = w * scale;
    dst.h = h * scale;
  }
  else
  {
    /* Largest size that keeps square pixels. */
    if ((long)out_w * h > (long)out_h * w) { dst.h = out_h; dst.w = out_h * w / h; }
    else                                    { dst.w = out_w; dst.h = out_w * h / w; }
  }
  dst.x = (out_w - dst.w) / 2;
  dst.y = (out_h - dst.h) / 2;
  last_dst = dst;
  last_w = w;
  last_h = h;

  {
    SDL_Rect src = { 0, 0, w, h };
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, &src, &dst);
    if (crt != CRT_OFF)
    {
      build_crt(w, h, crt);
      if (crt_tex) SDL_RenderCopy(renderer, crt_tex, NULL, &dst);
    }
    SDL_RenderPresent(renderer);
  }
}
