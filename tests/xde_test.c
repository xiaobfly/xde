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
        // and there is no source operand.
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
        static const uint8_t mov_store[] = { 0xC7, 0x04, 0x24, 0x00, 0x00, 0x00, 0x00 };
        expect_flag("xbegin not bad", 64, xbegin, 6, C_BAD, 0);
        expect_flag("xabort not bad", 64, xabort, 3, C_BAD, 0);
        expect_flag("mov [rsp],imm32 not bad", 64, mov_store, 7, C_BAD, 0);
        // Only the F8 ModR/M is XBEGIN/XABORT; every other /7 stays invalid.
        expect_flag("C7 FA still bad", 64, xbegin_bad, 6, C_BAD, 1);
        expect_flag("C6 FA still bad", 64, xabort_bad, 3, C_BAD, 1);
    }
    {
        // 0F 00 / 0F 01 keep the whole-set XA_UNDEF model, so their sets stay
        // unassertable; C_UNDEF is the only observable anchor.
        static const uint8_t sldt[] = { 0x0F, 0x00, 0xC0 };
        static const uint8_t smsw[] = { 0x0F, 0x01, 0xE0 };
        expect_flag("0F 00 undef", 64, sldt, 3, C_UNDEF, 1);
        expect_flag("0F 01 undef", 64, smsw, 3, C_UNDEF, 1);
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
