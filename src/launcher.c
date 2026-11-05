#include <stdio.h>
#include <string.h>
#include "launcher.h"
#include "romcheck.h"
#include "settings.h"
#include "soundtrack.h"

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
static Font     f_title, f_body, f_small;   /* gothic, see assets/fonts */
static RomCheck rom;
static char     notice[256];
static int      track_top;                  /* first visible soundtrack row */

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

static void load_font(Font *f, const char *base, const char *name)
{
  char path[1100];
  snprintf(path, sizeof(path), "%sassets/fonts/%s", base, name);
  font_load(f, path);
}

void launcher_init(const char *base_path)
{
  /* SAWhane's Sonic, cut out and faded into the background. */
  load_scaled(&sonic, base_path, "sonic_sawhane.png", 336, 440);
  image_fade(&sonic, 7, 120);
  /* The HellFire Saga lettering from the title screen. */
  load_scaled(&logo, base_path, "logo.png", LOGO_W, LOGO_W * 235 / 578);
  load_font(&f_title, base_path, "gothic_title");
  load_font(&f_body,  base_path, "gothic_body");
  load_font(&f_small, base_path, "gothic_small");
  screen = SCR_SETUP;
  cursor = 0;
}

void launcher_shutdown(void)
{
  image_free(&sonic);
  image_free(&logo);
  font_free(&f_title);
  font_free(&f_body);
  font_free(&f_small);
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

char launcher_native_status[64];

static const char *main_items[] = { "Play", "Soundtrack", "Options", "Credits", "Quit" };
#define MAIN_COUNT 5

#define OPT_COUNT 10

static const char *option_label(int i)
{
  static const char *labels[] = { "Screen", "Display", "Scaling", "CRT effect", "Frame rate", "VSync", "Invincible", "Game code", "Volume", "Back" };
  return labels[i];
}

static void option_value(int i, char *buf, size_t n)
{
  switch (i)
  {
    case 0: snprintf(buf, n, "%s", settings.widescreen ? "Wide 400x224" : "Original 320x224"); break;
    case 1: snprintf(buf, n, "%s", settings.fullscreen ? "Fullscreen" : "Window"); break;
    case 2: snprintf(buf, n, "%s", settings.scale_mode == SCALE_INTEGER ? "Pixel perfect" : "Fill screen"); break;
    case 3: snprintf(buf, n, "%s", settings.crt == CRT_FULL ? "CRT (scanlines + RGB mask)" :
                                   settings.crt == CRT_SCANLINES ? "Scanlines" : "Off"); break;
    case 4: snprintf(buf, n, "%s", settings.fps120 ? "120 FPS" : "60 FPS"); break;
    case 5: snprintf(buf, n, "%s", settings.vsync ? "On" : "Off"); break;
    case 6: snprintf(buf, n, "%s", settings.invincible ? "On (spikes, pits and crushing still hurt)" : "Off"); break;
    case 7: snprintf(buf, n, "%s", !settings.native ? "Emulated 68000" :
                                   launcher_native_status[0] ? launcher_native_status : "Native (recompiled C)"); break;
    case 8: snprintf(buf, n, "%d%%", settings.volume); break;
    default: buf[0] = 0; break;
  }
}

static LauncherResult change_option(int i, int dir)
{
  switch (i)
  {
    case 0: settings.widescreen = !settings.widescreen; break;
    case 1: settings.fullscreen = !settings.fullscreen; settings_save(); return LAUNCH_VIDEO_CHANGED;
    case 2: settings.scale_mode = !settings.scale_mode; break;
    case 3: settings.crt = (settings.crt + dir + CRT_COUNT) % CRT_COUNT; break;
    case 4: settings.fps120 = !settings.fps120; break;
    case 5: settings.vsync = !settings.vsync; settings_save(); return LAUNCH_VIDEO_CHANGED;
    case 6: settings.invincible = !settings.invincible; break;
    case 7: settings.native = !settings.native; break;
    case 8:
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
          case 1:
            if (!launcher_rom_ready())
              snprintf(notice, sizeof(notice), "The soundtrack plays from your ROM: set it up first.");
            else if (!soundtrack_open(settings.rom_path))
              snprintf(notice, sizeof(notice), "The soundtrack could not be loaded from your ROM.");
            else
            {
              screen = SCR_SOUNDTRACK;
              cursor = track_top = 0;
            }
            break;
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
      if (a == MENU_OK || a == MENU_BACK || a == MENU_PAUSE)
      {
        cursor = 3;
        screen = SCR_MAIN;
      }
      break;

    case SCR_SOUNDTRACK:
      if (a == MENU_UP)   move_cursor(-1, soundtrack_count());
      if (a == MENU_DOWN) move_cursor(+1, soundtrack_count());
      if (a == MENU_LEFT || a == MENU_RIGHT)
      {
        /* Previous / next track, playing it right away. */
        move_cursor(a == MENU_LEFT ? -1 : +1, soundtrack_count());
        soundtrack_play(cursor);
      }
      if (a == MENU_OK)
      {
        if (soundtrack_playing() == cursor) soundtrack_stop();
        else soundtrack_play(cursor);
      }
      if (a == MENU_BACK || a == MENU_PAUSE)
      {
        soundtrack_close();
        cursor = 1;
        screen = SCR_MAIN;
      }
      break;
  }
  return LAUNCH_NONE;
}

/* ---- drawing -------------------------------------------------------------- */

/* Layout: logo top-left, menus and text below it on the left, and SAWhane's
   Sonic faded into the background on the right. */
/* Menus and text sit in a centered column below the logo. */
#define CENTER_X  (LAUNCHER_W / 2)
#define COLUMN_W  460
#define CONTENT_Y 172

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
  int w = gfx_font_width(&f_title, s) / 2 + 16;
  gfx_font_centered(c, &f_title, CENTER_X, y, s, COL_ACCENT);
  /* A thin ember rule under the heading. */
  gfx_fill(c, CENTER_X - w, y + f_title.line_h - 2, w * 2, 1, RGBA(255, 140, 40, 110));
}

/* Highlight bar behind the selected row. */
static void draw_selection(Canvas *c, int y, int h, int w)
{
  gfx_fill(c, CENTER_X - w / 2, y, w, h, RGBA(255, 120, 30, 46));
  gfx_fill(c, CENTER_X - w / 2, y, w, 1, RGBA(255, 160, 60, 120));
  gfx_fill(c, CENTER_X - w / 2, y + h - 1, w, 1, RGBA(255, 160, 60, 120));
}

static void draw_menu(Canvas *c, const char **items, int count, int top)
{
  int i;
  for (i = 0; i < count; i++)
  {
    int y = top + i * 38;
    uint32_t col = i == cursor ? COL_ACCENT : COL_TEXT;
    if (i == cursor) draw_selection(c, y + 2, 36, 280);
    gfx_font_centered(c, &f_title, CENTER_X, y, items[i], col);
  }
}

/* Fine print in the bottom-right corner, under SAWhane's Sonic. */
static void draw_art_credit(Canvas *c)
{
  gfx_font_right(c, &f_small, LAUNCHER_W - 12, LAUNCHER_H - f_small.line_h - 4,
                 "Sonic artwork by SAWhane", RGBA(200, 170, 160, 170));
}

static void draw_footer(Canvas *c)
{
  char line[160];
  if (notice[0])
    gfx_font_centered(c, &f_small, CENTER_X, LAUNCHER_H - 2 * f_small.line_h - 6, notice, COL_GOOD);
  snprintf(line, sizeof(line), "ROM: %s", romcheck_status_name(rom.status));
  gfx_font_centered(c, &f_small, CENTER_X, LAUNCHER_H - f_small.line_h - 6, line,
                    rom.status == ROM_OK ? COL_DIM : COL_BAD);
}

static void draw_hint(Canvas *c, const char *s)
{
  gfx_font_centered(c, &f_small, CENTER_X, LAUNCHER_H - f_small.line_h - 6, s, COL_DIM);
}

static int paragraph(Canvas *c, int y, const char *s, uint32_t col)
{
  return gfx_font_wrapped(c, &f_body, CENTER_X, y, COLUMN_W, s, col);
}

static void draw_setup(Canvas *c)
{
  int y = CONTENT_Y;

  draw_heading(c, y, "Set Up Your Game");
  y += f_title.line_h + 8;
  y = paragraph(c, y, "This port does not include the game. You need your own "
                      "HellFire Saga v2.11 ROM, already patched.", COL_TEXT) + 8;
  y = paragraph(c, y, "1. Patch your own Sonic 3 ROM with the HellFire Saga v2.11 patch, "
                      "using any patching tool.", COL_TEXT) + 2;
  y = paragraph(c, y, "2. Drag the patched .gen or .bin file onto this window.", COL_TEXT) + 2;
  y = paragraph(c, y, "3. We check it and remember where it is.", COL_TEXT) + 10;

  if (rom.status != ROM_MISSING || notice[0])
    paragraph(c, y, notice[0] ? notice : romcheck_advice(rom.status), COL_BAD);

  draw_hint(c, "Esc: quit");
}

static void draw_options(Canvas *c)
{
  char value[64];
  int i;

  draw_heading(c, CONTENT_Y, "Options");
  for (i = 0; i < OPT_COUNT; i++)
  {
    int y = CONTENT_Y + f_title.line_h + 2 + i * 23;
    uint32_t col = i == cursor ? COL_ACCENT : COL_TEXT;
    if (i == cursor) draw_selection(c, y, 23, 440);
    option_value(i, value, sizeof(value));
    if (value[0])
    {
      y -= 1;
      /* Label right-aligned, value left-aligned, meeting at the center. */
      gfx_font_right(c, &f_body, CENTER_X - 12, y + 1, option_label(i), i == cursor ? COL_ACCENT : COL_DIM);
      gfx_font_text(c, &f_body, CENTER_X + 12, y + 1, value, col);
    }
    else
      gfx_font_centered(c, &f_body, CENTER_X, y + 1, option_label(i), col);
  }
  draw_hint(c, "Left/Right: change    X/Backspace: back");
}

/* Smaller, wider text for the credits list. */
static int fine_print(Canvas *c, int y, const char *s, uint32_t col)
{
  return gfx_font_wrapped(c, &f_small, CENTER_X, y, 560, s, col);
}

static void draw_credits(Canvas *c)
{
  int y = CONTENT_Y;
  draw_heading(c, y, "Credits");
  y += f_title.line_h + 8;
  y = fine_print(c, y, "Sonic the HellFire Saga by the HellFire Saga team.", COL_TEXT) + 6;
  y = fine_print(c, y, "Launcher art by SAWhane.", COL_TEXT) + 6;
  y = fine_print(c, y, "Sonic the Hedgehog 3 (c) SEGA. This is an unofficial, "
                      "free fan project. It does not include any game data.", COL_TEXT) + 6;
  y = fine_print(c, y, "Hardware emulation: Genesis Plus GX by Eke-Eke and "
                      "Charles MacDonald; widescreen fork by the libretro team.", COL_TEXT) + 6;
  fine_print(c, y, "Fonts: UnifrakturCook by j. 'mach' wust and Peter Wiegel; Grenze Gotisch by "
                  "Omnibus-Type; font8x8 by Daniel Hepper. Images: stb_image by Sean Barrett.", COL_TEXT);
  draw_hint(c, "Press any button");
}

#define TRACK_ROWS  8
#define TRACK_ROW_H 24

static void track_label(int i, char *buf, size_t n)
{
  const char *title = soundtrack_title(i);
  int num = soundtrack_number(i);
  if (num && title[0]) snprintf(buf, n, "%02d   %s", num, title);
  else if (num)        snprintf(buf, n, "%02d   Track %d", num, num);
  else                 snprintf(buf, n, "--   %s", title);
}

static void draw_soundtrack(Canvas *c)
{
  int i, y = CONTENT_Y, count = soundtrack_count(), playing = soundtrack_playing();
  int left = CENTER_X - 200;
  char line[160];

  draw_heading(c, y, "Soundtrack");
  y += f_title.line_h + 4;

  /* Keep the cursor in view. */
  if (cursor < track_top) track_top = cursor;
  if (cursor >= track_top + TRACK_ROWS) track_top = cursor - TRACK_ROWS + 1;

  for (i = track_top; i < count && i < track_top + TRACK_ROWS; i++)
  {
    int ry = y + (i - track_top) * TRACK_ROW_H;
    uint32_t col = i == cursor ? COL_ACCENT : (i == playing ? COL_GOOD : COL_TEXT);
    if (i == cursor) draw_selection(c, ry + 1, TRACK_ROW_H - 1, 440);
    track_label(i, line, sizeof(line));
    gfx_font_text(c, &f_body, left, ry + 2, line, col);
    if (i == playing)
    {
      int t = (int)soundtrack_elapsed();
      snprintf(line, sizeof(line), "%d:%02d", t / 60, t % 60);
      gfx_font_right(c, &f_body, CENTER_X + 200, ry + 2, line, COL_GOOD);
    }
  }

  /* Where the window is in the list. */
  snprintf(line, sizeof(line), "%d-%d of %d", track_top + 1,
           track_top + TRACK_ROWS < count ? track_top + TRACK_ROWS : count, count);
  gfx_font_centered(c, &f_small, CENTER_X, y + TRACK_ROWS * TRACK_ROW_H + 2, line, COL_DIM);

  draw_hint(c, "Enter: play / stop    Left/Right: previous / next    Esc: back");
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
  draw_art_credit(c);
}

/* ---- pause menu ----------------------------------------------------------- */

#define PAUSE_COUNT 4
static const char *pause_items[PAUSE_COUNT] = { "Resume", "Invincible", "Reset Game", "Back to Launcher" };
static int pause_cursor;

void pause_open(void) { pause_cursor = 0; }

PauseResult pause_event(MenuAction a)
{
  if (a == MENU_UP)   pause_cursor = (pause_cursor + PAUSE_COUNT - 1) % PAUSE_COUNT;
  if (a == MENU_DOWN) pause_cursor = (pause_cursor + 1) % PAUSE_COUNT;
  if (pause_cursor == 1 && (a == MENU_LEFT || a == MENU_RIGHT || a == MENU_OK))
  {
    settings.invincible = !settings.invincible;
    settings_save();
    return PAUSE_INVINCIBLE;
  }
  if (a == MENU_PAUSE || a == MENU_BACK) return PAUSE_RESUME;
  if (a == MENU_OK)
  {
    if (pause_cursor == 0) return PAUSE_RESUME;
    if (pause_cursor == 2) return PAUSE_RESET;
    return PAUSE_EXIT;
  }
  return PAUSE_NONE;
}

void pause_draw(Canvas *c)
{
  int i, row = f_body.atlas.px ? f_body.line_h + 2 : 16;
  int top = c->h / 2 - row;
  gfx_fill(c, 0, 0, c->w, c->h, RGBA(10, 0, 4, 170));
  gfx_font_centered(c, &f_title, c->w / 2, top - (f_title.atlas.px ? f_title.line_h : 16) - 4, "Paused", COL_ACCENT);
  for (i = 0; i < PAUSE_COUNT; i++)
  {
    int y = top + i * row;
    char label[48];
    uint32_t col = i == pause_cursor ? COL_ACCENT : COL_TEXT;
    if (i == 1) snprintf(label, sizeof(label), "Invincible: %s", settings.invincible ? "On" : "Off");
    else        snprintf(label, sizeof(label), "%s", pause_items[i]);
    if (i == pause_cursor) gfx_fill(c, c->w / 2 - 100, y, 200, row, RGBA(255, 120, 30, 60));
    gfx_font_centered(c, &f_body, c->w / 2, y + 1, label, col);
  }
}

/* ---- mouse ----------------------------------------------------------------- */

/* Which row of a centered list is under (x, y), or -1. */
static int row_at(int x, int y, int top, int step, int h, int count, int w)
{
  int i;
  if (x < CENTER_X - w / 2 || x >= CENTER_X + w / 2 || y < top) return -1;
  i = (y - top) / step;
  if (i >= count || y - top - i * step >= h) return -1;
  return i;
}

LauncherResult launcher_mouse(int x, int y, int button)
{
  int i;

  if (button == 3)
    return screen == SCR_SETUP ? LAUNCH_NONE : launcher_event(MENU_BACK);

  switch (screen)
  {
    case SCR_MAIN:
      i = row_at(x, y, CONTENT_Y + 8, 38, 36, MAIN_COUNT, 280);
      if (i < 0) break;
      cursor = i;
      if (button == 1) return launcher_event(MENU_OK);
      break;

    case SCR_OPTIONS:
      i = row_at(x, y, CONTENT_Y + f_title.line_h + 2, 23, 23, OPT_COUNT, 440);
      if (i < 0) break;
      cursor = i;
      if (button == 1) return launcher_event(MENU_OK);   /* cycles the value, or Back */
      break;

    case SCR_SOUNDTRACK:
      i = row_at(x, y, CONTENT_Y + f_title.line_h + 4, TRACK_ROW_H, TRACK_ROW_H, TRACK_ROWS, 440);
      if (i < 0 || track_top + i >= soundtrack_count()) break;
      cursor = track_top + i;
      if (button == 1) return launcher_event(MENU_OK);   /* play / stop */
      break;

    case SCR_CREDITS:
      if (button == 1) return launcher_event(MENU_OK);
      break;

    case SCR_SETUP:
      break;
  }
  return LAUNCH_NONE;
}

LauncherResult launcher_wheel(int steps)
{
  if (screen == SCR_SOUNDTRACK)
  {
    /* Scroll the list; the cursor follows so it stays in view. */
    int count = soundtrack_count(), max_top = count > TRACK_ROWS ? count - TRACK_ROWS : 0;
    track_top -= steps;
    if (track_top < 0) track_top = 0;
    if (track_top > max_top) track_top = max_top;
    if (cursor < track_top) cursor = track_top;
    if (cursor >= track_top + TRACK_ROWS) cursor = track_top + TRACK_ROWS - 1;
    return LAUNCH_NONE;
  }
  while (steps > 0) { launcher_event(MENU_UP); steps--; }
  while (steps < 0) { launcher_event(MENU_DOWN); steps++; }
  return LAUNCH_NONE;
}

PauseResult pause_mouse(int x, int y, int button, int canvas_w, int canvas_h)
{
  int row = f_body.atlas.px ? f_body.line_h + 2 : 16;
  int top = canvas_h / 2 - row, i;

  if (button == 3) return pause_event(MENU_BACK);
  if (x < canvas_w / 2 - 100 || x >= canvas_w / 2 + 100 || y < top) return PAUSE_NONE;
  i = (y - top) / row;
  if (i >= PAUSE_COUNT) return PAUSE_NONE;
  pause_cursor = i;
  return button == 1 ? pause_event(MENU_OK) : PAUSE_NONE;
}
