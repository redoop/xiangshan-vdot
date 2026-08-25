// ============================================================================
// gemm_i8.c — INT8 GEMM 微内核实现 (gemm_i8.h)
// ============================================================================
#include "gemm_i8.h"
#include <stddef.h>

// 4×INT8 打包字 -> 第 j 个 int8 (小端, 与 02 §4.1 一致)
static inline int32_t sext_byte(int32_t w, int j) {
  return (int32_t)(int8_t)((w >> (8 * j)) & 0xFF);
}

// 单打包字点积并累加 (与 vdot 语义一致: int8×int8 -> int32 wrap-around)
static inline int32_t dot4_acc(int32_t acc, int32_t a, int32_t b) {
  for (int j = 0; j < 4; j++)
    acc = (int32_t)((uint32_t)acc + (uint32_t)(sext_byte(a, j) * sext_byte(b, j)));
  return acc;
}

// 从 A 行第 k4 个字位置装载打包字; K 尾部不足 4 时补 0 (与打包器 padding 语义一致)
static inline int32_t load_pad4(const int8_t *row, int K, int k4) {
  int k = k4 * 4;
  if (k + 4 <= K) {
    const int8_t *p = row + k;
    return (int32_t)((uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8) |
                     ((uint32_t)(uint8_t)p[2] << 16) | ((uint32_t)(uint8_t)p[3] << 24));
  }
  uint32_t w = 0;
  for (int j = 0; j < 4 && k + j < K; j++)
    w |= ((uint32_t)(uint8_t)row[k + j]) << (8 * j);
  return (int32_t)w;
}

void gemm_i8_vdot_scalar(int M, int N, int K,
                         const int8_t *A, const int32_t *W,
                         int32_t *C, const float *sa, const float *sb,
                         float *Cf) {
  const int K4 = gemm_i8_k4(K);
  for (int m = 0; m < M; m++) {
    const int8_t *row = A + (size_t)m * K;
    for (int n = 0; n < N; n++) {
      const int32_t *wn = W + (size_t)n * K4;
      int32_t acc = 0;
      for (int k4 = 0; k4 < K4; k4++)
        acc = dot4_acc(acc, load_pad4(row, K, k4), wn[k4]);
      C[(size_t)m * N + n] = acc;
      if (Cf) {
        float s = (sa ? sa[m] : 1.0f) * (sb ? sb[n] : 1.0f);
        Cf[(size_t)m * N + n] = (float)acc * s;
      }
    }
  }
}

#if defined(__riscv_v)
#include <riscv_vector.h>
#include "xkhmvdot_intrin.h"

void gemm_i8_vdot_rvv(int M, int N, int K,
                      const int8_t *A, const int32_t *W,
                      int32_t *C, const float *sa, const float *sb,
                      float *Cf) {
  const int K4 = gemm_i8_k4(K);
  // 每 4 行为一组: 4 个 vdot 通道 = 4 个输出元素 (m, m+1, m+2, m+3) × n
  int m = 0;
  for (; m + 4 <= M; m += 4) {
    for (int n = 0; n < N; n++) {
      const int32_t *wn = W + (size_t)n * K4;
      vint32m1_t acc = __riscv_vmv_v_x_i32m1(0, 4);
      for (int k4 = 0; k4 + 4 <= K4; k4 += 4) {
        // vs1 = 4 个 m 行的第 k4..k4+3 个打包字 (转置为 lane)
        vint32m1_t a0 = __riscv_vle32_v_i32m1((const int32_t *)(A + (size_t)(m + 0) * K) + k4, 4);
        vint32m1_t a1 = __riscv_vle32_v_i32m1((const int32_t *)(A + (size_t)(m + 1) * K) + k4, 4);
        vint32m1_t a2 = __riscv_vle32_v_i32m1((const int32_t *)(A + (size_t)(m + 2) * K) + k4, 4);
        vint32m1_t a3 = __riscv_vle32_v_i32m1((const int32_t *)(A + (size_t)(m + 3) * K) + k4, 4);
        // 按 k4 步进逐 lane 累加: 4 次 vdot, 每次 lane j 处理第 k4+j 个字
        vint32m1_t t;
        t = __riscv_vmv_v_x_i32m1(wn[k4 + 0], 4);
        acc = __riscv_vdot_vv_i32m1(acc, t, a0, 4);
        t = __riscv_vmv_v_x_i32m1(wn[k4 + 1], 4);
        acc = __riscv_vdot_vv_i32m1(acc, t, a1, 4);
        t = __riscv_vmv_v_x_i32m1(wn[k4 + 2], 4);
        acc = __riscv_vdot_vv_i32m1(acc, t, a2, 4);
        t = __riscv_vmv_v_x_i32m1(wn[k4 + 3], 4);
        acc = __riscv_vdot_vv_i32m1(acc, t, a3, 4);
      }
      __riscv_vse32_v_i32m1(C + (size_t)m * N + n, acc, 4);
    }
  }
  // 尾部 m (M%4) 回落标量
  if (m < M) {
    gemm_i8_vdot_scalar(M - m, N, K, A + (size_t)m * K, W,
                        C + (size_t)m * N, sa ? sa + m : NULL, sb, Cf ? Cf + (size_t)m * N : NULL);
  }
}
#endif // __riscv_v
