/*
 * main.c - HellFire Saga PC: window, main loop, launcher <-> game switching.
 */
#include <stdio.h>
#include <string.h>
#include <SDL.h>

#include "emu.h"
#include "audio.h"
#include "video.h"
#include "controls.h"
#include "gfx.h"
#include "settings.h"
#include "launcher.h"
#include "widescreen.h"
#include "gamefx.h"
#include "soundtrack.h"

#define WIDE_EXTRA_COLUMNS 10   /* 320 + 10*8 = 400 pixels */

typedef enum { MODE_LAUNCHER, MODE_GAME, MODE_PAUSED } Mode;

static uint32_t launcher_px[LAUNCHER_W * LAUNCHER_H];
static uint32_t pause_px[576 * 240];
static uint32_t game_px[576 * 240];    /* game frame + bars, as shown */
static int      game_w, game_h;
static int16_t  sound[4096 * 2];

static int start_game(void)
{
  char sram_path[1200];
  snprintf(sram_path, sizeof(sram_path), "%shellfire.srm", settings_dir());
  if (!emu_load(settings.rom_path, sram_path, settings.widescreen ? WIDE_EXTRA_COLUMNS : 0))
  {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "HellFire Saga",
                             "The ROM could not be loaded.", video_window());
    return 0;
  }
  /* Make the game draw its level and sprites across the extra columns. */
  if (settings.widescreen) widescreen_apply();
  gamefx_fix_rom();
  gamefx_reset();
  gamefx_set_invincible(settings.invincible);
  game_w = game_h = 0;
  audio_clear();
  return 1;
}

/* Copies the shown game frame so the pause menu can draw over it. */
static Canvas snapshot_frame(void)
{
  Canvas c;
  memcpy(pause_px, game_px, sizeof(pause_px));
  c.px = pause_px; c.w = game_w; c.h = game_h; c.pitch = game_w;
  return c;
}

static void run_one_frame(void)
{
  int n;
  emu_set_pad(controls_pad());
  n = emu_run_frame(sound);
  audio_push(sound, n, settings.volume);
  gamefx_frame();
}

/* Copies the game frame, adds the bars and fixes, and shows it. dt is the
   real time since the last present, for smooth bar motion. */
static void present_game(double dt)
{
  int w, h, pitch, y;
  const uint32_t *px = emu_frame(&w, &h, &pitch);
  if (w > 576) w = 576;
  if (h > 240) h = 240;
  for (y = 0; y < h; y++)
    memcpy(game_px + y * w, (const uint8_t *)px + (size_t)y * pitch, (size_t)w * 4);
  game_w = w; game_h = h;
  gamefx_draw(game_px, w, h, w, dt);
  video_present_crt(game_px, w, h, w * 4, settings.scale_mode, settings.crt);
}

/* The refresh time (seconds), from the last 120 frame times: the median
   rejects missed and doubled refreshes, then the mean of the samples near
   it smooths out timing noise. 0 until there are enough samples. */
static double measure_refresh(double dt)
{
  enum { N = 120 };
  static double hist[N], estimate;
  static int count, pos;
  double sorted[N], sum = 0.0;
  int i, j, used = 0;

  if (dt <= 0.0 || dt > 0.1) return estimate;   /* a stall: not a refresh */
  hist[pos] = dt;
  pos = (pos + 1) % N;
  if (count < N) count++;
  if (count < 8) return estimate = 0.0;
  for (i = 0; i < count; i++)
  {
    double v = hist[i];
    for (j = i; j > 0 && sorted[j - 1] > v; j--) sorted[j] = sorted[j - 1];
    sorted[j] = v;
  }
  for (i = 0; i < count; i++)
    if (hist[i] > sorted[count / 2] * 0.75 && hist[i] < sorted[count / 2] * 1.25) { sum += hist[i]; used++; }
  estimate = sum / used;
  return estimate;
}

int main(int argc, char **argv)
{
  Mode mode = MODE_LAUNCHER;
  Canvas launcher = { launcher_px, LAUNCHER_W, LAUNCHER_H, LAUNCHER_W };
  char *base_path;
  int running = 1;
  Uint64 freq, last;
  double acc = 0.0;
  int tick = 0;
  double refresh = 0.0;      /* measured time between vsynced presents */
  int locked_every = 0;

  if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER) < 0)
  {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  settings_load();

  if (!video_open("Sonic the HellFire Saga", settings.fullscreen, settings.vsync))
  {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "HellFire Saga", SDL_GetError(), NULL);
    return 1;
  }
  audio_open(EMU_SAMPLE_RATE);
  controls_init();

  base_path = SDL_GetBasePath();
  {
    char icon_path[1200];
    Image icon;
    snprintf(icon_path, sizeof(icon_path), "%sassets/icon.png", base_path ? base_path : "./");
    if (image_load(&icon, icon_path)) { video_set_icon(icon.px, icon.w, icon.h); image_free(&icon); }
  }
  launcher_init(base_path ? base_path : "./");
  SDL_free(base_path);

  launcher_refresh_rom();
  if (argc > 1) launcher_offer_rom(argv[1]);
  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

  freq = SDL_GetPerformanceFrequency();
  last = SDL_GetPerformanceCounter();

  while (running)
  {
    SDL_Event e;
    Uint64 now;
    double period, dt;
    int every, paced_by_vsync;

    while (SDL_PollEvent(&e))
    {
      MenuAction a = controls_event(&e);

      if (e.type == SDL_QUIT) running = 0;

      if (e.type == SDL_DROPFILE)
      {
        if (mode == MODE_LAUNCHER) launcher_offer_rom(e.drop.file);
        SDL_free(e.drop.file);
      }

      if (a == MENU_FULLSCREEN)
      {
        settings.fullscreen = !settings.fullscreen;
        video_set_fullscreen(settings.fullscreen);
        settings_save();
        continue;
      }

      switch (mode)
      {
        case MODE_LAUNCHER:
          switch (launcher_event(a))
          {
            case LAUNCH_PLAY:
              if (launcher_rom_ready() && start_game())
              {
                mode = MODE_GAME;
                acc = 0.0;
              }
              break;
            case LAUNCH_QUIT:
              running = 0;
              break;
            case LAUNCH_VIDEO_CHANGED:
              video_set_fullscreen(settings.fullscreen);
              video_set_vsync(settings.vsync);
              break;
            default:
              break;
          }
          break;

        case MODE_GAME:
          if (a == MENU_PAUSE)
          {
            mode = MODE_PAUSED;
            pause_open();
            audio_pause(1);
          }
          break;

        case MODE_PAUSED:
          switch (pause_event(a))
          {
            case PAUSE_RESUME:
              mode = MODE_GAME;
              audio_pause(0);
              acc = 0.0;
              break;
            case PAUSE_INVINCIBLE:
              gamefx_set_invincible(settings.invincible);
              break;
            case PAUSE_RESET:
              emu_reset();
              gamefx_reset();
              mode = MODE_GAME;
              audio_pause(0);
              acc = 0.0;
              break;
            case PAUSE_EXIT:
              emu_unload();
              audio_pause(0);
              audio_clear();
              mode = MODE_LAUNCHER;
              break;
            default:
              break;
          }
          break;
      }
    }

    now = SDL_GetPerformanceCounter();
    dt = (double)(now - last) / (double)freq;
    last = now;

    if (mode == MODE_GAME)
    {
      /* The game itself updates ~60 times a second (59.92). With vsync on,
         measure the real time between refreshes (the reported rate is
         rounded and sometimes 0) and, when it divides into a game frame
         (60, 120, 180, 240 Hz...), run one game frame every N refreshes:
         every game frame is then shown for exactly the same time.
         Other rates (75, 144 Hz...) can't divide evenly; there game frames
         are paced by the refresh clock, which spreads the uneven ones out
         as evenly as possible. Without vsync, a high-resolution timer. */
      period = 1.0 / emu_fps();
      every = 0;
      if (settings.vsync)
      {
        /* The median of recent frame times is the refresh time: missed
           refreshes are rare. Some drivers ignore the vsync request; then
           presents return almost at once and we pace with the timer. */
        refresh = measure_refresh(dt);
        if (refresh > 0.0025)
        {
          /* Lock on within 1.5%; once locked, stay unless 4% off. */
          int n = (int)(period / refresh + 0.5);
          double err = SDL_fabs(n * refresh - period) / period;
          if (n >= 1 && n <= 4 && (err < 0.015 || (n == locked_every && err < 0.04))) every = n;
        }
      }
      paced_by_vsync = settings.vsync && refresh > 0.0025;
      locked_every = every;

      if (every)
      {
        /* A late refresh (dt about 2x) means one was missed: catch up. */
        int late = dt > refresh * 1.6 ? (int)(dt / refresh + 0.5) - 1 : 0;
        if (late > 3) late = 3;
        tick += 1 + late;
        while (tick >= every) { run_one_frame(); tick -= every; }
        acc = 0.0;
      }
      else
      {
        int steps = 0;
        /* With vsync, advance by whole refreshes, not by the jittery
           measured time, so the pattern of frames stays regular. */
        if (paced_by_vsync)
          acc += dt > refresh * 1.6 ? (int)(dt / refresh + 0.5) * refresh : refresh;
        else
          acc += dt;
        if (acc > period * 4) acc = period;   /* after a stall, don't fast-forward */
        while (acc >= period && steps < 4)
        {
          run_one_frame();
          acc -= period;
          steps++;
        }
      }
      present_game(dt);

      if (!paced_by_vsync)
      {
        /* Sleep until the next frame (or half frame at 120) is due. */
        double wait = settings.fps120 ? period / 2 : period - acc;
        if (settings.fps120 && period - acc < wait) wait = period - acc;
        if (wait > 0.002) SDL_Delay((Uint32)((wait - 0.001) * 1000.0));
      }
    }
    else if (mode == MODE_PAUSED)
    {
      Canvas c = snapshot_frame();
      pause_draw(&c);
      video_present_crt(c.px, c.w, c.h, c.w * 4, settings.scale_mode, settings.crt);
      if (!settings.vsync) SDL_Delay(16);
    }
    else
    {
      if (soundtrack_is_open())
      {
        /* The soundtrack player: run the game's sound driver in real time. */
        int steps = 0;
        period = 1.0 / emu_fps();
        acc += dt;
        if (acc > period * 4) acc = period;
        while (acc >= period && steps < 4)
        {
          int n = soundtrack_frame(sound);
          audio_push(sound, n, settings.volume);
          acc -= period;
          steps++;
        }
      }
      launcher_draw(&launcher, SDL_GetTicks());
      video_present(launcher.px, launcher.w, launcher.h, launcher.pitch * 4, SCALE_FIT);
      if (!settings.vsync) SDL_Delay(16);
    }
  }

  soundtrack_close();
  emu_unload();
  settings_save();
  launcher_shutdown();
  controls_shutdown();
  audio_close();
  video_close();
  SDL_Quit();
  return 0;
}
