#include "rv32emu.h"

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

static inline uint32_t rv32emu_bits(uint32_t value, int hi, int lo) {
  return (value >> lo) & ((1u << (hi - lo + 1)) - 1u);
}

static inline int32_t rv32emu_imm_i(uint32_t insn) {
  return (int32_t)rv32emu_sign_extend(insn >> 20, 12);
}

static inline int32_t rv32emu_imm_s(uint32_t insn) {
  uint32_t imm = (rv32emu_bits(insn, 31, 25) << 5) | rv32emu_bits(insn, 11, 7);
  return (int32_t)rv32emu_sign_extend(imm, 12);
}

static inline int32_t rv32emu_imm_b(uint32_t insn) {
  uint32_t imm = (rv32emu_bits(insn, 31, 31) << 12) | (rv32emu_bits(insn, 7, 7) << 11) |
                 (rv32emu_bits(insn, 30, 25) << 5) | (rv32emu_bits(insn, 11, 8) << 1);
  return (int32_t)rv32emu_sign_extend(imm, 13);
}

static inline int32_t rv32emu_imm_u(uint32_t insn) {
  return (int32_t)(insn & 0xfffff000u);
}

static inline int32_t rv32emu_imm_j(uint32_t insn) {
  uint32_t imm = (rv32emu_bits(insn, 31, 31) << 20) | (rv32emu_bits(insn, 19, 12) << 12) |
                 (rv32emu_bits(insn, 20, 20) << 11) | (rv32emu_bits(insn, 30, 21) << 1);
  return (int32_t)rv32emu_sign_extend(imm, 21);
}

static inline uint32_t rv32emu_c_bits(uint16_t value, int hi, int lo) {
  return (value >> lo) & ((1u << (hi - lo + 1)) - 1u);
}

static inline int32_t rv32emu_c_imm_addi(uint16_t insn) {
  uint32_t imm = (rv32emu_c_bits(insn, 12, 12) << 5) | rv32emu_c_bits(insn, 6, 2);
  return (int32_t)rv32emu_sign_extend(imm, 6);
}

static inline int32_t rv32emu_c_imm_j(uint16_t insn) {
  uint32_t imm = (rv32emu_c_bits(insn, 12, 12) << 11) | (rv32emu_c_bits(insn, 8, 8) << 10) |
                 (rv32emu_c_bits(insn, 10, 9) << 8) | (rv32emu_c_bits(insn, 6, 6) << 7) |
                 (rv32emu_c_bits(insn, 7, 7) << 6) | (rv32emu_c_bits(insn, 2, 2) << 5) |
                 (rv32emu_c_bits(insn, 11, 11) << 4) | (rv32emu_c_bits(insn, 5, 3) << 1);
  return (int32_t)rv32emu_sign_extend(imm, 12);
}

static inline int32_t rv32emu_c_imm_b(uint16_t insn) {
  uint32_t imm = (rv32emu_c_bits(insn, 12, 12) << 8) | (rv32emu_c_bits(insn, 6, 5) << 6) |
                 (rv32emu_c_bits(insn, 2, 2) << 5) | (rv32emu_c_bits(insn, 11, 10) << 3) |
                 (rv32emu_c_bits(insn, 4, 3) << 1);
  return (int32_t)rv32emu_sign_extend(imm, 9);
}

static bool rv32emu_csr_is_implemented(uint16_t csr_num) {
  switch (csr_num) {
  case CSR_FFLAGS:
  case CSR_FRM:
  case CSR_FCSR:
  case CSR_SSTATUS:
  case CSR_SCOUNTEREN:
  case CSR_SIE:
  case CSR_STVEC:
  case CSR_SSCRATCH:
  case CSR_SEPC:
  case CSR_SCAUSE:
  case CSR_STVAL:
  case CSR_SIP:
  case CSR_SATP:
  case CSR_MSTATUS:
  case CSR_MISA:
  case CSR_MCOUNTEREN:
  case CSR_MEDELEG:
  case CSR_MIDELEG:
  case CSR_MIE:
  case CSR_MTVEC:
  case CSR_MSCRATCH:
  case CSR_MEPC:
  case CSR_MCAUSE:
  case CSR_MTVAL:
  case CSR_MIP:
  case CSR_CYCLE:
  case CSR_TIME:
  case CSR_INSTRET:
  case CSR_CYCLEH:
  case CSR_TIMEH:
  case CSR_INSTRETH:
  case CSR_MVENDORID:
  case CSR_MARCHID:
  case CSR_MIMPID:
  case CSR_MHARTID:
    return true;
  default:
    return false;
  }
}

static void rv32emu_write_rd(rv32emu_machine_t *m, uint32_t rd, uint32_t value) {
  if (rd != 0u) {
    m->cpu.x[rd] = value;
  }
}

static bool rv32emu_exec_csr_op(rv32emu_machine_t *m, uint32_t insn, uint32_t rd,
                                uint32_t funct3, uint32_t rs1, uint32_t rs1v) {
  uint16_t csr_num = (uint16_t)rv32emu_bits(insn, 31, 20);
  uint32_t old_value = rv32emu_csr_read(m, csr_num);
  uint32_t new_value = old_value;
  uint32_t zimm = rs1 & 0x1fu;

  if (!rv32emu_csr_is_implemented(csr_num)) {
    return false;
  }

  switch (funct3) {
  case 0x1: /* csrrw */
    new_value = rs1v;
    rv32emu_csr_write(m, csr_num, new_value);
    break;
  case 0x2: /* csrrs */
    if (rs1 != 0u) {
      new_value = old_value | rs1v;
      rv32emu_csr_write(m, csr_num, new_value);
    }
    break;
  case 0x3: /* csrrc */
    if (rs1 != 0u) {
      new_value = old_value & ~rs1v;
      rv32emu_csr_write(m, csr_num, new_value);
    }
    break;
  case 0x5: /* csrrwi */
    new_value = zimm;
    rv32emu_csr_write(m, csr_num, new_value);
    break;
  case 0x6: /* csrrsi */
    if (zimm != 0u) {
      new_value = old_value | zimm;
      rv32emu_csr_write(m, csr_num, new_value);
    }
    break;
  case 0x7: /* csrrci */
    if (zimm != 0u) {
      new_value = old_value & ~zimm;
      rv32emu_csr_write(m, csr_num, new_value);
    }
    break;
  default:
    return false;
  }

  rv32emu_write_rd(m, rd, old_value);
  return true;
}

static bool rv32emu_exec_mret(rv32emu_machine_t *m, uint32_t *next_pc) {
  uint32_t mstatus;
  uint32_t mpp;

  if (m->cpu.priv != RV32EMU_PRIV_M) {
    return false;
  }

  mstatus = m->cpu.csr[CSR_MSTATUS];
  mpp = (mstatus & MSTATUS_MPP_MASK) >> MSTATUS_MPP_SHIFT;

  if ((mstatus & MSTATUS_MPIE) != 0u) {
    mstatus |= MSTATUS_MIE;
  } else {
    mstatus &= ~MSTATUS_MIE;
  }
  mstatus |= MSTATUS_MPIE;
  mstatus &= ~MSTATUS_MPP_MASK;

  m->cpu.csr[CSR_MSTATUS] = mstatus;
  m->cpu.priv = (rv32emu_priv_t)(mpp & 0x3u);
  *next_pc = m->cpu.csr[CSR_MEPC] & ~1u;
  return true;
}

static bool rv32emu_exec_sret(rv32emu_machine_t *m, uint32_t *next_pc) {
  uint32_t mstatus;

  if (m->cpu.priv == RV32EMU_PRIV_U) {
    return false;
  }

  mstatus = m->cpu.csr[CSR_MSTATUS];
  if ((mstatus & MSTATUS_SPIE) != 0u) {
    mstatus |= MSTATUS_SIE;
  } else {
    mstatus &= ~MSTATUS_SIE;
  }
  mstatus |= MSTATUS_SPIE;

  if ((mstatus & MSTATUS_SPP) != 0u) {
    m->cpu.priv = RV32EMU_PRIV_S;
  } else {
    m->cpu.priv = RV32EMU_PRIV_U;
  }
  mstatus &= ~MSTATUS_SPP;

  m->cpu.csr[CSR_MSTATUS] = mstatus;
  *next_pc = m->cpu.csr[CSR_SEPC] & ~1u;
  return true;
}

static bool rv32emu_load_value(rv32emu_machine_t *m, uint32_t addr, uint32_t funct3,
                               uint32_t *value_out) {
  uint32_t raw = 0;
  uint32_t b0 = 0;
  uint32_t b1 = 0;
  uint32_t b2 = 0;
  uint32_t b3 = 0;

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
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, 0);
    return false;
  }
}

static bool rv32emu_store_value(rv32emu_machine_t *m, uint32_t addr, uint32_t funct3,
                                uint32_t value) {
  bool ok = false;

  switch (funct3) {
  case 0x0: /* sb */
    ok = rv32emu_virt_write(m, addr, 1, RV32EMU_ACC_STORE, value);
    break;
  case 0x1: /* sh */
    if ((addr & 1u) == 0u) {
      ok = rv32emu_virt_write(m, addr, 2, RV32EMU_ACC_STORE, value);
    } else {
      ok = rv32emu_virt_write(m, addr, 1, RV32EMU_ACC_STORE, value & 0xffu) &&
           rv32emu_virt_write(m, addr + 1u, 1, RV32EMU_ACC_STORE, (value >> 8) & 0xffu);
    }
    break;
  case 0x2: /* sw */
    if ((addr & 3u) == 0u) {
      ok = rv32emu_virt_write(m, addr, 4, RV32EMU_ACC_STORE, value);
    } else {
      ok = rv32emu_virt_write(m, addr, 1, RV32EMU_ACC_STORE, value & 0xffu) &&
           rv32emu_virt_write(m, addr + 1u, 1, RV32EMU_ACC_STORE, (value >> 8) & 0xffu) &&
           rv32emu_virt_write(m, addr + 2u, 1, RV32EMU_ACC_STORE, (value >> 16) & 0xffu) &&
           rv32emu_virt_write(m, addr + 3u, 1, RV32EMU_ACC_STORE, (value >> 24) & 0xffu);
    }
    break;
  default:
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, 0);
    return false;
  }

  if (ok) {
    m->cpu.lr_valid = false;
  }
  return ok;
}

static bool rv32emu_load_u64(rv32emu_machine_t *m, uint32_t addr, uint64_t *value_out) {
  uint32_t lo = 0;
  uint32_t hi = 0;

  if (value_out == NULL) {
    return false;
  }
  if (!rv32emu_virt_read(m, addr, 4, RV32EMU_ACC_LOAD, &lo) ||
      !rv32emu_virt_read(m, addr + 4u, 4, RV32EMU_ACC_LOAD, &hi)) {
    return false;
  }

  *value_out = ((uint64_t)hi << 32) | (uint64_t)lo;
  return true;
}

static bool rv32emu_store_u64(rv32emu_machine_t *m, uint32_t addr, uint64_t value) {
  return rv32emu_virt_write(m, addr, 4, RV32EMU_ACC_STORE, (uint32_t)value) &&
         rv32emu_virt_write(m, addr + 4u, 4, RV32EMU_ACC_STORE, (uint32_t)(value >> 32));
}

static bool rv32emu_exec_fp_load(rv32emu_machine_t *m, uint32_t rd, uint32_t funct3,
                                 uint32_t base_addr, int32_t imm) {
  uint32_t addr = base_addr + (uint32_t)imm;
  uint32_t raw32 = 0;
  uint64_t raw64 = 0;

  if (rd >= 32u) {
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, 0);
    return false;
  }

  switch (funct3) {
  case 0x2: /* flw */
    if (!rv32emu_virt_read(m, addr, 4, RV32EMU_ACC_LOAD, &raw32)) {
      return false;
    }
    /* NaN-box single precision into an FLEN=64 register. */
    m->cpu.f[rd] = 0xffffffff00000000ull | (uint64_t)raw32;
    return true;
  case 0x3: /* fld */
    if (!rv32emu_load_u64(m, addr, &raw64)) {
      return false;
    }
    m->cpu.f[rd] = raw64;
    return true;
  default:
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, 0);
    return false;
  }
}

static bool rv32emu_exec_fp_store(rv32emu_machine_t *m, uint32_t rs2, uint32_t funct3,
                                  uint32_t base_addr, int32_t imm) {
  uint32_t addr = base_addr + (uint32_t)imm;

  if (rs2 >= 32u) {
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, 0);
    return false;
  }

  switch (funct3) {
  case 0x2: /* fsw */
    return rv32emu_virt_write(m, addr, 4, RV32EMU_ACC_STORE, (uint32_t)m->cpu.f[rs2]);
  case 0x3: /* fsd */
    return rv32emu_store_u64(m, addr, m->cpu.f[rs2]);
  default:
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, 0);
    return false;
  }
}

static bool rv32emu_exec_fp_op(rv32emu_machine_t *m, uint32_t rd, uint32_t rs1,
                               uint32_t rs2, uint32_t funct3, uint32_t funct7) {
  uint64_t a;
  uint64_t b;
  uint64_t sign_mask;
  uint64_t mag_mask;
  uint64_t result;

  if (rd >= 32u || rs1 >= 32u || rs2 >= 32u) {
    return false;
  }

  a = m->cpu.f[rs1];
  b = m->cpu.f[rs2];

  switch (funct7) {
  case 0x10: /* fsgnj.s/fsgnjn.s/fsgnjx.s */
    sign_mask = 0x80000000u;
    mag_mask = 0x7fffffffu;
    switch (funct3) {
    case 0x0:
      result = (a & mag_mask) | (b & sign_mask);
      break;
    case 0x1:
      result = (a & mag_mask) | ((~b) & sign_mask);
      break;
    case 0x2:
      result = (a & mag_mask) | ((a ^ b) & sign_mask);
      break;
    default:
      return false;
    }
    m->cpu.f[rd] = 0xffffffff00000000ull | (result & 0xffffffffu);
    return true;
  case 0x11: /* fsgnj.d/fsgnjn.d/fsgnjx.d */
    sign_mask = 1ull << 63;
    mag_mask = ~sign_mask;
    switch (funct3) {
    case 0x0:
      result = (a & mag_mask) | (b & sign_mask);
      break;
    case 0x1:
      result = (a & mag_mask) | ((~b) & sign_mask);
      break;
    case 0x2:
      result = (a & mag_mask) | ((a ^ b) & sign_mask);
      break;
    default:
      return false;
    }
    m->cpu.f[rd] = result;
    return true;
  case 0x70: /* fmv.x.w (rm=000, rs2=0) */
    if (funct3 != 0x0u || rs2 != 0u) {
      return false;
    }
    rv32emu_write_rd(m, rd, (uint32_t)a);
    return true;
  case 0x78: /* fmv.w.x (rm=000, rs2=0) */
    if (funct3 != 0x0u || rs2 != 0u) {
      return false;
    }
    m->cpu.f[rd] = 0xffffffff00000000ull | (uint64_t)m->cpu.x[rs1];
    return true;
  default:
    return false;
  }
}

static bool rv32emu_exec_muldiv(rv32emu_machine_t *m, uint32_t rd, uint32_t funct3,
                                uint32_t rs1v, uint32_t rs2v) {
  int32_t s_rs1 = (int32_t)rs1v;
  int32_t s_rs2 = (int32_t)rs2v;
  uint32_t result = 0;

  switch (funct3) {
  case 0x0: /* mul */
    result = (uint32_t)((int64_t)s_rs1 * (int64_t)s_rs2);
    break;
  case 0x1: /* mulh */
    result = (uint32_t)(((int64_t)s_rs1 * (int64_t)s_rs2) >> 32);
    break;
  case 0x2: /* mulhsu */
    result = (uint32_t)(((int64_t)s_rs1 * (int64_t)(uint64_t)rs2v) >> 32);
    break;
  case 0x3: /* mulhu */
    result = (uint32_t)(((uint64_t)rs1v * (uint64_t)rs2v) >> 32);
    break;
  case 0x4: /* div */
    if (rs2v == 0u) {
      result = 0xffffffffu;
    } else if (s_rs1 == INT32_MIN && s_rs2 == -1) {
      result = (uint32_t)INT32_MIN;
    } else {
      result = (uint32_t)(s_rs1 / s_rs2);
    }
    break;
  case 0x5: /* divu */
    result = (rs2v == 0u) ? 0xffffffffu : (rs1v / rs2v);
    break;
  case 0x6: /* rem */
    if (rs2v == 0u) {
      result = rs1v;
    } else if (s_rs1 == INT32_MIN && s_rs2 == -1) {
      result = 0u;
    } else {
      result = (uint32_t)(s_rs1 % s_rs2);
    }
    break;
  case 0x7: /* remu */
    result = (rs2v == 0u) ? rs1v : (rs1v % rs2v);
    break;
  default:
    return false;
  }

  rv32emu_write_rd(m, rd, result);
  return true;
}

static bool rv32emu_exec_amo(rv32emu_machine_t *m, uint32_t insn) {
  uint32_t rd = rv32emu_bits(insn, 11, 7);
  uint32_t rs1 = rv32emu_bits(insn, 19, 15);
  uint32_t rs2 = rv32emu_bits(insn, 24, 20);
  uint32_t funct3 = rv32emu_bits(insn, 14, 12);
  uint32_t funct5 = rv32emu_bits(insn, 31, 27);
  uint32_t addr = m->cpu.x[rs1];
  uint32_t old_val = 0;
  uint32_t new_val = 0;
  uint32_t rs2v = m->cpu.x[rs2];

  if (funct3 != 0x2u) {
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
    return false;
  }
  if ((addr & 3u) != 0u) {
    rv32emu_raise_exception(m, RV32EMU_EXC_LOAD_MISALIGNED, addr);
    return false;
  }

  if (funct5 == 0x2u) { /* lr.w */
    if (rs2 != 0u) {
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
      return false;
    }
    if (!rv32emu_virt_read(m, addr, 4, RV32EMU_ACC_LOAD, &old_val)) {
      return false;
    }
    m->cpu.lr_addr = addr;
    m->cpu.lr_valid = true;
    rv32emu_write_rd(m, rd, old_val);
    return true;
  }

  if (funct5 == 0x3u) { /* sc.w */
    uint32_t status = 1u;

    if (m->cpu.lr_valid && m->cpu.lr_addr == addr) {
      if (!rv32emu_virt_write(m, addr, 4, RV32EMU_ACC_STORE, rs2v)) {
        return false;
      }
      status = 0u;
    }
    m->cpu.lr_valid = false;
    rv32emu_write_rd(m, rd, status);
    return true;
  }

  if (!rv32emu_virt_read(m, addr, 4, RV32EMU_ACC_LOAD, &old_val)) {
    return false;
  }

  switch (funct5) {
  case 0x01: /* amoswap.w */
    new_val = rs2v;
    break;
  case 0x00: /* amoadd.w */
    new_val = old_val + rs2v;
    break;
  case 0x04: /* amoxor.w */
    new_val = old_val ^ rs2v;
    break;
  case 0x0c: /* amoand.w */
    new_val = old_val & rs2v;
    break;
  case 0x08: /* amoor.w */
    new_val = old_val | rs2v;
    break;
  case 0x10: /* amomin.w */
    new_val = ((int32_t)old_val < (int32_t)rs2v) ? old_val : rs2v;
    break;
  case 0x14: /* amomax.w */
    new_val = ((int32_t)old_val > (int32_t)rs2v) ? old_val : rs2v;
    break;
  case 0x18: /* amominu.w */
    new_val = (old_val < rs2v) ? old_val : rs2v;
    break;
  case 0x1c: /* amomaxu.w */
    new_val = (old_val > rs2v) ? old_val : rs2v;
    break;
  default:
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
    return false;
  }

  if (!rv32emu_virt_write(m, addr, 4, RV32EMU_ACC_STORE, new_val)) {
    return false;
  }
  m->cpu.lr_valid = false;
  rv32emu_write_rd(m, rd, old_val);
  return true;
}

static bool rv32emu_exec_compressed(rv32emu_machine_t *m, uint16_t insn, uint32_t *next_pc) {
  uint32_t quadrant = rv32emu_c_bits(insn, 1, 0);
  uint32_t funct3 = rv32emu_c_bits(insn, 15, 13);
  uint32_t rd;
  uint32_t rs1;
  uint32_t rs2;
  uint32_t addr;
  uint32_t imm;
  uint32_t shamt;
  uint32_t value;

  switch (quadrant) {
  case 0x0:
    switch (funct3) {
    case 0x0: /* c.addi4spn */
      rd = 8u + rv32emu_c_bits(insn, 4, 2);
      imm = (rv32emu_c_bits(insn, 6, 6) << 2) | (rv32emu_c_bits(insn, 5, 5) << 3) |
            (rv32emu_c_bits(insn, 12, 11) << 4) | (rv32emu_c_bits(insn, 10, 7) << 6);
      if (imm == 0u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      rv32emu_write_rd(m, rd, m->cpu.x[2] + imm);
      return true;
    case 0x1: /* c.fld */
      rs1 = 8u + rv32emu_c_bits(insn, 9, 7);
      rd = 8u + rv32emu_c_bits(insn, 4, 2);
      imm = (rv32emu_c_bits(insn, 12, 10) << 3) | (rv32emu_c_bits(insn, 6, 5) << 6);
      return rv32emu_exec_fp_load(m, rd, 0x3, m->cpu.x[rs1], (int32_t)imm);
    case 0x2: /* c.lw */
      rs1 = 8u + rv32emu_c_bits(insn, 9, 7);
      rd = 8u + rv32emu_c_bits(insn, 4, 2);
      imm = (rv32emu_c_bits(insn, 6, 6) << 2) | (rv32emu_c_bits(insn, 12, 10) << 3) |
            (rv32emu_c_bits(insn, 5, 5) << 6);
      addr = m->cpu.x[rs1] + imm;
      if (!rv32emu_load_value(m, addr, 0x2, &value)) {
        return false;
      }
      rv32emu_write_rd(m, rd, value);
      return true;
    case 0x3: /* c.flw */
      rs1 = 8u + rv32emu_c_bits(insn, 9, 7);
      rd = 8u + rv32emu_c_bits(insn, 4, 2);
      imm = (rv32emu_c_bits(insn, 6, 6) << 2) | (rv32emu_c_bits(insn, 12, 10) << 3) |
            (rv32emu_c_bits(insn, 5, 5) << 6);
      return rv32emu_exec_fp_load(m, rd, 0x2, m->cpu.x[rs1], (int32_t)imm);
    case 0x5: /* c.fsd */
      rs1 = 8u + rv32emu_c_bits(insn, 9, 7);
      rs2 = 8u + rv32emu_c_bits(insn, 4, 2);
      imm = (rv32emu_c_bits(insn, 12, 10) << 3) | (rv32emu_c_bits(insn, 6, 5) << 6);
      return rv32emu_exec_fp_store(m, rs2, 0x3, m->cpu.x[rs1], (int32_t)imm);
    case 0x6: /* c.sw */
      rs1 = 8u + rv32emu_c_bits(insn, 9, 7);
      rs2 = 8u + rv32emu_c_bits(insn, 4, 2);
      imm = (rv32emu_c_bits(insn, 6, 6) << 2) | (rv32emu_c_bits(insn, 12, 10) << 3) |
            (rv32emu_c_bits(insn, 5, 5) << 6);
      addr = m->cpu.x[rs1] + imm;
      return rv32emu_store_value(m, addr, 0x2, m->cpu.x[rs2]);
    case 0x7: /* c.fsw */
      rs1 = 8u + rv32emu_c_bits(insn, 9, 7);
      rs2 = 8u + rv32emu_c_bits(insn, 4, 2);
      imm = (rv32emu_c_bits(insn, 6, 6) << 2) | (rv32emu_c_bits(insn, 12, 10) << 3) |
            (rv32emu_c_bits(insn, 5, 5) << 6);
      return rv32emu_exec_fp_store(m, rs2, 0x2, m->cpu.x[rs1], (int32_t)imm);
    default:
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
      return false;
    }
  case 0x1:
    switch (funct3) {
    case 0x0: /* c.addi/c.nop */
      rd = rv32emu_c_bits(insn, 11, 7);
      imm = (uint32_t)rv32emu_c_imm_addi(insn);
      rv32emu_write_rd(m, rd, m->cpu.x[rd] + imm);
      return true;
    case 0x1: /* c.jal */
      rv32emu_write_rd(m, 1, m->cpu.pc + 2u);
      *next_pc = m->cpu.pc + (uint32_t)rv32emu_c_imm_j(insn);
      return true;
    case 0x2: /* c.li */
      rd = rv32emu_c_bits(insn, 11, 7);
      rv32emu_write_rd(m, rd, (uint32_t)rv32emu_c_imm_addi(insn));
      return true;
    case 0x3: /* c.addi16sp/c.lui */
      rd = rv32emu_c_bits(insn, 11, 7);
      if (rd == 2u) {
        imm = (rv32emu_c_bits(insn, 6, 6) << 4) | (rv32emu_c_bits(insn, 2, 2) << 5) |
              (rv32emu_c_bits(insn, 5, 5) << 6) | (rv32emu_c_bits(insn, 4, 3) << 7) |
              (rv32emu_c_bits(insn, 12, 12) << 9);
        if (imm == 0u) {
          rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
          return false;
        }
        rv32emu_write_rd(m, 2, m->cpu.x[2] + (uint32_t)rv32emu_sign_extend(imm, 10));
        return true;
      }
      if (rd == 0u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      imm = (rv32emu_c_bits(insn, 12, 12) << 5) | rv32emu_c_bits(insn, 6, 2);
      if (imm == 0u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      rv32emu_write_rd(m, rd, (uint32_t)rv32emu_sign_extend(imm, 6) << 12);
      return true;
    case 0x4:
      rd = 8u + rv32emu_c_bits(insn, 9, 7);
      switch (rv32emu_c_bits(insn, 11, 10)) {
      case 0x0: /* c.srli */
        if (rv32emu_c_bits(insn, 12, 12) != 0u) {
          rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
          return false;
        }
        shamt = rv32emu_c_bits(insn, 6, 2);
        rv32emu_write_rd(m, rd, m->cpu.x[rd] >> shamt);
        return true;
      case 0x1: /* c.srai */
        if (rv32emu_c_bits(insn, 12, 12) != 0u) {
          rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
          return false;
        }
        shamt = rv32emu_c_bits(insn, 6, 2);
        rv32emu_write_rd(m, rd, (uint32_t)((int32_t)m->cpu.x[rd] >> shamt));
        return true;
      case 0x2: /* c.andi */
        rv32emu_write_rd(m, rd, m->cpu.x[rd] & (uint32_t)rv32emu_c_imm_addi(insn));
        return true;
      case 0x3:
        if (rv32emu_c_bits(insn, 12, 12) != 0u) {
          rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
          return false;
        }
        rs2 = 8u + rv32emu_c_bits(insn, 4, 2);
        switch (rv32emu_c_bits(insn, 6, 5)) {
        case 0x0: /* c.sub */
          rv32emu_write_rd(m, rd, m->cpu.x[rd] - m->cpu.x[rs2]);
          return true;
        case 0x1: /* c.xor */
          rv32emu_write_rd(m, rd, m->cpu.x[rd] ^ m->cpu.x[rs2]);
          return true;
        case 0x2: /* c.or */
          rv32emu_write_rd(m, rd, m->cpu.x[rd] | m->cpu.x[rs2]);
          return true;
        case 0x3: /* c.and */
          rv32emu_write_rd(m, rd, m->cpu.x[rd] & m->cpu.x[rs2]);
          return true;
        default:
          break;
        }
        break;
      default:
        break;
      }
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
      return false;
    case 0x5: /* c.j */
      *next_pc = m->cpu.pc + (uint32_t)rv32emu_c_imm_j(insn);
      return true;
    case 0x6: /* c.beqz */
      rs1 = 8u + rv32emu_c_bits(insn, 9, 7);
      if (m->cpu.x[rs1] == 0u) {
        *next_pc = m->cpu.pc + (uint32_t)rv32emu_c_imm_b(insn);
      }
      return true;
    case 0x7: /* c.bnez */
      rs1 = 8u + rv32emu_c_bits(insn, 9, 7);
      if (m->cpu.x[rs1] != 0u) {
        *next_pc = m->cpu.pc + (uint32_t)rv32emu_c_imm_b(insn);
      }
      return true;
    default:
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
      return false;
    }
  case 0x2:
    switch (funct3) {
    case 0x0: /* c.slli */
      rd = rv32emu_c_bits(insn, 11, 7);
      if (rd == 0u || rv32emu_c_bits(insn, 12, 12) != 0u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      shamt = rv32emu_c_bits(insn, 6, 2);
      rv32emu_write_rd(m, rd, m->cpu.x[rd] << shamt);
      return true;
    case 0x1: /* c.fldsp */
      rd = rv32emu_c_bits(insn, 11, 7);
      if (rd == 0u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      imm = (rv32emu_c_bits(insn, 4, 2) << 6) | (rv32emu_c_bits(insn, 12, 12) << 5) |
            (rv32emu_c_bits(insn, 6, 5) << 3);
      return rv32emu_exec_fp_load(m, rd, 0x3, m->cpu.x[2], (int32_t)imm);
    case 0x2: /* c.lwsp */
      rd = rv32emu_c_bits(insn, 11, 7);
      if (rd == 0u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      imm = (rv32emu_c_bits(insn, 6, 4) << 2) | (rv32emu_c_bits(insn, 12, 12) << 5) |
            (rv32emu_c_bits(insn, 3, 2) << 6);
      addr = m->cpu.x[2] + imm;
      if (!rv32emu_load_value(m, addr, 0x2, &value)) {
        return false;
      }
      rv32emu_write_rd(m, rd, value);
      return true;
    case 0x3: /* c.flwsp */
      rd = rv32emu_c_bits(insn, 11, 7);
      if (rd == 0u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      imm = (rv32emu_c_bits(insn, 6, 4) << 2) | (rv32emu_c_bits(insn, 12, 12) << 5) |
            (rv32emu_c_bits(insn, 3, 2) << 6);
      return rv32emu_exec_fp_load(m, rd, 0x2, m->cpu.x[2], (int32_t)imm);
    case 0x4:
      rd = rv32emu_c_bits(insn, 11, 7);
      rs2 = rv32emu_c_bits(insn, 6, 2);
      if (rv32emu_c_bits(insn, 12, 12) == 0u) {
        if (rs2 == 0u) { /* c.jr */
          if (rd == 0u) {
            rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
            return false;
          }
          *next_pc = m->cpu.x[rd] & ~1u;
          return true;
        }
        if (rd == 0u) {
          rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
          return false;
        }
        rv32emu_write_rd(m, rd, m->cpu.x[rs2]); /* c.mv */
        return true;
      }
      if (rd == 0u && rs2 == 0u) { /* c.ebreak */
        rv32emu_raise_exception(m, RV32EMU_EXC_BREAKPOINT, m->cpu.pc);
        return false;
      }
      if (rs2 == 0u) { /* c.jalr */
        if (rd == 0u) {
          rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
          return false;
        }
        rv32emu_write_rd(m, 1, m->cpu.pc + 2u);
        *next_pc = m->cpu.x[rd] & ~1u;
        return true;
      }
      if (rd == 0u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      rv32emu_write_rd(m, rd, m->cpu.x[rd] + m->cpu.x[rs2]); /* c.add */
      return true;
    case 0x5: /* c.fsdsp */
      rs2 = rv32emu_c_bits(insn, 6, 2);
      imm = (rv32emu_c_bits(insn, 12, 10) << 3) | (rv32emu_c_bits(insn, 9, 7) << 6);
      return rv32emu_exec_fp_store(m, rs2, 0x3, m->cpu.x[2], (int32_t)imm);
    case 0x6: /* c.swsp */
      rs2 = rv32emu_c_bits(insn, 6, 2);
      imm = (rv32emu_c_bits(insn, 12, 9) << 2) | (rv32emu_c_bits(insn, 8, 7) << 6);
      addr = m->cpu.x[2] + imm;
      return rv32emu_store_value(m, addr, 0x2, m->cpu.x[rs2]);
    case 0x7: /* c.fswsp */
      rs2 = rv32emu_c_bits(insn, 6, 2);
      imm = (rv32emu_c_bits(insn, 12, 9) << 2) | (rv32emu_c_bits(insn, 8, 7) << 6);
      return rv32emu_exec_fp_store(m, rs2, 0x2, m->cpu.x[2], (int32_t)imm);
    default:
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
      return false;
    }
  default:
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
    return false;
  }
}

static bool rv32emu_exec_one(rv32emu_machine_t *m) {
  uint32_t insn = 0;
  uint32_t opcode;
  uint32_t rd;
  uint32_t funct3;
  uint32_t rs1;
  uint32_t rs2;
  uint32_t funct7;
  uint32_t next_pc;
  uint32_t rs1v;
  uint32_t rs2v;
  uint32_t tmp;
  uint32_t insn16 = 0;

  if ((m->cpu.pc & 1u) != 0u) {
    rv32emu_raise_exception(m, RV32EMU_EXC_INST_MISALIGNED, m->cpu.pc);
    return false;
  }

  if (!rv32emu_virt_read(m, m->cpu.pc, 2, RV32EMU_ACC_FETCH, &insn16)) {
    return false;
  }

  if ((insn16 & 0x3u) != 0x3u) {
    next_pc = m->cpu.pc + 2u;
    if (!rv32emu_exec_compressed(m, (uint16_t)insn16, &next_pc)) {
      return false;
    }

    m->cpu.pc = next_pc;
    m->cpu.x[0] = 0;
    m->cpu.cycle += 1;
    m->cpu.instret += 1;
    rv32emu_step_timer(m);
    return true;
  }

  if (!rv32emu_virt_read(m, m->cpu.pc, 4, RV32EMU_ACC_FETCH, &insn)) {
    return false;
  }

  opcode = rv32emu_bits(insn, 6, 0);
  rd = rv32emu_bits(insn, 11, 7);
  funct3 = rv32emu_bits(insn, 14, 12);
  rs1 = rv32emu_bits(insn, 19, 15);
  rs2 = rv32emu_bits(insn, 24, 20);
  funct7 = rv32emu_bits(insn, 31, 25);
  next_pc = m->cpu.pc + 4u;

  rs1v = m->cpu.x[rs1];
  rs2v = m->cpu.x[rs2];

  switch (opcode) {
  case 0x37: /* lui */
    rv32emu_write_rd(m, rd, (uint32_t)rv32emu_imm_u(insn));
    break;
  case 0x17: /* auipc */
    rv32emu_write_rd(m, rd, m->cpu.pc + (uint32_t)rv32emu_imm_u(insn));
    break;
  case 0x6f: /* jal */
    rv32emu_write_rd(m, rd, next_pc);
    next_pc = m->cpu.pc + (uint32_t)rv32emu_imm_j(insn);
    break;
  case 0x67: /* jalr */
    if (funct3 != 0x0) {
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
      return false;
    }
    tmp = next_pc;
    next_pc = (rs1v + (uint32_t)rv32emu_imm_i(insn)) & ~1u;
    rv32emu_write_rd(m, rd, tmp);
    break;
  case 0x63: /* branch */
    switch (funct3) {
    case 0x0:
      if (rs1v == rs2v) {
        next_pc = m->cpu.pc + (uint32_t)rv32emu_imm_b(insn);
      }
      break;
    case 0x1:
      if (rs1v != rs2v) {
        next_pc = m->cpu.pc + (uint32_t)rv32emu_imm_b(insn);
      }
      break;
    case 0x4:
      if ((int32_t)rs1v < (int32_t)rs2v) {
        next_pc = m->cpu.pc + (uint32_t)rv32emu_imm_b(insn);
      }
      break;
    case 0x5:
      if ((int32_t)rs1v >= (int32_t)rs2v) {
        next_pc = m->cpu.pc + (uint32_t)rv32emu_imm_b(insn);
      }
      break;
    case 0x6:
      if (rs1v < rs2v) {
        next_pc = m->cpu.pc + (uint32_t)rv32emu_imm_b(insn);
      }
      break;
    case 0x7:
      if (rs1v >= rs2v) {
        next_pc = m->cpu.pc + (uint32_t)rv32emu_imm_b(insn);
      }
      break;
    default:
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
      return false;
    }
    break;
  case 0x03: /* load */
    tmp = rs1v + (uint32_t)rv32emu_imm_i(insn);
    if (!rv32emu_load_value(m, tmp, funct3, &tmp)) {
      return false;
    }
    rv32emu_write_rd(m, rd, tmp);
    break;
  case 0x07: /* load-fp */
    if (!rv32emu_exec_fp_load(m, rd, funct3, rs1v, rv32emu_imm_i(insn))) {
      return false;
    }
    break;
  case 0x23: /* store */
    tmp = rs1v + (uint32_t)rv32emu_imm_s(insn);
    if (!rv32emu_store_value(m, tmp, funct3, rs2v)) {
      return false;
    }
    break;
  case 0x27: /* store-fp */
    if (!rv32emu_exec_fp_store(m, rs2, funct3, rs1v, rv32emu_imm_s(insn))) {
      return false;
    }
    break;
  case 0x13: /* op-imm */
    switch (funct3) {
    case 0x0:
      rv32emu_write_rd(m, rd, rs1v + (uint32_t)rv32emu_imm_i(insn));
      break;
    case 0x2:
      rv32emu_write_rd(m, rd, ((int32_t)rs1v < rv32emu_imm_i(insn)) ? 1u : 0u);
      break;
    case 0x3:
      rv32emu_write_rd(m, rd, (rs1v < (uint32_t)rv32emu_imm_i(insn)) ? 1u : 0u);
      break;
    case 0x4:
      rv32emu_write_rd(m, rd, rs1v ^ (uint32_t)rv32emu_imm_i(insn));
      break;
    case 0x6:
      rv32emu_write_rd(m, rd, rs1v | (uint32_t)rv32emu_imm_i(insn));
      break;
    case 0x7:
      rv32emu_write_rd(m, rd, rs1v & (uint32_t)rv32emu_imm_i(insn));
      break;
    case 0x1:
      rv32emu_write_rd(m, rd, rs1v << rv32emu_bits(insn, 24, 20));
      break;
    case 0x5:
      if (funct7 == 0x00u) {
        rv32emu_write_rd(m, rd, rs1v >> rv32emu_bits(insn, 24, 20));
      } else if (funct7 == 0x20u) {
        rv32emu_write_rd(m, rd, (uint32_t)((int32_t)rs1v >> rv32emu_bits(insn, 24, 20)));
      } else {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      break;
    default:
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
      return false;
    }
    break;
  case 0x33: /* op */
    if (funct7 == 0x01u) {
      if (!rv32emu_exec_muldiv(m, rd, funct3, rs1v, rs2v)) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      break;
    }
    switch (funct3) {
    case 0x0:
      if (funct7 == 0x00u) {
        rv32emu_write_rd(m, rd, rs1v + rs2v);
      } else if (funct7 == 0x20u) {
        rv32emu_write_rd(m, rd, rs1v - rs2v);
      } else {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      break;
    case 0x1:
      rv32emu_write_rd(m, rd, rs1v << (rs2v & 0x1fu));
      break;
    case 0x2:
      rv32emu_write_rd(m, rd, ((int32_t)rs1v < (int32_t)rs2v) ? 1u : 0u);
      break;
    case 0x3:
      rv32emu_write_rd(m, rd, (rs1v < rs2v) ? 1u : 0u);
      break;
    case 0x4:
      rv32emu_write_rd(m, rd, rs1v ^ rs2v);
      break;
    case 0x5:
      if (funct7 == 0x00u) {
        rv32emu_write_rd(m, rd, rs1v >> (rs2v & 0x1fu));
      } else if (funct7 == 0x20u) {
        rv32emu_write_rd(m, rd, (uint32_t)((int32_t)rs1v >> (rs2v & 0x1fu)));
      } else {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      break;
    case 0x6:
      rv32emu_write_rd(m, rd, rs1v | rs2v);
      break;
    case 0x7:
      rv32emu_write_rd(m, rd, rs1v & rs2v);
      break;
    default:
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
      return false;
    }
    break;
  case 0x0f: /* fence/fence.i */
    break;
  case 0x2f: /* amo */
    if (!rv32emu_exec_amo(m, insn)) {
      return false;
    }
    break;
  case 0x53: /* op-fp */
    if (!rv32emu_exec_fp_op(m, rd, rs1, rs2, funct3, funct7)) {
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
      return false;
    }
    break;
  case 0x73: /* system */
    if (funct3 != 0u) {
      if (!rv32emu_exec_csr_op(m, insn, rd, funct3, rs1, rs1v)) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      break;
    }
    if (insn == 0x00000073u) { /* ecall */
      uint32_t cause = RV32EMU_EXC_ECALL_M;
      if (m->cpu.priv == RV32EMU_PRIV_S) {
        cause = RV32EMU_EXC_ECALL_S;
      } else if (m->cpu.priv == RV32EMU_PRIV_U) {
        cause = RV32EMU_EXC_ECALL_U;
      }
      if (!rv32emu_handle_sbi_ecall(m)) {
        rv32emu_raise_exception(m, cause, 0);
        return false;
      }
      break;
    }
    if (insn == 0x00100073u) { /* ebreak */
      rv32emu_raise_exception(m, RV32EMU_EXC_BREAKPOINT, m->cpu.pc);
      return false;
    }
    if (insn == 0x30200073u) { /* mret */
      if (!rv32emu_exec_mret(m, &next_pc)) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      break;
    }
    if (insn == 0x10200073u) { /* sret */
      if (!rv32emu_exec_sret(m, &next_pc)) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      break;
    }
    if (insn == 0x10500073u) { /* wfi */
      break;
    }
    if ((insn & 0xfe007fffu) == 0x12000073u) { /* sfence.vma */
      if (m->cpu.priv == RV32EMU_PRIV_U) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
        return false;
      }
      break;
    }
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
    return false;
  default:
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, insn);
    return false;
  }

  m->cpu.pc = next_pc;
  m->cpu.x[0] = 0;
  m->cpu.cycle += 1;
  m->cpu.instret += 1;
  rv32emu_step_timer(m);
  return true;
}

int rv32emu_run(rv32emu_machine_t *m, uint64_t max_instructions) {
  uint64_t executed = 0;
  uint32_t next_hart = 0;

  if (m == NULL) {
    return -1;
  }

  if (m->hart_count == 0u || m->hart_count > RV32EMU_MAX_HARTS) {
    return -1;
  }

  if (max_instructions == 0) {
    max_instructions = RV32EMU_DEFAULT_MAX_INSTR;
  }

  while (executed < max_instructions) {
    bool progressed = false;
    uint32_t checked;

    for (checked = 0u; checked < m->hart_count; checked++) {
      uint32_t hart = (next_hart + checked) % m->hart_count;

      if (!m->harts[hart].running) {
        continue;
      }

      progressed = true;
      m->active_hart = hart;

      if (hart != 0u) {
        rv32emu_cpu_t tmp = m->harts[0];
        m->harts[0] = m->harts[hart];
        m->harts[hart] = tmp;
      }

      if (rv32emu_check_pending_interrupt(m)) {
        if (hart != 0u) {
          rv32emu_cpu_t tmp = m->harts[0];
          m->harts[0] = m->harts[hart];
          m->harts[hart] = tmp;
        }
        m->active_hart = 0u;
        next_hart = (hart + 1u) % m->hart_count;
        break;
      }

      if (rv32emu_exec_one(m)) {
        executed += 1;
      }

      if (hart != 0u) {
        rv32emu_cpu_t tmp = m->harts[0];
        m->harts[0] = m->harts[hart];
        m->harts[hart] = tmp;
      }
      m->active_hart = 0u;
      next_hart = (hart + 1u) % m->hart_count;
      break;
    }

    if (!progressed) {
      break;
    }
  }

  return (int)executed;
}
