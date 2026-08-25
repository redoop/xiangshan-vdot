package yunsuan.encoding.Opcode

import chisel3._

/** Sub-opcode space for the custom vdot (vector dot product) extension.
  *
  * Currently only one opcode (vdot) is defined. Future variants
  * (e.g. unsigned / saturating dot products) can be added here.
  */
object VdotOpcode {
  def width = 3

  /** vdot.vv: INT8 x INT8 -> INT32 dot product, accumulated into vd. */
  def vdot = "b000".U(width.W)
}
