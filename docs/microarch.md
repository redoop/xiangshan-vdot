# 03 微架构设计

> 本文档定义 vdot 在香山昆明湖 V2 向量后端中的硬件集成方案。指令语义以 [02_指令集设计](./02_指令集设计.md) 为准。
> 源码接入点基于 **`kunminghu-v2` 分支 commit `e12436c7cba86b195deec24981976d78bc263661`（2026-08-14）**深度调研（附录 A + [源码调研报告](../02_设计方案/07_附录_源码调研报告.md)），实现前须以实际 clone 复核行号。

---

## 1. 设计目标与总体思路

| 目标 | 约束 | 策略 |
| --- | --- | --- |
| 极小面积开销 | 新增面积 ≤ 全核 0.5%（估算口径见 §6） | 8×8 乘法阵列 + 加法树 + 32 位累加器，规模极小 |
| 数倍点积吞吐 | 1 条/周期，时延 2–3 周期 | 乘加阵列流水化，独立 FU 挂入向量 ExeBlock |
| 最小侵入 | 不动前端、访存、ROB 主路径 | 仅改：解码表 + FUType + 新执行单元 + 写回端口配置 |
| RVV 语义完整 | vl/vstart/掩码/尾/异常 | 复用向量后端既有机制（vstart 重放、尾元素处理、flush） |

总体位置（示意）：

```
               ┌───────────────────────── 后端 ─────────────────────────┐
  前端 ──► 译码/重命名/分发 ──► 整数/浮点/访存 ExeBlock …                │
                                │                                       │
                                ▼                                       │
                        ┌─ 向量 ExeBlock（VfuCluster）──────────┐        │
                        │  [VectorALU][VectorShift][VectorPermute]│       │
                        │  [VectorMul]  [VectorDiv]  [vdot 单元★] │ ← 新增 │
                        │  （共享 VPRF 读/写端口、writeback 端口）  │       │
                        └──────────────────────────────────────┘        │
                                              │                          │
                                              ▼                          │
                                        ROB 提交 ──► difftest ──► 写回状态  │
                        └─────────────────────────────────────────────────┘
```

## 2. 解码接入（基于源码的实际机制）

> **结构提示（kunminghu-v2 与老版本差异）**：向量执行单元的实际实现不在 `backend/vector/`（该目录下的 VIPU/VPerm/VIMacU/VPUSubModule 旧实现已整体注释），而在 **`backend/fu/wrapper/`**（VIAluFix/VIMacU/VIDiv/VIPU/VPPU/VFALU/VFMA/VFDivSqrt/VCVT/VSet）；公共框架在 `backend/fu/vector/`（VecPipedFuncUnit/VecNonPipedFuncUnit/Mgu/Mgtu/Bundles）。昆明湖**没有任何现成自定义指令钩子**（无 xvld/xvst/XCustom），vdot 必须走完整标准链路。

新增指令的接入遵循昆明湖既有 RVV 指令链路（以 `vwmaccu.vv` 为样板，每环均已定位）：

1. **编码 BitPat**：在 `backend/decode/Instructions.scala` 中按扩展对象风格登记 BitPat（**v1.0 已实现 `vdot.vv`，编码定稿 funct6=`111001`**，见 [02 §5.2] 与 [08_任务完成情况CheckList](../02_设计方案/08_任务完成情况CheckList.md)）：
   ```scala
   object Vdot { def VDOT_VV = BitPat("b111001???????????010?????1010111") }
   ```
2. **解码表映射**：`backend/decode/VecDecoder.scala` 加入映射，风格与既有条目一致（参考第 426 行 `VWMACCU_VV -> OPMVV(T, FuType.vimac, VimacType.vwmaccu, F, T, F, UopSplitType.VEC_VVW)`）：
   ```scala
   VDOTA4_VV -> OPMVV(T, FuType.vdot, VdotType.vdota4, F, T, F, UopSplitType.VEC_VVV),
   ```
   `UopSplitType.VEC_VVV`（1 uop/lmul）——vdot 每 128-bit 输出 4×32bit 结果，**不真正加宽**（见 §3.1），无需 `VEC_VVW` 的 2-uop 拆分（`DecodeUnitComp.scala:359` 现成）。
3. **fuOpType 编码**：新增 `VdotType`（yunsuan 子模块 `yunsuan/package.scala` 风格，9-bit OpType：vs2/vs1/vd 符号 + format + 子操作码），`VdotType.dummy` 保留；或扩展 `VimacType` 增加点积子操作码（见 §7 方案对比）。
4. **FuType**：`backend/fu/FuType.scala` 新增 `vdot`，并同步加入 `vecOPI`/`vecArith`/`vecAll` 集合（127–133 行）与 `FuConfig.isVecArith`/`needVecCtrl`/`needOg2`（162–190 行）——**漏改任一集合会导致 dispatch 找不到可接受 EXU（canAccept 全 false）**。
5. **illegal 检查**：`VecDecoder` 未命中项走 `DecodeUnit.scala:894-917` 的默认非法；向量专属检查在 `VecExceptionGen.scala`（EEW/EMUL/对齐/重叠/vstart 等，42–303 行）。**若 vdot 按"打包 4×8bit 于 32-bit 元素"语义（本设计，见 02 §4.1），需自行补充 EEW/EMUL/重叠检查逻辑**（其数据视图不同于标准 e8→e32 widening，不直接落入 `vdWideningInst` 清单，见 06 任务 2.3）。vstart≠0 的 illegal（`VecExceptionGen.scala:275`）与执行侧兜底（`FuncUnit.scala:253-258`）自动覆盖，无需新逻辑。
6. **调度/写回**：`VdotCfg` 挂入 **VFEX0**（`Parameters.scala:449`，与 VfmaCfg/VialuCfg/VimacCfg/VppuCfg 同 EXU），复用现有读端口（3×VfRD + V0RD + VlRD）与写端口（VfWB(0,0)）——**不新增 VfRD/VfWB 端口号**（否则 VPRF 端口数自动增大，面积/时序代价，见 §3.2）。

> 改动预估：Instructions.scala +8 BitPat；VecDecoder +8 映射；VdotType（yunsuan）+~30 行；FuType +1；FuConfig +1 配置；Parameters（VFEX0）+1；新执行单元 wrapper/VdotU.scala ~300–600 行 + yunsuan Vdot64b ~200–400 行；VecExceptionGen +10–40 行。合计硬件侧约 600–1200 行 Scala。

> ⚠️ **实现期关键教训（2026-08-18，已修复）**：依赖旧 vd 累加的指令（vmacc/vwmacc*/vdot）必须在 `DecodeUnit.scala` 的 **`vmaInsts`** 列表中登记，否则 `isDependOldVd=false` → IQ/dispatch（EntryBundles/NewDispatch）在 `ignoreTail/ignoreWhole` 时把 srcType(2) 置 `no`、**跳过 oldVd 读取**（src(2)=0），表现为"结果只算纯点积、不累加"。这是 vdot 差分 bug 的最终根因（详见 [08 §3.3](./08_任务完成情况CheckList.md)），**新增任何累加型向量指令都必须同步 `vmaInsts`**。

## 3. 执行单元设计（vdot 单元）

### 3.1 数据通路（VLEN=128，LMUL=1 情形）

每周期处理 **128 bit vs2 × 128 bit vs1**（16 个 int8 × 16 个 int8）→ 产生 **4 个 int32 部分积**（每 4 组点积），与 vd 旧值相加后写回：

```
vs2[127:0] = 16 × int8 ─┐
                        ├─► 16 个 8×8 乘法器 ──► 4 个 4:2 压缩/加法树 ──► 4 × int32 部分和
vs1[127:0] = 16 × int8 ─┘                                                  │
                                                       vd 旧值 (4 × int32) ─┤
                                                                           ▼
                                                              4 × int32 累加（回绕）
                                                                           │
                                                                           ▼
                                                              写回 vd（掩码/尾元素处理后）
```

- **乘法阵列**：16 个 8×8 有符号/无符号乘法器。符号性由指令变体（vdota4/u/su/us）控制输入符号扩展方式：4 个乘法器组共享同一符号模式，符号扩展在乘法器输入端以 2 选 1 实现。
- **加法树**：每组 4 个 16 位乘积 → 两级加法（或 4:2 压缩器 + 1 级加法），得到 17 位无溢出和（范围 [−65536, 64516]），再与 32 位累加器相加（回绕）。
- **累加器**：32 位回绕加法（与 RVV vadd 语义一致），支持"读 vd 旧值 + 加部分和"。
- **多周期流水**（建议 2–3 拍，与昆明湖既有向量乘法单元同构）：
  - 参考 `VIMacCfg.latency = CertainLatency(2)`（`FuConfig.scala:559`），对应 yunsuan `VIMac64b` 内部"3 段组合 + 2 级寄存器"结构（Booth 编码 + Wallace 树 → 寄存器 → 部分归约+加法 → 寄存器 → 舍入/饱和）。vdot 点积核心规模与 VIMac 相当，**建议 latency = 2**（若扩展 VIMac64b 增加点积模式则直接沿用其 2 级寄存器结构）；如新增独立核心且时序吃紧，可 3 拍（乘 → 加法树 → 累加+写回）。
  - 吞吐 1 条/周期（流水无结构冲突）；**latency 必须与运算核心内部寄存器级数严格一致**（`HasPipelineReg.latency`，`FuncUnit.scala:167`）。
- **掩码/尾/vstart**：
  - 掩码与尾元素：由执行单元内 **`Mgu`**（`backend/fu/vector/Mgu.scala:34`，mask/tail merge unit）统一处理——按 vstart/vl/eew/vma/vta 把计算结果与 oldVd 合并（active/agnostic/undisturbed），vdot 直接复用，无新逻辑（连接方式仿 `VIMacU.scala:132-150`）。
  - vstart：**vstart≠0 → illegal instruction**（解码侧 `VecExceptionGen.scala:275` + 执行侧 `FuncUnit.scala:253-258` 兜底，与昆明湖所有向量算术指令一致，见 02 §4.4），无新增机制。

### 3.2 寄存器文件端口（实测参数）

- **VPRF 物理规格**：128 项 × 128-bit（`Parameters.scala:193-197`；另有 v0=22 项、vl=32 项）。读/写端口数**不是硬编码**，由全部 ExeUnit 的 `VfRD`/`VfWB` 配置自动推导（`BackendParams.scala:295-306`）：当前为 **6 读 / 4 写**（各 128-bit，V0 读 2/写 6，Vl 读 3/写 3）。
- **读**：vs2、vs1、vd（累加旧值）——与 `VimacCfg` 的 `srcData = Seq(Seq(VecData(), VecData(), VecData(), V0Data(), VlData()))` 完全一致（5 个源：vs1/vs2/oldVd/v0/vl）。
- **写**：vd，复用 `VfWB` 写端口。
- **关键约束**：**vdot 单元挂入 VFEX0 复用现有端口**（`VfRD(0..2)` + `V0RD` + `VlRD`，`VfWB(0,0)`），**不新增 VfRD/VfWB 端口号**——新增端口会让 VPRF 端口数自动增大（面积/时序代价）。vdot 累加语义必须读 oldVd（src(2)），与 VimacCfg 布局天然吻合。
- LMUL>1：按寄存器组分段执行（每周期处理一个 128-bit 片段，`VEC_VVV` 1 uop/lmul，uop 数 = lmul），LMUL=2 → 2 周期/条，吞吐相应降低，与 RVV 一般规律一致。

### 3.3 LMUL 支持范围

- 本指令的三个操作数均以 **int32 元素视图**寻址（数据"加宽"仅存在于元素内部的数据重解释，不扩展寄存器组），因此 LMUL ∈ {1,2,4,8} 均可寻址，无加宽比限制。
- 硬件实现：每周期处理 4 个 int32 元素（VLEN=128/LMUL=1），LMUL=2/4/8 时按寄存器组分段执行（2/4/8 周期/条），吞吐相应降低——与 RVV 一般规律一致。
- **v1.0 实现简化选项**：若数据通路只实现 LMUL=1 的 4 元素/周期核心，LMUL>1 通过分段复用同一通路即可，无需额外硬件；LMUL=8 分段 8 次即可。因此**不设 LMUL 上限**，与 golden model 保持一致即可（若官方 Zvdot4a8i 最终限制 LMUL≤4，以官方为准并在文档注明）。
- 参考：社区 Zvdot4a8i 软件仿真工具（[rvv-intrinsic-emulation](https://github.com/nibrunie/rvv-intrinsic-emulation)）将其仿真限制在 LMUL≤4 是其仿真序列（vwmul 加宽）的约束，并非指令本身的寄存器寻址限制。

## 4. 调度 / 写回 / 异常集成

| 机制 | 集成方式 |
| --- | --- |
| 调度 | `VdotCfg` 挂入 **VFEX0**（`Parameters.scala:449`，与 VfmaCfg/VialuCfg/VimacCfg/VppuCfg 共享 EXU）；EXU 内多 FU 按 fuType one-hot 仲裁（`ExeUnit.scala:322-323`）；dispatch 侧按 fuType 查 IQ 分配表（`NewDispatch.scala:359-430`），FuType 集合（vecOPI/vecArith）必须同步，否则 `canAccept` 全 false 报错 |
| 写回 | 复用 `VfWB(0,0)` 写端口（latency=CertainLatency(2)，与运算核心寄存器级数一致）；`wd` 为 vd，`writeVecRf=true`、`writeV0Rf=true` |
| flush/redirect | 沿用 `HasPipelineReg` 按 ROB 指针的 flush 机制（`FuncUnit.scala:184`），无需新逻辑 |
| 异常 | vstart≠0 → illegal（解码侧 `VecExceptionGen.scala:275` + 执行侧兜底）；其余非法（SEW/EEW/EMUL/重叠）并入 `VecExceptionGen` 检查 |
| difftest | 提交路径不变（`Rob.scala:1543-1582` 上报 `DiffInstrCommit`，向量写回以 128-bit 拆 2×64-bit 上报，接口已覆盖）；NEMU golden 需实现 8 条指令语义并注册自定义编码（见 04） |

## 5. 性能建模（设计预估）

### 5.1 吞吐与数据率

| 参数 | 值 |
| --- | --- |
| VLEN / 数据通路宽度 | 128 bit（`Parameters.scala:60`；VecData 128-bit，`DataConfig.scala:17`） |
| 每周期点积组数 | 4（16 对 8×8 乘加） |
| 吞吐 | 1 条/周期（LMUL=1）；持续 INT8 乘加率 = 16 MAC/周期 |
| 时延 | 2 周期（建议，与 VIMac64b 两级寄存器结构一致；见 §3.1） |
| uop 拆分 | `UopSplitType.VEC_VVV`（1 uop/lmul，复用现成逻辑，`DecodeUnitComp.scala:359`） |
| 面积 | 8×8 乘法器 ~16×200 ≈ 3.2k 门当量 + 加法树/累加 ~2k 门（NAND2 等效，估算） |

### 5.2 相对 RVV 1.0 基线的收益（预估 + emu 实测）

> ✅ **2026-08-18 emu 实测（见 05 §5.2）**：vdot GEMM（M=N=K=16）指令数 6,797 vs 标量 C 86,554（12.7× 更少），GEMM 净周期 ~8.2× 更快；理论收益已验证。
> ✅ **注意力实测**：QKᵀ(vdot)+softmax+PV(vdot)（S=4/d=16）emu 差分通过（2,665 指令/10,088 周期），vdot 承担 GEMM 主负载。
> ⚠️ **RVV FP 局限（2026-08-18 记录）**：kunminghu-v2 的 RVV 浮点向量运算 lane≥1 返回 0（仅 element 0 正确）；softmax 降级标量规避，RVV FP 另立专项（疑 VFALU 实现局限）。

| 指标 | RVV 1.0 组合（vzext+vwmaccu+加宽+打包） | vdota4 | 提升 |
| --- | --- | --- | --- |
| 每 4×4 乘加指令数 | ~8–12 | 1 | ≥ 8×（指令数） |
| 动态功耗（数据搬运/胶水指令） | 高 | 低 | 显著（估算见 B3） |
| 寄存器压力 | 高（中间结果多） | 低 | 减少 ~50% |

## 6. 面积 / 功耗 / 时序估算方法

- **面积**：vdot 单元独立综合（Synopsys DC 或开源 OpenROAD），报告面积 ÷ 全核估计面积（昆明湖公开数据或器件数估计，注明假设）。目标 ≤ 0.5%。
- **功耗**：综合后功耗报告（基准波形回标 switching activity）；对比"同负载用 vwmacc 组合实现"的功耗 → TOPS/W。
- **时序**：8×8 乘法（~0.5–1ns@28nm）+ 加法树（1–2 级）+ 32 位累加；目标频率按昆明湖主频（如 1–2GHz 假设，注明）。不满足则 3.1 的流水拆分为 4 拍（§7）。
- **口径**：所有面积/功耗数字注明工艺、库、温度/电压条件，保证可复现可审计。

## 7. 备选实现方案（决策点，阶段二 2.4 前定稿）

| 方案 | 描述 | 优点 | 缺点 | 适用 |
| --- | --- | --- | --- | --- |
| A（默认） | 独立 `VdotU`（wrapper/）+ yunsuan `Vdot64b` 点积核心，2 拍流水，挂 VFEX0 复用现有端口 | 不动 VIMac 路径、时延可控、端口零新增 | 需新增核心与 OpType | 面积/端口优先 |
| B | 扩展 yunsuan `VIMac64b` 增加点积模式（新增 VimacType/VdotType 子操作码 + 点积数据通路分支），`VdotCfg` 仍挂 VFEX0 复用 `VimacCfg` 的端口/时延配置 | 完全复用乘法器/Booth/Wallace 与 2 级寄存器结构（latency=2 现成）、面积最小 | 改动侵入 VIMac 核心、与乘法指令共享吞吐 | 面积极敏感 |
| C | 在 VIAluFix 内以多周期微码实现 | 无新单元 | 吞吐低（ALU 被占）、时延高 | 兜底 |

> 决策依据：A 优先（与现有结构解耦、风险最小）；若面积评估显示 B 更优则评估 B（复用 `VIMac64b` 的 Booth/Wallace 是天然适配 INT8 点积的乘法器结构）。最终选择记录在评估报告（阶段三 3.4）。

## 8. 改动点清单（代码级，路径见附录 A）

| # | 文件/模块（kunminghu-v2 @ e12436c，相对 `src/main/scala/`） | 改动 | 估计量 |
| --- | --- | --- | --- |
| 1 | `xiangshan/backend/decode/Instructions.scala`（或 rocket-chip `Instructions.scala`） | 新增 `object Xkhmvdot`（8 条 BitPat，编码见 02 §5.2） | +8 条目 |
| 2 | `xiangshan/backend/decode/VecDecoder.scala` + `DecodeUnit.scala` | 新增 OPMVV 映射（`VEC_VVV`）；**`vmaInsts` 列表加入 `VDOT_VV`**（isDependOldVd，必改否则 oldVd 被跳过） | +1 映射 +1 列表项 |
| 3 | yunsuan `package.scala` + `encoding/Opcode/` | 新增 `VdotType`（9-bit OpType，`dummy` 保留） | +~30 行 |
| 4 | `xiangshan/backend/fu/FuType.scala` | 新增 `vdot`，同步 `vecOPI`/`vecArith`/`vecAll`（127–133 行） | +~8 行 |
| 5 | `xiangshan/backend/fu/FuConfig.scala` | 新增 `VdotCfg`（仿 `VimacCfg` 547–564 行），同步 `isVecArith`/`needVecCtrl`/`needOg2`（162–190 行） | +~20 行 |
| 6 | `xiangshan/Parameters.scala`（vfSchdParams:449） | `VdotCfg` 加入 VFEX0 的 fuConfigs（复用现有端口） | +1 行 |
| 7 | `xiangshan/backend/fu/wrapper/VdotU.scala`（新建） | `extends VecPipedFuncUnit`，仿 `VIMacU`（VecDataSplitModule + 2×64-bit 核心 + `Mgu`） | 新文件 ~300–600 行 |
| 8 | yunsuan `vector/vectorIMAC/Vdot64b.scala`（新建）或扩展 `VIMac64b` | 64-bit 点积核心：8×8 乘法 + 加法树 + int32 累加（可复用 Booth/Wallace 思路） | ~200–400 行 |
| 9 | `xiangshan/backend/decode/VecExceptionGen.scala` | 新增 vdot 的 EEW/EMUL/对齐/重叠检查（数据视图特殊，不直接落入 `vdWideningInst`） | +10–40 行 |
| 10 | `DecodeUnitComp.scala` / `UopInfoGen.scala` | 复用 `VEC_VVV`，**无需改动** | 0 |
| 11 | 测试 | yunsuan `Vdot64bSpec`（仿 `VIMac64bSpec`）+ 香山 decode 单测 + 回归 | 中 |
| 12 | NEMU（外部仓库） | 实现 8 条指令语义 + 自定义编码注册 | 中 |
| 13 | difftest | **无需改动**（`DiffInstrCommit` 已覆盖向量写回） | 0 |

**硬件侧合计约 600–1200 行 Scala + NEMU 实现与测试**（详见[源码调研报告](../02_设计方案/07_附录_源码调研报告.md)改动点清单）。

> ✅ **2026-08-18 差分验证通过**：VdotU（2×Vdot64b + Mgu）挂 VFEX0 复用现有端口（实测未新增 VfRD/VfWB），latency=CertainLatency(2) 与 Vdot64b 2 级流水一致；RTL 与 NEMU 差分固定 4/4 + 随机 24/24 GOOD TRAP（08 §3.3）。

## 附录 A：源码接入点对照表（源码调研填充）

> 调研基线：**OpenXiangShan/XiangShan 分支 `kunminghu-v2`，commit `e12436c7cba86b195deec24981976d78bc263661`（2026-08-14）**；子模块：yunsuan=`955921186e34bb8915806582a238181a6dc3435c`、difftest=`5d7d90bd6dcd7fede90183fc070f5e35897c6081`、rocket-chip=`46f1efefa1ff431bffe3262e4830bc50316842f4`。行号以该 commit 为准，实现前用实际 clone 复核。详细论证见[源码调研报告](../02_设计方案/07_附录_源码调研报告.md)。

| 接入点 | 文件路径（kunminghu-v2，相对 `src/main/scala/`） | 关键位置 | 说明 |
| --- | --- | --- | --- |
| VLEN/ELEN 参数 | `xiangshan/Parameters.scala` | 第 60 行 `VLEN: Int = 128`；61 行 `ELEN=64`；373 行 `vlWidth=8`；383/388 行 `minVecElen/maxElemPerVreg` | **确认 VLEN=128**，本文档设计假设成立 |
| 向量执行单元（实际实现） | `xiangshan/backend/fu/wrapper/` | `VIMacU.scala:45`（vimac）、`VIAluFix.scala:134`（vialuF）、`VIDiv.scala:15`、`VIPU.scala:88`、`VPPU.scala:31`、`VFMA.scala:16`、`VSet.scala` 等 | **注意：不在 `backend/vector/`**（旧实现已整体注释）；vdot 新单元 `VdotU.scala` 放这里 |
| 向量公共框架 | `xiangshan/backend/fu/vector/` | `VecPipedFuncUnit.scala:58`（piped 基类）、`VecNonPipedFuncUnit.scala:13`、`Mgu.scala:34`（mask/tail merge）、`Mgtu.scala:29`、`Bundles.scala`（VType/VConfig/VSew/Vl/Vxrm/Vxsat） | vdot 继承 VecPipedFuncUnit，Mgu 自动处理 mask/tail |
| 指令 BitPat 表 | `xiangshan/backend/decode/Instructions.scala`（Zvbb/Zimop）或 rocket-chip `Instructions.scala:821`（vwmaccu.vv） | `object Zvbb`/`Zimop` | 新增 `object Xkhmvdot`（8 条 BitPat） |
| RVV 解码表 | `xiangshan/backend/decode/VecDecoder.scala` | `OPMVV/OPMVX`（77–114 行）；`VWMACCU_VV -> OPMVV(T, FuType.vimac, VimacType.vwmaccu, F, T, F, UopSplitType.VEC_VVW)`（426 行） | 新增 8 条 `OPMVV/OPMVX(..., FuType.vdot, VdotType.*, ..., VEC_VVV)` 映射 |
| fuOpType 编码 | yunsuan 子模块 `yunsuan/package.scala` + `encoding/Opcode/` | `VimacType.vwmaccu`（package.scala:392，9-bit OpType：vs2/vs1/vd 符号+format+子opcode）；`VimacOpcode.scala:5-24` | 新增 `VdotType`（`dummy` 保留）或扩展 VimacType |
| FU 类型枚举 | `xiangshan/backend/fu/FuType.scala` | 向量定点 `vecOPI`（127 行）、`vecArith`/`vecAll`（130/133 行） | 新增 `vdot` 并加入集合（漏改→dispatch canAccept 全 false） |
| FU 配置 | `xiangshan/backend/fu/FuConfig.scala` | `VimacCfg`（547–564 行）：piped、latency=CertainLatency(2)、writeVecRf/writeV0Rf/writeVxsat、needSrcVxrm、destDataBits=128、exceptionOut=illegalInstr；`isVecArith`（181–185）、`needVecCtrl`（162–165）、`needOg2`（190） | 复制为 `VdotCfg` |
| 向量调度参数 | `xiangshan/Parameters.scala`（vfSchdParams:445-465） | VFEX0=VfmaCfg+VialuCfg+VimacCfg+VppuCfg（449 行），wb=VfWB(0,0)+V0WB(0,0)，rd=3×VfRD+V0RD+VlRD；共 5 EXU、3 IQ（16/16/10 项） | **VdotCfg 挂 VFEX0**（复用端口，不新增 VfRD/VfWB） |
| VPRF | `xiangshan/Parameters.scala:193-197`（128×128bit）；端口推导 `backend/BackendParams.scala:295-306` | 6R/4W（自动推导）；`DataConfig.scala:17` VecData=128-bit | vdot 复用现有端口，VPRF 面积零增长 |
| 执行单元样板 | `xiangshan/backend/fu/wrapper/VIMacU.scala:45-151` | VecPipedFuncUnit + `VecDataSplitModule`×3（拆 64-bit）+ 2×yunsuan `VIMac64b` + `Mgu`（132–150 行接 mask/tail/vstart/illegal/vxsat） | vdot 完全镜像此结构 |
| yunsuan 运算核心 | yunsuan `vector/vectorIMAC/VIMac64b.scala` | 3 段组合+2 级寄存器（168/208 行）；`VIMac64bSpec` 测试样板 | 新建 `Vdot64b` 或扩展 VIMac64b 增加点积模式 |
| vstart≠0 非法 | `xiangshan/backend/decode/VecExceptionGen.scala:275`；执行侧 `backend/fu/FuncUnit.scala:253-258` | `vstartIllegal = isVArith && (io.vstart =/= 0.U)` | vdot 自动继承，无新逻辑（02 §4.4） |
| 向量异常（EEW/EMUL/重叠） | `xiangshan/backend/decode/VecExceptionGen.scala:42-303` | `vdWideningInst`（83–96 行）、reg 对齐/重叠（219–292 行） | vdot 需自行补充数据视图检查（见 §2 第 5 条） |
| 复杂指令拆分 | `xiangshan/backend/decode/DecodeUnitComp.scala:359-568`、`UopInfoGen.scala:198-242` | `VEC_VVV`（359 行，1 uop/lmul）现成 | vdot 复用 VEC_VVV，零改动 |
| dispatch 分配 | `xiangshan/backend/dispatch/NewDispatch.scala:359-430` | fuType one-hot → IQ 分配表（minIQSelAll） | 新增 FuType 必须有 vf IQ 的 EXU canAccept |
| 写回仲裁 | `xiangshan/backend/exu/ExeUnit.scala:312-387` | 多 FU one-hot 仲裁 + writeVecRf/writeV0Rf 生成写回 | 复用，无需改动 |
| difftest 提交 | difftest 子模块 `Bundles.scala:66-93`（DiffInstrCommit）；硬件上报 `xiangshan/backend/rob/Rob.scala:1543-1582`（向量 128-bit 拆 2×64-bit 上报） | 接口已覆盖任意向量写回 | **difftest 零改动**；NEMU 需实现语义+编码注册 |
| 自定义指令先例 | 无 | grep "Custom/xvld/xvst/XCustom" 无指令级钩子 | vdot 走标准 RVV 链路，无捷径 |
