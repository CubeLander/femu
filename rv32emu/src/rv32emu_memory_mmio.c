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

#define CLINT_MSIP 0x0000u
#define CLINT_MTIMECMP 0x4000u
#define CLINT_MTIME 0xbff8u

#define PLIC_PENDING 0x1000u
#define PLIC_ENABLE0 0x2000u
#define PLIC_ENABLE1 0x2080u
#define PLIC_CTX0_CLAIM 0x200004u
#define PLIC_CTX1_CLAIM 0x201004u

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

static void rv32emu_update_plic_irq_lines(rv32emu_machine_t *m) {
  uint32_t pending_m = m->plat.plic_pending & m->plat.plic_enable0;
  uint32_t pending_s = m->plat.plic_pending & m->plat.plic_enable1;

  if (pending_m != 0u) {
    m->cpu.csr[CSR_MIP] |= MIP_MEIP;
  } else {
    m->cpu.csr[CSR_MIP] &= ~MIP_MEIP;
  }

  if (pending_s != 0u) {
    m->cpu.csr[CSR_MIP] |= MIP_SEIP;
  } else {
    m->cpu.csr[CSR_MIP] &= ~MIP_SEIP;
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

static uint32_t rv32emu_plic_claim(rv32emu_machine_t *m, bool s_context) {
  uint32_t enabled;
  uint32_t claim;

  if (m->plat.plic_claim != 0u) {
    return m->plat.plic_claim;
  }

  enabled = s_context ? m->plat.plic_enable1 : m->plat.plic_enable0;
  claim = rv32emu_plic_find_claimable(m->plat.plic_pending, enabled);
  if (claim != 0u) {
    m->plat.plic_claim = claim;
    m->plat.plic_pending &= ~(1u << claim);
    rv32emu_update_plic_irq_lines(m);
  }

  return m->plat.plic_claim;
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

  if (off >= RV32EMU_CLINT_SIZE) {
    return false;
  }

  if (len != 4) {
    return false;
  }

  switch (off) {
  case CLINT_MSIP:
    *out = m->plat.clint_msip;
    return true;
  case CLINT_MTIMECMP:
    *out = (uint32_t)(m->plat.mtimecmp & 0xffffffffu);
    return true;
  case CLINT_MTIMECMP + 4:
    *out = (uint32_t)(m->plat.mtimecmp >> 32);
    return true;
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

  if (off >= RV32EMU_CLINT_SIZE) {
    return false;
  }

  if (len != 4) {
    return false;
  }

  switch (off) {
  case CLINT_MSIP:
    m->plat.clint_msip = data & 1u;
    if (m->plat.clint_msip != 0) {
      m->cpu.csr[CSR_MIP] |= MIP_MSIP;
    } else {
      m->cpu.csr[CSR_MIP] &= ~MIP_MSIP;
    }
    return true;
  case CLINT_MTIMECMP:
    m->plat.mtimecmp = (m->plat.mtimecmp & 0xffffffff00000000ull) | (uint64_t)data;
    return true;
  case CLINT_MTIMECMP + 4:
    m->plat.mtimecmp = (m->plat.mtimecmp & 0x00000000ffffffffull) |
                       ((uint64_t)data << 32);
    return true;
  case CLINT_MTIME:
    m->plat.mtime = (m->plat.mtime & 0xffffffff00000000ull) | (uint64_t)data;
    m->cpu.csr[CSR_TIME] = (uint32_t)m->plat.mtime;
    return true;
  case CLINT_MTIME + 4:
    m->plat.mtime = (m->plat.mtime & 0x00000000ffffffffull) | ((uint64_t)data << 32);
    m->cpu.csr[CSR_TIME] = (uint32_t)m->plat.mtime;
    return true;
  default:
    return true;
  }
}

static bool rv32emu_handle_plic_read(rv32emu_machine_t *m, uint32_t paddr, int len,
                                     uint32_t *out) {
  uint32_t off = paddr - RV32EMU_PLIC_BASE;

  if (off >= RV32EMU_PLIC_SIZE) {
    return false;
  }

  if (len != 4) {
    return false;
  }

  switch (off) {
  case PLIC_PENDING:
    *out = m->plat.plic_pending;
    return true;
  case PLIC_ENABLE0:
    *out = m->plat.plic_enable0;
    return true;
  case PLIC_ENABLE1:
    *out = m->plat.plic_enable1;
    return true;
  case PLIC_CTX0_CLAIM:
    *out = rv32emu_plic_claim(m, false);
    return true;
  case PLIC_CTX1_CLAIM:
    *out = rv32emu_plic_claim(m, true);
    return true;
  default:
    *out = 0;
    return true;
  }
}

static bool rv32emu_handle_plic_write(rv32emu_machine_t *m, uint32_t paddr, int len,
                                      uint32_t data) {
  uint32_t off = paddr - RV32EMU_PLIC_BASE;

  if (off >= RV32EMU_PLIC_SIZE) {
    return false;
  }

  if (len != 4) {
    return false;
  }

  switch (off) {
  case PLIC_PENDING:
    m->plat.plic_pending = data;
    rv32emu_uart_sync_irq(m);
    return true;
  case PLIC_ENABLE0:
    m->plat.plic_enable0 = data;
    rv32emu_update_plic_irq_lines(m);
    return true;
  case PLIC_ENABLE1:
    m->plat.plic_enable1 = data;
    rv32emu_update_plic_irq_lines(m);
    return true;
  case PLIC_CTX0_CLAIM:
  case PLIC_CTX1_CLAIM:
    if (data == m->plat.plic_claim) {
      m->plat.plic_claim = 0;
      rv32emu_uart_sync_irq(m);
    } else {
      rv32emu_update_plic_irq_lines(m);
    }
    return true;
  default:
    return true;
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

void rv32emu_step_timer(rv32emu_machine_t *m) {
  if (m == NULL) {
    return;
  }

  m->plat.mtime += 1;
  m->cpu.csr[CSR_TIME] = (uint32_t)m->plat.mtime;

  if (m->plat.mtime >= m->plat.mtimecmp) {
    if (m->opts.enable_sbi_shim) {
      m->cpu.csr[CSR_MIP] |= MIP_STIP;
      m->cpu.csr[CSR_MIP] &= ~MIP_MTIP;
    } else {
      m->cpu.csr[CSR_MIP] |= MIP_MTIP;
    }
  } else {
    m->cpu.csr[CSR_MIP] &= ~MIP_MTIP;
    if (m->opts.enable_sbi_shim) {
      m->cpu.csr[CSR_MIP] &= ~MIP_STIP;
    }
  }
}
