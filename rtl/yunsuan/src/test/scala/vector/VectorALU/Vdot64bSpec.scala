
package yunsuan.vectortest.mac

import chiseltest._
import chisel3.util._
import org.scalatest.flatspec.AnyFlatSpec
import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.VecLiterals._
import yunsuan.vector._
import yunsuan.vectortest._
import chiseltest.WriteVcdAnnotation
import yunsuan.vectortest.dataType._
import yunsuan.vector.mac._

class Vdot64bInput extends Bundle {
  val info = new VIFuInfo
  val srcType = Vec(2, UInt(4.W))
  val vdType  = UInt(4.W)
  val vs1 = UInt(64.W)
  val vs2 = UInt(64.W)
  val oldVd = UInt(64.W)
}
class Vdot64bOutput extends Bundle {
  val vd = UInt(64.W)
}

class Vdot64bWrapper extends Module {
  val io = IO(new Bundle {
    val in = Flipped(Decoupled(new Vdot64bInput))
    val out = Decoupled(new Vdot64bOutput)
  })

  val vdot = Module(new Vdot64b)
  vdot.io.fire := io.in.valid
  vdot.io.info := io.in.bits.info
  vdot.io.srcType := io.in.bits.srcType
  vdot.io.vdType := io.in.bits.vdType
  vdot.io.vs1 := io.in.bits.vs1
  vdot.io.vs2 := io.in.bits.vs2
  vdot.io.oldVd := io.in.bits.oldVd

  io.out.bits.vd := vdot.io.vd
  // two register stages inside Vdot64b (products, accumulate)
  io.out.valid := RegNext(RegNext(io.in.valid))
  io.in.ready := io.out.ready
}

/** Reference model: 64-bit slice = 8 INT8 per operand, 2 lanes of
  * 4-element dot products accumulated with oldVd (wrap-around).
  */
object Vdot64bRef {
  def apply(vs1: String, vs2: String, oldVd: String): BigInt = {
    val v1 = BigInt(vs1.stripPrefix("h"), 16)
    val v2 = BigInt(vs2.stripPrefix("h"), 16)
    val ov = BigInt(oldVd.stripPrefix("h"), 16)
    def byte(x: BigInt, i: Int): BigInt = {
      val b = (x >> (8 * i)) & 0xff
      if (b >= 128) b - 256 else b // sext8
    }
    def dot(lo: Int): BigInt = {
      (0 until 4).map(i => byte(v1, lo + i) * byte(v2, lo + i)).sum
    }
    def lane(j: Int): BigInt = {
      (((ov >> (32 * j)) & 0xffffffffL) + dot(4 * j)) & 0xffffffffL
    }
    (lane(1) << 32) | lane(0)
  }
}

trait Vdot64bBehavior {
  this: AnyFlatSpec with ChiselScalatestTester with BundleGenHelper =>

  def genVdot64bInput(vs1: String, vs2: String, oldVd: String) = {
    (new Vdot64bInput).Lit(
      _.info -> (new VIFuInfo).Lit(
                 _.vm -> true.B,
                 _.ma -> true.B,
                 _.ta -> true.B,
                 _.vlmul -> 0.U,
                 _.vl -> 0.U,
                 _.vstart -> 0.U,
                 _.uopIdx -> 0.U,
                 _.vxrm -> 0.U),
      _.srcType -> Vec.Lit(0.U(4.W), 0.U(4.W)),
      _.vdType -> 0.U,
      _.vs1 -> vs1.U(64.W),
      _.vs2 -> vs2.U(64.W),
      _.oldVd -> oldVd.U(64.W),
    )
  }
  def genVdot64bOutput(vd: String) = {
    (new Vdot64bOutput).Lit(
      _.vd -> vd.U(64.W)
    )
  }

  def vdotTest0(): Unit = {
    it should "pass the INT8 x INT8 -> INT32 dot product (accumulate)" in {
      test(new Vdot64bWrapper).withAnnotations(Seq(WriteVcdAnnotation)) { dut =>
        dut.clock.setTimeout(20000)
        dut.io.in.initSource()
        dut.io.out.initSink()
        dut.io.out.ready.poke(true.B)

        // doc column: expected vd (LSB-first byte order inside each 64-bit slice)
        //   lane0 = bytes[0..3], lane1 = bytes[4..7]
        //   e.g. vs1 = 0x0102030405060708 -> bytes {08,07,06,05,04,03,02,01}
        //   lane0 = 08+07+06+05 = 0x1a, lane1 = 04+03+02+01 = 0x0a
        val cases = Seq(
          // all ones * consecutive bytes
          ("h0102030405060708", "h0101010101010101", "h0000000000000000", "h0000000a0000001a"),
          // negative byte (0xff = -1) in lane1's last element
          ("hff02030405060708", "h0101010101010101", "h0000000000000000", "h000000080000001a"),
          // accumulate with non-zero oldVd
          ("hff02030405060708", "h0101010101010101", "h0000000500000005", "h0000000d0000001f"),
          // larger magnitudes, wrap-around check: 4*127*127 + 0xffffffff -> 0x0000fc03
          ("h7f7f7f7f7f7f7f7f", "h7f7f7f7f7f7f7f7f", "hffffffffffffffff", "h0000fc030000fc03"),
        )
        val inputSeq = cases.map { case (a, b, c, _) => genVdot64bInput(a, b, c) }
        val outputSeq = cases.map { case (a, b, c, _) => genVdot64bOutput("h" + Vdot64bRef(a, b, c).toString(16)) }

        fork {
          dut.io.in.enqueueSeq(inputSeq)
        }.fork {
          dut.io.out.expectDequeueSeq(outputSeq)
        }.join()
        dut.clock.step(1)
      }
    }
  }

  def vdotTest1(): Unit = {
    it should "match the reference model on random inputs" in {
      test(new Vdot64bWrapper).withAnnotations(Seq(WriteVcdAnnotation)) { dut =>
        dut.clock.setTimeout(20000)
        dut.io.in.initSource()
        dut.io.out.initSink()
        dut.io.out.ready.poke(true.B)

        val rnd = new scala.util.Random(42)
        val inputSeq = Seq.tabulate(32) { _ =>
          val vs1 = "h" + BigInt(64, rnd).toString(16)
          val vs2 = "h" + BigInt(64, rnd).toString(16)
          val oldVd = "h" + BigInt(64, rnd).toString(16)
          genVdot64bInput(vs1, vs2, oldVd)
        }
        val outputSeq = inputSeq.map { in =>
          val a = in.vs1.litValue.toString(16)
          val b = in.vs2.litValue.toString(16)
          val c = in.oldVd.litValue.toString(16)
          genVdot64bOutput("h" + Vdot64bRef("h" + a, "h" + b, "h" + c).toString(16))
        }

        fork {
          dut.io.in.enqueueSeq(inputSeq)
        }.fork {
          dut.io.out.expectDequeueSeq(outputSeq)
        }.join()
        dut.clock.step(1)
      }
    }
  }
}

class Vdot64bSpec extends AnyFlatSpec with ChiselScalatestTester with BundleGenHelper with Vdot64bBehavior {
  vdotTest0()
  vdotTest1()
}
