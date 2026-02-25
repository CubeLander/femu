#include "rv32emu.h"

#include <stdio.h>

#define SATP_MODE_SV32 (1u << 31)
#define SATP_PPN_MASK 0x003fffffu

/*
 * Reservation invalidation needs range overlap checks because stores may be
 * 1/2/4-byte and may be unaligned (emulated as byte writes). We treat any
 * overlap with a reserved 4-byte word as a conflict.
 */
static bool rv32emu_ranges_overlap(uint32_t addr_a, uint32_t len_a, uint32_t addr_b,
                                   uint32_t len_b) {
  uint64_t end_a;
  uint64_t end_b;

  if (len_a == 0u || len_b == 0u) {
    return false;
  }

  end_a = (uint64_t)addr_a + (uint64_t)len_a;
  end_b = (uint64_t)addr_b + (uint64_t)len_b;
  return ((uint64_t)addr_a < end_b) && ((uint64_t)addr_b < end_a);
}

/*
 * RISC-V LR/SC rule (architecturally simplified for this emulator):
 * any successful store to the reserved word from any hart invalidates the
 * reservation, so a later sc.w must fail.
 *
 * We track one reservation per hart (lr_addr/lr_valid) and clear all
 * overlapping reservations after every committed store.
 */
static void rv32emu_invalidate_lr_reservations(rv32emu_machine_t *m, uint32_t vaddr, int len) {
  uint32_t hartid;

  if (m == NULL || len <= 0) {
    return;
  }

  for (hartid = 0u; hartid < m->hart_count; hartid++) {
    rv32emu_cpu_t *cpu = rv32emu_hart_cpu(m, hartid);

    if (cpu == NULL || !atomic_load_explicit(&cpu->lr_valid, memory_order_acquire)) {
      continue;
    }
    if (rv32emu_ranges_overlap(vaddr, (uint32_t)len, cpu->lr_addr, 4u)) {
      atomic_store_explicit(&cpu->lr_valid, false, memory_order_release);
    }
  }
}

static void rv32emu_raise_page_fault(rv32emu_machine_t *m, rv32emu_access_t access,
                                     uint32_t vaddr) {
  uint32_t cause = RV32EMU_EXC_LOAD_PAGE_FAULT;

  if (access == RV32EMU_ACC_FETCH) {
    cause = RV32EMU_EXC_INST_PAGE_FAULT;
  } else if (access == RV32EMU_ACC_STORE) {
    cause = RV32EMU_EXC_STORE_PAGE_FAULT;
  }

  rv32emu_raise_exception(m, cause, vaddr);
}

static void rv32emu_raise_access_fault(rv32emu_machine_t *m, rv32emu_access_t access,
                                       uint32_t vaddr) {
  uint32_t cause = RV32EMU_EXC_LOAD_ACCESS_FAULT;

  if (access == RV32EMU_ACC_FETCH) {
    cause = RV32EMU_EXC_INST_ACCESS_FAULT;
  } else if (access == RV32EMU_ACC_STORE) {
    cause = RV32EMU_EXC_STORE_ACCESS_FAULT;
  }

  rv32emu_raise_exception(m, cause, vaddr);
}

static void rv32emu_take_trap(rv32emu_machine_t *m, uint32_t cause, uint32_t tval,
                              bool is_interrupt) {
  uint32_t cause_value = cause;
  uint32_t cause_bit = cause & 0x1fu;
  uint32_t deleg_mask;
  bool delegated_to_s = false;
  uint32_t tvec_mode;
  rv32emu_priv_t from_priv;
  uint32_t from_pc;

  if (is_interrupt) {
    cause_value |= 0x80000000u;
  }

  from_priv = RV32EMU_CPU(m)->priv;
  from_pc = RV32EMU_CPU(m)->pc;
  deleg_mask = is_interrupt ? RV32EMU_CPU(m)->csr[CSR_MIDELEG] : RV32EMU_CPU(m)->csr[CSR_MEDELEG];
  if (RV32EMU_CPU(m)->priv != RV32EMU_PRIV_M) {
    delegated_to_s = (deleg_mask & (1u << cause_bit)) != 0u;
  }

  if (delegated_to_s) {
    uint32_t mstatus = RV32EMU_CPU(m)->csr[CSR_MSTATUS];
    uint32_t stvec_base = RV32EMU_CPU(m)->csr[CSR_STVEC] & ~0x3u;

    RV32EMU_CPU(m)->csr[CSR_SEPC] = RV32EMU_CPU(m)->pc;
    RV32EMU_CPU(m)->csr[CSR_SCAUSE] = cause_value;
    RV32EMU_CPU(m)->csr[CSR_STVAL] = tval;

    if ((mstatus & MSTATUS_SIE) != 0u) {
      mstatus |= MSTATUS_SPIE;
    } else {
      mstatus &= ~MSTATUS_SPIE;
    }
    mstatus &= ~MSTATUS_SIE;

    if (RV32EMU_CPU(m)->priv == RV32EMU_PRIV_S) {
      mstatus |= MSTATUS_SPP;
    } else {
      mstatus &= ~MSTATUS_SPP;
    }

    tvec_mode = RV32EMU_CPU(m)->csr[CSR_STVEC] & 0x3u;
    if (is_interrupt && tvec_mode == 1u) {
      stvec_base += cause_bit * 4u;
    }

    rv32emu_trace_trap(m, from_pc, from_priv, RV32EMU_PRIV_S, cause, tval, is_interrupt,
                       delegated_to_s, stvec_base);
    RV32EMU_CPU(m)->csr[CSR_MSTATUS] = mstatus;
    RV32EMU_CPU(m)->priv = RV32EMU_PRIV_S;
    RV32EMU_CPU(m)->pc = stvec_base;
    atomic_store_explicit(&RV32EMU_CPU(m)->running, stvec_base != 0u, memory_order_release);
    return;
  }

  {
    uint32_t mstatus = RV32EMU_CPU(m)->csr[CSR_MSTATUS];
    uint32_t mtvec_base = RV32EMU_CPU(m)->csr[CSR_MTVEC] & ~0x3u;

    RV32EMU_CPU(m)->csr[CSR_MEPC] = RV32EMU_CPU(m)->pc;
    RV32EMU_CPU(m)->csr[CSR_MCAUSE] = cause_value;
    RV32EMU_CPU(m)->csr[CSR_MTVAL] = tval;

    if ((mstatus & MSTATUS_MIE) != 0u) {
      mstatus |= MSTATUS_MPIE;
    } else {
      mstatus &= ~MSTATUS_MPIE;
    }
    mstatus &= ~MSTATUS_MIE;
    mstatus = (mstatus & ~MSTATUS_MPP_MASK) |
              (((uint32_t)RV32EMU_CPU(m)->priv << MSTATUS_MPP_SHIFT) & MSTATUS_MPP_MASK);

    tvec_mode = RV32EMU_CPU(m)->csr[CSR_MTVEC] & 0x3u;
    if (is_interrupt && tvec_mode == 1u) {
      mtvec_base += cause_bit * 4u;
    }

    rv32emu_trace_trap(m, from_pc, from_priv, RV32EMU_PRIV_M, cause, tval, is_interrupt,
                       delegated_to_s, mtvec_base);
    RV32EMU_CPU(m)->csr[CSR_MSTATUS] = mstatus;
    RV32EMU_CPU(m)->priv = RV32EMU_PRIV_M;
    RV32EMU_CPU(m)->pc = mtvec_base;
    atomic_store_explicit(&RV32EMU_CPU(m)->running, mtvec_base != 0u, memory_order_release);
  }
}

bool rv32emu_translate(rv32emu_machine_t *m, uint32_t vaddr, rv32emu_access_t access,
                       uint32_t *paddr_out) {
  uint32_t satp;
  uint32_t mstatus;
  rv32emu_priv_t effective_priv;
  uint32_t pt_addr;
  uint32_t vpn1;
  uint32_t vpn0;
  uint32_t vpn[2];

  if (m == NULL || paddr_out == NULL) {
    return false;
  }

  satp = RV32EMU_CPU(m)->csr[CSR_SATP];
  mstatus = rv32emu_csr_read(m, CSR_MSTATUS);
  effective_priv = RV32EMU_CPU(m)->priv;

  /* MPRV affects data accesses in M-mode by using MPP translation context. */
  if (RV32EMU_CPU(m)->priv == RV32EMU_PRIV_M && access != RV32EMU_ACC_FETCH &&
      (mstatus & MSTATUS_MPRV) != 0u) {
    effective_priv = (rv32emu_priv_t)((mstatus & MSTATUS_MPP_MASK) >> MSTATUS_MPP_SHIFT);
  }

  if ((satp & SATP_MODE_SV32) == 0 || effective_priv == RV32EMU_PRIV_M) {
    *paddr_out = vaddr;
    return true;
  }

  pt_addr = (satp & SATP_PPN_MASK) << 12;
  vpn1 = (vaddr >> 22) & 0x3ffu;
  vpn0 = (vaddr >> 12) & 0x3ffu;
  vpn[1] = vpn1;
  vpn[0] = vpn0;
  for (int level = 1; level >= 0; level--) {
    uint32_t pte_addr = pt_addr + vpn[level] * 4u;
    uint32_t pte = 0;
    uint32_t pte_flags;
    bool readable;
    bool writable;
    bool executable;
    bool user_page;
    bool leaf;
    bool allow_access = false;
    uint32_t pte_ppn0;
    uint32_t pte_ppn1;
    uint32_t offset;
    uint32_t pa_ppn0;
    uint32_t pa_ppn1;

    if (!rv32emu_phys_read(m, pte_addr, 4, &pte)) {
      rv32emu_raise_page_fault(m, access, vaddr);
      return false;
    }

    pte_flags = pte & 0x3ffu;
    readable = (pte_flags & PTE_R) != 0u;
    writable = (pte_flags & PTE_W) != 0u;
    executable = (pte_flags & PTE_X) != 0u;
    user_page = (pte_flags & PTE_U) != 0u;
    leaf = readable || executable;

    if ((pte_flags & PTE_V) == 0u || (!readable && writable)) {
      rv32emu_raise_page_fault(m, access, vaddr);
      return false;
    }

    if (!leaf) {
      if (level == 0) {
        rv32emu_raise_page_fault(m, access, vaddr);
        return false;
      }
      pt_addr = ((pte >> 10) & SATP_PPN_MASK) << 12;
      continue;
    }

    if (effective_priv == RV32EMU_PRIV_U && !user_page) {
      rv32emu_raise_page_fault(m, access, vaddr);
      return false;
    }

    if (effective_priv == RV32EMU_PRIV_S && user_page) {
      if (access == RV32EMU_ACC_FETCH ||
          ((access == RV32EMU_ACC_LOAD || access == RV32EMU_ACC_STORE) &&
           (mstatus & MSTATUS_SUM) == 0u)) {
        rv32emu_raise_page_fault(m, access, vaddr);
        return false;
      }
    }

    if (access == RV32EMU_ACC_FETCH) {
      allow_access = executable;
    } else if (access == RV32EMU_ACC_LOAD) {
      allow_access = readable || (((mstatus & MSTATUS_MXR) != 0u) && executable);
    } else {
      allow_access = writable;
    }
    if (!allow_access) {
      rv32emu_raise_page_fault(m, access, vaddr);
      return false;
    }

    if ((pte_flags & PTE_A) == 0u || (access == RV32EMU_ACC_STORE && (pte_flags & PTE_D) == 0u)) {
      pte |= PTE_A;
      if (access == RV32EMU_ACC_STORE) {
        pte |= PTE_D;
      }
      if (!rv32emu_phys_write(m, pte_addr, 4, pte)) {
        rv32emu_raise_page_fault(m, access, vaddr);
        return false;
      }
    }

    pte_ppn0 = (pte >> 10) & 0x3ffu;
    pte_ppn1 = (pte >> 20) & 0xfffu;
    if (level == 1 && pte_ppn0 != 0u) {
      rv32emu_raise_page_fault(m, access, vaddr);
      return false;
    }

    offset = vaddr & 0xfffu;
    if (level == 1) {
      pa_ppn1 = pte_ppn1;
      pa_ppn0 = vpn0;
    } else {
      pa_ppn1 = pte_ppn1;
      pa_ppn0 = pte_ppn0;
    }
    *paddr_out = (pa_ppn1 << 22) | (pa_ppn0 << 12) | offset;
    return true;
  }

  rv32emu_raise_page_fault(m, access, vaddr);
  return false;
}

bool rv32emu_virt_read(rv32emu_machine_t *m, uint32_t vaddr, int len,
                       rv32emu_access_t access, uint32_t *out) {
  uint32_t paddr;

  if (!rv32emu_translate(m, vaddr, access, &paddr)) {
    return false;
  }

  if (!rv32emu_phys_read(m, paddr, len, out)) {
    fprintf(stderr, "[WARN] virt_read access fault: va=0x%08x pa=0x%08x len=%d acc=%d\n", vaddr,
            paddr, len, (int)access);
    rv32emu_raise_access_fault(m, access, vaddr);
    return false;
  }

  return true;
}

bool rv32emu_virt_write(rv32emu_machine_t *m, uint32_t vaddr, int len,
                        rv32emu_access_t access, uint32_t data) {
  uint32_t paddr;

  if (!rv32emu_translate(m, vaddr, access, &paddr)) {
    return false;
  }

  if (!rv32emu_phys_write(m, paddr, len, data)) {
    fprintf(stderr, "[WARN] virt_write access fault: va=0x%08x pa=0x%08x len=%d acc=%d\n", vaddr,
            paddr, len, (int)access);
    rv32emu_raise_access_fault(m, access, vaddr);
    return false;
  }

  /*
   * Store-side LR/SC invalidation hook:
   * 1. only after the write is actually committed;
   * 2. applies to normal stores, AMO stores and sc.w stores uniformly;
   * 3. invalidates reservations on all harts for overlapping addresses.
   */
  rv32emu_invalidate_lr_reservations(m, vaddr, len);

  return true;
}

void rv32emu_raise_exception(rv32emu_machine_t *m, uint32_t cause, uint32_t tval) {
  if (m == NULL) {
    return;
  }
  rv32emu_take_trap(m, cause, tval, false);
}

void rv32emu_raise_interrupt(rv32emu_machine_t *m, uint32_t cause_num) {
  if (m == NULL || cause_num > 31u) {
    return;
  }

  rv32emu_cpu_mip_set_bits(RV32EMU_CPU(m), (1u << cause_num));
}

bool rv32emu_check_pending_interrupt(rv32emu_machine_t *m) {
  rv32emu_cpu_t *cpu;
  uint32_t enabled_pending;
  uint32_t mstatus;
  uint32_t mideleg;
  uint32_t selected_cause = 0u;
  bool has_selected = false;
  uint32_t priority[] = {RV32EMU_IRQ_MEIP, RV32EMU_IRQ_MSIP, RV32EMU_IRQ_MTIP,
                         RV32EMU_IRQ_SEIP, RV32EMU_IRQ_SSIP, RV32EMU_IRQ_STIP};

  if (m == NULL) {
    return false;
  }
  cpu = RV32EMU_CPU(m);
  if (cpu == NULL) {
    return false;
  }
  enabled_pending = cpu->csr[CSR_MIE] & rv32emu_cpu_mip_load(cpu);
  mstatus = cpu->csr[CSR_MSTATUS];
  mideleg = cpu->csr[CSR_MIDELEG];

  for (uint32_t i = 0; i < sizeof(priority) / sizeof(priority[0]); i++) {
    uint32_t cause = priority[i];
    uint32_t bit = 1u << cause;
    bool delegated = (mideleg & bit) != 0u;
    bool global_enabled = false;

    if ((enabled_pending & bit) == 0u) {
      continue;
    }

    if (delegated) {
      if (cpu->priv == RV32EMU_PRIV_M) {
        continue;
      }
      if (cpu->priv == RV32EMU_PRIV_S) {
        global_enabled = (mstatus & MSTATUS_SIE) != 0u;
      } else {
        global_enabled = true;
      }
    } else {
      if (cpu->priv == RV32EMU_PRIV_M) {
        global_enabled = (mstatus & MSTATUS_MIE) != 0u;
      } else {
        global_enabled = true;
      }
    }

    if (!global_enabled) {
      continue;
    }

    selected_cause = cause;
    has_selected = true;
    break;
  }

  if (has_selected) {
    rv32emu_take_trap(m, selected_cause, 0, true);
    return true;
  }

  return false;
}
