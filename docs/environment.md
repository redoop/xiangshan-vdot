# 09 软硬件环境需求分析（vdot 实现后续任务）

> 本文分析完成 [08_任务完成情况CheckList](./08_任务完成情况CheckList.md) 中 P0–P3 待办所需的**硬件资源**与**软件环境**，并对照本机现状给出差距清单与获取建议。
> 依据：香山官方 xs-env 环境说明（[课程-环境篇](../../xiangshan-course/docs/1-xiangshan-development-environment/1.CHN/Chapter_2_Preparing_and_Understanding_the_Tools.md)）、[06_实施计划与交付物](./06_实施计划与交付物.md)、[05_验证与评估方案](./05_验证与评估方案.md)。
> 更新日期：2026-08-18。

---

## 0. 结论速览

| 待办（08 §4） | 首要瓶颈 | 建议环境 |
| --- | --- | --- |
| P0-1 NEMU 实现 vdot | ~~需 Linux + RISC-V gcc + NEMU 源码~~ | ✅ 已完成（macOS 本机 riscv64-elf-gcc + NEMU 实测，08 §2.4） |
| P0-2 完整 clone 整机编译 | ~~需子模块 + ≥16GB 内存（make emu 全核）~~ | ✅ 已完成（Ubuntu 22.04 x86_64，80 线程/62G，08 §3.2：sim-verilog + emu + RTL 冒烟通过） |
| P1 回归 / 随机差分 | 需 riscv-tests + difftest + emu | ✅ vdot 差分已闭环（固定 4/4 + 随机 24/24 GOOD TRAP，08 §3.3）；riscv-tests 向量回归待做 |
| P2 LLVM / kernel / intrinsic | 需 RISC-V LLVM 交叉编译链 + 仿真 | Linux（8 核+）；intrinsic/打包器/GEMM kernel 已交付（08 §2.5，vdot-software/） |
| P3 B2 端到端 LLM 演示 | 需小模型权重 + 推理框架（可选 GPU） | Linux + GPU（可选） |
| P3 B3 面积/功耗/时序 | 需 EDA 综合工具 + 工艺库 | EDA 工作站 / OpenROAD + 开源 PDK |

> ⚠️ **本机（macOS arm64 / Apple M2 / 8GB / 30Gi 磁盘）定位**：适合**代码编写、yunsuan 单元测试、文档与 ISA 核对**（已验证 Vdot64bSpec 可跑通）。
> 全核 `make emu`、difftest、riscv-tests 回归、B2/B3 评估**不建议在本机运行**（内存与工具链限制），需按 §2 提供 Linux 环境。

---

## 1. 香山标准开发环境（xs-env）构成

### 1.1 官方推荐基线

| 类别 | 工具 | 版本要求 | 本机现状 |
| --- | --- | --- | --- |
| 操作系统 | Ubuntu 20.04+（或 Docker 容器） | 官方脚本基于 Ubuntu | macOS 26.3（arm64）— 不兼容官方脚本 |
| JDK | openjdk-11 | 11 | ✅ 11.0.23 |
| 构建 | mill | ≥1.0.4（chisel 6 工程用 mill） | ✘ 未安装 |
| 硬件描述 | Chisel（Scala 2.13） | 随工程（kunminghu-v2 用 chisel 6.7.0） | ✅ sbt 可解析（已验证） |
| 仿真器 | Verilator | ≥4.204（本机 5.041 ✅） | ✅ 5.041 |
| 商业仿真器（可选） | VCS | — | ✘ |
| RISC-V 交叉编译器 | riscv64-linux-gnu-gcc（apt）或 riscv64-unknown-elf-gcc | 官方 xs-env 用前者 | ✘ 未安装 |
| 参考模型 | NEMU / Spike | difftest 配套版本 | ✘ 未 clone |
| 内存仿真 | DRAMsim3 | xs-env 配套 | ✘ |
| difftest | difftest 子模块（框架 + 配套 so） | 随 XiangShan 子模块 | ✘（tarball 不含子模块内容） |
| 其他系统库 | protobuf/gRPC(python3)、zstd、libsdl2、dtc、flex/bison 等 | 见 1.2 | 部分缺失 |

### 1.2 官方 apt 依赖清单（xs-env install 脚本节选）

```text
proxychains4 vim wget git tmux make g++ clang llvm time curl
libreadline6-dev libsdl2-dev g++-riscv64-linux-gnu openjdk-11-jre zlib1g-dev
device-tree-compiler flex autoconf bison sqlite3 libsqlite3-dev zstd libzstd-dev
python-is-python3 python3-protobuf python3-grpc-tools rsync
```

---

## 2. 硬件资源需求（按任务）

### 2.1 各任务最小 / 推荐配置

| 任务 | CPU | 内存 | 磁盘 | 说明 |
| --- | --- | --- | --- | --- |
| yunsuan 单测（已完成） | 2 核 | 4GB | 5GB | 本机已验证 |
| NEMU 实现 + 单指令测试 | 2 核 | 8GB | 10GB | 编译 NEMU + riscv gcc |
| 全核 `make emu`（Verilator 编译香山） | 8 核 | **16GB（推荐 32GB）** | 50–100GB | Verilator 全核 C++ 生成/编译吃内存 |
| difftest / riscv-tests 回归 | 8 核 | 16GB | +20GB | 每轮回归耗时数小时 |
| 随机差分（≥1 万用例） | 8 核 | 16GB | +10GB | 与 emu 共用环境 |
| LLVM RISC-V 后端开发 | 8 核 | 16GB | +60GB（LLVM 全量构建） | 可降级为内联汇编（04 §3.3） |
| INT8 GEMM/注意力 kernel 微基准 | 4 核 | 8GB | +5GB | 可在 NEMU/Spike 或 emu 上跑 |
| B2 端到端 LLM 演示（≤2B INT8） | 8 核 | 16GB | +20GB | 纯 CPU 推理慢；GPU（≥16GB 显存）可加速 |
| B3 面积/功耗/时序综合 | 8 核 | 32GB | +50GB | 需 EDA 工具 + 工艺库（见 §3） |

### 2.2 推荐硬件形态

- **首选**：Linux x86_64 工作站或云主机（如 8c/32GB/300GB SSD），同时满足 P0-2、P1、P2、P3 大部分任务。
- **备选（本机 + 云混合）**：
  - 本机 Mac：代码编写、yunsuan 单测、文档、编码核对；
  - 云主机（按需开机）：全核编译、回归、difftest、kernel 评估。
- **B3 综合**：EDA 工具通常需 Linux x86_64 + 商业 license；若用开源 OpenROAD，则普通 Linux 工作站即可（需 PDK，见 §3）。

---

## 3. 按待办任务的详细环境需求

### 3.1 P0-1：NEMU 实现 vdot.vv（软件组）—— ✅ 已完成

> 2026-08-15 已在本机（macOS + riscv64-elf-gcc 15.2.0）完成并实测通过（08 §2.4/§3.1），以下为原始规划供复盘。

**所需软件**
- `riscv64-linux-gnu-gcc`（交叉编译 NEMU 参考镜像）或 xs-env 的 NEMU 构建流程；
- NEMU 源码（`OpenXiangShan/NEMU`，difftest 配套分支）；
- gdb/make/sed 等常规工具；
- vdot 编码表（本文档 08 §5：funct6=111001）供 NEMU 解码注册。

**硬件**：任意 Linux（或 macOS 若自行移植 NEMU 构建），8GB 内存即可。

### 3.2 P0-2：完整 clone 整机编译（硬件组）

**所需软件**
- 完整 `XiangShan` 仓库 + **全部子模块**（rocket-chip、difftest、yunsuan、fudian、huancun、coupledL2、NEMU、DRAMsim3 等）；
- mill（chisel 6 构建）、Verilator ≥4.204、RISC-V gcc；
- `make emu` / `make sim-verilog` 流程（课程 Chapter_6）。

> ⚠️ 本工作区 `xiangshan-code/xs-kunminghu-v2` 是**不含子模块内容**的 tarball 副本，仅用于代码级改动与参考；整机编译必须重新 `git clone --recursive` 或从 xs-env 拉取。

**硬件**：≥16GB 内存（推荐 32GB），50–100GB 磁盘，8 核以上可显著缩短编译时间。

### 3.3 P1：decode 单测 + riscv-tests + 随机差分（验证组）—— ✅ vdot 差分已闭环

> 2026-08-18 更新：vdot RTL bug 已定位并修复（`DecodeUnit.scala` `vmaInsts` 加 `VDOT_VV`，08 §3.3），emu 重建后差分复测**固定 4/4 + 随机 24/24 全部通过**。

**所需软件**
- riscv-tests、riscv-vector-tests（或用香山 vectors 用例集）；
- difftest 框架 + 已实现的 NEMU golden（P0-1 依赖）；
- 随机激励生成器（05 §3，可复用本仓库 `tools/verification-driver`、`tools/xiangshan-scenario-wave-test`）；
- Verilator（emu）或 VCS。

**硬件**：与 3.2 相同（同环境复用）。

**已完成**：vdot 固定用例差分（vdot_test / vdot_single_test / vdot_oldvd_src / vdot_src_test 4/4）+ 随机差分（24 用例，随机 INT8 vs1/vs2 + INT32 oldVd）全部 GOOD TRAP。

**待办**：riscv-tests 向量回归（安全回归，Linux gcc 缺 V-asm，需 macOS 交叉编译或工具链升级）。

### 3.4 P2：LLVM 支持 / intrinsic / kernel（编译器组、软件组）

**所需软件**
- LLVM 源码（RISC-V 后端，如需完整功能支持则从源码构建，耗时长）；
- 或先用 `riscv64-linux-gnu-gcc` + 内联汇编实现 intrinsic（04 §3.3 主路径，LLVM 降级为加分项）；
- Python 打包/量化工具链（numpy、PyTorch 可选，用于权重打包）。

**硬件**：8 核 + 16GB + 60GB（仅 LLVM 全量构建需要大磁盘；走 intrinsic 路径可减至 10GB）。

### 3.5 P3-B2：端到端 LLM 推理演示（软件组）

**所需软件**
- 小模型（≤2B）INT8 权重 + 推理框架（llama.cpp / vLLM / 自研 kernel）；
- 量化工具（llama.cpp 内置或自研打包脚本）；
- NEMU/emu 串通演示（短 prompt）。

**硬件**：纯 CPU 演示 16GB 内存即可；GPU（≥16GB 显存）可大幅提升演示流畅度（可选加分项）。

### 3.6 P3-B3：面积 / 功耗 / 时序评估（硬件组）

**所需软件（二选一）**
- 商业路径：Synopsys Design Compiler（综合）+ Primetime（时序/功耗）+ 目标工艺库（28nm 假设，03 §6）；
- 开源路径：**OpenROAD** + 开源 PDK（如 SkyWater 130nm / ASAP7）→ 面积/时序；功耗用综合后波形回标（03 §6）。

> ⚠️ 商业 EDA 需要 license；开源 PDK（Sky130/ASAP7）可在普通 Linux 工作站跑通完整 flow（推荐先走开源路径获取量级数据，再按需申请商业工具）。

**硬件**：32GB 内存 + 50GB 磁盘；商业综合通常需 EDA 工作站。

---

## 4. 本机与目标环境差距清单

| # | 需求项 | 本机现状 | 差距 | 获取方式 |
| --- | --- | --- | --- | --- |
| 1 | Linux（Ubuntu）环境 | macOS arm64 | 需 Linux 或 Docker | 云主机 / Docker Desktop / WSL（x86 服务器更佳） |
| 2 | mill | ✘ | 安装 | `curl -L https://repo1.maven.org/maven2/com/lihaoyi/mill-dist/1.0.4/mill-dist-1.0.4-mill.sh` |
| 3 | RISC-V gcc | ✘ | 安装 | apt `g++-riscv64-linux-gnu` 或 riscv-gnu-toolchain 自编译 |
| 4 | NEMU / Spike / DRAMsim3 | ✘ | clone + 构建 | xs-env 脚本 / OpenXiangShan 仓库 |
| 5 | 完整 XiangShan（含子模块） | 仅 tarball 副本 | `git clone --recursive` | GitHub（需网络策略允许，报告已确认直连受限→可用 API/镜像） |
| 6 | Verilator | ✅ 5.041 | 无 | — |
| 7 | JDK | ✅ 11 | 无 | — |
| 8 | 内存 ≥16GB | 8GB | 需升级/换机 | 云主机 32GB 实例 |
| 9 | 磁盘 ≥100GB | 30GB 可用 | 需扩充 | 云主机 / 外置盘 |
| 10 | EDA 综合工具 + PDK | ✘ | 安装/申请 | OpenROAD + Sky130/ASAP7（开源）；DC 需 license |

---

## 5. 推荐的分阶段环境落地路径

```
阶段 A（现在，本机即可）
  ├─ 代码编写 / yunsuan 单测（已验证 ✅）
  ├─ NEMU 实现（可先在 Linux 云主机做，或本机装 riscv gcc）
  └─ 文档 / 编码核对

阶段 B（申请云主机 8c/32GB/200GB，Ubuntu 22.04）
  ├─ xs-env 一键环境（或手动：JDK11+mill+verilator+riscv gcc）
  ├─ 完整 clone --recursive → make emu 编译
  ├─ NEMU vdot → difftest 冒烟 → riscv-tests/随机差分回归
  └─ LLVM（可选）/ kernel 微基准

阶段 C（B3 评估）
  ├─ OpenROAD + Sky130/ASAP7 开源流程（普通 Linux 工作站即可）
  └─ 若需商业 EDA（DC/PT），单独申请 license + EDA 工作站
```

---

## 6. 关键结论

1. **软件环境**：核心是 "Linux + JDK11 + mill + Verilator(≥4.204) + RISC-V gcc + NEMU/Spike + 完整 XiangShan(含子模块) + difftest"；本机已具备 JDK/Verilator，其余需在 Linux 环境补齐。
2. **硬件环境**：**内存 ≥16GB（推荐 32GB）是整机 emu 与回归的硬门槛**；8 核 + 200GB 磁盘为推荐基线；B3 综合另需 32GB + EDA/PDK。
3. **本机定位**：macOS M2 8GB 只承担"写代码 + 单测 + 文档"，不承担全核仿真；建议尽快开通 Linux 云主机作为开发/验证主环境。
4. **获取顺序**：先 xs-env（或手动装齐 1.1 清单）→ 完整 clone → make emu → NEMU → difftest → 回归 → 评估；每步依赖上一步（06 §3 关键路径一致）。
5. **✅ Linux 环境已实际落地（2026-08-17/18）**：Ubuntu 22.04 x86_64（2×Xeon E5-2673 v4 / 80 线程 / 62G RAM / 852G 磁盘）已配置 mill 0.12.3 + JDK17 + Verilator 5.028（源码构建）+ riscv64-unknown-elf-gcc + NEMU(master + vdot) + difftest；已完整跑通 sim-verilog + emu + vdot 差分（08 §3.2/§3.3）。
