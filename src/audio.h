/*
 * audio.h - low-latency audio output.
 *
 * We push each emulated frame's samples into an SDL queue and keep that
 * queue short (about two frames). Tiny resampling nudges (under 0.5%) keep
 * the queue from slowly draining or growing, so there are no pops and no
 * creeping lag even though the Genesis runs at 59.92 Hz, not 60.
 */
#ifndef HF_AUDIO_H
#define HF_AUDIO_H

#include <stdint.h>

int  audio_open(int sample_rate);
void audio_close(void);
void audio_push(const int16_t *stereo, int frames, int volume);
void audio_clear(void);
void audio_pause(int paused);

#endif
