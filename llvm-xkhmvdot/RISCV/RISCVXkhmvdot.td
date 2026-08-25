//===-- RISCVXkhmvdot.td - Xkhmvdot custom instructions ---*- tablegen -*-===//
//
// Xkhmvdot: Kunminghu Vector Dot Product (contest custom extension).
//   vdot.vv vd, vs1, vs2   # 4xINT8 dot product into INT32 accumulator
//   Encoding (OP-V): [31:26]=111001, [25]=vm, [24:20]=vs2, [19:15]=vs1,
//                    [14:12]=010(OPMVV), [11:7]=vd, [6:0]=1010111
//   Semantics: each 32-bit lane = sum_{i=0..3} sext8(vs1[j][i])*sext8(vs2[j][i])
//              accumulated into vd lane (wrap-around); requires vsew=e32.
//   Spec: 02_指令集设计.md §5.2 (vdota4.vv / vdot.vv, funct6=111001).
//   Pattern: modeled on VALUrVV (vmacc family) with EarlyClobber (accumulator).
//===----------------------------------------------------------------------===//

let Predicates = [HasVendorXkhmvdot] in {
  // vdot.vv vd, vs1, vs2 (vd = accumulate; vm mask via VMaskOp, default vm=1)
  def VDOT_VV : VALUrVV<0b111001, OPMVV, "vdot", EarlyClobber=1>,
                Sched<[]>;
}
