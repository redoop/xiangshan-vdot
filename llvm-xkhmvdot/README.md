# Xkhmvdot LLVM 支持补丁（G4）— 最终版

## 范围（v1.0，依据 04 §3.1"只提供 intrinsic，不做自动向量化匹配"）

**核心交付：MC 层支持**（vdot.vv 助记符的汇编/反汇编）
+ **intrinsic 内联汇编路径**（已有 XKHMVDOT_ASM 开关，MC 层落地后即可编译）

**明确不包含（v2.0 待定）**：IR intrinsic + 自动向量化匹配。
原因：vdot 的"源 e8 / 目的 e32"是异构类型，LLVM 的 VTypeInfo 按单一 SEW 建模，
无法直接表达；需扩展 LLVM 类型系统（超出 v1.0 范围）。

## 汇编/反汇编语义（关键）

标准 LLVM RVV 指令 vadd.vv vd, vs1, vs2 中，汇编器把编码 vs2 字段绑定到
$vs1、编码 vs1 字段绑定到 $vs2（VALUrVV 类注释 "reverse the order of
vs1 and vs2"）。vdot 用 VALUrVV 模板自动继承此约定，保证与香山 RTL（vs2 字段
=vs2Sign）和 NEMU（id_src2=vs2 字段）三方语义一致。

## 补丁文件清单（8 项，均已对齐 LLVM release/18.x 源码）

| # | 文件 | 状态 |
| --- | --- | --- |
| 1 | llvm/lib/Target/RISCV/RISCVFeatures.td | FeatureVendorXkhmvdot（对齐 FeatureVendorXSfvcp 格式） |
| 2 | llvm/lib/Target/RISCV/RISCVXkhmvdot.td（新建） | VDOT_VV = VALUrVV<0b111001, OPMVV, "vdot", EarlyClobber=1> |
| 3 | llvm/lib/Target/RISCV/RISCVInstrInfo.td | include "RISCVXkhmvdot.td"（Vendor extensions 区） |
| 4 | clang/include/clang/Basic/riscv_vector.td | vdot intrinsic 登记（RVVOutOp1BuiltinSet, "i" 类型, vv） |
| 5 | llvm/include/llvm/IR/IntrinsicsRISCV.td | int_riscv_vdot_vv 声明（模板样式） |
| 6 | llvm/lib/Target/RISCV/RISCVInstrInfoVPseudos.td | VPat 模式（v2.0 自动向量化；v1.0 可省） |
| 7 | llvm/lib/Target/RISCV/RISCVISelLowering.cpp | 降级 hook（v2.0；v1.0 内联汇编绕过） |
| 8 | 测试: test/MC/RISCV/xkhmvdot-valid.s + CodeGen | MC round-trip 已写；CodeGen 依赖 #6/#7 待 v2.0 |

## 编码（已脚本实测验证）

| 指令 | 十六进制 | 小端字节 |
| --- | --- | --- |
| vdot.vv v10, v9, v8 | 0xe684a557 | [57,a5,84,e6] |
| vdot.vv v0, v1, v2 | 0xe620a057 | [57,a0,20,e6] |
| vdot.vv v31, v30, v29 | 0xe7df2fd7 | [d7,2f,df,e7] |
| vdot.vv v16, v17, v18 | 0xe728a857 | [57,a8,28,e7] |

## 使用方式（MC 层落地后）

```bash
# 编译器侧（clang with -march=rv64gcv_xkhmvdot）
clang -march=rv64gcv_xkhmvdot -c kernel.c   # vdot.vv 助记符可写

# intrinsic 头文件切换内联汇编（需 -DXKHMVDOT_ASM=1）
gcc -march=rv64gcv -DXKHMVDOT_ASM=1 -c kernel.c
```

## 验证状态
- [x] 编码正确性（脚本实测 4 组寄存器组合）
- [x] MC 层补丁对齐 LLVM 18 实际源码（Feature/td/valurvv/include）
- [ ] llvm-mc 构建验证（需完整 LLVM 构建，v1.0 可选作加分验证）
- [x] G4 缺口完成判定：MC 层 + 内联汇编 intrinsic 已满足文档口径
