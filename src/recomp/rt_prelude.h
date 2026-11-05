/*
 * rt_prelude.h - the start of every generated file (the recompiler copies
 * it in). Recompiled code works directly on the interpreter's CPU state, so
 * the two can hand over to each other at any instruction.
 *
 * Before this text the generator writes the O_* field offsets of the host's
 * CPU structure, so the layout always matches the build that made it.
 * Everything here is self-contained C: no system headers, so it also builds
 * with the small compiler bundled for players' PCs.
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed char    s8;
typedef signed short   s16;
typedef signed int     s32;

/* One 64 KB page of the 68000 memory map: plain memory, or handlers. */
typedef struct {
  u8  *base;
  u32 (*r8)(u32);
  u32 (*r16)(u32);
  void (*w8)(u32, u32);
  void (*w16)(u32, u32);
} MM;

/* What the host gives generated code (see src/native.c). */
typedef struct {
  void (*exec_op)(u32 op);          /* interpreter handler for one opcode */
  void (*set_sr)(u32 v);            /* may take an interrupt */
  void (*trap)(u32 vector);
  void (*aerr)(u32 addr, u32 mode, u32 op);
  int  (*op_cycles)(u32 op);
} HfHost;

static u8 *C;
static const HfHost *HOST;

#define F32(o)    (*(u32 *)(C + (o)))
#define CYCLES    (*(s32 *)(C + O_cycles))
#define REFRESH   (*(s32 *)(C + O_refresh))
#define CEND      F32(O_cycle_end)
#define DAR       ((u32 *)(C + O_dar))
#define PC        F32(O_pc)
#define IR        F32(O_ir)
#define T1F       F32(O_t1)
#define SF        F32(O_s)
#define XF        F32(O_x)
#define NF        F32(O_n)
#define ZF        F32(O_z)
#define VF        F32(O_v)
#define CF        F32(O_c)
#define IMASK     F32(O_int_mask)
#define AERR_EN   F32(O_aerr)
#define MMAP      ((MM *)(C + O_mm))

/* ---- Musashi's helper macros (same meaning as in m68kcpu.h) ---- */
#define MUL 7
#define USE_CYCLES(A) CYCLES += (A)
#define MASK_OUT_ABOVE_8(A)  ((A) & 0xff)
#define MASK_OUT_ABOVE_16(A) ((A) & 0xffff)
#define MASK_OUT_ABOVE_32(A) (A)
#define MASK_OUT_BELOW_8(A)  ((A) & ~0xff)
#define MASK_OUT_BELOW_16(A) ((A) & ~0xffff)
#define MAKE_INT_8(A)  (s8)(A)
#define MAKE_INT_16(A) (s16)(A)
#define GET_MSB_8(A)  ((A) & 0x80)
#define GET_MSB_16(A) ((A) & 0x8000)
#define GET_MSB_32(A) ((A) & 0x80000000)
#define LSL(A, C) ((A) << (C))
#define LSR(A, C) ((A) >> (C))
#define LSR_32(A, C) ((C) < 32 ? (A) >> (C) : 0)
#define LSL_32(A, C) ((C) < 32 ? (A) << (C) : 0)
#define ROL_8(A, C)   MASK_OUT_ABOVE_8(LSL(A, C) | LSR(A, 8-(C)))
#define ROL_9(A, C)                   (LSL(A, C) | LSR(A, 9-(C)))
#define ROL_16(A, C) MASK_OUT_ABOVE_16(LSL(A, C) | LSR(A, 16-(C)))
#define ROL_17(A, C)                  (LSL(A, C) | LSR(A, 17-(C)))
#define ROL_32(A, C) MASK_OUT_ABOVE_32(LSL_32(A, C) | LSR_32(A, 32-(C)))
#define ROL_33(A, C)                  (LSL_32(A, C) | LSR_32(A, 33-(C)))
#define ROR_8(A, C)   MASK_OUT_ABOVE_8(LSR(A, C) | LSL(A, 8-(C)))
#define ROR_9(A, C)                   (LSR(A, C) | LSL(A, 9-(C)))
#define ROR_16(A, C) MASK_OUT_ABOVE_16(LSR(A, C) | LSL(A, 16-(C)))
#define ROR_17(A, C)                  (LSR(A, C) | LSL(A, 17-(C)))
#define ROR_32(A, C) MASK_OUT_ABOVE_32(LSR_32(A, C) | LSL_32(A, 32-(C)))
#define ROR_33(A, C)                  (LSR_32(A, C) | LSL_32(A, 33-(C)))
#define NFLAG_8(A)  (A)
#define NFLAG_16(A) ((A)>>8)
#define NFLAG_32(A) ((A)>>24)
#define CFLAG_8(A)  (A)
#define CFLAG_16(A) ((A)>>8)
#define CFLAG_ADD_32(S, D, R) (((S & D) | (~R & (S | D)))>>23)
#define CFLAG_SUB_32(S, D, R) (((S & R) | (~D & (S | R)))>>23)
#define VFLAG_ADD_8(S, D, R)  ((S^R) & (D^R))
#define VFLAG_ADD_16(S, D, R) (((S^R) & (D^R))>>8)
#define VFLAG_ADD_32(S, D, R) (((S^R) & (D^R))>>24)
#define VFLAG_SUB_8(S, D, R)  ((S^D) & (R^D))
#define VFLAG_SUB_16(S, D, R) (((S^D) & (R^D))>>8)
#define VFLAG_SUB_32(S, D, R) (((S^D) & (R^D))>>24)
#define NFLAG_SET   0x80
#define NFLAG_CLEAR 0
#define CFLAG_SET   0x100
#define CFLAG_CLEAR 0
#define XFLAG_SET   0x100
#define XFLAG_CLEAR 0
#define VFLAG_SET   0x80
#define VFLAG_CLEAR 0
#define ZFLAG_SET   0
#define ZFLAG_CLEAR 0xffffffff
#define CYC_SHIFT   (2 * MUL)
#define FLAG_X XF
#define FLAG_N NF
#define FLAG_Z ZF
#define FLAG_V VF
#define FLAG_C CF
#define XFLAG_AS_1() ((XF>>8)&1)

static int hf_abs(int v) { return v < 0 ? (int)(0u - (u32)v) : v; }
#define abs hf_abs

static const u8 m68ki_shift_8_table[65] = {
  0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff
};
static const u16 m68ki_shift_16_table[65] = {
  0x0000, 0x8000, 0xc000, 0xe000, 0xf000, 0xf800, 0xfc00, 0xfe00, 0xff00,
  0xff80, 0xffc0, 0xffe0, 0xfff0, 0xfff8, 0xfffc, 0xfffe, 0xffff, 0xffff,
  0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
  0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
  0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
  0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
  0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
  0xffff, 0xffff
};
static const u32 m68ki_shift_32_table[65] = {
  0x00000000, 0x80000000, 0xc0000000, 0xe0000000, 0xf0000000, 0xf8000000,
  0xfc000000, 0xfe000000, 0xff000000, 0xff800000, 0xffc00000, 0xffe00000,
  0xfff00000, 0xfff80000, 0xfffc0000, 0xfffe0000, 0xffff0000, 0xffff8000,
  0xffffc000, 0xffffe000, 0xfffff000, 0xfffff800, 0xfffffc00, 0xfffffe00,
  0xffffff00, 0xffffff80, 0xffffffc0, 0xffffffe0, 0xfffffff0, 0xfffffff8,
  0xfffffffc, 0xfffffffe, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
  0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
  0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
  0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
  0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
  0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
};

/* ---- memory: the same paths as m68ki_read_* / m68ki_write_* ---- */
static u32 R8(u32 a)
{
  MM *m = &MMAP[(a >> 16) & 0xff];
  if (m->r8) return m->r8(a & 0xffffff);
  return m->base[(a & 0xffff) ^ 1];
}
static u32 R16(u32 a)
{
  MM *m = &MMAP[(a >> 16) & 0xff];
  if (m->r16) return m->r16(a & 0xffffff);
  return *(u16 *)(m->base + (a & 0xffff));
}
/* immediate / PC-relative reads: memory only, never a handler */
#define RI16(a) (*(u16 *)(MMAP[((a) >> 16) & 0xff].base + ((a) & 0xffff)))
static u32 R32(u32 a)
{
  MM *m = &MMAP[(a >> 16) & 0xff];
  if (m->r16) return (m->r16(a & 0xffffff) << 16) | m->r16((a + 2) & 0xffffff);
  return ((u32)RI16(a) << 16) | RI16(a + 2);
}
static u32 RP8(u32 a) { return MMAP[(a >> 16) & 0xff].base[(a & 0xffff) ^ 1]; }
static u32 RP16(u32 a) { return RI16(a); }
static u32 RP32(u32 a) { return ((u32)RI16(a) << 16) | RI16(a + 2); }
static void W8(u32 a, u32 v)
{
  MM *m = &MMAP[(a >> 16) & 0xff];
  if (m->w8) m->w8(a & 0xffffff, v);
  else m->base[(a & 0xffff) ^ 1] = (u8)v;
}
static void W16(u32 a, u32 v)
{
  MM *m = &MMAP[(a >> 16) & 0xff];
  if (m->w16) m->w16(a & 0xffffff, v);
  else *(u16 *)(m->base + (a & 0xffff)) = (u16)v;
}
static void W32(u32 a, u32 v)
{
  W16(a, v >> 16);
  W16(a + 2, v & 0xffff);
}

#define MODE_READ  0x10
#define MODE_WRITE 0

/* SR / CCR */
#define GET_CCR() ( ((XF & 0x100) >> 4) | ((NF & 0x80) >> 4) | ((!ZF) << 2) | ((VF & 0x80) >> 6) | ((CF & 0x100) >> 8) )
#define GET_SR()  ( T1F | (SF << 11) | IMASK | GET_CCR() )
#define SET_CCR(v) do { u32 v_ = (v); XF = (v_ & 0x10) << 4; NF = (v_ & 0x08) << 4; ZF = !(v_ & 0x04); VF = (v_ & 0x02) << 6; CF = (v_ & 0x01) << 8; } while (0)

/* conditions 0-15 */
#define CC_T  1
#define CC_F  0
#define CC_HI (!(CF & 0x100) && ZF)
#define CC_LS ((CF & 0x100) || !ZF)
#define CC_CC (!(CF & 0x100))
#define CC_CS (CF & 0x100)
#define CC_NE (ZF)
#define CC_EQ (!ZF)
#define CC_VC (!(VF & 0x80))
#define CC_VS (VF & 0x80)
#define CC_PL (!(NF & 0x80))
#define CC_MI (NF & 0x80)
#define CC_GE (!((NF ^ VF) & 0x80))
#define CC_LT ((NF ^ VF) & 0x80)
#define CC_GT (!((NF ^ VF) & 0x80) && ZF)
#define CC_LE (((NF ^ VF) & 0x80) || !ZF)

/* At every instruction: stop when this line's cycles are used up, then
   the 68000's bus refresh delay, exactly as the interpreter's loop does. */
#define STEP(a) \
  if ((u32)CYCLES >= CEND) { PC = (a); return 1; } \
  if (CYCLES >= REFRESH + 896) { REFRESH = (CYCLES / 896) * 896; CYCLES += 14; }
