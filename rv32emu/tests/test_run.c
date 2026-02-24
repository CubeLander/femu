#include "rv32emu.h"
#include "rv32emu_tb.h"

#include <assert.h>
#include <stdlib.h>
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

static uint32_t enc_i(uint32_t opcode, uint32_t rd, uint32_t funct3, uint32_t rs1, int32_t imm12) {
  uint32_t imm = (uint32_t)imm12 & 0xfffu;
  return (imm << 20) | ((rs1 & 0x1fu) << 15) | ((funct3 & 0x7u) << 12) |
         ((rd & 0x1fu) << 7) | (opcode & 0x7fu);
}

static uint32_t enc_i_shift(uint32_t rd, uint32_t funct3, uint32_t rs1, uint32_t shamt,
                            uint32_t funct7) {
  uint32_t imm = ((funct7 & 0x7fu) << 5) | (shamt & 0x1fu);
  return enc_i(0x13u, rd, funct3, rs1, (int32_t)imm);
}

static uint32_t enc_r(uint32_t opcode, uint32_t rd, uint32_t funct3, uint32_t rs1, uint32_t rs2,
                      uint32_t funct7) {
  return ((funct7 & 0x7fu) << 25) | ((rs2 & 0x1fu) << 20) | ((rs1 & 0x1fu) << 15) |
         ((funct3 & 0x7u) << 12) | ((rd & 0x1fu) << 7) | (opcode & 0x7fu);
}

static uint32_t enc_s(uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2, int32_t imm12) {
  uint32_t imm = (uint32_t)imm12 & 0xfffu;
  return ((imm >> 5) << 25) | ((rs2 & 0x1fu) << 20) | ((rs1 & 0x1fu) << 15) |
         ((funct3 & 0x7u) << 12) | ((imm & 0x1fu) << 7) | (opcode & 0x7fu);
}

static uint32_t enc_u(uint32_t opcode, uint32_t rd, uint32_t imm20) {
  return ((imm20 & 0xfffffu) << 12) | ((rd & 0x1fu) << 7) | (opcode & 0x7fu);
}

static uint32_t enc_b(uint32_t opcode, uint32_t funct3, uint32_t rs1, uint32_t rs2, int32_t imm13) {
  uint32_t imm = (uint32_t)imm13 & 0x1fffu;
  return (((imm >> 12) & 0x1u) << 31) | (((imm >> 5) & 0x3fu) << 25) |
         ((rs2 & 0x1fu) << 20) | ((rs1 & 0x1fu) << 15) | ((funct3 & 0x7u) << 12) |
         (((imm >> 1) & 0xfu) << 8) | (((imm >> 11) & 0x1u) << 7) | (opcode & 0x7fu);
}

static uint32_t enc_csr(uint32_t funct3, uint32_t rd, uint32_t rs1_or_zimm, uint32_t csr) {
  return ((csr & 0xfffu) << 20) | ((rs1_or_zimm & 0x1fu) << 15) |
         ((funct3 & 0x7u) << 12) | ((rd & 0x1fu) << 7) | 0x73u;
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

static void test_jit_int_alu(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc;
  uint32_t prog[64];
  uint32_t n = 0u;
  int steps;

  setenv("RV32EMU_EXPERIMENTAL_JIT", "1", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);
  unsetenv("RV32EMU_EXPERIMENTAL_TB");

  prog[n++] = enc_i(0x13u, 1u, 0x0u, 0u, 5);             /* addi  x1,  x0, 5 */
  prog[n++] = enc_i(0x13u, 2u, 0x0u, 0u, -3);            /* addi  x2,  x0, -3 */
  prog[n++] = enc_i_shift(3u, 0x1u, 1u, 2u, 0x00u);      /* slli  x3,  x1, 2 */
  prog[n++] = enc_i(0x13u, 4u, 0x2u, 2u, 0);             /* slti  x4,  x2, 0 */
  prog[n++] = enc_i(0x13u, 5u, 0x3u, 2u, 1);             /* sltiu x5,  x2, 1 */
  prog[n++] = enc_i(0x13u, 6u, 0x4u, 1u, 0x3c);          /* xori  x6,  x1, 0x3c */
  prog[n++] = enc_i(0x13u, 7u, 0x6u, 1u, 0x30);          /* ori   x7,  x1, 0x30 */
  prog[n++] = enc_i(0x13u, 8u, 0x7u, 6u, 0x0f);          /* andi  x8,  x6, 0x0f */
  prog[n++] = enc_i_shift(9u, 0x5u, 6u, 1u, 0x00u);      /* srli  x9,  x6, 1 */
  prog[n++] = enc_i_shift(10u, 0x5u, 2u, 1u, 0x20u);     /* srai  x10, x2, 1 */
  prog[n++] = enc_r(0x33u, 11u, 0x0u, 1u, 6u, 0x00u);    /* add   x11, x1, x6 */
  prog[n++] = enc_r(0x33u, 12u, 0x0u, 11u, 1u, 0x20u);   /* sub   x12, x11, x1 */
  prog[n++] = enc_r(0x33u, 13u, 0x1u, 1u, 1u, 0x00u);    /* sll   x13, x1, x1 */
  prog[n++] = enc_r(0x33u, 14u, 0x2u, 2u, 1u, 0x00u);    /* slt   x14, x2, x1 */
  prog[n++] = enc_r(0x33u, 15u, 0x3u, 2u, 1u, 0x00u);    /* sltu  x15, x2, x1 */
  prog[n++] = enc_r(0x33u, 16u, 0x4u, 1u, 6u, 0x00u);    /* xor   x16, x1, x6 */
  prog[n++] = enc_r(0x33u, 17u, 0x5u, 16u, 1u, 0x00u);   /* srl   x17, x16, x1 */
  prog[n++] = enc_r(0x33u, 18u, 0x5u, 2u, 1u, 0x20u);    /* sra   x18, x2, x1 */
  prog[n++] = enc_r(0x33u, 19u, 0x6u, 1u, 6u, 0x00u);    /* or    x19, x1, x6 */
  prog[n++] = enc_r(0x33u, 20u, 0x7u, 1u, 6u, 0x00u);    /* and   x20, x1, x6 */
  prog[n++] = enc_i(0x13u, 0u, 0x0u, 1u, 123);           /* addi  x0, x1, 123 */
  prog[n++] = 0x00100073u;                               /* ebreak */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  pc = RV32EMU_DRAM_BASE + 0x500u;
  m.cpu.pc = pc;
  for (uint32_t i = 0u; i < n; i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  steps = rv32emu_run(&m, 128);
  assert(steps == (int)(n - 1u));
  assert(m.cpu.x[0] == 0u);
  assert(m.cpu.x[1] == 5u);
  assert(m.cpu.x[2] == 0xfffffffdu);
  assert(m.cpu.x[3] == 20u);
  assert(m.cpu.x[4] == 1u);
  assert(m.cpu.x[5] == 0u);
  assert(m.cpu.x[6] == 0x39u);
  assert(m.cpu.x[7] == 0x35u);
  assert(m.cpu.x[8] == 9u);
  assert(m.cpu.x[9] == 0x1cu);
  assert(m.cpu.x[10] == 0xfffffffeu);
  assert(m.cpu.x[11] == 62u);
  assert(m.cpu.x[12] == 57u);
  assert(m.cpu.x[13] == 160u);
  assert(m.cpu.x[14] == 1u);
  assert(m.cpu.x[15] == 0u);
  assert(m.cpu.x[16] == 60u);
  assert(m.cpu.x[17] == 1u);
  assert(m.cpu.x[18] == 0xffffffffu);
  assert(m.cpu.x[19] == 61u);
  assert(m.cpu.x[20] == 1u);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT");
}

static void test_jit_budget_respected(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc;
  uint32_t prog[40];
  uint32_t n = 0u;
  int steps;

  setenv("RV32EMU_EXPERIMENTAL_JIT", "1", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);
  unsetenv("RV32EMU_EXPERIMENTAL_TB");

  for (uint32_t i = 0u; i < 32u; i++) {
    prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 1); /* addi x1, x1, 1 */
  }
  prog[n++] = 0x00100073u; /* ebreak */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  pc = RV32EMU_DRAM_BASE + 0x700u;
  m.cpu.pc = pc;
  for (uint32_t i = 0u; i < n; i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  steps = rv32emu_run(&m, 5u);
  assert(steps == 5);
  assert(m.cpu.running == true);
  assert(m.cpu.x[1] == 5u);

  steps = rv32emu_run(&m, 128u);
  assert(steps == 27);
  assert(m.cpu.running == false);
  assert(m.cpu.x[1] == 32u);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT");
}

static void test_jit_load_store_basic(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc;
  uint32_t data_addr;
  uint32_t prog[16];
  uint32_t n = 0u;
  uint32_t loaded = 0u;
  int steps;

  setenv("RV32EMU_EXPERIMENTAL_JIT", "1", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);
  unsetenv("RV32EMU_EXPERIMENTAL_TB");

  data_addr = RV32EMU_DRAM_BASE + 0x7e0u;
  prog[n++] = 0x800000b7u;                                                           /* lui  x1,0x80000 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, (int32_t)(data_addr - RV32EMU_DRAM_BASE)); /* addi x1,x1,0x7e0 */
  prog[n++] = enc_i(0x13u, 2u, 0x0u, 0u, 0x7b);                                       /* addi x2 */
  prog[n++] = enc_s(0x23u, 0x2u, 1u, 2u, 0);                                           /* sw x2,0(x1) */
  prog[n++] = enc_i(0x03u, 3u, 0x2u, 1u, 0);                                           /* lw x3,0(x1) */
  prog[n++] = 0x00100073u;                                                             /* ebreak */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  pc = RV32EMU_DRAM_BASE + 0x780u;
  m.cpu.pc = pc;
  for (uint32_t i = 0u; i < n; i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  steps = rv32emu_run(&m, 64u);
  assert(steps == (int)(n - 1u));
  assert(m.cpu.x[3] == 0x7bu);
  assert(rv32emu_phys_read(&m, data_addr, 4, &loaded));
  assert(loaded == 0x7bu);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT");
}

static void test_jit_fault_partial_retire(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc;
  uint32_t prog[8];
  uint32_t n = 0u;
  int steps;

  setenv("RV32EMU_EXPERIMENTAL_JIT", "1", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);
  unsetenv("RV32EMU_EXPERIMENTAL_TB");

  prog[n++] = enc_i(0x13u, 5u, 0x0u, 0u, 7);   /* addi x5, x0, 7 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 0u, -16); /* addi x1, x0, -16 */
  prog[n++] = enc_i(0x03u, 2u, 0x2u, 1u, 0);   /* lw x2, 0(x1) -> fault */
  prog[n++] = enc_i(0x13u, 6u, 0x0u, 0u, 1);   /* addi x6, x0, 1 (must not run) */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  pc = RV32EMU_DRAM_BASE + 0x7c0u;
  m.cpu.pc = pc;
  for (uint32_t i = 0u; i < n; i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  steps = rv32emu_run(&m, 64u);
  assert(steps == 2);
  assert(m.cpu.x[5] == 7u);
  assert(m.cpu.x[6] == 0u);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_LOAD_ACCESS_FAULT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT");
}

static void test_jit_fault_last_insn_boundary(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc;
  uint32_t prog[16];
  uint32_t n = 0u;
  int steps;

  setenv("RV32EMU_EXPERIMENTAL_JIT", "1", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);
  unsetenv("RV32EMU_EXPERIMENTAL_TB");

  prog[n++] = enc_i(0x13u, 5u, 0x0u, 0u, 1);   /* addi x5, x0, 1 */
  prog[n++] = enc_i(0x13u, 5u, 0x0u, 5u, 1);   /* addi x5, x5, 1 */
  prog[n++] = enc_i(0x13u, 5u, 0x0u, 5u, 1);   /* addi x5, x5, 1 */
  prog[n++] = enc_i(0x13u, 5u, 0x0u, 5u, 1);   /* addi x5, x5, 1 */
  prog[n++] = enc_i(0x13u, 5u, 0x0u, 5u, 1);   /* addi x5, x5, 1 */
  prog[n++] = enc_i(0x13u, 5u, 0x0u, 5u, 1);   /* addi x5, x5, 1 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 0u, -16); /* addi x1, x0, -16 */
  prog[n++] = enc_i(0x03u, 2u, 0x2u, 1u, 0);   /* lw x2, 0(x1) -> fault */
  prog[n++] = enc_i(0x13u, 6u, 0x0u, 0u, 1);   /* addi x6, x0, 1 (must not run) */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  pc = RV32EMU_DRAM_BASE + 0x840u;
  m.cpu.pc = pc;
  for (uint32_t i = 0u; i < n; i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  steps = rv32emu_run(&m, 64u);
  assert(steps == 7);
  assert(m.cpu.x[5] == 6u);
  assert(m.cpu.x[6] == 0u);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_LOAD_ACCESS_FAULT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT");
}

static void test_jit_fault_first_insn_no_retire(void) {
#if defined(__x86_64__)
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  rv32emu_tb_cache_t cache;
  rv32emu_tb_jit_result_t result;
  uint32_t pc;
  uint32_t prog[2];

  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);

  prog[0] = enc_i(0x03u, 2u, 0x2u, 1u, 0); /* lw x2, 0(x1) -> fault */
  prog[1] = enc_i(0x13u, 3u, 0x0u, 0u, 1); /* addi x3, x0, 1 (must not run) */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  pc = RV32EMU_DRAM_BASE + 0x880u;
  m.cpu.pc = pc;
  m.cpu.x[1] = 0xfffffff0u;
  for (uint32_t i = 0u; i < 2u; i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  rv32emu_tb_cache_reset(&cache);
  result = rv32emu_exec_tb_jit(&m, &cache, 8u);

  assert(result.status == RV32EMU_TB_JIT_HANDLED_NO_RETIRE);
  assert(result.retired == 0u);
  assert(m.cpu.x[2] == 0u);
  assert(m.cpu.x[3] == 0u);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_LOAD_ACCESS_FAULT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
#else
  /* JIT backend is x86_64-only in current implementation. */
#endif
}

static void test_jit_interrupt_no_retire(void) {
#if defined(__x86_64__)
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  rv32emu_tb_cache_t cache;
  rv32emu_tb_jit_result_t result;
  uint32_t pc;
  uint32_t trap;
  uint32_t prog[2];

  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);

  prog[0] = enc_i(0x13u, 1u, 0x0u, 1u, 1); /* addi x1, x1, 1 */
  prog[1] = 0x00100073u;                   /* ebreak */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  pc = RV32EMU_DRAM_BASE + 0x8a0u;
  trap = RV32EMU_DRAM_BASE + 0x8c0u;
  m.cpu.pc = pc;
  m.cpu.csr[CSR_MTVEC] = trap;
  m.cpu.csr[CSR_MSTATUS] = MSTATUS_MIE;
  m.cpu.csr[CSR_MIE] = MIP_MTIP;
  rv32emu_cpu_mip_set_bits(&m.cpu, MIP_MTIP);
  for (uint32_t i = 0u; i < 2u; i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  rv32emu_tb_cache_reset(&cache);
  result = rv32emu_exec_tb_jit(&m, &cache, 8u);

  assert(result.status == RV32EMU_TB_JIT_HANDLED_NO_RETIRE);
  assert(result.retired == 0u);
  assert(m.cpu.x[1] == 0u);
  assert(m.cpu.pc == trap);
  assert(m.cpu.running == true);
  assert(m.cpu.csr[CSR_MCAUSE] == (0x80000000u | RV32EMU_IRQ_MTIP));

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
#else
  /* JIT backend is x86_64-only in current implementation. */
#endif
}

static void test_jit_chain_interrupt_then_handler_resume(void) {
#if defined(__x86_64__)
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE + 0x900u;
  uint32_t trap = base + 0x200u;
  uint32_t main_prog[16];
  uint32_t trap_prog[8];
  uint32_t n_main = 0u;
  uint32_t n_trap = 0u;
  int steps;

  setenv("RV32EMU_EXPERIMENTAL_JIT", "1", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);
  unsetenv("RV32EMU_EXPERIMENTAL_TB");

  main_prog[n_main++] = 0x020000b7u;                /* lui x1, 0x02000 (CLINT msip) */
  main_prog[n_main++] = enc_i(0x13u, 2u, 0x0u, 0u, 1);  /* addi x2, x0, 1 */
  main_prog[n_main++] = enc_i(0x13u, 10u, 0x0u, 10u, 1); /* addi x10, x10, 1 */
  main_prog[n_main++] = enc_i(0x13u, 10u, 0x0u, 10u, 1); /* addi x10, x10, 1 */
  main_prog[n_main++] = enc_s(0x23u, 0x2u, 1u, 2u, 0);   /* sw x2, 0(x1) -> set MSIP */
  main_prog[n_main++] = enc_i(0x13u, 10u, 0x0u, 10u, 1); /* addi x10, x10, 1 */
  main_prog[n_main++] = enc_i(0x13u, 10u, 0x0u, 10u, 1); /* addi x10, x10, 1 */
  main_prog[n_main++] = enc_i(0x13u, 10u, 0x0u, 10u, 1); /* addi x10, x10, 1 */
  main_prog[n_main++] = enc_i(0x13u, 11u, 0x0u, 11u, 1); /* addi x11, x11, 1 */
  main_prog[n_main++] = enc_i(0x13u, 11u, 0x0u, 11u, 1); /* addi x11, x11, 1 */
  main_prog[n_main++] = enc_i(0x13u, 11u, 0x0u, 11u, 1); /* addi x11, x11, 1 */
  main_prog[n_main++] = enc_i(0x13u, 11u, 0x0u, 11u, 1); /* addi x11, x11, 1 */
  main_prog[n_main++] = enc_csr(0x5u, 0u, 0u, CSR_MTVEC); /* csrrwi x0, mtvec, 0 */
  main_prog[n_main++] = 0x00100073u;                     /* ebreak */

  trap_prog[n_trap++] = enc_csr(0x2u, 12u, 0u, CSR_MCAUSE); /* csrrs x12, mcause, x0 */
  trap_prog[n_trap++] = enc_csr(0x2u, 13u, 0u, CSR_MEPC);   /* csrrs x13, mepc, x0 */
  trap_prog[n_trap++] = enc_csr(0x3u, 0u, 8u, CSR_MIP);     /* csrrc x0, mip, x8 */
  trap_prog[n_trap++] = 0x30200073u;                        /* mret */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.pc = base;
  m.cpu.csr[CSR_MTVEC] = trap;
  m.cpu.csr[CSR_MSTATUS] = MSTATUS_MIE;
  m.cpu.csr[CSR_MIE] = MIP_MSIP;
  m.cpu.x[8] = MIP_MSIP;

  for (uint32_t i = 0u; i < n_main; i++) {
    assert(rv32emu_phys_write(&m, base + i * 4u, 4, main_prog[i]));
  }
  for (uint32_t i = 0u; i < n_trap; i++) {
    assert(rv32emu_phys_write(&m, trap + i * 4u, 4, trap_prog[i]));
  }

  steps = rv32emu_run(&m, 256u);

  assert(steps == 17);
  assert(m.cpu.x[10] == 5u);
  assert(m.cpu.x[11] == 4u);
  assert(m.cpu.x[12] == (0x80000000u | RV32EMU_IRQ_MSIP));
  assert(m.cpu.x[13] == (base + 8u * 4u));
  assert((rv32emu_csr_read(&m, CSR_MIP) & MIP_MSIP) == 0u);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT");
#else
  /* JIT backend is x86_64-only in current implementation. */
#endif
}

static void test_jit_cross_block_fault_handler_entry(void) {
#if defined(__x86_64__)
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE + 0xb00u;
  uint32_t trap = base + 0x200u;
  uint32_t main_prog[20];
  uint32_t trap_prog[10];
  uint32_t n_main = 0u;
  uint32_t n_trap = 0u;
  uint32_t fault_pc;
  int steps;

  setenv("RV32EMU_EXPERIMENTAL_JIT", "1", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);
  unsetenv("RV32EMU_EXPERIMENTAL_TB");

  for (uint32_t i = 0u; i < 8u; i++) {
    main_prog[n_main++] = enc_i(0x13u, 20u, 0x0u, 20u, 1); /* addi x20, x20, 1 */
  }
  main_prog[n_main++] = enc_i(0x13u, 21u, 0x0u, 21u, 1); /* addi x21, x21, 1 */
  main_prog[n_main++] = enc_i(0x13u, 21u, 0x0u, 21u, 1); /* addi x21, x21, 1 */
  main_prog[n_main++] = enc_i(0x13u, 1u, 0x0u, 0u, -16); /* addi x1, x0, -16 */
  fault_pc = base + n_main * 4u;
  main_prog[n_main++] = enc_i(0x03u, 2u, 0x2u, 1u, 0);   /* lw x2, 0(x1) -> fault */
  main_prog[n_main++] = enc_i(0x13u, 22u, 0x0u, 22u, 1); /* addi x22, x22, 1 */
  main_prog[n_main++] = enc_csr(0x5u, 0u, 0u, CSR_MTVEC); /* csrrwi x0, mtvec, 0 */
  main_prog[n_main++] = 0x00100073u;                     /* ebreak */

  trap_prog[n_trap++] = enc_csr(0x2u, 23u, 0u, CSR_MCAUSE); /* csrrs x23, mcause, x0 */
  trap_prog[n_trap++] = enc_csr(0x2u, 24u, 0u, CSR_MEPC);   /* csrrs x24, mepc, x0 */
  trap_prog[n_trap++] = enc_i(0x13u, 24u, 0x0u, 24u, 4);    /* addi x24, x24, 4 */
  trap_prog[n_trap++] = enc_csr(0x1u, 0u, 24u, CSR_MEPC);   /* csrrw x0, mepc, x24 */
  trap_prog[n_trap++] = 0x30200073u;                        /* mret */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.pc = base;
  m.cpu.csr[CSR_MTVEC] = trap;

  for (uint32_t i = 0u; i < n_main; i++) {
    assert(rv32emu_phys_write(&m, base + i * 4u, 4, main_prog[i]));
  }
  for (uint32_t i = 0u; i < n_trap; i++) {
    assert(rv32emu_phys_write(&m, trap + i * 4u, 4, trap_prog[i]));
  }

  steps = rv32emu_run(&m, 256u);

  assert(steps == 18);
  assert(m.cpu.x[20] == 8u);
  assert(m.cpu.x[21] == 2u);
  assert(m.cpu.x[22] == 1u);
  assert(m.cpu.x[23] == RV32EMU_EXC_LOAD_ACCESS_FAULT);
  assert(m.cpu.x[24] == (fault_pc + 4u));
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT");
#else
  /* JIT backend is x86_64-only in current implementation. */
#endif
}

static void test_jit_chain_branch_side_exit_recovery(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc;
  uint32_t prog[16];
  uint32_t n = 0u;
  int steps;

  setenv("RV32EMU_EXPERIMENTAL_JIT", "1", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);
  unsetenv("RV32EMU_EXPERIMENTAL_TB");

  prog[n++] = enc_i(0x13u, 1u, 0x0u, 0u, 1); /* addi x1, x0, 1 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 1); /* addi x1, x1, 1 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 1); /* addi x1, x1, 1 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 1); /* addi x1, x1, 1 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 1); /* addi x1, x1, 1 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 1); /* addi x1, x1, 1 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 1); /* addi x1, x1, 1 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 1); /* addi x1, x1, 1 */
  prog[n++] = enc_b(0x63u, 0x0u, 1u, 1u, 8); /* beq x1, x1, +8 */
  prog[n++] = enc_i(0x13u, 2u, 0x0u, 0u, 99); /* addi x2, x0, 99 (skip) */
  prog[n++] = enc_i(0x13u, 2u, 0x0u, 0u, 7);  /* addi x2, x0, 7 */
  prog[n++] = 0x00100073u;                     /* ebreak */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  pc = RV32EMU_DRAM_BASE + 0xd00u;
  m.cpu.pc = pc;
  for (uint32_t i = 0u; i < n; i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  steps = rv32emu_run(&m, 128u);
  assert(steps == 10);
  assert(m.cpu.x[1] == 8u);
  assert(m.cpu.x[2] == 7u);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT");
}

static void test_jit_multi_trap_resume_consistency(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE + 0xe00u;
  uint32_t trap = base + 0x200u;
  uint32_t main_prog[20];
  uint32_t trap_prog[10];
  uint32_t n_main = 0u;
  uint32_t n_trap = 0u;
  int steps;

  setenv("RV32EMU_EXPERIMENTAL_JIT", "1", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);
  unsetenv("RV32EMU_EXPERIMENTAL_TB");

  main_prog[n_main++] = 0x020000b7u;                  /* lui x1, 0x02000 (CLINT msip) */
  main_prog[n_main++] = enc_i(0x13u, 2u, 0x0u, 0u, 1);  /* addi x2, x0, 1 */
  main_prog[n_main++] = enc_i(0x13u, 10u, 0x0u, 10u, 1); /* addi x10, x10, 1 */
  main_prog[n_main++] = enc_s(0x23u, 0x2u, 1u, 2u, 0);   /* sw x2, 0(x1) -> set MSIP #1 */
  main_prog[n_main++] = enc_i(0x13u, 10u, 0x0u, 10u, 1); /* addi x10, x10, 1 */
  main_prog[n_main++] = enc_i(0x13u, 10u, 0x0u, 10u, 1); /* addi x10, x10, 1 */
  main_prog[n_main++] = enc_i(0x13u, 10u, 0x0u, 10u, 1); /* addi x10, x10, 1 */
  main_prog[n_main++] = enc_i(0x13u, 11u, 0x0u, 11u, 1); /* addi x11, x11, 1 */
  main_prog[n_main++] = enc_s(0x23u, 0x2u, 1u, 2u, 0);   /* sw x2, 0(x1) -> set MSIP #2 */
  main_prog[n_main++] = enc_i(0x13u, 11u, 0x0u, 11u, 1); /* addi x11, x11, 1 */
  main_prog[n_main++] = enc_i(0x13u, 11u, 0x0u, 11u, 1); /* addi x11, x11, 1 */
  main_prog[n_main++] = enc_csr(0x5u, 0u, 0u, CSR_MTVEC); /* csrrwi x0, mtvec, 0 */
  main_prog[n_main++] = 0x00100073u;                      /* ebreak */

  trap_prog[n_trap++] = enc_i(0x13u, 30u, 0x0u, 30u, 1);  /* addi x30, x30, 1 */
  trap_prog[n_trap++] = enc_csr(0x2u, 12u, 0u, CSR_MCAUSE); /* csrrs x12, mcause, x0 */
  trap_prog[n_trap++] = enc_csr(0x2u, 13u, 0u, CSR_MEPC);   /* csrrs x13, mepc, x0 */
  trap_prog[n_trap++] = enc_csr(0x3u, 0u, 8u, CSR_MIP);     /* csrrc x0, mip, x8 */
  trap_prog[n_trap++] = 0x30200073u;                        /* mret */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.pc = base;
  m.cpu.csr[CSR_MTVEC] = trap;
  m.cpu.csr[CSR_MSTATUS] = MSTATUS_MIE;
  m.cpu.csr[CSR_MIE] = MIP_MSIP;
  m.cpu.x[8] = MIP_MSIP;

  for (uint32_t i = 0u; i < n_main; i++) {
    assert(rv32emu_phys_write(&m, base + i * 4u, 4, main_prog[i]));
  }
  for (uint32_t i = 0u; i < n_trap; i++) {
    assert(rv32emu_phys_write(&m, trap + i * 4u, 4, trap_prog[i]));
  }

  steps = rv32emu_run(&m, 256u);
  assert(steps == 22);
  assert(m.cpu.x[10] == 4u);
  assert(m.cpu.x[11] == 3u);
  assert(m.cpu.x[12] == (0x80000000u | RV32EMU_IRQ_MSIP));
  assert(m.cpu.x[13] == (base + 11u * 4u));
  assert(m.cpu.x[30] == 2u);
  assert((rv32emu_csr_read(&m, CSR_MIP) & MIP_MSIP) == 0u);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT");
}

static void test_jit_jal_jalr_helper_paths(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t pc;
  uint32_t target2;
  uint32_t prog[16];
  uint32_t n = 0u;
  int steps;

  setenv("RV32EMU_EXPERIMENTAL_JIT", "1", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_HOT", "1", 1);
  unsetenv("RV32EMU_EXPERIMENTAL_TB");

  pc = RV32EMU_DRAM_BASE + 0x1000u;
  target2 = pc + 8u * 4u;

  prog[n++] = enc_i(0x13u, 1u, 0x0u, 0u, 1);            /* addi x1, x0, 1 */
  prog[n++] = 0x008002efu;                              /* jal  x5, +8 */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 100);         /* addi x1, x1, 100 (skip) */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 2);           /* addi x1, x1, 2 */
  prog[n++] = enc_u(0x37u, 3u, target2 >> 12);         /* lui  x3, target2[31:12] */
  prog[n++] = enc_i(0x13u, 3u, 0x0u, 3u, (int32_t)(target2 & 0xfffu)); /* addi x3, x3, low12 */
  prog[n++] = enc_i(0x67u, 7u, 0x0u, 3u, 0);           /* jalr x7, 0(x3) */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 1000);        /* addi x1, x1, 1000 (skip) */
  prog[n++] = enc_i(0x13u, 1u, 0x0u, 1u, 4);           /* addi x1, x1, 4 */
  prog[n++] = 0x00100073u;                             /* ebreak */

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.pc = pc;
  for (uint32_t i = 0u; i < n; i++) {
    assert(rv32emu_phys_write(&m, pc + i * 4u, 4, prog[i]));
  }

  steps = rv32emu_run(&m, 128u);
  assert(steps == 7);
  assert(m.cpu.x[1] == 7u);
  assert(m.cpu.x[5] == (pc + 8u));
  assert(m.cpu.x[7] == (pc + 7u * 4u));
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_HOT");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT");
}

int main(void) {
  setenv("RV32EMU_EXPERIMENTAL_JIT_GUARD", "0", 1);
  setenv("RV32EMU_EXPERIMENTAL_JIT_SKIP_MMODE", "0", 1);

  test_base32();
  test_rvc_basic();
  test_multihart_round_robin();
  test_multihart_lr_sc_invalidation();
  test_jit_int_alu();
  test_jit_budget_respected();
  test_jit_load_store_basic();
  test_jit_fault_partial_retire();
  test_jit_fault_last_insn_boundary();
  test_jit_fault_first_insn_no_retire();
  test_jit_interrupt_no_retire();
  test_jit_chain_interrupt_then_handler_resume();
  test_jit_cross_block_fault_handler_entry();
  test_jit_chain_branch_side_exit_recovery();
  test_jit_multi_trap_resume_consistency();
  test_jit_jal_jalr_helper_paths();

  unsetenv("RV32EMU_EXPERIMENTAL_JIT_SKIP_MMODE");
  unsetenv("RV32EMU_EXPERIMENTAL_JIT_GUARD");
  puts("[OK] rv32emu run test passed");
  return 0;
}
