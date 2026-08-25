// ============================================================================
// Test 2/2: llvm/test/CodeGen/RISCV/rvv-intrinsics/xkhmvdot-vv.ll
//   CodeGen test: __riscv_vdot_vv_i32m1 lowers to vdot.vv.
//   Run: llc -mtriple=riscv64 -mattr=+m,+f,+d,+v,+xkhmvdot < this file
// ============================================================================
; RUN: llc -mtriple=riscv64 -mattr=+m,+f,+d,+v,+xkhmvdot -verify-machineinstrs ; RUN:   < %s | FileCheck %s

define <vscale x 4 x i32> @test_vdot_vv_i32m1(<vscale x 4 x i32> %vd,
                                              <vscale x 4 x i32> %vs1,
                                              <vscale x 4 x i32> %vs2,
                                              i64 %vl) {
; CHECK-LABEL: test_vdot_vv_i32m1:
; CHECK: vsetvli a0, a0, e32, m1, ta, ma
; CHECK: vdot.vv v8, v9, v10
; CHECK-NEXT: ret
  %res = call <vscale x 4 x i32> @llvm.riscv.vdot.vv.nxv4i32(<vscale x 4 x i32> %vd,
                                                              <vscale x 4 x i32> %vs1,
                                                              <vscale x 4 x i32> %vs2,
                                                              i64 %vl)
  ret <vscale x 4 x i32> %res
}

declare <vscale x 4 x i32> @llvm.riscv.vdot.vv.nxv4i32(<vscale x 4 x i32>,
                                                        <vscale x 4 x i32>,
                                                        <vscale x 4 x i32>,
                                                        i64)

; NOTE: exact scalable type nxv4i32 and intrinsic mangling must match this
; LLVM version's RVV conventions (see vwmaccu_vv tests in the same dir).
