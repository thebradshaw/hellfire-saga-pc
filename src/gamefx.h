/*
 * gamefx.h - things the port adds on top of HellFire Saga v2.11 by watching
 * the game's RAM or toggling small in-memory ROM patches:
 *
 *  - Black bars that slide in to the original 320px width during boss
 *    fights, cutscenes/text boxes and the title screen, and slide back out.
 *  - A fix for the SEGA screen, where the Miso logo waiting off the right
 *    edge wraps around into the extra pixels on the left.
 *  - An invincibility toggle: nothing hurts you, but pits still kill.
 *
 * Addresses are for HellFire Saga v2.11 only (SHA-1 f8f57b72...).
 */
#ifndef HF_GAMEFX_H
#define HF_GAMEFX_H

#include <stdint.h>

/* Call after loading or resetting the game. */
void gamefx_reset(void);

/* Call after every emulated game frame: reads game state. */
void gamefx_frame(void);

/* Call once per presented frame with the real time since the last one, so
   the bars animate smoothly at any refresh rate. Draws onto a copy of the
   game frame (0xAARRGGBB, pitch in pixels). Does nothing at 320px. */
void gamefx_draw(uint32_t *px, int w, int h, int pitch, double dt);

/* Turns invincibility on or off. Returns 0 if the ROM didn't match the
   expected code (nothing changed). Safe to call while the game runs. */
int  gamefx_set_invincible(int on);

#endif
