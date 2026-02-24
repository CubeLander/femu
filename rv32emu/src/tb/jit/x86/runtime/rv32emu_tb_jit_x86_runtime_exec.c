#include "../../../../internal/tb_jit_internal.h"

#if defined(__x86_64__)
/* Helper-side memory/control-flow execution for lowered JIT ops. */
static inline void rv32emu_jit_write_rd(rv32emu_cpu_t *cpu, uint32_t rd, uint32_t value) {
  if (cpu != NULL && rd != 0u) {
    cpu->x[rd] = value;
  }
}

static bool rv32emu_jit_load_value(rv32emu_machine_t *m, uint32_t addr, uint32_t funct3,
                                   uint32_t *value_out) {
  uint32_t raw = 0u;
  uint32_t b0 = 0u;
  uint32_t b1 = 0u;
  uint32_t b2 = 0u;
  uint32_t b3 = 0u;

  if (m == NULL || value_out == NULL) {
    return false;
  }

  switch (funct3) {
  case 0x0: /* lb */
    if (!rv32emu_virt_read(m, addr, 1, RV32EMU_ACC_LOAD, &raw)) {
      return false;
    }
    *value_out = rv32emu_sign_extend(raw & 0xffu, 8);
    return true;
  case 0x1: /* lh */
    if ((addr & 1u) == 0u) {
      if (!rv32emu_virt_read(m, addr, 2, RV32EMU_ACC_LOAD, &raw)) {
        return false;
      }
    } else {
      if (!rv32emu_virt_read(m, addr, 1, RV32EMU_ACC_LOAD, &b0) ||
          !rv32emu_virt_read(m, addr + 1u, 1, RV32EMU_ACC_LOAD, &b1)) {
        return false;
      }
      raw = (b0 & 0xffu) | ((b1 & 0xffu) << 8);
    }
    *value_out = rv32emu_sign_extend(raw & 0xffffu, 16);
    return true;
  case 0x2: /* lw */
    if ((addr & 3u) == 0u) {
      return rv32emu_virt_read(m, addr, 4, RV32EMU_ACC_LOAD, value_out);
    }
    if (!rv32emu_virt_read(m, addr, 1, RV32EMU_ACC_LOAD, &b0) ||
        !rv32emu_virt_read(m, addr + 1u, 1, RV32EMU_ACC_LOAD, &b1) ||
        !rv32emu_virt_read(m, addr + 2u, 1, RV32EMU_ACC_LOAD, &b2) ||
        !rv32emu_virt_read(m, addr + 3u, 1, RV32EMU_ACC_LOAD, &b3)) {
      return false;
    }
    *value_out = (b0 & 0xffu) | ((b1 & 0xffu) << 8) | ((b2 & 0xffu) << 16) |
                 ((b3 & 0xffu) << 24);
    return true;
  case 0x4: /* lbu */
    if (!rv32emu_virt_read(m, addr, 1, RV32EMU_ACC_LOAD, &raw)) {
      return false;
    }
    *value_out = raw & 0xffu;
    return true;
  case 0x5: /* lhu */
    if ((addr & 1u) == 0u) {
      if (!rv32emu_virt_read(m, addr, 2, RV32EMU_ACC_LOAD, &raw)) {
        return false;
      }
    } else {
      if (!rv32emu_virt_read(m, addr, 1, RV32EMU_ACC_LOAD, &b0) ||
          !rv32emu_virt_read(m, addr + 1u, 1, RV32EMU_ACC_LOAD, &b1)) {
        return false;
      }
      raw = (b0 & 0xffu) | ((b1 & 0xffu) << 8);
    }
    *value_out = raw & 0xffffu;
    return true;
  default:
    return false;
  }
}

static bool rv32emu_jit_decode_at_pc(rv32emu_machine_t *m, uint32_t pc,
                                     rv32emu_decoded_insn_t *decoded_out) {
  uint32_t insn16 = 0u;
  uint32_t insn32 = 0u;

  if (m == NULL || decoded_out == NULL) {
    return false;
  }
  if ((pc & 1u) != 0u) {
    return false;
  }
  if (!rv32emu_virt_read(m, pc, 2, RV32EMU_ACC_FETCH, &insn16)) {
    return false;
  }
  if ((insn16 & 0x3u) != 0x3u) {
    return rv32emu_decode16((uint16_t)insn16, decoded_out);
  }
  if (!rv32emu_virt_read(m, pc, 4, RV32EMU_ACC_FETCH, &insn32)) {
    return false;
  }
  rv32emu_decode32(insn32, decoded_out);
  return true;
}

static bool rv32emu_jit_store_value(rv32emu_machine_t *m, uint32_t addr, uint32_t funct3,
                                    uint32_t value) {
  if (m == NULL) {
    return false;
  }

  switch (funct3) {
  case 0x0: /* sb */
    return rv32emu_virt_write(m, addr, 1, RV32EMU_ACC_STORE, value);
  case 0x1: /* sh */
    if ((addr & 1u) == 0u) {
      return rv32emu_virt_write(m, addr, 2, RV32EMU_ACC_STORE, value);
    }
    return rv32emu_virt_write(m, addr, 1, RV32EMU_ACC_STORE, value & 0xffu) &&
           rv32emu_virt_write(m, addr + 1u, 1, RV32EMU_ACC_STORE, (value >> 8) & 0xffu);
  case 0x2: /* sw */
    if ((addr & 3u) == 0u) {
      return rv32emu_virt_write(m, addr, 4, RV32EMU_ACC_STORE, value);
    }
    return rv32emu_virt_write(m, addr, 1, RV32EMU_ACC_STORE, value & 0xffu) &&
           rv32emu_virt_write(m, addr + 1u, 1, RV32EMU_ACC_STORE, (value >> 8) & 0xffu) &&
           rv32emu_virt_write(m, addr + 2u, 1, RV32EMU_ACC_STORE, (value >> 16) & 0xffu) &&
           rv32emu_virt_write(m, addr + 3u, 1, RV32EMU_ACC_STORE, (value >> 24) & 0xffu);
  default:
    return false;
  }
}

uint32_t rv32emu_jit_exec_mem(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                              const rv32emu_decoded_insn_t *d, uint32_t insn_pc,
                              uint32_t retired_prefix) {
  rv32emu_tb_cache_t *cache = g_rv32emu_jit_tls_cache;
  rv32emu_decoded_insn_t decoded_local;
  const rv32emu_decoded_insn_t *effective = d;
  uint32_t rs1v;
  uint32_t rs2v;
  uint32_t addr;
  uint32_t value = 0u;
  bool ok = false;

  rv32emu_jit_stats_inc_helper_mem_calls();

  if (m == NULL || cpu == NULL || d == NULL) {
    g_rv32emu_jit_tls_handled = true;
    return rv32emu_jit_result_or_no_retire();
  }
  if (cache != NULL && cache->jit_async_enabled && cache->jit_async_redecode_helpers) {
    if (!rv32emu_jit_decode_at_pc(m, insn_pc, &decoded_local)) {
      if (retired_prefix != 0u) {
        rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
      }
      g_rv32emu_jit_tls_handled = true;
      return rv32emu_jit_result_or_no_retire();
    }
    effective = &decoded_local;
  }

  cpu->pc = insn_pc;
  rs1v = cpu->x[effective->rs1];
  rs2v = cpu->x[effective->rs2];

  switch (effective->opcode) {
  case 0x03: /* load */
    addr = rs1v + (uint32_t)effective->imm_i;
    if (!rv32emu_jit_load_value(m, addr, effective->funct3, &value)) {
      if (effective->funct3 != 0x0u && effective->funct3 != 0x1u && effective->funct3 != 0x2u &&
          effective->funct3 != 0x4u && effective->funct3 != 0x5u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, effective->raw);
      }
      if (retired_prefix != 0u) {
        rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
      }
      g_rv32emu_jit_tls_handled = true;
      return rv32emu_jit_result_or_no_retire();
    }
    rv32emu_jit_write_rd(cpu, effective->rd, value);
    return 0u;
  case 0x23: /* store */
    addr = rs1v + (uint32_t)effective->imm_s;
    ok = rv32emu_jit_store_value(m, addr, effective->funct3, rs2v);
    if (!ok) {
      if (effective->funct3 != 0x0u && effective->funct3 != 0x1u && effective->funct3 != 0x2u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, effective->raw);
      }
      if (retired_prefix != 0u) {
        rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
      }
      g_rv32emu_jit_tls_handled = true;
      return rv32emu_jit_result_or_no_retire();
    }
    return 0u;
  default:
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, effective->raw);
    if (retired_prefix != 0u) {
      rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
    }
    g_rv32emu_jit_tls_handled = true;
    return rv32emu_jit_result_or_no_retire();
  }
}

uint32_t rv32emu_jit_exec_cf(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                             const rv32emu_decoded_insn_t *d, uint32_t insn_pc,
                             uint32_t retired_prefix) {
  rv32emu_tb_cache_t *cache = g_rv32emu_jit_tls_cache;
  rv32emu_decoded_insn_t decoded_local;
  const rv32emu_decoded_insn_t *effective = d;
  uint32_t next_pc;
  uint32_t ret_pc;
  uint32_t rs1v;
  uint32_t rs2v;

  rv32emu_jit_stats_inc_helper_cf_calls();

  if (m == NULL || cpu == NULL || d == NULL) {
    g_rv32emu_jit_tls_handled = true;
    return rv32emu_jit_result_or_no_retire();
  }
  if (cache != NULL && cache->jit_async_enabled && cache->jit_async_redecode_helpers) {
    if (!rv32emu_jit_decode_at_pc(m, insn_pc, &decoded_local)) {
      if (retired_prefix != 0u) {
        rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
      }
      g_rv32emu_jit_tls_handled = true;
      return rv32emu_jit_result_or_no_retire();
    }
    effective = &decoded_local;
  }

  cpu->pc = insn_pc;
  next_pc = insn_pc + ((effective->insn_len == 2u) ? 2u : 4u);
  ret_pc = next_pc;
  rs1v = cpu->x[effective->rs1];
  rs2v = cpu->x[effective->rs2];

  switch (effective->opcode) {
  case 0x6f: /* jal */
    rv32emu_jit_write_rd(cpu, effective->rd, ret_pc);
    next_pc = insn_pc + (uint32_t)effective->imm_j;
    break;
  case 0x67: /* jalr */
    if (effective->funct3 != 0x0u) {
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, effective->raw);
      if (retired_prefix != 0u) {
        rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
      }
      g_rv32emu_jit_tls_handled = true;
      return rv32emu_jit_result_or_no_retire();
    }
    next_pc = (rs1v + (uint32_t)effective->imm_i) & ~1u;
    rv32emu_jit_write_rd(cpu, effective->rd, ret_pc);
    break;
  case 0x63: /* branch */
    switch (effective->funct3) {
    case 0x0: /* beq */
      if (rs1v == rs2v) {
        next_pc = insn_pc + (uint32_t)effective->imm_b;
      }
      break;
    case 0x1: /* bne */
      if (rs1v != rs2v) {
        next_pc = insn_pc + (uint32_t)effective->imm_b;
      }
      break;
    case 0x4: /* blt */
      if ((int32_t)rs1v < (int32_t)rs2v) {
        next_pc = insn_pc + (uint32_t)effective->imm_b;
      }
      break;
    case 0x5: /* bge */
      if ((int32_t)rs1v >= (int32_t)rs2v) {
        next_pc = insn_pc + (uint32_t)effective->imm_b;
      }
      break;
    case 0x6: /* bltu */
      if (rs1v < rs2v) {
        next_pc = insn_pc + (uint32_t)effective->imm_b;
      }
      break;
    case 0x7: /* bgeu */
      if (rs1v >= rs2v) {
        next_pc = insn_pc + (uint32_t)effective->imm_b;
      }
      break;
    default:
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, effective->raw);
      if (retired_prefix != 0u) {
        rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
      }
      g_rv32emu_jit_tls_handled = true;
      return rv32emu_jit_result_or_no_retire();
    }
    break;
  default:
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, effective->raw);
    if (retired_prefix != 0u) {
      rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
    }
    g_rv32emu_jit_tls_handled = true;
    return rv32emu_jit_result_or_no_retire();
  }

  return (uint32_t)rv32emu_jit_block_commit(m, cpu, next_pc, retired_prefix + 1u);
}
#endif
