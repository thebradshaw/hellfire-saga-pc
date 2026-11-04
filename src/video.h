/*
 * video.h - window, GPU texture upload and scaling.
 */
#ifndef HF_VIDEO_H
#define HF_VIDEO_H

#include <stdint.h>
#include <SDL.h>

int  video_open(const char *title, int fullscreen, int vsync);
void video_close(void);

void video_set_fullscreen(int on);
void video_set_vsync(int on);

/* Refresh rate of the monitor the window is on (0 if unknown). */
int  video_refresh_rate(void);

/* Shows a frame of w x h pixels (32-bit XRGB). The image is scaled up to
   the window with square pixels, either by whole numbers (sharpest) or as
   large as fits. Anything outside it is black. */
void video_present(const uint32_t *px, int w, int h, int pitch_bytes, int scale_mode);
/* Same, with a CRT look on top (CrtMode): scanlines, or scanlines plus an
   RGB aperture-grille mask. Each game line gets its own scanline. */
void video_present_crt(const uint32_t *px, int w, int h, int pitch_bytes, int scale_mode, int crt);
/* Sets the window icon from 32-bit ARGB pixels. */
void video_set_icon(const uint32_t *px, int w, int h);

SDL_Window *video_window(void);

#endif
