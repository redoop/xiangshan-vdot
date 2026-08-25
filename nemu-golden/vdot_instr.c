void vdot_instr(Decode *s) {
  require_vector(true);
  if (vtype->vill != 0) {
    longjmp_exception(EX_II);
  }
  // vdot fixes source EEW=8 (INT8) and dest EEW=32 (INT32).
  // Require vsew = e32 so that vl (in e32 units) matches the 4 x INT32
  // dest elements per 128-bit register group.
  if (vtype->vsew != 2) {
    longjmp_exception(EX_II);
  }
  if (vtype->vlmul == 4) {
    longjmp_exception(EX_II); // reserved
  }
  double vflmul = compute_vflmul();
  require_aligned(id_dest->reg, vflmul);
  require_aligned(id_src2->reg, vflmul);
  require_aligned(id_src->reg, vflmul);
  require_vm(s);
  check_vstart_exception(s);
  if (check_vstart_ignore(s)) {
    vp_set_dirty();
    return;
  }

  for (word_t idx = vstart->val; idx < vl->val; idx ++) {
    rtlreg_t mask = get_mask(0, idx);
    if (s->vm == 0 && mask == 0) {
      // masked-off: leave dest unmodified (undisturbed or agnostic)
      if (RVV_AGNOSTIC && vtype->vma) {
        *s1 = (uint64_t) -1;
        set_vreg(id_dest->reg, idx, *s1, 2, vtype->vlmul, 1);
      }
      continue;
    }

    // fetch 32-bit chunks of vs2 and vs1 at logical element idx
    get_vreg(id_src2->reg, idx, s0, 2, vtype->vlmul, 0, 1); // raw 32-bit chunk (bytes)
    get_vreg(id_src->reg,  idx, s1, 2, vtype->vlmul, 0, 1); // raw 32-bit chunk (bytes)
    uint32_t chunk_vs2 = (uint32_t)(*s0);
    uint32_t chunk_vs1 = (uint32_t)(*s1);

    // dot product of 4 x INT8
    int64_t dot = 0;
    for (int i = 0; i < 4; i ++) {
      int8_t a = (int8_t)((chunk_vs2 >> (8 * i)) & 0xff);
      int8_t b = (int8_t)((chunk_vs1 >> (8 * i)) & 0xff);
      dot += (int64_t)a * (int64_t)b;
    }

    // accumulate with old vd (32-bit lane, wrap-around)
    get_vreg(id_dest->reg, idx, s0, 2, vtype->vlmul, 0, 1); // old vd lane
    *s1 = (uint32_t)((uint64_t)*s0 + dot);
    set_vreg(id_dest->reg, idx, *s1, 2, vtype->vlmul, 1);
  }

  if (RVV_AGNOSTIC && vtype->vta) {
    int vlmax = get_vlen_max(vtype->vsew, vtype->vlmul, 0);
    for (int idx = vl->val; idx < vlmax; idx ++) {
      *s1 = (uint64_t) -1;
      set_vreg(id_dest->reg, idx, *s1, 2, vtype->vlmul, 1);
    }
  }

  rtl_li(s, s0, 0);
  vcsr_write(IDXVSTART, s0);
  vp_set_dirty();
}
