/* Helper-call stubs emitted for mem/cf fallback helpers. */
static bool rv32emu_emit_mov_rdx_imm64(rv32emu_x86_emit_t *e, uint64_t imm64) {
  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xbau) && rv32emu_emit_u64(e, imm64);
}

static bool rv32emu_emit_mov_r8d_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x41u) && rv32emu_emit_u8(e, 0xb8u) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_jit_mem_helper(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                        uint32_t insn_pc, uint32_t retired_prefix,
                                        uint8_t **insn_pc_imm_ptr) {
  if (e == NULL || d == NULL) {
    return false;
  }

  if (!rv32emu_emit_u8(e, 0x48u) || !rv32emu_emit_u8(e, 0x83u) || !rv32emu_emit_u8(e, 0xecu) ||
      !rv32emu_emit_u8(e, 0x08u)) {
    return false;
  }
  if (!rv32emu_emit_mov_rdx_imm64(e, (uint64_t)(uintptr_t)d)) {
    return false;
  }
  if (insn_pc_imm_ptr != NULL) {
    *insn_pc_imm_ptr = e->p + 1;
  }
  if (!rv32emu_emit_u8(e, 0xb9u) || !rv32emu_emit_u32(e, insn_pc)) {
    return false;
  }
  if (!rv32emu_emit_mov_r8d_imm32(e, retired_prefix)) {
    return false;
  }
  if (!rv32emu_emit_u8(e, 0x48u) || !rv32emu_emit_u8(e, 0xb8u) ||
      !rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_exec_mem)) {
    return false;
  }
  if (!rv32emu_emit_u8(e, 0xffu) || !rv32emu_emit_u8(e, 0xd0u)) {
    return false;
  }
  if (!rv32emu_emit_u8(e, 0x48u) || !rv32emu_emit_u8(e, 0x83u) || !rv32emu_emit_u8(e, 0xc4u) ||
      !rv32emu_emit_u8(e, 0x08u)) {
    return false;
  }
  if (!rv32emu_emit_u8(e, 0x85u) || !rv32emu_emit_u8(e, 0xc0u) || !rv32emu_emit_u8(e, 0x74u) ||
      !rv32emu_emit_u8(e, 0x03u)) {
    return false;
  }
  if (!rv32emu_emit_u8(e, 0x5eu) || !rv32emu_emit_u8(e, 0x5fu) || !rv32emu_emit_u8(e, 0xc3u)) {
    return false;
  }
  if (!rv32emu_emit_mov_rsi_saved(e) || !rv32emu_emit_mov_rdi_saved(e)) {
    return false;
  }
  return true;
}

static bool rv32emu_emit_jit_cf_helper(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                       uint32_t insn_pc, uint32_t retired_prefix,
                                       uint8_t **insn_pc_imm_ptr) {
  if (e == NULL || d == NULL) {
    return false;
  }

  if (!rv32emu_emit_u8(e, 0x48u) || !rv32emu_emit_u8(e, 0x83u) || !rv32emu_emit_u8(e, 0xecu) ||
      !rv32emu_emit_u8(e, 0x08u)) {
    return false;
  }
  if (!rv32emu_emit_mov_rdx_imm64(e, (uint64_t)(uintptr_t)d)) {
    return false;
  }
  if (insn_pc_imm_ptr != NULL) {
    *insn_pc_imm_ptr = e->p + 1;
  }
  if (!rv32emu_emit_u8(e, 0xb9u) || !rv32emu_emit_u32(e, insn_pc)) {
    return false;
  }
  if (!rv32emu_emit_mov_r8d_imm32(e, retired_prefix)) {
    return false;
  }
  if (!rv32emu_emit_u8(e, 0x48u) || !rv32emu_emit_u8(e, 0xb8u) ||
      !rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_exec_cf)) {
    return false;
  }
  if (!rv32emu_emit_u8(e, 0xffu) || !rv32emu_emit_u8(e, 0xd0u)) {
    return false;
  }
  if (!rv32emu_emit_u8(e, 0x48u) || !rv32emu_emit_u8(e, 0x83u) || !rv32emu_emit_u8(e, 0xc4u) ||
      !rv32emu_emit_u8(e, 0x08u)) {
    return false;
  }
  if (!rv32emu_emit_u8(e, 0x5eu) || !rv32emu_emit_u8(e, 0x5fu) || !rv32emu_emit_u8(e, 0xc3u)) {
    return false;
  }
  return true;
}
