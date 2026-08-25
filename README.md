# XiangShan Vdot —— 面向边缘 LLM 推理的 RISC-V 自定义向量点积指令

[English](README_EN.md) · [设计论文](docs/vdot-paper.pdf) · [ISA 规格](docs/ISA.md) · [微架构](docs/microarch.md) · [验证方案](docs/verification.md) · [任务 Checklist](docs/checklist.md)

> 在香山昆明湖 V2（kunminghu-v2）超乱序处理器上设计、实现并验证的自定义向量点积指令 **`vdot.vv`**（扩展名 `Xkhmvdot`，语义对齐 RISC-V 官方 `Zvdot4a8i`）。

---

## 1. 项目简介

Transformer 架构是大型语言模型（LLM）与推荐系统的计算基石，其自注意力 QKᵀ/AV 矩阵乘与 FFN 的两次 GEMM 均由海量点积构成。在资源受限的边缘端，INT8 量化（W8A8）是主流部署方案，但 **RVV 1.0 没有"一组 INT8 元素点积累加"的原生指令**，完成一次 INT32 累加需组合拆包/符号扩展/宽乘累加/打包等多条指令，严重拖累收益。

**`vdot.vv`** 一条指令完成 **4×INT8 点积并累加进 INT32 lane**，完整继承 RVV 的掩码/尾元素/vstart/异常语义，与官方 `Zvdot4a8i` 方向一致，官方扩展发布后可平滑迁移。

### 关键成果

| 维度 | 结果 |
| --- | --- |
| INT8 GEMM 指令数 | **12.7× 更少**（6,797 vs 86,554） |
| GEMM 净周期 | **~8.2× 更快** |
| vdot 吞吐 / 时延 | 独立 1.78 cyc/条 · 依赖链 7.02 cyc/条 |
| 端到端 LLM 推理 | TinyGPT 13,615 instr/token（NEMU 验证） |
| 面积开销 | 0.014%（相对全核，远低于 ≤0.5% 目标） |
| 验证 | NEMU / Spike / RTL **三方一致** · 随机差分 280/280 · riscv-tests 63 PASS |

## 2. 目标与特色

- **ISA 设计**：`vdot.vv` 编码 funct6=111001 + funct3=010（OPMVV）+ opcode=1010111；32-bit 元素 = 4 个打包 INT8（小端）；SEW=32 约束；wrap-around 累加。
- **微架构**：紧凑 64-bit 点积核心（8×8×8乘法器 + 加法树 + 32-bit 回绕累加，两级流水，时延 2），以最小侵入集成进 VFEX0 执行单元，复用既有 VPRF 端口。
- **软件栈**：intrinsic 头文件（C 参考/RVV/内联汇编三路径）、INT8 权重打包器、GEMM 微内核、LLVM MC 层补丁。
- **三方验证**：NEMU、Spike、RTL（difftest）三方一致的验证方法学。
- **最小面积**：Yosys + sky130 综合，面积仅占全核 0.014%。

## 3. 仓库结构

```
xiangshan-vdot/
├── software/                 # 软件栈：intrinsic、权重打包器、GEMM kernel、验证脚本
│   ├── include/xkhmvdot_intrin.h
│   ├── tools/weight_packer.c
│   ├── kernels/              # gemm_i8 + b2_tinygpt（LLM 演示）
│   └── verif/                # 差分脚本、随机测试生成器、流水追踪
├── llvm-xkhmvdot/            # G4 LLVM MC 层补丁（vdot.vv 汇编/反汇编支持）
├── spike-golden/             # G7 Spike golden patch + 三方一致验证
├── b3-synth/                 # G6 B3 面积/功耗/时序综合报告
├── docs/                     # 设计文档（ISA/微架构/软件栈/验证/CheckList）
├── vdot-paper.pdf            # 设计论文（TeX 源码见 vdot-paper.tex）
├── README.md                 # 本文档（中文）
└── README_EN.md              # 英文 README
```

## 4. 快速开始

### 4.1 软件栈（无需香山环境即可编译验证）

```bash
# intrinsic 自检（含累加 10→20 与回绕）
gcc -O2 -I software/include -o /tmp/selftest \
    -Dmain=xkhmvdot_selftest_main software/include/xkhmvdot_intrin.h
/tmp/selftest && echo "selftest OK"

# GEMM 自测（随机 M/N/K，含 K%4≠0 padding，vs int64 参考）
gcc -O2 -I software/include -I software/kernels \
    -o /tmp/gemm_test software/kernels/gemm_i8_test.c software/kernels/gemm_i8.c
/tmp/gemm_test 42
```

### 4.2 端到端 LLM 演示（TinyGPT，NEMU）

```bash
# 裸机程序：微型 1 层 Transformer + INT8 量化 + vdot 加速全部 GEMM + 标量 softmax
# 在支持 vdot 的 NEMU 上运行，输出 435,675 指令/32 tokens（13,615 instr/token）
```

### 4.3 硬件验证（需要在香山 emu + difftest 环境）

```bash
# 差分脚本（emu 与 NEMU .so，解析 GOOD TRAP/trap code）
./verif/vdot_diff_check.sh --emu <XiangShan>/build/emu \
                           --diff <NEMU>/build/riscv64-nemu-interpreter-so \
                           --img <vdot_test.bin>
```

## 5. ISA 摘要

| 字段 | 值 | 说明 |
| --- | --- | --- |
| opcode | `1010111` | RVV OP-V 主操作码 |
| funct6 | `111001` | Xkhmvdot 扩展（OP-V 空间空闲，已脚本核对无冲突） |
| funct3 | `010` | OPMVV（与 MAC 族同类） |
| 操作数 | vd, vs1, vs2 | vd 为累加器（读-写） |
| SEW 约束 | 必须 e32 | 否则 illegal instruction |

**语义**：\[ \text{vd}_j \mathrel{+}= \sum_{i=0}^{3} \operatorname{sext8}(\text{vs1}_j[i]) \cdot \operatorname{sext8}(\text{vs2}_j[i]) \;\; (\bmod 2^{32}) \]

## 6. 验证结果

| 层次 | 方法 | 结果 |
| --- | --- | --- |
| L0 指令语义 | Vdot64bSpec 固定 4 + 随机 32 | ✅ 全过 |
| L1 硬件功能 | emu 固定 4 + 随机 280 差分 | ✅ 280/280 GOOD TRAP |
| L2 系统集成 | riscv-tests 回归（Spike 路径） | ✅ 63 PASS |
| L3 真实负载 | GEMM / 注意力 / LLM 演示 | ✅ 全过 |

## 7. 相关链接

- [设计论文 PDF](vdot-paper.pdf) · [TeX 源码](vdot-paper.tex)
- 香山处理器：[OpenXiangShan/XiangShan](https://github.com/OpenXiangShan/XiangShan)
- 官方提案：[Zvdot4a8i / Zvqdotq Ratification Plan](https://riscv.atlassian.net/wiki/spaces/PSXX/pages/766672912)

## 8. 许可证

本项目为 **2026 CIE 全国 RISC-V 高水平创新及应用大赛**作品。代码与文档以研究/竞赛用途提供，参考实现细节见各子目录 LICENSE 与设计文档。

---

> **版权与致谢**：香山处理器由中科院计算所 / OpenXiangShan 开源社区维护。本扩展设计与官方 Zvdot4a8i 提案对齐，感谢香山社区与竞赛组委会。
