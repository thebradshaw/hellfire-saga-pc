/*
 * settings.h - player settings, saved as a small text file in the
 * per-user data folder (%APPDATA% on Windows, ~/.local/share on Linux).
 */
#ifndef HF_SETTINGS_H
#define HF_SETTINGS_H

typedef enum { SCALE_INTEGER = 0, SCALE_FIT = 1 } ScaleMode;
typedef enum { CRT_OFF = 0, CRT_SCANLINES = 1, CRT_FULL = 2, CRT_COUNT } CrtMode;

typedef struct
{
  char rom_path[1024];
  int  widescreen;   /* 1 = 400x224, 0 = original 320x224 */
  int  fullscreen;
  int  vsync;
  int  scale_mode;   /* ScaleMode */
  int  volume;       /* 0..100 */
  int  fps120;       /* 1 = present at 120 Hz (each game frame shown twice) */
  int  invincible;   /* 1 = nothing hurts you; pits still kill */
  int  crt;          /* CrtMode */
  int  native;       /* 1 = run the recompiled (native) game code */
} Settings;

extern Settings settings;

/* Folder for settings and battery saves, ending in a path separator. */
const char *settings_dir(void);

void settings_load(void);
void settings_save(void);

#endif
