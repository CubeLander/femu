#include "rv32emu.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t value = 0;
  uint32_t paddr;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  paddr = RV32EMU_DRAM_BASE + 0x100u;
  assert(rv32emu_phys_write(&m, paddr, 4, 0x11223344u));
  assert(rv32emu_phys_read(&m, paddr, 4, &value));
  assert(value == 0x11223344u);

  assert(rv32emu_virt_write(&m, paddr, 2, RV32EMU_ACC_STORE, 0xa55au));
  assert(rv32emu_virt_read(&m, paddr, 2, RV32EMU_ACC_LOAD, &value));
  assert(value == 0xa55au);

  assert(rv32emu_phys_read(&m, RV32EMU_UART_BASE + 5u, 1, &value));
  assert((value & 0x60u) == 0x60u);
  assert((value & 0x01u) == 0u);

  assert(rv32emu_uart_push_rx(&m, 'A'));
  assert(rv32emu_phys_read(&m, RV32EMU_UART_BASE + 5u, 1, &value));
  assert((value & 0x01u) != 0u);
  assert(rv32emu_phys_read(&m, RV32EMU_UART_BASE + 0u, 1, &value));
  assert((value & 0xffu) == 'A');
  assert(rv32emu_phys_read(&m, RV32EMU_UART_BASE + 5u, 1, &value));
  assert((value & 0x01u) == 0u);

  assert(rv32emu_phys_write(&m, RV32EMU_PLIC_BASE + 0x2080u, 4, (1u << 10)));
  assert(rv32emu_phys_write(&m, RV32EMU_UART_BASE + 1u, 1, 2u));
  assert((rv32emu_csr_read(&m, CSR_MIP) & MIP_SEIP) != 0u);
  assert(rv32emu_phys_read(&m, RV32EMU_UART_BASE + 2u, 1, &value));
  assert((value & 0x0fu) == 0x02u);
  assert(rv32emu_phys_read(&m, RV32EMU_PLIC_BASE + 0x201004u, 4, &value));
  assert(value == 10u);
  assert(rv32emu_phys_write(&m, RV32EMU_PLIC_BASE + 0x201004u, 4, 10u));

  assert(rv32emu_phys_write(&m, RV32EMU_UART_BASE + 1u, 1, 1u));
  assert(rv32emu_uart_push_rx(&m, 'B'));
  assert((rv32emu_csr_read(&m, CSR_MIP) & MIP_SEIP) != 0u);
  assert(rv32emu_phys_read(&m, RV32EMU_PLIC_BASE + 0x201004u, 4, &value));
  assert(value == 10u);
  assert(rv32emu_phys_write(&m, RV32EMU_PLIC_BASE + 0x201004u, 4, 10u));
  assert(rv32emu_phys_read(&m, RV32EMU_UART_BASE + 0u, 1, &value));
  assert((value & 0xffu) == 'B');

  assert(rv32emu_phys_write(&m, RV32EMU_CLINT_BASE + 0x4000u, 4, 3u));
  assert(rv32emu_phys_write(&m, RV32EMU_CLINT_BASE + 0x4004u, 4, 0u));

  rv32emu_step_timer(&m);
  rv32emu_step_timer(&m);
  assert((rv32emu_csr_read(&m, CSR_MIP) & MIP_MTIP) == 0);

  rv32emu_step_timer(&m);
  assert((rv32emu_csr_read(&m, CSR_MIP) & MIP_MTIP) != 0);

  assert(rv32emu_phys_write(&m, RV32EMU_CLINT_BASE + 0x0000u, 4, 1u));
  assert((rv32emu_csr_read(&m, CSR_MIP) & MIP_MSIP) != 0);
  assert(rv32emu_phys_write(&m, RV32EMU_CLINT_BASE + 0x0000u, 4, 0u));
  assert((rv32emu_csr_read(&m, CSR_MIP) & MIP_MSIP) == 0);

  m.plat.plic_claim = 9u;
  assert(rv32emu_phys_read(&m, RV32EMU_PLIC_BASE + 0x200004u, 4, &value));
  assert(value == 9u);
  assert(rv32emu_phys_write(&m, RV32EMU_PLIC_BASE + 0x200004u, 4, 9u));
  assert(m.plat.plic_claim == 0u);

  rv32emu_csr_write(&m, CSR_SATP, 0x80000000u);
  assert(rv32emu_translate(&m, RV32EMU_DRAM_BASE + 0x1234u, RV32EMU_ACC_FETCH, &value));
  assert(value == RV32EMU_DRAM_BASE + 0x1234u);

  rv32emu_platform_destroy(&m);
  puts("[OK] rv32emu platform test passed");
  return 0;
}
