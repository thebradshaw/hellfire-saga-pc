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

#define WIDE_EXTRA_COLUMNS 10   /* 320 + 10*8 = 400 pixels */

typedef enum { MODE_LAUNCHER, MODE_GAME, MODE_PAUSED } Mode;

static uint32_t launcher_px[LAUNCHER_W * LAUNCHER_H];
static uint32_t pause_px[576 * 240];
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
  audio_clear();
  return 1;
}

/* Copies the current game frame so the pause menu can draw over it. */
static Canvas snapshot_frame(void)
{
  Canvas c;
  int w, h, pitch, y;
  const uint32_t *px = emu_frame(&w, &h, &pitch);
  if (w > 576) w = 576;
  if (h > 240) h = 240;
  for (y = 0; y < h; y++)
    memcpy(pause_px + y * w, (const uint8_t *)px + (size_t)y * pitch, (size_t)w * 4);
  c.px = pause_px; c.w = w; c.h = h; c.pitch = w;
  return c;
}

static void run_one_frame(void)
{
  int n;
  emu_set_pad(controls_pad());
  n = emu_run_frame(sound);
  audio_push(sound, n, settings.volume);
}

static void present_game(void)
{
  int w, h, pitch;
  const uint32_t *px = emu_frame(&w, &h, &pitch);
  video_present(px, w, h, pitch, settings.scale_mode);
}

int main(int argc, char **argv)
{
  Mode mode = MODE_LAUNCHER;
  Canvas launcher = { launcher_px, LAUNCHER_W, LAUNCHER_H, LAUNCHER_W };
  char *base_path;
  int running = 1;
  Uint64 freq, last;
  double acc = 0.0;

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
    int lockstep;

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
            case PAUSE_RESET:
              emu_reset();
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
      /* With vsync on a ~60 Hz screen, run exactly one game frame per
         refresh: lowest latency, perfectly smooth scrolling. Otherwise pace
         frames with the high-resolution timer. */
      int hz = video_refresh_rate();
      period = 1.0 / emu_fps();
      lockstep = settings.vsync && hz >= 59 && hz <= 61;

      if (lockstep)
      {
        run_one_frame();
      }
      else
      {
        int steps = 0;
        acc += dt;
        if (acc > period * 4) acc = period;   /* after a stall, don't fast-forward */
        while (acc >= period && steps < 4)
        {
          run_one_frame();
          acc -= period;
          steps++;
        }
      }
      present_game();

      if (!settings.vsync)
      {
        /* Sleep until the next frame is due, without oversleeping. */
        double wait = period - acc;
        if (wait > 0.002) SDL_Delay((Uint32)((wait - 0.001) * 1000.0));
      }
    }
    else if (mode == MODE_PAUSED)
    {
      Canvas c = snapshot_frame();
      pause_draw(&c);
      video_present(c.px, c.w, c.h, c.w * 4, settings.scale_mode);
      if (!settings.vsync) SDL_Delay(16);
    }
    else
    {
      launcher_draw(&launcher, SDL_GetTicks());
      video_present(launcher.px, launcher.w, launcher.h, launcher.pitch * 4, SCALE_FIT);
      if (!settings.vsync) SDL_Delay(16);
    }
  }

  emu_unload();
  settings_save();
  launcher_shutdown();
  controls_shutdown();
  audio_close();
  video_close();
  SDL_Quit();
  return 0;
}
