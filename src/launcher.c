#include <stdio.h>
#include <string.h>
#include "launcher.h"
#include "romcheck.h"
#include "settings.h"

/* Palette pulled from the HellFire Saga title screen. */
#define COL_BG_TOP    RGB(26, 6, 14)
#define COL_BG_BOTTOM RGB(70, 10, 18)
#define COL_TEXT      RGB(240, 222, 210)
#define COL_DIM       RGB(150, 120, 120)
#define COL_ACCENT    RGB(255, 170, 40)
#define COL_BAD       RGB(255, 90, 70)
#define COL_GOOD      RGB(140, 230, 120)

#define LOGO_W 344   /* logo width on the 800x448 launcher canvas */

typedef enum { SCR_SETUP, SCR_MAIN, SCR_OPTIONS, SCR_CREDITS, SCR_SOUNDTRACK } Screen;

static Screen   screen;
static int      cursor;
static Image    sonic, logo;
static RomCheck rom;
static char     notice[256];

/* ---- setup ---------------------------------------------------------------- */

static void load_scaled(Image *out, const char *base, const char *file, int w, int h)
{
  char path[1100];
  Image full;
  snprintf(path, sizeof(path), "%sassets/%s", base, file);
  if (!image_load(&full, path)) { memset(out, 0, sizeof(*out)); return; }
  image_resize(out, &full, w, h);
  image_free(&full);
}

void launcher_init(const char *base_path)
{
  /* SAWhane's Sonic, cut out and faded into the background. */
  load_scaled(&sonic, base_path, "sonic_sawhane.png", 336, 440);
  image_fade(&sonic, 38, 120);
  /* The HellFire Saga lettering from the title screen. */
  load_scaled(&logo, base_path, "logo.png", LOGO_W, LOGO_W * 235 / 578);
  screen = SCR_SETUP;
  cursor = 0;
}

void launcher_shutdown(void)
{
  image_free(&sonic);
  image_free(&logo);
}

void launcher_refresh_rom(void)
{
  romcheck_file(settings.rom_path[0] ? settings.rom_path : NULL, &rom);
  if (rom.status == ROM_OK) { screen = SCR_MAIN; cursor = 0; }
  else                      { screen = SCR_SETUP; }
}

int launcher_rom_ready(void) { return rom.status == ROM_OK; }

void launcher_offer_rom(const char *path)
{
  RomCheck c;
  romcheck_file(path, &c);
  if (c.status == ROM_OK)
  {
    snprintf(settings.rom_path, sizeof(settings.rom_path), "%s", path);
    settings_save();
    rom = c;
    snprintf(notice, sizeof(notice), "ROM verified. You're ready to play!");
    screen = SCR_MAIN;
    cursor = 0;
  }
  else
  {
    rom = c;
    snprintf(notice, sizeof(notice), "%s", romcheck_advice(c.status));
    screen = SCR_SETUP;
  }
}

/* ---- menus ---------------------------------------------------------------- */

static const char *main_items[] = { "PLAY", "SOUNDTRACK", "OPTIONS", "CREDITS", "QUIT" };
#define MAIN_COUNT 5

#define OPT_COUNT 6

static void option_text(int i, char *buf, size_t n)
{
  switch (i)
  {
    case 0: snprintf(buf, n, "SCREEN     %s", settings.widescreen ? "WIDE 400x224" : "ORIGINAL 320x224"); break;
    case 1: snprintf(buf, n, "DISPLAY    %s", settings.fullscreen ? "FULLSCREEN" : "WINDOW"); break;
    case 2: snprintf(buf, n, "SCALING    %s", settings.scale_mode == SCALE_INTEGER ? "PIXEL PERFECT" : "FILL SCREEN"); break;
    case 3: snprintf(buf, n, "VSYNC      %s", settings.vsync ? "ON" : "OFF"); break;
    case 4: snprintf(buf, n, "VOLUME     %d%%", settings.volume); break;
    default: snprintf(buf, n, "BACK"); break;
  }
}

static LauncherResult change_option(int i, int dir)
{
  switch (i)
  {
    case 0: settings.widescreen = !settings.widescreen; break;
    case 1: settings.fullscreen = !settings.fullscreen; settings_save(); return LAUNCH_VIDEO_CHANGED;
    case 2: settings.scale_mode = !settings.scale_mode; break;
    case 3: settings.vsync = !settings.vsync; settings_save(); return LAUNCH_VIDEO_CHANGED;
    case 4:
      settings.volume += dir * 10;
      if (settings.volume < 0)   settings.volume = 0;
      if (settings.volume > 100) settings.volume = 100;
      break;
  }
  settings_save();
  return LAUNCH_NONE;
}

static void move_cursor(int dir, int count)
{
  cursor = (cursor + dir + count) % count;
}

LauncherResult launcher_event(MenuAction a)
{
  if (a == MENU_NONE) return LAUNCH_NONE;

  switch (screen)
  {
    case SCR_SETUP:
      if (a == MENU_PAUSE || a == MENU_BACK) return LAUNCH_QUIT;
      break;

    case SCR_MAIN:
      if (a == MENU_UP)   move_cursor(-1, MAIN_COUNT);
      if (a == MENU_DOWN) move_cursor(+1, MAIN_COUNT);
      if (a == MENU_PAUSE || a == MENU_BACK) { cursor = MAIN_COUNT - 1; break; }
      if (a == MENU_OK)
      {
        notice[0] = 0;
        switch (cursor)
        {
          case 0: return LAUNCH_PLAY;
          case 1: screen = SCR_SOUNDTRACK; break;
          case 2: screen = SCR_OPTIONS; cursor = 0; break;
          case 3: screen = SCR_CREDITS; break;
          case 4: return LAUNCH_QUIT;
        }
      }
      break;

    case SCR_OPTIONS:
      if (a == MENU_UP)   move_cursor(-1, OPT_COUNT);
      if (a == MENU_DOWN) move_cursor(+1, OPT_COUNT);
      if (a == MENU_LEFT  && cursor < OPT_COUNT - 1) return change_option(cursor, -1);
      if (a == MENU_RIGHT && cursor < OPT_COUNT - 1) return change_option(cursor, +1);
      if (a == MENU_OK    && cursor < OPT_COUNT - 1) return change_option(cursor, +1);
      if (a == MENU_BACK || a == MENU_PAUSE || (a == MENU_OK && cursor == OPT_COUNT - 1))
      {
        screen = SCR_MAIN;
        cursor = 2;
      }
      break;

    case SCR_CREDITS:
    case SCR_SOUNDTRACK:
      if (a == MENU_OK || a == MENU_BACK || a == MENU_PAUSE)
      {
        cursor = screen == SCR_CREDITS ? 3 : 1;
        screen = SCR_MAIN;
      }
      break;
  }
  return LAUNCH_NONE;
}

/* ---- drawing -------------------------------------------------------------- */

/* Layout: logo top-left, menus and text below it on the left, and SAWhane's
   Sonic faded into the background on the right. */
#define LEFT_X   32
#define LEFT_W   400
#define CONTENT_Y 178

static void draw_backdrop(Canvas *c, unsigned ticks)
{
  int i;
  gfx_vgradient(c, 0, 0, c->w, c->h, COL_BG_TOP, COL_BG_BOTTOM);

  /* slow drifting embers */
  for (i = 0; i < 48; i++)
  {
    unsigned seed = (unsigned)i * 2654435761u;
    int x = (int)(seed % LAUNCHER_W);
    int speed = 8 + (int)((seed >> 8) % 20);
    int y = LAUNCHER_H - (int)((ticks / 1000.0 * speed + (seed >> 16) % LAUNCHER_H)) % LAUNCHER_H;
    gfx_fill(c, x, y, 2, 2, RGBA(255, 140 + (seed >> 4) % 80, 40, 120));
  }

  gfx_blit(c, &sonic, LAUNCHER_W - sonic.w - 24, LAUNCHER_H - sonic.h + 8);
  gfx_blit(c, &logo, 20, 14);
}

static void draw_heading(Canvas *c, int y, const char *s)
{
  gfx_text_shadow(c, LEFT_X, y, s, 2, COL_ACCENT);
}

static void draw_menu(Canvas *c, const char **items, int count, int top)
{
  int i;
  for (i = 0; i < count; i++)
  {
    int y = top + i * 30;
    uint32_t col = i == cursor ? COL_ACCENT : COL_TEXT;
    if (i == cursor)
    {
      gfx_fill(c, LEFT_X - 8, y - 6, 260, 28, RGBA(255, 120, 30, 50));
      gfx_text_shadow(c, LEFT_X, y, ">", 2, col);
    }
    gfx_text_shadow(c, LEFT_X + 28, y, items[i], 2, col);
  }
}

static void draw_footer(Canvas *c)
{
  char line[160];
  if (notice[0])
    gfx_text_shadow(c, LEFT_X, LAUNCHER_H - 34, notice, 1, COL_GOOD);
  snprintf(line, sizeof(line), "ROM: %s", romcheck_status_name(rom.status));
  gfx_text_shadow(c, LEFT_X, LAUNCHER_H - 18, line, 1, rom.status == ROM_OK ? COL_DIM : COL_BAD);
}

static void draw_hint(Canvas *c, const char *s)
{
  gfx_text_shadow(c, LEFT_X, LAUNCHER_H - 18, s, 1, COL_DIM);
}

static void draw_setup(Canvas *c)
{
  int y = CONTENT_Y;

  draw_heading(c, y, "SET UP YOUR GAME");
  y += 28;
  y = gfx_text_wrapped(c, LEFT_X, y, LEFT_W,
        "This port does not include the game. You need your own "
        "HellFire Saga v2.11 ROM, already patched.", 1, COL_TEXT) + 8;
  y = gfx_text_wrapped(c, LEFT_X, y, LEFT_W,
        "1. Patch your own Sonic 3 ROM with the HellFire Saga v2.11 patch, "
        "using any patching tool.", 1, COL_TEXT) + 4;
  y = gfx_text_wrapped(c, LEFT_X, y, LEFT_W,
        "2. Drag the patched .gen or .bin file onto this window.", 1, COL_TEXT) + 4;
  y = gfx_text_wrapped(c, LEFT_X, y, LEFT_W,
        "3. We check it and remember where it is.", 1, COL_TEXT) + 10;

  if (rom.status != ROM_MISSING || notice[0])
    gfx_text_wrapped(c, LEFT_X, y, LEFT_W, notice[0] ? notice : romcheck_advice(rom.status), 1, COL_BAD);

  draw_hint(c, "Esc: quit");
}

static void draw_options(Canvas *c)
{
  char label[64];
  int i;

  draw_heading(c, CONTENT_Y, "OPTIONS");
  for (i = 0; i < OPT_COUNT; i++)
  {
    int y = CONTENT_Y + 34 + i * 22;
    uint32_t col = i == cursor ? COL_ACCENT : COL_TEXT;
    option_text(i, label, sizeof(label));
    if (i == cursor) gfx_fill(c, LEFT_X - 8, y - 5, 300, 18, RGBA(255, 120, 30, 50));
    gfx_text_shadow(c, LEFT_X + 8, y, label, 1, col);
  }
  draw_hint(c, "Left/Right: change   X/Backspace: back");
}

static void draw_credits(Canvas *c)
{
  int y = CONTENT_Y;
  draw_heading(c, y, "CREDITS");
  y += 28;
  y = gfx_text_wrapped(c, LEFT_X, y, LEFT_W, "Sonic the HellFire Saga by the HellFire Saga team.", 1, COL_TEXT) + 4;
  y = gfx_text_wrapped(c, LEFT_X, y, LEFT_W, "Launcher art by SAWhane.", 1, COL_TEXT) + 4;
  y = gfx_text_wrapped(c, LEFT_X, y, LEFT_W, "Sonic the Hedgehog 3 (c) SEGA. This is an unofficial, "
                                             "free fan project. It does not include any game data.", 1, COL_TEXT) + 4;
  y = gfx_text_wrapped(c, LEFT_X, y, LEFT_W, "Hardware emulation: Genesis Plus GX by Eke-Eke and "
                                             "Charles MacDonald; widescreen fork by the libretro team.", 1, COL_TEXT) + 4;
  gfx_text_wrapped(c, LEFT_X, y, LEFT_W, "Font: font8x8 by Daniel Hepper. Images: stb_image by Sean Barrett.", 1, COL_TEXT);
  draw_hint(c, "Press any button");
}

static void draw_soundtrack(Canvas *c)
{
  draw_heading(c, CONTENT_Y, "SOUNDTRACK");
  gfx_text_wrapped(c, LEFT_X, CONTENT_Y + 34, LEFT_W, "Coming in a later update: every track, rendered from "
                                                     "your own ROM, playable right here.", 1, COL_TEXT);
  draw_hint(c, "Press any button");
}

void launcher_draw(Canvas *c, unsigned ticks)
{
  draw_backdrop(c, ticks);
  switch (screen)
  {
    case SCR_SETUP:      draw_setup(c); break;
    case SCR_MAIN:       draw_menu(c, main_items, MAIN_COUNT, CONTENT_Y + 6); draw_footer(c); break;
    case SCR_OPTIONS:    draw_options(c); break;
    case SCR_CREDITS:    draw_credits(c); break;
    case SCR_SOUNDTRACK: draw_soundtrack(c); break;
  }
}

/* ---- pause menu ----------------------------------------------------------- */

static const char *pause_items[] = { "RESUME", "RESET GAME", "BACK TO LAUNCHER" };
static int pause_cursor;

void pause_open(void) { pause_cursor = 0; }

PauseResult pause_event(MenuAction a)
{
  if (a == MENU_UP)   pause_cursor = (pause_cursor + 2) % 3;
  if (a == MENU_DOWN) pause_cursor = (pause_cursor + 1) % 3;
  if (a == MENU_PAUSE || a == MENU_BACK) return PAUSE_RESUME;
  if (a == MENU_OK)
  {
    if (pause_cursor == 0) return PAUSE_RESUME;
    if (pause_cursor == 1) return PAUSE_RESET;
    return PAUSE_EXIT;
  }
  return PAUSE_NONE;
}

void pause_draw(Canvas *c)
{
  int i, top = c->h / 2 - 30;
  gfx_fill(c, 0, 0, c->w, c->h, RGBA(10, 0, 4, 170));
  gfx_text_centered(c, c->w / 2, top - 24, "PAUSED", 1, COL_ACCENT);
  for (i = 0; i < 3; i++)
  {
    uint32_t col = i == pause_cursor ? COL_ACCENT : COL_TEXT;
    if (i == pause_cursor) gfx_fill(c, c->w / 2 - 80, top + i * 16 - 4, 160, 15, RGBA(255, 120, 30, 60));
    gfx_text_centered(c, c->w / 2, top + i * 16, pause_items[i], 1, col);
  }
}
