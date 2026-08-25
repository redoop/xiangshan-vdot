# 04 软件栈与工具链设计

> 目标：让 vdot 指令从"RTL 能跑"变成"算法能写、程序能编、仿真能验、模型能推理"。本文档定义软件全链路的设计与推进顺序。指令语义以 [02_指令集设计](./02_指令集设计.md) 为准。

---

## 1. 软件栈总览

```
┌──────────────────────────────────────────────────────────────────────┐
│  应用层   LLM 推理（llama.cpp/ggml 或自研轻量栈）· 推荐打分 · 基准测试      │
├──────────────────────────────────────────────────────────────────────┤
│  Kernel 层  INT8 GEMM / GEMV · 注意力 QKᵀ/AV · 量化与反量化 · softmax    │
├──────────────────────────────────────────────────────────────────────┤
│  Intrinsic  __riscv_vdota4_*（LLVM） / 内联汇编 / C fallback 头文件      │
├──────────────────────────────────────────────────────────────────────┤
│  编译器层   LLVM（Xkhmvdot subtarget + intrinsic + MC）· binutils      │
├──────────────────────────────────────────────────────────────────────┤
│  模拟层    NEMU（difftest golden）· Spike · 软件仿真器（开发期）         │
├──────────────────────────────────────────────────────────────────────┤
│  硬件层    昆明湖 V2 RTL（vdot 执行单元，见 03 微架构文档）              │
└──────────────────────────────────────────────────────────────────────┘
```

**推进顺序原则**：模拟层（golden model）先行 → intrinsic/内联汇编让算法组先写 kernel → 编译器支持后置（不影响主路径）→ 最后集成 LLM 推理。

## 2. 模拟层（golden model）—— difftest 正确性的前提

### 2.1 NEMU（香山 difftest 参考模型）

- 仓库：[OpenXiangShan/NEMU](https://github.com/OpenXiangShan/NEMU)。
- 改动点：在 NEMU 的 RVV 指令解码/执行路径（`src/isa/riscv64/` 下 vector 相关文件，如 `vvector.c`/`vector*`）中，按 [02 §4.2] 伪代码实现 8 条指令的语义；注意 NEMU 对 RVV 通常按"伪指令序列/逐元素仿真"实现，vdot 可同样按 32 位元素逐元素模拟。
- **编码注册（关键）**：NEMU 按 RISC-V 规范解码指令，我们的自定义编码（02 §5.2，funct3=010/110 空间）不在规范内，**必须在 NEMU 的解码表/扩展注册处登记**（否则 NEMU 对 vdota4 报 illegal，与硬件 diff 必然失配）。参考 NEMU 对自定义扩展（如 Zvbb 等）的注册方式。
- **一致性要求**：与 RTL 完全一致的行为点（掩码粒度、vstart 非法、尾元素、回绕、illegal 条件 C1–C4）。**差异点清单**必须作为文档随代码交付（见 05 验证方案的"三方一致性矩阵"）。
- **验证方法**：NEMU 单独跑指令级测试套件（见 05）；再通过 difftest 与 RTL 对拍。

### 2.2 Spike

- 用于非香山环境的纯软件验证与 LLVM 测试（`spike --isa=rv64gcv_xkhmvdot` 需扩展名注册）。
- 改动点：`riscv/insns/vdota4*.h` + `riscv/processor.cc` 解码表 + `riscv/riscv.mk.in` 扩展名；或直接按自定义指令扩展机制添加。

### 2.3 软件仿真器（开发期 fallback）

- 一个独立的小工具：读二进制/汇编 → 按 §4.2 伪代码逐元素仿真 → 输出结果与统计。
- 用途：RTL 未就绪时供 kernel/算法组开发与基准预估；作为"第 3 个参考实现"参与随机差分对拍（三方一致：软件仿真 vs NEMU vs RTL）。

## 3. 编译器与汇编器

### 3.1 LLVM（主编译器）

参考实现路径（均已存在于上游，可直接照抄模式）：

| 参考 | 链接/位置 | 借鉴点 |
| --- | --- | --- |
| Zvdot4a8i LLVM 支持 | [llvm/llvm-project#184089](https://github.com/llvm/llvm-project/pull/184089) | SubtargetFeature、intrinsic 声明与命名（`vdota4_vv_i32m*`）、MC 编码 |
| XSMTVDot 支持 | [llvm/llvm-project@6842cc5](https://github.com/llvm/llvm-project/commit/6842cc556222659256b32883bb2b63ff019100e0) | 自定义扩展接入后端的最小改动集 |
| Xsfvdot/Xsfvqmacc（SiFive） | LLVM `RISCVXsfvdot.td` / `RISCVXsfvqmacc.td` | 自定义扩展的 `TargetFeature` + 伪指令 + MC 层 |

改动清单（`llvm/lib/Target/RISCV/`）：

1. `RISCVFeatures.td`：新增 `FeatureXkhmvdot`（及对齐官方时的 `FeatureZvdot4a8i`）。
2. `RISCVInstrInfoV.td`（或新 `RISCVXkhmvdot.td`）：定义 8 条指令的编码（§5.2）、汇编/反汇编语法。
3. `RISCVInstrInfoV.td` + `RISCVInstrInfo.cpp`：intrinsic 声明（`vint32m*_t` 操作数，对齐 [riscv-rvv-intrinsic-doc#422](https://github.com/riscv-non-isa/riscv-rvv-intrinsic-doc/pull/422) 的"int32 输入参数"约定）。
4. `RISCVISelLowering.cpp`：如需要，为 `vp.*`/`llvm.riscv.vdota4` 提供 DAG 模式（v1.0 可只提供 intrinsic，不做自动向量化匹配）。
5. `clang` 侧：`riscv_common_builtins.cpp` + 头文件 `riscv_vector.h` 增加 `__riscv_vdota4_*`。
6. 测试：`test/MC/RISCV/xkhmvdot-valid.s`、`test/CodeGen/RISCV/`、`test/CodeGen/RISCV/rvv-intrinsics/`。

### 3.2 binutils（GAS/objdump）

- `opcodes/riscv-opc.c`：8 条指令的助记符与编码（与 LLVM 一致）。
- `gas/testsuite/gas/riscv/`：汇编 round-trip 测试。
- 若时间有限，binutils 支持可后置：LLVM MC + LLVM 内置汇编器（`clang -c`）已可覆盖开发与测试需求。

### 3.3 Intrinsic 头文件（无编译器支持时的起步方案）

```c
// xkhmvdot_intrin.h —— 不依赖 LLVM 改动的起步实现（内联汇编 + 宏）
#define vdota4_vv_i32m1(vd, vs2, vs1, vl) ({   \
  vint32m1_t _d = (vd), _a = (vs2), _b = (vs1); \
  asm volatile("vdota4.vv %0, %1, %2" : "+vr"(_d) : "vr"(_a), "vr"(_b)); \
  _d; })
```

- 该头文件让算法组**在 RTL/golden 就绪前**即可用 `-march=rv64gcv -mno-... + 内联汇编` 写 kernel。
- 缺点：无寄存器分配优化、无掩码/尾策略封装——仅作为过渡，LLVM intrinsic 落地后替换。

## 4. 量化方案（INT8 部署链路）

### 4.1 量化策略分级

| 级别 | 粒度 | 精度 | 实现成本 | vdot 适配 |
| --- | --- | --- | --- | --- |
| L1 | per-tensor | 一般 | 最低 | 直接支持 |
| L2 | per-channel（权重按输出通道） | 好 | 低 | 权重打包时带 scale 向量 |
| L3 | per-group（如 GPTQ/AWQ 风格 group=128） | 最好 | 中 | 反量化循环中按 group 取 scale |

- v1.0 交付 **L1 + L2**，L3 作为扩展工作（kernel 框架预留）。
- 对称 INT8（权重对称量化 + 激活对称量化）为默认，避免零点偏移计算；非对称（零点）作为 L2.5 可选。

### 4.2 反量化（requantize）与 vdot 的衔接

INT8 GEMM 累加结果为 INT32，需乘 scale 并右移回 INT8/FP32：

```
acc32 = vdota4 累加结果（int32 向量）
# 逐元素: out_fp32 = acc32 * (scale_a * scale_b)  或
#         out_i8   = saturate(acc32 * s + zero) >> shift
# 实现: vwmul 或 vfmul（转 fp32）→ 可选 vnsrl 打包
```

- 反量化指令序列本身也可用 RVV 1.0 完成（`vfmul.vf`/`vmul.vv`+`vsra`），vdot 只负责最重的乘加部分。
- kernel 设计约束：K 维累加必须保持 int32 精度，因此 **K 维内循环全部由 vdota4 承担，反量化移到 K 循环之外**（标准 GEMM 分块做法）。

### 4.3 权重打包布局（与 vdot 数据视图对齐）

- 我们的 vdot 将 32 位元素视为 4 个打包 int8（[02 §4.1]）。因此权重矩阵 B（K×N）在 kernel 前**重排打包**为：每个 32 位字装 K 维方向相邻的 4 个 int8（同一输出通道），即"K 维连续、4 元素一包"。
- 打包在**离线/初始化阶段**完成（权重静态），运行时零开销。
- K 不是 4 的倍数：padding 补 0（对 int32 累加无影响，需在打包器实现）。
- 激活矩阵 A（M×K）按同样布局逐行打包（运行时打包，或用未打包布局 + 加载后 `vnsrl`/`vzext` 组装——性能取舍见 03/05 的基准设计）。

## 5. Kernel 库设计

### 5.1 INT8 GEMM/GEMV microkernel（核心交付）

伪代码（M=1 GEMV 示例，K=4096，N 分块）：

```c
// 输入: x[i8][K] 打包, W[i8][K][N] 打包(4组/32bit), scale_a, scale_b[N]
for (n = 0; n < N; n += 4) {              // 一次处理 4 个输出通道
  vint32m1_t acc = 0;
  for (k = 0; k < K; k += 4) {
    vint32m1_t xv = vle32_v_i32m1(&x_packed[k/4]);   // 4 个 int8 打包
    vint32m1_t wv = vle32_v_i32m1(&W_packed[k/4][n/4]);
    acc = __riscv_vdota4_vv_i32m1(acc, xv, wv, VLMAX); // 一条指令 4 组点积
  }
  // 反量化 + 写回
}
```

收益（理论）：相比 RVV 1.0 基线（`vzext`+`vwmaccu`+加宽+`vnsrl` 组合），**每 4×4 乘加由 ~8–12 条指令降为 1 条 vdota4 + 2 条 vle32**，指令数下降 ≥ 60%，寄存器压力减半（见 05 基准设计，含估算表）。

> ✅ **2026-08-18 emu 实测（见 05 §5.2）**：vdot GEMM（M=N=K=16）RTL 差分通过，指令数 **6,797 vs 标量 C 86,554（12.7× 更少）**，GEMM 净周期 **~8.2× 更快**——理论收益在真实 RTL 上得到验证。

### 5.2 注意力 kernel

- QKᵀ：M=1（单 token），K=hdim（如 128/group），vdot 逐组点积 → fp32 → softmax。
- AV：S×V，vdot 累加 → 输出 token。
- 长序列分段（如 S=4096 时按 chunk 处理）以控制寄存器与延迟。

> ✅ **2026-08-18 emu 实测（S=4, d=16）**：QKᵀ（vdot）+ softmax + PV（vdot）在 RTL 差分通过（GOOD TRAP，**2,665 指令 / 10,088 周期**），softmax 输出与标量参考一致（见 05 §5.2）。
> ⚠️ **RVV FP 局限（已记录）**：kunminghu-v2 RTL 的 RVV 浮点向量运算（vfcvt.f.x.v / vfmul.vv / vfadd.vv / vfdiv.vv）对 **lane≥1 返回 0**（仅 element 0 正确，NEMU 与 RTL 一致）。softmax 当前用**标量**实现规避（QKᵀ/PV 仍用 vdot）；RVV FP 问题另立专项排查。

### 5.3 LLM 推理集成（两种路径，二选一或并行）

| 路径 | 说明 | 工作量 | 推荐度 |
| --- | --- | --- | --- |
| A. 自研轻量推理栈 | 单头/多头注意力 + FFN + 量化 + 一个 ≤2B 模型（如 TinyLlama-1.1B、Qwen2.5-0.5B）的 INT8 权重 | 中 | ★★★（可控、可演示、可复现） |
| B. 接入 llama.cpp/ggml | 在 ggml 的 INT8 量化 GEMM 路径（如 `ggml_vec_dot_q8_0_q8_0` 的向量化版本）中替换为 vdot kernel | 高 | ★★（生态完整，但 ggml 量化格式（q8_0 等）与 vdot 打包格式需适配层） |

- 建议：**路径 A 为主交付**（完整可控、适合竞赛演示），路径 B 作为加分项（若时间允许，对 ggml `q8_0` 格式做 vdot 适配，贡献上游）。

## 6. 测试与工具链打通顺序（依赖图）

```
[1] 软件仿真器（§2.3）─────► [2] NEMU 实现 ─────► [3] difftest 对拍（RTL 就绪后）
        │                          ▲
        ▼                          │
[4] intrinsic 头文件 + kernel 开发 ─┘
        │
        ▼
[5] LLVM 支持（feature/intrinsic/MC）──► [6] binutils（可后置）
        │
        ▼
[7] 基准与 LLM 演示
```

## 7. 交付物清单（软件侧）

| 交付物 | 说明 |
| --- | --- |
| NEMU/Spike patch | 8 条指令 golden model + 测试 |
| 软件仿真器 | 独立参考实现（含 C 头文件） |
| intrinsic 头文件 | 内联汇编起步实现 |
| LLVM patch | Feature + intrinsic + MC + 测试（目标：合入上游） |
| 打包器工具 | INT8 权重打包（含 padding、per-channel scale） |
| GEMM/注意力 kernel | 基准与集成用 |
| LLM 推理演示 | 轻量栈 + 量化脚本 + 复现说明 |
