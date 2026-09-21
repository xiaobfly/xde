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
| `int xde_disasm(const uint8_t *opcode, struct xde_instr *diza)` | 64 位模式解码（`src/xde.c:1540`） |
| `int xde_disasm_ex(const uint8_t *opcode, struct xde_instr *diza, unsigned mode)` | 指定 16/32/64（`src/xde.c:1535`） |
| `int xde_disasm_buf(const uint8_t *opcode, unsigned max_len, struct xde_instr *diza, unsigned mode)` | 额外限制读取上限（`src/xde.c:1002`） |
| `int xde_asm(uint8_t *opcode, const struct xde_instr *diza)` | 结构 → 字节；`return xde_asm_buf(opcode, XDE_MAXLEN, diza);` 的包装（`src/xde.c:1625`） |
| `int xde_asm_buf(uint8_t *opcode, unsigned max_len, const struct xde_instr *diza)` | 结构 → 字节，额外限制写入上限；装不下返回 0（`src/xde.c:1574`） |
| `void xde_sprintfl(char *output, uint64_t fl)` | flag → 串，缓冲区 ≥256 字节（`src/xde_text.c:7`，实测最坏 227 字节） |
| `void xde_sprintset(char *output, uint64_t set)` | 对象集 → 串，缓冲区 ≥256 字节（`src/xde_text.c:51`，实测最坏 79 字节） |
| `void xde_sprintset2(char *output, uint64_t set2)` | 第二对象集字 → 串，缓冲区 ≥256 字节（声明 `include/xde.h:299`，实现 `src/xde_text.c:132`，实测最坏 97 字节） |

**返回值契约**：解码返回指令长度，`0` = 失败（截断 / 该模式下非法 / undefined）。编码返回写入字节数，`xde_asm_buf` 在结构体所需字节数 `> max_len` 时返回 `0`（`xde_asm` 给的是 `XDE_MAXLEN`，所以除空指针外永远成功）。没有错误码枚举、没有 errno、没有 out-param 状态。**注意 `max_len == 0` 不是「零字节上限」而**是「用默认 `XDE_MAXLEN`(15)」（`src/xde.c:1018-1021`，`xde_asm_buf` 同构 `:1581-1582`）——传 0 不会失败，而是按 15 解码 / 编码；`max_len == 16` 也被夹到 15（同一处 `> XDE_MAXLEN` 判定，两边同构），所以「传 16 就多写一字节」的期待不成立；要限长必须传显式上界（批 B v2 的截断扫描因此在 `len == 1`（即 `len-1 == 0`）的 **8,437,552** 例上单列跳过）。

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
  └─ xde_disasm / xde_disasm_ex            (src/xde.c:1540 / :1535, 只差默认参数)
       └─ xde_disasm_buf(ptr, max_len, d, mode)   ← 唯一真正的入口 (src/xde.c:1002-1533)
            ├─ xde_attr[map][opcode]      (查表, :1307)  ← src/xdetbl.c 生成
            ├─ xde_group[gid][modrm.reg]  (二次查表, :1341)
            └─ parse_modrm (`:876`) → apply_modrm_usage (:430)
                                  → apply_usage_special (:146)
                                  → apply_implicit_gp (`:760`)
  └─ xde_asm(out, d) → xde_asm_buf(out, XDE_MAXLEN, d)   (src/xde.c:1625 / `:1574-1623`, 纯字节重组)
```

`src/xde.c` 共 19 个函数：5 个导出 + 14 个 `static`。无全局可变状态，无堆分配。

### 解码流水线（分阶段行号）

| # | 阶段 | 行 |
|---|------|-----|
| 1 | 入参守卫：空指针 → 0；`mode ∉ {16,32,64}` → 0；`max_len` 0/超限一律夹到 15 | `:1014-1021` |
| 2 | `memset` 清零 + 预置 `mode` / `defaddr` / `defdata` | `:1023-1026` |
| 3 | 游标初始化 `beg`/`p`/`end` | `:1028-1030` |
| 4 | `C_BAD` 启发式 | `:1032-1036` |
| 5 | 遗留前缀循环 | `:1038-1092` |
| 6 | 取下一个字节 | `:1094-1096` |
| 7 | REX（`40-4F`，仅 64 位） | `:1098-1111` |
| 8 | REX2（`D5`，仅 64 位） | `:1113-1144` |
| 9 | EVEX（`62`） | `:1146-1195` |
| 10 | VEX（`C4`/`C5`） | `:1197-1255` |
| 11 | XOP（`8F`） | `:1257-1290` |
| 12 | 遗留 opcode + `enc = XDE_ENC_LEGACY` | `:1292-1295` |
| 13 | 汇合点标签 `got_opcode:` | `:1297` |
| 14 | map 越界检查 + `attr = xde_attr[map][mop]` | `:1305-1307` |
| 15 | 三种拒绝：`XA_INVALID` → 0；`XA_I64 && mode==64` → 0；`XA_O64 && mode!=64` → 0 | `:1309-1314` |
| 16 | `apply_attr_flags`：`XA_*` → `C_*` 映射（**`C_REL` 的唯一来源**，收尾不再重复置位） | `:1316`（实现 `:123-144`） |
| 17 | `XA_GROUP` 强制 `XA_MODRM`，并就地补置 `C_MODRM`（`apply_attr_flags` 已经跑过） | `:1318-1324` |
| 18 | peek ModR/M，取 `reg = (mpeek >> 3) & 7` | `:1326-1332` |
| 19 | group 二次查表：`xde_group[gid][reg]` 再跑一次 `apply_attr_flags` | `:1338-1345` |
| 20 | 硬编码特例：移位组 `C0 C1 D0-D3` 写 FL（`RCL`/`RCR` 另读 FL、`D2`/`D3` 另读 CL）／`C6 C7 8F` 的 `C_BAD`（`F8` 例外 = `XBEGIN`/`XABORT`）／**本轮新增的六个 `C_BAD` 判据（`0F 00 /6 /7`、`0F 01` 内存 `/5` 与 mod=3 保留子槽、`FF /3`/`/5` mod=3、`62` mod=3、x87 `D8-DF`，见「`C_BAD` 的来源」第 8-13 条）**／`F6`/`F7` 的标志与 `MUL`/`DIV` 累加器规则 | `:1346-1464` |
| 21 | `parse_modrm` + `apply_modrm_usage` | `:1466-1477` |
| 22 | MOFFS 路径（非 ModR/M 的 `A0-A3` 等） | `:1478-1491` |
| 23 | `apply_usage_special` / `apply_implicit_gp` | `:1493-1494` |
| 24 | `XA_UNDEF` → `src_set = dst_set = XSET_UNDEF`、`src_set2 = dst_set2 = XSET2_ALL`（**赋值，非 OR**）；随后 `undef_sys_operands` 按 ModR/M 回填 `0F 00`/`0F 01`/`0F 02`/`0F 03` 已建模形式的真实集合并清 `C_UNDEF`，未回填者保持整集 + `C_UNDEF` | `:1496-1507`（实现 `:308-428`，`ea_set` 暂存 `:1472-1475`，调用 `:1505`） |
| 25 | `imm_bytes` 定长 → 拷贝到 `data_b` + `C_DATA*` | `:1509-1522` |
| 26 | 收尾：`len = cur.p - opcode`；`0` 或 `>15` → 0；置 `len`；返回 | `:1524-1532` |

**3DNow 没有特例分支**：表把尾随 opcode 字节建模成 `XA_IMM_IB`，`XA_3DNOW` 只负责置 `C_3DNOW`（收尾注释 `:1524-1525`，现在是准确描述而非待办）。

**移位组与组 3 都写标志**：legacy map 的 `C0`/`C1`/`D0-D3`（八个操作）一律 `dst_set |= XSET_FL`；`reg == 2 || 3`（`RCL`/`RCR`）另加 `src_set |= XSET_FL`（读 CF），`D2`/`D3` 另加 `src_set |= XSET_CL`。`F6`/`F7` 在 `reg != 2` 时 `dst_set |= XSET_FL`（`NOT`(/2) 不写标志），`/4`-`/7` 的累加器规则不变（`:1346-1464`）。这与 ALU 路径一致（`:628`/`:632`/`:784`/`:818`/`:824`）：凡写标志都要在 `dst_set` 里出现 `XSET_FL`。

**`rex` 口径统一**：`apply_modrm_usage`（`:433`）与 `apply_implicit_gp`（`:763`）都用 `(diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY)`，8 位寄存器命名不会因走哪一趟而不同。`gp_set` 对 `sz ∉ {1,2,4,8}` 返回 `XSET_OTHER`（`:89`），不会冒充 64 位。

**字节读取纪律**：所有读取都走 `cur_left`（`:17`）/ `get_byte`（`:24`）/ `peek_byte`（`:32`）。`cur_left` 双重夹取 `min(end - p, beg + XDE_MAXLEN - p)`；由于 `max_len` 已夹到 15，第二项实际永远不是较小者（死代码，但无害）。

**`C_BAD` 的来源**（共 13 类；第 1-7 类是原有判据，第 8-13 类是本轮 `C_BAD` 普查新增的六个 `0F`/`FF`/`62`/x87 判据，全部落在 `xde_disasm_buf` 的 `got_opcode` 段）：

1. 首两字节构成的 16 位小端字等于 `0x0000` 或 `0xFFFF`（`:1032-1036`）；
2. 同一类遗留前缀**重复出现**（`66`/`67`/段/`F2F3`/`F0`，`:1053`/`:1066`/`:1074`/`:1081`/`:1088`）；
3. 表属性 `XA_BAD`（映射见 `:130`；语义 = 「**任何模式下都不可用的编码**」，模式受限的形式改由 `XA_I64`/`XA_O64`/`XA_F64`/`XA_D64` 表达，见 Known Gaps）；
4. `C6`/`C7`/`8F` 在 legacy map 下 `reg != 0` **且该 ModR/M 字节不等于 `0xF8`**（`:1361-1363`）——`C7 F8`/`C6 F8` 是合法的 `XBEGIN`/`XABORT`，其余 `/7` 编码（`C7 FA`/`C6 FA`/`C7 38`）才算坏。判据必须**字节精确**：写成 `reg != 0 && reg != 7` 会把所有非法 `/7` 一并豁免。
5. `0F AE` mod=3 的非法子形式：无 `F3` 的 `/0-/3` 与 `/4`（表里记的是内存形式，mod=3 时这两档没有定义；（`:1364-1371`）；
6. `0F 18 /0-/3` 与 `0F 0D /0-/1` 的 mod=3 **保留形式**（SDM 只定义 `m8`，`mod=11b` 不成立；（`:1372-1379`）；
7. **REX 紧邻 VEX/EVEX/XOP/REX2 引导字节**（SDM 视作 `#UD`；判据 `:1298-1304`，`rex_seen` 在 `:1107` 置位）——编码侧因 `nvex != 0` 永不发 REX，故这类输入不可能字节级往返（见 Known Gaps「编码器契约与「已判 `C_BAD` 不保证往返」边界」条）。
8. `0F 00` 组的 `/6`/`/7`（**全 mod** 共 64 槽；`:1385-1387`）——SDM 给该组只定义 `/0-/5`（SLDT/STR、LLDT/LTR、VERR/VERW），寄存器形式与内存形式都没有 `/6`/`/7`；REX2 也走这条（`legacy_enc` 在 `:1337`，覆盖写进 `opcode2` 的形式）。
9. `0F 01` **内存形式**的 `/5`（24 槽；`:1401-1403`）——内存表是 SGDT/SIDT、LGDT/LIDT、SMSW、LMSW、INVLPG，`/5` 空着；mod=3 的 `/5`（`E8`-`EF`）另有自己的表，不受影响。
10. `0F 01` **mod=3 的保留子槽**（12 槽；`:1404-1408`）——`C6 C7 CC CD CE D2 D3 E9 EA EB EC ED`；同一张表里 `C0-C5`（ENCLV…PCONFIG）、`C8-CB`/`CF`（MONITOR/MWAIT/CLAC/STAC/ENCLS）、`D0/D1/D4-D7`（XGETBV/XSETBV/VMFUNC/XEND/XTEST/ENCLU）、`D8-DF`（SVM）、`E0-E7`（SMSW）、`EE`/`EF`（RDPKRU/WRPKRU）、`F0-F7`（LMSW）、`F8-FF`（SWAPGS/RDTSCP/MONITORX…）都是真指令，**不得误标**。
11. `FF /3`/`/5` 的 **mod=3**（16 槽；`:1415-1417`）——far `CALL`/far `JMP` 只定义内存形式的 `m16:16/m16:32/m16:64`，mod=3 无编码；`/2`/`/4`（near `CALL`/`JMP r/m`）与 `/3`/`/5` 的内存形式保持干净，`/7` 本来就由表侧 `XA_BAD` 覆盖。
12. `62`（BOUND）的 **mod=3**（64 槽；`:1422-1424`）——BOUND 的两个操作数都从内存读；判据只看 `XDE_ENC_LEGACY`，所以 `62` 在 32 位被当 EVEX 引导时不受影响（`XDE_ENC_EVEX` 不置 `C_BAD`），64 位下 BOUND 早已被 `XA_I64` 拒。
13. **x87 逃逸表（`D8-DF`）的空白槽**（251 槽；`:1428-1437`）——两张掩码表：`x87_resm[8]`（`:872-874`，内存形式的 `/reg` 位掩码，SDM 空白的是 `D9 /1`、`DB /4`、`DB /6`、`DD /5`，共 96 槽）与 `x87_res3[8]`（`:866-871`，mod=3 的 `bit(reg*8+rm)` 掩码，155 槽），合计 96 + 155 = 251。**有意排除 12 槽**：`DB E0/E1/E4/E5`（8087/287 的 FENI/FDISI/FSETPM/FRSTPM）与 `DF C0-C7`（FFREEP）在 SDM 里同样空白，但 binutils 仍给它们助记符，标 `C_BAD` 会是假阳性 ⇒ 表里留 0，并有 `not bad` 对照断言钉住（见 Known Gaps「应标 `C_BAD` 却未标：431 槽」条的两条告诫）。

**前缀语义**：`66` 翻转 `defdata` 2↔4（`:1050-1051`）；`67` 在 64 位翻转 `defaddr` 8↔4、其余模式 2↔4（`:1059-1064`）；段前缀存 `p_seg`、`F2/F3` 存 `p_rep`、`F0` 存 `p_lock`。**每类只保留最后见到的字节**，**尺寸效应与之一致：同组重复前缀只在最后一次生效**（`if (!twice)`，`:1050`/`:1059`）——否则 `p_66`/`p_67` 已置而 `defdata`/`defaddr` 退回默认值，struct 自相矛盾，`xde_asm` 写出的字节无法解码回自身长度；重复本身仍标 `C_BAD`。段前缀本来就是「后者替换前者」的口径（`2E 26 06` → `26 06`）。注意前缀循环在 REX 判定**之前**跑完且 REX 只判一次，所以 `48 66 90` 会把 `48` 当 REX、再把 `66` 当 opcode——解码侧不拒绝非规范前缀顺序，但**重编码会按 SDM 组序规范化**（见 Known Gaps）。

### 编码类分派与歧义消解

前缀字节有歧义，各分支的判定门槛（每个都只看一两个前瞻字节）：

| 字节 | 两种解释 | 判定门槛 | 行 |
|------|----------|----------|-----|
| `62` | EVEX / BOUND | `peek(1..3)` 全成功 **且** `(b2 & 0x04)` **且**（`mode == 64` 或 `(b1 & 0xC0) == 0xC0`）→ 否则 BOUND（走 legacy 时 mod=3 一律 `C_BAD`，见「`C_BAD` 的来源」第 12 条；EVEX 分派不受影响） | `:1148-1149` |
| `C4` `C5` | VEX2/VEX3 / LES/LDS | `mode == 64` 或 `(b1 & 0xC0) == 0xC0`（`C4` 还需第三字节）→ 否则 LES/LDS | `:1197-1200` |
| `8F` | XOP / POP r/m | `(b1 & 0x1F) >= 8` **且**（`mode == 64` 或 `(b1 & 0xC0) == 0xC0`）→ 否则 `8F /0` = POP r/m | `:1261` |
| `D5` | REX2 (APX) / AAD | 仅 64 位且 `b == 0xD5` → 否则按 AAD 走 legacy | `:1113-1144` |

三个向量前缀门槛（`62` / `C4`+`C5` / `8F`）写法一致：64 位下无条件成立，16/32 位下要求 `mod == 11b` 或等价的 `0xC0` 掩码；因此 16/32 位里 `8F 08`（mod ≠ 11）不再被当作 XOP，而是走非法 POP（长度 2，`C_BAD` 置位）。

门槛失败即落到 `parse_legacy_opcode`（定义 `:966-1000`，调用 `:1292-1293`）。这就是 README 那句「`C4`/`C5`/`62`/`8F` 只有在后随字节符合前缀形式时才是 VEX/EVEX/XOP」的实现。

各编码类写回的结构字段：

| 类 | `enc` | `nvex` | `vex[]` | `map` 来源 | 其他 |
|----|-------|--------|---------|-----------|------|
| REX | 不改（仍是 LEGACY） | — | — | — | `rex` + `rex_w/r/x/b`，`C_REX` |
| REX2 | `XDE_ENC_REX2` | 2 | `D5, b1` | `(b1 & 0x80) ? 0F : LEGACY` | 读 bit6/5/4 → `rex_r4`/`rex_x4`/`rex_b4`，并置 `diza->rex = 0x40 \| (b1 & 0x0F)`（对寄存器命名等价于 REX）；`C_REX2 \| C_REX` |
| EVEX | `XDE_ENC_EVEX` | 4 | `62, P0, P1, P2` | `b1 & 7`（→ map 4-7） | `C_EVEX \| C_VEX`，`evex_r2/z/b/aaa`，`vex_vvvv` 含 `V'` 位；清 `p_66`/`p_rep` |
| VEX | `XDE_ENC_VEX2`(C5) / `XDE_ENC_VEX3`(C4) | 2 / 3 | 全前缀头 | `0F`(C5) / `b1 & 0x1F`(C4) | `C_VEX`；清 `p_66`/`p_rep` |
| XOP | `XDE_ENC_XOP` | 3 | `8F, b1, b2` | `b1 & 0x1F`（→ map 8-10） | `C_XOP`；清 `p_66`/`p_rep`（`:1281-1282`）；只写 `opcode`/`map`（`:1287-1288`） |

**`opcode2`/`opcode3` 的写入规则**：只有 `map ∈ {XDE_MAP_0F, XDE_MAP_0F38, XDE_MAP_0F3A}` 时才写（VEX `:1244-1252`、EVEX `:1183-1191`）；EVEX 的 map 4-7、VEX 的 map 7（`VEX7`）、XOP 的 map 8/9/A 都不写。XOP 与 VEX/EVEX 同一规则，不是缺口——`opcode`/`opcode2`/`opcode3` 只描述 `0F`/`0F 38`/`0F 3A` 这条链。

**`opcode2`/`opcode3` 的语义（按 map 决定谁是真 opcode）**：`map == XDE_MAP_0F` 时 `opcode2` **就是**真 opcode（`0F xx` 里的 `xx`）；`map == XDE_MAP_0F38`/`XDE_MAP_0F3A` 时 `opcode2` 存的是**转义字节** `0x38`/`0x3A`，真 opcode 落在 `opcode3`（写入顺序见 `parse_legacy_opcode` `:966-1000`，VEX/EVEX 路径同构）。所以判 0F38/0F3A 指令必须读 `opcode3`——读者若在 `apply_modrm_usage` 里照 `opcode2` 判断，永远只会看到 `0x38`/`0x3A`（上一轮 MOVBE 缺陷的一部分就是这么漏的）。

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

`XA_*` 到 `C_*` 的映射在 `apply_attr_flags`（`src/xde.c:123-144`）：17 条一对一 OR，**只增不减**（读-改-写 `diza->flag`）。`XA_VVVV_GPR` 与 group 位不经此函数（`XA_VVVV_GPR` 在 `:587`/`:599`/`:608`/`:646`/`:917` 直接读，`XA_GRP_ID` 只在 `:1339` 读）。

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

尺寸→标志：1 → `C_DATA1`，2 → `C_DATA2`，**3 → `C_DATA1 \| C_DATA2`**，4 → `C_DATA4`，8 → `C_DATA8`，**6 → `C_DATA4 \| C_DATA2`**（`:1516-1521`）。

### 对象集推导（三趟）

1. `apply_modrm_usage`（`:430-758`）——ModR/M 的 reg/r/m 字段。`mod == 3` 走寄存器分支，否则内存分支。`rex` 启发式在 `:433`：`rex != 0 || enc != LEGACY`（即 VEX/EVEX/XOP/REX2 一律当作「有 REX」，影响 8 位寄存器命名）。reg 字段带扩展位组成 `regx = rex_r4<<4 | rex_r<<3 | reg`（`:440-441`），r/m 侧同理用 `rex_b4`（`:638-639`）；`int setcc = (map == 0F && c == 0x0F && c2 ∈ 90..9F)` 在 `:443`。`mod == 3` 的寄存器分支里，`dst` 白名单 = ALU / `MOV r/m` / 移位 / `F6`/`F7` / `FE`/`FF` / `80-83` / **`C6`/`C7`** / **`SETcc`** / **带 `XA_VVVV_GPR` 的 VEX/EVEX/XOP**（`:700-706`，原因见下段），而 `src` 赋值排除 `0x8D` **以及 MOV 存储形式 `0x88`/`0x89`/`0xC6`/`0xC7` 与 `SETcc`**（`:694-695`）——这几类只写 r/m，不读；内存分支同样处理（`src` 排除 `:718-721`、`dst` 白名单 `:724-731`）。**分组 opcode 另有一层 reg 字段分派**：`mov_crdr`（`:471-472`）、`rdrand`（`:476-477`）、`incssp`（`:483-484`）、`umonitor`/`umwait`/`tpause`→`waitpkg`（`:491-498`）、`fence`（`:499-500`）、`ptwrite`（`:506-507`）、`clrssbsy`（`:512-513`）、`fsgsbase`（`:514-516`）、`endbr`（`:518-519`）、`nop1e`（`:524-525`）、`rdssp`（`:526-527`）、`nop_ea`（`:533-535`）、`prefetch_ea`（`:536-539`）、`mem_store`（`:546-553`）、`cmpxchg8b`（`:562-563`）、`xsave_mask`（`:569-574`）这些判定里，`rdrand`/`fence`/`incssp`/`fsgsbase`/`endbr`/`nop1e`/`nop_ea`/`prefetch_ea`/`rdssp`/`ptwrite`/`waitpkg` 先折成 `reg_not_dst`（`:555-556`），`mod == 3` 分支在 `:650-676` 按它们分派并让通用规则让位，内存分支在 `:718-723` 与 `:724` 用 `nop_ea`/`nop1e`/`prefetch_ea`/`mem_store` 改写「有内存操作数就有寄存器读写」的默认假设。
2. `apply_usage_special`（`:146-306`）——隐式操作数：REP/串操作（`A4-A7`/`AA-AF`/`6C-6F`/`AC-AD`）、IO（`E4-E7`/`EC-EF`）、`SAHF/LAHF`、`CBW/CWD`、`AAA/AAS`、`AAM/AAD`、`PUSHA/POPA`、`PUSH/POP sreg`、`XLAT`、`ENTER/LEAVE`、`MOV` 段寄存器，以及 0F map 的 `CPUID`/`SHLD/SHRD`/`LSS` 等。**其 `attr` 形参未使用**（`(void)attr;` `:305`）。标志规则**按指令区分**（`:251-260`）：`A6/A7`（CMPS）、`AE/AF`（SCAS）、`FC/FD`（CLD/STD）→ `dst_set |= XSET_FL`；`p_rep` 且 `A6/A7/AE/AF` → `src_set |= XSET_FL`（REP 循环测 ZF）；`MOVS/STOS/LODS/INS/OUTS` 完全不动标志，**DF 有意不作为源**。`REP` 本身**只在串操作上**把 `CX/ECX/RCX` 计入 `src`+`dst`（`:158-168`），不再无条件置 `XSET_FL`、也不再把 `F2`/`F3` 当 REP 给每条 SSE 标量指令带上 `RCX`。0F 段的 `SETcc`（`c2 ∈ 90..9F`）读标志：`src_set |= XSET_FL`（`:298-300`）。I/O 的 DX 端口形式（`EC`/`ED`/`EE`/`EF`）四种都读 `DX`，因此**一律**记进 `src_set`——`EE`/`EF`（`OUT DX,AL/AX`）不进 `dst_set`；立即数端口形式 `E4-E7` 不含 `DX`（`:229-237`）。
3. `apply_implicit_gp`（`:760-848`）——opcode 隐含的 GPR：`INC/DEC r`、`PUSH/POP r`、`XCHG r8,eAX`、`MOV r,Iv`、`ALU AL/eAX, Iv`、`BSWAP`，外加 `XA_PUSH`/`XA_POP` 的栈列（`stack_set`：16 → `XSET_SP`，32 → `XSET_ESP`，64 → `XSET_RSP`，（`:839-848`）。其 `rex` 判定在 `:763`，用的是与 `:433` **相同的** `(diza->rex != 0) || (diza->enc != XDE_ENC_LEGACY)`。

**VEX/XOP 的 GPR 路径**：表里带 `XA_VVVV_GPR` 的条目表示 **`vvvv` 本身就是 GPR 操作数**，所以 `0` = `EAX` 合法。现在 `src/xde.c:585-595` 是单层 if/else：`XA_VVVV_GPR` → **无条件** `gp_set(dsz, vex_vvvv, 1)`（含 `0`）并把 EGPR 并进 `src_set2`（`:587-590`）；只有 `else`（真向量编码）才 `src_set |= XSET_OTHER; dst_set |= XSET_OTHER;`（`:591-594`）。该属性只标在 GPR 操作数上，所以取 `vvvv` 不是哨兵判断（注释 `:580-584`）。早前修掉的两处：① 原实现把整个分支包在 `if (vex_vvvv != 0 && vex_vvvv != 0xF)` 里，`andn eax,eax,ecx`（`C4 E2 78 F2 C1`，vvvv=EAX）因此丢掉 EAX 源；② load-form 的「reg 字段是目的寄存器」白名单只枚举了 0F map 的 `0F 40-4F`/`AF`/`BC`/`BD`/`B8`（及 `8A`/`8B`/`8D`），而 BMI/BMI2 经 **VEX 0F38/0F3A 与 XOP** 到达解码器，`andn`/`bextr`/`sarx`/`bzhi`/`mulx`/`rorx` 等的目的寄存器只报成 `XSET_OTHER`；现在条件加了 `(attr & XA_VVVV_GPR)`（该属性本就表示「reg 字段是目的寄存器」，注释见 `:597-598`），并在 `:607-608` 的编码类判定里同样放行——回退任一处后 `andn`/`bextr`/`sarx` 各 2 条断言失败（共 6 条）。**上一轮又在同一处修掉两件**：① **`XA_VVVV_GPR` 指令的 `XSET_OTHER` 假阳性**——原实现无条件先 `src_set |= XSET_OTHER; dst_set |= XSET_OTHER;`，于是 ANDN/BEXTR/BZHI/SARX/SHLX/SHRX（表侧 `XA_VVVV_GPR` 落在 `m2` 的 `0xF2`/`0xF3`/`0xF5`/`0xF6`/`0xF7`、`m3[0xF0]`、`m9` 的 `0x01`/`0x02`/`0x90-0x9B`、`ma` 的 `0x10`/`0x12`）以及 XOP map 9/A 的 `vvvv` 形式（**操作数全为 GPR**）被硬塞 `OTHER`，与 legacy-SSE 那处同类（见下段第 1 条）；现该分支只加 `gp_set(vvvv)` + `src_set2`，只有 `else`（真向量编码）才折 `OTHER`。`apply_modrm_usage` 的内存分支（`:916-918`，`!(attr & XA_VVVV_GPR)` → 才折 `OTHER`）早有同一门槛，上一轮补的是同趟里漏掉的那处，风格与之一致。② **死支 + 错误哨兵规则**——删掉的 `else if (diza->vex_vvvv != 0 && diza->vex_vvvv != 0xF)` 是**死代码**（`src_set |= OTHER` 已在上方无条件置位，再置不可观测）；证据是差分扫描而非推理：对 **623,360** 条 VEX2/VEX3/EVEX/XOP 解码（mode 64）算 FNV-1a 哈希（`len` + src/dst + src2/dst2 + flag），HEAD = `BEB1E61249EEA0A3`、删掉该支后 = **同一哈希**（逐字节 no-op）；对照非空转——HEAD ≠ 上一轮修复后 `220302639237A323`。旧注释的断言也是错的：VEX.vvvv 倒序存储，**只有解码值 0**（编码位全 1）才表示「无 vvvv」，解码值 `0xF`（编码 0000b）是合法的 `XMM15`/`mm7`，EVEX 带 `V'` 时 16-31 也合法——旧注释把 `0xF` 也当哨兵。mutation 证据：M1（恢复无条件 `OTHER`）→ **13 条**断言失败（六个 BMI 族各 `no src other`/`no dst other` 共 12 条 + `evex andn no src other`）；M2（撤掉 `gp_set(vvvv)`）→ **10 条**失败（`andn`/`bextr`/`sarx` 的 3 条旧断言 + 六族各 `src vvvv` + `evex andn src vvvv R16`）。

**上一轮另外修掉四处缺陷（同一趟 `apply_modrm_usage`）**：

1. **legacy SSE/MMX 的操作数被当成 GPR（影响最大）**：新增 `int simd_0f`（`:453-465`）——`map == XDE_MAP_0F && enc == XDE_ENC_LEGACY` 且 `opcode2` **不在**枚举的 GPR 名单内者，其操作数按 SIMD 折成 `XSET_OTHER`，与向量编码同一口径。名单：`0F 20-23`（MOV Rd,Cd/Dd）、`A3`（BT）、`A4`/`A5`（SHLD）、`AB`（BTS）、`AC`/`AD`（SHRD）、`AF`（IMUL）、`B0`/`B1`（CMPXCHG）、`B2`（LSS）、`B3`（BTR）、`B4`（LFS）、`B5`（LGS）、`B6`/`B7`（MOVZX）、`B8`（POPCNT，带 `F3`）、`BA`（组 8）、`BB`（BTC）、`BC`（BSF）、`BD`（BSR）、`BE`/`BF`（MOVSX）、`C0`/`C1`（XADD）、`C3`（MOVNTI）、`40-4F`（CMOVcc）、`90-9F`（SETcc）。两个使用点：`mod == 3` 分支把 `rset` 改成 `XSET_OTHER`（`:644-649`）、目的寄存器走 `dst |= XSET_OTHER`（条件 `:599`，simd 分支 `:605-606`）。修前 `F3 0F 10 C1`（`movss xmm0,xmm1`）的 `src` 是 **RCX**（假阳性），现为 `XSET_OTHER`。**取舍**：名单漏掉的 opcode 会**丢失** GPR 归属（假阴性），而不再产生假阳性——因为 0F map 里 SIMD 远多于 GPR。名单是**硬编码**（`:457-465`），新增 0F-map GPR 指令时必须同步，否则该指令的 r/m 会被误记为 `OTHER`。
2. **`F2`/`F3` 被当成 REP 前缀**：原实现任何 `p_rep` 都无条件把计数寄存器计入 src+dst，于是每条 SSE 标量指令、`PAUSE`、`ENDBR`、`CRC32` 都会带上 `RCX`（`F3 0F 1E FA` 亦然）；现只在**串操作**（`A4-A7`/`AA-AF`/`6C-6F`/`AC-AD`）上计入（`:158-168`）。
3. **MOVBE 载入形式的 reg 目的缺失**：`0F 38 F0` 不在 load-form 的 opcode 名单里（该名单只枚举 `8A`/`8B`/`8D` 与 0F map 的若干），于是 `movbe eax,[rax]` 的 `dst` 为空。新增 `reg_dst`（`:448-450`，`map == XDE_MAP_0F38 && (opcode3 == 0xF0 || (opcode3 == 0xF1 && p_rep != 0))`）并加进 load 条件（`:599`）。
4. **MOVBE 存储形式的 reg 源与内存目的缺失**：`reg_src`（`:451-452`，`opcode3 == 0xF1 && p_rep == 0`）加进 store-form 的 src 条件（`:616`）与内存分支的 dst 白名单（`:724`）。注意 `0F 38 F1` 的 reg 角色取决于前缀：无前缀是 MOVBE（reg=源），带 `F2`/`F3` 是 CRC32（reg=目的）——`CRC32` 的 `dst` 修前也不对（被 REP 规则塞进 `RCX`，见第 2 条）。

mutation 证据：四处一起回退 → 11 条断言失败；另有一条**写弱的断言**被修正（`movbe [rax],eax src EAX` 即使漏掉 reg 源也会通过，因为基址寄存器 `RAX` 含 `XSET_EAX` 位），改用非 EAX 基址（`0F 38 F1 02` = `movbe [rdx],eax`）后单独验证，回退即失败。

**本轮修掉的 7 组分组 opcode 缺陷（同趟 `apply_modrm_usage`，外加 `xde_disasm_buf` 的一处 `C_BAD` 判据）**——每组都按「先写断言看到失败 → 改 → 通过 → mutation 回退即失败」走：

1. **`0F 20-23` MOV CR/DR 的 src/dst 整个反了（最重）**：新增 `mov_crdr`（`:471-472`），`mod == 3` 分支按 `/20 /21`（CR/DR → r/m）与 `/22 /23`（r/m → CR/DR）分派（`:650-660`），CR/DR 侧折 `OTHER`。修前 `mov eax,cr0` 报 `src={RAX}`、`dst={}`（EAX 明明是目的却记成源，`CR0` 整个丢掉）；现为 `src=OTHER`、`dst=RAX`，`mov cr0,eax` 补上 `dst=OTHER`。mutation M1（`mov_crdr = 0`）→ 9 条断言失败。
2. **`0F C7 /6`/`/7` mod=3 = RDRAND/RDSEED 的假 `OTHER`**：新增 `rdrand`（`:476-477`），只写 `dst` = r/m GPR、没有源（`:661-665`）。mutation M2 → 8 条失败。
3. **`0F AE` mod=3**：LFENCE/MFENCE/SFENCE（`:499-500`，`reg >= 5`）**无操作数**，两侧清空；`F3 0F AE /0-/3`（`:514-516`）是 RDFSBASE/RDGSBASE（写 r/m）/ WRFSBASE/WRGSBASE（读 r/m、写 FS/GS base = `OTHER`），按 `reg <= 1` 分派（`:666-676`）。mutation M3（`fence = 0`）→ 6 条、M4（`fsgsbase = 0`）→ 8 条失败。
4. **`0F AE`/`0F C7` 内存**写**形式的 `dst` 漏 `MEM`**：新增白名单 `mem_store`（`:546-553`）并加进内存分支的 dst 条件（`:724`）。修前 `fxsave`/`stmxcsr`/`xsave`/`xsaveopt`/`clwb`/`cmpxchg8b`/`cmpxchg16b`/`xrstors`/`xsavec`/`xsaves` 的 `dst` 为空（`src` 反倒有 `MEM`），现 `dst` 含 `MEM`；**只读对照**（`fxrstor`/`ldmxcsr`/`xrstor`/`vmptrld`/`vmptrst`）保持无 `MEM`。mutation M8 → 10 条失败。
5. **NOP / prefetch / ENDBR 的对象集**：`nop_ea`（`:533-535`）覆盖 `0F 18 /4-/7`、`0F 19`、`0F 1D`、`0F 1F`——这些**不访问操作数**，`src`/`dst` 清空；`prefetch_ea`（`:536-539`）覆盖 `0F 18 /0-/3`（PREFETCHNTA/T0/T1/T2）与 `0F 0D /0`（PREFETCHW）——**只读内存**，保留 `MEM`、去掉 `OTHER`；`F3 0F 1E FA` ENDBR64（`:518-519`）两侧清空（上一轮只钉了「无 `RCX`」，没钉 `OTHER`）。**有意保留寻址寄存器**：`parse_modrm` 对所有内存操作数统一记录基址/索引，所以 `nop [rax]` 的 `src` 仍有 `RAX`——这是引擎的统一口径，不是遗漏。
6. **`0F B2`/`B4`/`B5`（LSS/LFS/LGS）缺 GPR 目的**：0F-map 的目的白名单里有 `B6`/`B7`/`BE`/`BF` 却漏了这三个（`:601-604` 的 `c2` 列表）→ `lss eax,[rax]` 的 `dst` 现含 `RAX`（+`OTHER`），与 MOVBE 载入形式同类。
7. **`C7 F8`（XBEGIN）/ `C6 F8`（XABORT）被误标 `C_BAD`**（两者都是合法指令）：成因是 `C6`/`C7`/`8F` 的 `reg != 0` 判据，现改用**字节精确**判据 `mpeek != 0xF8`（`:1361-1363`）；`reg != 0 && reg != 7` 那种写法会把 `C7 FA`/`C6 FA`/`C7 38` 等非法 `/7` 编码一并豁免（mutation M10b：`C7 FA`/`C6 FA` 的「仍应 bad」断言失败）。mutation M10（恢复 `reg != 0`）→ 2 条失败。

**此后又推进两批（`78aaca1` 与其后的补批，当时记为「批 A/批 B」）——全在 `apply_modrm_usage` 的谓词表，外加 `xde_disasm_buf` 新增的一处 `C_BAD` 判据，仍未动表**——每条同样按「先写断言看到失败 → 改 → 通过 → mutation 回退即失败」走：

1. **`0F 1E`（无 `F3`）是 NOP Ev**（与 `0F 1F` 同类）：mod≠3 与 mod=3 都**不访问操作数**。新增 `nop1e`（判据 `map0F && LEGACY && c2 == 0x1E && p_rep == 0`，`:524-525`），加入 `reg_not_dst`（`:555-556`）、mod=3 的空操作数分支与内存分支的 `src` 排除（`!nop1e`，`:718`）。mutation → 6 条失败（断言在 `tests/xde_test.c:1688-1698`）。
2. **`F3 0F 1E /0`（RDSSPD/RDSSPQ）**：r/m 是**目的 GPR**、SSP 折 `OTHER`——`rdssp`（`:526-527`）在 mod=3 分支 `src |= OTHER`、`dst |= rset`（`:686-690`）。mutation → 4 条失败。
3. **`0F 0D /1`（PREFETCHWT1）去掉假 `OTHER`**：`prefetch_ea`（`:536-539`）的 reg 判据从 `reg == 0` 扩到 `reg ∈ {0,1}`，与 `/0`（PREFETCHW）同口径——**只读内存**。mutation → 2 条失败。
4. **`0F C7 /7` mod≠3（VMPTRST）的 `dst` 补 `MEM`**：`mem_store`（`:546-553`）的 `0F C7` 列表加上 `reg == 7`（VMPTRST **写** VMCS 指针到内存）；`VMPTRLD /6` 实测只读、**不动**。**旧断言 `vmptrst (no dst M)` 删除而非改写**。mutation → 1 条失败。
5. **`0F AE` mod=3 的非法子形式 → `C_BAD`**：无 `F3` 的 `/0-/3` 与 `/4` 在表里仍是内存形式的编码，mod=3 时没有任何定义，原先当普通 `OTHER|OTHER` 指令报出；`xde_disasm_buf` 新增**字节精确**判据 `opcode == 0x0F && map == XDE_MAP_0F && opcode2 == 0xAE && (mpeek >> 6) == 3 && reg <= 4 && p_rep != 0xF3`（其中的 `p_rep!=0xF3` 就是 `F3` 豁免条件；`:1364-1371`）。mutation → 5 条失败；另加 9 条「不得误伤」对照（`lfence`/`mfence`/`sfence`/四个 FS/GS base 形式/`incsspd`/`fxsave` 内存形式都不带 `C_BAD`，`tests/xde_test.c:1412-1425`）。
6. **NOP Ev 的 mod=3 形式**（`{0F,1F,C0}`/`{0F,19,C0}`/`{0F,1D,C0}`/`{0F,18,E0}`）与 mod≠3 一致清空：`nop_ea`（`:533-535`）去掉 `mod != 3`，mod=3 分支与 `fence`/`endbr`/`nop1e` 共用空操作数分支。mutation（重新插回 `mod != 3 &&`）→ 8 条失败（断言在 `tests/xde_test.c:1653-1660`）。
7. **`F3 0F AE /5`（INCSSPD/INCSSPQ）**：读 r/m GPR、写 SSP（折 `OTHER`）。新增 `incssp`（`:483-484`，`map0F && LEGACY && c2 == 0xAE && mod == 3 && reg == 5 && p_rep == 0xF3`），`fence` 判据改成 `reg >= 5 && !incssp`（`:499-500`），加入 `reg_not_dst`（`:555-556`）与 mod=3 分支（`:680-685`）；`C_BAD` 保持 0。mutation A（去掉 `!incssp`）→ 4 条、B（`dst |= OTHER|rset`）→ 2 条；断言在 `tests/xde_test.c:1504-1517`。

**有意取舍（INCSSP 的 `src`）**：SSP 是读-改-写，所以 `src |= XSET_OTHER` 也有一说（对照 `rdsspd` 确实置 `src OTHER`）；实现严格按「`src` = r/m GPR、`dst` = `OTHER`」，并用断言 `incsspd (no src other)` 把这个选择钉住。改这里要先定口径，不要顺手补 `src OTHER`。

**方法论注记（写断言时的判别力陷阱）**：`wrfsbase dst other` / `wrgsbase dst other` 两条断言**没有判别力**——旧路径本来也会往 `dst` 里多置 `OTHER`，这组的判别力全在 `src` 侧（`wrfsbase src RAX` + `wrfsbase (no src other)`）。**判据要按「新旧实现结果是否不同」来挑，而不是按「看起来该成立」来挑**；同理，`movbe [rax],eax src EAX` 那种用 EAX 作基址的写法也会被 `XSET_EAX` 位掩盖（见上一段的 mutation 证据）。

**上一轮两批（批 A 提交 `fdce0f3`，批 B 随后提交）——4 项 0F 系统组隐式寄存器/标志 + 2 项编解码往返缺陷，外加 1 项经核实拒绝**——同样按「先写断言看到失败 → 改 → 通过 → mutation 回退即失败」走：

批 A（`apply_modrm_usage` 的谓词表 + `xde_disasm_buf` 一处新 `C_BAD` 判据，**未动表**）：

1. **`RDRAND`/`RDSEED`（`0F C7 /6`/`/7` mod=3）写 CF** → `dst_set |= XSET_FL`（`:664`；与 `add`/`inc`/`mul` 同类。`SETcc`/`BT` 是**读** FL，归 `src`）。mutation：去掉 FL → 3 条失败。
2. **`CMPXCHG8B`/`CMPXCHG16B`（`0F C7 /1` mod≠3）**：比较并替换 `EDX:EAX`（`REX.W` 下 `RDX:RAX`）并写 ZF → 新增 `cmpxchg8b` 谓词（`:562-563`），`src`/`dst` 各含寄存器对、`dst |= XSET_FL`（`:736-742`）。**宽度由 `REX.W` 决定而非操作数尺寸**（8B 形式在 64 位仍用 32 位半寄存器，有断言钉住）。mutation：去寄存器对 → 4 条、整块删 → 11 条失败。
3. **`XSAVE`/`XSAVEOPT`/`XRSTOR`（`0F AE /4 /5 /6`）与 `XSAVEC`/`XSAVES`/`XRSTORS`（`0F C7 /3 /4 /5`）读状态组件掩码 `EDX:EAX`** → 新增 `xsave_mask` 谓词（`:569-574`），`src |= EAX|EDX`（`:744-745`）。**对照（不得置位）**：FXSAVE/FXRSTOR、LDMXCSR/STMXCSR、CLFLUSH/**CLWB**（`66` 前缀须排除 ⇒ 谓词里 `reg == 6 && p_66 == 0`）、VMPTRLD/VMPTRST。mutation：去 EDX → 6 条、整条删 → 10 条失败。
4. **`0F 18 /0-/3` 与 `0F 0D /0-/1` 的 mod=3 是保留形式**（SDM 只定义 `m8`）→ `C_BAD`（`:1372-1379`）；`/4-/7` 的 mod=3 NOP 形式与内存形式保持干净。mutation：范围收窄 → 2 条失败。

**经核实拒绝的一项（务必别照原计划改）**：原计划给 `F3 0F AE` 的 `/4`、`/6`、`/7` 置 `C_BAD`，经 SDM + objdump 核实三条**都有定义**——`/4` = **PTWRITE r32/m32**、`/6` = **UMONITOR r16/32/64**（WAITPKG）、`/7` = **SFENCE**（`F3` 冗余）——加 `C_BAD` 会错，故当轮**未改**——**最新一轮批 A 已把这三条的对象集全部建模**（PTWRITE 的只读 r/m 源、WAITPKG 的 `EDX:EAX`/`FL`、CLRSSBSY 的 `FL`），见下文「最新一轮两批」段与 Known Gaps「PTWRITE / WAITPKG / CLRSSBSY」条。

**方法论注记（写断言时的判别力陷阱，与上文 `wrfsbase dst other`/`movbe [rax],eax` 的判别力注记同源）**：CMPXCHG8B/16B 与 XSAVE 的断言改用 `[rcx]` 而非 `[rax]`——`[rax]` 的基址寄存器会把 `EAX` 自己塞进 `src`，使「`src` 含 EAX」**恒真**、断言永不失败（同一陷阱本仓库此前在 MOVBE 上踩过）。

批 B（改 `xde_disasm_buf` 的前缀循环 + `got_opcode`，**仍未动表**）：

5. **重复 `66`/`67` 的尺寸效应与自身字节不自洽**：原实现遇同组重复前缀时置 `C_BAD` 并**取消**该前缀的尺寸效应（留默认 `datasize`/`defaddr`）**却仍填 `p_66`/`p_67`** ⇒ struct 自相矛盾（`67 67 00 06` 写出 3 字节、重解码 5）。改为**最后者生效**（SDM：同组只有最后一个前缀有效；**段前缀本就是该口径**，`2E 26 06` → `26 06`），判据形如 `if (!twice) diza->defdata = …` / `if (!twice) { … defaddr … }`（`:1050-1051` / `:1059-1064`），`C_BAD` 保留。**有意后果**：部分重复前缀输入的 `len` 会变（如 `66` 生效后 Jcc 走 rel16 而非 rel32）——实测**没有任何既有断言因此变化**（新增恰为该轮的 22 条）。
6. **REX 紧邻 VEX/EVEX/XOP/REX2 引导字节**（`40 C5 04 08` 这类）：SDM 视作非法 `#UD`，此前被**静默接受**；而编码侧因 `nvex != 0` 永不发 REX ⇒ 丢字节（`66 48 D5 …` 还会变立即数宽度）。按已定口径置 `C_BAD`（判据在 `got_opcode`：`rex_seen && enc != XDE_ENC_LEGACY`，`:1298-1304`；`rex_seen` 在 `:1107` 置位），并配 3 条「不得误伤」对照（合法 `48 31 C0`、无 REX 的 `C5 04 08`、`vaddps`）。**全空间往返扫描与残差口径**见 Known Gaps「编码器契约与「已判 `C_BAD` 不保证往返」边界」条。

**最新一轮两批（批 A 未提交；批 B 只读、未改仓库任何文件）——把 `F3`/`F2`/`66 0F AE /4 /6` 一族的对象集补齐：44 条新断言 + mutation 对照**——仍按「先写断言看到失败 → 改 → 通过 → mutation 回退即失败」走：

批 A（**未动表**，全部落在 `apply_modrm_usage` 的谓词表与函数末尾的隐式项）：

1. **`F3 0F AE /4` = PTWRITE**（r/m 是**只读** GPR 源或**只读内存**，**无目的操作数**）：新增 `ptwrite` 谓词（`:506-507`）、加进 `reg_not_dst`（`:555-556`）——`mod == 3` 形式因此走通用 r/m 读路径（不另设分支）、reg 字段不再冒充目的；内存形式另从 `mem_store`（`:549` 的 `(reg == 4 && !ptwrite)`）与 `xsave_mask`（`:572`）**排除**（不是写，也不读 `EDX:EAX`）。编码 `{F3,0F,AE,E0}`、`{F3,48,0F,AE,E0}`、`{F3,0F,AE,20}`，断言在 `tests/xde_test.c:1428` 起的块。
2. **`F3 0F AE /6` mod=3 = UMONITOR**（WAITPKG；r/m 只读 GPR）：新增 `umonitor`（`:491-492`），与同字段同 mod 的 `umwait`（`F2`，`:493-494`）、`tpause`（`66`，`:495-497`）折成 `waitpkg`（`:498`），`fence` 判据加 `&& !waitpkg`（`:499-500`）——否则 WAITPKG 三个会被**无操作数**的 MFENCE 吞掉；`waitpkg` 与 `ptwrite` 一并进 `reg_not_dst`。断言在 `tests/xde_test.c:1453` 起的块。
3. **`F2 0F AE /6` = UMWAIT、`66 0F AE /6` = TPAUSE**：除 r/m 只读外还读 `EDX:EAX`（唤醒期限）、写 `FL`（CF 报告唤醒原因）——`:750-753`。
4. **`F3 0F AE /6` mod≠3 = CLRSSBSY**（CET；清 shadow stack token 的 busy 标志）：`clrssbsy`（`:512-513`）——m64 读且写（由 `mem_store` 的 `reg == 6` 覆盖）、`dst_set |= XSET_FL`（`:756-757`）、`xsave_mask` 用 `!clrssbsy` 排除（`:573`）。断言在 `tests/xde_test.c:1490-1491` 起的块。
5. **`reg_not_dst += ptwrite || waitpkg`**（`:555-556`）：消除 reg 字段带来的 `OTHER` 目的与 rset→`OTHER`。

**mutation 对照**（每一组都先看到失败才留下）：M1 `fence` 去 `!waitpkg` → **4** 条；M2 删 UMWAIT/TPAUSE 块 → **4** 条；M3 `mem_store` 去 PTWRITE 排除 → **1** 条；M4 `xsave_mask` 去排除 → **4** 条；M5 去 CLRSSBSY 的 `FL` → **1** 条；M6/M8/M9 `reg_not_dst` 去 `ptwrite`/`waitpkg` → **16/6/10** 条；M10 CLRSSBSY 判据归 0 → **3** 条。开发中还删掉一条**无判别力**的分支（其 mutation 得 **0 FAIL**，故删）。

**不得误伤（最新一轮新加对照）**：`{F3,0F,AE,F8}` 的冗余 `F3` 仍是**无操作数**的 SFENCE（`tests/xde_test.c:1384-1390`）；既有的 `{0F,AE,E8/F0/F8}`（LFENCE/MFENCE/SFENCE）、`{F3,0F,AE,C0}`（RDFSBASE）、`{F3,0F,AE,E8}`（INCSSPD）对照（`tests/xde_test.c:1412-1425`）保持不动。

批 B（**只读属性扫描 v2，未改仓库任何文件**）：

- **解码截断 / 编码容量的空间不变式零违例**（99,176,906 + 107,614,458 例）与**扩展枚举往返**（136,156,160 次输入）的规模、mutation 对照、以及对上一轮基线的**独立复刻**，逐条见 Known Gaps「读 / 写边界不动点…」与「编码器契约…」两条；
- 顺带确认 **`max_len == 0` 被当作「用默认 15」** 这一 API 语义坑（见上文「返回值契约」段）。

**REX2 的 r/m 是 GPR，不是向量寄存器**：`parse_modrm` 的 SIB 分支（`:916-918`）与 `apply_modrm_usage` 的内存分支（`:732-733`）在判断「是否按向量寄存器记 `XSET_OTHER`」时都会排除 `XDE_ENC_REX2`；`mod == 3` 分支同样排除（`:644-649`）。另外 `parse_modrm` 的两个「无基址」判定——SIB 的 `mod == 0 && base == 5`（`:914`）与 `mod == 0 && rm == 5`（`:933`）——在 `rex_b4` 置位时不再成立，此时它指的是真寄存器 r21，不是 disp32。

`gp_set`（`:43-90`）的寄存器列映射：`reg > 31 → XSET_OTHER`（**解码路径不可达**，`reg` 由 5 位扩展位拼出，上限 31）；`reg >= 16` 交给其第 4 个形参 `uint64_t *egpr`——EGPR 写进**第二对象集字**（`*egpr |= XSET2_R16 << (reg - 16)`，`:66-72`），固定寄存器处传 `NULL`；`reg >= 8` 分两支：首字仍返回**宽度无关**的 `XSET_R8 << (reg-8)`，并且当 `sz <= 1`（8 位形式 r8b-r15b）时**额外**写入第二字的 `XSET2_R8B << (reg-8)`（`:73-79`）——即**叠加**而非替换：`XSET_R8..XSET_R15` 照旧，第二字另有 8 位宽度位；`sz <= 1` 时按 `rex` 选 `lo8_norex`（AL/CL/DL/BL/**AH/CH/DH/BH**）或 `lo8_rex`（AL/CL/DL/BL/**SPL/BPL/SIL/DIL**——独立的 `XSET_SPL/BPL/SIL/DIL` 位，不再复用 16 位那几位）；`sz` 2/4/8 → `w16`/`w32`/`w64`，**`sz ∉ {1,2,4,8}` 返回 `XSET_OTHER`，不冒充 64 位**（`:87-89`）。

### 编码方向（`xde_asm`）

**不做校验、不解码**；`xde_asm_buf`（`src/xde.c:1574-1623`）只多做一次容量检查，`xde_asm`（`:1625-1628`）是 `return xde_asm_buf(opcode, XDE_MAXLEN, diza);` 的薄包装。固定顺序拼接：

```
max_len 夹取：0 或 > XDE_MAXLEN 一律按 15                        (:1581-1582)
→ 计数夹到数组容量：nvex ≤ 4 / naddr ≤ 8 / ndata ≤ 8            (:1585-1587)
→ asm_size() 算所需字节数，> max_len 则 return 0                 (:1546-1572, :1589-1590)
p_lock → p_rep → p_seg → p_66 → p_67    （SDM 组序 1→2→3→4(:1597-1601)
  ├─ 若 nvex：vex[0..nvex-1] → opcode   （此路径不发 rex）
  └─ 否则：rex → opcode
             └─ 若 opcode == 0x0F：opcode2 →（若 opcode2 ∈ {38,3A}）opcode3
→ flag & C_MODRM ? modrm
→ flag & C_SIB   ? sib
→ naddr 个 addr_b[]
→ ndata 个 data_b[]
```

**容量安全**：`addrsize`/`datasize`/`nvex` 是 `uint8_t`，被写坏时解码侧的计数会越过 `addr_b[8]`/`data_b[8]`/`vex[4]`（真 UB）。`xde_asm_buf` 先把这三个计数夹到数组容量（`:1585-1587`），再用 `asm_size()`（`:1546-1572`）算出所需字节数，`> max_len` 就返回 `0`（`:1589-1590`）；`max_len == 0` 或 `> XDE_MAXLEN` 一律按 `XDE_MAXLEN` 处理（`:1581-1582`），与 `xde_disasm_buf` 的 `max_len` 处理同构。所以解码成功的指令（`len ≤ 15`）经 `xde_asm` 永远编得出来（不变量与 13 项同步清单见 Known Gaps「`asm_size()` 是写出序列的第二份文本实现」条）。

`p_66` 现在与其它遗留前缀一起在 `:1600` 统一发射（没有 nvex 专属分支）：这是为 REX2 准备的——REX2 是唯一**保留** `p_66`/`p_rep` 的编码类（VEX/EVEX/XOP 在解码时就清掉，`:1213-1214`/`:1236-1237`/`:1176-1177`/`:1281-1282`），而它们的 `vex[]` 头自带 pp 字段，不走 `p_66`；所以 `66 D5 …` 现在能字节级往返。

它只读 `nvex`/`vex[]`、`p_seg/p_lock/p_rep/p_67/p_66/rex`、`opcode/opcode2/opcode3`、`modrm`、`sib`、`addr_b+addrsize`、`data_b+datasize`，**完全不看 `map` / `enc` / `defdata` / `defaddr` / `len` / 对象集**。因此：

- 手搓一个 `nvex == 0` 但 `map == XDE_MAP_0F38` 的结构体，`xde_asm_buf` 不会补出 `0F 38` 前缀——它只信字节字段；
- 返回 `0` 只有两种情形：`!opcode || !diza`（`:1579-1580`），或所需字节数 `> max_len`（`:1589-1590`）；解码成功的指令不可能编出 0 字节；
- **契约边界**：对解码时**已标 `C_BAD`** 的输入不保证字节级往返（编码器不为非法形式背书），合法输入的往返保证与实测证据见 Known Gaps「编码器契约与「已判 `C_BAD` 不保证往返」边界」条。

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
| `src/xde.c` | 解码器 + 编码器（1628 行），全部核心逻辑 | `xde_disasm_buf:1002`, `xde_asm_buf:1574`, `xde_asm:1625`, `asm_size:1546`, `undef_sys_operands:339`, 14 个 static 助手 + `x87_res3`/`x87_resm` 两张 x87 保留槽掩码表（`:866-874`） |
| `src/xdetbl.c` | **机器生成**的属性表（414 行） | `xde_attr:5`, `xde_group:382` |
| `src/xdetbl.h` | 手写的表层契约（86 行） | `XA_*`, `enum xde_group_id`, `XDE_MAP_COUNT 11` |
| `src/xde_text.c` | 调试打印（165 行，3 个函数） | `xde_sprintfl:7`, `xde_sprintset:51`, `xde_sprintset2:132` |
| `tools/gen_tables.py` | 表生成器（682 行），唯一写出 `src/xdetbl.c` 的地方 | `OUT:9`, `MAPS:545`, `check_header:593`, emit 块 `:655-675` |
| `tests/xde_test.c` | 唯一测试文件（2294 行） | 9 个 `expect_*` 助手（含 `expect_seteq:194`）, `main:237` |
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

退出码 `0` = 全通过，`1` = 有失败。`main` 是 `int main(void)`，**没有任何 CLI 参数**（无 filter / verbose / 单用例选择），每次运行都执行全部 878 项检查。

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
- **注释风格**：行内 `//`，多用于标注特例原因，例：`// 32-bit GP writes zero-extend in 64-bit mode.`（`:562`）、`// NOT (/2) writes no flags`（`:1336`）、`// prefetches do read memory`（`:709`）、`// segment override`（`:705`）。**全树没有任何 `TODO`/`FIXME`/`XXX`/`HACK`/`NOTE` 标记**（`src/` 已核）。
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
| `src/xde.c:1002-1533` | `xde_disasm_buf` —— 改解码行为从这里读起 |
| `src/xde.c:1574-1623` | `xde_asm_buf` —— 字节重组 + 容量检查（`xde_asm` 是它的包装，`:1625-1628`） |
| `src/xde.c:1546-1572` | `asm_size` —— 估算编码所需字节数，与 `xde_asm_buf` 的落地循环必须同步 |
| `src/xde.c:1148-1149` / `:1197-1200` / `:1261` | 三处编码类歧义门槛 |
| `src/xde.c:123-144` | `XA_*` → `C_*` 映射表（新增属性位必改） |
| `src/xde.c:146` / `:430` / `:760` | 三趟对象集推导 |
| `src/xde_text.c:132` | `xde_sprintset2` —— 第二对象集字的打印器 |
| `src/xde_text.c:7` | `xde_sprintfl` —— flag 打印器（新增 flag 覆盖时改这里） |
| `src/xdetbl.h:9-45` | `XA_*` 位定义 + IMM / GRP 位移 |
| `src/xdetbl.h:47-81` | `enum xde_group_id`、`XA_GRP`、`XDE_MAP_COUNT 11` |
| `tools/gen_tables.py:9` / `:90-544` / `:655-675` | 输出路径 / 全部表数据 / emit 块 |
| `tools/gen_tables.py:593-638` | `check_header` —— 生成前的 `xdetbl.h` 一致性校验（不一致即拒绝生成） |
| `tests/xde_test.c:237` | 测试 `main`，全部用例内联于此 |
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

**自研极简 harness，无第三方框架，无 `ASSERT`/`CHECK`/`TEST` 宏**。全部逻辑在 `tests/xde_test.c`（2294 行）的单个 `int main(void)`（`:237-2294`）中：124 个匿名 `{ }` 块，每块一个 `static const uint8_t` 向量紧跟 `expect_*` 调用。**零文件 IO、零 fixture、零 golden 文件**。

断言助手（括注是实测的调用次数）：

| 助手 | 行 | 契约 |
|------|-----|------|
| `fail` | `:10-14` | 打印 `FAIL <name>: <msg>`，`g_fail++` |
| `hexbytes` | `:16-25` | 字节数组 → 空格分隔 `%02X` 串 |
| `expect_len(name, mode, b, n, want)` | `:27-45` | `xde_disasm_buf(b, n, ...)`，要求 `got == want` **且** `d.len == got`（46 次） |
| `expect_enc(name, mode, b, n, want_len, enc)` | `:47-65` | 长度与 `d.enc`；形参 `n` 已生效（`:51`）（16 次） |
| `expect_fail(name, mode, b, n)` | `:67-78` | 要求返回 `0`（23 次） |
| `expect_roundtrip(name, mode, b, n)` | `:80-113` | disasm → `xde_asm`（长度须等于 `n`）→ **`memcmp(out, b, n)` 字节级比较**（`:101`）→ 再 disasm，比较 `len` + `opcode` + `modrm`（6 次） |
| `expect_sizes(name, mode, b, n, want_defdata, want_defaddr)` | `:115-134` | 断言前缀解析出的 `defdata`/`defaddr` 与它记录的前缀字节**自洽**（重复前缀块 `:732-764` 内两处）（2 次） |
| `expect_selflen(name, mode, b, n)` | `:136-163` | disasm → `xde_asm` → 要求**写出的字节数等于再解码出的长度**；用于「输出故意短于输入」的折叠前缀情形（`expect_roundtrip` 在那里表达不了）（4 次） |
| `expect_set(name, mode, b, n, sel, bit, want)` | `:167-188` | 要求解码长度 == `n`，再断言单个对象集位的存在/不存在。`sel` 0/1/2/3 = `src_set`/`dst_set`/`src_set2`/`dst_set2`，`want` 1 = 置位、0 = 未置位；成功打 `set<n> 0x… set|clear`（416 次） |
| `expect_seteq(name, mode, b, n, sel, want)` | `:190-214` | 要求解码长度 == `n`，再断言**整个**对象集字等于 `want`（精确相等，不是「含某位」）。`XSET_UNDEF` 是全 1，而 `xde_sprintset`/`xde_sprintset2` 的 undef 判定是子集式，所以「存在性」断言在整集未知上**恒真**；只有精确比较能区分已建模与未回填。成功打 `set<n> 0x…`（65 次） |
| `expect_flag(name, mode, b, n, bit, want)` | `:217-235` | 要求解码长度 == `n`，再断言单个 flag 位。`bit` 取单个 `C_*` 常量，`want` 1 = 置位、0 = 未置位；成功打 `flag 0x… set|clear`（276 次） |

### 覆盖矩阵（878 项检查）

878 = 854 次助手调用（`expect_len` 46 + `expect_enc` 16 + `expect_fail` 23 + `expect_roundtrip` 6 + `expect_set` 416 + `expect_seteq` 65 + `expect_flag` 276 + `expect_sizes` 2 + `expect_selflen` 4）+ 24 项其余检查（24 项内联/打印器块——`C_RIPREL`、Object sets 打印器、闸门/asm 区段、规范前缀、`sete al` 非 undef、打印器最坏情况、Coverage 区段 ud2、Jcc/LOOP 打印器、`0F C7`/`0F AE` 保留编码等，逐项见覆盖矩阵各行）。**上一轮（批 A + 批 B）新增 61 项**：批 A 的 39 项落在 `// System groups…` 区段，批 B 的 22 项落在 `// Canonical…` 区段的两个新块（重复前缀折叠 14 + REX 紧邻引导字节 8）；**最新一轮新增 44 项**（批 A：PTWRITE 12 + WAITPKG 23 + CLRSSBSY 6 + SFENCE 对照 3，全在 `// System groups…` 区段；批 B 是只读扫描、不加断言）；**本轮（UNDEF 回填）新增 69 条断言、删旧锚点 2 条**——62 条 `expect_seteq` + 7 条 `C_UNDEF` 的 `expect_flag`，全在 `// System groups…` 区段的新块（`:1736-1863`），删掉的是旧的 `0F 00`/`0F 01` 整集 `C_UNDEF` 锚点 ⇒ 净增 67 项；**本轮（`C_BAD` 普查 + `CPUID`/`XABORT`）再新增 124 项**——119 条 `expect_flag`（`62`(BOUND) 7 + `0F 00`/`0F 01` 保留槽 41 + x87 62 + `FF /3`/`/5` 9；其中 `C_BAD` 置位 68 条、未置位 51 条）+ 3 条 `expect_seteq`（`C6 F8` 的 `dst`/`src` + `C6 C0` 的 `dst`）+ 1 条 `expect_set`（`CPUID` 的 `ECX`）+ 1 条 `expect_enc`（32 位 EVEX 对照，证明 BOUND 的 `C_BAD` 不影响 `62` 的 EVEX 分派）⇒ **878**（比上一轮净增 124）；`### 覆盖矩阵` 标题与 CLI 说明已同步改为 878。以上计数都是对 **2294** 行文件的实测值（助手自身的定义行不计入调用）。

| 区段注释 | 行 | 检查数 | 内容 |
|----------|-----|--------|------|
| `// 64-bit GP` | `:239` | 26 | nop、`xor eax,eax`、`xor rax,rax`（含往返）、`mov rax,imm64`、`mov eax,imm32`、`mov r8,imm64`、RIP 相对（+ 内联 `C_RIPREL`）、`call [rip+0]`、`call rel32`、`66 call rel32`、ret、`push rbp`、`sub rsp,0x20`、SIB、`mov r8,[rsp+0x28]`、`nop dword [rax+rax]`、endbr64、syscall、movsxd、`moffs64` ×2、`test rax,imm32`、bt、`67` 前缀 |
| `// SSE / 0F38 / 0F3A` | `:352` | 6 | movups、palignr、pshufb、crc32、movbe、3DNow `pavgusb` |
| `// VEX` | `:378` | 6 | VEX2 `vaddps`（含往返）、VEX3 `andn` ×2、`rorx`、`vzeroupper` |
| `// EVEX` | `:401` | 4 | `vaddps zmm`（含往返）、EVEX 内存形式、`vrndscaless` |
| `// XOP` | `:420` | 4 | `vfrczpd`（含往返）、`vpcomb`、`bextr` |
| `// 32-bit mode: LES vs VEX, BOUND vs EVEX` | `:435` | 18 | LES/LDS、32 位 VEX2、BOUND（**本轮 +7 条 `C_BAD` 断言**：`62 /0`/`/3`/`/7` 的 mod=3 在 32 位、`/0` 的 mod=3 在 16 位、`bound [eax]`/`bound [eax] /1` 两条不置位对照，`:446-462`）、32 位 EVEX `vaddps` 对照（**本轮 +1 条 `expect_enc`**：`62` 仍按 EVEX 分派，见 Known Gaps 的 BOUND 条）、`inc eax`、truncated REX、`rex nop`、aaa 非法/合法、`push es` 非法 |
| （无区段注释的独立块） | `:483-486` | 1 | `xor eax,eax legacy enc`（`XDE_ENC_LEGACY`）——只证明 legacy 指令不被误判成向量/REX2 编码，作用域有限，见「覆盖缺口」第 3 条 |
| `// REX2 (APX)` | `:488` | 2 | `rex2 lea`、`rex2 imul` |
| `// Object sets: 8-bit extension registers vs high bytes, and APX EGPRs` | `:498-601` | 32 | 27 项 `expect_set` + 4 项内联打印器检查 + 1 项字节级往返：`mov spl,al`（`XSET_SPL` 置位且**不含** `XSET_SP`）、`mov ah,al`（`XSET_AH` 置位且**不含** `XSET_SPL`，钉住无 REX 时的差异）、`rex2 lea r16d dst2`（`dst_set2` 含 `XSET2_R16`、`dst_set` 不含 `XSET_OTHER`）、`rex2 lea r31d,[rax]`（`XSET2_R31`）、`rex2 push r16`（`src_set2` 含 `XSET2_R16`，钉住 opcode+r 的 B4）、`rex2 mov eax,[r16]`（SIB base 的 B4 → `src_set2` 含 R16，同时 `src_set` 含 `XSET_RAX`）、`rex2 add rax,r16`（`mod == 3` 下 reg 是 EGPR、r/m 是 legacy GPR，`:529-535`）；打印器输出串断言（`R16` / `R8B` / `SPL` / 解码后再打印 `R16`，`:536-565`）。`:567-601` 是同区段续块（注释 `// REX2 round-trip with a legacy prefix, MOV store forms, 8-bit r8-r15.`）：`66 D5 40 8D 00` 往返（钉 `p_66` 与 REX2 头共存）、`C6 C0 12`（`mov al,0x12` 报 dst 不报 src）、`8B C3`（`mov eax,ebx` 的 r/m 仍计入 src，反向控制）、`41 88 C0`（`mov r8b,al` → `XSET_R8` 与 `XSET2_R8B` 同时置位）、`41 88 C7`（`r15b` → `XSET2_R15B`）、`49 8B C0`（`mov rax,r8` → **无** `R8B`）、`44 8B C0`（`mov r8d,eax` → **无** `R8B`） |
| `// XOP gate in 16/32-bit, the relocated C_REL flag, and xde_asm limits` | `:603-708` | 14 | 4 次助手调用 + 10 项内联检查：`8F 08` 在 32 位 → `expect_len` 长度 2、`C_BAD` 置位；`call rel32` → `C_REL` 置位且 `C_BAD` 未置位；`xde_asm` 对 `datasize=200`/`addrsize=200`/`nvex=200` 夹到数组容量后分别返回 9/9/5；`xde_asm_buf(out,4)` 对 7 字节指令返回 0、`xde_asm_buf(out,7)` 返回 7；`xde_sprintfl` 对 `push rbp` 含 `C_PUSH`、对 `ret` 含 `C_CMD_RET`、对 `mov eax,imm32` 含 `C_DATA4`、对 RIP 相对 `mov` 含 `C_ADDR4`；`xde_sprintset2(XSET2_ALL \| 1<<40)` 打印 `???` |
| `// Canonical prefix order, and the flag intents recorded in xde102/todo.` | `:710-871` | 45 | **上一轮新增的两个编解码往返块**——重复前缀折叠 14 项（`:732-764`：`expect_sizes` 2 / `expect_len` 4 / `expect_selflen` 4 / `expect_flag` 4，钉住「同组最后者生效」、重复仍标 `C_BAD`、以及 `xde_asm` 输出自洽）+ REX 紧邻 VEX/EVEX/XOP/REX2 引导字节 8 项（`:765-789`：`rex then vex2/vex3/evex/xop/rex2 bad` 5 条置位，`rex xor`／无 REX 的 `C5 04 08`／`vaddps` 3 条对照不置位）——外加 17 项 `expect_set` + 6 项内联检查：规范前缀顺序 2 项（`:711-731`，`67 66 90` → `66 67 90`、`64 F3 A4` → `F3 64 A4`）；REP/CMPS 标志 7 项（`:790-802`：`rep movsb` src/dst 均无 FL 且有 RCX；`rep cmpsb` src+dst 有 FL；`cmpsb` dst 有 FL、src 无 FL）；`cld`/`std` dst 有 FL 各 1 项（`:803-808`）；`SETcc` 4 项（`:809-827`：`sete al` dst 有 AL、src 有 FL、src 无 AL；`sete r8b` dst2 有 `R8B`）+ 1 项内联「`sete al` 的 `xde_sprintset(src_set)` 不是 `???`」；`SAHF`/`LAHF` 4 项（`:828-835`：`sahf` src 有 AH / dst 有 FL，`lahf` src 有 FL / dst 有 AH）；打印器最坏情况 3 项（`:836-871`：全位置位的 `allflags` → `xde_sprintfl` 227 字节、`xde_sprintset(~0ULL ^ 1<<63)` 79 字节、`xde_sprintset2(XSET2_ALL & ~XSET2_R16)` 97 字节，均断言 `< 256`） |
| `// Coverage: mode-dependent sets, implicit registers, I/O and flags.` | `:873-1017` | 72 | 40 项 `expect_set` + 31 项 `expect_flag` + 1 项内联 `fail`（`ud2 sets undefined`）：模式相关栈集（`push` 64 位 → `XSET_RSP`；32 位 → `XSET_ESP` 且 `XSET_RSP & ~XSET_ESP` 清）、16 位 `movsb` → SI/DI、16 位 `mov ax,[1234]` → MEM/AX/`C_ADDR2`、`PUSHA` → EAX/EDI/ESP、I/O → DEV/DX（含 `OUT` 的负向控制）、`CPUID` → src EAX **+ECX**（本轮补的 `:913-914`）、dst EAX/EBX/ECX/EDX、`MOV` 与段寄存器互传 → OTHER、`LEAVE` → RSP/RBP、移位 → CL/AL/FL（含 `shl` 不读 FL 与 `rcl` 读 FL 的正反对照）、`F7 /0` → dst FL、`F6 /2`（`NOT`）→ 无 dst FL、标志 `C_STOP`/`C_CMD_RET`/`C_CMD_JMP`/`C_CMD_JCC`/`C_CMD_CALL`/`C_I64`/`C_REL`/`C_F64`/`C_3DNOW`/`C_OPSZ8`/`C_SIB`/`C_ADDR1`/`C_DATA2`/`C_MODRM`/`C_REX`/`C_REX2`/`C_VEX`/`C_EVEX`/`C_XOP`/`C_PUSH`/`C_POP`/`C_ADDR67`/`C_ADDR8`/`C_DATA8`/`C_DATA1`、`67` + 64 位 mod=0/rm=5 → `C_RIPREL` 清且 `C_ADDR4` 置、`0F 0B`（UD2）→ `C_UNDEF` 且 `xde_sprintset` 输出 `"???"` |
| `// Group-encoded operand forms: 8F /0 POP, F6 /0 TEST, F6 /2 NOT, FF /2 CALL.` | `:1018-1034` | 4 | `pop rax (8F /0)`、`test al,0x12`（`F6 /0`）、`not al`（`F6 /2`）、`call rax`（`FF /2`） |
| `// Jcc and LOOP/JCXZ report what they test instead of an undefined set.` | `:1036-1078` | 11 | 8 项 `expect_set` + 3 项内联打印器检查：`jz rel8`/`jz rel32` → `src_set` 有 `XSET_FL`，`loop` → `src_set`/`dst_set` 都有 `RCX`（且 `src_set` 不读 `FL`），`loope` → `src_set` 有 `FL`，`jcxz` → `src_set` 有 `RCX` 而 `dst_set` 无；3 项内联用 `xde_sprintset` 钉住 `jmp rel8` 的 `src_set`/`dst_set` 打印成**空串**、`jz rel8` 的 `src_set` 打印成 `F`（不再是 `???`） |
| `// Implicit operands: strings, conversions, segment and port I/O.` | `:1080-1134` | 31 | 31 项 `expect_set`：`LODS`（src+dst SI）、`STOS`（DI）、`INS`（DI+DEV+DX，`OUTS` 只断言 RSI/DEV 作正向对照）、`CBW`/`CWDE`/`CDQE`（AL→AX / AX→EAX / EAX→RAX）、`CWD`/`CQO`（AX→DX / RAX→RDX）、`AAA`（src+dst AH）、`AAM`（src AL、dst AX）、`AAD`（src AX）、`POPA`（dst EAX+EDI）、`PUSH ES`/`POP ES`（OTHER）、`XLAT`（RBX）、`ENTER`（src RSP、dst RBP） |
| `// 0F-map implicit operands, and a VEX vvvv that names a GPR.` | `:1135-1229` | 61 | 61 项 `expect_set`：`PUSH FS`（src OTHER）、`POP FS`（dst OTHER）、`SHLD eax,ecx,cl`（src CL），以及 `andn`/`bextr`/`sarx` 各 3 条（`dst reg`/`src rm`/`src vvvv`，三者 `vvvv` 都是 `EAX`，钉住早前的两处缺陷）；**上一轮新增 49 条**——BMI 六族 `andn`/`bextr`/`bzhi`/`sarx`/`shlx`/`shrx` 各 7 条（`dst reg EDX`/`src rm`/`src vvvv`/`no src other`/`no dst other`/`no src mem`/`no dst mem`，编码 `andn70 C4 E2 70 F2 D0`/`bextr78 C4 E2 78 F7 D1`/`bzhi70 C4 E2 70 F5 D0`/`sarx7a C4 E2 7A F7 D1`/`shlx71 C4 E2 71 F7 D0`/`shrx7b C4 E2 7B F7 D1`，半数的 `vvvv == 0` = EAX）、向量对照 4 条（`vaddps` vvvv=1 `C5 F0 58 C2` 与 vvvv=0 `C5 F8 58 C1`，`src`/`dst` 均含 `OTHER`，钉住哨兵语义不影响向量路径）、APX EGPR 3 条（`evex andn` `62 F2 7C 00 F2 C0`，`V'`=0 → vvvv = r16：`src_set2` 含 `XSET2_R16`、`src` 无 `OTHER`、`dst reg EAX`） |
| `// Addressing forms: SIB with an index, without one, and disp32 no base.` | `:1230-1241` | 6 | 6 项 `expect_set`：SIB 带 index（`8B 04 48` → src RAX+RCX）、SIB 无 index（`8B 04 20` → src RAX 且**无** RCX）、disp32 无基址（`8B 04 25 …` → src MEM 且**无** RAX） |
| `// Opcode-embedded registers and the implicit accumulator rules.` | `:1242-1270` | 17 | 17 项 `expect_set`：`BSWAP eax`（src+dst EAX）、`XCHG ecx,eax`（src EAX+ECX、dst ECX）、`MOV AL,Ib`/`MOV EDI,Iv`（dst 寄存器来自 opcode 低 3 位）、`ADD AL,Ib`/`ADD EAX,Iv`（src 累加器、dst FL）、`F7 /4 MUL`（src EAX、dst EDX+FL）、`INC eax`（32 位 src+dst EAX+FL） |
| `// MOVBE / CRC32 share 0F 38 F0/F1; plus the vector object sets.` | `:1271-1325` | 28 | 28 项 `expect_set`（区段内还有 3 行子注释，`:1296`/`:1310`/`:1317`）：`movbe eax,[rax]` 载入形式 2（dst EAX、src M）、`movbe [rdx],eax` 存储形式 2（src EAX、dst M）、`crc32 eax,cl` 2（dst EAX、src ECX）、XOP map 9/8 的 `vfrczpd`/`vpcomb` 各 2（src/dst 均为 `OTHER`）、EVEX 内存形式 3（src M + src/dst OTHER）、`endbr64` 2（src/dst 均无 `RCX`）+ `movss` 2（无 `RCX`、src `OTHER`）——钉住「`F2`/`F3` 不是 REP」与 legacy SSE 缺陷、legacy SSE/MMX 6（`movups` 寄存器形式 3：无 `RCX` + src/dst `OTHER`；`paddb` 1；`movups` 内存形式 2）+ 3DNow `pavgusb` 1（src `OTHER`）、GPR 对照 4（`popcnt` dst EAX/src ECX、`bt` src ECX、`cmpxchg` src ECX） |
| `// System groups: CR/DR moves, RDRAND/RDSEED, fences, FS/GS base, save/restore, prefetch.` | `:1326-2085` | 389 | 181 项 `expect_set` + 143 项 `expect_flag` + 65 项 `expect_seteq`（**本轮新增 69 项断言、删 2 条**：`0F 00`/`0F 01`/`0F 02`/`0F 03` 系统组的 62 条精确集合断言 + 7 条 `C_UNDEF`，见「0F 00 / 0F 01 / 0F 02 / 0F 03」条；**上一轮批 A 新增 39 项**：`RDRAND`/`RDSEED` 的 CF 3 条、`CMPXCHG8B`/`CMPXCHG16B` 的 `EDX:EAX`（`REX.W` 下 `RDX:RAX`）读写对 + ZF 4 条、`XSAVE`/`XSAVEOPT`/`XSAVEC`/`XSAVES`/`XRSTOR`/`XRSTORS` 的状态掩码 `EDX:EAX` 6 条、`0F 18 /0-/3` 与 `0F 0D /0-/1` 的 mod=3 保留编码 2 条，其余为 FXSAVE/FXRSTOR、LDMXCSR/STMXCSR、CLFLUSH/**CLWB**、VMPTRLD/VMPTRST 等**不得置位**对照；区段共 15 个匿名块、78 行子注释，均为实测；**最新一轮批 A 再加 44 项**：`F3 0F AE /4` PTWRITE 12（r/m 只读 GPR 或只读内存、**无目的操作数**、`m` 形式无 `dst M`、不读 `EAX`/`EDX`、无 `C_BAD`）、`F3`/`F2`/`66 0F AE /6` 的 UMONITOR/UMWAIT/TPAUSE 23（r/m 只读 GPR；UMWAIT/TPAUSE 另读 `EDX:EAX` 并写 `FL`；三者两侧无 `OTHER`、均无 `C_BAD`）、`F3 0F AE /6` mod≠3 的 CLRSSBSY 6（`src`/`dst` 均含 `M`、`dst` 含 `FL`、不读 `EDX:EAX`、无 `C_BAD`）、`{F3,0F,AE,F8}` 的 SFENCE 对照 3（仍**无操作数**、无 `C_BAD`））：MOV CR/DR 12（`mov eax,cr0`/`mov eax,dr0`/`mov rax,cr0`：`src` 含 `OTHER` 且**无** `RAX`、`dst` 含 `RAX`；`mov cr0,eax`/`mov dr0,eax`：`src` 含 `RAX`、`dst` 含 `OTHER` 且**无** `RAX`）、`rdrand`/`rdseed`/`rdrand rax` 8（`dst` 含 GPR，两侧无 `OTHER`）、`lfence`/`mfence`/`sfence` 6 + FS/GS base 10（`rdfsbase`/`rdgsbase` 的 `dst` 含 `RAX`；`wrfsbase`/`wrgsbase` 的 `src` 含 `RAX`、`dst` 含 `OTHER`）+ 14 条 `C_BAD` 边界（5 条 `0F AE` mod=3 非法子形式 + 9 条「不得误伤」对照）、`F3 0F AE /5` INCSSPD/INCSSPQ 7（`src` 含 `RAX` 且无 `OTHER`、`dst` 含 `OTHER` 且无 `RAX`）、`0F AE`/`0F C7` 内存 20（11 条写形式 `dst` 含 `M`、4 条只读对照无 `dst M`，另有 `fxsave`/`cmpxchg8b`/`vmptrst`/`vmptrld` 的 `src` 含 `M` 与 `vmptrst dst other`）、prefetch/NOP/ENDBR 32（PREFETCHNTA/T0/T1/T2、PREFETCHW 与 PREFETCHWT1：`src` 含 `M`、无 `OTHER`；NOP Ev/`0F 19`/`0F 1D`/`0F 18 /4` 的 mod≠3 与 mod=3 两组：两侧无 `M`/`OTHER`；`endbr64`：两侧无 `OTHER`）、`0F 1E` NOP Ev / RDSSPD 11（无前缀的 `0F 1E`（mod≠3 与 mod=3）两侧无 `M`/`OTHER`；`F3 0F 1E /0` 的 RDSSPD/RDSSPQ：`src` 含 `OTHER`、`dst` 含 `RAX` 且无 `OTHER`）、`lss`/`lfs`/`lgs`/`movzx` 6（`dst` 含 `RAX`）、`C7 F8`/`C6 F8`（无 `C_BAD`）与 `C7 FA`/`C6 FA`（仍有 `C_BAD`）5 条 + `sldt`/`smsw` 的 `C_UNDEF` 2 条（**本轮已删除，换成下层那批**）+ **最新一轮批 A 的 44 条**（PTWRITE/WAITPKG/CLRSSBSY/SFENCE 对照，见上）+ **本轮的 69 条**（62 条 `expect_seteq` 精确集合断言 + 7 条 `C_UNDEF`，覆盖 `0F 00`/`0F 01`/`0F 02`/`0F 03` 的已建模形式与未回填面）+ **本轮（`C_BAD` 普查）的 106 条**（3 条 `expect_seteq`：`C6 F8` 的 `dst EAX` 与空 `src`、`C6 C0` 的 `dst AL`，`:1730-1734`；41 条 `C_BAD` 断言——`0F 00 /6`/`/7` 全 mod 与 REX2 形式、`0F 01` 内存 `/5` 的四种寻址、mod=3 保留子槽 `C6 C7 CC CD CE D2 D3 E9 EA EB EC ED` 12 槽，`:1864-1953`；62 条 x87——`D8-DF` 逃逸表空白槽（内存 `D9 /1`、`DB /4`、`DB /6`、`DD /5` 与 mod=3 的 27 槽）加 24 条「槽位相邻的真实形式」与 12 槽 alias 对照，`:1955-2085`） |
| `// XA_BAD means "not a usable encoding in any mode", so the legacy forms below, legal in 16/32/64-bit alike, must not carry it.` | `:2087-2274` | 99 | 66 项 `expect_flag(…, C_BAD, 0)` + 7 项 `expect_flag(…, C_I64, 1)` + 19 项 `expect_fail`（64 位拒绝）+ 7 项 `expect_flag(…, C_BAD, 1)`（`FF /7`、`0F B9` UD1 参照，加**本轮** `FF /3`/`/5` 的 mod=3 5 条），4 个匿名块（子注释 `:2087-2088`、`:2162`、`:2171-2173`、`:2248`；最后一块的 `FF /3`/`/5` 断言在 `:2263-2273`）：全合法 legacy 35（`6C-6F` INS/OUTS、`70/71/7A/7B`、`8C/8E`、`9C-9F`、`AD/AF`、`CA/CB/CC/CF`、`D7`、`E0/E1`、`E4-E7`、`EC-EF`、`F4/F5`、`FA/FB`）、`0F B2/B4/B5` 3、`XA_I64` 形式在 16/32 位的合法性 24（22 个 opcode，`push cs`/`aaa` 各两种模式）、64 位 `expect_fail` 19、仍非法 group 2 |
| `// 16-bit` | `:2276` | 2 | `add ax,ax`、`mov ax,[moffs16]` |
| `// truncated` | `:2286` | 1 | 截断的 `mov rax,imm64` |

### 输出与退出码

成功打 `ok %-28s ...`（`len=` / `enc=` / `rejected` / `rt` / `set<n> 0x… set|clear` / `flag 0x… set|clear` / `RIPREL` 以及打印器块的裸串等格式），失败打 `FAIL <name>: <msg>`；结尾固定：

```c
printf("\n%d failure(s)\n", g_fail);
return g_fail ? 1 : 0;
```

**退出码就是唯一 CI 信号**（`build.bat:44` 原样透传）。

### 覆盖缺口（被要求"补测试"时的清单）

1. **对象集覆盖仍偏浅，但已铺到更多指令族**：现在对象集断言共 481 项（416 项 `expect_set` + 65 项 `expect_seteq`；276 项 `expect_flag` 单列于下一条），分布为 Object sets 区段 27 项（`mov spl,al`/`mov ah,al` 两条高字节/扩展字节案例、5 个 REX2/EGPR 向量共 9 条断言，以及 `C6`/`MOV` 存储形式与 8 位 r8b-r15b 几条），Canonical 区段 17 项（`XSET_FL` 9 项 + `SETcc` 4 项 + `SAHF`/`LAHF` 4 项），Coverage 区段 39 项（模式相关栈集、16 位串操作与寻址、`PUSHA`、I/O、`CPUID`、段寄存器、`LEAVE`、移位与组 3 的标志、若干 `C_*` 位、`UD2`），Jcc/LOOP 区段 8 项（`JZ` 读 FL、`LOOP`/`LOOPE`/`JCXZ` 的计数寄存器），以及前两轮新增的五个区段 143 项——`// Implicit operands…` 31 项（字符串 `LODS`/`STOS`/`INS`/`OUTS`、转换 `CBW`/`CWDE`/`CDQE`/`CWD`/`CQO`、BCD `AAA`/`AAM`/`AAD`、栈 `POPA`/`ENTER`、段寄存器 `PUSH ES`/`POP ES`、`XLAT`）、`// 0F-map implicit operands…` 61 项（0F 隐式操作数 + BMI 六族的 `dst reg`/`src rm`/`src vvvv`/两侧无 `OTHER`/两侧无 `MEM` 正负断言 + 向量 `vaddps` vvvv=1/0 的 `OTHER` 对照 + `evex andn` 的 EGPR `vvvv` 落第二字）、`// Addressing forms…` 6 项（SIB 带 index / SIB 不带 index / disp32 无基址）、`// Opcode-embedded registers…` 17 项（`BSWAP`/`XCHG`/`MOV r,I`/`ADD AL|EAX,Iv`/`F7 /4 MUL`/`INC eax`）、`// MOVBE / CRC32…` 28 项（上一轮：MOVBE 载入/存储各 2、`CRC32` 2、XOP map 9/8 的 `vfrczpd`/`vpcomb` 各 2、EVEX 内存形式 3、`endbr64`+`movss` 4、legacy SSE/MMX 6、3DNow `pavgusb` 1、GPR 对照 4），以及 `// System groups…` 区段 80 项（MOV CR/DR 12、RDRAND/RDSEED 8、fences 6 + FS/GS base 10、`0F AE`/`0F C7` 内存 17、prefetch/NOP/ENDBR 21、LSS/LFS/LGS 与 `movzx` 6），**此后两批再 +32 项**（INCSSPD/INCSSPQ 7、`0F AE`/`0F C7` 内存 3、PREFETCHWT1 3、NOP Ev 的 mod=3 形式 8、`0F 1E` NOP Ev 与 RDSSPD/RDSSPQ 11）。**上一轮再 +61 项**：批 A 39 项在 `// System groups…` 区段（`RDRAND`/`RDSEED` 的 CF、`CMPXCHG8B/16B` 的 `EDX:EAX` 对 + ZF、`XSAVE`/`XRSTOR(S)` 掩码与全部对照、`0F 18`/`0F 0D` mod=3 保留编码），批 B 22 项在 `// Canonical…` 区段的两个新块（重复前缀折叠 14 + REX 紧邻引导字节 8）。**更早一轮再 +90 项**，全在 `// XA_BAD means "not a usable encoding in any mode"…` 区段（`:2087-2274`），**不含 `expect_set`**：62 条 `C_BAD` 未置位 + 7 条 `C_I64` 置位 + 2 条 `C_BAD` 置位 + 19 条 64 位 `expect_fail`。**上一轮从「仍未覆盖」里移出**：MOVBE、CRC32、3DNow（现有 `XSET` 断言）、legacy SSE/MMX（`movups`/`paddb` 已覆盖寄存器与内存形式）；**同一轮移出**：MOV CR/DR、RDRAND/RDSEED、LFENCE/MFENCE/SFENCE、FS/GS base、`0F AE`/`0F C7` 的存/取内存形式、PREFETCH\*/NOP Ev/ENDBR、LSS/LFS/LGS、`C7 F8`/`C6 F8` 的 `C_BAD` 边界，以及**向量操作数的 `vvvv`**（`XSET_OTHER` 分支：`vaddps` 的 vvvv=1/0 都折 `OTHER`，`evex andn` 的 `V'` 让 vvvv 落进 `src_set2`）。**此后两批再移出**：`0F 1E` 的 NOP Ev 与 RDSSPD/RDSSPQ、PREFETCHWT1、VMPTRST 的 `dst`、`0F AE` mod=3 的非法子形式（`C_BAD`）、INCSSPD/INCSSPQ、NOP Ev 的 mod=3 形式。**仍缺**：`vzeroupper`（`C5 F8 77`）目前 `src_set`/`dst_set` **皆为 0**——`xde_attr[0F][0x77] == 0`（无 `XA_MODRM`）→ `:1326` 的守卫为假，`parse_modrm`/`apply_modrm_usage`（`:1466-1477`）根本不执行，`memset`（`:1023`）后保持 0；VZEROUPPER 清零 YMM 上半部，按本引擎口径至少应是控制寄存器语义（`OTHER`）——**HEAD 即如此、尚未建模**，不是本轮缺陷。另有 EVEX 的 `aaa`/`z`/`b` 语义（本引擎不建模 → 只能断 `OTHER`）、XOP map 8/9 的其余 opcode、`0F 00`/`0F 01`/`0F 02`/`0F 03` 系统组的**未回填形式**（表侧 `XA_UNDEF` 仍在：`:1496-1507` 先**赋值**整集 `XSET_UNDEF` + `XSET2_ALL`，随后 `undef_sys_operands` 只覆盖已建模形式；未回填者仍恒真、**不可证伪**〔读码推断〕）、`CALL`/`RET`（同理）、非 64 位下的 `CPUID`（断言只在 64 位，`tests/xde_test.c:911-918`；解码侧 `src/xde.c:292-295` 按 `map == 0F` 分派、不按模式，语义本就与模式无关 → 是覆盖缺口而非缺陷；实测 `src` 只置 `EAX` 的问题**本轮已修**——现在 `src` 含 `EAX | ECX`（SDM：子叶选择输入，`src/xde.c:292-294`，断言 `tests/xde_test.c:913-914`）），以及 `LOOP`/`Jcc` 之外的控制转移。位掩码里绝大多数 `XSET_*` 仍无断言；`xde_sprintset`/`xde_sprintset2`/`xde_sprintfl` 三个打印器合计也只被 16 项内联检查触碰（Object sets 区段 4 + 闸门区段 5 + `sete al` 非 undef 1 + 打印器最坏情况 3 + Jcc/LOOP 区段 3）。**最新一轮再 +44 项**（批 A；批 B 是只读扫描、不加断言）：`F3 0F AE /4` PTWRITE 12、`F3`/`F2`/`66 0F AE /6` 的 UMONITOR/UMWAIT/TPAUSE 23、`F3 0F AE /6` mod≠3 的 CLRSSBSY 6、`{F3,0F,AE,F8}` 的 SFENCE 对照 3——全在 `// System groups…` 区段；**本轮再 +62 项 `expect_seteq`**（`0F 00`/`0F 01`/`0F 02`/`0F 03` 系统组，见 Known Gaps「0F 00 / 0F 01 / 0F 02 / 0F 03」条）⇒ 该区段因此在覆盖矩阵里记为 **181 项 `expect_set` + 143 项 `expect_flag` + 65 项 `expect_seteq`**（合计 389，本轮 +106：`0F 00`/`0F 01` 保留槽 41 + x87 62 + `C6 F8`/`C6 C0` 3 条 `expect_seteq`）。对象集是本库的一半价值，断言密度仍低于长度/编码类。
2. **`flag` 位已覆盖解码器能置位的全部 `C_*`（276 条 `expect_flag`）**：`expect_flag` 覆盖 `C_BAD`（未置位 138 条 / 置位 92 条）、`C_REL`、`C_D64`、`C_ADDR2`、`C_STOP`、`C_CMD_RET`/`C_CMD_JMP`/`C_CMD_JCC`/`C_CMD_CALL`、`C_F64`、`C_O64`、`C_3DNOW`、`C_OPSZ8`、`C_SIB`、`C_ADDR1`/`C_ADDR4`、`C_DATA2`、`C_UNDEF`、`C_I64` 与 `C_RIPREL`（未置位），上一轮补齐了 `C_MODRM`（`88 C4`）、`C_REX`（`48 31 C0`）、`C_REX2`（`D5 40 8D 00`）、`C_VEX`（`C5 F8 58 C1`）、`C_EVEX`（`62 F1 7C 48 58 C1`）、`C_XOP`（`8F E9 78 81 C1`）、`C_PUSH`（`55`）、`C_POP`（`8F C0`）、`C_ADDR67`/`C_ADDR8`（`48 A1 <8>`）、`C_DATA8`（`48 B8 <8>`）、`C_DATA1`（`B0 12`），都追加在 Coverage 区段的 flags 块（`tests/xde_test.c:979-990`）；随后那轮再补 System groups 区段的 7 条——`xbegin`（`C7 F8`）/`xabort`（`C6 F8`）/`mov [rsp],imm32` 无 `C_BAD`、`C7 FA`/`C6 FA` 仍有 `C_BAD`、`sldt`/`smsw` 的 `C_UNDEF` 锚点（这两条**本轮已删**，换成精确集合断言）；此后两批再补 14 条 `C_BAD` 断言（5 条 `0F AE` mod=3 非法子形式 + 9 条「不得误伤」对照，`tests/xde_test.c:1405-1418`）；**此后那批再补 90 条**（62 条 `C_BAD` 未置位 + 7 条 `C_I64` 置位 + 2 条 `C_BAD` 置位 + 19 条 64 位 `expect_fail`，`tests/xde_test.c:2087-2274`）；**上一轮再补 61 项**（批 A 39 项在 `// System groups…` 区段、批 B 22 项在 `// Canonical…` 区段，逐项见覆盖矩阵；并新增 `expect_sizes`/`expect_selflen` 两个助手，`C_BAD` 置位断言由 10 条增至 24 条、**本轮（`C_BAD` 普查）再增至 92 条**）；**最新一轮再补 44 条**（4 个新块：PTWRITE 12 / WAITPKG 23 / CLRSSBSY 6 / SFENCE 对照 3，六条 `C_BAD` 断言全部是**未置位** ⇒ 未置位 81 → **87** 条、置位仍 24 条）；**本轮再补 7 条 `C_UNDEF` 断言**（3 条未置位 + 4 条置位，替换旧的 2 条整集锚点）；**本轮（`C_BAD` 普查）再补 119 条 `C_BAD` 断言**（`tests/xde_test.c:446-462` 7 + `:1864-1953` 41 + `:1955-2085` 62 + `:2247-2274` 9）；内联另有 `C_RIPREL`（`:270-277`）与 `C_PUSH`/`C_CMD_RET`/`C_DATA4`/`C_ADDR4` 的 `xde_sprintfl` 串断言（`:665-698`）。于是**无断言的 `C_*` 只剩 2.00 从不置位的那批 1.02 词表项**——`C_SPECIAL`、`C_DATA66`、`C_SRC_*`/`C_DST_*` 操作数微位及其 `C_MOD_*` 别名、`C_ERROR`——已在 `include/xde.h:53-55` 标注，尺寸看 `addrsize`/`datasize`/`p_66`。（`XSET_FL` 是对象集位、不是 `flag` 位，`expect_set` 名下现有 28 项 `XSET_FL` 断言（最新一轮 +3：`umwait`/`tpause`/`clrssbsy` 的 `dst FL`）。）
3. **六种编码类的断言效力不对称**：`XDE_ENC_VEX2`=1 / `VEX3`=2 / `EVEX`=3 / `XOP`=4 / `REX2`=5（`include/xde.h:32-37`）都非 0，这五类的 `expect_enc` 只要解码器漏掉 `diza->enc` 赋值就会失败；而 `XDE_ENC_LEGACY == 0`（`:32`）与 `memset(diza, 0, sizeof(*diza))`（`src/xde.c:1023`）之后的默认值同值，所以 `xor eax,eax legacy enc`（`tests/xde_test.c:485`）只能证明「legacy 指令不被误判成向量/REX2 编码」，**不能**证明解码器真的执行了 `diza->enc = XDE_ENC_LEGACY;`（`XDE_ENC_LEGACY;`（`src/xde.c:1295`）。别把它读成「六种编码类被等价覆盖」。

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
5. 若新属性需要影响 `flag` 或对象集，还要改 `src/xde.c` 的 `apply_attr_flags`（`:123-144`）或三趟对象集推导（`:146` / `:430` / `:760`）；
6. 加测试用例并 `build.bat`。

上一轮示例：`m1[0x90..0x9F]`（SETcc）在 `gen_tables.py:329-330` 从 `XA_MODRM | XA_OPSZ8 | XA_UNDEF` 改成 `XA_MODRM | XA_OPSZ8`（去掉 `XA_UNDEF`），重跑生成器后 `src/xdetbl.c` 的行数与结构不变；FL 读取改在 `apply_usage_special`/`apply_modrm_usage` 里按 opcode 硬编码（`XA_*` 位空间已满，没有对应属性位）。

本轮（分组 opcode 口径）**没有动任何表**：改动全在 `src/xde.c` 的 `apply_modrm_usage`（新增 `mov_crdr`/`rdrand`/`fence`/`fsgsbase`/`endbr`/`nop_ea`/`prefetch_ea`/`mem_store` 八个 opcode+mod+reg 判定，并让通用规则让位）与 `xde_disasm_buf` 的 `C6`/`C7`/`8F` `C_BAD` 判据（加 `mpeek != 0xF8`），所以 `tools/gen_tables.py`、`src/xdetbl.c`、`src/xdetbl.h` 三者**逐字节未变**；`src/xde.c` 里带 `XA_VVVV_GPR` 的分支与 `simd_0f` 名单也一并保持原样。改这类「reg 字段是选择器而非寄存器」的组时，**三层都要看**：表属性、`apply_modrm_usage` 的 `mod == 3` / 内存两条分支、以及 `xde_disasm_buf` 里的组特例。此后两批同样**没有动任何表**：新增 `nop1e`/`incssp` 两个判定、去掉 `nop_ea` 的 mod 条件、给 `mem_store` 的 `0F C7` 列表加上 `/7`，并在 `xde_disasm_buf` 新增 `0F AE` mod=3 的 `C_BAD` 判据（`:1364-1371`）——`tools/gen_tables.py`、`src/xdetbl.c`、`src/xdetbl.h` 仍**逐字节未变**。**上一轮（批 A 提交 `fdce0f3` + 批 B）同样零表改动**：全部落在 `src/xde.c` 的 `apply_modrm_usage` 谓词表与 `xde_disasm_buf`（前缀循环的 `if (!twice)` + `got_opcode` 的 `rex_seen` 判据），`src/xdetbl.c`/`src/xdetbl.h`/`tools/gen_tables.py` 仍逐字节未变。**最新一轮（批 A）同样零表改动**：仍只动 `src/xde.c` 的 `apply_modrm_usage` 谓词表（`umonitor`/`umwait`/`tpause`/`waitpkg`/`ptwrite`/`clrssbsy`），`src/xdetbl.c`/`src/xdetbl.h`/`tools/gen_tables.py` 继续逐字节未变。**本轮（`0F 00`/`0F 01`/`0F 02`/`0F 03` 系统组回填）同样零表改动**：新增 `src/xde.c` 的 `undef_sys_operands`（`:308-428`）、它的调用与 `C_UNDEF` 清理（`:1496-1507`）以及寻址寄存器的 `ea_set` 暂存（`:1012`、`:1472-1475`）——表上的 `XA_UNDEF` 位**原样保留**，回填只在解码侧做，所以 `src/xdetbl.c`/`src/xdetbl.h`/`tools/gen_tables.py` 仍逐字节未变。

**本轮（`C_BAD` 定口径 + 表侧清理）是第一次按「`XA_BAD` = 该编码在任何模式下都不可用」改 legacy / 0F map**：`tools/gen_tables.py` 删掉 60 个 `XA_BAD` 槽（35 个三模式全合法的 legacy 槽 + 22 个带 `XA_I64` 的 legacy 槽 + `m1` 的 `0F B2`/`B4`/`B5`），并在 `:20-21` 给 `XA_BAD` 补注释「not a usable encoding in any mode; mode-limited forms use XA_I64/XA_O64/XA_F64/XA_D64 instead」；`src/xdetbl.c` 由重跑生成器产出（24 行改动、24 插 24 删，**字节数 40140 不变** ⇒ 只翻转属性位，`src/xdetbl.c` **未手工编辑**）；`src/xdetbl.h` 未动（无新常量，`check_header()` 通过）；`src/xde.c` 未动（64 位拒绝仍走 `:1311` 的 `XA_I64` 分支，与 `XA_BAD` 无关）。删后 `XA_BAD` 余 **28 槽** = `m0[0xD6]`（SALC，`XA_BAD|XA_OPSZ8`）+ `m0[0xF1]`（INT1/ICEBP，`XA_BAD|XA_UNDEF`）+ 26 个 group 槽。生成器幂等实测：连跑两次逐字节一致（md5 `3fc495d3a3ce7180179bd5e8ddd261f0`、40140 字节、退出码 0）。双向 mutation 实测：把 `XA_BAD` 加回 `m0[0xCC]`（INT3）→ 重跑生成器后**恰好 1 条**失败（`int3 not bad`）；删掉 `group[XG_5][7]` 的 `XA_BAD` → **恰好 1 条**失败（`FF /7 still bad`）；恢复后 md5 复原、`0 failure(s)`。测试侧同步加 90 条断言（见「覆盖缺口」与覆盖矩阵的 `:2087-2274` 行）。

**注意**：`XA_*` 的 32 位已占满（标志 0-20、IMM 21-24、group 25-31）。加第 22 个属性标志需要先把 `xde_attr`/`attr` 拓宽到 64 位（影响 `src/xdetbl.h`、`src/xdetbl.c`、`gen_tables.py`、`src/xde.c` 的 `uint32_t attr` 形参与 `apply_attr_flags`），或复用/回收现有位。

### 加一个 group（`/reg` 分派）

改 `tools/gen_tables.py` 的 `XG_*` 常量与对应 map 的 `GRP(n)`，**并同步** `src/xdetbl.h:47-79` 的 `enum xde_group_id`（顺序即 id，`XG_COUNT` 必须跟着变，它同时决定 `xde_group[30][8]` 的行数），然后重新生成。

### 加一个公共 API 函数

1. `include/xde.h` 声明（放在 `:285-299` 区块内，保持 `extern "C"` 覆盖）；
2. `src/xde.c` 实现，导出函数用 `__cdecl`（把新函数加进 `src/xde.c` 也要相应更新 `Key Directories` 的 static 计数）；
3. `tests/xde_test.c` 加用例（仓库现有 9 个 `expect_*` 助手，够用就别新增助手）；
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
| `xde_disasm_buf(ptr, 0, …)` 没报错却按 15 字节解码 | `max_len == 0` 是「用默认 `XDE_MAXLEN`」而不是「零字节上限」（`src/xde.c:1018-1021`；`xde_asm_buf` 同构，`:1581-1582`）。要限长请传显式上界；批 B v2 的截断扫描因此把 `len == 1` 的 **8,437,552** 例单列跳过。 |

## Known Gaps / Cautions

以下是**代码可证**的已知缺口（作者未用 TODO 标注）：

- **REX2 保留 `p_66`/`p_rep`（刻意行为，不是缺口）**（对比 VEX `:1213-1214`/`:1236-1237`、EVEX `:1176-1177`、XOP `:1281-1282` 都清）——`66` 是 REX2 合法的遗留前缀，不是多余前缀。编码侧 `p_66` 与其它遗留前缀一起在 `:1600` 统一发射（无 nvex 专属分支），所以 `66 D5 …` 现在能字节级往返；改这里要解码/编码两侧一起动。
- **`C_ADDR8` 只来自 MOFFS（原先那条不可达分支已删）**：`parse_modrm` 里 `disp` 只会是 1/2/4（`:953-962`，末行注释写明），原来永不触发的 `else → C_ADDR8` 已删除；8 字节地址只经 MOFFS 路径（`:1486`）。新增位移宽度前先确认这条不变量还成立。
- **`xde_sprintfl` 覆盖解码器能产生的每一个 flag 位**（外加从不置位的 `C_DATA66`）（`src/xde_text.c:7-49`，不再是缺口）：共 34 个名字——低半 12 个（`C_BAD`/`C_REL`/`C_STOP`/`C_MODRM`/`C_SIB`/`C_RIPREL`/`C_REX`/`C_VEX`/`C_EVEX`/`C_XOP`/`C_REX2`/`C_UNDEF`，`:10-21`）+ `C_OPSZ8`（`:22`）+ 10 个尺寸类（`C_ADDR67`/`C_DATA66`/`C_ADDR1/2/4/8`/`C_DATA1/2/4/8`，`:23-32`；这 34 个名字里 `C_DATA66`（`:24`）是 2.00 从不置位的词表项）+ 7 个（`C_PUSH`/`C_POP`/`C_I64`/`C_O64`/`C_F64`/`C_D64`/`C_3DNOW`，`:33-39`）+ `C_CMD_*` 4 个（用 `XDE_CMD(fl)` 的 switch，`:40-46`）。解码路径不再置位的操作数角色位 `C_SRC_*`/`C_DST_*`（2.00 的 `src/xde.c` 一处都不写）仍不打印。缓冲区契约可验证：`tests/xde_test.c:836-871` 用全位置位的 `allflags` 钉住最坏情况 `xde_sprintfl` **227 字节**（断言 `< 256`），`xde_sprintset(~0ULL ^ 1<<63)` 79 字节、`xde_sprintset2(XSET2_ALL & ~XSET2_R16)` 97 字节，所以头文件那句「output should be at least 256 bytes」本轮首次被测到。
- **undef 标记是子集判定**：`(set & XSET_UNDEF) == XSET_UNDEF`（`src/xde_text.c:55`）与 `(set2 & XSET2_ALL) == XSET2_ALL`（`:142`）。后者严格更稳健——`set2` 带 bit ≥ 24 的杂位时仍打 `"???"`（`tests/xde_test.c:702` 用 `XSET2_ALL | 0x10000000000ULL` 钉住），不再依赖解码侧的整体赋值。
- **`XA_UNDEF` 已按「可确定性」拆分，只留给副作用确实未建模的指令**：`Jcc`（`70-7F` / `0F 80-8F`）只读 `XSET_FL`；`LOOP`/`LOOPE`/`LOOPNE`（`E0`/`E1`/`E2`）读并写计数寄存器（`XSET_CX`/`ECX`/`RCX`，宽度约定同 REP），`JCXZ`（`E3`）只读；近 `JMP`（`E9`/`EB`）读写都是空集（表侧 `tools/gen_tables.py:147-149`、`:230-234`、`:240`/`:242`，解码侧 `src/xde.c:264-265`、`:266-271` 与 0F 段的 `:301-302`）。仍带 `XA_UNDEF` 是刻意选择：`CALL`（`E8`、grp5 `/2`/`/3`）与 `RET`/`RETF`（`C2`/`C3`/`CA`/`CB`）——被调用者会破坏未知寄存器；远 `JMP`（`EA`、grp5 `/4`/`/5`）与 `IRET`（`CF`）——要装载 `CS`，而段寄存器在本引擎里只折成 `XSET_OTHER`；`INT`/`INTO`/`INT1`（`CD`/`CE`/`F1`，`INT3`（`CC`）只带 `XA_BAD`）、`BOUND`（`62`）、`WAIT`（`9B`）、x87（`D8-DF`）、`RSM`（`0F AA`）、`CLTS`/`INVD`/`WBINVD`/`WRMSR`/`RDMSR`（`0F 06`/`08`/`09`/`30`/`32`）、`UD2`（`0F 0B`）——系统/MSR/x87 状态同样未建模（`LAR`/`LSL` 与 `0F 00`/`0F 01` 的 `SLDT`…`INVLPG` 已**本轮回填**，见下条）。`flag` 不受影响（`C_CMD_JCC`/`C_REL`/`C_F64` 照常置位，见 `tests/xde_test.c:956-961`）；一旦置位，`src/xde.c:1496-1501` 把 `src_set`/`dst_set` 整体赋值成 `XSET_UNDEF`（**赋值，非 OR**）；此后 `undef_sys_operands`（`:1505`）决定是否回填，`C_UNDEF` 因此**只剩未回填面**（`0F 00 /6 /7`、`0F 01 /5`、`0F 01` 的 mod=3 特殊组、SGDT/SIDT/LGDT/LIDT/INVLPG 的 reg 形式）。
- **`C_I64` 只在 16/32 位出现，`C_O64` 只在 64 位出现**：`apply_attr_flags`（`src/xde.c:134`）照 `XA_I64` 置 `C_I64`，但带 `XA_I64` 的指令在 `mode == 64` 时更早被拒（`src/xde.c:1311-1312`）；16/32 位下它们合法，所以 `inc eax`（`40`，`gen_tables.py:124`）与 `aaa`（`37`，`:120`）在 32 位解码后会带上 `C_I64`（`aaa` 本轮起**不再**带 `C_BAD`，见下「表侧 `XA_BAD`」条）。`C_O64` 相反，只在 64 位成功解码上出现（`syscall`，`m1[0x05]`）。两个名字 `xde_sprintfl` 都打印（`src/xde_text.c:35-36`），且现在都有解码断言（`C_I64` → 32 位 `inc eax`，`C_O64` → `syscall`，`tests/xde_test.c:958-962`）——这条记录的是「位只在单一模式下可达」这一事实，不是覆盖缺口。
- **编码侧已按 SDM 组序规范化前缀**：`xde_asm_buf` 现在按 SDM 组序发射遗留前缀——lock/rep（组1）→ segment（组2）→ `66`（组3）→ `67`（组4）（`:1597-1601`），随后才是 REX 或 `vex[]` + opcode；`asm_size()`（`:1546-1572`）的计数顺序同步调整（字节总数不变）。解码侧仍不限制前缀顺序，所以非规范序输入（如 `67 66 90`、`64 F3 A4`）能解码，但**重编码会规范化**为 `66 67 90`、`F3 64 A4`——`xde_asm` 的输出不再逐字节等于非规范输入，往返测试因此只用规范序输入，或显式断言规范化结果。**批 B 起同组重复前缀也折叠为「最后者生效」**：`66 66 …`/`67 67 …` 只发一个前缀，输出必然短于输入（这类输入改用 `expect_selflen` 钉自洽性，`expect_roundtrip` 表达不了）。
- **编码器契约与「已判 `C_BAD` 不保证往返」边界（连续实测；依据是全空间 decode→encode 往返扫描 + 批 B v2 的截断/容量/扩展枚举扫描）**：`xde_asm` 对**合法**（解码时未标 `C_BAD`）的输入应**字节级往返**，对**已判 `C_BAD`** 的输入**不提供保证**——编码器不为非法形式背书；**扫描规模**：**38,522,108** 次往返（非法编码 `len=0` 12,964,612 次不计），**字节全等 38,190,516（99.14%）**、不等 331,592、`xde_asm` 返回 0 **0 次**（从未触及 `max_len`）；按 map 分：legacy 4,662,760 解码 / 不等 70,196，0F 4,770,864 / **0**，0F38 4,358,592 / **0**，0F3A 2,283,072 / **0**，VEX2 5,787,648 / **0**，VEX3 4,698,112 / **0**，EVEX 3,475,456 / **0**，XOP 2,157,568 / **0**，REX2 5,750,784 / **0**，双前缀 pair 577,252 / 261,396 ⇒ 转义/VEX 类合计 **33,282,096 次 0 不等**；**探针非空过**：把 `xde_asm_buf` 的 `if (diza->flag & C_MODRM) *p++ = diza->modrm;` 改成不发，0F map 的不等数由 **0** 跳到 **2,949,520**（改回恢复 0）；**本轮再叠加 `asm_size()` 与写出序列的一致性扫描（见下「`asm_size()` 是写出序列的第二份文本实现」条）**：**170,675,981** 例解码里 `asm_size() == 实际写出字节数` **零违例**（`S > 15` / 溢出写 / `ret == 0` 却写入 / `K == 0` 各 0 例），四个变异分别被 **987,834**（删 `C_SIB` 项）/ **14 例容量违例（全部触发 `PAGE_NOACCESS` 保护页）** / **185,016**（删 `p_seg` 项）/ **240** 例（容量判定 `>` → `>=`）捕获；**不等分 4 类**：① 遗留前缀重排 186,996、② 重复前缀折叠 117,048（两者皆为已知规范化，②只丢冗余字节、语义不变）、③ **重复 `66`/`67` 的尺寸效应** 19,220 / ~450 类、④ **REX 紧邻向量引导字节** 8,328 / ~151 类——③④ 是真缺陷，批 B 已修；**修复后同探针重扫**：`canon_len_CBAD_only` 315,616 → **325,288**、`REAL_semantic` 14,902 → **6,288**、`REAL_redecode_len` 1,074 → **16**、`REAL_asm0` 0 → 0、不等类数 1,369 → **921**，转义/VEX 类仍 **0**；**残差是声明式的**：6,288 + 16 例**全部**是「REX 紧邻引导字节」的输入，修复后**已标 `C_BAD`** ⇒ 记为**声明式残差**，不再追求字节级复现；**差分审计（无附带损伤的证据）**：2,097,152 个输入（16 种前缀组合 × 全 opcode × 全 modrm × mode 64/32）在 HEAD 与修复后**逐字段**比对（`len`/`flag`/`datasize`/`defdata`/`defaddr`/`addrsize`）——变动仅 260,032（重复 `66`/`67`）+ 6,144（REX 紧邻引导字节），**附带损伤 0**；**批 B v2（同一探针的扩展枚举，只读、未改仓库文件）**：**136,156,160** 次输入（其中 `K ≥ 1`、可编码 **107,614,458** 次），identical 97,937,564 + canon 9,665,262 = **99.9892%**（分母 107,614,458），残差 **11,632（0.0108%）** = REAL-sem 11,584 + REAL-len 48，**全部**是 mode64/legacy 的「REX 紧邻 VEX/EVEX/XOP/REX2」输入（`C5` 8,164 / `C4` 2,992 / `8F` 174 / `62` 166 / `D5` 88；REAL-len 全为 `D5 48`），自动分标签另证「无 REX 的向量引导」= **0**、「其他 opcode」= **0** ⇒ **无第三种形状**；**新增枚举面**：mode **16**、SIB `mod≠3 && rm==4` **全 256 种**、非均匀 disp/imm（`80`/`FF`/混合）、前缀链 **3–4 深**（含 `2E 64 66 67` 等 6⁴ 组合）、46 条字面量前缀 × mode16/32 ⇒ **无新违例、无新缺陷类**（mode16/mode32 与各 map 的 REAL-sem/REAL-len 均 **0**）；**上一轮基线的独立复刻**：把同一探针编到 `fdce0f3` 重跑得 38,522,108 解码 / 38,190,516 identical / canon 315,616 / REAL-sem 14,902 / REAL-len 1,074，与上一轮记录**逐数字一致**，再从当前树同枚举得 canon 315,616 → **325,288**、REAL-sem 14,902 → **6,288**、REAL-len 1,074 → **16** ⇒ `ee28e9d` 修复的**独立确证**；**新事实**：`66 48 D5 04 25 00 00 00 00` 重编码 9 → **6** 字节（不只丢 REX，**长度也变**）；**外部交叉验证**：objdump 2.36.1 对 `40 c5 04 08` 报 `(bad)`、对 `66 48 d5` 报 `data16 rex.W (bad)`、对 `0f 0b` 报 `ud2`，与 xde 的 `C_BAD` 判定一致。**扫描边界（v2 后更新）**：单 modrm 全枚举仍在，SIB 已扩到全 256、disp/imm 改非均匀、「mode 16 未测」与「前缀组合 ≤2」两项已被 v2 扩掉；仍未测的是 >4 深前缀链与多指令串。
- **读 / 写边界不动点：截断与容量在 99,176,906 + 107,614,458 例上零违例（批 B v2，只读扫描、未改仓库文件）**：① **解码截断**——对 **99,176,906** 个「可解码且 `len ≥ 2`」输入（mode16 33,356,182 / mode32 34,763,444 / mode64 31,057,280）逐条改喂 `xde_disasm_buf(buf, len-1, …)`：返回非 0 **0** 例、越过 `max_len` 读 **0** 例（把 `buf + max_len` 贴到 `VirtualAlloc` 的 `PAGE_NOACCESS` 保护页页尾、靠 AV 触发来验）、`(buf, len) != len` **0** 例；最小复现 `0F 0B`(L=2)→(1)=0/(2)=2、`48 B8 1122334455667788`(L=10)→(9)=0/(10)=10、`C5 F8 58 C0`(L=4)→(3)=0/(4)=4。② **编码容量**——对 **107,614,458** 个「编码成功且 `K ≥ 1`」输入逐条改喂 `xde_asm_buf(out, K-1, …)`（本轮补的 `asm_size()` 一致性维度见下条）：返回非 0 **0** 例、16 字节 `0xA5` canary 被写 **0** 例（返回 0 时**一个字节都没写**）、越界写 AV **0** 例、`(out, K) != K` **0** 例；`K == 0`（解码成功却编不出字节）**从未出现**；例 `48 31 C0`(K=3)→(2)=0 且 canary 完好 /(3)=3。③ **mutation 对照（证明检查会响）**：MUT-A 禁截断（`cur.end = opcode + max_len` → `opcode + XDE_MAXLEN`）→ 越界读 **13,173,479/13,173,479（100%）**；MUT-B 放宽容量判断（`asm_size(…) > max_len` → `> max_len + 1`）→ 返回非 0 / canary 被写 / 越界写 **三者同时 13,173,479** 例。④ **已知语义坑**：`max_len == 0` 被当作默认 15（见「返回值契约」段），所以「`len == 1`」的 **8,437,552** 例在①的 `len-1` 子检查里被单列跳过——**不是缺陷**，但调用方要限长必须传显式上界。
- **`xde102/todo` 的 4 条现已全部处理**（1.02 时代的留档，不再有未决项）：① `REP` 对不同串指令的标志差异 → 已修：`REP` **只在串操作上**把 `CX/ECX/RCX` 计入 src+dst，不再无条件置 FL；`CMPS`/`SCAS` 写 FL、带 `REP` 时再读 FL（`src/xde.c:158-168`、`:251-260`）；② `setxx` → 已修：`0F 90-9F` 读 FL 且 r/m 只写不读（`:298-300`、`:443`）；③ `cld/std/cmpsb` 的 DF 源集 → 按「DF 不作为源」处理（`CLD`/`STD` 只置 `dst_set |= XSET_FL`）；④ `PUSH` 的栈宽不受 `67` 影响 → 2.00 早已由 `XA_PUSH` → `stack_set(mode)` 处理（`:839-848`）。被要求处理这些行为前先确认是否仍适用。
- **`xde102/` 与 `src/` 存在同名文件**（`xde.c`/`xde.h`/`xdetbl.c`/`xde_text.c`）。搜索或批量替换务必限定路径，否则会误改参考资料。`xde102/xde.c:5` 用 `#include "xdetbl.c"` 文本包含其数据表，**无法与 2.00 同编译**（并重复定义 `xde_disasm`/`xde_asm`/`xde_sprintfl`/`xde_sprintset`）。仓库中没有任何构建文件或脚本引用 `xde102/`。
- **`0F 00` / `0F 01` / `0F 02` / `0F 03` 系统组：本轮已按 ModR/M 回填，只有未建模形式仍是整集恒真**：`src/xde.c:1496-1501` 仍先对整集**赋值** `XSET_UNDEF` + `XSET2_ALL`（**赋值，非 OR**），紧接着 `undef_sys_operands(diza, ea_set, ea_set2)`（定义 `:339-428`，调用 `:1505`）按 `(opcode2, mod, reg)` 重写 `src`/`dst`（含第二对象集字）并清 `C_UNDEF`，返回 0 时原样保留整集与 `C_UNDEF`。已建模形式：`0F 00 /0 SLDT`、`/1 STR` → `dst` = r/m（mod=3 走 GPR、mod≠3 走 `MEM`）、无源；`/2 LLDT`、`/3 LTR` → `src` = r/m16、`dst |= OTHER`；`/4 VERR`、`/5 VERW` → `src` = r/m16、`dst |= XSET_FL`（ZF）；`0F 01 /0 SGDT`、`/1 SIDT` → `dst |= MEM`；`/2 LGDT`、`/3 LIDT` → `src |= MEM`、`dst |= OTHER`；`/4 SMSW` → `dst` = r/m（GPR/MEM）；`/6 LMSW` → `src` = r/m16、`dst |= OTHER`；`/7 INVLPG` → `src |= MEM`、无目的；`0F 02 LAR`、`0F 03 LSL` → `dst` = reg 字段（按操作数尺寸）、`src` = r/m16、`dst |= XSET_FL`。**仍未回填**（整集 + `C_UNDEF` 原样保留）：`0F 00 /6`、`/7`、`0F 01 /5`、`0F 01` 的 **mod=3 特殊组**、SGDT/SIDT/LGDT/LIDT/INVLPG 的 **reg 形式**（SDM 判 #UD），以及任何 VEX/EVEX/XOP 编码（函数的第一个判据即返回 0）⇒ **`C_UNDEF` 现在只覆盖这些未回填形式，不再等价于表侧的 `XA_UNDEF`**。
- **一处 SDM 驱动的偏离（有意，已裁决保留）**：任务原写「SLDT/STR 的 `dst` 是 16 位 GPR」，但 SDM 说 64 位寄存器目的**零扩展**、32 位目的高 16 位 cleared/undefined，SMSW 更明确列 r16/r32/r64 并零扩展 CR0 ⇒ 实现按**操作数尺寸**建模（64 位 `0F 00 C0` → `XSET_RAX`；`66 0F 00 C0` → `XSET_AX`；32 位模式 → `XSET_EAX`）。只读的 r/m16（LLDT/LTR/VERR/VERW/LMSW）固定 16 位，LAR/LSL 只用选择子的低 16 位。
- **判据方法论（回填整集 UNDEF 时只能用精确集合比较）**：`XSET_UNDEF` 是**全 1**，而 `xde_sprintset`/`xde_sprintset2` 的 undef 判定是**子集式**（`(set & XSET_UNDEF) == XSET_UNDEF`），所以任何「含某位 / 不含某位」的存在性断言（`expect_set`）在整集 UNDEF 下**恒真**——用它写回填会得出「修复前后都通过」的假证据。本轮因此新增 `expect_seteq`（`tests/xde_test.c:190-214`，对 sel 0/1/2/3 做**精确集合相等**比较）。旧锚点 `expect_flag("0F 00 undef"/"0F 01 undef", C_UNDEF, 1)` **删除**（不改写成反向钉子），换成本轮 69 条断言（`tests/xde_test.c:1736-1863`：62 条 `expect_seteq` + 7 条 `C_UNDEF`）。mutation 对照（9 组，各对应一组精确断言）：全回填关闭 → 53 条 `seteq` + 3 条 not-undef 全红（56 条）；`0F 00` 目的系统寄存器改错 → 3；r/m16 源宽度 → 5；SGDT/SIDT 误加 src `MEM` → 2；LAR/LSL 目的宽度 → 4；LAR/LSL 源 → 3；SLDT/STR/SMSW 目的宽度 → 3；地址寄存器保留（`src_set` 只赋 src、丢掉 `ea_set`）→ 11；`C_UNDEF` 清理 → 3。
- **UNDEF 面的覆盖面（并行只读审计，实测）**：`XA_UNDEF` 的唯一整集赋值点就是 `src/xde.c:1496-1501`（**赋值，非 OR**，在对象集三趟之后）；表侧赋值点在 `tools/gen_tables.py` 的 `m0`（`62`/`9A`/`9B`/`C2`/`C3`/`CA`/`CB`/`CD`/`CE`/`CF`/`D8-DF`(x87)/`E8`/`EA`/`F1` 与 `group[XG_5][2..5]`，`:135`/`:173-174`/`:204-205`/`:212-213`/`:215-217`/`:228`/`:239`/`:241`/`:248`/`:557-560`）与 `m1`（`00`/`01`（整组）/`02`/`03`/`06`/`08`/`09`/`0B`/`30`/`32`/`AA`，`:265-274`/`:299-301`/`:342`）。objdump 判合法且被 UNDEF 覆盖的 ModR/M 槽共 **3025** 个（`0F 00` 192/256、`0F 01` 220/256、`0F 02` 与 `0F 03` 各 256、x87 **1797**、`FF /2-/5` 112、`62`(m32) 192），另有 19 个无 ModR/M 的编码。
- **UNDEF 面的长度 / 立即数（实测）**：9 组 × 256 槽 = **1744** 组，`len` 与 objdump **零差异**；`0F 01` 无立即数 ⇒ `datasize` 恒 0 **正确**（`datasize` 语义 = 立即数字节数，不是操作数宽度）。`m16:32`/`m16:64` 是**内存操作数宽度**，`struct xde_instr` 没有承载字段（`addrsize` = 位移、`datasize` = 立即数）⇒ 现状**不可表达**，这**不是** `datasize` 的缺陷；要区分需新增字段。
- **应标 `C_BAD` 却未标：431 槽——本轮已全部修掉（普查口径：objdump 判 `(bad)` 而 xde 接受且 `bad == 0`）**：`0F 00` 64（`/6`/`/7` 全 mod）、`0F 01` 36（mod≠3 的 `/5` 24 槽 + mod=3 的 `C6 C7 CC CD CE D2 D3 E9 EA EB EC ED`）、x87 251、`FF /3`/`/5` mod=3 共 16、`62`(m32) mod=3 64 —— 六类判据见上文「`C_BAD` 的来源」第 8-13 条，全部只在 `src/xde.c` 里按 `(opcode, mod, reg, rm)` 置位（`0F 00`/`0F 01` 在表里是**整行** `XA_UNDEF`，直接加 `XA_BAD` 会误伤合法的 `/reg`）。每条都按「先写断言看到失败 → 改 → 通过 → mutation 回退即失败」走，mutation 失败数：类1（`reg >= 6` → `>= 9`）**8**；类2 禁用 `0F 01` 内存 `/5` 块 **4**；类3 禁用 mod=3 保留子槽块 **12**；类4 禁用 `FF /3`/`/5` 块 **5**；类5（`(mpeek>>6)==3` → `==7`）**4**；类6（`0xD8-0xDF` → `0xE0-0xEF`）**35 且无误伤**。对照（不得误伤）：`0F 00 /0-/5` 与内存形式、`0F 01 /7` INVLPG、`0F 01 /4` SMSW 内存形式、`serialize`/`rdpkru`/`wrpkru`、`enclv`/`pconfig`/`encls`/`enclu`/`vmrun`/`invlpga`/`swapgs`/`smsw eax`、`FF /2`/`/4` 的 mod=3 与 `/3`/`/5` 的内存形式、32 位 EVEX `vaddps`。
  **两条告诫**：① x87 那 155 个 mod=3 槽里有 **56 个**只在次级来源（sandpile.org / coder32 / handwiki）有别名（`FSTP1`/`FCOM2`/`FCOMP3`/`FXCH4`/`FCOMP5`/`FXCH7`/`FSTP8`/`FSTP9` 之类），本轮的标记口径是「SDM 保留 + binutils 判 bad」；若改按「硬件可执行」口径，标记集会降到 **195**（99 + 96）——这是**主线程的裁决点**，当前保留标记。② 整个普查的依据是 SDM 的「reserved and must not be used」措辞，**不是** `#UD`：x87 逃逸页只在 LOCK 前缀下列 `#UD`，所以这些槽的准确含义是「保留、不该出现」，而非「硬件保证异常」。
- **`0F 01` 的 mod=3 特殊组（未回填；52 槽，Intel 定义 40，其中 12 个保留子槽**本轮已标 `C_BAD`**，见「`C_BAD` 的来源」第 10 条）——必须按 `(reg, rm)` 分发，不能按单个 `/reg`**（`INVLPG` 的 mod=3 是 #UD，该槽被 SWAPGS 占用）：`reg0`：`C0` ENCLV / `C1` VMCALL / `C2` VMLAUNCH / `C3` VMRESUME / `C4` VMXOFF / `C5` PCONFIG；`reg1`：`C8` MONITOR（读 EAX/ECX/EDX）/ `C9` MWAIT（读 EAX/ECX）/ `CA` CLAC / `CB` STAC（写 `FL.AC`）/ `CF` ENCLS；`reg2`：`D0` XGETBV（读 ECX、写 EDX:EAX）/ `D1` XSETBV（读 ECX + EDX:EAX）/ `D4` VMFUNC / `D5` XEND / `D6` XTEST（写 ZF）/ `D7` ENCLU；`reg3`：`D8-DF` AMD SVM（VMRUN/VMMCALL/VMLOAD/VMSAVE/STGI/CLGI/SKINIT/INVLPGA，Intel 保留）；`reg4`：`E0-E7` SMSW r32/r64（合法，**本轮已回填**）；`reg5`：`E8` SERIALIZE / `EE` RDPKRU / `EF` WRPKRU；`reg6`：`F0-F7` LMSW（合法，**本轮已回填**）；`reg7`：`F8` SWAPGS / `F9` RDTSCP（写 EDX:EAX + ECX）/ `FA` MONITORX / `FB` MWAITX（AMD）/ `FC-FF` AMD（CLZERO/RDPRU/INVLPGB/TLBSYNC）。
- **其它待建模项（实测与读码混合）**：`0F 30` WRMSR 读 `ECX` + `EDX:EAX`、`0F 32` RDMSR 读 `ECX`、写 `EDX:EAX`；`0F A2` CPUID 的 `ECX` 子叶选择输入**本轮已补**（实测现为 `src = EAX | ECX`；SDM 的输入列 EAX 并注明「in some cases, ECX as well」；`src/xde.c:292-294`、断言 `tests/xde_test.c:913-914`）；`C7 F8` XBEGIN（`src = []`、`dst = [RAX]`）；`C6 F8` XABORT **本轮已修**（`src = []`、`dst = [EAX]`——SDM 把 imm8 写进 **EAX[31:24]**，所以 `dst` 是全宽 `EAX`，ModR/M 字节 `F8` 本身不是操作数；`C6`/`C7` 组的 `MOV r/m8|v,imm8|v` 行为不变；`src/xde.c:272-282`、断言 `tests/xde_test.c:1728-1734`）；`FF /2 /3 /4 /5` 共 112 槽（near/far `CALL` 与 `JMP` 的栈/`RIP`/`CS`；其中 `/3`/`/5` 的 mod=3 **16 槽本轮已标 `C_BAD`**，见第 11 条，栈/`RIP` 的建模仍未做）；`62`(BOUND) 两个操作数**都只读**（mod=3 的 64 槽**本轮已标 `C_BAD`**，见第 12 条）；x87 `D8-DF` 的 **1797** 槽（其中 251 个空白槽**本轮已标 `C_BAD`**，见第 13 条）；legacy 的 19 个编码（`RET`/`RETF`/`IRET`/`INT n`/`INTO`/`CALL rel`/`CALLF`/`JMPF`/`FWAIT`/`ICEBP`）。两条**未建模的隐式项**：SGDT/SIDT 读 GDTR/IDTR 未记进 `src`（SDM 只列一个写操作数）、`LTR` 置 TSS 描述符 busy 位的隐式内存 RMW 未建模。
- **EVEX 的 `aaa`/`z`/`b` 无可用位，但位账已经算清（读码核实）**：word0 空闲 **49-63**（15 位）；word1 已用 0-15（r16-r31）与 16-23（r8b-r15b）⇒ 空闲 **24-63**（40 位）；`XSET2_ALL = 0xFFFFFF` 只覆盖 0-23，所以新位取 **≥24 不会**触发 `(set2 & XSET2_ALL) == XSET2_ALL` 的 undef 哨兵；1.02 兼容面只在 word0 低 32 位 ⇒ 取 **word1 位 24-30 命名 `k1`-`k7`，与 1.02 零冲突**。代价：要改 `include/xde.h` 加宏 + `src/xde_text.c`（`xde_sprintset2` 目前**静默丢弃** ≥24 的位，没有打印分支）。SDM 语义：`aaa ≠ 0` 时 k1-k7 是**隐式读**，`z = 0`（merging）让目的变读-改-写，store 形式无 zeroing 分支；方向已由 `OTHER` 覆盖 ⇒ 现状**继续折 `OTHER`**。
- **`0F C7 /6`/`/7` 的 mod≠3 与 `0F AE` 的只读形式**：`mem_store`（`src/xde.c:546-553`）列 `0F AE /0 /3 /4 /6` 与 `0F C7 /1 /3 /4 /5 /7`——`VMPTRST /7` 此后那批已改成**写内存**（`dst` 补 `MEM`），`VMPTRLD /6` 实测只读、仍走只读路径（`:540-545` 的注释把两者分开）。**`F3 0F AE /4`（PTWRITE）最新一轮已从该名单排除**（`(reg == 4 && !ptwrite)`，`:549`）——它只读、不写内存；`F3 0F AE /6` mod≠3（CLRSSBSY）仍由 `reg == 6` 覆盖，它确实写 m64。
- **`0F 18 /0-/3` 与 `0F 0D /0-/1` 的 mod=3 是保留形式，不是 NOP**：SDM 里 PREFETCHh/PREFETCHW/PREFETCHWT1 只定义 `m8`（Mod≠11），所以 `{0F,18,C0}`、`{0F,0D,C0}` 这类 mod=11 编码**不并入** `nop_ea`（`:533-535`），现状走通用路径报 `OTHER|OTHER`（`bad=0`），且**有意不加断言**——正确集合未定义。（`0F 1E`（无 `F3`）的 NOP Ev 此后那批已并入 `nop1e`。）
- **未定义编码只记录、不建模**：`0F 0D /2-/7` mod≠3、`F3 0F 1E /1..6`、`F3 0F 1E /0` 的 mod≠3、`F2 0F 1E C0`——UD/未定义编码，无可辩驳的目标语义 → 不写断言（`F3 0F AE /4` 的 mod=3 形式**最新一轮已按 PTWRITE 建模**，不再算「未定义」，见上条；`F3 0F AE /4` 因 `F3` 豁免而无 `C_BAD`；`0F AE` mod=3 的其余非法子形式此后那批已加 `C_BAD`，见 `src/xde.c:1364-1371`）。
- **`0F C7` / `0F AE` 这一族只剩 `XBEGIN` 的隐式项未建模**（`XABORT` 本轮已建模：`dst = EAX`；更广的隐式面见下「其它待建模项」条：`WRMSR`/`RDMSR` 同样未建模，`CPUID` 本轮已补 `ECX`）：RDRAND/RDSEED 的 CF、CMPXCHG8B/16B 的 `EDX:EAX` + ZF、XSAVE 族/XRSTOR(S) 的 `EDX:EAX` 已在**上一轮批 A** 补齐（`cmpxchg8b`/`xsave_mask` 两个谓词，`:562-563`/`:569-574` 定义、`:738-745` 使用）；`C7 F8`（XBEGIN）/`C6 F8`（XABORT）的隐式项仍未建模——这两条只钉了显式 r/m 与 `MEM`，（SDM 的 `C6 F8` 把 imm8 写进 `EAX[31:24]` 这点本轮已按 `dst = EAX` 建模，见「其它待建模项」条）。PTWRITE 的 r/m 源（`F3 0F AE /4`）、WAITPKG 的 `EDX:EAX`/`FL`（`F2`/`66 0F AE /6`）与 CLRSSBSY 的 `FL`（`F3 0F AE /6` mod≠3）已在**最新一轮批 A** 建模（见下条）。
- **PTWRITE / WAITPKG / CLRSSBSY 与 `F3 0F AE` 的 `/4`、`/6`、`/7`（上一轮只核实、最新一轮已建模）**：上一轮原计划把 `F3 0F AE` 的 `/4`、`/6`、`/7` 一并标 `C_BAD`，经 SDM + objdump 核实三条**都有定义** —— `/4` = **PTWRITE**（r/m 是**只读 GPR 源**或只读内存，**无目的操作数**）、`/6` mod=11B = **UMONITOR**（WAITPKG，r/m 只读 GPR）、`/7` = **SFENCE**（`F3` 冗余）—— 故当时**未改**（加 `C_BAD` 会错）。**最新一轮批 A 已把这几条建模**：`ptwrite`（`:506-507`）加进 `reg_not_dst`（`:555-556`）、并从 `mem_store`（`:549` 的 `(reg == 4 && !ptwrite)`）与 `xsave_mask`（`:572`）**排除**——它的 `mod == 3` 形式不另设分支、直接走通用 r/m 读；`umonitor`/`umwait`/`tpause` 折成 `waitpkg`（`:491-498`），`fence` 判据加 `&& !waitpkg`（`:499-500`）；`F2`/`66 0F AE /6` 读 `EDX:EAX`、写 `FL`（`:750-753`）；`F3 0F AE /6` mod≠3 = **CLRSSBSY**（`:512-513`，m64 读且写、`dst` 补 `FL` `:756-757`、`xsave_mask` 用 `!clrssbsy` 排除 `:573`）。
- **`0F B2`/`B4`/`B5`（LSS/LFS/LGS）的表侧 `XA_BAD` 假阳性已在本轮修掉**：生成器 `tools/gen_tables.py:351/353/354` 从 `XA_MODRM|XA_BAD`（`0x00000201`）改成只有 `XA_MODRM`（`0x1`），重跑后 `src/xdetbl.c:63`（0F map 的 `B0-B7` 行）为 `0x00000081, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001`。这三个 opcode 在 16/32/64 位都合法，解码侧本就按正常载入处理（`simd_0f` 名单与 dst 白名单都含它们），所以表侧随即与解码侧一致；三条 `C_BAD` 未置位断言钉住（`tests/xde_test.c:2166-2168`）。
- **`src/xdetbl.h` 的 `XA_*`/`XG_*` 已有自动校验**（`check_header()`，`tools/gen_tables.py:593-638`，调用 `:677`）：`gen_tables.py` 顶部仍镜像一份常量，但生成前会解析 `xdetbl.h`，逐名比对 `XA_*`（含 `XA_IMM_*`、`XA_GRP_MASK`、两个 shift）、`enum xde_group_id` 的逐个枚举值（按位置，含 `XG_NONE`）与 `#define XDE_MAP_COUNT`（对 `len(MAPS)`），不一致就列出差异并 `SystemExit(1)`，**在写文件之前**退出，所以两边不会再静默漂移。改常量仍要动两处，只是现在忘一边会被拒绝而不是产出错误解释。
- **生成器无漂移（已实测）**：`python tools/gen_tables.py` 在本轮表改动后**逐字节重现** `src/xdetbl.c`（40140 字节、md5 `3fc495d3a3ce7180179bd5e8ddd261f0`、退出码 0），且**幂等**（连跑两次结果相同）；脚本只写 `.c`，`src/xdetbl.h` 由 `check_header()` 只校验不写；`OUT = <repo>/src/xdetbl.c`（`tools/gen_tables.py:9`），无 argv、无 `--check`；`src/xdetbl.c:1` 的标语**不含哈希** ⇒ 表有没有漂移只能靠重跑比对，不能靠读首行标语。**本轮的表改动正是「大小不变」的反例**：24 行被改（24 插 24 删），总行数 414 与字节数 40140 都纹丝不动，只有属性位翻了 ⇒ 「行数/大小未变」**不等于**表未变。
- **表侧 `XA_BAD` 已按 2.00 口径清理为 28 槽（本轮，用户决策）**：口径 = `XA_BAD` 表示**该编码在任何模式下都不可用**，模式受限的形式改由 `XA_I64`/`XA_O64`/`XA_F64`/`XA_D64` 表达（详见下条）。据此删掉 60 槽：**35 槽三模式全合法的 legacy**——`6C-6F`（INS/OUTS）、`70/71/7A/7B`（JO/JNO/JP/JNP）、`8C/8E`（MOV sreg）、`9C/9D`（PUSHF/POPF）、`9E/9F`（SAHF/LAHF）、`AD/AF`（LODSD/SCASD）、`CA/CB/CC/CF`（RETF imm16/RETF/INT3/IRET）、`D7`（XLAT）、`E0/E1`（LOOPE/LOOPNE）、`E4-E7`/`EC-EF`（IN/OUT）、`F4`（HLT）、`F5`（CMC）、`FA/FB`（CLI/STI）；**22 槽带 `XA_I64` 的 legacy**——`06/07/0E/16/17/1E/1F`、`27/2F/37/3F`、`60/61`、`62`、`82`、`9A`、`C4/C5`、`CE`、`D4/D5`、`EA`（16/32 位合法、64 位非法，模式限制已由 `XA_I64` 表达）；以及 `m1` 的 `0F B2`/`B4`/`B5`（见上条）。余 **28 槽**：`m0[0xD6]`（SALC，`XA_BAD|XA_OPSZ8`）与 `m0[0xF1]`（INT1/ICEBP，`XA_BAD|XA_UNDEF`）——这两个**任何模式下都未文档化**、不可依赖 ⇒ 按「任何模式都不可用」保留；另 26 槽全是 group 槽（组内非法 `/reg`，如 `0xFF /7`、`0F B9`=UD1），全部正确。旧 88 槽版**内部自相矛盾即铁证**：`AC/AE` 未标 vs `AD/AF` 标、`E2/E3` 未标 vs `E0/E1` 标、`CD` 未标 vs `CC` 标、`F8/F9/FC/FD`（CLC/STC/CLD/STD）未标 vs `F5/F4/FA/FB` 标 ⇒ 该位当时**不表达合法性**。**三条审计纠正**（本轮复核旧记录时改正）：① legacy 槽按实测分群是 **35 个全合法 + 22 个 `XA_I64` + 2 个保留（`D6`/`F1`）= 59**，旧记录写成「34 + 25」有误；② 错分的是 `CA`/`CB`/`CF`（RETF imm16/RETF/IRET）——它们**不带 `XA_I64`**、在 16/32/64 位都合法 ⇒ 归入「全合法」那 35 槽；③ `C4`/`C5`（LES/LDS）与 `62`（BOUND）在 64 位会被 VEX/EVEX 前缀路径遮蔽（形状完整即按前缀解析），其 `XA_I64` 拒绝只在**未构成合法前缀的字节形状**（如 `62 00`）上可达 ⇒ 这三者的合法性**只有 16/32 位可断言**（`tests/xde_test.c:2220-2222` 只断 32 位）。**方法论**：`expect_fail` **无法**钉 `XA_BAD`——`decode == 0` 只可能来自 `XA_INVALID`/`XA_I64`/`XA_O64`/截断（`src/xde.c:1309-1314`），所以「删掉某槽 `XA_BAD`」这类回归只能靠 `expect_flag(…, C_BAD, …)` 钉住（本轮新增的 19 条 64 位 `expect_fail` 钉的是 `XA_I64` 拒绝语义，与删 `XA_BAD` 无关）。
- **`C_BAD` 的语义（已决策，不再是未决项）**：**2.00 口径 = 「非法 / 不可用的编码形式」**，即该编码在任何模式下都不成立；1.02 头注释那句 `#define C_BAD 0x00000800 /* "bad", i.e. rarely used instruction */`（`xde102/xde.h:33`）里的「罕见指令」读法**已废弃**，只作为 1.02 历史留档。据此 `XA_BAD` 只保留在「该形式在任何模式下都非法」处（本轮清理后余 28 槽，见上条）；`push es`/`aaa`/`int3`/`hlt` 这类**只是 64 位非法**（改由 `XA_I64` 表达）或**三模式全合法**（不标任何位）的形式一律去掉该位，解码侧消费口径（`src/xde.c:130` 的 `if (attr & XA_BAD) f |= C_BAD;`）无需改动。旧断言审计：全文件搜 `C_BAD=1`（当时 10 条，此后两轮增至 **24 条**；最新一轮的六条新增断言全是 `C_BAD` **未置位**，置位数不变）后逐条判定，**没有一条钉的是合法形式**，故未删任何旧断言；重判保留的三组（`8F /1`、`0F AE /0-/4` mod=3、`C7 FA`/`C6 FA`）均正确。
- **`/reg` 级对象集的位账**：`XA_*` 的 21 个 flag 位**已满**；bit24 名义空闲但落在 `XA_IMM_MASK`（`0xF<<21`）的码域内，当 flag 会让 `imm_bytes` 的 kind ≥ 8 → 立即数长度静默变 0；group 码 30-127 空闲，但只能按 **opcode** 表达、无法表达 `/reg`。**最省的 `/reg` 通路 = 新增第二张同形 `xde_group` 表**（30×8，`uint32_t` = 960 B），在 `src/xde.c` 的二次查表处 OR 进去，不动 `attr` 宽度与 `apply_attr_flags` 签名。另：`tests/xde_test.c` 对 `xde_group`/`XA_GRP`/`XG_` **零直接覆盖**（任何组级新表必须自带断言）。

- **`asm_size()` 是写出序列的第二份文本实现：13 项同步清单 + 容量不变量零违例（本轮批 B，只读扫描、未改仓库文件）**：`asm_size()`（`src/xde.c:1546-1572`）**不是**从 `xde_asm_buf` 的落地循环推导出来的，而是同一套发射规则的**独立复刻**——两者唯一的共享「接缝」是 `xde_asm_buf` 先算好**夹取后**的 `nvex`/`naddr`/`ndata` 并按值传给 `asm_size()`（`:1585-1589`），而夹取（`nvex ≤ 4` / `naddr ≤ 8` / `ndata ≤ 8`）与 `max_len` 归一（0 或 >15 → 15）**只存在于 `xde_asm_buf`**（`:1585-1587`/`:1581-1582`），`asm_size()` 无条件信任入参。必须同步的 **13 项**：① `p_lock`、② `p_rep`、③ `p_seg`、④ `p_66`、⑤ `p_67`、⑥ `nvex > 0 → nvex + 1`（该分支**不含** `rex`/`opcode2`/`opcode3`）、⑦ `else` 分支的 `rex`、⑧ `opcode`、⑨ `opcode == 0x0F → opcode2`、⑩ `opcode2 ∈ {0x38, 0x3A} → opcode3`、⑪ `flag & C_MODRM → modrm`、⑫ `flag & C_SIB → sib`、⑬ `naddr`/`ndata` 两项（唯一「传参而非自推导」的项）；另需同步的常量是夹取上限 4/8/8 与 `max_len` 归一（二者只在 `xde_asm_buf`）。
  **实测（本轮批 B）**：**170,675,981** 例解码（mode16 64,665,149 / mode32 64,665,149 / mode64 41,345,683；候选 175,270,944，解码成功率 97.4%）中 `asm_size() == 实际写出字节数` **零违例**，同时 `S > 15` = 0、溢出写 = 0、`ret == 0` 却写入 = 0、`K == 0` = 0。**容量夹取**：13 个病态结构（`addrsize`/`datasize`/`nvex` = 255/200；`flag |= C_MODRM|C_SIB`）× 11 个 `max_len`（含 0、16、17、100、`0xFFFFFFFF`）＋ 2 次显式调用，共 **169 次**，canary（`0xA5`）与 `VirtualAlloc` `PAGE_NOACCESS` 保护页 + 向量化异常处理器 + `longjmp` 验证**零违例**；夹取实测生效（raw 255/255/255 → 按 8/8/4 计，`K` 最大 28）。**4 个变异全部被捕获**：m1 删 `asm_size()` 的 `C_SIB` 项 → `S != K` **987,834**；m2 写循环改用未夹取计数 → **14 例**容量违例且**全部命中保护页**；m3 删 `p_seg` 项 → **185,016**；m4 容量判定 `>` → `>=` → **240** 例「装得下却报 0」。最小复现各一：m1 mode64 `00 04 00` → `K=3 S=2`；m4 `62 00 A5 …` → `len=15 S=15 K=0`。
  **维护风险**：① `if (nvex)` 分支与 legacy `0F` 逃逸计数必须保持互斥；② 将来新增任何一个发射字节，要同时改写出循环、`asm_size()`、夹取入参三处；③ 三个计数是「受信任参数」——第二个调用者若直接传原始字段只会偏大（保守返 0），但若同时把写循环也改成原始计数，就是 m2 型越界。**附带实测（非缺陷）**：`K != d.len` 共 **85,428** 例（0.050%，全部 `K < len`）——79,480 例带 `C_BAD`（如 `26 26 00 00`）、6,748 例不带（如 `66 C5 00 00 00`，`len=5 K=4`、`nvex=2`），即文档已写的**规范化**类（冗余遗留前缀不再发射 / VEX 路径不发 REX）；这些例中 `S == K` 恒成立。**语义陷阱补记**：`max_len == 0` 被当 15（已记），`max_len == 16` 也被夹到 15（`xde_asm_buf` 同构判定）。
- **未 triage 的 objdump↔xde 分歧（本轮普查的副产物）**：普查本身发现的分歧**多于**这 431 槽——前后两次字节计数的差值是 **296**，与本轮判据在这些扫描集里命中的槽数一致 ⇒ 本轮改动**没有引入新分歧**（差值就是被修掉的那些）。**`LEA 8D` 的 mod=3（每个模式 64 槽；SDM 要求内存操作数）是明确的下一候选**，本轮**未**处理；其余大多是普查伪影（缓冲区缺必需 `66`/`F3` 前缀）、`0F 13`/`17`/`2B`/`50` 的 mod=3、`0F 0F`、`0F 38`/`0F 3A`、`C4`/`C5`/`D5` 的截断引导字节、以及 64 位下的裸前缀字节。要按本轮的流程推进：先确定 SDM 口径（reserved vs `#UD`）再决定是否标 `C_BAD`。

## Documentation Map

| 文档 | 内容 |
|------|------|
| `README.md`（110 行） | 唯一面向使用者的文档：`## What it does`（`:11`）、`## Layout`（`:23`）、`## Build (MSVC)`（`:36`，含生成器一致性校验那一句）、`## API`（`:57`，示例 `:60-67`，含 `xde_asm_buf`）、`## Notes`（`:85`，编码规则）。**无许可证段落、无外部链接。** |
| `AGENTS.md`（本文件） | 面向 AI 助手 / 新维护者的完整工程参考 |
| `xde102/xde.txt`（242 行） | 1.02 设计文档：版本历史（`:8-9`）、对象集设计原则与「不区分内存地址」的理由（`:16-40`）、被否决的 "Permutation conditions" 方案（`:42-44`）、API 与结构体清单（`:46-140`）、REP INSB 示例及作者承认的 bug（`:111-117`）、结尾是 Mistfall 项目的 `AnalyzeRegs` 用法示例（`:142-242`）。是理解 2.00 设计取舍的最佳背景读物。 |
| `xde102/todo`（11 行） | 1.02 时代的遗留问题清单，见 Known Gaps 中「`xde102/todo` 的 4 条现已全部处理」 |
| `LICENSE`（21 行） | MIT |
