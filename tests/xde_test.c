// XDE 2.00 self-test: length, encoding class, and rt assembly.

#include "xde.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

static void fail(const char *name, const char *msg)
{
    printf("FAIL %s: %s\n", name, msg);
    g_fail++;
}

static void hexbytes(char *out, const uint8_t *b, int n)
{
    int i;
    out[0] = 0;
    for (i = 0; i < n; i++) {
        char tmp[8];
        sprintf(tmp, "%s%02X", i ? " " : "", b[i]);
        strcat(out, tmp);
    }
}

static void expect_len(const char *name, unsigned mode,
                       const uint8_t *b, unsigned n, int want)
{
    struct xde_instr d;
    int got = xde_disasm_buf(b, n, &d, mode);
    char hx[128];
    hexbytes(hx, b, (int)n);
    if (got != want) {
        char msg[256];
        sprintf(msg, "len=%d want=%d bytes=%s", got, want, hx);
        fail(name, msg);
        return;
    }
    if (got > 0 && d.len != (uint8_t)got) {
        fail(name, "diza.len mismatch");
        return;
    }
    printf("ok %-28s %s len=%d\n", name, hx, got);
}

static void expect_enc(const char *name, unsigned mode,
                       const uint8_t *b, unsigned n, int want_len, int enc)
{
    struct xde_instr d;
    int got = xde_disasm_buf(b, n, &d, mode);
    if (got != want_len) {
        char msg[128];
        sprintf(msg, "len=%d want=%d", got, want_len);
        fail(name, msg);
        return;
    }
    if (d.enc != (uint8_t)enc) {
        char msg[128];
        sprintf(msg, "enc=%u want=%d", d.enc, enc);
        fail(name, msg);
        return;
    }
    printf("ok %-28s enc=%d len=%d\n", name, enc, got);
}

static void expect_fail(const char *name, unsigned mode, const uint8_t *b, unsigned n)
{
    struct xde_instr d;
    int got = xde_disasm_buf(b, n, &d, mode);
    if (got != 0) {
        char msg[128];
        sprintf(msg, "expected 0, got %d", got);
        fail(name, msg);
        return;
    }
    printf("ok %-28s rejected\n", name);
}

static void expect_roundtrip(const char *name, unsigned mode, const uint8_t *b, unsigned n)
{
    struct xde_instr d;
    uint8_t out[16];
    int got, asz, got2;
    struct xde_instr d2;

    got = xde_disasm_buf(b, 15, &d, mode);
    if (got != (int)n) {
        char msg[128];
        sprintf(msg, "disasm len=%d want=%u", got, n);
        fail(name, msg);
        return;
    }
    asz = xde_asm(out, &d);
    if (asz != got) {
        char msg[128];
        sprintf(msg, "asm len=%d want=%d", asz, got);
        fail(name, msg);
        return;
    }
    if (memcmp(out, b, n) != 0) {
        char hx[128];
        hexbytes(hx, out, (int)n);
        fail(name, hx);
        return;
    }
    got2 = xde_disasm_buf(out, 15, &d2, mode);
    if (got2 != got || d2.opcode != d.opcode || d2.modrm != d.modrm) {
        fail(name, "re-disasm mismatch");
        return;
    }
    printf("ok %-28s rt %d\n", name, got);
}

// Assert the size-effect fields the prefix parser derived. They must agree
// with the prefix bytes it recorded, or the struct contradicts itself.
static void expect_sizes(const char *name, unsigned mode, const uint8_t *b, unsigned n,
                         int want_defdata, int want_defaddr)
{
    struct xde_instr d;

    if (xde_disasm_buf(b, n, &d, mode) != (int)n) {
        fail(name, "decode length mismatch");
        return;
    }
    if ((int)d.defdata != want_defdata || (int)d.defaddr != want_defaddr) {
        char msg[128];
        sprintf(msg, "defdata=%u defaddr=%u want=%d,%d", d.defdata, d.defaddr,
                want_defdata, want_defaddr);
        fail(name, msg);
        return;
    }
    printf("ok %-28s defdata=%u defaddr=%u\n", name, d.defdata, d.defaddr);
}

// The bytes xde_asm() writes must decode back to exactly the length it wrote.
// expect_roundtrip() cannot express this for a folded duplicate prefix, whose
// output is deliberately shorter than its input.
static void expect_selflen(const char *name, unsigned mode, const uint8_t *b, unsigned n)
{
    struct xde_instr d, d2;
    uint8_t out[16];
    int got, asz, back;
    char hx[128];

    got = xde_disasm_buf(b, n, &d, mode);
    if (got != (int)n) {
        char msg[128];
        sprintf(msg, "disasm len=%d want=%u", got, n);
        fail(name, msg);
        return;
    }
    asz = xde_asm(out, &d);
    back = asz > 0 ? xde_disasm_buf(out, (unsigned)asz, &d2, mode) : 0;
    if (asz <= 0 || back != asz) {
        char msg[128];
        sprintf(msg, "asm=%d re-decoded=%d", asz, back);
        fail(name, msg);
        return;
    }
    hexbytes(hx, out, asz);
    printf("ok %-28s self %s\n", name, hx);
}

// Assert that one object-set bit is present (want=1) or absent (want=0).
// sel: 0 = src_set, 1 = dst_set, 2 = src_set2, 3 = dst_set2.
static void expect_set(const char *name, unsigned mode, const uint8_t *b, unsigned n,
                       int sel, uint64_t bit, int want)
{
    struct xde_instr d;
    uint64_t got;

    if (xde_disasm_buf(b, n, &d, mode) != (int)n) {
        fail(name, "decode length mismatch");
        return;
    }
    got = sel == 0 ? d.src_set : sel == 1 ? d.dst_set
                     : sel == 2 ? d.src_set2 : d.dst_set2;
    if (((got & bit) != 0) != (want != 0)) {
        char msg[256];
        sprintf(msg, "sel=%d bit=0x%llX want=%d got=0x%llX", sel,
                (unsigned long long)bit, want, (unsigned long long)got);
        fail(name, msg);
        return;
    }
    printf("ok %-28s set%d 0x%llX %s\n", name, sel,
           (unsigned long long)bit, want ? "set" : "clear");
}

// Assert that a whole object set holds exactly `want`. XSET_UNDEF is all ones,
// so a "contains" check cannot tell a modelled form from a wholly unknown one;
// only an exact comparison can. sel: 0 = src_set, 1 = dst_set, 2 = src_set2,
// 3 = dst_set2.
static void expect_seteq(const char *name, unsigned mode, const uint8_t *b, unsigned n,
                         int sel, uint64_t want)
{
    struct xde_instr d;
    uint64_t got;

    if (xde_disasm_buf(b, n, &d, mode) != (int)n) {
        fail(name, "decode length mismatch");
        return;
    }
    got = sel == 0 ? d.src_set : sel == 1 ? d.dst_set
                     : sel == 2 ? d.src_set2 : d.dst_set2;
    if (got != want) {
        char msg[256];
        sprintf(msg, "sel=%d want=0x%llX got=0x%llX", sel,
                (unsigned long long)want, (unsigned long long)got);
        fail(name, msg);
        return;
    }
    printf("ok %-28s set%d 0x%llX\n", name, sel, (unsigned long long)want);
}

// Assert that one flag bit is set (want=1) or clear (want=0).
static void expect_flag(const char *name, unsigned mode, const uint8_t *b, unsigned n,
                        uint64_t bit, int want)
{
    struct xde_instr d;

    if (xde_disasm_buf(b, n, &d, mode) != (int)n) {
        fail(name, "decode length mismatch");
        return;
    }
    if (((d.flag & bit) != 0) != (want != 0)) {
        char msg[128];
        sprintf(msg, "flag 0x%llX want=%d got=0x%llX", (unsigned long long)bit, want,
                (unsigned long long)d.flag);
        fail(name, msg);
        return;
    }
    printf("ok %-28s flag 0x%llX %s\n", name, (unsigned long long)bit,
           want ? "set" : "clear");
}

int main(void)
{
    // 64-bit GP
    {
        static const uint8_t nop[] = { 0x90 };
        expect_len("nop", 64, nop, 1, 1);
    }
    {
        static const uint8_t xor_eax[] = { 0x31, 0xC0 };
        expect_len("xor eax,eax", 64, xor_eax, 2, 2);
    }
    {
        static const uint8_t xor_rax[] = { 0x48, 0x31, 0xC0 };
        expect_len("xor rax,rax", 64, xor_rax, 3, 3);
        expect_roundtrip("xor rax,rax rt", 64, xor_rax, 3);
    }
    {
        static const uint8_t mov_imm64[] = {
            0x48, 0xB8, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
        };
        expect_len("mov rax,imm64", 64, mov_imm64, 10, 10);
    }
    {
        static const uint8_t mov_eax[] = { 0xB8, 0x01, 0x02, 0x03, 0x04 };
        expect_len("mov eax,imm32", 64, mov_eax, 5, 5);
    }
    {
        static const uint8_t mov_r8[] = { 0x49, 0xB8, 1,2,3,4,5,6,7,8 };
        expect_len("mov r8,imm64", 64, mov_r8, 10, 10);
    }
    {
        static const uint8_t rip[] = { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00 };
        expect_len("mov rax,[rip+0]", 64, rip, 7, 7);
        {
            struct xde_instr d;
            xde_disasm(rip, &d);
            if (!(d.flag & C_RIPREL))
                fail("mov rax,[rip+0] flag", "missing C_RIPREL");
            else
                printf("ok %-28s RIPREL\n", "mov rax,[rip+0] flag");
        }
    }
    {
        static const uint8_t callm[] = { 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00 };
        expect_len("call [rip+0]", 64, callm, 6, 6);
    }
    {
        static const uint8_t call[] = { 0xE8, 0x00, 0x00, 0x00, 0x00 };
        expect_len("call rel32", 64, call, 5, 5);
    }
    {
        static const uint8_t call66[] = { 0x66, 0xE8, 0x00, 0x00, 0x00, 0x00 };
        expect_len("66 call rel32 (Intel f64)", 64, call66, 6, 6);
    }
    {
        static const uint8_t ret[] = { 0xC3 };
        expect_len("ret", 64, ret, 1, 1);
    }
    {
        static const uint8_t push[] = { 0x55 };
        expect_len("push rbp", 64, push, 1, 1);
    }
    {
        static const uint8_t sub[] = { 0x48, 0x83, 0xEC, 0x20 };
        expect_len("sub rsp,0x20", 64, sub, 4, 4);
    }
    {
        static const uint8_t sib[] = { 0x89, 0x4C, 0x24, 0x08 };
        expect_len("mov [rsp+8],ecx", 64, sib, 4, 4);
    }
    {
        static const uint8_t r8[] = { 0x4C, 0x8B, 0x44, 0x24, 0x28 };
        expect_len("mov r8,[rsp+0x28]", 64, r8, 5, 5);
    }
    {
        static const uint8_t longnop[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
        expect_len("nop dword [rax+rax]", 64, longnop, 5, 5);
    }
    {
        static const uint8_t endbr[] = { 0xF3, 0x0F, 0x1E, 0xFA };
        expect_len("endbr64", 64, endbr, 4, 4);
    }
    {
        static const uint8_t sys[] = { 0x0F, 0x05 };
        expect_len("syscall", 64, sys, 2, 2);
    }
    {
        static const uint8_t movsxd[] = { 0x48, 0x63, 0xC3 };
        expect_len("movsxd rax,ebx", 64, movsxd, 3, 3);
    }
    {
        static const uint8_t moffs[] = {
            0xA1, 1,2,3,4,5,6,7,8
        };
        expect_len("mov eax,[moffs64]", 64, moffs, 9, 9);
    }
    {
        static const uint8_t rex_moffs[] = {
            0x48, 0xA1, 1,2,3,4,5,6,7,8
        };
        expect_len("mov rax,[moffs64]", 64, rex_moffs, 10, 10);
    }
    {
        static const uint8_t testf7[] = { 0x48, 0xF7, 0xC0, 0xFF, 0x00, 0x00, 0x00 };
        expect_len("test rax,imm32", 64, testf7, 7, 7);
    }
    {
        static const uint8_t bt[] = { 0x48, 0x0F, 0xBA, 0xE0, 0x01 };
        expect_len("bt rax,1", 64, bt, 5, 5);
    }
    {
        static const uint8_t a67[] = { 0x67, 0x8B, 0x00 };
        expect_len("67 mov eax,[eax]", 64, a67, 3, 3);
    }

    // SSE / 0F38 / 0F3A
    {
        static const uint8_t movups[] = { 0x0F, 0x10, 0x00 };
        expect_len("movups xmm0,[rax]", 64, movups, 3, 3);
    }
    {
        static const uint8_t palignr[] = { 0x66, 0x0F, 0x3A, 0x0F, 0xC0, 0x01 };
        expect_len("palignr xmm0,xmm0,1", 64, palignr, 6, 6);
    }
    {
        static const uint8_t pshufb[] = { 0x66, 0x0F, 0x38, 0x00, 0xC1 };
        expect_len("pshufb xmm0,xmm1", 64, pshufb, 5, 5);
    }
    {
        static const uint8_t crc[] = { 0xF2, 0x0F, 0x38, 0xF0, 0xC1 };
        expect_len("crc32 eax,cl", 64, crc, 5, 5);
    }
    {
        static const uint8_t movbe[] = { 0x0F, 0x38, 0xF0, 0x00 };
        expect_len("movbe eax,[rax]", 64, movbe, 4, 4);
    }
    {
        static const uint8_t now[] = { 0x0F, 0x0F, 0xC1, 0xBF };
        expect_len("pavgusb mm0,mm1 (3DNow)", 64, now, 4, 4);
    }

    // VEX
    {
        static const uint8_t vaddps[] = { 0xC5, 0xF8, 0x58, 0xC1 };
        expect_enc("vaddps xmm0,xmm0,xmm1", 64, vaddps, 4, 4, XDE_ENC_VEX2);
        expect_roundtrip("vaddps rt", 64, vaddps, 4);
    }
    {
        static const uint8_t vandn[] = { 0xC4, 0xE2, 0x78, 0xF2, 0xC1 };
        expect_enc("andn eax,eax,ecx", 64, vandn, 5, 5, XDE_ENC_VEX3);
    }
    {
        static const uint8_t vandn64[] = { 0xC4, 0xE2, 0xF8, 0xF2, 0xC1 };
        expect_enc("andn rax,rax,rcx", 64, vandn64, 5, 5, XDE_ENC_VEX3);
    }
    {
        static const uint8_t rorx[] = { 0xC4, 0xE3, 0xFB, 0xF0, 0xC1, 0x03 };
        expect_enc("rorx rax,rcx,3", 64, rorx, 6, 6, XDE_ENC_VEX3);
    }
    {
        static const uint8_t vzeroupper[] = { 0xC5, 0xF8, 0x77 };
        expect_enc("vzeroupper", 64, vzeroupper, 3, 3, XDE_ENC_VEX2);
    }

    // EVEX
    {
        static const uint8_t evadd[] = { 0x62, 0xF1, 0x7C, 0x48, 0x58, 0xC1 };
        expect_enc("vaddps zmm0,zmm0,zmm1", 64, evadd, 6, 6, XDE_ENC_EVEX);
        expect_roundtrip("evex vaddps rt", 64, evadd, 6);
    }
    {
        static const uint8_t evmem[] = {
            0x62, 0xF1, 0x7C, 0x48, 0x58, 0x05, 0x00, 0x00, 0x00, 0x00
        };
        expect_enc("vaddps zmm0,zmm0,[rip]", 64, evmem, 10, 10, XDE_ENC_EVEX);
    }
    {
        static const uint8_t evib[] = {
            0x62, 0xF3, 0x7D, 0x48, 0x0A, 0xC1, 0x01
        };
        expect_enc("vrndscaless xmm0,xmm0,xmm1,1", 64, evib, 7, 7, XDE_ENC_EVEX);
    }

    // XOP
    {
        static const uint8_t vfrcz[] = { 0x8F, 0xE9, 0x78, 0x81, 0xC1 };
        expect_enc("vfrczpd xmm0,xmm1", 64, vfrcz, 5, 5, XDE_ENC_XOP);
        expect_roundtrip("xop vfrczpd rt", 64, vfrcz, 5);
    }
    {
        static const uint8_t vpcom[] = { 0x8F, 0xE8, 0x78, 0xCC, 0xC1, 0x00 };
        expect_enc("vpcomb xmm0,xmm0,xmm1,0", 64, vpcom, 6, 6, XDE_ENC_XOP);
    }
    {
        static const uint8_t bextr[] = { 0x8F, 0xEA, 0x78, 0x10, 0xC1, 0x01, 0x00, 0x00, 0x00 };
        expect_enc("bextr eax,ecx,imm32", 64, bextr, 9, 9, XDE_ENC_XOP);
    }

    // 32-bit mode: LES vs VEX, BOUND vs EVEX
    {
        static const uint8_t les[] = { 0xC4, 0x00 };
        expect_len("les eax,[eax] (32)", 32, les, 2, 2);
    }
    {
        static const uint8_t vex32[] = { 0xC5, 0xF8, 0x58, 0xC0 };
        expect_enc("vaddps (32-bit VEX2)", 32, vex32, 4, 4, XDE_ENC_VEX2);
    }
    {
        static const uint8_t bound[] = { 0x62, 0x00 };
        static const uint8_t bound1[] = { 0x62, 0x08 };
        static const uint8_t bound3_0[] = { 0x62, 0xC0 };
        static const uint8_t bound3_3[] = { 0x62, 0xDF };
        static const uint8_t bound3_7[] = { 0x62, 0xF8 };
        static const uint8_t evex32[] = { 0x62, 0xF1, 0x7C, 0x48, 0x58, 0xC1 };
        expect_len("bound eax,[eax] (32)", 32, bound, 2, 2);
        expect_flag("bound [eax] not bad", 32, bound, 2, C_BAD, 0);
        expect_flag("bound [eax] /1 not bad", 32, bound1, 2, C_BAD, 0);
        // BOUND reads a pair of bounds from memory, so its mod=3 encodings are
        // reserved. The EVEX prefix that shares the 0x62 lead byte is taken
        // first and is unaffected.
        expect_flag("62 /0 m3 bad (32)", 32, bound3_0, 2, C_BAD, 1);
        expect_flag("62 /3 m3 bad (32)", 32, bound3_3, 2, C_BAD, 1);
        expect_flag("62 /7 m3 bad (32)", 32, bound3_7, 2, C_BAD, 1);
        expect_flag("62 /0 m3 bad (16)", 16, bound3_0, 2, C_BAD, 1);
        expect_enc("evex vaddps (32-bit)", 32, evex32, 6, 6, XDE_ENC_EVEX);
        expect_flag("evex vaddps not bad (32)", 32, evex32, 6, C_BAD, 0);
    }
    {
        static const uint8_t inc[] = { 0x40 };
        expect_len("inc eax (32)", 32, inc, 1, 1);
        expect_fail("truncated REX in 64", 64, inc, 1);
    }
    {
        static const uint8_t rex_nop[] = { 0x40, 0x90 };
        expect_len("rex nop", 64, rex_nop, 2, 2);
    }
    {
        static const uint8_t aaa[] = { 0x37 };
        expect_fail("aaa invalid in 64", 64, aaa, 1);
        expect_len("aaa in 32", 32, aaa, 1, 1);
    }
    {
        static const uint8_t push_es[] = { 0x06 };
        expect_fail("push es invalid in 64", 64, push_es, 1);
    }

    {
        static const uint8_t legacy[] = { 0x31, 0xC0 };
        expect_enc("xor eax,eax legacy enc", 64, legacy, 2, 2, XDE_ENC_LEGACY);
    }

    // REX2 (APX)
    {
        static const uint8_t rex2[] = { 0xD5, 0x40, 0x8D, 0x00 };
        expect_enc("rex2 lea r16d,[rax]", 64, rex2, 4, 4, XDE_ENC_REX2);
    }
    {
        static const uint8_t rex2m[] = { 0xD5, 0xC0, 0xAF, 0xC0 };
        expect_enc("rex2 imul r16d,eax", 64, rex2m, 4, 4, XDE_ENC_REX2);
    }

    // Object sets: 8-bit extension registers vs high bytes, and APX EGPRs.
    {
        static const uint8_t spl_mov[] = { 0x40, 0x88, 0xC4 };
        expect_set("mov spl,al", 64, spl_mov, 3, 1, XSET_SPL, 1);
        expect_set("mov spl,al (not SP)", 64, spl_mov, 3, 1, XSET_SP, 0);
        expect_set("mov spl,al (src AL)", 64, spl_mov, 3, 0, XSET_AL, 1);
        expect_set("mov spl,al (no src SPL)", 64, spl_mov, 3, 0, XSET_SPL, 0);
    }
    {
        static const uint8_t ah_mov[] = { 0x88, 0xC4 };
        expect_set("mov ah,al", 64, ah_mov, 2, 1, XSET_AH, 1);
        expect_set("mov ah,al (not SPL)", 64, ah_mov, 2, 1, XSET_SPL, 0);
    }
    {
        static const uint8_t egpr_lea[] = { 0xD5, 0x40, 0x8D, 0x00 };
        expect_set("rex2 lea r16d dst2", 64, egpr_lea, 4, 3, XSET2_R16, 1);
        expect_set("rex2 lea r16d (not other)", 64, egpr_lea, 4, 1, XSET_OTHER, 0);
    }
    {
        static const uint8_t egpr_lea31[] = { 0xD5, 0x44, 0x8D, 0x38 };
        expect_set("rex2 lea r31d,[rax]", 64, egpr_lea31, 4, 3, XSET2_R31, 1);
    }
    {
        static const uint8_t egpr_push[] = { 0xD5, 0x10, 0x50 };
        expect_set("rex2 push r16", 64, egpr_push, 3, 2, XSET2_R16, 1);
    }
    {
        static const uint8_t egpr_sib[] = { 0xD5, 0x10, 0x8B, 0x04, 0x00 };
        expect_set("rex2 mov eax,[r16]", 64, egpr_sib, 5, 2, XSET2_R16, 1);
        expect_set("rex2 mov eax,[r16+rax]", 64, egpr_sib, 5, 2, XSET_RAX, 1);
    }
    {
        // mod == 3 with REX2: the reg field is an EGPR, the r/m a legacy GPR.
        static const uint8_t egpr_add[] = { 0xD5, 0x40, 0x01, 0xC0 };
        expect_set("rex2 add rax,r16", 64, egpr_add, 4, 2, XSET2_R16, 1);
        expect_set("rex2 add rax,r16 dst", 64, egpr_add, 4, 1, XSET_RAX, 1);
        expect_set("rex2 add rax,r16 (not other)", 64, egpr_add, 4, 1, XSET_OTHER, 0);
    }
    {
        char buf[512];
        struct xde_instr d;
        static const uint8_t egpr_lea2[] = { 0xD5, 0x40, 0x8D, 0x00 };

        xde_sprintset2(buf, XSET2_R16);
        if (strcmp(buf, "R16") != 0)
            fail("sprintset2 R16", buf);
        else
            printf("ok %-28s %s\n", "sprintset2 R16", buf);

        xde_sprintset2(buf, XSET2_R8B);
        if (strcmp(buf, "R8B") != 0)
            fail("sprintset2 R8B", buf);
        else
            printf("ok %-28s %s\n", "sprintset2 R8B", buf);

        xde_sprintset(buf, XSET_SPL);
        if (strcmp(buf, "SPL") != 0)
            fail("sprintset SPL", buf);
        else
            printf("ok %-28s %s\n", "sprintset SPL", buf);

        xde_disasm(egpr_lea2, &d);
        xde_sprintset2(buf, d.dst_set2);
        if (strcmp(buf, "R16") != 0)
            fail("sprintset2 decoded", buf);
        else
            printf("ok %-28s %s\n", "sprintset2 decoded", buf);
    }

    // REX2 round-trip with a legacy prefix, MOV store forms, 8-bit r8-r15.
    {
        static const uint8_t rex2_66[] = { 0x66, 0xD5, 0x40, 0x8D, 0x00 };
        expect_roundtrip("66 rex2 lea rt", 64, rex2_66, 5);
    }
    {
        static const uint8_t mov_al_imm[] = { 0xC6, 0xC0, 0x12 };
        expect_set("mov al,0x12 dst", 64, mov_al_imm, 3, 1, XSET_AL, 1);
        expect_set("mov al,0x12 (no src)", 64, mov_al_imm, 3, 0, XSET_AL, 0);
    }
    {
        static const uint8_t mov_load[] = { 0x8B, 0xC3 };
        expect_set("mov eax,ebx src", 64, mov_load, 2, 0, XSET_EBX, 1);
        expect_set("mov eax,ebx dst", 64, mov_load, 2, 1, XSET_EAX, 1);
    }
    {
        static const uint8_t mov_r8b[] = { 0x41, 0x88, 0xC0 };
        expect_set("mov r8b,al R8", 64, mov_r8b, 3, 1, XSET_R8, 1);
        expect_set("mov r8b,al R8B", 64, mov_r8b, 3, 3, XSET2_R8B, 1);
    }
    {
        static const uint8_t mov_r15b[] = { 0x41, 0x88, 0xC7 };
        expect_set("mov r15b,al R15", 64, mov_r15b, 3, 1, XSET_R15, 1);
        expect_set("mov r15b,al R15B", 64, mov_r15b, 3, 3, XSET2_R15B, 1);
    }
    {
        static const uint8_t mov_r8q[] = { 0x49, 0x8B, 0xC0 };
        expect_set("mov rax,r8 src R8", 64, mov_r8q, 3, 0, XSET_R8, 1);
        expect_set("mov rax,r8 (no R8B)", 64, mov_r8q, 3, 2, XSET2_R8B, 0);
    }
    {
        static const uint8_t mov_r8d[] = { 0x44, 0x8B, 0xC0 };
        expect_set("mov r8d,eax R8", 64, mov_r8d, 3, 1, XSET_R8, 1);
        expect_set("mov r8d,eax (no R8B)", 64, mov_r8d, 3, 3, XSET2_R8B, 0);
    }

    // XOP gate in 16/32-bit, the relocated C_REL flag, and xde_asm limits.
    {
        static const uint8_t xop32[] = { 0x8F, 0x08 };
        expect_len("8f 08 (32-bit POP)", 32, xop32, 2, 2);
        expect_flag("8f 08 bad (32)", 32, xop32, 2, C_BAD, 1);
    }
    {
        static const uint8_t callrel[] = { 0xE8, 0x00, 0x00, 0x00, 0x00 };
        expect_flag("call rel32 has C_REL", 64, callrel, 5, C_REL, 1);
        expect_flag("call rel32 no C_BAD", 64, callrel, 5, C_BAD, 0);
    }
    {
        // Counts longer than the arrays must not read or write past them.
        uint8_t out[32];
        struct xde_instr d;

        memset(&d, 0, sizeof(d));
        d.opcode = 0x90;
        d.datasize = 200;
        if (xde_asm(out, &d) != 9) {
            fail("asm clamp datasize", "want 9");
        } else {
            printf("ok %-28s clamped\n", "asm clamp datasize");
        }
        d.datasize = 0;
        d.addrsize = 200;
        if (xde_asm(out, &d) != 9) {
            fail("asm clamp addrsize", "want 9");
        } else {
            printf("ok %-28s clamped\n", "asm clamp addrsize");
        }
        d.addrsize = 0;
        d.nvex = 200;
        if (xde_asm(out, &d) != 5) {
            fail("asm clamp nvex", "want 5");
        } else {
            printf("ok %-28s clamped\n", "asm clamp nvex");
        }
    }
    {
        static const uint8_t rip7[] = { 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00 };
        uint8_t out[16];
        struct xde_instr d;

        xde_disasm(rip7, &d);
        if (xde_asm_buf(out, 4, &d) != 0) {
            fail("asm_buf too small", "want 0");
        } else {
            printf("ok %-28s refused\n", "asm_buf too small");
        }
        if (xde_asm_buf(out, 7, &d) != 7) {
            fail("asm_buf exact", "want 7");
        } else {
            printf("ok %-28s fits\n", "asm_buf exact");
        }
    }
    {
        char buf[512];
        struct xde_instr d;
        static const uint8_t pushrbp[] = { 0x55 };
        static const uint8_t retn[] = { 0xC3 };

        xde_disasm(pushrbp, &d);
        xde_sprintfl(buf, d.flag);
        if (strstr(buf, "C_PUSH") == NULL) {
            fail("sprintfl C_PUSH", buf);
        } else {
            printf("ok %-28s %s\n", "sprintfl C_PUSH", buf);
        }

        xde_disasm(retn, &d);
        xde_sprintfl(buf, d.flag);
        if (strstr(buf, "C_CMD_RET") == NULL) {
            fail("sprintfl C_CMD_RET", buf);
        } else {
            printf("ok %-28s %s\n", "sprintfl C_CMD_RET", buf);
        }

        {
            static const uint8_t movimm[] = { 0xB8, 0x01, 0x02, 0x03, 0x04 };
            static const uint8_t riprel[] = { 0x48, 0x8B, 0x05, 0, 0, 0, 0 };

            xde_disasm(movimm, &d);
            xde_sprintfl(buf, d.flag);
            if (strstr(buf, "C_DATA4") == NULL) {
                fail("sprintfl C_DATA4", buf);
            } else {
                printf("ok %-28s %s\n", "sprintfl C_DATA4", buf);
            }

            xde_disasm(riprel, &d);
            xde_sprintfl(buf, d.flag);
            if (strstr(buf, "C_ADDR4") == NULL) {
                fail("sprintfl C_ADDR4", buf);
            } else {
                printf("ok %-28s %s\n", "sprintfl C_ADDR4", buf);
            }
        }

        xde_sprintset2(buf, XSET2_ALL | 0x10000000000ULL);
        if (strcmp(buf, "???") != 0) {
            fail("sprintset2 undef subset", buf);
        } else {
            printf("ok %-28s %s\n", "sprintset2 undef subset", buf);
        }
    }

    // Canonical prefix order, and the flag intents recorded in xde102/todo.
    {
        static const uint8_t messy[] = { 0x67, 0x66, 0x90 };
        static const uint8_t canon[] = { 0x66, 0x67, 0x90 };
        static const uint8_t messy2[] = { 0x64, 0xF3, 0xA4 };
        static const uint8_t canon2[] = { 0xF3, 0x64, 0xA4 };
        uint8_t out[16];
        struct xde_instr d;

        if (xde_disasm(messy, &d) != 3 || xde_asm(out, &d) != 3 ||
            memcmp(out, canon, 3) != 0) {
            fail("canonical 66 67", "want 66 67 90");
        } else {
            printf("ok %-28s 66 67 90\n", "canonical 66 67");
        }
        if (xde_disasm(messy2, &d) != 3 || xde_asm(out, &d) != 3 ||
            memcmp(out, canon2, 3) != 0) {
            fail("canonical f3 64", "want F3 64 A4");
        } else {
            printf("ok %-28s F3 64 A4\n", "canonical f3 64");
        }
    }
    {
        // A repeated 66/67 is the same SDM prefix group, so only the last one
        // counts: the override stays in force instead of toggling back to the
        // mode default. Cancelling it leaves p_66/p_67 set with a default
        // operand/address size, and then the bytes xde_asm() writes no longer
        // decode to their own length. C_BAD still marks the repeat.
        static const uint8_t dup67[] = { 0x67, 0x67, 0x00, 0x06, 0x11, 0x22 };
        static const uint8_t dup66_push[] = { 0x66, 0x66, 0x06 };
        static const uint8_t dup66_jcc[] = { 0x66, 0x66, 0x0F, 0x80, 0x11, 0x22 };
        static const uint8_t dup67_moffs[] = { 0x67, 0x67, 0xA0, 0x11, 0x22, 0x33, 0x44 };
        static const uint8_t dup_seg[] = { 0x2E, 0x26, 0x06 };

        expect_sizes("67 67 keeps addr16 (32)", 32, dup67, 6, 4, 2);
        expect_len("67 67 add [si],al (32)", 32, dup67, 6, 6);
        expect_selflen("67 67 self-consistent", 32, dup67, 6);
        expect_flag("67 67 dup still bad", 32, dup67, 6, C_BAD, 1);

        expect_sizes("66 66 keeps opsz16 (32)", 32, dup66_push, 3, 2, 4);
        expect_len("66 66 push es (32)", 32, dup66_push, 3, 3);

        expect_len("66 66 jcc rel16 (32)", 32, dup66_jcc, 6, 6);
        expect_selflen("66 66 jcc self-consistent", 32, dup66_jcc, 6);
        expect_flag("66 66 jcc dup still bad", 32, dup66_jcc, 6, C_BAD, 1);

        expect_len("67 67 mov al,[moffs32] (64)", 64, dup67_moffs, 7, 7);
        expect_selflen("67 67 moffs self-consistent", 64, dup67_moffs, 7);
        expect_flag("67 67 moffs dup still bad", 64, dup67_moffs, 7, C_BAD, 1);

        // The segment group is the precedent: a later segment prefix already
        // replaces the earlier one, so the fold stays self-consistent.
        expect_selflen("2E 26 seg last wins", 32, dup_seg, 3);
        expect_flag("2E 26 dup still bad", 32, dup_seg, 3, C_BAD, 1);
    }
    {
        // A REX prefix cannot be followed by a VEX/EVEX/XOP/REX2 lead byte:
        // SDM treats that form as #UD, so it must not decode as an ordinary
        // REX-prefixed instruction (which silently drops the REX byte on
        // re-encoding). Legal REX and prefix-free VEX keep C_BAD clear.
        static const uint8_t rex_vex2[] = { 0x40, 0xC5, 0x04, 0x08 };
        static const uint8_t rex_vex3[] = { 0x48, 0xC4, 0xE2, 0x78, 0xF2, 0xC1 };
        static const uint8_t rex_evex[] = { 0x48, 0x62, 0xF1, 0x7C, 0x48, 0x58, 0xC1 };
        static const uint8_t rex_xop[] = { 0x48, 0x8F, 0xE9, 0x78, 0x81, 0xC1 };
        static const uint8_t rex_rex2[] = {
            0x66, 0x48, 0xD5, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00
        };
        static const uint8_t rex_xor[] = { 0x48, 0x31, 0xC0 };
        static const uint8_t vex2_bare[] = { 0xC5, 0x04, 0x08 };
        static const uint8_t vex2_vaddps[] = { 0xC5, 0xF8, 0x58, 0xC1 };

        expect_flag("rex then vex2 bad", 64, rex_vex2, 4, C_BAD, 1);
        expect_flag("rex then vex3 bad", 64, rex_vex3, 6, C_BAD, 1);
        expect_flag("rex then evex bad", 64, rex_evex, 7, C_BAD, 1);
        expect_flag("rex then xop bad", 64, rex_xop, 6, C_BAD, 1);
        expect_flag("rex then rex2 bad", 64, rex_rex2, 9, C_BAD, 1);
        expect_flag("rex xor not bad", 64, rex_xor, 3, C_BAD, 0);
        expect_flag("vex2 without rex not bad", 64, vex2_bare, 3, C_BAD, 0);
        expect_flag("vaddps not bad", 64, vex2_vaddps, 4, C_BAD, 0);
    }
    {
        static const uint8_t rep_movs[] = { 0xF3, 0xA4 };
        static const uint8_t rep_cmps[] = { 0xF3, 0xA6 };
        static const uint8_t cmps[] = { 0xA6 };

        expect_set("rep movsb src no FL", 64, rep_movs, 2, 0, XSET_FL, 0);
        expect_set("rep movsb dst no FL", 64, rep_movs, 2, 1, XSET_FL, 0);
        expect_set("rep movsb src RCX", 64, rep_movs, 2, 0, XSET_RCX, 1);
        expect_set("rep cmpsb src FL", 64, rep_cmps, 2, 0, XSET_FL, 1);
        expect_set("rep cmpsb dst FL", 64, rep_cmps, 2, 1, XSET_FL, 1);
        expect_set("cmpsb dst FL", 64, cmps, 1, 1, XSET_FL, 1);
        expect_set("cmpsb src no FL", 64, cmps, 1, 0, XSET_FL, 0);
    }
    {
        static const uint8_t cld[] = { 0xFC };
        static const uint8_t stdn[] = { 0xFD };
        expect_set("cld dst FL", 64, cld, 1, 1, XSET_FL, 1);
        expect_set("std dst FL", 64, stdn, 1, 1, XSET_FL, 1);
    }
    {
        static const uint8_t sete_al[] = { 0x0F, 0x94, 0xC0 };
        static const uint8_t sete_r8b[] = { 0x41, 0x0F, 0x94, 0xC0 };
        char buf[512];
        struct xde_instr d;

        expect_set("sete al dst", 64, sete_al, 3, 1, XSET_AL, 1);
        expect_set("sete al src FL", 64, sete_al, 3, 0, XSET_FL, 1);
        expect_set("sete al (no src AL)", 64, sete_al, 3, 0, XSET_AL, 0);
        expect_set("sete r8b dst R8B", 64, sete_r8b, 4, 3, XSET2_R8B, 1);

        xde_disasm(sete_al, &d);
        xde_sprintset(buf, d.src_set);
        if (strcmp(buf, "???") == 0) {
            fail("sete al not undef", buf);
        } else {
            printf("ok %-28s %s\n", "sete al not undef", buf);
        }
    }
    {
        static const uint8_t sahf[] = { 0x9E };
        static const uint8_t lahf[] = { 0x9F };
        expect_set("sahf src AH", 64, sahf, 1, 0, XSET_AH, 1);
        expect_set("sahf dst FL", 64, sahf, 1, 1, XSET_FL, 1);
        expect_set("lahf src FL", 64, lahf, 1, 0, XSET_FL, 1);
        expect_set("lahf dst AH", 64, lahf, 1, 1, XSET_AH, 1);
    }
    {
        // Every printer must stay inside the documented 256-byte buffer, for
        // any input, including the impossible-looking all-bits case.
        char buf[512];
        unsigned n;
        const uint64_t allflags =
            C_ADDR1 | C_ADDR2 | C_ADDR4 | C_MODRM | C_SIB | C_ADDR67 | C_DATA66 |
            C_UNDEF | C_DATA1 | C_DATA2 | C_DATA4 | C_BAD | C_REL | C_STOP |
            C_OPSZ8 | C_PUSH | C_POP | C_DATA8 | C_ADDR8 | C_RIPREL | C_REX |
            C_VEX | C_EVEX | C_XOP | C_REX2 | C_I64 | C_O64 | C_F64 | C_D64 |
            C_3DNOW | C_CMD_CALL;

        xde_sprintfl(buf, allflags);
        n = (unsigned)strlen(buf);
        if (n == 0 || n >= 256) {
            fail("sprintfl worst case", "over 255 bytes or empty");
        } else {
            printf("ok %-28s %u bytes\n", "sprintfl worst case", n);
        }

        xde_sprintset(buf, ~0ULL ^ (1ULL << 63));
        n = (unsigned)strlen(buf);
        if (n == 0 || n >= 256) {
            fail("sprintset worst case", "over 255 bytes or empty");
        } else {
            printf("ok %-28s %u bytes\n", "sprintset worst case", n);
        }

        xde_sprintset2(buf, XSET2_ALL & ~XSET2_R16);
        n = (unsigned)strlen(buf);
        if (n == 0 || n >= 256) {
            fail("sprintset2 worst case", "over 255 bytes or empty");
        } else {
            printf("ok %-28s %u bytes\n", "sprintset2 worst case", n);
        }
    }

    // Coverage: mode-dependent sets, implicit registers, I/O and flags.
    {
        static const uint8_t push64[] = { 0x55 };
        expect_set("push rbp src RSP", 64, push64, 1, 0, XSET_RSP, 1);
        expect_set("push rbp src RBP", 64, push64, 1, 0, XSET_RBP, 1);
        expect_set("push rbp dst MEM", 64, push64, 1, 1, XSET_MEM, 1);
        expect_flag("push rbp C_D64", 64, push64, 1, C_D64, 1);
        expect_set("push (32) src ESP", 32, push64, 1, 0, XSET_ESP, 1);
        expect_set("push (32) not RSP", 32, push64, 1, 0, XSET_RSP & ~XSET_ESP, 0);
    }
    {
        static const uint8_t movsb16[] = { 0xA4 };
        static const uint8_t mov16[] = { 0x8B, 0x06, 0x34, 0x12 };
        expect_set("movsb (16) src SI", 16, movsb16, 1, 0, XSET_SI, 1);
        expect_set("movsb (16) src DI", 16, movsb16, 1, 0, XSET_DI, 1);
        expect_set("mov ax,[1234] (16) src M", 16, mov16, 4, 0, XSET_MEM, 1);
        expect_set("mov ax,[1234] (16) dst AX", 16, mov16, 4, 1, XSET_AX, 1);
        expect_flag("mov ax,[1234] C_ADDR2", 16, mov16, 4, C_ADDR2, 1);
    }
    {
        static const uint8_t pusha32[] = { 0x60 };
        expect_set("pusha (32) src EAX", 32, pusha32, 1, 0, XSET_EAX, 1);
        expect_set("pusha (32) src EDI", 32, pusha32, 1, 0, XSET_EDI, 1);
        expect_set("pusha (32) src ESP", 32, pusha32, 1, 0, XSET_ESP, 1);
    }
    {
        static const uint8_t in_dx[] = { 0xEC };
        static const uint8_t out_dx[] = { 0xEE };
        static const uint8_t in_imm[] = { 0xE5, 0x10 };
        expect_set("in al,dx src DEV", 64, in_dx, 1, 0, XSET_DEV, 1);
        expect_set("in al,dx src DX", 64, in_dx, 1, 0, XSET_DX, 1);
        expect_set("out dx,al src DEV", 64, out_dx, 1, 0, XSET_DEV, 1);
        expect_set("out dx,al src DX", 64, out_dx, 1, 0, XSET_DX, 1);
        expect_set("out dx,al (not dst DX)", 64, out_dx, 1, 1, XSET_DX, 0);
        expect_set("in eax,0x10 src DEV", 64, in_imm, 2, 0, XSET_DEV, 1);
        expect_set("in eax,0x10 (no DX)", 64, in_imm, 2, 0, XSET_DX, 0);
    }
    {
        static const uint8_t cpuid[] = { 0x0F, 0xA2 };
        expect_set("cpuid src EAX", 64, cpuid, 2, 0, XSET_EAX, 1);
        // The leaf's sub-leaf index is an input in ECX as well.
        expect_set("cpuid src ECX", 64, cpuid, 2, 0, XSET_ECX, 1);
        expect_set("cpuid dst EAX", 64, cpuid, 2, 1, XSET_EAX, 1);
        expect_set("cpuid dst EBX", 64, cpuid, 2, 1, XSET_EBX, 1);
        expect_set("cpuid dst ECX", 64, cpuid, 2, 1, XSET_ECX, 1);
        expect_set("cpuid dst EDX", 64, cpuid, 2, 1, XSET_EDX, 1);
    }
    {
        static const uint8_t mov_es[] = { 0x8C, 0xC0 };
        static const uint8_t mov_sreg[] = { 0x8E, 0xC0 };
        static const uint8_t leave[] = { 0xC9 };
        static const uint8_t shl_cl[] = { 0xD2, 0xE0 };
        static const uint8_t rcl_cl[] = { 0xD2, 0xD0 };
        expect_set("mov ax,es src other", 64, mov_es, 2, 0, XSET_OTHER, 1);
        expect_set("mov es,ax dst other", 64, mov_sreg, 2, 1, XSET_OTHER, 1);
        expect_set("leave src RSP", 64, leave, 1, 0, XSET_RSP, 1);
        expect_set("leave dst RBP", 64, leave, 1, 1, XSET_RBP, 1);
        expect_set("shl al,cl src CL", 64, shl_cl, 2, 0, XSET_CL, 1);
        expect_set("shl al,cl src AL", 64, shl_cl, 2, 0, XSET_AL, 1);
        expect_set("shl al,cl (no src FL)", 64, shl_cl, 2, 0, XSET_FL, 0);
        expect_set("shl al,cl dst AL", 64, shl_cl, 2, 1, XSET_AL, 1);
        expect_set("shl al,cl dst FL", 64, shl_cl, 2, 1, XSET_FL, 1);
        expect_set("rcl al,cl src FL", 64, rcl_cl, 2, 0, XSET_FL, 1);
        expect_set("rcl al,cl dst FL", 64, rcl_cl, 2, 1, XSET_FL, 1);
        {
            static const uint8_t test_eax[] = { 0xF7, 0xC0, 0x01, 0, 0, 0 };
            static const uint8_t not_al[] = { 0xF6, 0xD0 };
            expect_set("test eax,1 dst FL", 64, test_eax, 6, 1, XSET_FL, 1);
            expect_set("not al (no dst FL)", 64, not_al, 2, 1, XSET_FL, 0);
        }
    }
    {
        static const uint8_t retn[] = { 0xC3 };
        static const uint8_t jmp8[] = { 0xEB, 0x00 };
        static const uint8_t jz8[] = { 0x74, 0x00 };
        static const uint8_t sysc[] = { 0x0F, 0x05 };
        static const uint8_t callrel[] = { 0xE8, 0, 0, 0, 0 };
        static const uint8_t inc32[] = { 0x40 };
        static const uint8_t now[] = { 0x0F, 0x0F, 0xC1, 0xBF };
        static const uint8_t mov8[] = { 0x88, 0xC4 };
        static const uint8_t sib[] = { 0x89, 0x4C, 0x24, 0x08 };
        expect_flag("ret C_STOP", 64, retn, 1, C_STOP, 1);
        expect_flag("ret C_CMD_RET", 64, retn, 1, C_CMD_RET, 1);
        expect_flag("jmp rel8 C_CMD_JMP", 64, jmp8, 2, C_CMD_JMP, 1);
        expect_flag("jz rel8 C_CMD_JCC", 64, jz8, 2, C_CMD_JCC, 1);
        expect_flag("call rel32 C_CMD_CALL", 64, callrel, 5, C_CMD_CALL, 1);
        expect_flag("inc eax (32) C_I64", 32, inc32, 1, C_I64, 1);
        expect_flag("jz rel8 C_REL", 64, jz8, 2, C_REL, 1);
        expect_flag("jz rel8 C_F64", 64, jz8, 2, C_F64, 1);
        expect_flag("syscall C_O64", 64, sysc, 2, C_O64, 1);
        expect_flag("pavgusb C_3DNOW", 64, now, 4, C_3DNOW, 1);
        expect_flag("mov ah,al C_OPSZ8", 64, mov8, 2, C_OPSZ8, 1);
        expect_flag("mov [rsp+8],ecx C_SIB", 64, sib, 4, C_SIB, 1);
        expect_flag("mov [rsp+8],ecx C_ADDR1", 64, sib, 4, C_ADDR1, 1);
        {
            static const uint8_t xorq[] = { 0x48, 0x31, 0xC0 };
            static const uint8_t lea2[] = { 0xD5, 0x40, 0x8D, 0x00 };
            static const uint8_t vexps[] = { 0xC5, 0xF8, 0x58, 0xC1 };
            static const uint8_t evexps[] = { 0x62, 0xF1, 0x7C, 0x48, 0x58, 0xC1 };
            static const uint8_t xopps[] = { 0x8F, 0xE9, 0x78, 0x81, 0xC1 };
            static const uint8_t pushr[] = { 0x55 };
            static const uint8_t popr[] = { 0x8F, 0xC0 };
            static const uint8_t moffs8[] = { 0x48, 0xA1, 1, 2, 3, 4, 5, 6, 7, 8 };
            static const uint8_t imm64[] = { 0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8 };
            static const uint8_t imm8b[] = { 0xB0, 0x12 };

            expect_flag("mov ah,al C_MODRM", 64, mov8, 2, C_MODRM, 1);
            expect_flag("xor rax,rax C_REX", 64, xorq, 3, C_REX, 1);
            expect_flag("rex2 lea C_REX2", 64, lea2, 4, C_REX2, 1);
            expect_flag("vaddps C_VEX", 64, vexps, 4, C_VEX, 1);
            expect_flag("evex vaddps C_EVEX", 64, evexps, 6, C_EVEX, 1);
            expect_flag("vfrczpd C_XOP", 64, xopps, 5, C_XOP, 1);
            expect_flag("push rbp C_PUSH", 64, pushr, 1, C_PUSH, 1);
            expect_flag("pop rax C_POP", 64, popr, 2, C_POP, 1);
            expect_flag("mov rax,[moffs64] C_ADDR67", 64, moffs8, 10, C_ADDR67, 1);
            expect_flag("mov rax,[moffs64] C_ADDR8", 64, moffs8, 10, C_ADDR8, 1);
            expect_flag("mov rax,imm64 C_DATA8", 64, imm64, 10, C_DATA8, 1);
            expect_flag("mov al,0x12 C_DATA1", 64, imm8b, 2, C_DATA1, 1);
        }
    }
    {
        // 67 in 64-bit makes mod=0/rm=5 an absolute disp32, not RIP-relative.
        static const uint8_t abs32[] = { 0x67, 0x8B, 0x05, 0, 0, 0, 0 };
        expect_flag("67 abs32 no C_RIPREL", 64, abs32, 7, C_RIPREL, 0);
        expect_flag("67 abs32 C_ADDR4", 64, abs32, 7, C_ADDR4, 1);
        expect_set("67 abs32 src M", 64, abs32, 7, 0, XSET_MEM, 1);
    }
    {
        static const uint8_t mov_imm16[] = { 0x66, 0xB8, 0x34, 0x12 };
        static const uint8_t ud2[] = { 0x0F, 0x0B };
        char buf[512];
        struct xde_instr d;

        expect_flag("66 mov ax,imm16 C_DATA2", 64, mov_imm16, 4, C_DATA2, 1);
        expect_set("66 mov ax,imm16 dst AX", 64, mov_imm16, 4, 1, XSET_AX, 1);

        expect_flag("ud2 C_UNDEF", 64, ud2, 2, C_UNDEF, 1);
        xde_disasm(ud2, &d);
        xde_sprintset(buf, d.src_set);
        if (strcmp(buf, "???") != 0) {
            fail("ud2 sets undefined", buf);
        } else {
            printf("ok %-28s %s\n", "ud2 sets undefined", buf);
        }
    }
    // Group-encoded operand forms: 8F /0 POP, F6 /0 TEST, F6 /2 NOT, FF /2 CALL.
    {
        static const uint8_t pop[] = { 0x8F, 0xC0 };
        expect_len("pop rax (8F /0)", 64, pop, 2, 2);
    }
    {
        static const uint8_t test8[] = { 0xF6, 0xC0, 0x12 };
        expect_len("test al,0x12", 64, test8, 3, 3);
    }
    {
        static const uint8_t notal[] = { 0xF6, 0xD0 };
        expect_len("not al", 64, notal, 2, 2);
    }
    {
        static const uint8_t callreg[] = { 0xFF, 0xD0 };
        expect_len("call rax", 64, callreg, 2, 2);
    }

    // Jcc and LOOP/JCXZ report what they test instead of an undefined set.
    {
        static const uint8_t jz8t[] = { 0x74, 0x00 };
        static const uint8_t jz32t[] = { 0x0F, 0x84, 0, 0, 0, 0 };
        static const uint8_t jmp8t[] = { 0xEB, 0x00 };
        static const uint8_t loopn[] = { 0xE2, 0x00 };
        static const uint8_t loope[] = { 0xE1, 0x00 };
        static const uint8_t jcxz[] = { 0xE3, 0x00 };
        char buf[512];
        struct xde_instr d;

        expect_set("jz rel8 src FL", 64, jz8t, 2, 0, XSET_FL, 1);
        expect_set("jz rel32 src FL", 64, jz32t, 6, 0, XSET_FL, 1);
        expect_set("loop src RCX", 64, loopn, 2, 0, XSET_RCX, 1);
        expect_set("loop dst RCX", 64, loopn, 2, 1, XSET_RCX, 1);
        expect_set("loop (no FL)", 64, loopn, 2, 0, XSET_FL, 0);
        expect_set("loope src FL", 64, loope, 2, 0, XSET_FL, 1);
        expect_set("jcxz src RCX", 64, jcxz, 2, 0, XSET_RCX, 1);
        expect_set("jcxz (no dst RCX)", 64, jcxz, 2, 1, XSET_RCX, 0);

        // A relative JMP touches nothing at all, and no Jcc stays undefined.
        xde_disasm(jmp8t, &d);
        xde_sprintset(buf, d.src_set);
        if (strcmp(buf, "") != 0) {
            fail("jmp rel8 empty src", buf);
        } else {
            printf("ok %-28s (empty)\n", "jmp rel8 empty src");
        }
        xde_sprintset(buf, d.dst_set);
        if (strcmp(buf, "") != 0) {
            fail("jmp rel8 empty dst", buf);
        } else {
            printf("ok %-28s (empty)\n", "jmp rel8 empty dst");
        }

        xde_disasm(jz8t, &d);
        xde_sprintset(buf, d.src_set);
        if (strcmp(buf, "F") != 0) {
            fail("jz rel8 src is F", buf);
        } else {
            printf("ok %-28s F\n", "jz rel8 src is F");
        }
    }

    // Implicit operands: strings, conversions, segment and port I/O.
    {
        static const uint8_t lodsb[] = { 0xAC };
        static const uint8_t stosb[] = { 0xAA };
        static const uint8_t insb[] = { 0x6C };
        static const uint8_t outsb[] = { 0x6E };
        expect_set("lodsb src RSI", 64, lodsb, 1, 0, XSET_RSI, 1);
        expect_set("lodsb dst RSI", 64, lodsb, 1, 1, XSET_RSI, 1);
        expect_set("stosb src RDI", 64, stosb, 1, 0, XSET_RDI, 1);
        expect_set("stosb dst RDI", 64, stosb, 1, 1, XSET_RDI, 1);
        expect_set("insb src RDI", 64, insb, 1, 0, XSET_RDI, 1);
        expect_set("insb src DEV", 64, insb, 1, 0, XSET_DEV, 1);
        expect_set("insb src DX", 64, insb, 1, 0, XSET_DX, 1);
        expect_set("outsb src RSI", 64, outsb, 1, 0, XSET_RSI, 1);
        expect_set("outsb src DEV", 64, outsb, 1, 0, XSET_DEV, 1);
    }
    {
        static const uint8_t cbw[] = { 0x66, 0x98 };
        static const uint8_t cwde[] = { 0x98 };
        static const uint8_t cdqe[] = { 0x48, 0x98 };
        static const uint8_t cwd[] = { 0x66, 0x99 };
        static const uint8_t cqo[] = { 0x48, 0x99 };
        expect_set("cbw src AL", 64, cbw, 2, 0, XSET_AL, 1);
        expect_set("cbw dst AX", 64, cbw, 2, 1, XSET_AX, 1);
        expect_set("cwde src AX", 64, cwde, 1, 0, XSET_AX, 1);
        expect_set("cwde dst EAX", 64, cwde, 1, 1, XSET_EAX, 1);
        expect_set("cdqe src EAX", 64, cdqe, 2, 0, XSET_EAX, 1);
        expect_set("cdqe dst RAX", 64, cdqe, 2, 1, XSET_RAX, 1);
        expect_set("cwd src AX", 64, cwd, 2, 0, XSET_AX, 1);
        expect_set("cwd dst DX", 64, cwd, 2, 1, XSET_DX, 1);
        expect_set("cqo src RAX", 64, cqo, 2, 0, XSET_RAX, 1);
        expect_set("cqo dst RDX", 64, cqo, 2, 1, XSET_RDX, 1);
    }
    {
        static const uint8_t aaa[] = { 0x37 };
        static const uint8_t aam[] = { 0xD4, 0x0A };
        static const uint8_t aad[] = { 0xD5, 0x0A };
        static const uint8_t popa32[] = { 0x61 };
        static const uint8_t push_es[] = { 0x06 };
        static const uint8_t pop_es[] = { 0x07 };
        static const uint8_t xlat[] = { 0xD7 };
        static const uint8_t enter[] = { 0xC8, 0x00, 0x00, 0x00 };
        expect_set("aaa (32) src AH", 32, aaa, 1, 0, XSET_AH, 1);
        expect_set("aaa (32) dst AH", 32, aaa, 1, 1, XSET_AH, 1);
        expect_set("aam src AL", 32, aam, 2, 0, XSET_AL, 1);
        expect_set("aam dst AX", 32, aam, 2, 1, XSET_AX, 1);
        expect_set("aad src AX", 32, aad, 2, 0, XSET_AX, 1);
        expect_set("popa (32) dst EAX", 32, popa32, 1, 1, XSET_EAX, 1);
        expect_set("popa (32) dst EDI", 32, popa32, 1, 1, XSET_EDI, 1);
        expect_set("push es (16) src other", 16, push_es, 1, 0, XSET_OTHER, 1);
        expect_set("pop es (16) dst other", 16, pop_es, 1, 1, XSET_OTHER, 1);
        expect_set("xlat src RBX", 64, xlat, 1, 0, XSET_RBX, 1);
        expect_set("enter src RSP", 64, enter, 4, 0, XSET_RSP, 1);
        expect_set("enter dst RBP", 64, enter, 4, 1, XSET_RBP, 1);
    }
    // 0F-map implicit operands, and a VEX vvvv that names a GPR.
    {
        static const uint8_t push_fs[] = { 0x0F, 0xA0 };
        static const uint8_t pop_fs[] = { 0x0F, 0xA1 };
        static const uint8_t shld_cl[] = { 0x0F, 0xA5, 0xC1 };
        static const uint8_t andn[] = { 0xC4, 0xE2, 0x78, 0xF2, 0xC1 };
        expect_set("push fs src other", 64, push_fs, 2, 0, XSET_OTHER, 1);
        expect_set("pop fs dst other", 64, pop_fs, 2, 1, XSET_OTHER, 1);
        expect_set("shld eax,ecx,cl src CL", 64, shld_cl, 3, 0, XSET_CL, 1);
        expect_set("andn src vvvv EAX", 64, andn, 5, 0, XSET_EAX, 1);
        expect_set("andn src rm ECX", 64, andn, 5, 0, XSET_ECX, 1);
        expect_set("andn dst reg EAX", 64, andn, 5, 1, XSET_EAX, 1);
        {
            static const uint8_t bextr[] = { 0x8F, 0xEA, 0x78, 0x10, 0xC1, 0x01, 0, 0, 0 };
            static const uint8_t sarx[] = { 0xC4, 0xE2, 0x7A, 0xF7, 0xC1 };
            expect_set("bextr dst reg EAX", 64, bextr, 9, 1, XSET_EAX, 1);
            expect_set("bextr src rm ECX", 64, bextr, 9, 0, XSET_ECX, 1);
            expect_set("bextr src vvvv EAX", 64, bextr, 9, 0, XSET_EAX, 1);
            expect_set("sarx dst reg EAX", 64, sarx, 5, 1, XSET_EAX, 1);
            expect_set("sarx src rm ECX", 64, sarx, 5, 0, XSET_ECX, 1);
            expect_set("sarx src vvvv EAX", 64, sarx, 5, 0, XSET_EAX, 1);
        }
        // XA_VVVV_GPR covers the BMI family: every operand is a GPR, so
        // OTHER must never appear on either side. One encoding per opcode,
        // half of them with vvvv == 0 (EAX) and half with a non-zero vvvv.
        {
            static const uint8_t andn70[]  = { 0xC4, 0xE2, 0x70, 0xF2, 0xD0 }; // andn edx,ecx,eax
            static const uint8_t bextr78[] = { 0xC4, 0xE2, 0x78, 0xF7, 0xD1 }; // bextr edx,ecx,eax
            static const uint8_t bzhi70[]  = { 0xC4, 0xE2, 0x70, 0xF5, 0xD0 }; // bzhi edx,ecx,eax
            static const uint8_t sarx7a[]  = { 0xC4, 0xE2, 0x7A, 0xF7, 0xD1 }; // sarx edx,ecx,eax
            static const uint8_t shlx71[]  = { 0xC4, 0xE2, 0x71, 0xF7, 0xD0 }; // shlx edx,ecx,eax
            static const uint8_t shrx7b[]  = { 0xC4, 0xE2, 0x7B, 0xF7, 0xD1 }; // shrx edx,ecx,eax
            expect_set("andn70 dst reg EDX", 64, andn70, 5, 1, XSET_EDX, 1);
            expect_set("andn70 src rm EAX", 64, andn70, 5, 0, XSET_EAX, 1);
            expect_set("andn70 src vvvv ECX", 64, andn70, 5, 0, XSET_ECX, 1);
            expect_set("andn70 no src other", 64, andn70, 5, 0, XSET_OTHER, 0);
            expect_set("andn70 no dst other", 64, andn70, 5, 1, XSET_OTHER, 0);
            expect_set("andn70 no src mem", 64, andn70, 5, 0, XSET_MEM, 0);
            expect_set("andn70 no dst mem", 64, andn70, 5, 1, XSET_MEM, 0);
            expect_set("bextr78 dst reg EDX", 64, bextr78, 5, 1, XSET_EDX, 1);
            expect_set("bextr78 src rm ECX", 64, bextr78, 5, 0, XSET_ECX, 1);
            expect_set("bextr78 src vvvv EAX", 64, bextr78, 5, 0, XSET_EAX, 1);
            expect_set("bextr78 no src other", 64, bextr78, 5, 0, XSET_OTHER, 0);
            expect_set("bextr78 no dst other", 64, bextr78, 5, 1, XSET_OTHER, 0);
            expect_set("bextr78 no src mem", 64, bextr78, 5, 0, XSET_MEM, 0);
            expect_set("bextr78 no dst mem", 64, bextr78, 5, 1, XSET_MEM, 0);
            expect_set("bzhi70 dst reg EDX", 64, bzhi70, 5, 1, XSET_EDX, 1);
            expect_set("bzhi70 src rm EAX", 64, bzhi70, 5, 0, XSET_EAX, 1);
            expect_set("bzhi70 src vvvv ECX", 64, bzhi70, 5, 0, XSET_ECX, 1);
            expect_set("bzhi70 no src other", 64, bzhi70, 5, 0, XSET_OTHER, 0);
            expect_set("bzhi70 no dst other", 64, bzhi70, 5, 1, XSET_OTHER, 0);
            expect_set("bzhi70 no src mem", 64, bzhi70, 5, 0, XSET_MEM, 0);
            expect_set("bzhi70 no dst mem", 64, bzhi70, 5, 1, XSET_MEM, 0);
            expect_set("sarx7a dst reg EDX", 64, sarx7a, 5, 1, XSET_EDX, 1);
            expect_set("sarx7a src rm ECX", 64, sarx7a, 5, 0, XSET_ECX, 1);
            expect_set("sarx7a src vvvv EAX", 64, sarx7a, 5, 0, XSET_EAX, 1);
            expect_set("sarx7a no src other", 64, sarx7a, 5, 0, XSET_OTHER, 0);
            expect_set("sarx7a no dst other", 64, sarx7a, 5, 1, XSET_OTHER, 0);
            expect_set("sarx7a no src mem", 64, sarx7a, 5, 0, XSET_MEM, 0);
            expect_set("sarx7a no dst mem", 64, sarx7a, 5, 1, XSET_MEM, 0);
            expect_set("shlx71 dst reg EDX", 64, shlx71, 5, 1, XSET_EDX, 1);
            expect_set("shlx71 src rm EAX", 64, shlx71, 5, 0, XSET_EAX, 1);
            expect_set("shlx71 src vvvv ECX", 64, shlx71, 5, 0, XSET_ECX, 1);
            expect_set("shlx71 no src other", 64, shlx71, 5, 0, XSET_OTHER, 0);
            expect_set("shlx71 no dst other", 64, shlx71, 5, 1, XSET_OTHER, 0);
            expect_set("shlx71 no src mem", 64, shlx71, 5, 0, XSET_MEM, 0);
            expect_set("shlx71 no dst mem", 64, shlx71, 5, 1, XSET_MEM, 0);
            expect_set("shrx7b dst reg EDX", 64, shrx7b, 5, 1, XSET_EDX, 1);
            expect_set("shrx7b src rm ECX", 64, shrx7b, 5, 0, XSET_ECX, 1);
            expect_set("shrx7b src vvvv EAX", 64, shrx7b, 5, 0, XSET_EAX, 1);
            expect_set("shrx7b no src other", 64, shrx7b, 5, 0, XSET_OTHER, 0);
            expect_set("shrx7b no dst other", 64, shrx7b, 5, 1, XSET_OTHER, 0);
            expect_set("shrx7b no src mem", 64, shrx7b, 5, 0, XSET_MEM, 0);
            expect_set("shrx7b no dst mem", 64, shrx7b, 5, 1, XSET_MEM, 0);
        }
        // Plain vector VEX still folds both sides to OTHER, whatever vvvv
        // decodes to: 0xF0 has vvvv = ECX, 0xF8 has vvvv = 0 (vvvv is not a
        // "no operand" sentinel, it just names a register).
        {
            static const uint8_t vaddps70[] = { 0xC5, 0xF0, 0x58, 0xC2 };
            static const uint8_t vaddps78[] = { 0xC5, 0xF8, 0x58, 0xC1 };
            expect_set("vaddps vvvv=1 src other", 64, vaddps70, 4, 0, XSET_OTHER, 1);
            expect_set("vaddps vvvv=1 dst other", 64, vaddps70, 4, 1, XSET_OTHER, 1);
            expect_set("vaddps vvvv=0 src other", 64, vaddps78, 4, 0, XSET_OTHER, 1);
            expect_set("vaddps vvvv=0 dst other", 64, vaddps78, 4, 1, XSET_OTHER, 1);
        }
        // EVEX carries V', so vvvv can name an APX EGPR (r16-r31) of the same
        // GPR fold: ANDN with vvvv = r16 must land in the second set word.
        {
            static const uint8_t ev_andn_r16[] = { 0x62, 0xF2, 0x7C, 0x00, 0xF2, 0xC0 };
            expect_set("evex andn src vvvv R16", 64, ev_andn_r16, 6, 2, XSET2_R16, 1);
            expect_set("evex andn no src other", 64, ev_andn_r16, 6, 0, XSET_OTHER, 0);
            expect_set("evex andn dst reg EAX", 64, ev_andn_r16, 6, 1, XSET_EAX, 1);
        }
    }
    // Addressing forms: SIB with an index, without one, and disp32 no base.
    {
        static const uint8_t sib_idx[] = { 0x8B, 0x04, 0x48 };
        static const uint8_t sib_noidx[] = { 0x8B, 0x04, 0x20 };
        static const uint8_t sib_nobase[] = { 0x8B, 0x04, 0x25, 0, 0, 0, 0 };
        expect_set("sib index src RAX", 64, sib_idx, 3, 0, XSET_RAX, 1);
        expect_set("sib index src RCX", 64, sib_idx, 3, 0, XSET_RCX, 1);
        expect_set("sib no index src RAX", 64, sib_noidx, 3, 0, XSET_RAX, 1);
        expect_set("sib no index (no RCX)", 64, sib_noidx, 3, 0, XSET_RCX, 0);
        expect_set("sib no base src M", 64, sib_nobase, 7, 0, XSET_MEM, 1);
        expect_set("sib no base (no RAX)", 64, sib_nobase, 7, 0, XSET_RAX, 0);
    }
    // Opcode-embedded registers and the implicit accumulator rules.
    {
        static const uint8_t bswap[] = { 0x0F, 0xC8 };
        static const uint8_t xchg[] = { 0x91 };
        static const uint8_t movb[] = { 0xB0, 0x12 };
        static const uint8_t movdi[] = { 0xBF, 0x01, 0, 0, 0 };
        static const uint8_t addal[] = { 0x04, 0x12 };
        static const uint8_t addeax[] = { 0x05, 0x01, 0, 0, 0 };
        static const uint8_t mul[] = { 0xF7, 0xE0 };
        static const uint8_t inc32b[] = { 0x40 };
        expect_set("bswap eax src EAX", 64, bswap, 2, 0, XSET_EAX, 1);
        expect_set("bswap eax dst EAX", 64, bswap, 2, 1, XSET_EAX, 1);
        expect_set("xchg ecx,eax src EAX", 64, xchg, 1, 0, XSET_EAX, 1);
        expect_set("xchg ecx,eax src ECX", 64, xchg, 1, 0, XSET_ECX, 1);
        expect_set("xchg ecx,eax dst ECX", 64, xchg, 1, 1, XSET_ECX, 1);
        expect_set("mov al,imm dst AL", 64, movb, 2, 1, XSET_AL, 1);
        expect_set("mov edi,imm dst EDI", 64, movdi, 5, 1, XSET_EDI, 1);
        expect_set("add al,imm src AL", 64, addal, 2, 0, XSET_AL, 1);
        expect_set("add al,imm dst FL", 64, addal, 2, 1, XSET_FL, 1);
        expect_set("add eax,imm src EAX", 64, addeax, 5, 0, XSET_EAX, 1);
        expect_set("add eax,imm dst FL", 64, addeax, 5, 1, XSET_FL, 1);
        expect_set("mul eax src EAX", 64, mul, 2, 0, XSET_EAX, 1);
        expect_set("mul eax dst EDX", 64, mul, 2, 1, XSET_EDX, 1);
        expect_set("mul eax dst FL", 64, mul, 2, 1, XSET_FL, 1);
        expect_set("inc eax (32) src EAX", 32, inc32b, 1, 0, XSET_EAX, 1);
        expect_set("inc eax (32) dst EAX", 32, inc32b, 1, 1, XSET_EAX, 1);
        expect_set("inc eax (32) dst FL", 32, inc32b, 1, 1, XSET_FL, 1);
    }

    // MOVBE / CRC32 share 0F 38 F0/F1; plus the vector object sets.
    {
        static const uint8_t movbe_load[] = { 0x0F, 0x38, 0xF0, 0x00 };
        static const uint8_t movbe_store[] = { 0x0F, 0x38, 0xF1, 0x02 };
        static const uint8_t crc32b[] = { 0xF2, 0x0F, 0x38, 0xF0, 0xC1 };
        static const uint8_t vfrcz[] = { 0x8F, 0xE9, 0x78, 0x81, 0xC1 };
        static const uint8_t vpcom[] = { 0x8F, 0xE8, 0x78, 0xCC, 0xC1, 0x00 };
        static const uint8_t evmem[] = { 0x62, 0xF1, 0x7C, 0x48, 0x58, 0x05, 0, 0, 0, 0 };

        expect_set("movbe eax,[rax] dst EAX", 64, movbe_load, 4, 1, XSET_EAX, 1);
        expect_set("movbe eax,[rax] src M", 64, movbe_load, 4, 0, XSET_MEM, 1);
        expect_set("movbe [rdx],eax src EAX", 64, movbe_store, 4, 0, XSET_EAX, 1);
        expect_set("movbe [rdx],eax dst M", 64, movbe_store, 4, 1, XSET_MEM, 1);
        expect_set("crc32 eax,cl src ECX", 64, crc32b, 5, 0, XSET_ECX, 1);
        expect_set("crc32 eax,cl dst EAX", 64, crc32b, 5, 1, XSET_EAX, 1);
        expect_set("vfrczpd src other", 64, vfrcz, 5, 0, XSET_OTHER, 1);
        expect_set("vfrczpd dst other", 64, vfrcz, 5, 1, XSET_OTHER, 1);
        expect_set("vpcomb src other", 64, vpcom, 6, 0, XSET_OTHER, 1);
        expect_set("vpcomb dst other", 64, vpcom, 6, 1, XSET_OTHER, 1);
        expect_set("evex vaddps src M", 64, evmem, 10, 0, XSET_MEM, 1);
        expect_set("evex vaddps src other", 64, evmem, 10, 0, XSET_OTHER, 1);
        expect_set("evex vaddps dst other", 64, evmem, 10, 1, XSET_OTHER, 1);
        {
            static const uint8_t endbr[] = { 0xF3, 0x0F, 0x1E, 0xFA };
            static const uint8_t movss[] = { 0xF3, 0x0F, 0x10, 0xC1 };
            // F2/F3 is not a REP outside the string ops: no count register.
            expect_set("endbr64 (no src RCX)", 64, endbr, 4, 0, XSET_RCX, 0);
            expect_set("endbr64 (no dst RCX)", 64, endbr, 4, 1, XSET_RCX, 0);
            expect_set("movss (no src RCX)", 64, movss, 4, 0, XSET_RCX, 0);
            expect_set("movss src other", 64, movss, 4, 0, XSET_OTHER, 1);
        }
        {
            static const uint8_t movups_r[] = { 0x0F, 0x10, 0xC1 };
            static const uint8_t paddb[] = { 0x0F, 0xFC, 0xC1 };
            static const uint8_t movups_m[] = { 0x0F, 0x10, 0x00 };
            static const uint8_t popcnt[] = { 0xF3, 0x0F, 0xB8, 0xC1 };
            static const uint8_t bt[] = { 0x0F, 0xA3, 0xC1 };
            static const uint8_t cmpxchg[] = { 0x0F, 0xB1, 0xC1 };
            static const uint8_t now3d[] = { 0x0F, 0x0F, 0xC1, 0xBF };
            // Legacy SSE/MMX operands collapse to OTHER, never to a GPR.
            expect_set("movups (no src RCX)", 64, movups_r, 3, 0, XSET_RCX, 0);
            expect_set("movups src other", 64, movups_r, 3, 0, XSET_OTHER, 1);
            expect_set("movups dst other", 64, movups_r, 3, 1, XSET_OTHER, 1);
            expect_set("paddb src other", 64, paddb, 3, 0, XSET_OTHER, 1);
            expect_set("movups m dst other", 64, movups_m, 3, 1, XSET_OTHER, 1);
            expect_set("movups m src M", 64, movups_m, 3, 0, XSET_MEM, 1);
            // ... while the GPR opcodes in the same 0F map stay GPR.
            expect_set("popcnt dst EAX", 64, popcnt, 4, 1, XSET_EAX, 1);
            expect_set("popcnt src ECX", 64, popcnt, 4, 0, XSET_ECX, 1);
            expect_set("bt src ECX", 64, bt, 3, 0, XSET_ECX, 1);
            expect_set("cmpxchg src ECX", 64, cmpxchg, 3, 0, XSET_ECX, 1);
            expect_set("pavgusb src other", 64, now3d, 4, 0, XSET_OTHER, 1);
        }
    }

    // SSE mandatory prefixes: the prefix byte is part of the opcode, so the
    // same escaped opcode under any other prefix names no instruction.
    {
        static const uint8_t punpcklqdq[]    = { 0x66, 0x0F, 0x6C, 0xC0 };
        static const uint8_t punpcklqdq_no[] = { 0x0F, 0x6C, 0xC0 };
        static const uint8_t punpcklqdq_f2[] = { 0xF2, 0x0F, 0x6C, 0xC0 };
        static const uint8_t punpcklqdq_f3[] = { 0xF3, 0x0F, 0x6C, 0xC0 };
        static const uint8_t punpckhqdq_no[] = { 0x0F, 0x6D, 0xC0 };
        static const uint8_t punpckhqdq_f2[] = { 0xF2, 0x0F, 0x6D, 0xC0 };
        static const uint8_t popcnt[]        = { 0xF3, 0x0F, 0xB8, 0xC0 };
        static const uint8_t popcnt_no[]     = { 0x0F, 0xB8, 0xC0 };
        static const uint8_t popcnt_66[]     = { 0x66, 0x0F, 0xB8, 0xC0 };
        static const uint8_t popcnt_f2[]     = { 0xF2, 0x0F, 0xB8, 0xC0 };
        static const uint8_t lddqu[]         = { 0xF2, 0x0F, 0xF0, 0x00 };
        static const uint8_t lddqu_no[]      = { 0x0F, 0xF0, 0x00 };
        static const uint8_t lddqu_f3[]      = { 0xF3, 0x0F, 0xF0, 0x00 };
        static const uint8_t haddpd[]        = { 0x66, 0x0F, 0x7C, 0xC0 };
        static const uint8_t haddps[]        = { 0xF2, 0x0F, 0x7C, 0xC0 };
        static const uint8_t hadd_no[]       = { 0x0F, 0x7C, 0xC0 };
        static const uint8_t hadd_f3[]       = { 0xF3, 0x0F, 0x7C, 0xC0 };
        static const uint8_t hsub_no[]       = { 0x0F, 0x7D, 0xC0 };
        static const uint8_t addsubpd[]      = { 0x66, 0x0F, 0xD0, 0xC0 };
        static const uint8_t addsubps[]      = { 0xF2, 0x0F, 0xD0, 0xC0 };
        static const uint8_t addsub_no[]     = { 0x0F, 0xD0, 0xC0 };
        static const uint8_t addsub_f3[]     = { 0xF3, 0x0F, 0xD0, 0xC0 };
        static const uint8_t movq_d6[]       = { 0x66, 0x0F, 0xD6, 0xC0 };
        static const uint8_t movdq2q[]       = { 0xF2, 0x0F, 0xD6, 0xC0 };
        static const uint8_t movq2dq[]       = { 0xF3, 0x0F, 0xD6, 0xC0 };
        static const uint8_t movq_d6_no[]    = { 0x0F, 0xD6, 0xC0 };
        static const uint8_t cvttpd2dq[]     = { 0x66, 0x0F, 0xE6, 0xC0 };
        static const uint8_t cvtpd2dq[]      = { 0xF2, 0x0F, 0xE6, 0xC0 };
        static const uint8_t cvtdq2pd[]      = { 0xF3, 0x0F, 0xE6, 0xC0 };
        static const uint8_t cvt_e6_no[]     = { 0x0F, 0xE6, 0xC0 };
        static const uint8_t wbinvd[]        = { 0x0F, 0x09 };
        static const uint8_t wbnoinvd[]      = { 0xF3, 0x0F, 0x09 };
        static const uint8_t wbinvd_66[]     = { 0x66, 0x0F, 0x09 };
        static const uint8_t wbinvd_f2[]     = { 0xF2, 0x0F, 0x09 };
        static const uint8_t rsqrtps[]       = { 0x0F, 0x52, 0xC0 };
        static const uint8_t rsqrtss[]       = { 0xF3, 0x0F, 0x52, 0xC0 };
        static const uint8_t rsqrt_66[]      = { 0x66, 0x0F, 0x52, 0xC0 };
        static const uint8_t rsqrt_f2[]      = { 0xF2, 0x0F, 0x52, 0xC0 };
        static const uint8_t cvtdq2ps[]      = { 0x0F, 0x5B, 0xC0 };
        static const uint8_t cvtps2dq[]      = { 0x66, 0x0F, 0x5B, 0xC0 };
        static const uint8_t cvttps2dq[]     = { 0xF3, 0x0F, 0x5B, 0xC0 };
        static const uint8_t cvt_5b_f2[]     = { 0xF2, 0x0F, 0x5B, 0xC0 };
        static const uint8_t vmread[]        = { 0x0F, 0x78, 0xC0 };
        static const uint8_t extrq[]         = { 0x66, 0x0F, 0x78, 0xC0 };
        static const uint8_t insertq[]       = { 0xF2, 0x0F, 0x78, 0xC0 };
        static const uint8_t vmread_f3[]     = { 0xF3, 0x0F, 0x78, 0xC0 };
        static const uint8_t movq_mm[]       = { 0x0F, 0x6F, 0xC0 };
        static const uint8_t movdqa[]        = { 0x66, 0x0F, 0x6F, 0xC0 };
        static const uint8_t movdqu[]        = { 0xF3, 0x0F, 0x6F, 0xC0 };
        static const uint8_t movq_mm_f2[]    = { 0xF2, 0x0F, 0x6F, 0xC0 };
        static const uint8_t movd_mm[]       = { 0x0F, 0x7E, 0xC0 };
        static const uint8_t movq_xmm[]      = { 0xF3, 0x0F, 0x7E, 0xC0 };
        static const uint8_t movq_st[]       = { 0x0F, 0x7F, 0xC0 };
        static const uint8_t movdqu_st[]     = { 0xF3, 0x0F, 0x7F, 0xC0 };
        static const uint8_t addps[]         = { 0x0F, 0x58, 0xC1 };
        static const uint8_t addpd[]         = { 0x66, 0x0F, 0x58, 0xC1 };
        static const uint8_t addss[]         = { 0xF3, 0x0F, 0x58, 0xC1 };
        static const uint8_t addsd[]         = { 0xF2, 0x0F, 0x58, 0xC1 };
        static const uint8_t psrlw_f2[]      = { 0xF2, 0x0F, 0x71, 0xD0, 0x12 };
        static const uint8_t psrlw_f3[]      = { 0xF3, 0x0F, 0x71, 0xD0, 0x12 };
        static const uint8_t psrlw[]         = { 0x0F, 0x71, 0xD0, 0x12 };
        static const uint8_t undef_7a[]      = { 0x0F, 0x7A, 0xC0 };
        static const uint8_t undef_7a_66[]   = { 0x66, 0x0F, 0x7A, 0xC0 };
        static const uint8_t undef_7b[]      = { 0x0F, 0x7B, 0xC0 };
        static const uint8_t vaddps[]        = { 0xC5, 0xF8, 0x58, 0xC1 };
        static const uint8_t andn[]          = { 0xC4, 0xE2, 0x78, 0xF2, 0xC1 };
        static const uint8_t vaddps_evex[]   = { 0x62, 0xF1, 0x7C, 0x48, 0x58, 0xC1 };
        expect_flag("punpcklqdq ok", 64, punpcklqdq, 4, C_BAD, 0);
        expect_flag("punpcklqdq no 66 bad", 64, punpcklqdq_no, 3, C_BAD, 1);
        expect_flag("punpcklqdq F2 bad", 64, punpcklqdq_f2, 4, C_BAD, 1);
        expect_flag("punpcklqdq F3 bad", 64, punpcklqdq_f3, 4, C_BAD, 1);
        expect_flag("punpckhqdq no 66 bad", 64, punpckhqdq_no, 3, C_BAD, 1);
        expect_flag("punpckhqdq F2 bad", 64, punpckhqdq_f2, 4, C_BAD, 1);
        expect_flag("popcnt ok", 64, popcnt, 4, C_BAD, 0);
        expect_flag("popcnt no F3 bad", 64, popcnt_no, 3, C_BAD, 1);
        expect_flag("popcnt 66 bad", 64, popcnt_66, 4, C_BAD, 1);
        expect_flag("popcnt F2 bad", 64, popcnt_f2, 4, C_BAD, 1);
        expect_flag("lddqu ok", 64, lddqu, 4, C_BAD, 0);
        expect_flag("lddqu no F2 bad", 64, lddqu_no, 3, C_BAD, 1);
        expect_flag("lddqu F3 bad", 64, lddqu_f3, 4, C_BAD, 1);
        expect_flag("haddpd ok", 64, haddpd, 4, C_BAD, 0);
        expect_flag("haddps ok", 64, haddps, 4, C_BAD, 0);
        expect_flag("hadd no prefix bad", 64, hadd_no, 3, C_BAD, 1);
        expect_flag("hadd F3 bad", 64, hadd_f3, 4, C_BAD, 1);
        expect_flag("hsub no prefix bad", 64, hsub_no, 3, C_BAD, 1);
        expect_flag("addsubpd ok", 64, addsubpd, 4, C_BAD, 0);
        expect_flag("addsubps ok", 64, addsubps, 4, C_BAD, 0);
        expect_flag("addsub no prefix bad", 64, addsub_no, 3, C_BAD, 1);
        expect_flag("addsub F3 bad", 64, addsub_f3, 4, C_BAD, 1);
        expect_flag("66 0F D6 ok", 64, movq_d6, 4, C_BAD, 0);
        expect_flag("F2 0F D6 ok", 64, movdq2q, 4, C_BAD, 0);
        expect_flag("F3 0F D6 ok", 64, movq2dq, 4, C_BAD, 0);
        expect_flag("0F D6 without prefix bad", 64, movq_d6_no, 3, C_BAD, 1);
        expect_flag("66 0F E6 ok", 64, cvttpd2dq, 4, C_BAD, 0);
        expect_flag("F2 0F E6 ok", 64, cvtpd2dq, 4, C_BAD, 0);
        expect_flag("F3 0F E6 ok", 64, cvtdq2pd, 4, C_BAD, 0);
        expect_flag("0F E6 without prefix bad", 64, cvt_e6_no, 3, C_BAD, 1);
        expect_flag("wbinvd ok", 64, wbinvd, 2, C_BAD, 0);
        expect_flag("wbnoinvd ok", 64, wbnoinvd, 3, C_BAD, 0);
        expect_flag("wbinvd 66 bad", 64, wbinvd_66, 3, C_BAD, 1);
        expect_flag("wbinvd F2 bad", 64, wbinvd_f2, 3, C_BAD, 1);
        expect_flag("rsqrtps ok", 64, rsqrtps, 3, C_BAD, 0);
        expect_flag("rsqrtss ok", 64, rsqrtss, 4, C_BAD, 0);
        expect_flag("rsqrtps 66 bad", 64, rsqrt_66, 4, C_BAD, 1);
        expect_flag("rsqrtps F2 bad", 64, rsqrt_f2, 4, C_BAD, 1);
        expect_flag("cvtdq2ps ok", 64, cvtdq2ps, 3, C_BAD, 0);
        expect_flag("cvtps2dq ok", 64, cvtps2dq, 4, C_BAD, 0);
        expect_flag("cvttps2dq ok", 64, cvttps2dq, 4, C_BAD, 0);
        expect_flag("0F 5B F2 bad", 64, cvt_5b_f2, 4, C_BAD, 1);
        expect_flag("vmread ok", 64, vmread, 3, C_BAD, 0);
        expect_flag("extrq ok", 64, extrq, 4, C_BAD, 0);
        expect_flag("insertq ok", 64, insertq, 4, C_BAD, 0);
        expect_flag("0F 78 F3 bad", 64, vmread_f3, 4, C_BAD, 1);
        expect_flag("movq mm ok", 64, movq_mm, 3, C_BAD, 0);
        expect_flag("movdqa ok", 64, movdqa, 4, C_BAD, 0);
        expect_flag("movdqu ok", 64, movdqu, 4, C_BAD, 0);
        expect_flag("movq mm F2 bad", 64, movq_mm_f2, 4, C_BAD, 1);
        expect_flag("movd mm ok", 64, movd_mm, 3, C_BAD, 0);
        expect_flag("movq xmm ok", 64, movq_xmm, 4, C_BAD, 0);
        expect_flag("movq mm store ok", 64, movq_st, 3, C_BAD, 0);
        expect_flag("movdqu store ok", 64, movdqu_st, 4, C_BAD, 0);
        expect_flag("addps ok", 64, addps, 3, C_BAD, 0);
        expect_flag("addpd ok", 64, addpd, 4, C_BAD, 0);
        expect_flag("addss ok", 64, addss, 4, C_BAD, 0);
        expect_flag("addsd ok", 64, addsd, 4, C_BAD, 0);
        expect_flag("F2 0F 71 bad", 64, psrlw_f2, 5, C_BAD, 1);
        expect_flag("F3 0F 71 bad", 64, psrlw_f3, 5, C_BAD, 1);
        expect_flag("0F 71 ok", 64, psrlw, 4, C_BAD, 0);
        expect_flag("0F 7A bad", 64, undef_7a, 3, C_BAD, 1);
        expect_flag("66 0F 7A bad", 64, undef_7a_66, 4, C_BAD, 1);
        expect_flag("0F 7B bad", 64, undef_7b, 3, C_BAD, 1);
        expect_flag("VEX vaddps untouched", 64, vaddps, 4, C_BAD, 0);
        expect_flag("VEX andn untouched", 64, andn, 5, C_BAD, 0);
        expect_flag("EVEX vaddps untouched", 64, vaddps_evex, 6, C_BAD, 0);
    }

    // The escaped-map whitelists, one assertion per opcode. xde_pfx_38 and
    // xde_pfx_3a hold the prefixes the SDM defines each 0F 38 / 0F 3A opcode
    // under as a legacy instruction, so an opcode that exists only as a VEX
    // / EVEX form is absent from them and its legacy encoding names nothing;
    // the third group does the same for the restricted 0F opcodes the block
    // above leaves out. Every byte string here is cross-checked against
    // binutils: the ones it names are asserted with C_BAD clear and the ones
    // it rejects with C_BAD set.
    {
        // 0F 38 (SSSE3 through the SHA, GFNI, Key Locker and MOVDIRI groups).
        expect_flag("0F 38 00 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x00, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 00 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x00, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 01 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x01, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 01 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x01, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 02 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x02, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 02 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x02, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 03 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x03, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 03 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x03, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 04 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x04, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 04 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x04, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 05 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x05, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 05 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x05, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 06 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x06, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 06 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x06, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 07 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x07, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 07 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x07, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 08 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x08, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 08 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x08, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 09 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x09, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 09 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x09, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 0A none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x0A, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 0A F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x0A, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 0B none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x0B, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 0B F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x0B, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 0C none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x0C, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 0D none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x0D, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 0E none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x0E, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 0F none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x0F, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 10 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x10, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 10 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x10, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 11 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x11, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 12 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x12, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 13 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x13, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 14 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x14, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 14 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x14, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 15 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x15, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 15 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x15, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 16 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x16, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 17 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x17, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 17 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x17, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 18 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x18, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 19 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x19, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 1A none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x1A, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 1B none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x1B, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 1C none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x1C, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 1C F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x1C, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 1D none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x1D, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 1D F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x1D, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 1E none ok", 64, (const uint8_t[]){0x0F, 0x38, 0x1E, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 1E F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0x1E, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 1F none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x1F, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 20 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x20, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 20 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x20, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 21 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x21, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 21 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x21, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 22 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x22, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 22 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x22, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 23 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x23, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 23 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x23, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 24 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x24, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 24 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x24, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 25 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x25, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 25 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x25, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 26 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x26, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 27 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x27, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 28 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x28, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 28 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x28, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 29 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x29, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 29 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x29, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 2A none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x2A, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 2B 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x2B, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 2B none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x2B, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 2C none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x2C, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 2D none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x2D, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 2E none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x2E, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 2F none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x2F, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 30 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x30, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 30 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x30, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 31 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x31, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 31 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x31, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 32 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x32, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 32 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x32, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 33 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x33, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 33 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x33, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 34 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x34, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 34 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x34, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 35 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x35, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 35 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x35, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 36 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x36, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 37 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x37, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 37 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x37, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 38 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x38, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 38 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x38, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 39 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x39, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 39 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x39, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 3A 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x3A, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 3A none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x3A, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 3B 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x3B, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 3B none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x3B, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 3C 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x3C, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 3C none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x3C, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 3D 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x3D, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 3D none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x3D, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 3E 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x3E, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 3E none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x3E, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 3F 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x3F, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 3F none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x3F, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 40 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x40, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 40 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x40, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 41 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x41, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 41 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x41, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 42 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x42, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 43 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x43, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 44 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x44, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 45 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x45, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 46 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x46, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 47 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x47, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 49 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x49, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 4B none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x4B, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 4C none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x4C, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 4D none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x4D, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 4E none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x4E, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 4F none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x4F, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 50 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x50, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 51 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x51, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 52 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x52, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 53 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x53, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 54 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x54, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 55 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x55, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 58 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x58, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 59 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x59, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 5A none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x5A, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 5B none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x5B, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 5C none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x5C, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 5E none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x5E, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 62 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x62, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 63 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x63, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 64 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x64, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 65 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x65, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 66 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x66, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 68 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x68, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 6C none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x6C, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 70 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x70, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 71 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x71, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 72 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x72, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 73 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x73, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 75 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x75, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 76 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x76, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 77 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x77, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 78 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x78, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 79 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x79, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 7A none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x7A, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 7B none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x7B, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 7C none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x7C, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 7D none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x7D, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 7E none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x7E, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 7F none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x7F, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 80 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x80, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 80 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x80, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 81 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x81, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 81 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x81, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 82 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0x82, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 82 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x82, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 83 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x83, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 88 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x88, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 89 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x89, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 8A none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x8A, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 8B none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x8B, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 8C none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x8C, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 8D none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x8D, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 8E none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x8E, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 8F none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x8F, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 90 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x90, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 91 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x91, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 92 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x92, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 93 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x93, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 96 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x96, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 97 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x97, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 98 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x98, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 99 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x99, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 9A none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x9A, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 9B none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x9B, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 9C none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x9C, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 9D none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x9D, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 9E none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x9E, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 9F none bad", 64, (const uint8_t[]){0x0F, 0x38, 0x9F, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 A0 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xA0, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 A1 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xA1, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 A2 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xA2, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 A3 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xA3, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 A6 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xA6, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 A7 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xA7, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 A8 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xA8, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 A9 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xA9, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 AA none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xAA, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 AB none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xAB, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 AC none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xAC, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 AD none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xAD, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 AE none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xAE, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 AF none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xAF, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 B0 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xB0, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 B1 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xB1, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 B4 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xB4, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 B5 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xB5, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 B6 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xB6, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 B7 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xB7, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 B8 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xB8, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 B9 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xB9, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 BA none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xBA, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 BB none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xBB, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 BC none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xBC, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 BD none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xBD, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 BE none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xBE, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 BF none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xBF, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 C4 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xC4, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 C6 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xC6, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 C7 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xC7, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 C8 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0xC8, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 C8 66   bad", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xC8, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 C9 none ok", 64, (const uint8_t[]){0x0F, 0x38, 0xC9, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 C9 66   bad", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xC9, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 CA none ok", 64, (const uint8_t[]){0x0F, 0x38, 0xCA, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 CA 66   bad", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xCA, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 CB none ok", 64, (const uint8_t[]){0x0F, 0x38, 0xCB, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 CB 66   bad", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xCB, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 CC none ok", 64, (const uint8_t[]){0x0F, 0x38, 0xCC, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 CC 66   bad", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xCC, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 CD none ok", 64, (const uint8_t[]){0x0F, 0x38, 0xCD, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 CD 66   bad", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xCD, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 CF 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xCF, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 CF none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xCF, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 D2 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xD2, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 D3 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xD3, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 D8 F3   ok", 64, (const uint8_t[]){0xF3, 0x0F, 0x38, 0xD8, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 D8 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xD8, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 DA none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xDA, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 DB 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xDB, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 DB none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xDB, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 DC 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xDC, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 DC none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xDC, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 DD 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xDD, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 DD none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xDD, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 DE 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xDE, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 DE none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xDE, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 DF 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xDF, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 DF none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xDF, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 E0 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xE0, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 E1 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xE1, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 E2 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xE2, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 E3 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xE3, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 E4 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xE4, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 E5 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xE5, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 E6 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xE6, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 E7 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xE7, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 E8 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xE8, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 E9 none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xE9, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 EA none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xEA, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 EB none bad", 64, (const uint8_t[]){0x0F, 0x38, 0xEB, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 EC 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xEC, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 ED 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xED, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 EE 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xEE, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 EF 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xEF, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 F0 none bad", 64, (const uint8_t[]){0xF3, 0x0F, 0x38, 0xF0, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 F1 none bad", 64, (const uint8_t[]){0xF3, 0x0F, 0x38, 0xF1, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 F2 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xF2, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 F3 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xF3, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 F5 F2   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xF5, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 F5 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xF5, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 F6 66   ok", 64, (const uint8_t[]){0x0F, 0x38, 0xF6, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 F6 F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x38, 0xF6, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 F7 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xF7, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 F8 F2   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xF8, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 F8 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xF8, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 F9 66   ok", 64, (const uint8_t[]){0x0F, 0x38, 0xF9, 0x00}, 4, C_BAD, 0);
        expect_flag("0F 38 F9 F2   bad", 64, (const uint8_t[]){0x66, 0x0F, 0x38, 0xF9, 0x00}, 5, C_BAD, 1);
        expect_flag("0F 38 FA none ok", 64, (const uint8_t[]){0xF3, 0x0F, 0x38, 0xFA, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 FA 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xFA, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 FB none ok", 64, (const uint8_t[]){0xF3, 0x0F, 0x38, 0xFB, 0x00}, 5, C_BAD, 0);
        expect_flag("0F 38 FB 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xFB, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 38 FC 66   bad", 64, (const uint8_t[]){0x0F, 0x38, 0xFC, 0x00}, 4, C_BAD, 1);

        // 0F 3A (SSE4.1 and 4.2 rounds, blends and extracts, PCLMULQDQ,
        // the string compares, SHA1RNDS4, GFNI, AESKEYGENASSIST, HRESET).
        expect_flag("0F 3A 00 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x00, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 01 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x01, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 02 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x02, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 03 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x03, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 04 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x04, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 05 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x05, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 06 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x06, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 08 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x08, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 08 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x08, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 09 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x09, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 09 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x09, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 0A 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x0A, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 0A none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x0A, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 0B 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x0B, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 0B none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x0B, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 0C 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x0C, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 0C none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x0C, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 0D 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x0D, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 0D none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x0D, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 0E 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x0E, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 0E none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x0E, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 0F none ok", 64, (const uint8_t[]){0x0F, 0x3A, 0x0F, 0x00, 0x90}, 5, C_BAD, 0);
        expect_flag("0F 3A 0F F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x3A, 0x0F, 0x00, 0x90}, 6, C_BAD, 1);
        expect_flag("0F 3A 14 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x14, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 14 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x14, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 15 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x15, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 15 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x15, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 16 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x16, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 16 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x16, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 17 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x17, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 17 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x17, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 18 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x18, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 19 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x19, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 1A none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x1A, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 1B none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x1B, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 1D none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x1D, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 1E none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x1E, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 1F none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x1F, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 20 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x20, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 20 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x20, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 21 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x21, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 21 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x21, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 22 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x22, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 22 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x22, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 23 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x23, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 25 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x25, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 26 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x26, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 27 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x27, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 30 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x30, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 31 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x31, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 32 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x32, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 33 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x33, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 38 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x38, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 39 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x39, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 3A none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x3A, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 3B none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x3B, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 3E none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x3E, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 3F none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x3F, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 40 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x40, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 40 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x40, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 41 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x41, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 41 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x41, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 42 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x42, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 42 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x42, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 43 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x43, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 44 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x44, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 44 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x44, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 46 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x46, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 4A none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x4A, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 4B none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x4B, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 4C none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x4C, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 50 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x50, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 51 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x51, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 54 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x54, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 55 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x55, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 56 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x56, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 57 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x57, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 60 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x60, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 60 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x60, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 61 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x61, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 61 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x61, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 62 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x62, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 62 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x62, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 63 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0x63, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A 63 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x63, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 66 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x66, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 67 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x67, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 70 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x70, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 71 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x71, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 72 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x72, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A 73 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0x73, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A C2 none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0xC2, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A CC none ok", 64, (const uint8_t[]){0x0F, 0x3A, 0xCC, 0x00, 0x90}, 5, C_BAD, 0);
        expect_flag("0F 3A CC 66   bad", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0xCC, 0x00, 0x90}, 6, C_BAD, 1);
        expect_flag("0F 3A CE 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0xCE, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A CE none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0xCE, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A CF 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0xCF, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A CF none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0xCF, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A DE none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0xDE, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A DF 66   ok", 64, (const uint8_t[]){0x66, 0x0F, 0x3A, 0xDF, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A DF none bad", 64, (const uint8_t[]){0x0F, 0x3A, 0xDF, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 3A F0 none ok", 64, (const uint8_t[]){0xF3, 0x0F, 0x3A, 0xF0, 0x00, 0x90}, 6, C_BAD, 0);
        expect_flag("0F 3A F0 66   bad", 64, (const uint8_t[]){0x0F, 0x3A, 0xF0, 0x00, 0x90}, 5, C_BAD, 1);

        // The restricted 0F opcodes the block above does not already pin.
        expect_flag("0F 13 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x13, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 14 none ok", 64, (const uint8_t[]){0x0F, 0x14, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 14 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x14, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 15 none ok", 64, (const uint8_t[]){0x0F, 0x15, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 15 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x15, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 16 none ok", 64, (const uint8_t[]){0x0F, 0x16, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 16 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x16, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 17 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x17, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 28 none ok", 64, (const uint8_t[]){0x0F, 0x28, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 28 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x28, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 29 none ok", 64, (const uint8_t[]){0x0F, 0x29, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 29 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x29, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 2E none ok", 64, (const uint8_t[]){0x0F, 0x2E, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 2E F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x2E, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 2F none ok", 64, (const uint8_t[]){0x0F, 0x2F, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 2F F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x2F, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 50 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x50, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 53 none ok", 64, (const uint8_t[]){0x0F, 0x53, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 53 66   bad", 64, (const uint8_t[]){0x66, 0x0F, 0x53, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 54 none ok", 64, (const uint8_t[]){0x0F, 0x54, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 54 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x54, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 55 none ok", 64, (const uint8_t[]){0x0F, 0x55, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 55 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x55, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 56 none ok", 64, (const uint8_t[]){0x0F, 0x56, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 56 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x56, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 57 none ok", 64, (const uint8_t[]){0x0F, 0x57, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 57 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x57, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 60 none ok", 64, (const uint8_t[]){0x0F, 0x60, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 60 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x60, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 61 none ok", 64, (const uint8_t[]){0x0F, 0x61, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 61 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x61, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 62 none ok", 64, (const uint8_t[]){0x0F, 0x62, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 62 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x62, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 63 none ok", 64, (const uint8_t[]){0x0F, 0x63, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 63 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x63, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 64 none ok", 64, (const uint8_t[]){0x0F, 0x64, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 64 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x64, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 65 none ok", 64, (const uint8_t[]){0x0F, 0x65, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 65 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x65, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 66 none ok", 64, (const uint8_t[]){0x0F, 0x66, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 66 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x66, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 67 none ok", 64, (const uint8_t[]){0x0F, 0x67, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 67 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x67, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 68 none ok", 64, (const uint8_t[]){0x0F, 0x68, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 68 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x68, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 69 none ok", 64, (const uint8_t[]){0x0F, 0x69, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 69 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x69, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 6A none ok", 64, (const uint8_t[]){0x0F, 0x6A, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 6A F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x6A, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 6B none ok", 64, (const uint8_t[]){0x0F, 0x6B, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 6B F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x6B, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 6E none ok", 64, (const uint8_t[]){0x0F, 0x6E, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 6E F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x6E, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 72 none ok", 64, (const uint8_t[]){0x0F, 0x72, 0x00, 0x90}, 4, C_BAD, 0);
        expect_flag("0F 72 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x72, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 73 none ok", 64, (const uint8_t[]){0x0F, 0x73, 0x00, 0x90}, 4, C_BAD, 0);
        expect_flag("0F 73 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x73, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F 74 none ok", 64, (const uint8_t[]){0x0F, 0x74, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 74 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x74, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 75 none ok", 64, (const uint8_t[]){0x0F, 0x75, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 75 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x75, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 76 none ok", 64, (const uint8_t[]){0x0F, 0x76, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 76 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0x76, 0x00}, 4, C_BAD, 1);
        expect_flag("0F 77 none ok", 64, (const uint8_t[]){0x0F, 0x77}, 2, C_BAD, 0);
        expect_flag("0F 77 66   bad", 64, (const uint8_t[]){0x66, 0x0F, 0x77}, 3, C_BAD, 1);
        expect_flag("0F 79 none ok", 64, (const uint8_t[]){0x0F, 0x79, 0x00}, 3, C_BAD, 0);
        expect_flag("0F 79 F3   bad", 64, (const uint8_t[]){0xF3, 0x0F, 0x79, 0x00}, 4, C_BAD, 1);
        expect_flag("0F BC none ok", 64, (const uint8_t[]){0x0F, 0xBC, 0x00}, 3, C_BAD, 0);
        expect_flag("0F BC F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xBC, 0x00}, 4, C_BAD, 1);
        expect_flag("0F BD none ok", 64, (const uint8_t[]){0x0F, 0xBD, 0x00}, 3, C_BAD, 0);
        expect_flag("0F BD F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xBD, 0x00}, 4, C_BAD, 1);
        expect_flag("0F C3 none ok", 64, (const uint8_t[]){0x0F, 0xC3, 0x00}, 3, C_BAD, 0);
        expect_flag("0F C3 66   bad", 64, (const uint8_t[]){0x66, 0x0F, 0xC3, 0x00}, 4, C_BAD, 1);
        expect_flag("0F C4 none ok", 64, (const uint8_t[]){0x0F, 0xC4, 0x00, 0x90}, 4, C_BAD, 0);
        expect_flag("0F C4 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xC4, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F C5 none ok", 64, (const uint8_t[]){0x0F, 0xC5, 0x00, 0x90}, 4, C_BAD, 0);
        expect_flag("0F C5 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xC5, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F C6 none ok", 64, (const uint8_t[]){0x0F, 0xC6, 0x00, 0x90}, 4, C_BAD, 0);
        expect_flag("0F C6 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xC6, 0x00, 0x90}, 5, C_BAD, 1);
        expect_flag("0F D1 none ok", 64, (const uint8_t[]){0x0F, 0xD1, 0x00}, 3, C_BAD, 0);
        expect_flag("0F D1 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xD1, 0x00}, 4, C_BAD, 1);
        expect_flag("0F D2 none ok", 64, (const uint8_t[]){0x0F, 0xD2, 0x00}, 3, C_BAD, 0);
        expect_flag("0F D2 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xD2, 0x00}, 4, C_BAD, 1);
        expect_flag("0F D3 none ok", 64, (const uint8_t[]){0x0F, 0xD3, 0x00}, 3, C_BAD, 0);
        expect_flag("0F D3 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xD3, 0x00}, 4, C_BAD, 1);
        expect_flag("0F D4 none ok", 64, (const uint8_t[]){0x0F, 0xD4, 0x00}, 3, C_BAD, 0);
        expect_flag("0F D4 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xD4, 0x00}, 4, C_BAD, 1);
        expect_flag("0F D5 none ok", 64, (const uint8_t[]){0x0F, 0xD5, 0x00}, 3, C_BAD, 0);
        expect_flag("0F D5 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xD5, 0x00}, 4, C_BAD, 1);
        expect_flag("0F D8 none ok", 64, (const uint8_t[]){0x0F, 0xD8, 0x00}, 3, C_BAD, 0);
        expect_flag("0F D8 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xD8, 0x00}, 4, C_BAD, 1);
        expect_flag("0F D9 none ok", 64, (const uint8_t[]){0x0F, 0xD9, 0x00}, 3, C_BAD, 0);
        expect_flag("0F D9 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xD9, 0x00}, 4, C_BAD, 1);
        expect_flag("0F DA none ok", 64, (const uint8_t[]){0x0F, 0xDA, 0x00}, 3, C_BAD, 0);
        expect_flag("0F DA F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xDA, 0x00}, 4, C_BAD, 1);
        expect_flag("0F DB none ok", 64, (const uint8_t[]){0x0F, 0xDB, 0x00}, 3, C_BAD, 0);
        expect_flag("0F DB F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xDB, 0x00}, 4, C_BAD, 1);
        expect_flag("0F DC none ok", 64, (const uint8_t[]){0x0F, 0xDC, 0x00}, 3, C_BAD, 0);
        expect_flag("0F DC F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xDC, 0x00}, 4, C_BAD, 1);
        expect_flag("0F DD none ok", 64, (const uint8_t[]){0x0F, 0xDD, 0x00}, 3, C_BAD, 0);
        expect_flag("0F DD F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xDD, 0x00}, 4, C_BAD, 1);
        expect_flag("0F DE none ok", 64, (const uint8_t[]){0x0F, 0xDE, 0x00}, 3, C_BAD, 0);
        expect_flag("0F DE F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xDE, 0x00}, 4, C_BAD, 1);
        expect_flag("0F DF none ok", 64, (const uint8_t[]){0x0F, 0xDF, 0x00}, 3, C_BAD, 0);
        expect_flag("0F DF F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xDF, 0x00}, 4, C_BAD, 1);
        expect_flag("0F E0 none ok", 64, (const uint8_t[]){0x0F, 0xE0, 0x00}, 3, C_BAD, 0);
        expect_flag("0F E0 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xE0, 0x00}, 4, C_BAD, 1);
        expect_flag("0F E1 none ok", 64, (const uint8_t[]){0x0F, 0xE1, 0x00}, 3, C_BAD, 0);
        expect_flag("0F E1 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xE1, 0x00}, 4, C_BAD, 1);
        expect_flag("0F E2 none ok", 64, (const uint8_t[]){0x0F, 0xE2, 0x00}, 3, C_BAD, 0);
        expect_flag("0F E2 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xE2, 0x00}, 4, C_BAD, 1);
        expect_flag("0F E3 none ok", 64, (const uint8_t[]){0x0F, 0xE3, 0x00}, 3, C_BAD, 0);
        expect_flag("0F E3 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xE3, 0x00}, 4, C_BAD, 1);
        expect_flag("0F E4 none ok", 64, (const uint8_t[]){0x0F, 0xE4, 0x00}, 3, C_BAD, 0);
        expect_flag("0F E4 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xE4, 0x00}, 4, C_BAD, 1);
        expect_flag("0F E5 none ok", 64, (const uint8_t[]){0x0F, 0xE5, 0x00}, 3, C_BAD, 0);
        expect_flag("0F E5 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xE5, 0x00}, 4, C_BAD, 1);
        expect_flag("0F E7 none ok", 64, (const uint8_t[]){0x0F, 0xE7, 0x00}, 3, C_BAD, 0);
        expect_flag("0F E7 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xE7, 0x00}, 4, C_BAD, 1);
        expect_flag("0F E8 none ok", 64, (const uint8_t[]){0x0F, 0xE8, 0x00}, 3, C_BAD, 0);
        expect_flag("0F E8 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xE8, 0x00}, 4, C_BAD, 1);
        expect_flag("0F E9 none ok", 64, (const uint8_t[]){0x0F, 0xE9, 0x00}, 3, C_BAD, 0);
        expect_flag("0F E9 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xE9, 0x00}, 4, C_BAD, 1);
        expect_flag("0F EA none ok", 64, (const uint8_t[]){0x0F, 0xEA, 0x00}, 3, C_BAD, 0);
        expect_flag("0F EA F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xEA, 0x00}, 4, C_BAD, 1);
        expect_flag("0F EB none ok", 64, (const uint8_t[]){0x0F, 0xEB, 0x00}, 3, C_BAD, 0);
        expect_flag("0F EB F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xEB, 0x00}, 4, C_BAD, 1);
        expect_flag("0F EC none ok", 64, (const uint8_t[]){0x0F, 0xEC, 0x00}, 3, C_BAD, 0);
        expect_flag("0F EC F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xEC, 0x00}, 4, C_BAD, 1);
        expect_flag("0F ED none ok", 64, (const uint8_t[]){0x0F, 0xED, 0x00}, 3, C_BAD, 0);
        expect_flag("0F ED F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xED, 0x00}, 4, C_BAD, 1);
        expect_flag("0F EE none ok", 64, (const uint8_t[]){0x0F, 0xEE, 0x00}, 3, C_BAD, 0);
        expect_flag("0F EE F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xEE, 0x00}, 4, C_BAD, 1);
        expect_flag("0F EF none ok", 64, (const uint8_t[]){0x0F, 0xEF, 0x00}, 3, C_BAD, 0);
        expect_flag("0F EF F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xEF, 0x00}, 4, C_BAD, 1);
        expect_flag("0F F1 none ok", 64, (const uint8_t[]){0x0F, 0xF1, 0x00}, 3, C_BAD, 0);
        expect_flag("0F F1 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xF1, 0x00}, 4, C_BAD, 1);
        expect_flag("0F F2 none ok", 64, (const uint8_t[]){0x0F, 0xF2, 0x00}, 3, C_BAD, 0);
        expect_flag("0F F2 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xF2, 0x00}, 4, C_BAD, 1);
        expect_flag("0F F3 none ok", 64, (const uint8_t[]){0x0F, 0xF3, 0x00}, 3, C_BAD, 0);
        expect_flag("0F F3 F2   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xF3, 0x00}, 4, C_BAD, 1);
        expect_flag("0F F4 none ok", 64, (const uint8_t[]){0x0F, 0xF4, 0x00}, 3, C_BAD, 0);
        expect_flag("0F F4 F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xF4, 0x00}, 4, C_BAD, 1);
        expect_flag("0F F5 none ok", 64, (const uint8_t[]){0x0F, 0xF5, 0x00}, 3, C_BAD, 0);
        expect_flag("0F F5 F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xF5, 0x00}, 4, C_BAD, 1);
        expect_flag("0F F6 66   ok", 64, (const uint8_t[]){0x0F, 0xF6, 0x00}, 3, C_BAD, 0);
        expect_flag("0F F6 F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xF6, 0x00}, 4, C_BAD, 1);
        expect_flag("0F F7 F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xF7, 0x00}, 4, C_BAD, 1);
        expect_flag("0F F8 66   ok", 64, (const uint8_t[]){0x0F, 0xF8, 0x00}, 3, C_BAD, 0);
        expect_flag("0F F8 F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xF8, 0x00}, 4, C_BAD, 1);
        expect_flag("0F F9 66   ok", 64, (const uint8_t[]){0x0F, 0xF9, 0x00}, 3, C_BAD, 0);
        expect_flag("0F F9 F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xF9, 0x00}, 4, C_BAD, 1);
        expect_flag("0F FA 66   ok", 64, (const uint8_t[]){0x0F, 0xFA, 0x00}, 3, C_BAD, 0);
        expect_flag("0F FA F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xFA, 0x00}, 4, C_BAD, 1);
        expect_flag("0F FB 66   ok", 64, (const uint8_t[]){0x0F, 0xFB, 0x00}, 3, C_BAD, 0);
        expect_flag("0F FB F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xFB, 0x00}, 4, C_BAD, 1);
        expect_flag("0F FC 66   ok", 64, (const uint8_t[]){0x0F, 0xFC, 0x00}, 3, C_BAD, 0);
        expect_flag("0F FC F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xFC, 0x00}, 4, C_BAD, 1);
        expect_flag("0F FD 66   ok", 64, (const uint8_t[]){0x0F, 0xFD, 0x00}, 3, C_BAD, 0);
        expect_flag("0F FD F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xFD, 0x00}, 4, C_BAD, 1);
        expect_flag("0F FE 66   ok", 64, (const uint8_t[]){0x0F, 0xFE, 0x00}, 3, C_BAD, 0);
        expect_flag("0F FE F3   bad", 64, (const uint8_t[]){0xF2, 0x0F, 0xFE, 0x00}, 4, C_BAD, 1);
    }

    // System groups: CR/DR moves, RDRAND/RDSEED, fences, FS/GS base, save/restore, prefetch.
    {
        // 0F 20-23: the reg field names the CR/DR operand, r/m names the GPR.
        // 0F 20/21 move CR/DR into r/m, 0F 22/23 move r/m into CR/DR.
        static const uint8_t mov_eax_cr0[] = { 0x0F, 0x20, 0xC0 };
        static const uint8_t mov_eax_dr0[] = { 0x0F, 0x21, 0xC0 };
        static const uint8_t mov_cr0_eax[] = { 0x0F, 0x22, 0xC0 };
        static const uint8_t mov_dr0_eax[] = { 0x0F, 0x23, 0xC0 };
        static const uint8_t mov_rax_cr0[] = { 0x48, 0x0F, 0x20, 0xC0 };
        expect_set("mov eax,cr0 (no src RAX)", 64, mov_eax_cr0, 3, 0, XSET_RAX, 0);
        expect_set("mov eax,cr0 src other", 64, mov_eax_cr0, 3, 0, XSET_OTHER, 1);
        expect_set("mov eax,cr0 dst RAX", 64, mov_eax_cr0, 3, 1, XSET_RAX, 1);
        expect_set("mov eax,dr0 (no src RAX)", 64, mov_eax_dr0, 3, 0, XSET_RAX, 0);
        expect_set("mov eax,dr0 src other", 64, mov_eax_dr0, 3, 0, XSET_OTHER, 1);
        expect_set("mov eax,dr0 dst RAX", 64, mov_eax_dr0, 3, 1, XSET_RAX, 1);
        expect_set("mov cr0,eax src RAX", 64, mov_cr0_eax, 3, 0, XSET_RAX, 1);
        expect_set("mov cr0,eax dst other", 64, mov_cr0_eax, 3, 1, XSET_OTHER, 1);
        expect_set("mov cr0,eax (no dst RAX)", 64, mov_cr0_eax, 3, 1, XSET_RAX, 0);
        expect_set("mov dr0,eax src RAX", 64, mov_dr0_eax, 3, 0, XSET_RAX, 1);
        expect_set("mov dr0,eax dst other", 64, mov_dr0_eax, 3, 1, XSET_OTHER, 1);
        expect_set("mov rax,cr0 dst RAX", 64, mov_rax_cr0, 4, 1, XSET_RAX, 1);
    }
    {
        // 0F C7 /6 /7 mod=3: RDRAND/RDSEED, r/m is a plain GPR destination
        // and there is no source operand. Both report success in CF, which is
        // the only flag they touch (the others are cleared).
        static const uint8_t rdrand[] = { 0x0F, 0xC7, 0xF0 };
        static const uint8_t rdseed[] = { 0x0F, 0xC7, 0xF8 };
        static const uint8_t rdrand64[] = { 0x48, 0x0F, 0xC7, 0xF0 };
        expect_set("rdrand eax dst EAX", 64, rdrand, 3, 1, XSET_EAX, 1);
        expect_set("rdrand (no src other)", 64, rdrand, 3, 0, XSET_OTHER, 0);
        expect_set("rdrand (no dst other)", 64, rdrand, 3, 1, XSET_OTHER, 0);
        expect_set("rdseed eax dst EAX", 64, rdseed, 3, 1, XSET_EAX, 1);
        expect_set("rdseed (no src other)", 64, rdseed, 3, 0, XSET_OTHER, 0);
        expect_set("rdseed (no dst other)", 64, rdseed, 3, 1, XSET_OTHER, 0);
        expect_set("rdrand rax dst RAX", 64, rdrand64, 4, 1, XSET_RAX, 1);
        expect_set("rdrand rax (no src other)", 64, rdrand64, 4, 0, XSET_OTHER, 0);
        expect_set("rdrand eax dst FL", 64, rdrand, 3, 1, XSET_FL, 1);
        expect_set("rdseed eax dst FL", 64, rdseed, 3, 1, XSET_FL, 1);
        expect_set("rdrand rax dst FL", 64, rdrand64, 4, 1, XSET_FL, 1);
        expect_set("rdrand (no src FL)", 64, rdrand, 3, 0, XSET_FL, 0);
    }
    {
        // 0F AE mod=3: /5 /6 /7 are LFENCE/MFENCE/SFENCE and take no operand
        // at all; F3 0F AE /0-/3 is the FS/GS base group, where r/m is a GPR.
        static const uint8_t lfence[] = { 0x0F, 0xAE, 0xE8 };
        static const uint8_t mfence[] = { 0x0F, 0xAE, 0xF0 };
        static const uint8_t sfence[] = { 0x0F, 0xAE, 0xF8 };
        static const uint8_t rdfsbase[] = { 0xF3, 0x0F, 0xAE, 0xC0 };
        static const uint8_t rdgsbase[] = { 0xF3, 0x0F, 0xAE, 0xC8 };
        static const uint8_t wrfsbase[] = { 0xF3, 0x0F, 0xAE, 0xD0 };
        static const uint8_t wrgsbase[] = { 0xF3, 0x0F, 0xAE, 0xD8 };
        expect_set("lfence (no src other)", 64, lfence, 3, 0, XSET_OTHER, 0);
        expect_set("lfence (no dst other)", 64, lfence, 3, 1, XSET_OTHER, 0);
        expect_set("mfence (no src other)", 64, mfence, 3, 0, XSET_OTHER, 0);
        expect_set("mfence (no dst other)", 64, mfence, 3, 1, XSET_OTHER, 0);
        expect_set("sfence (no src other)", 64, sfence, 3, 0, XSET_OTHER, 0);
        expect_set("sfence (no dst other)", 64, sfence, 3, 1, XSET_OTHER, 0);
        // SFENCE is encoded by any opcode of the form 0F AE Fx, so the prefix
        // slot is free: F3 0F AE F8 is still the operand-less store fence. The
        // same prefix on /5 selects INCSSPD, which does have an operand.
        static const uint8_t sfence_f3[] = { 0xF3, 0x0F, 0xAE, 0xF8 };
        expect_set("sfence (F3) no src other", 64, sfence_f3, 4, 0, XSET_OTHER, 0);
        expect_set("sfence (F3) no dst other", 64, sfence_f3, 4, 1, XSET_OTHER, 0);
        expect_flag("sfence (F3) not bad", 64, sfence_f3, 4, C_BAD, 0);
        expect_set("rdfsbase dst RAX", 64, rdfsbase, 4, 1, XSET_RAX, 1);
        expect_set("rdfsbase (no src other)", 64, rdfsbase, 4, 0, XSET_OTHER, 0);
        expect_set("rdgsbase dst RAX", 64, rdgsbase, 4, 1, XSET_RAX, 1);
        expect_set("rdgsbase (no src other)", 64, rdgsbase, 4, 0, XSET_OTHER, 0);
        expect_set("wrfsbase src RAX", 64, wrfsbase, 4, 0, XSET_RAX, 1);
        expect_set("wrfsbase (no src other)", 64, wrfsbase, 4, 0, XSET_OTHER, 0);
        expect_set("wrfsbase dst other", 64, wrfsbase, 4, 1, XSET_OTHER, 1);
        expect_set("wrgsbase src RAX", 64, wrgsbase, 4, 0, XSET_RAX, 1);
        expect_set("wrgsbase (no src other)", 64, wrgsbase, 4, 0, XSET_OTHER, 0);
        expect_set("wrgsbase dst other", 64, wrgsbase, 4, 1, XSET_OTHER, 1);
        // 0F AE keeps the memory form's encoding at mod=3, where only /5 /6 /7
        // (and the F3-prefixed /0-/3 above) exist; the other sub-forms are
        // invalid encodings and must be flagged, but /5-/7, the FS/GS moves,
        // the CET INCSSP form and the memory forms must not be.
        static const uint8_t ae_bad0[] = { 0x0F, 0xAE, 0xC0 };
        static const uint8_t ae_bad1[] = { 0x0F, 0xAE, 0xC8 };
        static const uint8_t ae_bad2[] = { 0x0F, 0xAE, 0xD0 };
        static const uint8_t ae_bad3[] = { 0x0F, 0xAE, 0xD8 };
        static const uint8_t ae_bad4[] = { 0x0F, 0xAE, 0xE0 };
        static const uint8_t incsspd[] = { 0xF3, 0x0F, 0xAE, 0xE8 };
        static const uint8_t fxsave_m[] = { 0x0F, 0xAE, 0x00 };
        expect_flag("0F AE /0 m3 bad", 64, ae_bad0, 3, C_BAD, 1);
        expect_flag("0F AE /1 m3 bad", 64, ae_bad1, 3, C_BAD, 1);
        expect_flag("0F AE /2 m3 bad", 64, ae_bad2, 3, C_BAD, 1);
        expect_flag("0F AE /3 m3 bad", 64, ae_bad3, 3, C_BAD, 1);
        expect_flag("0F AE /4 m3 bad", 64, ae_bad4, 3, C_BAD, 1);
        expect_flag("lfence not bad", 64, lfence, 3, C_BAD, 0);
        expect_flag("mfence not bad", 64, mfence, 3, C_BAD, 0);
        expect_flag("sfence not bad", 64, sfence, 3, C_BAD, 0);
        expect_flag("rdfsbase not bad", 64, rdfsbase, 4, C_BAD, 0);
        expect_flag("rdgsbase not bad", 64, rdgsbase, 4, C_BAD, 0);
        expect_flag("wrfsbase not bad", 64, wrfsbase, 4, C_BAD, 0);
        expect_flag("wrgsbase not bad", 64, wrgsbase, 4, C_BAD, 0);
        expect_flag("incsspd not bad", 64, incsspd, 4, C_BAD, 0);
        expect_flag("fxsave mem not bad", 64, fxsave_m, 3, C_BAD, 0);
    }
    {
        // F3 0F AE /4 is PTWRITE: the r/m GPR (mod=3) or the memory operand is
        // read and encoded into a processor trace packet, so the instruction
        // has a source but no destination, and no register operand other than
        // the source one.
        static const uint8_t ptwrite32[] = { 0xF3, 0x0F, 0xAE, 0xE0 };
        static const uint8_t ptwrite64[] = { 0xF3, 0x48, 0x0F, 0xAE, 0xE0 };
        static const uint8_t ptwrite_m[] = { 0xF3, 0x0F, 0xAE, 0x20 };
        static const uint8_t ptwrite_m1[] = { 0xF3, 0x0F, 0xAE, 0x21 };
        expect_set("ptwrite src RAX", 64, ptwrite32, 4, 0, XSET_RAX, 1);
        expect_set("ptwrite (no src other)", 64, ptwrite32, 4, 0, XSET_OTHER, 0);
        expect_set("ptwrite (no dst RAX)", 64, ptwrite32, 4, 1, XSET_RAX, 0);
        expect_set("ptwrite (no dst other)", 64, ptwrite32, 4, 1, XSET_OTHER, 0);
        expect_set("ptwrite r64 src RAX", 64, ptwrite64, 5, 0, XSET_RAX, 1);
        expect_set("ptwrite r64 (no src other)", 64, ptwrite64, 5, 0, XSET_OTHER, 0);
        expect_set("ptwrite r64 (no dst other)", 64, ptwrite64, 5, 1, XSET_OTHER, 0);
        expect_set("ptwrite m src M", 64, ptwrite_m, 4, 0, XSET_MEM, 1);
        expect_set("ptwrite m (no dst M)", 64, ptwrite_m, 4, 1, XSET_MEM, 0);
        // The m form reads memory only, so the XSAVE mask of the same reg
        // field must not leak in: with the base register off RAX, neither
        // EDX:EAX half appears.
        expect_set("ptwrite m (no src EAX)", 64, ptwrite_m1, 4, 0, XSET_EAX, 0);
        expect_set("ptwrite m (no src EDX)", 64, ptwrite_m1, 4, 0, XSET_EDX, 0);
        expect_flag("ptwrite not bad", 64, ptwrite32, 4, C_BAD, 0);
    }
    {
        // WAITPKG: F3 0F AE /6 is UMONITOR, whose r/m GPR holds the address to
        // monitor, F2 0F AE /6 is UMWAIT and 66 0F AE /6 is TPAUSE, whose r/m
        // GPR holds the optimized-state hint. All three need mod=11, and all
        // three are read-only: the destination is the monitor hardware or the
        // wake-up state. UMWAIT and TPAUSE also read the EDX:EAX deadline and
        // report the wake-up cause in CF, clearing the other arithmetic flags.
        static const uint8_t umonitor[] = { 0xF3, 0x0F, 0xAE, 0xF0 };
        static const uint8_t umonitor_c[] = { 0xF3, 0x0F, 0xAE, 0xF1 };
        static const uint8_t umwait[] = { 0xF2, 0x0F, 0xAE, 0xF0 };
        static const uint8_t umwait_c[] = { 0xF2, 0x0F, 0xAE, 0xF1 };
        static const uint8_t tpause[] = { 0x66, 0x0F, 0xAE, 0xF0 };
        static const uint8_t tpause_c[] = { 0x66, 0x0F, 0xAE, 0xF1 };
        expect_set("umonitor src RAX", 64, umonitor, 4, 0, XSET_RAX, 1);
        expect_set("umonitor (no src other)", 64, umonitor, 4, 0, XSET_OTHER, 0);
        expect_set("umonitor (no src EDX)", 64, umonitor, 4, 0, XSET_EDX, 0);
        expect_set("umonitor (no dst RAX)", 64, umonitor, 4, 1, XSET_RAX, 0);
        expect_set("umonitor (no dst other)", 64, umonitor, 4, 1, XSET_OTHER, 0);
        expect_set("umonitor ecx src RCX", 64, umonitor_c, 4, 0, XSET_RCX, 1);
        expect_set("umwait src EAX", 64, umwait, 4, 0, XSET_EAX, 1);
        expect_set("umwait src EDX", 64, umwait, 4, 0, XSET_EDX, 1);
        expect_set("umwait (no src ECX)", 64, umwait, 4, 0, XSET_ECX, 0);
        expect_set("umwait (no src other)", 64, umwait, 4, 0, XSET_OTHER, 0);
        expect_set("umwait (no dst other)", 64, umwait, 4, 1, XSET_OTHER, 0);
        expect_set("umwait dst FL", 64, umwait, 4, 1, XSET_FL, 1);
        expect_set("umwait ecx src RCX", 64, umwait_c, 4, 0, XSET_RCX, 1);
        expect_set("tpause src EAX", 64, tpause, 4, 0, XSET_EAX, 1);
        expect_set("tpause src EDX", 64, tpause, 4, 0, XSET_EDX, 1);
        expect_set("tpause (no src ECX)", 64, tpause, 4, 0, XSET_ECX, 0);
        expect_set("tpause (no src other)", 64, tpause, 4, 0, XSET_OTHER, 0);
        expect_set("tpause (no dst other)", 64, tpause, 4, 1, XSET_OTHER, 0);
        expect_set("tpause dst FL", 64, tpause, 4, 1, XSET_FL, 1);
        expect_set("tpause ecx src RCX", 64, tpause_c, 4, 0, XSET_RCX, 1);
        expect_flag("umonitor not bad", 64, umonitor, 4, C_BAD, 0);
        expect_flag("umwait not bad", 64, umwait, 4, C_BAD, 0);
        expect_flag("tpause not bad", 64, tpause, 4, C_BAD, 0);
    }
    {
        // F3 0F AE /6 with a memory operand is not UMONITOR -- that form needs
        // mod=11 -- but the CET CLRSSBSY, which clears the busy flag of a
        // supervisor shadow stack token in m64. The token is read and written,
        // CF reports an invalid token and the other arithmetic flags are
        // cleared, and no EDX:EAX state-component mask is involved: that mask
        // belongs to the neighbouring 0F AE /4-/6 XSAVE forms.
        static const uint8_t clrssbsy[] = { 0xF3, 0x0F, 0xAE, 0x31 };
        expect_set("clrssbsy src M", 64, clrssbsy, 4, 0, XSET_MEM, 1);
        expect_set("clrssbsy dst M", 64, clrssbsy, 4, 1, XSET_MEM, 1);
        expect_set("clrssbsy (no src EAX)", 64, clrssbsy, 4, 0, XSET_EAX, 0);
        expect_set("clrssbsy (no src EDX)", 64, clrssbsy, 4, 0, XSET_EDX, 0);
        expect_set("clrssbsy dst FL", 64, clrssbsy, 4, 1, XSET_FL, 1);
        expect_flag("clrssbsy not bad", 64, clrssbsy, 4, C_BAD, 0);
    }
    {
        // F3 0F AE /5 is the CET INCSSPD/INCSSPQ group. It shares mod=3 /5
        // with LFENCE but does touch operands: the r/m GPR is added to the
        // shadow stack pointer, which folds into OTHER.
        static const uint8_t incsspd[] = { 0xF3, 0x0F, 0xAE, 0xE8 };
        static const uint8_t incsspq[] = { 0xF3, 0x48, 0x0F, 0xAE, 0xE8 };
        expect_set("incsspd src RAX", 64, incsspd, 4, 0, XSET_RAX, 1);
        expect_set("incsspd (no src other)", 64, incsspd, 4, 0, XSET_OTHER, 0);
        expect_set("incsspd dst other", 64, incsspd, 4, 1, XSET_OTHER, 1);
        expect_set("incsspd (no dst RAX)", 64, incsspd, 4, 1, XSET_RAX, 0);
        expect_set("incsspq src RAX", 64, incsspq, 5, 0, XSET_RAX, 1);
        expect_set("incsspq dst other", 64, incsspq, 5, 1, XSET_OTHER, 1);
        expect_set("incsspq (no dst RAX)", 64, incsspq, 5, 1, XSET_RAX, 0);
    }
    {
        // Memory forms that store into their r/m operand, plus the read-only
        // siblings that must stay source-only.
        static const uint8_t fxsave[] = { 0x0F, 0xAE, 0x00 };
        static const uint8_t stmxcsr[] = { 0x0F, 0xAE, 0x18 };
        static const uint8_t xsave[] = { 0x0F, 0xAE, 0x20 };
        static const uint8_t xsaveopt[] = { 0x0F, 0xAE, 0x30 };
        static const uint8_t clwb[] = { 0x66, 0x0F, 0xAE, 0x30 };
        static const uint8_t cmpxchg8b[] = { 0x0F, 0xC7, 0x08 };
        static const uint8_t cmpxchg16b[] = { 0x48, 0x0F, 0xC7, 0x08 };
        static const uint8_t xrstors[] = { 0x0F, 0xC7, 0x18 };
        static const uint8_t xsavec[] = { 0x0F, 0xC7, 0x20 };
        static const uint8_t xsaves[] = { 0x0F, 0xC7, 0x28 };
        static const uint8_t vmptrst[] = { 0x0F, 0xC7, 0x38 };
        expect_set("fxsave dst M", 64, fxsave, 3, 1, XSET_MEM, 1);
        expect_set("fxsave src M", 64, fxsave, 3, 0, XSET_MEM, 1);
        expect_set("stmxcsr dst M", 64, stmxcsr, 3, 1, XSET_MEM, 1);
        expect_set("xsave dst M", 64, xsave, 3, 1, XSET_MEM, 1);
        expect_set("xsaveopt dst M", 64, xsaveopt, 3, 1, XSET_MEM, 1);
        expect_set("clwb dst M", 64, clwb, 4, 1, XSET_MEM, 1);
        expect_set("cmpxchg8b dst M", 64, cmpxchg8b, 3, 1, XSET_MEM, 1);
        expect_set("cmpxchg8b src M", 64, cmpxchg8b, 3, 0, XSET_MEM, 1);
        expect_set("cmpxchg16b dst M", 64, cmpxchg16b, 4, 1, XSET_MEM, 1);
        expect_set("xrstors dst M", 64, xrstors, 3, 1, XSET_MEM, 1);
        expect_set("xsavec dst M", 64, xsavec, 3, 1, XSET_MEM, 1);
        expect_set("xsaves dst M", 64, xsaves, 3, 1, XSET_MEM, 1);
        expect_set("vmptrst dst M", 64, vmptrst, 3, 1, XSET_MEM, 1);
        expect_set("vmptrst src M", 64, vmptrst, 3, 0, XSET_MEM, 1);
        expect_set("vmptrst dst other", 64, vmptrst, 3, 1, XSET_OTHER, 1);
        {
            static const uint8_t fxrstor[] = { 0x0F, 0xAE, 0x08 };
            static const uint8_t ldmxcsr[] = { 0x0F, 0xAE, 0x10 };
            static const uint8_t xrstor[] = { 0x0F, 0xAE, 0x28 };
            static const uint8_t vmptrld[] = { 0x0F, 0xC7, 0x30 };
            expect_set("fxrstor (no dst M)", 64, fxrstor, 3, 1, XSET_MEM, 0);
            expect_set("ldmxcsr (no dst M)", 64, ldmxcsr, 3, 1, XSET_MEM, 0);
            expect_set("xrstor (no dst M)", 64, xrstor, 3, 1, XSET_MEM, 0);
            expect_set("vmptrld src M", 64, vmptrld, 3, 0, XSET_MEM, 1);
            expect_set("vmptrld (no dst M)", 64, vmptrld, 3, 1, XSET_MEM, 0);
        }
    }
    {
        // CMPXCHG8B (0F C7 /1) compares and conditionally loads the EDX:EAX
        // pair, and REX.W promotes it to CMPXCHG16B over RDX:RAX. The pair is
        // therefore both read and written, and ZF reports the comparison.
        // CMPXCHG8B keeps its 32-bit halves even in 64-bit mode, so the
        // 64-bit-width bits of RDX:RAX stay clear on that form.
        static const uint8_t cmpxchg8b_c[] = { 0x0F, 0xC7, 0x09 };
        static const uint8_t cmpxchg16b_c[] = { 0x48, 0x0F, 0xC7, 0x09 };
        expect_set("cmpxchg8b src EAX", 64, cmpxchg8b_c, 3, 0, XSET_EAX, 1);
        expect_set("cmpxchg8b src EDX", 64, cmpxchg8b_c, 3, 0, XSET_EDX, 1);
        expect_set("cmpxchg8b src (32-bit pair)", 64, cmpxchg8b_c, 3, 0,
                   XSET_RDX & ~XSET_EDX, 0);
        expect_set("cmpxchg8b dst EAX", 64, cmpxchg8b_c, 3, 1, XSET_EAX, 1);
        expect_set("cmpxchg8b dst EDX", 64, cmpxchg8b_c, 3, 1, XSET_EDX, 1);
        expect_set("cmpxchg8b dst FL", 64, cmpxchg8b_c, 3, 1, XSET_FL, 1);
        expect_set("cmpxchg8b (32) src EDX", 32, cmpxchg8b_c, 3, 0, XSET_EDX, 1);
        expect_set("cmpxchg16b src RAX", 64, cmpxchg16b_c, 4, 0, XSET_RAX, 1);
        expect_set("cmpxchg16b src RDX", 64, cmpxchg16b_c, 4, 0, XSET_RDX, 1);
        expect_set("cmpxchg16b dst RAX", 64, cmpxchg16b_c, 4, 1, XSET_RAX, 1);
        expect_set("cmpxchg16b dst RDX", 64, cmpxchg16b_c, 4, 1, XSET_RDX, 1);
        expect_set("cmpxchg16b dst FL", 64, cmpxchg16b_c, 4, 1, XSET_FL, 1);
    }
    {
        // XSAVE/XSAVEOPT/XSAVEC/XSAVES and XRSTOR/XRSTORS take the
        // state-component mask in EDX:EAX, an input shared by all six. The
        // other memory forms of these groups (FXSAVE/FXRSTOR, LDMXCSR/
        // STMXCSR, CLFLUSH/CLWB and the VMCS-pointer forms) take no mask.
        static const uint8_t xsave_c[] = { 0x0F, 0xAE, 0x21 };
        static const uint8_t xsaveopt_c[] = { 0x0F, 0xAE, 0x31 };
        static const uint8_t xrstor_c[] = { 0x0F, 0xAE, 0x29 };
        static const uint8_t xsavec_c[] = { 0x0F, 0xC7, 0x21 };
        static const uint8_t xsaves_c[] = { 0x0F, 0xC7, 0x29 };
        static const uint8_t xrstors_c[] = { 0x0F, 0xC7, 0x19 };
        static const uint8_t fxrstor_c[] = { 0x0F, 0xAE, 0x09 };
        static const uint8_t stmxcsr_c[] = { 0x0F, 0xAE, 0x19 };
        static const uint8_t clwb_c[] = { 0x66, 0x0F, 0xAE, 0x31 };
        static const uint8_t vmptrld_c[] = { 0x0F, 0xC7, 0x31 };
        expect_set("xsave src EAX", 64, xsave_c, 3, 0, XSET_EAX, 1);
        expect_set("xsave src EDX", 64, xsave_c, 3, 0, XSET_EDX, 1);
        expect_set("xsave (no dst EDX)", 64, xsave_c, 3, 1, XSET_EDX, 0);
        expect_set("xsaveopt src EDX", 64, xsaveopt_c, 3, 0, XSET_EDX, 1);
        expect_set("xrstor src EAX", 64, xrstor_c, 3, 0, XSET_EAX, 1);
        expect_set("xrstor src EDX", 64, xrstor_c, 3, 0, XSET_EDX, 1);
        expect_set("xsavec src EDX", 64, xsavec_c, 3, 0, XSET_EDX, 1);
        expect_set("xsaves src EAX", 64, xsaves_c, 3, 0, XSET_EAX, 1);
        expect_set("xsaves src EDX", 64, xsaves_c, 3, 0, XSET_EDX, 1);
        expect_set("xrstors src EAX", 64, xrstors_c, 3, 0, XSET_EAX, 1);
        expect_set("xrstors src EDX", 64, xrstors_c, 3, 0, XSET_EDX, 1);
        expect_set("fxrstor (no src EDX)", 64, fxrstor_c, 3, 0, XSET_EDX, 0);
        expect_set("stmxcsr (no src EDX)", 64, stmxcsr_c, 3, 0, XSET_EDX, 0);
        expect_set("clwb (no src EDX)", 64, clwb_c, 4, 0, XSET_EDX, 0);
        expect_set("vmptrld (no src EDX)", 64, vmptrld_c, 3, 0, XSET_EDX, 0);
    }
    {
        // 0F 18 /0-/3 and 0F 0D /0 read memory and name no register operand;
        // 0F 18 /4-/7, 0F 19, 0F 1D and 0F 1F are NOPs and access nothing.
        static const uint8_t prefetchnta[] = { 0x0F, 0x18, 0x00 };
        static const uint8_t prefetcht0[] = { 0x0F, 0x18, 0x08 };
        static const uint8_t prefetcht1[] = { 0x0F, 0x18, 0x10 };
        static const uint8_t prefetcht2[] = { 0x0F, 0x18, 0x18 };
        static const uint8_t nop18_4[] = { 0x0F, 0x18, 0x20 };
        static const uint8_t prefetchw[] = { 0x0F, 0x0D, 0x00 };
        static const uint8_t prefetchwt1[] = { 0x0F, 0x0D, 0x08 };
        static const uint8_t nop19[] = { 0x0F, 0x19, 0x00 };
        static const uint8_t nop1d[] = { 0x0F, 0x1D, 0x00 };
        static const uint8_t nop1f[] = { 0x0F, 0x1F, 0x00 };
        static const uint8_t endbr64[] = { 0xF3, 0x0F, 0x1E, 0xFA };
        expect_set("prefetchnta src M", 64, prefetchnta, 3, 0, XSET_MEM, 1);
        expect_set("prefetchnta (no src other)", 64, prefetchnta, 3, 0, XSET_OTHER, 0);
        expect_set("prefetchnta (no dst other)", 64, prefetchnta, 3, 1, XSET_OTHER, 0);
        expect_set("prefetcht0 (no src other)", 64, prefetcht0, 3, 0, XSET_OTHER, 0);
        expect_set("prefetcht1 (no src other)", 64, prefetcht1, 3, 0, XSET_OTHER, 0);
        expect_set("prefetcht2 (no src other)", 64, prefetcht2, 3, 0, XSET_OTHER, 0);
        expect_set("prefetchw src M", 64, prefetchw, 3, 0, XSET_MEM, 1);
        expect_set("prefetchw (no src other)", 64, prefetchw, 3, 0, XSET_OTHER, 0);
        expect_set("prefetchwt1 src M", 64, prefetchwt1, 3, 0, XSET_MEM, 1);
        expect_set("prefetchwt1 (no src other)", 64, prefetchwt1, 3, 0, XSET_OTHER, 0);
        expect_set("prefetchwt1 (no dst other)", 64, prefetchwt1, 3, 1, XSET_OTHER, 0);
        expect_set("nop Ev (no src M)", 64, nop1f, 3, 0, XSET_MEM, 0);
        expect_set("nop Ev (no src other)", 64, nop1f, 3, 0, XSET_OTHER, 0);
        expect_set("nop Ev (no dst other)", 64, nop1f, 3, 1, XSET_OTHER, 0);
        expect_set("nop Ev (no dst M)", 64, nop1f, 3, 1, XSET_MEM, 0);
        expect_set("0F 19 nop (no src M)", 64, nop19, 3, 0, XSET_MEM, 0);
        expect_set("0F 19 nop (no src other)", 64, nop19, 3, 0, XSET_OTHER, 0);
        expect_set("0F 1D nop (no src M)", 64, nop1d, 3, 0, XSET_MEM, 0);
        expect_set("0F 1D nop (no src other)", 64, nop1d, 3, 0, XSET_OTHER, 0);
        expect_set("0F 18 /4 nop (no src M)", 64, nop18_4, 3, 0, XSET_MEM, 0);
        expect_set("0F 18 /4 nop (no src other)", 64, nop18_4, 3, 0, XSET_OTHER, 0);
        expect_set("0F 18 /4 nop (no dst other)", 64, nop18_4, 3, 1, XSET_OTHER, 0);
        // The same NOPs with a register operand (mod=3) access nothing either.
        static const uint8_t nop1f_r[] = { 0x0F, 0x1F, 0xC0 };
        static const uint8_t nop19_r[] = { 0x0F, 0x19, 0xC0 };
        static const uint8_t nop1d_r[] = { 0x0F, 0x1D, 0xC0 };
        static const uint8_t nop18_4r[] = { 0x0F, 0x18, 0xE0 };
        expect_set("nop r/m Ev (no src other)", 64, nop1f_r, 3, 0, XSET_OTHER, 0);
        expect_set("nop r/m Ev (no dst other)", 64, nop1f_r, 3, 1, XSET_OTHER, 0);
        expect_set("0F 19 r/m nop (no src other)", 64, nop19_r, 3, 0, XSET_OTHER, 0);
        expect_set("0F 19 r/m nop (no dst other)", 64, nop19_r, 3, 1, XSET_OTHER, 0);
        expect_set("0F 1D r/m nop (no src other)", 64, nop1d_r, 3, 0, XSET_OTHER, 0);
        expect_set("0F 1D r/m nop (no dst other)", 64, nop1d_r, 3, 1, XSET_OTHER, 0);
        expect_set("0F 18 /4 r/m nop (no src other)", 64, nop18_4r, 3, 0, XSET_OTHER, 0);
        expect_set("0F 18 /4 r/m nop (no dst other)", 64, nop18_4r, 3, 1, XSET_OTHER, 0);
        expect_set("endbr64 (no src other)", 64, endbr64, 4, 0, XSET_OTHER, 0);
        expect_set("endbr64 (no dst other)", 64, endbr64, 4, 1, XSET_OTHER, 0);
        // PREFETCHh and PREFETCH/PREFETCHW are defined only with a memory
        // operand, so their mod=3 encodings are reserved; 0F 18 /4-/7 keeps
        // its NOP form at mod=3.
        static const uint8_t prefetch_18_0[] = { 0x0F, 0x18, 0xC0 };
        static const uint8_t prefetch_18_3[] = { 0x0F, 0x18, 0xD8 };
        static const uint8_t prefetch_0d_0[] = { 0x0F, 0x0D, 0xC0 };
        static const uint8_t prefetch_0d_1[] = { 0x0F, 0x0D, 0xC8 };
        expect_flag("0F 18 /0 m3 bad", 64, prefetch_18_0, 3, C_BAD, 1);
        expect_flag("0F 18 /3 m3 bad", 64, prefetch_18_3, 3, C_BAD, 1);
        expect_flag("0F 0D /0 m3 bad", 64, prefetch_0d_0, 3, C_BAD, 1);
        expect_flag("0F 0D /1 m3 bad", 64, prefetch_0d_1, 3, C_BAD, 1);
        expect_flag("0F 18 /0 m3 bad (32)", 32, prefetch_18_0, 3, C_BAD, 1);
        expect_flag("prefetchnta mem not bad", 64, prefetchnta, 3, C_BAD, 0);
        expect_flag("prefetchw mem not bad", 64, prefetchw, 3, C_BAD, 0);
        expect_flag("0F 18 /4 m3 nop not bad", 64, nop18_4r, 3, C_BAD, 0);
    }
    {
        // LEA names its second operand as a memory address, so a mod=3
        // ModR/M byte is not an encoding of it in any mode. The opcode sits
        // in the legacy map, which the REX2 prefix keeps, so one rule covers
        // both encodings. The memory form stays legal everywhere.
        static const uint8_t lea_r32[]   = { 0x8D, 0xC0 };          // lea eax,eax
        static const uint8_t lea_r32b[]  = { 0x8D, 0xFF };          // /7 rm7
        static const uint8_t lea_r64[]   = { 0x48, 0x8D, 0xC0 };
        static const uint8_t lea_r2[]    = { 0xD5, 0x40, 0x8D, 0xC0 };
        static const uint8_t lea_m32[]   = { 0x8D, 0x00 };          // lea eax,[eax]
        static const uint8_t lea_m64[]   = { 0x48, 0x8D, 0x00 };
        expect_flag("8D C0 m3 bad (64)", 64, lea_r32, 2, C_BAD, 1);
        expect_flag("8D C0 m3 bad (32)", 32, lea_r32, 2, C_BAD, 1);
        expect_flag("8D C0 m3 bad (16)", 16, lea_r32, 2, C_BAD, 1);
        expect_flag("8D FF m3 bad (32)", 32, lea_r32b, 2, C_BAD, 1);
        expect_flag("48 8D C0 m3 bad (64)", 64, lea_r64, 3, C_BAD, 1);
        expect_flag("rex2 8D C0 m3 bad (64)", 64, lea_r2, 4, C_BAD, 1);
        expect_flag("lea [eax] not bad (64)", 64, lea_m32, 2, C_BAD, 0);
        expect_flag("lea [eax] not bad (32)", 32, lea_m32, 2, C_BAD, 0);
        expect_flag("lea [eax] not bad (16)", 16, lea_m32, 2, C_BAD, 0);
        expect_flag("48 8D 00 not bad (64)", 64, lea_m64, 3, C_BAD, 0);

        // MOVLPS / MOVHPS / MOVNTPS store to memory only, so their mod=3
        // ModR/M bytes name no instruction. The 66-prefixed doubles and the
        // REX2 encoding keep the same r/m, and the memory forms stay legal in
        // every mode.
        static const uint8_t movlps_r[]  = { 0x0F, 0x13, 0xC0 };
        static const uint8_t movlps_m[]  = { 0x0F, 0x13, 0x00 };
        static const uint8_t movlpd_r[]  = { 0x66, 0x0F, 0x13, 0xC0 };
        static const uint8_t movlpd_m[]  = { 0x66, 0x0F, 0x13, 0x00 };
        static const uint8_t movlps_r2[] = { 0xD5, 0x80, 0x13, 0xC0 };
        static const uint8_t movhps_r[]  = { 0x0F, 0x17, 0xC0 };
        static const uint8_t movhps_m[]  = { 0x0F, 0x17, 0x00 };
        static const uint8_t movntps_r[] = { 0x0F, 0x2B, 0xC0 };
        static const uint8_t movntps_m[] = { 0x0F, 0x2B, 0x00 };
        expect_flag("0F 13 /r m3 bad (64)", 64, movlps_r, 3, C_BAD, 1);
        expect_flag("0F 13 /r m3 bad (32)", 32, movlps_r, 3, C_BAD, 1);
        expect_flag("0F 13 /r m3 bad (16)", 16, movlps_r, 3, C_BAD, 1);
        expect_flag("66 0F 13 /r m3 bad (64)", 64, movlpd_r, 4, C_BAD, 1);
        expect_flag("rex2 0F 13 /r m3 bad (64)", 64, movlps_r2, 4, C_BAD, 1);
        expect_flag("0F 17 /r m3 bad (64)", 64, movhps_r, 3, C_BAD, 1);
        expect_flag("0F 17 /r m3 bad (32)", 32, movhps_r, 3, C_BAD, 1);
        expect_flag("0F 17 /r m3 bad (16)", 16, movhps_r, 3, C_BAD, 1);
        expect_flag("0F 2B /r m3 bad (64)", 64, movntps_r, 3, C_BAD, 1);
        expect_flag("0F 2B /r m3 bad (32)", 32, movntps_r, 3, C_BAD, 1);
        expect_flag("0F 2B /r m3 bad (16)", 16, movntps_r, 3, C_BAD, 1);
        expect_flag("movlps m64 not bad (64)", 64, movlps_m, 3, C_BAD, 0);
        expect_flag("movlps m64 not bad (32)", 32, movlps_m, 3, C_BAD, 0);
        expect_flag("66 movlpd m64 not bad (64)", 64, movlpd_m, 4, C_BAD, 0);
        expect_flag("movhps m64 not bad (64)", 64, movhps_m, 3, C_BAD, 0);
        expect_flag("movntps m128 not bad (64)", 64, movntps_m, 3, C_BAD, 0);

        // MOVMSKPS / MOVMSKPD read their xmm source from a register, so the
        // r/m must be mod=3 and any other mod is not an encoding of them.
        static const uint8_t movmskps_r[] = { 0x0F, 0x50, 0xC0 };
        static const uint8_t movmskps_m[] = { 0x0F, 0x50, 0x00 };
        static const uint8_t movmskps_m7[] = { 0x0F, 0x50, 0x3F };
        static const uint8_t movmskpd_r[] = { 0x66, 0x0F, 0x50, 0xC0 };
        static const uint8_t movmskpd_m[] = { 0x66, 0x0F, 0x50, 0x00 };
        expect_flag("0F 50 m0 bad (64)", 64, movmskps_m, 3, C_BAD, 1);
        expect_flag("0F 50 m0 bad (32)", 32, movmskps_m, 3, C_BAD, 1);
        expect_flag("0F 50 m0 bad (16)", 16, movmskps_m, 3, C_BAD, 1);
        expect_flag("0F 50 /7 m0 bad (64)", 64, movmskps_m7, 3, C_BAD, 1);
        expect_flag("66 0F 50 m0 bad (64)", 64, movmskpd_m, 4, C_BAD, 1);
        expect_flag("movmskps r32 not bad (64)", 64, movmskps_r, 3, C_BAD, 0);
        expect_flag("movmskps r32 not bad (32)", 32, movmskps_r, 3, C_BAD, 0);
        expect_flag("movmskps r32 not bad (16)", 16, movmskps_r, 3, C_BAD, 0);
        expect_flag("movmskpd r32 not bad (64)", 64, movmskpd_r, 4, C_BAD, 0);
    }
    {
        // The same mod rule covers every other opcode whose r/m the SDM
        // fixes to memory or to a register. 66 0F 12 / 0F 16 are the
        // memory-only MOVLPD/MOVHPD pair; without the 66 and with F3/F2 the
        // same opcodes have register forms (MOVHLPS/MOVLHPS, MOVSLDUP/
        // MOVSHDUP, MOVDDUP), so only that one prefix combination is marked.
        static const uint8_t movlpd_r[]  = { 0x66, 0x0F, 0x12, 0xC0 };
        static const uint8_t movlpd_m[]  = { 0x66, 0x0F, 0x12, 0x00 };
        static const uint8_t movhpd_r[]  = { 0x66, 0x0F, 0x16, 0xC0 };
        static const uint8_t movhpd_m[]  = { 0x66, 0x0F, 0x16, 0x00 };
        static const uint8_t movhlps[]   = { 0x0F, 0x12, 0xC0 };
        static const uint8_t movlhps[]   = { 0x0F, 0x16, 0xC0 };
        static const uint8_t movsldup[]  = { 0xF3, 0x0F, 0x12, 0xC0 };
        static const uint8_t movshdup[]  = { 0xF3, 0x0F, 0x16, 0xC0 };
        static const uint8_t movddup[]   = { 0xF2, 0x0F, 0x12, 0xC0 };
        expect_flag("66 0F 12 /r m3 bad (64)", 64, movlpd_r, 4, C_BAD, 1);
        expect_flag("66 0F 12 /r m3 bad (32)", 32, movlpd_r, 4, C_BAD, 1);
        expect_flag("66 0F 12 /r m3 bad (16)", 16, movlpd_r, 4, C_BAD, 1);
        expect_flag("66 0F 16 /r m3 bad (64)", 64, movhpd_r, 4, C_BAD, 1);
        expect_flag("66 movlpd m64 not bad (64)", 64, movlpd_m, 4, C_BAD, 0);
        expect_flag("66 movhpd m64 not bad (64)", 64, movhpd_m, 4, C_BAD, 0);
        expect_flag("movhlps not bad (64)", 64, movhlps, 3, C_BAD, 0);
        expect_flag("movlhps not bad (64)", 64, movlhps, 3, C_BAD, 0);
        expect_flag("movsldup not bad (64)", 64, movsldup, 4, C_BAD, 0);
        expect_flag("movshdup not bad (64)", 64, movshdup, 4, C_BAD, 0);
        expect_flag("movddup not bad (64)", 64, movddup, 4, C_BAD, 0);

        // LSS/LFS/LGS read a far pointer from memory, MOVNTI and the MOVNTQ/
        // MOVNTDQ pair store to memory, and LDDQU is the F2-prefixed m128
        // load: all of them name a memory r/m in every prefix combination.
        static const uint8_t lss_r[]     = { 0x0F, 0xB2, 0xC0 };
        static const uint8_t lss16_r[]   = { 0x66, 0x0F, 0xB2, 0xC0 };
        static const uint8_t lss_m[]     = { 0x0F, 0xB2, 0x00 };
        static const uint8_t lfs_r[]     = { 0x0F, 0xB4, 0xC0 };
        static const uint8_t lgs_r[]     = { 0x0F, 0xB5, 0xC0 };
        static const uint8_t movnti_r[]  = { 0x0F, 0xC3, 0xC0 };
        static const uint8_t movntq_r[]  = { 0x0F, 0xE7, 0xC0 };
        static const uint8_t movntdq_r[] = { 0x66, 0x0F, 0xE7, 0xC0 };
        static const uint8_t lddqu_r[]   = { 0xF2, 0x0F, 0xF0, 0xC0 };
        static const uint8_t lddqu_m[]   = { 0xF2, 0x0F, 0xF0, 0x00 };
        expect_flag("0F B2 m3 bad (64)", 64, lss_r, 3, C_BAD, 1);
        expect_flag("0F B2 m3 bad (32)", 32, lss_r, 3, C_BAD, 1);
        expect_flag("0F B2 m3 bad (16)", 16, lss_r, 3, C_BAD, 1);
        expect_flag("66 0F B2 m3 bad (64)", 64, lss16_r, 4, C_BAD, 1);
        expect_flag("0F B4 m3 bad (64)", 64, lfs_r, 3, C_BAD, 1);
        expect_flag("0F B5 m3 bad (64)", 64, lgs_r, 3, C_BAD, 1);
        expect_flag("0F C3 m3 bad (64)", 64, movnti_r, 3, C_BAD, 1);
        expect_flag("0F C3 m3 bad (32)", 32, movnti_r, 3, C_BAD, 1);
        expect_flag("0F E7 m3 bad (64)", 64, movntq_r, 3, C_BAD, 1);
        expect_flag("0F E7 m3 bad (32)", 32, movntq_r, 3, C_BAD, 1);
        expect_flag("66 0F E7 m3 bad (64)", 64, movntdq_r, 4, C_BAD, 1);
        expect_flag("66 0F E7 m3 bad (16)", 16, movntdq_r, 4, C_BAD, 1);
        expect_flag("F2 0F F0 m3 bad (64)", 64, lddqu_r, 4, C_BAD, 1);
        expect_flag("F2 0F F0 m3 bad (32)", 32, lddqu_r, 4, C_BAD, 1);
        expect_flag("lss [rax] not bad (64)", 64, lss_m, 3, C_BAD, 0);
        expect_flag("lddqu m128 not bad (64)", 64, lddqu_m, 4, C_BAD, 0);

        // PMOVMSKB and MASKMOVQ/MASKMOVDQU name a register r/m, and so does
        // MOVDQ2Q -- F2 0F D6. The unprefixed and 66 readings of 0F D6 are
        // the MOVQ r/m forms and stay legal at mod=3.
        static const uint8_t pmovmskb_m[]   = { 0x0F, 0xD7, 0x00 };
        static const uint8_t pmovmskb_r[]   = { 0x0F, 0xD7, 0xC0 };
        static const uint8_t pmovmskb66_m[] = { 0x66, 0x0F, 0xD7, 0x00 };
        static const uint8_t pmovmskb66_r[] = { 0x66, 0x0F, 0xD7, 0xC0 };
        static const uint8_t maskmovq_m[]   = { 0x0F, 0xF7, 0x00 };
        static const uint8_t maskmovq_r[]   = { 0x0F, 0xF7, 0xC0 };
        static const uint8_t maskmovdqu_m[] = { 0x66, 0x0F, 0xF7, 0x00 };
        static const uint8_t maskmovdqu_r[] = { 0x66, 0x0F, 0xF7, 0xC0 };
        static const uint8_t movdq2q_m[]    = { 0xF2, 0x0F, 0xD6, 0x00 };
        static const uint8_t movdq2q_r[]    = { 0xF2, 0x0F, 0xD6, 0xC0 };
        static const uint8_t movq66_r[]     = { 0x66, 0x0F, 0xD6, 0xC0 };
        static const uint8_t movq66_m[]     = { 0x66, 0x0F, 0xD6, 0x00 };
        expect_flag("0F D7 m bad (64)", 64, pmovmskb_m, 3, C_BAD, 1);
        expect_flag("0F D7 m bad (32)", 32, pmovmskb_m, 3, C_BAD, 1);
        expect_flag("0F D7 m bad (16)", 16, pmovmskb_m, 3, C_BAD, 1);
        expect_flag("66 0F D7 m bad (64)", 64, pmovmskb66_m, 4, C_BAD, 1);
        expect_flag("0F F7 m bad (64)", 64, maskmovq_m, 3, C_BAD, 1);
        expect_flag("0F F7 m bad (32)", 32, maskmovq_m, 3, C_BAD, 1);
        expect_flag("66 0F F7 m bad (64)", 64, maskmovdqu_m, 4, C_BAD, 1);
        expect_flag("F2 0F D6 m bad (64)", 64, movdq2q_m, 4, C_BAD, 1);
        expect_flag("F2 0F D6 m bad (32)", 32, movdq2q_m, 4, C_BAD, 1);
        expect_flag("pmovmskb mm not bad (64)", 64, pmovmskb_r, 3, C_BAD, 0);
        expect_flag("pmovmskb xmm not bad (64)", 64, pmovmskb66_r, 4, C_BAD, 0);
        expect_flag("maskmovq not bad (64)", 64, maskmovq_r, 3, C_BAD, 0);
        expect_flag("maskmovdqu not bad (64)", 64, maskmovdqu_r, 4, C_BAD, 0);
        expect_flag("movdq2q not bad (64)", 64, movdq2q_r, 4, C_BAD, 0);
        expect_flag("movq xmm not bad (64)", 64, movq66_r, 4, C_BAD, 0);
        expect_flag("movq xmm m64 not bad (64)", 64, movq66_m, 4, C_BAD, 0);
        // The plain 0F D6 is not a MOVQ form: the SDM spells the mm store out
        // as 0F 7F and the xmm store as 66 0F D6, so the bare encoding names
        // no instruction (binutils and LLVM both decode no instruction here).
        static const uint8_t movq_bare_r[]  = { 0x0F, 0xD6, 0xC0 };
        static const uint8_t movq_bare_m[]  = { 0x0F, 0xD6, 0x00 };
        expect_flag("0F D6 r bad (64)", 64, movq_bare_r, 3, C_BAD, 1);
        expect_flag("0F D6 m bad (64)", 64, movq_bare_m, 3, C_BAD, 1);

        // The 0F 38 map holds the remaining memory-only r/m operands:
        // MOVNTDQA, the INVEPT/INVVPID/INVPCID descriptors, MOVBE (whose F2
        // reading is CRC32 instead), WRUSSD/WRUSSQ, WRSSD/WRSSQ, MOVDIR64B,
        // ENQCMD/ENQCMDS and MOVDIRI.
        static const uint8_t movntdqa_r[]  = { 0x66, 0x0F, 0x38, 0x2A, 0xC0 };
        static const uint8_t movntdqa_m[]  = { 0x66, 0x0F, 0x38, 0x2A, 0x00 };
        static const uint8_t invept_r[]    = { 0x66, 0x0F, 0x38, 0x80, 0xC0 };
        static const uint8_t invvpid_r[]   = { 0x66, 0x0F, 0x38, 0x81, 0xC0 };
        static const uint8_t invpcid_r[]   = { 0x66, 0x0F, 0x38, 0x82, 0xC0 };
        static const uint8_t movbe_r0[]    = { 0x0F, 0x38, 0xF0, 0xC0 };
        static const uint8_t movbe_r1[]    = { 0x0F, 0x38, 0xF1, 0xC0 };
        static const uint8_t movbe16_r0[]  = { 0x66, 0x0F, 0x38, 0xF0, 0xC0 };
        static const uint8_t movbe16_r1[]  = { 0x66, 0x0F, 0x38, 0xF1, 0xC0 };
        static const uint8_t movbe_m0[]    = { 0x0F, 0x38, 0xF0, 0x00 };
        static const uint8_t movbe_m1[]    = { 0x0F, 0x38, 0xF1, 0x00 };
        static const uint8_t crc32_rm8[]   = { 0xF2, 0x0F, 0x38, 0xF0, 0xC0 };
        static const uint8_t crc32_rm32[]  = { 0xF2, 0x0F, 0x38, 0xF1, 0xC0 };
        static const uint8_t wruss_r[]     = { 0x66, 0x0F, 0x38, 0xF5, 0xC0 };
        static const uint8_t wrss_r[]      = { 0x0F, 0x38, 0xF6, 0xC0 };
        static const uint8_t movdir64b_r[] = { 0x66, 0x0F, 0x38, 0xF8, 0xC0 };
        static const uint8_t enqcmd_r[]    = { 0xF2, 0x0F, 0x38, 0xF8, 0xC0 };
        static const uint8_t movdiri_r[]   = { 0x0F, 0x38, 0xF9, 0xC0 };
        expect_flag("66 0F 38 2A m3 bad (64)", 64, movntdqa_r, 5, C_BAD, 1);
        expect_flag("66 0F 38 80 m3 bad (64)", 64, invept_r, 5, C_BAD, 1);
        expect_flag("66 0F 38 81 m3 bad (64)", 64, invvpid_r, 5, C_BAD, 1);
        expect_flag("66 0F 38 82 m3 bad (64)", 64, invpcid_r, 5, C_BAD, 1);
        expect_flag("0F 38 F0 m3 bad (64)", 64, movbe_r0, 4, C_BAD, 1);
        expect_flag("0F 38 F1 m3 bad (64)", 64, movbe_r1, 4, C_BAD, 1);
        expect_flag("0F 38 F1 m3 bad (32)", 32, movbe_r1, 4, C_BAD, 1);
        expect_flag("66 0F 38 F0 m3 bad (64)", 64, movbe16_r0, 5, C_BAD, 1);
        expect_flag("66 0F 38 F1 m3 bad (16)", 16, movbe16_r1, 5, C_BAD, 1);
        expect_flag("66 0F 38 F5 m3 bad (64)", 64, wruss_r, 5, C_BAD, 1);
        expect_flag("0F 38 F6 m3 bad (64)", 64, wrss_r, 4, C_BAD, 1);
        expect_flag("66 0F 38 F8 m3 bad (64)", 64, movdir64b_r, 5, C_BAD, 1);
        expect_flag("F2 0F 38 F8 m3 bad (64)", 64, enqcmd_r, 5, C_BAD, 1);
        expect_flag("0F 38 F9 m3 bad (64)", 64, movdiri_r, 4, C_BAD, 1);
        expect_flag("movbe m32 not bad (64)", 64, movbe_m0, 4, C_BAD, 0);
        expect_flag("movbe m32 store not bad (64)", 64, movbe_m1, 4, C_BAD, 0);
        expect_flag("movntdqa not bad (64)", 64, movntdqa_m, 5, C_BAD, 0);
        expect_flag("crc32 r/m8 not bad (64)", 64, crc32_rm8, 5, C_BAD, 0);
        expect_flag("crc32 r/m32 not bad (64)", 64, crc32_rm32, 5, C_BAD, 0);
    }
    {
        // 0F 1E without a prefix is NOP Ev, the same class as 0F 1F: neither
        // the r/m register nor the memory operand is accessed. With F3 the
        // same opcode carries ENDBR64/32 (/7) and RDSSPD/RDSSPQ (/0).
        static const uint8_t nop1e_m[] = { 0x0F, 0x1E, 0x00 };
        static const uint8_t nop1e_m4[] = { 0x0F, 0x1E, 0x20 };
        static const uint8_t nop1e[] = { 0x0F, 0x1E, 0xC0 };
        static const uint8_t rdsspd[] = { 0xF3, 0x0F, 0x1E, 0xC0 };
        static const uint8_t rdsspq[] = { 0xF3, 0x48, 0x0F, 0x1E, 0xC0 };
        expect_set("0F 1E nop (no src M)", 64, nop1e_m, 3, 0, XSET_MEM, 0);
        expect_set("0F 1E nop (no src other)", 64, nop1e_m, 3, 0, XSET_OTHER, 0);
        expect_set("0F 1E nop (no dst other)", 64, nop1e_m, 3, 1, XSET_OTHER, 0);
        expect_set("0F 1E /4 nop (no src other)", 64, nop1e_m4, 3, 0, XSET_OTHER, 0);
        expect_set("0F 1E nop Ev (no src other)", 64, nop1e, 3, 0, XSET_OTHER, 0);
        expect_set("0F 1E nop Ev (no dst other)", 64, nop1e, 3, 1, XSET_OTHER, 0);
        expect_set("rdsspd src other", 64, rdsspd, 4, 0, XSET_OTHER, 1);
        expect_set("rdsspd dst RAX", 64, rdsspd, 4, 1, XSET_RAX, 1);
        expect_set("rdsspd (no dst other)", 64, rdsspd, 4, 1, XSET_OTHER, 0);
        expect_set("rdsspq dst RAX", 64, rdsspq, 5, 1, XSET_RAX, 1);
        expect_set("rdsspq (no dst other)", 64, rdsspq, 5, 1, XSET_OTHER, 0);
    }
    {
        // 0F B2/B4/B5 LSS/LFS/LGS write a GPR and read a segment + memory.
        static const uint8_t lss[] = { 0x0F, 0xB2, 0x00 };
        static const uint8_t lfs[] = { 0x0F, 0xB4, 0x00 };
        static const uint8_t lgs[] = { 0x0F, 0xB5, 0x00 };
        static const uint8_t movzx[] = { 0x0F, 0xB6, 0x00 };
        expect_set("lss eax,[rax] dst RAX", 64, lss, 3, 1, XSET_RAX, 1);
        expect_set("lss eax,[rax] dst other", 64, lss, 3, 1, XSET_OTHER, 1);
        expect_set("lss eax,[rax] src M", 64, lss, 3, 0, XSET_MEM, 1);
        expect_set("lfs eax,[rax] dst RAX", 64, lfs, 3, 1, XSET_RAX, 1);
        expect_set("lgs eax,[rax] dst RAX", 64, lgs, 3, 1, XSET_RAX, 1);
        expect_set("movzx eax,[rax] dst RAX", 64, movzx, 3, 1, XSET_RAX, 1);
    }
    {
        // C7 F8 / C6 F8 are XBEGIN / XABORT, not the group's r/m forms, so the
        // C_BAD that guards reg != 0 must not fire for /7.
        static const uint8_t xbegin[] = { 0xC7, 0xF8, 0x00, 0x00, 0x00, 0x00 };
        static const uint8_t xabort[] = { 0xC6, 0xF8, 0x00 };
        static const uint8_t xbegin_bad[] = { 0xC7, 0xFA, 0x00, 0x00, 0x00, 0x00 };
        static const uint8_t xabort_bad[] = { 0xC6, 0xFA, 0x00 };
        static const uint8_t mov_al_imm[] = { 0xC6, 0xC0, 0x00 };
        static const uint8_t mov_store[] = { 0xC7, 0x04, 0x24, 0x00, 0x00, 0x00, 0x00 };
        expect_flag("xbegin not bad", 64, xbegin, 6, C_BAD, 0);
        expect_flag("xabort not bad", 64, xabort, 3, C_BAD, 0);
        expect_flag("mov [rsp],imm32 not bad", 64, mov_store, 7, C_BAD, 0);
        // Only the F8 ModR/M is XBEGIN/XABORT; every other /7 stays invalid.
        expect_flag("C7 FA still bad", 64, xbegin_bad, 6, C_BAD, 1);
        expect_flag("C6 FA still bad", 64, xabort_bad, 3, C_BAD, 1);
        // XABORT puts its immediate in bits 31:24 of EAX, so EAX is its one
        // operand and the AL the group's r/m8 form would name is not one.
        expect_seteq("xabort dst EAX", 64, xabort, 3, 1, XSET_EAX);
        expect_seteq("xabort src none", 64, xabort, 3, 0, 0);
        // C6 /0 stays the MOV r/m8,imm8 form, with the byte's r/m as its
        // destination register.
        expect_seteq("mov al,imm8 dst", 64, mov_al_imm, 3, 1, XSET_AL);
    }
    {
        // 0F 00 / 0F 01 / 0F 02 / 0F 03: the table marks the whole group
        // unknown, but the SDM defines the operand of every form except the
        // 0F 00 /6 /7 holes, 0F 01 /5 and the register forms of 0F 01. Those
        // forms get their sets rebuilt from the ModR/M byte; the rest keep the
        // whole-set unknown and C_UNDEF. XSET_UNDEF is all ones, so only an
        // exact-set check can tell a modelled form from an unknown one.
        //
        // SLDT / STR / SMSW write their register destination at the operand
        // size: the SDM zero-extends the selector into a 64-bit destination
        // and clears (or leaves undefined) the high half of a 32-bit one, and
        // SMSW r32 zero-extends CR0. The r/m16 operands that are only read
        // keep the 16 bits they consume: LLDT/LTR/VERR/VERW/LMSW fix their
        // operand size at 16 bits, and LAR/LSL use the selector's low 16 bits.
        static const uint8_t sldt_r[]   = { 0x0F, 0x00, 0xC0 };       // sldt eax
        static const uint8_t sldt_r16[] = { 0x66, 0x0F, 0x00, 0xC0 }; // sldt ax
        static const uint8_t str_r16[]  = { 0x66, 0x0F, 0x00, 0xC8 }; // str ax
        static const uint8_t sldt_m[]   = { 0x0F, 0x00, 0x00 };       // sldt [rax]
        static const uint8_t lldt_r[]   = { 0x0F, 0x00, 0xD0 };       // lldt ax
        static const uint8_t ltr_r[]    = { 0x0F, 0x00, 0xD8 };       // ltr ax
        static const uint8_t lldt_m[]   = { 0x0F, 0x00, 0x10 };       // lldt [rax]
        static const uint8_t verr_r[]   = { 0x0F, 0x00, 0xE0 };       // verr ax
        static const uint8_t verw_r[]   = { 0x0F, 0x00, 0xE8 };       // verw ax
        static const uint8_t verr_m[]   = { 0x0F, 0x00, 0x20 };       // verr [rax]
        static const uint8_t sgdt_m[]   = { 0x0F, 0x01, 0x00 };       // sgdt [rax]
        static const uint8_t sidt_m[]   = { 0x0F, 0x01, 0x08 };       // sidt [rax]
        static const uint8_t lgdt_m[]   = { 0x0F, 0x01, 0x10 };       // lgdt [rax]
        static const uint8_t lidt_m[]   = { 0x0F, 0x01, 0x18 };       // lidt [rax]
        static const uint8_t smsw_r[]   = { 0x0F, 0x01, 0xE0 };       // smsw eax
        static const uint8_t smsw_m[]   = { 0x0F, 0x01, 0x20 };       // smsw [rax]
        static const uint8_t lmsw_r[]   = { 0x0F, 0x01, 0xF0 };       // lmsw ax
        static const uint8_t lmsw_m[]   = { 0x0F, 0x01, 0x30 };       // lmsw [rax]
        static const uint8_t invlpg_m[] = { 0x0F, 0x01, 0x38 };       // invlpg [rax]
        static const uint8_t lar_r[]    = { 0x0F, 0x02, 0xC1 };       // lar eax,ecx
        static const uint8_t lar_r16[]  = { 0x66, 0x0F, 0x02, 0xC1 }; // lar ax,cx
        static const uint8_t lar_r8[]   = { 0x44, 0x0F, 0x02, 0xC1 }; // lar r8d,ecx
        static const uint8_t lar_w[]    = { 0x48, 0x0F, 0x02, 0xC1 }; // lar rax,rcx
        static const uint8_t lsl_r[]    = { 0x0F, 0x03, 0xC1 };       // lsl eax,ecx
        static const uint8_t lar_m[]    = { 0x0F, 0x02, 0x00 };       // lar eax,[rax]
        static const uint8_t rex2_lar[] = { 0xD5, 0x80, 0x02, 0xC1 }; // lar eax,ecx
        static const uint8_t rex2_sldt[] = { 0xD5, 0x90, 0x00, 0xC0 }; // sldt r16

        // The register forms of SGDT/SIDT/LGDT/LIDT and INVLPG are #UD, and
        // 0F 00 /6 /7, 0F 01 /5 and the 0F 01 register group (SERIALIZE,
        // RDPKRU, SWAPGS, VMCALL, XGETBV...) are not modelled yet: all of
        // those stay wholly unknown. A VEX encoding reaches this opcode too,
        // and defines nothing either.
        static const uint8_t grp6_6[]   = { 0x0F, 0x00, 0xF0 };
        static const uint8_t grp6_7[]   = { 0x0F, 0x00, 0xF8 };
        static const uint8_t grp7_5[]   = { 0x0F, 0x01, 0xE8 }; // SERIALIZE
        static const uint8_t grp7_5m[]  = { 0x0F, 0x01, 0x28 };
        static const uint8_t vmcall[]   = { 0x0F, 0x01, 0xC1 };
        static const uint8_t monitor[]  = { 0x0F, 0x01, 0xC8 };
        static const uint8_t xgetbv[]   = { 0x0F, 0x01, 0xD0 };
        static const uint8_t sgdt_r[]   = { 0x0F, 0x01, 0xC0 };
        static const uint8_t vex_grp6[] = { 0xC5, 0xF8, 0x00, 0xC0 };

        expect_seteq("sldt eax dst", 64, sldt_r, 3, 1, XSET_RAX);
        expect_seteq("sldt eax src", 64, sldt_r, 3, 0, 0);
        expect_seteq("sldt eax (32) dst", 32, sldt_r, 3, 1, XSET_EAX);
        expect_seteq("sldt ax dst", 64, sldt_r16, 4, 1, XSET_AX);
        expect_seteq("sldt ax src", 64, sldt_r16, 4, 0, 0);
        expect_seteq("str ax dst", 64, str_r16, 4, 1, XSET_AX);
        expect_seteq("str ax src", 64, str_r16, 4, 0, 0);
        expect_seteq("sldt [rax] dst", 64, sldt_m, 3, 1, XSET_MEM);
        expect_seteq("sldt [rax] src", 64, sldt_m, 3, 0, XSET_RAX);
        expect_seteq("lldt ax src", 64, lldt_r, 3, 0, XSET_AX);
        expect_seteq("lldt ax dst", 64, lldt_r, 3, 1, XSET_OTHER);
        expect_seteq("ltr ax src", 64, ltr_r, 3, 0, XSET_AX);
        expect_seteq("ltr ax dst", 64, ltr_r, 3, 1, XSET_OTHER);
        expect_seteq("lldt [rax] src", 64, lldt_m, 3, 0, XSET_RAX | XSET_MEM);
        expect_seteq("lldt [rax] dst", 64, lldt_m, 3, 1, XSET_OTHER);
        expect_seteq("verr ax src", 64, verr_r, 3, 0, XSET_AX);
        expect_seteq("verr ax dst", 64, verr_r, 3, 1, XSET_FL);
        expect_seteq("verw ax src", 64, verw_r, 3, 0, XSET_AX);
        expect_seteq("verw ax dst", 64, verw_r, 3, 1, XSET_FL);
        expect_seteq("verr [rax] src", 64, verr_m, 3, 0, XSET_RAX | XSET_MEM);
        expect_seteq("verr [rax] dst", 64, verr_m, 3, 1, XSET_FL);
        expect_seteq("sgdt [rax] dst", 64, sgdt_m, 3, 1, XSET_MEM);
        expect_seteq("sgdt [rax] src", 64, sgdt_m, 3, 0, XSET_RAX);
        expect_seteq("sidt [rax] dst", 64, sidt_m, 3, 1, XSET_MEM);
        expect_seteq("sidt [rax] src", 64, sidt_m, 3, 0, XSET_RAX);
        expect_seteq("lgdt [rax] src", 64, lgdt_m, 3, 0, XSET_RAX | XSET_MEM);
        expect_seteq("lgdt [rax] dst", 64, lgdt_m, 3, 1, XSET_OTHER);
        expect_seteq("lidt [rax] src", 64, lidt_m, 3, 0, XSET_RAX | XSET_MEM);
        expect_seteq("lidt [rax] dst", 64, lidt_m, 3, 1, XSET_OTHER);
        expect_seteq("smsw eax dst", 64, smsw_r, 3, 1, XSET_RAX);
        expect_seteq("smsw eax src", 64, smsw_r, 3, 0, 0);
        expect_seteq("smsw [rax] dst", 64, smsw_m, 3, 1, XSET_MEM);
        expect_seteq("smsw [rax] src", 64, smsw_m, 3, 0, XSET_RAX);
        expect_seteq("lmsw ax src", 64, lmsw_r, 3, 0, XSET_AX);
        expect_seteq("lmsw ax dst", 64, lmsw_r, 3, 1, XSET_OTHER);
        expect_seteq("lmsw [rax] src", 64, lmsw_m, 3, 0, XSET_RAX | XSET_MEM);
        expect_seteq("lmsw [rax] dst", 64, lmsw_m, 3, 1, XSET_OTHER);
        expect_seteq("invlpg [rax] src", 64, invlpg_m, 3, 0, XSET_RAX | XSET_MEM);
        expect_seteq("invlpg [rax] dst", 64, invlpg_m, 3, 1, 0);
        expect_seteq("lar eax,ecx dst", 64, lar_r, 3, 1, XSET_RAX | XSET_FL);
        expect_seteq("lar eax,ecx src", 64, lar_r, 3, 0, XSET_CX);
        expect_seteq("lar ax,cx dst", 64, lar_r16, 4, 1, XSET_AX | XSET_FL);
        expect_seteq("lar ax,cx src", 64, lar_r16, 4, 0, XSET_CX);
        expect_seteq("lar r8d,ecx dst", 64, lar_r8, 4, 1, XSET_R8 | XSET_FL);
        expect_seteq("lar r8d,ecx src", 64, lar_r8, 4, 0, XSET_CX);
        expect_seteq("lar rax,rcx dst", 64, lar_w, 4, 1, XSET_RAX | XSET_FL);
        expect_seteq("lsl eax,ecx dst", 64, lsl_r, 3, 1, XSET_RAX | XSET_FL);
        expect_seteq("lsl eax,ecx src", 64, lsl_r, 3, 0, XSET_CX);
        expect_seteq("lar eax,[rax] src", 64, lar_m, 3, 0, XSET_RAX | XSET_MEM);
        expect_seteq("lar eax,[rax] dst", 64, lar_m, 3, 1, XSET_RAX | XSET_FL);
        expect_seteq("rex2 lar dst", 64, rex2_lar, 4, 1, XSET_RAX | XSET_FL);
        expect_seteq("rex2 sldt r16 dst2", 64, rex2_sldt, 4, 3, XSET2_R16);
        expect_seteq("rex2 sldt r16 dst", 64, rex2_sldt, 4, 1, 0);

        expect_flag("sldt eax not undef", 64, sldt_r, 3, C_UNDEF, 0);
        expect_flag("lar eax,ecx not undef", 64, lar_r, 3, C_UNDEF, 0);
        expect_flag("smsw eax not undef", 64, smsw_r, 3, C_UNDEF, 0);
        expect_seteq("0F 00 /6 undef", 64, grp6_6, 3, 1, XSET_UNDEF);
        expect_seteq("0F 00 /7 undef", 64, grp6_7, 3, 1, XSET_UNDEF);
        expect_seteq("0F 01 /5 undef", 64, grp7_5, 3, 1, XSET_UNDEF);
        expect_seteq("0F 01 /5 m undef", 64, grp7_5m, 3, 1, XSET_UNDEF);
        expect_seteq("vmcall undef", 64, vmcall, 3, 1, XSET_UNDEF);
        expect_seteq("monitor undef", 64, monitor, 3, 1, XSET_UNDEF);
        expect_seteq("xgetbv undef", 64, xgetbv, 3, 1, XSET_UNDEF);
        expect_seteq("sgdt r undef", 64, sgdt_r, 3, 1, XSET_UNDEF);
        expect_seteq("vex 0F 00 undef", 64, vex_grp6, 4, 1, XSET_UNDEF);
        expect_flag("0F 00 /6 keeps undef", 64, grp6_6, 3, C_UNDEF, 1);
        expect_flag("0F 01 /5 keeps undef", 64, grp7_5, 3, C_UNDEF, 1);
        expect_flag("vmcall keeps undef", 64, vmcall, 3, C_UNDEF, 1);
        expect_flag("vex 0F 00 keeps undef", 64, vex_grp6, 4, C_UNDEF, 1);
    }
    {
        // The reserved slots of those two groups are what C_BAD is for. The
        // 0F 00 group leaves /6 and /7 out of its register table and its
        // memory table alike, so every ModR/M byte with those reg values is
        // unusable in every mode. 0F 01 reserves /5 in its memory table only,
        // and the register table it switches to at mod=3 leaves C6/C7, CC-CE,
        // D2/D3 and E9-ED unassigned. The slots around those holes are real
        // instructions and must not be marked.
        static const uint8_t g6_6m3[]   = { 0x0F, 0x00, 0xF0 };             // 0F 00 /6
        static const uint8_t g6_7m3[]   = { 0x0F, 0x00, 0xF8 };             // 0F 00 /7
        static const uint8_t g6_6m0[]   = { 0x0F, 0x00, 0x30 };             // 0F 00 /6 [rax]
        static const uint8_t g6_6m1[]   = { 0x0F, 0x00, 0x70, 0x00 };       // ... [rax+0]
        static const uint8_t g6_6m2[]   = { 0x0F, 0x00, 0xB0, 0, 0, 0, 0 }; // ... [rax+0]
        static const uint8_t g6_7m0[]   = { 0x0F, 0x00, 0x38 };             // 0F 00 /7 [rax]
        static const uint8_t rex2_g6_6[] = { 0xD5, 0x80, 0x00, 0xF0 };      // rex2 0F 00 /6
        static const uint8_t sldt_r[]   = { 0x0F, 0x00, 0xC0 };             // sldt eax
        static const uint8_t verw_r[]   = { 0x0F, 0x00, 0xE8 };             // verw ax
        static const uint8_t sldt_m[]   = { 0x0F, 0x00, 0x00 };             // sldt [rax]
        static const uint8_t verr_m[]   = { 0x0F, 0x00, 0x20 };             // verr [rax]
        static const uint8_t g7_5m0[]   = { 0x0F, 0x01, 0x28 };             // 0F 01 /5 [rax]
        static const uint8_t g7_5m1[]   = { 0x0F, 0x01, 0x68, 0x00 };
        static const uint8_t g7_5m2[]   = { 0x0F, 0x01, 0xA8, 0, 0, 0, 0 };
        static const uint8_t g7_5rm7[]  = { 0x0F, 0x01, 0x3D, 0, 0, 0, 0 }; // 0F 01 /7 INVLPG
        static const uint8_t smsw_m[]   = { 0x0F, 0x01, 0x20 };             // 0F 01 /4 [rax]
        static const uint8_t serialize[] = { 0x0F, 0x01, 0xE8 };
        static const uint8_t rdpkru[]   = { 0x0F, 0x01, 0xEE };
        static const uint8_t wrpkru[]   = { 0x0F, 0x01, 0xEF };
        static const uint8_t r_c6[]     = { 0x0F, 0x01, 0xC6 };
        static const uint8_t r_c7[]     = { 0x0F, 0x01, 0xC7 };
        static const uint8_t r_cc[]     = { 0x0F, 0x01, 0xCC };
        static const uint8_t r_cd[]     = { 0x0F, 0x01, 0xCD };
        static const uint8_t r_ce[]     = { 0x0F, 0x01, 0xCE };
        static const uint8_t r_d2[]     = { 0x0F, 0x01, 0xD2 };
        static const uint8_t r_d3[]     = { 0x0F, 0x01, 0xD3 };
        static const uint8_t r_e9[]     = { 0x0F, 0x01, 0xE9 };
        static const uint8_t r_ea[]     = { 0x0F, 0x01, 0xEA };
        static const uint8_t r_eb[]     = { 0x0F, 0x01, 0xEB };
        static const uint8_t r_ec[]     = { 0x0F, 0x01, 0xEC };
        static const uint8_t r_ed[]     = { 0x0F, 0x01, 0xED };
        static const uint8_t d_c0[]     = { 0x0F, 0x01, 0xC0 };  // ENCLV
        static const uint8_t d_c5[]     = { 0x0F, 0x01, 0xC5 };  // PCONFIG
        static const uint8_t d_cf[]     = { 0x0F, 0x01, 0xCF };  // ENCLS
        static const uint8_t d_d7[]     = { 0x0F, 0x01, 0xD7 };  // ENCLU
        static const uint8_t d_d8[]     = { 0x0F, 0x01, 0xD8 };  // VMRUN
        static const uint8_t d_df[]     = { 0x0F, 0x01, 0xDF };  // INVLPGA
        static const uint8_t d_f8[]     = { 0x0F, 0x01, 0xF8 };  // SWAPGS
        static const uint8_t smsw_r[]   = { 0x0F, 0x01, 0xE0 };  // smsw eax

        expect_flag("0F 00 /6 bad", 64, g6_6m3, 3, C_BAD, 1);
        expect_flag("0F 00 /7 bad", 64, g6_7m3, 3, C_BAD, 1);
        expect_flag("0F 00 /6 m0 bad", 64, g6_6m0, 3, C_BAD, 1);
        expect_flag("0F 00 /6 m1 bad", 64, g6_6m1, 4, C_BAD, 1);
        expect_flag("0F 00 /6 m2 bad", 64, g6_6m2, 7, C_BAD, 1);
        expect_flag("0F 00 /7 m0 bad", 64, g6_7m0, 3, C_BAD, 1);
        expect_flag("0F 00 /6 m3 bad (32)", 32, g6_6m3, 3, C_BAD, 1);
        expect_flag("rex2 0F 00 /6 bad", 64, rex2_g6_6, 4, C_BAD, 1);
        expect_flag("sldt eax not bad", 64, sldt_r, 3, C_BAD, 0);
        expect_flag("verw ax not bad", 64, verw_r, 3, C_BAD, 0);
        expect_flag("sldt [rax] not bad", 64, sldt_m, 3, C_BAD, 0);
        expect_flag("verr [rax] not bad", 64, verr_m, 3, C_BAD, 0);
        expect_flag("0F 01 /5 m0 bad", 64, g7_5m0, 3, C_BAD, 1);
        expect_flag("0F 01 /5 m1 bad", 64, g7_5m1, 4, C_BAD, 1);
        expect_flag("0F 01 /5 m2 bad", 64, g7_5m2, 7, C_BAD, 1);
        expect_flag("0F 01 /5 m0 bad (32)", 32, g7_5m0, 3, C_BAD, 1);
        expect_flag("0F 01 /7 m not bad", 64, g7_5rm7, 7, C_BAD, 0);
        expect_flag("0F 01 /4 m not bad", 64, smsw_m, 3, C_BAD, 0);
        expect_flag("serialize not bad", 64, serialize, 3, C_BAD, 0);
        expect_flag("rdpkru not bad", 64, rdpkru, 3, C_BAD, 0);
        expect_flag("wrpkru not bad", 64, wrpkru, 3, C_BAD, 0);
        expect_flag("0F 01 C6 bad", 64, r_c6, 3, C_BAD, 1);
        expect_flag("0F 01 C7 bad", 64, r_c7, 3, C_BAD, 1);
        expect_flag("0F 01 CC bad", 64, r_cc, 3, C_BAD, 1);
        expect_flag("0F 01 CD bad", 64, r_cd, 3, C_BAD, 1);
        expect_flag("0F 01 CE bad", 64, r_ce, 3, C_BAD, 1);
        expect_flag("0F 01 D2 bad", 64, r_d2, 3, C_BAD, 1);
        expect_flag("0F 01 D3 bad", 64, r_d3, 3, C_BAD, 1);
        expect_flag("0F 01 E9 bad", 64, r_e9, 3, C_BAD, 1);
        expect_flag("0F 01 EA bad", 64, r_ea, 3, C_BAD, 1);
        expect_flag("0F 01 EB bad", 64, r_eb, 3, C_BAD, 1);
        expect_flag("0F 01 EC bad", 64, r_ec, 3, C_BAD, 1);
        expect_flag("0F 01 ED bad", 64, r_ed, 3, C_BAD, 1);
        expect_flag("enclv not bad", 64, d_c0, 3, C_BAD, 0);
        expect_flag("pconfig not bad", 64, d_c5, 3, C_BAD, 0);
        expect_flag("encls not bad", 64, d_cf, 3, C_BAD, 0);
        expect_flag("enclu not bad", 64, d_d7, 3, C_BAD, 0);
        expect_flag("vmrun not bad", 64, d_d8, 3, C_BAD, 0);
        expect_flag("invlpga not bad", 64, d_df, 3, C_BAD, 0);
        expect_flag("swapgs not bad", 64, d_f8, 3, C_BAD, 0);
        expect_flag("smsw eax not bad", 64, smsw_r, 3, C_BAD, 0);
    }

    {
        // x87 (D8-DF): the SDM's escape tables leave slots blank in the memory
        // table (D9 /1, DB /4, DB /6, DD /5) and in the mod=3 ST(i) table, and
        // reserve every blank. Those slots carry C_BAD; the slots holding the
        // forms around them do not. The SDM also leaves DB E0/E1/E4/E5 and
        // DF C0-C7 blank, but a second decoder names those (the 8087/287
        // FENI/FDISI/FSETPM/FRSTPM and FFREEP), so they stay unmarked.
        static const uint8_t fld_m32[] = { 0xD9, 0x00 };
        static const uint8_t d9_1m0[]  = { 0xD9, 0x08 };
        static const uint8_t d9_1m1[]  = { 0xD9, 0x48, 0x00 };
        static const uint8_t d9_1m2[]  = { 0xD9, 0x88, 0, 0, 0, 0 };
        static const uint8_t d9_1m5[]  = { 0xD9, 0x0D, 0, 0, 0, 0 };
        static const uint8_t db_4m[]   = { 0xDB, 0x20 };
        static const uint8_t db_6m[]   = { 0xDB, 0x30 };
        static const uint8_t dd_5m[]   = { 0xDD, 0x28 };
        static const uint8_t db_5m[]   = { 0xDB, 0x28 };
        static const uint8_t dd_6m[]   = { 0xDD, 0x30 };
        static const uint8_t d9_d1[]   = { 0xD9, 0xD1 };
        static const uint8_t d9_d7[]   = { 0xD9, 0xD7 };
        static const uint8_t d9_d8[]   = { 0xD9, 0xD8 };
        static const uint8_t d9_df[]   = { 0xD9, 0xDF };
        static const uint8_t d9_e2[]   = { 0xD9, 0xE2 };
        static const uint8_t d9_ef[]   = { 0xD9, 0xEF };
        static const uint8_t da_e0[]   = { 0xDA, 0xE0 };
        static const uint8_t da_e8[]   = { 0xDA, 0xE8 };
        static const uint8_t da_ea[]   = { 0xDA, 0xEA };
        static const uint8_t da_ff[]   = { 0xDA, 0xFF };
        static const uint8_t db_e6[]   = { 0xDB, 0xE6 };
        static const uint8_t db_f8[]   = { 0xDB, 0xF8 };
        static const uint8_t db_ff[]   = { 0xDB, 0xFF };
        static const uint8_t dc_d0[]   = { 0xDC, 0xD0 };
        static const uint8_t dc_df[]   = { 0xDC, 0xDF };
        static const uint8_t dd_c8[]   = { 0xDD, 0xC8 };
        static const uint8_t dd_cf[]   = { 0xDD, 0xCF };
        static const uint8_t dd_f0[]   = { 0xDD, 0xF0 };
        static const uint8_t dd_ff[]   = { 0xDD, 0xFF };
        static const uint8_t de_d0[]   = { 0xDE, 0xD0 };
        static const uint8_t de_d8[]   = { 0xDE, 0xD8 };
        static const uint8_t de_da[]   = { 0xDE, 0xDA };
        static const uint8_t df_c8[]   = { 0xDF, 0xC8 };
        static const uint8_t df_df[]   = { 0xDF, 0xDF };
        static const uint8_t df_e1[]   = { 0xDF, 0xE1 };
        static const uint8_t df_f8[]   = { 0xDF, 0xF8 };
        static const uint8_t d9_c0[]   = { 0xD9, 0xC0 };
        static const uint8_t d9_d0[]   = { 0xD9, 0xD0 };
        static const uint8_t d9_e0[]   = { 0xD9, 0xE0 };
        static const uint8_t d9_f8[]   = { 0xD9, 0xF8 };
        static const uint8_t da_c0[]   = { 0xDA, 0xC0 };
        static const uint8_t da_e9[]   = { 0xDA, 0xE9 };
        static const uint8_t db_c0[]   = { 0xDB, 0xC0 };
        static const uint8_t db_e2[]   = { 0xDB, 0xE2 };
        static const uint8_t db_e3[]   = { 0xDB, 0xE3 };
        static const uint8_t db_e8[]   = { 0xDB, 0xE8 };
        static const uint8_t db_f0[]   = { 0xDB, 0xF0 };
        static const uint8_t dc_c0[]   = { 0xDC, 0xC0 };
        static const uint8_t dc_e0[]   = { 0xDC, 0xE0 };
        static const uint8_t dd_c0[]   = { 0xDD, 0xC0 };
        static const uint8_t dd_d0[]   = { 0xDD, 0xD0 };
        static const uint8_t dd_e8[]   = { 0xDD, 0xE8 };
        static const uint8_t de_c0[]   = { 0xDE, 0xC0 };
        static const uint8_t de_d9[]   = { 0xDE, 0xD9 };
        static const uint8_t df_e0[]   = { 0xDF, 0xE0 };
        static const uint8_t df_e8[]   = { 0xDF, 0xE8 };
        static const uint8_t df_f0[]   = { 0xDF, 0xF0 };
        static const uint8_t df_c0[]   = { 0xDF, 0xC0 };
        static const uint8_t db_e0[]   = { 0xDB, 0xE0 };
        static const uint8_t db_e4[]   = { 0xDB, 0xE4 };

        expect_flag("x87 D9 /1 m0 bad", 64, d9_1m0, 2, C_BAD, 1);
        expect_flag("x87 D9 /1 m1 bad", 64, d9_1m1, 3, C_BAD, 1);
        expect_flag("x87 D9 /1 m2 bad", 64, d9_1m2, 6, C_BAD, 1);
        expect_flag("x87 D9 /1 rm5 bad", 64, d9_1m5, 6, C_BAD, 1);
        expect_flag("x87 D9 /1 m0 bad (32)", 32, d9_1m0, 2, C_BAD, 1);
        expect_flag("x87 DB /4 m bad", 64, db_4m, 2, C_BAD, 1);
        expect_flag("x87 DB /6 m bad", 64, db_6m, 2, C_BAD, 1);
        expect_flag("x87 DD /5 m bad", 64, dd_5m, 2, C_BAD, 1);
        expect_flag("x87 fld m32 not bad", 64, fld_m32, 2, C_BAD, 0);
        expect_flag("x87 DB /5 m not bad", 64, db_5m, 2, C_BAD, 0);
        expect_flag("x87 DD /6 m not bad", 64, dd_6m, 2, C_BAD, 0);
        expect_flag("x87 D9 D1 bad", 64, d9_d1, 2, C_BAD, 1);
        expect_flag("x87 D9 D7 bad", 64, d9_d7, 2, C_BAD, 1);
        expect_flag("x87 D9 D8 bad", 64, d9_d8, 2, C_BAD, 1);
        expect_flag("x87 D9 DF bad", 64, d9_df, 2, C_BAD, 1);
        expect_flag("x87 D9 E2 bad", 64, d9_e2, 2, C_BAD, 1);
        expect_flag("x87 D9 EF bad", 64, d9_ef, 2, C_BAD, 1);
        expect_flag("x87 DA E0 bad", 64, da_e0, 2, C_BAD, 1);
        expect_flag("x87 DA E8 bad", 64, da_e8, 2, C_BAD, 1);
        expect_flag("x87 DA EA bad", 64, da_ea, 2, C_BAD, 1);
        expect_flag("x87 DA FF bad", 64, da_ff, 2, C_BAD, 1);
        expect_flag("x87 DB E6 bad", 64, db_e6, 2, C_BAD, 1);
        expect_flag("x87 DB F8 bad", 64, db_f8, 2, C_BAD, 1);
        expect_flag("x87 DB FF bad", 64, db_ff, 2, C_BAD, 1);
        expect_flag("x87 DC D0 bad", 64, dc_d0, 2, C_BAD, 1);
        expect_flag("x87 DC DF bad", 64, dc_df, 2, C_BAD, 1);
        expect_flag("x87 DD C8 bad", 64, dd_c8, 2, C_BAD, 1);
        expect_flag("x87 DD CF bad", 64, dd_cf, 2, C_BAD, 1);
        expect_flag("x87 DD F0 bad", 64, dd_f0, 2, C_BAD, 1);
        expect_flag("x87 DD FF bad", 64, dd_ff, 2, C_BAD, 1);
        expect_flag("x87 DE D0 bad", 64, de_d0, 2, C_BAD, 1);
        expect_flag("x87 DE D8 bad", 64, de_d8, 2, C_BAD, 1);
        expect_flag("x87 DE DA bad", 64, de_da, 2, C_BAD, 1);
        expect_flag("x87 DF C8 bad", 64, df_c8, 2, C_BAD, 1);
        expect_flag("x87 DF DF bad", 64, df_df, 2, C_BAD, 1);
        expect_flag("x87 DF E1 bad", 64, df_e1, 2, C_BAD, 1);
        expect_flag("x87 DF F8 bad", 64, df_f8, 2, C_BAD, 1);
        expect_flag("x87 D9 CF bad (32)", 32, dd_cf, 2, C_BAD, 1);
        expect_flag("x87 fld st0 not bad", 64, d9_c0, 2, C_BAD, 0);
        expect_flag("x87 fnop not bad", 64, d9_d0, 2, C_BAD, 0);
        expect_flag("x87 fchs not bad", 64, d9_e0, 2, C_BAD, 0);
        expect_flag("x87 fprem not bad", 64, d9_f8, 2, C_BAD, 0);
        expect_flag("x87 fcmovb not bad", 64, da_c0, 2, C_BAD, 0);
        expect_flag("x87 fucompp not bad", 64, da_e9, 2, C_BAD, 0);
        expect_flag("x87 fcmovnb not bad", 64, db_c0, 2, C_BAD, 0);
        expect_flag("x87 fnclex not bad", 64, db_e2, 2, C_BAD, 0);
        expect_flag("x87 fninit not bad", 64, db_e3, 2, C_BAD, 0);
        expect_flag("x87 fucomi not bad", 64, db_e8, 2, C_BAD, 0);
        expect_flag("x87 fcomi not bad", 64, db_f0, 2, C_BAD, 0);
        expect_flag("x87 fadd st0 not bad", 64, dc_c0, 2, C_BAD, 0);
        expect_flag("x87 fsubr st0 not bad", 64, dc_e0, 2, C_BAD, 0);
        expect_flag("x87 ffree not bad", 64, dd_c0, 2, C_BAD, 0);
        expect_flag("x87 fst st0 not bad", 64, dd_d0, 2, C_BAD, 0);
        expect_flag("x87 fucomp not bad", 64, dd_e8, 2, C_BAD, 0);
        expect_flag("x87 faddp not bad", 64, de_c0, 2, C_BAD, 0);
        expect_flag("x87 fcompp not bad", 64, de_d9, 2, C_BAD, 0);
        expect_flag("x87 fnstsw not bad", 64, df_e0, 2, C_BAD, 0);
        expect_flag("x87 fucomip not bad", 64, df_e8, 2, C_BAD, 0);
        expect_flag("x87 fcomip not bad", 64, df_f0, 2, C_BAD, 0);
        expect_flag("x87 ffreep not bad", 64, df_c0, 2, C_BAD, 0);
        expect_flag("x87 fneni not bad", 64, db_e0, 2, C_BAD, 0);
        expect_flag("x87 fnsetpm not bad", 64, db_e4, 2, C_BAD, 0);
    }

    // XA_BAD means "not a usable encoding in any mode", so the legacy forms
    // below, legal in 16/32/64-bit alike, must not carry it.
    {
        static const uint8_t insb[] = { 0x6C };
        static const uint8_t insd[] = { 0x6D };
        static const uint8_t outsb[] = { 0x6E };
        static const uint8_t outsd[] = { 0x6F };
        static const uint8_t jo8[] = { 0x70, 0x00 };
        static const uint8_t jno8[] = { 0x71, 0x00 };
        static const uint8_t jp8[] = { 0x7A, 0x00 };
        static const uint8_t jnp8[] = { 0x7B, 0x00 };
        static const uint8_t mov_r_sreg[] = { 0x8C, 0xC0 };
        static const uint8_t mov_sreg_r[] = { 0x8E, 0xC0 };
        static const uint8_t pushf[] = { 0x9C };
        static const uint8_t popf[] = { 0x9D };
        static const uint8_t sahf[] = { 0x9E };
        static const uint8_t lahf[] = { 0x9F };
        static const uint8_t lodsd[] = { 0xAD };
        static const uint8_t scasd[] = { 0xAF };
        static const uint8_t int3[] = { 0xCC };
        static const uint8_t xlat[] = { 0xD7 };
        static const uint8_t loopne8[] = { 0xE0, 0x00 };
        static const uint8_t loope8[] = { 0xE1, 0x00 };
        static const uint8_t in_al_ib[] = { 0xE4, 0x00 };
        static const uint8_t in_eax_ib[] = { 0xE5, 0x00 };
        static const uint8_t out_ib_al[] = { 0xE6, 0x00 };
        static const uint8_t out_ib_eax[] = { 0xE7, 0x00 };
        static const uint8_t in_al_dx[] = { 0xEC };
        static const uint8_t in_eax_dx[] = { 0xED };
        static const uint8_t out_dx_al[] = { 0xEE };
        static const uint8_t out_dx_eax[] = { 0xEF };
        static const uint8_t hlt[] = { 0xF4 };
        static const uint8_t cmc[] = { 0xF5 };
        static const uint8_t cli[] = { 0xFA };
        static const uint8_t sti[] = { 0xFB };
        static const uint8_t retf_ib[] = { 0xCA, 0x00, 0x00 };
        static const uint8_t retf[] = { 0xCB };
        static const uint8_t iret[] = { 0xCF };
        expect_flag("insb not bad", 64, insb, 1, C_BAD, 0);
        expect_flag("insd not bad", 64, insd, 1, C_BAD, 0);
        expect_flag("outsb not bad", 64, outsb, 1, C_BAD, 0);
        expect_flag("outsd not bad", 64, outsd, 1, C_BAD, 0);
        expect_flag("jo rel8 not bad", 64, jo8, 2, C_BAD, 0);
        expect_flag("jno rel8 not bad", 64, jno8, 2, C_BAD, 0);
        expect_flag("jp rel8 not bad", 64, jp8, 2, C_BAD, 0);
        expect_flag("jnp rel8 not bad", 64, jnp8, 2, C_BAD, 0);
        expect_flag("mov eax,sreg not bad", 64, mov_r_sreg, 2, C_BAD, 0);
        expect_flag("mov sreg,eax not bad", 64, mov_sreg_r, 2, C_BAD, 0);
        expect_flag("pushf not bad", 64, pushf, 1, C_BAD, 0);
        expect_flag("popf not bad", 64, popf, 1, C_BAD, 0);
        expect_flag("sahf not bad", 64, sahf, 1, C_BAD, 0);
        expect_flag("lahf not bad", 64, lahf, 1, C_BAD, 0);
        expect_flag("lodsd not bad", 64, lodsd, 1, C_BAD, 0);
        expect_flag("scasd not bad", 64, scasd, 1, C_BAD, 0);
        expect_flag("int3 not bad", 64, int3, 1, C_BAD, 0);
        expect_flag("xlat not bad", 64, xlat, 1, C_BAD, 0);
        expect_flag("loopne rel8 not bad", 64, loopne8, 2, C_BAD, 0);
        expect_flag("loope rel8 not bad", 64, loope8, 2, C_BAD, 0);
        expect_flag("in al,imm8 not bad", 64, in_al_ib, 2, C_BAD, 0);
        expect_flag("in eax,imm8 not bad", 64, in_eax_ib, 2, C_BAD, 0);
        expect_flag("out imm8,al not bad", 64, out_ib_al, 2, C_BAD, 0);
        expect_flag("out imm8,eax not bad", 64, out_ib_eax, 2, C_BAD, 0);
        expect_flag("in al,dx not bad", 64, in_al_dx, 1, C_BAD, 0);
        expect_flag("in eax,dx not bad", 64, in_eax_dx, 1, C_BAD, 0);
        expect_flag("out dx,al not bad", 64, out_dx_al, 1, C_BAD, 0);
        expect_flag("out dx,eax not bad", 64, out_dx_eax, 1, C_BAD, 0);
        expect_flag("hlt not bad", 64, hlt, 1, C_BAD, 0);
        expect_flag("cmc not bad", 64, cmc, 1, C_BAD, 0);
        expect_flag("cli not bad", 64, cli, 1, C_BAD, 0);
        expect_flag("sti not bad", 64, sti, 1, C_BAD, 0);
        expect_flag("retf imm16 not bad", 64, retf_ib, 3, C_BAD, 0);
        expect_flag("retf not bad", 64, retf, 1, C_BAD, 0);
        expect_flag("iret not bad", 64, iret, 1, C_BAD, 0);
    }
    {
        // LSS/LFS/LGS are plain ModRM instructions in every mode.
        static const uint8_t lss[] = { 0x0F, 0xB2, 0x00 };
        static const uint8_t lfs[] = { 0x0F, 0xB4, 0x00 };
        static const uint8_t lgs[] = { 0x0F, 0xB5, 0x00 };
        expect_flag("lss eax,[rax] not bad", 64, lss, 3, C_BAD, 0);
        expect_flag("lfs eax,[rax] not bad", 64, lfs, 3, C_BAD, 0);
        expect_flag("lgs eax,[rax] not bad", 64, lgs, 3, C_BAD, 0);
    }
    {
        // XA_I64 marks the 16/32-bit-only forms: they stay legal there
        // (C_BAD clear, C_I64 set) and the decoder still rejects them in
        // 64-bit through XA_I64, not through XA_BAD.
        static const uint8_t push_es[] = { 0x06 };
        static const uint8_t pop_es[] = { 0x07 };
        static const uint8_t push_cs[] = { 0x0E };
        static const uint8_t push_ss[] = { 0x16 };
        static const uint8_t pop_ss[] = { 0x17 };
        static const uint8_t push_ds[] = { 0x1E };
        static const uint8_t pop_ds[] = { 0x1F };
        static const uint8_t daa[] = { 0x27 };
        static const uint8_t das[] = { 0x2F };
        static const uint8_t aaa[] = { 0x37 };
        static const uint8_t aas[] = { 0x3F };
        static const uint8_t pusha[] = { 0x60 };
        static const uint8_t popa[] = { 0x61 };
        static const uint8_t bound[] = { 0x62, 0x00 };
        static const uint8_t grp1_82[] = { 0x82, 0xC0, 0x00 };
        static const uint8_t callf[] = { 0x9A, 0, 0, 0, 0, 0, 0 };
        static const uint8_t les[] = { 0xC4, 0x00 };
        static const uint8_t lds[] = { 0xC5, 0x00 };
        static const uint8_t into[] = { 0xCE };
        static const uint8_t aam[] = { 0xD4, 0x0A };
        static const uint8_t aad[] = { 0xD5, 0x0A };
        static const uint8_t jmpf[] = { 0xEA, 0, 0, 0, 0, 0, 0 };
        expect_flag("push es (32) not bad", 32, push_es, 1, C_BAD, 0);
        expect_flag("push es (32) C_I64", 32, push_es, 1, C_I64, 1);
        expect_flag("pop es (32) not bad", 32, pop_es, 1, C_BAD, 0);
        expect_flag("push cs (32) not bad", 32, push_cs, 1, C_BAD, 0);
        expect_flag("push cs (16) not bad", 16, push_cs, 1, C_BAD, 0);
        expect_flag("push cs (16) C_I64", 16, push_cs, 1, C_I64, 1);
        expect_flag("push ss (32) not bad", 32, push_ss, 1, C_BAD, 0);
        expect_flag("pop ss (32) not bad", 32, pop_ss, 1, C_BAD, 0);
        expect_flag("push ds (32) not bad", 32, push_ds, 1, C_BAD, 0);
        expect_flag("pop ds (32) not bad", 32, pop_ds, 1, C_BAD, 0);
        expect_flag("daa (32) not bad", 32, daa, 1, C_BAD, 0);
        expect_flag("daa (32) C_I64", 32, daa, 1, C_I64, 1);
        expect_flag("das (32) not bad", 32, das, 1, C_BAD, 0);
        expect_flag("aaa (32) not bad", 32, aaa, 1, C_BAD, 0);
        expect_flag("aaa (32) C_I64", 32, aaa, 1, C_I64, 1);
        expect_flag("aaa (16) not bad", 16, aaa, 1, C_BAD, 0);
        expect_flag("aas (32) not bad", 32, aas, 1, C_BAD, 0);
        expect_flag("pusha (32) not bad", 32, pusha, 1, C_BAD, 0);
        expect_flag("pusha (32) C_I64", 32, pusha, 1, C_I64, 1);
        expect_flag("popa (32) not bad", 32, popa, 1, C_BAD, 0);
        expect_flag("bound (32) not bad", 32, bound, 2, C_BAD, 0);
        expect_flag("bound (32) C_I64", 32, bound, 2, C_I64, 1);
        expect_flag("82 /0 (32) not bad", 32, grp1_82, 3, C_BAD, 0);
        expect_flag("call far (32) not bad", 32, callf, 7, C_BAD, 0);
        expect_flag("les (32) not bad", 32, les, 2, C_BAD, 0);
        expect_flag("les (32) C_I64", 32, les, 2, C_I64, 1);
        expect_flag("lds (32) not bad", 32, lds, 2, C_BAD, 0);
        expect_flag("into (32) not bad", 32, into, 1, C_BAD, 0);
        expect_flag("aam (32) not bad", 32, aam, 2, C_BAD, 0);
        expect_flag("aad (32) not bad", 32, aad, 2, C_BAD, 0);
        expect_flag("jmp far (32) not bad", 32, jmpf, 7, C_BAD, 0);
        expect_fail("push es invalid in 64", 64, push_es, 1);
        expect_fail("pop es invalid in 64", 64, pop_es, 1);
        expect_fail("push cs invalid in 64", 64, push_cs, 1);
        expect_fail("push ss invalid in 64", 64, push_ss, 1);
        expect_fail("pop ss invalid in 64", 64, pop_ss, 1);
        expect_fail("push ds invalid in 64", 64, push_ds, 1);
        expect_fail("pop ds invalid in 64", 64, pop_ds, 1);
        expect_fail("daa invalid in 64", 64, daa, 1);
        expect_fail("das invalid in 64", 64, das, 1);
        expect_fail("aas invalid in 64", 64, aas, 1);
        expect_fail("pusha invalid in 64", 64, pusha, 1);
        expect_fail("popa invalid in 64", 64, popa, 1);
        expect_fail("bound invalid in 64", 64, bound, 2);
        expect_fail("82 /0 invalid in 64", 64, grp1_82, 3);
        expect_fail("call far invalid in 64", 64, callf, 7);
        expect_fail("into invalid in 64", 64, into, 1);
        expect_fail("aam invalid in 64", 64, aam, 2);
        expect_fail("aad invalid in 64", 64, aad, 2);
        expect_fail("jmp far invalid in 64", 64, jmpf, 7);
    }
    {
        // Still-illegal group entries keep C_BAD.
        static const uint8_t grp5_7[] = { 0xFF, 0xF8 };
        static const uint8_t ud1[] = { 0x0F, 0xB9, 0x00 };
        // Far CALL (/3) and far JMP (/5) read their target from memory only,
        // so the mod=3 encodings of those two reg values are not instructions.
        // The near CALL/JMP r/m forms (/2 and /4) take a register operand, and
        // /3 and /5 stay legal with a memory operand.
        static const uint8_t far3_r0[] = { 0xFF, 0xD8 };
        static const uint8_t far3_r7[] = { 0xFF, 0xDF };
        static const uint8_t far5_r0[] = { 0xFF, 0xE8 };
        static const uint8_t far5_r7[] = { 0xFF, 0xEF };
        static const uint8_t near2[] = { 0xFF, 0xD0 };
        static const uint8_t near4[] = { 0xFF, 0xE0 };
        static const uint8_t far3_m[] = { 0xFF, 0x18 };
        static const uint8_t far5_m[] = { 0xFF, 0x28 };
        expect_flag("FF /7 still bad", 64, grp5_7, 2, C_BAD, 1);
        expect_flag("0F B9 UD1 still bad", 64, ud1, 3, C_BAD, 1);
        expect_flag("FF /3 m3 bad", 64, far3_r0, 2, C_BAD, 1);
        expect_flag("FF /3 m3 rm7 bad", 64, far3_r7, 2, C_BAD, 1);
        expect_flag("FF /5 m3 bad", 64, far5_r0, 2, C_BAD, 1);
        expect_flag("FF /5 m3 rm7 bad", 64, far5_r7, 2, C_BAD, 1);
        expect_flag("FF /3 m3 bad (32)", 32, far3_r0, 2, C_BAD, 1);
        expect_flag("FF /2 m3 not bad", 64, near2, 2, C_BAD, 0);
        expect_flag("FF /4 m3 not bad", 64, near4, 2, C_BAD, 0);
        expect_flag("FF /3 m not bad", 64, far3_m, 2, C_BAD, 0);
        expect_flag("FF /5 m not bad", 64, far5_m, 2, C_BAD, 0);
    }

    // 16-bit
    {
        static const uint8_t add16[] = { 0x01, 0xC0 };
        expect_len("add ax,ax (16)", 16, add16, 2, 2);
    }
    {
        static const uint8_t movoff[] = { 0xA1, 0x00, 0x10 };
        expect_len("mov ax,[moffs16]", 16, movoff, 3, 3);
    }

    // truncated
    {
        static const uint8_t cut[] = { 0x48, 0xB8, 0x01 };
        expect_fail("truncated mov rax,imm64", 64, cut, 3);
    }

    printf("\n%d failure(s)\n", g_fail);
    return g_fail ? 1 : 0;
}
