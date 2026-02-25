#include "rv32emu.h"

#define VIRTIO_MMIO_BASE 0x10001000u
#define VIRTIO_MMIO_SIZE 0x00008000u
#define VIRTIO_MMIO_STRIDE 0x00001000u
#define VIRTIO_MMIO_MAGIC_VALUE 0x000u
#define VIRTIO_MMIO_VERSION 0x004u
#define VIRTIO_MMIO_DEVICE_ID 0x008u
#define VIRTIO_MMIO_VENDOR_ID 0x00cu
#define VIRTIO_MMIO_STATUS 0x070u

bool rv32emu_mmio_uart_read_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out);
bool rv32emu_mmio_uart_write_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data);
bool rv32emu_mmio_clint_read_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out);
bool rv32emu_mmio_clint_write_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data);
bool rv32emu_mmio_plic_read_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out);
bool rv32emu_mmio_plic_write_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data);

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
    *out = 0x74726976u;
    return true;
  case VIRTIO_MMIO_VERSION:
    *out = 2u;
    return true;
  case VIRTIO_MMIO_DEVICE_ID:
    *out = 0u;
    return true;
  case VIRTIO_MMIO_VENDOR_ID:
    *out = 0x554d4551u;
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

bool rv32emu_mmio_read_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out) {
  bool ok = false;

  if (m == NULL || out == NULL) {
    return false;
  }

  if (paddr >= RV32EMU_UART_BASE && paddr < RV32EMU_UART_BASE + RV32EMU_UART_SIZE) {
    ok = rv32emu_mmio_uart_read_locked(m, paddr, len, out);
    rv32emu_trace_mmio(m, false, paddr, len, ok ? *out : 0u, ok);
    return ok;
  }

  if (paddr >= RV32EMU_CLINT_BASE && paddr < RV32EMU_CLINT_BASE + RV32EMU_CLINT_SIZE) {
    ok = rv32emu_mmio_clint_read_locked(m, paddr, len, out);
    rv32emu_trace_mmio(m, false, paddr, len, ok ? *out : 0u, ok);
    return ok;
  }

  if (paddr >= RV32EMU_PLIC_BASE && paddr < RV32EMU_PLIC_BASE + RV32EMU_PLIC_SIZE) {
    ok = rv32emu_mmio_plic_read_locked(m, paddr, len, out);
    rv32emu_trace_mmio(m, false, paddr, len, ok ? *out : 0u, ok);
    return ok;
  }

  if (paddr >= VIRTIO_MMIO_BASE && paddr < VIRTIO_MMIO_BASE + VIRTIO_MMIO_SIZE) {
    ok = rv32emu_handle_virtio_mmio_read(m, paddr, len, out);
    rv32emu_trace_mmio(m, false, paddr, len, ok ? *out : 0u, ok);
    return ok;
  }

  rv32emu_trace_mmio(m, false, paddr, len, 0u, false);
  return false;
}

bool rv32emu_mmio_write_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data) {
  bool ok = false;

  if (m == NULL) {
    return false;
  }

  if (paddr >= RV32EMU_UART_BASE && paddr < RV32EMU_UART_BASE + RV32EMU_UART_SIZE) {
    ok = rv32emu_mmio_uart_write_locked(m, paddr, len, data);
    rv32emu_trace_mmio(m, true, paddr, len, data, ok);
    return ok;
  }

  if (paddr >= RV32EMU_CLINT_BASE && paddr < RV32EMU_CLINT_BASE + RV32EMU_CLINT_SIZE) {
    ok = rv32emu_mmio_clint_write_locked(m, paddr, len, data);
    rv32emu_trace_mmio(m, true, paddr, len, data, ok);
    return ok;
  }

  if (paddr >= RV32EMU_PLIC_BASE && paddr < RV32EMU_PLIC_BASE + RV32EMU_PLIC_SIZE) {
    ok = rv32emu_mmio_plic_write_locked(m, paddr, len, data);
    rv32emu_trace_mmio(m, true, paddr, len, data, ok);
    return ok;
  }

  if (paddr >= VIRTIO_MMIO_BASE && paddr < VIRTIO_MMIO_BASE + VIRTIO_MMIO_SIZE) {
    ok = rv32emu_handle_virtio_mmio_write(m, paddr, len, data);
    rv32emu_trace_mmio(m, true, paddr, len, data, ok);
    return ok;
  }

  rv32emu_trace_mmio(m, true, paddr, len, data, false);
  return false;
}
