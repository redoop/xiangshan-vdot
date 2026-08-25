# 10 软硬件需求清单（vdot 实现后续任务）

> 本清单以**可勾选**的条目列出完成 vdot 后续任务（P0–P3，见 [08_任务完成情况CheckList](./08_任务完成情况CheckList.md)）所需的全部软硬件资源，供团队按项准备、逐项打勾核对。
> 详细分析与理由见 [09_软硬件环境需求分析](./09_软硬件环境需求分析.md)。
> 更新日期：2026-08-14。勾选约定：`[x]` = 已就绪/已完成；`[ ]` = 待准备；`[~]` = 部分就绪/可降级。

---

## 1. 硬件清单

### 1.1 开发/验证主机（阶段 A + B 共用）

- [x] 本机 macOS（Apple M2 / 8GB / 30Gi）— 承担：代码编写、yunsuan 单测、文档、编码核对
- [x] **Linux x86_64 主机（推荐 8 核 / 32GB 内存 / ≥200GB 磁盘）** — ✅ 已就绪并完成 P0-2（Ubuntu 22.04 / 2×Xeon E5-2673 v4 80 线程 / 62G，08 §3.2）
- [~] 备用：云主机实例（按需开机，Ubuntu 22.04，规格 ≥ 8c/32GB/200GB SSD）— 未启用（本地 Linux 已够）
- [ ] 备用：Docker Desktop / WSL 2（若暂不申请云主机，可先跑通部分 Linux 工具）

### 1.2 按任务的最小硬件清单

| 任务 | 最小配置 | 已具备？ |
| --- | --- | --- |
| yunsuan 单测 | 2 核 / 4GB / 5GB | [x] 本机可跑 |
| NEMU 实现 + 单指令测试 | 2 核 / 8GB / 10GB | [x] 本机已完成（riscv64-elf-gcc + NEMU 实测） |
| 全核 `make emu` | 8 核 / **16GB（推荐 32GB）** / 50–100GB | [x] Linux 已跑通（80 线程/62G，多次 emu 重建，08 §3.2/§3.3） |
| difftest / riscv-tests 回归 | 8 核 / 16GB / +20GB | [x] difftest 已跑通（vdot 4/4 + 随机 24/24）；riscv-tests 向量回归待做 |
| 随机差分（≥1 万用例） | 8 核 / 16GB / +10GB | [x] 已做 24 用例随机差分（08 §3.3）；可扩展 |
| LLVM RISC-V 后端 | 8 核 / 16GB / +60GB | [~] 可降级为内联汇编（04 §3.3） |
| INT8 GEMM/注意力 kernel 微基准 | 4 核 / 8GB / +5GB | [ ] 需 Linux（或 NEMU 上跑） |
| B2 端到端 LLM 演示（≤2B INT8） | 8 核 / 16GB / +20GB | [ ] GPU ≥16GB 显存为加速项（可选） |
| B3 面积/功耗/时序综合 | 8 核 / 32GB / +50GB | [ ] 需 EDA 工具 + PDK（见 §2.3） |

### 1.3 可选硬件

- [ ] GPU（≥16GB 显存）— 仅 B2 演示加速用，非必需
- [ ] 外置存储（≥100GB）— 若本机承担部分仿真缓存
- [ ] 千兆网 / 内网 NAS — 团队共享环境与数据（可选）

---

## 2. 软件清单

### 2.1 基础工具链

- [x] JDK 11（openjdk-11）— 本机 11.0.23
- [x] Verilator ≥4.204 — 本机 5.041
- [x] sbt — 本机可用（yunsuan 单测已验证）
- [x] mill 0.12.3（chisel 6 工程构建）— Linux 已装
- [x] RISC-V 交叉编译器：`riscv64-unknown-elf-gcc` 10.2.0 — Linux 已装（V-asm 不支持，随机测试用 macOS 15.2.0 交叉编译）
- [x] make / g++ / clang / llvm / git / wget / curl / tmux / vim — Linux 已装
- [x] python3 + python3-protobuf + python3-grpc-tools（difftest 依赖）— Linux 已装
- [x] zstd / libzstd-dev / libsdl2-dev / zlib1g-dev / device-tree-compiler — Linux 已装
- [x] flex / autoconf / bison / sqlite3 / libsqlite3-dev / rsync — Linux 已装
- [ ] LLVM（RISC-V 后端开发用；可降级为 gcc + 内联汇编）

### 2.2 源码仓库

- [x] 完整 `XiangShan` 仓库（`git clone --recursive`，含全部子模块）— Linux 已完成（08 §3.2）
- [x] 子模块：rocket-chip — Linux 已拉取（46f1efef，含 VDOT_VV 登记）
- [x] 子模块：difftest（框架 + 配套参考 so）— Linux 已拉取
- [x] 子模块：yunsuan — Linux 子模块已拉取（改动已合入）
- [x] 子模块：fudian / huancun / coupledL2 / openLLC 等 — Linux 已拉取
- [x] NEMU（`OpenXiangShan/NEMU`，difftest 配套分支）— Linux master + vdot 已构建（08 §3.2/§3.3）
- [ ] Spike（参考模型备用）
- [ ] DRAMsim3（内存仿真，B1 评估可选）
- [ ] riscv-tests / riscv-vector-tests（回归用例）
- [ ] nexus-am（应用程序框架，Hello XiangShan 用，可选）

### 2.3 验证 / 评估工具

- [ ] difftest 流程（`emu --diff riscv64-nemu-interpreter-so`）
- [ ] Verilator（emu）— 已具备；VCS 为商业可选
- [ ] 随机激励生成器（可复用本仓库 `tools/verification-driver`）
- [ ] 波形查看（GTKWave / WaveDrom，可选）
- [ ] B3：OpenROAD + 开源 PDK（SkyWater 130nm / ASAP7）— 开源首选
- [ ] B3：Synopsys Design Compiler + Primetime + 工艺库 — 商业备选（需 license）

### 2.4 软件应用栈（B2 演示）

- [ ] 小模型 INT8 权重（≤2B，如 Qwen1.5-1.8B-INT8 / Llama-3.2-1B）
- [ ] 推理框架：llama.cpp / vLLM / 自研 kernel
- [ ] 量化/打包工具（llama.cpp 内置或自研打包脚本）
- [ ] Python：numpy / PyTorch（权重处理用，可选）

---

## 3. 按待办任务的环境就绪检查（勾选对照）

### 3.1 P0-1 NEMU 实现 vdot.vv（软件组）—— ✅ 已完成

- [x] 环境：Linux 主机（或本机装 riscv gcc）—— 本机 macOS 已装 riscv64-elf-gcc 15.2.0
- [x] NEMU 源码获取（codeload tarball → `xiangshan-code/NEMU`）
- [x] `riscv64-elf-gcc` 可用
- [x] NEMU 可编译（`make` 通过，riscv64-nemu-interpreter 生成）
- [x] vdot 编码表（funct6=111001）已备
- [x] NEMU 中注册 vdot 解码 + 实现语义（4×INT8→INT32 累加）
- [x] 指令级测试通过（单次 lane0=0x0a，累加=0x14，见 08 §2.4/§3.1）

### 3.2 P0-2 完整 clone 整机编译（硬件组）—— ✅ 已完成（2026-08-15，详见 08 §3.2）

- [x] Linux 主机 ≥16GB（推荐 32GB）已就绪（实际 62G / 80 线程）
- [x] 手动工具链装齐：openjdk-17、Verilator 5.028（源码构建，apt 版 4.220 过旧）、mill 0.12.3、flex/bison、help2man、libsdl2-dev、libzstd-dev、libsqlite3-dev、gcc-riscv64-unknown-elf
- [x] 完整 git clone --recursive 成功（11 个子模块 + 嵌套，约 1.5GB）
- [x] mill xiangshan.compile 通过（383 个 Scala 源）
- [x] make sim-verilog 通过（2022 个 .sv，含 VdotU.sv/Vdot64b.sv）
- [x] make emu 通过（build/emu 118MB）
- [x] 冒烟：emu --no-diff 跑通 vdot 裸机测试（HIT GOOD TRAP），NEMU 指令级测试 trap code 20
- [x] 合并 vdot 改动（6 处修改 + 1 新文件主仓库 / 1 修改 + 2 新文件 yunsuan）
- [x] （已解决）difftest ABI 对齐：改用 master NEMU + 官方 riscv64-xs-ref_defconfig，接口匹配，0 失配基线达成

### 3.3 P1 回归 / 随机差分（验证组）

- [x] emu 可用（3.2 完成，含 EMU_TRACE=1 波形版）
- [x] NEMU golden 已实现（3.1 完成）
- [x] riscv-tests 已拉取（标量可构建）
- [x] difftest 冒烟通过（0 失配基线，trivial 208 条指令）
- [x] vdot 差分测试（发现 RTL oldVd 累加 bug；真根因 = `vmaInsts` 缺 `VDOT_VV`，修复后 4/4+24/24 通过，见 08 §3.3；早期波形结论受 VCD id 串扰污染，不可靠）
- [x] 复测脚本就绪（`vdot-software/verif/vdot_diff_check.sh` + `P1_DIFF_CHECKLIST.md`，08 §2.5）
- [x] vdot 差分复测（2026-08-18）：修复（`vmaInsts` 加 `VDOT_VV`）后 emu 重建，固定 4/4 + 随机 24/24 全部 GOOD TRAP（08 §3.3）
- [x] 随机激励生成器就绪（macOS gcc 生成随机用例 → Linux emu 差分）
- [ ] vdot 指令级测试矩阵扩展 + riscv-tests 向量回归（05 §2/§3，安全回归）

### 3.4 P2 LLVM / intrinsic / kernel（编译器组、软件组）

- [ ] riscv gcc 可用（intrinsic 主路径；Linux 环境需装 `gcc-riscv64-unknown-elf`）
- [ ] （可选）LLVM 源码 + RISC-V 后端构建
- [x] intrinsic 头文件 + 打包器（`vdot-software/include/xkhmvdot_intrin.h` + `tools/weight_packer.c`，自测通过，08 §2.5）
- [x] INT8 GEMM kernel v1（`vdot-software/kernels/gemm_i8.*`，3 seed 自测通过，08 §2.5）
- [ ] 注意力 kernel v1 + B0/B1 微基准（待 RTL 差分闭环后在 emu/RTL 实测）

### 3.5 P3-B2 端到端 LLM 演示（软件组）

- [ ] 小模型权重 + 量化工具
- [ ] 推理框架（llama.cpp 等）
- [ ] 短 prompt 演示 + per-token 数据（NEMU 或 emu）

### 3.6 P3-B3 面积 / 功耗 / 时序（硬件组）

- [ ] OpenROAD + 开源 PDK 安装（或商业 DC/PT + license）
- [ ] vdot 单元独立综合（目标 ≤ 全核面积 0.5%）
- [ ] 功耗报告（波形回标 switching activity）
- [ ] 时序检查（目标频率假设 1–2GHz @28nm，注明口径）

---

## 4. 环境准备总览（一页速查）

| 类别 | 项 | 状态 | 备注 |
| --- | --- | --- | --- |
| 硬件 | Linux x86_64 主机 ≥32GB | [x] | ✅ P0-2 已在其上完成（08 §3.2） |
| 硬件 | 本机 Mac 8GB | [x] | 代码/单测/文档 |
| 软件 | JDK11 + Verilator + sbt | [x] | 已具备 |
| 软件 | mill + riscv gcc | [~] | mill 已具备（Linux 整机编译用）；riscv gcc 用于 intrinsic 主路径，待装 |
| 软件 | 完整 XiangShan + 子模块 | [x] | ✅ Linux 递归 clone 完成（08 §3.2） |
| 软件 | NEMU + difftest + DRAMsim3 | [x] | ✅ master NEMU + 官方 defconfig，ABI 已对齐（08 §3.3） |
| 软件 | riscv-tests / vectors | [~] | 已拉取（标量可构建）；向量用例回归待 P1 |
| 软件 | intrinsic/打包器/GEMM kernel | [x] | ✅ vdot-software 已交付并自测（08 §2.5） |
| 软件 | OpenROAD + PDK（B3） | [ ] | 开源首选 |
| 软件 | LLVM（P2，可选） | [~] | 可降级 intrinsic（已留 XKHMVDOT_ASM 开关） |
| 软件 | 小模型 + 推理框架（B2） | [ ] | GPU 可选 |

---

## 5. 下一步行动（按优先级）

1. [x] ~~开通 Linux 主机（8c/32GB/200GB，Ubuntu 22.04）~~ — ✅ 已就绪（80 线程/62G，08 §3.2）
2. [x] ~~在 Linux 上跑通工具链~~ — ✅ mill 0.12.3 + JDK17 + Verilator 5.028 + riscv gcc + NEMU + difftest 全就绪
3. [x] ~~完整 clone + `make emu` 冒烟~~ — ✅ 已完成（08 §3.2）
4. [x] ~~NEMU 实现 vdot~~ — ✅ 已完成并实测（08 §2.4）
5. [x] ~~difftest → 回归 → 随机差分~~ — ✅ vdot 固定 4/4 + 随机 24/24 通过（08 §3.3）；riscv-tests 向量回归待做
6. [ ] kernel / intrinsic → B0/B1 基准（RTL 已闭环，可启动）
7. [ ] B2 演示、B3 综合（OpenROAD 开源路径）

---

## 附：本机已具备项复现命令

```bash
java -version        # 11.0.23 ✅
verilator --version  # 5.041 ✅
sbt --version        # 可用 ✅
node --version       # 可用 ✅
# yunsuan 单测（已验证通过）
#   sbt 'testOnly yunsuan.vectortest.mac.Vdot64bSpec'   （chisel 6.7.0 scratch 工程）
```

---

## 附 B：macOS（本机）可支持步骤清单

> 实测环境：macOS 26.3 / Apple M2 / 8GB / 30Gi；JDK11 ✅、Verilator 5.041 ✅、sbt ✅、node ✅、brew ✅。
> 网络：`github.com` 直连 ✘，但 `codeload.github.com` / `raw.githubusercontent.com` / `api.github.com` / Maven Central ✅（tarball 拉取已实测可行）。

| # | 步骤 | 支持度 | 说明 |
| --- | --- | --- | --- |
| 1 | 代码编写与审阅（VdotU / Vdot64b / FuType 等） | ✅ | 本机主业，无环境依赖 |
| 2 | yunsuan 单元测试（chisel 6.7.0） | ✅ 已验证 | `sbt 'testOnly yunsuan.vectortest.mac.Vdot64bSpec'` |
| 3 | 编码核对 / 冲突扫描脚本 | ✅ | python3 |
| 4 | 文档编写 + 文档站构建 | ✅ | `./build-docs.sh` |
| 5 | 源码 tarball 拉取（NEMU / 子模块 / 工具链源码） | ✅ 已验证 | `curl -L https://codeload.github.com/...tar.gz` |
| 6 | NEMU vdot 语义代码编写 | ✅ | 编译验证依赖 riscv gcc 或 Linux |
| 7 | 安装 RISC-V bare-metal 工具链 | 🟡 可尝试 | `brew install riscv64-elf-gcc riscv64-elf-binutils`（ghcr bottle 需实测） |
| 8 | NEMU 宿主程序本机编译 | 🟡 需适配 | NEMU 官方以 Linux 为主，macOS 需少量 Makefile 适配 |
| 9 | 指令级测试用例编写（riscv-tests 风格） | ✅ 可写 | 运行需 riscv gcc 编译（见 7） |
| 10 | INT8 GEMM / 注意力 kernel 代码编写 + native 验证 | ✅/🟡 | 算法正确性本机可验；处理器实测需 emu |
| 11 | B0/B1 微基准（native 版） | 🟡 | 仅算法/数值层面，非处理器实测 |
| 12 | 波形查看 GTKWave | 🟡 可装 | `brew install gtkwave` |
| 13 | 全核 `make emu` / difftest / riscv-tests 回归 | ❌ | 8GB 内存 + 官方流程仅 Linux，建议云主机 |
| 14 | B2 LLM 端到端演示 | ❌ | 需 Linux + ≥16GB |
| 15 | B3 综合（OpenROAD / DC） | ❌ | 官方 flow 基于 Linux |

**结论**：macOS 本机承担 **代码、单测、源码获取、文档** 四类工作（步骤 1–6 完全可行）；
RISC-V 工具链/NEMU 编译可尝试（步骤 7–8）；**全核仿真、回归、评估必须转 Linux 主机**（步骤 13–15，见 §1.1）。
