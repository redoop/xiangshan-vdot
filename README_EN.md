# XiangShan Vdot — A Custom RISC-V Vector Dot-Product Instruction for Edge LLM Inference

[中文](README.md) · [Design Paper](docs/vdot-paper.pdf) · [ISA Spec](docs/ISA.md) · [Microarchitecture](docs/microarch.md) · [Verification](docs/verification.md) · [Checklist](docs/checklist.md)

> Design, implementation, and validation of a custom vector dot-product instruction **`vdot.vv`** (extension `Xkhmvdot`, aligned with the official RISC-V `Zvdot4a8i`) on the XiangShan Kunminghu V2 (kunminghu-v2) high-performance out-of-order processor.

---

## 1. Overview

Transformer architectures are the computational backbone of large language models (LLM) and recommendation systems: the self-attention QKᵀ/AV matrix multiplies and the two GEMMs of the FFN are dominated by dot products. On resource-constrained edge devices, INT8 quantization (W8A8) is the mainstream deployment practice, yet **RVV 1.0 has no native "dot-product-accumulate over a group of INT8 elements" instruction**. Completing a single INT32 accumulation requires composing multiple instructions (unpack, sign-extend, widening multiply-accumulate, repack), which severely limits throughput.

**`vdot.vv`** performs a **4×INT8 dot product accumulated into an INT32 lane** in a single instruction, fully inheriting RVV masking, tail, vstart, and exception semantics. It aligns with the official `Zvdot4a8i` direction for smooth migration once the official extension is ratified.

### Key Results

| Metric | Result |
| --- | --- |
| INT8 GEMM instruction count | **12.7× fewer** (6,797 vs 86,554) |
| GEMM net cycles | **~8.2× faster** |
| vdot throughput / latency | 1.78 cyc/inst (independent) · 7.02 cyc (dependent chain) |
| End-to-end LLM inference | TinyGPT 13,615 instr/token (verified on NEMU) |
| Area overhead | 0.014% of full core (far below the ≤0.5% budget) |
| Verification | NEMU / Spike / RTL **three-way consistent** · 280/280 random difftest · 63 riscv-tests PASS |

## 2. Highlights

- **ISA design**: `vdot.vv` encoding funct6=111001 + funct3=010 (OPMVV) + opcode=1010111; 32-bit element = 4 packed INT8 (little-endian); SEW=32 constraint; wrap-around accumulation.
- **Microarchitecture**: compact 64-bit dot-product core (8× 8×8 multipliers + adder tree + 32-bit wrap-around accumulator, 2-stage pipeline, latency 2), minimally integrated into the VFEX0 execution unit reusing existing VPRF ports.
- **Software stack**: intrinsic header (C reference / RVV / inline-asm paths), INT8 weight packer, GEMM microkernel, LLVM MC-layer patch.
- **Three-way verification**: NEMU, Spike, and RTL (difftest) three-way consistent methodology.
- **Minimal area**: synthesized with Yosys + sky130, using only 0.014% of the full-core area.

## 3. Repository Layout

```
xiangshan-vdot/
├── software/                 # Software stack: intrinsics, weight packer, GEMM kernel, verif scripts
│   ├── include/xkhmvdot_intrin.h
│   ├── tools/weight_packer.c
│   ├── kernels/              # gemm_i8 + b2_tinygpt (LLM demo)
│   └── verif/                # diff scripts, random test generator, pipeline tracer
├── llvm-xkhmvdot/            # G4 LLVM MC-layer patch (vdot.vv asm/disasm support)
├── nemu-golden/              # G7 NEMU golden (vdot_instr impl + instruction-level tests)
├── spike-golden/             # G7 Spike golden patch + three-way consistency
├── b3-synth/                 # G6 B3 area/power/timing synthesis report
├── rtl/                      # Chisel RTL implementation patches (vdot.vv hw path: yunsuan + main repo, with CHANGES.md)
├── docs/                     # Design documents (ISA/microarch/software/verif/checklist)
├── vdot-paper.pdf            # Design paper (TeX source in vdot-paper.tex)
├── README.md                 # This document (Chinese)
└── README_EN.md              # English README
```

## 4. Quick Start

### 4.1 Software stack (no XiangShan environment required)

```bash
# intrinsic self-test (accumulate 10→20 and wrap-around)
gcc -O2 -I software/include -o /tmp/selftest \
    -Dmain=xkhmvdot_selftest_main software/include/xkhmvdot_intrin.h
/tmp/selftest && echo "selftest OK"

# GEMM self-test (random M/N/K incl. K%4≠0 padding, vs int64 reference)
gcc -O2 -I software/include -I software/kernels \
    -o /tmp/gemm_test software/kernels/gemm_i8_test.c software/kernels/gemm_i8.c
/tmp/gemm_test 42
```

### 4.2 End-to-end LLM demo (TinyGPT, on NEMU)

```bash
# Bare-metal micro 1-layer Transformer + INT8 quantization + vdot-accelerated GEMMs
# + scalar softmax; runs on vdot-enabled NEMU: 435,675 instr / 32 tokens (13,615 instr/token)
```

### 4.3 Hardware verification (requires XiangShan emu + difftest environment)

```bash
./software/verif/vdot_diff_check.sh --emu <XiangShan>/build/emu \
                                    --diff <NEMU>/build/riscv64-nemu-interpreter-so \
                                    --img <vdot_test.bin>
```

## 5. ISA Summary

| Field | Value | Description |
| --- | --- | --- |
| opcode | `1010111` | RVV OP-V primary opcode |
| funct6 | `111001` | Xkhmvdot extension (free in OP-V space, verified conflict-free) |
| funct3 | `010` | OPMVV (same class as the MAC family) |
| operands | vd, vs1, vs2 | vd is accumulator (read-write) |
| SEW constraint | must be e32 | otherwise illegal instruction |

**Semantics**: \[ \text{vd}_j \mathrel{+}= \sum_{i=0}^{3} \operatorname{sext8}(\text{vs1}_j[i]) \cdot \operatorname{sext8}(\text{vs2}_j[i]) \;\; (\bmod 2^{32}) \]

## 6. Verification Results

| Level | Method | Result |
| --- | --- | --- |
| L0 Instruction semantics | Vdot64bSpec fixed 4 + random 32 | ✅ all pass |
| L1 Hardware function | emu fixed 4 + random 280 difftest | ✅ 280/280 GOOD TRAP |
| L2 System integration | riscv-tests regression (Spike path) | ✅ 63 PASS |
| L3 Real workloads | GEMM / attention / LLM demo | ✅ all pass |

## 7. Related Links

- [Design paper (PDF)](vdot-paper.pdf) · [TeX source](vdot-paper.tex)
- XiangShan processor: [OpenXiangShan/XiangShan](https://github.com/OpenXiangShan/XiangShan)
- Official proposal: [Zvdot4a8i / Zvqdotq Ratification Plan](https://riscv.atlassian.net/wiki/spaces/PSXX/pages/766672912)

## 8. License

This project is a submission to the **2026 CIE National RISC-V High-Level Innovation & Application Contest**. Code and documents are provided for research/contest purposes; see the LICENSE files in each subdirectory and the design documents for implementation details.

---

> **Acknowledgements**: The XiangShan processor is maintained by the Institute of Computing Technology, CAS / the OpenXiangShan community. This extension is aligned with the official Zvdot4a8i proposal.
