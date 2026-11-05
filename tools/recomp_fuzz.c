/*
 * recomp_fuzz.c - checks recompiled instructions against the interpreter.
 *
 *   hellfire_recomp_fuzz [seed] [rounds] [opcode]   (Linux, needs gcc on PATH)
 *
 * Each round places a few thousand instructions with random extension
 * words in a 64 KB memory (mirrored over the whole address space),
 * recompiles them one per region, builds that with gcc and loads it. Then
 * every instruction runs once on the interpreter and once as recompiled
 * code, from the same random registers, flags, cycle count and memory, and
 * everything is compared: registers, SR, PC, cycles, memory.
 * By default all valid opcodes are covered; pass an opcode (hex) to test
 * only that one.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shared.h"
#include "native.h"
#include "recomp/gen68k.h"
#include "recomp/m68kdec.h"

/* glue the core expects from a frontend (not used here) */
t_config config;
uint8_t cart_size;
int load_archive(char *f, unsigned char *b, int m, char *e) { (void)f; (void)b; (void)m; (void)e; return 0; }
void osd_input_update(void) {}
unsigned long crc32(unsigned long c, const unsigned char *b, unsigned int l) { (void)b; (void)l; return c; }
/* native.c needs these; the fuzz test builds its own modules */
uint8_t emu_rom_read8(uint32_t a) { (void)a; return 0xff; }
void *tcc_build(const char *s, void **i, char *e, size_t n) { (void)s; (void)i; (void)e; (void)n; return NULL; }
void tcc_build_free(void *h) { (void)h; }

static uint8_t mem[0x10000];      /* interpreter byte order (pairs swapped) */
static uint8_t start_mem[0x10000];
static uint8_t out_mem[0x10000];

static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }

#define SLOT 16
#define FIRST 0x1000
#define PER_ROUND ((0x10000 - FIRST) / SLOT)

typedef struct {
  uint32_t dar[16], pc, sr, usp, isp, stopped;
  int32_t cycles, refresh;
} State;

static void save(State *s)
{
  memcpy(s->dar, m68k.dar, sizeof(s->dar));
  s->pc = m68k.pc;
  s->sr = m68k_get_sr_ext();
  s->usp = m68k.s_flag ? m68k.sp[0] : m68k.dar[15];
  s->isp = m68k.s_flag ? m68k.dar[15] : m68k.sp[4];
  s->stopped = m68k.stopped;
  s->cycles = m68k.cycles;
  s->refresh = m68k.refresh_cycles;
}

static void load_random(State *s)
{
  int i;
  for (i = 0; i < 16; i++) s->dar[i] = rnd();
  /* small shift counts and divisors happen a lot in real code */
  if (rnd() & 1) s->dar[rnd() & 7] = rnd() & 0x3f;
  if (rnd() & 1) s->dar[rnd() & 7] = rnd() & 0xff;
  s->sr = (rnd() & 0x271f) | ((rnd() & 3) ? 0x2000 : 0);
  s->usp = rnd();
  s->isp = rnd();
  s->cycles = (int32_t)(rnd() % 200000);
  s->refresh = (s->cycles / 896 - (int32_t)(rnd() & 1)) * 896;
}

static void apply(const State *s, uint32_t pc)
{
  m68k.int_level = 0;
  m68k.stopped = 0;
  m68k.sp[0] = s->usp;
  m68k.sp[4] = s->isp;
  m68k.s_flag = (s->sr >> 11) & 4;
  memcpy(m68k.dar, s->dar, 15 * sizeof(uint32_t));
  m68k.dar[15] = m68k.s_flag ? s->isp : s->usp;
  m68k_set_sr_ext(s->sr);
  m68k.pc = pc;
  m68k.cycles = s->cycles;
  m68k.refresh_cycles = s->refresh;
  m68k.cycle_end = (uint32_t)s->cycles + 1;
  m68k.aerr_enabled = 0;
}

static int same(const State *a, const State *b)
{
  return !memcmp(a->dar, b->dar, sizeof(a->dar)) && a->pc == b->pc && a->sr == b->sr &&
         a->usp == b->usp && a->isp == b->isp && a->stopped == b->stopped &&
         a->cycles == b->cycles && a->refresh == b->refresh;
}

static void dump(const char *who, const State *s)
{
  int i;
  printf("  %-6s pc=%06X sr=%04X cyc=%d ref=%d stop=%u usp=%08X isp=%08X\n        ", who, s->pc, s->sr, s->cycles, s->refresh, s->stopped, s->usp, s->isp);
  for (i = 0; i < 16; i++) printf("%08X%s", s->dar[i], i == 7 ? "\n        " : " ");
  printf("\n");
}

static uint16_t img16(uint32_t a) { return (uint16_t)((mem[(a & 0xffff) ^ 1] << 8) | mem[((a + 1) & 0xffff) ^ 1]); }
static uint16_t rd_img(void *c, uint32_t a) { (void)c; return img16(a); }

int main(int argc, char **argv)
{
  uint32_t seed = argc > 1 ? (uint32_t)strtoul(argv[1], 0, 0) : 1;
  int rounds = argc > 2 ? atoi(argv[2]) : 4;
  int only = argc > 3 ? (int)strtol(argv[3], 0, 16) : -1;
  static uint16_t ops[65536];
  int nops = 0, op, r, i, fails = 0, tested = 0;
  Gen68kLayout lay;
  native_layout(&lay);

  rng ^= (uint64_t)seed * 0x9E3779B97F4A7C15ull;
  for (op = 0; op < 65536; op++)
    if (m68k_op_valid((unsigned)op) && (only < 0 || op == only)) ops[nops++] = (uint16_t)op;

  m68k_init();
  for (i = 0; i < 256; i++)
  {
    m68k.memory_map[i].base = mem;
    m68k.memory_map[i].read8 = NULL; m68k.memory_map[i].read16 = NULL;
    m68k.memory_map[i].write8 = NULL; m68k.memory_map[i].write16 = NULL;
  }

  for (r = 0; r < rounds; r++)
  {
    static uint32_t entries[PER_ROUND];
    static uint8_t img[0x10000];
    Gen68kConfig cfg;
    Gen68kStats st;
    FILE *f;
    void *so;
    char cmd[512];
    int (*init)(void *, const void *, const void **);
    const struct { uint32_t start, len, hash; int (*fn)(void); } *regs;
    const void *list;
    int nreg, k;

    for (i = 0; i < 0x10000; i++) mem[i] = (uint8_t)rnd();
    for (k = 0; k < PER_ROUND; k++)
    {
      uint32_t a = FIRST + (uint32_t)k * SLOT;
      uint16_t o = ops[(only >= 0) ? 0 : (size_t)(((uint64_t)r * PER_ROUND + (uint64_t)k) * 7919u % (uint64_t)nops)];
      if (only < 0 && r == 0) o = ops[(size_t)k * (size_t)nops / PER_ROUND];
      mem[a ^ 1] = (uint8_t)(o >> 8);
      mem[(a + 1) ^ 1] = (uint8_t)o;
      entries[k] = a;
    }
    for (i = 0; i < 0x10000; i += 2) { img[i] = (uint8_t)(img16((uint32_t)i) >> 8); img[i + 1] = (uint8_t)img16((uint32_t)i); }
    (void)rd_img;

    memset(&cfg, 0, sizeof(cfg));
    cfg.rom = img; cfg.rom_size = 0x10000;
    cfg.entries = entries; cfg.n_entries = PER_ROUND;
    cfg.region_shift = 4; cfg.layout = lay;
    cfg.op_cycles = (int (*)(unsigned))m68k_op_cycles;
    cfg.single = 1;
    f = fopen("fuzz_gen.c", "w");
    if (gen68k_write(&cfg, f, &st) != 0) { printf("generator failed\n"); return 1; }
    fclose(f);
    snprintf(cmd, sizeof(cmd), "gcc -O1 -w -shared -fPIC -fno-strict-aliasing -fwrapv fuzz_gen.c -o fuzz_gen.so");
    if (system(cmd) != 0) { printf("gcc failed\n"); return 1; }
    so = dlopen("./fuzz_gen.so", RTLD_NOW | RTLD_LOCAL);
    if (!so) { printf("dlopen: %s\n", dlerror()); return 1; }
    init = (int (*)(void *, const void *, const void **))dlsym(so, "hf_native_init");
    nreg = init(&m68k, native_host(), &list);
    regs = list;
    memcpy(start_mem, mem, sizeof(mem));

    for (k = 0; k < nreg; k++)
    {
      int t;
      uint32_t a = regs[k].start;
      for (t = 0; t < 8; t++)
      {
        State s0, si, sn;
        int ok;
        load_random(&s0);
        memcpy(mem, start_mem, sizeof(mem));
        apply(&s0, a);
        m68k_step();
        save(&si);
        memcpy(out_mem, mem, sizeof(mem));

        memcpy(mem, start_mem, sizeof(mem));
        apply(&s0, a);
        regs[k].fn();
        save(&sn);
        ok = same(&si, &sn) && !memcmp(out_mem, mem, sizeof(mem));
        tested++;
        if (!ok)
        {
          Insn in;
          dec68k(rd_img, NULL, a, &in);
          if (fails < 25)
          {
            printf("MISMATCH %s op=%04X at %04X (len %d ext %04X %04X %04X %04X)\n", dec68k_name(&in), in.op, a, in.len,
                   img16(a + 2), img16(a + 4), img16(a + 6), img16(a + 8));
            dump("start", &s0);
            dump("interp", &si);
            dump("native", &sn);
            for (i = 0; i < 0x10000; i++)
              if (out_mem[i] != mem[i]) { printf("  mem %04X: interp %02X native %02X\n", i ^ 1, out_mem[i], mem[i]); break; }
          }
          fails++;
        }
      }
    }
    dlclose(so);
    printf("round %d: %d instructions (%u native, %u via interpreter), %d checks, %d mismatches so far\n",
           r, nreg, st.native, st.helper, tested, fails);
  }
  return fails ? 1 : 0;
}
