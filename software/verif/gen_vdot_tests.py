#!/usr/bin/env python3
# ============================================================================
# gen_vdot_tests.py — vdot 随机差分测试激励生成器
#
# 生成 RISC-V 汇编测试用例 (每例一个 .S), 覆盖 vdot.vv 的随机组合:
#   - vlmul: m1/m2/m4        (寄存器组 vs2=v8/vs1=v16/vd=v24 恒不重叠)
#   - vsew : e32 (合法, 85%) / e8,e16,e64 (非法, 15%)  -> 非法用例走 mtvec 处理
#   - vl   : 0 / 1 / VLMAX//2 / VLMAX-1 / VLMAX / 随机
#   - mask : vm=1 / vm=0 (v0 全0、全1、交替、随机)
#   - vstart: 0 (合法) / 少数 vstart!=0 (非法, 与昆明湖策略一致)
#   - 数据 : 随机 int8 + 极值 (127/-128/±1/0), 1~3 条连续 vdot 累加
# 每个用例自带预期 lane0 (Python 侧按 vdot 语义计算, wrap-around),
# 退出码 = vmv.x.s 提取的 lane0, 供 runner 自校验 (与 NEMU/emu 比对)。
#
# 用法:
#   gen_vdot_tests.py --seed <N> --cases <M> --outdir <dir> [--illegal-pct <P>]
# 输出: vdot_rand_<seed>_<i>.S  +  vdot_rand.ld(若无)  +  manifest.tsv
# ============================================================================
import argparse, os, random, sys

OP_V = 0x57
FUNCT3 = 0b010
FUNCT6 = 0b111001
NEMU_TRAP = 0x0000006b

def vdot_word(vd, vs1, vs2, vm=1):
    return (FUNCT6 << 26) | (vm << 25) | (vs2 << 20) | (vs1 << 15) | (FUNCT3 << 12) | (vd << 7) | OP_V

def sext8(x):
    return x - 256 if x >= 128 else x

def dot4(acc, a, b):
    # a/b: 打包 int32 (4×int8 小端)
    for j in range(4):
        acc = (acc + sext8((a >> (8*j)) & 0xFF) * sext8((b >> (8*j)) & 0xFF)) & 0xFFFFFFFF
    return acc if acc < 0x80000000 else acc - 0x100000000  # int32 wrap-around

def pack4(vals):
    w = 0
    for j, v in enumerate(vals):
        w |= (v & 0xFF) << (8*j)
    return w

def pack_mask_bytes(bits, nbytes):
    """RVV 掩码按位索引: 元素 i 用 v0 的 bit i。
    mask_bits 按位打包为 nbytes 字节 (小端, bit i -> 字节 i//8 的位 i%8), 高位补 0。"""
    out = bytearray(nbytes)
    for i, b in enumerate(bits):
        if b:
            out[i // 8] |= 1 << (i % 8)
    return out

def gen_data(rng, n_elems, pattern):
    """生成 n_elems 个打包 32-bit 元素 (每元素 4×int8)"""
    out = []
    for i in range(n_elems):
        if pattern == "ones":      vals = [1, 1, 1, 1]
        elif pattern == "seq":     vals = [(i*4+j+1) & 0xFF for j in range(4)]
        elif pattern == "extreme": vals = [rng.choice([127, -128, 1, -1, 0]) for _ in range(4)]
        else:                      vals = [rng.randint(-128, 127) for _ in range(4)]
        out.append(pack4(vals))
    return out

def gen_case(rng, idx, seed, illegal_pct):
    lmul = rng.choices([1, 2, 4], weights=[60, 25, 15])[0]
    vlmax = 4 * lmul  # e32 下 VLEN=128
    illegal_sew = (rng.random() * 100) < illegal_pct
    vsew = rng.choice([8, 16, 64]) if illegal_sew else 32
    vstart_nz = (not illegal_sew) and (rng.random() < 0.06)
    masked = (not illegal_sew) and (rng.random() < 0.40)
    vm = 0 if masked else 1
    vl = rng.choice([0, 1, max(1, vlmax//2), max(0, vlmax-1), vlmax, rng.randint(0, vlmax)])
    vl = max(0, min(vl, vlmax))

    n_elems = vl  # vle32.v 装载 vl 个 e32 元素; 打包视图每个元素 4×int8
    pat = rng.choice(["rand", "rand", "ones", "seq", "extreme"])
    vs1 = gen_data(rng, n_elems, pat)
    pat2 = rng.choice(["rand", "ones", "extreme"])
    vs2 = gen_data(rng, n_elems, pat2)
    init_vd = rng.choice([0, 0, 0, rng.randint(-16, 15)])  # 多数从 0 开始; vmv.v.i 立即数限 -16..15
    n_dot = rng.randint(1, 3)

    # 掩码位 (按 32-bit 元素, 最多 vlmax 位)
    if masked:
        m = rng.choice(["all0", "all1", "alt", "rand"])
        mask_bits = [rng.randint(0,1) for _ in range(vlmax)] if m == "rand" else \
                    ([1]*vlmax if m == "all1" else ([0]*vlmax if m == "all0" else [(i%2) for i in range(vlmax)]))
    else:
        mask_bits = [1]*vlmax

    # 期望 lane0 (vdot 语义, 含 mask/累加次数/vstart 非法)
    # 注意: vmv.v.i 受 vl 约束 (vl=0 不写任何元素), 且非法用例(vsew!=e32)下
    #       vmv.v.i 按该 SEW 语义写 vd —— 因此 vl=0 / 非法 / vstart_nz 用例
    #       统一跳过 vd 初始化, vd 保持复位值 0 (NEMU 与 RTL 均从 0 开始)。
    no_init = (illegal_sew or vstart_nz or vl == 0)
    if no_init:
        expected = 0
    else:
        acc = init_vd & 0xFFFFFFFF
        for _ in range(n_dot):
            if mask_bits[0]:
                acc = dot4(acc, vs1[0], vs2[0])
        expected = acc if acc < 0x80000000 else acc - 0x100000000

    return dict(idx=idx, seed=seed, lmul=lmul, vsew=vsew, vl=vl, vm=vm,
                vstart_nz=vstart_nz, mask_bits=mask_bits, vs1=vs1, vs2=vs2,
                init_vd=init_vd, n_dot=n_dot, no_init=no_init, expected=expected)

def asm_emit(c, rng):
    vs1_b, vs2_b, vd_b = 16, 8, 24   # 恒不重叠的寄存器组基址
    L = []
    L.append(f"# vdot random case {c['idx']} (seed {c['seed']})")
    L.append(f"# lmul=m{c['lmul']} vsew=e{c['vsew']} vl={c['vl']} vm={c['vm']} "
             f"vstart_nz={int(c['vstart_nz'])} n_dot={c['n_dot']} expected_lane0={c['expected']}")
    L.append("  .section .data")
    L.append("vs1_data:")
    for w in c["vs1"]:
        L.append(f"  .word 0x{w & 0xFFFFFFFF:08x}")
    L.append("vs2_data:")
    for w in c["vs2"]:
        L.append(f"  .word 0x{w & 0xFFFFFFFF:08x}")
    L.append("mask_data:")
    nbits = len(c["mask_bits"])
    mbytes = list(pack_mask_bytes(c["mask_bits"], (nbits + 7) // 8))
    mbytes += [0] * (nbits - len(mbytes))   # 补齐到 vlmax 字节, 防 vle8 读越界
    L.append(f"  .byte {','.join(str(b) for b in mbytes)}   # 位打包 (元素 i -> bit i)")
    L.append("")
    L.append("  .section .text.startup")
    L.append("  .globl _start")
    L.append("_start:")
    L.append("  # 使能向量: mstatus.VS=Initial")
    L.append("  csrr  t0, mstatus")
    L.append("  li    t1, (3 << 9)")
    L.append("  or    t0, t0, t1")
    L.append("  csrw  mstatus, t0")
    L.append("  # 设置 mtvec -> trap 处理 (非法指令/异常时跳过并继续)")
    L.append("  la    t0, trap_handler")
    L.append("  csrw  mtvec, t0")
    L.append(f"  li    t1, {c['vl']}          # 实际 vl (x0 是 VLMAX 特例, 不能用)")
    L.append(f"  vsetvli t0, t1, e{c['vsew']}, m{c['lmul']}")
    if c["vstart_nz"]:
        L.append("  li    t1, 1")
        L.append("  csrw  vstart, t1     # vstart!=0 -> illegal (昆明湖策略)")
    L.append("  la    a0, vs1_data")
    L.append(f"  vle32.v v{vs1_b}, (a0)")
    L.append("  la    a0, vs2_data")
    L.append(f"  vle32.v v{vs2_b}, (a0)")
    if not c["no_init"]:
        L.append(f"  vmv.v.i v{vd_b}, {c['init_vd']}   # vd 初值 (vl>=1 才生效; 非法/vl=0 用例跳过, vd=0)")
    if not c["vm"]:
        L.append("  la    a0, mask_data")
        L.append("  vle8.v v0, (a0)      # v0 掩码 (按 32-bit 元素)")
    for i in range(c["n_dot"]):
        L.append(f"  .word 0x{vdot_word(vd_b, vs1_b, vs2_b, vm=c['vm']):08x}   # vdot.vv v{vd_b}, v{vs1_b}, v{vs2_b}")
    L.append(f"  vmv.x.s a0, v{vd_b}     # lane0")
    # 软件自校验: lane0 == 期望 -> a0=0x5A5A(PASS); 否则 a0=0xDEAD(FAIL)
    # 注意: 不能用原始 lane0 作退出码 —— NEMU nemu_trap 对 a0==0x100/0x101 有特殊语义(不退出)
    L.append(f"  li    t2, {c['expected']}          # 期望 lane0 (生成器计算)")
    L.append("  beq   a0, t2, 1f")
    L.append("  li    a0, 0xDEAD")
    L.append(f"  .word 0x{NEMU_TRAP:08x} # nemu_trap (FAIL)")
    L.append("1:")
    L.append("  li    a0, 0x5A5A")
    L.append(f"  .word 0x{NEMU_TRAP:08x} # nemu_trap (PASS)")
    L.append("")
    L.append("  # 简易 trap 处理: mepc+=4 后 mret (非法指令等继续执行)")
    L.append("  .align 2")
    L.append("trap_handler:")
    L.append("  csrr  t0, mepc")
    L.append("  addi  t0, t0, 4")
    L.append("  csrw  mepc, t0")
    L.append("  mret")
    return "\n".join(L)

# ---------------------------------------------------------------------------
# 定向边界用例 (--directed): 回绕/极值/掩码部分命中/尾元素(vta)/掩码宽松(vma)/非法
# 每个用例自校验全部 4 个 lane (VLMAX=4, m1), 退出码 PASS=0x5A5A / FAIL=0xDEAD
# 语义依据: NEMU 构建 CONFIG_RVV_AGNOSTIC=y 且香山 Mgu agnosticEn=全1 -> vta/vma 一致
#   (02 §4.4: 尾/掩码 agnostic 填全 1; undisturbed 保留旧值)
# ---------------------------------------------------------------------------
def P(*b):  # 4 个 int8 -> 打包 int32 (小端, 子元素 j 在位 [8j,8j+7])
    w = 0
    for j, v in enumerate(b):
        w |= (v & 0xFF) << (8 * j)
    return w

DIRECTED_CASES = [
    # --- 回绕 (02 §4.3: 模 2^32 wrap-around) ---
    dict(name="d01_wrap_ovf", vl=4, vsew=32, ta="tu", vm=1, vstart_nz=False,
         vd=[0x7FFFFFFF, 1, 2, 3],
         vs1=[P(1,0,0,0), 0, 0, 0], vs2=[P(1,0,0,0), 0, 0, 0], n=1,
         exp=[0x80000000, 1, 2, 3]),                     # 0x7FFFFFFF+1 -> 0x80000000
    dict(name="d02_wrap_udf", vl=4, vsew=32, ta="tu", vm=1, vstart_nz=False,
         vd=[0x80000000, 5, 6, 7],
         vs1=[P(-1,0,0,0), 0, 0, 0], vs2=[P(1,0,0,0), 0, 0, 0], n=1,
         exp=[0x7FFFFFFF, 5, 6, 7]),                     # 0x80000000-1 -> 0x7FFFFFFF
    # --- 极值 (指令内中间和 16 位无溢出, 跨指令回绕) ---
    dict(name="d03_all127_x2", vl=4, vsew=32, ta="tu", vm=1, vstart_nz=False,
         vd=[0, 0, 0, 0],
         vs1=[P(127,127,127,127)]*4, vs2=[P(127,127,127,127)]*4, n=2,
         exp=[129032]*4),                                # 2 * 4*127*127
    dict(name="d04_allm128_x2", vl=4, vsew=32, ta="tu", vm=1, vstart_nz=False,
         vd=[0, 0, 0, 0],
         vs1=[P(-128,-128,-128,-128)]*4, vs2=[P(-128,-128,-128,-128)]*4, n=2,
         exp=[131072]*4),                                # 2 * 4*(-128)*(-128)
    # --- 掩码部分命中 (只更新 lane2) ---
    dict(name="d05_mask_lane2", vl=4, vsew=32, ta="tu", vm=0, vstart_nz=False,
         vd=[10, 20, 30, 40], mask=[0, 0, 1, 0],
         vs1=[0, 0, P(1,1,1,1), 0], vs2=[0, 0, P(1,1,1,1), 0], n=1,
         exp=[10, 20, 34, 40]),                          # lane2: 30+4=34
    # --- 尾元素: vl=2, undisturbed (vta=tu) ---
    dict(name="d06_tail_tu", vl=2, vsew=32, ta="tu", vm=1, vstart_nz=False,
         vd=[100, 200, 300, 400],
         vs1=[P(1,2,3,4), P(1,2,3,4), 0, 0], vs2=[P(1,1,1,1), P(1,1,1,1), 0, 0], n=1,
         exp=[110, 210, 300, 400]),                      # lane0/1 += 10; lane2/3 保留
    # --- 尾元素: vl=2, agnostic (vta=ta) -> 尾填全 1 (-1) ---
    dict(name="d07_tail_ta", vl=2, vsew=32, ta="ta", vm=1, vstart_nz=False,
         vd=[100, 200, 300, 400],
         vs1=[P(1,2,3,4), P(1,2,3,4), 0, 0], vs2=[P(1,1,1,1), P(1,1,1,1), 0, 0], n=1,
         exp=[110, 210, -1, -1]),
    # --- 掩码宽松 (vma=ma): lane0 掩码关 -> 全 1 (-1) ---
    dict(name="d08_mask_agnostic", vl=4, vsew=32, ta="tu", vm=0, vstart_nz=False,
         vd=[1, 2, 3, 4], mask=[0, 1, 1, 1], ma=True,
         vs1=[P(1,1,1,1)]*4, vs2=[P(1,1,1,1)]*4, n=1,
         exp=[-1, 6, 7, 8]),                              # lane1..3: init+4
    # --- 非法: vstart!=0 -> vd 不变 ---
    dict(name="d09_vstart_nz", vl=4, vsew=32, ta="tu", vm=1, vstart_nz=True,
         vd=[1, 2, 3, 4], vs1=[P(1,1,1,1)]*4, vs2=[P(1,1,1,1)]*4, n=1,
         exp=[1, 2, 3, 4]),
    # --- 非法: vsew=e8 -> vd 不变 ---
    dict(name="d10_vsew_e8", vl=4, vsew=8, ta="tu", vm=1, vstart_nz=False,
         vd=[11, 22, 33, 44], vs1=[P(1,1,1,1)]*4, vs2=[P(1,1,1,1)]*4, n=1,
         exp=[11, 22, 33, 44]),
    # --- 混合: vl=3, 掩码[1,0,1,0], 2 次累加, vta=tu ---
    dict(name="d11_mixed", vl=3, vsew=32, ta="tu", vm=0, vstart_nz=False,
         vd=[10, 20, 30, 40], mask=[1, 0, 1, 0],
         vs1=[P(1,2,3,4), P(1,2,3,4), P(1,2,3,4), 0],
         vs2=[P(1,1,1,1), P(1,1,1,1), P(1,1,1,1), 0], n=2,
         exp=[30, 20, 50, 40]),                           # lane0: 10+10*2=30; lane2: 30+10*2=50
    # --- 4 lane 各自独立点积 ---
    dict(name="d12_all4", vl=4, vsew=32, ta="tu", vm=1, vstart_nz=False,
         vd=[0, 0, 0, 0],
         vs1=[P(1,1,1,1)]*4, vs2=[P(1,0,0,0), P(2,0,0,0), P(3,0,0,0), P(4,0,0,0)], n=1,
         exp=[1, 2, 3, 4]),
]

def emit_directed(c):
    vs1_b, vs2_b, vd_b, tmp_b = 16, 8, 24, 28
    L = []
    L.append(f"# vdot directed: {c['name']}  vl={c['vl']} vsew=e{c['vsew']} ta={c['ta']} "
             f"vm={c['vm']} vstart_nz={int(c['vstart_nz'])}  exp={c['exp']}")
    L.append("  .section .data")
    L.append("vd_data:")
    for w in c["vd"]:
        L.append(f"  .word 0x{w & 0xFFFFFFFF:08x}")
    L.append("vs1_data:")
    for w in c["vs1"]:
        L.append(f"  .word 0x{w & 0xFFFFFFFF:08x}")
    L.append("vs2_data:")
    for w in c["vs2"]:
        L.append(f"  .word 0x{w & 0xFFFFFFFF:08x}")
    if c["vm"] == 0:
        L.append("mask_data:")
        mbytes = pack_mask_bytes(c.get('mask', [0]*4), 1)   # VLMAX=4, 1 字节足够
        L.append(f"  .byte {','.join(str(b) for b in mbytes)},0,0,0   # 位打包 (元素 i -> bit i)")
        L.append("")
    L.append("  .section .text.startup")
    L.append("  .globl _start")
    L.append("_start:")
    L.append("  csrr  t0, mstatus")
    L.append("  li    t1, (3 << 9)")
    L.append("  or    t0, t0, t1")
    L.append("  csrw  mstatus, t0")
    L.append("  la    t0, trap_handler")
    L.append("  csrw  mtvec, t0")
    # 1) 初始化 vd 全部 4 lane (e32, vl=4)
    L.append("  li    t1, 4")
    L.append("  vsetvli t0, t1, e32, m1")
    L.append("  la    a0, vd_data")
    L.append(f"  vle32.v v{vd_b}, (a0)")
    # 2) 载入 vs1/vs2 (用 e32 vl=4 载入即可; 非法 vsew 用例的 vdot 不执行, 值无所谓)
    L.append("  la    a0, vs1_data")
    L.append(f"  vle32.v v{vs1_b}, (a0)")
    L.append("  la    a0, vs2_data")
    L.append(f"  vle32.v v{vs2_b}, (a0)")
    # 3) 设置用例 vl 与策略 (vta/vma)
    if c["vsew"] == 32:
        ma = "ma" if c.get("ma") else "mu"
        L.append(f"  li    t1, {c['vl']}")
        L.append(f"  vsetvli t0, t1, e32, m1, {c['ta']}, {ma}")
        if c["vstart_nz"]:
            L.append("  li    t1, 1")
            L.append("  csrw  vstart, t1     # vstart!=0 -> illegal")
        if c["vm"] == 0:
            L.append("  la    a0, mask_data")
            L.append("  vle8.v v0, (a0)      # v0 掩码")
        for i in range(c["n"]):
            L.append(f"  .word 0x{vdot_word(vd_b, vs1_b, vs2_b, vm=c['vm']):08x}   # vdot.vv")
    else:
        # 非法 vsew: 直接切到 e8 后执行 vdot (vle32 载入的数据保持)
        L.append(f"  li    t1, {4 * (32 // c['vsew'])}")
        L.append(f"  vsetvli t0, t1, e{c['vsew']}, m1")
        L.append(f"  .word 0x{vdot_word(vd_b, vs1_b, vs2_b, vm=c['vm']):08x}   # vdot.vv (illegal)")
    # 4) 全 lane 自校验 (lane0 直接取 vd; lane1..3 用 vslidedown 取)
    L.append("  li    t1, 4")
    L.append("  vsetvli t0, t1, e32, m1      # 复位 vl 供 slide")
    for i in range(4):
        if i == 0:
            L.append(f"  vmv.x.s a0, v{vd_b}           # lane0")
        else:
            L.append(f"  li    t3, {i}")
            L.append(f"  vslidedown.vx v{tmp_b}, v{vd_b}, t3")
            L.append(f"  vmv.x.s a0, v{tmp_b}          # lane{i}")
        s32 = c['exp'][i] if c['exp'][i] < 0x80000000 else c['exp'][i] - 0x100000000
        L.append(f"  li    t2, {s32}   # lane{i} 期望 (有符号, 匹配 vmv.x.s 符号扩展)")
        L.append("  beq   a0, t2, 1f")
        L.append("  li    a0, 0xDEAD")
        L.append(f"  .word 0x{NEMU_TRAP:08x} # nemu_trap (FAIL)")
        L.append("1:")
    L.append("  li    a0, 0x5A5A")
    L.append(f"  .word 0x{NEMU_TRAP:08x} # nemu_trap (PASS)")
    L.append("")
    L.append("  # 简易 trap 处理: mepc+=4 后 mret")
    L.append("  .align 2")
    L.append("trap_handler:")
    L.append("  csrr  t0, mepc")
    L.append("  addi  t0, t0, 4")
    L.append("  csrw  mepc, t0")
    L.append("  mret")
    return "\n".join(L)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--cases", type=int, default=50)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--illegal-pct", type=float, default=15.0)
    ap.add_argument("--directed", action="store_true", help="生成定向边界用例(回绕/极值/掩码/尾元素/非法)")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    rng = random.Random(a.seed)
    ld = os.path.join(a.outdir, "vdot_rand.ld")
    if not os.path.exists(ld):
        with open(ld, "w") as f:
            f.write("OUTPUT_ARCH(riscv)\nENTRY(_start)\nSECTIONS {\n"
                    "  . = 0x80000000;\n  .text : { *(.text.startup) *(.text*) }\n"
                    "  . = ALIGN(16);\n  .data : { *(.data*) }\n  .bss : { *(.bss*) }\n}\n")
    manifest = []
    if a.directed:
        for c in DIRECTED_CASES:
            fn = os.path.join(a.outdir, f"vdot_dir_{c['name']}.S")
            with open(fn, "w") as f:
                f.write(emit_directed(c) + "\n")
            manifest.append(f"{c['name']}\t23130\t{c['exp'][0]}\t{','.join(str(x) for x in c['exp'])}")
        header = "case\tpass_code\tlane0_expected\tlanes_expected\n"
    else:
        for i in range(a.cases):
            c = gen_case(rng, i, a.seed, a.illegal_pct)
            fn = os.path.join(a.outdir, f"vdot_rand_{a.seed}_{i:04d}.S")
            with open(fn, "w") as f:
                f.write(asm_emit(c, rng) + "\n")
            manifest.append(f"{i}\t23130\t{c['expected']}\t{c['lmul']}\t{c['vsew']}\t{c['vl']}\t{c['vm']}\t{int(c['vstart_nz'])}")
        header = "case\tpass_code\tlane0_expected\tlmul\tvsew\tvl\tvm\tvstart_nz\n"
    with open(os.path.join(a.outdir, "manifest.tsv"), "w") as f:
        f.write(header + "\n".join(manifest) + "\n")
    print(f"generated {'directed' if a.directed else a.cases} cases (seed={a.seed}) -> {a.outdir}")

if __name__ == "__main__":
    main()
