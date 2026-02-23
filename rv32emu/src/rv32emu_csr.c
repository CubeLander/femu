#include "rv32emu.h"

#define SSTATUS_MASK                                                                \
  (MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_SUM | MSTATUS_MXR)
#define SIE_MASK (MIP_SSIP | MIP_STIP | MIP_SEIP)

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

uint32_t rv32emu_csr_read(rv32emu_machine_t *m, uint16_t csr_num) {
  if (m == NULL || csr_num >= 4096) {
    return 0;
  }

  switch (csr_num) {
  case CSR_FFLAGS:
    return RV32EMU_CPU(m)->csr[CSR_FCSR] & 0x1fu;
  case CSR_FRM:
    return (RV32EMU_CPU(m)->csr[CSR_FCSR] >> 5) & 0x7u;
  case CSR_FCSR:
    return RV32EMU_CPU(m)->csr[CSR_FCSR] & 0xffu;
  case CSR_SSTATUS:
    return RV32EMU_CPU(m)->csr[CSR_MSTATUS] & SSTATUS_MASK;
  case CSR_SIE:
    return RV32EMU_CPU(m)->csr[CSR_MIE] & SIE_MASK;
  case CSR_SIP:
    return RV32EMU_CPU(m)->csr[CSR_MIP] & SIE_MASK;
  case CSR_CYCLE:
    return (uint32_t)RV32EMU_CPU(m)->cycle;
  case CSR_TIME:
    return (uint32_t)m->plat.mtime;
  case CSR_INSTRET:
    return (uint32_t)RV32EMU_CPU(m)->instret;
  case CSR_CYCLEH:
    return (uint32_t)(RV32EMU_CPU(m)->cycle >> 32);
  case CSR_TIMEH:
    return (uint32_t)(m->plat.mtime >> 32);
  case CSR_INSTRETH:
    return (uint32_t)(RV32EMU_CPU(m)->instret >> 32);
  default:
    break;
  }

  if (!rv32emu_csr_is_implemented(csr_num)) {
    return 0;
  }

  return RV32EMU_CPU(m)->csr[csr_num];
}

void rv32emu_csr_write(rv32emu_machine_t *m, uint16_t csr_num, uint32_t value) {
  if (m == NULL || csr_num >= 4096) {
    return;
  }

  switch (csr_num) {
  case CSR_FFLAGS:
    RV32EMU_CPU(m)->csr[CSR_FCSR] = (RV32EMU_CPU(m)->csr[CSR_FCSR] & ~0x1fu) | (value & 0x1fu);
    return;
  case CSR_FRM:
    RV32EMU_CPU(m)->csr[CSR_FCSR] = (RV32EMU_CPU(m)->csr[CSR_FCSR] & ~(0x7u << 5)) | ((value & 0x7u) << 5);
    return;
  case CSR_FCSR:
    RV32EMU_CPU(m)->csr[CSR_FCSR] = value & 0xffu;
    return;
  case CSR_SSTATUS:
    RV32EMU_CPU(m)->csr[CSR_MSTATUS] =
        (RV32EMU_CPU(m)->csr[CSR_MSTATUS] & ~SSTATUS_MASK) | (value & SSTATUS_MASK);
    return;
  case CSR_SIE:
    RV32EMU_CPU(m)->csr[CSR_MIE] = (RV32EMU_CPU(m)->csr[CSR_MIE] & ~SIE_MASK) | (value & SIE_MASK);
    return;
  case CSR_SIP:
    RV32EMU_CPU(m)->csr[CSR_MIP] = (RV32EMU_CPU(m)->csr[CSR_MIP] & ~SIE_MASK) | (value & SIE_MASK);
    return;
  case CSR_CYCLE:
  case CSR_TIME:
  case CSR_INSTRET:
  case CSR_CYCLEH:
  case CSR_TIMEH:
  case CSR_INSTRETH:
    return;
  default:
    if (!rv32emu_csr_is_implemented(csr_num)) {
      return;
    }
    RV32EMU_CPU(m)->csr[csr_num] = value;
    return;
  }
}
