#include "video.h"
#include "settings.h"

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;
static int tex_w, tex_h;
static int vsync_on;

static int create_renderer(int vsync)
{
  Uint32 flags = SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
  if (texture)  { SDL_DestroyTexture(texture);   texture = NULL; tex_w = tex_h = 0; }
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

void video_present(const uint32_t *px, int w, int h, int pitch_bytes, int scale_mode)
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

  {
    SDL_Rect src = { 0, 0, w, h };
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, &src, &dst);
    SDL_RenderPresent(renderer);
  }
}
