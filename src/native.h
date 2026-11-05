/*
 * native.h - runs the game's recompiled (native C) code in place of the
 * emulated 68000.
 *
 * At launch the recompiler turns the player's ROM into C (src/recomp),
 * the bundled compiler builds it in memory, and the CPU loop calls it.
 * Anything it does not cover still runs on the interpreter, so the game
 * always plays.
 */
#ifndef HF_NATIVE_H
#define HF_NATIVE_H

#include <stddef.h>
#include <stdint.h>
#include "recomp/gen68k.h"

typedef struct {
  unsigned regions;          /* regions loaded */
  unsigned regions_off;      /* turned off because their ROM bytes changed */
  unsigned instructions;     /* instructions recompiled */
  unsigned native;           /* of those, translated to C */
  double   gen_seconds;      /* time to write the C */
  double   build_seconds;    /* time to compile it */
  size_t   c_bytes;
  char     error[256];
} NativeInfo;

/* Fills in the CPU field offsets of this build, for the generator. */
void native_layout(Gen68kLayout *layout);

/* The host functions generated code calls (an HfHost, see rt_prelude.h). */
const void *native_host(void);

/* Recompiles the ROM that is loaded now (with its patches applied) and
   switches the CPU over to it. extra_entries: more code addresses, such as
   ones the interpreter ran last time. Returns 1 on success. */
int  native_start(const uint32_t *extra_entries, size_t n_extra, NativeInfo *info);

/* Uses an already built module: its hf_native_init function. */
int  native_attach(int (*init)(void *cpu, const void *host, const void **regions), NativeInfo *info);

/* Goes back to the interpreter only and frees the module. */
void native_stop(void);

int  native_active(void);

/* Call after changing ROM bytes: regions whose bytes no longer match what
   was recompiled run on the interpreter until the bytes match again. */
void native_rom_changed(uint32_t addr, uint32_t len);

/* Counters since the last call: instructions the interpreter ran on ROM
   addresses (code not recompiled) and how many calls into native code. */
void native_counters(uint64_t *interp_steps, uint64_t *native_calls);

/* ROM addresses the interpreter has run since native_start (code the
   recompiler missed). Writes up to max, returns how many there are. */
size_t native_missed(uint32_t *out, size_t max);

/* debugging: called before every step (native or interpreted) */
extern void (*native_trace)(void);

#endif
