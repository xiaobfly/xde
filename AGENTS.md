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

设计取舍（沿自 1.02，见 `xde102/xde.txt:22-45`）：

- **不区分** segment / FPU / MMX / XMM / YMM / ZMM / CR / DR / K 寄存器，统一折叠为 `XSET_OTHER` 一个位；
- **不区分**内存地址。`mov [eax], ebx` 与 `push ecx` 都只给 `XSET_MEM`。理由是面向静态文件分析，寄存器值未知，无法判断 `eax == esp` 之类的别名；
- 源集与目标集**之间没有覆盖关系**（作者明确拒绝 "Permutation conditions" 那套语义）。

**非目标**：不输出助记符、不做反汇编美化、不做数据流分析、不做多指令串扫、不做符号/重定位处理。

**API 一览**（`include/xde.h:282-296`，8 个函数，全部 `__cdecl`，无 export/visibility 宏）：

| 函数 | 语义 |
|------|------|
| `int xde_disasm(const uint8_t *opcode, struct xde_instr *diza)` | 64 位模式解码（`src/xde.c:1008`） |
| `int xde_disasm_ex(const uint8_t *opcode, struct xde_instr *diza, unsigned mode)` | 指定 16/32/64（`src/xde.c:1003`） |
| `int xde_disasm_buf(const uint8_t *opcode, unsigned max_len, struct xde_instr *diza, unsigned mode)` | 额外限制读取上限（`src/xde.c:591`） |
| `int xde_asm(uint8_t *opcode, const struct xde_instr *diza)` | 结构 → 字节；`return xde_asm_buf(opcode, XDE_MAXLEN, diza);` 的包装（`src/xde.c:1093`） |
| `int xde_asm_buf(uint8_t *opcode, unsigned max_len, const struct xde_instr *diza)` | 结构 → 字节，额外限制写入上限；装不下返回 0（`src/xde.c:1043`） |
| `void xde_sprintfl(char *output, uint64_t fl)` | flag → 串，缓冲区 ≥256 字节（`src/xde_text.c:7`） |
| `void xde_sprintset(char *output, uint64_t set)` | 对象集 → 串，缓冲区 ≥256 字节（`src/xde_text.c:43`） |
| `void xde_sprintset2(char *output, uint64_t set2)` | 第二对象集字 → 串，缓冲区 ≥256 字节（声明 `include/xde.h:296`，实现 `src/xde_text.c:124`） |

**返回值契约**：解码返回指令长度，`0` = 失败（截断 / 该模式下非法 / undefined）。编码返回写入字节数，`xde_asm_buf` 在结构体所需字节数 `> max_len` 时返回 `0`（`xde_asm` 给的是 `XDE_MAXLEN`，所以除空指针外永远成功）。没有错误码枚举、没有 errno、没有 out-param 状态。

用法（`README.md:57-64` 原文）：

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
  └─ xde_disasm / xde_disasm_ex            (src/xde.c:1008 / :1003, 只差默认参数)
       └─ xde_disasm_buf(ptr, max_len, d, mode)   ← 唯一真正的入口 (src/xde.c:591-1001)
            ├─ xde_attr[map][opcode]      (查表, :880)  ← src/xdetbl.c 生成
            ├─ xde_group[gid][modrm.reg]  (二次查表, :908)
            └─ parse_modrm (:465) → apply_modrm_usage (:257)
                                  → apply_usage_special (:146)
                                  → apply_implicit_gp (:375)
  └─ xde_asm(out, d) → xde_asm_buf(out, XDE_MAXLEN, d)   (src/xde.c:1093 / :1043-1091, 纯字节重组)
```

`src/xde.c` 共 18 个函数：5 个导出 + 13 个 `static`。无全局可变状态，无堆分配。

### 解码流水线（分阶段行号）

| # | 阶段 | 行 |
|---|------|-----|
| 1 | 入参守卫：空指针 → 0；`mode ∉ {16,32,64}` → 0；`max_len` 0/超限一律夹到 15 | `:602-609` |
| 2 | `memset` 清零 + 预置 `mode` / `defaddr` / `defdata` | `:611-614` |
| 3 | 游标初始化 `beg`/`p`/`end` | `:616-618` |
| 4 | `C_BAD` 启发式 | `:620-624` |
| 5 | 遗留前缀循环 | `:626-673` |
| 6 | 取下一个字节 | `:675-677` |
| 7 | REX（`40-4F`，仅 64 位） | `:679-691` |
| 8 | REX2（`D5`，仅 64 位） | `:693-724` |
| 9 | EVEX（`62`） | `:726-775` |
| 10 | VEX（`C4`/`C5`） | `:777-835` |
| 11 | XOP（`8F`） | `:837-870` |
| 12 | 遗留 opcode + `enc = XDE_ENC_LEGACY` | `:872-875` |
| 13 | 汇合点标签 `got_opcode:` | `:877` |
| 14 | map 越界检查 + `attr = xde_attr[map][mop]` | `:878-880` |
| 15 | 三种拒绝：`XA_INVALID` → 0；`XA_I64 && mode==64` → 0；`XA_O64 && mode!=64` → 0 | `:882-887` |
| 16 | `apply_attr_flags`：`XA_*` → `C_*` 映射（**`C_REL` 的唯一来源**，收尾不再重复置位） | `:889`（实现 `:123-144`） |
| 17 | `XA_GROUP` 强制 `XA_MODRM`，并就地补置 `C_MODRM`（`apply_attr_flags` 已经跑过） | `:892-897` |
| 18 | peek ModR/M，取 `reg = (mpeek >> 3) & 7` | `:899-904` |
| 19 | group 二次查表：`xde_group[gid][reg]` 再跑一次 `apply_attr_flags` | `:905-912` |
| 20 | 硬编码特例（移位计数 / `C6 C7 8F` / `F6` / `F7`） | `:913-946` |
| 21 | `parse_modrm` + `apply_modrm_usage` | `:948-951` |
| 22 | MOFFS 路径（非 ModR/M 的 `A0-A3` 等） | `:952-965` |
| 23 | `apply_usage_special` / `apply_implicit_gp` | `:967-968` |
| 24 | `XA_UNDEF` → `src_set = dst_set = XSET_UNDEF`、`src_set2 = dst_set2 = XSET2_ALL`（**赋值，非 OR**） | `:970-975` |
| 25 | `imm_bytes` 定长 → 拷贝到 `data_b` + `C_DATA*` | `:977-990` |
| 26 | 收尾：`len = cur.p - opcode`；`0` 或 `>15` → 0；置 `len`；返回 | `:994-1000` |

**3DNow 没有特例分支**：表把尾随 opcode 字节建模成 `XA_IMM_IB`，`XA_3DNOW` 只负责置 `C_3DNOW`（收尾注释 `:992-993`，现在是准确描述而非待办）。

**`rex` 口径统一**：`apply_modrm_usage`（`:260`）与 `apply_implicit_gp`（`:378`）都用 `(diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY)`，8 位寄存器命名不会因走哪一趟而不同。`gp_set` 对 `sz ∉ {1,2,4,8}` 返回 `XSET_OTHER`（`:89`），不会冒充 64 位。

**字节读取纪律**：所有读取都走 `cur_left`（`:17`）/ `get_byte`（`:24`）/ `peek_byte`（`:32`）。`cur_left` 双重夹取 `min(end - p, beg + XDE_MAXLEN - p)`；由于 `max_len` 已夹到 15，第二项实际永远不是较小者（死代码，但无害）。

**`C_BAD` 的来源**（共 4 类）：

1. 首两字节构成的 16 位小端字等于 `0x0000` 或 `0xFFFF`（`:620-624`）；
2. 同一类遗留前缀**重复出现**（`66`/`67`/段/`F2F3`/`F0`，`:636`/`:647`/`:655`/`:662`/`:669`）；
3. 表属性 `XA_BAD`（映射见 `:130`）；
4. `C6`/`C7`/`8F` 在 legacy map 下 `reg != 0`（`:921-923`）。

**前缀语义**：`66` 翻转 `defdata` 2↔4（`:634`）；`67` 在 64 位翻转 `defaddr` 8↔4、其余模式 2↔4（`:642-645`）；段前缀存 `p_seg`、`F2/F3` 存 `p_rep`、`F0` 存 `p_lock`。**每类只保留最后见到的字节**。注意前缀循环在 REX 判定**之前**跑完且 REX 只判一次，所以 `48 66 90` 会把 `48` 当 REX、再把 `66` 当 opcode——非规范前缀顺序不被拒绝。

### 编码类分派与歧义消解

前缀字节有歧义，各分支的判定门槛（每个都只看一两个前瞻字节）：

| 字节 | 两种解释 | 判定门槛 | 行 |
|------|----------|----------|-----|
| `62` | EVEX / BOUND | `peek(1..3)` 全成功 **且** `(b2 & 0x04)` **且**（`mode == 64` 或 `(b1 & 0xC0) == 0xC0`）→ 否则 BOUND | `:726-729` |
| `C4` `C5` | VEX2/VEX3 / LES/LDS | `mode == 64` 或 `(b1 & 0xC0) == 0xC0`（`C4` 还需第三字节）→ 否则 LES/LDS | `:777-780`, `:796-798` |
| `8F` | XOP / POP r/m | `(b1 & 0x1F) >= 8` **且**（`mode == 64` 或 `(b1 & 0xC0) == 0xC0`）→ 否则 `8F /0` = POP r/m | `:841` |
| `D5` | REX2 (APX) / AAD | 仅 64 位且 `b == 0xD5` → 否则按 AAD 走 legacy | `:693-724` |

三个向量前缀门槛（`62` / `C4`+`C5` / `8F`）写法一致：64 位下无条件成立，16/32 位下要求 `mod == 11b` 或等价的 `0xC0` 掩码；因此 16/32 位里 `8F 08`（mod ≠ 11）不再被当作 XOP，而是走非法 POP（长度 2，`C_BAD` 置位）。

门槛失败即落到 `parse_legacy_opcode`（定义 `:555`，调用 `:872-874`）。这就是 README 那句「`C4`/`C5`/`62`/`8F` 只有在后随字节符合前缀形式时才是 VEX/EVEX/XOP」的实现。

各编码类写回的结构字段：

| 类 | `enc` | `nvex` | `vex[]` | `map` 来源 | 其他 |
|----|-------|--------|---------|-----------|------|
| REX | 不改（仍是 LEGACY） | — | — | — | `rex` + `rex_w/r/x/b`，`C_REX` |
| REX2 | `XDE_ENC_REX2` | 2 | `D5, b1` | `(b1 & 0x80) ? 0F : LEGACY` | 读 bit6/5/4 → `rex_r4`/`rex_x4`/`rex_b4`，并置 `diza->rex = 0x40 \| (b1 & 0x0F)`（对寄存器命名等价于 REX）；`C_REX2 \| C_REX` |
| EVEX | `XDE_ENC_EVEX` | 4 | `62, P0, P1, P2` | `b1 & 7`（→ map 4-7） | `C_EVEX \| C_VEX`，`evex_r2/z/b/aaa`，`vex_vvvv` 含 `V'` 位；清 `p_66`/`p_rep` |
| VEX | `XDE_ENC_VEX2`(C5) / `XDE_ENC_VEX3`(C4) | 2 / 3 | 全前缀头 | `0F`(C5) / `b1 & 0x1F`(C4) | `C_VEX`；清 `p_66`/`p_rep` |
| XOP | `XDE_ENC_XOP` | 3 | `8F, b1, b2` | `b1 & 0x1F`（→ map 8-10） | `C_XOP`；清 `p_66`/`p_rep`（`:861-862`）；只写 `opcode`/`map`（`:866-867`） |

**`opcode2`/`opcode3` 的写入规则**：只有 `map ∈ {XDE_MAP_0F, XDE_MAP_0F38, XDE_MAP_0F3A}` 时才写（VEX `:824-832`、EVEX `:763-771`）；EVEX 的 map 4-7、VEX 的 map 7（`VEX7`）、XOP 的 map 8/9/A 都不写。XOP 与 VEX/EVEX 同一规则，不是缺口——`opcode`/`opcode2`/`opcode3` 只描述 `0F`/`0F 38`/`0F 3A` 这条链。

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

`XA_*` 到 `C_*` 的映射在 `apply_attr_flags`（`src/xde.c:123-144`）：17 条一对一 OR，**只增不减**（读-改-写 `diza->flag`）。`XA_VVVV_GPR` 与 group 位不经此函数（`XA_VVVV_GPR` 在 `:280`/`:295`/`:332`/`:506` 直接读，`XA_GRP_ID` 只在 `:906` 读）。

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

尺寸→标志：1 → `C_DATA1`，2 → `C_DATA2`，**3 → `C_DATA1 \| C_DATA2`**，4 → `C_DATA4`，8 → `C_DATA8`，**6 → `C_DATA4 \| C_DATA2`**（`:984-989`）。

### 对象集推导（三趟）

1. `apply_modrm_usage`（`:257-373`）——ModR/M 的 reg/r/m 字段。`mod == 3` 走寄存器分支，否则内存分支。`rex` 启发式在 `:260`：`rex != 0 || enc != LEGACY`（即 VEX/EVEX/XOP/REX2 一律当作「有 REX」，影响 8 位寄存器命名）。reg 字段带扩展位组成 `regx = rex_r4<<4 | rex_r<<3 | reg`（`:267-268`），r/m 侧同理用 `rex_b4`（`:325-326`）。`mod == 3` 的寄存器分支里，`dst` 白名单 = ALU / `MOV r/m` / 移位 / `F6`/`F7` / `FE`/`FF` / `80-83` / **`C6`/`C7`**（`:343-348`），而 `src` 赋值排除 `0x8D` **以及 MOV 存储形式 `0x88`/`0x89`/`0xC6`/`0xC7`**（`:336-338`）——这三类只写 r/m，不读。
2. `apply_usage_special`（`:146-255`）——隐式操作数：REP/串操作（`A4-A7`/`AA-AF`/`6C-6F`/`AC-AD`）、IO（`E4-E7`/`EC-EF`）、`SAHF/LAHF`、`CBW/CWD`、`AAA/AAS`、`AAM/AAD`、`PUSHA/POPA`、`PUSH/POP sreg`、`XLAT`、`ENTER/LEAVE`、`MOV` 段寄存器，以及 0F map 的 `CPUID`/`SHLD/SHRD`/`LSS` 等。**其 `attr` 形参未使用**（`(void)attr;` `:254`）。
3. `apply_implicit_gp`（`:375-463`）——opcode 隐含的 GPR：`INC/DEC r`、`PUSH/POP r`、`XCHG r8,eAX`、`MOV r,Iv`、`ALU AL/eAX, Iv`、`BSWAP`，外加 `XA_PUSH`/`XA_POP` 的栈列（`stack_set`：16 → `XSET_SP`，32 → `XSET_ESP`，64 → `XSET_RSP`）。其 `rex` 判定在 `:378`，用的是与 `:260` **相同的** `(diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY)`。

**REX2 的 r/m 是 GPR，不是向量寄存器**：`parse_modrm` 的 SIB 分支（`:505-506`）与 `apply_modrm_usage` 的内存分支（`:370`）在判断「是否按向量寄存器记 `XSET_OTHER`」时都会排除 `XDE_ENC_REX2`；`mod == 3` 分支同样排除（`:331-335`）。另外 `parse_modrm` 的两个「无基址」判定——SIB 的 `mod == 0 && base == 5`（`:503`）与 `mod == 0 && rm == 5`（`:522`）——在 `rex_b4` 置位时不再成立，此时它指的是真寄存器 r21，不是 disp32。

`gp_set`（`:43-90`）的寄存器列映射：`reg > 31 → XSET_OTHER`（**解码路径不可达**，`reg` 由 5 位扩展位拼出，上限 31）；`reg >= 16` 交给其第 4 个形参 `uint64_t *egpr`——EGPR 写进**第二对象集字**（`*egpr |= XSET2_R16 << (reg - 16)`，`:66-72`），固定寄存器处传 `NULL`；`reg >= 8` 分两支：首字仍返回**宽度无关**的 `XSET_R8 << (reg-8)`，并且当 `sz <= 1`（8 位形式 r8b-r15b）时**额外**写入第二字的 `XSET2_R8B << (reg-8)`（`:73-79`）——即**叠加**而非替换：`XSET_R8..XSET_R15` 照旧，第二字另有 8 位宽度位；`sz <= 1` 时按 `rex` 选 `lo8_norex`（AL/CL/DL/BL/**AH/CH/DH/BH**）或 `lo8_rex`（AL/CL/DL/BL/**SPL/BPL/SIL/DIL**——独立的 `XSET_SPL/BPL/SIL/DIL` 位，不再复用 16 位那几位）；`sz` 2/4/8 → `w16`/`w32`/`w64`，**`sz ∉ {1,2,4,8}` 返回 `XSET_OTHER`，不冒充 64 位**（`:87-89`）。

### 编码方向（`xde_asm`）

**不做校验、不解码**；`xde_asm_buf`（`src/xde.c:1043-1091`）只多做一次容量检查，`xde_asm`（`:1093-1096`）是 `return xde_asm_buf(opcode, XDE_MAXLEN, diza);` 的薄包装。固定顺序拼接：

```
max_len 夹取：0 或 > XDE_MAXLEN 一律按 15                        (:1050-1051)
→ 计数夹到数组容量：nvex ≤ 4 / naddr ≤ 8 / ndata ≤ 8            (:1054-1056)
→ asm_size() 算所需字节数，> max_len 则 return 0                 (:1014-1041, :1058-1059)
p_seg → p_lock → p_rep → p_67
  ├─ 若 nvex：p_66 → vex[0..nvex-1] → opcode   （此路径不发 rex）
  └─ 否则：p_66 → rex → opcode
             └─ 若 opcode == 0x0F：opcode2 →（若 opcode2 ∈ {38,3A}）opcode3
→ flag & C_MODRM ? modrm
→ flag & C_SIB   ? sib
→ naddr 个 addr_b[]
→ ndata 个 data_b[]
```

**容量安全**：`addrsize`/`datasize`/`nvex` 是 `uint8_t`，被写坏时解码侧的计数会越过 `addr_b[8]`/`data_b[8]`/`vex[4]`（真 UB）。`xde_asm_buf` 先把这三个计数夹到数组容量（`:1054-1056`），再用 `asm_size()`（`:1014-1041`）算出所需字节数，`> max_len` 就返回 `0`（`:1058-1059`）；`max_len == 0` 或 `> XDE_MAXLEN` 一律按 `XDE_MAXLEN` 处理（`:1050-1051`），与 `xde_disasm_buf` 的 `max_len` 处理同构。所以解码成功的指令（`len ≤ 15`）经 `xde_asm` 永远编得出来。

`nvex` 路径补发 `p_66`（`:1070`）是为 REX2：REX2 是唯一**保留** `p_66`/`p_rep` 的编码类（VEX/EVEX/XOP 在解码时就清掉，`:793-794`/`:816-817`/`:756-757`/`:861-862`），而它们的 `vex[]` 头自带 pp 字段，不走 `p_66`；所以 `66 D5 …` 现在能字节级往返。

它只读 `nvex`/`vex[]`、`p_seg/p_lock/p_rep/p_67/p_66/rex`、`opcode/opcode2/opcode3`、`modrm`、`sib`、`addr_b+addrsize`、`data_b+datasize`，**完全不看 `map` / `enc` / `defdata` / `defaddr` / `len` / 对象集**。因此：

- 手搓一个 `nvex == 0` 但 `map == XDE_MAP_0F38` 的结构体，`xde_asm_buf` 不会补出 `0F 38` 前缀——它只信字节字段；
- 返回 `0` 只有两种情形：`!opcode || !diza`（`:1048-1049`），或所需字节数 `> max_len`（`:1058-1059`）；解码成功的指令不可能编出 0 字节。

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
| `src/xde.c` | 解码器 + 编码器（1096 行），全部核心逻辑 | `xde_disasm_buf:591`, `xde_asm_buf:1043`, `xde_asm:1093`, 13 个 static 助手 |
| `src/xdetbl.c` | **机器生成**的属性表（414 行） | `xde_attr:5`, `xde_group:382` |
| `src/xdetbl.h` | 手写的表层契约（86 行） | `XA_*`, `enum xde_group_id`, `XDE_MAP_COUNT 11` |
| `src/xde_text.c` | 调试打印（157 行，3 个函数） | `xde_sprintfl:7`, `xde_sprintset:43`, `xde_sprintset2:124` |
| `tools/gen_tables.py` | 表生成器（617 行），唯一写出 `src/xdetbl.c` 的地方 | `OUT:7-8`, `MAPS:530`, emit 块 `:592-617` |
| `tests/xde_test.c` | 唯一测试文件（627 行） | 6 个 `expect_*` 助手, `main:161` |
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

退出码 `0` = 全通过，`1` = 有失败。`main` 是 `int main(void)`，**没有任何 CLI 参数**（无 filter / verbose / 单用例选择），每次运行都执行全部 108 项检查。

### 重新生成属性表

```bat
python tools\gen_tables.py
```

只写 `src/xdetbl.c`（`OUT` 定义在 `tools/gen_tables.py:7-8`），**不生成 `src/xdetbl.h`**——头文件是手写的，改常量要两边同步。脚本无参数、无 `--check`、无外部输入文件（全部表数据以 Python 字面量内联在 `:74-528`），仅依赖标准库（`import os`），需要 Python 3.7+（用了 `from __future__ import annotations` 与 f-string）。以 `newline="\n"` 写文件（`:615-616`），保持这一点以免重新生成时行尾抖动。

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
- **注释风格**：行内 `//`，多用于标注特例原因，例：`// 32-bit GP writes zero-extend in 64-bit mode.`（`:270`）、`// Vector encodings use OTHER for the reg field.`（`:274`）、`// segment override`（`:360`）。**全树没有任何 `TODO`/`FIXME`/`XXX`/`HACK`/`NOTE` 标记**（`src/` 已核）。
- **表纪律**：列宽固定，`xde_attr` 每行 8 个 `0x%08X`，每组带 `// legacy` / `// 0F` 之类行尾注释。
- **对象集的打印策略**（`xde_sprintset`，`src/xde_text.c:43-121`）：按列从宽到窄取第一个命中——含有 `RAX` 就绝不打印 `EAX`/`AX`；16 位列之后还有 8 位扩展寄存器兜底（`SP`→`SPL`、`BP`→`BPL`、`SI`→`SIL`、`DI`→`DIL`，`:84-102`）；`XSET_UNDEF` 用**子集判定** `(set & XSET_UNDEF) == XSET_UNDEF`（`:47`）特判为 `"???"`。第二对象集字由 `xde_sprintset2` 打印（`src/xde_text.c:124-157`）：`(set2 & XSET2_ALL) == XSET2_ALL`（`:134`）特判为 `"???"`，否则先逐位输出 `R16`…`R31`（`:139-144`），再逐位输出 `R8B`…`R15B`（`:145-153`）。两处都是子集判定，`set2` 带 bit ≥ 24 的杂位也不会破坏 undef 标记。

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
| `src/xde.c:591-1001` | `xde_disasm_buf` —— 改解码行为从这里读起 |
| `src/xde.c:1043-1091` | `xde_asm_buf` —— 字节重组 + 容量检查（`xde_asm` 是它的包装，`:1093-1096`） |
| `src/xde.c:1014-1041` | `asm_size` —— 估算编码所需字节数，与 `xde_asm_buf` 的落地循环必须同步 |
| `src/xde.c:726-729` / `:777-780` / `:841` | 三处编码类歧义门槛 |
| `src/xde.c:123-144` | `XA_*` → `C_*` 映射表（新增属性位必改） |
| `src/xde.c:146` / `:257` / `:375` | 三趟对象集推导 |
| `src/xde_text.c:124` | `xde_sprintset2` —— 第二对象集字的打印器 |
| `src/xde_text.c:7` | `xde_sprintfl` —— flag 打印器（新增 flag 覆盖时改这里） |
| `src/xdetbl.h:9-45` | `XA_*` 位定义 + IMM / GRP 位移 |
| `src/xdetbl.h:47-81` | `enum xde_group_id`、`XA_GRP`、`XDE_MAP_COUNT 11` |
| `tools/gen_tables.py:7-8` / `:74-528` / `:592-617` | 输出路径 / 全部表数据 / emit 块 |
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

**自研极简 harness，无第三方框架，无 `ASSERT`/`CHECK`/`TEST` 宏**。全部逻辑在 `tests/xde_test.c`（627 行）的单个 `int main(void)`（`:161-627`）中：77 个匿名 `{ }` 块，每块一个 `static const uint8_t` 向量紧跟 `expect_*` 调用。**零文件 IO、零 fixture、零 golden 文件**。

断言助手（括注是本轮实测的调用次数）：

| 助手 | 行 | 契约 |
|------|-----|------|
| `fail` | `:10-14` | 打印 `FAIL <name>: <msg>`，`g_fail++` |
| `hexbytes` | `:16-25` | 字节数组 → 空格分隔 `%02X` 串 |
| `expect_len(name, mode, b, n, want)` | `:27-45` | `xde_disasm_buf(b, n, ...)`，要求 `got == want` **且** `d.len == got`（42 次） |
| `expect_enc(name, mode, b, n, want_len, enc)` | `:47-65` | 要求长度 + `d.enc`。**实现里硬编码传 15，忽略形参 `n`**（`:51`）（14 次） |
| `expect_fail(name, mode, b, n)` | `:67-78` | 要求返回 `0`（4 次） |
| `expect_roundtrip(name, mode, b, n)` | `:80-113` | disasm → `xde_asm`（长度须等于 `n`）→ **`memcmp(out, b, n)` 字节级比较**（`:101`）→ 再 disasm，比较 `len` + `opcode` + `modrm`（5 次） |
| `expect_set(name, mode, b, n, sel, bit, want)` | `:117-138` | 要求解码长度 == `n`，再断言单个对象集位的存在/不存在。`sel` 0/1/2/3 = `src_set`/`dst_set`/`src_set2`/`dst_set2`，`want` 1 = 置位、0 = 未置位；成功打 `set<n> 0x… set|clear`（27 次） |
| `expect_flag(name, mode, b, n, bit, want)` | `:141-159` | 要求解码长度 == `n`，再断言单个 flag 位。`bit` 取单个 `C_*` 常量，`want` 1 = 置位、0 = 未置位；成功打 `flag 0x… set|clear`（3 次） |

### 覆盖矩阵（108 项检查 / 95 个用例名）

108 = 95 次助手调用（`expect_len` 42 + `expect_enc` 14 + `expect_fail` 4 + `expect_roundtrip` 5 + `expect_set` 27 + `expect_flag` 3）+ 13 项内联检查（`C_RIPREL` 1 + 打印器 4 + 新闸门区段 8）。

| 区段注释 | 行 | 检查数 | 内容 |
|----------|-----|--------|------|
| `// 64-bit GP` | `:163` | 26 | nop、`xor eax,eax`、`xor rax,rax`（含往返）、`mov rax,imm64`、`mov eax,imm32`、`mov r8,imm64`、RIP 相对（+ 内联 `C_RIPREL`）、`call [rip+0]`、`call rel32`、`66 call rel32`、ret、`push rbp`、`sub rsp,0x20`、SIB、`mov r8,[rsp+0x28]`、`nop dword [rax+rax]`、endbr64、syscall、movsxd、`moffs64` ×2、`test rax,imm32`、bt、`67` 前缀 |
| `// SSE / 0F38 / 0F3A` | `:276` | 6 | movups、palignr、pshufb、crc32、movbe、3DNow `pavgusb` |
| `// VEX` | `:302` | 6 | VEX2 `vaddps`（含往返）、VEX3 `andn` ×2、`rorx`、`vzeroupper` |
| `// EVEX` | `:325` | 4 | `vaddps zmm`（含往返）、EVEX 内存形式、`vrndscaless` |
| `// XOP` | `:344` | 4 | `vfrczpd`（含往返）、`vpcomb`、`bextr` |
| `// 32-bit mode: LES vs VEX, BOUND vs EVEX` | `:359` | 9 | LES/LDS、32 位 VEX2、BOUND、`inc eax`、truncated REX、`rex nop`、aaa 非法/合法、`push es` 非法 |
| `// REX2 (APX)` | `:391` | 2 | `rex2 lea`、`rex2 imul` |
| `// Object sets: 8-bit extension registers vs high bytes, and APX EGPRs` | `:401-504` | 32 | 27 项 `expect_set` + 4 项内联打印器检查 + 1 项字节级往返：`mov spl,al`（`XSET_SPL` 置位且**不含** `XSET_SP`）、`mov ah,al`（`XSET_AH` 置位且**不含** `XSET_SPL`，钉住无 REX 时的差异）、`rex2 lea r16d,[rax]`（`dst_set2` 含 `XSET2_R16`、`dst_set` 不含 `XSET_OTHER`）、`rex2 lea r31d,[rax]`（`XSET2_R31`）、`rex2 push r16`（`src_set2` 含 `XSET2_R16`，钉住 opcode+r 的 B4）、`rex2 mov eax,[r16]`（SIB base 的 B4 → `src_set2` 含 R16，同时 `src_set` 含 `XSET_RAX`）、`rex2 add rax,r16`（`mod == 3` 下 reg 是 EGPR、r/m 是 legacy GPR，`:432-438`）；打印器输出串断言（`R16` / `R8B` / `SPL` / 解码后再打印 `R16`，`:439-468`）。`:470-504` 是同区段续块（注释 `// REX2 round-trip with a legacy prefix, MOV store forms, 8-bit r8-r15.`）：`66 D5 40 8D 00` 往返（钉 `nvex` 路径补发 `p_66`）、`C6 C0 12`（`mov al,0x12` 报 dst 不报 src）、`8B C3`（`mov eax,ebx` 的 r/m 仍计入 src，反向控制）、`41 88 C0`（`mov r8b,al` → `XSET_R8` 与 `XSET2_R8B` 同时置位）、`41 88 C7`（`r15b` → `XSET2_R15B`）、`49 8B C0`（`mov rax,r8` → **无** `R8B`）、`44 8B C0`（`mov r8d,eax` → **无** `R8B`） |
| `// XOP gate in 16/32-bit, the relocated C_REL flag, and xde_asm limits` | `:506-590` | 12 | 4 次助手调用 + 8 项内联检查：`8F 08` 在 32 位 → `expect_len` 长度 2、`C_BAD` 置位；`call rel32` → `C_REL` 置位且 `C_BAD` 未置位；`xde_asm` 对 `datasize=200`/`addrsize=200`/`nvex=200` 夹到数组容量后分别返回 9/9/5；`xde_asm_buf(out,4)` 对 7 字节指令返回 0、`xde_asm_buf(out,7)` 返回 7；`xde_sprintfl` 对 `push rbp` 含 `C_PUSH`、对 `ret` 含 `C_CMD_RET`；`xde_sprintset2(XSET2_ALL \| 1<<40)` 打印 `???` |
| （无区段注释） | `:592-607` | 4 | `pop rax (8F /0)`、`test al,0x12`、`not al`、`call rax` |
| `// 16-bit` | `:609` | 2 | `add ax,ax`、`mov ax,[moffs16]` |
| `// truncated` | `:619` | 1 | 截断的 `mov rax,imm64` |

### 输出与退出码

成功打 `ok %-28s ...`（`len=` / `enc=` / `rejected` / `rt` / `set<n> 0x… set|clear` / `flag 0x… set|clear` / `RIPREL` 以及打印器块的裸串等格式），失败打 `FAIL <name>: <msg>`；结尾固定：

```c
printf("\n%d failure(s)\n", g_fail);
return g_fail ? 1 : 0;
```

**退出码就是唯一 CI 信号**（`build.bat:44` 原样透传）。

### 覆盖缺口（被要求"补测试"时的清单）

1. **对象集覆盖仍然很浅**：现在有 27 项 `expect_set` 断言（见上表 Object sets 区段），但只走了少数几条编码路径（`mov spl,al`/`mov ah,al` 两条高字节/扩展字节案例、5 个 REX2/EGPR 向量共 9 条断言，以及 `C6`/`MOV` 存储形式与 8 位 r8b-r15b 几条）；`xde_sprintset`/`xde_sprintset2`/`xde_sprintfl` 三个打印器合计也只被 7 项内联检查触碰（Object sets 区段 4 + 闸门区段 3）。对象集是本库的一半价值，断言密度仍远低于长度/编码类。
2. **flag 只有 4 条断言**：3 条 `expect_flag`（`C_BAD` 置位 / `C_REL` 置位 / `C_BAD` 未置位，`:506-516`）+ 1 条内联 `C_RIPREL`（`:194-201`，唯一还留在 `expect_*` 之外的内联 `fail`）。其余 `C_*` 无覆盖。
3. `expect_enc` 忽略 `n` 形参（硬编码 15，`:51`）→ 该助手下从不测试截断行为。
4. 重复用例名：`"mov rax,[rip+0]"`（`:193` 是 `expect_len` 与 `:198` 是内联 `fail`/`printf`）与 `"rex2 lea r16d,[rax]"`（`:394` 的 `expect_enc` 与 `:416` 的 `expect_set`）——按名字 grep 输出会有歧义；缓冲区也被跨用例复用（`inc[]` `:373`、`aaa[]` `:382`）。
5. `:592-607` 四例缺区段注释，位置夹在闸门区段与 `// 16-bit` 之间，容易误归类。
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

1. `tools/gen_tables.py:74-528` 里改对应的 Python map（`m0` = legacy、`m1` = 0F、`m2` = 0F38、`m3` = 0F3A、`m4-m6` = EVEX 4-6、`m7` = VEX 7、`m8`/`m9`/`ma` = XOP 8/9/A）；
2. 若用到**新的** `XA_*` 位/常量，同步 `src/xdetbl.h:9-45`（Python 侧常量在 `gen_tables.py` 顶部各写一份，无自动校验）；
3. 重新生成：`python tools\gen_tables.py`；
4. 把 `src/xdetbl.c` 的改动一并提交（它入库）；
5. 若新属性需要影响 `flag` 或对象集，还要改 `src/xde.c` 的 `apply_attr_flags`（`:123-144`）或三趟对象集推导（`:146` / `:257` / `:375`）；
6. 加测试用例并 `build.bat`。

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

- **REX2 保留 `p_66`/`p_rep`（刻意行为，不是缺口）**（对比 VEX `:793-794`/`:816-817`、EVEX `:756-757`、XOP `:861-862` 都清）——`66` 是 REX2 合法的遗留前缀，不是多余前缀。编码侧由 `xde_asm_buf` 在 `nvex` 路径补发 `p_66`（`:1070`），所以 `66 D5 …` 现在能字节级往返；改这里要解码/编码两侧一起动。
- **`C_ADDR8` 只来自 MOFFS（原先那条不可达分支已删）**：`parse_modrm` 里 `disp` 只会是 1/2/4（`:542-550`，末行注释写明），原来永不触发的 `else → C_ADDR8` 已删除；8 字节地址只经 MOFFS 路径（`:960`）。新增位移宽度前先确认这条不变量还成立。
- **`xde_sprintfl` 有意不打印尺寸类 flag**（`src/xde_text.c:7-41`）：实际打印 24 个名字——低半 12 个（`C_BAD`/`C_REL`/`C_STOP`/`C_MODRM`/`C_SIB`/`C_RIPREL`/`C_REX`/`C_VEX`/`C_EVEX`/`C_XOP`/`C_REX2`/`C_UNDEF`，`:10-21`）+ 高半 8 个（`C_OPSZ8`/`C_PUSH`/`C_POP`/`C_I64`/`C_O64`/`C_F64`/`C_D64`/`C_3DNOW`，`:24-31`）+ `C_CMD_*` 4 个（用 `XDE_CMD(fl)` 的 switch，`:32-38`）。**有意省略** `C_ADDR1/2/4/8`、`C_DATA1/2/4/8`、`C_ADDR67`、`C_DATA66`——它们可由 `addrsize`/`datasize`/`p_67`/`p_66` 推得（注释 `:22-23`），重复打印只会拉长输出；操作数角色位 `C_SRC_*`/`C_DST_*` 同样不打印。想靠文本核对尺寸类 flag 时不要以为是解码漏了。
- **undef 标记是子集判定**：`(set & XSET_UNDEF) == XSET_UNDEF`（`src/xde_text.c:47`）与 `(set2 & XSET2_ALL) == XSET2_ALL`（`:134`）。后者严格更稳健——`set2` 带 bit ≥ 24 的杂位时仍打 `"???"`（`tests/xde_test.c:584` 用 `XSET2_ALL | 1<<40` 钉住），不再依赖解码侧的整体赋值。
- **编码侧不还原规范前缀顺序**：`xde_asm_buf` 按 `p_seg` → `p_lock` → `p_rep` → `p_67` → `p_66`/`rex` → opcode 拼接（`:1063-1083`），解码侧不限制前缀顺序，所以能字节级往返的输入未必是规范序（`xde_asm_buf` 只做容量检查，不做规范化）。
- **`xde102/todo` 的 4 条历史遗留**（1.02 时代，未同步进 2.00 注释）：`REP` 对不同串指令的标志差异（`CMPSB`/`SCASB` 改标志，`LODSB`/`INSB`/`OUTSB`/`MOVSB` 不改）、`setxx`、`cld/std/cmpsb` 的 DF 源集撤销（2.00 无 DF 概念，已过时）、`PUSH` 的栈段宽度不受 `67` 影响（2.00 的 `XA_PUSH` 走 `stack_set(mode)`，已处理）。被要求处理这些行为前先确认是否仍适用。
- **`xde102/` 与 `src/` 存在同名文件**（`xde.c`/`xde.h`/`xdetbl.c`/`xde_text.c`）。搜索或批量替换务必限定路径，否则会误改参考资料。`xde102/xde.c:5` 用 `#include "xdetbl.c"` 文本包含其数据表，**无法与 2.00 同编译**（并重复定义 `xde_disasm`/`xde_asm`/`xde_sprintfl`/`xde_sprintset`）。仓库中没有任何构建文件或脚本引用 `xde102/`。
- **`src/xdetbl.h` 的 `XA_*`/`XG_*` 是 `gen_tables.py` 里常量块的手工副本**，无自动同步检查——改一边忘另一边会得到错误的表解释且编译期不报错。

## Documentation Map

| 文档 | 内容 |
|------|------|
| `README.md`（96 行） | 唯一面向使用者的文档：`## What it does`（`:11`）、`## Layout`（`:23`）、`## Build (MSVC)`（`:36`）、`## API`（`:54`，示例 `:57-64`，含 `xde_asm_buf`）、`## Notes`（`:82`，编码规则）。**无许可证段落、无外部链接。** |
| `AGENTS.md`（本文件） | 面向 AI 助手 / 新维护者的完整工程参考 |
| `xde102/xde.txt`（242 行） | 1.02 设计文档：版本历史（`:7-9`）、对象集设计原则与「不区分内存地址」的理由（`:16-40`）、被否决的 "Permutation conditions" 方案（`:42-44`）、API 与结构体清单（`:46-140`）、REP INSB 示例及作者承认的 bug（`:111-117`）、结尾是 Mistfall 项目的 `AnalyzeRegs` 用法示例（`:142-242`）。是理解 2.00 设计取舍的最佳背景读物。 |
| `xde102/todo`（11 行） | 1.02 时代的遗留问题清单，见 Known Gaps 中「`xde102/todo` 的 4 条历史遗留」 |
| `LICENSE`（21 行） | MIT |
