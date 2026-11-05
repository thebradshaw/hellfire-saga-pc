/*
 * soundtrack.h - the launcher's soundtrack player. It plays the game's own
 * music straight from the player's ROM: the game boots in the background
 * until its sound driver is ready, then its logic is frozen while the sound
 * driver keeps running, and each track is started by its sound ID.
 *
 * Addresses are for HellFire Saga v2.11 only.
 */
#ifndef HF_SOUNDTRACK_H
#define HF_SOUNDTRACK_H

#include <stdint.h>

int         soundtrack_count(void);
const char *soundtrack_title(int i);    /* e.g. "Forest of Decay, Stage I" */
int         soundtrack_number(int i);   /* number in the game's sound test, 0 if none */

/* Loads the ROM and starts its sound driver. Returns 0 on failure. Uses the
   emulator, so it can't run while a game is loaded. */
int  soundtrack_open(const char *rom_path);
void soundtrack_close(void);
int  soundtrack_is_open(void);

void soundtrack_play(int i);
void soundtrack_stop(void);
int  soundtrack_playing(void);          /* track index, or -1 */
double soundtrack_elapsed(void);        /* seconds since the track started */

/* Runs one frame of the sound driver; returns stereo samples written. */
int  soundtrack_frame(int16_t *audio_out);

#endif
