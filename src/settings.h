/*
 * settings.h - player settings, saved as a small text file in the
 * per-user data folder (%APPDATA% on Windows, ~/.local/share on Linux).
 */
#ifndef HF_SETTINGS_H
#define HF_SETTINGS_H

typedef enum { SCALE_INTEGER = 0, SCALE_FIT = 1 } ScaleMode;

typedef struct
{
  char rom_path[1024];
  int  widescreen;   /* 1 = 400x224, 0 = original 320x224 */
  int  fullscreen;
  int  vsync;
  int  scale_mode;   /* ScaleMode */
  int  volume;       /* 0..100 */
} Settings;

extern Settings settings;

/* Folder for settings and battery saves, ending in a path separator. */
const char *settings_dir(void);

void settings_load(void);
void settings_save(void);

#endif
