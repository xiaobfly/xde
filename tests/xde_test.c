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
    int got = xde_disasm_buf(b, 15, &d, mode);
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
                fail("mov rax,[rip+0]", "missing C_RIPREL");
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
        expect_len("bound eax,[eax] (32)", 32, bound, 2, 2);
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
        expect_set("rex2 lea r16d,[rax]", 64, egpr_lea, 4, 3, XSET2_R16, 1);
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

        xde_sprintset2(buf, XSET2_ALL | 0x10000000000ULL);
        if (strcmp(buf, "???") != 0) {
            fail("sprintset2 undef subset", buf);
        } else {
            printf("ok %-28s %s\n", "sprintset2 undef subset", buf);
        }
    }

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
