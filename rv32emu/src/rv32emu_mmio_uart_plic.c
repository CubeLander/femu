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

#define PLIC_PENDING 0x1000u
#define PLIC_ENABLE_BASE 0x2000u
#define PLIC_ENABLE_STRIDE 0x80u
#define PLIC_CONTEXT_BASE 0x200000u
#define PLIC_CONTEXT_STRIDE 0x1000u
#define PLIC_CONTEXT_THRESHOLD 0x0u
#define PLIC_CONTEXT_CLAIM 0x4u

static uint32_t rv32emu_plic_context_count(const rv32emu_machine_t *m) {
  if (m == NULL || m->hart_count > RV32EMU_MAX_HARTS) {
    return 0u;
  }
  return m->hart_count * 2u;
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
      rv32emu_cpu_mip_set_bits(cpu, MIP_MEIP);
    } else {
      rv32emu_cpu_mip_clear_bits(cpu, MIP_MEIP);
    }

    if (pending_s != 0u) {
      rv32emu_cpu_mip_set_bits(cpu, MIP_SEIP);
    } else {
      rv32emu_cpu_mip_clear_bits(cpu, MIP_SEIP);
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

bool rv32emu_mmio_uart_read_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out) {
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

bool rv32emu_mmio_uart_write_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data) {
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

bool rv32emu_mmio_plic_read_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out) {
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

bool rv32emu_mmio_plic_write_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data) {
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
