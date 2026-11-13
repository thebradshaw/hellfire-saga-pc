/*
 * widescreen.h - in-memory patches that make HellFire Saga v2.11 draw its
 * level and objects across the full 400-pixel-wide screen.
 *
 * The game was written for a 320-pixel screen: it only draws the level
 * tiles and sprites that fit in that width. The emulator can show 80 extra
 * pixels (40 each side), but they stay empty unless the game is told to
 * fill them. These patches widen the game's own drawing ranges.
 *
 * Patches go into the in-memory copy of the ROM after it's loaded. The
 * player's file is never modified.
 */
#ifndef HF_WIDESCREEN_H
#define HF_WIDESCREEN_H

/* Applies the patches for a screen 320 + extra_columns*8 pixels wide
   (10 = 400, 13 = 424). With tall set, also for a screen 240 lines high
   (see emu_set_extra_lines). Returns the number applied, or -1 if the ROM didn't
   contain the expected code (nothing is changed in that case). */
int widescreen_apply(int extra_columns, int tall);

#endif
