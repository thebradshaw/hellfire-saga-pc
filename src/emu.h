/*
 * emu.h - thin wrapper around the Genesis Plus GX (Wide) core.
 *
 * Everything the rest of the program needs from the emulator goes through
 * here, so the core's globals stay out of the launcher and UI code.
 */
#ifndef HF_EMU_H
#define HF_EMU_H

#include <stdint.h>

#define EMU_SAMPLE_RATE 48000

/* Genesis pad buttons, same bit layout as the core's INPUT_* values. */
enum {
  EMU_BTN_MODE  = 0x0800,
  EMU_BTN_X     = 0x0400,
  EMU_BTN_Y     = 0x0200,
  EMU_BTN_Z     = 0x0100,
  EMU_BTN_UP    = 0x0001,
  EMU_BTN_DOWN  = 0x0002,
  EMU_BTN_LEFT  = 0x0004,
  EMU_BTN_RIGHT = 0x0008,
  EMU_BTN_B     = 0x0010,
  EMU_BTN_C     = 0x0020,
  EMU_BTN_A     = 0x0040,
  EMU_BTN_START = 0x0080
};

/* Loads the ROM file and powers on the console. Returns 1 on success. */
int emu_load(const char *rom_path, const char *sram_path, int extra_columns);

/* Writes battery save data (if the game uses any) and frees the core. */
void emu_unload(void);

int  emu_is_loaded(void);
void emu_reset(void);

/* Sets the held buttons for player 1 (EMU_BTN_* bits). */
void emu_set_pad(uint16_t buttons);

/* Runs exactly one video frame. Returns the number of stereo sample
   frames written to audio_out (which must hold at least 2048 frames). */
int emu_run_frame(int16_t *audio_out);

/* The latest frame: 32-bit XRGB pixels. */
const uint32_t *emu_frame(int *width, int *height, int *pitch_bytes);

/* Frames per second of the loaded game (60 NTSC, 50 PAL). */
double emu_fps(void);

/* Direct read of 68000 work RAM (0xFF0000-0xFFFFFF), for watching game
   state such as "is a boss fight active". */
uint8_t emu_read_ram8(uint32_t address);
uint16_t emu_read_ram16(uint32_t address);   /* big-endian word */
void    emu_write_ram8(uint32_t address, uint8_t value);

/* Read/patch the loaded copy of the ROM in memory (the file is untouched). */
uint8_t emu_rom_read8(uint32_t address);
void    emu_rom_write8(uint32_t address, uint8_t value);

#endif
