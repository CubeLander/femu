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

static void test_multihart_lr_sc_invalidation(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc0;
  uint32_t pc1;
  uint32_t shared_addr;
  uint32_t sc_pc;
  uint32_t value;
  uint32_t prog1[] = {
      0x0030a023u, /* sw x3, 0(x1) */
      0x00100073u, /* ebreak */
  };
  int steps;

  rv32emu_default_options(&opts);
  opts.hart_count = 2u;
  assert(rv32emu_platform_init(&m, &opts));

  pc0 = RV32EMU_DRAM_BASE + 0x300u;
  pc1 = RV32EMU_DRAM_BASE + 0x700u;
  shared_addr = RV32EMU_DRAM_BASE + 0x900u;
  /*
   * rv32emu uses a per-hart instruction slice scheduler (64 instructions).
   * Put sc.w after 64 nops so hart1 can run its store in between hart0 lr.w
   * and hart0 sc.w, making cross-hart reservation invalidation observable.
   */
  sc_pc = pc0 + 65u * 4u;

  assert(rv32emu_phys_write(&m, pc0, 4, 0x1000a2afu)); /* lr.w x5, (x1) */
  for (uint32_t i = 1u; i <= 64u; i++) {
    assert(rv32emu_phys_write(&m, pc0 + i * 4u, 4, 0x00000013u)); /* nop */
  }
  assert(rv32emu_phys_write(&m, sc_pc, 4, 0x1820a32fu)); /* sc.w x6, x2, (x1) */
  assert(rv32emu_phys_write(&m, sc_pc + 4u, 4, 0x00100073u)); /* ebreak */

  for (uint32_t i = 0; i < (uint32_t)(sizeof(prog1) / sizeof(prog1[0])); i++) {
    assert(rv32emu_phys_write(&m, pc1 + i * 4u, 4, prog1[i]));
  }
  assert(rv32emu_phys_write(&m, shared_addr, 4, 0u));

  m.harts[0].pc = pc0;
  m.harts[0].running = true;
  m.harts[0].priv = RV32EMU_PRIV_M;
  m.harts[0].x[1] = shared_addr;
  m.harts[0].x[2] = 0xaaaa5555u;

  m.harts[1].pc = pc1;
  m.harts[1].running = true;
  m.harts[1].priv = RV32EMU_PRIV_M;
  m.harts[1].csr[CSR_MHARTID] = 1u;
  m.harts[1].x[1] = shared_addr;
  m.harts[1].x[3] = 0x12345678u;

  steps = rv32emu_run(&m, 256);
  assert(steps >= 67);
  assert(m.harts[0].x[6] == 1u); /* sc.w must fail after hart1 store */
  assert(rv32emu_phys_read(&m, shared_addr, 4, &value));
  assert(value == 0x12345678u);

  rv32emu_platform_destroy(&m);
}

int main(void) {
  test_base32();
  test_rvc_basic();
  test_multihart_round_robin();
  test_multihart_lr_sc_invalidation();
  puts("[OK] rv32emu run test passed");
  return 0;
}
