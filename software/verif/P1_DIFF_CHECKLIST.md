# P1 vdot 差分复测检查清单（emu 重建完成后执行）

> 对应 [08_任务完成情况CheckList](../../2026-CIE-RISC-V-Contest-Application-Track/02_设计方案/08_任务完成情况CheckList.md) §3.3 与 [05_验证与评估方案](../../2026-CIE-RISC-V-Contest-Application-Track/02_设计方案/05_验证与评估方案.md) §2/§3。
> 前置：RTL 已含 vdRen=T 修复（`VecDecoder.scala:431` `OPMVV(T,...)`），`make emu` 重建完成。
> 状态图例：⬜ 未执行 ｜ ✅ 通过 ｜ ❌ 失败（记录现象）｜ ⏸ 阻塞

---

## 0. 前置（Linux 完整环境）

- [ ] `git rev-parse HEAD` 确认 emu 对应的源码 commit（应含 vdot 修复）
- [ ] `build/emu` 存在且时间戳晚于 RTL 修复提交（Verilator 重建 ~2.5h）
- [ ] NEMU .so（`riscv64-nemu-interpreter-so`，master + 官方 defconfig，含 vdot）就绪
- [ ] 测试镜像（`vdot_test.bin` 等）就绪（NEMU `tests/vdot/` 编译产物）

## 1. 核心差分复测（本轮目标，验证 oldVd 修复）

- [ ] 单次 vdot：`emu -i vdot_test.bin --diff <nemu.so>` → HIT GOOD TRAP，trap code **0x0a**
- [ ] 两次 vdot（累加）：→ trap code **0x14**（NEMU 与 RTL 一致 → 修复生效）
- [ ] 连续两条 vdot 且与 vwmaccu.vv / vmacc.vv 穿插（对照：基线累加机制正常）
- [ ] 差分无失配（无 "MISMATCH" / abort 输出）

## 2. 指令行为边界（05 §2 测试矩阵，定向用例）

- [ ] `vl=0`：vd 不变
- [ ] `vl ∈ {1,2,3}`（非 VLMAX）：部分元素更新，其余尾元素按 vta 处理
- [ ] `vl=VLMAX`（e32/m1 → 4）
- [ ] 掩码：vm=0 全 0（vd 不变）、部分掩码（仅掩码位=1 更新）、掩码寄存器内容随机
- [ ] 尾元素：vta=0（undisturbed）与 vta=1（agnostic→实现按 undisturbed）
- [ ] **vstart≠0 → illegal instruction**（解码侧 `VecExceptionGen`；同既有向量算术）
- [ ] **vsew≠e32（e8/e16/e64）→ illegal instruction**（`vdotEewIllegal`）
- [ ] LMUL=2/4（寄存器组分段、uop 数=lmul）
- [ ] 累加回绕：构造 `0x7FFFFFFF + 正点积 → 0x80000000` 用例

## 3. 随机差分（05 §3 三方一致）

- [x] 随机激励生成器就绪：`gen_vdot_tests.py`（vl 边界/掩码/非法 vsew/vstart≠0/lmul/极值数据/1-3 次累加，内置期望 lane0 自校验）
- [x] 定向边界用例就绪：`gen_vdot_tests.py --directed`（**d01-d12**：回绕上/下溢、全 127/-128、掩码部分命中、尾元素 vta=tu/ta、掩码宽松 vma=ma、vstart≠0、vsew≠e32、混合、4 lane 独立——全 lane 自校验）
- [x] NEMU-only 预验证：`run_random_diff.sh --nemu-only` —— **180 随机（seed 7/20260817/12345）+ 12 定向 = 192 例全部 PASS**
- [ ] RTL(emu) vs NEMU 差分：`run_random_diff.sh --emu build/emu --diff nemu.so`（Linux，待 emu 重建完成）
- [ ] 失配自动最小化复现（delta-debug）→ 入定向回归集

> 注意①：退出码采用自校验约定（PASS=0x5A5A/23130，FAIL=0xDEAD/57005）——不能直接用原始 lane0 作退出码（NEMU nemu_trap 对 a0==0x100/0x101 有特殊语义不退出）。
> 注意②：**RVV 掩码按位索引**（元素 i 用 v0 的 bit i）——mask 数据必须位打包（`.byte 0b00000100` 表示仅元素 2），不能按元素逐字节发射（曾因布局错误导致掩码用例静默失配，已修复）。

## 4. 回归（无退化）

- [ ] riscv-tests 全量（标量 + 向量）0 失配（与基线对比）
- [ ] coremark 冒烟（基线 IPC 无回退）
- [ ] kernel 冒烟：`gemm_i8_test`（标量路径）在 RTL 上以 vdot 加速路径跑通并比对

## 5. 收尾

- [ ] 更新 [08_CheckList](../../2026-CIE-RISC-V-Contest-Application-Track/02_设计方案/08_任务完成情况CheckList.md) §3.3（最终差分结果、移除"🔍/🔄"状态）
- [ ] 更新 [VDOT_IMPLEMENTATION.md](../../../xiangshan-code/VDOT_IMPLEMENTATION.md) 验证情况
- [ ] 记录 emu/源码 commit、seed、trap code 到 [05 §8 验收清单](../../2026-CIE-RISC-V-Contest-Application-Track/02_设计方案/05_验证与评估方案.md)

---

## 快速执行

```bash
# 核心差分（§1）一行跑完：
./vdot_diff_check.sh --emu <NOOP_HOME>/build/emu \
                     --diff <NEMU>/build/riscv64-nemu-interpreter-so \
                     --img <vdot_test.bin>
```
