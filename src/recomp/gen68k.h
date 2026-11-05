/*
 * gen68k.h - static 68000-to-C recompiler.
 *
 * Finds the game's code in a ROM image and writes it out as C. The C works
 * on the interpreter's own CPU state, so the two hand over at any
 * instruction: code the recompiler did not find, or does not translate,
 * still runs on the interpreter.
 */
#ifndef HF_GEN68K_H
#define HF_GEN68K_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Byte offsets of the CPU fields generated code uses (from the host). */
typedef struct {
  unsigned mm, cycles, refresh, cycle_end, dar, pc, ir;
  unsigned t1, s, x, n, z, v, c, int_mask, aerr;
} Gen68kLayout;

typedef struct {
  const uint8_t  *rom;          /* big-endian image, as the CPU sees it */
  uint32_t        rom_size;
  const uint32_t *entries;      /* extra known code addresses */
  size_t          n_entries;
  unsigned        region_shift; /* log2 of region size in bytes (e.g. 11) */
  Gen68kLayout    layout;
  int           (*op_cycles)(unsigned op);   /* interpreter's base cycles */
  int             weak_scan;    /* also try addresses found in immediates */
  int             single;       /* tests: only the entries themselves, no vectors or flow */
} Gen68kConfig;

typedef struct {
  unsigned instructions;   /* instructions found */
  unsigned native;         /* translated to C */
  unsigned helper;         /* run through the interpreter's handler */
  unsigned regions;
  size_t   bytes_out;
} Gen68kStats;

/* Writes the generated C to out. Returns 0 on success. */
int gen68k_write(const Gen68kConfig *cfg, FILE *out, Gen68kStats *stats);

/* The checksum the runtime uses to tell whether a region's ROM bytes still
   match what was recompiled. Covers [start, start + len). */
uint32_t gen68k_hash(const uint8_t *rom, uint32_t rom_size, uint32_t start, uint32_t len);

/* Bytes after a region's end that its last instructions can reach. */
#define GEN68K_REGION_TAIL 10

/* Version of the generated code's interface with the host. */
#define GEN68K_ABI 1

#endif
