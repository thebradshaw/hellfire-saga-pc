#include <SDL.h>
#include "audio.h"

static SDL_AudioDeviceID dev;
static int rate;
static double phase;          /* resampler position between input samples */
static int16_t last_l, last_r;

int audio_open(int sample_rate)
{
  SDL_AudioSpec want, have;

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return 0;
  SDL_zero(want);
  want.freq     = sample_rate;
  want.format   = AUDIO_S16SYS;
  want.channels = 2;
  want.samples  = 512;        /* ~10 ms device buffer */
  want.callback = NULL;       /* we use SDL_QueueAudio */

  dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!dev) return 0;
  rate = sample_rate;
  SDL_PauseAudioDevice(dev, 0);
  return 1;
}

void audio_close(void)
{
  if (dev) SDL_CloseAudioDevice(dev);
  dev = 0;
}

void audio_clear(void)
{
  if (dev) SDL_ClearQueuedAudio(dev);
  phase = 0;
}

void audio_pause(int paused)
{
  if (!dev) return;
  if (paused) SDL_ClearQueuedAudio(dev);
  SDL_PauseAudioDevice(dev, paused);
}

void audio_push(const int16_t *in, int frames, int volume)
{
  static int16_t out[8192 * 2];
  const int bytes_per_frame = 4;
  int target, queued, n = 0;
  double step, ratio;

  if (!dev || frames <= 0) return;

  /* Aim for ~2.5 video frames of audio waiting in the queue. */
  target = rate / 24;
  queued = (int)(SDL_GetQueuedAudioSize(dev) / bytes_per_frame);

  /* Way too much queued (e.g. after a hitch): drop it to restore low latency. */
  if (queued > target * 4) { SDL_ClearQueuedAudio(dev); queued = 0; }

  /* Less than target: stretch slightly. More than target: squeeze slightly. */
  ratio = (double)(target - queued) / target;
  if (ratio >  1.0) ratio =  1.0;
  if (ratio < -1.0) ratio = -1.0;
  step = 1.0 / (1.0 + ratio * 0.005);

  /* Linear interpolation resample of this frame's samples. */
  while (phase < frames && n < 8192)
  {
    int i = (int)phase;
    double f = phase - i;
    int l0 = i == 0 ? last_l : in[(i - 1) * 2], r0 = i == 0 ? last_r : in[(i - 1) * 2 + 1];
    int l1 = in[i * 2], r1 = in[i * 2 + 1];
    int l = (int)(l0 + (l1 - l0) * f), r = (int)(r0 + (r1 - r0) * f);
    out[n * 2]     = (int16_t)(l * volume / 100);
    out[n * 2 + 1] = (int16_t)(r * volume / 100);
    n++;
    phase += step;
  }
  phase -= frames;
  last_l = in[(frames - 1) * 2];
  last_r = in[(frames - 1) * 2 + 1];

  SDL_QueueAudio(dev, out, (Uint32)(n * bytes_per_frame));
}
