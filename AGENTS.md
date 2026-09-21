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

**API 一览**（`include/xde.h:285-299`，8 个函数，全部 `__cdecl`，无 export/visibility 宏）：

| 函数 | 语义 |
|------|------|
| `int xde_disasm(const uint8_t *opcode, struct xde_instr *diza)` | 64 位模式解码（`src/xde.c:1206`） |
| `int xde_disasm_ex(const uint8_t *opcode, struct xde_instr *diza, unsigned mode)` | 指定 16/32/64（`src/xde.c:1201`） |
| `int xde_disasm_buf(const uint8_t *opcode, unsigned max_len, struct xde_instr *diza, unsigned mode)` | 额外限制读取上限（`src/xde.c:770`） |
| `int xde_asm(uint8_t *opcode, const struct xde_instr *diza)` | 结构 → 字节；`return xde_asm_buf(opcode, XDE_MAXLEN, diza);` 的包装（`src/xde.c:1291`） |
| `int xde_asm_buf(uint8_t *opcode, unsigned max_len, const struct xde_instr *diza)` | 结构 → 字节，额外限制写入上限；装不下返回 0（`src/xde.c:1240`） |
| `void xde_sprintfl(char *output, uint64_t fl)` | flag → 串，缓冲区 ≥256 字节（`src/xde_text.c:7`，实测最坏 227 字节） |
| `void xde_sprintset(char *output, uint64_t set)` | 对象集 → 串，缓冲区 ≥256 字节（`src/xde_text.c:51`，实测最坏 79 字节） |
| `void xde_sprintset2(char *output, uint64_t set2)` | 第二对象集字 → 串，缓冲区 ≥256 字节（声明 `include/xde.h:299`，实现 `src/xde_text.c:132`，实测最坏 97 字节） |

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
  └─ xde_disasm / xde_disasm_ex            (src/xde.c:1206 / :1201, 只差默认参数)
       └─ xde_disasm_buf(ptr, max_len, d, mode)   ← 唯一真正的入口 (src/xde.c:770-1199)
            ├─ xde_attr[map][opcode]      (查表, :1059)  ← src/xdetbl.c 生成
            ├─ xde_group[gid][modrm.reg]  (二次查表, :1087)
            └─ parse_modrm (:644) → apply_modrm_usage (:294)
                                  → apply_usage_special (:146)
                                  → apply_implicit_gp (:554)
  └─ xde_asm(out, d) → xde_asm_buf(out, XDE_MAXLEN, d)   (src/xde.c:1291 / :1240-1289, 纯字节重组)
```

`src/xde.c` 共 18 个函数：5 个导出 + 13 个 `static`。无全局可变状态，无堆分配。

### 解码流水线（分阶段行号）

| # | 阶段 | 行 |
|---|------|-----|
| 1 | 入参守卫：空指针 → 0；`mode ∉ {16,32,64}` → 0；`max_len` 0/超限一律夹到 15 | `:781-788` |
| 2 | `memset` 清零 + 预置 `mode` / `defaddr` / `defdata` | `:790-793` |
| 3 | 游标初始化 `beg`/`p`/`end` | `:795-797` |
| 4 | `C_BAD` 启发式 | `:799-803` |
| 5 | 遗留前缀循环 | `:805-852` |
| 6 | 取下一个字节 | `:854-856` |
| 7 | REX（`40-4F`，仅 64 位） | `:858-870` |
| 8 | REX2（`D5`，仅 64 位） | `:872-903` |
| 9 | EVEX（`62`） | `:905-954` |
| 10 | VEX（`C4`/`C5`） | `:956-1014` |
| 11 | XOP（`8F`） | `:1016-1049` |
| 12 | 遗留 opcode + `enc = XDE_ENC_LEGACY` | `:1051-1054` |
| 13 | 汇合点标签 `got_opcode:` | `:1056` |
| 14 | map 越界检查 + `attr = xde_attr[map][mop]` | `:1057-1059` |
| 15 | 三种拒绝：`XA_INVALID` → 0；`XA_I64 && mode==64` → 0；`XA_O64 && mode!=64` → 0 | `:1061-1066` |
| 16 | `apply_attr_flags`：`XA_*` → `C_*` 映射（**`C_REL` 的唯一来源**，收尾不再重复置位） | `:1068`（实现 `:123-144`） |
| 17 | `XA_GROUP` 强制 `XA_MODRM`，并就地补置 `C_MODRM`（`apply_attr_flags` 已经跑过） | `:1070-1076` |
| 18 | peek ModR/M，取 `reg = (mpeek >> 3) & 7` | `:1078-1083` |
| 19 | group 二次查表：`xde_group[gid][reg]` 再跑一次 `apply_attr_flags` | `:1084-1091` |
| 20 | 硬编码特例：移位组 `C0 C1 D0-D3` 写 FL（`RCL`/`RCR` 另读 FL、`D2`/`D3` 另读 CL）／`C6 C7 8F` 的 `C_BAD`（`F8` 例外 = `XBEGIN`/`XABORT`）／`F6`/`F7` 的标志与 `MUL`/`DIV` 累加器规则 | `:1092-1144` |
| 21 | `parse_modrm` + `apply_modrm_usage` | `:1146-1149` |
| 22 | MOFFS 路径（非 ModR/M 的 `A0-A3` 等） | `:1150-1163` |
| 23 | `apply_usage_special` / `apply_implicit_gp` | `:1165-1166` |
| 24 | `XA_UNDEF` → `src_set = dst_set = XSET_UNDEF`、`src_set2 = dst_set2 = XSET2_ALL`（**赋值，非 OR**） | `:1168-1173` |
| 25 | `imm_bytes` 定长 → 拷贝到 `data_b` + `C_DATA*` | `:1175-1188` |
| 26 | 收尾：`len = cur.p - opcode`；`0` 或 `>15` → 0；置 `len`；返回 | `:1190-1198` |

**3DNow 没有特例分支**：表把尾随 opcode 字节建模成 `XA_IMM_IB`，`XA_3DNOW` 只负责置 `C_3DNOW`（收尾注释 `:1190-1191`，现在是准确描述而非待办）。

**移位组与组 3 都写标志**：legacy map 的 `C0`/`C1`/`D0-D3`（八个操作）一律 `dst_set |= XSET_FL`；`reg == 2 || 3`（`RCL`/`RCR`）另加 `src_set |= XSET_FL`（读 CF），`D2`/`D3` 另加 `src_set |= XSET_CL`。`F6`/`F7` 在 `reg != 2` 时 `dst_set |= XSET_FL`（`NOT`(/2) 不写标志），`/4`-`/7` 的累加器规则不变（`:1092-1144`）。这与 ALU 路径一致（`:446`/`:450`/`:578`/`:612`/`:618`）：凡写标志都要在 `dst_set` 里出现 `XSET_FL`。

**`rex` 口径统一**：`apply_modrm_usage`（`:297`）与 `apply_implicit_gp`（`:557`）都用 `(diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY)`，8 位寄存器命名不会因走哪一趟而不同。`gp_set` 对 `sz ∉ {1,2,4,8}` 返回 `XSET_OTHER`（`:89`），不会冒充 64 位。

**字节读取纪律**：所有读取都走 `cur_left`（`:17`）/ `get_byte`（`:24`）/ `peek_byte`（`:32`）。`cur_left` 双重夹取 `min(end - p, beg + XDE_MAXLEN - p)`；由于 `max_len` 已夹到 15，第二项实际永远不是较小者（死代码，但无害）。

**`C_BAD` 的来源**（共 4 类）：

1. 首两字节构成的 16 位小端字等于 `0x0000` 或 `0xFFFF`（`:799-803`）；
2. 同一类遗留前缀**重复出现**（`66`/`67`/段/`F2F3`/`F0`，`:815`/`:826`/`:834`/`:841`/`:848`）；
3. 表属性 `XA_BAD`（映射见 `:130`；语义 = 「**任何模式下都不可用的编码**」，模式受限的形式改由 `XA_I64`/`XA_O64`/`XA_F64`/`XA_D64` 表达，见 Known Gaps）；
4. `C6`/`C7`/`8F` 在 legacy map 下 `reg != 0` **且该 ModR/M 字节不等于 `0xF8`**（`:1107-1109`）——`C7 F8`/`C6 F8` 是合法的 `XBEGIN`/`XABORT`，其余 `/7` 编码（`C7 FA`/`C6 FA`/`C7 38`）才算坏。判据必须**字节精确**：写成 `reg != 0 && reg != 7` 会把所有非法 `/7` 一并豁免。

**前缀语义**：`66` 翻转 `defdata` 2↔4（`:813`）；`67` 在 64 位翻转 `defaddr` 8↔4、其余模式 2↔4（`:821-824`）；段前缀存 `p_seg`、`F2/F3` 存 `p_rep`、`F0` 存 `p_lock`。**每类只保留最后见到的字节**。注意前缀循环在 REX 判定**之前**跑完且 REX 只判一次，所以 `48 66 90` 会把 `48` 当 REX、再把 `66` 当 opcode——解码侧不拒绝非规范前缀顺序，但**重编码会按 SDM 组序规范化**（见 Known Gaps）。

### 编码类分派与歧义消解

前缀字节有歧义，各分支的判定门槛（每个都只看一两个前瞻字节）：

| 字节 | 两种解释 | 判定门槛 | 行 |
|------|----------|----------|-----|
| `62` | EVEX / BOUND | `peek(1..3)` 全成功 **且** `(b2 & 0x04)` **且**（`mode == 64` 或 `(b1 & 0xC0) == 0xC0`）→ 否则 BOUND | `:907-908` |
| `C4` `C5` | VEX2/VEX3 / LES/LDS | `mode == 64` 或 `(b1 & 0xC0) == 0xC0`（`C4` 还需第三字节）→ 否则 LES/LDS | `:956-959` |
| `8F` | XOP / POP r/m | `(b1 & 0x1F) >= 8` **且**（`mode == 64` 或 `(b1 & 0xC0) == 0xC0`）→ 否则 `8F /0` = POP r/m | `:1020` |
| `D5` | REX2 (APX) / AAD | 仅 64 位且 `b == 0xD5` → 否则按 AAD 走 legacy | `:872-903` |

三个向量前缀门槛（`62` / `C4`+`C5` / `8F`）写法一致：64 位下无条件成立，16/32 位下要求 `mod == 11b` 或等价的 `0xC0` 掩码；因此 16/32 位里 `8F 08`（mod ≠ 11）不再被当作 XOP，而是走非法 POP（长度 2，`C_BAD` 置位）。

门槛失败即落到 `parse_legacy_opcode`（定义 `:734-768`，调用 `:1051-1052`）。这就是 README 那句「`C4`/`C5`/`62`/`8F` 只有在后随字节符合前缀形式时才是 VEX/EVEX/XOP」的实现。

各编码类写回的结构字段：

| 类 | `enc` | `nvex` | `vex[]` | `map` 来源 | 其他 |
|----|-------|--------|---------|-----------|------|
| REX | 不改（仍是 LEGACY） | — | — | — | `rex` + `rex_w/r/x/b`，`C_REX` |
| REX2 | `XDE_ENC_REX2` | 2 | `D5, b1` | `(b1 & 0x80) ? 0F : LEGACY` | 读 bit6/5/4 → `rex_r4`/`rex_x4`/`rex_b4`，并置 `diza->rex = 0x40 \| (b1 & 0x0F)`（对寄存器命名等价于 REX）；`C_REX2 \| C_REX` |
| EVEX | `XDE_ENC_EVEX` | 4 | `62, P0, P1, P2` | `b1 & 7`（→ map 4-7） | `C_EVEX \| C_VEX`，`evex_r2/z/b/aaa`，`vex_vvvv` 含 `V'` 位；清 `p_66`/`p_rep` |
| VEX | `XDE_ENC_VEX2`(C5) / `XDE_ENC_VEX3`(C4) | 2 / 3 | 全前缀头 | `0F`(C5) / `b1 & 0x1F`(C4) | `C_VEX`；清 `p_66`/`p_rep` |
| XOP | `XDE_ENC_XOP` | 3 | `8F, b1, b2` | `b1 & 0x1F`（→ map 8-10） | `C_XOP`；清 `p_66`/`p_rep`（`:1040-1041`）；只写 `opcode`/`map`（`:1046-1047`） |

**`opcode2`/`opcode3` 的写入规则**：只有 `map ∈ {XDE_MAP_0F, XDE_MAP_0F38, XDE_MAP_0F3A}` 时才写（VEX `:1003-1011`、EVEX `:942-950`）；EVEX 的 map 4-7、VEX 的 map 7（`VEX7`）、XOP 的 map 8/9/A 都不写。XOP 与 VEX/EVEX 同一规则，不是缺口——`opcode`/`opcode2`/`opcode3` 只描述 `0F`/`0F 38`/`0F 3A` 这条链。

**`opcode2`/`opcode3` 的语义（按 map 决定谁是真 opcode）**：`map == XDE_MAP_0F` 时 `opcode2` **就是**真 opcode（`0F xx` 里的 `xx`）；`map == XDE_MAP_0F38`/`XDE_MAP_0F3A` 时 `opcode2` 存的是**转义字节** `0x38`/`0x3A`，真 opcode 落在 `opcode3`（写入顺序见 `parse_legacy_opcode` `:734-768`，VEX/EVEX 路径同构）。所以判 0F38/0F3A 指令必须读 `opcode3`——读者若在 `apply_modrm_usage` 里照 `opcode2` 判断，永远只会看到 `0x38`/`0x3A`（上一轮 MOVBE 缺陷的一部分就是这么漏的）。

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

`XA_*` 到 `C_*` 的映射在 `apply_attr_flags`（`src/xde.c:123-144`）：17 条一对一 OR，**只增不减**（读-改-写 `diza->flag`）。`XA_VVVV_GPR` 与 group 位不经此函数（`XA_VVVV_GPR` 在 `:405`/`:417`/`:426`/`:464`/`:685` 直接读，`XA_GRP_ID` 只在 `:1085` 读）。

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

尺寸→标志：1 → `C_DATA1`，2 → `C_DATA2`，**3 → `C_DATA1 \| C_DATA2`**，4 → `C_DATA4`，8 → `C_DATA8`，**6 → `C_DATA4 \| C_DATA2`**（`:1182-1187`）。

### 对象集推导（三趟）

1. `apply_modrm_usage`（`:294-552`）——ModR/M 的 reg/r/m 字段。`mod == 3` 走寄存器分支，否则内存分支。`rex` 启发式在 `:297`：`rex != 0 || enc != LEGACY`（即 VEX/EVEX/XOP/REX2 一律当作「有 REX」，影响 8 位寄存器命名）。reg 字段带扩展位组成 `regx = rex_r4<<4 | rex_r<<3 | reg`（`:304-305`），r/m 侧同理用 `rex_b4`（`:456-457`）；`int setcc = (map == 0F && c == 0x0F && c2 ∈ 90..9F)` 在 `:307`。`mod == 3` 的寄存器分支里，`dst` 白名单 = ALU / `MOV r/m` / 移位 / `F6`/`F7` / `FE`/`FF` / `80-83` / **`C6`/`C7`** / **`SETcc`** / **带 `XA_VVVV_GPR` 的 VEX/EVEX/XOP**（`:517-523`，原因见下段），而 `src` 赋值排除 `0x8D` **以及 MOV 存储形式 `0x88`/`0x89`/`0xC6`/`0xC7` 与 `SETcc`**（`:511-512`）——这几类只写 r/m，不读；内存分支同样处理（`src` 排除 `:535-538`、`dst` 白名单 `:541-548`）。**分组 opcode 另有一层 reg 字段分派**：`mov_crdr`（`:335-336`）、`rdrand`（`:340-341`）、`fence`（`:349-350`）、`fsgsbase`（`:351-353`）、`endbr`（`:355-356`）、`nop_ea`（`:370-372`）、`prefetch_ea`（`:373-376`）、`mem_store`（`:383-389`）八个判定先在 `:391-392` 折成 `reg_not_dst`，`mod == 3` 分支在 `:468-493` 按它们分派并让通用规则让位，内存分支在 `:535-540` 与 `:541` 用 `nop_ea`/`prefetch_ea`/`mem_store` 改写「有内存操作数就有寄存器读写」的默认假设。
2. `apply_usage_special`（`:146-292`）——隐式操作数：REP/串操作（`A4-A7`/`AA-AF`/`6C-6F`/`AC-AD`）、IO（`E4-E7`/`EC-EF`）、`SAHF/LAHF`、`CBW/CWD`、`AAA/AAS`、`AAM/AAD`、`PUSHA/POPA`、`PUSH/POP sreg`、`XLAT`、`ENTER/LEAVE`、`MOV` 段寄存器，以及 0F map 的 `CPUID`/`SHLD/SHRD`/`LSS` 等。**其 `attr` 形参未使用**（`(void)attr;` `:291`）。标志规则**按指令区分**（`:251-260`）：`A6/A7`（CMPS）、`AE/AF`（SCAS）、`FC/FD`（CLD/STD）→ `dst_set |= XSET_FL`；`p_rep` 且 `A6/A7/AE/AF` → `src_set |= XSET_FL`（REP 循环测 ZF）；`MOVS/STOS/LODS/INS/OUTS` 完全不动标志，**DF 有意不作为源**。`REP` 本身**只在串操作上**把 `CX/ECX/RCX` 计入 `src`+`dst`（`:158-168`），不再无条件置 `XSET_FL`、也不再把 `F2`/`F3` 当 REP 给每条 SSE 标量指令带上 `RCX`。0F 段的 `SETcc`（`c2 ∈ 90..9F`）读标志：`src_set |= XSET_FL`（`:284-286`）。I/O 的 DX 端口形式（`EC`/`ED`/`EE`/`EF`）四种都读 `DX`，因此**一律**记进 `src_set`——`EE`/`EF`（`OUT DX,AL/AX`）不进 `dst_set`；立即数端口形式 `E4-E7` 不含 `DX`（`:229-237`）。
3. `apply_implicit_gp`（`:554-642`）——opcode 隐含的 GPR：`INC/DEC r`、`PUSH/POP r`、`XCHG r8,eAX`、`MOV r,Iv`、`ALU AL/eAX, Iv`、`BSWAP`，外加 `XA_PUSH`/`XA_POP` 的栈列（`stack_set`：16 → `XSET_SP`，32 → `XSET_ESP`，64 → `XSET_RSP`，`:633-642`）。其 `rex` 判定在 `:557`，用的是与 `:297` **相同的** `(diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY)`。

**VEX/XOP 的 GPR 路径**：表里带 `XA_VVVV_GPR` 的条目表示 **`vvvv` 本身就是 GPR 操作数**，所以 `0` = `EAX` 合法。现在 `src/xde.c:403-413` 是单层 if/else：`XA_VVVV_GPR` → **无条件** `gp_set(dsz, vex_vvvv, 1)`（含 `0`）并把 EGPR 并进 `src_set2`（`:405-408`）；只有 `else`（真向量编码）才 `src_set |= XSET_OTHER; dst_set |= XSET_OTHER;`（`:409-412`）。该属性只标在 GPR 操作数上，所以取 `vvvv` 不是哨兵判断（注释 `:398-402`）。早前修掉的两处：① 原实现把整个分支包在 `if (vex_vvvv != 0 && vex_vvvv != 0xF)` 里，`andn eax,eax,ecx`（`C4 E2 78 F2 C1`，vvvv=EAX）因此丢掉 EAX 源；② load-form 的「reg 字段是目的寄存器」白名单只枚举了 0F map 的 `0F 40-4F`/`AF`/`BC`/`BD`/`B8`（及 `8A`/`8B`/`8D`），而 BMI/BMI2 经 **VEX 0F38/0F3A 与 XOP** 到达解码器，`andn`/`bextr`/`sarx`/`bzhi`/`mulx`/`rorx` 等的目的寄存器只报成 `XSET_OTHER`；现在条件加了 `(attr & XA_VVVV_GPR)`（该属性本就表示「reg 字段是目的寄存器」，注释见 `:415-416`），并在 `:425-426` 的编码类判定里同样放行——回退任一处后 `andn`/`bextr`/`sarx` 各 2 条断言失败（共 6 条）。**上一轮又在同一处修掉两件**：① **`XA_VVVV_GPR` 指令的 `XSET_OTHER` 假阳性**——原实现无条件先 `src_set |= XSET_OTHER; dst_set |= XSET_OTHER;`，于是 ANDN/BEXTR/BZHI/SARX/SHLX/SHRX（表侧 `XA_VVVV_GPR` 落在 `m2` 的 `0xF2`/`0xF3`/`0xF5`/`0xF6`/`0xF7`、`m3[0xF0]`、`m9` 的 `0x01`/`0x02`/`0x90-0x9B`、`ma` 的 `0x10`/`0x12`）以及 XOP map 9/A 的 `vvvv` 形式（**操作数全为 GPR**）被硬塞 `OTHER`，与 legacy-SSE 那处同类（见下段第 1 条）；现该分支只加 `gp_set(vvvv)` + `src_set2`，只有 `else`（真向量编码）才折 `OTHER`。`apply_modrm_usage` 的内存分支（`:684-686`，`!(attr & XA_VVVV_GPR)` → 才折 `OTHER`）早有同一门槛，上一轮补的是同趟里漏掉的那处，风格与之一致。② **死支 + 错误哨兵规则**——删掉的 `else if (diza->vex_vvvv != 0 && diza->vex_vvvv != 0xF)` 是**死代码**（`src_set |= OTHER` 已在上方无条件置位，再置不可观测）；证据是差分扫描而非推理：对 **623,360** 条 VEX2/VEX3/EVEX/XOP 解码（mode 64）算 FNV-1a 哈希（`len` + src/dst + src2/dst2 + flag），HEAD = `BEB1E61249EEA0A3`、删掉该支后 = **同一哈希**（逐字节 no-op）；对照非空转——HEAD ≠ 上一轮修复后 `220302639237A323`。旧注释的断言也是错的：VEX.vvvv 倒序存储，**只有解码值 0**（编码位全 1）才表示「无 vvvv」，解码值 `0xF`（编码 0000b）是合法的 `XMM15`/`mm7`，EVEX 带 `V'` 时 16-31 也合法——旧注释把 `0xF` 也当哨兵。mutation 证据：M1（恢复无条件 `OTHER`）→ **13 条**断言失败（六个 BMI 族各 `no src other`/`no dst other` 共 12 条 + `evex andn no src other`）；M2（撤掉 `gp_set(vvvv)`）→ **10 条**失败（`andn`/`bextr`/`sarx` 的 3 条旧断言 + 六族各 `src vvvv` + `evex andn src vvvv R16`）。

**上一轮另外修掉四处缺陷（同一趟 `apply_modrm_usage`）**：

1. **legacy SSE/MMX 的操作数被当成 GPR（影响最大）**：新增 `int simd_0f`（`:317-329`）——`map == XDE_MAP_0F && enc == XDE_ENC_LEGACY` 且 `opcode2` **不在**枚举的 GPR 名单内者，其操作数按 SIMD 折成 `XSET_OTHER`，与向量编码同一口径。名单：`0F 20-23`（MOV Rd,Cd/Dd）、`A3`（BT）、`A4`/`A5`（SHLD）、`AB`（BTS）、`AC`/`AD`（SHRD）、`AF`（IMUL）、`B0`/`B1`（CMPXCHG）、`B2`（LSS）、`B3`（BTR）、`B4`（LFS）、`B5`（LGS）、`B6`/`B7`（MOVZX）、`B8`（POPCNT，带 `F3`）、`BA`（组 8）、`BB`（BTC）、`BC`（BSF）、`BD`（BSR）、`BE`/`BF`（MOVSX）、`C0`/`C1`（XADD）、`C3`（MOVNTI）、`40-4F`（CMOVcc）、`90-9F`（SETcc）。两个使用点：`mod == 3` 分支把 `rset` 改成 `XSET_OTHER`（`:462-467`）、目的寄存器走 `dst |= XSET_OTHER`（条件 `:417`，simd 分支 `:423-424`）。修前 `F3 0F 10 C1`（`movss xmm0,xmm1`）的 `src` 是 **RCX**（假阳性），现为 `XSET_OTHER`。**取舍**：名单漏掉的 opcode 会**丢失** GPR 归属（假阴性），而不再产生假阳性——因为 0F map 里 SIMD 远多于 GPR。名单是**硬编码**（`:321-329`），新增 0F-map GPR 指令时必须同步，否则该指令的 r/m 会被误记为 `OTHER`。
2. **`F2`/`F3` 被当成 REP 前缀**：原实现任何 `p_rep` 都无条件把计数寄存器计入 src+dst，于是每条 SSE 标量指令、`PAUSE`、`ENDBR`、`CRC32` 都会带上 `RCX`（`F3 0F 1E FA` 亦然）；现只在**串操作**（`A4-A7`/`AA-AF`/`6C-6F`/`AC-AD`）上计入（`:158-168`）。
3. **MOVBE 载入形式的 reg 目的缺失**：`0F 38 F0` 不在 load-form 的 opcode 名单里（该名单只枚举 `8A`/`8B`/`8D` 与 0F map 的若干），于是 `movbe eax,[rax]` 的 `dst` 为空。新增 `reg_dst`（`:312-314`，`map == XDE_MAP_0F38 && (opcode3 == 0xF0 || (opcode3 == 0xF1 && p_rep != 0))`）并加进 load 条件（`:417`）。
4. **MOVBE 存储形式的 reg 源与内存目的缺失**：`reg_src`（`:315-316`，`opcode3 == 0xF1 && p_rep == 0`）加进 store-form 的 src 条件（`:434`）与内存分支的 dst 白名单（`:541`）。注意 `0F 38 F1` 的 reg 角色取决于前缀：无前缀是 MOVBE（reg=源），带 `F2`/`F3` 是 CRC32（reg=目的）——`CRC32` 的 `dst` 修前也不对（被 REP 规则塞进 `RCX`，见第 2 条）。

mutation 证据：四处一起回退 → 11 条断言失败；另有一条**写弱的断言**被修正（`movbe [rax],eax src EAX` 即使漏掉 reg 源也会通过，因为基址寄存器 `RAX` 含 `XSET_EAX` 位），改用非 EAX 基址（`0F 38 F1 02` = `movbe [rdx],eax`）后单独验证，回退即失败。

**本轮修掉的 7 组分组 opcode 缺陷（同趟 `apply_modrm_usage`，外加 `xde_disasm_buf` 的一处 `C_BAD` 判据）**——每组都按「先写断言看到失败 → 改 → 通过 → mutation 回退即失败」走：

1. **`0F 20-23` MOV CR/DR 的 src/dst 整个反了（最重）**：新增 `mov_crdr`（`:335-336`），`mod == 3` 分支按 `/20 /21`（CR/DR → r/m）与 `/22 /23`（r/m → CR/DR）分派（`:468-478`），CR/DR 侧折 `OTHER`。修前 `mov eax,cr0` 报 `src={RAX}`、`dst={}`（EAX 明明是目的却记成源，`CR0` 整个丢掉）；现为 `src=OTHER`、`dst=RAX`，`mov cr0,eax` 补上 `dst=OTHER`。mutation M1（`mov_crdr = 0`）→ 9 条断言失败。
2. **`0F C7 /6`/`/7` mod=3 = RDRAND/RDSEED 的假 `OTHER`**：新增 `rdrand`（`:340-341`），只写 `dst` = r/m GPR、没有源（`:479-482`）。mutation M2 → 8 条失败。
3. **`0F AE` mod=3**：LFENCE/MFENCE/SFENCE（`:349-350`，`reg >= 5`）**无操作数**，两侧清空；`F3 0F AE /0-/3`（`:351-353`）是 RDFSBASE/RDGSBASE（写 r/m）/ WRFSBASE/WRGSBASE（读 r/m、写 FS/GS base = `OTHER`），按 `reg <= 1` 分派（`:483-493`）。mutation M3（`fence = 0`）→ 6 条、M4（`fsgsbase = 0`）→ 8 条失败。
4. **`0F AE`/`0F C7` 内存**写**形式的 `dst` 漏 `MEM`**：新增白名单 `mem_store`（`:383-389`）并加进内存分支的 dst 条件（`:541`）。修前 `fxsave`/`stmxcsr`/`xsave`/`xsaveopt`/`clwb`/`cmpxchg8b`/`cmpxchg16b`/`xrstors`/`xsavec`/`xsaves` 的 `dst` 为空（`src` 反倒有 `MEM`），现 `dst` 含 `MEM`；**只读对照**（`fxrstor`/`ldmxcsr`/`xrstor`/`vmptrld`/`vmptrst`）保持无 `MEM`。mutation M8 → 10 条失败。
5. **NOP / prefetch / ENDBR 的对象集**：`nop_ea`（`:370-372`）覆盖 `0F 18 /4-/7`、`0F 19`、`0F 1D`、`0F 1F`——这些**不访问操作数**，`src`/`dst` 清空；`prefetch_ea`（`:373-376`）覆盖 `0F 18 /0-/3`（PREFETCHNTA/T0/T1/T2）与 `0F 0D /0`（PREFETCHW）——**只读内存**，保留 `MEM`、去掉 `OTHER`；`F3 0F 1E FA` ENDBR64（`:355-356`）两侧清空（上一轮只钉了「无 `RCX`」，没钉 `OTHER`）。**有意保留寻址寄存器**：`parse_modrm` 对所有内存操作数统一记录基址/索引，所以 `nop [rax]` 的 `src` 仍有 `RAX`——这是引擎的统一口径，不是遗漏。
6. **`0F B2`/`B4`/`B5`（LSS/LFS/LGS）缺 GPR 目的**：0F-map 的目的白名单里有 `B6`/`B7`/`BE`/`BF` 却漏了这三个（`:419-422` 的 `c2` 列表）→ `lss eax,[rax]` 的 `dst` 现含 `RAX`（+`OTHER`），与 MOVBE 载入形式同类。
7. **`C7 F8`（XBEGIN）/ `C6 F8`（XABORT）被误标 `C_BAD`**（两者都是合法指令）：成因是 `C6`/`C7`/`8F` 的 `reg != 0` 判据，现改用**字节精确**判据 `mpeek != 0xF8`（`:1107-1109`）；`reg != 0 && reg != 7` 那种写法会把 `C7 FA`/`C6 FA`/`C7 38` 等非法 `/7` 编码一并豁免（mutation M10b：`C7 FA`/`C6 FA` 的「仍应 bad」断言失败）。mutation M10（恢复 `reg != 0`）→ 2 条失败。

**此后又推进两批（批 A 已提交 `78aaca1`，批 B 随本文档一起提交）——全在 `apply_modrm_usage` 的谓词表，外加 `xde_disasm_buf` 新增的一处 `C_BAD` 判据，仍未动表**——每条同样按「先写断言看到失败 → 改 → 通过 → mutation 回退即失败」走：

1. **`0F 1E`（无 `F3`）是 NOP Ev**（与 `0F 1F` 同类）：mod≠3 与 mod=3 都**不访问操作数**。新增 `nop1e`（判据 `map0F && LEGACY && c2 == 0x1E && p_rep == 0`，`:361-362`），加入 `reg_not_dst`（`:391-392`）、mod=3 的空操作数分支与内存分支的 `src` 排除（`!nop1e`，`:535`）。mutation → 6 条失败（断言在 `tests/xde_test.c:1379-1389`）。
2. **`F3 0F 1E /0`（RDSSPD/RDSSPQ）**：r/m 是**目的 GPR**、SSP 折 `OTHER`——`rdssp`（`:363-364`）在 mod=3 分支 `src |= OTHER`、`dst |= rset`（`:503-507`）。mutation → 4 条失败。
3. **`0F 0D /1`（PREFETCHWT1）去掉假 `OTHER`**：`prefetch_ea`（`:373-376`）的 reg 判据从 `reg == 0` 扩到 `reg ∈ {0,1}`，与 `/0`（PREFETCHW）同口径——**只读内存**。mutation → 2 条失败。
4. **`0F C7 /7` mod≠3（VMPTRST）的 `dst` 补 `MEM`**：`mem_store`（`:383-389`）的 `0F C7` 列表加上 `reg == 7`（VMPTRST **写** VMCS 指针到内存）；`VMPTRLD /6` 实测只读、**不动**。**旧断言 `vmptrst (no dst M)` 删除而非改写**。mutation → 1 条失败。
5. **`0F AE` mod=3 的非法子形式 → `C_BAD`**：无 `F3` 的 `/0-/3` 与 `/4` 在表里仍是内存形式的编码，mod=3 时没有任何定义，原先当普通 `OTHER|OTHER` 指令报出；`xde_disasm_buf` 新增**字节精确**判据 `opcode == 0x0F && map == XDE_MAP_0F && opcode2 == 0xAE && (mpeek >> 6) == 3 && reg <= 4 && p_rep != 0xF3`（其中的 `p_rep!=0xF3` 就是 `F3` 豁免条件；`:1110-1117`）。mutation → 5 条失败；另加 9 条「不得误伤」对照（`lfence`/`mfence`/`sfence`/四个 FS/GS base 形式/`incsspd`/`fxsave` 内存形式都不带 `C_BAD`，`tests/xde_test.c:1248-1261`）。
6. **NOP Ev 的 mod=3 形式**（`{0F,1F,C0}`/`{0F,19,C0}`/`{0F,1D,C0}`/`{0F,18,E0}`）与 mod≠3 一致清空：`nop_ea`（`:370-372`）去掉 `mod != 3`，mod=3 分支与 `fence`/`endbr`/`nop1e` 共用空操作数分支。mutation（重新插回 `mod != 3 &&`）→ 8 条失败（断言在 `tests/xde_test.c:1359-1366`）。
7. **`F3 0F AE /5`（INCSSPD/INCSSPQ）**：读 r/m GPR、写 SSP（折 `OTHER`）。新增 `incssp`（`:347-348`，`map0F && LEGACY && c2 == 0xAE && mod == 3 && reg == 5 && p_rep == 0xF3`），`fence` 判据改成 `reg >= 5 && !incssp`（`:349-350`），加入 `reg_not_dst`（`:391-392`）与 mod=3 分支（`:497-502`）；`C_BAD` 保持 0。mutation A（去掉 `!incssp`）→ 4 条、B（`dst |= OTHER|rset`）→ 2 条；断言在 `tests/xde_test.c:1263-1276`。

**有意取舍（INCSSP 的 `src`）**：SSP 是读-改-写，所以 `src |= XSET_OTHER` 也有一说（对照 `rdsspd` 确实置 `src OTHER`）；实现严格按「`src` = r/m GPR、`dst` = `OTHER`」，并用断言 `incsspd (no src other)` 把这个选择钉住。改这里要先定口径，不要顺手补 `src OTHER`。

**方法论注记（写断言时的判别力陷阱）**：`wrfsbase dst other` / `wrgsbase dst other` 两条断言**没有判别力**——旧路径本来也会往 `dst` 里多置 `OTHER`，这组的判别力全在 `src` 侧（`wrfsbase src RAX` + `wrfsbase (no src other)`）。**判据要按「新旧实现结果是否不同」来挑，而不是按「看起来该成立」来挑**；同理，`movbe [rax],eax src EAX` 那种用 EAX 作基址的写法也会被 `XSET_EAX` 位掩盖（见上一段的 mutation 证据）。

**REX2 的 r/m 是 GPR，不是向量寄存器**：`parse_modrm` 的 SIB 分支（`:684-686`）与 `apply_modrm_usage` 的内存分支（`:549-550`）在判断「是否按向量寄存器记 `XSET_OTHER`」时都会排除 `XDE_ENC_REX2`；`mod == 3` 分支同样排除（`:462-467`）。另外 `parse_modrm` 的两个「无基址」判定——SIB 的 `mod == 0 && base == 5`（`:682`）与 `mod == 0 && rm == 5`（`:701`）——在 `rex_b4` 置位时不再成立，此时它指的是真寄存器 r21，不是 disp32。

`gp_set`（`:43-90`）的寄存器列映射：`reg > 31 → XSET_OTHER`（**解码路径不可达**，`reg` 由 5 位扩展位拼出，上限 31）；`reg >= 16` 交给其第 4 个形参 `uint64_t *egpr`——EGPR 写进**第二对象集字**（`*egpr |= XSET2_R16 << (reg - 16)`，`:66-72`），固定寄存器处传 `NULL`；`reg >= 8` 分两支：首字仍返回**宽度无关**的 `XSET_R8 << (reg-8)`，并且当 `sz <= 1`（8 位形式 r8b-r15b）时**额外**写入第二字的 `XSET2_R8B << (reg-8)`（`:73-79`）——即**叠加**而非替换：`XSET_R8..XSET_R15` 照旧，第二字另有 8 位宽度位；`sz <= 1` 时按 `rex` 选 `lo8_norex`（AL/CL/DL/BL/**AH/CH/DH/BH**）或 `lo8_rex`（AL/CL/DL/BL/**SPL/BPL/SIL/DIL**——独立的 `XSET_SPL/BPL/SIL/DIL` 位，不再复用 16 位那几位）；`sz` 2/4/8 → `w16`/`w32`/`w64`，**`sz ∉ {1,2,4,8}` 返回 `XSET_OTHER`，不冒充 64 位**（`:87-89`）。

### 编码方向（`xde_asm`）

**不做校验、不解码**；`xde_asm_buf`（`src/xde.c:1240-1289`）只多做一次容量检查，`xde_asm`（`:1291-1294`）是 `return xde_asm_buf(opcode, XDE_MAXLEN, diza);` 的薄包装。固定顺序拼接：

```
max_len 夹取：0 或 > XDE_MAXLEN 一律按 15                        (:1247-1248)
→ 计数夹到数组容量：nvex ≤ 4 / naddr ≤ 8 / ndata ≤ 8            (:1251-1253)
→ asm_size() 算所需字节数，> max_len 则 return 0                 (:1212-1238, :1255-1256)
p_lock → p_rep → p_seg → p_66 → p_67    （SDM 组序 1→2→3→4，:1263-1267）
  ├─ 若 nvex：vex[0..nvex-1] → opcode   （此路径不发 rex）
  └─ 否则：rex → opcode
             └─ 若 opcode == 0x0F：opcode2 →（若 opcode2 ∈ {38,3A}）opcode3
→ flag & C_MODRM ? modrm
→ flag & C_SIB   ? sib
→ naddr 个 addr_b[]
→ ndata 个 data_b[]
```

**容量安全**：`addrsize`/`datasize`/`nvex` 是 `uint8_t`，被写坏时解码侧的计数会越过 `addr_b[8]`/`data_b[8]`/`vex[4]`（真 UB）。`xde_asm_buf` 先把这三个计数夹到数组容量（`:1251-1253`），再用 `asm_size()`（`:1212-1238`）算出所需字节数，`> max_len` 就返回 `0`（`:1255-1256`）；`max_len == 0` 或 `> XDE_MAXLEN` 一律按 `XDE_MAXLEN` 处理（`:1247-1248`），与 `xde_disasm_buf` 的 `max_len` 处理同构。所以解码成功的指令（`len ≤ 15`）经 `xde_asm` 永远编得出来。

`p_66` 现在与其它遗留前缀一起在 `:1266` 统一发射（没有 nvex 专属分支）：这是为 REX2 准备的——REX2 是唯一**保留** `p_66`/`p_rep` 的编码类（VEX/EVEX/XOP 在解码时就清掉，`:972-973`/`:995-996`/`:935-936`/`:1040-1041`），而它们的 `vex[]` 头自带 pp 字段，不走 `p_66`；所以 `66 D5 …` 现在能字节级往返。

它只读 `nvex`/`vex[]`、`p_seg/p_lock/p_rep/p_67/p_66/rex`、`opcode/opcode2/opcode3`、`modrm`、`sib`、`addr_b+addrsize`、`data_b+datasize`，**完全不看 `map` / `enc` / `defdata` / `defaddr` / `len` / 对象集**。因此：

- 手搓一个 `nvex == 0` 但 `map == XDE_MAP_0F38` 的结构体，`xde_asm_buf` 不会补出 `0F 38` 前缀——它只信字节字段；
- 返回 `0` 只有两种情形：`!opcode || !diza`（`:1245-1246`），或所需字节数 `> max_len`（`:1255-1256`）；解码成功的指令不可能编出 0 字节。

### 设计不变量（改动前必读）

1. **低 32 位兼容 XDE 1.02**：`flag` / `src_set` / `dst_set` 的低 32 位语义（EAX-EDI）冻结；64 位宽度位、R8-R15、RIP、编码类标志全在高半（`include/xde.h:106-182`）。新增的 `XSET_SPL/BPL/SIL/DIL` 占用的正是 1.02 显式保留的 `XSET_rsrv1..4` 位（低位 26/27/30/31，`xde102/xde.h:102-105`），没有改变任何 1.02 已定义位的语义；`r16-r31` 放不进首字——低位冻结、高半已被 64 位宽度位 / R8-R15 / RIP 占去，仅剩 15 位空闲（bit 49-63）< 需要的 16 位——因此落在第二字 `src_set2`/`dst_set2`（`XSET2_*`）。新增能力**不得挪用低位**。
2. **零堆分配**：`struct xde_instr` 由调用方提供，游标在栈上。解码路径不得引入 `malloc`/`strdup`。
3. **失败统一返回 0**：不要引入状态枚举——`if (!n)` 遍布整个调用面。
4. **`src/xdetbl.c` 是生成物**：首行即 `Auto-generated by tools/gen_tables.py - do not edit by hand.`（`:1`），永不手改。
5. **`xde_instr` 的字段来源**：成功解码无条件写 `mode`/`defaddr`/`defdata`/`len`/`map`/`enc`/`opcode`/`flag`/`src_set`/`dst_set`/`src_set2`/`dst_set2`，以及 `rex_r4`/`rex_x4`/`rex_b4`（REX2 分支写入；非 REX2 编码保持 `memset` 后的 0，因为此时不存在 R4/X4/B4 位）；`addrsize`/`datasize`/`p_*`/`sib`/`opcode2`/`opcode3`/`vex[]`/`evex_*` 只在对应特征出现时写入（无特征时保持 `memset` 后的 0）。

## Key Directories

| 路径 | 用途 | 关键符号 |
|------|------|----------|
| `include/xde.h` | 唯一公共头（309 行）：宏词汇表 + `struct xde_instr`（`:217-279`） + 8 个 API（`:285-299`） | `XDE_MODE_*`, `XDE_ENC_*`, `XDE_MAP_*`, `C_*`, `XSET_*`, `XSET2_*` |
| `src/xde.c` | 解码器 + 编码器（1294 行），全部核心逻辑 | `xde_disasm_buf:770`, `xde_asm_buf:1240`, `xde_asm:1291`, 13 个 static 助手 |
| `src/xdetbl.c` | **机器生成**的属性表（414 行） | `xde_attr:5`, `xde_group:382` |
| `src/xdetbl.h` | 手写的表层契约（86 行） | `XA_*`, `enum xde_group_id`, `XDE_MAP_COUNT 11` |
| `src/xde_text.c` | 调试打印（165 行，3 个函数） | `xde_sprintfl:7`, `xde_sprintset:51`, `xde_sprintset2:132` |
| `tools/gen_tables.py` | 表生成器（682 行），唯一写出 `src/xdetbl.c` 的地方 | `OUT:9`, `MAPS:545`, `check_header:593`, emit 块 `:655-675` |
| `tests/xde_test.c` | 唯一测试文件（1614 行） | 6 个 `expect_*` 助手, `main:161` |
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

退出码 `0` = 全通过，`1` = 有失败。`main` 是 `int main(void)`，**没有任何 CLI 参数**（无 filter / verbose / 单用例选择），每次运行都执行全部 582 项检查。

### 重新生成属性表

```bat
python tools\gen_tables.py
```

只写 `src/xdetbl.c`（`OUT` 定义在 `tools/gen_tables.py:9`），**不生成 `src/xdetbl.h`**——头文件是手写的，改常量要两边同步。生成前会先跑 `check_header()`（`tools/gen_tables.py:593-638`，调用 `:677`）：解析 `src/xdetbl.h` 的 `XA_*`、`enum xde_group_id`（按位置）与 `#define XDE_MAP_COUNT`，与脚本镜像常量逐名比对，不一致就列出差异并 `SystemExit(1)`，**在写文件之前**退出。脚本无参数、无 `--check`、无外部输入文件（全部表数据以 Python 字面量内联在 `:90-544`），仅依赖标准库（`import os`），需要 Python 3.7+（用了 `from __future__ import annotations` 与 f-string）。以 `newline="\n"` 写文件（`:680-681`），保持这一点以免重新生成时行尾抖动。

**普通构建不需要跑生成器**：`src/xdetbl.c` 已入库（`.gitignore` 未排除）。

### 消费者接入（仓库无 lib 目标，需自行拼装）

```bat
cl /nologo /W3 /O2 /TC /std:c11 /Iinclude /Isrc /c src\xde.c src\xdetbl.c src\xde_text.c
```

### 不存在的工具

无 CMake、无 Makefile、无 `msbuild`/`devenv` 调用、无 lint/format 配置、无 CI（`.github/workflows` / appveyor / azure-pipelines / travis / circleci 全部不存在）。

## Code Conventions & Common Patterns

- **语言**：纯 C11（`<stdint.h>`）。头文件带 `extern "C"` 守卫（`include/xde.h:10-12`、`:305-307`），可被 C++ 包含。为 nameless union 用 `#pragma warning(push/disable: 4201)` 包住（`:16-19`、`:301-303`）。
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
- **注释风格**：行内 `//`，多用于标注特例原因，例：`// 32-bit GP writes zero-extend in 64-bit mode.`（`:394`）、`// NOT (/2) writes no flags`（`:1120`）、`// prefetches do read memory`（`:540`）、`// segment override`（`:536`）。**全树没有任何 `TODO`/`FIXME`/`XXX`/`HACK`/`NOTE` 标记**（`src/` 已核）。
- **表纪律**：列宽固定，`xde_attr` 每行 8 个 `0x%08X`，每组带 `// legacy` / `// 0F` 之类行尾注释。
- **对象集的打印策略**（`xde_sprintset`，`src/xde_text.c:51-129`）：按列从宽到窄取第一个命中——含有 `RAX` 就绝不打印 `EAX`/`AX`；16 位列之后还有 8 位扩展寄存器兜底（`SP`→`SPL`、`BP`→`BPL`、`SI`→`SIL`、`DI`→`DIL`，`:92-110`）；`XSET_UNDEF` 用**子集判定** `(set & XSET_UNDEF) == XSET_UNDEF`（`:55`）特判为 `"???"`。第二对象集字由 `xde_sprintset2` 打印（`src/xde_text.c:132-165`）：`(set2 & XSET2_ALL) == XSET2_ALL`（`:142`）特判为 `"???"`，否则先逐位输出 `R16`…`R31`（`:147-152`），再逐位输出 `R8B`…`R15B`（`:153-161`）。两处都是子集判定，`set2` 带 bit ≥ 24 的杂位也不会破坏 undef 标记。

### `struct xde_instr` 字段参考

| 组 | 字段 | 说明 |
|----|------|------|
| 模式 | `mode` `defaddr` `defdata` | `mode` = 16/32/64；`defaddr` = 2/4/8（`67` 翻转）；`defdata` = 2/4/8（`66` 翻转，`REX.W`/`VEX.W` → 8） |
| 派生 | `len` `addrsize` `datasize` | 总长（1-15）；位移/moffs 字节数；立即数字节数 |
| 分类 | `enc` `map` | `XDE_ENC_*` / `XDE_MAP_*` |
| 语义 | `flag` `src_set` `dst_set` `src_set2` `dst_set2` | 64 位；`flag`/`src_set`/`dst_set` 低 32 位兼容 1.02；第二字 `src_set2`/`dst_set2`（`:231-232`）装 APX EGPR `r16-r31`（`XSET2_R16..R31`）与 8 位 `r8b-r15b` 宽度位（`XSET2_R8B..R15B`） |
| 前缀 | `p_lock` `p_66` `p_67` `p_rep` `p_seg` `rex` | 存原始字节值（无则 0） |
| 向量头 | `nvex` `vex[4]` | `nvex` = 0/2/3/4；`vex[]` 存原始前缀字节，便于 `xde_asm` 原样回放 |
| opcode | `opcode` `opcode2` `opcode3` | 首字节（双字节 `0F` 时存 `0x0F`）/ 第二 / 第三 |
| 寻址 | `modrm` `sib` | 原始字节 |
| 位展开 | `rex_w` `rex_r` `rex_x` `rex_b` `rex_r4` `rex_x4` `rex_b4` `vex_pp` `vex_l` `vex_vvvv` `evex_z` `evex_b` `evex_aaa` `evex_r2` | 已解码的位；`rex_r4`/`rex_x4`/`rex_b4`（`:250`）是 REX2 的 EGPR 扩展位（ModR/M.reg / SIB.index / r/m 的 bit 4），非 REX2 编码恒为 0；`vex_vvvv` 是**非取反**后的值（0-31） |
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

`flag` 低位（兼容 1.02，`include/xde.h:56-103`；`:53-55` 的注释列出 2.00 **从不置位**的 1.02 词表项——`C_SPECIAL`、`C_DATA66`、`C_SRC_*`/`C_DST_*` 操作数微位与其 `C_MOD_*` 别名、`C_ERROR`——尺寸请读 `addrsize`/`datasize`/`p_66`）：`C_ADDR1/2/4`、`C_MODRM`、`C_SIB`、`C_ADDR67`、`C_DATA66`、`C_UNDEF`、`C_DATA1/2/4`、`C_BAD`、`C_REL`、`C_STOP`、`C_OPSZ8`、`C_SRC_FL`/`C_DST_FL`、`C_SRC_REG`/`C_SRC_RM`/`C_DST_REG`/`C_DST_RM`、`C_SRC_ACC`/`C_DST_ACC`、`C_SRC_R0`/`C_DST_R0`、`C_PUSH`/`C_POP`，以及 `C_CMD_*`（`CALL`/`JMP`/`JCC`/`RET`，占 `0x08000000`-`0x80000000`，用 `XDE_CMD(fl)` 提取）。复合宏 `C_MOD_FL`/`C_MOD_REG`/`C_MOD_RM`/`C_MOD_ACC`/`C_MOD_R0`。

`flag` 高位（2.00 扩展，`:106-118`）：`C_DATA8` `C_ADDR8` `C_RIPREL` `C_REX` `C_VEX` `C_EVEX` `C_XOP` `C_REX2` `C_I64` `C_O64` `C_F64`（forced-64，Intel 在 64 位忽略 `Jz` 上的 `66`）`C_D64`（默认 64 位操作数，PUSH/POP）`C_3DNOW`。

对象集（`src_set` / `dst_set`）：低 32 位 GPR/标志/内存/设备（`XSET_AL`…`XSET_EDI`、`XSET_FL`、`XSET_MEM`、`XSET_OTHER`、`XSET_DEV`，及 `XSET_ALL16`/`XSET_ALL32`）；其中 `XSET_SPL`/`XSET_BPL`/`XSET_SIL`/`XSET_DIL` 占低位 26/27/30/31（`include/xde.h:148-151`）——正是 1.02 的 `XSET_rsrv1..4`，`lo8_rex` 用它区分 SPL/BPL/SIL/DIL 与 16 位的 SP/BP/SI/DI；高半为**64 位宽度位**（`XSET_RAX` = `XSET_EAX | 高半一位`，…、`XSET_ALL64`）、`XSET_R8`…`XSET_R15`（每寄存器一位，不区分宽度）、`XSET_RIP`；`XSET_UNDEF` = 全 1。

第二对象集字（`src_set2` / `dst_set2`，`include/xde.h:184-214`）含**两组**位：

- `XSET2_R16`…`XSET2_R31` —— bit 0-15，每位一个 APX EGPR，不区分宽度（同 `XSET_R8`…`XSET_R15` 的口径）；
- `XSET2_R8B`…`XSET2_R15B`（`:206-213`）—— bit 16-23，`r8b`-`r15b` 的 8 位**宽度**位，与首字 `XSET_R8`…`XSET_R15` **叠加**（首字只报宽度无关的寄存器号，第二字补宽度）。

`XSET2_ALL`（`:214`）已随之扩为 **24 位全 1**（`0x0000000000FFFFFFULL`），即 `XA_UNDEF` 时 `src_set2`/`dst_set2` 的整体赋值标记。首字剩余位不足以再放 16 个寄存器，所以 EGPR 单独占一个字。

## Important Files

| 文件 | 说明 |
|------|------|
| `include/xde.h:285-299` | 8 个导出函数声明 + 语义注释 |
| `include/xde.h:217-279` | `struct xde_instr` |
| `include/xde.h:184-214` | `XSET2_*` 词汇（第二对象集字：APX `r16-r31` + 8 位 `r8b-r15b` 宽度位） |
| `src/xde.c:770-1199` | `xde_disasm_buf` —— 改解码行为从这里读起 |
| `src/xde.c:1240-1289` | `xde_asm_buf` —— 字节重组 + 容量检查（`xde_asm` 是它的包装，`:1291-1294`） |
| `src/xde.c:1212-1238` | `asm_size` —— 估算编码所需字节数，与 `xde_asm_buf` 的落地循环必须同步 |
| `src/xde.c:907-908` / `:956-959` / `:1020` | 三处编码类歧义门槛 |
| `src/xde.c:123-144` | `XA_*` → `C_*` 映射表（新增属性位必改） |
| `src/xde.c:146` / `:294` / `:554` | 三趟对象集推导 |
| `src/xde_text.c:132` | `xde_sprintset2` —— 第二对象集字的打印器 |
| `src/xde_text.c:7` | `xde_sprintfl` —— flag 打印器（新增 flag 覆盖时改这里） |
| `src/xdetbl.h:9-45` | `XA_*` 位定义 + IMM / GRP 位移 |
| `src/xdetbl.h:47-81` | `enum xde_group_id`、`XA_GRP`、`XDE_MAP_COUNT 11` |
| `tools/gen_tables.py:9` / `:90-544` / `:655-675` | 输出路径 / 全部表数据 / emit 块 |
| `tools/gen_tables.py:593-638` | `check_header` —— 生成前的 `xdetbl.h` 一致性校验（不一致即拒绝生成） |
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
- **警告级别**：`/W3`（`Level3`）。注意 `/W3` **不会**报未使用的参数（那是 `/W4` 的 C4100）——历史上的死形参问题就是这么漏掉的（`expect_enc` 的 `n` 现已真正使用，`:51`）。
- **生成文件已入库**：`src/xdetbl.c` 在版本控制内（`.gitignore` 只排除 `build/`、`*.obj`、`*.exe`、`*.pdb`、`*.ilk`、`*.idb`、`*.suo`、`*.user`、`.vs/`、`x64/`、`Debug/`、`Release/`）。

## Testing & QA

### 框架与结构

**自研极简 harness，无第三方框架，无 `ASSERT`/`CHECK`/`TEST` 宏**。全部逻辑在 `tests/xde_test.c`（1614 行）的单个 `int main(void)`（`:161-1614`）中：115 个匿名 `{ }` 块，每块一个 `static const uint8_t` 向量紧跟 `expect_*` 调用。**零文件 IO、零 fixture、零 golden 文件**。

断言助手（括注是本轮实测的调用次数）：

| 助手 | 行 | 契约 |
|------|-----|------|
| `fail` | `:10-14` | 打印 `FAIL <name>: <msg>`，`g_fail++` |
| `hexbytes` | `:16-25` | 字节数组 → 空格分隔 `%02X` 串 |
| `expect_len(name, mode, b, n, want)` | `:27-45` | `xde_disasm_buf(b, n, ...)`，要求 `got == want` **且** `d.len == got`（42 次） |
| `expect_enc(name, mode, b, n, want_len, enc)` | `:47-65` | 长度与 `d.enc`；形参 `n` 已生效（`:51`）（15 次） |
| `expect_fail(name, mode, b, n)` | `:67-78` | 要求返回 `0`（23 次） |
| `expect_roundtrip(name, mode, b, n)` | `:80-113` | disasm → `xde_asm`（长度须等于 `n`）→ **`memcmp(out, b, n)` 字节级比较**（`:101`）→ 再 disasm，比较 `len` + `opcode` + `modrm`（5 次） |
| `expect_set(name, mode, b, n, sel, bit, want)` | `:117-138` | 要求解码长度 == `n`，再断言单个对象集位的存在/不存在。`sel` 0/1/2/3 = `src_set`/`dst_set`/`src_set2`/`dst_set2`，`want` 1 = 置位、0 = 未置位；成功打 `set<n> 0x… set|clear`（346 次） |
| `expect_flag(name, mode, b, n, bit, want)` | `:141-159` | 要求解码长度 == `n`，再断言单个 flag 位。`bit` 取单个 `C_*` 常量，`want` 1 = 置位、0 = 未置位；成功打 `flag 0x… set|clear`（126 次） |

### 覆盖矩阵（582 项检查）

582 = 557 次助手调用（`expect_len` 42 + `expect_enc` 15 + `expect_fail` 23 + `expect_roundtrip` 5 + `expect_set` 346 + `expect_flag` 126）+ 25 项内联检查（`C_RIPREL` 1 + Object sets 打印器 4 + 闸门/asm 区段 10 + 规范前缀 2 + `sete al` 非 undef 1 + 打印器最坏情况 3 + Coverage 区段 ud2 1 + Jcc/LOOP 区段打印器 3）。**本轮新增的 90 项落在 `// XA_BAD means "not a usable encoding in any mode"…` 区段（`:1428-1594`）**；以上计数都是对 1614 行文件的实测值（助手自身的定义行不计入调用）。

| 区段注释 | 行 | 检查数 | 内容 |
|----------|-----|--------|------|
| `// 64-bit GP` | `:163` | 26 | nop、`xor eax,eax`、`xor rax,rax`（含往返）、`mov rax,imm64`、`mov eax,imm32`、`mov r8,imm64`、RIP 相对（+ 内联 `C_RIPREL`）、`call [rip+0]`、`call rel32`、`66 call rel32`、ret、`push rbp`、`sub rsp,0x20`、SIB、`mov r8,[rsp+0x28]`、`nop dword [rax+rax]`、endbr64、syscall、movsxd、`moffs64` ×2、`test rax,imm32`、bt、`67` 前缀 |
| `// SSE / 0F38 / 0F3A` | `:276` | 6 | movups、palignr、pshufb、crc32、movbe、3DNow `pavgusb` |
| `// VEX` | `:302` | 6 | VEX2 `vaddps`（含往返）、VEX3 `andn` ×2、`rorx`、`vzeroupper` |
| `// EVEX` | `:325` | 4 | `vaddps zmm`（含往返）、EVEX 内存形式、`vrndscaless` |
| `// XOP` | `:344` | 4 | `vfrczpd`（含往返）、`vpcomb`、`bextr` |
| `// 32-bit mode: LES vs VEX, BOUND vs EVEX` | `:359` | 10 | LES/LDS、32 位 VEX2、BOUND、`inc eax`、truncated REX、`rex nop`、aaa 非法/合法、`push es` 非法 |
| （无区段注释的独立块） | `:391-394` | 1 | `xor eax,eax legacy enc`（`XDE_ENC_LEGACY`）——只证明 legacy 指令不被误判成向量/REX2 编码，作用域有限，见「覆盖缺口」第 3 条 |
| `// REX2 (APX)` | `:396` | 2 | `rex2 lea`、`rex2 imul` |
| `// Object sets: 8-bit extension registers vs high bytes, and APX EGPRs` | `:406-509` | 32 | 27 项 `expect_set` + 4 项内联打印器检查 + 1 项字节级往返：`mov spl,al`（`XSET_SPL` 置位且**不含** `XSET_SP`）、`mov ah,al`（`XSET_AH` 置位且**不含** `XSET_SPL`，钉住无 REX 时的差异）、`rex2 lea r16d dst2`（`dst_set2` 含 `XSET2_R16`、`dst_set` 不含 `XSET_OTHER`）、`rex2 lea r31d,[rax]`（`XSET2_R31`）、`rex2 push r16`（`src_set2` 含 `XSET2_R16`，钉住 opcode+r 的 B4）、`rex2 mov eax,[r16]`（SIB base 的 B4 → `src_set2` 含 R16，同时 `src_set` 含 `XSET_RAX`）、`rex2 add rax,r16`（`mod == 3` 下 reg 是 EGPR、r/m 是 legacy GPR，`:437-443`）；打印器输出串断言（`R16` / `R8B` / `SPL` / 解码后再打印 `R16`，`:444-473`）。`:475-509` 是同区段续块（注释 `// REX2 round-trip with a legacy prefix, MOV store forms, 8-bit r8-r15.`）：`66 D5 40 8D 00` 往返（钉 `p_66` 与 REX2 头共存）、`C6 C0 12`（`mov al,0x12` 报 dst 不报 src）、`8B C3`（`mov eax,ebx` 的 r/m 仍计入 src，反向控制）、`41 88 C0`（`mov r8b,al` → `XSET_R8` 与 `XSET2_R8B` 同时置位）、`41 88 C7`（`r15b` → `XSET2_R15B`）、`49 8B C0`（`mov rax,r8` → **无** `R8B`）、`44 8B C0`（`mov r8d,eax` → **无** `R8B`） |
| `// XOP gate in 16/32-bit, the relocated C_REL flag, and xde_asm limits` | `:511-616` | 14 | 4 次助手调用 + 10 项内联检查：`8F 08` 在 32 位 → `expect_len` 长度 2、`C_BAD` 置位；`call rel32` → `C_REL` 置位且 `C_BAD` 未置位；`xde_asm` 对 `datasize=200`/`addrsize=200`/`nvex=200` 夹到数组容量后分别返回 9/9/5；`xde_asm_buf(out,4)` 对 7 字节指令返回 0、`xde_asm_buf(out,7)` 返回 7；`xde_sprintfl` 对 `push rbp` 含 `C_PUSH`、对 `ret` 含 `C_CMD_RET`、对 `mov eax,imm32` 含 `C_DATA4`、对 RIP 相对 `mov` 含 `C_ADDR4`；`xde_sprintset2(XSET2_ALL \| 1<<40)` 打印 `???` |
| `// Canonical prefix order, and the flag intents recorded in xde102/todo.` | `:618-721` | 23 | 17 项 `expect_set` + 6 项内联检查：规范前缀顺序 2 项（`:619-639`，`67 66 90` → `66 67 90`、`64 F3 A4` → `F3 64 A4`）；REP/CMPS 标志 7 项（`:640-652`：`rep movsb` src/dst 均无 FL 且有 RCX；`rep cmpsb` src+dst 有 FL；`cmpsb` dst 有 FL、src 无 FL）；`cld`/`std` dst 有 FL 各 1 项（`:653-658`）；`SETcc` 4 项（`:659-677`：`sete al` dst 有 AL、src 有 FL、src 无 AL；`sete r8b` dst2 有 `R8B`）+ 1 项内联「`sete al` 的 `xde_sprintset(src_set)` 不是 `???`」；`SAHF`/`LAHF` 4 项（`:678-685`：`sahf` src 有 AH / dst 有 FL，`lahf` src 有 FL / dst 有 AH）；打印器最坏情况 3 项（`:686-721`：全位置位的 `allflags` → `xde_sprintfl` 227 字节、`xde_sprintset(~0ULL ^ 1<<63)` 79 字节、`xde_sprintset2(XSET2_ALL & ~XSET2_R16)` 97 字节，均断言 `< 256`） |
| `// Coverage: mode-dependent sets, implicit registers, I/O and flags.` | `:723-865` | 71 | 39 项 `expect_set` + 31 项 `expect_flag` + 1 项内联 `fail`（`ud2 sets undefined`）：模式相关栈集（`push` 64 位 → `XSET_RSP`；32 位 → `XSET_ESP` 且 `XSET_RSP & ~XSET_ESP` 清）、16 位 `movsb` → SI/DI、16 位 `mov ax,[1234]` → MEM/AX/`C_ADDR2`、`PUSHA` → EAX/EDI/ESP、I/O → DEV/DX（含 `OUT` 的负向控制）、`CPUID` → src EAX、dst EAX/EBX/ECX/EDX、`MOV` 与段寄存器互传 → OTHER、`LEAVE` → RSP/RBP、移位 → CL/AL/FL（含 `shl` 不读 FL 与 `rcl` 读 FL 的正反对照）、`F7 /0` → dst FL、`F6 /2`（`NOT`）→ 无 dst FL、标志 `C_STOP`/`C_CMD_RET`/`C_CMD_JMP`/`C_CMD_JCC`/`C_CMD_CALL`/`C_I64`/`C_REL`/`C_F64`/`C_3DNOW`/`C_OPSZ8`/`C_SIB`/`C_ADDR1`/`C_DATA2`/`C_MODRM`/`C_REX`/`C_REX2`/`C_VEX`/`C_EVEX`/`C_XOP`/`C_PUSH`/`C_POP`/`C_ADDR67`/`C_ADDR8`/`C_DATA8`/`C_DATA1`、`67` + 64 位 mod=0/rm=5 → `C_RIPREL` 清且 `C_ADDR4` 置、`0F 0B`（UD2）→ `C_UNDEF` 且 `xde_sprintset` 输出 `"???"` |
| `// Group-encoded operand forms: 8F /0 POP, F6 /0 TEST, F6 /2 NOT, FF /2 CALL.` | `:866-882` | 4 | `pop rax (8F /0)`、`test al,0x12`（`F6 /0`）、`not al`（`F6 /2`）、`call rax`（`FF /2`） |
| `// Jcc and LOOP/JCXZ report what they test instead of an undefined set.` | `:884-926` | 11 | 8 项 `expect_set` + 3 项内联打印器检查：`jz rel8`/`jz rel32` → `src_set` 有 `XSET_FL`，`loop` → `src_set`/`dst_set` 都有 `RCX`（且 `src_set` 不读 `FL`），`loope` → `src_set` 有 `FL`，`jcxz` → `src_set` 有 `RCX` 而 `dst_set` 无；3 项内联用 `xde_sprintset` 钉住 `jmp rel8` 的 `src_set`/`dst_set` 打印成**空串**、`jz rel8` 的 `src_set` 打印成 `F`（不再是 `???`） |
| `// Implicit operands: strings, conversions, segment and port I/O.` | `:928-982` | 31 | 31 项 `expect_set`：`LODS`（src+dst SI）、`STOS`（DI）、`INS`（DI+DEV+DX，`OUTS` 只断言 RSI/DEV 作正向对照）、`CBW`/`CWDE`/`CDQE`（AL→AX / AX→EAX / EAX→RAX）、`CWD`/`CQO`（AX→DX / RAX→RDX）、`AAA`（src+dst AH）、`AAM`（src AL、dst AX）、`AAD`（src AX）、`POPA`（dst EAX+EDI）、`PUSH ES`/`POP ES`（OTHER）、`XLAT`（RBX）、`ENTER`（src RSP、dst RBP） |
| `// 0F-map implicit operands, and a VEX vvvv that names a GPR.` | `:983-1077` | 61 | 61 项 `expect_set`：`PUSH FS`（src OTHER）、`POP FS`（dst OTHER）、`SHLD eax,ecx,cl`（src CL），以及 `andn`/`bextr`/`sarx` 各 3 条（`dst reg`/`src rm`/`src vvvv`，三者 `vvvv` 都是 `EAX`，钉住早前的两处缺陷）；**上一轮新增 49 条**——BMI 六族 `andn`/`bextr`/`bzhi`/`sarx`/`shlx`/`shrx` 各 7 条（`dst reg EDX`/`src rm`/`src vvvv`/`no src other`/`no dst other`/`no src mem`/`no dst mem`，编码 `andn70 C4 E2 70 F2 D0`/`bextr78 C4 E2 78 F7 D1`/`bzhi70 C4 E2 70 F5 D0`/`sarx7a C4 E2 7A F7 D1`/`shlx71 C4 E2 71 F7 D0`/`shrx7b C4 E2 7B F7 D1`，半数的 `vvvv == 0` = EAX）、向量对照 4 条（`vaddps` vvvv=1 `C5 F0 58 C2` 与 vvvv=0 `C5 F8 58 C1`，`src`/`dst` 均含 `OTHER`，钉住哨兵语义不影响向量路径）、APX EGPR 3 条（`evex andn` `62 F2 7C 00 F2 C0`，`V'`=0 → vvvv = r16：`src_set2` 含 `XSET2_R16`、`src` 无 `OTHER`、`dst reg EAX`） |
| `// Addressing forms: SIB with an index, without one, and disp32 no base.` | `:1078-1089` | 6 | 6 项 `expect_set`：SIB 带 index（`8B 04 48` → src RAX+RCX）、SIB 无 index（`8B 04 20` → src RAX 且**无** RCX）、disp32 无基址（`8B 04 25 …` → src MEM 且**无** RAX） |
| `// Opcode-embedded registers and the implicit accumulator rules.` | `:1090-1118` | 17 | 17 项 `expect_set`：`BSWAP eax`（src+dst EAX）、`XCHG ecx,eax`（src EAX+ECX、dst ECX）、`MOV AL,Ib`/`MOV EDI,Iv`（dst 寄存器来自 opcode 低 3 位）、`ADD AL,Ib`/`ADD EAX,Iv`（src 累加器、dst FL）、`F7 /4 MUL`（src EAX、dst EDX+FL）、`INC eax`（32 位 src+dst EAX+FL） |
| `// MOVBE / CRC32 share 0F 38 F0/F1; plus the vector object sets.` | `:1119-1173` | 28 | 28 项 `expect_set`（区段内还有 3 行子注释，`:1144`/`:1158`/`:1165`）：`movbe eax,[rax]` 载入形式 2（dst EAX、src M）、`movbe [rdx],eax` 存储形式 2（src EAX、dst M）、`crc32 eax,cl` 2（dst EAX、src ECX）、XOP map 9/8 的 `vfrczpd`/`vpcomb` 各 2（src/dst 均为 `OTHER`）、EVEX 内存形式 3（src M + src/dst OTHER）、`endbr64` 2（src/dst 均无 `RCX`）+ `movss` 2（无 `RCX`、src `OTHER`）——钉住「`F2`/`F3` 不是 REP」与 legacy SSE 缺陷、legacy SSE/MMX 6（`movups` 寄存器形式 3：无 `RCX` + src/dst `OTHER`；`paddb` 1；`movups` 内存形式 2）+ 3DNow `pavgusb` 1（src `OTHER`）、GPR 对照 4（`popcnt` dst EAX/src ECX、`bt` src ECX、`cmpxchg` src ECX） |
| `// System groups: CR/DR moves, RDRAND/RDSEED, fences, FS/GS base, save/restore, prefetch.` | `:1174-1427` | 133 | 112 项 `expect_set` + 21 项 `expect_flag`（10 个匿名块，区段内有 10 行子注释）：MOV CR/DR 12（`mov eax,cr0`/`mov eax,dr0`/`mov rax,cr0`：`src` 含 `OTHER` 且**无** `RAX`、`dst` 含 `RAX`；`mov cr0,eax`/`mov dr0,eax`：`src` 含 `RAX`、`dst` 含 `OTHER` 且**无** `RAX`）、`rdrand`/`rdseed`/`rdrand rax` 8（`dst` 含 GPR，两侧无 `OTHER`）、`lfence`/`mfence`/`sfence` 6 + FS/GS base 10（`rdfsbase`/`rdgsbase` 的 `dst` 含 `RAX`；`wrfsbase`/`wrgsbase` 的 `src` 含 `RAX`、`dst` 含 `OTHER`）+ 14 条 `C_BAD` 边界（5 条 `0F AE` mod=3 非法子形式 + 9 条「不得误伤」对照）、`F3 0F AE /5` INCSSPD/INCSSPQ 7（`src` 含 `RAX` 且无 `OTHER`、`dst` 含 `OTHER` 且无 `RAX`）、`0F AE`/`0F C7` 内存 20（11 条写形式 `dst` 含 `M`、4 条只读对照无 `dst M`，另有 `fxsave`/`cmpxchg8b`/`vmptrst`/`vmptrld` 的 `src` 含 `M` 与 `vmptrst dst other`）、prefetch/NOP/ENDBR 32（PREFETCHNTA/T0/T1/T2、PREFETCHW 与 PREFETCHWT1：`src` 含 `M`、无 `OTHER`；NOP Ev/`0F 19`/`0F 1D`/`0F 18 /4` 的 mod≠3 与 mod=3 两组：两侧无 `M`/`OTHER`；`endbr64`：两侧无 `OTHER`）、`0F 1E` NOP Ev / RDSSPD 11（无前缀的 `0F 1E`（mod≠3 与 mod=3）两侧无 `M`/`OTHER`；`F3 0F 1E /0` 的 RDSSPD/RDSSPQ：`src` 含 `OTHER`、`dst` 含 `RAX` 且无 `OTHER`）、`lss`/`lfs`/`lgs`/`movzx` 6（`dst` 含 `RAX`）、`C7 F8`/`C6 F8`（无 `C_BAD`）与 `C7 FA`/`C6 FA`（仍有 `C_BAD`）5 条 + `sldt`/`smsw` 的 `C_UNDEF` 2 条 |
| `// XA_BAD means "not a usable encoding in any mode", so the legacy forms below, legal in 16/32/64-bit alike, must not carry it.` | `:1428-1594` | 90 | 62 项 `expect_flag(…, C_BAD, 0)` + 7 项 `expect_flag(…, C_I64, 1)` + 19 项 `expect_fail`（64 位拒绝）+ 2 项 `expect_flag(…, C_BAD, 1)`（`FF /7`、`0F B9` UD1 参照），4 个匿名块（子注释 `:1428-1429`、`:1503`、`:1512-1514`、`:1589`）：全合法 legacy 35（`6C-6F` INS/OUTS、`70/71/7A/7B`、`8C/8E`、`9C-9F`、`AD/AF`、`CA/CB/CC/CF`、`D7`、`E0/E1`、`E4-E7`、`EC-EF`、`F4/F5`、`FA/FB`）、`0F B2/B4/B5` 3、`XA_I64` 形式在 16/32 位的合法性 24（22 个 opcode，`push cs`/`aaa` 各两种模式）、64 位 `expect_fail` 19、仍非法 group 2 |
| `// 16-bit` | `:1596` | 2 | `add ax,ax`、`mov ax,[moffs16]` |
| `// truncated` | `:1606` | 1 | 截断的 `mov rax,imm64` |

### 输出与退出码

成功打 `ok %-28s ...`（`len=` / `enc=` / `rejected` / `rt` / `set<n> 0x… set|clear` / `flag 0x… set|clear` / `RIPREL` 以及打印器块的裸串等格式），失败打 `FAIL <name>: <msg>`；结尾固定：

```c
printf("\n%d failure(s)\n", g_fail);
return g_fail ? 1 : 0;
```

**退出码就是唯一 CI 信号**（`build.bat:44` 原样透传）。

### 覆盖缺口（被要求"补测试"时的清单）

1. **对象集覆盖仍偏浅，但已铺到更多指令族**：现在对象集断言共 346 项（全部是 `expect_set`；126 项 `expect_flag` 单列于下一条），分布为 Object sets 区段 27 项（`mov spl,al`/`mov ah,al` 两条高字节/扩展字节案例、5 个 REX2/EGPR 向量共 9 条断言，以及 `C6`/`MOV` 存储形式与 8 位 r8b-r15b 几条），Canonical 区段 17 项（`XSET_FL` 9 项 + `SETcc` 4 项 + `SAHF`/`LAHF` 4 项），Coverage 区段 39 项（模式相关栈集、16 位串操作与寻址、`PUSHA`、I/O、`CPUID`、段寄存器、`LEAVE`、移位与组 3 的标志、若干 `C_*` 位、`UD2`），Jcc/LOOP 区段 8 项（`JZ` 读 FL、`LOOP`/`LOOPE`/`JCXZ` 的计数寄存器），以及前两轮新增的五个区段 143 项——`// Implicit operands…` 31 项（字符串 `LODS`/`STOS`/`INS`/`OUTS`、转换 `CBW`/`CWDE`/`CDQE`/`CWD`/`CQO`、BCD `AAA`/`AAM`/`AAD`、栈 `POPA`/`ENTER`、段寄存器 `PUSH ES`/`POP ES`、`XLAT`）、`// 0F-map implicit operands…` 61 项（0F 隐式操作数 + BMI 六族的 `dst reg`/`src rm`/`src vvvv`/两侧无 `OTHER`/两侧无 `MEM` 正负断言 + 向量 `vaddps` vvvv=1/0 的 `OTHER` 对照 + `evex andn` 的 EGPR `vvvv` 落第二字）、`// Addressing forms…` 6 项（SIB 带 index / SIB 不带 index / disp32 无基址）、`// Opcode-embedded registers…` 17 项（`BSWAP`/`XCHG`/`MOV r,I`/`ADD AL|EAX,Iv`/`F7 /4 MUL`/`INC eax`）、`// MOVBE / CRC32…` 28 项（上一轮：MOVBE 载入/存储各 2、`CRC32` 2、XOP map 9/8 的 `vfrczpd`/`vpcomb` 各 2、EVEX 内存形式 3、`endbr64`+`movss` 4、legacy SSE/MMX 6、3DNow `pavgusb` 1、GPR 对照 4），以及 `// System groups…` 区段 80 项（MOV CR/DR 12、RDRAND/RDSEED 8、fences 6 + FS/GS base 10、`0F AE`/`0F C7` 内存 17、prefetch/NOP/ENDBR 21、LSS/LFS/LGS 与 `movzx` 6），**此后两批再 +32 项**（INCSSPD/INCSSPQ 7、`0F AE`/`0F C7` 内存 3、PREFETCHWT1 3、NOP Ev 的 mod=3 形式 8、`0F 1E` NOP Ev 与 RDSSPD/RDSSPQ 11）。**本轮再 +90 项**，全在 `// XA_BAD means "not a usable encoding in any mode"…` 区段（`:1428-1594`），**不含 `expect_set`**（故 346 不变）：62 条 `C_BAD` 未置位 + 7 条 `C_I64` 置位 + 2 条 `C_BAD` 置位 + 19 条 64 位 `expect_fail`。**上一轮从「仍未覆盖」里移出**：MOVBE、CRC32、3DNow（现有 `XSET` 断言）、legacy SSE/MMX（`movups`/`paddb` 已覆盖寄存器与内存形式）；**本轮移出**：MOV CR/DR、RDRAND/RDSEED、LFENCE/MFENCE/SFENCE、FS/GS base、`0F AE`/`0F C7` 的存/取内存形式、PREFETCH\*/NOP Ev/ENDBR、LSS/LFS/LGS、`C7 F8`/`C6 F8` 的 `C_BAD` 边界，以及**向量操作数的 `vvvv`**（`XSET_OTHER` 分支：`vaddps` 的 vvvv=1/0 都折 `OTHER`，`evex andn` 的 `V'` 让 vvvv 落进 `src_set2`）。**此后两批再移出**：`0F 1E` 的 NOP Ev 与 RDSSPD/RDSSPQ、PREFETCHWT1、VMPTRST 的 `dst`、`0F AE` mod=3 的非法子形式（`C_BAD`）、INCSSPD/INCSSPQ、NOP Ev 的 mod=3 形式。**仍缺**：`vzeroupper`（`C5 F8 77`）目前 `src_set`/`dst_set` **皆为 0**——`xde_attr[0F][0x77] == 0`（无 `XA_MODRM`）→ `:1078` 的守卫为假，`parse_modrm`/`apply_modrm_usage`（`:1146-1149`）根本不执行，`memset`（`:790`）后保持 0；VZEROUPPER 清零 YMM 上半部，按本引擎口径至少应是控制寄存器语义（`OTHER`）——**HEAD 即如此、尚未建模**，不是本轮缺陷。另有 EVEX 的 `aaa`/`z`/`b` 语义（本引擎不建模 → 只能断 `OTHER`）、XOP map 8/9 的其余 opcode、`0F 00`/`0F 01` 系统组（带 `XA_UNDEF`，全有或全无：`:1168-1173` **赋值**整集 `XSET_UNDEF` + `XSET2_ALL`，故这两个 opcode 上的任何集合断言恒真、**不可证伪**〔读码推断、未实测〕）、`CALL`/`RET`（同理）、非 64 位下的 `CPUID`（断言只在 64 位，`tests/xde_test.c:761-766`；解码侧 `src/xde.c:278-281` 按 `map == 0F` 分派、不按模式，语义本就与模式无关 → 是覆盖缺口而非缺陷；且 `src` 只置 `EAX`、未含 `ECX` 子叶选择输入〔读码推断〕），以及 `LOOP`/`Jcc` 之外的控制转移。位掩码里绝大多数 `XSET_*` 仍无断言；`xde_sprintset`/`xde_sprintset2`/`xde_sprintfl` 三个打印器合计也只被 16 项内联检查触碰（Object sets 区段 4 + 闸门区段 5 + `sete al` 非 undef 1 + 打印器最坏情况 3 + Jcc/LOOP 区段 3）。对象集是本库的一半价值，断言密度仍低于长度/编码类。
2. **`flag` 位已覆盖解码器能置位的全部 `C_*`（126 条 `expect_flag`）**：`expect_flag` 覆盖 `C_BAD`（未置位 75 条 / 置位 10 条）、`C_REL`、`C_D64`、`C_ADDR2`、`C_STOP`、`C_CMD_RET`/`C_CMD_JMP`/`C_CMD_JCC`/`C_CMD_CALL`、`C_F64`、`C_O64`、`C_3DNOW`、`C_OPSZ8`、`C_SIB`、`C_ADDR1`/`C_ADDR4`、`C_DATA2`、`C_UNDEF`、`C_I64` 与 `C_RIPREL`（未置位），上一轮补齐了 `C_MODRM`（`88 C4`）、`C_REX`（`48 31 C0`）、`C_REX2`（`D5 40 8D 00`）、`C_VEX`（`C5 F8 58 C1`）、`C_EVEX`（`62 F1 7C 48 58 C1`）、`C_XOP`（`8F E9 78 81 C1`）、`C_PUSH`（`55`）、`C_POP`（`8F C0`）、`C_ADDR67`/`C_ADDR8`（`48 A1 <8>`）、`C_DATA8`（`48 B8 <8>`）、`C_DATA1`（`B0 12`），都追加在 Coverage 区段的 flags 块（`tests/xde_test.c:827-838`）；本轮再补 System groups 区段的 7 条——`xbegin`（`C7 F8`）/`xabort`（`C6 F8`）/`mov [rsp],imm32` 无 `C_BAD`、`C7 FA`/`C6 FA` 仍有 `C_BAD`、`sldt`/`smsw` 的 `C_UNDEF` 锚点；此后两批再补 14 条 `C_BAD` 断言（5 条 `0F AE` mod=3 非法子形式 + 9 条「不得误伤」对照，`tests/xde_test.c:1248-1261`）；**本轮再补 90 条**（62 条 `C_BAD` 未置位 + 7 条 `C_I64` 置位 + 2 条 `C_BAD` 置位 + 19 条 64 位 `expect_fail`，`tests/xde_test.c:1428-1594`）；内联另有 `C_RIPREL`（`:194-201`）与 `C_PUSH`/`C_CMD_RET`/`C_DATA4`/`C_ADDR4` 的 `xde_sprintfl` 串断言（`:573-606`）。于是**无断言的 `C_*` 只剩 2.00 从不置位的那批 1.02 词表项**——`C_SPECIAL`、`C_DATA66`、`C_SRC_*`/`C_DST_*` 操作数微位及其 `C_MOD_*` 别名、`C_ERROR`——已在 `include/xde.h:53-55` 标注，尺寸看 `addrsize`/`datasize`/`p_66`。（`XSET_FL` 是对象集位、不是 `flag` 位，`expect_set` 名下现有 25 项 `XSET_FL` 断言。）
3. **六种编码类的断言效力不对称**：`XDE_ENC_VEX2`=1 / `VEX3`=2 / `EVEX`=3 / `XOP`=4 / `REX2`=5（`include/xde.h:32-37`）都非 0，这五类的 `expect_enc` 只要解码器漏掉 `diza->enc` 赋值就会失败；而 `XDE_ENC_LEGACY == 0`（`:32`）与 `memset(diza, 0, sizeof(*diza))`（`src/xde.c:790`）之后的默认值同值，所以 `xor eax,eax legacy enc`（`tests/xde_test.c:393`）只能证明「legacy 指令不被误判成向量/REX2 编码」，**不能**证明解码器真的执行了 `diza->enc = XDE_ENC_LEGACY;`（`src/xde.c:1054`）。别把它读成「六种编码类被等价覆盖」。

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

1. `tools/gen_tables.py:90-544` 里改对应的 Python map（`m0` = legacy、`m1` = 0F、`m2` = 0F38、`m3` = 0F3A、`m4-m6` = EVEX 4-6、`m7` = VEX 7、`m8`/`m9`/`ma` = XOP 8/9/A）；
2. 若用到**新的** `XA_*` 位/常量，同步 `src/xdetbl.h:9-45`（Python 侧在 `gen_tables.py:11-56` 各镜像一份）；
3. 重新生成：`python tools\gen_tables.py`——`check_header()`（`gen_tables.py:593-638`）会先逐名比对 `xdetbl.h` 与脚本常量，漂移就列出差异并 `SystemExit(1)` 且**不写文件**，所以同步 `src/xdetbl.h` 是硬性前置；
4. 把 `src/xdetbl.c` 的改动一并提交（它入库）；
5. 若新属性需要影响 `flag` 或对象集，还要改 `src/xde.c` 的 `apply_attr_flags`（`:123-144`）或三趟对象集推导（`:146` / `:294` / `:554`）；
6. 加测试用例并 `build.bat`。

上一轮示例：`m1[0x90..0x9F]`（SETcc）在 `gen_tables.py:329-330` 从 `XA_MODRM | XA_OPSZ8 | XA_UNDEF` 改成 `XA_MODRM | XA_OPSZ8`（去掉 `XA_UNDEF`），重跑生成器后 `src/xdetbl.c` 的行数与结构不变；FL 读取改在 `apply_usage_special`/`apply_modrm_usage` 里按 opcode 硬编码（`XA_*` 位空间已满，没有对应属性位）。

本轮（分组 opcode 口径）**没有动任何表**：改动全在 `src/xde.c` 的 `apply_modrm_usage`（新增 `mov_crdr`/`rdrand`/`fence`/`fsgsbase`/`endbr`/`nop_ea`/`prefetch_ea`/`mem_store` 八个 opcode+mod+reg 判定，并让通用规则让位）与 `xde_disasm_buf` 的 `C6`/`C7`/`8F` `C_BAD` 判据（加 `mpeek != 0xF8`），所以 `tools/gen_tables.py`、`src/xdetbl.c`、`src/xdetbl.h` 三者**逐字节未变**；`src/xde.c` 里带 `XA_VVVV_GPR` 的分支与 `simd_0f` 名单也一并保持原样。改这类「reg 字段是选择器而非寄存器」的组时，**三层都要看**：表属性、`apply_modrm_usage` 的 `mod == 3` / 内存两条分支、以及 `xde_disasm_buf` 里的组特例。此后两批同样**没有动任何表**：新增 `nop1e`/`incssp` 两个判定、去掉 `nop_ea` 的 mod 条件、给 `mem_store` 的 `0F C7` 列表加上 `/7`，并在 `xde_disasm_buf` 新增 `0F AE` mod=3 的 `C_BAD` 判据（`:1110-1117`）——`tools/gen_tables.py`、`src/xdetbl.c`、`src/xdetbl.h` 仍**逐字节未变**。

**本轮（`C_BAD` 定口径 + 表侧清理）是第一次按「`XA_BAD` = 该编码在任何模式下都不可用」改 legacy / 0F map**：`tools/gen_tables.py` 删掉 60 个 `XA_BAD` 槽（35 个三模式全合法的 legacy 槽 + 22 个带 `XA_I64` 的 legacy 槽 + `m1` 的 `0F B2`/`B4`/`B5`），并在 `:20-21` 给 `XA_BAD` 补注释「not a usable encoding in any mode; mode-limited forms use XA_I64/XA_O64/XA_F64/XA_D64 instead」；`src/xdetbl.c` 由重跑生成器产出（24 行改动、24 插 24 删，**字节数 40140 不变** ⇒ 只翻转属性位，`src/xdetbl.c` **未手工编辑**）；`src/xdetbl.h` 未动（无新常量，`check_header()` 通过）；`src/xde.c` 未动（64 位拒绝仍走 `:1063` 的 `XA_I64` 分支，与 `XA_BAD` 无关）。删后 `XA_BAD` 余 **28 槽** = `m0[0xD6]`（SALC，`XA_BAD|XA_OPSZ8`）+ `m0[0xF1]`（INT1/ICEBP，`XA_BAD|XA_UNDEF`）+ 26 个 group 槽。生成器幂等实测：连跑两次逐字节一致（md5 `3fc495d3a3ce7180179bd5e8ddd261f0`、40140 字节、退出码 0）。双向 mutation 实测：把 `XA_BAD` 加回 `m0[0xCC]`（INT3）→ 重跑生成器后**恰好 1 条**失败（`int3 not bad`）；删掉 `group[XG_5][7]` 的 `XA_BAD` → **恰好 1 条**失败（`FF /7 still bad`）；恢复后 md5 复原、`0 failure(s)`。测试侧同步加 90 条断言（见「覆盖缺口」与覆盖矩阵的 `:1428-1594` 行）。

**注意**：`XA_*` 的 32 位已占满（标志 0-20、IMM 21-24、group 25-31）。加第 22 个属性标志需要先把 `xde_attr`/`attr` 拓宽到 64 位（影响 `src/xdetbl.h`、`src/xdetbl.c`、`gen_tables.py`、`src/xde.c` 的 `uint32_t attr` 形参与 `apply_attr_flags`），或复用/回收现有位。

### 加一个 group（`/reg` 分派）

改 `tools/gen_tables.py` 的 `XG_*` 常量与对应 map 的 `GRP(n)`，**并同步** `src/xdetbl.h:47-79` 的 `enum xde_group_id`（顺序即 id，`XG_COUNT` 必须跟着变，它同时决定 `xde_group[30][8]` 的行数），然后重新生成。

### 加一个公共 API 函数

1. `include/xde.h` 声明（放在 `:285-299` 区块内，保持 `extern "C"` 覆盖）；
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

- **REX2 保留 `p_66`/`p_rep`（刻意行为，不是缺口）**（对比 VEX `:972-973`/`:995-996`、EVEX `:935-936`、XOP `:1040-1041` 都清）——`66` 是 REX2 合法的遗留前缀，不是多余前缀。编码侧 `p_66` 与其它遗留前缀一起在 `:1266` 统一发射（无 nvex 专属分支），所以 `66 D5 …` 现在能字节级往返；改这里要解码/编码两侧一起动。
- **`C_ADDR8` 只来自 MOFFS（原先那条不可达分支已删）**：`parse_modrm` 里 `disp` 只会是 1/2/4（`:721-730`，末行注释写明），原来永不触发的 `else → C_ADDR8` 已删除；8 字节地址只经 MOFFS 路径（`:1158`）。新增位移宽度前先确认这条不变量还成立。
- **`xde_sprintfl` 覆盖解码器能产生的每一个 flag 位**（外加从不置位的 `C_DATA66`）（`src/xde_text.c:7-49`，不再是缺口）：共 34 个名字——低半 12 个（`C_BAD`/`C_REL`/`C_STOP`/`C_MODRM`/`C_SIB`/`C_RIPREL`/`C_REX`/`C_VEX`/`C_EVEX`/`C_XOP`/`C_REX2`/`C_UNDEF`，`:10-21`）+ `C_OPSZ8`（`:22`）+ 10 个尺寸类（`C_ADDR67`/`C_DATA66`/`C_ADDR1/2/4/8`/`C_DATA1/2/4/8`，`:23-32`；这 34 个名字里 `C_DATA66`（`:24`）是 2.00 从不置位的词表项）+ 7 个（`C_PUSH`/`C_POP`/`C_I64`/`C_O64`/`C_F64`/`C_D64`/`C_3DNOW`，`:33-39`）+ `C_CMD_*` 4 个（用 `XDE_CMD(fl)` 的 switch，`:40-46`）。解码路径不再置位的操作数角色位 `C_SRC_*`/`C_DST_*`（2.00 的 `src/xde.c` 一处都不写）仍不打印。缓冲区契约可验证：`tests/xde_test.c:686-721` 用全位置位的 `allflags` 钉住最坏情况 `xde_sprintfl` **227 字节**（断言 `< 256`），`xde_sprintset(~0ULL ^ 1<<63)` 79 字节、`xde_sprintset2(XSET2_ALL & ~XSET2_R16)` 97 字节，所以头文件那句「output should be at least 256 bytes」本轮首次被测到。
- **undef 标记是子集判定**：`(set & XSET_UNDEF) == XSET_UNDEF`（`src/xde_text.c:55`）与 `(set2 & XSET2_ALL) == XSET2_ALL`（`:142`）。后者严格更稳健——`set2` 带 bit ≥ 24 的杂位时仍打 `"???"`（`tests/xde_test.c:610` 用 `XSET2_ALL | 0x10000000000ULL` 钉住），不再依赖解码侧的整体赋值。
- **`XA_UNDEF` 已按「可确定性」拆分，只留给副作用确实未建模的指令**：`Jcc`（`70-7F` / `0F 80-8F`）只读 `XSET_FL`；`LOOP`/`LOOPE`/`LOOPNE`（`E0`/`E1`/`E2`）读并写计数寄存器（`XSET_CX`/`ECX`/`RCX`，宽度约定同 REP），`JCXZ`（`E3`）只读；近 `JMP`（`E9`/`EB`）读写都是空集（表侧 `tools/gen_tables.py:147-149`、`:230-234`、`:240`/`:242`，解码侧 `src/xde.c:264-265`、`:266-271` 与 0F 段的 `:287-288`）。仍带 `XA_UNDEF` 是刻意选择：`CALL`（`E8`、grp5 `/2`/`/3`）与 `RET`/`RETF`（`C2`/`C3`/`CA`/`CB`）——被调用者会破坏未知寄存器；远 `JMP`（`EA`、grp5 `/4`/`/5`）与 `IRET`（`CF`）——要装载 `CS`，而段寄存器在本引擎里只折成 `XSET_OTHER`；`INT`/`INTO`/`INT1`（`CD`/`CE`/`F1`，`INT3`（`CC`）只带 `XA_BAD`）、`BOUND`（`62`）、`WAIT`（`9B`）、x87（`D8-DF`）、`RSM`（`0F AA`）、`CLTS`/`INVD`/`WBINVD`/`WRMSR`/`RDMSR`（`0F 06`/`08`/`09`/`30`/`32`）、`UD2`（`0F 0B`）、`LAR`/`LSL`（`0F 02`/`03`）与 `0F 00`/`0F 01` 的 `SLDT`…`INVLPG` 系统组（`XA_UNDEF` 挂在这两个 opcode 上）——系统/MSR/x87 状态同样未建模。`flag` 不受影响（`C_CMD_JCC`/`C_REL`/`C_F64` 照常置位，见 `tests/xde_test.c:804-809`）；一旦置位，`src/xde.c:1168-1172` 把 `src_set`/`dst_set` 整体赋值成 `XSET_UNDEF`（**赋值，非 OR**）。
- **`C_I64` 只在 16/32 位出现，`C_O64` 只在 64 位出现**：`apply_attr_flags`（`src/xde.c:134`）照 `XA_I64` 置 `C_I64`，但带 `XA_I64` 的指令在 `mode == 64` 时更早被拒（`src/xde.c:1063-1064`）；16/32 位下它们合法，所以 `inc eax`（`40`，`gen_tables.py:124`）与 `aaa`（`37`，`:120`）在 32 位解码后会带上 `C_I64`（`aaa` 本轮起**不再**带 `C_BAD`，见下「表侧 `XA_BAD`」条）。`C_O64` 相反，只在 64 位成功解码上出现（`syscall`，`m1[0x05]`）。两个名字 `xde_sprintfl` 都打印（`src/xde_text.c:35-36`），且现在都有解码断言（`C_I64` → 32 位 `inc eax`，`C_O64` → `syscall`，`tests/xde_test.c:806-810`）——这条记录的是「位只在单一模式下可达」这一事实，不是覆盖缺口。
- **编码侧已按 SDM 组序规范化前缀**：`xde_asm_buf` 现在按 SDM 组序发射遗留前缀——lock/rep（组1）→ segment（组2）→ `66`（组3）→ `67`（组4）（`:1263-1267`），随后才是 REX 或 `vex[]` + opcode；`asm_size()`（`:1212-1238`）的计数顺序同步调整（字节总数不变）。解码侧仍不限制前缀顺序，所以非规范序输入（如 `67 66 90`、`64 F3 A4`）能解码，但**重编码会规范化**为 `66 67 90`、`F3 64 A4`——`xde_asm` 的输出不再逐字节等于非规范输入，往返测试因此只用规范序输入，或显式断言规范化结果。
- **`xde102/todo` 的 4 条现已全部处理**（1.02 时代的留档，不再有未决项）：① `REP` 对不同串指令的标志差异 → 已修：`REP` **只在串操作上**把 `CX/ECX/RCX` 计入 src+dst，不再无条件置 FL；`CMPS`/`SCAS` 写 FL、带 `REP` 时再读 FL（`src/xde.c:158-168`、`:251-260`）；② `setxx` → 已修：`0F 90-9F` 读 FL 且 r/m 只写不读（`:284-286`、`:307`）；③ `cld/std/cmpsb` 的 DF 源集 → 按「DF 不作为源」处理（`CLD`/`STD` 只置 `dst_set |= XSET_FL`）；④ `PUSH` 的栈宽不受 `67` 影响 → 2.00 早已由 `XA_PUSH` → `stack_set(mode)` 处理（`:633-642`）。被要求处理这些行为前先确认是否仍适用。
- **`xde102/` 与 `src/` 存在同名文件**（`xde.c`/`xde.h`/`xdetbl.c`/`xde_text.c`）。搜索或批量替换务必限定路径，否则会误改参考资料。`xde102/xde.c:5` 用 `#include "xdetbl.c"` 文本包含其数据表，**无法与 2.00 同编译**（并重复定义 `xde_disasm`/`xde_asm`/`xde_sprintfl`/`xde_sprintset`）。仓库中没有任何构建文件或脚本引用 `xde102/`。
- **`0F 00`/`0F 01` 系统组的对象集断言恒真、不可证伪**（`XA_UNDEF`）：`src/xde.c:1168-1173` 对整集**赋值** `XSET_UNDEF` + `XSET2_ALL`，这两个 opcode 上的任何集合断言都恒真；要修必须在 `:1168` 之后按 opcode/reg/mod 白名单回填真实集合，而 group 行全 0、`XA_*` 32 位已满 → 风险/收益最差。本轮只补了 `sldt`/`smsw` 的 `C_UNDEF` 锚点（`tests/xde_test.c:1420-1427`）。
- **EVEX 的 `aaa`/`z`/`b` 无可用位**：word0 位 49-63 与第二对象集的位 24-63 虽然空闲，但**没有名字**可以命名 k1-k7（本轮禁改 `include/xde.h`）→ 继续折 `OTHER`。
- **`0F C7 /6`/`/7` 的 mod≠3 与 `0F AE` 的只读形式**：`mem_store`（`src/xde.c:383-389`）列 `0F AE /0 /3 /4 /6` 与 `0F C7 /1 /3 /4 /5 /7`——`VMPTRST /7` 此后那批已改成**写内存**（`dst` 补 `MEM`），`VMPTRLD /6` 实测只读、仍走只读路径（`:377-382` 的注释把两者分开）。
- **`0F 18 /0-/3` 与 `0F 0D /0-/1` 的 mod=3 是保留形式，不是 NOP**：SDM 里 PREFETCHh/PREFETCHW/PREFETCHWT1 只定义 `m8`（Mod≠11），所以 `{0F,18,C0}`、`{0F,0D,C0}` 这类 mod=11 编码**不并入** `nop_ea`（`:370-372`），现状走通用路径报 `OTHER|OTHER`（`bad=0`），且**有意不加断言**——正确集合未定义。（`0F 1E`（无 `F3`）的 NOP Ev 此后那批已并入 `nop1e`。）
- **未定义编码只记录、不建模**：`0F 0D /2-/7` mod≠3、`F3 0F 1E /1..6`、`F3 0F 1E /0` 的 mod≠3、`F2 0F 1E C0`、`F3 0F AE /4` mod=3——UD/未定义编码，无可辩驳的目标语义 → 不写断言（`F3 0F AE /4` 因 `F3` 豁免而无 `C_BAD`；`0F AE` mod=3 的其余非法子形式此后那批已加 `C_BAD`，见 `src/xde.c:1110-1117`）。
- **隐式操作数仍缺**：RDRAND/RDSEED 的 CF、CMPXCHG8B/16B 的 `EDX:EAX` + FL、XSAVE 族/XRSTORS 的 `EDX:EAX`、XBEGIN/XABORT 的隐式项——本轮这些组只钉了显式 r/m 与 `MEM`。
- **`0F B2`/`B4`/`B5`（LSS/LFS/LGS）的表侧 `XA_BAD` 假阳性已在本轮修掉**：生成器 `tools/gen_tables.py:351/353/354` 从 `XA_MODRM|XA_BAD`（`0x00000201`）改成只有 `XA_MODRM`（`0x1`），重跑后 `src/xdetbl.c:63`（0F map 的 `B0-B7` 行）为 `0x00000081, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001`。这三个 opcode 在 16/32/64 位都合法，解码侧本就按正常载入处理（`simd_0f` 名单与 dst 白名单都含它们），所以表侧随即与解码侧一致；三条 `C_BAD` 未置位断言钉住（`tests/xde_test.c:1507-1509`）。
- **`src/xdetbl.h` 的 `XA_*`/`XG_*` 已有自动校验**（`check_header()`，`tools/gen_tables.py:593-638`，调用 `:677`）：`gen_tables.py` 顶部仍镜像一份常量，但生成前会解析 `xdetbl.h`，逐名比对 `XA_*`（含 `XA_IMM_*`、`XA_GRP_MASK`、两个 shift）、`enum xde_group_id` 的逐个枚举值（按位置，含 `XG_NONE`）与 `#define XDE_MAP_COUNT`（对 `len(MAPS)`），不一致就列出差异并 `SystemExit(1)`，**在写文件之前**退出，所以两边不会再静默漂移。改常量仍要动两处，只是现在忘一边会被拒绝而不是产出错误解释。
- **生成器无漂移（已实测）**：`python tools/gen_tables.py` 在本轮表改动后**逐字节重现** `src/xdetbl.c`（40140 字节、md5 `3fc495d3a3ce7180179bd5e8ddd261f0`、退出码 0），且**幂等**（连跑两次结果相同）；脚本只写 `.c`，`src/xdetbl.h` 由 `check_header()` 只校验不写；`OUT = <repo>/src/xdetbl.c`（`tools/gen_tables.py:9`），无 argv、无 `--check`；`src/xdetbl.c:1` 的标语**不含哈希** ⇒ 表有没有漂移只能靠重跑比对，不能靠读首行标语。**本轮的表改动正是「大小不变」的反例**：24 行被改（24 插 24 删），总行数 414 与字节数 40140 都纹丝不动，只有属性位翻了 ⇒ 「行数/大小未变」**不等于**表未变。
- **表侧 `XA_BAD` 已按 2.00 口径清理为 28 槽（本轮，用户决策）**：口径 = `XA_BAD` 表示**该编码在任何模式下都不可用**，模式受限的形式改由 `XA_I64`/`XA_O64`/`XA_F64`/`XA_D64` 表达（详见下条）。据此删掉 60 槽：**35 槽三模式全合法的 legacy**——`6C-6F`（INS/OUTS）、`70/71/7A/7B`（JO/JNO/JP/JNP）、`8C/8E`（MOV sreg）、`9C/9D`（PUSHF/POPF）、`9E/9F`（SAHF/LAHF）、`AD/AF`（LODSD/SCASD）、`CA/CB/CC/CF`（RETF imm16/RETF/INT3/IRET）、`D7`（XLAT）、`E0/E1`（LOOPE/LOOPNE）、`E4-E7`/`EC-EF`（IN/OUT）、`F4`（HLT）、`F5`（CMC）、`FA/FB`（CLI/STI）；**22 槽带 `XA_I64` 的 legacy**——`06/07/0E/16/17/1E/1F`、`27/2F/37/3F`、`60/61`、`62`、`82`、`9A`、`C4/C5`、`CE`、`D4/D5`、`EA`（16/32 位合法、64 位非法，模式限制已由 `XA_I64` 表达）；以及 `m1` 的 `0F B2`/`B4`/`B5`（见上条）。余 **28 槽**：`m0[0xD6]`（SALC，`XA_BAD|XA_OPSZ8`）与 `m0[0xF1]`（INT1/ICEBP，`XA_BAD|XA_UNDEF`）——这两个**任何模式下都未文档化**、不可依赖 ⇒ 按「任何模式都不可用」保留；另 26 槽全是 group 槽（组内非法 `/reg`，如 `0xFF /7`、`0F B9`=UD1），全部正确。旧 88 槽版**内部自相矛盾即铁证**：`AC/AE` 未标 vs `AD/AF` 标、`E2/E3` 未标 vs `E0/E1` 标、`CD` 未标 vs `CC` 标、`F8/F9/FC/FD`（CLC/STC/CLD/STD）未标 vs `F5/F4/FA/FB` 标 ⇒ 该位当时**不表达合法性**。**三条审计纠正**（本轮复核旧记录时改正）：① legacy 槽按实测分群是 **35 个全合法 + 22 个 `XA_I64` + 2 个保留（`D6`/`F1`）= 59**，旧记录写成「34 + 25」有误；② 错分的是 `CA`/`CB`/`CF`（RETF imm16/RETF/IRET）——它们**不带 `XA_I64`**、在 16/32/64 位都合法 ⇒ 归入「全合法」那 35 槽；③ `C4`/`C5`（LES/LDS）与 `62`（BOUND）在 64 位会被 VEX/EVEX 前缀路径遮蔽（形状完整即按前缀解析），其 `XA_I64` 拒绝只在**未构成合法前缀的字节形状**（如 `62 00`）上可达 ⇒ 这三者的合法性**只有 16/32 位可断言**（`tests/xde_test.c:1561-1563` 只断 32 位）。**方法论**：`expect_fail` **无法**钉 `XA_BAD`——`decode == 0` 只可能来自 `XA_INVALID`/`XA_I64`/`XA_O64`/截断（`src/xde.c:1061-1066`），所以「删掉某槽 `XA_BAD`」这类回归只能靠 `expect_flag(…, C_BAD, …)` 钉住（本轮新增的 19 条 64 位 `expect_fail` 钉的是 `XA_I64` 拒绝语义，与删 `XA_BAD` 无关）。
- **`C_BAD` 的语义（已决策，不再是未决项）**：**2.00 口径 = 「非法 / 不可用的编码形式」**，即该编码在任何模式下都不成立；1.02 头注释那句 `#define C_BAD 0x00000800 /* "bad", i.e. rarely used instruction */`（`xde102/xde.h:33`）里的「罕见指令」读法**已废弃**，只作为 1.02 历史留档。据此 `XA_BAD` 只保留在「该形式在任何模式下都非法」处（本轮清理后余 28 槽，见上条）；`push es`/`aaa`/`int3`/`hlt` 这类**只是 64 位非法**（改由 `XA_I64` 表达）或**三模式全合法**（不标任何位）的形式一律去掉该位，解码侧消费口径（`src/xde.c:130` 的 `if (attr & XA_BAD) f |= C_BAD;`）无需改动。旧断言审计：全文件搜 `C_BAD=1`（现 10 条）后逐条判定，**没有一条钉的是合法形式**，故未删任何旧断言；重判保留的三组（`8F /1`、`0F AE /0-/4` mod=3、`C7 FA`/`C6 FA`）均正确。
- **`/reg` 级对象集的位账**：`XA_*` 的 21 个 flag 位**已满**；bit24 名义空闲但落在 `XA_IMM_MASK`（`0xF<<21`）的码域内，当 flag 会让 `imm_bytes` 的 kind ≥ 8 → 立即数长度静默变 0；group 码 30-127 空闲，但只能按 **opcode** 表达、无法表达 `/reg`。**最省的 `/reg` 通路 = 新增第二张同形 `xde_group` 表**（30×8，`uint32_t` = 960 B），在 `src/xde.c` 的二次查表处 OR 进去，不动 `attr` 宽度与 `apply_attr_flags` 签名。另：`tests/xde_test.c` 对 `xde_group`/`XA_GRP`/`XG_` **零直接覆盖**（任何组级新表必须自带断言）。

## Documentation Map

| 文档 | 内容 |
|------|------|
| `README.md`（110 行） | 唯一面向使用者的文档：`## What it does`（`:11`）、`## Layout`（`:23`）、`## Build (MSVC)`（`:36`，含生成器一致性校验那一句）、`## API`（`:57`，示例 `:60-67`，含 `xde_asm_buf`）、`## Notes`（`:85`，编码规则）。**无许可证段落、无外部链接。** |
| `AGENTS.md`（本文件） | 面向 AI 助手 / 新维护者的完整工程参考 |
| `xde102/xde.txt`（242 行） | 1.02 设计文档：版本历史（`:8-9`）、对象集设计原则与「不区分内存地址」的理由（`:16-40`）、被否决的 "Permutation conditions" 方案（`:42-44`）、API 与结构体清单（`:46-140`）、REP INSB 示例及作者承认的 bug（`:111-117`）、结尾是 Mistfall 项目的 `AnalyzeRegs` 用法示例（`:142-242`）。是理解 2.00 设计取舍的最佳背景读物。 |
| `xde102/todo`（11 行） | 1.02 时代的遗留问题清单，见 Known Gaps 中「`xde102/todo` 的 4 条现已全部处理」 |
| `LICENSE`（21 行） | MIT |
