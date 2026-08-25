# 02 指令集设计（ISA）

> 本文档是 vdot 扩展的**唯一权威规格**。微架构、软件栈、验证文档均引用本文；发现不一致以本文为准并同步修订。
> 目标平台：香山昆明湖 V2（Kunminghu V2，RVV 1.0，VLEN=128）。

---

## 1. 设计原则

1. **RVV 1.0 语义一致**：vdot 指令必须完整继承 RVV 的 `vl`/`vstart`/掩码/尾元素/异常语义，不能发明新的执行模型。这保证：与 `vsetvl` 无缝协作、difftest/golden model 语义唯一、编译器向量化器可安全处理。
2. **与官方方向对齐**：RISC-V 官方 fast-track 提案 [Zvdot4a8i（4×INT8 点积 → INT32 累加）](https://riscv.atlassian.net/wiki/spaces/PSXX/pages/766672912/Dot-Product+Zvdot4a8i+Zvqdotq+Ratification+Plan)正在进行 Ratification；[LLVM 已合入其支持](https://github.com/llvm/llvm-project/pull/184089)。本设计**语义与助记符完全对齐 Zvdot4a8i**，作为自定义扩展 `Xkhmvdot` 先行实现，官方扩展正式发布后仅需改扩展名即可平滑迁移。
3. **最小侵入**：编码落在 RVV 的 OP-V 主操作码（`1010111`）未占用空间，不触碰昆明湖已实现的 RVV 1.0 指令编码；解码/调度/写回路径的改动局限在向量后端（详见 03）。
4. **可验证**：每条指令的语义都有可执行的伪代码定义与参考实现（NEMU/Spike/软件仿真，见 04/05），支撑差分随机测试。

## 2. 扩展命名与使能

| 项目 | 值 | 说明 |
| --- | --- | --- |
| 扩展名 | `Xkhmvdot` | X 前缀 = 自定义扩展；khm = Kunminghu；vdot = vector dot product |
| 指令助记符 | `vdota4` / `vdota4u` / `vdota4su` / `vdota4us` | 与 Zvdot4a8i 一致 |
| 使能方式 | v1.0 恒使能（无独立 enable CSR） | 昆明湖为 SoC 集成核，自定义扩展默认开启；后续如需可增加 `misa` 之外的扩展使能 CSR（见 §7） |
| 对应官方扩展 | `Zvdot4a8i`（Ratification 进行中） | 发布后 `Xkhmvdot` 可视为其先行实现 |

## 3. 指令形态总览

8 条指令（4 种符号性 × 2 种操作数形式）：

| 指令 | 操作数 | vs2 元素 | vs1 元素 | vd 累加类型 | 语义 |
| --- | --- | --- | --- | --- | --- |
| `vdota4.vv vd, vs2, vs1` | vv | int8×4 | int8×4 | int32 | 有符号×有符号 |
| `vdota4u.vv vd, vs2, vs1` | vv | uint8×4 | uint8×4 | uint32 | 无符号×无符号 |
| `vdota4su.vv vd, vs2, vs1` | vv | int8×4 | uint8×4 | int32 | 有符号×无符号 |
| `vdota4us.vv vd, vs2, vs1` | vv | uint8×4 | int8×4 | int32 | 无符号×有符号 |
| `vdota4.vx vd, vs2, rs1` | vx | int8×4 | rs1 低位 32 位含 4 个 int8 | int32 | 同上（vs1 为标量广播） |
| `vdota4u.vx vd, vs2, rs1` | vx | uint8×4 | rs1 低位 32 位含 4 个 uint8 | uint32 | 同上 |
| `vdota4su.vx vd, vs2, rs1` | vx | int8×4 | rs1 低位 32 位含 4 个 uint8 | int32 | 同上 |
| `vdota4us.vx vd, vs2, rs1` | vx | uint8×4 | rs1 低位 32 位含 4 个 int8 | int32 | 同上 |

> **说明**：`.vx` 形式将标量寄存器 rs1 视为"打包的 4 个 8 位元素"（与 Zvdot4a8i 的 vx 形式一致），用于权重/常量向量广播场景（如注意力 QKᵀ 中一侧为固定向量、推荐系统打分），避免频繁搬运向量寄存器。

## 4. 语义精确定义

### 4.1 数据视图（关键约定）

- 本族指令的所有向量操作数（`vd`、`vs2`、`vs1`）以 **SEW = 32** 的元素视图访问（`vsew` 必须为 32，见 §5.1 约束）。
- `vs2`/`vs1` 的每个 32 位元素内部被**重新解释为 4 个打包的 8 位子元素**（little-endian，子元素 j 位于位 [8j, 8j+7]，j = 0..3，与 RVV 整数小端约定一致）。
- 因此本指令的**元素计数（vl/vstart）单位是 32 位元素**，与普通 SEW=32 向量指令完全一致，**没有 vl%4 之类的对齐约束**。

### 4.2 伪代码（权威定义）

`vdota4.vv vd, vs2, vs1`（其余符号性变体仅替换扩展方式）：

```
# 输入: vsew=32, vl, vstart, vm, vd 旧值, vs2, vs1
# 输出: vd 新值
SEW = 32
VLMAX = LMUL * VLEN / SEW          # LMUL 由 vsetvl 决定
for i in [vstart, vl):
    if vm == 0 and mask[i] == 0:   # 掩码粒度 = 1 位/32 位元素（同普通 RVV 掩码指令）
        continue                    # 该元素不更新
    acc = SExt32(vd[i])             # 读 vd 旧值，按 32 位有符号扩展（vdota4u 为无符号）
    for j in 0..3:
        a = Sub8(vs2[i], j)         # vs2[i] 的 [8j, 8j+7] 位，符号性依变体
        b = Sub8(vs1[i], j)         # vs1[i] 的 [8j, 8j+7] 位，符号性依变体
        acc = acc + a * b           # 乘积为 16 位有符号，累加为 32 位，模 2^32 回绕（wrapping）
    vd[i] = acc                     # 写回，32 位
# i >= vl 的元素（尾元素）: 按 vta（tail agnostic/undisturbed）处理，见 §4.3
# vstart: 与昆明湖所有向量算术指令一致，vstart≠0 在解码侧报 illegal instruction
#         （RVV 规范允许仅支持 vstart=0 的实现；vsetvl 保证正常执行时 vstart=0，见 §4.4）
```

各变体的符号性矩阵：

| 变体 | vs2 子元素解释 | vs1 子元素解释 | 累加器解释 |
| --- | --- | --- | --- |
| `vdota4` | signed int8 | signed int8 | signed int32（回绕） |
| `vdota4u` | unsigned uint8 | unsigned uint8 | unsigned uint32（回绕） |
| `vdota4su` | signed int8 | unsigned uint8 | signed int32（回绕） |
| `vdota4us` | unsigned uint8 | signed int8 | signed int32（回绕） |

### 4.3 溢出与饱和策略

- **指令内**：单元素一次点积的中间结果为 4 个 int8×int8 乘积之和，范围 ⊆ [−4×128×128, +4×127×127] = [−65536, +64516]，**16 位有符号可完整表示，不会溢出**。
- **跨指令累加**：`vd` 的旧值与新点积相加采用 **32 位模 2^32 回绕**（wrapping），与 RVV `vadd.vv` 的整型回绕语义一致，**不做饱和**。理由：(1) 与 RVV 整型算术一致，硬件/golden 实现简单；(2) INT8 GEMM 的 K 维累加（如 K=4096，累加范围 ±4096×65536≈2^28）在 int32 内天然安全，饱和无收益；(3) 若未来需要饱和变体（如 QKᵀ 打分饱和），作为独立指令扩展，不在本版范围。

### 4.4 vstart / 掩码 / 尾元素

| 机制 | 定义 |
| --- | --- |
| `vstart` | **v1.0 采用与昆明湖一致的策略：`vstart≠0` 的向量算术指令在解码侧判 illegal instruction**（RVV 1.0 规范允许实现仅支持 vstart=0 并对此报非法；昆明湖对所有向量算术指令如此实现：`VecExceptionGen.scala` 第 275 行 `vstartIllegal = isVArith && (io.vstart =/= 0.U)`，执行侧 `HasPipelineReg` 兜底）。正常执行时 `vsetvl` 将 vstart 清零，无需软件干预；异步异常重放路径由硬件/软件按 RVV 规范复位 vstart 后整条重执行。**vdot 无需新增任何 vstart 机制，与 vadd/vwmacc 行为完全一致**。 |
| 掩码 `vm` | `vm==1`：无掩码；`vm==0`：读掩码寄存器（EEW=1，元素数 = vl），掩码位 i 对应 32 位结果元素 i。掩码为 0 的元素**不读源、不计算、不写 vd**（与 RVV masked-off 行为一致；硬件由执行单元内 `Mgu` 统一处理）。 |
| 尾元素 `vta` | `vta==1`（agnostic）：v1.0 实现按 undisturbed 处理（写回旧值），与 RVV 的 agnostic 允许范围一致；`vta==0`（undisturbed）：写回 vd 旧值。由 `Mgu` 统一处理。 |

### 4.5 与 RVV 1.0 的交互约束（违反即 illegal instruction）

| # | 约束 | 异常类型 |
| --- | --- | --- |
| C1 | `vsew` 必须为 32（即 `vsetvl` 设置的 SEW=32） | illegal instruction |
| C2 | `vlmul` 使得 VLMAX ≥ 1 且 vd/vs2/vs1 寄存器组不越界（LMUL ∈ {1,2,4,8}，昆明湖 RVV 1.0 支持范围） | illegal instruction |
| C3 | 向量扩展已使能（`mstatus.VS != Off`；昆明湖 RVV 1.0 要求） | illegal instruction（同 RVV 规则） |
| C4 | 指令编码落入本扩展已定义空间但 funct3/funct6 组合未定义 | illegal instruction（见 §5） |

除上述外，本指令**不产生任何访存、无副作用 CSR 更新、无特权级限制**，任何模式下可执行（与 RVV 算术指令一致）。

## 5. 编码

### 5.1 编码模板（RVV OP-V 空间）

本族指令使用 RVV 主操作码 **OP-V（`1010111`，0x57）**，编码模板：

```
 31        26 25 24     20 19     15 14    12 11      7 6       0
┌───────────┬──┬─────────┬─────────┬────────┬─────────┬─────────┐
│  funct6   │vm│  vs2    │  vs1/rs1│ funct3 │   vd    │ 1010111 │
└───────────┴──┴─────────┴─────────┴────────┴─────────┴─────────┘
```

- 位 [31:26] `funct6`：指令变体（vdota4/vdota4u/vdota4su/vdota4us）
- 位 [25] `vm`：掩码位（0=掩码，1=无掩码）
- 位 [24:20] `vs2`：向量源 2
- 位 [19:15] `vs1`（vv 形式）或 `rs1`（vx 形式）
- 位 [14:12] `funct3`：操作数类别（**vv = 010 OPMVV，vx = 110 OPMVX**）
- 位 [11:7] `vd`：向量目的（同时为累加器读源）

> **funct3 选型依据（重要）**：RVV 1.0 中"乘累加/宽乘累加"指令族（`vmacc.vv`、`vwmacc.vv`、`vwmaccu.vv` 等）统一使用 funct3=`010`（vv 形式）与 funct3=`110`（vx 形式），例如 `VWMACCU_VV = b111100…010…1010111`、`VWMACCU_VX = b111100…110…1010111`（见 rocket-chip `Instructions.scala`，昆明湖解码器直接引用）。vdot 在语义上是"打包 4 元素宽乘累加"，与 MAC 族同类，故采用 010/110 而非 000/100（后者为普通整型算术类别）。此选型与 RISC-V 官方 Zvdot4a8i 的编码方向一致（最终以官方定稿核对为准）。

### 5.2 编码定稿与预留（已与实现同步）

> ✅ **v1.0 实现已定稿并验证**（见 [08_任务完成情况CheckList](./08_任务完成情况CheckList.md)）：`vdot.vv` 采用 **funct6=`111001`，funct3=`010`，opcode=`1010111`**（`VDOT_VV = BitPat("b111001???????????010?????1010111")`），已脚本核对在 OP-V 空间无冲突（与 `VWMACCU_VV` 同格式；`111001` 在 funct3=010 与 110 两个空间均为空闲值，未来实现 `vdot.vx` 可沿用同一 funct6 + funct3=110）。
> ✅ **差分验证通过（2026-08-18）**：RTL 与 NEMU 差分固定用例 4/4 + 随机用例 24/24 全部 GOOD TRAP（08 §3.3），含 vsew=e32 强制、wrap-around 累加、oldVd 累加等语义均一致；并在真实 GEMM 负载（M=N=K=16）emu 实测通过，指令数较标量基线减少 **12.7×**、GEMM 净周期 ~8.2× 更快（05 §5.2）。
> 以下 8 条指令表：**已实现**条目为最终编码；其余为**预留**（v1.0 后续版本实现时启用，候选值同样经冲突核对，最终以官方 Zvdot4a8i 定稿为准）。

| 指令 | funct6 | funct3 | vm | 状态 |
| --- | --- | --- | --- | --- |
| `vdota4.vv`（vdot.vv） | `111001` (57) | `010`（OPMVV） | 25 | ✅ 已实现（yunsuan Vdot64b + XiangShan VdotU） |
| `vdota4.vx` | `111001` (57) | `110`（OPMVX） | 25 | ⬜ 预留（funct6 与 vv 共用，空间已核对空闲） |
| `vdota4u.vv` | `010100` (20) | `010` | 25 | ⬜ 预留 |
| `vdota4u.vx` | `010100` (20) | `110` | 25 | ⬜ 预留 |
| `vdota4su.vv` | `010101` (21) | `010` | 25 | ⬜ 预留 |
| `vdota4su.vx` | `010101` (21) | `110` | 25 | ⬜ 预留 |
| `vdota4us.vv` | `010110` (22) | `010` | 25 | ⬜ 预留 |
| `vdota4us.vx` | `010110` (22) | `110` | 25 | ⬜ 预留 |

> 预留 funct6 值（20–22）已核对在 funct3=010/110 空间未被 RVV 1.0 占用；实现时仍需与官方 Zvdot4a8i 编码比对，优先采用官方值。

### 5.3 编码核对记录（已完成）

> ✅ **核对已完成**（实现期，见 [08_任务完成情况CheckList](./08_任务完成情况CheckList.md) 决策记录）：funct6=111001 经脚本扫描 rocket-chip 全部 OP-V BitPat + 香山自建表，确认无冲突。剩余核对项（官方定稿比对、反汇编 round-trip）在官方 Zvdot4a8i 发布或工具链落地时执行：

1. 对照 **Zvdot4a8i 官方提案最新版**的正式编码，优先直接采用官方编码（若已冻结）——若官方编码与本设计不同，切换为官方值并记录冲突矩阵。
2. 已核对：`funct6=111001` 在 funct3=010/110 空间与 RVV 1.0 全表（rocket-chip `Instructions.scala`）无冲突；昆明湖已实现 Zvbb（funct3=000/100/011 空间）与 Zimop（MISC-OP 空间），不占用本选型。
3. 若与官方编码冲突：退化为自定义编码并记录冲突矩阵（官方编码 vs 本设计编码），在文档中公开。
4. 对每个已实现编码生成"反汇编 round-trip"测试（编码→反汇编→编码）确保工具链一致（LLVM 支持落地时执行，见 04）。

## 6. 指令行为汇总表（供验证组建测试矩阵）

| 场景 | 期望行为 |
| --- | --- |
| 正常 vv，vl=VLMAX | 全量计算，结果正确 |
| vl=0 | 无操作，vd 不变 |
| 掩码全 0 | vd 不变（不读源、不计算） |
| 掩码部分 1 | 仅掩码位=1 的元素更新 |
| vstart > 0 | **illegal instruction**（与昆明湖所有向量算术指令一致，见 §4.4） |
| 尾元素（i ∈ [vl, VLMAX)） | 按 vta 策略（undisturbed/agnostic） |
| 符号混合变体（su/us） | 按 §4.2 符号矩阵计算 |
| 溢出回绕 | 模 2^32 回绕（可构造边界用例） |
| SEW≠32（如 SEW=8/16/64） | illegal instruction |
| VS=Off（向量未使能） | illegal instruction |
| 未定义编码组合 | illegal instruction |
| vx 形式 | rs1 低位 32 位作为打包 4×8bit 广播 |

## 7. 演进路径与后续版本

| 版本 | 内容 | 触发条件 |
| --- | --- | --- |
| v1.0（本赛题交付） | 上述 8 条指令，恒使能 | — |
| v1.1 | 扩展使能 CSR（如自定义 CSR 置位才解码，便于虚拟化/多核选择性使能） | 有虚拟化/异构需求 |
| v2.0 | 若官方 Zvdot4a8i 发布，切换为官方编码与扩展名；增加饱和变体、8×8 分组变体、INT8×INT8→INT64 长累加（对齐 Zvqdotq） | 官方 Ratification 完成 |

## 8. 附：软件接口约定（草案）

- **汇编助记符**：`vdota4.vv vd, vs2, vs1`（8 条，见 §3 表）。
- **LLVM intrinsic（草案，对齐 riscv-rvv-intrinsic-doc 风格）**：
  ```c
  vint32m1_t __riscv_vdota4_vv_i32m1(vint32m1_t vd, vint32m1_t vs2, vint32m1_t vs1, size_t vl);
  vuint32m1_t __riscv_vdota4u_vv_u32m1(vuint32m1_t vd, vuint32m1_t vs2, vuint32m1_t vs1, size_t vl);
  // … 依 LMUL(m1/m2/m4)、掩码/尾策略展开（对齐 Zvdot4a8i 的 intrinsic 矩阵）
  ```
- **C 语言 fallback（无编译器支持时的起步实现）**：`#define` 宏 + 内联汇编，或调用 §4.2 伪代码的软件仿真函数，保证算法组先行开发。
- 详细工具链方案见 [04_软件栈与工具链设计](./04_软件栈与工具链设计.md)。
