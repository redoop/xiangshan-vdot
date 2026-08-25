// ============================================================================
// gemm_i8_test.c — INT8 GEMM 微内核自测
//   随机 M/N/K (含 K 非 4 倍数) vs int64 朴素参考 (避免参考溢出), 含反量化比对。
//   用法: gcc -O2 -Wall -Iinclude -Ikernels -o gemm_test kernels/gemm_i8_test.c \
//              kernels/gemm_i8.c && ./gemm_test [seed]
// ============================================================================
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gemm_i8.h"

static uint32_t rng_state;
static uint32_t rnd(void) { // xorshift32
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
  return rng_state;
}
static int8_t ri8(void) { return (int8_t)(rnd() & 0xFF); }
static float rf(void) { return (float)((rnd() & 0xFFFF) - 0x8000) / 4096.0f + 0.5f; }

// int64 朴素参考 (K 累加不可能溢出 int64)
static void ref_naive(int M, int N, int K, const int8_t *A, const int32_t *W,
                      int64_t *C) {
  const int K4 = gemm_i8_k4(K);
  for (int m = 0; m < M; m++)
    for (int n = 0; n < N; n++) {
      int64_t acc = 0;
      for (int k4 = 0; k4 < K4; k4++) {
        int32_t w = W[(size_t)n * K4 + k4];
        for (int j = 0; j < 4; j++) {
          int k = k4 * 4 + j;
          if (k >= K) break; // padding 0
          acc += (int64_t)(int8_t)((w >> (8 * j)) & 0xFF) * A[(size_t)m * K + k];
        }
      }
      C[(size_t)m * N + n] = acc;
    }
}

int main(int argc, char **argv) {
  rng_state = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 0xC0FFEEu;
  const int M = 13, N = 7, K = 9; // K 非 4 倍数 -> 覆盖 padding
  int8_t *A = malloc((size_t)M * K);
  int32_t *W = calloc((size_t)N * gemm_i8_k4(K), sizeof(int32_t));
  int32_t *C = malloc((size_t)M * N * sizeof(int32_t));
  int64_t *R = malloc((size_t)M * N * sizeof(int64_t));
  float *sa = malloc((size_t)M * sizeof(float)), *sb = malloc((size_t)N * sizeof(float));
  float *Cf = malloc((size_t)M * N * sizeof(float));

  // 用打包器布局生成 W: 每通道 K 个 int8 -> 打包字 (含 padding 0)
  for (int n = 0; n < N; n++)
    for (int k = 0; k < K; k++) {
      int8_t v = ri8();
      int k4 = k / 4, j = k % 4;
      W[(size_t)n * gemm_i8_k4(K) + k4] |= ((uint32_t)(uint8_t)v) << (8 * j);
    }
  for (int m = 0; m < M; m++) A[(size_t)m * K + m % 7] = ri8(); // 覆盖性: 随机
  for (int i = 0; i < M * K; i++) if (!(i % 3)) A[i] = ri8();
  for (int i = 0; i < M; i++) sa[i] = rf();
  for (int i = 0; i < N; i++) sb[i] = rf();

  gemm_i8_vdot_scalar(M, N, K, A, W, C, sa, sb, Cf);
  ref_naive(M, N, K, A, W, R);

  int errs = 0;
  for (int i = 0; i < M * N; i++) {
    if ((int64_t)C[i] != R[i]) {
      if (errs < 5) fprintf(stderr, "FAIL C[%d]=%d ref=%lld\n", i, C[i], (long long)R[i]);
      errs++;
    }
    float expect = (float)R[i] * sa[i / N] * sb[i % N];
    if (fabsf(Cf[i] - expect) > 1e-3f * fmaxf(1.0f, fabsf(expect))) {
      if (errs < 5) fprintf(stderr, "FAIL Cf[%d]=%g expect=%g\n", i, Cf[i], expect);
      errs++;
    }
  }
  free(A); free(W); free(C); free(R); free(sa); free(sb); free(Cf);
  if (errs) { fprintf(stderr, "gemm_i8_test: FAIL (%d errors)\n", errs); return 1; }
  printf("gemm_i8_test: PASS (M=%d,N=%d,K=%d, %d outputs, int32 + requant)\n", M, N, K, M * N);
  return 0;
}
