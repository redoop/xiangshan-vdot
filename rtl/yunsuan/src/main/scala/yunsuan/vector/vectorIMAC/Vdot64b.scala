package yunsuan.vector.mac

import chisel3._
import chisel3.util._
import yunsuan.vector._
import yunsuan.util._

/** 64-bit vector dot-product unit for the custom vdot.vv instruction.
  *
  * Semantics (per the contest design report):
  *   vdot.vv vd, vs1, vs2  (INT8 x INT8 -> INT32, accumulate)
  *
  * Each 64-bit slice of vs1/vs2 holds 8 INT8 elements. The 64-bit core
  * computes 2 independent dot products of 4 consecutive elements each:
  *   lane0 = oldVd[31:0]   + sum_{i=0..3} sext8(vs1[8i+7:8i]) * sext8(vs2[8i+7:8i])
  *   lane1 = oldVd[63:32]  + sum_{i=0..3} sext8(vs1[8(4+i)+7:8(4+i)]) * sext8(vs2[8(4+i)+7:8(4+i)])
  * with wrap-around (no saturation) accumulation.
  *
  * Pipeline: 2 register stages, matching [[yunsuan.vector.mac.VIMac64b]]
  * stage count so the FU latency stays consistent (VdotCfg.latency =
  * CertainLatency(2)):
  *   stage 1 (fire)  : lock vs1/vs2/oldVd inputs (cycle T)
  *   comb            : dot products + partial sums + lane sums + accumulate
  *   stage 2 (fireS1): lock the accumulated 64-bit vd result (cycle T+1)
  * io.vd is therefore valid two cycles after io.fire, aligned with the
  * VecPipedFuncUnit outData/outCtrl pipeline. A fully-combinational core
  * presents its result in the fire cycle while the wrapper consumes it two
  * cycles later, desynchronizing oldVd from vs1/vs2 and dropping the
  * accumulation (oldVd reads as 0).
  */
class Vdot64b extends Module {
  val io = IO(new Bundle {
    val fire = Input(Bool())
    val info = Input(new VIFuInfo)
    val srcType = Input(Vec(2, UInt(4.W)))
    val vdType  = Input(UInt(4.W))
    val vs1 = Input(UInt(64.W))
    val vs2 = Input(UInt(64.W))
    val oldVd = Input(UInt(64.W))

    val vd = Output(UInt(64.W))
  })

  val fire   = io.fire
  val fireS1 = GatedValidRegNext(fire)

  // -------- Stage 1: lock inputs at fire (cycle T) --------
  val vs1Reg   = RegEnable(io.vs1,   fire)
  val vs2Reg   = RegEnable(io.vs2,   fire)
  val oldVdReg = RegEnable(io.oldVd, fire)

  // -------- Combinational dot product from stage-1 registers --------
  // 8 signed 8-bit multiplies; each product fits in 16 bits (signed).
  val vs1Bytes = vs1Reg.asTypeOf(Vec(8, UInt(8.W)))
  val vs2Bytes = vs2Reg.asTypeOf(Vec(8, UInt(8.W)))
  val products = Wire(Vec(8, SInt(16.W)))
  for (i <- 0 until 8) {
    products(i) := vs1Bytes(i).asSInt * vs2Bytes(i).asSInt
  }
  // 4 partial sums of 2 products each: 17 bits (signed) is enough
  // (2 * (127 * -128) worst case fits in 17b).
  val partialSum = Wire(Vec(4, SInt(17.W)))
  for (j <- 0 until 4) {
    partialSum(j) := products(2 * j) +& products(2 * j + 1)
  }
  // Each 32-bit lane = sum of two partial sums (18 bits) + oldVd lane (32 bits).
  val oldVdLanes = oldVdReg.asTypeOf(Vec(2, UInt(32.W)))
  val laneSum = Wire(Vec(2, SInt(18.W)))
  for (j <- 0 until 2) {
    laneSum(j) := partialSum(2 * j) +& partialSum(2 * j + 1)
  }
  val vd = Wire(Vec(2, UInt(32.W)))
  for (j <- 0 until 2) {
    // wrap-around accumulate: sign-extend the signed lane sum to 32 bits,
    // then two's-complement add with oldVd (asUInt alone would zero-extend
    // negative sums and corrupt the result). SInt.pad sign-extends.
    vd(j) := (oldVdLanes(j) +& laneSum(j).pad(32).asUInt)(31, 0)
  }

  // -------- Stage 2: lock result at fireS1 (cycle T+1 -> valid at T+2) --------
  io.vd := RegEnable(VecInit(Seq.tabulate(2)(i => vd(i))).asUInt, fireS1)
}
