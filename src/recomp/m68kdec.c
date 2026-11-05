/*
 * m68kdec.c - 68000 instruction decoder (see m68kdec.h).
 *
 * The encodings follow the MC68000 user manual. Validity is decided per
 * family below; tools/recomp_test.c checks it against the interpreter for
 * all 65536 opcodes.
 */
#include <string.h>
#include "m68kdec.h"

typedef struct {
  Dec68kRead16 rd;
  void        *ctx;
  uint32_t     pc;      /* next extension word */
  int          next;    /* index into ext[] */
  Insn        *in;
} D;

static uint16_t ext16(D *d)
{
  uint16_t w = d->rd(d->ctx, d->pc);
  if (d->next < 5) d->in->ext[d->next++] = w;
  d->pc += 2;
  return w;
}

static uint32_t ext32(D *d)
{
  uint32_t hi = ext16(d);
  return (hi << 16) | ext16(d);
}

/* EA classes (bit per mode) */
#define M_DN   (1u << EA_DN)
#define M_AN   (1u << EA_AN)
#define M_AI   (1u << EA_AI)
#define M_PI   (1u << EA_PI)
#define M_PD   (1u << EA_PD)
#define M_DI   (1u << EA_DI)
#define M_IX   (1u << EA_IX)
#define M_AW   (1u << EA_AW)
#define M_AL   (1u << EA_AL)
#define M_PCDI (1u << EA_PCDI)
#define M_PCIX (1u << EA_PCIX)
#define M_IMM  (1u << EA_IMM)

#define C_MEMALT  (M_AI | M_PI | M_PD | M_DI | M_IX | M_AW | M_AL)
#define C_DATALT  (M_DN | C_MEMALT)
#define C_ALT     (C_DATALT | M_AN)
#define C_DATA    (C_DATALT | M_PCDI | M_PCIX | M_IMM)
#define C_ALL     (C_DATA | M_AN)
#define C_CTRL    (M_AI | M_DI | M_IX | M_AW | M_AL | M_PCDI | M_PCIX)

/* Mode of the 6-bit EA field, without reading extension words. */
static int ea_mode(int mode, int reg)
{
  switch (mode)
  {
    case 0: return EA_DN;
    case 1: return EA_AN;
    case 2: return EA_AI;
    case 3: return EA_PI;
    case 4: return EA_PD;
    case 5: return EA_DI;
    case 6: return EA_IX;
    default:
      switch (reg)
      {
        case 0: return EA_AW;
        case 1: return EA_AL;
        case 2: return EA_PCDI;
        case 3: return EA_PCIX;
        case 4: return EA_IMM;
        default: return EA_NONE;
      }
  }
}

static int ea_ok(int mode, int reg, unsigned cls)
{
  int m = ea_mode(mode, reg);
  return m != EA_NONE && (cls & (1u << m));
}

/* Reads the EA's extension words. size is needed for immediates. */
static void ea_read(D *d, int mode, int reg, int size, Ea *ea)
{
  memset(ea, 0, sizeof(*ea));
  ea->mode = (uint8_t)ea_mode(mode, reg);
  ea->reg = (uint8_t)reg;
  switch (ea->mode)
  {
    case EA_DI:
      ea->disp = (int16_t)ext16(d);
      break;
    case EA_IX:
    {
      uint16_t w = ext16(d);
      ea->xreg = (uint8_t)(w >> 12);
      ea->xlong = (uint8_t)((w >> 11) & 1);
      ea->disp = (int8_t)(w & 0xff);
      break;
    }
    case EA_AW:
      ea->val = (uint32_t)(int32_t)(int16_t)ext16(d);
      break;
    case EA_AL:
      ea->val = ext32(d);
      break;
    case EA_PCDI:
      ea->val = d->pc;                 /* base: address of the extension word */
      ea->disp = (int16_t)ext16(d);
      break;
    case EA_PCIX:
    {
      uint16_t w;
      ea->val = d->pc;
      w = ext16(d);
      ea->xreg = (uint8_t)(w >> 12);
      ea->xlong = (uint8_t)((w >> 11) & 1);
      ea->disp = (int8_t)(w & 0xff);
      break;
    }
    case EA_IMM:
      if (size == 4) ea->val = ext32(d);
      else if (size == 2) ea->val = ext16(d);
      else ea->val = ext16(d) & 0xff;
      break;
    default:
      break;
  }
}

static void dn(Ea *ea, int r) { memset(ea, 0, sizeof(*ea)); ea->mode = EA_DN; ea->reg = (uint8_t)r; }
static void an(Ea *ea, int r) { memset(ea, 0, sizeof(*ea)); ea->mode = EA_AN; ea->reg = (uint8_t)r; }

static const int size_of_ss[4] = { 1, 2, 4, 0 };

/* Returns 1 if decoded, 0 if the encoding is not a valid instruction. */
static int decode(D *d, Insn *in)
{
  uint16_t op = in->op;
  int line = op >> 12;
  int rx = (op >> 9) & 7, ry = op & 7;
  int mode = (op >> 3) & 7;
  int ss = (op >> 6) & 3;
  int opm = (op >> 6) & 7;

  switch (line)
  {
    case 0x0:
    {
      if (op & 0x0100)
      {
        if (mode == 1)
        {
          /* MOVEP: interpreter */
          in->kind = K_INTERP;
          ext16(d);
          return 1;
        }
        /* dynamic bit ops: Dn,<ea> */
        in->kind = K_BTST + ss;
        in->dreg = (uint8_t)rx;
        in->dir = 0;
        if (ss == 0) { if (!ea_ok(mode, ry, C_DATA)) return 0; }
        else if (!ea_ok(mode, ry, C_DATALT)) return 0;
        in->size = mode == 0 ? 4 : 1;
        ea_read(d, mode, ry, 1, &in->dst);
        return 1;
      }
      switch (rx)
      {
        case 4: /* static bit ops: #n,<ea> */
          in->kind = K_BTST + ss;
          in->dir = 1;
          if (ss == 0) { if (!ea_ok(mode, ry, C_DATA & ~M_IMM)) return 0; }
          else if (!ea_ok(mode, ry, C_DATALT)) return 0;
          in->cnt = (uint8_t)(ext16(d) & 0xff);
          in->size = mode == 0 ? 4 : 1;
          ea_read(d, mode, ry, 1, &in->dst);
          return 1;
        case 0: case 1: case 5: /* ORI ANDI EORI, and their CCR/SR forms */
          if (op == 0x003C || op == 0x023C || op == 0x0A3C)
          {
            in->kind = op == 0x003C ? K_ORI_CCR : op == 0x023C ? K_ANDI_CCR : K_EORI_CCR;
            in->src.mode = EA_IMM;
            in->src.val = ext16(d);
            in->size = 1;
            return 1;
          }
          if (op == 0x007C || op == 0x027C || op == 0x0A7C)
          {
            in->kind = op == 0x007C ? K_ORI_SR : op == 0x027C ? K_ANDI_SR : K_EORI_SR;
            in->src.mode = EA_IMM;
            in->src.val = ext16(d);
            in->size = 2;
            return 1;
          }
          /* fall through */
        case 2: case 3: case 6:
        {
          static const uint8_t k[8] = { K_ORI, K_ANDI, K_SUBI, K_ADDI, 0, K_EORI, K_CMPI, 0 };
          if (ss == 3) return 0;
          if (!ea_ok(mode, ry, C_DATALT)) return 0;
          in->kind = k[rx];
          in->size = (uint8_t)size_of_ss[ss];
          in->src.mode = EA_IMM;
          if (in->size == 4) in->src.val = ext32(d);
          else if (in->size == 2) in->src.val = ext16(d);
          else in->src.val = ext16(d) & 0xff;
          ea_read(d, mode, ry, in->size, &in->dst);
          return 1;
        }
        default:
          return 0;
      }
    }

    case 0x1: case 0x2: case 0x3:
    {
      int size = line == 1 ? 1 : line == 3 ? 2 : 4;
      int dmode = opm, dreg = rx;
      if (!ea_ok(mode, ry, C_ALL)) return 0;
      if (size == 1 && mode == 1) return 0;
      in->size = (uint8_t)size;
      if (dmode == 1)
      {
        if (size == 1) return 0;
        in->kind = K_MOVEA;
        ea_read(d, mode, ry, size, &in->src);
        an(&in->dst, dreg);
        return 1;
      }
      if (!ea_ok(dmode, dreg, C_DATALT)) return 0;
      in->kind = K_MOVE;
      ea_read(d, mode, ry, size, &in->src);
      ea_read(d, dmode, dreg, size, &in->dst);
      return 1;
    }

    case 0x4:
    {
      if (op & 0x0100)
      {
        if (opm == 7)
        {
          /* LEA */
          if (!ea_ok(mode, ry, C_CTRL)) return 0;
          in->kind = K_LEA;
          in->size = 4;
          ea_read(d, mode, ry, 4, &in->src);
          an(&in->dst, rx);
          return 1;
        }
        if (opm == 6)
        {
          /* CHK.w: interpreter (can trap) */
          if (!ea_ok(mode, ry, C_DATA)) return 0;
          in->kind = K_INTERP;
          in->size = 2;
          ea_read(d, mode, ry, 2, &in->src);
          return 1;
        }
        return 0;
      }
      switch (rx)
      {
        case 0: /* NEGX / MOVE from SR */
          if (ss == 3)
          {
            if (!ea_ok(mode, ry, C_DATALT)) return 0;
            in->kind = K_MOVE_FROM_SR;
            in->size = 2;
            ea_read(d, mode, ry, 2, &in->dst);
            return 1;
          }
          if (!ea_ok(mode, ry, C_DATALT)) return 0;
          in->kind = K_NEGX;
          in->size = (uint8_t)size_of_ss[ss];
          ea_read(d, mode, ry, in->size, &in->dst);
          return 1;
        case 1: /* CLR */
          if (ss == 3) return 0;
          if (!ea_ok(mode, ry, C_DATALT)) return 0;
          in->kind = K_CLR;
          in->size = (uint8_t)size_of_ss[ss];
          ea_read(d, mode, ry, in->size, &in->dst);
          return 1;
        case 2: /* NEG / MOVE to CCR */
          if (ss == 3)
          {
            if (!ea_ok(mode, ry, C_DATA)) return 0;
            in->kind = K_MOVE_TO_CCR;
            in->size = 2;
            ea_read(d, mode, ry, 2, &in->src);
            return 1;
          }
          if (!ea_ok(mode, ry, C_DATALT)) return 0;
          in->kind = K_NEG;
          in->size = (uint8_t)size_of_ss[ss];
          ea_read(d, mode, ry, in->size, &in->dst);
          return 1;
        case 3: /* NOT / MOVE to SR */
          if (ss == 3)
          {
            if (!ea_ok(mode, ry, C_DATA)) return 0;
            in->kind = K_MOVE_TO_SR;
            in->size = 2;
            ea_read(d, mode, ry, 2, &in->src);
            return 1;
          }
          if (!ea_ok(mode, ry, C_DATALT)) return 0;
          in->kind = K_NOT;
          in->size = (uint8_t)size_of_ss[ss];
          ea_read(d, mode, ry, in->size, &in->dst);
          return 1;
        case 4:
          switch (ss)
          {
            case 0: /* NBCD: interpreter */
              if (!ea_ok(mode, ry, C_DATALT)) return 0;
              in->kind = K_INTERP;
              in->size = 1;
              ea_read(d, mode, ry, 1, &in->dst);
              return 1;
            case 1:
              if (mode == 0) { in->kind = K_SWAP; in->size = 4; dn(&in->dst, ry); return 1; }
              if (!ea_ok(mode, ry, C_CTRL)) return 0;
              in->kind = K_PEA;
              in->size = 4;
              ea_read(d, mode, ry, 4, &in->src);
              return 1;
            default: /* EXT / MOVEM regs->mem */
              if (mode == 0)
              {
                in->kind = K_EXT;
                in->size = ss == 2 ? 2 : 4;
                dn(&in->dst, ry);
                return 1;
              }
              if (!ea_ok(mode, ry, M_AI | M_PD | M_DI | M_IX | M_AW | M_AL)) return 0;
              in->kind = K_MOVEM;
              in->dir = 0;
              in->size = ss == 2 ? 2 : 4;
              in->mask = ext16(d);
              ea_read(d, mode, ry, in->size, &in->dst);
              return 1;
          }
        case 5: /* TST / TAS / ILLEGAL */
          if (ss == 3)
          {
            if (op == 0x4AFC) return 0;
            if (!ea_ok(mode, ry, C_DATALT)) return 0;
            in->kind = K_INTERP;     /* TAS */
            in->size = 1;
            ea_read(d, mode, ry, 1, &in->dst);
            return 1;
          }
          if (!ea_ok(mode, ry, C_DATALT)) return 0;   /* 68000: no An/PC/imm */
          in->kind = K_TST;
          in->size = (uint8_t)size_of_ss[ss];
          ea_read(d, mode, ry, in->size, &in->dst);
          return 1;
        case 6: /* MOVEM mem->regs */
          if (ss < 2) return 0;
          if (!ea_ok(mode, ry, M_AI | M_PI | M_DI | M_IX | M_AW | M_AL | M_PCDI | M_PCIX)) return 0;
          in->kind = K_MOVEM;
          in->dir = 1;
          in->size = ss == 2 ? 2 : 4;
          in->mask = ext16(d);
          ea_read(d, mode, ry, in->size, &in->src);
          return 1;
        case 7:
          if (ss == 1)
          {
            switch (mode)
            {
              case 0: case 1:   /* TRAP */
                in->kind = K_INTERP;
                in->flow = FLOW_STOP;
                return 1;
              case 2:           /* LINK */
                in->kind = K_LINK;
                in->dreg = (uint8_t)ry;
                in->src.mode = EA_IMM;
                in->src.val = (uint32_t)(int32_t)(int16_t)ext16(d);
                return 1;
              case 3:           /* UNLK */
                in->kind = K_UNLK;
                in->dreg = (uint8_t)ry;
                return 1;
              case 4: case 5:   /* MOVE USP */
                in->kind = K_INTERP;
                return 1;
              case 6:
                switch (ry)
                {
                  case 0: in->kind = K_INTERP; return 1;                       /* RESET */
                  case 1: in->kind = K_NOP; return 1;
                  case 2: in->kind = K_INTERP; ext16(d); in->flow = FLOW_STOP; return 1; /* STOP */
                  case 3: in->kind = K_RTE; in->flow = FLOW_RETURN; return 1;
                  case 5: in->kind = K_RTS; in->flow = FLOW_RETURN; return 1;
                  case 6: in->kind = K_INTERP; return 1;                       /* TRAPV */
                  case 7: in->kind = K_RTR; in->flow = FLOW_RETURN; return 1;
                  default: return 0;
                }
              default:
                return 0;
            }
          }
          if (ss == 2 || ss == 3)
          {
            if (!ea_ok(mode, ry, C_CTRL)) return 0;
            in->kind = ss == 2 ? K_JSR : K_JMP;
            in->size = 4;
            ea_read(d, mode, ry, 4, &in->src);
            return 1;
          }
          return 0;
      }
      return 0;
    }

    case 0x5:
      if (ss == 3)
      {
        in->cond = (uint8_t)((op >> 8) & 15);
        if (mode == 1)
        {
          uint32_t base = d->pc;
          in->kind = K_DBCC;
          in->dreg = (uint8_t)ry;
          in->target = base + (uint32_t)(int32_t)(int16_t)ext16(d);
          return 1;
        }
        if (!ea_ok(mode, ry, C_DATALT)) return 0;
        in->kind = K_SCC;
        in->size = 1;
        ea_read(d, mode, ry, 1, &in->dst);
        return 1;
      }
      if (!ea_ok(mode, ry, C_ALT)) return 0;
      if (ss == 0 && mode == 1) return 0;
      in->kind = (op & 0x0100) ? K_SUBQ : K_ADDQ;
      in->size = (uint8_t)size_of_ss[ss];
      in->cnt = (uint8_t)(((rx - 1) & 7) + 1);
      ea_read(d, mode, ry, in->size, &in->dst);
      return 1;

    case 0x6:
    {
      int cond = (op >> 8) & 15;
      int8_t d8 = (int8_t)(op & 0xff);
      uint32_t base = in->addr + 2;
      in->cond = (uint8_t)cond;
      in->kind = cond == 0 ? K_BRA : cond == 1 ? K_BSR : K_BCC;
      if (d8 == 0)
      {
        in->size = 2;
        in->target = base + (uint32_t)(int32_t)(int16_t)ext16(d);
      }
      else
      {
        /* $FF is a 32-bit branch on the 68020; on the 68000 it is a short
           branch by -1 (what the interpreter does). */
        in->size = 1;
        in->target = base + (uint32_t)(int32_t)d8;
      }
      return 1;
    }

    case 0x7:
      if (op & 0x0100) return 0;
      in->kind = K_MOVEQ;
      in->size = 4;
      in->src.mode = EA_IMM;
      in->src.val = (uint32_t)(int32_t)(int8_t)(op & 0xff);
      dn(&in->dst, rx);
      return 1;

    case 0x8: case 0xC:
      if (opm == 3 || opm == 7)
      {
        if (!ea_ok(mode, ry, C_DATA)) return 0;
        in->kind = line == 8 ? (opm == 3 ? K_DIVU : K_DIVS) : (opm == 3 ? K_MULU : K_MULS);
        in->size = 2;
        ea_read(d, mode, ry, 2, &in->src);
        dn(&in->dst, rx);
        return 1;
      }
      if (opm == 4 && (mode == 0 || mode == 1))
      {
        /* SBCD / ABCD: interpreter */
        in->kind = K_INTERP;
        in->size = 1;
        return 1;
      }
      if (line == 0xC && (opm == 5 || opm == 6) && mode <= 1)
      {
        /* EXG */
        if (opm == 5)
        {
          in->kind = K_EXG;
          if (mode == 0) { dn(&in->src, rx); dn(&in->dst, ry); }
          else           { an(&in->src, rx); an(&in->dst, ry); }
          return 1;
        }
        if (mode == 1)
        {
          in->kind = K_EXG;
          dn(&in->src, rx); an(&in->dst, ry);
          return 1;
        }
        return 0;
      }
      in->kind = line == 8 ? K_OR : K_AND;
      in->size = (uint8_t)size_of_ss[ss];
      if (opm < 3)
      {
        if (!ea_ok(mode, ry, C_DATA)) return 0;
        in->dir = 0;
        ea_read(d, mode, ry, in->size, &in->src);
        dn(&in->dst, rx);
      }
      else
      {
        if (!ea_ok(mode, ry, C_MEMALT)) return 0;
        in->dir = 1;
        dn(&in->src, rx);
        ea_read(d, mode, ry, in->size, &in->dst);
      }
      return 1;

    case 0x9: case 0xD:
    {
      int add = line == 0xD;
      if (opm == 3 || opm == 7)
      {
        if (!ea_ok(mode, ry, C_ALL)) return 0;
        in->kind = add ? K_ADDA : K_SUBA;
        in->size = opm == 3 ? 2 : 4;
        ea_read(d, mode, ry, in->size, &in->src);
        an(&in->dst, rx);
        return 1;
      }
      in->size = (uint8_t)size_of_ss[ss];
      if (opm >= 4 && mode <= 1)
      {
        in->kind = add ? K_ADDX : K_SUBX;
        in->dir = (uint8_t)mode;
        if (mode == 0) { dn(&in->src, ry); dn(&in->dst, rx); }
        else
        {
          memset(&in->src, 0, sizeof(Ea)); in->src.mode = EA_PD; in->src.reg = (uint8_t)ry;
          memset(&in->dst, 0, sizeof(Ea)); in->dst.mode = EA_PD; in->dst.reg = (uint8_t)rx;
        }
        return 1;
      }
      in->kind = add ? K_ADD : K_SUB;
      if (opm < 3)
      {
        if (!ea_ok(mode, ry, C_ALL)) return 0;
        if (in->size == 1 && mode == 1) return 0;
        in->dir = 0;
        ea_read(d, mode, ry, in->size, &in->src);
        dn(&in->dst, rx);
      }
      else
      {
        if (!ea_ok(mode, ry, C_MEMALT)) return 0;
        in->dir = 1;
        dn(&in->src, rx);
        ea_read(d, mode, ry, in->size, &in->dst);
      }
      return 1;
    }

    case 0xB:
      if (opm == 3 || opm == 7)
      {
        if (!ea_ok(mode, ry, C_ALL)) return 0;
        in->kind = K_CMPA;
        in->size = opm == 3 ? 2 : 4;
        ea_read(d, mode, ry, in->size, &in->src);
        an(&in->dst, rx);
        return 1;
      }
      in->size = (uint8_t)size_of_ss[ss];
      if (opm < 3)
      {
        if (!ea_ok(mode, ry, C_ALL)) return 0;
        if (in->size == 1 && mode == 1) return 0;
        in->kind = K_CMP;
        ea_read(d, mode, ry, in->size, &in->src);
        dn(&in->dst, rx);
        return 1;
      }
      if (mode == 1)
      {
        in->kind = K_CMPM;
        memset(&in->src, 0, sizeof(Ea)); in->src.mode = EA_PI; in->src.reg = (uint8_t)ry;
        memset(&in->dst, 0, sizeof(Ea)); in->dst.mode = EA_PI; in->dst.reg = (uint8_t)rx;
        return 1;
      }
      if (!ea_ok(mode, ry, C_DATALT)) return 0;
      in->kind = K_EOR;
      in->dir = 1;
      dn(&in->src, rx);
      ea_read(d, mode, ry, in->size, &in->dst);
      return 1;

    case 0xE:
      if (ss == 3)
      {
        /* memory shift: bits 10-9 type, bit 8 dir, word by 1 */
        if (op & 0x0800) return 0;
        if (!ea_ok(mode, ry, C_MEMALT)) return 0;
        in->kind = K_SHIFTM;
        in->shtype = (uint8_t)((op >> 9) & 3);
        in->shleft = (uint8_t)((op >> 8) & 1);
        in->size = 2;
        in->cnt = 1;
        ea_read(d, mode, ry, 2, &in->dst);
        return 1;
      }
      in->kind = K_SHIFT;
      in->shtype = (uint8_t)((op >> 3) & 3);
      in->shleft = (uint8_t)((op >> 8) & 1);
      in->shreg = (uint8_t)((op >> 5) & 1);
      in->size = (uint8_t)size_of_ss[ss];
      in->cnt = in->shreg ? (uint8_t)rx : (uint8_t)(((rx - 1) & 7) + 1);
      dn(&in->dst, ry);
      return 1;

    default: /* line A / line F */
      return 0;
  }
}

int dec68k(Dec68kRead16 rd, void *ctx, uint32_t addr, Insn *out)
{
  D d;
  if (addr & 1) return 0;
  memset(out, 0, sizeof(*out));
  out->addr = addr;
  out->op = rd(ctx, addr);
  d.rd = rd;
  d.ctx = ctx;
  d.pc = addr + 2;
  d.next = 0;
  d.in = out;
  if (!decode(&d, out))
  {
    memset(out, 0, sizeof(*out));
    out->addr = addr;
    out->op = rd(ctx, addr);
    out->kind = K_ILLEGAL;
    out->flow = FLOW_STOP;
    out->len = 2;
    return 2;
  }
  out->len = (uint8_t)(d.pc - addr);

  /* control flow */
  switch (out->kind)
  {
    case K_BCC: out->flow = FLOW_COND; break;
    case K_DBCC: out->flow = out->cond == 0 ? FLOW_NEXT : FLOW_COND; break;
    case K_BRA: out->flow = FLOW_JUMP; break;
    case K_BSR: out->flow = FLOW_CALL; break;
    case K_JMP:
    case K_JSR:
    {
      int known = out->src.mode == EA_AW || out->src.mode == EA_AL || out->src.mode == EA_PCDI;
      if (known)
        out->target = out->src.mode == EA_PCDI ? out->src.val + (uint32_t)out->src.disp : out->src.val;
      if (out->kind == K_JMP) out->flow = known ? FLOW_JUMP : FLOW_IJUMP;
      else                    out->flow = known ? FLOW_CALL : FLOW_ICALL;
      break;
    }
    default: break;
  }
  return out->len;
}

static uint16_t read_op_then_zero(void *ctx, uint32_t addr)
{
  return addr == 0 ? *(const uint16_t *)ctx : 0;
}

int dec68k_valid(uint16_t op)
{
  /* validity depends only on the opcode word */
  Insn in;
  dec68k(read_op_then_zero, &op, 0, &in);
  return in.kind != K_ILLEGAL;
}

static const char *const names[K_COUNT] = {
  "interp", "illegal", "move", "movea", "moveq",
  "add", "sub", "and", "or", "eor", "cmp",
  "adda", "suba", "cmpa",
  "addi", "subi", "andi", "ori", "eori", "cmpi",
  "addq", "subq", "addx", "subx", "cmpm",
  "neg", "negx", "not", "clr", "tst",
  "btst", "bchg", "bclr", "bset",
  "shift", "shiftm",
  "mulu", "muls", "divu", "divs",
  "ext", "swap", "exg", "lea", "pea", "link", "unlk",
  "movem", "scc", "dbcc", "bcc", "bra", "bsr",
  "jmp", "jsr", "rts", "rtr", "rte", "nop",
  "move_from_sr", "move_to_sr", "move_to_ccr",
  "andi_sr", "ori_sr", "eori_sr", "andi_ccr", "ori_ccr", "eori_ccr"
};

const char *dec68k_name(const Insn *in)
{
  return in->kind < K_COUNT ? names[in->kind] : "?";
}
