#include "rv32emu.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint16_t enc_c_addi(uint32_t rd, int32_t imm6) {
  uint32_t u = (uint32_t)imm6 & 0x3fu;
  return (uint16_t)(((u >> 5) << 12) | ((rd & 0x1fu) << 7) | ((u & 0x1fu) << 2) | 0x1u);
}

static uint16_t enc_c_mv(uint32_t rd, uint32_t rs2) {
  return (uint16_t)((4u << 13) | ((rd & 0x1fu) << 7) | ((rs2 & 0x1fu) << 2) | 0x2u);
}

static uint16_t enc_c_add(uint32_t rd, uint32_t rs2) {
  return (uint16_t)((4u << 13) | (1u << 12) | ((rd & 0x1fu) << 7) |
                    ((rs2 & 0x1fu) << 2) | 0x2u);
}

static void test_base32(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc;
  uint32_t prog[] = {
      0x00500093u, /* addi x1, x0, 5 */
      0x00700113u, /* addi x2, x0, 7 */
      0x002081b3u, /* add  x3, x1, x2 */
      0x00100073u, /* ebreak */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  pc = RV32EMU_DRAM_BASE;
  m.cpu.pc = pc;

  for (uint32_t i = 0; i < (uint32_t)(sizeof(prog) / sizeof(prog[0])); i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  steps = rv32emu_run(&m, 32);
  assert(steps == 3);
  assert(m.cpu.x[1] == 5u);
  assert(m.cpu.x[2] == 7u);
  assert(m.cpu.x[3] == 12u);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_rvc_basic(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc;
  uint16_t prog[] = {
      enc_c_addi(1, 5), /* c.addi x1, 5 */
      enc_c_addi(2, 7), /* c.addi x2, 7 */
      enc_c_mv(3, 1),   /* c.mv   x3, x1 */
      enc_c_add(3, 2),  /* c.add  x3, x2 */
      0x9002u,          /* c.ebreak */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  pc = RV32EMU_DRAM_BASE + 0x100u;
  m.cpu.pc = pc;

  for (uint32_t i = 0; i < (uint32_t)(sizeof(prog) / sizeof(prog[0])); i++) {
    assert(rv32emu_phys_write(&m, pc + i * 2u, 2, prog[i]));
  }

  steps = rv32emu_run(&m, 32);
  assert(steps == 4);
  assert(m.cpu.x[1] == 5u);
  assert(m.cpu.x[2] == 7u);
  assert(m.cpu.x[3] == 12u);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_multihart_round_robin(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc;
  uint32_t prog[] = {
      0x00128293u, /* addi x5, x5, 1 */
      0x00100073u, /* ebreak */
  };
  int steps;

  rv32emu_default_options(&opts);
  opts.hart_count = 2u;
  assert(rv32emu_platform_init(&m, &opts));
  assert(m.hart_count == 2u);

  pc = RV32EMU_DRAM_BASE + 0x200u;
  for (uint32_t i = 0; i < (uint32_t)(sizeof(prog) / sizeof(prog[0])); i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  m.harts[0].pc = pc;
  m.harts[0].running = true;

  m.harts[1].pc = pc;
  m.harts[1].running = true;
  m.harts[1].priv = RV32EMU_PRIV_M;
  m.harts[1].csr[CSR_MHARTID] = 1u;

  steps = rv32emu_run(&m, 64);
  assert(steps == 2);

  assert(m.harts[0].x[5] == 1u);
  assert(m.harts[1].x[5] == 1u);
  assert(m.harts[0].running == false);
  assert(m.harts[1].running == false);
  assert(m.harts[0].csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);
  assert(m.harts[1].csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);
  assert(m.harts[0].csr[CSR_MHARTID] == 0u);
  assert(m.harts[1].csr[CSR_MHARTID] == 1u);

  rv32emu_platform_destroy(&m);
}

int main(void) {
  test_base32();
  test_rvc_basic();
  test_multihart_round_robin();
  puts("[OK] rv32emu run test passed");
  return 0;
}
