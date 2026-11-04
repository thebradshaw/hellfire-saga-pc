#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "settings.h"
#include "hf_file.h"

Settings settings;

static char dir[1024];

const char *settings_dir(void)
{
  if (!dir[0])
  {
    char *p = SDL_GetPrefPath("HellFireSagaPC", "hellfire");
    snprintf(dir, sizeof(dir), "%s", p ? p : "./");
    SDL_free(p);
  }
  return dir;
}

static void defaults(void)
{
  memset(&settings, 0, sizeof(settings));
  settings.widescreen = 1;
  settings.fullscreen = 0;
  settings.vsync      = 1;
  settings.scale_mode = SCALE_INTEGER;
  settings.volume     = 100;
}

void settings_load(void)
{
  char path[1100], line[1200];
  FILE *f;

  defaults();
  snprintf(path, sizeof(path), "%ssettings.ini", settings_dir());
  f = hf_fopen(path, "r");
  if (!f) return;

  while (fgets(line, sizeof(line), f))
  {
    char *eq = strchr(line, '='), *val, *end;
    if (!eq) continue;
    *eq = 0;
    val = eq + 1;
    end = val + strlen(val);
    while (end > val && (end[-1] == '\n' || end[-1] == '\r')) *--end = 0;

    if      (!strcmp(line, "rom_path"))   snprintf(settings.rom_path, sizeof(settings.rom_path), "%s", val);
    else if (!strcmp(line, "widescreen")) settings.widescreen = atoi(val) != 0;
    else if (!strcmp(line, "fullscreen")) settings.fullscreen = atoi(val) != 0;
    else if (!strcmp(line, "vsync"))      settings.vsync      = atoi(val) != 0;
    else if (!strcmp(line, "scale_mode")) settings.scale_mode = atoi(val) ? SCALE_FIT : SCALE_INTEGER;
    else if (!strcmp(line, "volume"))     settings.volume     = SDL_clamp(atoi(val), 0, 100);
  }
  fclose(f);
}

void settings_save(void)
{
  char path[1100];
  FILE *f;

  snprintf(path, sizeof(path), "%ssettings.ini", settings_dir());
  f = hf_fopen(path, "w");
  if (!f) return;
  fprintf(f, "rom_path=%s\n",   settings.rom_path);
  fprintf(f, "widescreen=%d\n", settings.widescreen);
  fprintf(f, "fullscreen=%d\n", settings.fullscreen);
  fprintf(f, "vsync=%d\n",      settings.vsync);
  fprintf(f, "scale_mode=%d\n", settings.scale_mode);
  fprintf(f, "volume=%d\n",     settings.volume);
  fclose(f);
}
