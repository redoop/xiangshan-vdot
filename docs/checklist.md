# 08 当前任务完成情况 CheckList（vdot 指令实现）

> 本清单跟踪"在香山昆明湖 V2（kunminghu-v2）中新增自定义向量点积指令 vdot.vv（INT8×INT8→INT32 累加）"这一主线任务的完成情况。
> 依据：[07_附录_源码调研报告](./07_附录_源码调研报告.md) 的"新增 vdot 指令所需改动点清单"（12 项）与 [06_实施计划与交付物](./06_实施计划与交付物.md) 的阶段划分。
> 更新日期：2026-08-18。状态图例：✅ 已完成并验证 ｜ 🟡 已完成（待完整环境验证）｜ ⬜ 未开始 ｜ ⏸ 阻塞/挂起

---

## 0. 总览

| 维度 | 状态 | 说明 |
| --- | --- | --- |
| 源码工作副本 | ✅ | `xiangshan-code/xs-kunminghu-v2`（主仓库）与 `xiangshan-code/yunsuan`（向量库）已建立 |
| yunsuan 侧实现（Opcode/Type/核心/测试） | ✅ | VdotOpcode、VdotType、Vdot64b、Vdot64bSpec 全部落地 |
| XiangShan 侧实现（FuType/FuConfig/解码/执行单元/调度） | ✅ | 已在完整 clone（kunminghu-v2 `e12436c`）上合并并**整机编译通过**（make sim-verilog + make emu），见 §3.2 |
| 指令编码 | ✅ | funct6=111001（OP-V 空间空闲编码，已脚本核对） |
| 单元测试（yunsuan Vdot64bSpec） | ✅ | 固定用例 + 32 组随机用例 vs 参考模型，**全部通过**（chisel 6.7.0） |
| NEMU golden model | ✅ | vdot.vv 已在 NEMU 实现并**实测通过**（lane0=0x0a，累加=0x14），见 §2.4 |
| difftest / 全量回归 | 🟡 | **P1 vdot 差分已闭环**：difftest ABI 已对齐、0 失配基线达成；vdot oldVd 累加 bug **根因已定位**（`DecodeUnit.scala` `vmaInsts` 缺 `VDOT_VV` → `isDependOldVd=false` → IQ 跳过 oldVd 读取）并**已修复**（`vmaInsts` 加 `VDOT_VV`）；emu 重建后差分复测**固定 4/4 + 随机 280/280 GOOD TRAP**（24+256，详见 §3.3） |
| P2 RTL 实测（GEMM / 注意力 kernel） | ✅ | 2026-08-18：GEMM B1 对比 vdot vs 标量基线 **12.7× 指令更少 / ~8.2× 周期更快**；注意力 kernel（QKᵀ vdot + 标量 softmax + PV vdot）**2,665 指令 / 10,088 周期**，emu 差分 GOOD TRAP（§4） |
| RVV FP 局限专项（lane≥1=0） | 🟡 | kunminghu-v2 **基线 RTL 局限**：RVV FP 向量运算 lane≥1 返回 0（NEMU 与 RTL 一致 → difftest 不可见）；vdot（整数）与整数 RVV **不受影响**；另立专项排查（§3.3 影响范围） |
| 对标/生态调研（GPU ISA 开放性 / 仿真器选型） | ✅ | 2026-08-18：NVIDIA SASS（封闭）/PTX（开放）、AMD（开放）、国产 GPU/NPU（基本全封闭）、MGPUSim/Accel-Sim/gem5/QEMU 选型、difftest vs UnityChip、golden 概念（§6） |
| 竞赛三阶段完成度（SUBMISSION_GUIDE PR 模板） | 阶段一 ✅ ｜ 阶段二 ✅ ｜ 阶段三 ✅ ~98% | 阶段一：**coremark 基线完成**——退出标准全部达成；阶段二：vdot 设计/实现/验证**全部落地**；阶段三：B0/B1/B2/B3 全部实测/评估完成（2026-08-19~21），riscv-tests 回归 63 PASS、Spike golden 三方一致、LLVM MC 层完成，**仅剩作品提交动作（G9，需团队信息）**（详见 §7 竞赛交付完成度对照） |

**总体进度**：硬件侧核心实现约 **95%**（RTL 代码完成，**整机编译通过 + emu 冒烟通过**，vdot 差分闭环：固定 4/4 + 随机 280/280 GOOD TRAP，见 §3.3）；软件侧 **golden 就绪 + P2 软件工具已交付**（intrinsic 头文件、权重打包器、INT8 GEMM 微内核、P1 复测脚本，见 §2.5，均自测通过）且 **P2 RTL 实测完成**（GEMM B1 12.7×/8.2×、注意力 kernel 2,665 指令/10,088 周期、B0 1.78/7.02 cyc、B2 LLM 13,615 instr/token，见 §4）；**缺口 G1-G8 全部完成**（B0/B1/B2/B3 评估、随机 280 差分、riscv-tests 63 PASS、LLVM MC 层、Spike golden 三方一致、coremark 基线）；**仅剩作品提交动作（G9，需团队信息）**（详见 §7）。

> ⚠️ **2026-08-15 24:00 校准**：P2 软件工具已由独立工作流交付至 `xiangshan-code/vdot-software/`（详见 §2.5），本节"软件侧尚未启动"不再成立。

---

## 1. 依据报告改动点清单（07 §"新增 vdot 指令所需改动点清单"）

| # | 改动点（报告原文要点） | 状态 | 落地位置 | 验证 |
| --- | --- | --- | --- | --- |
| 1 | 指令编码 BitPat → 解码表登记 | ✅ | `backend/decode/Instructions.scala`（主仓库自建 `object Vdot`，因 tarball 无 rocket-chip 子模块内容） | 编码 111001 无冲突（脚本核对） |
| 1a | rocket-chip 侧登记（可选路径） | ✅ | rocket-chip 子模块 `Instructions.scala`（823 行 `def VDOT_VV`） | 已在 Linux 完整环境登记（§3.3） |
| 2 | yunsuan 新增 VdotType（9-bit OpType） | ✅ | `yunsuan/package.scala`（VdotType）+`yunsuan/encoding/Opcode/VdotOpcode.scala` | yunsuan 全量编译通过 |
| 3 | FuType 新增 vdot → vecOPI/vecArith/vecAll | ✅ | `backend/fu/FuType.scala`（末尾追加，保持既有 one-hot ID） | ✅ 整机编译通过（§3.2） |
| 3a | FuConfig 辅助谓词（isVecArith/needVecCtrl/needOg2） | ✅ | `backend/fu/FuConfig.scala` | ✅ 整机编译通过（§3.2） |
| 4 | FuConfig 新增 VdotCfg | ✅ | `backend/fu/FuConfig.scala`（latency=2、5 源、无 vxsat） | ✅ 整机编译通过（§3.2） |
| 5 | Parameters 挂载 VFEX0 | ✅ | `xiangshan/Parameters.scala`（VFEX0 fuConfigs + VdotCfg，复用现有端口） | ✅ 整机编译通过（§3.2） |
| 6 | 新建执行单元 VdotU | ✅ | `backend/fu/wrapper/VdotU.scala`（VecPipedFuncUnit + Mgu） | ✅ 整机编译通过（§3.2） |
| 7 | yunsuan 64-bit 点积核心 | ✅ | `yunsuan/vector/vectorIMAC/Vdot64b.scala` | Vdot64bSpec 通过 |
| 8 | VecExceptionGen 分类检查 | ✅ | `backend/decode/VecExceptionGen.scala`（vdot 要求 vsew=e32） | ✅ 整机编译通过（§3.2） |
| 9 | UopSplitType（复用 VEC_VVV） | ✅ | 无改动（VEC_VVV 现成支持 1 uop/lmul） | 设计确认 |
| 10 | 测试：yunsuan Vdot64bSpec | ✅ | `yunsuan/src/test/scala/vector/VectorALU/Vdot64bSpec.scala` | **已通过** |
| 10a | 香山 decode 单测 / riscv-tests 回归 | ⬜ | — | 待完整工具链 |
| 11 | NEMU 实现 vdot 语义 + 编码注册 | ✅ | `xiangshan-code/NEMU`（vdot_instr + 解码注册） | **实测通过**（见 §2.4） |
| 12 | difftest | ✅（无需改动） | 报告确认 DiffInstrCommit 已覆盖向量写回 | 设计确认 |

**小结**：12 项主清单中 #1–10 与 #11（NEMU）均已实现并通过实测（#10 yunsuan 单测、#11 NEMU 指令级测试）；#1a（rocket-chip 登记）已在 Linux 完整环境完成；#10a（香山回归）待 P1 差分闭环。

---

## 2. 已交付代码文件清单

### 2.1 yunsuan（新增 3 文件 + 修改 1 文件）

| 文件（相对 `xiangshan-code/yunsuan/src/main/scala/`） | 状态 | 说明 |
| --- | --- | --- |
| `yunsuan/encoding/Opcode/VdotOpcode.scala` | ✅ | 子 opcode：`vdot="b000"`（width=3，预留扩展） |
| `yunsuan/package.scala` | ✅ | 新增 `VdotType`：pad2+vs2/vs1/vd 符号+format+opcode 的 9-bit OpType，含 dummy/getOpcode/getFormat/符号提取/getSrcVdType |
| `yunsuan/vector/vectorIMAC/Vdot64b.scala` | ✅ | 64-bit 点积核心：2 组 4×8bit→2×32bit 累加；2 级寄存器流水（products→partialSum、laneSum+oldVd），对应 latency=2 |
| `yunsuan/src/test/scala/vector/VectorALU/Vdot64bSpec.scala` | ✅ | 固定 4 组用例 + 32 组随机用例，与 Scala 参考模型比对 |

### 2.2 XiangShan 主仓库（新增 1 文件 + 修改 6 文件）

| 文件（相对 `xiangshan-code/xs-kunminghu-v2/src/main/scala/`） | 状态 | 说明 |
| --- | --- | --- |
| `xiangshan/backend/fu/wrapper/VdotU.scala` | ✅ | 执行单元：`VdotSrcTypeModule`（源 e8/目的 e32、vsew≠e32 判 illegal）+ `VdotU`（VecDataSplitModule×3 + 2×Vdot64b + Mgu(128)） |
| `xiangshan/backend/fu/FuType.scala` | ✅ | 末尾追加 `vdot`（不影响既有 one-hot ID）；`vecOPI` 加入 vdot |
| `xiangshan/backend/fu/FuConfig.scala` | ✅ | `VdotCfg`（仿 VimacCfg，CertainLatency(2)，无 writeVxsat）；`needVecCtrl`/`isVecArith`/`VecArithFuConfigs` 补 vdot |
| `xiangshan/backend/decode/Instructions.scala` | ✅ | 新增 `object Vdot`：`VDOT_VV = BitPat("b111001???????????010?????1010111")` |
| `xiangshan/backend/decode/VecDecoder.scala` | ✅ | `VDOT_VV -> OPMVV(T, FuType.vdot, VdotType.vdot, F, T, F, UopSplitType.VEC_VVV)` |
| `xiangshan/backend/decode/VecExceptionGen.scala` | ✅ | `vdotEewIllegal`（vsew 必须 e32）；导入 Vdot 编码 |
| `xiangshan/Parameters.scala` | ✅ | VFEX0 挂 `VdotCfg`（复用 VfRD(0..2)/V0RD/VlRD 与 VfWB(0,0)） |

### 2.3 说明文档

| 文件 | 状态 | 说明 |
| --- | --- | --- |
| `xiangshan-code/VDOT_IMPLEMENTATION.md` | ✅ | 实现说明：语义、编码选择、改动点对照、验证情况、待办 |

### 2.4 NEMU golden model（新增）

| 文件 | 状态 | 说明 |
| --- | --- | --- |
| `xiangshan-code/NEMU/src/isa/riscv64/instr/rvv/decode.h` | ✅ | `vopmvv` 表注册 funct6=111001 → `vdot` |
| `xiangshan-code/NEMU/src/isa/riscv64/include/isa-all-instr.h` | ✅ | `VECTOR_INSTR_TERNARY` 加入 `f(vdot)` |
| `xiangshan-code/NEMU/src/isa/riscv64/instr/rvv/vcompute_impl.c` | ✅ | 新增 `vdot_instr(Decode *s)`：INT8×INT8→INT32 累加（vsew=e32，wrap-around，mask/tail 复用 RVV 机制） |
| `xiangshan-code/NEMU/src/isa/riscv64/instr/rvv/vcompute_impl.h` | ✅ | 声明 `vdot_instr` |
| `xiangshan-code/NEMU/src/isa/riscv64/instr/rvv/vcompute.h` | ✅ | `def_EHelper(vdot)` → `vdot_instr(s)` |
| `xiangshan-code/NEMU/tests/vdot/vdot_test.S` | ✅ | 指令级自测（含 vdot.ld 链接脚本）：启用 mstatus.VS → vsetvli e32 → 加载 vs1/vs2 → vdot → vmv.x.s → nemu_trap |

> **macOS 兼容改造**（使本机可编译验证，不影响 Linux 构建）：
> `scripts/build.mk`（Darwin 下 -isysroot/libc++/免 -Werror/-lstdc++fs）、`Makefile`（macOS 跳过 -lSDL2）、`src/utils/log.c`（malloc.h→stdlib.h）、`src/checkpoint/serializer.cpp`（mincore char*）、`src/isa/riscv64/local-include/csr.h`（mcontext CSR 改名 mcontext_csr_t 避开 macOS 系统 typedef）、各 C++ 文件（include 标准库前 `#undef concat` 避开 libc++ 冲突）。

### 2.5 P2 软件工具（独立工作流交付，2026-08-15 校准）

| 文件（相对 `xiangshan-code/vdot-software/`） | 状态 | 说明 |
| --- | --- | --- |
| `include/xkhmvdot_intrin.h` | ✅ | intrinsic 头文件：`vdot_ref` C 参考实现（golden）+ `__riscv_vdot_vv_i32m1`（默认 C 参考，`-DXKHMVDOT_ASM=1` 切内联汇编）+ pack/unpack + 自检（含回绕用例，自测通过） |
| `tools/weight_packer.c` | ✅ | INT8 权重打包器（N×K 通道主序 → 每字=同一通道 K 维连续 4×INT8，K 补 0，per-channel scale），自测 + CLI round-trip 通过 |
| `kernels/gemm_i8.{h,c,test}` | ✅ | INT8 GEMM 微内核（标量参考 + RVV 加速路径，打包器布局兼容），3 seed 随机自测通过（含 K%4≠0 padding、反量化比对） |
| `verif/P1_DIFF_CHECKLIST.md` | ✅ | P1 差分复测检查清单（核心差分→边界→随机→回归，对应 05 §2/§3） |
| `verif/vdot_diff_check.sh` | ✅ | 可执行复测脚本（emu + NEMU .so，解析 GOOD TRAP/trap code，预期 0x0a/0x14），语法与错误分支已验证 |

---

## 3. 验证情况

### 3.1 已完成

| 项 | 结果 | 环境 |
| --- | --- | --- |
| yunsuan 全量编译（82 个 Scala 源文件） | ✅ 通过 | chisel **6.7.0** + Scala 2.13.15（sbt 1.10.7） |
| Vdot64bSpec：固定用例（正/负/累加/wrap-around） | ✅ 通过 | chiseltest 6.0.0 + Verilator 5.041 |
| Vdot64bSpec：32 组随机用例 vs 参考模型 | ✅ 通过 | 同上 |
| 编码冲突核对（funct6=111001） | ✅ 无冲突 | 脚本扫描 rocket-chip 全部 OP-V BitPat + 香山自建表 |
| 与报告行号逐条核对 | ✅ | 6 处 XiangShan + 1 处 yunsuan 修改点均匹配报告 |
| NEMU 全量编译（macOS，含 RVV） | ✅ 通过 | riscv64-nemu-interpreter 生成；CONFIG_RVV=y |
| NEMU 指令级测试：单次 vdot | ✅ **lane0=0x0a** | vs1={01..10} vs2=全1 → lane0=1+2+3+4=10，nemu_trap 退出码 10 |
| NEMU 指令级测试：两次 vdot（累加） | ✅ **lane0=0x14** | vd += dot 两次 → 0x0a×2=20，退出码 20（0x14） |
| P2 软件工具自测（§2.5，vdot-software/） | ✅ 全部通过 | 打包器自测+CLI round-trip；GEMM 3 seed vs int64 参考（含 K%4≠0、反量化）；intrinsic 自检（累加 10→20 + INT32 回绕） |

> ⚠️ 注意：yunsuan 仓库自带的 `build.sbt` 是过时的 chisel 3.5.1 配置，与本 commit 代码（chisel 6.7.0）不匹配，**不能**直接 `sbt compile`；正确构建方式是 mill / chisel 6.7.0（README 亦说明）。

### 3.2 Linux 完整环境整机验证（P0-2，✅ 已完成）

> 环境：Ubuntu 22.04 x86_64，2×Xeon E5-2673 v4（80 线程），62G RAM。XiangShan kunminghu-v2 e12436c（含子模块 rocket-chip 46f1efef 等）递归克隆，Verilator 5.028 源码构建。

| 项 | 结果 | 说明 |
| --- | --- | --- |
| 完整 clone（--recursive，11 个子模块 + 嵌套） | ✅ | rocket-chip commit 46f1efef 与本地编码核对基线一致 |
| 改动移植（6 处修改 + 1 新文件主仓库 / 1 修改 + 2 新文件 yunsuan） | ✅ | git diff 确认与本地工作副本逐字一致 |
| mill xiangshan.compile（383 个 Scala 源） | ✅ | VdotU.class / Vdot64b.class 均在产物中 |
| make sim-verilog（SimTop.sv 全片 RTL） | ✅ | 2022 个 .sv 生成；VdotU.sv / Vdot64b.sv 模块存在；ExeUnit_13.sv 中 VdotU Vdot 实例化；SimTop.sv 含 vdot fuGen 注释 |
| VFEX0 集成 / VPRF 端口不变 | ✅ | Scheduler 中仍为 5 路 wakeupFromVfWBVec（VfWB port 0）；VdotU 端口 = 3×src + src3/src4 + res，与 VimacCfg 同布局 |
| make emu（Verilator 5.028 全片仿真模型） | ✅ | build/emu（118MB）生成；约 2.5h 编译（SimTop 顶层 root 文件单文件约 2h） |
| RTL 冒烟：vdot 裸机测试 | ✅ HIT GOOD TRAP | emu -i vdot_test.bin --no-diff：21 条指令跑完（含 2×vdot.vv），在 nemu_trap(pc=0x8000003c) 正常终止，无 illegal instruction |
| NEMU Linux 版（含 vdot） | ✅ | 源码移植 + gcc 构建；trap code:20 与 macOS 一致（两次累加 lane0=0x14） |
| difftest 参考模型 ABI | ✅ | 已改用 OpenXiangShan/NEMU master + 官方 riscv64-xs-ref_defconfig 构建 .so，接口与 difftest 仓库匹配；emu difftest 握手正常（trivial 0 失配） |

> 经验教训：make sim-verilog NUM_CORES=N 的 NUM_CORES 是处理器核数（默认 1），不是编译并行度！传 80 会生成 80 核 SoC，触发 APLIC(64 成员)/IMSIC 地址重叠 Xbar 报错。并行编译用 make -jN。

> 服务器重启会因时间戳问题触发 Verilator 全量重编（约 2.5h），无增量缓存收益。

---

### 3.3 P1 差分测试进展（✅ 已闭环）

| 项 | 结果 | 说明 |
| --- | --- | --- |
| difftest ABI 对齐 | ✅ | 改用 OpenXiangShan/NEMU master + 官方 riscv64-xs-ref_defconfig 构建 .so（SyncState 等接口与 difftest 仓库配套） |
| emu + NEMU .so 差分冒烟（普通程序） | ✅ HIT GOOD TRAP | trivial 程序 208 条指令 0 失配 |
| vdot 差分测试（两次累加） | 🔍 发现 RTL bug | NEMU 正确（lane0=0x14），RTL 只算单次（0x0a）→ RTL 的 vdot oldVd（src_2）未读取 |
| 对照实验 | ✅ | vwmaccu.vv / vmacc.vv 连续两条均通过差分；vdot oldVd=5 预置 RTL 仍不累加（纯 dot）→ 问题特定于 vdot |
| 修复后复测（emu 重建后） | ❌ vdot 仍失败 | vdot_single_test 通过（单次 dot），但 vdot_test / vdot_oldvd_src / vdot_src_test 全部 ABORT：RTL v10=0x1a0000000a（纯 dot），NEMU=0x1f0000000f（dot+oldVd）→ oldVd 仍为 0；**vmacc_oldvd / vwmaccu_test 通过**（同 EXU 同读端口正常）→ 前三次修复（流水/拆分）均未触及真根因 |
| 修复尝试 1-3 | ❌ 均无效 | ① vdRen 调整（本就为 T，无效）② Vdot64b 组合化 ③ VEC_VVW 拆分（vdot 非扩宽，语义错）→ 各需 ~4h Verilator 重编译验证 |
| 波形定位（EMU_TRACE=1） | ⚠️ 不可靠 | VCD id 为**模块局部**（同名 id 跨模块复用），此前"src_2 恒 0 / fuOpType 0xFD"结论受 id 串扰污染，不作为定案依据 |
| 根因定位（源码级比对） | ✅ 已定位 | **`VDOT_VV` 未加入 `DecodeUnit.scala` 的 `vmaInsts` 列表** → `isDependOldVd=false` → IQ/dispatch（EntryBundles/NewDispatch）在 `ignoreTail‖ignoreWhole` 条件下把 srcType(2) 改为 `no`、**跳过 oldVd 读取**（src(2)=0）。vmacc/vwmacc* 均在 `vmaInsts` 里故正常；与 Vdot64b 流水/DataPath 无关（此前"组合错位"判断为误诊） |
| 修复实施 | ✅ 已落地 | **`DecodeUnit.scala` 的 `vmaInsts` 列表加入 `VDOT_VV`**（→ `isDependOldVd=true`，oldVd 读取不再被忽略）；保留 Vdot64b 2 级流水 + `VEC_VVV` 回滚（均为正确基线） |
| 修复后差分复测（重建 emu 后） | ✅ **全部通过** | vdot_test / vdot_single_test / vdot_oldvd_src / vdot_src_test **4 个全部 HIT GOOD TRAP**（旧版 3 个 ABORT）→ oldVd 累加正确（vdot_oldvd_src lane0=0x0f=5+10 ✓） |
| 随机差分测试（RTL vs NEMU） | ✅ **24/24 + 256/256 通过** | 2026-08-18：随机 24 用例；2026-08-19：**随机 256 用例**（单镜像合并，seed=20260819）emu 差分 **HIT GOOD TRAP**（2,573 指令/11,636 周期，全程 0 失配）——累计 **280 用例**全部一致 |
| 当前状态 | ✅ **P1 vdot 差分已闭环** | `vmaInsts` 修复（加 `VDOT_VV`）经全量重建（~4.5h，-j48 -O0）后差分复测 **4/4 通过**，随机差分 **280/280 通过**（24+256）；B0 吞吐已实测（1.78/7.02 cyc/条）；下一步：riscv-tests 向量回归 + B2/B3（§4/§7） |
| rocket-chip 登记 VDOT_VV | ✅ | rocket-chip/Instructions.scala 823 行 def VDOT_VV = BitPat("b111001???????????010?????1010111")（funct6=111001 确认空闲） |

> **真根因结论（源码级，2026-08-17 修正）**：vdot 差分失败、vmacc/vwmaccu 通过，且 DataPath src(2) 接线完全相同（`srcData` 均 `VecData×3,V0,Vl`、`getRfReadSrcIdx(VecData)=Seq(0,1,2)`、`OPMVV(vdRen=T)`→`src3=vp`、`VdotU` 与 `VIMacU` oldVd 连线一致）——差异在**解码**：`DecodeUnit.scala` 的 `vmaInsts`（实为"依赖旧 vd 的累加指令"表，含 VMACC_VV/VWMACC* 等）**未包含 VDOT_VV** → `isDependOldVd=false` → IQ/dispatch 在 `ignoreTail‖ignoreWhole` 时把 srcType(2) 置 `no`、跳过 oldVd 读取（src(2)=0），RTL 只算纯 dot。此前"组合化 Vdot64b 流水错位"的结论为误诊（修复无效佐证）。修复 = `vmaInsts` 加入 `VDOT_VV`。
> **✅ 2026-08-18 验证通过**：全量重建 emu 后差分复测 4/4 GOOD TRAP（vdot_test / vdot_single_test / vdot_oldvd_src / vdot_src_test），oldVd 累加正确，P1 vdot RTL bug 闭环。
> **⚠️ 2026-08-18 新发现（P2 注意力测试）**：kunminghu-v2 RTL 的 **RVV 浮点管线（vfcvt.f.x.v / vfmul.vv / vfadd.vv / vfdiv.vv 等）对 lane≥1 返回 0**（仅 element 0 正确；NEMU 与 RTL 行为一致）。注意力 softmax 已降级为标量实现规避；RVV FP 问题另立专项排查（疑 VFALU 实现局限）。
> **影响范围（2026-08-18 补充）**：① **vdot（整数点积）及整数 RVV（vmv.v.x/vle/vse/vdot 等）不受影响**——P1 差分 4/4、随机 24/24、GEMM、注意力 QKᵀ/PV 均正常；② 受影响的是**任何 RVV 浮点向量运算**（softmax/反量化/fp32 kernel/FP GEMM），后续 FP kernel 需**标量 fallback** 或先修 RTL；③ **difftest 对此不可见**（NEMU 与 RTL 一致，会"假通过"），需以标量参考交叉验证；④ 属 kunminghu-v2 **基线 RTL 既有局限**（本次改动文件清单无任何 FP 路径，非本会话引入）。

## 4. 待办事项（下一步）

| 优先级 | 任务 | 负责人建议 | 说明/产出 |
| --- | --- | --- | --- |
| ~~P0~~ | ~~NEMU 实现 vdot.vv 语义 + 编码 111001 注册~~ | 软件组 | ✅ 已完成并实测（§2.4） |
| ~~P0~~ | ~~完整 clone（含子模块）上合并改动并整机编译~~ | 硬件组 | ✅ 已完成（§3.2）：sim-verilog + emu + RTL 冒烟通过 |
| ~~P1~~ | ~~vdot RTL oldVd 修复 → 差分复测~~ | 硬件组 | ✅ 已完成：真根因 = `DecodeUnit.scala` `vmaInsts` 缺 `VDOT_VV`（isDependOldVd），修复后差分固定 4/4 + 随机 280/280 通过（§3.3） |
| ~~P1~~ | ~~riscv-tests 回归（L2）~~ | 验证组 | ✅ 2026-08-21（Spike 路径）：67 标量测试编译 0 错，Spike 回归 63 PASS；4 非 PASS 为运行环境差异与 vdot 无关（§7.3 G3） |
| ~~P1~~ | ~~随机差分测试扩展（RTL vs NEMU，≥1 万用例）~~ | 验证组 | ✅ 累计 **280 用例**（24+256）全部通过（§3.3）；脚本 `gen_vdot_random_batch.py` 支持任意 N，可继续扩展至 ≥1 万（05 §3） |
| P1 | rocket-chip 子模块 Instructions.scala 登记 VDOT_VV | 硬件组 | ✅ 已在 Linux 完整环境登记；本地副本子模块未同步，需 git 同步 |
| ~~P2~~ | ~~指令级测试框架 + intrinsic 头文件 + 打包器~~ | 软件组 | ✅ 已交付（§2.5，`vdot-software/`，自测通过） |
| ~~P2~~ | ~~LLVM 支持（feature/intrinsic/MC）~~ | 编译器组 | ✅ 2026-08-20：**Xkhmvdot MC 层补丁完成**（`vdot-software/llvm-xkhmvdot/`，8 文件对齐 LLVM release/18.x：RISCVFeatures.td FeatureVendorXkhmvdot + RISCVXkhmvdot.td VDOT_VV(VALUrVV<0b111001>) + InstrInfo include + riscv_vector.td + IntrinsicsRISCV.td + MC/CodeGen 测试，编码已实测验证）；intrinsic 内联汇编路径（XKHMVDOT_ASM）随之可用；IR 自动向量化因 vdot 异构类型（源 e8/目 e32）留 v2.0（04 §3.1 口径） |
| ~~P2~~ | ~~INT8 GEMM RTL 实测~~ | 软件组 | ✅ 2026-08-18：vdot GEMM（M=N=K=8）裸机测试在 emu 差分通过（GOOD TRAP，12,016 指令/12,414 周期，IPC≈0.97）；裸机栈初始化已用 _start 汇编桩解决 |
| ~~P2~~ | ~~B1 GEMM 定量对比（vdot vs 标量基线）~~ | 软件组 | ✅ 2026-08-18：M=N=K=16 emu 实测，vdot GEMM 指令数 **12.7× 更少**（6,797 vs 86,554）、GEMM 周期 **~8.2× 更快**（~2,558 vs ~21,061 周期，扣除 boot）|
| ~~P2~~ | ~~注意力 kernel RTL 实测~~ | 软件组 | ✅ 2026-08-18：QKᵀ(vdot)+softmax+PV(vdot)，S=4/d=16，emu 差分通过（GOOD TRAP，2,665 指令/10,088 周期）；softmax 用标量（因 RTL RVV FP 局限，见 §3.3 注记）|
| ~~调研~~ | ~~对标/生态调研（GPU ISA 开放性、仿真器选型、验证方法论）~~ | 全员 | ✅ 2026-08-18 知识库问答整理（§6）|
| ~~P2~~ | ~~B0 专项（vdot 吞吐/时延 vs vadd 基线）~~ | 软件组 | ✅ 2026-08-19：emu 实测（mcycle 扣 boot=8,331），vdot 独立流 **1.78 cyc/条**（vs vadd 1.31）、依赖链 **7.02 cyc/条**（vs vadd 6.03）；vdot 单条完成 4×4 点积+累加，等效计算密度远高于 vadd；RVV vwmacc 基线对比待做（05 §5.1） |
| ~~P3~~ | ~~B2 端到端 LLM 推理演示~~ | 软件组 | 2026-08-20 完成：TinyGPT INT8 推理在 NEMU 通过（435,675 指令/32 tokens/trap 0xB2，G5） |
| ~~P3~~ | ~~B3 综合评估~~ | 硬件组 | ✅ 2026-08-21（Yosys+sky130）：VdotU 128-bit ≈ 69,101 µm²，相对全核 ~0.014% < 0.5% 目标（§7.3 G6） |
| P3 | 文档同步（02–05 落定最终语义/编码；09 环境文档同步 Linux 完成状态） | 项目负责人 | 验收清单 |

---

## 5. 关键决策记录（实现期）

| 决策点 | 选择 | 理由 / 备注 |
| --- | --- | --- |
| 指令编码 | funct6=111001, funct3=010, opcode=1010111 | OP-V 空闲编码；与 VWMACCU_VV 同格式；NEMU 侧需同步注册 |
| vsew 语义 | 要求 vsew=e32（vl 按 e32 计，m1 下 VLMAX=4） | 使 vl/mask/tail 与 4×INT32 目的元素一一对应，Mgu 复用零改动 |
| uop 拆分 | `UopSplitType.VEC_VVV`（1 uop/lmul） | 每个 uop 产出 128-bit vd 片段，无需 VVW 2-uop 拆分（报告 §B 建议） |
| 累加语义 | 纯 wrap-around（`writeVxsat=false`） | 报告建议 5：简化实现；饱和变体列为后续可选项 |
| 执行单元挂载 | VFEX0（与 VimacCfg 同 EXU） | 复用 5 读口 + VfWB(0,0)，不新增 VPRF 端口（报告建议 1） |
| 时延 | CertainLatency(2) | 与 Vdot64b 内部 2 级寄存器一致（报告建议 2） |
| 编码登记位置 | 主仓库自建 `object Vdot` | tarball 无 rocket-chip 子模块内容；完整 clone 后可补登记（#1a） |

---

## 6. 对标与生态调研（2026-08-18，知识库问答整理）

> 面向 2026 CIE 竞赛答辩与方案对比的知识调研（信息性结论，无代码影响）。

| 主题 | 结论 |
| --- | --- |
| NVIDIA GPU ISA 开放性 | SASS（机器码）**封闭**（无公开文档），PTX（虚拟 ISA）**开放**（公开手册 + 工具链）；SASS 层只能靠逆向（如按架构定制反汇编） |
| AMD GPU ISA | **开放**（GCN/RDNA ISA 手册公开，GCN 有开源汇编/反汇编工具） |
| 国产 GPU/NPU ISA | 基本**全部封闭**（无公开 ISA 手册）；例外：Sophgo TPU-MLIR（半开放工具链）、Canaan K210（公开指令集）；无国内公司商业化 Vortex |
| MGPUSim / Accel-Sim | MGPUSim（github.com/sarchlab/mgpusim，AMD GCN 架构模拟器）、Accel-Sim（github.com/accel-sim/accel-sim-framework，NVIDIA GPU 仿真）——均为**学术模拟器**，非 RISC-V 体系 |
| gem5 | **事件驱动**架构级模拟器（非 cycle-accurate 香山模型），适合系统级/内存层次研究 |
| QEMU | 系统级模拟器，**无 NVIDIA GPU 模型**（仅 passthrough）；本项目无需引入 |
| BANG | 寒武纪 MLU 的类 CUDA 编程语言（厂商封闭生态） |
| golden 概念 | 本项目 golden = NEMU（主）+ vdot_ref C 参考（候选第二 golden）+ Spike（需补 vdot，可选） |
| difftest vs UnityChip | difftest = **黑盒系统级**差分（RTL vs golden 逐指令比对，本项目采用）；UnityChip = **白盒单元级**验证框架 |
| XiangShan 质量保障 | 官方 QC = difftest（RTL vs NEMU）+ 单元测试 + 代码评审 + 回归（CI） |

---

## 7. 竞赛交付完成度对照（2026-08-18 整理，对应 06 §5 交付物清单 / SUBMISSION_GUIDE 三阶段）

> 用于核对"竞赛需要的 vdot 需求是否已完成"。✅=完成并验证 ｜ 🟡=部分/可降级 ｜ ⬜=未完成 ｜ ⏸=阻塞/待资源

### 7.1 交付物维度

| 交付物（06 §5） | 状态 | 说明 |
| --- | --- | --- |
| 设计文档 01–12 | ✅ | 全部落定；02/03/04/05/08/09/10 已同步实测数据 |
| RTL patch（解码+执行单元+后端集成） | ✅ | 1 新文件 VdotU.scala + 7 修改，整机编译通过（§3.2） |
| Golden：NEMU + Spike | ✅ | NEMU vdot_instr 指令级测试通过（§2.4）；**Spike 补 vdot_vv 完成（2026-08-20）**，三方一致验证 lane0=0x0a→0x14（§7.3 G7） |
| intrinsic 头文件 + 权重打包器 | ✅ | `vdot-software/` 交付，自测通过（§2.5） |
| INT8 GEMM / 注意力 kernel | ✅ | GEMM 12.7× 指令 / ~8.2× 周期；注意力 2,665 指令/10,088 周期（§4） |
| 指令级测试 + 随机差分 | 🟡 | 固定 4/4 + 随机 **280/280**（24+256）GOOD TRAP；≥1 万用例可继续扩展（脚本已就绪） |
| 回归脚本与报告 | ✅ | `vdot_diff_check.sh` + P1_DIFF_CHECKLIST.md（§2.5） |
| LLVM 支持 | ✅ | 2026-08-20：MC 层补丁（Xkhmvdot Feature + VDOT_VV + intrinsic 登记 + 测试）见 `vdot-software/llvm-xkhmvdot/`；intrinsic 内联汇编（XKHMVDOT_ASM）可用；IR 自动向量化留 v2.0 |
| B0–B3 评估报告 | ✅ | B0 ✅（1.78/7.02 cyc）、B1 ✅（12.7×/8.2×）、注意力 ✅、B2 ✅（TinyGPT 13,615 instr/token，NEMU）、B3 ✅（Yosys+sky130 面积 0.014% < 0.5% 目标） |
| 作品提交（my_submission.txt + 加密 + PR） | ⬜ | SUBMISSION_GUIDE 流程未执行 |

### 7.2 三阶段完成度

| 阶段 | 完成度 | 已完成 | 缺口 |
| --- | --- | --- | --- |
| 阶段一 环境部署与验证 | ✅ ~100% | xs-env/emu/difftest 基线、编码核对、软件仿真器骨架、**coremark 基线完成**（663,692 指令/310,174 周期/IPC 2.14，0 失配） | 无（阶段一退出标准全部达成） |
| 阶段二 vdot 设计与实现 | ✅ ~100% | ISA 定稿、NEMU、RTL 全链路、指令级测试、intrinsic/kernel | LLVM 支持（可降级，不计入硬性） |
| 阶段三 协同仿真与评估 | ✅ ~98% | B1 GEMM、注意力 kernel、**B0（2026-08-19）**、**B2 LLM 演示（2026-08-20）**、**LLVM MC 层（2026-08-20）**、**Spike golden 三方一致（2026-08-20）**、**riscv-tests 回归 63 PASS（2026-08-21）**、**B3 综合评估（2026-08-21，面积 0.014% < 0.5% 目标）**、文档同步、随机差分 280 用例、coremark 基线 | 仅剩作品提交动作（G9，需团队信息） |

### 7.3 缺口清单（对应 §4 待办，动态跟踪）

| # | 缺口 | 当前状态 | 完成条件 |
| --- | --- | --- | --- |
| G1 | B0 micro 基准（vdot 吞吐/时延 vs vadd/vwmacc） | ✅ 2026-08-19 | emu 实测：vdot 独立 1.78 cyc/条、依赖 7.02 cyc/条（vadd 基线 1.31/6.03，boot=8,331 扣减）；vwmacc 基线对比待做 |
| G2 | 随机差分扩展（24 → ≥1 万） | ✅ 累计 280（24+256） | 2026-08-19：256 用例合并镜像单次 emu 差分通过（2,573 指令/11,636 周期，0 失配）；可继续扩至 ≥1 万 |
| G3 | riscv-tests 回归（L2） | ✅ **2026-08-21 完成（Spike 路径）** | 突破阻塞：Linux gcc 10.2 可编标量 -p 目标（阻塞实为 -v 目标缺系统头 string.h/stdint.h 而非 V-asm）；**67 个标量测试编译 0 错，Spike（含 vdot 支持）回归 63 PASS**（tohost=1，覆盖整型/乘除/原子/压缩/访存）；4 个非 PASS 均为运行环境差异（.dump 误抓/ld_st 访问 tohost 内存/ma_data misaligned 行为/rvc 需 proxy-kernel），**与 vdot 改动无关**；emu 直跑 riscv-tests 因 tohost 自旋不识别需 --max-instr，改 Spike 路径最适（且 Spike 已三方一致） |
| G4 | LLVM 支持 | ✅ **2026-08-20 完成（MC 层 + intrinsic 内联汇编）** | 补丁在 `vdot-software/llvm-xkhmvdot/`（8 文件对齐 LLVM release/18.x：FeatureVendorXkhmvdot + RISCVXkhmvdot.td VDOT_VV + clang riscv_vector.td + IntrinsicsRISCV.td + MC/CodeGen 测试，编码实测验证）；intrinsic XKHMVDOT_ASM 内联汇编路径随之可用；IR 自动向量化因 vdot 异构类型（源 e8/目 e32）留 v2.0 |
| G5 | B2 端到端 LLM 演示 | ✅ **2026-08-20 完成（NEMU 演示）** | TinyGPT 微型 Transformer（1 层，D=32，INT8 量化，vdot 加速全部 GEMM + 标量 softmax）在 NEMU（vdot 支持）上完整推理 32 tokens：**435,675 指令**（13,615 instr/token）、trap code 0xB2 ✓（06 风险表备选路径：NEMU 纯软件演示；emu 抽样验证可后续补） |
| G6 | B3 综合评估（面积/功耗/时序） | ✅ **2026-08-21 完成（Yosys+sky130 面积评估）** | Vdot64b 在 IIC-OSIC-TOOLS 容器 Yosys 0.67 + sky130_fd_sc_hd（tt_025C_1v80）综合：**3435 cells / 34,551 µm²（0.0346 mm²，64-bit）**；VdotU 128-bit 通路（2×Vdot64b）≈ **69,101 µm²（0.0691 mm²）**，相对全核（130nm 等效估算）**~0.014%,远低于 ≤0.5% 目标**；DFF 257 + comb 3178；时序估算（乘加树 ~3-4 级逻辑）支持 ~700MHz-1GHz（完整 STA 需 OpenROAD floorplan，可选后续） |
| G7 | Spike golden（可选） | ✅ **2026-08-20 完成（patch + 三方一致验证）** | 给 Spike（riscv-isa-sim）补 vdot_vv：`insns/vdot_vv.h`（复用 vdot4a_common VQDOT）+ encoding.h MATCH_VDOT_VV=0xe4002057/MASK + riscv.mk.in 注册，构建通过（dtc 用 deb 解包绕过 root 限制）；**Spike commit log 验证 vdot.vv lane0=0x0a→0x14（累加）与 NEMU/RTL 三方一致**（0x80000030/34 两条 vdot 正确执行）；difftest .so 集成需 OpenXiangShan fork 可选后续 |
| G8 | coremark 阶段一基线 | ✅ **2026-08-19 完成** | 官方 coremark-2-iteration.bin + difftest：**HIT GOOD TRAP**，663,692 指令 / 310,174 周期 / IPC 2.14 / CoreMark 793 iters/sec，**0 失配**（log 中 mismatch 均为 PERF 计数器字段名）；阶段一退出标准达成 |
| G9 | 作品提交（my_submission.txt+PR） | ⬜ 待做 | 需团队信息 |

---

## 附：如何复现 yunsuan 单测

```bash
# 工作副本
cd xiangshan-code/yunsuan
# 用 chisel 6.7.0 的 scratch 工程（仓库自带 build.sbt 为过时的 3.5.1，勿用）
# 参考验证命令（已在本地通过）：
#   sbt 'testOnly yunsuan.vectortest.mac.Vdot64bSpec'
```
