# XiangShan Vdot — RTL 补丁说明

本目录包含 vdot.vv 指令的 Chisel/Scala RTL 实现补丁, 以及应用、验证、回滚补丁的完整方法。

## 1. 补丁内容概览

共 12 个 Chisel/Scala 文件, 覆盖 vdot.vv 指令的完整硬件链路:

| 仓库 | 文件数 | 内容 |
| --- | --- | --- |
| yunsuan 子模块 | 4 | VdotOpcode / VdotType / Vdot64b 核心 / Vdot64bSpec 测试 |
| 香山主仓库 | 8 | VdotU 执行单元 / FuType / FuConfig / 解码指令 / VecDecoder / VecExceptionGen / DecodeUnit(vmaInsts 修复) / Parameters |

详见 CHANGES.md 逐文件改动说明。

## 2. 前置条件

应用补丁前, 需先获取两个源码树:

```bash
# 香山主仓库 (kunminghu-v2 分支)
git clone --recursive https://github.com/OpenXiangShan/XiangShan.git -b kunminghu-v2
# yunsuan 子模块 (clone 时 --recursive 会自动拉取)
```

确认目标已有对应基线文件 (应用脚本会逐个 diff, 不会覆盖用户改动, 只会新增/更新 vdot 相关文件)。

## 3. 应用补丁

用仓库内脚本一键应用 (会先 diff 预览, 再按需复制):

```bash
cd xiangshan-vdot
./rtl/apply_rtl_patches.sh --xiangshan <XiangShan根> --yunsuan <yunsuan根>
# 例如
./rtl/apply_rtl_patches.sh --xiangshan ../XiangShan --yunsuan ../XiangShan/yunsuan
```

安全选项:

```bash
# 先预览将应用的清单 (不改动任何文件)
./rtl/apply_rtl_patches.sh --xiangshan ../XiangShan --yunsuan ../XiangShan/yunsuan --dry-run

# 应用前为目标生成 .orig 备份
./rtl/apply_rtl_patches.sh --xiangshan ../XiangShan --yunsuan ../XiangShan/yunsuan --backup
```

脚本输出 [new]/[mod]/[same] 状态, 方便确认哪些文件被更新、哪些已一致。

## 4. 验证补丁

应用后, 在香山仓库验证编译与测试:

```bash
# 1) yunsuan 单元测试 (Vdot64b 核心)
cd <yunsuan根> && mill yunsuan.test

# 2) 香山主仓库整机编译 (Verilator + difftest)
cd <XiangShan根>
make init; make sim-verilog; make emu

# 3) difftest 冒烟 (vdot 单指令/累加)
build/emu -i <vdot_test.bin> --diff NEMU/build/riscv64-nemu-interpreter-so
```

完整验证矩阵见 ../../docs/verification.md 与 docs/checklist.md。

## 5. 回滚补丁

三种方式:

1. **--backup 备份**: 若应用时用了 --backup, 目标生成 .orig 文件, 删除更新文件并恢复 .orig 即可:
```bash
# 逐个回滚 (示例)
git -C <XiangShan根> checkout -- src/main/scala/xiangshan/backend/fu/FuType.scala
```

2. **git 回滚**: 香山/yunsuan 均为 git 仓库, 直接丢弃改动:
```bash
git -C <XiangShan根> checkout -- .
git -C <XiangShan根/yunsuan> checkout -- .
```

3. **人工回滚**: 按 CHANGES.md 中标注的改动点手工移除 vdot 相关代码。

## 6. 文件清单 (rtl/)

| 文件 | 说明 |
| --- | --- |
| apply_rtl_patches.sh | 一键应用补丁脚本 (diff 预览 / 复制 / 备份) |
| CHANGES.md | 逐文件改动点说明 (含 P1 vmaInsts 根因修复) |
| xs-kunminghu-v2/src/** | 香山主仓库 8 个修改文件 (按上游路径镜像) |
| yunsuan/src/** | yunsuan 子模块 4 个修改文件 (按上游路径镜像) |

## 7. 常见问题

- **Q: 脚本提示 [same] 不代表补丁没生效?**
  A: 对 —— [same] 表示目标文件已与补丁一致 (可能已应用过或用户手动改过), 无需重复复制; 只有 [mod]/[new] 才会被更新。

- **Q: 目标不是香山仓库会怎样?**
  A: 脚本会校验 src/main/scala/xiangshan 是否存在并给出告警, 但不会阻止复制; 请确认目标路径正确。

- **Q: 补丁依赖 rocket-chip 子模块吗?**
  A: 完整环境 (Linux 服务器) 在 rocket-chip/Instructions.scala 额外登记了 VDOT_VV; 若你的 clone 不含此登记, 需补上 (见 CHANGES.md 末尾)。
