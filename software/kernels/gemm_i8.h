// ============================================================================
// gemm_i8.h — INT8 GEMM 微内核 API (配合 vdot 打包布局, 04 §5.1)
//
// 计算:  C[m][n] = sum_k A[m][k] * W[n][k]        (INT32 累加, wrap-around)
//   A : M×K int8, 行主序 (K 连续)   —— 行内 K 连续, 天然适配 4×INT8 打包视图
//   W : N×K4 int32 打包字 (weight_packer 输出, n-major, K4=ceil(K/4),
//       每字 = 同一输出通道 K 维连续 4 个 INT8, 小端)
//   C : M×N int32
// 可选反量化: Cf[m][n] = C[m][n] * sa[m] * sb[n]   (对称 per-row/per-channel scale)
//
// 与 vdot.vv 的对应 (02 §4): 每 4 个 K 元素 = 一个打包字 = vdot 的一个 32-bit 元素;
//   scalar 路径逐 (m,n) 累加 dot4; RVV 路径每 4 个 m 行为 4 个 vdot 通道。
// ============================================================================
#ifndef GEMM_I8_H
#define GEMM_I8_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// K4 = ceil(K/4); K 不必为 4 倍数 (尾部不足 4 的 A 字节按 0 处理, 与打包器 padding 一致)
static inline int gemm_i8_k4(int K) { return (K + 3) / 4; }

// 标量参考实现 (语义与 vdot_ref 逐点一致; 在任何平台可编译/测试)
//   C: M*N int32 (必填)   Cf: M*N float32 (可选, NULL 则跳过反量化)
//   sa: M 个 per-row scale / sb: N 个 per-channel scale (NULL 视为 1.0)
void gemm_i8_vdot_scalar(int M, int N, int K,
                         const int8_t *A, const int32_t *W,
                         int32_t *C, const float *sa, const float *sb,
                         float *Cf);

// RVV 加速路径 (需 -march=rv64gcv 且包含 xkhmvdot_intrin.h; 每 4 行为一组,
//   M%4 尾行回落标量). 当前实现正确性等价于标量版, 供 B0/B1 基准用。
#if defined(__riscv_v)
#include <riscv_vector.h>
#include "xkhmvdot_intrin.h"
void gemm_i8_vdot_rvv(int M, int N, int K,
                      const int8_t *A, const int32_t *W,
                      int32_t *C, const float *sa, const float *sb,
                      float *Cf);
#endif

#ifdef __cplusplus
}
#endif

#endif // GEMM_I8_H
