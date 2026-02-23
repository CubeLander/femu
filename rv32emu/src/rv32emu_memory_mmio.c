#include "rv32emu.h"

#include <stdio.h>

#define UART_REG_RBR 0x00u
#define UART_REG_THR 0x00u
#define UART_REG_IER 0x01u
#define UART_REG_IIR 0x02u
#define UART_REG_FCR 0x02u
#define UART_REG_LCR 0x03u
#define UART_REG_MCR 0x04u
#define UART_REG_LSR 0x05u
#define UART_REG_MSR 0x06u
#define UART_REG_SCR 0x07u

#define UART_IER_RDI (1u << 0)
#define UART_IER_THRI (1u << 1)
#define UART_IIR_NO_INT 0x01u
#define UART_IIR_THRI 0x02u
#define UART_IIR_RDI 0x04u
#define UART_LSR_DR (1u << 0)
#define UART_LSR_THRE (1u << 5)
#define UART_LSR_TEMT (1u << 6)
#define UART_PLIC_IRQ 10u

#define CLINT_MSIP_BASE 0x0000u
#define CLINT_MTIMECMP_BASE 0x4000u
#define CLINT_MTIME 0xbff8u

#define PLIC_PENDING 0x1000u
#define PLIC_ENABLE_BASE 0x2000u
#define PLIC_ENABLE_STRIDE 0x80u
#define PLIC_CONTEXT_BASE 0x200000u
#define PLIC_CONTEXT_STRIDE 0x1000u
#define PLIC_CONTEXT_THRESHOLD 0x0u
#define PLIC_CONTEXT_CLAIM 0x4u

#define VIRTIO_MMIO_BASE 0x10001000u
#define VIRTIO_MMIO_SIZE 0x00008000u
#define VIRTIO_MMIO_STRIDE 0x00001000u
#define VIRTIO_MMIO_MAGIC_VALUE 0x000u
#define VIRTIO_MMIO_VERSION 0x004u
#define VIRTIO_MMIO_DEVICE_ID 0x008u
#define VIRTIO_MMIO_VENDOR_ID 0x00cu
#define VIRTIO_MMIO_STATUS 0x070u

static bool rv32emu_read_u32_le(const uint8_t *p, int len, uint32_t *out) {
  if (len == 1) {
    *out = p[0];
    return true;
  }
  if (len == 2) {
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    return true;
  }
  if (len == 4) {
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
    return true;
  }
  return false;
}

static bool rv32emu_write_u32_le(uint8_t *p, int len, uint32_t data) {
  if (len == 1) {
    p[0] = (uint8_t)data;
    return true;
  }
  if (len == 2) {
    p[0] = (uint8_t)data;
    p[1] = (uint8_t)(data >> 8);
    return true;
  }
  if (len == 4) {
    p[0] = (uint8_t)data;
    p[1] = (uint8_t)(data >> 8);
    p[2] = (uint8_t)(data >> 16);
    p[3] = (uint8_t)(data >> 24);
    return true;
  }
  return false;
}

static uint32_t rv32emu_plic_context_count(const rv32emu_machine_t *m) {
  if (m == NULL || m->hart_count > RV32EMU_MAX_HARTS) {
    return 0u;
  }
  return m->hart_count * 2u;
}

static void rv32emu_sync_timer_irq_for_hart(rv32emu_machine_t *m, uint32_t hartid) {
  rv32emu_cpu_t *cpu;
  bool expired;

  if (m == NULL || hartid >= m->hart_count) {
    return;
  }

  cpu = rv32emu_hart_cpu(m, hartid);
  if (cpu == NULL) {
    return;
  }

  expired = m->plat.mtime >= m->plat.clint_mtimecmp[hartid];
  if (m->opts.enable_sbi_shim) {
    if (expired) {
      cpu->csr[CSR_MIP] |= MIP_STIP;
    } else {
      cpu->csr[CSR_MIP] &= ~MIP_STIP;
    }
    cpu->csr[CSR_MIP] &= ~MIP_MTIP;
    return;
  }

  if (expired) {
    cpu->csr[CSR_MIP] |= MIP_MTIP;
  } else {
    cpu->csr[CSR_MIP] &= ~MIP_MTIP;
  }
}

static void rv32emu_update_plic_irq_lines(rv32emu_machine_t *m) {
  uint32_t hart;

  if (m == NULL) {
    return;
  }

  for (hart = 0u; hart < m->hart_count; hart++) {
    rv32emu_cpu_t *cpu = rv32emu_hart_cpu(m, hart);
    uint32_t m_context = hart * 2u;
    uint32_t s_context = m_context + 1u;
    uint32_t pending_m;
    uint32_t pending_s;

    if (cpu == NULL) {
      continue;
    }

    pending_m = m->plat.plic_pending & m->plat.plic_enable[m_context];
    pending_s = m->plat.plic_pending & m->plat.plic_enable[s_context];

    if (pending_m != 0u) {
      cpu->csr[CSR_MIP] |= MIP_MEIP;
    } else {
      cpu->csr[CSR_MIP] &= ~MIP_MEIP;
    }

    if (pending_s != 0u) {
      cpu->csr[CSR_MIP] |= MIP_SEIP;
    } else {
      cpu->csr[CSR_MIP] &= ~MIP_SEIP;
    }
  }
}

static bool rv32emu_uart_irq_should_assert(const rv32emu_machine_t *m) {
  return m->plat.uart_rx_count != 0u && (m->plat.uart_regs[UART_REG_IER] & UART_IER_RDI) != 0u;
}

static bool rv32emu_uart_tx_irq_should_assert(const rv32emu_machine_t *m) {
  return m->plat.uart_tx_irq_pending &&
         (m->plat.uart_regs[UART_REG_IER] & UART_IER_THRI) != 0u;
}

static void rv32emu_uart_sync_irq(rv32emu_machine_t *m) {
  uint32_t uart_bit = 1u << UART_PLIC_IRQ;

  if (rv32emu_uart_irq_should_assert(m) || rv32emu_uart_tx_irq_should_assert(m)) {
    m->plat.plic_pending |= uart_bit;
  } else {
    m->plat.plic_pending &= ~uart_bit;
  }
  rv32emu_update_plic_irq_lines(m);
}

static bool rv32emu_uart_pop_rx(rv32emu_machine_t *m, uint8_t *out) {
  if (m->plat.uart_rx_count == 0u) {
    return false;
  }

  *out = m->plat.uart_rx_fifo[m->plat.uart_rx_head];
  m->plat.uart_rx_head = (uint16_t)((m->plat.uart_rx_head + 1u) % RV32EMU_UART_RX_FIFO_SIZE);
  m->plat.uart_rx_count--;
  return true;
}

bool rv32emu_uart_push_rx(rv32emu_machine_t *m, uint8_t data) {
  if (m == NULL || m->plat.uart_rx_count >= RV32EMU_UART_RX_FIFO_SIZE) {
    return false;
  }

  m->plat.uart_rx_fifo[m->plat.uart_rx_tail] = data;
  m->plat.uart_rx_tail = (uint16_t)((m->plat.uart_rx_tail + 1u) % RV32EMU_UART_RX_FIFO_SIZE);
  m->plat.uart_rx_count++;
  rv32emu_uart_sync_irq(m);
  return true;
}

static uint32_t rv32emu_plic_find_claimable(uint32_t pending, uint32_t enabled) {
  uint32_t active = pending & enabled;
  uint32_t irq;

  for (irq = 1u; irq < 32u; irq++) {
    if ((active & (1u << irq)) != 0u) {
      return irq;
    }
  }

  return 0u;
}

static uint32_t rv32emu_plic_claim(rv32emu_machine_t *m, uint32_t context) {
  uint32_t enabled;
  uint32_t claim;

  if (m == NULL || context >= rv32emu_plic_context_count(m)) {
    return 0u;
  }

  if (m->plat.plic_claim[context] != 0u) {
    return m->plat.plic_claim[context];
  }

  enabled = m->plat.plic_enable[context];
  claim = rv32emu_plic_find_claimable(m->plat.plic_pending, enabled);
  if (claim != 0u) {
    m->plat.plic_claim[context] = claim;
    m->plat.plic_pending &= ~(1u << claim);
    rv32emu_update_plic_irq_lines(m);
  }

  return m->plat.plic_claim[context];
}

static bool rv32emu_handle_uart_read(rv32emu_machine_t *m, uint32_t paddr, int len,
                                     uint32_t *out) {
  uint32_t off = paddr - RV32EMU_UART_BASE;
  uint8_t idx;
  uint32_t value = 0;

  if (off >= RV32EMU_UART_SIZE) {
    return false;
  }

  if (len != 1 && len != 4) {
    return false;
  }

  idx = (uint8_t)(off & 0x7u);

  switch (idx) {
  case UART_REG_RBR: {
    uint8_t ch = 0;
    if (rv32emu_uart_pop_rx(m, &ch)) {
      value = ch;
      rv32emu_uart_sync_irq(m);
    } else {
      value = 0u;
    }
    break;
  }
  case UART_REG_IIR:
    if (rv32emu_uart_irq_should_assert(m)) {
      value = UART_IIR_RDI;
    } else if (rv32emu_uart_tx_irq_should_assert(m)) {
      value = UART_IIR_THRI;
    } else {
      value = UART_IIR_NO_INT;
    }
    break;
  case UART_REG_LSR:
    value = UART_LSR_THRE | UART_LSR_TEMT;
    if (m->plat.uart_rx_count != 0u) {
      value |= UART_LSR_DR;
    }
    break;
  case UART_REG_MSR:
    value = 0;
    break;
  default:
    value = m->plat.uart_regs[idx];
    break;
  }

  *out = value;
  return true;
}

static bool rv32emu_handle_uart_write(rv32emu_machine_t *m, uint32_t paddr, int len,
                                      uint32_t data) {
  uint32_t off = paddr - RV32EMU_UART_BASE;
  uint8_t idx;
  uint8_t ch = (uint8_t)data;

  if (off >= RV32EMU_UART_SIZE) {
    return false;
  }

  if (len != 1 && len != 4) {
    return false;
  }

  idx = (uint8_t)(off & 0x7u);

  switch (idx) {
  case UART_REG_THR:
    putchar((int)ch);
    fflush(stdout);
    if ((m->plat.uart_regs[UART_REG_IER] & UART_IER_THRI) != 0u) {
      m->plat.uart_tx_irq_pending = true;
      rv32emu_uart_sync_irq(m);
    }
    break;
  case UART_REG_IER:
    m->plat.uart_regs[UART_REG_IER] = ch;
    if ((ch & UART_IER_THRI) != 0u) {
      m->plat.uart_tx_irq_pending = true;
    } else {
      m->plat.uart_tx_irq_pending = false;
    }
    rv32emu_uart_sync_irq(m);
    break;
  case UART_REG_FCR:
    m->plat.uart_regs[UART_REG_FCR] = ch;
    if ((ch & 0x02u) != 0u) {
      m->plat.uart_rx_head = 0u;
      m->plat.uart_rx_tail = 0u;
      m->plat.uart_rx_count = 0u;
    }
    rv32emu_uart_sync_irq(m);
    break;
  default:
    m->plat.uart_regs[idx] = ch;
    break;
  }

  return true;
}

static bool rv32emu_handle_clint_read(rv32emu_machine_t *m, uint32_t paddr, int len,
                                      uint32_t *out) {
  uint32_t off = paddr - RV32EMU_CLINT_BASE;
  uint32_t rel;
  uint32_t hart;

  if (off >= RV32EMU_CLINT_SIZE) {
    return false;
  }

  if (len != 4) {
    return false;
  }

  if (off < CLINT_MSIP_BASE + m->hart_count * 4u) {
    if ((off & 0x3u) != 0u) {
      return false;
    }
    hart = (off - CLINT_MSIP_BASE) / 4u;
    *out = m->plat.clint_msip[hart];
    return true;
  }

  if (off >= CLINT_MTIMECMP_BASE && off < CLINT_MTIMECMP_BASE + m->hart_count * 8u) {
    rel = off - CLINT_MTIMECMP_BASE;
    if ((rel & 0x3u) != 0u) {
      return false;
    }
    hart = rel / 8u;
    if ((rel & 0x4u) == 0u) {
      *out = (uint32_t)(m->plat.clint_mtimecmp[hart] & 0xffffffffu);
    } else {
      *out = (uint32_t)(m->plat.clint_mtimecmp[hart] >> 32);
    }
    return true;
  }

  switch (off) {
  case CLINT_MTIME:
    *out = (uint32_t)(m->plat.mtime & 0xffffffffu);
    return true;
  case CLINT_MTIME + 4:
    *out = (uint32_t)(m->plat.mtime >> 32);
    return true;
  default:
    *out = 0;
    return true;
  }
}

static bool rv32emu_handle_clint_write(rv32emu_machine_t *m, uint32_t paddr, int len,
                                       uint32_t data) {
  uint32_t off = paddr - RV32EMU_CLINT_BASE;
  uint32_t rel;
  uint32_t hart;

  if (off >= RV32EMU_CLINT_SIZE) {
    return false;
  }

  if (len != 4) {
    return false;
  }

  if (off < CLINT_MSIP_BASE + m->hart_count * 4u) {
    rv32emu_cpu_t *cpu;

    if ((off & 0x3u) != 0u) {
      return false;
    }

    hart = (off - CLINT_MSIP_BASE) / 4u;
    m->plat.clint_msip[hart] = data & 1u;
    cpu = rv32emu_hart_cpu(m, hart);
    if (cpu != NULL) {
      /*
       * OpenSBI generic platform uses software interrupt to poke secondary
       * HARTs into warmboot. If a hart has not started yet, treat MSIP=1 as
       * its wakeup event.
       */
      if (m->plat.clint_msip[hart] != 0u && !cpu->running) {
        cpu->running = true;
      }
      if (m->plat.clint_msip[hart] != 0u) {
        cpu->csr[CSR_MIP] |= MIP_MSIP;
      } else {
        cpu->csr[CSR_MIP] &= ~MIP_MSIP;
      }
    }
    return true;
  }

  if (off >= CLINT_MTIMECMP_BASE && off < CLINT_MTIMECMP_BASE + m->hart_count * 8u) {
    rel = off - CLINT_MTIMECMP_BASE;
    if ((rel & 0x3u) != 0u) {
      return false;
    }

    hart = rel / 8u;
    if ((rel & 0x4u) == 0u) {
      m->plat.clint_mtimecmp[hart] =
          (m->plat.clint_mtimecmp[hart] & 0xffffffff00000000ull) | (uint64_t)data;
    } else {
      m->plat.clint_mtimecmp[hart] =
          (m->plat.clint_mtimecmp[hart] & 0x00000000ffffffffull) | ((uint64_t)data << 32);
    }
    rv32emu_sync_timer_irq_for_hart(m, hart);
    return true;
  }

  switch (off) {
  case CLINT_MTIME:
    m->plat.mtime = (m->plat.mtime & 0xffffffff00000000ull) | (uint64_t)data;
    break;
  case CLINT_MTIME + 4:
    m->plat.mtime = (m->plat.mtime & 0x00000000ffffffffull) | ((uint64_t)data << 32);
    break;
  default:
    return true;
  }

  for (hart = 0u; hart < m->hart_count; hart++) {
    rv32emu_cpu_t *cpu = rv32emu_hart_cpu(m, hart);
    if (cpu != NULL) {
      cpu->csr[CSR_TIME] = (uint32_t)m->plat.mtime;
    }
    rv32emu_sync_timer_irq_for_hart(m, hart);
  }
  return true;
}

static bool rv32emu_handle_plic_read(rv32emu_machine_t *m, uint32_t paddr, int len,
                                     uint32_t *out) {
  uint32_t off = paddr - RV32EMU_PLIC_BASE;
  uint32_t context_count;
  uint32_t rel;
  uint32_t context;
  uint32_t context_off;

  if (off >= RV32EMU_PLIC_SIZE) {
    return false;
  }

  if (len != 4) {
    return false;
  }

  context_count = rv32emu_plic_context_count(m);

  switch (off) {
  case PLIC_PENDING:
    *out = m->plat.plic_pending;
    return true;
  }

  if (off >= PLIC_ENABLE_BASE && off < PLIC_ENABLE_BASE + context_count * PLIC_ENABLE_STRIDE) {
    rel = off - PLIC_ENABLE_BASE;
    context = rel / PLIC_ENABLE_STRIDE;
    context_off = rel % PLIC_ENABLE_STRIDE;
    if (context_off == 0u) {
      *out = m->plat.plic_enable[context];
    } else {
      *out = 0u;
    }
    return true;
  }

  if (off >= PLIC_CONTEXT_BASE &&
      off < PLIC_CONTEXT_BASE + context_count * PLIC_CONTEXT_STRIDE) {
    rel = off - PLIC_CONTEXT_BASE;
    context = rel / PLIC_CONTEXT_STRIDE;
    context_off = rel % PLIC_CONTEXT_STRIDE;
    if (context_off == PLIC_CONTEXT_THRESHOLD) {
      *out = 0u;
      return true;
    }
    if (context_off == PLIC_CONTEXT_CLAIM) {
      *out = rv32emu_plic_claim(m, context);
      return true;
    }
  }

  *out = 0u;
  return true;
}

static bool rv32emu_handle_plic_write(rv32emu_machine_t *m, uint32_t paddr, int len,
                                      uint32_t data) {
  uint32_t off = paddr - RV32EMU_PLIC_BASE;
  uint32_t context_count;
  uint32_t rel;
  uint32_t context;
  uint32_t context_off;

  if (off >= RV32EMU_PLIC_SIZE) {
    return false;
  }

  if (len != 4) {
    return false;
  }

  context_count = rv32emu_plic_context_count(m);

  switch (off) {
  case PLIC_PENDING:
    m->plat.plic_pending = data;
    rv32emu_uart_sync_irq(m);
    return true;
  default:
    break;
  }

  if (off >= PLIC_ENABLE_BASE && off < PLIC_ENABLE_BASE + context_count * PLIC_ENABLE_STRIDE) {
    rel = off - PLIC_ENABLE_BASE;
    context = rel / PLIC_ENABLE_STRIDE;
    context_off = rel % PLIC_ENABLE_STRIDE;
    if (context_off == 0u) {
      m->plat.plic_enable[context] = data;
      rv32emu_update_plic_irq_lines(m);
    }
    return true;
  }

  if (off >= PLIC_CONTEXT_BASE &&
      off < PLIC_CONTEXT_BASE + context_count * PLIC_CONTEXT_STRIDE) {
    rel = off - PLIC_CONTEXT_BASE;
    context = rel / PLIC_CONTEXT_STRIDE;
    context_off = rel % PLIC_CONTEXT_STRIDE;
    if (context_off == PLIC_CONTEXT_CLAIM) {
      if (data == m->plat.plic_claim[context]) {
        m->plat.plic_claim[context] = 0u;
        rv32emu_uart_sync_irq(m);
      } else {
        rv32emu_update_plic_irq_lines(m);
      }
      return true;
    }
    if (context_off == PLIC_CONTEXT_THRESHOLD) {
      return true;
    }
  }

  return true;
}

void rv32emu_step_timer(rv32emu_machine_t *m) {
  uint32_t hart;

  if (m == NULL) {
    return;
  }

  m->plat.mtime += 1;
  for (hart = 0u; hart < m->hart_count; hart++) {
    rv32emu_cpu_t *cpu = rv32emu_hart_cpu(m, hart);
    if (cpu != NULL) {
      cpu->csr[CSR_TIME] = (uint32_t)m->plat.mtime;
    }
    rv32emu_sync_timer_irq_for_hart(m, hart);
  }
}

static bool rv32emu_handle_virtio_mmio_read(rv32emu_machine_t *m, uint32_t paddr, int len,
                                            uint32_t *out) {
  uint32_t off = paddr - VIRTIO_MMIO_BASE;
  uint32_t slot_off;

  (void)m;

  if (off >= VIRTIO_MMIO_SIZE || len != 4) {
    return false;
  }

  slot_off = off % VIRTIO_MMIO_STRIDE;
  switch (slot_off) {
  case VIRTIO_MMIO_MAGIC_VALUE:
    *out = 0x74726976u; /* "virt" */
    return true;
  case VIRTIO_MMIO_VERSION:
    *out = 2u;
    return true;
  case VIRTIO_MMIO_DEVICE_ID:
    *out = 0u; /* no device attached */
    return true;
  case VIRTIO_MMIO_VENDOR_ID:
    *out = 0x554d4551u; /* QEMU */
    return true;
  case VIRTIO_MMIO_STATUS:
    *out = 0u;
    return true;
  default:
    *out = 0u;
    return true;
  }
}

static bool rv32emu_handle_virtio_mmio_write(rv32emu_machine_t *m, uint32_t paddr, int len,
                                             uint32_t data) {
  uint32_t off = paddr - VIRTIO_MMIO_BASE;

  (void)m;
  (void)data;

  if (off >= VIRTIO_MMIO_SIZE || len != 4) {
    return false;
  }

  return true;
}

uint8_t *rv32emu_dram_ptr(rv32emu_machine_t *m, uint32_t paddr, size_t len) {
  uint32_t off;

  if (m == NULL || m->plat.dram == NULL || paddr < m->plat.dram_base) {
    return NULL;
  }

  off = paddr - m->plat.dram_base;
  if ((uint64_t)off + (uint64_t)len > (uint64_t)m->plat.dram_size) {
    return NULL;
  }

  return m->plat.dram + off;
}

bool rv32emu_phys_read(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out) {
  uint8_t *ptr;

  if (m == NULL || out == NULL) {
    return false;
  }

  ptr = rv32emu_dram_ptr(m, paddr, (size_t)len);
  if (ptr != NULL) {
    return rv32emu_read_u32_le(ptr, len, out);
  }

  if (paddr >= RV32EMU_UART_BASE && paddr < RV32EMU_UART_BASE + RV32EMU_UART_SIZE) {
    return rv32emu_handle_uart_read(m, paddr, len, out);
  }

  if (paddr >= RV32EMU_CLINT_BASE && paddr < RV32EMU_CLINT_BASE + RV32EMU_CLINT_SIZE) {
    return rv32emu_handle_clint_read(m, paddr, len, out);
  }

  if (paddr >= RV32EMU_PLIC_BASE && paddr < RV32EMU_PLIC_BASE + RV32EMU_PLIC_SIZE) {
    return rv32emu_handle_plic_read(m, paddr, len, out);
  }

  if (paddr >= VIRTIO_MMIO_BASE && paddr < VIRTIO_MMIO_BASE + VIRTIO_MMIO_SIZE) {
    return rv32emu_handle_virtio_mmio_read(m, paddr, len, out);
  }

  return false;
}

bool rv32emu_phys_write(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data) {
  uint8_t *ptr;

  if (m == NULL) {
    return false;
  }

  ptr = rv32emu_dram_ptr(m, paddr, (size_t)len);
  if (ptr != NULL) {
    return rv32emu_write_u32_le(ptr, len, data);
  }

  if (paddr >= RV32EMU_UART_BASE && paddr < RV32EMU_UART_BASE + RV32EMU_UART_SIZE) {
    return rv32emu_handle_uart_write(m, paddr, len, data);
  }

  if (paddr >= RV32EMU_CLINT_BASE && paddr < RV32EMU_CLINT_BASE + RV32EMU_CLINT_SIZE) {
    return rv32emu_handle_clint_write(m, paddr, len, data);
  }

  if (paddr >= RV32EMU_PLIC_BASE && paddr < RV32EMU_PLIC_BASE + RV32EMU_PLIC_SIZE) {
    return rv32emu_handle_plic_write(m, paddr, len, data);
  }

  if (paddr >= VIRTIO_MMIO_BASE && paddr < VIRTIO_MMIO_BASE + VIRTIO_MMIO_SIZE) {
    return rv32emu_handle_virtio_mmio_write(m, paddr, len, data);
  }

  return false;
}
