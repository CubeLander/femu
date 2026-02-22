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

int main(void) {
  test_base32();
  test_rvc_basic();
  puts("[OK] rv32emu run test passed");
  return 0;
}
