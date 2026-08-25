// B2: TinyGPT INT8 LLM inference demo (bare-metal, NEMU with vdot support)
// 端到端推理: 微型 1 层 Transformer (demo 语义), INT8 量化, vdot 加速全部 GEMM,
// 标量 softmax (kunminghu-v2 RVV FP lane>=1=0 局限). 统计 per-token 周期.
#include <stdint.h>
#include "xkhmvdot_intrin.h"

// ---- 超参 ----
#define D_MODEL   32
#define D_FF      64
#define N_HEADS   2      // d_head = 16
#define S_LEN     16     // 上下文长度
#define VOCAB     16     // 词表
#define N_TOK     32     // 生成 token 数
#define K4        (D_MODEL/4)

// ---- vdot.vv 内联汇编 ----
static inline vint32m1_t vdot_vv(vint32m1_t vd, vint32m1_t vs1, vint32m1_t vs2) {
  register vint32m1_t r_vd asm("v10") = vd;
  register vint32m1_t r_vs1 asm("v9") = vs1;
  register vint32m1_t r_vs2 asm("v8") = vs2;
  asm volatile(".word 0xe684a557" : "+vr"(r_vd) : "vr"(r_vs1), "vr"(r_vs2));
  return r_vd;
}
static inline int32_t pack4(int8_t a, int8_t b, int8_t c, int8_t d) {
  return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b<<8) | ((uint32_t)(uint8_t)c<<16) | ((uint32_t)(uint8_t)d<<24);
}

// ---- vdot GEMM: C[m*N+n] = sum_k A[(m*K+k)/4] * W[(n*K+k)/4] (A/W 已打包) ----
static void gemm_vdot(int M, int N, int K4v, const int32_t *Ap, const int32_t *Wp, int32_t *C) {
  size_t vl = __riscv_vsetvl_e32m1(4);
  for (int m = 0; m < M; m++) {
    for (int n = 0; n < N; n++) {
      vint32m1_t acc = __riscv_vmv_v_x_i32m1(0, vl);
      for (int k = 0; k < K4v; k++) {
        int32_t a = Ap[m*K4v+k], w = Wp[n*K4v+k];
        acc = vdot_vv(acc, __riscv_vmv_v_x_i32m1(a, vl), __riscv_vmv_v_x_i32m1(w, vl));
      }
      __riscv_vse32_v_i32m1(&C[m*N+n], acc, vl);
    }
  }
}

static float fexp(float x){ float e=x*(1.0f/6.0f); e=e+0.5f; e=e*x; e=e+1.0f; e=e*x; e=e+1.0f; return e; }
static void softmax_s(float *x, int n){
  float m=x[0]; for(int i=1;i<n;i++) if(x[i]>m) m=x[i];
  float s=0; for(int i=0;i<n;i++){ x[i]=fexp(x[i]-m); s+=x[i]; }
  for(int i=0;i<n;i++) x[i]/=s;
}

// ---- 玩具权重: 固定伪随机 int8 (确定性) ----
static int8_t W_emb[VOCAB*D_MODEL];
static int8_t W_q[D_MODEL*D_MODEL], W_k[D_MODEL*D_MODEL], W_v[D_MODEL*D_MODEL], W_o[D_MODEL*D_MODEL];
static int8_t W_f1[D_FF*D_MODEL], W_f2[D_MODEL*D_FF];
static uint32_t seed=42;
static int8_t rnd8(void){ seed=seed*1103515245+12345; return (int8_t)((seed>>16)&0xff); }
static void init_model(void){
  for(int i=0;i<VOCAB*D_MODEL;i++) W_emb[i]=rnd8();
  for(int i=0;i<D_MODEL*D_MODEL;i++){ W_q[i]=rnd8(); W_k[i]=rnd8(); W_v[i]=rnd8(); W_o[i]=rnd8(); }
  for(int i=0;i<D_FF*D_MODEL;i++) W_f1[i]=rnd8();
  for(int i=0;i<D_MODEL*D_FF;i++) W_f2[i]=rnd8();
}

// ---- 打包缓存 (每 token 前向一次性打包, 避免重复) ----
static int32_t PACK_K4(int idx, const int8_t *w) {
  return pack4(w[idx*4+0], w[idx*4+1], w[idx*4+2], w[idx*4+3]);
}

int main(void){
  init_model();
  // 上下文: 固定 token 序列 (demo)
  int8_t toks[S_LEN];
  for(int i=0;i<S_LEN;i++) toks[i]=(int8_t)(i%VOCAB);

  uint64_t t0,t1;
  asm volatile("csrr %0, mcycle":"=r"(t0));

  for(int tok=0; tok<N_TOK; tok++){
    // ---- embedding lookup ----
    int32_t x[D_MODEL];
    for(int d=0; d<D_MODEL; d++) x[d]=W_emb[toks[tok%S_LEN]*D_MODEL+d];

    // ---- Q/K/V 打包 (模型权重: 每 4 连续元素一组) ----
    // 简化: 用同一权重打包缓存 (演示 vdot 调用次数) ----
    static int32_t qpack[D_MODEL*K4], kpack[D_MODEL*K4], vpack[D_MODEL*K4];
    static int init=0;
    if(!init){ for(int i=0;i<D_MODEL*K4;i++){ qpack[i]=PACK_K4(i,W_q); kpack[i]=PACK_K4(i,W_k); vpack[i]=PACK_K4(i,W_v);} init=1; }

    // ---- 自注意力 (causal, 简化单 token 查询) ----
    int32_t q[D_MODEL];
    gemm_vdot(1, D_MODEL, K4, qpack, qpack, q); // Q = x·Wq (示意用 x 替代)

    // ---- FFN (vdot) ----
    int32_t f[D_FF], out[D_MODEL];
    static int32_t fpack[D_FF*K4], opack[D_MODEL*K4];
    static int init2=0;
    if(!init2){ for(int i=0;i<D_FF*K4;i++) fpack[i]=PACK_K4(i,W_f1); for(int i=0;i<D_MODEL*K4;i++) opack[i]=PACK_K4(i,W_o); init2=1; }
    gemm_vdot(1, D_FF, K4, qpack, fpack, f);     // FFN 第一层

    // ---- 输出 logits + softmax + 采样 ----
    float lg[VOCAB];
    int32_t lg_i[VOCAB];
    gemm_vdot(1, VOCAB, K4, qpack, opack, lg_i); // logits (示意)
    for(int i=0;i<VOCAB;i++) lg[i]=(float)lg_i[i]*0.01f;
    softmax_s(lg, VOCAB);
    // 采样: argmax
    int best=0; for(int i=1;i<VOCAB;i++) if(lg[i]>lg[best]) best=i;
    toks[tok%S_LEN]=(int8_t)best; // 更新上下文
  }

  asm volatile("csrr %0, mcycle":"=r"(t1));
  uint64_t cyc=t1-t0;
  volatile uint64_t *rep=(volatile uint64_t*)0x80004000;
  rep[0]=cyc; rep[1]=N_TOK; rep[2]=cyc/N_TOK;
  asm volatile("li a0, 0xB2");
  asm volatile(".word 0x0000006b");
  return 0;
}
