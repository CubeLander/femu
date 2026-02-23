#include "rv32emu.h"

#include <stdbool.h>
#include <stdint.h>

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

bool rv32emu_exec_csr_op(rv32emu_machine_t *m, uint32_t insn, uint32_t rd, uint32_t funct3,
                         uint32_t rs1, uint32_t rs1v) {
  uint16_t csr_num = (uint16_t)(insn >> 20);
  uint32_t old_value = rv32emu_csr_read(m, csr_num);
  uint32_t new_value = old_value;
  uint32_t zimm = rs1 & 0x1fu;

  if (!rv32emu_csr_is_implemented(csr_num)) {
    return false;
  }

  switch (funct3) {
  case 0x1:
    new_value = rs1v;
    rv32emu_csr_write(m, csr_num, new_value);
    break;
  case 0x2:
    if (rs1 != 0u) {
      new_value = old_value | rs1v;
      rv32emu_csr_write(m, csr_num, new_value);
    }
    break;
  case 0x3:
    if (rs1 != 0u) {
      new_value = old_value & ~rs1v;
      rv32emu_csr_write(m, csr_num, new_value);
    }
    break;
  case 0x5:
    new_value = zimm;
    rv32emu_csr_write(m, csr_num, new_value);
    break;
  case 0x6:
    if (zimm != 0u) {
      new_value = old_value | zimm;
      rv32emu_csr_write(m, csr_num, new_value);
    }
    break;
  case 0x7:
    if (zimm != 0u) {
      new_value = old_value & ~zimm;
      rv32emu_csr_write(m, csr_num, new_value);
    }
    break;
  default:
    return false;
  }

  if (rd != 0u) {
    RV32EMU_CPU(m)->x[rd] = old_value;
  }
  return true;
}

bool rv32emu_exec_mret(rv32emu_machine_t *m, uint32_t *next_pc) {
  uint32_t mstatus;
  uint32_t mpp;

  if (RV32EMU_CPU(m)->priv != RV32EMU_PRIV_M) {
    return false;
  }

  mstatus = RV32EMU_CPU(m)->csr[CSR_MSTATUS];
  mpp = (mstatus & MSTATUS_MPP_MASK) >> MSTATUS_MPP_SHIFT;

  if ((mstatus & MSTATUS_MPIE) != 0u) {
    mstatus |= MSTATUS_MIE;
  } else {
    mstatus &= ~MSTATUS_MIE;
  }
  mstatus |= MSTATUS_MPIE;
  mstatus &= ~MSTATUS_MPP_MASK;

  RV32EMU_CPU(m)->csr[CSR_MSTATUS] = mstatus;
  RV32EMU_CPU(m)->priv = (rv32emu_priv_t)(mpp & 0x3u);
  *next_pc = RV32EMU_CPU(m)->csr[CSR_MEPC] & ~1u;
  return true;
}

bool rv32emu_exec_sret(rv32emu_machine_t *m, uint32_t *next_pc) {
  uint32_t mstatus;

  if (RV32EMU_CPU(m)->priv == RV32EMU_PRIV_U) {
    return false;
  }

  mstatus = RV32EMU_CPU(m)->csr[CSR_MSTATUS];
  if ((mstatus & MSTATUS_SPIE) != 0u) {
    mstatus |= MSTATUS_SIE;
  } else {
    mstatus &= ~MSTATUS_SIE;
  }
  mstatus |= MSTATUS_SPIE;

  if ((mstatus & MSTATUS_SPP) != 0u) {
    RV32EMU_CPU(m)->priv = RV32EMU_PRIV_S;
  } else {
    RV32EMU_CPU(m)->priv = RV32EMU_PRIV_U;
  }
  mstatus &= ~MSTATUS_SPP;

  RV32EMU_CPU(m)->csr[CSR_MSTATUS] = mstatus;
  *next_pc = RV32EMU_CPU(m)->csr[CSR_SEPC] & ~1u;
  return true;
}
