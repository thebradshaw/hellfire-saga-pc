/*
 * m68kdec.h - 68000 instruction decoder for the static recompiler.
 *
 * Decodes one instruction from a byte image into a form the C generator can
 * use directly: kind, size, operands with their extension words resolved,
 * and how it changes control flow.
 */
#ifndef HF_M68KDEC_H
#define HF_M68KDEC_H

#include <stdint.h>

/* Effective address modes */
enum {
  EA_NONE = 0,
  EA_DN,     /* Dn */
  EA_AN,     /* An */
  EA_AI,     /* (An) */
  EA_PI,     /* (An)+ */
  EA_PD,     /* -(An) */
  EA_DI,     /* d16(An) */
  EA_IX,     /* d8(An,Xn) */
  EA_AW,     /* abs.w */
  EA_AL,     /* abs.l */
  EA_PCDI,   /* d16(PC) */
  EA_PCIX,   /* d8(PC,Xn) */
  EA_IMM     /* #imm */
};

typedef struct {
  uint8_t  mode;
  uint8_t  reg;      /* An / Dn number */
  uint8_t  xreg;     /* index register 0-15 (D0-D7, A0-A7) */
  uint8_t  xlong;    /* index is .l */
  int32_t  disp;     /* d16 / d8 */
  uint32_t val;      /* abs address, immediate, or PC-relative target base */
} Ea;

/* Instruction kinds */
enum {
  K_INTERP = 0,   /* run by the interpreter's handler (rare instructions) */
  K_ILLEGAL,      /* illegal / line A / line F: exception via the interpreter */
  K_MOVE, K_MOVEA, K_MOVEQ,
  K_ADD, K_SUB, K_AND, K_OR, K_EOR, K_CMP,      /* with dir: 0 = ea->Dn, 1 = Dn->ea */
  K_ADDA, K_SUBA, K_CMPA,
  K_ADDI, K_SUBI, K_ANDI, K_ORI, K_EORI, K_CMPI,
  K_ADDQ, K_SUBQ,
  K_ADDX, K_SUBX,   /* dir: 0 = Dy,Dx  1 = -(Ay),-(Ax) */
  K_CMPM,
  K_NEG, K_NEGX, K_NOT, K_CLR, K_TST,
  K_BTST, K_BCHG, K_BCLR, K_BSET,   /* dir: 0 = bit # in Dn (dreg), 1 = immediate */
  K_SHIFT,          /* register shifts and rotates */
  K_SHIFTM,         /* memory shifts and rotates (word, by 1) */
  K_MULU, K_MULS, K_DIVU, K_DIVS,
  K_EXT, K_SWAP, K_EXG, K_LEA, K_PEA, K_LINK, K_UNLK,
  K_MOVEM,          /* dir: 0 = regs->mem, 1 = mem->regs */
  K_SCC, K_DBCC, K_BCC, K_BRA, K_BSR,
  K_JMP, K_JSR, K_RTS, K_RTR, K_RTE, K_NOP,
  K_MOVE_FROM_SR, K_MOVE_TO_SR, K_MOVE_TO_CCR,
  K_ANDI_SR, K_ORI_SR, K_EORI_SR, K_ANDI_CCR, K_ORI_CCR, K_EORI_CCR,
  K_COUNT
};

/* Shift types for K_SHIFT / K_SHIFTM */
enum { SH_AS = 0, SH_LS = 1, SH_ROX = 2, SH_RO = 3 };

/* Control flow after the instruction */
enum {
  FLOW_NEXT = 0,    /* falls through */
  FLOW_COND,        /* conditional branch: target or next */
  FLOW_JUMP,        /* always goes to target */
  FLOW_CALL,        /* subroutine call to target (returns to next) */
  FLOW_ICALL,       /* call to a computed address */
  FLOW_IJUMP,       /* jump to a computed address */
  FLOW_RETURN,      /* rts / rtr / rte */
  FLOW_STOP         /* exception, stop, illegal: unknown */
};

typedef struct {
  uint32_t addr;
  uint16_t op;
  uint8_t  len;       /* bytes, including the opcode */
  uint8_t  kind;
  uint8_t  size;      /* 1, 2 or 4 (0 when it does not apply) */
  uint8_t  dir;       /* see kinds */
  uint8_t  cond;      /* condition code 0-15 for Bcc / DBcc / Scc */
  uint8_t  flow;
  uint8_t  shtype;    /* SH_* */
  uint8_t  shleft;    /* 1 = left */
  uint8_t  shreg;     /* count in Dn (cnt = register number) */
  uint8_t  cnt;       /* shift count 1-8, quick data 1-8, or register */
  uint8_t  dreg;      /* Dn / An operand of register-direct forms */
  uint16_t mask;      /* movem register list */
  Ea       src, dst;
  uint32_t target;    /* branch / call / jump target when known */
  uint32_t ext[5];    /* raw extension words, in order (for debugging) */
} Insn;

/* Reads a 16-bit big-endian word of the image. Returns 0 past the end. */
typedef uint16_t (*Dec68kRead16)(void *ctx, uint32_t addr);

/* Decodes the instruction at addr. Returns its length in bytes (2..10), or
   0 if addr is odd. Unknown opcodes come back as K_ILLEGAL. */
int dec68k(Dec68kRead16 rd, void *ctx, uint32_t addr, Insn *out);

/* 1 if the opcode is one this decoder treats as a valid instruction. */
int dec68k_valid(uint16_t op);

/* Short mnemonic for listings. */
const char *dec68k_name(const Insn *in);

#endif
