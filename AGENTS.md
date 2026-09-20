# Repository Guidelines

XDE 2.00（作者 Fyyre）——eXtended disassembler engine。覆盖 x86 / x86-64 / VEX / EVEX / XOP 的**指令长度解码、拆分/合并、源/目标对象集分析**库。是 z0mbie XDE 1.02 的继任者（1.02 原始源码与设计文档作为参考保留在 `xde102/`，不参与构建）。

| 项 | 值 |
|------|------|
| 语言 | 纯 C11（MSVC `/std:c11` + `CompileAsC`） |
| 许可证 | MIT，Copyright (c) 2026 James (Fyyre)（`LICENSE:1-3`） |
| 构建 | MSVC only：`build.bat`（脚本）或 `msvc/xde.sln`（IDE） |
| 测试 | `tests/xde_test.c`，自研 harness，退出码 0/1 |
| 外部依赖 | 无（生成器只用 Python 标准库） |
| 生成物 | `src/xdetbl.c`（已入库，仅改生成器时才需重跑） |
| 产物 | 仅 `xde_test.exe`；**不产出 `.lib`/`.dll`** |
| CI | 无 |

## Project Overview

本项目**不是**文本反汇编器——它不产出助记符字符串。三件事：

1. 计算指令**长度**（上限 15 字节 = `XDE_MAXLEN`，Intel 限制）；
2. 把指令**拆分**成 `struct xde_instr`：前缀、REX/VEX/EVEX/XOP/REX2 头、opcode map、ModR/M、SIB、位移、立即数；
3. 输出 `src_set` / `dst_set` **对象集位掩码**（读/写了哪些 GPR、标志、内存、I/O），APX 扩展寄存器 `r16-r31` 另记在第二字 `src_set2` / `dst_set2`。

反向的 `xde_asm` 把结构拼回字节。`src/xde_text.c` 只把 flag / 对象集渲染成可读串（调试用）。

设计取舍（沿自 1.02，见 `xde102/xde.txt:16-44`）：

- **不区分** segment / FPU / MMX / XMM / YMM / ZMM / CR / DR / K 寄存器，统一折叠为 `XSET_OTHER` 一个位；
- **不区分**内存地址。`mov [eax], ebx` 与 `push ecx` 都只给 `XSET_MEM`。理由是面向静态文件分析，寄存器值未知，无法判断 `eax == esp` 之类的别名；
- 源集与目标集**之间没有覆盖关系**（作者明确拒绝 "Permutation conditions" 那套语义）。

**非目标**：不输出助记符、不做反汇编美化、不做数据流分析、不做多指令串扫、不做符号/重定位处理。

**API 一览**（`include/xde.h:282-296`，8 个函数，全部 `__cdecl`，无 export/visibility 宏）：

| 函数 | 语义 |
|------|------|
| `int xde_disasm(const uint8_t *opcode, struct xde_instr *diza)` | 64 位模式解码（`src/xde.c:1048`） |
| `int xde_disasm_ex(const uint8_t *opcode, struct xde_instr *diza, unsigned mode)` | 指定 16/32/64（`src/xde.c:1043`） |
| `int xde_disasm_buf(const uint8_t *opcode, unsigned max_len, struct xde_instr *diza, unsigned mode)` | 额外限制读取上限（`src/xde.c:626`） |
| `int xde_asm(uint8_t *opcode, const struct xde_instr *diza)` | 结构 → 字节；`return xde_asm_buf(opcode, XDE_MAXLEN, diza);` 的包装（`src/xde.c:1133`） |
| `int xde_asm_buf(uint8_t *opcode, unsigned max_len, const struct xde_instr *diza)` | 结构 → 字节，额外限制写入上限；装不下返回 0（`src/xde.c:1082`） |
| `void xde_sprintfl(char *output, uint64_t fl)` | flag → 串，缓冲区 ≥256 字节（`src/xde_text.c:7`，实测最坏 227 字节） |
| `void xde_sprintset(char *output, uint64_t set)` | 对象集 → 串，缓冲区 ≥256 字节（`src/xde_text.c:51`，实测最坏 79 字节） |
| `void xde_sprintset2(char *output, uint64_t set2)` | 第二对象集字 → 串，缓冲区 ≥256 字节（声明 `include/xde.h:296`，实现 `src/xde_text.c:132`，实测最坏 97 字节） |

**返回值契约**：解码返回指令长度，`0` = 失败（截断 / 该模式下非法 / undefined）。编码返回写入字节数，`xde_asm_buf` 在结构体所需字节数 `> max_len` 时返回 `0`（`xde_asm` 给的是 `XDE_MAXLEN`，所以除空指针外永远成功）。没有错误码枚举、没有 errno、没有 out-param 状态。

用法（`README.md:60-67` 原文）：

```c
#include "xde.h"

struct xde_instr diza;
int n = xde_disasm(ptr, &diza);				// 64-bit mode
n = xde_disasm_ex(ptr, &diza, XDE_MODE_32); // 16 / 32 / 64
n = xde_disasm_buf(ptr, max_len, &diza, XDE_MODE_64);
int m = xde_asm(out, &diza);				// at most 15 bytes
m = xde_asm_buf(out, out_len, &diza);		// 0 if the struct needs more
```

**接入方式**：仓库不打包库，消费者直接把 `src/xde.c`、`src/xdetbl.c`、`src/xde_text.c` 编进自己的目标（`-Iinclude -Isrc`）。若要出 DLL，需自行补 `__declspec(dllexport)` / `.def`——头文件里没有任何导出宏。

## Architecture & Data Flow

### 调用链

```
consumer
  └─ xde_disasm / xde_disasm_ex            (src/xde.c:1048 / :1043, 只差默认参数)
       └─ xde_disasm_buf(ptr, max_len, d, mode)   ← 唯一真正的入口 (src/xde.c:626-1041)
            ├─ xde_attr[map][opcode]      (查表, :915)  ← src/xdetbl.c 生成
            ├─ xde_group[gid][modrm.reg]  (二次查表, :943)
            └─ parse_modrm (:500) → apply_modrm_usage (:287)
                                  → apply_usage_special (:146)
                                  → apply_implicit_gp (:410)
  └─ xde_asm(out, d) → xde_asm_buf(out, XDE_MAXLEN, d)   (src/xde.c:1133 / :1082-1131, 纯字节重组)
```

`src/xde.c` 共 18 个函数：5 个导出 + 13 个 `static`。无全局可变状态，无堆分配。

### 解码流水线（分阶段行号）

| # | 阶段 | 行 |
|---|------|-----|
| 1 | 入参守卫：空指针 → 0；`mode ∉ {16,32,64}` → 0；`max_len` 0/超限一律夹到 15 | `:637-644` |
| 2 | `memset` 清零 + 预置 `mode` / `defaddr` / `defdata` | `:646-649` |
| 3 | 游标初始化 `beg`/`p`/`end` | `:651-653` |
| 4 | `C_BAD` 启发式 | `:655-659` |
| 5 | 遗留前缀循环 | `:661-708` |
| 6 | 取下一个字节 | `:710-712` |
| 7 | REX（`40-4F`，仅 64 位） | `:714-726` |
| 8 | REX2（`D5`，仅 64 位） | `:728-759` |
| 9 | EVEX（`62`） | `:761-810` |
| 10 | VEX（`C4`/`C5`） | `:812-870` |
| 11 | XOP（`8F`） | `:872-905` |
| 12 | 遗留 opcode + `enc = XDE_ENC_LEGACY` | `:907-910` |
| 13 | 汇合点标签 `got_opcode:` | `:912` |
| 14 | map 越界检查 + `attr = xde_attr[map][mop]` | `:913-915` |
| 15 | 三种拒绝：`XA_INVALID` → 0；`XA_I64 && mode==64` → 0；`XA_O64 && mode!=64` → 0 | `:917-922` |
| 16 | `apply_attr_flags`：`XA_*` → `C_*` 映射（**`C_REL` 的唯一来源**，收尾不再重复置位） | `:924`（实现 `:123-144`） |
| 17 | `XA_GROUP` 强制 `XA_MODRM`，并就地补置 `C_MODRM`（`apply_attr_flags` 已经跑过） | `:926-932` |
| 18 | peek ModR/M，取 `reg = (mpeek >> 3) & 7` | `:934-939` |
| 19 | group 二次查表：`xde_group[gid][reg]` 再跑一次 `apply_attr_flags` | `:940-946` |
| 20 | 硬编码特例：移位组 `C0 C1 D0-D3` 写 FL（`RCL`/`RCR` 另读 FL、`D2`/`D3` 另读 CL）／`C6 C7 8F` 的 `C_BAD`／`F6`/`F7` 的标志与 `MUL`/`DIV` 累加器规则 | `:948-986` |
| 21 | `parse_modrm` + `apply_modrm_usage` | `:988-991` |
| 22 | MOFFS 路径（非 ModR/M 的 `A0-A3` 等） | `:992-1005` |
| 23 | `apply_usage_special` / `apply_implicit_gp` | `:1007-1008` |
| 24 | `XA_UNDEF` → `src_set = dst_set = XSET_UNDEF`、`src_set2 = dst_set2 = XSET2_ALL`（**赋值，非 OR**） | `:1010-1014` |
| 25 | `imm_bytes` 定长 → 拷贝到 `data_b` + `C_DATA*` | `:1017-1030` |
| 26 | 收尾：`len = cur.p - opcode`；`0` 或 `>15` → 0；置 `len`；返回 | `:1034-1040` |

**3DNow 没有特例分支**：表把尾随 opcode 字节建模成 `XA_IMM_IB`，`XA_3DNOW` 只负责置 `C_3DNOW`（收尾注释 `:1032-1033`，现在是准确描述而非待办）。

**移位组与组 3 都写标志**：legacy map 的 `C0`/`C1`/`D0-D3`（八个操作）一律 `dst_set |= XSET_FL`；`reg == 2 || 3`（`RCL`/`RCR`）另加 `src_set |= XSET_FL`（读 CF），`D2`/`D3` 另加 `src_set |= XSET_CL`。`F6`/`F7` 在 `reg != 2` 时 `dst_set |= XSET_FL`（`NOT`(/2) 不写标志），`/4`-`/7` 的累加器规则不变（`:948-986`）。这与 ALU 路径一致（`:347`/`:351`/`:434`/`:468`/`:474`）：凡写标志都要在 `dst_set` 里出现 `XSET_FL`。

**`rex` 口径统一**：`apply_modrm_usage`（`:290`）与 `apply_implicit_gp`（`:413`）都用 `(diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY)`，8 位寄存器命名不会因走哪一趟而不同。`gp_set` 对 `sz ∉ {1,2,4,8}` 返回 `XSET_OTHER`（`:89`），不会冒充 64 位。

**字节读取纪律**：所有读取都走 `cur_left`（`:17`）/ `get_byte`（`:24`）/ `peek_byte`（`:32`）。`cur_left` 双重夹取 `min(end - p, beg + XDE_MAXLEN - p)`；由于 `max_len` 已夹到 15，第二项实际永远不是较小者（死代码，但无害）。

**`C_BAD` 的来源**（共 4 类）：

1. 首两字节构成的 16 位小端字等于 `0x0000` 或 `0xFFFF`（`:655-659`）；
2. 同一类遗留前缀**重复出现**（`66`/`67`/段/`F2F3`/`F0`，`:671`/`:682`/`:690`/`:697`/`:704`）；
3. 表属性 `XA_BAD`（映射见 `:130`）；
4. `C6`/`C7`/`8F` 在 legacy map 下 `reg != 0`（`:957-959`）。

**前缀语义**：`66` 翻转 `defdata` 2↔4（`:669`）；`67` 在 64 位翻转 `defaddr` 8↔4、其余模式 2↔4（`:677-680`）；段前缀存 `p_seg`、`F2/F3` 存 `p_rep`、`F0` 存 `p_lock`。**每类只保留最后见到的字节**。注意前缀循环在 REX 判定**之前**跑完且 REX 只判一次，所以 `48 66 90` 会把 `48` 当 REX、再把 `66` 当 opcode——解码侧不拒绝非规范前缀顺序，但**重编码会按 SDM 组序规范化**（见 Known Gaps）。

### 编码类分派与歧义消解

前缀字节有歧义，各分支的判定门槛（每个都只看一两个前瞻字节）：

| 字节 | 两种解释 | 判定门槛 | 行 |
|------|----------|----------|-----|
| `62` | EVEX / BOUND | `peek(1..3)` 全成功 **且** `(b2 & 0x04)` **且**（`mode == 64` 或 `(b1 & 0xC0) == 0xC0`）→ 否则 BOUND | `:763-764` |
| `C4` `C5` | VEX2/VEX3 / LES/LDS | `mode == 64` 或 `(b1 & 0xC0) == 0xC0`（`C4` 还需第三字节）→ 否则 LES/LDS | `:812-815` |
| `8F` | XOP / POP r/m | `(b1 & 0x1F) >= 8` **且**（`mode == 64` 或 `(b1 & 0xC0) == 0xC0`）→ 否则 `8F /0` = POP r/m | `:876` |
| `D5` | REX2 (APX) / AAD | 仅 64 位且 `b == 0xD5` → 否则按 AAD 走 legacy | `:728-759` |

三个向量前缀门槛（`62` / `C4`+`C5` / `8F`）写法一致：64 位下无条件成立，16/32 位下要求 `mod == 11b` 或等价的 `0xC0` 掩码；因此 16/32 位里 `8F 08`（mod ≠ 11）不再被当作 XOP，而是走非法 POP（长度 2，`C_BAD` 置位）。

门槛失败即落到 `parse_legacy_opcode`（定义 `:590`，调用 `:907-908`）。这就是 README 那句「`C4`/`C5`/`62`/`8F` 只有在后随字节符合前缀形式时才是 VEX/EVEX/XOP」的实现。

各编码类写回的结构字段：

| 类 | `enc` | `nvex` | `vex[]` | `map` 来源 | 其他 |
|----|-------|--------|---------|-----------|------|
| REX | 不改（仍是 LEGACY） | — | — | — | `rex` + `rex_w/r/x/b`，`C_REX` |
| REX2 | `XDE_ENC_REX2` | 2 | `D5, b1` | `(b1 & 0x80) ? 0F : LEGACY` | 读 bit6/5/4 → `rex_r4`/`rex_x4`/`rex_b4`，并置 `diza->rex = 0x40 \| (b1 & 0x0F)`（对寄存器命名等价于 REX）；`C_REX2 \| C_REX` |
| EVEX | `XDE_ENC_EVEX` | 4 | `62, P0, P1, P2` | `b1 & 7`（→ map 4-7） | `C_EVEX \| C_VEX`，`evex_r2/z/b/aaa`，`vex_vvvv` 含 `V'` 位；清 `p_66`/`p_rep` |
| VEX | `XDE_ENC_VEX2`(C5) / `XDE_ENC_VEX3`(C4) | 2 / 3 | 全前缀头 | `0F`(C5) / `b1 & 0x1F`(C4) | `C_VEX`；清 `p_66`/`p_rep` |
| XOP | `XDE_ENC_XOP` | 3 | `8F, b1, b2` | `b1 & 0x1F`（→ map 8-10） | `C_XOP`；清 `p_66`/`p_rep`（`:896-897`）；只写 `opcode`/`map`（`:901-902`） |

**`opcode2`/`opcode3` 的写入规则**：只有 `map ∈ {XDE_MAP_0F, XDE_MAP_0F38, XDE_MAP_0F3A}` 时才写（VEX `:859-867`、EVEX `:798-806`）；EVEX 的 map 4-7、VEX 的 map 7（`VEX7`）、XOP 的 map 8/9/A 都不写。XOP 与 VEX/EVEX 同一规则，不是缺口——`opcode`/`opcode2`/`opcode3` 只描述 `0F`/`0F 38`/`0F 3A` 这条链。

### 表驱动层

`src/xdetbl.c`（生成，414 行）提供两张 `const` 表：

| 符号 | 行 | 形状 | 含义 |
|------|-----|------|------|
| `xde_attr` | `:5-380` | `uint32_t [11][256]` | map × opcode → `XA_*` 属性位 |
| `xde_group` | `:382-413` | `uint32_t [30][8]` | group id × ModR/M.reg → 追加的 `XA_*` 位 |

`XA_*` 位布局（`src/xdetbl.h`）：

| 区段 | 位 | 内容 |
|------|-----|------|
| 属性标志 | 0-20 | 21 个：`XA_MODRM`…`XA_RET`（`src/xdetbl.h:9-29`） |
| 立即数类型 | 21-24 | `XA_IMM_SHIFT = 21`，4 位掩码；NONE 0 / IB 1 / IW 2 / IZ 3 / IV 4 / AP 5 / ENTER 6 / ID 7（`:31-40`） |
| group id | 25-31 | `XA_GRP_SHIFT = 25`，7 位掩码；`XA_GRP(n)` 打包，`XA_GRP_ID(a)` 取回（`:42-45`） |

**整 32 位已占满**——新增一个属性位必须先把 `attr` 拓宽到 64 位（或复用现有位），并同步 4 处，见 Change Workflow。

`XA_*` 到 `C_*` 的映射在 `apply_attr_flags`（`src/xde.c:123-144`）：17 条一对一 OR，**只增不减**（读-改-写 `diza->flag`）。`XA_VVVV_GPR` 与 group 位不经此函数（`XA_VVVV_GPR` 在 `:312`/`:327`/`:364`/`:541` 直接读，`XA_GRP_ID` 只在 `:941` 读）。

立即数尺寸表（`imm_bytes`，`src/xde.c:101-121`）：

| kind | 字节数 |
|------|--------|
| `XA_IMM_IB` | 1 |
| `XA_IMM_IW` | 2 |
| `XA_IMM_IZ` | 64 位且带 `XA_F64` → 4；否则 `defdata == 2 ? 2 : 4` |
| `XA_IMM_IV` | `defdata`（2/4/8） |
| `XA_IMM_AP` | 4（ptr16:16）/ 6（ptr16:32） |
| `XA_IMM_ENTER` | 3（iw + ib） |
| `XA_IMM_ID` | 4 |
| 其他 | 0 |

尺寸→标志：1 → `C_DATA1`，2 → `C_DATA2`，**3 → `C_DATA1 \| C_DATA2`**，4 → `C_DATA4`，8 → `C_DATA8`，**6 → `C_DATA4 \| C_DATA2`**（`:1024-1029`）。

### 对象集推导（三趟）

1. `apply_modrm_usage`（`:287-409`）——ModR/M 的 reg/r/m 字段。`mod == 3` 走寄存器分支，否则内存分支。`rex` 启发式在 `:290`：`rex != 0 || enc != LEGACY`（即 VEX/EVEX/XOP/REX2 一律当作「有 REX」，影响 8 位寄存器命名）。reg 字段带扩展位组成 `regx = rex_r4<<4 | rex_r<<3 | reg`（`:297-298`），r/m 侧同理用 `rex_b4`（`:357-358`）；`int setcc = (map == 0F && c == 0x0F && c2 ∈ 90..9F)` 在 `:300`。`mod == 3` 的寄存器分支里，`dst` 白名单 = ALU / `MOV r/m` / 移位 / `F6`/`F7` / `FE`/`FF` / `80-83` / **`C6`/`C7`** / **`SETcc`**（`:376-385`），而 `src` 赋值排除 `0x8D` **以及 MOV 存储形式 `0x88`/`0x89`/`0xC6`/`0xC7` 与 `SETcc`**（`:370-371`）——这几类只写 r/m，不读；内存分支同样处理（`src` 排除 `:393-396`、`dst` 白名单 `:397-404`）。
2. `apply_usage_special`（`:146-286`）——隐式操作数：REP/串操作（`A4-A7`/`AA-AF`/`6C-6F`/`AC-AD`）、IO（`E4-E7`/`EC-EF`）、`SAHF/LAHF`、`CBW/CWD`、`AAA/AAS`、`AAM/AAD`、`PUSHA/POPA`、`PUSH/POP sreg`、`XLAT`、`ENTER/LEAVE`、`MOV` 段寄存器，以及 0F map 的 `CPUID`/`SHLD/SHRD`/`LSS` 等。**其 `attr` 形参未使用**（`(void)attr;` `:284`）。标志规则**按指令区分**（`:244-253`）：`A6/A7`（CMPS）、`AE/AF`（SCAS）、`FC/FD`（CLD/STD）→ `dst_set |= XSET_FL`；`p_rep` 且 `A6/A7/AE/AF` → `src_set |= XSET_FL`（REP 循环测 ZF）；`MOVS/STOS/LODS/INS/OUTS` 完全不动标志，**DF 有意不作为源**。`REP` 本身只把 `CX/ECX/RCX` 计入 `src`+`dst`（`:155-160`），不再无条件置 `XSET_FL`。0F 段的 `SETcc`（`c2 ∈ 90..9F`）读标志：`src_set |= XSET_FL`（`:277-279`）。I/O 的 DX 端口形式（`EC`/`ED`/`EE`/`EF`）四种都读 `DX`，因此**一律**记进 `src_set`——`EE`/`EF`（`OUT DX,AL/AX`）不进 `dst_set`；立即数端口形式 `E4-E7` 不含 `DX`（`:222-230`）。
3. `apply_implicit_gp`（`:410-499`）——opcode 隐含的 GPR：`INC/DEC r`、`PUSH/POP r`、`XCHG r8,eAX`、`MOV r,Iv`、`ALU AL/eAX, Iv`、`BSWAP`，外加 `XA_PUSH`/`XA_POP` 的栈列（`stack_set`：16 → `XSET_SP`，32 → `XSET_ESP`，64 → `XSET_RSP`，`:489-497`）。其 `rex` 判定在 `:413`，用的是与 `:290` **相同的** `(diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY)`。

**REX2 的 r/m 是 GPR，不是向量寄存器**：`parse_modrm` 的 SIB 分支（`:540-542`）与 `apply_modrm_usage` 的内存分支（`:405-406`）在判断「是否按向量寄存器记 `XSET_OTHER`」时都会排除 `XDE_ENC_REX2`；`mod == 3` 分支同样排除（`:363-367`）。另外 `parse_modrm` 的两个「无基址」判定——SIB 的 `mod == 0 && base == 5`（`:538`）与 `mod == 0 && rm == 5`（`:557`）——在 `rex_b4` 置位时不再成立，此时它指的是真寄存器 r21，不是 disp32。

`gp_set`（`:43-90`）的寄存器列映射：`reg > 31 → XSET_OTHER`（**解码路径不可达**，`reg` 由 5 位扩展位拼出，上限 31）；`reg >= 16` 交给其第 4 个形参 `uint64_t *egpr`——EGPR 写进**第二对象集字**（`*egpr |= XSET2_R16 << (reg - 16)`，`:66-72`），固定寄存器处传 `NULL`；`reg >= 8` 分两支：首字仍返回**宽度无关**的 `XSET_R8 << (reg-8)`，并且当 `sz <= 1`（8 位形式 r8b-r15b）时**额外**写入第二字的 `XSET2_R8B << (reg-8)`（`:73-79`）——即**叠加**而非替换：`XSET_R8..XSET_R15` 照旧，第二字另有 8 位宽度位；`sz <= 1` 时按 `rex` 选 `lo8_norex`（AL/CL/DL/BL/**AH/CH/DH/BH**）或 `lo8_rex`（AL/CL/DL/BL/**SPL/BPL/SIL/DIL**——独立的 `XSET_SPL/BPL/SIL/DIL` 位，不再复用 16 位那几位）；`sz` 2/4/8 → `w16`/`w32`/`w64`，**`sz ∉ {1,2,4,8}` 返回 `XSET_OTHER`，不冒充 64 位**（`:87-89`）。

### 编码方向（`xde_asm`）

**不做校验、不解码**；`xde_asm_buf`（`src/xde.c:1082-1131`）只多做一次容量检查，`xde_asm`（`:1133-1136`）是 `return xde_asm_buf(opcode, XDE_MAXLEN, diza);` 的薄包装。固定顺序拼接：

```
max_len 夹取：0 或 > XDE_MAXLEN 一律按 15                        (:1089-1090)
→ 计数夹到数组容量：nvex ≤ 4 / naddr ≤ 8 / ndata ≤ 8            (:1092-1095)
→ asm_size() 算所需字节数，> max_len 则 return 0                 (:1054-1080, :1097-1098)
p_lock → p_rep → p_seg → p_66 → p_67    （SDM 组序 1→2→3→4，:1105-1109）
  ├─ 若 nvex：vex[0..nvex-1] → opcode   （此路径不发 rex）
  └─ 否则：rex → opcode
             └─ 若 opcode == 0x0F：opcode2 →（若 opcode2 ∈ {38,3A}）opcode3
→ flag & C_MODRM ? modrm
→ flag & C_SIB   ? sib
→ naddr 个 addr_b[]
→ ndata 个 data_b[]
```

**容量安全**：`addrsize`/`datasize`/`nvex` 是 `uint8_t`，被写坏时解码侧的计数会越过 `addr_b[8]`/`data_b[8]`/`vex[4]`（真 UB）。`xde_asm_buf` 先把这三个计数夹到数组容量（`:1092-1095`），再用 `asm_size()`（`:1054-1080`）算出所需字节数，`> max_len` 就返回 `0`（`:1097-1098`）；`max_len == 0` 或 `> XDE_MAXLEN` 一律按 `XDE_MAXLEN` 处理（`:1089-1090`），与 `xde_disasm_buf` 的 `max_len` 处理同构。所以解码成功的指令（`len ≤ 15`）经 `xde_asm` 永远编得出来。

`p_66` 现在与其它遗留前缀一起在 `:1108` 统一发射（没有 nvex 专属分支）：这是为 REX2 准备的——REX2 是唯一**保留** `p_66`/`p_rep` 的编码类（VEX/EVEX/XOP 在解码时就清掉，`:828-829`/`:851-852`/`:791-792`/`:896-897`），而它们的 `vex[]` 头自带 pp 字段，不走 `p_66`；所以 `66 D5 …` 现在能字节级往返。

它只读 `nvex`/`vex[]`、`p_seg/p_lock/p_rep/p_67/p_66/rex`、`opcode/opcode2/opcode3`、`modrm`、`sib`、`addr_b+addrsize`、`data_b+datasize`，**完全不看 `map` / `enc` / `defdata` / `defaddr` / `len` / 对象集**。因此：

- 手搓一个 `nvex == 0` 但 `map == XDE_MAP_0F38` 的结构体，`xde_asm_buf` 不会补出 `0F 38` 前缀——它只信字节字段；
- 返回 `0` 只有两种情形：`!opcode || !diza`（`:1087-1088`），或所需字节数 `> max_len`（`:1097-1098`）；解码成功的指令不可能编出 0 字节。

### 设计不变量（改动前必读）

1. **低 32 位兼容 XDE 1.02**：`flag` / `src_set` / `dst_set` 的低 32 位语义（EAX-EDI）冻结；64 位宽度位、R8-R15、RIP、编码类标志全在高半（`include/xde.h:103-179`）。新增的 `XSET_SPL/BPL/SIL/DIL` 占用的正是 1.02 显式保留的 `XSET_rsrv1..4` 位（低位 26/27/30/31，`xde102/xde.h:102-105`），没有改变任何 1.02 已定义位的语义；`r16-r31` 放不进首字——低位冻结、高半已被 64 位宽度位 / R8-R15 / RIP 占去，仅剩 15 位空闲（bit 49-63）< 需要的 16 位——因此落在第二字 `src_set2`/`dst_set2`（`XSET2_*`）。新增能力**不得挪用低位**。
2. **零堆分配**：`struct xde_instr` 由调用方提供，游标在栈上。解码路径不得引入 `malloc`/`strdup`。
3. **失败统一返回 0**：不要引入状态枚举——`if (!n)` 遍布整个调用面。
4. **`src/xdetbl.c` 是生成物**：首行即 `Auto-generated by tools/gen_tables.py - do not edit by hand.`（`:1`），永不手改。
5. **`xde_instr` 的字段来源**：成功解码无条件写 `mode`/`defaddr`/`defdata`/`len`/`map`/`enc`/`opcode`/`flag`/`src_set`/`dst_set`/`src_set2`/`dst_set2`，以及 `rex_r4`/`rex_x4`/`rex_b4`（REX2 分支写入；非 REX2 编码保持 `memset` 后的 0，因为此时不存在 R4/X4/B4 位）；`addrsize`/`datasize`/`p_*`/`sib`/`opcode2`/`opcode3`/`vex[]`/`evex_*` 只在对应特征出现时写入（无特征时保持 `memset` 后的 0）。

## Key Directories

| 路径 | 用途 | 关键符号 |
|------|------|----------|
| `include/xde.h` | 唯一公共头（306 行）：宏词汇表 + `struct xde_instr`（`:214-276`） + 8 个 API（`:282-296`） | `XDE_MODE_*`, `XDE_ENC_*`, `XDE_MAP_*`, `C_*`, `XSET_*`, `XSET2_*` |
| `src/xde.c` | 解码器 + 编码器（1136 行），全部核心逻辑 | `xde_disasm_buf:626`, `xde_asm_buf:1082`, `xde_asm:1133`, 13 个 static 助手 |
| `src/xdetbl.c` | **机器生成**的属性表（414 行） | `xde_attr:5`, `xde_group:382` |
| `src/xdetbl.h` | 手写的表层契约（86 行） | `XA_*`, `enum xde_group_id`, `XDE_MAP_COUNT 11` |
| `src/xde_text.c` | 调试打印（165 行，3 个函数） | `xde_sprintfl:7`, `xde_sprintset:51`, `xde_sprintset2:132` |
| `tools/gen_tables.py` | 表生成器（684 行），唯一写出 `src/xdetbl.c` 的地方 | `OUT:9`, `MAPS:547`, `check_header:595`, emit 块 `:657-677` |
| `tests/xde_test.c` | 唯一测试文件（915 行） | 6 个 `expect_*` 助手, `main:161` |
| `msvc/` | VS 解决方案与工程（一个工程） | `xde.sln`, `xde.vcxproj` |
| `xde102/` | 原始 XDE 1.02 源码与设计文档，**仅参考、不构建** | `xde.txt`（242 行）, `todo`（11 行） |

## Development Commands

### 构建 + 跑测试（推荐入口）

```bat
build.bat
```

行为（`build.bat`）：`cd /d "%~dp0"` → 定位 `vcvars64.bat`：**优先问 VS 安装器** `vswhere.exe`（`-latest -prerelease -products *` 且要求 `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`，覆盖任意版本/版本分支）（`:7-14`），未命中则回落到显式路径列表 VS 18 Insiders / VS2022 Community / Professional / BuildTools（`:16-32`）→ 都找不到则 `echo Could not find vcvars64.bat` + `exit /b 1` → `call` 该脚本 → `if not exist build mkdir build`（`:37`）→ 编译（`:39`）→ `if errorlevel 1 exit /b 1` → `echo.` → 运行 `build\xde_test.exe`（`:43`）→ `exit /b %ERRORLEVEL%`（`:44`）。

**不接受任何参数**（无 `%1`/`%*`/`shift`），传入参数会被静默忽略。

编译命令行原文（`build.bat:39`）：

```bat
cl /nologo /W3 /O2 /TC /std:c11 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Isrc /Fo:build\ /Fe:build\xde_test.exe src\xde.c src\xdetbl.c src\xde_text.c tests\xde_test.c
```

- 工作目录 = 仓库根（脚本自己 `cd` 过去），所以 `include` / `src` / `tests` 相对根目录解析；
- 输出 `build\xde_test.exe`；四个 `.obj` 平铺在 `build\`（`/Fo:build\` 无文件名）。

### IDE 构建

```
msvc\xde.sln
```

工具集 `PlatformToolset` **v145**（四个配置都是）；被提示时重定位工具集（`README.md:46`）。产物 `build\<Platform>\<Configuration>\xde_test.exe`，中间文件 `build\obj\<Platform>\<Configuration>\`。工程**无任何 PreBuild/PostBuild/CustomBuild/Exec 步骤**。

### 只跑测试

```bat
build\xde_test.exe
```

退出码 `0` = 全通过，`1` = 有失败。`main` 是 `int main(void)`，**没有任何 CLI 参数**（无 filter / verbose / 单用例选择），每次运行都执行全部 203 项检查。

### 重新生成属性表

```bat
python tools\gen_tables.py
```

只写 `src/xdetbl.c`（`OUT` 定义在 `tools/gen_tables.py:9`），**不生成 `src/xdetbl.h`**——头文件是手写的，改常量要两边同步。生成前会先跑 `check_header()`（`tools/gen_tables.py:595-640`，调用 `:679`）：解析 `src/xdetbl.h` 的 `XA_*`、`enum xde_group_id`（按位置）与 `#define XDE_MAP_COUNT`，与脚本镜像常量逐名比对，不一致就列出差异并 `SystemExit(1)`，**在写文件之前**退出。脚本无参数、无 `--check`、无外部输入文件（全部表数据以 Python 字面量内联在 `:89-546`），仅依赖标准库（`import os`），需要 Python 3.7+（用了 `from __future__ import annotations` 与 f-string）。以 `newline="\n"` 写文件（`:682-683`），保持这一点以免重新生成时行尾抖动。

**普通构建不需要跑生成器**：`src/xdetbl.c` 已入库（`.gitignore` 未排除）。

### 消费者接入（仓库无 lib 目标，需自行拼装）

```bat
cl /nologo /W3 /O2 /TC /std:c11 /Iinclude /Isrc /c src\xde.c src\xdetbl.c src\xde_text.c
```

### 不存在的工具

无 CMake、无 Makefile、无 `msbuild`/`devenv` 调用、无 lint/format 配置、无 CI（`.github/workflows` / appveyor / azure-pipelines / travis / circleci 全部不存在）。

## Code Conventions & Common Patterns

- **语言**：纯 C11（`<stdint.h>`）。头文件带 `extern "C"` 守卫（`include/xde.h:10-12`、`:302-304`），可被 C++ 包含。为 nameless union 用 `#pragma warning(push/disable: 4201)` 包住（`:16-19`、`:298-300`）。
- **缩进**：**4 个空格，全树零 tab**（`src/`、`include/`、`tests/`、`tools/` 一致）。
  **本仓库没有 `.clang-format`，通用「tab + clang-format」默认规则在此不适用——请沿用 4 空格。** 例外：`msvc/xde.sln` 的 `Global` 段用 tab（VS 生成），`build.bat` 与 `.vcxproj` 用 2 空格。
- **花括号**：Allman 风格（左花括号独占一行），函数间空一行。
- **命名**：
  - 公共函数 `xde_*`，全部 `__cdecl`；
  - 内部辅助一律 `static`，无下划线前缀；
  - 宏分族：指令标志 `C_*`、表属性 `XA_*`、map `XDE_MAP_*`、编码类 `XDE_ENC_*`、对象集 `XSET_*`、group 枚举 `XG_*`、group 内建宏 `XA_GRP`/`XA_GRP_ID`/`XDE_CMD`；
  - 结构体用**非 typedef** 的 `struct xde_instr`，使用处一律写全 `struct xde_instr`；
  - 定宽类型（`uint8_t` 等）用于编码字段；`mode` 参数用 `unsigned`。
- **错误处理**：返回长度 / `0`。无状态枚举、无 errno、无 out-param 状态。
- **内存**：零堆分配。字面量表用 `static const uint8_t ...[]` 放在函数作用域内。
- **边界安全**：读字节必须走 `cur_left` / `get_byte` / `peek_byte`。
- **注释风格**：行内 `//`，多用于标注特例原因，例：`// 32-bit GP writes zero-extend in 64-bit mode.`（`:302`）、`// Vector encodings use OTHER for the reg field.`（`:306`）、`// segment override`（`:394`）。**全树没有任何 `TODO`/`FIXME`/`XXX`/`HACK`/`NOTE` 标记**（`src/` 已核）。
- **表纪律**：列宽固定，`xde_attr` 每行 8 个 `0x%08X`，每组带 `// legacy` / `// 0F` 之类行尾注释。
- **对象集的打印策略**（`xde_sprintset`，`src/xde_text.c:51-129`）：按列从宽到窄取第一个命中——含有 `RAX` 就绝不打印 `EAX`/`AX`；16 位列之后还有 8 位扩展寄存器兜底（`SP`→`SPL`、`BP`→`BPL`、`SI`→`SIL`、`DI`→`DIL`，`:92-110`）；`XSET_UNDEF` 用**子集判定** `(set & XSET_UNDEF) == XSET_UNDEF`（`:55`）特判为 `"???"`。第二对象集字由 `xde_sprintset2` 打印（`src/xde_text.c:132-165`）：`(set2 & XSET2_ALL) == XSET2_ALL`（`:142`）特判为 `"???"`，否则先逐位输出 `R16`…`R31`（`:147-152`），再逐位输出 `R8B`…`R15B`（`:153-161`）。两处都是子集判定，`set2` 带 bit ≥ 24 的杂位也不会破坏 undef 标记。

### `struct xde_instr` 字段参考

| 组 | 字段 | 说明 |
|----|------|------|
| 模式 | `mode` `defaddr` `defdata` | `mode` = 16/32/64；`defaddr` = 2/4/8（`67` 翻转）；`defdata` = 2/4/8（`66` 翻转，`REX.W`/`VEX.W` → 8） |
| 派生 | `len` `addrsize` `datasize` | 总长（1-15）；位移/moffs 字节数；立即数字节数 |
| 分类 | `enc` `map` | `XDE_ENC_*` / `XDE_MAP_*` |
| 语义 | `flag` `src_set` `dst_set` `src_set2` `dst_set2` | 64 位；`flag`/`src_set`/`dst_set` 低 32 位兼容 1.02；第二字 `src_set2`/`dst_set2`（`:228-229`）装 APX EGPR `r16-r31`（`XSET2_R16..R31`）与 8 位 `r8b-r15b` 宽度位（`XSET2_R8B..R15B`） |
| 前缀 | `p_lock` `p_66` `p_67` `p_rep` `p_seg` `rex` | 存原始字节值（无则 0） |
| 向量头 | `nvex` `vex[4]` | `nvex` = 0/2/3/4；`vex[]` 存原始前缀字节，便于 `xde_asm` 原样回放 |
| opcode | `opcode` `opcode2` `opcode3` | 首字节（双字节 `0F` 时存 `0x0F`）/ 第二 / 第三 |
| 寻址 | `modrm` `sib` | 原始字节 |
| 位展开 | `rex_w` `rex_r` `rex_x` `rex_b` `rex_r4` `rex_x4` `rex_b4` `vex_pp` `vex_l` `vex_vvvv` `evex_z` `evex_b` `evex_aaa` `evex_r2` | 已解码的位；`rex_r4`/`rex_x4`/`rex_b4`（`:247`）是 REX2 的 EGPR 扩展位（ModR/M.reg / SIB.index / r/m 的 bit 4），非 REX2 编码恒为 0；`vex_vvvv` 是**非取反**后的值（0-31） |
| 操作数 | `addr_*[]` / `data_*[]` | 两个匿名联合，各 8 字节，提供 `_b`/`_w`/`_d`/`_q` 与有符号 `_c`/`_s`/`_l`/`_q64` 视图 |

### 常量参考

模式 / 编码类 / map：

| `XDE_MODE_*` | | `XDE_ENC_*` | | `XDE_MAP_*` | |
|---|---|---|---|---|---|
| `XDE_MODE_16` 16 | | `LEGACY` 0 | | `LEGACY` 0 | `EVEX4` 4 |
| `XDE_MODE_32` 32 | | `VEX2` 1 | | `0F` 1 | `EVEX5` 5 |
| `XDE_MODE_64` 64 | | `VEX3` 2 | | `0F38` 2 | `EVEX6` 6 |
| | | `EVEX` 3 | | `0F3A` 3 | `VEX7` 7 |
| | | `XOP` 4 | | | `XOP8` 8 / `XOP9` 9 / `XOPA` 10 |
| | | `REX2` 5 | | | |

`flag` 低位（兼容 1.02，`include/xde.h:53-100`）：`C_ADDR1/2/4`、`C_MODRM`、`C_SIB`、`C_ADDR67`、`C_DATA66`、`C_UNDEF`、`C_DATA1/2/4`、`C_BAD`、`C_REL`、`C_STOP`、`C_OPSZ8`、`C_SRC_FL`/`C_DST_FL`、`C_SRC_REG`/`C_SRC_RM`/`C_DST_REG`/`C_DST_RM`、`C_SRC_ACC`/`C_DST_ACC`、`C_SRC_R0`/`C_DST_R0`、`C_PUSH`/`C_POP`，以及 `C_CMD_*`（`CALL`/`JMP`/`JCC`/`RET`，占 `0x08000000`-`0x80000000`，用 `XDE_CMD(fl)` 提取）。复合宏 `C_MOD_FL`/`C_MOD_REG`/`C_MOD_RM`/`C_MOD_ACC`/`C_MOD_R0`。

`flag` 高位（2.00 扩展，`:103-115`）：`C_DATA8` `C_ADDR8` `C_RIPREL` `C_REX` `C_VEX` `C_EVEX` `C_XOP` `C_REX2` `C_I64` `C_O64` `C_F64`（forced-64，Intel 在 64 位忽略 `Jz` 上的 `66`）`C_D64`（默认 64 位操作数，PUSH/POP）`C_3DNOW`。

对象集（`src_set` / `dst_set`）：低 32 位 GPR/标志/内存/设备（`XSET_AL`…`XSET_EDI`、`XSET_FL`、`XSET_MEM`、`XSET_OTHER`、`XSET_DEV`，及 `XSET_ALL16`/`XSET_ALL32`）；其中 `XSET_SPL`/`XSET_BPL`/`XSET_SIL`/`XSET_DIL` 占低位 26/27/30/31（`include/xde.h:145-148`）——正是 1.02 的 `XSET_rsrv1..4`，`lo8_rex` 用它区分 SPL/BPL/SIL/DIL 与 16 位的 SP/BP/SI/DI；高半为**64 位宽度位**（`XSET_RAX` = `XSET_EAX | 高半一位`，…、`XSET_ALL64`）、`XSET_R8`…`XSET_R15`（每寄存器一位，不区分宽度）、`XSET_RIP`；`XSET_UNDEF` = 全 1。

第二对象集字（`src_set2` / `dst_set2`，`include/xde.h:181-211`）含**两组**位：

- `XSET2_R16`…`XSET2_R31` —— bit 0-15，每位一个 APX EGPR，不区分宽度（同 `XSET_R8`…`XSET_R15` 的口径）；
- `XSET2_R8B`…`XSET2_R15B`（`:203-210`）—— bit 16-23，`r8b`-`r15b` 的 8 位**宽度**位，与首字 `XSET_R8`…`XSET_R15` **叠加**（首字只报宽度无关的寄存器号，第二字补宽度）。

`XSET2_ALL`（`:211`）已随之扩为 **24 位全 1**（`0x0000000000FFFFFFULL`），即 `XA_UNDEF` 时 `src_set2`/`dst_set2` 的整体赋值标记。首字剩余位不足以再放 16 个寄存器，所以 EGPR 单独占一个字。

## Important Files

| 文件 | 说明 |
|------|------|
| `include/xde.h:282-296` | 8 个导出函数声明 + 语义注释 |
| `include/xde.h:214-276` | `struct xde_instr` |
| `include/xde.h:181-211` | `XSET2_*` 词汇（第二对象集字：APX `r16-r31` + 8 位 `r8b-r15b` 宽度位） |
| `src/xde.c:626-1041` | `xde_disasm_buf` —— 改解码行为从这里读起 |
| `src/xde.c:1082-1131` | `xde_asm_buf` —— 字节重组 + 容量检查（`xde_asm` 是它的包装，`:1133-1136`） |
| `src/xde.c:1054-1080` | `asm_size` —— 估算编码所需字节数，与 `xde_asm_buf` 的落地循环必须同步 |
| `src/xde.c:763-766` / `:812-815` / `:876` | 三处编码类歧义门槛 |
| `src/xde.c:123-144` | `XA_*` → `C_*` 映射表（新增属性位必改） |
| `src/xde.c:146` / `:287` / `:410` | 三趟对象集推导 |
| `src/xde_text.c:132` | `xde_sprintset2` —— 第二对象集字的打印器 |
| `src/xde_text.c:7` | `xde_sprintfl` —— flag 打印器（新增 flag 覆盖时改这里） |
| `src/xdetbl.h:9-45` | `XA_*` 位定义 + IMM / GRP 位移 |
| `src/xdetbl.h:47-81` | `enum xde_group_id`、`XA_GRP`、`XDE_MAP_COUNT 11` |
| `tools/gen_tables.py:9` / `:89-546` / `:657-677` | 输出路径 / 全部表数据 / emit 块 |
| `tools/gen_tables.py:595-640` | `check_header` —— 生成前的 `xdetbl.h` 一致性校验（不一致即拒绝生成） |
| `tests/xde_test.c:161` | 测试 `main`，全部用例内联于此 |
| `msvc/xde.vcxproj:71-74` | 四个编译单元；`TargetName=xde_test`（app 即测试） |
| `build.bat:39` | 唯一编译命令 |

## Runtime/Tooling Preferences

- **编译器**：**仅 MSVC `cl.exe`**。`build.bat` 走 `vcvars64.bat` → **只支持 x64 宿主**（工程另有 Win32 配置，但脚本路径不覆盖）。没有 GCC / clang 构建路径，尽管 `src/` 只依赖 `stdint.h`/`string.h`，理论上可移植（跨平台需自写构建脚本）。
- **VS 版本**：脚本先用 `vswhere.exe` 定位（任意 VS 版本与版本分支，含 Insiders / BuildTools），回落到 VS 18 Insiders / VS2022 Community / Professional / BuildTools 的显式路径；工程钉 `v145` 工具集。脚本不校验工具集版本，所以装了非 v145 的 VS 也能编过，但 `.sln` 需要重定位。
- **Python**：仅生成器需要，标准库唯一依赖，Python 3.7+。
- **包管理器**：无。无锁文件、无 vendored 依赖。
- **优化/调试**：`build.bat` 硬编码 `/O2`（无 `/Od`/`/Zi`/`/MDd`），所以脚本路径无法捕获 debug-only 行为；`.vcxproj` 的 Debug 配置未显式设置 `<Optimization>`/`<RuntimeLibrary>`，走 MSVC 默认。
- **换行**：`.gitattributes` 仅 `* text=auto`（无 `eol` 强制）。生成器显式输出 LF。
- **警告级别**：`/W3`（`Level3`）。注意 `/W3` **不会**报未使用的参数（那是 `/W4` 的 C4100）——实践中已掩盖一处 `expect_enc` 忽略 `n` 形参的问题。
- **生成文件已入库**：`src/xdetbl.c` 在版本控制内（`.gitignore` 只排除 `build/`、`*.obj`、`*.exe`、`*.pdb`、`*.ilk`、`*.idb`、`*.suo`、`*.user`、`.vs/`、`x64/`、`Debug/`、`Release/`）。

## Testing & QA

### 框架与结构

**自研极简 harness，无第三方框架，无 `ASSERT`/`CHECK`/`TEST` 宏**。全部逻辑在 `tests/xde_test.c`（915 行）的单个 `int main(void)`（`:161-915`）中：93 个匿名 `{ }` 块，每块一个 `static const uint8_t` 向量紧跟 `expect_*` 调用。**零文件 IO、零 fixture、零 golden 文件**。

断言助手（括注是本轮实测的调用次数）：

| 助手 | 行 | 契约 |
|------|-----|------|
| `fail` | `:10-14` | 打印 `FAIL <name>: <msg>`，`g_fail++` |
| `hexbytes` | `:16-25` | 字节数组 → 空格分隔 `%02X` 串 |
| `expect_len(name, mode, b, n, want)` | `:27-45` | `xde_disasm_buf(b, n, ...)`，要求 `got == want` **且** `d.len == got`（42 次） |
| `expect_enc(name, mode, b, n, want_len, enc)` | `:47-65` | 要求长度 + `d.enc`。**实现里硬编码传 15，忽略形参 `n`**（`:51`）（14 次） |
| `expect_fail(name, mode, b, n)` | `:67-78` | 要求返回 `0`（4 次） |
| `expect_roundtrip(name, mode, b, n)` | `:80-113` | disasm → `xde_asm`（长度须等于 `n`）→ **`memcmp(out, b, n)` 字节级比较**（`:101`）→ 再 disasm，比较 `len` + `opcode` + `modrm`（5 次） |
| `expect_set(name, mode, b, n, sel, bit, want)` | `:117-138` | 要求解码长度 == `n`，再断言单个对象集位的存在/不存在。`sel` 0/1/2/3 = `src_set`/`dst_set`/`src_set2`/`dst_set2`，`want` 1 = 置位、0 = 未置位；成功打 `set<n> 0x… set|clear`（91 次） |
| `expect_flag(name, mode, b, n, bit, want)` | `:141-159` | 要求解码长度 == `n`，再断言单个 flag 位。`bit` 取单个 `C_*` 常量，`want` 1 = 置位、0 = 未置位；成功打 `flag 0x… set|clear`（22 次） |

### 覆盖矩阵（203 项检查）

203 = 178 次助手调用（`expect_len` 42 + `expect_enc` 14 + `expect_fail` 4 + `expect_roundtrip` 5 + `expect_set` 91 + `expect_flag` 22）+ 25 项内联检查（`C_RIPREL` 1 + Object sets 打印器 4 + 闸门/asm 区段 10 + 规范前缀 2 + `sete al` 非 undef 1 + 打印器最坏情况 3 + Coverage 区段 ud2 1 + Jcc/LOOP 区段打印器 3）。

| 区段注释 | 行 | 检查数 | 内容 |
|----------|-----|--------|------|
| `// 64-bit GP` | `:163` | 26 | nop、`xor eax,eax`、`xor rax,rax`（含往返）、`mov rax,imm64`、`mov eax,imm32`、`mov r8,imm64`、RIP 相对（+ 内联 `C_RIPREL`）、`call [rip+0]`、`call rel32`、`66 call rel32`、ret、`push rbp`、`sub rsp,0x20`、SIB、`mov r8,[rsp+0x28]`、`nop dword [rax+rax]`、endbr64、syscall、movsxd、`moffs64` ×2、`test rax,imm32`、bt、`67` 前缀 |
| `// SSE / 0F38 / 0F3A` | `:276` | 6 | movups、palignr、pshufb、crc32、movbe、3DNow `pavgusb` |
| `// VEX` | `:302` | 6 | VEX2 `vaddps`（含往返）、VEX3 `andn` ×2、`rorx`、`vzeroupper` |
| `// EVEX` | `:325` | 4 | `vaddps zmm`（含往返）、EVEX 内存形式、`vrndscaless` |
| `// XOP` | `:344` | 4 | `vfrczpd`（含往返）、`vpcomb`、`bextr` |
| `// 32-bit mode: LES vs VEX, BOUND vs EVEX` | `:359` | 9 | LES/LDS、32 位 VEX2、BOUND、`inc eax`、truncated REX、`rex nop`、aaa 非法/合法、`push es` 非法 |
| `// REX2 (APX)` | `:391` | 2 | `rex2 lea`、`rex2 imul` |
| `// Object sets: 8-bit extension registers vs high bytes, and APX EGPRs` | `:401-504` | 32 | 27 项 `expect_set` + 4 项内联打印器检查 + 1 项字节级往返：`mov spl,al`（`XSET_SPL` 置位且**不含** `XSET_SP`）、`mov ah,al`（`XSET_AH` 置位且**不含** `XSET_SPL`，钉住无 REX 时的差异）、`rex2 lea r16d,[rax]`（`dst_set2` 含 `XSET2_R16`、`dst_set` 不含 `XSET_OTHER`）、`rex2 lea r31d,[rax]`（`XSET2_R31`）、`rex2 push r16`（`src_set2` 含 `XSET2_R16`，钉住 opcode+r 的 B4）、`rex2 mov eax,[r16]`（SIB base 的 B4 → `src_set2` 含 R16，同时 `src_set` 含 `XSET_RAX`）、`rex2 add rax,r16`（`mod == 3` 下 reg 是 EGPR、r/m 是 legacy GPR，`:432-438`）；打印器输出串断言（`R16` / `R8B` / `SPL` / 解码后再打印 `R16`，`:439-468`）。`:470-504` 是同区段续块（注释 `// REX2 round-trip with a legacy prefix, MOV store forms, 8-bit r8-r15.`）：`66 D5 40 8D 00` 往返（钉 `p_66` 与 REX2 头共存）、`C6 C0 12`（`mov al,0x12` 报 dst 不报 src）、`8B C3`（`mov eax,ebx` 的 r/m 仍计入 src，反向控制）、`41 88 C0`（`mov r8b,al` → `XSET_R8` 与 `XSET2_R8B` 同时置位）、`41 88 C7`（`r15b` → `XSET2_R15B`）、`49 8B C0`（`mov rax,r8` → **无** `R8B`）、`44 8B C0`（`mov r8d,eax` → **无** `R8B`） |
| `// XOP gate in 16/32-bit, the relocated C_REL flag, and xde_asm limits` | `:506-612` | 14 | 4 次助手调用 + 10 项内联检查：`8F 08` 在 32 位 → `expect_len` 长度 2、`C_BAD` 置位；`call rel32` → `C_REL` 置位且 `C_BAD` 未置位；`xde_asm` 对 `datasize=200`/`addrsize=200`/`nvex=200` 夹到数组容量后分别返回 9/9/5；`xde_asm_buf(out,4)` 对 7 字节指令返回 0、`xde_asm_buf(out,7)` 返回 7；`xde_sprintfl` 对 `push rbp` 含 `C_PUSH`、对 `ret` 含 `C_CMD_RET`、对 `mov eax,imm32` 含 `C_DATA4`、对 RIP 相对 `mov` 含 `C_ADDR4`；`xde_sprintset2(XSET2_ALL \| 1<<40)` 打印 `???` |
| `// Canonical prefix order, and the flag intents recorded in xde102/todo.` | `:613-717` | 23 | 17 项 `expect_set` + 6 项内联检查：规范前缀顺序 2 项（`:614-634`，`67 66 90` → `66 67 90`、`64 F3 A4` → `F3 64 A4`）；REP/CMPS 标志 7 项（`:635-647`：`rep movsb` src/dst 均无 FL 且有 RCX；`rep cmpsb` src+dst 有 FL；`cmpsb` dst 有 FL、src 无 FL）；`cld`/`std` dst 有 FL 各 1 项（`:648-653`）；`SETcc` 4 项（`:654-672`：`sete al` dst 有 AL、src 有 FL、src 无 AL；`sete r8b` dst2 有 `R8B`）+ 1 项内联「`sete al` 的 `xde_sprintset(src_set)` 不是 `???`」；`SAHF`/`LAHF` 4 项（`:673-680`：`sahf` src 有 AH / dst 有 FL，`lahf` src 有 FL / dst 有 AH）；打印器最坏情况 3 项（`:681-717`：全位置位的 `allflags` → `xde_sprintfl` 227 字节、`xde_sprintset(~0ULL ^ 1<<63)` 79 字节、`xde_sprintset2(XSET2_ALL & ~XSET2_R16)` 97 字节，均断言 `< 256`） |
| `// Coverage: mode-dependent sets, implicit registers, I/O and flags.` | `:718-835` | 59 | 39 项 `expect_set` + 19 项 `expect_flag` + 1 项内联 `fail`（`ud2 sets undefined`）：模式相关栈集（`push` 64 位 → `XSET_RSP`；32 位 → `XSET_ESP` 且 `XSET_RSP & ~XSET_ESP` 清）、16 位 `movsb` → SI/DI、16 位 `mov ax,[1234]` → MEM/AX/`C_ADDR2`、`PUSHA` → EAX/EDI/ESP、I/O → DEV/DX（含 `OUT` 的负向控制）、`CPUID` → src EAX、dst EAX/EBX/ECX/EDX、`MOV` 与段寄存器互传 → OTHER、`LEAVE` → RSP/RBP、移位 → CL/AL/FL（含 `shl` 不读 FL 与 `rcl` 读 FL 的正反对照）、`F7 /0` → dst FL、`F6 /2`（`NOT`）→ 无 dst FL、标志 `C_STOP`/`C_CMD_RET`/`C_CMD_JMP`/`C_CMD_JCC`/`C_CMD_CALL`/`C_I64`/`C_REL`/`C_F64`/`C_3DNOW`/`C_OPSZ8`/`C_SIB`/`C_ADDR1`/`C_DATA2`、`67` + 64 位 mod=0/rm=5 → `C_RIPREL` 清且 `C_ADDR4` 置、`0F 0B`（UD2）→ `C_UNDEF` 且 `xde_sprintset` 输出 `"???"` |
| （无区段注释） | `:836-851` | 4 | `pop rax (8F /0)`、`test al,0x12`、`not al`、`call rax` |
| `// Jcc and LOOP/JCXZ report what they test instead of an undefined set.` | `:853-895` | 11 | 8 项 `expect_set` + 3 项内联打印器检查：`jz rel8`/`jz rel32` → `src_set` 有 `XSET_FL`，`loop` → `src_set`/`dst_set` 都有 `RCX`（且 `src_set` 不读 `FL`），`loope` → `src_set` 有 `FL`，`jcxz` → `src_set` 有 `RCX` 而 `dst_set` 无；3 项内联用 `xde_sprintset` 钉住 `jmp rel8` 的 `src_set`/`dst_set` 打印成**空串**、`jz rel8` 的 `src_set` 打印成 `F`（不再是 `???`） |
| `// 16-bit` | `:897` | 2 | `add ax,ax`、`mov ax,[moffs16]` |
| `// truncated` | `:907` | 1 | 截断的 `mov rax,imm64` |

### 输出与退出码

成功打 `ok %-28s ...`（`len=` / `enc=` / `rejected` / `rt` / `set<n> 0x… set|clear` / `flag 0x… set|clear` / `RIPREL` 以及打印器块的裸串等格式），失败打 `FAIL <name>: <msg>`；结尾固定：

```c
printf("\n%d failure(s)\n", g_fail);
return g_fail ? 1 : 0;
```

**退出码就是唯一 CI 信号**（`build.bat:44` 原样透传）。

### 覆盖缺口（被要求"补测试"时的清单）

1. **对象集覆盖仍然很浅**：现在对象集断言共 113 项（91 项 `expect_set` + 22 项 `expect_flag`），但 `expect_set` 那部分分散在少数几条编码路径上——Object sets 区段 27 项（`mov spl,al`/`mov ah,al` 两条高字节/扩展字节案例、5 个 REX2/EGPR 向量共 9 条断言，以及 `C6`/`MOV` 存储形式与 8 位 r8b-r15b 几条），Canonical 区段 17 项（`XSET_FL` 9 项 + `SETcc` 4 项 + `SAHF`/`LAHF` 4 项），Coverage 区段 39 项（模式相关栈集、16 位串操作与寻址、`PUSHA`、I/O、`CPUID`、段寄存器、`LEAVE`、移位与组 3 的标志、若干 `C_*` 位、`UD2`）与 Jcc/LOOP 区段 8 项（`JZ` 读 FL、`LOOP`/`LOOPE`/`JCXZ` 的计数寄存器）。位掩码里绝大多数 `XSET_*` 仍无断言；`xde_sprintset`/`xde_sprintset2`/`xde_sprintfl` 三个打印器合计也只被 16 项内联检查触碰（Object sets 区段 4 + 闸门区段 5 + `sete al` 非 undef 1 + 打印器最坏情况 3 + Jcc/LOOP 区段 3）。对象集是本库的一半价值，断言密度仍远低于长度/编码类。
2. **`flag` 位已覆盖 22 条 `expect_flag`，但仍有整类盲区**：`expect_flag` 覆盖 `C_BAD`（置位与未置位各一）、`C_REL`、`C_D64`、`C_ADDR2`、`C_STOP`、`C_CMD_RET`/`C_CMD_JMP`/`C_CMD_JCC`/`C_CMD_CALL`、`C_F64`、`C_O64`、`C_3DNOW`、`C_OPSZ8`、`C_SIB`、`C_ADDR1`/`C_ADDR4`、`C_DATA2`、`C_UNDEF`、`C_I64` 与 `C_RIPREL`（未置位）；内联另有 `C_RIPREL`（`:194-201`）与 `C_PUSH`/`C_CMD_RET`/`C_DATA4`/`C_ADDR4` 的 `xde_sprintfl` 串断言（`:568-601`）。仍无断言的：`C_ADDR67`/`C_DATA66`/`C_DATA1`/`C_DATA8`/`C_ADDR8`/`C_MODRM`/`C_POP`/`C_VEX`/`C_EVEX`/`C_XOP`/`C_REX`/`C_REX2`——这些名字目前只出现在 `:681-717` 的全位置位样例里，不构成解码行为断言。（`C_CMD_CALL`/`C_I64` 的断言是本轮新增，位于 `:801-802`。）（`XSET_FL` 是对象集位、不是 `flag` 位，`expect_set` 名下现有 21 项 `XSET_FL` 断言。）
3. `expect_enc` 忽略 `n` 形参（硬编码 15，`:51`）→ 该助手下从不测试截断行为。
4. 重复用例名：`"mov rax,[rip+0]"`（`:193` 是 `expect_len` 与 `:198` 是内联 `fail`/`printf`）与 `"rex2 lea r16d,[rax]"`（`:394` 的 `expect_enc` 与 `:416` 的 `expect_set`）——按名字 grep 输出会有歧义；缓冲区也被跨用例复用（`inc[]` `:373`、`aaa[]` `:382`）。
5. `:836-851` 四例缺区段注释，位置夹在「规范前缀 / 标志」区段与 `// 16-bit` 之间，容易误归类。
6. `XDE_ENC_LEGACY` 从未被 `expect_enc` 断言。

### 无 CLI 面

`int main(void)`，无 `argc`/`argv`/`getenv`，无 filter / verbose / `--`，**无法单跑某个用例**。

## Change Workflow

### 加一个测试用例

在 `tests/xde_test.c` 的 `main` 内找一个匿名块（或新区段），写：

```c
{
    static const uint8_t v[] = { 0x62, 0xF1, 0x7C, 0x48, 0x58, 0xC1 };
    expect_enc("vaddps zmm0,zmm0,zmm1", 64, v, 6, 6, XDE_ENC_EVEX);
}
```

保持「成功只打一行 `ok`」的既有风格。然后 `build.bat`。

### 改一个 opcode 的属性

**不要动 `src/xdetbl.c`。** 正确路径：

1. `tools/gen_tables.py:89-546` 里改对应的 Python map（`m0` = legacy、`m1` = 0F、`m2` = 0F38、`m3` = 0F3A、`m4-m6` = EVEX 4-6、`m7` = VEX 7、`m8`/`m9`/`ma` = XOP 8/9/A）；
2. 若用到**新的** `XA_*` 位/常量，同步 `src/xdetbl.h:9-45`（Python 侧在 `gen_tables.py:11-55` 各镜像一份）；
3. 重新生成：`python tools\gen_tables.py`——`check_header()`（`gen_tables.py:595-640`）会先逐名比对 `xdetbl.h` 与脚本常量，漂移就列出差异并 `SystemExit(1)` 且**不写文件**，所以同步 `src/xdetbl.h` 是硬性前置；
4. 把 `src/xdetbl.c` 的改动一并提交（它入库）；
5. 若新属性需要影响 `flag` 或对象集，还要改 `src/xde.c` 的 `apply_attr_flags`（`:123-144`）或三趟对象集推导（`:146` / `:287` / `:410`）；
6. 加测试用例并 `build.bat`。

本轮示例：`m1[0x90..0x9F]`（SETcc）在 `gen_tables.py:331-332` 从 `XA_MODRM | XA_OPSZ8 | XA_UNDEF` 改成 `XA_MODRM | XA_OPSZ8`（去掉 `XA_UNDEF`），重跑生成器后 `src/xdetbl.c` 的行数与结构不变；FL 读取改在 `apply_usage_special`/`apply_modrm_usage` 里按 opcode 硬编码（`XA_*` 位空间已满，没有对应属性位）。

**注意**：`XA_*` 的 32 位已占满（标志 0-20、IMM 21-24、group 25-31）。加第 22 个属性标志需要先把 `xde_attr`/`attr` 拓宽到 64 位（影响 `src/xdetbl.h`、`src/xdetbl.c`、`gen_tables.py`、`src/xde.c` 的 `uint32_t attr` 形参与 `apply_attr_flags`），或复用/回收现有位。

### 加一个 group（`/reg` 分派）

改 `tools/gen_tables.py` 的 `XG_*` 常量与对应 map 的 `GRP(n)`，**并同步** `src/xdetbl.h:47-79` 的 `enum xde_group_id`（顺序即 id，`XG_COUNT` 必须跟着变，它同时决定 `xde_group[30][8]` 的行数），然后重新生成。

### 加一个公共 API 函数

1. `include/xde.h` 声明（放在 `:282-296` 区块内，保持 `extern "C"` 覆盖）；
2. `src/xde.c` 实现，导出函数用 `__cdecl`（把新函数加进 `src/xde.c` 也要相应更新 `Key Directories` 的 static 计数）；
3. `tests/xde_test.c` 加用例（仓库现有 6 个 `expect_*` 助手，够用就别新增助手）；
4. `README.md` 的 API 段落同步。

无导出宏、无 `.def`、无版本脚本需要改。

## Troubleshooting

| 现象 | 原因 / 处理 |
|------|-------------|
| `Could not find vcvars64.bat` 然后退出 1 | 脚本先试 `vswhere.exe`（`%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe`），再回落四个固定路径（`build.bat:7-32`）。都落空说明没装 VS 的 C++ 工具集组件——装 VS 时勾「使用 C++ 的桌面开发」。临时绕过：自行 `call` 对应 `vcvars64.bat` 再手敲 `:39` 那条 `cl`。 |
| `build.bat` 成功但 `.sln` 打开报工具集 | 工程钉 `v145`，本地工具集不匹配 → 重定位（`README.md:46` 已预告）。 |
| 改了表但行为没变 | 忘了跑 `python tools\gen_tables.py`，或跑了但没重新编译——生成器不挂在构建里。 |
| 重新生成后 `src/xdetbl.c` 全文件 diff | 非 Windows 上跑生成器导致行尾变化；脚本以 `newline="\n"` 写文件，检查你的 diff 工具设置而不是改脚本。 |
| 运行输出一堆 `FAIL ...` | 看 `FAIL <name>: <msg>` 里的 `len=`/`want=`/`bytes=`；退出码 1。没有按用例过滤的手段，只能读全量输出。 |
| `xde_asm` 编出的字节比预期短 | `xde_asm` 只按 `flag & C_MODRM` / `C_SIB` 决定是否发 `modrm`/`sib`——手搓结构体时这两个位必须自己置对，它不校验。需要容量保护就用 `xde_asm_buf(out, max_len, &d)`：装不下返回 `0`（整体拒绝，不截断），并且先把 `addrsize`/`datasize`/`nvex` 夹到数组容量，不会越界读。 |

## Known Gaps / Cautions

以下是**代码可证**的已知缺口（作者未用 TODO 标注）：

- **REX2 保留 `p_66`/`p_rep`（刻意行为，不是缺口）**（对比 VEX `:828-829`/`:851-852`、EVEX `:791-792`、XOP `:896-897` 都清）——`66` 是 REX2 合法的遗留前缀，不是多余前缀。编码侧 `p_66` 与其它遗留前缀一起在 `:1108` 统一发射（无 nvex 专属分支），所以 `66 D5 …` 现在能字节级往返；改这里要解码/编码两侧一起动。
- **`C_ADDR8` 只来自 MOFFS（原先那条不可达分支已删）**：`parse_modrm` 里 `disp` 只会是 1/2/4（`:577-586`，末行注释写明），原来永不触发的 `else → C_ADDR8` 已删除；8 字节地址只经 MOFFS 路径（`:1000`）。新增位移宽度前先确认这条不变量还成立。
- **`xde_sprintfl` 已打印解码器可能产生的每一个 flag 位**（`src/xde_text.c:7-49`，不再是缺口）：共 34 个名字——低半 12 个（`C_BAD`/`C_REL`/`C_STOP`/`C_MODRM`/`C_SIB`/`C_RIPREL`/`C_REX`/`C_VEX`/`C_EVEX`/`C_XOP`/`C_REX2`/`C_UNDEF`，`:10-21`）+ `C_OPSZ8`（`:22`）+ 10 个尺寸类（`C_ADDR67`/`C_DATA66`/`C_ADDR1/2/4/8`/`C_DATA1/2/4/8`，`:23-32`）+ 7 个（`C_PUSH`/`C_POP`/`C_I64`/`C_O64`/`C_F64`/`C_D64`/`C_3DNOW`，`:33-39`）+ `C_CMD_*` 4 个（用 `XDE_CMD(fl)` 的 switch，`:40-46`）。解码路径不再置位的操作数角色位 `C_SRC_*`/`C_DST_*`（2.00 的 `src/xde.c` 一处都不写）仍不打印。缓冲区契约可验证：`tests/xde_test.c:681-717` 用全位置位的 `allflags` 钉住最坏情况 `xde_sprintfl` **227 字节**（断言 `< 256`），`xde_sprintset(~0ULL ^ 1<<63)` 79 字节、`xde_sprintset2(XSET2_ALL & ~XSET2_R16)` 97 字节，所以头文件那句「output should be at least 256 bytes」本轮首次被测到。
- **undef 标记是子集判定**：`(set & XSET_UNDEF) == XSET_UNDEF`（`src/xde_text.c:55`）与 `(set2 & XSET2_ALL) == XSET2_ALL`（`:142`）。后者严格更稳健——`set2` 带 bit ≥ 24 的杂位时仍打 `"???"`（`tests/xde_test.c:605` 用 `XSET2_ALL | 0x10000000000ULL` 钉住），不再依赖解码侧的整体赋值。
- **`XA_UNDEF` 已按「可确定性」拆分，只留给副作用确实未建模的指令**：`Jcc`（`70-7F` / `0F 80-8F`）只读 `XSET_FL`；`LOOP`/`LOOPE`/`LOOPNE`（`E0`/`E1`/`E2`）读并写计数寄存器（`XSET_CX`/`ECX`/`RCX`，宽度约定同 REP），`JCXZ`（`E3`）只读；近 `JMP`（`E9`/`EB`）读写都是空集（表侧 `tools/gen_tables.py:146-151`、`:232-236`、`:242`/`:244`，解码侧 `src/xde.c:257-258`、`:259-264` 与 0F 段的 `:280-281`）。仍带 `XA_UNDEF` 是刻意选择：`CALL`（`E8`、grp5 `/2`/`/3`）与 `RET`/`RETF`（`C2`/`C3`/`CA`/`CB`）——被调用者会破坏未知寄存器；远 `JMP`（`EA`、grp5 `/4`/`/5`）与 `IRET`（`CF`）——要装载 `CS`，而段寄存器在本引擎里只折成 `XSET_OTHER`；`INT`/`INTO`/`INT1`（`CD`/`CE`/`F1`，`INT3`（`CC`）只带 `XA_BAD`）、`BOUND`（`62`）、`WAIT`（`9B`）、x87（`D8-DF`）、`RSM`（`0F AA`）、`CLTS`/`INVD`/`WBINVD`/`WRMSR`/`RDMSR`（`0F 06`/`08`/`09`/`30`/`32`）、`UD2`（`0F 0B`）、`LAR`/`LSL`（`0F 02`/`03`）与 `0F 00`/`0F 01` 的 `SLDT`…`INVLPG` 系统组（`XA_UNDEF` 挂在这两个 opcode 上）——系统/MSR/x87 状态同样未建模。`flag` 不受影响（`C_CMD_JCC`/`C_REL`/`C_F64` 照常置位，见 `tests/xde_test.c:799-804`）；一旦置位，`src/xde.c:1010-1014` 把 `src_set`/`dst_set` 整体赋值成 `XSET_UNDEF`（**赋值，非 OR**）。
- **`C_I64` 只在 16/32 位出现，`C_O64` 只在 64 位出现**：`apply_attr_flags`（`src/xde.c:134`）照 `XA_I64` 置 `C_I64`，但带 `XA_I64` 的指令在 `mode == 64` 时更早被拒（`src/xde.c:919-920`）；16/32 位下它们合法，所以 `inc eax`（`40`，`gen_tables.py:124`）与 `aaa`（`37`，`:119`）在 32 位解码后会带上 `C_I64`（`aaa` 还带 `C_BAD`）。`C_O64` 相反，只在 64 位成功解码上出现（`syscall`，`m1[0x05]`）。两个名字 `xde_sprintfl` 都打印（`src/xde_text.c:35-36`），且现在都有解码断言（`C_I64` → 32 位 `inc eax`，`C_O64` → `syscall`，`tests/xde_test.c:801-805`）——这条记录的是「位只在单一模式下可达」这一事实，不是覆盖缺口。
- **编码侧已按 SDM 组序规范化前缀**：`xde_asm_buf` 现在按 SDM 组序发射遗留前缀——lock/rep（组1）→ segment（组2）→ `66`（组3）→ `67`（组4）（`:1105-1109`），随后才是 REX 或 `vex[]` + opcode；`asm_size()`（`:1054-1080`）的计数顺序同步调整（字节总数不变）。解码侧仍不限制前缀顺序，所以非规范序输入（如 `67 66 90`、`64 F3 A4`）能解码，但**重编码会规范化**为 `66 67 90`、`F3 64 A4`——`xde_asm` 的输出不再逐字节等于非规范输入，往返测试因此只用规范序输入，或显式断言规范化结果。
- **`xde102/todo` 的 4 条现已全部处理**（1.02 时代的留档，不再有未决项）：① `REP` 对不同串指令的标志差异 → 已修：`REP` 只把 `CX/ECX/RCX` 计入 src+dst，不再无条件置 FL；`CMPS`/`SCAS` 写 FL、带 `REP` 时再读 FL（`src/xde.c:155-160`、`:244-253`）；② `setxx` → 已修：`0F 90-9F` 读 FL 且 r/m 只写不读（`:277-279`、`:300`）；③ `cld/std/cmpsb` 的 DF 源集 → 按「DF 不作为源」处理（`CLD`/`STD` 只置 `dst_set |= XSET_FL`）；④ `PUSH` 的栈宽不受 `67` 影响 → 2.00 早已由 `XA_PUSH` → `stack_set(mode)` 处理（`:489-497`）。被要求处理这些行为前先确认是否仍适用。
- **`xde102/` 与 `src/` 存在同名文件**（`xde.c`/`xde.h`/`xdetbl.c`/`xde_text.c`）。搜索或批量替换务必限定路径，否则会误改参考资料。`xde102/xde.c:5` 用 `#include "xdetbl.c"` 文本包含其数据表，**无法与 2.00 同编译**（并重复定义 `xde_disasm`/`xde_asm`/`xde_sprintfl`/`xde_sprintset`）。仓库中没有任何构建文件或脚本引用 `xde102/`。
- **`src/xdetbl.h` 的 `XA_*`/`XG_*` 已有自动校验**（`check_header()`，`tools/gen_tables.py:595-640`，调用 `:679`）：`gen_tables.py` 顶部仍镜像一份常量，但生成前会解析 `xdetbl.h`，逐名比对 `XA_*`（含 `XA_IMM_*`、`XA_GRP_MASK`、两个 shift）、`enum xde_group_id` 的逐个枚举值（按位置，含 `XG_NONE`）与 `#define XDE_MAP_COUNT`（对 `len(MAPS)`），不一致就列出差异并 `SystemExit(1)`，**在写文件之前**退出，所以两边不会再静默漂移。改常量仍要动两处，只是现在忘一边会被拒绝而不是产出错误解释。

## Documentation Map

| 文档 | 内容 |
|------|------|
| `README.md`（110 行） | 唯一面向使用者的文档：`## What it does`（`:11`）、`## Layout`（`:23`）、`## Build (MSVC)`（`:36`，含生成器一致性校验那一句）、`## API`（`:57`，示例 `:60-67`，含 `xde_asm_buf`）、`## Notes`（`:85`，编码规则）。**无许可证段落、无外部链接。** |
| `AGENTS.md`（本文件） | 面向 AI 助手 / 新维护者的完整工程参考 |
| `xde102/xde.txt`（242 行） | 1.02 设计文档：版本历史（`:8-9`）、对象集设计原则与「不区分内存地址」的理由（`:16-40`）、被否决的 "Permutation conditions" 方案（`:42-44`）、API 与结构体清单（`:46-140`）、REP INSB 示例及作者承认的 bug（`:111-117`）、结尾是 Mistfall 项目的 `AnalyzeRegs` 用法示例（`:142-242`）。是理解 2.00 设计取舍的最佳背景读物。 |
| `xde102/todo`（11 行） | 1.02 时代的遗留问题清单，见 Known Gaps 中「`xde102/todo` 的 4 条现已全部处理」 |
| `LICENSE`（21 行） | MIT |
