/*
 * launcher.h - the front-end screens: ROM setup, main menu, options,
 * credits, and the in-game pause menu.
 */
#ifndef HF_LAUNCHER_H
#define HF_LAUNCHER_H

#include "gfx.h"
#include "controls.h"

#define LAUNCHER_W 800
#define LAUNCHER_H 448

typedef enum
{
  LAUNCH_NONE = 0,
  LAUNCH_PLAY,
  LAUNCH_QUIT,
  LAUNCH_VIDEO_CHANGED   /* fullscreen / vsync / scale changed in options */
} LauncherResult;

typedef enum
{
  PAUSE_NONE = 0,
  PAUSE_RESUME,
  PAUSE_RESET,
  PAUSE_EXIT,
  PAUSE_INVINCIBLE       /* the invincibility toggle changed */
} PauseResult;

void launcher_init(const char *base_path);
void launcher_shutdown(void);

/* Re-checks the ROM saved in settings. Call at startup. */
void launcher_refresh_rom(void);
int  launcher_rom_ready(void);

/* A file was dragged onto the window (or given on the command line). */
void launcher_offer_rom(const char *path);

LauncherResult launcher_event(MenuAction a);
void           launcher_draw(Canvas *c, unsigned ticks);

/* Pause menu, drawn over the frozen game frame. */
void        pause_open(void);
PauseResult pause_event(MenuAction a);
void        pause_draw(Canvas *c);

#endif
