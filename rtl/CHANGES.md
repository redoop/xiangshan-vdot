# Chisel RTL 实现补丁（vdot.vv）

本目录镜像 vdot 指令在香山昆明湖 V2（kunminghu-v2）上的 Chisel/Scala RTL 实现改动。
源码按上游结构完整复制（rtl/yunsuan = yunsuan 子模块，rtl/xs-kunminghu-v2 = 香山主仓库），每个文件含 vdot 相关改动。

## 一行总结

一条 vdot.vv（funct6=111001, funct3=010, OP-V）指令的完整硬件链路：
编码登记 -> 解码表映射 -> FuType/FuConfig -> 执行单元（Vdot64b + VdotSrcTypeModule + Mgu）-> 调度/写回。

---

## yunsuan 子模块

### yunsuan/encoding/Opcode/VdotOpcode.scala（新增）
```scala
object VdotOpcode { def width = 3; def vdot = "b000".U(width.W) }
```
子 opcode 空间（3-bit），当前仅 vdot=b000，预留扩展。

### yunsuan/package.scala（修改 — 新增 VdotType）
9-bit OpType（vs2/vs1/vd 符号各 1 位 + format 1 位 + opcode 3 位，pad 2 位），
含 dummy/getOpcode/getFormat/符号提取/getSrcVdType（vs1/vs2 固定 e8, vd 固定 e32）。

### yunsuan/vector/vectorIMAC/Vdot64b.scala（新增）
64-bit 点积核心，2 级流水（fire 锁存 vs1/vs2/oldVd -> 组合点积 -> fireS1 锁存结果）：
- 8 个 8x8 符号乘法（16-bit 积）
- 4 组部分和（17-bit，带进位加）-> 2 组 lane 和（18-bit）
- 每 lane (oldVdLanes(j) +& laneSum(j).pad(32).asUInt)(31,0) —— wrap-around 累加
- 关键：laneSum.pad(32) 做符号扩展（SInt.pad），避免 asUInt 零扩展损坏负数结果

### yunsuan/src/test/scala/vector/VectorALU/Vdot64bSpec.scala（新增）
单元测试：4 固定用例 + 32 随机用例，与 Vdot64bRef 参考模型比对。

---

## 香山主仓库（1 新文件 + 7 修改）

### xiangshan/backend/fu/wrapper/VdotU.scala（新增）
执行单元 wrapper：
- VdotSrcTypeModule：vs1/vs2 固定 e8、vd 固定 e32，vsew =/= VSew.e32 -> illegal
- VdotU：VecDataSplitModule x3 + 2x Vdot64b + Mgu(128)；outEew=e32；mask/tail/vstart 全走 Mgu

### xiangshan/backend/fu/FuType.scala（修改）
末尾追加 val vdot = addType(name = "vdot")（保持 one-hot ID 稳定）；vecOPI 加入 vdot。

### xiangshan/backend/fu/FuConfig.scala（修改）
- needVecCtrl / isVecArith：vdot 加入
- VdotCfg：仿 VimacCfg，piped / writeVecRf / writeV0Rf / latency=CertainLatency(2) / destDataBits=128
- VecArithFuConfigs：加入 VdotCfg

### xiangshan/backend/decode/Instructions.scala（修改）
新增 object Vdot { def VDOT_VV = BitPat("b111001???????????010?????1010111") }

### xiangshan/backend/decode/VecDecoder.scala（修改）
VDOT_VV -> OPMVV(T, FuType.vdot, VdotType.vdot, F, T, F, UopSplitType.VEC_VVV)；
VEC_VVV（1 uop/lmul，每 uop 产出 128-bit vd 片段，无需 VVW 拆分）。

### xiangshan/backend/decode/VecExceptionGen.scala（修改）
vdotEewIllegal = (VDOT_VV === inst.ALL) && SEW =/= VSew.e32；加入 eewIllegal。

### xiangshan/backend/decode/DecodeUnit.scala（修改 — P1 关键修复）
vmaInsts 列表加入 VDOT_VV（依赖旧 vd 累加指令表）：
这是 vdot 差分 bug 的最终根因修复。新增任何累加型向量指令都必须同步 vmaInsts。

### xiangshan/Parameters.scala（修改）
VFEX0 挂载 VdotCfg（复用 VfRD(0..2)/V0RD/VlRD 读口 + VfWB(0,0) 写口，不新增 VPRF 端口）。

---

## 涉及子模块

- rocket-chip：Instructions.scala 增加 def VDOT_VV（完整环境登记）