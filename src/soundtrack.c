#include <stddef.h>
#include "soundtrack.h"
#include "emu.h"

/* ---- HellFire Saga v2.11 ------------------------------------------------- */

#define RAM_GAME_MODE   0xE6B8
#define MODE_TITLE      0x0C      /* the sound driver takes music from here on */
#define RAM_SOUND_QUEUE 0xDAC8    /* word: next sound ID for the driver      */
#define SOUND_STOP      0x031C
#define WAIT_VSYNC_RTS  0x154E    /* end of the game's wait-for-VBlank loop  */

typedef struct
{
  uint16_t    id;
  int         number;             /* in the game's sound test */
  const char *title;
} Track;

/* In the order of the game's sound test (its list is at ROM 0x37E86), plus
   one track that is in the game but not in the sound test. Titles are only
   given where the game shows where a track plays. */
static const Track tracks[] = {
  { 0x290,  1, "Forest of Decay, Stage I" },
  { 0x294,  2, "Forest of Decay, Stage II" },
  { 0x298,  3, "Forest of Decay, Stage III" },
  { 0x2A8,  4, "Sin City, Stage I" },
  { 0x2AC,  5, "Sin City, Stage II" },
  { 0x2B0,  6, "Sin City, Stage III" },
  { 0x29C,  7, "Malicious Glance, Stage I" },
  { 0x2A0,  8, "Malicious Glance, Stage II" },
  { 0x2A4,  9, "Malicious Glance, Stage III" },
  { 0x2B4, 10, NULL },
  { 0x2B8, 11, NULL },
  { 0x2BC, 12, NULL },
  { 0x2E0, 13, NULL },
  { 0x2DC, 14, NULL },
  { 0x288, 15, NULL },
  { 0x2C0, 16, "Forest of Decay, Boss Arena" },
  { 0x2C4, 17, NULL },
  { 0x28C, 18, "Title Screen" },
  { 0x2C8, 19, NULL },
  { 0x2D8, 20, NULL },
  { 0x2D0, 21, "Intro" },
  { 0x2D4, 22, NULL },
  { 0x2E4, 23, NULL },
  { 0x2E8, 24, NULL },
  { 0x2EC, 25, NULL },
  { 0x2CC,  0, "Not in the sound test" },
};

#define TRACK_COUNT ((int)(sizeof(tracks) / sizeof(tracks[0])))

static int    open_;
static int    playing = -1;
static long   frames_played;
static int    pending = -1;       /* track to start after the stop lands */

int soundtrack_count(void) { return TRACK_COUNT; }

const char *soundtrack_title(int i)
{
  if (i < 0 || i >= TRACK_COUNT) return "";
  return tracks[i].title ? tracks[i].title : "";
}

int soundtrack_number(int i)
{
  return i >= 0 && i < TRACK_COUNT ? tracks[i].number : 0;
}

static void queue(uint16_t id)
{
  emu_write_ram8(RAM_SOUND_QUEUE, (uint8_t)(id >> 8));
  emu_write_ram8(RAM_SOUND_QUEUE + 1, (uint8_t)id);
}

int soundtrack_open(const char *rom_path)
{
  static int16_t scratch[8192];
  int f, after = -1;

  soundtrack_close();
  if (!emu_load(rom_path, NULL, 0)) return 0;

  /* Boot until the title screen has run for half a second (about 10 s of
     game time, a fraction of a second to emulate here). */
  for (f = 0; f < 1800; f++)
  {
    emu_run_frame(scratch);
    if (after < 0 && emu_read_ram8(RAM_GAME_MODE) == MODE_TITLE) after = 30;
    if (after >= 0 && after-- == 0) break;
  }

  /* Freeze the game: its wait-for-VBlank routine now loops forever, while
     the VBlank interrupt keeps running the sound driver. */
  emu_rom_write8(WAIT_VSYNC_RTS, 0x60);      /* bra.s to its start (0x1544) */
  emu_rom_write8(WAIT_VSYNC_RTS + 1, 0xF4);
  queue(SOUND_STOP);

  open_ = 1;
  playing = pending = -1;
  return 1;
}

void soundtrack_close(void)
{
  if (open_) emu_unload();
  open_ = 0;
  playing = pending = -1;
}

int soundtrack_is_open(void) { return open_; }

void soundtrack_play(int i)
{
  if (!open_ || i < 0 || i >= TRACK_COUNT) return;
  queue(SOUND_STOP);              /* the new track starts on the next frame */
  pending = i;
  playing = i;
  frames_played = 0;
}

void soundtrack_stop(void)
{
  if (!open_) return;
  queue(SOUND_STOP);
  playing = pending = -1;
}

int soundtrack_playing(void) { return playing; }

double soundtrack_elapsed(void)
{
  return open_ && playing >= 0 ? frames_played / emu_fps() : 0.0;
}

int soundtrack_frame(int16_t *audio_out)
{
  int n;
  if (!open_) return 0;
  n = emu_run_frame(audio_out);
  if (pending >= 0) { queue(tracks[pending].id); pending = -1; }
  else if (playing >= 0) frames_played++;
  return n;
}
