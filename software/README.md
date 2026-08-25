# vdot-software — Xkhmvdot 软件工具（P2 起步交付）

> 对应 [04_软件栈与工具链设计](../../2026-CIE-RISC-V-Contest-Application-Track/02_设计方案/04_软件栈与工具链设计.md) §3.3（intrinsic 起步方案）与 §4.3（权重打包）。
> 指令语义以 [02_指令集设计](../../2026-CIE-RISC-V-Contest-Application-Track/02_设计方案/02_指令集设计.md) 为准；当前实现为 `vdot.vv` 单条（funct6=111001），实现细节见 [VDOT_IMPLEMENTATION.md](../VDOT_IMPLEMENTATION.md)。

## 目录

```
vdot-software/
├── include/xkhmvdot_intrin.h   # intrinsic 头文件（C 参考实现 + 汇编路径开关）
├── tools/weight_packer.c       # INT8 权重打包器（K 连续 4×8bit → 32bit 字 + per-channel scale）
├── kernels/
│   ├── gemm_i8.h / gemm_i8.c   # INT8 GEMM 微内核（标量参考 + RVV 加速路径，04 §5.1）
│   └── gemm_i8_test.c          # 自测：随机 M/N/K（含 K%4≠0）vs int64 参考 + 反量化比对
└── verif/
    ├── P1_DIFF_CHECKLIST.md    # P1 差分复测检查清单（对应 05 §2/§3 与 08 §3.3）
    ├── LINUX_RUN_OPERATION.md  # Linux 差分复测操作单（emu 编译完成→复测→回写）
    ├── vdot_diff_check.sh      # 核心差分脚本（emu + NEMU .so，解析 trap code）
    ├── gen_vdot_tests.py       # 随机差分激励生成器（vl/掩码/非法/极值/累加，内嵌期望自校验）
    └── run_random_diff.sh      # 随机差分 runner（生成→汇编→逐例→汇总；--nemu-only 本机可跑）
```

## 1. intrinsic 头文件

`include/xkhmvdot_intrin.h` 提供：

| 接口 | 说明 |
| --- | --- |
| `xkhmvdot_pack4 / unpack4` | 4×INT8 ⇄ 32-bit 打包字（与 02 §4.1 数据视图一致，小端） |
| `vdot_ref(vd, vs1, vs2, vl)` | C 参考实现（golden，与 02 §4.2 伪代码逐条对应，wrap-around） |
| `__riscv_vdot_vv_i32m1(vd, vs1, vs2, vl)` | intrinsic：默认走 C 参考；`-DXKHMVDOT_ASM=1` 且汇编器支持时走内联汇编 |
| `vdota4_vv_i32m1` | 宏别名（对齐 ISA 文档 vdota4 家族命名） |
| `xkhmvdot_selftest()` | 自检：含累加（10→20，与 NEMU/emu 实测 trap code 20 一致）与回绕用例 |

**使用**（kernel 可先开发，无需工具链改动）：
```c
#include <riscv_vector.h>
#include "xkhmvdot_intrin.h"
// vsetvl 必须设 vsew=e32（vdot 约束，02 §4.5 C1）
vint32m1_t acc = __riscv_vdot_vv_i32m1(acc, xs, ws, VLMAX);
```
> ⚠️ 当前 RVV intrinsic（`riscv_vector.h`）与 `__riscv_vdot_*` 需 `-march=rv64gcv`；在 LLVM/binutils 支持 vdot 助记符前，intrinsic 解析为 C 参考实现（功能正确、性能待加速）。

## 2. 权重打包器

`tools/weight_packer.c`（C99，无依赖）：

```bash
gcc -O2 -o weight_packer tools/weight_packer.c
./weight_packer -t                              # 自检（含 padding 与符号位 round-trip）
./weight_packer <K> <N> <in.bin> <out.bin> [scales.bin]
```

- 输入：`K*N` 个 int8（行主序 B[k][n]）；输出：`(K/4)*N` 个 int32 打包字（K 补 0 到 4 倍数）+ 可选 per-channel 对称 scale（max|w|/127）。
- 布局与 vdot 视图一致：每字 = K 维连续 4 个 INT8（小端），GEMM kernel 直接 `vle32` 加载即用。

## 3. GEMM 微内核（kernels/）

`gemm_i8.h/c`：`C[m][n] = Σ_k A[m][k]·W[n][k]`（INT32 回绕累加 + 可选 per-row/per-channel 反量化）。

- **输入布局与打包器一致**：`W` 为 N×K4 打包字（weight_packer 输出，n-major，K4=ceil(K/4)）；`A` 为 M×K 行主序（K 连续，天然适配 4×INT8 视图，尾部不足 4 自动补 0）。
- **标量路径** `gemm_i8_vdot_scalar`：逐 (m,n) 用 dot4（与 vdot 语义逐点一致），任何平台可编译验证。
- **RVV 路径** `gemm_i8_vdot_rvv`（`__riscv_v` 下启用）：每 4 个 m 行为一组、每 k4 步进用 `__riscv_vdot_vv_i32m1` 累加（M%4 尾行回落标量）——供 B0/B1 基准与 RTL 实测用。
- **自测**（已通过，3 个 seed）：`gcc -O2 -Iinclude -Ikernels -o gemm_test kernels/gemm_i8_test.c kernels/gemm_i8.c && ./gemm_test <seed>`（M=13,N=7,K=9，K%4≠0 覆盖 padding；int64 参考防溢出；含反量化比对）。

## 4. P1 差分复测（verif/）

- `P1_DIFF_CHECKLIST.md`：emu 重建完成后的完整复测项（核心差分 → 边界用例 → 随机差分 → 回归），对应 05 §2/§3。
- `vdot_diff_check.sh`：核心差分一键执行（emu + NEMU .so，解析 GOOD TRAP/trap code，预期 0x0a 单次 / 0x14 两次累加）：
  ```bash
  ./verif/vdot_diff_check.sh --emu <NOOP_HOME>/build/emu \
                             --diff <NEMU>/build/riscv64-nemu-interpreter-so \
                             --img <vdot_test.bin>
  ```
- `gen_vdot_tests.py` + `run_random_diff.sh`：随机差分激励（vl 边界/掩码/非法 vsew/vstart≠0/lmul/极值/1-3 次累加，测试内嵌期望自校验，退出码 PASS=0x5A5A / FAIL=0xDEAD）与**定向边界用例**（`--directed`，d01-d12：回绕/全极值/掩码部分命中/尾元素 vta=tu,ta/掩码宽松 vma=ma/非法/混合/4 lane 独立，全 lane 自校验）：
  ```bash
  # 本机可跑（NEMU 独立校验生成器与 golden 一致）——已验证 180 随机+12 定向全过：
  ./verif/run_random_diff.sh --seed 20260817 --cases 60 --outdir /tmp/vdot_rand \
                             --nemu <NEMU>/build/riscv64-nemu-interpreter --nemu-only
  ./verif/run_random_diff.sh --directed --seed 1 --cases 12 --outdir /tmp/vdot_dir \
                             --nemu <NEMU>/build/riscv64-nemu-interpreter --nemu-only
  # Linux 差分（RTL vs NEMU）：
  ./verif/run_random_diff.sh --seed 20260817 --cases 100 --outdir /tmp/vdot_rand \
                             --emu <NOOP_HOME>/build/emu --diff <nemu.so> --parallel 8
  ```
  > ⚠️ 退出码约定：不能把原始 lane0 当退出码——NEMU `nemu_trap` 对 a0==0x100/0x101 有特殊语义（不退出、继续执行）。
  > ⚠️ **RVV 掩码按位索引**（元素 i 用 v0 的 bit i）——mask 数据必须位打包（`0b00000100` = 仅元素 2），不可按元素逐字节发射。

## 5. 下一步（依赖关系）

| 项 | 依赖 | 说明 |
| --- | --- | --- |
| GEMM/注意力 kernel（B0/B1 基准） | 本头文件 + 打包器 | 04 §5.1，与 emu 侧 vdot 差分（P1）并行推进 |
| LLVM 支持（feature/intrinsic/MC） | 04 §3.4 | 落地后 intrinsic 自动切换为汇编路径，性能开启 |
| 测试 | 05 §2 | `xkhmvdot_selftest` 可并入指令级测试框架 |
