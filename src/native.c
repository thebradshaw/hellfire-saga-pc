/*
 * native.c - loads recompiled code and runs it in the 68000's place.
 *
 * The CPU loop (m68k_run in the vendored core) asks native_hook first at
 * every instruction boundary. The hook finds the region holding the PC and
 * calls its function, which runs native code until the line's cycles are
 * used up, the code leaves the region, or it meets an address it has no
 * code for. Returning 0 lets the interpreter run one instruction.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "shared.h"
#include "native.h"
#include "emu.h"
#include "recomp/tcc_build.h"

typedef int (*RegionFn)(void);
typedef struct { uint32_t start, len, hash; RegionFn fn; } Region;
typedef int (*InitFn)(void *cpu, const void *host, const void **regions);

#define ROM_LIMIT 0x400000u

static const Region *regions;
static int           nregions;
static unsigned      shift;
static RegionFn     *table;        /* by pc >> shift, NULL = interpreter */
static uint8_t      *region_on;
static uint8_t       missed[ROM_LIMIT / 2];
static uint64_t      n_interp, n_native;
static void         *module;       /* compiled code, freed by tcc_build_free */

/* ---- host functions for generated code ---------------------------------- */

static void h_exec_op(u32 op)          { m68k_exec_op(op); }
static void h_set_sr(u32 v)            { m68k_set_sr_ext(v); }
static void h_trap(u32 vector)         { m68k_trap_ext(vector); }
static void h_aerr(u32 a, u32 mode, u32 op) { m68k_address_error_ext(a, mode, op); }
static int  h_op_cycles(u32 op)        { return m68k_op_cycles(op); }

typedef struct {
  void (*exec_op)(u32 op);
  void (*set_sr)(u32 v);
  void (*trap)(u32 vector);
  void (*aerr)(u32 addr, u32 mode, u32 op);
  int  (*op_cycles)(u32 op);
} Host;

static const Host host = { h_exec_op, h_set_sr, h_trap, h_aerr, h_op_cycles };

const void *native_host(void) { return &host; }

void native_layout(Gen68kLayout *L)
{
  L->mm        = (unsigned)offsetof(m68ki_cpu_core, memory_map);
  L->cycles    = (unsigned)offsetof(m68ki_cpu_core, cycles);
  L->refresh   = (unsigned)offsetof(m68ki_cpu_core, refresh_cycles);
  L->cycle_end = (unsigned)offsetof(m68ki_cpu_core, cycle_end);
  L->dar       = (unsigned)offsetof(m68ki_cpu_core, dar);
  L->pc        = (unsigned)offsetof(m68ki_cpu_core, pc);
  L->ir        = (unsigned)offsetof(m68ki_cpu_core, ir);
  L->t1        = (unsigned)offsetof(m68ki_cpu_core, t1_flag);
  L->s         = (unsigned)offsetof(m68ki_cpu_core, s_flag);
  L->x         = (unsigned)offsetof(m68ki_cpu_core, x_flag);
  L->n         = (unsigned)offsetof(m68ki_cpu_core, n_flag);
  L->z         = (unsigned)offsetof(m68ki_cpu_core, not_z_flag);
  L->v         = (unsigned)offsetof(m68ki_cpu_core, v_flag);
  L->c         = (unsigned)offsetof(m68ki_cpu_core, c_flag);
  L->int_mask  = (unsigned)offsetof(m68ki_cpu_core, int_mask);
  L->aerr      = (unsigned)offsetof(m68ki_cpu_core, aerr_enabled);
}

/* ---- the hook ------------------------------------------------------------ */

static int native_hook(void)
{
  uint32_t pc = m68k.pc;
  RegionFn fn;
  if (pc < ROM_LIMIT && (fn = table[pc >> shift]) != NULL && fn())
  {
    n_native++;
    return 1;
  }
  if (pc < ROM_LIMIT) missed[pc >> 1] = 1;
  n_interp++;
  return 0;
}

/* ---- regions --------------------------------------------------------------- */

static uint8_t rom_byte(uint32_t a) { return emu_rom_read8(a); }

static uint32_t live_hash(uint32_t start, uint32_t len)
{
  uint32_t h = 2166136261u, i;
  for (i = start; i < start + len && i < ROM_LIMIT; i++)
  {
    h ^= rom_byte(i);
    h *= 16777619u;
  }
  return h;
}

static void check_region(int i)
{
  const Region *r = &regions[i];
  int on = live_hash(r->start, r->len) == r->hash;
  region_on[i] = (uint8_t)on;
  table[r->start >> shift] = on ? r->fn : NULL;
}

void native_rom_changed(uint32_t addr, uint32_t len)
{
  int i;
  if (!table) return;
  for (i = 0; i < nregions; i++)
  {
    const Region *r = &regions[i];
    if (addr < r->start + r->len && addr + len > r->start) check_region(i);
  }
}

int native_attach(InitFn init, NativeInfo *info)
{
  int i;
  const void *list = NULL;
  unsigned (*mm_size)(void) = NULL;
  (void)mm_size;

  m68k_native_hook = NULL;
  nregions = init(&m68k, &host, &list);
  regions = (const Region *)list;
  if (nregions <= 0 || !regions) return 0;

  shift = 31;
  for (i = 1; i < nregions; i++)
  {
    /* regions are aligned blocks; the smallest gap between starts is the size */
    uint32_t d = regions[i].start - regions[i - 1].start;
    unsigned s = 0;
    while ((1u << (s + 1)) <= d && s < 30) s++;
    if (s < shift) shift = s;
  }
  if (nregions == 1) shift = 11;

  free(table);
  free(region_on);
  table = (RegionFn *)calloc((ROM_LIMIT >> shift) + 1, sizeof(RegionFn));
  region_on = (uint8_t *)calloc((size_t)nregions, 1);
  if (!table || !region_on) return 0;
  for (i = 0; i < nregions; i++) check_region(i);

  memset(missed, 0, sizeof(missed));
  n_interp = n_native = 0;
  if (info)
  {
    info->regions = (unsigned)nregions;
    info->regions_off = 0;
    for (i = 0; i < nregions; i++) if (!region_on[i]) info->regions_off++;
  }
  m68k_native_hook = native_hook;
  return 1;
}

static double now_seconds(void)
{
  return (double)clock() / CLOCKS_PER_SEC;
}

/* growable memory "file" for the generated C */
int native_start(const uint32_t *extra, size_t n_extra, NativeInfo *info)
{
  Gen68kConfig cfg;
  Gen68kStats st;
  NativeInfo dummy;
  uint8_t *rom;
  uint32_t size = ROM_LIMIT, a;
  FILE *tmp;
  char *text;
  long len;
  double t0, t1;
  InitFn init;
  void *mod;

  if (!info) info = &dummy;
  memset(info, 0, sizeof(*info));
  native_stop();

  rom = (uint8_t *)malloc(size);
  if (!rom) return 0;
  for (a = 0; a < size; a++) rom[a] = emu_rom_read8(a);

  memset(&cfg, 0, sizeof(cfg));
  cfg.rom = rom;
  cfg.rom_size = size;
  cfg.entries = extra;
  cfg.n_entries = n_extra;
  cfg.region_shift = 11;
  cfg.op_cycles = (int (*)(unsigned))m68k_op_cycles;
  cfg.weak_scan = 1;
  native_layout(&cfg.layout);

  t0 = now_seconds();
  tmp = tmpfile();
  if (!tmp) { free(rom); snprintf(info->error, sizeof(info->error), "no temp file"); return 0; }
  if (gen68k_write(&cfg, tmp, &st) != 0)
  {
    fclose(tmp); free(rom);
    snprintf(info->error, sizeof(info->error), "recompiler failed");
    return 0;
  }
  free(rom);
  len = ftell(tmp);
  text = (char *)malloc((size_t)len + 1);
  if (!text) { fclose(tmp); return 0; }
  rewind(tmp);
  if (fread(text, 1, (size_t)len, tmp) != (size_t)len) { free(text); fclose(tmp); return 0; }
  text[len] = 0;
  fclose(tmp);
  t1 = now_seconds();
  info->gen_seconds = t1 - t0;
  info->c_bytes = (size_t)len;
  info->instructions = st.instructions;
  info->native = st.native;

  mod = tcc_build(text, (void **)&init, info->error, sizeof(info->error));
  free(text);
  info->build_seconds = now_seconds() - t1;
  if (!mod) return 0;
  module = mod;
  if (!native_attach(init, info))
  {
    native_stop();
    snprintf(info->error, sizeof(info->error), "module has no code");
    return 0;
  }
  return 1;
}

void native_stop(void)
{
  m68k_native_hook = NULL;
  free(table);
  table = NULL;
  free(region_on);
  region_on = NULL;
  regions = NULL;
  nregions = 0;
  if (module) tcc_build_free(module);
  module = NULL;
}

int native_active(void)
{
  return m68k_native_hook != NULL;
}

void native_counters(uint64_t *interp_steps, uint64_t *native_calls)
{
  if (interp_steps) *interp_steps = n_interp;
  if (native_calls) *native_calls = n_native;
  n_interp = n_native = 0;
}

size_t native_missed(uint32_t *out, size_t max)
{
  size_t n = 0;
  uint32_t i;
  for (i = 0; i < ROM_LIMIT / 2; i++)
    if (missed[i])
    {
      if (n < max) out[n] = i * 2;
      n++;
    }
  return n;
}
