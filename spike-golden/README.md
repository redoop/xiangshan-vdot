# Spike golden — vdot.vv 支持补丁（G7）

## 目标
给 Spike（riscv-isa-sim）添加 `vdot.vv`（funct6=111001, funct3=010, OP-V）支持，
作为第二个 golden 参考（NEMU 主 golden，Spike 交叉验证），实现 RTL/NEMU/Spike 三方一致。

## 补丁文件（3 处 + 1 新文件，已对齐 riscv-isa-sim master）

| 文件 | 改动 |
| --- | --- |
| `riscv/insns/vdot_vv.h`（新建） | 复用 `vdot4a_common.h` 的 VQDOT 宏：`vd = (vd + result) & 0xffffffff`，require vsew=e32 |
| `riscv/encoding.h` | `MATCH_VDOT_VV = 0xe4002057`（funct6=111001）+ `MASK_VDOT_VV = 0xfc00707f`（vm 通配）+ `DECLARE_INSN(vdot_vv, ...)` |
| `riscv/riscv.mk.in` | `riscv_insn_ext_zvdot4a` 清单加 `vdot_vv`（随 V 组编译 + 自动生成 decode 表） |

## 编码（与香山 BitPat 一致）
- `MATCH_VDOT_VV = 0xe4002057`：funct6=111001 + funct3=010(OPMVV) + opcode=1010111
- `MASK_VDOT_VV = 0xfc00707f`：vm 位(bit25)通配（同 vdot4a 的 MASK），支持 masked/unmasked
- 校验：`0xe684a557 (vdot.vv v10,v9,v8) & MASK == MATCH` ✓

## 构建
```bash
mkdir build && cd build
export PATH=/tmp/dtc-pkg/usr/bin:$PATH   # dtc 用户级解包（root 缺失时）
../configure && make -j48
```

## 验证（Spike --log-commits 三方一致 ✓）
```
core 0: 3 0x80000030 (0xe684a557) v10 0x0000003a0000002a0000001a0000000a   # lane0=0x0a ✓
core 0: 3 0x80000034 (0xe684a557) v10 0x00000074000000540000003400000014   # 累加 lane0=0x14 ✓
core 0: 3 0x80000038 (0x42a02557) x10 0x0000000000000014                  # vmv.x.s = 0x14 ✓
```
结论：**Spike (0x14) == NEMU (trap 0x14) == RTL (lane0=0x14)** 三方一致。

## 范围说明
- 完成：vdot_vv 编码的 Spike 支持 + 独立运行验证（FESVR 可执行版）
- 可选后续：OpenXiangShan/riscv-isa-sim fork 构建 difftest .so（`riscv64-spike-so`），
  使 emu --diff 可直接用 Spike 参考端（当前 difftest 用 NEMU 为主）
