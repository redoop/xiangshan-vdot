# vdot P1 差分复测 · Linux 操作单（2026-08-17 状态）

> 目的：在 Linux 完整环境执行 vdot 差分复测，验证 RTL oldVd 修复（08 §3.3 根因：组合化 Vdot64b 与 FU 2 级流水错位 → 修复 = 恢复内部 2 级流水 + 回滚 VEC_VVV）。
> 本单与 `vdot_diff_check.sh` + `P1_DIFF_CHECKLIST.md` 配套；执行完成后把结果**同步回** 08 §3.3 与 P1_DIFF_CHECKLIST。

---

## 0. 当前状态（8-17）

| 项 | 状态 |
| --- | --- |
| 修复代码 | ✅ 已落地：`Vdot64b.scala` 恢复 2 级内部流水（fire 锁 vs1/vs2/oldVd → 组合点积 → fireS1 锁结果）；`VecDecoder.scala` VDOT_VV 回滚 VEC_VVW→VEC_VVV |
| `mill xiangshan.compile` | ✅ 通过（~13s） |
| `make sim-verilog` | ✅ 通过（~628s；`Vdot64b.sv` 含 vs1/vs2/oldVdReg 三锁存 + fireS1 结果锁存） |
| `make emu`（Verilator） | 🔄 后台进行中（~3-4h，nohup，日志 `/tmp/make_emu_fix.log`） |

## 1. 前置检查（emu 编译完成后）

```bash
cd <NOOP_HOME>            # NOOP_HOME = XiangShan 主仓库（Linux 完整 clone）
tail -20 /tmp/make_emu_fix.log      # 确认末尾成功（无 ERROR/FAILED）
ls -la build/emu                    # 时间戳应晚于修复提交
git rev-parse HEAD                  # 记录当前源码 commit（回填 08 §3.3）
```

> ⚠️ 服务器重启会因时间戳触发 Verilator 全量重编（~3-4h）；期间勿中断 nohup 进程。

## 2. 测试镜像

- 镜像在 NEMU 侧编译（`xiangshan-code/NEMU/tests/vdot/`）：
  ```bash
  riscv64-unknown-elf-gcc -nostdlib -T vdot.ld -o vdot_test vdot_test.S \
    -march=rv64gcv -mabi=lp64d
  riscv64-unknown-elf-objcopy -O binary vdot_test vdot_test.bin
  ```
- 本单需要的镜像：`vdot_test.bin`（两次累加，期望 trap code **0x14**）与 `vdot_oldvd_src.bin`（oldVd 预置用例，专门验证 oldVd 读取修复，期望值对照 NEMU golden）。
- 若 Linux 已存在旧镜像，**必须用修复后的 RTL + 同一 NEMU .so 重跑**。

## 3. 执行差分复测

```bash
# 方式 A：一键脚本（推荐）
bash <repo>/vdot-software/verif/vdot_diff_check.sh \
     --emu  $NOOP_HOME/build/emu \
     --diff $NOOP_HOME/ready-to-run/riscv64-nemu-interpreter-so \
     --img  vdot_test.bin \
     --img  vdot_oldvd_src.bin

# 方式 B：手动单跑（便于看完整日志）
$NOOP_HOME/build/emu -i vdot_test.bin --diff <nemu.so> 2>&1 | tee /tmp/vdot_diff1.log
$NOOP_HOME/build/emu -i vdot_oldvd_src.bin --diff <nemu.so> 2>&1 | tee /tmp/vdot_diff2.log
```

## 4. 预期结果与判定

| 镜像 | 预期 | 判定 |
| --- | --- | --- |
| `vdot_test.bin`（两次累加） | HIT GOOD TRAP，trap code **0x14**（0x0a×2），差分 0 失配 | 修复生效：oldVd 累加恢复 |
| `vdot_oldvd_src.bin` | HIT GOOD TRAP，trap code 与 NEMU golden 一致 | oldVd 预置值参与累加 |

**通过** → 更新 08 §3.3（复测 PASS、移除"🔄"）→ 进入 `P1_DIFF_CHECKLIST.md` §2（边界用例）→ §3（随机差分）→ §4（riscv-tests/coremark 回归）。
**失败** → 记录现象（trap code/失配指令/波形）→ 对照 §3.3 根因排查（Vdot64b 流水时序、Mgu 接线）→ 更新 08。

## 5. 结果回写（同步进度）

1. `08_任务完成情况CheckList.md` §3.3：填入复测结果、源码 commit、trap code；总览"difftest/全量回归"行 🟡→✅（若 §2 边界 + §3 随机也通过）。
2. `P1_DIFF_CHECKLIST.md`：§1 核心差分逐项打勾。
3. `VDOT_IMPLEMENTATION.md`：验证情况同步。
4. （本地副本）如需回迁：把修复后的 `Vdot64b.scala` / `VecDecoder.scala` 从 Linux 同步回 `xiangshan-code/` 工作副本。
