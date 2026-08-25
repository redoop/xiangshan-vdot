package xiangshan.backend.fu.wrapper

import org.chipsalliance.cde.config.Parameters
import chisel3._
import chisel3.util._
import utility.XSError
import xiangshan.backend.fu.FuConfig
import xiangshan.backend.fu.vector.Bundles.VSew
import xiangshan.backend.fu.vector.utils.VecDataSplitModule
import xiangshan.backend.fu.vector.{Mgu, VecPipedFuncUnit, VecSrcTypeModule}
import xiangshan.ExceptionNO
import yunsuan.VdotType
import yunsuan.vector.mac.Vdot64b

/** Source/dest type decode for the custom vdot.vv instruction.
  *
  * vdot.vv fixes its data layout regardless of vsetvl:
  *   - sources vs1/vs2 are always INT8 (EEW = 8): 16 elements per 128-bit register
  *   - dest vd is always INT32 (EEW = 32): 4 elements per 128-bit register
  * Each 32-bit dest lane accumulates the dot product of 4 consecutive INT8 pairs.
  *
  * The vsew recorded by vsetvl must be e32 so that vl (in e32 units) matches the
  * number of INT32 dest elements; otherwise the instruction is illegal.
  */
class VdotSrcTypeModule extends VecSrcTypeModule {

  private val vs2Sign = VdotType.vs2Sign(fuOpType)
  private val vs1Sign = VdotType.vs1Sign(fuOpType)
  private val vdSign  = VdotType.vdSign(fuOpType)

  private val vs2IntType = Cat(0.U(1.W), vs2Sign)
  private val vs1IntType = Cat(0.U(1.W), vs1Sign)
  private val vdIntType  = Cat(0.U(1.W), vdSign)

  // fixed EEW: INT8 sources, INT32 dest
  private val vs2Type = Cat(vs2IntType, VSew.e8)
  private val vs1Type = Cat(vs1IntType, VSew.e8)
  private val vdType  = Cat(vdIntType, VSew.e32)

  private val vsewIllegal = vsew =/= VSew.e32

  io.out.illegal := vsewIllegal
  io.out.vs2Type := vs2Type
  io.out.vs1Type := vs1Type
  io.out.vdType  := vdType
}

class VdotU(cfg: FuConfig)(implicit p: Parameters) extends VecPipedFuncUnit(cfg) {
  XSError(io.in.valid && io.in.bits.ctrl.fuOpType === VdotType.dummy, "Vdot OpType not supported")

  // params alias
  private val dataWidth = cfg.destDataBits
  private val dataWidthOfDataModule = 64
  private val numVecModule = dataWidth / dataWidthOfDataModule

  // io alias
  private val opcode = VdotType.getOpcode(fuOpType)

  // modules
  private val typeMod = Module(new VdotSrcTypeModule)
  private val vs2Split = Module(new VecDataSplitModule(dataWidth, dataWidthOfDataModule))
  private val vs1Split = Module(new VecDataSplitModule(dataWidth, dataWidthOfDataModule))
  private val oldVdSplit  = Module(new VecDataSplitModule(dataWidth, dataWidthOfDataModule))
  private val vdots = Seq.fill(numVecModule)(Module(new Vdot64b))
  private val mgu = Module(new Mgu(dataWidth))

  /**
    * [[typeMod]]'s in connection
    */
  typeMod.io.in.fuOpType := fuOpType
  typeMod.io.in.vsew := vsew
  typeMod.io.in.isReverse := isReverse
  typeMod.io.in.isExt := isExt
  typeMod.io.in.isDstMask := vecCtrl.isDstMask
  typeMod.io.in.isMove := isMove

  /**
    * In connection of [[vs2Split]], [[vs1Split]] and [[oldVdSplit]]
    */
  vs2Split.io.inVecData := vs2
  vs1Split.io.inVecData := vs1
  oldVdSplit.io.inVecData := oldVd

  /**
    * [[vdots]]'s in connection
    */
  vdots.zipWithIndex.foreach {
    case (mod, i) =>
      mod.io.fire        := io.in.valid
      mod.io.info.vm     := vm
      mod.io.info.ma     := vma
      mod.io.info.ta     := vta
      mod.io.info.vlmul  := vlmul
      mod.io.info.vl     := srcVConfig.vl
      mod.io.info.vstart := vstart
      mod.io.info.uopIdx := vuopIdx
      mod.io.info.vxrm   := vxrm
      mod.io.srcType(0)  := typeMod.io.out.vs2Type
      mod.io.srcType(1)  := typeMod.io.out.vs1Type
      mod.io.vdType      := typeMod.io.out.vdType
      mod.io.vs1         := vs1Split.io.outVec64b(i)
      mod.io.vs2         := vs2Split.io.outVec64b(i)
      mod.io.oldVd       := oldVdSplit.io.outVec64b(i)
  }

  /**
    * [[mgu]]'s in connection
    */
  private val outVd = Cat(vdots.reverse.map(_.io.vd))

  // dest element width is always e32 for vdot
  private val outEew = VSew.e32
  mgu.io.in.vd := outVd
  mgu.io.in.oldVd := outOldVd
  mgu.io.in.mask := outSrcMask
  mgu.io.in.info.ta := outVecCtrl.vta
  mgu.io.in.info.ma := outVecCtrl.vma
  mgu.io.in.info.vl := outVl
  mgu.io.in.info.vlmul := outVecCtrl.vlmul
  mgu.io.in.info.valid := io.out.valid
  mgu.io.in.info.vstart := outVecCtrl.vstart
  mgu.io.in.info.eew := outEew
  mgu.io.in.info.vsew := outVecCtrl.vsew
  mgu.io.in.info.vdIdx := outVecCtrl.vuopIdx
  mgu.io.in.info.narrow := 0.B
  mgu.io.in.info.dstMask := 0.B
  mgu.io.in.isIndexedVls := false.B

  io.out.bits.res.data := mgu.io.out.vd
  io.out.bits.ctrl.exceptionVec.get(ExceptionNO.illegalInstr) := mgu.io.out.illegal
}
