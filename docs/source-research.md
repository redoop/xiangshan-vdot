# 香山昆明湖 V2（kunminghu-v2）新增 vdot 向量点积指令 —— 代码级集成点调研报告

> 调研目标：为「在香山昆明湖 V2 处理器中新增自定义向量点积指令 vdot（INT8×INT8→INT32 累加）」提供精确的代码级集成点。
> 所有文件路径、类名、行号均来自对源码的真实读取；凡 grep 不到的内容均明确标注"未找到"。

---

## 0. 调研信息

| 项目 | 值 |
|---|---|
| 分支 | `kunminghu-v2`（存在，未回退到 master） |
| commit SHA | `e12436c7cba86b195deec24981976d78bc263661`（2026-08-14，作者 Anzo，消息 "fix(Store): prevent rdataptr from advancing out of order (#6353)"，经 GitHub API 获取） |
| 源码获取方式 | git clone 直连 GitHub 失败（网络限制），改用 GitHub API 获取 commit SHA + codeload tarball 解压至 `/tmp/xs-kunminghu-v2` |
| 关键子模块版本（tarball 不含 submodule 内容，已单独获取 commit SHA 并下载源码） | yunsuan(YunSuan)=`955921186e34bb8915806582a238181a6dc3435c`；difftest=`5d7d90bd6dcd7fede90183fc070f5e35897c6081`；rocket-chip=`46f1efefa1ff431bffe3262e4830bc50316842f4` |
| 主仓库源码根目录 | `/tmp/xs-kunminghu-v2/src/main/scala/xiangshan/` |

> ⚠️ 结构提示：kunminghu-v2 分支的后端目录结构与老版本（如 master 早期）差异较大。向量执行单元**不在** `backend/vector/` 而在 `backend/fu/` 下：
> - 公共框架类（VecPipedFuncUnit / VecNonPipedFuncUnit / Mgu / Mgtu / Bundles）：`backend/fu/vector/`
> - 各向量执行单元的具体实现：`backend/fu/wrapper/`（`vector/` 下的 VIPU.scala / VPerm.scala / VPUSubModule.scala 等旧实现已整体被注释，见下文 A1）
> - 解码：`backend/decode/`；调度/发射：`backend/issue/`；执行块：`backend/exu/`

---

## A. 向量后端总体结构

### A1. 向量执行单元清单

**实际实现（`src/main/scala/xiangshan/backend/fu/wrapper/`）：**

| 文件 | 类 | 功能 | FuType |
|---|---|---|---|
| `VIAluFix.scala:134` | `VIAluFix extends VecPipedFuncUnit` | 向量定点 ALU：加减/移位/饱和/平均/定点缩放（vadd、vssra、vaaddu、vsadd、vwadd、vnsrl、vsext、Zvbb 位操作等），内部例化 yunsuan `VIntFixpAlu64b`×2 | `vialuF` |
| `VIMacU.scala:45` | `VIMacU extends VecPipedFuncUnit` | 向量整数乘/乘累加：vmul/vmulh/vmacc/vnmsac/vmadd/vnmsub/vsmul 及 widening 版（vwmulu/vwmacc/vwmaccu/vwmaccsu/vwmaccus），内部例化 yunsuan `VIMac64b`×2 | `vimac` |
| `VIDiv.scala:15` | `VIDiv extends VecNonPipedFuncUnit` | 向量整数除法/取余（vdiv/vdivu/vrem/vremu），内部例化 yunsuan `VectorIdiv` | `vidiv` |
| `VIPU.scala:88` | `VIPU extends VecPipedFuncUnit` | 向量整数 permute/reduction：vredsum/vredmax/vredmin/vredor/vredand/vredxor、vmv.x.s 等 | `vipu` |
| `VPPU.scala:31` | `VPPU extends VecPipedFuncUnit` | 向量 permute：vslide/vrgather/vcompress/vid/viota 等 | `vppu` |
| `VFALU.scala` | `VFAlu` | 向量浮点加/减/比较/归约（含 vfwadd/vfwsub widening） | `vfalu` |
| `VFMA.scala:16` | `VFMA` | 向量浮点乘加（vfmacc/vfnmacc/vfmsac/vfnmsac、vfmul、vfwmacc 等） | `vfma` |
| `VFDivSqrt.scala` | `VFDivSqrt` | 向量浮点除/开方 | `vfdiv` |
| `VCVT.scala` | `VCVT` | 向量浮点/整数转换 | `vfcvt` |
| `VSet.scala` | `VSet` | vsetvl/vsetvli/vsetivli | `vsetiwi/vsetiwf/vsetfwf` |

**公共框架（`src/main/scala/xiangshan/backend/fu/vector/`）：**

| 文件 | 类 | 功能 |
|---|---|---|
| `VecPipedFuncUnit.scala:58` | `VecPipedFuncUnit extends FuncUnit with HasPipelineReg with VecFuncUnitAlias` | 定长时延流水向量单元基类（所有 piped 向量单元继承），提供 vs1/vs2/oldVd 别名、vstart/vl/vm/vma/vta/vsew/vlmul 等信号提取 |
| `VecNonPipedFuncUnit.scala:13` | `VecNonPipedFuncUnit extends FuncUnit with VecFuncUnitAlias` | 非流水（不确定时延）向量单元基类（VIDiv/VFDivSqrt 用） |
| `Mgu.scala:34` | `Mgu(vlen)` | mask/tail merge unit：按 vstart/vl/eew/vma/vta 把计算结果与 oldVd 合并（active/agnostic/undisturbed），输出 illegal |
| `Mgtu.scala:29` | `Mgtu(vlen)` | mask-generating 指令的 tail 处理 |
| `Bundles.scala` | `VType/VConfig/VSew/VLmul/Vl/Vxrm/Vxsat` 等 | 向量控制 bundle 与常量 |
| `VecSrcTypeModule.scala` | `VecSrcTypeModule` | fuOpType→vs1/vs2/vd 类型（EEW、符号）译码基类 |

> ⚠️ `backend/fu/vector/VIPU.scala`、`VPerm.scala`、`VPUSubModule.scala`、`VIMacU.scala` 中旧实现**整体被注释**（如 `VIMacU.scala:19-75` 全部是 `//`），属历史遗留；当前生效实现全部在 `wrapper/` 下。FuConfig 中 `fuGen` 指向的类均为 wrapper 版（见 A2）。

### A2. 向量执行单元的挂载（ExeBlock / issue 队列 / FUType / 端口）

- **FuType 枚举**：`backend/fu/FuType.scala:11-72`。向量定点 `vecOPI = Seq(vipu, vialuF, vppu, vimac, vidiv)`（127 行）；向量浮点 `vecOPF`（128 行）；vset（129 行）；向量访存 `vldu/vstu/vsegldu/vsegstu`（131 行）。新增 vdot 的 FuType 应加入 `vecOPI` 或新增一个条目并同步 `vecArith/vecAll`（130、133 行）与 `FuConfig.isVecArith`（`FuConfig.scala:181-185`）、`FuConfig.needVecCtrl`（162-165 行）、`needOg2`（190 行）。
- **FuConfig 定义**（`backend/fu/FuConfig.scala:46-78` 的参数表；各向量单元的配置在 528 行之后）：
  - `VimacCfg`（547-564 行）：`fuType=vimac`，`fuGen = ... new VIMacU(cfg)(p)`，`srcData = Seq(Seq(VecData(), VecData(), VecData(), V0Data(), VlData()))`（vs1/vs2/oldVd/v0/vl 共 5 个源），`piped=true`，`writeVecRf=true`，`writeV0Rf=true`，`writeVxsat=true`，`needSrcVxrm=true`，`latency=CertainLatency(2)`，`vconfigWakeUp=true`，`maskWakeUp=true`，`destDataBits=128`，`exceptionOut=Seq(illegalInstr)`。
  - 其余：`VialuCfg`（528-545，latency=1）、`VidivCfg`（566-581，非流水 UncertainLatency）、`VppuCfg`（583-598，latency=2）、`VipuCfg`（600-616，latency=2）、`VfaluCfg`（618-636，latency=1）、`VfmaCfg`（638-655，latency=3）、`VfdivCfg`（657+）。
- **执行块/调度参数**（`Parameters.scala:445-465`，`vfSchdParams`，SchedulerType=`VfScheduler`）：

```
IssueBlock0 (numEntries=16, numEnq=2, numComp=12):
  VFEX0 = ExeUnitParams("VFEX0", Seq(VfmaCfg, VialuCfg, VimacCfg, VppuCfg),
                        wb = Seq(VfWB(port=0,0), V0WB(port=0,0)),
                        rd = Seq(Seq(VfRD(0,0)),Seq(VfRD(1,0)),Seq(VfRD(2,0)),Seq(V0RD(0,0)),Seq(VlRD(0,0))))
  VFEX1 = ExeUnitParams("VFEX1", Seq(VfaluCfg, VfcvtCfg, VipuCfg, VSetRvfWvfCfg),
                        wb = Seq(VfWB(port=0,1), V0WB(port=0,1), VlWB(port=1,0), IntWB(port=1,1), FpWB(port=0,1)),
                        rd = Seq(Seq(VfRD(0,1)),Seq(VfRD(1,1)),Seq(VfRD(2,1)),Seq(V0RD(0,1)),Seq(VlRD(0,1))))
IssueBlock1 (numEntries=16, numEnq=2, numComp=12):
  VFEX2 = Seq(VfmaCfg, VialuCfg)   wb=VfWB(1,0)+V0WB(1,0)   rd=VfRD(3,0)(4,0)(5,0)+V0RD(1,0)+VlRD(1,0)
  VFEX3 = Seq(VfaluCfg)            wb=VfWB(2,1)+V0WB(2,1)+FpWB(1,1)  rd=VfRD(3,1)(4,1)(5,1)+V0RD(1,1)+VlRD(1,1)
IssueBlock2 (numEntries=10, numEnq=2, numComp=6):
  VFEX4 = Seq(VfdivCfg, VidivCfg)  wb=VfWB(3,1)+V0WB(3,1)   rd=VfRD(3,2)(4,2)(5,2)+V0RD(1,2)+VlRD(1,2)
```

  行号：`Parameters.scala:449`（VFEX0/VFEX1）、`453-454`（VFEX2/VFEX3）、`457`（VFEX4）、`451/455/458`（IQ 参数）。`numPregs = vfPreg.numEntries`（460 行）、`rfDataWidth = 128`（463 行）。
- **ExeUnit 参数模型**：`backend/exu/ExeUnitParams.scala:16-27`（`name/fuConfigs/wbPortConfigs/rfrPortConfigs/copyWakeupOut/copyDistance/fakeUnit`），派生端口/写回属性在 36-121 行。
- **ExuBlock 与 Backend 实例化**：`backend/exu/ExuBlock.scala:17-23`（`ExuBlock` 由 `SchdBlockParams` 构造，`exus = issueBlockParams.flatMap(_.exuBlockParams.map(x => LazyModule(x.genExuModule)))`）；`backend/Backend.scala:182/187`（`vfScheduler`、`vfExuBlock` 的 LazyModule 实例化），`180-187` 同时实例化 int/fp/vf/mem 四类。
- **Dispatch 到 IQ 的选择**：`backend/dispatch/NewDispatch.scala:359-430`，按 fuType one-hot（`fuTypeOH`）查表（`minIQSelAll`）把 uop 分配到能接受该 fuType 的 IQ；多 EXU 共享 fuType 时按队列占用均衡（`IQSort`/`issueQueueCount`）。**新增 FuType 必须保证有某个 vf IQ 的 EXU `canAccept` 它，否则 dispatch 报错**（`ExeUnitParams.canAccept`，`ExeUnitParams.scala:314-316`）。
- **每条向量指令的 uop 拆分**：`backend/decode/DecodeUnitComp.scala:359-568`（VEC_VVV 359、VEC_VVW 487-500：widening 拆 2 个 uop，`lsrc` 同一 vs1/vs2，`ldest`/`vuopIdx` 为 `dest+2i` / `dest+2i+1`）；uop 数由 `UopSplitType` 决定（`UopInfoGen.scala:198-242`，`VEC_VVW → numOfUopWV = 2*lmul`，177-181 行）。**vdot（若按 widening 或固定 2 个 64-bit 输出实现）可复用 VEC_VVW 模式或自定义一个 split 类型**。

### A3. 向量寄存器文件（VPRF）

- **物理寄存器数**：`Parameters.scala:193-197` `vfPreg: VfPregParams(numEntries = 128, numRead = None, numWrite = None)` → VPRF 128 项、每项 128-bit（`VfPregParams` 定义 `backend/regfile/PregParams.scala:37-45`，`dataCfg = VecData()`）。另有 `v0Preg=22`（198-202）、`vlPreg=32`（203-207）。
- **读/写端口数**：不是硬编码，而是由全部 ExeUnit 的 `VfRD(port, priority)` / `VfWB(port, priority)` 配置**自动推导**（`backend/BackendParams.scala:295-306` `getVfRfReadSize/getVfRfWriteSize`，`numRead=None` 时取 `getRdPortIndices(VecData()).size`）。由 A2 的 vfSchdParams 统计：
  - VPRF 读端口 = 6（`VfRD(0..5)`），每端口 128-bit
  - VPRF 写端口 = 4（`VfWB(0..3)`），每端口 128-bit；同一物理端口可共享（priority 分时隙，如 VFEX0 与 VFEX1 都写 `VfWB(0,*)`）
  - V0 读 2 / 写 6；Vl 读 3 / 写 3
  - 打印确认：`backend/datapath/DataPath.scala:34` `println("[DataPath] Vf R(...), W(...)")`；读地址/数据连线 242-243 行；`VfRegFile` 实例化 322-324 行（`vfRfSplitNum = 4`，241 行，128-bit 分 4 bank 写）；Regfile 生成 `backend/regfile/Regfile.scala:360`（`VfRegFile` object）、`162-173`（`numReadPorts = raddr.length`）。
  - **新增 vdot EXU 若占用新的 `VfWB` port（port>3）或新的 `VfRD` port（port>5），会自动增大 VPRF 端口数（面积/时序开销）；尽量复用现有端口。**

---

## B. 向量数据通路参数

| 参数 | 值 | 位置 |
|---|---|---|
| VLEN | 128 | `xiangshan/Parameters.scala:60` |
| ELEN | 64 | `Parameters.scala:61` |
| DLEN/向量数据通路宽度 | 128-bit（`VecData()` 数据宽度） | `backend/datapath/DataConfig.scala:17` `case class VecData() extends DataConfig("vec", 128)` |
| minVecElen | 8 | `Parameters.scala:383` |
| maxElemPerVreg | VLEN/minVecElen = 16 | `Parameters.scala:388` |
| vsew | 2-bit，e8/e16/e32/e64（e64=0b11） | `backend/fu/vector/Bundles.scala:135-139` |
| vlmul | 3-bit，1/8~8（mf8=0b101 … m8=0b011，0b100 reserved） | `Bundles.scala:161-177` |
| vl 位宽 | log2Up(VLEN)+1 = 8 | `Parameters.scala:373` `vlWidth` |
| 解码宽度 DecodeWidth | 6 | `Parameters.scala:149` |
| RenameWidth | 6 | `Parameters.scala:150` |
| MaxUopSize（单指令最大 uop 数） | 65 | `Parameters.scala:154` |
| 向量 IQ 参数 | IssueBlock0/1：numEntries=16, numEnq=2, numComp=12；IssueBlock2：numEntries=10, numEnq=2, numComp=6 | `Parameters.scala:451/455/458` |
| issue 槽占用 | 拆出的**每个 uop 占一个 IQ 槽**；每周期每个 IQ 最多入队 numEnq=2 个 uop；每 EXU 一个出队口（vf 共 5 个出队口） | `issue/IssueBlockParams.scala:20-23`（numEntries/numEnq/numComp）、`143`（numDeq = exuBlockParams.length）、`Scheduler.scala:54`（IssueQueueDeqSum） |
| Vxrm（定点舍入） | 2-bit | `Bundles.scala`（Vxrm） |
| 向量尾/tail 处理 | 由各执行单元内 Mgu 完成（见 D） | `backend/fu/vector/Mgu.scala` |

> 对 vdot 的意义：INT8 输入（e8，每 128-bit 寄存器 16 个元素）、INT32 累加输出（每 128-bit 4 个元素）。若 vdot 是"每 128-bit 的 vs1/vs2 内做 4 组 4×8bit 点积、各得 1 个 32-bit 结果"，则一个 uop 正好产生一个 128-bit vd 片段（4×32bit），**不需要 widening 的 2-uop 拆分**，可走 `UopSplitType.VEC_VVV`（1 uop/lmul）模式。

---

## C. 指令解码路径（新增指令接入点）

### C1. 完整注册链路（以 `vwmaccu.vv` 为例，每一环都给出文件+行号）

1. **指令编码 BitPat**：`vwmaccu.vv = b111100???????????010?????1010111`，定义于 **rocket-chip** 子模块 `src/main/scala/rocket/Instructions.scala:821`（`object Instructions`）。RVV 布局：`[31:25]funct6=111100 [24:20]vs2 [19:15]vs1 [14:12]vm=010 [11:7]vd [6:0]opcode=1010111(OP-V)`。对照 `VADD_VV`（407 行，funct6=000000）。**vdot 需要在这里（或主仓库自建 object）登记一个新 BitPat，占用 OP-V 空间的一个未用 funct6 编码。**
2. **解码表条目**：`xiangshan/backend/decode/VecDecoder.scala:426`
   ```scala
   VWMACCU_VV -> OPMVV(T, FuType.vimac, VimacType.vwmaccu, F, T, F, UopSplitType.VEC_VVW),
   ```
   `OPMVV`（`VecDecoder.scala:77-96`）展开为 `XSDecode(src1=vp, src2=vp, src3=vdRen?vp:X, fu, fuOp, SelImm.X, uopSplitType, xWen=F, vWen=T, ...)`；`XSDecode.generate()`（`DecodeUnit.scala:94-111`）产出 15 列 BitPat（src1/src2/src3/fuType/fuOpType/…/uopSplitType/selImm）。
3. **fuOpType 编码**：`VimacType.vwmaccu` 定义于 yunsuan 子模块 `yunsuan/src/main/scala/yunsuan/package.scala:392`：
   `LiteralCat(0.U(2.W), INT.U, INT.U, INT.U, FMT.VVW, VimacOpcode.vmacc)`（9-bit OpType：vs2 符号/ vs1 符号/ vd 符号 / widening 标志 / 3-bit 子操作码；`VimacOpcode` 见 `yunsuan/encoding/Opcode/VimacOpcode.scala:5-24`，`vmacc=0b010`）。**vdot 需要新增一个 OpType 常量（建议在 VimacType 或新建 VdotType）。**
4. **FuType**：`FuType.vimac`（`backend/fu/FuType.scala:57`）；`VimacType`/`VialuFixType` 等在 `VecDecoder.scala:13` 从 yunsuan import。
5. **执行单元**：fuOpType 下发到 `VIMacU`（`wrapper/VIMacU.scala:45`），由 `VIMacSrcTypeModule`（18-43 行）解码出 srcType/vdType，送 yunsuan `VIMac64b`（`yunsuan/vector/vectorIMAC/VIMac64b.scala:12`）执行；结果经 `Mgu` 合并 mask/tail 后写回。
6. **复杂指令拆分**：`vwmaccu.vv` 标 `UopSplitType.VEC_VVW`（`xiangshan/package.scala:774`），`DecodeUnitComp.scala:487-500` 拆 2 个 uop；`UopInfoGen.scala:214` 给出 uop 数 = 2×lmul。
7. **decode_table 拼接**：`DecodeUnit.scala:806-818`（`XDecode.table ++ FpDecode.table ++ … ++ VecDecoder.table ++ …`）；`DecodedInst.decode(instr, decode_table)`（827 行）完成真值表解码。

### C2. 自定义/预留指令钩子

- **指令级自定义钩子：未找到**。grep `"Custom|custom|XCustom|ext_custom"` 于 decode/issue/exec 目录，命中仅：
  - `backend/decode/DecodeUnit.scala:24` `import freechips.rocketchip.rocket.CustomInstructions._`（死 import，源码中无任何使用点）；
  - `backend/decode/FusionDecoder.scala:197-490` 多处注释提到 "customized internal opcode"（指微架构内部伪指令，如 `szewl1/sh4add/lui32` 融合目标，非用户自定义指令扩展）；
  - `CustomCSRCtrlIO`（`xiangshan/Bundle.scala:622-659`）是 **CSR 自定义控制信号**（预取器/分支预测器/内存子系统开关），与指令集自定义扩展无关，出现在 `DecodeStage.scala:56`、`DecodeUnit.scala:792`。
- **xvld/xvst 等自定义向量访存指令：未找到**（grep "xvld|xvst|XCustom" 无结果）。昆明湖**没有**任何现成的自定义指令实现可模仿，只能按标准 RVV 指令路径新增。
- **结论**：vdot 需要走完整的"编码 BitPat → 解码表 → OpType → FuType → FuConfig → EXU"标准链路，无捷径钩子。

### C3. 未定义编码的 illegal 处理

- 解码默认项：`DecodeUnit.scala:50-61` `decodeDefault`，`selImm = SelImm.INVALID_INSTR` 标记非法；所有未命中解码表的编码落到该项。
- 非法判定：`DecodeUnit.scala:894-917` `exceptionII = decodedInst.selImm === SelImm.INVALID_INSTR || …`；932 行 `decodedInst.exceptionVec(illegalInstr) := exceptionII || …`。
- 向量专属非法检查：`backend/decode/VecExceptionGen.scala:42-303`（`illegalInst = instIllegal || villIllegal || eewIllegal || emulIllegal || regNumIllegal || regOverlapIllegal || vstartIllegal`，294 行）。其中：
  - widening 指令清单 `vdWideningInst`（83-96 行，**含 VWMACCU_VV 等**）→ 决定 EEW/EMUL/对齐检查；**若 vdot 走 widening 语义，必须把 vdot 加进对应清单（vdWideningInst/emul 检查等）**；
  - `vstartIllegal = isVArith && (io.vstart =/= 0.U)`（275 行）：**vstart≠0 的向量指令直接在解码侧判非法**（香山选择不支持部分执行中断）；
  - `villIllegal`（178 行）、reg 对齐/重叠检查（219-292 行）。
- 执行侧兜底：`backend/fu/FuncUnit.scala:253-258`（HasPipelineReg，`vstartIllegal = outVstart =/= 0.U` → exceptionVec(illegalInstr)）与 `VecNonPipedFuncUnit.scala:48-53` 同样处理。
- 上报路径：非法经 `ExeUnit`（`backend/exu/ExeUnit.scala:383`）→ ROB commit 时触发异常（`backend/rob/Rob.scala`，vstart 复位逻辑见 728-743 行）。

---

## D. 向量执行单元的实现模式（新增 vdot 执行单元要模仿的样板）

### D1. 流水基类与握手

- **`FuncUnit`**（`backend/fu/FuncUnit.scala:107`）：所有执行单元基类，`io.in = Flipped(DecoupledIO(new FuncUnitInput(cfg)))`（93 行）、`io.out = DecoupledIO(new FuncUnitOutput(cfg))`（94 行）。输入含 `ctrl`（robIdx/pdest/fuOpType/rfWen/vecWen/v0Wen/vpu 等）与 `data`（src(0..n)/imm/pc 等，`FuncUnitDataInput` 56-65 行）。
- **`HasPipelineReg`**（`FuncUnit.scala:166-288`）：
  - `pipelineReg(init, valid, ready, latency, flush)`（173-212 行）：生成 `validVec/rdyVec/ctrlVec/dataVec` 逐级流水寄存器；`flushVec = validVec.zip(robIdxVec).map(x => x._1 && x._2.needFlush(flush))`（184 行）按 ROB 指针做重定向冲刷；`io.in.ready := fixRdyVec.head`、`io.out.valid := fixValidVec.last`（237-238 行）。
  - 提供 `S1Reg/S2Reg/S3Reg/S4Reg/S5Reg`（279-287 行）与 `SNReg`（270-277 行）辅助打拍。
  - **vstart 非法**（253-258 行）：`outVstart =/= 0.U` → `io.out.bits.ctrl.exceptionVec.get(illegalInstr)`。
- **`VecPipedFuncUnit`**（`backend/fu/vector/VecPipedFuncUnit.scala:58-95`）：固定时延向量单元统一入口；`vs1=inData.src(0)、vs2=src(1)、oldVd=src(2)`（62-66 行）；输出侧用 `ctrlVec.last/dataVec.last`（68-69 行）对齐流水尾部；`outSrcMask`（84-91 行）在 vm=1 时全真、否则取 `data.getSrcMask`（由 data 通路携带的 mask 数据，见 `VecFuncUnitAlias` 48-53 行：`if(!cfg.maskWakeUp) inCtrl.vpu.get.vmask else MuxCase(inData.getSrcMask, needClearMask→全0, vm→全1)`）。
- **`VecNonPipedFuncUnit`**（`backend/fu/vector/VecNonPipedFuncUnit.scala:13-56`）：不确定时延（VIDiv/VFDivSqrt），`outCtrl/outData = DataHoldBypass(io.in.bits.*, io.in.fire)`（23-24 行）保持输入直到出结果。

### D2. 多周期流水与写回（以 VIMacU / VIAluFix 为例）

`wrapper/VIMacU.scala:45-151` 结构（vdot 执行单元的直接模板）：
1. **输入拆块**：`VecDataSplitModule(dataWidth=128, dataWidthOfDataModule=64)`×3（61-63 行）把 vs2/vs1/oldVd 拆成 64-bit 块；`vimacs = Seq.fill(2)(Module(new VIMac64b))`（64 行）——128-bit 数据通路 = 2×64-bit 运算核心。
2. **运算核心接口**（yunsuan `VIMac64b.scala:12-30`）：`io.fire`、`io.info(VIFuInfo：vm/ma/ta/vlmul/vl/vstart/uopIdx/vxrm)`、`io.srcType(2×4bit)/io.vdType`、`io.vs1/vs2/oldVd(64bit)`、`io.highHalf/isMacc/isSub/widen/isFixP`，输出 `io.vd(64bit)/io.vxsat(8bit)`。连接见 `VIMacU.scala:99-121`。
3. **VIMac64b 内部流水**：3 段组合+2 级寄存器（Booth 编码+Wallace 树 → `wallaceOut_mid_reg`（168 行）→ 第二级部分归约+加法（178-201 行）→ `walOut` 寄存器（208 行）→ 第三级舍入/饱和/减法（216-295 行））。所以 `VimacCfg.latency=CertainLatency(2)`（`FuConfig.scala:559`）与内部两级寄存器对应——**latency 与运算核心寄存器级数必须一致**。
4. **结果合并与 mask/tail**：`mgu = Module(new Mgu(dataWidth))`（65 行），`outVd = Cat(vimacs.reverse.map(_.io.vd))`（126 行）；Mgu 输入 `info.ta/ma/vl/vlmul/valid/vstart/eew/vsew/vdIdx(=vuopIdx)/narrow/dstMask`（132-146 行）；`io.out.bits.res.data := mgu.io.out.vd`（148 行）；`vxsat`（149 行）；`illegal`（150 行）。
5. **ExeUnit 层写回**：`backend/exu/ExeUnit.scala:312-387`——`OutresVecs` 处理 extraLatency 打拍（312-319 行）；多 FU 输出 one-hot 仲裁（322-323 行）；按 `writeVecRf/writeV0Rf/...` 生成 `outDataVec/outDataValidOH`（337-362 行）并 `Mux1H` 出 `io.out.bits.data`（371 行）及各 wen（374-378 行）。写回经 `WbArbiter`/`WbFuBusyTable`（`backend/datapath/WbArbiter.scala`、`WbFuBusyTable.scala`）进 VPRF 与唤醒网络。

### D3. vstart / 掩码 / 尾元素处理要点

| 机制 | 位置 | 说明 |
|---|---|---|
| vstart 来源 | `backend/decode/DecodeStage.scala:76/121`（`io.vstart` → `dst.io.enq.vstart`）；`DecodeUnit.scala:1071` `decodedInst.vpu.vstart := io.enq.vstart` | vstart 来自 CSR 侧（vstart CSR 值），随指令逐级下发 |
| vstart≠0 行为 | `DecodeUnit.scala:1065-1069`（`vstartIsNotZero` → `isDependOldVd`，vstart≠0 时读旧 vd 且 `DecodeUnitComp.scala:209-210` blockBackward/flushPipe，即不投机）；`VecExceptionGen.scala:275` 直接判非法 | 香山不支持 vstart 部分执行语义，vstart≠0 的向量算术指令报 illegalInstr |
| mask（v0.t） | `VecFuncUnitAlias.srcMask`（`VecPipedFuncUnit.scala:48-53`）：vm=1→全真；否则用 data 通路携带的 mask（`inData.getSrcMask`，来自 V0 寄存器经 RFReadArbiter 读回）；Mgu 内 `maskUsed = VecDataToMaskDataVec(in.mask, realEw)(vdIdx)`（`Mgu.scala:60-61`） | 每个 uop 只取自己 vd 片段对应的 mask 位 |
| tail（vta/vma） | `Mgu.scala:63-94`：`ByteMaskTailGen` 以 `begin=vstart、end=vl` 生成 `activeEn/agnosticEn`；`resVecByte(i) = MuxCase(oldVdVecByte(i), activeEn→新结果, agnosticEn→全1)` | tail-agnostic 填 1，tail-undisturbed 保留 oldVd；mask-off 元素同样处理（ma） |
| 结果写回使能 | Mgu 输出 `io.out.active`（用于 vxsat 屏蔽）与 `illegal`；`VIAluFix.scala:358-360` 用 `outVstartGeVl` 特判 vstart≥vl 时保持 oldVd | 已在样板中实现，vdot 复制即可 |

---

## E. Difftest 与 golden model 接口

- **difftest 子模块接口定义**：`difftest/src/main/scala/Bundles.scala:66-93` `class InstrCommit(numPhyRegs)`：`skip/isRVC/rfwen/fpwen/vecwen/v0wen/wpdest/wdest/otherwpdest/pc/instr/robIdx/lqIdx/sqIdx/isLoad/isStore/nFused/special`。difftest 侧实例化 `DiffInstrCommit`（`difftest/src/main/scala/Difftest.scala:242`）。
- **硬件侧上报**：`xiangshan/backend/rob/Rob.scala:1543-1582`
  - `DifftestModule(new DiffInstrCommit(diffMaxPhyRegs), delay = 3, dontCare = true)`；`diffMaxPhyRegs = Seq(MaxPhyRegs, 2*(V0PhyRegs+VfPhyRegs)).max`（1542 行）；
  - `difftest.vecwen := … basicDebug.vecWen`（1552 行）、`wpdest := basicDebug.pdest`（1554 行）、`wdest := … basicDebug.ldest`（1555 行）；向量寄存器以"128-bit 拆成 2 个 64-bit"上报 `otherwpdest`（1558-1568 行，索引 = 2×(V0PhyRegs + 物理号) + {0,1}）。
- **架构状态另报**：`backend/datapath/DataPath.scala:329` `DiffPhyVecRegState(vecDiffNumPregs)`（V0+Vf 全部物理寄存器、每 128-bit 拆 2×64-bit，281-294 行）；`Rob.scala` 的 commit 数据与 NEMU 逐条比对。
- **NEMU 侧需要做的**（简述，未 clone NEMU）：NEMU（`github.com/OpenXiangShan/NEMU`）是 RISC-V 指令模拟器，difftest 逐条比对 PC+instr+写回寄存器；新增 vdot 只需在 NEMU 的指令执行层实现 vdot 的语义（按 vstart/vl/mask/tail 规则计算并写 vd），**无需修改 difftest 接口**（bundle 字段已覆盖任意向量写回）。注意编码需与硬件一致（NEMU 通常按 RISC-V 规范解码，自定义编码需在 NEMU 解码表/`riscv_extension` 中注册，否则 NEMU 会报 illegal 导致 diff 失败）。

---

## F. vdot / dot / macc / vfdot 相关现状

| 关键词 | 结果 | 证据 |
|---|---|---|
| `vdot`（主仓库 + yunsuan） | **未找到** | `grep -rni "vdot" /tmp/xs-kunminghu-v2/src/ /tmp/yunsuan/src/` 无任何输出 |
| `dot` / `DotProduct` | **未找到**（仅误匹配 `idot` 类无关词，无实质实现） | 同上 grep |
| `vwmacc / vwmaccu`（整数乘累加） | 存在，见 C1 全链路 | `VecDecoder.scala:424-426,472-476`；`wrapper/VIMacU.scala`；`yunsuan/package.scala:391-395`；`yunsuan/vector/vectorIMAC/VIMac64b.scala` |
| `vfdot` | **未找到** | grep 无结果 |
| `vfwmacc / vfwmacc.vv`（浮点 widen 乘加） | 存在（可作为 fp 点积的指令级替代，非点积指令） | `VecDecoder.scala:602` `VFWMACC_VV -> OPFVV(..., FuType.vfma, VfmaType.vfmacc_w, ..., VEC_VVW)`；`yunsuan/package.scala:495` `vfmacc_w` |
| fp16 支持 | 部分支持：向量浮点除法支持 fp16（`yunsuan/vector/VectorFloatDivider.scala:100-147`），标量/向量 FMA 有 fp16（`VecDecoder.scala:550/567-570` FMADD_H 等） | 同上 |
| bf16 支持 | **未集成**：yunsuan 有实验性 `VectorBrainFloatAdder`（`yunsuan/vector/VectorBrainFloatAdder.scala:47-68`，"bf16 still under consideration"），主仓库无任何引用 | `grep -rn "VectorBrainFloat" /tmp/xs-kunminghu-v2/src/main/scala/` 无结果 |
| 结论 | 昆明湖**没有**任何点积指令（整数/浮点/bf16 均无），vdot 是全新扩展 | 上述 grep |

---

## 新增 vdot 指令所需改动点清单

> 假设：vdot 为 `vdot.vv`（vs1/vs2 各含 16 个 INT8，输出 4 个 INT32 累加到 vd 对应 32-bit 槽；若沿用 RVV 的"按 EEW=8、vlmul 分组、4 组 8 元素点积"语义，每个 uop 输出 128-bit）。编码建议取 OP-V（`1010111`）中未使用的 funct6。

| # | 文件（相对 `src/main/scala/`） | 改动内容 | 改动量 |
|---|---|---|---|
| 1 | `xiangshan/backend/decode/VecDecoder.scala`（或新建 `VdotDecoder`） | 新增指令 BitPat → `OPMVV(T, FuType.vdot, VdotType.vdot, F, T, F, UopSplitType.VEC_VVV)`；若编码在 rocket-chip 侧登记则同步 `rocket-chip/.../Instructions.scala` | 小（2~5 行） |
| 2 | yunsuan `yunsuan/package.scala` + `yunsuan/encoding/Opcode/` | 新增 `VdotType`（9-bit OpType：vs2/vs1 符号 + format + 子 opcode）或扩展 `VimacType`；`VdotType.dummy` 必须保留 | 中（~30 行） |
| 3 | `xiangshan/backend/fu/FuType.scala:127/130/133` + 相关 `is*` 辅助 | 新增 `val vdot = addType(...)` 加入 `vecOPI`（127 行）→ `vecArith/vecAll`；同步 `FuConfig.isVecArith`（`backend/fu/FuConfig.scala:181-185`）、`needVecCtrl`（162-165 行）、`needOg2`（190 行） | 小（~8 行） |
| 4 | `xiangshan/backend/fu/FuConfig.scala` | 新增 `VdotCfg`（仿 `VimacCfg` 547-564 行）：`fuType=vdot`、`fuGen=…new VdotU(cfg)(p)`、`srcData=Seq(Seq(VecData(),VecData(),VecData(),V0Data(),VlData()))`、`piped=true`、`writeVecRf=true`、`writeV0Rf=true`、`latency=CertainLatency(N)`、`vconfigWakeUp=true`、`maskWakeUp=true`、`destDataBits=128`、`exceptionOut=Seq(illegalInstr)` | 中（~20 行） |
| 5 | `xiangshan/Parameters.scala:449`（vfSchdParams） | 把 `VdotCfg` 加入某个 vf EXU 的 `fuConfigs`（建议 VFEX0，与 VimacCfg 同 EXU，复用 `VfWB(0,0)` 与 `VfRD(0..2)` 端口；若吞吐不足再考虑新增 EXU/端口） | 小（1 行） |
| 6 | `xiangshan/backend/fu/wrapper/VdotU.scala`（新建）+ `xiangshan/backend/fu/vector/` | 新执行单元：`extends VecPipedFuncUnit`；仿 VIMacU 结构（`VecDataSplitModule` + 2×64-bit 点积核心 + `Mgu`），点积核心可直接新建（Booth 乘法器树 + 累加）或扩展 yunsuan `VIMac64b`（增加点积模式：16×INT8 → 4×INT32，即 4 个 4 元素点积） | 大（300~600 行） |
| 7 | yunsuan（新建 `Vdot64b` 或扩展 `VIMac64b`） | 64-bit 点积核心：INT8×INT8→INT32 的乘法+加法树+累加器（可复用 Booth/Wallace 思路，`VIMac64b.scala:47-160`） | 大（200~400 行） |
| 8 | `xiangshan/backend/decode/VecExceptionGen.scala` | 若 vdot 视为 e8→e32（EEW 3）语义：加入 `vdWideningInst`（83-96 行）等清单以启用 EEW/EMUL/对齐/重叠检查；若按"固定 16×8bit→4×32bit 无 vlmul 变化"实现，则需自行新增对应的 eew/emul 检查 | 小~中（10~40 行） |
| 9 | `xiangshan/backend/decode/UopInfoGen.scala` / `DecodeUnitComp.scala` | 若复用 `VEC_VVV` 则无需改；若按 widening 输出 256-bit 则用 `VEC_VVW`（`DecodeUnitComp.scala:487-500` 已支持） | 0 或小 |
| 10 | 测试 | yunsuan `Vdot64bSpec`（仿 `yunsuan/src/test/scala/vector/VectorALU/VIMac64bSpec.scala`）；香山侧可加 decode 单测；回归跑 riscv-tests 向量用例 + 自定义用例 | 中 |
| 11 | NEMU（外部仓库） | 实现 vdot 语义 + 解码表注册（自定义编码） | 中 |
| 12 | difftest | **无需改动**（DiffInstrCommit 已覆盖向量写回） | 0 |

**估计总改动量**：硬件侧（香山 + yunsuan）约 600~1200 行 Scala，外加 NEMU 实现与测试。

---

## 关键设计建议

1. **复用现有单元与端口，避免动 VPRF 面积**：
   - 建议把 `VdotCfg` 挂到 **VFEX0**（`Parameters.scala:449`），与 `VimacCfg`/`VialuCfg`/`VfmaCfg` 共享该 EXU：EXU 内多 FU 按 fuType one-hot 仲裁（`ExeUnit.scala:322-323/428`），共享 3 个 `VfRD` + `V0RD` + `VlRD` 读口（5 源正好覆盖 vs1/vs2/oldVd/v0/vl）与 `VfWB(0,0)` 写口。**不要新增 VfRD/VfWB port 号**（否则 VPRF 端口数自动增大，`BackendParams.scala:295-306`）。
   - 若 vdot 不需要读 oldVd（非累加语义可清零），仍建议保留 `srcData` 中的 `VecData()×3` 以复用现有 EXU 读端口布局；累加语义（vd+=Σ）必须读 oldVd，与 VimacCfg 完全一致。
2. **时延设置依据**：
   - 参考 `VimacCfg.latency=CertainLatency(2)`（`FuConfig.scala:559`）与 `VIMac64b` 内部两级寄存器（`VIMac64b.scala:168/208`）。vdot 的乘法器+加法树规模与 VIMac 相当（16×8bit 乘加 vs 8/16/32/64-bit 混合 MAC），**建议 latency=2~3**；若在 yunsuan 扩展 `VIMac64b` 增加点积模式，保持其 3 段 2 级寄存器结构则 latency=2 即可。时延必须与核心内寄存器级数严格一致（`HasPipelineReg.latency`，`FuncUnit.scala:167`）。
   - 若为了 INT8 峰值吞吐选择每周期处理 256-bit（2 个 uop/周期），可让 VdotCfg `destDataBits=128` 但 issue 双发（两个 EXU 各挂 1 个 VdotCfg），或保持单 uop/lmul 与 vfma 同级吞吐。
3. **指令语义与拆分**：INT8×INT8→INT32 若输出不 widening（每 128-bit 出 4×32bit），**直接复用 `UopSplitType.VEC_VVV`（1 uop/lmul）**，无需新拆分逻辑；`VEC_VVW`（2 uop/lmul、结果 256-bit）仅当想支持 lmul 下的 2×128-bit 输出时用。`UopInfoGen.scala:201`（VEC_VVV→lmul）与 `DecodeUnitComp.scala:359-367`（VEC_VVV 拆法）已现成可用。
4. **mask/tail/vstart 全部复用现成模块**：执行单元里例化 `Mgu(128)`（`backend/fu/vector/Mgu.scala:34`），按 `VIAluFix.scala:333-360` / `VIMacU.scala:132-150` 的连接方式接线即可；vstart≠0 的 illegal 由 `HasPipelineReg`（`FuncUnit.scala:253-258`）与 `VecExceptionGen.scala:275` 自动覆盖，无需新逻辑。
5. **vxsat**：若 vdot 采用饱和累加语义（参考 vsmul 的 `isFixP` 路径，`VIMac64b.scala:223-232`），保留 `writeVxsat=true` 并接 `io.out.bits.res.vxsat`（仿 `VIMacU.scala:149`）；若纯 wrap-around 则 `writeVxsat=false`。
6. **解码/异常登记三处必改**：`VecDecoder.scala` 解码表、`VecExceptionGen.scala` 的指令分类清单（尤其 widening/EEW/重叠检查）、`FuType` 的 `vecArith/vecAll` 集合（`FuType.scala:127-133`）——漏掉任何一处会导致 dispatch 找不到 EXU（`canAccept` 全 false）或非法检查不正确。
7. **编码空间**：OP-V（opcode `1010111`）中 funct6=000000~111111 大部分已被 RVV 占用；vdot 需选一个未用 funct6（并避开 `VecDecoder` 已登记的 Zvbb 等扩展位），同时在 NEMU 侧注册同一编码。

---

## 附：报告所引关键行号速查

- 分支/commit：见第 0 节（GitHub API + tarball）
- `VimacCfg`：`backend/fu/FuConfig.scala:547-564`
- vfSchdParams（VFEX0-4/IQ）：`Parameters.scala:445-465`
- VPRF 128 项：`Parameters.scala:193-197`；V0=22/Vl=32：198-207；端口推导：`backend/BackendParams.scala:295-306`
- VLEN=128：`Parameters.scala:60`；VecData 128-bit：`backend/datapath/DataConfig.scala:17`
- vwmaccu 解码：`backend/decode/VecDecoder.scala:426`；编码：rocket-chip `Instructions.scala:821`
- VimacType.vwmaccu：yunsuan `package.scala:392`；VimacOpcode：yunsuan `encoding/Opcode/VimacOpcode.scala:5-24`
- decode_table 拼接：`backend/decode/DecodeUnit.scala:806-818`；illegal：894-917、932
- VecExceptionGen：`backend/decode/VecExceptionGen.scala:42-303`（vstart 非法 275 行）
- 复杂指令拆分：`backend/decode/DecodeUnitComp.scala:359-568`（VEC_VVW 487-500）；uop 数：`backend/decode/UopInfoGen.scala:198-242`
- 执行单元样板：`backend/fu/wrapper/VIMacU.scala:45-151`、`backend/fu/wrapper/VIAluFix.scala:134-377`、`backend/fu/wrapper/VIDiv.scala:15-71`
- 流水基类：`backend/fu/FuncUnit.scala:166-288`；`backend/fu/vector/VecPipedFuncUnit.scala:58-95`；`VecNonPipedFuncUnit.scala:13-56`
- Mgu：`backend/fu/vector/Mgu.scala:34-245`
- ExeUnit 写回仲裁：`backend/exu/ExeUnit.scala:312-387`
- Difftest：difftest `Bundles.scala:66-93`；硬件上报 `backend/rob/Rob.scala:1543-1582`；`backend/datapath/DataPath.scala:329`
- 无 vdot/vfdot/自定义指令钩子：见 F 节 grep 结果
