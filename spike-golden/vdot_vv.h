// vdot.vv vd, vs2, vs1, vm  (custom Xkhmvdot, funct6=111001)
// 4xINT8 dot product into INT32 accumulator (kunminghu-v2 contest design).
// Semantics identical to vdot4a_vv (Zvdot4a8i), different encoding.
// Spec: 02_指令集设计.md §5.2 (vdota4.vv / vdot.vv, funct6=111001).
#include "vdot4a_common.h"

require_extension('V');
require(P.VU.vsew == e32);

VI_VV_LOOP
({
  VQDOT(vs1, vs2, int8_t, int8_t);
  vd = (vd + result) & 0xffffffff;
})
