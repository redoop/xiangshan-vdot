// ============================================================================
// xkhmvdot_intrin.h — Xkhmvdot (vdot.vv) 软件接口头文件
//
// 指令: vdot.vv vd, vs1, vs2   (funct6=111001, funct3=010, opcode=1010111)
//   语义: 每个 32-bit 元素视为 4 个打包 INT8(小端, 子元素 j 位于位 [8j,8j+7]);
//         vd[j] += sum_{i=0..3} sext8(vs1[j][i]) * sext8(vs2[j][i])   (int32 回绕)
//   约束: 要求 vsetvl 的 vsew=e32 (vl 按 e32 计, LMUL=1 时 VLMAX=4); vstart!=0 非法
//
// 说明: 在 LLVM/binutils 汇编支持落地前 (P2), 本头文件的 __riscv_vdot_* 走
//       C 参考实现 (vdot_ref), 保证 kernel 可先开发、结果可验证;
//       定义 XKHMVDOT_ASM=1 且汇编器支持 vdot.vv 后, 自动切换到内联汇编。
//       语义权威定义: 02_指令集设计.md §4 (vdota4 家族), 本实现为 vdot.vv 单条。
// ============================================================================
#ifndef XKHMVDOT_INTRIN_H
#define XKHMVDOT_INTRIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// 0. RVV 头可用性 (宿主侧无 -march=rv64gcv 时仅提供 C 参考实现)
// ---------------------------------------------------------------------------
#if defined(__riscv_v)
#include <riscv_vector.h>
#define XKHMVDOT_HAS_RVV 1
#else
#define XKHMVDOT_HAS_RVV 0
#endif

// ---------------------------------------------------------------------------
// 0. 类型与打包辅助 (与 02 §4.1 数据视图一致: 32-bit = 4×INT8, 小端)
// ---------------------------------------------------------------------------
typedef int32_t xkhmvdot_t; // 一个"打包 4×INT8"的 32-bit 元素

static inline xkhmvdot_t xkhmvdot_pack4(const int8_t a[4]) {
  return (int32_t)((uint32_t)(uint8_t)a[0]        |
                   ((uint32_t)(uint8_t)a[1] << 8) |
                   ((uint32_t)(uint8_t)a[2] << 16)|
                   ((uint32_t)(uint8_t)a[3] << 24));
}
static inline void xkhmvdot_unpack4(int8_t a[4], xkhmvdot_t x) {
  a[0] = (int8_t)(x & 0xFF); a[1] = (int8_t)((x >> 8) & 0xFF);
  a[2] = (int8_t)((x >> 16) & 0xFF); a[3] = (int8_t)((x >> 24) & 0xFF);
}

// ---------------------------------------------------------------------------
// 1. C 参考实现 (golden, 与 02 §4.2 伪代码逐条对应)
//    vd[i] += sum_{j=0..3} sext8(vs1[i] 子元素 j) * sext8(vs2[i] 子元素 j)
//    其中 vs1[i]/vs2[i] 为打包元素; 累加为 int32 模 2^32 回绕。
//    只处理 [0, vl) 个元素; 掩码/尾元素/vstart 由上层 RVV 机制处理
//    (本函数语义 = vm=1, vstart=0, 尾元素由调用方保证 undisturbed/agnostic)。
// ---------------------------------------------------------------------------
static inline void vdot_ref(xkhmvdot_t *vd, const xkhmvdot_t *vs1,
                            const xkhmvdot_t *vs2, size_t vl) {
  for (size_t i = 0; i < vl; i++) {
    int32_t acc = vd[i];
    for (int j = 0; j < 4; j++) {
      int32_t a = (int8_t)((vs1[i] >> (8 * j)) & 0xFF);
      int32_t b = (int8_t)((vs2[i] >> (8 * j)) & 0xFF);
      acc = (int32_t)((uint32_t)acc + (uint32_t)(a * b)); // wrap-around
    }
    vd[i] = acc;
  }
}

// ---------------------------------------------------------------------------
// 2. RVV intrinsic 路径 (需 -march=rv64gcv; 汇编器支持 vdot.vv 后 -DXKHMVDOT_ASM=1)
//    编码: 0b111001_0_vs2[4:0]_vs1[4:0]_010_vd[4:0]_1010111
//    操作数顺序与 RVV 一致: vdot.vv vd, vs1, vs2
// ---------------------------------------------------------------------------
#if XKHMVDOT_HAS_RVV
#if defined(XKHMVDOT_ASM)
static inline vint32m1_t __riscv_vdot_vv_i32m1(vint32m1_t vd, vint32m1_t vs1,
                                               vint32m1_t vs2, size_t vl) {
  asm volatile("vdot.vv %0, %1, %2" : "+vr"(vd) : "vr"(vs1), "vr"(vs2)
               : /* no clobber */);
  (void)vl;
  return vd;
}
#else
// 无汇编支持时: C 参考实现包装 (功能正确, 性能待工具链)
static inline vint32m1_t __riscv_vdot_vv_i32m1(vint32m1_t vd, vint32m1_t vs1,
                                               vint32m1_t vs2, size_t vl) {
  xkhmvdot_t *pd = (xkhmvdot_t *)&vd, *p1 = (xkhmvdot_t *)&vs1,
             *p2 = (xkhmvdot_t *)&vs2;
  vdot_ref(pd, p1, p2, vl); // 调用方保证 vsetvl e32 且 vl<=VLMAX
  return vd;
}
#endif
#endif // XKHMVDOT_HAS_RVV

// 兼容名: 官方对齐名 vdota4 宏别名 (ISA 文档 02 使用 vdota4 家族命名)
#ifndef vdota4_vv_i32m1
#define vdota4_vv_i32m1 __riscv_vdot_vv_i32m1
#endif

// ---------------------------------------------------------------------------
// 3. 自检 (可选, 供单元测试/冒烟): 返回 0=通过
// ---------------------------------------------------------------------------
static inline int xkhmvdot_selftest(void) {
  xkhmvdot_t vs1[4] = {xkhmvdot_pack4((int8_t[]){1, 2, 3, 4}),
                       xkhmvdot_pack4((int8_t[]){-1, -2, -3, -4}),
                       xkhmvdot_pack4((int8_t[]){127, -128, 0, 1}),
                       0};
  xkhmvdot_t vs2[4] = {xkhmvdot_pack4((int8_t[]){1, 1, 1, 1}),
                       xkhmvdot_pack4((int8_t[]){1, 1, 1, 1}),
                       xkhmvdot_pack4((int8_t[]){1, 1, 1, 1}),
                       0};
  xkhmvdot_t vd[4] = {0, 0, 0, 0};
  vdot_ref(vd, vs1, vs2, 4);
  // lane0 = 1+2+3+4 = 10; lane1 = -1-2-3-4 = -10; lane2 = 127-128+0+1 = 0
  if (vd[0] != 10 || vd[1] != -10 || vd[2] != 0 || vd[3] != 0) return -1;
  // 累加: 再来一次 -> lane0=20, lane1=-20 (与 NEMU/emu 实测 trap code 20 一致)
  vdot_ref(vd, vs1, vs2, 4);
  if (vd[0] != 20 || vd[1] != -20) return -2;
  // 回绕: INT32_MAX + 正点积 -> 回绕 (构造: 0x7FFFFFFF + 1)
  xkhmvdot_t big[1] = {INT32_MAX};
  xkhmvdot_t one[1] = {xkhmvdot_pack4((int8_t[]){1, 0, 0, 0})};
  vdot_ref(big, one, one, 1);
  if (big[0] != INT32_MIN) return -3; // 0x7FFFFFFF + 1 -> 0x80000000 (回绕)
  return 0;
}

#ifdef __cplusplus
}
#endif

#endif // XKHMVDOT_INTRIN_H
