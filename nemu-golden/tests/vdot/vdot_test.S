# vdot.vv comprehensive self-test for NEMU (riscv64)
# Enables vector (mstatus.VS=Initial), runs vdot twice to verify accumulate.
# vs1 bytes = {01 02 03 04 | 05 06 07 08 | 09 0a 0b 0c | 0d 0e 0f 10}
# vs2 bytes = all 01
# lane0 = 0x0a, lane1 = 0x1a, lane2 = 0x2a, lane3 = 0x3a
# run vdot twice -> each lane doubled: 0x14, 0x34, 0x54, 0x74
# exit code = lane0 = 0x14 (20) if accumulate works

  .section .data
vs1_data:
  .byte 0x01,0x02,0x03,0x04, 0x05,0x06,0x07,0x08, 0x09,0x0a,0x0b,0x0c, 0x0d,0x0e,0x0f,0x10
vs2_data:
  .byte 0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01, 0x01,0x01,0x01,0x01

  .section .text.startup
  .globl _start
_start:
  csrr  t0, mstatus
  li    t1, (3 << 9)
  or    t0, t0, t1
  csrw  mstatus, t0

  vsetvli t0, x0, e32, m1
  la   a0, vs1_data
  vle32.v v8, (a0)
  la   a0, vs2_data
  vle32.v v9, (a0)
  vmv.v.i v10, 0
  .word 0xe684a557   # vdot.vv v10, v9, v8  (1st)
  .word 0xe684a557   # vdot.vv v10, v9, v8  (2nd, accumulate)
  vmv.x.s a0, v10
  .word 0x0000006b   # nemu_trap
