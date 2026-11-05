/*
 * gen68k.c - static 68000-to-C recompiler (see gen68k.h).
 *
 * 1. Finding code: recursive descent from the reset and interrupt vectors
 *    and any extra entry points, following branches, calls and the
 *    jump tables Sonic games use.
 * 2. Writing C: the code is cut into regions (2 KB of ROM each by default).
 *    Each region is one C function with a label per instruction, so it can
 *    be entered at any instruction and branches inside it are plain gotos.
 *    Every instruction mirrors the interpreter's handler step by step:
 *    the same memory accesses in the same order, the same flags (in the
 *    interpreter's own flag format) and the same cycle counts.
 *
 * The rare instructions (BCD, MOVEP, TRAP, STOP, RTE, writes to SR...)
 * call the interpreter's handler for that one opcode instead.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "gen68k.h"
#include "m68kdec.h"

/* text of rt_prelude.h and rt_helpers.h, made by the build */
#include "rt_embed.inc"

typedef struct {
  const Gen68kConfig *cfg;
  uint8_t  *code;        /* per 16-bit word: 1 = an instruction starts here */
  uint32_t *queue;
  size_t    qn, qcap;
  FILE     *f;
  Gen68kStats st;
  /* current region while emitting */
  uint32_t  rstart, rend;
  int       fail;
} G;

/* ------------------------------------------------------------------------ */

static uint16_t rd16(void *ctx, uint32_t a)
{
  const G *g = (const G *)ctx;
  a &= 0xffffff;
  if (a + 1 >= g->cfg->rom_size) return 0;
  return (uint16_t)((g->cfg->rom[a] << 8) | g->cfg->rom[a + 1]);
}

static int in_rom(const G *g, uint32_t a)
{
  return a < g->cfg->rom_size && !(a & 1);
}

static int is_code(const G *g, uint32_t a)
{
  return in_rom(g, a) && g->code[a >> 1];
}

static void push(G *g, uint32_t a)
{
  a &= 0xffffff;
  if (!in_rom(g, a) || g->code[a >> 1]) return;
  if (g->qn == g->qcap)
  {
    g->qcap = g->qcap ? g->qcap * 2 : 4096;
    g->queue = (uint32_t *)realloc(g->queue, g->qcap * sizeof(uint32_t));
  }
  g->queue[g->qn++] = a;
}

/* Jump tables. Handles the two idioms the engine uses:
     move.w  Tbl(pc,dN.w),dM   +  jmp/jsr Tbl(pc,dM.w)   (word offsets)
     jmp     Tbl(pc,dN.w)  into a list of bra / jmp instructions          */
static int plausible(G *g, uint32_t a);

static uint32_t rd32(G *g, uint32_t a)
{
  return ((uint32_t)rd16(g, a) << 16) | rd16(g, a + 2);
}

/* Table of code pointers (longs) at base: take entries while they point
   at plausible code. */
static void long_table(G *g, uint32_t base, int max)
{
  int k;
  for (k = 0; k < max; k++)
  {
    uint32_t t = rd32(g, base + 4u * (uint32_t)k);
    if (!in_rom(g, t) || t < 0x200 || !plausible(g, t)) break;
    push(g, t);
  }
}

static void jump_table(G *g, const Insn *prev, const Insn *in)
{
  uint32_t base;
  /*  movea.l Tbl(pc,dN.w),aM  +  jsr/jmp (aM)   (long pointers) */
  if (in->src.mode == EA_AI && prev && prev->kind == K_MOVEA && prev->size == 4 && prev->dst.reg == in->src.reg)
  {
    if (prev->src.mode == EA_PCIX) long_table(g, prev->src.val + (uint32_t)prev->src.disp, 256);
    else if (prev->src.mode == EA_PCDI) long_table(g, prev->src.val + (uint32_t)prev->src.disp, 1);
    else if (prev->src.mode == EA_AL || prev->src.mode == EA_AW)
    {
      if (in_rom(g, prev->src.val)) long_table(g, prev->src.val, 1);
    }
    return;
  }
  if (in->src.mode != EA_PCIX && in->src.mode != EA_PCDI) return;
  base = in->src.val + (uint32_t)in->src.disp;
  if (in->src.mode == EA_PCIX && prev && prev->kind == K_MOVE && prev->size == 2 &&
      prev->src.mode == EA_PCIX && prev->src.val + (uint32_t)prev->src.disp == base &&
      prev->dst.mode == EA_DN && prev->dst.reg == (in->src.xreg & 7) && in->src.xreg < 8)
  {
    uint32_t k, lowest = 0xffffffffu;
    for (k = 0; k < 256; k++)
    {
      uint32_t at = base + 2 * k, t;
      if (at >= lowest || !in_rom(g, at)) break;
      t = base + (uint32_t)(int32_t)(int16_t)rd16(g, at);
      if (!in_rom(g, t)) break;
      if (t > base && t < lowest) lowest = t;
      push(g, t);
    }
    return;
  }
  if (in->src.mode == EA_PCIX)
  {
    Insn t;
    int stride = 0, k;
    dec68k(rd16, g, base, &t);
    if (t.kind == K_BRA) stride = t.len;
    else if (t.kind == K_JMP && (t.src.mode == EA_AL || t.src.mode == EA_AW)) stride = t.len;
    if (!stride) return;
    for (k = 0; k < 128; k++)
    {
      uint32_t at = base + (uint32_t)(stride * k);
      if (!in_rom(g, at)) break;
      dec68k(rd16, g, at, &t);
      if (t.len != stride || (t.kind != K_BRA && t.kind != K_JMP)) break;
      push(g, at);
    }
  }
}

static void discover(G *g)
{
  while (g->qn)
  {
    uint32_t a = g->queue[--g->qn];
    Insn in, prev;
    int have_prev = 0;
    for (;;)
    {
      if (!in_rom(g, a) || g->code[a >> 1]) break;
      dec68k(rd16, g, a, &in);
      if (a + in.len > g->cfg->rom_size) break;
      g->code[a >> 1] = 1;
      g->st.instructions++;
      if (in.kind == K_ILLEGAL) break;
      switch (in.flow)
      {
        case FLOW_COND:  push(g, in.target); break;
        case FLOW_CALL:  push(g, in.target); break;
        case FLOW_JUMP:  push(g, in.target); break;
        case FLOW_ICALL: jump_table(g, have_prev ? &prev : NULL, &in); break;
        case FLOW_IJUMP: jump_table(g, have_prev ? &prev : NULL, &in); break;
        default: break;
      }
      if (in.flow == FLOW_JUMP || in.flow == FLOW_IJUMP || in.flow == FLOW_RETURN || in.flow == FLOW_STOP)
        break;
      prev = in;
      have_prev = 1;
      a += in.len;
    }
  }
}

/* Addresses in immediates (lea, pea, move.l #) that start a plausible run
   of code: decodes cleanly up to a return or jump. */
static int plausible(G *g, uint32_t a)
{
  int n;
  for (n = 0; n < 200; n++)
  {
    Insn in;
    if (!in_rom(g, a)) return 0;
    if (g->code[a >> 1]) return 1;
    dec68k(rd16, g, a, &in);
    if (in.kind == K_ILLEGAL || in.kind == K_INTERP) return 0;
    if (in.flow == FLOW_JUMP || in.flow == FLOW_IJUMP || in.flow == FLOW_RETURN) return 1;
    a += in.len;
  }
  return 0;
}

static void weak_scan(G *g)
{
  uint32_t a;
  int round;
  for (round = 0; round < 4; round++)
  {
    size_t before = g->st.instructions;
    for (a = 0; a < g->cfg->rom_size; a += 2)
    {
      Insn in;
      uint32_t cand = 0xffffffffu;
      if (!g->code[a >> 1]) continue;
      dec68k(rd16, g, a, &in);
      if (in.kind == K_LEA || in.kind == K_PEA)
      {
        if (in.src.mode == EA_PCDI) cand = in.src.val + (uint32_t)in.src.disp;
        else if (in.src.mode == EA_AL || in.src.mode == EA_AW) cand = in.src.val;
      }
      else if ((in.kind == K_MOVE || in.kind == K_MOVEA) && in.size == 4 && in.src.mode == EA_IMM)
        cand = in.src.val;
      if (cand != 0xffffffffu && in_rom(g, cand) && cand >= 0x200 && !g->code[cand >> 1] && plausible(g, cand))
        push(g, cand);
    }
    /* runs of 2+ longs that all point at plausible code: pointer tables */
    for (a = 0; a + 8 <= g->cfg->rom_size; a += 2)
    {
      uint32_t t0 = rd32(g, a), t1 = rd32(g, a + 4);
      if (g->code[a >> 1]) continue;
      if (in_rom(g, t0) && in_rom(g, t1) && t0 >= 0x200 && t1 >= 0x200 &&
          plausible(g, t0) && plausible(g, t1))
      {
        push(g, t0);
        push(g, t1);
      }
    }
    discover(g);
    if (g->st.instructions == before) break;
  }
}

/* ------------------------------------------------------------------------ */
/* C output                                                                  */

static void P(G *g, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vfprintf(g->f, fmt, ap);
  va_end(ap);
}

static uint32_t mask_of(int size) { return size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu; }
static int bits_of(int size) { return size * 8; }

/* "goto L..." when addr is code in this region, else leave the region. */
static void go(G *g, uint32_t addr)
{
  if (addr >= g->rstart && addr < g->rend && is_code(g, addr))
    P(g, "goto L%06X;", addr);
  else
    P(g, "{ PC = 0x%X; return 1; }", addr);
}

static void idx(G *g, const Ea *e)
{
  if (e->xlong) P(g, "DAR[%d]", e->xreg);
  else          P(g, "(u32)(s16)DAR[%d]", e->xreg);
}

static int step_of(const Ea *e, int size)
{
  return (size == 1 && e->reg == 7) ? 2 : size;
}

/* Leaves the operand's address in variable v, with (An)+ / -(An) effects. */
static void ea_addr(G *g, const Ea *e, int size, const char *v)
{
  int r = 8 + e->reg;
  switch (e->mode)
  {
    case EA_AI:   P(g, "%s = DAR[%d]; ", v, r); break;
    case EA_PI:   P(g, "%s = DAR[%d]; DAR[%d] += %d; ", v, r, r, step_of(e, size)); break;
    case EA_PD:   P(g, "DAR[%d] -= %d; %s = DAR[%d]; ", r, step_of(e, size), v, r); break;
    case EA_DI:   P(g, "%s = DAR[%d] + 0x%Xu; ", v, r, (uint32_t)e->disp); break;
    case EA_IX:   P(g, "%s = DAR[%d] + ", v, r); idx(g, e); P(g, " + 0x%Xu; ", (uint32_t)e->disp); break;
    case EA_AW:
    case EA_AL:   P(g, "%s = 0x%Xu; ", v, e->val); break;
    case EA_PCDI: P(g, "%s = 0x%Xu; ", v, e->val + (uint32_t)e->disp); break;
    case EA_PCIX: P(g, "%s = 0x%Xu + ", v, e->val + (uint32_t)e->disp); idx(g, e); P(g, "; "); break;
    default:      g->fail = 1; break;
  }
}

static int is_mem(const Ea *e)
{
  return e->mode >= EA_AI && e->mode <= EA_AL;
}

static void aerr(G *g, const Insn *in, const char *v, const char *mode)
{
  P(g, "if ((%s & 1) && AERR_EN) { PC = 0x%X; HOST->aerr(%s, %s, 0x%04X); return 1; } ",
    v, in->addr + in->len, v, mode, in->op);
}

/* Reads an operand into variable out (masked to its size). */
static void rd(G *g, const Insn *in, const Ea *e, int size, const char *ea, const char *out)
{
  uint32_t m = mask_of(size);
  switch (e->mode)
  {
    case EA_DN:
      if (size == 4) P(g, "%s = DAR[%d]; ", out, e->reg);
      else           P(g, "%s = DAR[%d] & 0x%X; ", out, e->reg, m);
      return;
    case EA_AN:
      if (size == 4) P(g, "%s = DAR[%d]; ", out, 8 + e->reg);
      else           P(g, "%s = DAR[%d] & 0x%X; ", out, 8 + e->reg, m);
      return;
    case EA_IMM:
      P(g, "%s = 0x%Xu; ", out, e->val & m);
      return;
    case EA_PCDI:
    case EA_PCIX:
      ea_addr(g, e, size, ea);
      P(g, "%s = RP%d(%s); ", out, bits_of(size), ea);
      return;
    default:
      ea_addr(g, e, size, ea);
      if (size > 1) aerr(g, in, ea, "MODE_READ");
      P(g, "%s = R%d(%s); ", out, bits_of(size), ea);
      return;
  }
}

/* Writes value (a variable, already masked) to a Dn or a memory address. */
static void wr(G *g, const Insn *in, const Ea *e, int size, const char *ea, const char *val)
{
  if (e->mode == EA_DN)
  {
    if (size == 4)      P(g, "DAR[%d] = %s; ", e->reg, val);
    else if (size == 2) P(g, "DAR[%d] = (DAR[%d] & 0xffff0000u) | %s; ", e->reg, e->reg, val);
    else                P(g, "DAR[%d] = (DAR[%d] & 0xffffff00u) | %s; ", e->reg, e->reg, val);
    return;
  }
  if (size > 1) aerr(g, in, ea, "MODE_WRITE");
  P(g, "W%d(%s, %s); ", bits_of(size), ea, val);
}

/* Whether the instruction may touch hardware (anything but RAM / stack):
   the interpreter's PC and IR must then be current, and an interrupt can
   be taken inside the access. */
static int ea_needs_sync(const Ea *e)
{
  if (!is_mem(e)) return 0;
  if (e->mode == EA_AW || e->mode == EA_AL) return (e->val & 0xffffff) < 0xe00000;
  return e->reg != 7;
}

static int needs_sync(const Insn *in)
{
  switch (in->kind)
  {
    case K_BSR: case K_JSR: case K_RTS: case K_RTR: case K_PEA: case K_LINK:
    case K_BCC: case K_BRA: case K_DBCC: case K_JMP: case K_LEA:
      return 0;
    case K_UNLK:
      return in->dreg != 7;
    default:
      return ea_needs_sync(&in->src) || ea_needs_sync(&in->dst);
  }
}

static const char *const cc_names[16] = {
  "CC_T", "CC_F", "CC_HI", "CC_LS", "CC_CC", "CC_CS", "CC_NE", "CC_EQ",
  "CC_VC", "CC_VS", "CC_PL", "CC_MI", "CC_GE", "CC_LT", "CC_GT", "CC_LE"
};

static const char *nflag(int size)
{
  return size == 1 ? "NFLAG_8" : size == 2 ? "NFLAG_16" : "NFLAG_32";
}

/* res = dst <op> src, flags as the interpreter sets them. kind is one of
   K_ADD/K_SUB/K_CMP/K_AND/K_OR/K_EOR (immediate and quick forms map here). */
static void alu_math(G *g, int kind, int size)
{
  int b = bits_of(size);
  uint32_t m = mask_of(size);
  switch (kind)
  {
    case K_ADD:
      P(g, "res = src + dst; NF = %s(res); VF = VFLAG_ADD_%d(src, dst, res); ", nflag(size), b);
      if (size == 4) P(g, "XF = CF = CFLAG_ADD_32(src, dst, res); ZF = res; ");
      else           P(g, "XF = CF = CFLAG_%d(res); ZF = res & 0x%X; res = ZF; ", b, m);
      break;
    case K_SUB:
      P(g, "res = dst - src; NF = %s(res); ", nflag(size));
      if (size == 4) P(g, "XF = CF = CFLAG_SUB_32(src, dst, res); ");
      else           P(g, "XF = CF = CFLAG_%d(res); ", b);
      P(g, "VF = VFLAG_SUB_%d(src, dst, res); ", b);
      if (size == 4) P(g, "ZF = res; ");
      else           P(g, "ZF = res & 0x%X; res = ZF; ", m);
      break;
    case K_CMP:
      P(g, "res = dst - src; NF = %s(res); ", nflag(size));
      if (size == 4) P(g, "ZF = res; VF = VFLAG_SUB_32(src, dst, res); CF = CFLAG_SUB_32(src, dst, res); ");
      else           P(g, "ZF = res & 0x%X; VF = VFLAG_SUB_%d(src, dst, res); CF = CFLAG_%d(res); ", m, b, b);
      break;
    case K_AND: case K_OR: case K_EOR:
      P(g, "res = dst %s src; NF = %s(res); ZF = res; CF = 0; VF = 0; ",
        kind == K_AND ? "&" : kind == K_OR ? "|" : "^", nflag(size));
      break;
  }
}

static int alu_base(int kind)
{
  switch (kind)
  {
    case K_ADDI: case K_ADDQ: return K_ADD;
    case K_SUBI: case K_SUBQ: return K_SUB;
    case K_CMPI: return K_CMP;
    case K_ANDI: return K_AND;
    case K_ORI:  return K_OR;
    case K_EORI: return K_EOR;
    default:     return kind;
  }
}

static void push32(G *g, const Insn *in, const char *val)
{
  P(g, "DAR[15] -= 4; ");
  aerr(g, in, "DAR[15]", "MODE_WRITE");
  P(g, "W32(DAR[15], %s); ", val);
}

static void pull(G *g, const Insn *in, int size, const char *out)
{
  P(g, "t = DAR[15]; DAR[15] += %d; ", size);
  aerr(g, in, "t", "MODE_READ");
  P(g, "%s = R%d(t); ", out, bits_of(size));
}

static const char *sh_name(int type, int left)
{
  static const char *const n[4][2] = { { "asr", "asl" }, { "lsr", "lsl" }, { "roxr", "roxl" }, { "ror", "rol" } };
  return n[type & 3][left & 1];
}

/* The interpreter runs this opcode. */
static void emit_helper(G *g, const Insn *in)
{
  uint32_t next = in->addr + in->len;
  g->st.helper++;
  P(g, "PC = 0x%X; HOST->exec_op(0x%04X); CYCLES += HOST->op_cycles(IR);\n", in->addr + 2, in->op);
  P(g, "  if (PC != 0x%X) goto redispatch; ", next);
}

/* Emits one instruction. Returns 1 if control can fall through to the
   next address. */
static int emit_insn(G *g, const Insn *in)
{
  uint32_t next = in->addr + in->len;
  int base = g->cfg->op_cycles(in->op);
  int size = in->size;
  int sync = 0;
  int falls = 1;

  P(g, "L%06X: STEP(0x%X) /* %s */\n  { u32 ea, ea2, src, dst, res, t; (void)ea; (void)ea2; (void)src; (void)dst; (void)res; (void)t;\n  ",
    in->addr, in->addr, dec68k_name(in));

  switch (in->kind)
  {
    case K_INTERP:
    case K_ILLEGAL:
    case K_RTE:
    case K_MOVE_TO_SR:
    case K_ANDI_SR:
    case K_ORI_SR:
    case K_EORI_SR:
      emit_helper(g, in);
      P(g, "}\n");
      return 1;
    default:
      break;
  }

  g->st.native++;
  sync = needs_sync(in);
  if (sync) P(g, "PC = 0x%X; IR = 0x%04X; ", next, in->op);

  switch (in->kind)
  {
    case K_MOVE:
      rd(g, in, &in->src, size, "ea", "res");
      if (in->dst.mode == EA_DN)
      {
        wr(g, in, &in->dst, size, "ea2", "res");
        P(g, "NF = %s(res); ZF = res; VF = 0; CF = 0; ", nflag(size));
      }
      else
      {
        ea_addr(g, &in->dst, size, "ea2");
        P(g, "NF = %s(res); ZF = res; VF = 0; CF = 0; ", nflag(size));
        if (size == 4 && in->dst.mode == EA_PD)
        {
          /* move.l to -(An) writes the low word first */
          P(g, "t = ea2 + 2; ");
          aerr(g, in, "t", "MODE_WRITE");
          P(g, "W16(t, res & 0xffff); ");
          aerr(g, in, "ea2", "MODE_WRITE");
          P(g, "W16(ea2, res >> 16); ");
        }
        else
          wr(g, in, &in->dst, size, "ea2", "res");
      }
      break;

    case K_MOVEA:
      rd(g, in, &in->src, size, "ea", "res");
      if (size == 2) P(g, "DAR[%d] = (u32)(s16)res; ", 8 + in->dst.reg);
      else           P(g, "DAR[%d] = res; ", 8 + in->dst.reg);
      break;

    case K_MOVEQ:
    {
      uint32_t v = in->src.val;
      P(g, "DAR[%d] = 0x%Xu; NF = 0x%X; ZF = 0x%Xu; VF = 0; CF = 0; ", in->dst.reg, v, v >> 24, v);
      break;
    }

    case K_ADD: case K_SUB: case K_AND: case K_OR: case K_CMP:
      if (in->dir == 0)
      {
        rd(g, in, &in->src, size, "ea", "src");
        P(g, "dst = DAR[%d]", in->dst.reg);
        if (size < 4) P(g, " & 0x%X", mask_of(size));
        P(g, "; ");
        alu_math(g, in->kind, size);
        if (in->kind != K_CMP) wr(g, in, &in->dst, size, "ea2", "res");
        break;
      }
      /* fall through: Dn,<ea> */
    case K_EOR:
    case K_ADDI: case K_SUBI: case K_ANDI: case K_ORI: case K_EORI: case K_CMPI:
    case K_ADDQ: case K_SUBQ:
    {
      int k = alu_base(in->kind);
      if ((in->kind == K_ADDQ || in->kind == K_SUBQ) && in->dst.mode == EA_AN)
      {
        P(g, "DAR[%d] %s= %d; ", 8 + in->dst.reg, in->kind == K_ADDQ ? "+" : "-", in->cnt);
        break;
      }
      if (in->kind == K_ADDQ || in->kind == K_SUBQ) P(g, "src = %d; ", in->cnt);
      else if (in->src.mode == EA_DN) rd(g, in, &in->src, size, "ea2", "src");
      else P(g, "src = 0x%Xu; ", in->src.val & mask_of(size));
      if (in->dst.mode == EA_DN)
      {
        rd(g, in, &in->dst, size, "ea", "dst");
        alu_math(g, k, size);
        if (k != K_CMP) wr(g, in, &in->dst, size, "ea", "res");
      }
      else
      {
        rd(g, in, &in->dst, size, "ea", "dst");
        alu_math(g, k, size);
        if (k != K_CMP) wr(g, in, &in->dst, size, "ea", "res");
      }
      break;
    }

    case K_ADDA: case K_SUBA:
      rd(g, in, &in->src, size, "ea", "src");
      if (size == 2) P(g, "src = (u32)(s16)src; ");
      P(g, "DAR[%d] %s= src; ", 8 + in->dst.reg, in->kind == K_ADDA ? "+" : "-");
      break;

    case K_CMPA:
      rd(g, in, &in->src, size, "ea", "src");
      if (size == 2) P(g, "src = (u32)(s16)src; ");
      P(g, "dst = DAR[%d]; res = dst - src; NF = NFLAG_32(res); ZF = res; VF = VFLAG_SUB_32(src, dst, res); CF = CFLAG_SUB_32(src, dst, res); ",
        8 + in->dst.reg);
      break;

    case K_ADDX: case K_SUBX:
    {
      int b = bits_of(size);
      uint32_t m = mask_of(size);
      if (in->dir == 0)
      {
        P(g, "src = DAR[%d]", in->src.reg); if (size < 4) P(g, " & 0x%X", m); P(g, "; ");
        P(g, "dst = DAR[%d]", in->dst.reg); if (size < 4) P(g, " & 0x%X", m); P(g, "; ");
      }
      else
      {
        rd(g, in, &in->src, size, "ea2", "src");
        ea_addr(g, &in->dst, size, "ea");
        if (size > 1) aerr(g, in, "ea", "MODE_READ");
        P(g, "dst = R%d(ea); ", b);
      }
      if (in->kind == K_ADDX)
      {
        P(g, "res = src + dst + XFLAG_AS_1(); NF = %s(res); VF = VFLAG_ADD_%d(src, dst, res); ", nflag(size), b);
        if (size == 4) P(g, "XF = CF = CFLAG_ADD_32(src, dst, res); ");
        else           P(g, "XF = CF = CFLAG_%d(res); ", b);
      }
      else
      {
        P(g, "res = dst - src - XFLAG_AS_1(); NF = %s(res); ", nflag(size));
        if (size == 4) P(g, "XF = CF = CFLAG_SUB_32(src, dst, res); ");
        else           P(g, "XF = CF = CFLAG_%d(res); ", b);
        P(g, "VF = VFLAG_SUB_%d(src, dst, res); ", b);
      }
      if (size < 4) P(g, "res &= 0x%X; ", m);
      P(g, "ZF |= res; ");
      if (in->dir == 0) wr(g, in, &in->dst, size, "ea", "res");
      else              wr(g, in, &in->dst, size, "ea", "res");
      break;
    }

    case K_CMPM:
      rd(g, in, &in->src, size, "ea", "src");
      rd(g, in, &in->dst, size, "ea2", "dst");
      alu_math(g, K_CMP, size);
      break;

    case K_NEG: case K_NEGX: case K_NOT: case K_CLR: case K_TST:
    {
      int b = bits_of(size);
      uint32_t m = mask_of(size);
      int sh = size == 1 ? 0 : size == 2 ? 8 : 24;
      if (in->kind == K_CLR)
      {
        if (in->dst.mode == EA_DN) P(g, "res = 0; ");
        else { ea_addr(g, &in->dst, size, "ea"); P(g, "res = 0; "); }
        wr(g, in, &in->dst, size, "ea", "res");
        P(g, "NF = 0; VF = 0; CF = 0; ZF = 0; ");
        break;
      }
      rd(g, in, &in->dst, size, "ea", "src");
      switch (in->kind)
      {
        case K_TST:
          P(g, "NF = %s(src); ZF = src; VF = 0; CF = 0; ", nflag(size));
          break;
        case K_NOT:
          P(g, "res = ~src & 0x%X; ", m);
          wr(g, in, &in->dst, size, "ea", "res");
          P(g, "NF = %s(res); ZF = res; CF = 0; VF = 0; ", nflag(size));
          break;
        case K_NEG:
          P(g, "res = 0 - src; NF = %s(res); ", nflag(size));
          if (size == 4) P(g, "CF = XF = CFLAG_SUB_32(src, 0, res); ");
          else           P(g, "CF = XF = CFLAG_%d(res); ", b);
          P(g, "VF = (src & res) >> %d; ", sh);
          if (size < 4) P(g, "res &= 0x%X; ", m);
          P(g, "ZF = res; ");
          wr(g, in, &in->dst, size, "ea", "res");
          break;
        case K_NEGX:
          P(g, "res = 0 - src - XFLAG_AS_1(); NF = %s(res); ", nflag(size));
          if (size == 4) P(g, "XF = CF = CFLAG_SUB_32(src, 0, res); ");
          else           P(g, "XF = CF = CFLAG_%d(res); ", b);
          P(g, "VF = (src & res) >> %d; ", sh);
          if (size < 4) P(g, "res &= 0x%X; ", m);
          P(g, "ZF |= res; ");
          wr(g, in, &in->dst, size, "ea", "res");
          break;
      }
      break;
    }

    case K_BTST: case K_BCHG: case K_BCLR: case K_BSET:
    {
      static const char *const opx[4] = { "", "^", "&~", "|" };
      int k = in->kind - K_BTST;
      int bitmask = in->dst.mode == EA_DN ? 0x1f : 7;
      if (in->dir == 0) P(g, "t = 1u << (DAR[%d] & %d); ", in->dreg, bitmask);
      else              P(g, "t = 0x%Xu; ", 1u << (in->cnt & bitmask));
      if (in->dst.mode == EA_DN)
      {
        if (k != 0) P(g, "if (t & 0xffff0000u) CYCLES += 14; ");
        P(g, "ZF = DAR[%d] & t; ", in->dst.reg);
        if (k == 1) P(g, "DAR[%d] ^= t; ", in->dst.reg);
        if (k == 2) P(g, "DAR[%d] &= ~t; ", in->dst.reg);
        if (k == 3) P(g, "DAR[%d] |= t; ", in->dst.reg);
        break;
      }
      rd(g, in, &in->dst, 1, "ea", "src");
      P(g, "ZF = src & t; ");
      if (k != 0)
      {
        P(g, "res = (src %s t) & 0xff; ", opx[k]);
        P(g, "W8(ea, res); ");
      }
      break;
    }

    case K_SHIFT:
      P(g, "sh_%s_%d_%s(%d, %d); ", sh_name(in->shtype, in->shleft), bits_of(size),
        in->shreg ? "r" : "s", in->dst.reg, in->cnt);
      break;

    case K_SHIFTM:
      ea_addr(g, &in->dst, 2, "ea");
      aerr(g, in, "ea", "MODE_READ");
      P(g, "shm_%s(ea); ", sh_name(in->shtype, in->shleft));
      break;

    case K_MULU:
      rd(g, in, &in->src, 2, "ea", "src");
      P(g, "res = src * (DAR[%d] & 0xffff); UseMuluCycles((u16)src); DAR[%d] = res; ZF = res; NF = NFLAG_32(res); VF = 0; CF = 0; ",
        in->dst.reg, in->dst.reg);
      break;

    case K_MULS:
      rd(g, in, &in->src, 2, "ea", "src");
      P(g, "{ s32 s_ = (s16)src; res = (u32)(s_ * (s32)(s16)(DAR[%d] & 0xffff)); UseMulsCycles((s16)s_); } DAR[%d] = res; ZF = res; NF = NFLAG_32(res); VF = 0; CF = 0; ",
        in->dst.reg, in->dst.reg);
      break;

    case K_DIVU:
      rd(g, in, &in->src, 2, "ea", "src");
      P(g, "if (src == 0) { CF = 0; PC = 0x%X; IR = 0x%04X; HOST->trap(5); CYCLES += %d; goto redispatch; }\n  ", next, in->op, base);
      P(g, "dst = DAR[%d]; { u32 q_ = dst / src, r_ = dst %% src; ", in->dst.reg);
      P(g, "if (q_ < 0x10000) { UseDivuCycles(dst, src); ZF = q_; NF = NFLAG_16(q_); VF = 0; CF = 0; DAR[%d] = (q_ & 0xffff) | (r_ << 16); } ", in->dst.reg);
      P(g, "else { CYCLES += 70; VF = 0x80; NF = 0x80; CF = 0; } } ");
      break;

    case K_DIVS:
      rd(g, in, &in->src, 2, "ea", "src");
      P(g, "{ s32 s_ = (s16)src; if (s_ == 0) { CF = 0; PC = 0x%X; IR = 0x%04X; HOST->trap(5); CYCLES += %d; goto redispatch; }\n  ", next, in->op, base);
      P(g, "dst = DAR[%d]; UseDivsCycles((s32)dst, (s16)s_); ", in->dst.reg);
      P(g, "if (dst == 0x80000000u && s_ == -1) { ZF = 0; NF = 0; VF = 0; CF = 0; DAR[%d] = 0; } ", in->dst.reg);
      P(g, "else { s32 q_ = (s32)dst / s_, r_ = (s32)dst %% s_; if (q_ == (s16)q_) { ZF = (u32)q_; NF = NFLAG_16((u32)q_); VF = 0; CF = 0; DAR[%d] = ((u32)q_ & 0xffff) | ((u32)r_ << 16); } else { VF = 0x80; NF = 0x80; CF = 0; } } } ",
        in->dst.reg);
      break;

    case K_EXT:
    {
      int r = in->dst.reg;
      if (size == 2)
        P(g, "DAR[%d] = (DAR[%d] & 0xffff0000u) | (DAR[%d] & 0xff) | ((DAR[%d] & 0x80) ? 0xff00 : 0); NF = NFLAG_16(DAR[%d]); ZF = DAR[%d] & 0xffff; VF = 0; CF = 0; ",
          r, r, r, r, r, r);
      else
        P(g, "DAR[%d] = (DAR[%d] & 0xffff) | ((DAR[%d] & 0x8000) ? 0xffff0000u : 0); NF = NFLAG_32(DAR[%d]); ZF = DAR[%d]; VF = 0; CF = 0; ",
          r, r, r, r, r);
      break;
    }

    case K_SWAP:
    {
      int r = in->dst.reg;
      P(g, "ZF = DAR[%d] << 16; DAR[%d] = (DAR[%d] >> 16) | ZF; ZF = DAR[%d]; NF = NFLAG_32(DAR[%d]); CF = 0; VF = 0; ",
        r, r, r, r, r);
      break;
    }

    case K_EXG:
    {
      int a = in->src.mode == EA_AN ? 8 + in->src.reg : in->src.reg;
      int b = in->dst.mode == EA_AN ? 8 + in->dst.reg : in->dst.reg;
      P(g, "t = DAR[%d]; DAR[%d] = DAR[%d]; DAR[%d] = t; ", a, a, b, b);
      break;
    }

    case K_LEA:
      ea_addr(g, &in->src, 4, "ea");
      P(g, "DAR[%d] = ea; ", 8 + in->dst.reg);
      break;

    case K_PEA:
      ea_addr(g, &in->src, 4, "ea");
      push32(g, in, "ea");
      break;

    case K_LINK:
      if (in->dreg == 7)
      {
        P(g, "DAR[15] -= 4; ");
        aerr(g, in, "DAR[15]", "MODE_WRITE");
        P(g, "W32(DAR[15], DAR[15]); DAR[15] += 0x%Xu; ", in->src.val);
      }
      else
      {
        P(g, "t = DAR[%d]; ", 8 + in->dreg);
        push32(g, in, "t");
        P(g, "DAR[%d] = DAR[15]; DAR[15] += 0x%Xu; ", 8 + in->dreg, in->src.val);
      }
      break;

    case K_UNLK:
      if (in->dreg == 7)
      {
        aerr(g, in, "DAR[15]", "MODE_READ");
        P(g, "DAR[15] = R32(DAR[15]); ");
      }
      else
      {
        P(g, "DAR[15] = DAR[%d]; ", 8 + in->dreg);
        pull(g, in, 4, "res");
        P(g, "DAR[%d] = res; ", 8 + in->dreg);
      }
      break;

    case K_MOVEM:
    {
      int i, count = 0, w = size == 2;
      for (i = 0; i < 16; i++) if (in->mask & (1 << i)) count++;
      if (in->dir == 0)
      {
        if (in->dst.mode == EA_PD)
        {
          P(g, "ea = DAR[%d]; ", 8 + in->dst.reg);
          for (i = 0; i < 16; i++)
            if (in->mask & (1 << i))
            {
              if (w)
              {
                P(g, "ea -= 2; "); aerr(g, in, "ea", "MODE_WRITE");
                P(g, "W16(ea, DAR[%d] & 0xffff); ", 15 - i);
              }
              else
              {
                P(g, "ea -= 4; t = ea + 2; "); aerr(g, in, "t", "MODE_WRITE");
                P(g, "W16(t, DAR[%d] & 0xFFFF); ", 15 - i);
                aerr(g, in, "ea", "MODE_WRITE");
                P(g, "W16(ea, (DAR[%d] >> 16) & 0xFFFF); ", 15 - i);
              }
            }
          P(g, "DAR[%d] = ea; ", 8 + in->dst.reg);
        }
        else
        {
          ea_addr(g, &in->dst, size, "ea");
          for (i = 0; i < 16; i++)
            if (in->mask & (1 << i))
            {
              aerr(g, in, "ea", "MODE_WRITE");
              if (w) P(g, "W16(ea, DAR[%d] & 0xffff); ea += 2; ", i);
              else   P(g, "W32(ea, DAR[%d]); ea += 4; ", i);
            }
        }
      }
      else
      {
        int pcrel = in->src.mode == EA_PCDI || in->src.mode == EA_PCIX;
        if (in->src.mode == EA_PI) P(g, "ea = DAR[%d]; ", 8 + in->src.reg);
        else ea_addr(g, &in->src, size, "ea");
        for (i = 0; i < 16; i++)
          if (in->mask & (1 << i))
          {
            if (!pcrel) aerr(g, in, "ea", "MODE_READ");
            if (w) P(g, "DAR[%d] = (u32)(s16)%s(ea); ea += 2; ", i, pcrel ? "RP16" : "R16");
            else   P(g, "DAR[%d] = %s(ea); ea += 4; ", i, pcrel ? "RP32" : "R32");
          }
        if (in->src.mode == EA_PI) P(g, "DAR[%d] = ea; ", 8 + in->src.reg);
        aerr(g, in, "ea", "MODE_READ");
        P(g, "(void)R16(ea); ");
      }
      P(g, "CYCLES += %d; ", count * (w ? 28 : 56));
      break;
    }

    case K_SCC:
      if (in->dst.mode == EA_DN)
      {
        int r = in->dst.reg;
        if (in->cond == 0)      P(g, "DAR[%d] |= 0xff; ", r);
        else if (in->cond == 1) P(g, "DAR[%d] &= 0xffffff00u; ", r);
        else P(g, "if (%s) { DAR[%d] |= 0xff; CYCLES += 14; } else DAR[%d] &= 0xffffff00u; ", cc_names[in->cond], r, r);
      }
      else
      {
        ea_addr(g, &in->dst, 1, "ea");
        P(g, "W8(ea, (%s) ? 0xff : 0); ", cc_names[in->cond]);
      }
      break;

    case K_DBCC:
    {
      int r = in->dreg;
      if (in->cond == 0) break;   /* dbt: does nothing */
      if (in->cond == 1) P(g, "{ ");
      else P(g, "if (!(%s)) { ", cc_names[in->cond]);
      P(g, "res = (DAR[%d] - 1) & 0xffff; DAR[%d] = (DAR[%d] & 0xffff0000u) | res; ", r, r, r);
      P(g, "if (res != 0xffff) { CYCLES += %d; ", base - 14);
      go(g, in->target);
      P(g, " } CYCLES += %d; ", base + 14);
      if (in->cond == 1) P(g, "} ");
      else P(g, "} else CYCLES += %d; ", base);
      P(g, "}\n");
      return 1;
    }

    case K_BCC:
      P(g, "if (%s) { CYCLES += %d; ", cc_names[in->cond], base);
      go(g, in->target);
      P(g, " } CYCLES += %d; }\n", base + (size == 1 ? -14 : 14));
      return 1;

    case K_BRA:
      P(g, "CYCLES += %d; ", base);
      go(g, in->target);
      P(g, " }\n");
      return 0;

    case K_BSR:
      P(g, "t = 0x%Xu; ", next);
      push32(g, in, "t");
      P(g, "CYCLES += %d; ", base);
      go(g, in->target);
      P(g, " }\n");
      return 0;

    case K_JMP:
    case K_JSR:
      ea_addr(g, &in->src, 4, "ea");
      if (in->kind == K_JSR) { P(g, "t = 0x%Xu; ", next); push32(g, in, "t"); }
      P(g, "CYCLES += %d; ", base);
      if (in->flow == FLOW_JUMP || in->flow == FLOW_CALL) go(g, in->target);
      else P(g, "PC = ea; goto redispatch;");
      P(g, " }\n");
      return 0;

    case K_RTS:
      pull(g, in, 4, "res");
      P(g, "CYCLES += %d; PC = res; goto redispatch; }\n", base);
      return 0;

    case K_RTR:
      pull(g, in, 2, "res");
      P(g, "SET_CCR(res); ");
      pull(g, in, 4, "res");
      P(g, "CYCLES += %d; PC = res; goto redispatch; }\n", base);
      return 0;

    case K_NOP:
      break;

    case K_MOVE_FROM_SR:
      if (in->dst.mode == EA_DN)
        P(g, "DAR[%d] = (DAR[%d] & 0xffff0000u) | GET_SR(); ", in->dst.reg, in->dst.reg);
      else
      {
        ea_addr(g, &in->dst, 2, "ea");
        P(g, "res = GET_SR(); ");
        wr(g, in, &in->dst, 2, "ea", "res");
      }
      break;

    case K_MOVE_TO_CCR:
      rd(g, in, &in->src, 2, "ea", "src");
      P(g, "SET_CCR(src); ");
      break;

    case K_ANDI_CCR: P(g, "SET_CCR(GET_CCR() & 0x%X); ", in->src.val); break;
    case K_ORI_CCR:  P(g, "SET_CCR(GET_CCR() | 0x%X); ", in->src.val); break;
    case K_EORI_CCR: P(g, "SET_CCR(GET_CCR() ^ 0x%X); ", in->src.val); break;

    default:
      g->fail = 1;
      break;
  }

  if (sync)
    P(g, "\n  if (PC != 0x%X || IR != 0x%04X) { CYCLES += HOST->op_cycles(IR); goto redispatch; } ", next, in->op);
  P(g, "CYCLES += %d; }\n", base);
  (void)falls;
  return 1;
}

uint32_t gen68k_hash(const uint8_t *rom, uint32_t rom_size, uint32_t start, uint32_t len)
{
  uint32_t h = 2166136261u, i;
  for (i = start; i < start + len && i < rom_size; i++)
  {
    h ^= rom[i];
    h *= 16777619u;
  }
  return h;
}

static void emit_region(G *g, uint32_t start, uint32_t end, unsigned id)
{
  uint32_t a;
  P(g, "\n/* ---- region %u: 0x%06X-0x%06X ---- */\nstatic int r%u(void)\n{\n  int ran = 0, one = 0; (void)one;\ndispatch:\n  switch (PC) {\n", id, start, end - 1, id);
  for (a = start; a < end; a += 2)
    if (is_code(g, a)) P(g, "    case 0x%X: goto L%06X;\n", a, a);
  P(g, "    default: return ran;\n  }\nredispatch:\n  ran = 1;\n  goto dispatch;\n");

  g->rstart = start;
  g->rend = end;
  for (a = start; a < end; a += 2)
  {
    Insn in;
    uint32_t n;
    if (!is_code(g, a)) continue;
    dec68k(rd16, g, a, &in);
    if (emit_insn(g, &in))
    {
      /* fall through to the next instruction */
      n = a + in.len;
      {
        uint32_t b = a + 2;
        while (b < end && !is_code(g, b)) b += 2;
        if (b != n || b >= end) { P(g, "  "); go(g, n); P(g, "\n"); }
      }
    }
  }
  P(g, "}\n");
}

int gen68k_write(const Gen68kConfig *cfg, FILE *out, Gen68kStats *stats)
{
  G g;
  uint32_t a, rsize;
  unsigned id, nreg = 0;
  long pos0;
  const Gen68kLayout *L = &cfg->layout;
  int v;

  memset(&g, 0, sizeof(g));
  g.cfg = cfg;
  g.f = out;
  g.code = (uint8_t *)calloc(cfg->rom_size / 2 + 1, 1);
  if (!g.code) return -1;

  if (cfg->single)
  {
    for (a = 0; a < cfg->n_entries; a++)
    {
      Insn in;
      uint32_t e = cfg->entries[a];
      if (!in_rom(&g, e) || g.code[e >> 1]) continue;
      dec68k(rd16, &g, e, &in);
      g.code[e >> 1] = 1;
      g.st.instructions++;
    }
  }
  /* entry points: reset, every exception / interrupt / trap vector */
  else for (v = 1; v < 64; v++)
  {
    uint32_t t = (uint32_t)((cfg->rom[v * 4] << 24) | (cfg->rom[v * 4 + 1] << 16) | (cfg->rom[v * 4 + 2] << 8) | cfg->rom[v * 4 + 3]);
    push(&g, t);
  }
  if (!cfg->single)
  {
    for (a = 0; a < cfg->n_entries; a++) push(&g, cfg->entries[a]);
    discover(&g);
    if (cfg->weak_scan) weak_scan(&g);
  }

  rsize = 1u << cfg->region_shift;
  pos0 = ftell(out);

  P(&g, "/* Generated by the hellfire-saga-pc recompiler from the player's own ROM.\n"
        "   Contains code translated from that ROM: do not distribute. */\n");
  P(&g, "#define O_mm %u\n#define O_cycles %u\n#define O_refresh %u\n#define O_cycle_end %u\n#define O_dar %u\n#define O_pc %u\n#define O_ir %u\n",
    L->mm, L->cycles, L->refresh, L->cycle_end, L->dar, L->pc, L->ir);
  P(&g, "#define O_t1 %u\n#define O_s %u\n#define O_x %u\n#define O_n %u\n#define O_z %u\n#define O_v %u\n#define O_c %u\n#define O_int_mask %u\n#define O_aerr %u\n",
    L->t1, L->s, L->x, L->n, L->z, L->v, L->c, L->int_mask, L->aerr);
  fputs(rt_prelude_text, out);
  fputs(rt_helpers_text, out);

  for (id = 0; id * rsize < cfg->rom_size; id++)
  {
    uint32_t s = id * rsize, e = s + rsize, b;
    int any = 0;
    if (e > cfg->rom_size) e = cfg->rom_size;
    for (b = s; b < e; b += 2) if (g.code[b >> 1]) { any = 1; break; }
    if (!any) continue;
    emit_region(&g, s, e, id);
    nreg++;
  }

  /* region table: start address, function, checksum of the bytes it was
     made from (the runtime turns a region off if those bytes change) */
  P(&g, "\ntypedef int (*HfRegionFn)(void);\n");
  P(&g, "typedef struct { u32 start, len, hash; HfRegionFn fn; } HfRegion;\n");
  P(&g, "static const HfRegion hf_regions_[] = {\n");
  for (id = 0; id * rsize < cfg->rom_size; id++)
  {
    uint32_t s = id * rsize, e = s + rsize, b;
    int any = 0;
    if (e > cfg->rom_size) e = cfg->rom_size;
    for (b = s; b < e; b += 2) if (g.code[b >> 1]) { any = 1; break; }
    if (!any) continue;
    P(&g, "  { 0x%X, %u, 0x%08Xu, r%u },\n", s, (e - s) + GEN68K_REGION_TAIL,
      gen68k_hash(cfg->rom, cfg->rom_size, s, (e - s) + GEN68K_REGION_TAIL), id);
  }
  P(&g, "};\n\n");
  P(&g, "int hf_native_abi(void) { return %d; }\n", GEN68K_ABI);
  P(&g, "unsigned hf_native_mm_size(void) { return sizeof(MM); }\n");
  P(&g, "unsigned hf_native_region_shift(void) { return %u; }\n", cfg->region_shift);
  P(&g, "int hf_native_init(void *cpu, const void *host, const void **regions)\n"
        "{\n  C = (u8 *)cpu;\n  HOST = (const HfHost *)host;\n  *regions = hf_regions_;\n"
        "  return (int)(sizeof(hf_regions_) / sizeof(hf_regions_[0]));\n}\n");

  g.st.regions = nreg;
  g.st.bytes_out = (size_t)(ftell(out) - pos0);
  if (stats) *stats = g.st;
  free(g.code);
  free(g.queue);
  return g.fail ? -2 : 0;
}
