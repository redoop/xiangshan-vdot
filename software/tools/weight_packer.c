// ============================================================================
// weight_packer.c — INT8 权重打包器 (vdot 数据视图: 32-bit = 4×INT8 打包)
//
// 用途 (04 §4.3): GEMM 权重在 kernel 前离线重排为 vdot 打包视图,
//   每个 32-bit 字 = 同一输出通道 K 维方向连续 4 个 INT8 (小端),
//   与 vdot.vv 的打包视图完全一致, 运行时零打包开销。
//
// 输入布局 (重要): N×K 通道主序 (n-major), 即 B[n][k]:
//   每个输出通道 n 的 K 个 INT8 在内存中连续 (K 维最快), 共 N 段。
//   (若手头是行主序 B[k][n], 先用转置工具或按 B[n*K+k] 重排)
//
// 用法:
//   gcc -O2 -o weight_packer weight_packer.c
//   ./weight_packer <K> <N> <in.bin> <out.bin> [scales.bin]
//      in.bin   : N*K 个 int8_t (n-major, B[n][k])
//      out.bin  : N*(K/4) 个 int32_t (packed[n][k4]), K 向上补齐到 4 的倍数(补0)
//      scales.bin (可选): N 个 float32, 每输出通道的对称量化 scale
//   ./weight_packer -t           : 自检 (含 padding 与 round-trip)
// ============================================================================
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 打包: 对每个输出通道 n, 将 K 个 int8 按 4 个一组装入 int32 (小端, 子元素 j 在位 [8j,8j+7])
// K 不足 4 的倍数时补 0 (对 int32 累加无影响, 02 §4.3)。
static void pack_weights(const int8_t *B /* N*K, n-major */, int K, int N,
                         int32_t *out /* N*K4 */, float *scales) {
  int K4 = (K + 3) / 4;
  for (int n = 0; n < N; n++) {
    const int8_t *bn = B + (size_t)n * K;
    float maxa = 1.0f;
    for (int k = 0; k < K; k++) {
      float a = (float)abs(bn[k]);
      if (a > maxa) maxa = a;
    }
    if (scales) scales[n] = maxa / 127.0f; // 对称量化 per-channel scale
    for (int k4 = 0; k4 < K4; k4++) {
      uint32_t w = 0;
      for (int j = 0; j < 4; j++) {
        int k = k4 * 4 + j;
        int8_t v = (k < K) ? bn[k] : 0; // padding 补 0
        w |= ((uint32_t)(uint8_t)v) << (8 * j);
      }
      out[(size_t)n * K4 + k4] = (int32_t)w;
    }
  }
}

// 反打包 (验证用): int32 打包字 -> 4 个 int8
static void unpack_word(int32_t w, int8_t o[4]) {
  o[0] = (int8_t)(w & 0xFF); o[1] = (int8_t)((w >> 8) & 0xFF);
  o[2] = (int8_t)((w >> 16) & 0xFF); o[3] = (int8_t)((w >> 24) & 0xFF);
}

static int selftest(void) {
  enum { K = 9, N = 2 }; // K 非 4 倍数, 验证 padding
  // 输入 N×K 通道主序: 通道0 = 前 9 个值, 通道1 = 后 9 个值
  int8_t B[K * N] = {1, -2, 3, -4, 5, -6, 7, -8, 9,
                     -1, 2, -3, 4, -5, 6, -7, 8, -9};
  int K4 = (K + 3) / 4;
  int32_t packed[N * K4];
  float scales[N];
  pack_weights(B, K, N, packed, scales);
  // 通道 0: 字0 = pack(1,-2,3,-4) = 0xFC03FE01; 字1 = pack(5,-6,7,-8) = 0xF807FA05
  //         字2 = pack(9,0,0,0) (padding) = 0x00000009
  if (packed[0] != (int32_t)0xFC03FE01u) { fprintf(stderr, "FAIL word0 %08x\n", packed[0]); return 1; }
  if (packed[1] != (int32_t)0xF807FA05u) { fprintf(stderr, "FAIL word1\n"); return 2; }
  if (packed[2] != 0x00000009) { fprintf(stderr, "FAIL word2 pad\n"); return 3; }
  // 通道 1: 字0 = pack(-1,2,-3,4) = 0x04FDFEFF
  if (packed[3] != (int32_t)0x04FD02FFu) { fprintf(stderr, "FAIL word3 sign\n"); return 4; }
  // scale: 通道0 max|w|=9 -> 9/127
  if (fabsf(scales[0] - 9.0f / 127.0f) > 1e-6f) { fprintf(stderr, "FAIL scale0\n"); return 5; }
  // round-trip
  int8_t o[4];
  unpack_word(packed[0], o);
  if (o[0] != 1 || o[1] != -2 || o[2] != 3 || o[3] != -4) { fprintf(stderr, "FAIL rt\n"); return 6; }
  printf("weight_packer selftest: PASS (K=%d,N=%d,K4=%d, n-major input)\n", K, N, K4);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "-t") == 0) return selftest();
  if (argc < 5 || argc > 6) {
    fprintf(stderr, "usage: %s <K> <N> <in.bin> <out.bin> [scales.bin]\n"
                    "       %s -t\n", argv[0], argv[0]);
    return 2;
  }
  int K = atoi(argv[1]), N = atoi(argv[2]);
  if (K <= 0 || N <= 0) { fprintf(stderr, "bad K/N\n"); return 2; }
  size_t nbytes = (size_t)K * N;
  int8_t *B = malloc(nbytes);
  FILE *fin = fopen(argv[3], "rb");
  if (!fin || fread(B, 1, nbytes, fin) != nbytes) {
    fprintf(stderr, "read %s failed (need %zu bytes)\n", argv[3], nbytes);
    return 2;
  }
  fclose(fin);
  int K4 = (K + 3) / 4;
  int32_t *packed = calloc((size_t)K4 * N, sizeof(int32_t));
  float *scales = malloc((size_t)N * sizeof(float));
  pack_weights(B, K, N, packed, scales);
  FILE *fo = fopen(argv[4], "wb");
  if (!fo || fwrite(packed, sizeof(int32_t), (size_t)K4 * N, fo) != (size_t)K4 * N) {
    fprintf(stderr, "write %s failed\n", argv[4]);
    return 2;
  }
  fclose(fo);
  if (argc == 6) {
    FILE *fs = fopen(argv[5], "wb");
    if (!fs || fwrite(scales, sizeof(float), (size_t)N, fs) != (size_t)N) {
      fprintf(stderr, "write %s failed\n", argv[5]);
      return 2;
    }
    fclose(fs);
  }
  printf("packed %zu bytes -> %s (%d words/col, scales%s)\n",
         nbytes, argv[4], K4, argc == 6 ? " saved" : " skipped");
  free(B); free(packed); free(scales);
  return 0;
}
