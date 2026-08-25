# NEMU golden — vdot.vv 支持（G7 主 golden）

NEMU 是 vdot 的 difftest 主 golden 参考（RTL 逐指令比对的对端）。项目在 NEMU（OpenXiangShan/NEMU master + RVV）上实现 vdot.vv 语义并注册编码。

## 改动文件（5 处 + 1 测试）

| 文件 | 改动 |
| --- | --- |
| decode.h | vopmvv 表注册 funct6=111001 -> vdot |
| isa-all-instr.h | VECTOR_INSTR_TERNARY 加 f(vdot) |
| vcompute.h | def_EHelper(vdot) -> vdot_instr(s) |
| vcompute_impl.h | 声明 void vdot_instr(Decode *s) |
| vcompute_impl.c | vdot_instr 实现（本目录 vdot_instr.c 完整函数体） |
| tests/vdot/vdot_test.S | 指令级自测（累加 0x0a -> 0x14） |
| tests/vdot/vdot.ld | 裸机链接脚本 |

全路径均在 NEMU/src/isa/riscv64/ 下（decode 在 instr/rvv/，isa-all-instr 在 include/）。

## vdot_instr 语义（与 RTL 逐点一致）

```c
// vcompute_impl.c 中 vdot_instr 的要点
void vdot_instr(Decode *s) {
  require_vector(true);
  if (vtype->vill) longjmp_exception(EX_II);
  if (vtype->vsew != 2) longjmp_exception(EX_II);  // 必须 e32
  if (vtype->vlmul == 4) longjmp_exception(EX_II); // 保留
  for (idx = vstart; idx < vl; idx++) {   // 逐 32-bit 元素
    chunk_vs2 = vs2 的 32-bit 片段;  chunk_vs1 = vs1 的 32-bit 片段;
    dot = sum_{i=0..3} sext8(a[i]) * sext8(b[i]);  // 4xINT8 点积
    vd[idx] = vd旧值 + dot (mod 2^32, wrap-around);
  }
  // 掩码(ma/vm) / 尾元素(vta) 按 RVV 通用机制处理
}
```

完整函数体见 vdot_instr.c。

## 验证（NEMU 独立运行）

```bash
cd NEMU && make -j48                    # CONFIG_RVV=y
./build/riscv64-nemu-interpreter -b tests/vdot/vdot_test.bin
# 输出: trap code 20 (0x14) = lane0 累加正确
```

## 三方一致

NEMU (trap 0x14) == Spike (v10 lane0 0x14) == RTL (emu lane0 0x14)

详见 ../spike-golden/README.md 与 ../docs/verification.md。

## 备注

NEMU 为完整第三方仓库（OpenXiangShan/NEMU）。本目录只收录 vdot 相关改动片段
（decode 登记 / helper / 实现函数 / 测试），供对照与手动应用。完整 NEMU 源码请从
OpenXiangShan/NEMU 获取后，在上述文件位置落地 vdot 改动。
