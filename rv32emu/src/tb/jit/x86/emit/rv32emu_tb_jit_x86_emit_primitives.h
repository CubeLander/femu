#ifndef RV32EMU_TB_JIT_X86_EMIT_PRIMITIVES_H
#define RV32EMU_TB_JIT_X86_EMIT_PRIMITIVES_H

#include "../../../../internal/tb_jit_internal.h"

#if defined(__x86_64__)
bool rv32emu_emit_u8(rv32emu_x86_emit_t *e, uint8_t v);
bool rv32emu_emit_u32(rv32emu_x86_emit_t *e, uint32_t v);
bool rv32emu_emit_u64(rv32emu_x86_emit_t *e, uint64_t v);
bool rv32emu_emit_mov_rsi_saved(rv32emu_x86_emit_t *e);
bool rv32emu_emit_mov_rdi_saved(rv32emu_x86_emit_t *e);

bool rv32emu_emit_mov_eax_mem_rsi(rv32emu_x86_emit_t *e, uint32_t disp32);
bool rv32emu_emit_mov_ecx_mem_rsi(rv32emu_x86_emit_t *e, uint32_t disp32);
bool rv32emu_emit_mov_mem_rsi_eax(rv32emu_x86_emit_t *e, uint32_t disp32);
bool rv32emu_emit_add_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32);
bool rv32emu_emit_add_eax_ecx(rv32emu_x86_emit_t *e);
bool rv32emu_emit_sub_eax_ecx(rv32emu_x86_emit_t *e);
bool rv32emu_emit_xor_eax_ecx(rv32emu_x86_emit_t *e);
bool rv32emu_emit_or_eax_ecx(rv32emu_x86_emit_t *e);
bool rv32emu_emit_and_eax_ecx(rv32emu_x86_emit_t *e);
bool rv32emu_emit_xor_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32);
bool rv32emu_emit_or_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32);
bool rv32emu_emit_and_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32);
bool rv32emu_emit_cmp_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32);
bool rv32emu_emit_cmp_eax_ecx(rv32emu_x86_emit_t *e);
bool rv32emu_emit_setl_al(rv32emu_x86_emit_t *e);
bool rv32emu_emit_setb_al(rv32emu_x86_emit_t *e);
bool rv32emu_emit_movzx_eax_al(rv32emu_x86_emit_t *e);
bool rv32emu_emit_shl_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt);
bool rv32emu_emit_shr_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt);
bool rv32emu_emit_sar_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt);
bool rv32emu_emit_shl_eax_cl(rv32emu_x86_emit_t *e);
bool rv32emu_emit_shr_eax_cl(rv32emu_x86_emit_t *e);
bool rv32emu_emit_sar_eax_cl(rv32emu_x86_emit_t *e);
bool rv32emu_emit_mov_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32);

bool rv32emu_emit_mov_rdx_imm64(rv32emu_x86_emit_t *e, uint64_t imm64);
bool rv32emu_emit_mov_r8d_imm32(rv32emu_x86_emit_t *e, uint32_t imm32);
bool rv32emu_emit_jit_mem_helper(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                 uint32_t insn_pc, uint32_t retired_prefix,
                                 uint8_t **insn_pc_imm_ptr);
bool rv32emu_emit_jit_cf_helper(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                uint32_t insn_pc, uint32_t retired_prefix,
                                uint8_t **insn_pc_imm_ptr);
#endif

#endif
