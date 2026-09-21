// XDE v2.00 by Fyyre - eXtended disassembler engine (x86 / x86-64 / VEX / EVEX / XOP)

// Successor to z0mbie's XDE 1.02: instruction length, split/merge, and source/destination object sets.
// Original 1.02 sources present in xde102.

#include "xde.h"
#include "xdetbl.h"

#include <string.h>

typedef struct {
    const uint8_t *beg;
    const uint8_t *p;
    const uint8_t *end;
} xde_cur;

static int cur_left(const xde_cur *c)
{
    size_t a = (size_t)(c->end - c->p);
    size_t b = (size_t)(c->beg + XDE_MAXLEN - c->p);
    return (int)(a < b ? a : b);
}

static int get_byte(xde_cur *c, uint8_t *out)
{
    if (cur_left(c) < 1)
        return 0;
    *out = *c->p++;
    return 1;
}

static int peek_byte(const xde_cur *c, unsigned off, uint8_t *out)
{
    if (cur_left(c) < (int)off + 1)
        return 0;
    *out = c->p[off];
    return 1;
}

// Map (size, register number, REX-present) to one object-set bit.
// regs 16..31 are APX EGPRs and are reported through *egpr, which points into
// the second set word; pass NULL where EGPR cannot occur (fixed registers).
static uint64_t gp_set(int sz, unsigned reg, int rex, uint64_t *egpr)
{
    static const uint64_t lo8_norex[8] = {
        XSET_AL, XSET_CL, XSET_DL, XSET_BL,
        XSET_AH, XSET_CH, XSET_DH, XSET_BH
    };
    static const uint64_t lo8_rex[8] = {
        XSET_AL, XSET_CL, XSET_DL, XSET_BL,
        XSET_SPL, XSET_BPL, XSET_SIL, XSET_DIL
    };
    static const uint64_t w16[8] = {
        XSET_AX, XSET_CX, XSET_DX, XSET_BX,
        XSET_SP, XSET_BP, XSET_SI, XSET_DI
    };
    static const uint64_t w32[8] = {
        XSET_EAX, XSET_ECX, XSET_EDX, XSET_EBX,
        XSET_ESP, XSET_EBP, XSET_ESI, XSET_EDI
    };
    static const uint64_t w64[8] = {
        XSET_RAX, XSET_RCX, XSET_RDX, XSET_RBX,
        XSET_RSP, XSET_RBP, XSET_RSI, XSET_RDI
    };

    if (reg >= 16) {
        if (reg > 31)
            return XSET_OTHER;
        if (egpr)
            *egpr |= XSET2_R16 << (reg - 16);
        return 0;
    }
    if (reg >= 8) {
        // 8-bit access to r8..r15: add the width-specific second-word bit, the
        // first word keeps reporting the width-agnostic register.
        if (sz <= 1 && egpr)
            *egpr |= XSET2_R8B << (reg - 8);
        return XSET_R8 << (reg - 8);
    }

    if (sz <= 1)
        return rex ? lo8_rex[reg] : lo8_norex[reg];
    if (sz == 2)
        return w16[reg];
    if (sz == 4)
        return w32[reg];
    if (sz == 8)
        return w64[reg];
    return XSET_OTHER;   // unexpected size: do not claim 64-bit
}

static uint64_t stack_set(unsigned mode)
{
    if (mode == 16)
        return XSET_SP;
    if (mode == 32)
        return XSET_ESP;
    return XSET_RSP;
}

static unsigned imm_bytes(uint32_t attr, const struct xde_instr *diza)
{
    unsigned kind = (attr & XA_IMM_MASK);
    unsigned data = diza->defdata;

    switch (kind) {
    case XA_IMM_IB:     return 1;
    case XA_IMM_IW:     return 2;
    case XA_IMM_IZ:
        if (diza->mode == 64 && (attr & XA_F64))
            return 4;
        return data == 2 ? 2u : 4u;
    case XA_IMM_IV:
        return data;
    case XA_IMM_AP:
        return (data == 2 ? 2u : 4u) + 2u;
    case XA_IMM_ENTER:  return 3;
    case XA_IMM_ID:     return 4;
    default:            return 0;
    }
}

static void apply_attr_flags(struct xde_instr *diza, uint32_t attr)
{
    uint64_t f = diza->flag;
    if (attr & XA_MODRM)  f |= C_MODRM;
    if (attr & XA_REL)    f |= C_REL;
    if (attr & XA_STOP)   f |= C_STOP;
    if (attr & XA_UNDEF)  f |= C_UNDEF;
    if (attr & XA_BAD)    f |= C_BAD;
    if (attr & XA_OPSZ8)  f |= C_OPSZ8;
    if (attr & XA_PUSH)   f |= C_PUSH;
    if (attr & XA_POP)    f |= C_POP;
    if (attr & XA_I64)    f |= C_I64;
    if (attr & XA_O64)    f |= C_O64;
    if (attr & XA_F64)    f |= C_F64;
    if (attr & XA_D64)    f |= C_D64;
    if (attr & XA_3DNOW)  f |= C_3DNOW;
    if (attr & XA_CALL)   f |= C_CMD_CALL;
    if (attr & XA_JMP)    f |= C_CMD_JMP;
    if (attr & XA_JCC)    f |= C_CMD_JCC;
    if (attr & XA_RET)    f |= C_CMD_RET;
    diza->flag = f;
}

static void apply_usage_special(struct xde_instr *diza, uint32_t attr,
                                unsigned opcode, unsigned opcode2)
{
    unsigned mode = diza->mode;
    unsigned data = diza->defdata;
    unsigned addr = diza->defaddr;
    uint64_t xset;
    uint8_t c = (uint8_t)opcode;

    // REP/REPE/REPNE: CX/ECX/RCX is src and dst, but only on the string ops.
    // On everything else F2/F3 is an opcode selector rather than a repeat
    // (SSE scalars, PAUSE, ENDBR, CRC32), so it names no count register.
    if (diza->p_rep) {
        int str = (c == 0xA4 || c == 0xA5 || c == 0xA6 || c == 0xA7 ||
                   c == 0xAA || c == 0xAB || c == 0xAC || c == 0xAD ||
                   c == 0xAE || c == 0xAF || c == 0x6C || c == 0x6D ||
                   c == 0x6E || c == 0x6F);
        if (str) {
            xset = (mode == 64) ? XSET_RCX : (addr == 2 ? XSET_CX : XSET_ECX);
            diza->src_set |= xset;
            diza->dst_set |= xset;
        }
    }

    if (diza->map == XDE_MAP_LEGACY) {
        if ((c == 0xA4) || (c == 0xA5) || (c == 0xA6) || (c == 0xA7)) {
            xset = addr == 2 ? (XSET_SI | XSET_DI)
                 : addr == 4 ? (XSET_ESI | XSET_EDI)
                             : (XSET_RSI | XSET_RDI);
            diza->src_set |= xset;
            diza->dst_set |= xset;
        }
        if ((c == 0xAC) || (c == 0xAD)) {
            xset = addr == 2 ? XSET_SI : addr == 4 ? XSET_ESI : XSET_RSI;
            diza->src_set |= xset;
            diza->dst_set |= xset;
        }
        if ((c == 0xAA) || (c == 0xAB) || (c == 0xAE) || (c == 0xAF)) {
            xset = addr == 2 ? XSET_DI : addr == 4 ? XSET_EDI : XSET_RDI;
            diza->src_set |= xset;
            diza->dst_set |= xset;
        }
        if ((c == 0x6C) || (c == 0x6D)) {
            xset = XSET_DEV | (addr == 2 ? XSET_DI : addr == 4 ? XSET_EDI : XSET_RDI);
            diza->src_set |= xset | XSET_DX;
            diza->dst_set |= xset;
        }
        if ((c == 0x6E) || (c == 0x6F)) {
            xset = XSET_DEV | (addr == 2 ? XSET_SI : addr == 4 ? XSET_ESI : XSET_RSI);
            diza->src_set |= xset | XSET_DX;
            diza->dst_set |= xset;
        }
        // SAHF reads AH and writes the flags; LAHF is the other way round.
        if (c == 0x9E) {
            diza->src_set |= XSET_AH;
            diza->dst_set |= XSET_FL;
        }
        if (c == 0x9F) {
            diza->src_set |= XSET_FL;
            diza->dst_set |= XSET_AH;
        }
        if (c == 0x98) {
            if (data == 2) { diza->src_set |= XSET_AL;  diza->dst_set |= XSET_AX; }
            else if (data == 4) { diza->src_set |= XSET_AX;  diza->dst_set |= XSET_EAX; }
            else { diza->src_set |= XSET_EAX; diza->dst_set |= XSET_RAX; }
        }
        if (c == 0x99) {
            if (data == 2) { diza->src_set |= XSET_AX;  diza->dst_set |= XSET_DX; }
            else if (data == 4) { diza->src_set |= XSET_EAX; diza->dst_set |= XSET_EDX; }
            else { diza->src_set |= XSET_RAX; diza->dst_set |= XSET_RDX; }
        }
        if ((c == 0x37) || (c == 0x3F)) {
            diza->src_set |= XSET_AH;
            diza->dst_set |= XSET_AH;
        }
        if ((c == 0xD4) || (c == 0xD5)) {
            diza->src_set |= (c == 0xD4) ? XSET_AL : XSET_AX;
            diza->dst_set |= XSET_AX;
        }
        if (c == 0x60)
            diza->src_set |= data == 2 ? XSET_ALL16 : XSET_ALL32;
        if (c == 0x61)
            diza->dst_set |= data == 2 ? XSET_ALL16 : XSET_ALL32;
        if ((c == 0xE4) || (c == 0xE5) || (c == 0xE6) || (c == 0xE7) ||
            (c == 0xEC) || (c == 0xED) || (c == 0xEE) || (c == 0xEF)) {
            diza->src_set |= XSET_DEV;
            diza->dst_set |= XSET_DEV;
            // The DX-port forms (EC/ED IN, EE/EF OUT) always read the port
            // number from DX, so it belongs in src for all four.
            if ((c == 0xEC) || (c == 0xED) || (c == 0xEE) || (c == 0xEF))
                diza->src_set |= XSET_DX;
        }
        if ((c == 0x06) || (c == 0x0E) || (c == 0x16) || (c == 0x1E))
            diza->src_set |= XSET_OTHER;
        if ((c == 0x07) || (c == 0x17) || (c == 0x1F) || (c == 0xC4) || (c == 0xC5))
            diza->dst_set |= XSET_OTHER;
        if (c == 0xD7)
            diza->src_set |= addr == 2 ? XSET_BX : addr == 4 ? XSET_EBX : XSET_RBX;
        if ((c == 0xC8) || (c == 0xC9)) {
            xset = stack_set(mode) | (mode == 16 ? XSET_BP : mode == 32 ? XSET_EBP : XSET_RBP);
            diza->src_set |= xset;
            diza->dst_set |= xset;
        }
        if (c == 0x8C) diza->src_set |= XSET_OTHER;
        if (c == 0x8E) diza->dst_set |= XSET_OTHER;
        // CMPS (A6/A7) and SCAS (AE/AF) write FL; with a REP prefix the loop
        // also reads ZF. CLD (FC) / STD (FD) write DF, which folds into FL.
        // MOVS/STOS/LODS/INS/OUTS never touch flags, and DF is deliberately
        // not reported as a source (see xde102/todo).
        if ((c == 0xA6) || (c == 0xA7) || (c == 0xAE) || (c == 0xAF) ||
            (c == 0xFC) || (c == 0xFD))
            diza->dst_set |= XSET_FL;
        if (diza->p_rep &&
            ((c == 0xA6) || (c == 0xA7) || (c == 0xAE) || (c == 0xAF)))
            diza->src_set |= XSET_FL;   // REP loop tests ZF
        // Jcc and LOOP/JCXZ read only what they test: the flags, and the count
        // in CX/ECX/RCX (which LOOP also decrements). Nothing else is unknown,
        // so these carry no XA_UNDEF.
        if ((c >= 0x70 && c <= 0x7F) || (c == 0xE0) || (c == 0xE1))
            diza->src_set |= XSET_FL;
        if (c >= 0xE0 && c <= 0xE3) {
            xset = (mode == 64) ? XSET_RCX : (addr == 2 ? XSET_CX : XSET_ECX);
            diza->src_set |= xset;
            if (c != 0xE3)
                diza->dst_set |= xset;   // LOOP decrements the count
        }
        // C6 F8 is XABORT rather than the group's MOV r/m8,imm8 form: the
        // immediate ends up in bits 31:24 of EAX (SDM), which makes EAX the
        // one operand and the byte ModR/M names not an operand at all. The
        // sets built from the ModR/M byte above are replaced wholesale.
        if (c == 0xC6 && (diza->enc == XDE_ENC_LEGACY ||
                          diza->enc == XDE_ENC_REX2) && diza->modrm == 0xF8) {
            diza->src_set = 0;
            diza->src_set2 = 0;
            diza->dst_set = XSET_EAX;
            diza->dst_set2 = 0;
        }
    } else if (diza->map == XDE_MAP_0F) {
        uint8_t c2 = (uint8_t)opcode2;
        if ((c2 == 0xB2) || (c2 == 0xB4) || (c2 == 0xB5) || (c2 == 0xA1) || (c2 == 0xA9))
            diza->dst_set |= XSET_OTHER;
        if ((c2 == 0xA0) || (c2 == 0xA8))
            diza->src_set |= XSET_OTHER;
        // CPUID selects its leaf with EAX and, on the leaves that have them,
        // its sub-leaf with ECX: both are inputs. It reports back in all four
        // of EAX/EBX/ECX/EDX.
        if (c2 == 0xA2) {
            diza->src_set |= XSET_EAX | XSET_ECX;
            diza->dst_set |= XSET_EAX | XSET_EBX | XSET_ECX | XSET_EDX;
        }
        if ((c2 == 0xA5) || (c2 == 0xAD))
            diza->src_set |= XSET_CL;
        // SETcc tests the flags it was chosen for; its r/m8 is write-only.
        if (c2 >= 0x90 && c2 <= 0x9F)
            diza->src_set |= XSET_FL;
        if (c2 >= 0x80 && c2 <= 0x8F)
            diza->src_set |= XSET_FL;   // Jcc rel32 reads FL
    }

    (void)attr;
}

// The table marks 0F 00 / 0F 01 / 0F 02 / 0F 03 unknown as a whole, which is
// what the group needed while none of its forms were modelled. The SDM does
// define them, so this rebuilds their operand sets from the ModR/M byte, after
// the whole-set unknown has been assigned:
//
//   0F 00 /0 SLDT, /1 STR    r/m16 written
//   0F 00 /2 LLDT, /3 LTR    r/m16 read (fixed 16-bit), LDTR/TR written
//   0F 00 /4 VERR, /5 VERW   r/m16 read (fixed 16-bit), ZF written
//   0F 01 /0 SGDT, /1 SIDT   memory written (memory only: mod=3 is #UD)
//   0F 01 /2 LGDT, /3 LIDT   memory read (memory only), GDTR/IDTR written
//   0F 01 /4 SMSW            r/m16 written
//   0F 01 /6 LMSW            r/m16 read (fixed 16-bit), CR0 written
//   0F 01 /7 INVLPG          memory read (memory only)
//   0F 02 LAR, 0F 03 LSL     reg written at the operand size, r/m16 read, ZF
//
// SLDT/STR/SMSW write their register destination at the operand size, because
// the SDM zero-extends the 16-bit selector into a 64-bit destination, clears
// (or leaves undefined) the high half of a 32-bit one, and zero-extends CR0
// into the 32-bit form of SMSW. The r/m16 operands that are only read keep the
// 16 bits they consume: LLDT/LTR/VERR/VERW/LMSW fix their operand size at 16
// bits, and LAR/LSL use only the selector's low 16 bits.
//
// 0F 00 /6 /7, 0F 01 /5, the register forms of SGDT/SIDT/LGDT/LIDT/INVLPG
// (which the SDM makes #UD) and the register forms of 0F 01 (SERIALIZE,
// RDPKRU/WRPKRU, SWAPGS/RDTSCP, MONITOR/MWAIT, CLAC/STAC, XGETBV/XSETBV,
// VMFUNC/XEND/XTEST, the VMX and SVM groups) stay undefined and keep C_UNDEF;
// a VEX/EVEX/XOP encoding never defines them either, and falls through here.
//
// ea_set / ea_set2 hold the addressing registers parse_modrm recorded, which
// the memory forms keep; the register forms have none. Returns 1 when the
// form was modelled.
static int undef_sys_operands(struct xde_instr *diza, uint64_t ea_set,
                              uint64_t ea_set2)
{
    unsigned mod = diza->modrm >> 6;
    unsigned reg = (diza->modrm >> 3) & 7;
    unsigned rm = diza->modrm & 7;
    int rex = (diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY);
    int sz = (int)diza->defdata;
    int dsz = (diza->mode == 64 && sz == 4) ? 8 : sz;
    // ModR/M.reg / r/m with their REX/REX2 extension bits (r16-r31 need bit 4).
    unsigned regx = ((unsigned)diza->rex_r4 << 4) |
                    ((unsigned)diza->rex_r << 3) | reg;
    unsigned rmreg = ((unsigned)diza->rex_b4 << 4) |
                     ((unsigned)diza->rex_b << 3) | rm;
    uint8_t c2 = diza->opcode2;
    uint64_t src = 0, dst = 0, src2 = 0, dst2 = 0;
    uint64_t greg = 0, grm = 0;

    if (diza->map != XDE_MAP_0F || (diza->enc != XDE_ENC_LEGACY &&
                                    diza->enc != XDE_ENC_REX2))
        return 0;

    if (c2 == 0x02 || c2 == 0x03) {             // LAR / LSL
        dst = gp_set(dsz, regx, rex, &greg) | XSET_FL;
        dst2 = greg;
        if (mod == 3) {
            src = gp_set(2, rmreg, rex, &grm);
            src2 = grm;
        } else {
            src = XSET_MEM;                     // 16 bits of the descriptor
        }
    } else if (c2 == 0x00) {
        if (reg <= 1) {                         // SLDT / STR
            if (mod == 3) {
                dst = gp_set(dsz, rmreg, rex, &grm);
                dst2 = grm;
            } else {
                dst = XSET_MEM;
            }
        } else if (reg <= 5) {                  // LLDT / LTR / VERR / VERW
            if (mod == 3) {
                src = gp_set(2, rmreg, rex, &grm);
                src2 = grm;
            } else {
                src = XSET_MEM;
            }
            dst = (reg <= 3) ? XSET_OTHER : XSET_FL;
        } else {
            return 0;                           // /6 and /7 are not defined
        }
    } else if (c2 == 0x01) {
        if (mod == 3) {
            // The register forms of this group belong to the instructions left
            // above, except SMSW /4 and LMSW /6, whose r/m16 the SDM defines
            // with a register operand.
            if (reg == 4) {
                dst = gp_set(dsz, rmreg, rex, &grm);
                dst2 = grm;
            } else if (reg == 6) {
                src = gp_set(2, rmreg, rex, &grm);
                src2 = grm;
                dst = XSET_OTHER;
            } else {
                return 0;
            }
        } else if (reg <= 1) {                  // SGDT / SIDT
            dst = XSET_MEM;
        } else if (reg <= 3) {                  // LGDT / LIDT
            src = XSET_MEM;
            dst = XSET_OTHER;
        } else if (reg == 4) {                  // SMSW
            dst = XSET_MEM;
        } else if (reg == 6) {                  // LMSW
            src = XSET_MEM;
            dst = XSET_OTHER;
        } else if (reg == 7) {                  // INVLPG
            src = XSET_MEM;
        } else {
            return 0;                           // /5
        }
    } else {
        return 0;
    }

    diza->src_set = ea_set | src;
    diza->src_set2 = ea_set2 | src2;
    diza->dst_set = dst;
    diza->dst_set2 = dst2;
    return 1;
}

static void apply_modrm_usage(struct xde_instr *diza, uint32_t attr,
                              unsigned mod, unsigned reg, unsigned rm)
{
    int rex = (diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY);
    int opsz8 = (attr & XA_OPSZ8) != 0;
    int sz = opsz8 ? 1 : (int)diza->defdata;
    int dsz = sz;
    uint8_t c = diza->opcode;
    uint8_t c2 = diza->opcode2;
    // ModR/M.reg with its REX/REX2 extension bits (r16-r31 need bit 4).
    unsigned regx = ((unsigned)diza->rex_r4 << 4) |
                    ((unsigned)diza->rex_r << 3) | reg;
    // SETcc (0F 90-9F) only writes its r/m8 and tests the flags.
    int setcc = (diza->map == XDE_MAP_0F && c == 0x0F && c2 >= 0x90 && c2 <= 0x9F);
    // 0F 38 F0/F1: MOVBE (reg is the destination on the load form, the source
    // on the store form) and CRC32, which marks reg-as-destination with the
    // F2/F3 prefix it needs anyway. opcode2 holds the 38 escape byte for this
    // map, so the real opcode is opcode3.
    int reg_dst = (diza->map == XDE_MAP_0F38 &&
                   (diza->opcode3 == 0xF0 ||
                    (diza->opcode3 == 0xF1 && diza->p_rep != 0)));
    int reg_src = (diza->map == XDE_MAP_0F38 && diza->opcode3 == 0xF1 &&
                   diza->p_rep == 0);
    // Legacy SSE/MMX share the 0F map with the GPR opcodes; anything outside
    // this GPR set has SIMD operands, which collapse to XSET_OTHER the same
    // way the vector encodings do. For this map opcode2 holds the 0F opcode.
    int simd_0f = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                   !((c2 >= 0x20 && c2 <= 0x23) || c2 == 0xA3 ||
                     c2 == 0xA4 || c2 == 0xA5 || c2 == 0xAB ||
                     c2 == 0xAC || c2 == 0xAD || c2 == 0xAF || c2 == 0xB0 ||
                     c2 == 0xB1 || c2 == 0xB2 || c2 == 0xB3 || c2 == 0xB4 ||
                     c2 == 0xB5 || c2 == 0xB6 || c2 == 0xB7 || c2 == 0xB8 ||
                     c2 == 0xBA || c2 == 0xBB || c2 == 0xBC || c2 == 0xBD ||
                     c2 == 0xBE || c2 == 0xBF || c2 == 0xC0 || c2 == 0xC1 ||
                     c2 == 0xC3 || (c2 >= 0x40 && c2 <= 0x4F) ||
                     (c2 >= 0x90 && c2 <= 0x9F)));
    // The 0F groups below keep an instruction selector in the reg field
    // instead of a register operand, so the generic reg rule above and the
    // generic mod==3 / memory rules below must be told to stand aside.
    // 0F 20-23 MOV to/from CR/DR: r/m is always a GPR; /20 /21 read CR/DR into
    // it, /22 /23 write it into CR/DR, which folds into OTHER.
    int mov_crdr = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                    c2 >= 0x20 && c2 <= 0x23);
    // 0F C7 /6 /7 with mod=3 is RDRAND/RDSEED: r/m is a plain GPR destination
    // and there is no source operand. The mod!=3 forms of /6 /7 are
    // VMPTRLD/VMPTRST and keep the generic memory path.
    int rdrand = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                  c2 == 0xC7 && mod == 3 && (reg == 6 || reg == 7));
    // 0F AE with mod=3: /6 /7 are MFENCE/SFENCE, which take no operand at
    // all, /5 without a prefix is LFENCE, and /0-/3 with the F3 prefix are the
    // FS/GS base moves, whose r/m is a GPR.
    // F3 0F AE /5 is INCSSPD/INCSSPQ: it shares mod=3 /5 with LFENCE but does
    // read its r/m operand, so it must stay out of the fence rule.
    int incssp = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                  c2 == 0xAE && mod == 3 && reg == 5 && diza->p_rep == 0xF3);
    // WAITPKG picks its instruction from the prefix on the shared /6 field:
    // F3 0F AE /6 is UMONITOR, whose r/m GPR holds the address to monitor,
    // F2 0F AE /6 is UMWAIT and 66 0F AE /6 is TPAUSE, whose r/m GPR holds
    // the optimized-state hint. All three require mod=11 and all three read
    // their r/m operand, so none of them is the operand-less MFENCE; UMWAIT
    // and TPAUSE additionally read the EDX:EAX deadline.
    int umonitor = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                    c2 == 0xAE && mod == 3 && reg == 6 && diza->p_rep == 0xF3);
    int umwait = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                  c2 == 0xAE && mod == 3 && reg == 6 && diza->p_rep == 0xF2);
    int tpause = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                  c2 == 0xAE && mod == 3 && reg == 6 && diza->p_rep == 0 &&
                  diza->p_66 != 0);
    int waitpkg = umonitor || umwait || tpause;
    int fence = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                 c2 == 0xAE && mod == 3 && reg >= 5 && !incssp && !waitpkg);
    // F3 0F AE /4 is PTWRITE: the r/m GPR (mod=3) or the memory operand is
    // read and encoded into a processor trace packet, so the instruction has
    // a source but no destination operand. Its register form needs no branch
    // of its own below: with the reg field out of the way it is the generic
    // r/m read, and PTWRITE touches no flags.
    int ptwrite = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                   c2 == 0xAE && reg == 4 && diza->p_rep == 0xF3);
    // F3 0F AE /6 with a memory operand is not UMONITOR -- that form needs
    // mod=11 -- but the CET CLRSSBSY, which clears the busy flag of a
    // supervisor shadow stack token: the m64 is read and written and CF
    // reports an invalid token. It takes no EDX:EAX state-component mask.
    int clrssbsy = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                    c2 == 0xAE && mod != 3 && reg == 6 && diza->p_rep == 0xF3);
    int fsgsbase = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                    c2 == 0xAE && mod == 3 && reg <= 3 &&
                    diza->p_rep == 0xF3);
    // F3 0F 1E FA/FB is ENDBR64/ENDBR32: no operands at all.
    int endbr = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                 c2 == 0x1E && mod == 3 && reg == 7 && diza->p_rep == 0xF3);
    // 0F 1E with no rep prefix is NOP Ev, the same class as 0F 1F: neither
    // the r/m register nor the memory operand is accessed. Its F3-prefixed
    // forms are ENDBR64/32 and RDSSPD/RDSSPQ, whose r/m is a plain GPR that
    // receives the shadow stack pointer.
    int nop1e = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                 c2 == 0x1E && diza->p_rep == 0);
    int rdssp = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                 c2 == 0x1E && mod == 3 && reg == 0 && diza->p_rep == 0xF3);
    // 0F 18 /4-/7, 0F 19, 0F 1D and 0F 1F are NOPs: their r/m operand is not
    // accessed, register or memory (parse_modrm still records the address
    // registers). 0F 18 /0-/3 (PREFETCHNTA/PREFETCHT0/T1/T2) and 0F 0D /0 /1
    // (PREFETCHW, PREFETCHWT1) do read memory but name no register operand
    // either; their reserved mod=3 encodings keep the generic path.
    int nop_ea = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                  (c2 == 0x19 || c2 == 0x1D || c2 == 0x1F ||
                   (c2 == 0x18 && reg >= 4)));
    int prefetch_ea = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                       mod != 3 &&
                       ((c2 == 0x18 && reg <= 3) ||
                        (c2 == 0x0D && (reg == 0 || reg == 1))));
    // Memory forms that store into their r/m operand: 0F AE /0 /3 /4 /6
    // (FXSAVE, STMXCSR, XSAVE, XSAVEOPT, and CLWB which is /6 with a 66
    // prefix) and 0F C7 /1 /3 /4 /5 /7 (CMPXCHG8B/16B, XRSTORS, XSAVEC,
    // XSAVES, VMPTRST, which writes the VMCS pointer to memory). Their
    // read-only siblings FXRSTOR /1, LDMXCSR /2, XRSTOR /5, VMPTRLD /6 and
    // PTWRITE /4 (which only reads the traced data) stay source-only.
    int mem_store = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                     mod != 3 &&
                     ((c2 == 0xAE &&
                       (reg == 0 || reg == 3 || (reg == 4 && !ptwrite) ||
                        reg == 6)) ||
                      (c2 == 0xC7 &&
                       (reg == 1 || reg == 3 || reg == 4 || reg == 5 ||
                        reg == 7))));
    // Forms whose reg field is not a register operand at all.
    int reg_not_dst = rdrand || fence || incssp || fsgsbase || endbr || nop1e ||
                      nop_ea || prefetch_ea || rdssp || ptwrite || waitpkg;
    // 0F C7 /1 with a memory operand is CMPXCHG8B, or CMPXCHG16B once REX.W
    // widens the operation. Both compare and conditionally load the EDX:EAX
    // pair (RDX:RAX for the 128-bit form), which is the one register pair the
    // r/m rule below cannot see. CMPXCHG8B keeps its 32-bit halves even in
    // 64-bit mode, so REX.W alone selects the 64-bit pair.
    int cmpxchg8b = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                     c2 == 0xC7 && mod != 3 && reg == 1);
    // XSAVE/XSAVEOPT/XSAVEC/XSAVES and XRSTOR/XRSTORS all read the
    // state-component mask from EDX:EAX. The remaining memory forms of these
    // groups (FXSAVE/FXRSTOR, LDMXCSR/STMXCSR, CLFLUSH/CLWB -- /6 with the 66
    // prefix -- and the VMCS-pointer forms) take no mask, and neither do the
    // F3-prefixed /4 and /6, which are PTWRITE and CLRSSBSY.
    int xsave_mask = (diza->map == XDE_MAP_0F && diza->enc == XDE_ENC_LEGACY &&
                      mod != 3 &&
                      ((c2 == 0xAE &&
                        ((reg == 4 && !ptwrite) || reg == 5 ||
                         (reg == 6 && diza->p_66 == 0 && !clrssbsy))) ||
                       (c2 == 0xC7 && (reg == 3 || reg == 4 || reg == 5))));

    // 32-bit GP writes zero-extend in 64-bit mode.
    if (diza->mode == 64 && dsz == 4)
        dsz = 8;

    // A vector encoding always carries at least one vector / mask / control
    // register, so both sets fold to OTHER outright; no vvvv test is needed
    // for that. XA_VVVV_GPR marks the VEX/EVEX/XOP forms whose operands are
    // all GPRs (BMI, RORX, XOP vvvv forms): vvvv names a GPR there, 0
    // included, which is EAX.
    if (diza->enc == XDE_ENC_VEX2 || diza->enc == XDE_ENC_VEX3 ||
        diza->enc == XDE_ENC_EVEX || diza->enc == XDE_ENC_XOP) {
        if (attr & XA_VVVV_GPR) {
            uint64_t v2 = 0;
            diza->src_set |= gp_set(dsz, diza->vex_vvvv, 1, &v2);
            diza->src_set2 |= v2;
        } else {
            diza->src_set |= XSET_OTHER;
            diza->dst_set |= XSET_OTHER;
        }
    }

    // XA_VVVV_GPR marks a VEX/EVEX/XOP instruction whose reg field is a
    // destination register, which the 0F-map list below cannot see.
    if (c == 0x8B || c == 0x8A || c == 0x8D || (attr & XA_VVVV_GPR) || reg_dst ||
        (simd_0f && !reg_not_dst) ||
        (c == 0x0F && (c2 == 0xB2 || c2 == 0xB4 || c2 == 0xB5 ||
                       c2 == 0xB6 || c2 == 0xB7 || c2 == 0xBE || c2 == 0xBF ||
                       (c2 >= 0x40 && c2 <= 0x4F) || c2 == 0xAF || c2 == 0xBC || c2 == 0xBD ||
                       c2 == 0xB8))) {
        if (simd_0f) {
            diza->dst_set |= XSET_OTHER;
        } else if (diza->enc == XDE_ENC_LEGACY || diza->enc == XDE_ENC_REX2 ||
                   (attr & XA_VVVV_GPR)) {
            uint64_t g2 = 0;
            diza->dst_set |= gp_set(dsz, regx, rex, &g2);
            diza->dst_set2 |= g2;
        } else {
            diza->dst_set |= XSET_OTHER;
        }
    }
    if (c == 0x89 || c == 0x88 || reg_src) {
        uint64_t g2 = 0;
        diza->src_set |= gp_set(sz, regx, rex, &g2);
        diza->src_set2 |= g2;
    }
    if ((c <= 0x3D) && ((c & 7) <= 3) && diza->map == XDE_MAP_LEGACY) {
        unsigned form = c & 7;
        uint64_t rset2 = 0;
        uint64_t rset = gp_set((form & 1) ? dsz : 1, regx, rex, &rset2);
        if (form == 0 || form == 1) {
            diza->src_set |= rset;
            diza->src_set2 |= rset2;
            diza->dst_set |= XSET_FL;
        } else if (form == 2 || form == 3) {
            diza->src_set |= rset;
            diza->src_set2 |= rset2;
            diza->dst_set |= rset | XSET_FL;
            diza->dst_set2 |= rset2;
        }
    }

    if (mod == 3) {
        unsigned rmreg = ((unsigned)diza->rex_b4 << 4) |
                         ((unsigned)diza->rex_b << 3) | rm;
        uint64_t rset2 = 0;
        uint64_t rset = gp_set(sz, rmreg, rex, &rset2);
        if (diza->mode == 64 && sz == 4)
            rset = gp_set(8, rmreg, rex, &rset2);
        if ((simd_0f && !reg_not_dst) ||
            (diza->enc != XDE_ENC_LEGACY && diza->enc != XDE_ENC_REX2 &&
             !(attr & XA_VVVV_GPR))) {
            rset = XSET_OTHER;
            rset2 = 0;
        }
        if (mov_crdr) {
            // 0F 20/21 read CR/DR into r/m, 0F 22/23 write r/m into CR/DR.
            if (c2 == 0x20 || c2 == 0x21) {
                diza->src_set |= XSET_OTHER;
                diza->dst_set |= rset;
                diza->dst_set2 |= rset2;
            } else {
                diza->src_set |= rset;
                diza->src_set2 |= rset2;
                diza->dst_set |= XSET_OTHER;
            }
        } else if (rdrand) {
            // RDRAND/RDSEED write r/m and read nothing; CF reports whether
            // the value was valid.
            diza->dst_set |= rset | XSET_FL;
            diza->dst_set2 |= rset2;
        } else if (fsgsbase) {
            // RDFSBASE/RDGSBASE write r/m; WRFSBASE/WRGSBASE read it and write
            // the FS/GS base register, which folds into OTHER.
            if (reg <= 1) {
                diza->dst_set |= rset;
                diza->dst_set2 |= rset2;
            } else {
                diza->src_set |= rset;
                diza->src_set2 |= rset2;
                diza->dst_set |= XSET_OTHER;
            }
        } else if (fence || endbr || nop1e || nop_ea) {
            // LFENCE/MFENCE/SFENCE, ENDBR64/ENDBR32 and the NOP Ev group
            // (0F 1E, 0F 18 /4-/7, 0F 19, 0F 1D, 0F 1F) take no operand.
        } else if (incssp) {
            // INCSSPD/INCSSPQ add the r/m GPR to the shadow stack pointer:
            // the GPR is read and the SSP write folds into OTHER.
            diza->src_set |= rset;
            diza->src_set2 |= rset2;
            diza->dst_set |= XSET_OTHER;
        } else if (rdssp) {
            // RDSSPD/RDSSPQ read the shadow stack pointer into r/m.
            diza->src_set |= XSET_OTHER;
            diza->dst_set |= rset;
            diza->dst_set2 |= rset2;
        } else {
            // MOV store forms (88/89, C6/C7) and SETcc only write their r/m
            // operand, so it must not land in src_set; other forms read it.
            if (!setcc && c != 0x8D && c != 0x88 && c != 0x89 &&
                c != 0xC6 && c != 0xC7) {
                diza->src_set |= rset;
                diza->src_set2 |= rset2;
            }
            // dest of r/m for ALU, MOV r/m, shifts, SETcc, etc.
            if (setcc ||
                (diza->map == XDE_MAP_LEGACY &&
                 ((c <= 0x33 && (c & 7) <= 1) || c == 0x86 || c == 0x87 ||
                  c == 0x88 || c == 0x89 || (c >= 0xC0 && c <= 0xC1) ||
                  (c >= 0xD0 && c <= 0xD3) || c == 0xF6 || c == 0xF7 ||
                  c == 0xFE || c == 0xFF || (c >= 0x80 && c <= 0x83) ||
                  c == 0xC6 || c == 0xC7))) {
                diza->dst_set |= rset;
                diza->dst_set2 |= rset2;
            }
        }
        if (c == 0x0F && (c2 == 0xB6 || c2 == 0xB7 || c2 == 0xBE || c2 == 0xBF)) {
            int srcsz = (c2 == 0xB6 || c2 == 0xBE) ? 1 : 2;
            uint64_t m2 = 0;
            diza->src_set |= gp_set(srcsz, rmreg, rex, &m2);
            diza->src_set2 |= m2;
        }
    } else {
        if (!setcc && c != 0x8D && !nop_ea && !nop1e && !prefetch_ea) {
            diza->src_set |= XSET_OTHER; // segment override
            diza->src_set |= XSET_MEM;
        }
        if (prefetch_ea)
            diza->src_set |= XSET_MEM;   // prefetches do read memory
        if (mem_store || setcc || reg_src ||
            (diza->map == XDE_MAP_LEGACY &&
             ((c <= 0x33 && (c & 7) <= 1) || c == 0x86 || c == 0x87 ||
              c == 0x88 || c == 0x89 || (c >= 0xC0 && c <= 0xC1) ||
              (c >= 0xD0 && c <= 0xD3) || c == 0xF6 || c == 0xF7 ||
              c == 0xFE || c == 0xFF || (c >= 0x80 && c <= 0x83) ||
              c == 0xC6 || c == 0xC7)))
            diza->dst_set |= XSET_MEM;
        else if (diza->enc != XDE_ENC_LEGACY && diza->enc != XDE_ENC_REX2)
            diza->src_set |= XSET_MEM;
    }

    // The compared/loaded pair is read as well as written, and ZF reports the
    // outcome of the comparison.
    if (cmpxchg8b) {
        int psz = (diza->mode == 64 && diza->rex_w) ? 8 : 4;
        uint64_t pair = gp_set(psz, 0, rex, NULL) | gp_set(psz, 2, rex, NULL);
        diza->src_set |= pair;
        diza->dst_set |= pair | XSET_FL;
    }
    if (xsave_mask)
        diza->src_set |= XSET_EAX | XSET_EDX;
    // UMWAIT and TPAUSE read the wake-up deadline from EDX:EAX, the last
    // implicit operand of the group, and report the wake-up cause in CF,
    // clearing the other arithmetic flags. The r/m operand they read is the
    // generic r/m read above, shared with UMONITOR's address register.
    if (umwait || tpause) {
        diza->src_set |= XSET_EAX | XSET_EDX;
        diza->dst_set |= XSET_FL;
    }
    // CLRSSBSY reports an invalid shadow stack token in CF and clears the
    // other arithmetic flags.
    if (clrssbsy)
        diza->dst_set |= XSET_FL;
}

static void apply_implicit_gp(struct xde_instr *diza, uint32_t attr)
{
    uint8_t c = diza->opcode;
    int rex = (diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY);
    int sz = (attr & XA_OPSZ8) ? 1 : (int)diza->defdata;
    int dsz = (diza->mode == 64 && sz == 4) ? 8 : sz;
    uint64_t xset;

    if (diza->map != XDE_MAP_LEGACY && diza->map != XDE_MAP_0F)
        return;

    // opcode+r forms take bit 4 of the register number from B4 (APX); the XCHG
    // form names its second register from R4/R.
    unsigned rop = ((unsigned)diza->rex_b4 << 4) |
                   ((unsigned)diza->rex_b << 3) | (unsigned)(c & 7);
    unsigned rrr = ((unsigned)diza->rex_r4 << 4) |
                   ((unsigned)diza->rex_r << 3);

    if (diza->map == XDE_MAP_LEGACY) {
        if ((c & 0xF8) == 0x40 || (c & 0xF8) == 0x48) { // INC/DEC r
            uint64_t g2 = 0;
            xset = gp_set(dsz, rop, rex, &g2);
            diza->src_set |= xset;
            diza->src_set2 |= g2;
            diza->dst_set |= xset | XSET_FL;
            diza->dst_set2 |= g2;
        }
        if ((c & 0xF8) == 0x50) {
            uint64_t g2 = 0;
            diza->src_set |= gp_set(dsz, rop, rex, &g2);
            diza->src_set2 |= g2;
        }
        if ((c & 0xF8) == 0x58) {
            uint64_t g2 = 0;
            diza->dst_set |= gp_set(dsz, rop, rex, &g2);
            diza->dst_set2 |= g2;
        }
        if ((c & 0xF8) == 0x90 && c != 0x90) {
            uint64_t a2 = 0, b2 = 0, yset;
            xset = gp_set(dsz, rop, rex, &a2);
            yset = gp_set(dsz, rrr, rex, &b2);
            diza->src_set |= xset | yset;
            diza->dst_set |= xset | yset;
            diza->src_set2 |= a2 | b2;
            diza->dst_set2 |= a2 | b2;
        }
        if ((c & 0xF8) == 0xB0) {
            uint64_t g2 = 0;
            diza->dst_set |= gp_set(1, rop, rex, &g2);
            diza->dst_set2 |= g2;
        }
        if ((c & 0xF8) == 0xB8) {
            uint64_t g2 = 0;
            diza->dst_set |= gp_set(dsz, rop, rex, &g2);
            diza->dst_set2 |= g2;
        }
        if ((c & 7) == 4 && c < 0x3E) { // ALU AL, Ib
            diza->src_set |= XSET_AL;
            diza->dst_set |= XSET_AL | XSET_FL;
            if (c >= 0x38) diza->dst_set &= ~XSET_AL;
        }
        if ((c & 7) == 5 && c < 0x3E) {
            xset = gp_set(dsz, 0, rex, NULL);
            diza->src_set |= xset;
            diza->dst_set |= xset | XSET_FL;
            if (c >= 0x38) diza->dst_set &= ~xset;
        }
    }
    if (diza->map == XDE_MAP_0F && (diza->opcode2 & 0xF8) == 0xC8) {
        unsigned r = (diza->opcode2 & 7) | ((unsigned)diza->rex_b << 3) |
                     ((unsigned)diza->rex_b4 << 4);
        uint64_t g2 = 0;
        xset = gp_set(dsz, r, 1, &g2);
        diza->src_set |= xset;
        diza->dst_set |= xset;
        diza->src_set2 |= g2;
        diza->dst_set2 |= g2;
    }

    xset = stack_set(diza->mode);
    if (attr & XA_PUSH) {
        diza->src_set |= xset;
        diza->dst_set |= xset | XSET_MEM;
    }
    if (attr & XA_POP) {
        diza->src_set |= xset | XSET_MEM;
        diza->dst_set |= xset;
    }
}

// The x87 escapes (D8-DF) are two tables in one: with a memory operand the
// ModR/M.reg field selects the operation, and at mod=3 the whole byte selects
// an ST(i) form. The SDM's own escape tables (Vol 2D, Appendix A.5) leave
// slots blank in both, and the footnote repeated under them is that "all
// blanks in all opcode maps are reserved and must not be used":
//
//   memory: D9 /1, DB /4, DB /6, DD /5
//   mod=3:  D9 D1-D7, D9 D8-DF, D9 E2/E3/E6/E7/EF, DA E0-E8, DA EA-FF,
//           DB E6/E7, DB F8-FF, DC D0-DF, DD C8-CF, DD F0-FF, DE D0-D8,
//           DE DA-DF, DF C8-DF, DF E1-E7, DF F8-FF
//
// Bit (reg * 8 + rm) of x87_res3[opcode - 0xD8] is a mod=3 slot, bit reg of
// x87_resm[opcode - 0xD8] the memory form of that /reg field. The slots the
// SDM also leaves blank but that another decoder still names are deliberately
// left out: DB E0/E1/E4/E5 (the 8087/287 FENI/FDISI/FSETPM/FRSTPM) and
// DF C0-C7 (FFREEP), both of which binutils decodes.
static const uint64_t x87_res3[8] = {
    0x0000000000000000ULL, 0x000080CCFFFE0000ULL,
    0xFFFFFDFF00000000ULL, 0xFF0000C000000000ULL,
    0x00000000FFFF0000ULL, 0xFFFF00000000FF00ULL,
    0x00000000FDFF0000ULL, 0xFF0000FEFFFFFF00ULL
};
static const uint8_t x87_resm[8] = {
    0x00, 0x02, 0x00, 0x50, 0x00, 0x20, 0x00, 0x00
};

// 3DNow! spells its opcode in the immediate byte of 0F 0F /r ib, and AMD
// assigned it only 24 values: 0C and 0D (PI2FW, PI2FD), 1C and 1D (PF2IW,
// PF2ID) and the twenty from 8A to BF. Bit (sel & 63) of now_ok[sel >> 6] is
// one of them; the remaining 232 selectors name no instruction.
static const uint64_t now_ok[4] = {
    0x0000000030003000ULL, 0x0000000000000000ULL,
    0x88D144D144D14400ULL, 0x0000000000000000ULL
};

// An SSE instruction spells its mandatory prefix out as part of the opcode:
// the SDM defines 66 0F 6C as PUNPCKLQDQ and none of the other three
// prefixes as anything, and 0F B8 as POPCNT only under F3. The bit per prefix
// class is one of XDE_PFX_*, and xde_pfx_0f holds the set the SDM defines
// each 0F opcode under as a legacy instruction, with 0 for the opcodes it
// defines under none of them (0F 7A, 0F 7B). The escaped maps 0F 38 / 0F 3A
// are listed by the same scheme. The rows for 0x38 and 0x3A stay 0 because
// those bytes are the escapes themselves: a legacy 0F 38 selects
// XDE_MAP_0F38 and never reaches the table with opcode2 == 0x38.
enum {
    XDE_PFX_NONE = 0x01,    // no 66 / F2 / F3
    XDE_PFX_66   = 0x02,
    XDE_PFX_F2   = 0x04,
    XDE_PFX_F3   = 0x08
};
static const uint8_t xde_pfx_0f[256] = {
    0x0F, 0x0F, 0x0F, 0x0F, 0x00, 0x0F, 0x0F, 0x0F,   // 00-07
    0x0F, 0x09, 0x00, 0x0F, 0x00, 0x0F, 0x0F, 0x0F,   // 08-0F
    0x0F, 0x0F, 0x0F, 0x03, 0x03, 0x03, 0x0B, 0x03,   // 10-17
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // 18-1F
    0x0F, 0x0F, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x00,   // 20-27
    0x03, 0x03, 0x0F, 0x0F, 0x0F, 0x0F, 0x03, 0x03,   // 28-2F
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x00, 0x0F,   // 30-37
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 38-3F
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // 40-47
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // 48-4F
    0x03, 0x0F, 0x09, 0x09, 0x03, 0x03, 0x03, 0x03,   // 50-57
    0x0F, 0x0F, 0x0F, 0x0B, 0x0F, 0x0F, 0x0F, 0x0F,   // 58-5F
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,   // 60-67
    0x03, 0x03, 0x03, 0x03, 0x02, 0x02, 0x03, 0x0B,   // 68-6F
    0x0F, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x01,   // 70-77
    0x07, 0x07, 0x00, 0x00, 0x06, 0x06, 0x0B, 0x0B,   // 78-7F
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // 80-87
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // 88-8F
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // 90-97
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // 98-9F
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // A0-A7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // A8-AF
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // B0-B7
    0x08, 0x0F, 0x0F, 0x0F, 0x0B, 0x0B, 0x0F, 0x0F,   // B8-BF
    0x0F, 0x0F, 0x0F, 0x01, 0x03, 0x03, 0x03, 0x0F,   // C0-C7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,   // C8-CF
    0x06, 0x03, 0x03, 0x03, 0x03, 0x03, 0x0E, 0x0F,   // D0-D7
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,   // D8-DF
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x0E, 0x03,   // E0-E7
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,   // E8-EF
    0x04, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,   // F0-F7
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x0F,   // F8-FF
};

// The 0F 38 and 0F 3A maps carry the same bits, but here 0 is not a rarity:
// most of the two maps exists only as a VEX / EVEX form, and the SDM gives
// the legacy encoding of an opcode that does have one the same short list of
// prefixes as the 0F map does. 0F 38 0C (VPERMILPS) and 0F 3A 08 (ROUNDPS
// without its 66) therefore name nothing, while 0F 38 00 keeps both its MMX
// and its 66 form and 0F 3A 0F keeps PALIGNR's.
static const uint8_t xde_pfx_38[256] = {
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,   // 00-07
    0x03, 0x03, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00,   // 08-0F
    0x02, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x02,   // 10-17
    0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x00,   // 18-1F
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00,   // 20-27
    0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00,   // 28-2F
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00, 0x02,   // 30-37
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,   // 38-3F
    0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 40-47
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 48-4F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 50-57
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 58-5F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 60-67
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 68-6F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 70-77
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 78-7F
    0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,   // 80-87
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 88-8F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 90-97
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 98-9F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // A0-A7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // A8-AF
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // B0-B7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // B8-BF
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // C0-C7
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x02,   // C8-CF
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // D0-D7
    0x08, 0x00, 0x00, 0x02, 0x0A, 0x0A, 0x0A, 0x0A,   // D8-DF
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // E0-E7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // E8-EF
    0x07, 0x07, 0x00, 0x00, 0x00, 0x02, 0x0B, 0x00,   // F0-F7
    0x0E, 0x01, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00,   // F8-FF
};
static const uint8_t xde_pfx_3a[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 00-07
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03,   // 08-0F
    0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x02, 0x02,   // 10-17
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 18-1F
    0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,   // 20-27
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 28-2F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 30-37
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 38-3F
    0x02, 0x02, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00,   // 40-47
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 48-4F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 50-57
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 58-5F
    0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00,   // 60-67
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 68-6F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 70-77
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 78-7F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 80-87
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 88-8F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 90-97
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 98-9F
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // A0-A7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // A8-AF
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // B0-B7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // B8-BF
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // C0-C7
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x02,   // C8-CF
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // D0-D7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,   // D8-DF
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // E0-E7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // E8-EF
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // F0-F7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // F8-FF
};

static int parse_modrm(xde_cur *cur, struct xde_instr *diza, uint32_t attr)
{
    uint8_t m, sib;
    unsigned mod, rm, addr;
    unsigned disp = 0;
    unsigned i;

    if (!get_byte(cur, &m))
        return 0;
    diza->modrm = m;
    diza->flag |= C_MODRM;

    mod = m >> 6;
    rm  = m & 7;
    addr = diza->defaddr;

    if (mod == 3)
        return 1;

    if (addr == 2) {
        // 16-bit addressing: no SIB
        if (mod == 1)
            disp = 1;
        else if (mod == 2)
            disp = 2;
        else if (rm == 6)
            disp = 2;
    } else {
        if (rm == 4) {
            if (!get_byte(cur, &sib))
                return 0;
            diza->sib = sib;
            diza->flag |= C_SIB;
            {
                unsigned base = sib & 7;
                unsigned index = (sib >> 3) & 7;
                // With APX B4, the "no base" encoding (mod 0, base 5) names a
                // real register (r21), so it needs no disp32.
                if (mod == 0 && base == 5 && !diza->rex_b4)
                    disp = 4;
                if (diza->enc != XDE_ENC_LEGACY && diza->enc != XDE_ENC_REX2 &&
                    !(attr & XA_VVVV_GPR))
                    diza->src_set |= XSET_OTHER;
                else {
                    uint64_t b2 = 0, i2 = 0;
                    if (!(mod == 0 && base == 5 && !diza->rex_b4) ||
                        diza->rex_b || diza->rex_b4)
                        diza->src_set |= gp_set((int)addr,
                            ((unsigned)diza->rex_b4 << 4) |
                            ((unsigned)diza->rex_b << 3) | base, 1, &b2);
                    if (index != 4 || diza->rex_x || diza->rex_x4)
                        diza->src_set |= gp_set((int)addr,
                            ((unsigned)diza->rex_x4 << 4) |
                            ((unsigned)diza->rex_x << 3) | index, 1, &i2);
                    diza->src_set2 |= b2 | i2;
                }
            }
        } else if (mod == 0 && rm == 5 && !diza->rex_b4) {
            disp = 4;
            if (addr == 8) {
                diza->flag |= C_RIPREL;
                diza->src_set |= XSET_RIP;
            }
        } else {
            uint64_t b2 = 0;
            diza->src_set |= gp_set((int)addr,
                ((unsigned)diza->rex_b4 << 4) |
                ((unsigned)diza->rex_b << 3) | rm, 1, &b2);
            diza->src_set2 |= b2;
        }

        if (mod == 1)
            disp = 1;
        else if (mod == 2)
            disp = 4;
    }

    if (disp) {
        if (cur_left(cur) < (int)disp)
            return 0;
        for (i = 0; i < disp; i++)
            diza->addr_b[i] = *cur->p++;
        diza->addrsize = (uint8_t)disp;
        if (disp == 1) diza->flag |= C_ADDR1;
        else if (disp == 2) diza->flag |= C_ADDR2;
        else diza->flag |= C_ADDR4;   // disp is 1, 2 or 4 here
    }
    return 1;
}

static int parse_legacy_opcode(xde_cur *cur, struct xde_instr *diza,
                               unsigned *map, uint8_t *mop)
{
    uint8_t b;
    if (!get_byte(cur, &b))
        return 0;
    diza->opcode = b;
    if (b != 0x0F) {
        *map = XDE_MAP_LEGACY;
        *mop = b;
        return 1;
    }
    if (!get_byte(cur, &b))
        return 0;
    diza->opcode2 = b;
    if (b == 0x38) {
        if (!get_byte(cur, &b))
            return 0;
        diza->opcode3 = b;
        *map = XDE_MAP_0F38;
        *mop = b;
        return 1;
    }
    if (b == 0x3A) {
        if (!get_byte(cur, &b))
            return 0;
        diza->opcode3 = b;
        *map = XDE_MAP_0F3A;
        *mop = b;
        return 1;
    }
    *map = XDE_MAP_0F;
    *mop = b;
    return 1;
}

int __cdecl xde_disasm_buf(const uint8_t *opcode, unsigned max_len,
                           struct xde_instr *diza, unsigned mode)
{
    xde_cur cur;
    uint8_t b, b1, b2, b3;
    unsigned map;
    uint8_t mop;
    uint32_t attr, gattr;
    unsigned i, dbytes;
    int twice, rex_seen = 0;
    uint64_t ea_set = 0, ea_set2 = 0;

    if (!opcode || !diza)
        return 0;
    if (mode != 16 && mode != 32 && mode != 64)
        return 0;
    if (max_len == 0)
        max_len = XDE_MAXLEN;
    if (max_len > XDE_MAXLEN)
        max_len = XDE_MAXLEN;

    memset(diza, 0, sizeof(*diza));
    diza->mode = (uint8_t)mode;
    diza->defaddr = (uint8_t)(mode == 16 ? 2 : mode == 32 ? 4 : 8);
    diza->defdata = (uint8_t)(mode == 16 ? 2 : 4);

    cur.beg = opcode;
    cur.p = opcode;
    cur.end = opcode + max_len;

    if (max_len >= 2) {
        uint16_t w = (uint16_t)(opcode[0] | (opcode[1] << 8));
        if (w == 0x0000 || w == 0xFFFF)
            diza->flag |= C_BAD;
    }

    // legacy prefixes (groups 1-4)
    for (;;) {
        if (!peek_byte(&cur, 0, &b))
            return 0;
        twice = 0;
        if (b == 0x66) {
            twice = diza->p_66 != 0;
            diza->p_66 = 0x66;
            // Only the last prefix of an SDM group counts, so a repeat keeps
            // the operand-size override in force. Toggling it back would leave
            // p_66 set at the mode default, and then the bytes xde_asm_buf()
            // writes do not decode back to their own length.
            if (!twice)
                diza->defdata = (uint8_t)(diza->defdata == 2 ? 4 : 2);
            cur.p++;
            if (twice) diza->flag |= C_BAD;
            continue;
        }
        if (b == 0x67) {
            twice = diza->p_67 != 0;
            diza->p_67 = 0x67;
            if (!twice) {
                if (mode == 64)
                    diza->defaddr = (uint8_t)(diza->defaddr == 8 ? 4 : 8);
                else
                    diza->defaddr = (uint8_t)(diza->defaddr == 2 ? 4 : 2);
            }
            cur.p++;
            if (twice) diza->flag |= C_BAD;
            continue;
        }
        if (b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E ||
            b == 0x64 || b == 0x65) {
            twice = diza->p_seg != 0;
            diza->p_seg = b;
            cur.p++;
            if (twice) diza->flag |= C_BAD;
            continue;
        }
        if (b == 0xF2 || b == 0xF3) {
            twice = diza->p_rep != 0;
            diza->p_rep = b;
            cur.p++;
            if (twice) diza->flag |= C_BAD;
            continue;
        }
        if (b == 0xF0) {
            twice = diza->p_lock != 0;
            diza->p_lock = b;
            cur.p++;
            if (twice) diza->flag |= C_BAD;
            continue;
        }
        break;
    }

    // REX / REX2 / VEX / EVEX / XOP
    if (!peek_byte(&cur, 0, &b))
        return 0;

    if (mode == 64 && b >= 0x40 && b <= 0x4F) {
        diza->rex = b;
        diza->rex_w = (uint8_t)((b >> 3) & 1);
        diza->rex_r = (uint8_t)((b >> 2) & 1);
        diza->rex_x = (uint8_t)((b >> 1) & 1);
        diza->rex_b = (uint8_t)(b & 1);
        diza->flag |= C_REX;
        if (diza->rex_w)
            diza->defdata = 8;
        rex_seen = 1;
        cur.p++;
        if (!peek_byte(&cur, 0, &b))
            return 0;
    }

    if (mode == 64 && b == 0xD5) {
        // REX2: D5 [M R4 X4 B4 W R X B]
        if (!peek_byte(&cur, 1, &b1))
            return 0;
        diza->enc = XDE_ENC_REX2;
        diza->nvex = 2;
        diza->vex[0] = 0xD5;
        diza->vex[1] = b1;
        diza->flag |= C_REX2 | C_REX;
        diza->rex_w = (uint8_t)((b1 >> 3) & 1);
        diza->rex_r = (uint8_t)((b1 >> 2) & 1);
        diza->rex_x = (uint8_t)((b1 >> 1) & 1);
        diza->rex_b = (uint8_t)(b1 & 1);
        // R4/X4/B4: bit 4 of ModRM.reg / SIB.index / r/m (EGPR r16-r31).
        diza->rex_r4 = (uint8_t)((b1 >> 6) & 1);
        diza->rex_x4 = (uint8_t)((b1 >> 5) & 1);
        diza->rex_b4 = (uint8_t)((b1 >> 4) & 1);
        // REX2 counts as a REX prefix for register naming, so r/m 4-7 in
        // 8-bit form is SPL/BPL/SIL/DIL and never AH/CH/DH/BH.
        diza->rex = (uint8_t)(0x40 | (b1 & 0x0F));
        if (diza->rex_w)
            diza->defdata = 8;
        cur.p += 2;
        map = (b1 & 0x80) ? XDE_MAP_0F : XDE_MAP_LEGACY;
        if (!get_byte(&cur, &mop))
            return 0;
        diza->opcode = mop;
        if (map == XDE_MAP_0F)
            diza->opcode2 = mop;
        diza->map = (uint8_t)map;
        goto got_opcode;
    }

    if (b == 0x62) {
        // Need four bytes to even consider EVEX. otherwise this is BOUND.
        if (peek_byte(&cur, 1, &b1) && peek_byte(&cur, 2, &b2) && peek_byte(&cur, 3, &b3) &&
            (b2 & 0x04) && (mode == 64 || (b1 & 0xC0) == 0xC0)) {
            // EVEX: P1.bit2 must be 1. In 32-bit, P0.mod must be 11b (else BOUND).
            diza->enc = XDE_ENC_EVEX;
            diza->nvex = 4;
            diza->vex[0] = 0x62;
            diza->vex[1] = b1;
            diza->vex[2] = b2;
            diza->vex[3] = b3;
            diza->flag |= C_EVEX | C_VEX;
            diza->rex_r  = (uint8_t)((~b1 >> 7) & 1);
            diza->rex_x  = (uint8_t)((~b1 >> 6) & 1);
            diza->rex_b  = (uint8_t)((~b1 >> 5) & 1);
            diza->evex_r2 = (uint8_t)((~b1 >> 4) & 1);
            map = b1 & 7;
            diza->rex_w = (uint8_t)((b2 >> 7) & 1);
            diza->vex_vvvv = (uint8_t)((~b2 >> 3) & 0xF);
            diza->vex_pp = (uint8_t)(b2 & 3);
            diza->evex_z = (uint8_t)((b3 >> 7) & 1);
            diza->vex_l  = (uint8_t)((b3 >> 5) & 3);
            diza->evex_b = (uint8_t)((b3 >> 4) & 1);
            diza->evex_aaa = (uint8_t)(b3 & 7);
            if (!((b3 >> 3) & 1)) // V' == 0 extends vvvv
                diza->vex_vvvv |= 0x10;
            diza->defdata = (uint8_t)(mode == 16 ? 2 : 4);
            if (diza->rex_w)
                diza->defdata = 8;
            // pp implied 66/F3/F2 is not a legacy prefix
            diza->p_66 = 0;
            diza->p_rep = 0;
            cur.p += 4;
            if (!get_byte(&cur, &mop))
                return 0;
            diza->opcode = mop;
            diza->map = (uint8_t)map;
            if (map == XDE_MAP_0F)
                diza->opcode2 = mop;
            else if (map == XDE_MAP_0F38) {
                diza->opcode2 = 0x38;
                diza->opcode3 = mop;
            } else if (map == XDE_MAP_0F3A) {
                diza->opcode2 = 0x3A;
                diza->opcode3 = mop;
            }
            goto got_opcode;
        }
        // fall through: BOUND in 16/32-bit, or a truncated 0x62
    }

    if (b == 0xC4 || b == 0xC5) {
        if (!peek_byte(&cur, 1, &b1))
            return 0;
        if (mode == 64 || (b1 & 0xC0) == 0xC0) {
            if (b == 0xC5) {
                diza->enc = XDE_ENC_VEX2;
                diza->nvex = 2;
                diza->vex[0] = 0xC5;
                diza->vex[1] = b1;
                diza->flag |= C_VEX;
                diza->rex_r = (uint8_t)((~b1 >> 7) & 1);
                diza->vex_vvvv = (uint8_t)((~b1 >> 3) & 0xF);
                diza->vex_l = (uint8_t)((b1 >> 2) & 1);
                diza->vex_pp = (uint8_t)(b1 & 3);
                map = XDE_MAP_0F;
                diza->defdata = (uint8_t)(mode == 16 ? 2 : 4);
                diza->p_66 = 0;
                diza->p_rep = 0;
                cur.p += 2;
            } else {
                if (!peek_byte(&cur, 2, &b2))
                    return 0;
                diza->enc = XDE_ENC_VEX3;
                diza->nvex = 3;
                diza->vex[0] = 0xC4;
                diza->vex[1] = b1;
                diza->vex[2] = b2;
                diza->flag |= C_VEX;
                diza->rex_r = (uint8_t)((~b1 >> 7) & 1);
                diza->rex_x = (uint8_t)((~b1 >> 6) & 1);
                diza->rex_b = (uint8_t)((~b1 >> 5) & 1);
                map = b1 & 0x1F;
                diza->rex_w = (uint8_t)((b2 >> 7) & 1);
                diza->vex_vvvv = (uint8_t)((~b2 >> 3) & 0xF);
                diza->vex_l = (uint8_t)((b2 >> 2) & 1);
                diza->vex_pp = (uint8_t)(b2 & 3);
                diza->defdata = (uint8_t)(mode == 16 ? 2 : 4);
                if (diza->rex_w)
                    diza->defdata = 8;
                diza->p_66 = 0;
                diza->p_rep = 0;
                cur.p += 3;
            }
            if (!get_byte(&cur, &mop))
                return 0;
            diza->opcode = mop;
            diza->map = (uint8_t)map;
            if (map == XDE_MAP_0F)
                diza->opcode2 = mop;
            else if (map == XDE_MAP_0F38) {
                diza->opcode2 = 0x38;
                diza->opcode3 = mop;
            } else if (map == XDE_MAP_0F3A) {
                diza->opcode2 = 0x3A;
                diza->opcode3 = mop;
            }
            goto got_opcode;
        }
    }

    if (b == 0x8F) {
        if (!peek_byte(&cur, 1, &b1))
            return 0;
        // In 16/32-bit mode XOP needs mod == 11b, else this is POP r/m.
        if ((b1 & 0x1F) >= 8 && (mode == 64 || (b1 & 0xC0) == 0xC0)) {
            if (!peek_byte(&cur, 2, &b2))
                return 0;
            diza->enc = XDE_ENC_XOP;
            diza->nvex = 3;
            diza->vex[0] = 0x8F;
            diza->vex[1] = b1;
            diza->vex[2] = b2;
            diza->flag |= C_XOP;
            diza->rex_r = (uint8_t)((~b1 >> 7) & 1);
            diza->rex_x = (uint8_t)((~b1 >> 6) & 1);
            diza->rex_b = (uint8_t)((~b1 >> 5) & 1);
            map = b1 & 0x1F;
            diza->rex_w = (uint8_t)((b2 >> 7) & 1);
            diza->vex_vvvv = (uint8_t)((~b2 >> 3) & 0xF);
            diza->vex_l = (uint8_t)((b2 >> 2) & 1);
            diza->vex_pp = (uint8_t)(b2 & 3);
            diza->defdata = (uint8_t)(mode == 16 ? 2 : 4);
            if (diza->rex_w)
                diza->defdata = 8;
            diza->p_66 = 0;
            diza->p_rep = 0;
            cur.p += 3;
            if (!get_byte(&cur, &mop))
                return 0;
            diza->opcode = mop;
            diza->map = (uint8_t)map;
            goto got_opcode;
        }
    }

    if (!parse_legacy_opcode(&cur, diza, &map, &mop))
        return 0;
    diza->map = (uint8_t)map;
    diza->enc = XDE_ENC_LEGACY;

got_opcode:
    // A REX prefix must be the last prefix before the opcode, so a REX
    // immediately followed by one of the encodings that carry their own
    // prefix bytes (VEX/EVEX/XOP/REX2) is an illegal form. It is accepted and
    // marked unusable rather than rejected: xde_asm_buf() never emits REX
    // alongside those, so such an input cannot survive a re-encode.
    if (rex_seen && diza->enc != XDE_ENC_LEGACY)
        diza->flag |= C_BAD;
    if (map >= XDE_MAP_COUNT)
        return 0;
    attr = xde_attr[map][mop];

    if (attr & XA_INVALID)
        return 0;
    if ((attr & XA_I64) && mode == 64)
        return 0;
    if ((attr & XA_O64) && mode != 64)
        return 0;

    apply_attr_flags(diza, attr);

    // Group extra (immediate / CALL / JMP) after ModR/M.reg
    if (attr & XA_GROUP) {
        if (!(attr & XA_MODRM)) {
            attr |= XA_MODRM;
            diza->flag |= C_MODRM;   // apply_attr_flags has already run
        }
    }

    // The reserved forms below exist only in the legacy encoding: a VEX /
    // EVEX / XOP opcode selects map 0F or the legacy map with an opcode byte
    // of its own, and defines none of these groups. REX2 carries the same map
    // and opcode bytes as the legacy encoding it prefixes.
    int legacy_enc = (diza->enc == XDE_ENC_LEGACY || diza->enc == XDE_ENC_REX2);

    // The mandatory prefix of the SSE family is part of the opcode, so an
    // escaped opcode reached under the wrong one names no instruction:
    // 0F 6C is PUNPCKLQDQ only as 66 0F 6C, 0F B8 is POPCNT only as F3 0F B8,
    // and 0F 09 is WBINVD under neither 66 nor F2 (F3 0F 09 is the separate
    // WBNOINVD). Opcodes the SDM defines under no prefix at all carry 0 in
    // the table. The 0F 38 and 0F 3A maps are shared with the VEX / EVEX
    // encodings, whose opcode space is far larger, so there an opcode is
    // absent from the table unless it has a legacy form too: 0F 38 0C and
    // 0F 3A 08 name nothing without a VEX header, while 66 0F 38 00 (PSHUFB)
    // and 0F 3A 0F (PALIGNR) keep the forms the SDM gives them. The VEX /
    // EVEX encodings carry their own prefix byte in the header and never
    // reach this test, and it needs no ModR/M byte either, which is why it
    // runs before the group below.
    if (legacy_enc && diza->map >= XDE_MAP_0F && diza->map <= XDE_MAP_0F3A) {
        unsigned pfx = diza->p_66 ? XDE_PFX_66
                     : (diza->p_rep == 0xF2 ? XDE_PFX_F2
                     : (diza->p_rep == 0xF3 ? XDE_PFX_F3 : XDE_PFX_NONE));
        unsigned esc = (diza->map == XDE_MAP_0F)   ? xde_pfx_0f[diza->opcode2]
                     : (diza->map == XDE_MAP_0F38) ? xde_pfx_38[diza->opcode3]
                                                   : xde_pfx_3a[diza->opcode3];
        if (!(esc & pfx))
            diza->flag |= C_BAD;
    }

    if (attr & XA_MODRM) {
        uint8_t mpeek;
        unsigned reg;
        if (!peek_byte(&cur, 0, &mpeek))
            return 0;
        reg = (mpeek >> 3) & 7;
        if (attr & XA_GROUP) {
            unsigned gid = XA_GRP_ID(attr);
            if (gid < XG_COUNT) {
                gattr = xde_group[gid][reg];
                attr |= gattr;
                apply_attr_flags(diza, gattr);
            }
        }
        if ((diza->opcode == 0xC0 || diza->opcode == 0xC1 ||
             (diza->opcode >= 0xD0 && diza->opcode <= 0xD3)) &&
            diza->map == XDE_MAP_LEGACY) {
            if (reg == 2 || reg == 3)
                diza->src_set |= XSET_FL;   // RCL/RCR read CF
            diza->dst_set |= XSET_FL;       // all eight write CF/OF
            if (diza->opcode == 0xD2 || diza->opcode == 0xD3)
                diza->src_set |= XSET_CL;
        }
        // C6 /0 and C7 /0 are the only valid forms of these groups. The one
        // /7 exception is the exact ModR/M byte F8 (mod=3, reg=7, rm=0):
        // C7 F8 is XBEGIN and C6 F8 is XABORT, which carry a relative or
        // immediate operand instead of an r/m. Every other /7 stays invalid;
        // for 0x8F that exempted byte is XOP's map selector and never reaches
        // this point as a legacy opcode.
        if ((diza->opcode == 0xC6 || diza->opcode == 0xC7 || diza->opcode == 0x8F) &&
            diza->map == XDE_MAP_LEGACY && reg != 0 && mpeek != 0xF8)
            diza->flag |= C_BAD;
        // 0F AE only defines LFENCE /5, MFENCE /6 and SFENCE /7 at mod=3,
        // plus the F3-prefixed FS/GS base moves /0-/3. The rest of the group
        // keeps the memory form's encoding, where reg is an opcode selector
        // and mod=3 is therefore not an encoding of anything.
        if (diza->opcode == 0x0F && diza->map == XDE_MAP_0F &&
            diza->opcode2 == 0xAE && (mpeek >> 6) == 3 && reg <= 4 &&
            diza->p_rep != 0xF3)
            diza->flag |= C_BAD;
        // The fence slot is byte-exact for MFENCE /6 and SFENCE /7 (0F AE F0
        // and 0F AE F8), so a nonzero rm is not one of them; LFENCE keeps the
        // rm binutils tolerates. The 66 and F2 /5 name nothing -- their /6 is
        // TPAUSE and UMWAIT, whose r/m is a general register and stays -- and
        // the F3 /5 and /6 (INCSSPD and UMONITOR) keep theirs as well.
        if (diza->map == XDE_MAP_0F && legacy_enc && diza->opcode2 == 0xAE &&
            (mpeek >> 6) == 3 && (mpeek & 7) != 0 &&
            ((reg == 6 && !diza->p_66 && diza->p_rep == 0) || reg == 7))
            diza->flag |= C_BAD;
        if (diza->map == XDE_MAP_0F && legacy_enc && diza->opcode2 == 0xAE &&
            (mpeek >> 6) == 3 && reg == 5 &&
            (diza->p_66 || diza->p_rep == 0xF2))
            diza->flag |= C_BAD;
        // The 0F AE memory table has no 66 reading of /4 and /5 -- XSAVE64
        // and XRSTOR64 are REX.W forms -- no F2 reading of /4-/7 at all, and
        // no F3 reading of /5 or /7, whose F3 /4 and /6 are PTWRITE and
        // CLRSSBSY.
        if (diza->map == XDE_MAP_0F && legacy_enc && diza->opcode2 == 0xAE &&
            (mpeek >> 6) != 3 &&
            ((diza->p_66 && (reg == 4 || reg == 5)) ||
             (diza->p_rep == 0xF2 && reg >= 4) ||
             (diza->p_rep == 0xF3 && (reg == 5 || reg == 7))))
            diza->flag |= C_BAD;
        // PREFETCHh (0F 18 /0-/3) and PREFETCH/PREFETCHW/PREFETCHWT1
        // (0F 0D /0 /1 and /2-/7, which the SDM leaves blank) are defined only
        // with a memory operand, so their mod=3 encodings are reserved. The
        // 0F 18 /4-/7 slots stay as they were: binutils rejects their mod=3
        // form while the LLVM tables behind capstone call it a NOP, and the
        // SDM's own table leaves the slot blank, so the flag is left clear.
        if (diza->opcode == 0x0F && diza->map == XDE_MAP_0F &&
            (mpeek >> 6) == 3 &&
            ((diza->opcode2 == 0x18 && reg <= 3) ||
             diza->opcode2 == 0x0D))
            diza->flag |= C_BAD;
        // The 0F 00 group defines SLDT/STR (/0 /1), LLDT/LTR (/2 /3) and
        // VERR/VERW (/4 /5) in the register form and the memory form alike, so
        // /6 and /7 are not an encoding of anything in either of them. A REX2
        // prefix keeps the 0F map and writes the opcode to opcode2 itself,
        // which is why these rules read opcode2 rather than opcode.
        if (diza->map == XDE_MAP_0F && diza->opcode2 == 0x00 &&
            reg >= 6 && legacy_enc)
            diza->flag |= C_BAD;
        // 0F 01 keeps its memory table and its register table apart. The
        // memory forms are SGDT/SIDT (/0 /1), LGDT/LIDT (/2 /3), SMSW (/4),
        // LMSW (/6) and INVLPG (/7): /5 is reserved there. The register form
        // (mod=3) holds a second table, in which the SDM leaves 0F 01 C6/C7,
        // CC-CE, D2/D3 and E9-ED reserved; every other mod=3 encoding names an
        // instruction (ENCLV/VMCALL/VMLAUNCH/VMRESUME/VMXOFF/PCONFIG,
        // MONITOR/MWAIT/CLAC/STAC/ENCLS, XGETBV/XSETBV/VMFUNC/XEND/XTEST/
        // ENCLU, VMRUN and the rest of the SVM group, SERIALIZE, RDPKRU/
        // WRPKRU, SWAPGS/RDTSCP, and the MONITORX/MWAITX/CLZERO/RDPRU/
        // INVLPGB/TLBSYNC additions). SMSW /4 and LMSW /6, whose r/m16 the
        // SDM defines with a register operand too, are not part of that table.
        if (diza->map == XDE_MAP_0F && diza->opcode2 == 0x01 && legacy_enc) {
            unsigned rm = mpeek & 7;
            if ((mpeek >> 6) != 3) {
                if (reg == 5)
                    diza->flag |= C_BAD;            // memory form of /5
            } else if ((reg == 0 && rm >= 6) ||
                       (reg == 1 && rm >= 4 && rm <= 6) ||
                       (reg == 2 && (rm == 2 || rm == 3)) ||
                       (reg == 5 && rm >= 1 && rm <= 5)) {
                diza->flag |= C_BAD;
            }
        }
        // Far CALL (/3) and far JMP (/5) take a memory operand only, so their
        // mod=3 encodings are not instructions either. /2 and /4 are the near
        // CALL/JMP r/m forms and stay legal with a register operand, and /7
        // stays reserved in both forms.
        if (diza->opcode == 0xFF && diza->map == XDE_MAP_LEGACY &&
            (mpeek >> 6) == 3 && (reg == 3 || reg == 5) && legacy_enc)
            diza->flag |= C_BAD;
        // BOUND (62 /r) reads a pair of bounds from memory, so its mod=3
        // encodings are reserved. Only the legacy forms reach this point: 0x62
        // is taken as an EVEX prefix first, and BOUND is invalid in 64-bit
        // mode, where the REX2 form could arise.
        if (diza->opcode == 0x62 && diza->map == XDE_MAP_LEGACY &&
            diza->enc == XDE_ENC_LEGACY && (mpeek >> 6) == 3)
            diza->flag |= C_BAD;
        // LEA (8D /r) computes the address of its second operand, which the
        // SDM spells as a memory operand, so a mod=3 ModR/M byte is not an
        // encoding of it in any mode. The opcode sits in the legacy map and a
        // REX2 prefix keeps that map, so this one test covers both.
        if (diza->opcode == 0x8D && diza->map == XDE_MAP_LEGACY &&
            (mpeek >> 6) == 3)
            diza->flag |= C_BAD;
        // MOVLPS (0F 13) and MOVHPS (0F 17) store to memory only -- the
        // 66-prefixed MOVLPD/MOVHPD doubles and the REX2 encoding keep the
        // same m64 r/m -- so their mod=3 encodings name no instruction.
        // MOVNTPS (0F 2B) writes a whole m128 and is memory-only as well.
        // The 0F 12 / 0F 16 loads are the forms that do have a mod=3
        // encoding (MOVHLPS/MOVLHPS), so they are left alone.
        if (diza->map == XDE_MAP_0F && legacy_enc && (mpeek >> 6) == 3 &&
            (diza->opcode2 == 0x13 || diza->opcode2 == 0x17 ||
             diza->opcode2 == 0x2B))
            diza->flag |= C_BAD;
        // MOVMSKPS (0F 50) and its 66-prefixed MOVMSKPD gather the packed
        // sign bits out of an xmm register, so the r/m is a register and
        // every mod other than 3 is an encoding of nothing.
        if (diza->map == XDE_MAP_0F && legacy_enc &&
            diza->opcode2 == 0x50 && (mpeek >> 6) != 3)
            diza->flag |= C_BAD;
        // The rest of the opcodes whose r/m the SDM fixes take it in memory
        // or in a register in every form the encoding defines, so the mod
        // value the other class names is reserved there as well.
        //   66 0F 12 / 0F 16  MOVLPD/MOVHPD  m64   -- the plain and the
        //     F3/F2 readings of the same opcodes (MOVHLPS/MOVLHPS, MOVSLDUP/
        //     MOVSHDUP, MOVDDUP) have a register r/m and keep it.
        //   0F B2 / 0F B4 / 0F B5  LSS/LFS/LGS  m16:16/32/64, the far
        //     pointer loads.
        //   0F C3  MOVNTI m32/m64, 0F E7 MOVNTQ m64 and its 66 MOVNTDQ m128.
        //   F2 0F F0  LDDQU m128.
        // LSS, LFS and LGS carry no mandatory prefix, so F2 and F3 leave
        // them in the same memory-only form; the F3 reading of 0F C3 and of
        // 0F E7 is not MOVNTI/MOVNTQ at all, which is why those two keep
        // their gate.
        if (diza->map == XDE_MAP_0F && legacy_enc && (mpeek >> 6) == 3 &&
            (((diza->opcode2 == 0x12 || diza->opcode2 == 0x16) &&
              diza->p_66 && !diza->p_rep) ||
             diza->opcode2 == 0xB2 || diza->opcode2 == 0xB4 ||
             diza->opcode2 == 0xB5 ||
             ((diza->opcode2 == 0xC3 || diza->opcode2 == 0xE7) &&
              !diza->p_rep) ||
             (diza->opcode2 == 0xF0 && diza->p_rep == 0xF2)))
            diza->flag |= C_BAD;
        // PMOVMSKB (0F D7, 66 for the xmm form; F2 and F3 leave it the same
        // register form) and MASKMOVQ/MASKMOVDQU (0F F7, 66 for the xmm
        // form) name their r/m register, as do MOVDQ2Q (F2 0F D6) and MOVQ2DQ
        // (F3 0F D6); the plain and 66 readings of 0F D6 are the MOVQ r/m
        // forms and must keep their mod=3 encoding.
        if (diza->map == XDE_MAP_0F && legacy_enc && (mpeek >> 6) != 3 &&
            ((diza->opcode2 == 0xD7 || diza->opcode2 == 0xF7 ||
              (diza->opcode2 == 0xD6 &&
               (diza->p_rep == 0xF2 || diza->p_rep == 0xF3)))))
            diza->flag |= C_BAD;
        // The 0F 38 map: MOVNTDQA (66 0F 38 2A), the INVEPT/INVVPID/INVPCID
        // descriptor operands (66 0F 38 80-82), MOVBE (0F 38 F0/F1, whose F2
        // reading is CRC32), WRUSSD/WRUSSQ (66 0F 38 F5), WRSSD/WRSSQ
        // (0F 38 F6), MOVDIR64B (66 0F 38 F8), ENQCMD/ENQCMDS (F2 0F 38 F8)
        // and MOVDIRI (0F 38 F9) all take their r/m in memory.
        if (diza->map == XDE_MAP_0F38 && legacy_enc && (mpeek >> 6) == 3 &&
            ((diza->opcode3 == 0x2A && diza->p_66) ||
             (diza->opcode3 == 0x80 && diza->p_66) ||
             (diza->opcode3 == 0x81 && diza->p_66) ||
             (diza->opcode3 == 0x82 && diza->p_66) ||
             ((diza->opcode3 == 0xF0 || diza->opcode3 == 0xF1) &&
              !diza->p_rep) ||
             (diza->opcode3 == 0xF5 && diza->p_66) ||
             (diza->opcode3 == 0xF6 && !diza->p_66 && !diza->p_rep) ||
             (diza->opcode3 == 0xF8 && (diza->p_66 || diza->p_rep == 0xF2)) ||
             (diza->opcode3 == 0xF9 && !diza->p_66 && !diza->p_rep)))
            diza->flag |= C_BAD;
        // The group shifts (0F 71 / 0F 72 / 0F 73) take the register the r/m
        // field names together with an immediate count, and /reg selects the
        // operation: 2, 4 and 6 for PSRLW/PSRAW/PSLLW and PSRLD/PSRAD/PSLLD,
        // 2 and 6 for PSRLQ and PSLLQ, plus 3 and 7 for the 66-only PSRLDQ
        // and PSLLDQ. Every memory operand, and every other /reg, is empty.
        if (diza->map == XDE_MAP_0F && legacy_enc &&
            (diza->opcode2 == 0x71 || diza->opcode2 == 0x72)) {
            if ((mpeek >> 6) != 3 || reg == 0 || (reg & 1))
                diza->flag |= C_BAD;
        }
        if (diza->map == XDE_MAP_0F && legacy_enc && diza->opcode2 == 0x73) {
            if ((mpeek >> 6) != 3 ||
                !(reg == 2 || reg == 6 ||
                  (diza->p_66 && (reg == 3 || reg == 7))))
                diza->flag |= C_BAD;
        }
        // The VIA PadLock group is register-only and byte-exact: MONTMUL,
        // XSHA1 and XSHA256 are 0F A6 /0-/2 and XSTORE-RNG with the XCRYPT
        // modes are 0F A7 /0-/5, each with rm=0 and no memory form.
        if (diza->map == XDE_MAP_0F && legacy_enc &&
            (diza->opcode2 == 0xA6 || diza->opcode2 == 0xA7)) {
            unsigned last = (diza->opcode2 == 0xA6) ? 2 : 5;
            if ((mpeek >> 6) != 3 || reg > last || (mpeek & 7) != 0)
                diza->flag |= C_BAD;
        }
        // PEXTRW (0F C5, 66 for the xmm form) and the AMD EXTRQ / INSERTQ
        // (66 and F2 0F 78 / 0F 79, whose second operand is an immediate)
        // name their r/m in a register.
        if (diza->map == XDE_MAP_0F && legacy_enc && (mpeek >> 6) != 3 &&
            (diza->opcode2 == 0xC5 ||
             ((diza->opcode2 == 0x78 || diza->opcode2 == 0x79) &&
              (diza->p_66 || diza->p_rep == 0xF2))))
            diza->flag |= C_BAD;
        // BNDLDX and BNDSTX (0F 1A / 0F 1B) name one of the four bound
        // registers in /reg and take a mib operand, which the SDM's operand
        // type excludes the no-base form of. The 66, F2 and F3 selections
        // (BNDMOV, BNDCU/BNDCN, BNDCL and BNDMK) name the same four
        // registers, BNDMOV taking a plain m128 that does have the no-base
        // form; the bare reading and F3 0F 1B are NOPs that name no operand
        // at all, though BNDMK keeps the mib operand its memory form has.
        if (diza->map == XDE_MAP_0F && legacy_enc &&
            (diza->opcode2 == 0x1A || diza->opcode2 == 0x1B)) {
            unsigned rm = mpeek & 7;
            int bare_or_bndmk = (diza->p_66 == 0 && diza->p_rep == 0) ||
                                (diza->p_rep == 0xF3 && diza->opcode2 == 0x1B);
            if ((mpeek >> 6) == 3) {
                if (!bare_or_bndmk &&
                    (reg > 3 || (diza->p_66 && rm > 3)))
                    diza->flag |= C_BAD;
            } else if (reg > 3 ||
                       (bare_or_bndmk && (mpeek >> 6) == 0 && rm == 5)) {
                diza->flag |= C_BAD;
            }
        }
        // Group 9 (0F C7) selects by /reg: /1 CMPXCHG8B, /3 XRSTORS, /4
        // XSAVEC, /5 XSAVES, /6 VMPTRLD and /7 VMPTRST in memory, with 66 and
        // F3 giving VMCLEAR and VMXON at /6, and F2 defining no /6 at all.
        // At mod=3 /6 and /7 are RDRAND and RDSEED, F3 /6 being SENDUIPI, and
        // the register form has no other encoding, nor any F2 one.
        if (diza->map == XDE_MAP_0F && legacy_enc && diza->opcode2 == 0xC7) {
            if ((mpeek >> 6) == 3) {
                if (reg < 6 || diza->p_rep == 0xF2)
                    diza->flag |= C_BAD;
            } else if (reg == 0 || reg == 2 ||
                       (diza->p_rep == 0xF2 && reg == 6)) {
                diza->flag |= C_BAD;
            }
        }
        // The Key Locker forms the F3 prefix selects are memory forms:
        // AESENCWIDE128KL and its three siblings at 0F 38 D8 /0-/3, the
        // AESENC/AESDEC 128KL and 256KL at DD-DF, whose /reg names the xmm
        // operand, and ENQCMDS at 0F 38 F8. ENCODEKEY128 and ENCODEKEY256 at
        // 0F 38 FA/FB move between two general registers instead.
        if (diza->map == XDE_MAP_0F38 && legacy_enc && diza->p_rep == 0xF3) {
            if ((diza->opcode3 == 0xD8 && ((mpeek >> 6) == 3 || reg > 3)) ||
                ((diza->opcode3 == 0xDD || diza->opcode3 == 0xDE ||
                  diza->opcode3 == 0xDF || diza->opcode3 == 0xF8) &&
                 (mpeek >> 6) == 3) ||
                ((diza->opcode3 == 0xFA || diza->opcode3 == 0xFB) &&
                 (mpeek >> 6) != 3))
                diza->flag |= C_BAD;
        }
        // HRESET is the byte-exact 0F 3A F0 /0 form with the F3 prefix and an
        // immediate; the rest of that group is empty.
        if (diza->map == XDE_MAP_0F3A && legacy_enc && diza->p_rep == 0xF3 &&
            diza->opcode3 == 0xF0 &&
            ((mpeek >> 6) != 3 || reg != 0 || (mpeek & 7) != 0))
            diza->flag |= C_BAD;
        // x87: the blank slots of the escape tables D8-DF, which the SDM
        // reserves and neither mod=3 (the ST(i) table) nor the memory table
        // assigns to an instruction.
        if (diza->map == XDE_MAP_LEGACY && diza->opcode >= 0xD8 &&
            diza->opcode <= 0xDF && legacy_enc) {
            unsigned rm = mpeek & 7;
            if ((mpeek >> 6) == 3) {
                if (x87_res3[diza->opcode - 0xD8] & (1ULL << (reg * 8 + rm)))
                    diza->flag |= C_BAD;
            } else if (x87_resm[diza->opcode - 0xD8] & (1u << reg)) {
                diza->flag |= C_BAD;
            }
        }
        if (diza->opcode == 0xF6 && diza->map == XDE_MAP_LEGACY) {
            if (reg != 2)
                diza->dst_set |= XSET_FL;   // NOT (/2) writes no flags
            if (reg == 4 || reg == 5) {
                diza->src_set |= XSET_AL;
                diza->dst_set |= XSET_AX;
            }
            if (reg == 6 || reg == 7) {
                diza->src_set |= XSET_AX;
                diza->dst_set |= XSET_AX;
            }
        }
        if (diza->opcode == 0xF7 && diza->map == XDE_MAP_LEGACY) {
            int sz = (int)diza->defdata;
            uint64_t acc = gp_set(sz, 0, diza->rex != 0, NULL);
            uint64_t dx  = gp_set(sz, 2, diza->rex != 0, NULL);
            if (reg != 2)
                diza->dst_set |= XSET_FL;   // NOT (/2) writes no flags
            if (reg == 4 || reg == 5) {
                diza->src_set |= acc;
                diza->dst_set |= acc | dx | XSET_FL;
            }
            if (reg == 6 || reg == 7) {
                diza->src_set |= acc | dx;
                diza->dst_set |= acc | dx | XSET_FL;
            }
        }

        if (!parse_modrm(&cur, diza, attr))
            return 0;
        // undef_sys_operands() rebuilds the sets of the 0F 00 / 0F 01 /
        // 0F 02 / 0F 03 forms below, after the whole-set unknown has replaced
        // them; the addressing registers parse_modrm just recorded are the one
        // part of the sets those forms still need.
        if (attr & XA_UNDEF) {
            ea_set = diza->src_set;
            ea_set2 = diza->src_set2;
        }
        apply_modrm_usage(diza, attr, diza->modrm >> 6,
                          (diza->modrm >> 3) & 7, diza->modrm & 7);
    } else if (attr & XA_MOFFS) {
        unsigned asz = diza->defaddr;
        if (cur_left(&cur) < (int)asz)
            return 0;
        for (i = 0; i < asz; i++)
            diza->addr_b[i] = *cur.p++;
        diza->addrsize = (uint8_t)asz;
        diza->flag |= C_ADDR67;
        if (asz == 8) diza->flag |= C_ADDR8;
        else if (asz == 4) diza->flag |= C_ADDR4;
        else if (asz == 2) diza->flag |= C_ADDR2;
        diza->src_set |= XSET_MEM;
        diza->dst_set |= (diza->opcode == 0xA0 || diza->opcode == 0xA1) ? 0 : XSET_MEM;
    }

    apply_usage_special(diza, attr, diza->opcode, diza->opcode2);
    apply_implicit_gp(diza, attr);

    if (attr & XA_UNDEF) {
        diza->src_set = XSET_UNDEF;
        diza->dst_set = XSET_UNDEF;
        diza->src_set2 = XSET2_ALL;
        diza->dst_set2 = XSET2_ALL;
        // Most forms of 0F 00 / 0F 01 / 0F 02 / 0F 03 have SDM-defined
        // operands, so they replace the whole-set unknown above with what the
        // ModR/M byte names and stop being undef. Whatever the table leaves
        // unmodelled keeps the sets above and C_UNDEF.
        if (undef_sys_operands(diza, ea_set, ea_set2))
            diza->flag &= ~C_UNDEF;
    }

    dbytes = imm_bytes(attr, diza);
    if (dbytes) {
        if (cur_left(&cur) < (int)dbytes)
            return 0;
        for (i = 0; i < dbytes; i++)
            diza->data_b[i] = *cur.p++;
        diza->datasize = (uint8_t)dbytes;
        if (dbytes == 1) diza->flag |= C_DATA1;
        else if (dbytes == 2) diza->flag |= C_DATA2;
        else if (dbytes == 3) diza->flag |= C_DATA1 | C_DATA2;
        else if (dbytes == 4) diza->flag |= C_DATA4;
        else if (dbytes == 8) diza->flag |= C_DATA8;
        else if (dbytes == 6) diza->flag |= C_DATA4 | C_DATA2;
    }

    // 3DNow! puts its opcode in the trailing immediate byte, which the copy
    // above has just stored, so this test belongs after it rather than with
    // the ModR/M group. Only the selectors AMD assigned name an instruction
    // and a prefix does not change that either way.
    if (diza->map == XDE_MAP_0F && diza->opcode2 == 0x0F && legacy_enc &&
        diza->datasize == 1) {
        unsigned sel = diza->data_b[0];
        if (!(now_ok[sel >> 6] & (1ULL << (sel & 63))))
            diza->flag |= C_BAD;
    }

    // 3DNow keeps its own flag: the table marks 0F 0F with XA_3DNOW, which
    // sets C_3DNOW, and the selector above is the immediate byte it names.
    {
        unsigned len = (unsigned)(cur.p - opcode);
        if (len == 0 || len > XDE_MAXLEN)
            return 0;
        diza->len = (uint8_t)len;
        return (int)len;
    }
}

int __cdecl xde_disasm_ex(const uint8_t *opcode, struct xde_instr *diza, unsigned mode)
{
    return xde_disasm_buf(opcode, XDE_MAXLEN, diza, mode);
}

int __cdecl xde_disasm(const uint8_t *opcode, struct xde_instr *diza)
{
    return xde_disasm_buf(opcode, XDE_MAXLEN, diza, XDE_MODE_64);
}

// Bytes xde_asm_buf() emits for counts already clamped to the arrays.
static unsigned asm_size(const struct xde_instr *diza, unsigned nvex,
                         unsigned naddr, unsigned ndata)
{
    unsigned n = 0;

    if (diza->p_lock) n++;
    if (diza->p_rep)  n++;
    if (diza->p_seg)  n++;
    if (diza->p_66)   n++;
    if (diza->p_67)   n++;

    if (nvex) {
        n += nvex + 1;
    } else {
        if (diza->rex)  n++;
        n++;
        if (diza->opcode == 0x0F) {
            n++;
            if (diza->opcode2 == 0x38 || diza->opcode2 == 0x3A)
                n++;
        }
    }

    if (diza->flag & C_MODRM) n++;
    if (diza->flag & C_SIB)   n++;
    return n + naddr + ndata;
}

int __cdecl xde_asm_buf(uint8_t *opcode, unsigned max_len, const struct xde_instr *diza)
{
    uint8_t *p;
    unsigned i, nvex, naddr, ndata;

    if (!opcode || !diza)
        return 0;
    if (max_len == 0 || max_len > XDE_MAXLEN)
        max_len = XDE_MAXLEN;

    // Counts are caller-supplied; clamp them to what the arrays hold.
    nvex  = diza->nvex     > sizeof(diza->vex)    ? (unsigned)sizeof(diza->vex)    : diza->nvex;
    naddr = diza->addrsize > sizeof(diza->addr_b) ? (unsigned)sizeof(diza->addr_b) : diza->addrsize;
    ndata = diza->datasize > sizeof(diza->data_b) ? (unsigned)sizeof(diza->data_b) : diza->datasize;

    if (asm_size(diza, nvex, naddr, ndata) > max_len)
        return 0;

    p = opcode;

    // Legacy prefixes in SDM group order (1: lock/rep, 2: segment, 3: operand
    // size, 4: address size), so re-encoding always yields the canonical order
    // no matter what order the input used.
    if (diza->p_lock) *p++ = diza->p_lock;
    if (diza->p_rep)  *p++ = diza->p_rep;
    if (diza->p_seg)  *p++ = diza->p_seg;
    if (diza->p_66)   *p++ = diza->p_66;
    if (diza->p_67)   *p++ = diza->p_67;

    if (nvex) {
        for (i = 0; i < nvex; i++)
            *p++ = diza->vex[i];
        *p++ = diza->opcode;
    } else {
        if (diza->rex)  *p++ = diza->rex;
        *p++ = diza->opcode;
        if (diza->opcode == 0x0F) {
            *p++ = diza->opcode2;
            if (diza->opcode2 == 0x38 || diza->opcode2 == 0x3A)
                *p++ = diza->opcode3;
        }
    }

    if (diza->flag & C_MODRM) *p++ = diza->modrm;
    if (diza->flag & C_SIB)   *p++ = diza->sib;
    for (i = 0; i < naddr; i++) *p++ = diza->addr_b[i];
    for (i = 0; i < ndata; i++) *p++ = diza->data_b[i];

    return (int)(p - opcode);
}

int __cdecl xde_asm(uint8_t *opcode, const struct xde_instr *diza)
{
    return xde_asm_buf(opcode, XDE_MAXLEN, diza);
}
