// ============================================================================
// Test 1/2: llvm/test/MC/RISCV/xkhmvdot-valid.s
//   MC round-trip tests for vdot.vv (assembly + disassembly).
//   Encodings verified with funct6=111001, vm=1, funct3=010, opcode=1010111:
//     vdot.vv v10,v9,v8  -> 0xe684a557  bytes [57,a5,84,e6]
//     vdot.vv v0,v1,v2   -> 0xe620a057  bytes [57,a0,20,e6]
//     vdot.vv v31,v30,v29-> 0xe7df2fd7  bytes [d7,2f,df,e7]
//     vdot.vv v16,v17,v18-> 0xe728a857  bytes [57,a8,28,e7]
//   Run: llvm-mc -triple riscv64 -mattr=+xkhmvdot -show-encoding < this file
// ============================================================================
# RUN: llvm-mc -triple riscv64 -mattr=+xkhmvdot -show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple riscv64 -mattr=+xkhmvdot -filetype=obj %s | # RUN:   llvm-objdump -d -M no-aliases - | FileCheck --check-prefix=DIS %s

# CHECK: vdot.vv v10, v9, v8              # encoding: [0x57,0xa5,0x84,0xe6]
# CHECK: vdot.vv v0, v1, v2               # encoding: [0x57,0xa0,0x20,0xe6]
# CHECK: vdot.vv v31, v30, v29            # encoding: [0xd7,0x2f,0xdf,0xe7]
# CHECK: vdot.vv v16, v17, v18            # encoding: [0x57,0xa8,0x28,0xe7]
vdot.vv v10, v9, v8
vdot.vv v0, v1, v2
vdot.vv v31, v30, v29
vdot.vv v16, v17, v18

# DIS: vdot.vv v10, v9, v8
# DIS: vdot.vv v0, v1, v2
# DIS: vdot.vv v31, v30, v29
# DIS: vdot.vv v16, v17, v18
