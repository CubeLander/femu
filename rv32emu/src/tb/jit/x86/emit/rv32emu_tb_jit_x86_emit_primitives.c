#include "rv32emu_tb_jit_x86_emit_primitives.h"

#if defined(__x86_64__)
/* Base byte emitters and shared frame-register helpers. */
bool rv32emu_emit_u8(rv32emu_x86_emit_t *e, uint8_t v) {
  if (e == NULL || e->p == NULL || e->p >= e->end) {
    return false;
  }
  *e->p++ = v;
  return true;
}

bool rv32emu_emit_u32(rv32emu_x86_emit_t *e, uint32_t v) {
  if (!rv32emu_emit_u8(e, (uint8_t)(v & 0xffu))) {
    return false;
  }
  if (!rv32emu_emit_u8(e, (uint8_t)((v >> 8) & 0xffu))) {
    return false;
  }
  if (!rv32emu_emit_u8(e, (uint8_t)((v >> 16) & 0xffu))) {
    return false;
  }
  return rv32emu_emit_u8(e, (uint8_t)((v >> 24) & 0xffu));
}

bool rv32emu_emit_u64(rv32emu_x86_emit_t *e, uint64_t v) {
  if (!rv32emu_emit_u32(e, (uint32_t)(v & 0xffffffffu))) {
    return false;
  }
  return rv32emu_emit_u32(e, (uint32_t)(v >> 32));
}

bool rv32emu_emit_mov_rsi_saved(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x34u) &&
         rv32emu_emit_u8(e, 0x24u);
}

bool rv32emu_emit_mov_rdi_saved(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x7cu) &&
         rv32emu_emit_u8(e, 0x24u) && rv32emu_emit_u8(e, 0x08u);
}

/* Register/memory ALU primitive encoders used by lowering paths. */
bool rv32emu_emit_mov_eax_mem_rsi(rv32emu_x86_emit_t *e, uint32_t disp32) {
  return rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x86u) && rv32emu_emit_u32(e, disp32);
}

bool rv32emu_emit_mov_ecx_mem_rsi(rv32emu_x86_emit_t *e, uint32_t disp32) {
  return rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x8eu) && rv32emu_emit_u32(e, disp32);
}

bool rv32emu_emit_mov_mem_rsi_eax(rv32emu_x86_emit_t *e, uint32_t disp32) {
  return rv32emu_emit_u8(e, 0x89u) && rv32emu_emit_u8(e, 0x86u) && rv32emu_emit_u32(e, disp32);
}

bool rv32emu_emit_add_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x05u) && rv32emu_emit_u32(e, imm32);
}

bool rv32emu_emit_add_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x01u) && rv32emu_emit_u8(e, 0xc8u);
}

bool rv32emu_emit_sub_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x29u) && rv32emu_emit_u8(e, 0xc8u);
}

bool rv32emu_emit_xor_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x31u) && rv32emu_emit_u8(e, 0xc8u);
}

bool rv32emu_emit_or_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x09u) && rv32emu_emit_u8(e, 0xc8u);
}

bool rv32emu_emit_and_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x21u) && rv32emu_emit_u8(e, 0xc8u);
}

bool rv32emu_emit_xor_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x35u) && rv32emu_emit_u32(e, imm32);
}

bool rv32emu_emit_or_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x0du) && rv32emu_emit_u32(e, imm32);
}

bool rv32emu_emit_and_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x25u) && rv32emu_emit_u32(e, imm32);
}

bool rv32emu_emit_cmp_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x3du) && rv32emu_emit_u32(e, imm32);
}

bool rv32emu_emit_cmp_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x39u) && rv32emu_emit_u8(e, 0xc8u);
}

bool rv32emu_emit_setl_al(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x0fu) && rv32emu_emit_u8(e, 0x9cu) && rv32emu_emit_u8(e, 0xc0u);
}

bool rv32emu_emit_setb_al(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x0fu) && rv32emu_emit_u8(e, 0x92u) && rv32emu_emit_u8(e, 0xc0u);
}

bool rv32emu_emit_movzx_eax_al(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x0fu) && rv32emu_emit_u8(e, 0xb6u) && rv32emu_emit_u8(e, 0xc0u);
}

bool rv32emu_emit_shl_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt) {
  return rv32emu_emit_u8(e, 0xc1u) && rv32emu_emit_u8(e, 0xe0u) && rv32emu_emit_u8(e, shamt);
}

bool rv32emu_emit_shr_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt) {
  return rv32emu_emit_u8(e, 0xc1u) && rv32emu_emit_u8(e, 0xe8u) && rv32emu_emit_u8(e, shamt);
}

bool rv32emu_emit_sar_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt) {
  return rv32emu_emit_u8(e, 0xc1u) && rv32emu_emit_u8(e, 0xf8u) && rv32emu_emit_u8(e, shamt);
}

bool rv32emu_emit_shl_eax_cl(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0xd3u) && rv32emu_emit_u8(e, 0xe0u);
}

bool rv32emu_emit_shr_eax_cl(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0xd3u) && rv32emu_emit_u8(e, 0xe8u);
}

bool rv32emu_emit_sar_eax_cl(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0xd3u) && rv32emu_emit_u8(e, 0xf8u);
}

bool rv32emu_emit_mov_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0xb8u) && rv32emu_emit_u32(e, imm32);
}

/* Helper-call stubs emitted for mem/cf fallback helpers. */
bool rv32emu_emit_mov_rdx_imm64(rv32emu_x86_emit_t *e, uint64_t imm64) {
  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xbau) && rv32emu_emit_u64(e, imm64);
}

bool rv32emu_emit_mov_r8d_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x41u) && rv32emu_emit_u8(e, 0xb8u) && rv32emu_emit_u32(e, imm32);
}

bool rv32emu_emit_jit_mem_helper(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
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

bool rv32emu_emit_jit_cf_helper(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
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
#endif
