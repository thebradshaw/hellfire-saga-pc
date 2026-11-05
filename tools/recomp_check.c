/*
 * recomp_check.c - plays the same inputs on the interpreter and on the
 * recompiled code and compares them frame by frame.
 *
 *   hellfire_recomp_check <rom> [frames] [extra_columns]
 *
 * Every frame it hashes work RAM, VRAM, CRAM, VSRAM, Z80 RAM, the 68000
 * registers and the sound output. The first frame that differs is
 * reported with what differs. Inputs: Start taps to get through the
 * menus, then running and jumping, with changes of direction.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "shared.h"
#include "emu.h"
#include "native.h"
#include "widescreen.h"
#include "gamefx.h"

enum { H_RAM, H_VRAM, H_CRAM, H_VSRAM, H_Z80, H_CPU, H_AUDIO, H_COUNT };
static const char *names[H_COUNT] = { "work RAM", "VRAM", "CRAM", "VSRAM", "Z80 RAM", "68000 registers", "sound" };

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
  const uint8_t *b = (const uint8_t *)p;
  while (n--) { h ^= *b++; h *= 16777619u; }
  return h;
}

static uint16_t pad_for(int i)
{
  if (i < 1500) return (i % 120) < 4 ? EMU_BTN_START : 0;
  {
    int phase = (i / 240) % 5;
    uint16_t p = phase == 3 ? EMU_BTN_LEFT : phase == 4 ? 0 : EMU_BTN_RIGHT;
    if ((i % 70) < 12) p |= EMU_BTN_C;
    if ((i / 900) % 4 == 3 && (i % 300) < 30) p |= EMU_BTN_DOWN;
    return p;
  }
}

static int16_t audio[8192];

/* instruction trace for the first trace_frames frames */
typedef struct { uint32_t pc, sr, h; int32_t cyc; } Tr;
static Tr *trace[2];
static size_t ntrace[2], cap = 4000000;
static int cur;
static int trace_hook_interp(void);
static void trace_one(void)
{
  Tr *t;
  if (ntrace[cur] >= cap) return;
  t = &trace[cur][ntrace[cur]++];
  t->pc = m68k.pc; t->sr = m68k_get_sr_ext(); t->cyc = m68k.cycles;
  t->h = fnv(2166136261u, m68k.dar, sizeof(m68k.dar));
}
static int trace_hook_interp(void) { trace_one(); return 0; }

static double run(const char *rom, int frames, int extra, int use_native, uint32_t (*hash)[H_COUNT])
{
  int i;
  clock_t t0;
  NativeInfo info;
  /* registers survive a reset on real hardware too: start both runs alike */
  memset(m68k.dar, 0, sizeof(m68k.dar));
  if (!emu_load(rom, NULL, extra)) { fprintf(stderr, "cannot load %s\n", rom); exit(1); }
  if (extra) widescreen_apply();
  gamefx_reset();
  if (use_native)
  {
    clock_t g0 = clock();
    if (!native_start(NULL, 0, &info)) { fprintf(stderr, "native_start failed: %s\n", info.error); exit(1); }
    printf("native: %u instructions found, %u as C, %u regions, %.1f MB of C, generate %.2fs, compile %.2fs (%.2fs total)\n",
           info.instructions, info.native, info.regions, info.c_bytes / 1048576.0, info.gen_seconds, info.build_seconds,
           (double)(clock() - g0) / CLOCKS_PER_SEC);
  }
  t0 = clock();
  {
    const char *tf = getenv("TRACE_FRAMES");
    cur = use_native;
    if (tf && !trace[cur]) trace[cur] = malloc(cap * sizeof(Tr));
    if (tf) { if (use_native) native_trace = trace_one; else m68k_native_hook = trace_hook_interp; }
  }
  for (i = 0; i < frames; i++)
  {
    if (getenv("TRACE_FRAMES") && i == atoi(getenv("TRACE_FRAMES"))) { native_trace = NULL; if (!use_native) m68k_native_hook = NULL; }
    int n, k;
    uint32_t regs[20];
    emu_set_pad(pad_for(i));
    n = emu_run_frame(audio);
    gamefx_frame();
    for (k = 0; k < 16; k++) regs[k] = m68k.dar[k];
    regs[16] = m68k.pc; regs[17] = m68k_get_sr_ext(); regs[18] = (uint32_t)m68k.cycles; regs[19] = m68k.stopped;
    hash[i][H_RAM]   = fnv(2166136261u, work_ram, sizeof(work_ram));
    hash[i][H_VRAM]  = fnv(2166136261u, vram, sizeof(vram));
    hash[i][H_CRAM]  = fnv(2166136261u, cram, sizeof(cram));
    hash[i][H_VSRAM] = fnv(2166136261u, vsram, sizeof(vsram));
    hash[i][H_Z80]   = fnv(2166136261u, zram, sizeof(zram));
    hash[i][H_CPU]   = fnv(2166136261u, regs, sizeof(regs));
    hash[i][H_AUDIO] = fnv(2166136261u, audio, (size_t)n * 4);
  }
  {
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    if (use_native)
    {
      uint64_t interp, calls;
      uint32_t miss[16];
      size_t nm = native_missed(miss, 16), k;
      native_counters(&interp, &calls);
      printf("native: %llu interpreter steps on ROM/RAM, %llu native calls, %zu ROM addresses missed:",
             (unsigned long long)interp, (unsigned long long)calls, nm);
      for (k = 0; k < nm && k < 16; k++) printf(" %06X", miss[k]);
      printf("\n");
    }
    emu_unload();
    return secs;
  }
}

int main(int argc, char **argv)
{
  int frames = argc > 2 ? atoi(argv[2]) : 3000;
  int extra = argc > 3 ? atoi(argv[3]) : 10;
  uint32_t (*a)[H_COUNT] = calloc((size_t)frames, sizeof(*a));
  uint32_t (*b)[H_COUNT] = calloc((size_t)frames, sizeof(*b));
  double ta, tb;
  int i, k;
  if (argc < 2) { fprintf(stderr, "usage: %s <rom> [frames] [extra_columns]\n", argv[0]); return 2; }
  ta = run(argv[1], frames, extra, 0, a);
  tb = run(argv[1], frames, extra, 1, b);
  printf("interpreter %.2fs, native %.2fs for %d frames\n", ta, tb, frames);
  if (trace[0] && trace[1])
  {
    size_t n = ntrace[0] < ntrace[1] ? ntrace[0] : ntrace[1], j;
    printf("trace: %zu vs %zu steps\n", ntrace[0], ntrace[1]);
    for (j = 0; j < n; j++)
      if (memcmp(&trace[0][j], &trace[1][j], sizeof(Tr)))
      {
        size_t q;
        printf("trace differs at step %zu\n", j);
        for (q = j > 5 ? j - 5 : 0; q <= j + 2 && q < n; q++)
          printf("  %zu interp pc=%06X sr=%04X cyc=%d h=%08X | native pc=%06X sr=%04X cyc=%d h=%08X\n", q,
                 trace[0][q].pc, trace[0][q].sr, trace[0][q].cyc, trace[0][q].h,
                 trace[1][q].pc, trace[1][q].sr, trace[1][q].cyc, trace[1][q].h);
        break;
      }
  }
  for (i = 0; i < frames; i++)
    for (k = 0; k < H_COUNT; k++)
      if (a[i][k] != b[i][k])
      {
        printf("FIRST DIFFERENCE at frame %d: %s\n", i, names[k]);
        return 1;
      }
  printf("all %d frames identical\n", frames);
  return 0;
}
