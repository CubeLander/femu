#include "../../../../internal/tb_jit_internal.h"

#if defined(__x86_64__)
#include <stdlib.h>
#include "rv32emu_tb_jit_x86_emit_primitives_base_impl.h"
#include "rv32emu_tb_jit_x86_emit_primitives_alu_impl.h"
#include "rv32emu_tb_jit_x86_emit_primitives_helper_impl.h"

static bool rv32emu_jit_insn_supported(const rv32emu_decoded_insn_t *d) {
  const char *disable_alu_env;
  const char *disable_mem_env;
  const char *disable_cf_env;
  bool allow_alu;
  bool allow_mem;
  bool allow_cf;

  if (d == NULL) {
    return false;
  }

  disable_alu_env = getenv("RV32EMU_EXPERIMENTAL_JIT_DISABLE_ALU");
  disable_mem_env = getenv("RV32EMU_EXPERIMENTAL_JIT_DISABLE_MEM");
  disable_cf_env = getenv("RV32EMU_EXPERIMENTAL_JIT_DISABLE_CF");
  allow_alu = !(disable_alu_env != NULL && disable_alu_env[0] == '1');
  allow_mem = !(disable_mem_env != NULL && disable_mem_env[0] == '1');
  allow_cf = !(disable_cf_env != NULL && disable_cf_env[0] == '1');

  switch (d->opcode) {
  case 0x37: /* lui */
  case 0x17: /* auipc */
    if (!allow_alu) {
      return false;
    }
    return true;
  case 0x13: /* op-imm */
    if (!allow_alu) {
      return false;
    }
    switch (d->funct3) {
    case 0x0: /* addi */
    case 0x1: /* slli */
    case 0x2: /* slti */
    case 0x3: /* sltiu */
    case 0x4: /* xori */
    case 0x6: /* ori */
    case 0x7: /* andi */
      return true;
    case 0x5: /* srli/srai */
      return d->funct7 == 0x00u || d->funct7 == 0x20u;
    default:
      return false;
    }
  case 0x33: /* op */
    if (!allow_alu) {
      return false;
    }
    if (d->funct7 == 0x01u) {
      return false; /* M-extension currently falls back to interpreter. */
    }
    switch (d->funct3) {
    case 0x0:
      return d->funct7 == 0x00u || d->funct7 == 0x20u; /* add/sub */
    case 0x1:                                                 /* sll */
    case 0x2:                                                 /* slt */
    case 0x3:                                                 /* sltu */
    case 0x4:                                                 /* xor */
    case 0x6:                                                 /* or */
    case 0x7:                                                 /* and */
      return true;
    case 0x5:
      return d->funct7 == 0x00u || d->funct7 == 0x20u; /* srl/sra */
    default:
      return false;
    }
  case 0x03: /* load */
    if (!allow_mem) {
      return false;
    }
    return d->funct3 == 0x0u || d->funct3 == 0x1u || d->funct3 == 0x2u || d->funct3 == 0x4u ||
           d->funct3 == 0x5u;
  case 0x23: /* store */
    if (!allow_mem) {
      return false;
    }
    return d->funct3 == 0x0u || d->funct3 == 0x1u || d->funct3 == 0x2u;
  case 0x6f: /* jal */
    if (!allow_cf) {
      return false;
    }
    return true;
  case 0x67: /* jalr */
    if (!allow_cf) {
      return false;
    }
    return d->funct3 == 0x0u;
  case 0x63: /* branch */
    if (!allow_cf) {
      return false;
    }
    return d->funct3 == 0x0u || d->funct3 == 0x1u || d->funct3 == 0x4u || d->funct3 == 0x5u ||
           d->funct3 == 0x6u || d->funct3 == 0x7u;
  default:
    return false;
  }
}

static bool rv32emu_jit_record_pc_reloc(rv32emu_jit_compiled_artifact_t *artifact, uint8_t *code_ptr,
                                        uint8_t *imm_ptr) {
  uint32_t off;

  if (artifact == NULL || code_ptr == NULL || imm_ptr == NULL || imm_ptr < code_ptr ||
      artifact->pc_reloc_count >= RV32EMU_JIT_MAX_PC_RELOCS) {
    return false;
  }

  off = (uint32_t)(uintptr_t)(imm_ptr - code_ptr);
  if (off > UINT16_MAX) {
    return false;
  }
  artifact->pc_reloc_off[artifact->pc_reloc_count++] = (uint16_t)off;
  return true;
}

static inline uint32_t rv32emu_cpu_x_off(uint32_t idx) {
  return (uint32_t)(offsetof(rv32emu_cpu_t, x) + idx * sizeof(uint32_t));
}

/* ALU/compare/shift class lowering. */
static bool rv32emu_jit_emit_one_alu(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                     uint32_t insn_pc, uint8_t *code_ptr,
                                     rv32emu_jit_compiled_artifact_t *artifact) {
  uint32_t rd_off;
  uint32_t rs1_off;
  uint32_t rs2_off;
  uint8_t *auipc_imm_ptr = NULL;

  if (e == NULL || d == NULL || code_ptr == NULL || artifact == NULL) {
    return false;
  }

  rd_off = rv32emu_cpu_x_off(d->rd);
  rs1_off = rv32emu_cpu_x_off(d->rs1);
  rs2_off = rv32emu_cpu_x_off(d->rs2);

  switch (d->opcode) {
  case 0x37: /* lui */
    if (!rv32emu_emit_mov_eax_imm32(e, (uint32_t)d->imm_u)) {
      return false;
    }
    if (d->rd != 0u && !rv32emu_emit_mov_mem_rsi_eax(e, rd_off)) {
      return false;
    }
    return true;
  case 0x17: /* auipc */
    auipc_imm_ptr = e->p + 1;
    if (!rv32emu_emit_mov_eax_imm32(e, insn_pc + (uint32_t)d->imm_u)) {
      return false;
    }
    if (!rv32emu_jit_record_pc_reloc(artifact, code_ptr, auipc_imm_ptr)) {
      return false;
    }
    if (d->rd != 0u && !rv32emu_emit_mov_mem_rsi_eax(e, rd_off)) {
      return false;
    }
    return true;
  case 0x13: /* op-imm */
    if (!rv32emu_emit_mov_eax_mem_rsi(e, rs1_off)) {
      return false;
    }
    switch (d->funct3) {
    case 0x0: /* addi */
      if (d->imm_i != 0 && !rv32emu_emit_add_eax_imm32(e, (uint32_t)d->imm_i)) {
        return false;
      }
      break;
    case 0x1: /* slli */
      if (!rv32emu_emit_shl_eax_imm8(e, (uint8_t)d->rs2)) {
        return false;
      }
      break;
    case 0x2: /* slti */
      if (!rv32emu_emit_cmp_eax_imm32(e, (uint32_t)d->imm_i) || !rv32emu_emit_setl_al(e) ||
          !rv32emu_emit_movzx_eax_al(e)) {
        return false;
      }
      break;
    case 0x3: /* sltiu */
      if (!rv32emu_emit_cmp_eax_imm32(e, (uint32_t)d->imm_i) || !rv32emu_emit_setb_al(e) ||
          !rv32emu_emit_movzx_eax_al(e)) {
        return false;
      }
      break;
    case 0x4: /* xori */
      if (!rv32emu_emit_xor_eax_imm32(e, (uint32_t)d->imm_i)) {
        return false;
      }
      break;
    case 0x5: /* srli/srai */
      if (d->funct7 == 0x00u) {
        if (!rv32emu_emit_shr_eax_imm8(e, (uint8_t)d->rs2)) {
          return false;
        }
        break;
      }
      if (d->funct7 == 0x20u) {
        if (!rv32emu_emit_sar_eax_imm8(e, (uint8_t)d->rs2)) {
          return false;
        }
        break;
      }
      return false;
    case 0x6: /* ori */
      if (!rv32emu_emit_or_eax_imm32(e, (uint32_t)d->imm_i)) {
        return false;
      }
      break;
    case 0x7: /* andi */
      if (!rv32emu_emit_and_eax_imm32(e, (uint32_t)d->imm_i)) {
        return false;
      }
      break;
    default:
      return false;
    }
    if (d->rd != 0u && !rv32emu_emit_mov_mem_rsi_eax(e, rd_off)) {
      return false;
    }
    return true;
  case 0x33: /* op */
    if (d->funct7 == 0x01u) {
      return false;
    }
    if (!rv32emu_emit_mov_eax_mem_rsi(e, rs1_off)) {
      return false;
    }
    switch (d->funct3) {
    case 0x0: /* add/sub */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off)) {
        return false;
      }
      if (d->funct7 == 0x00u) {
        if (!rv32emu_emit_add_eax_ecx(e)) {
          return false;
        }
        break;
      }
      if (d->funct7 == 0x20u) {
        if (!rv32emu_emit_sub_eax_ecx(e)) {
          return false;
        }
        break;
      }
      return false;
    case 0x1: /* sll */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_shl_eax_cl(e)) {
        return false;
      }
      break;
    case 0x2: /* slt */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_cmp_eax_ecx(e) ||
          !rv32emu_emit_setl_al(e) || !rv32emu_emit_movzx_eax_al(e)) {
        return false;
      }
      break;
    case 0x3: /* sltu */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_cmp_eax_ecx(e) ||
          !rv32emu_emit_setb_al(e) || !rv32emu_emit_movzx_eax_al(e)) {
        return false;
      }
      break;
    case 0x4: /* xor */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_xor_eax_ecx(e)) {
        return false;
      }
      break;
    case 0x5: /* srl/sra */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off)) {
        return false;
      }
      if (d->funct7 == 0x00u) {
        if (!rv32emu_emit_shr_eax_cl(e)) {
          return false;
        }
        break;
      }
      if (d->funct7 == 0x20u) {
        if (!rv32emu_emit_sar_eax_cl(e)) {
          return false;
        }
        break;
      }
      return false;
    case 0x6: /* or */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_or_eax_ecx(e)) {
        return false;
      }
      break;
    case 0x7: /* and */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_and_eax_ecx(e)) {
        return false;
      }
      break;
    default:
      return false;
    }
    if (d->rd != 0u && !rv32emu_emit_mov_mem_rsi_eax(e, rd_off)) {
      return false;
    }
    return true;
  default:
    return false;
  }
}

/* Load/store class lowering via helper trampoline. */
static bool rv32emu_jit_emit_one_mem(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *helper_d,
                                     uint32_t insn_pc, uint32_t retired_before, uint8_t *code_ptr,
                                     rv32emu_jit_compiled_artifact_t *artifact) {
  uint8_t *insn_pc_imm_ptr = NULL;

  if (e == NULL || helper_d == NULL || code_ptr == NULL || artifact == NULL) {
    return false;
  }

  if (!rv32emu_emit_jit_mem_helper(e, helper_d, insn_pc, retired_before, &insn_pc_imm_ptr)) {
    return false;
  }

  return rv32emu_jit_record_pc_reloc(artifact, code_ptr, insn_pc_imm_ptr);
}

/* Control-flow class lowering via helper trampoline. */
static bool rv32emu_jit_emit_one_cf(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *helper_d,
                                    uint32_t insn_pc, uint32_t retired_before, uint8_t *code_ptr,
                                    rv32emu_jit_compiled_artifact_t *artifact) {
  uint8_t *insn_pc_imm_ptr = NULL;

  if (e == NULL || helper_d == NULL || code_ptr == NULL || artifact == NULL) {
    return false;
  }

  if (!rv32emu_emit_jit_cf_helper(e, helper_d, insn_pc, retired_before, &insn_pc_imm_ptr)) {
    return false;
  }

  return rv32emu_jit_record_pc_reloc(artifact, code_ptr, insn_pc_imm_ptr);
}

static bool rv32emu_jit_emit_one(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                 const rv32emu_decoded_insn_t *helper_d, uint32_t insn_pc,
                                 uint32_t retired_before, uint8_t *code_ptr,
                                 rv32emu_jit_compiled_artifact_t *artifact) {
  if (e == NULL || d == NULL || helper_d == NULL || code_ptr == NULL || artifact == NULL) {
    return false;
  }

  switch (d->opcode) {
  case 0x37: /* lui */
  case 0x17: /* auipc */
  case 0x13: /* op-imm */
  case 0x33: /* op */
    return rv32emu_jit_emit_one_alu(e, d, insn_pc, code_ptr, artifact);
  case 0x03: /* load */
  case 0x23: /* store */
    return rv32emu_jit_emit_one_mem(e, helper_d, insn_pc, retired_before, code_ptr, artifact);
  case 0x63: /* branch */
  case 0x67: /* jalr */
  case 0x6f: /* jal */
    return rv32emu_jit_emit_one_cf(e, helper_d, insn_pc, retired_before, code_ptr, artifact);
  default:
    return false;
  }
}

bool rv32emu_jit_insn_supported_query(const rv32emu_decoded_insn_t *d) {
  return rv32emu_jit_insn_supported(d);
}

bool rv32emu_jit_emit_one_lowered(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                  const rv32emu_decoded_insn_t *helper_d, uint32_t insn_pc,
                                  uint32_t retired_before, uint8_t *code_ptr,
                                  rv32emu_jit_compiled_artifact_t *artifact) {
  return rv32emu_jit_emit_one(e, d, helper_d, insn_pc, retired_before, code_ptr, artifact);
}

bool rv32emu_jit_record_pc_reloc_public(rv32emu_jit_compiled_artifact_t *artifact,
                                        uint8_t *code_ptr, uint8_t *imm_ptr) {
  return rv32emu_jit_record_pc_reloc(artifact, code_ptr, imm_ptr);
}
#endif
