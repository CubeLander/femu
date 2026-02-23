#include "rv32emu.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define SBI_EXT_LEGACY_SET_TIMER 0x00u
#define SBI_EXT_LEGACY_SHUTDOWN 0x08u
#define SBI_EXT_BASE 0x10u
#define SBI_EXT_TIME 0x54494d45u
#define SBI_EXT_HSM 0x48534du
#define SATP_MODE_SV32 (1u << 31)
#define SATP_PPN_MASK 0x003fffffu

static uint32_t enc_addi(uint32_t rd, uint32_t rs1, int32_t imm12) {
  return (((uint32_t)imm12 & 0xfffu) << 20) | ((rs1 & 0x1fu) << 15) | (0u << 12) |
         ((rd & 0x1fu) << 7) | 0x13u;
}

static uint32_t enc_csr(uint32_t funct3, uint32_t rd, uint32_t rs1_or_zimm, uint32_t csr) {
  return ((csr & 0xfffu) << 20) | ((rs1_or_zimm & 0x1fu) << 15) |
         ((funct3 & 0x7u) << 12) | ((rd & 0x1fu) << 7) | 0x73u;
}

static uint32_t enc_lui(uint32_t rd, uint32_t imm20) {
  return ((imm20 & 0xfffffu) << 12) | ((rd & 0x1fu) << 7) | 0x37u;
}

static uint32_t enc_op(uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t funct3,
                       uint32_t rd) {
  return ((funct7 & 0x7fu) << 25) | ((rs2 & 0x1fu) << 20) | ((rs1 & 0x1fu) << 15) |
         ((funct3 & 0x7u) << 12) | ((rd & 0x1fu) << 7) | 0x33u;
}

static uint32_t enc_amo(uint32_t funct5, uint32_t rs2, uint32_t rs1, uint32_t rd) {
  return ((funct5 & 0x1fu) << 27) | ((rs2 & 0x1fu) << 20) | ((rs1 & 0x1fu) << 15) |
         (0x2u << 12) | ((rd & 0x1fu) << 7) | 0x2fu;
}

static uint32_t enc_fp_load(uint32_t rd, uint32_t rs1, int32_t imm12, uint32_t width_funct3) {
  return (((uint32_t)imm12 & 0xfffu) << 20) | ((rs1 & 0x1fu) << 15) |
         ((width_funct3 & 0x7u) << 12) | ((rd & 0x1fu) << 7) | 0x07u;
}

static uint32_t enc_fp_store(uint32_t rs2, uint32_t rs1, int32_t imm12, uint32_t width_funct3) {
  uint32_t imm = (uint32_t)imm12 & 0xfffu;
  return (((imm >> 5) & 0x7fu) << 25) | ((rs2 & 0x1fu) << 20) | ((rs1 & 0x1fu) << 15) |
         ((width_funct3 & 0x7u) << 12) | ((imm & 0x1fu) << 7) | 0x27u;
}

static uint32_t enc_fp_op(uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t rm,
                          uint32_t rd) {
  return ((funct7 & 0x7fu) << 25) | ((rs2 & 0x1fu) << 20) | ((rs1 & 0x1fu) << 15) |
         ((rm & 0x7u) << 12) | ((rd & 0x1fu) << 7) | 0x53u;
}

static void write_prog(rv32emu_machine_t *m, uint32_t base, const uint32_t *prog,
                       uint32_t words) {
  for (uint32_t i = 0; i < words; i++) {
    assert(rv32emu_phys_write(m, base + i * 4u, 4, prog[i]));
  }
}

static void write_prog16(rv32emu_machine_t *m, uint32_t addr, uint16_t insn) {
  assert(rv32emu_phys_write(m, addr, 2, (uint32_t)insn));
}

static uint32_t pte_ptr(uint32_t paddr) {
  return ((paddr >> 12) << 10) | PTE_V;
}

static uint32_t pte_leaf(uint32_t paddr, uint32_t flags) {
  return ((paddr >> 12) << 10) | PTE_V | flags;
}

static void test_sbi_shim_handle_and_ecall(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE;
  uint32_t prog[] = {
      0x00000073u, /* ecall (SBI base get_spec_version) */
      enc_addi(1, 0, 1),
      0x00100073u, /* ebreak */
  };
  int steps;

  rv32emu_default_options(&opts);
  opts.enable_sbi_shim = false;
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.x[17] = SBI_EXT_BASE;
  m.cpu.x[16] = 0;
  assert(!rv32emu_handle_sbi_ecall(&m));
  rv32emu_platform_destroy(&m);

  rv32emu_default_options(&opts);
  opts.enable_sbi_shim = true;
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.priv = RV32EMU_PRIV_S;
  m.cpu.x[17] = SBI_EXT_BASE;
  m.cpu.x[16] = 0;
  assert(rv32emu_handle_sbi_ecall(&m));
  assert(m.cpu.x[10] == 0u);
  assert(m.cpu.x[11] == 0x00000002u);

  m.cpu.x[17] = SBI_EXT_BASE;
  m.cpu.x[16] = 3;
  m.cpu.x[10] = SBI_EXT_TIME;
  assert(rv32emu_handle_sbi_ecall(&m));
  assert(m.cpu.x[10] == 0u);
  assert(m.cpu.x[11] == 1u);

  m.plat.mtime = 100u;
  rv32emu_cpu_mip_store(&m.cpu, MIP_STIP | MIP_MTIP);
  m.cpu.x[17] = SBI_EXT_TIME;
  m.cpu.x[16] = 0;
  m.cpu.x[10] = 120u;
  m.cpu.x[11] = 0u;
  assert(rv32emu_handle_sbi_ecall(&m));
  assert(m.plat.clint_mtimecmp[0] == 120u);
  assert((rv32emu_cpu_mip_load(&m.cpu) & (MIP_STIP | MIP_MTIP)) == 0u);
  for (uint32_t i = 0; i < 20; i++) {
    rv32emu_step_timer(&m);
  }
  assert((rv32emu_cpu_mip_load(&m.cpu) & MIP_STIP) != 0u);
  assert((rv32emu_cpu_mip_load(&m.cpu) & MIP_MTIP) == 0u);

  m.cpu.x[17] = SBI_EXT_LEGACY_SET_TIMER;
  m.cpu.x[10] = 140u;
  m.cpu.x[11] = 0u;
  assert(rv32emu_handle_sbi_ecall(&m));
  assert(m.plat.clint_mtimecmp[0] == 140u);
  assert(m.cpu.x[10] == 0u);

  m.cpu.running = true;
  m.cpu.x[17] = SBI_EXT_LEGACY_SHUTDOWN;
  assert(rv32emu_handle_sbi_ecall(&m));
  assert(m.cpu.running == false);

  m.cpu.running = true;
  m.cpu.x[17] = 0x12345678u;
  m.cpu.x[16] = 0u;
  assert(rv32emu_handle_sbi_ecall(&m));
  assert((int32_t)m.cpu.x[10] == -2);

  m.cpu.pc = base;
  m.cpu.priv = RV32EMU_PRIV_S;
  m.cpu.running = true;
  m.cpu.csr[CSR_MTVEC] = 0u;
  m.cpu.x[17] = SBI_EXT_BASE;
  m.cpu.x[16] = 0u;
  write_prog(&m, base, prog, (uint32_t)(sizeof(prog) / sizeof(prog[0])));
  steps = rv32emu_run(&m, 16);
  assert(steps == 2);
  assert(m.cpu.x[1] == 1u);
  assert(m.cpu.x[10] == 0u);
  assert(m.cpu.running == false);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_sbi_hsm_start_status(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;

  rv32emu_default_options(&opts);
  opts.enable_sbi_shim = true;
  opts.hart_count = 2u;
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.priv = RV32EMU_PRIV_S;

  m.cpu.x[17] = SBI_EXT_HSM;
  m.cpu.x[16] = 2u; /* hart_status */
  m.cpu.x[10] = 1u;
  assert(rv32emu_handle_sbi_ecall(&m));
  assert(m.cpu.x[10] == 0u);
  assert(m.cpu.x[11] == 1u); /* stopped */

  m.cpu.x[17] = SBI_EXT_HSM;
  m.cpu.x[16] = 0u; /* hart_start */
  m.cpu.x[10] = 1u;
  m.cpu.x[11] = 0x80400000u;
  m.cpu.x[12] = 0x1234u;
  assert(rv32emu_handle_sbi_ecall(&m));
  assert(m.cpu.x[10] == 0u);
  assert(m.harts[1].running == true);
  assert(m.harts[1].pc == 0x80400000u);
  assert(m.harts[1].x[10] == 1u);
  assert(m.harts[1].x[11] == 0x1234u);
  assert(m.harts[1].priv == RV32EMU_PRIV_S);

  m.cpu.x[17] = SBI_EXT_HSM;
  m.cpu.x[16] = 2u; /* hart_status */
  m.cpu.x[10] = 1u;
  assert(rv32emu_handle_sbi_ecall(&m));
  assert(m.cpu.x[10] == 0u);
  assert(m.cpu.x[11] == 0u); /* started */

  m.cpu.x[17] = SBI_EXT_HSM;
  m.cpu.x[16] = 0u; /* hart_start again */
  m.cpu.x[10] = 1u;
  m.cpu.x[11] = 0x80410000u;
  m.cpu.x[12] = 0x55u;
  assert(rv32emu_handle_sbi_ecall(&m));
  assert((int32_t)m.cpu.x[10] == -6);

  rv32emu_platform_destroy(&m);
}

static void test_sv32_translate_and_ad_bits(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  const uint32_t root = RV32EMU_DRAM_BASE + 0x1000u;
  const uint32_t l0 = RV32EMU_DRAM_BASE + 0x2000u;
  const uint32_t target = RV32EMU_DRAM_BASE + 0x3000u;
  const uint32_t vaddr = 0x40000000u;
  const uint32_t vpn1 = (vaddr >> 22) & 0x3ffu;
  const uint32_t vpn0 = (vaddr >> 12) & 0x3ffu;
  uint32_t paddr = 0;
  uint32_t value = 0;
  uint32_t pte = 0;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.priv = RV32EMU_PRIV_S;

  assert(rv32emu_phys_write(&m, root + vpn1 * 4u, 4, pte_ptr(l0)));
  assert(rv32emu_phys_write(&m, l0 + vpn0 * 4u, 4, pte_leaf(target, PTE_R | PTE_W)));
  rv32emu_csr_write(&m, CSR_SATP, SATP_MODE_SV32 | ((root >> 12) & SATP_PPN_MASK));

  assert(rv32emu_translate(&m, vaddr, RV32EMU_ACC_LOAD, &paddr));
  assert(paddr == target);

  assert(rv32emu_phys_read(&m, l0 + vpn0 * 4u, 4, &pte));
  assert((pte & PTE_A) != 0u);
  assert((pte & PTE_D) == 0u);

  assert(rv32emu_virt_write(&m, vaddr, 4, RV32EMU_ACC_STORE, 0x55667788u));
  assert(rv32emu_phys_read(&m, target, 4, &value));
  assert(value == 0x55667788u);

  assert(rv32emu_phys_read(&m, l0 + vpn0 * 4u, 4, &pte));
  assert((pte & PTE_A) != 0u);
  assert((pte & PTE_D) != 0u);

  rv32emu_platform_destroy(&m);
}

static void test_mprv_translate_for_m_mode_data_access(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  const uint32_t root = RV32EMU_DRAM_BASE + 0xb000u;
  const uint32_t l0 = RV32EMU_DRAM_BASE + 0xc000u;
  const uint32_t target = RV32EMU_DRAM_BASE + 0xd000u;
  const uint32_t vaddr = 0xc0001000u;
  const uint32_t vpn1 = (vaddr >> 22) & 0x3ffu;
  const uint32_t vpn0 = (vaddr >> 12) & 0x3ffu;
  uint32_t paddr = 0;
  uint32_t value = 0;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.priv = RV32EMU_PRIV_M;

  assert(rv32emu_phys_write(&m, root + vpn1 * 4u, 4, pte_ptr(l0)));
  assert(rv32emu_phys_write(&m, l0 + vpn0 * 4u, 4,
                            pte_leaf(target, PTE_R | PTE_W | PTE_A | PTE_D)));
  rv32emu_csr_write(&m, CSR_SATP, SATP_MODE_SV32 | ((root >> 12) & SATP_PPN_MASK));

  assert(rv32emu_translate(&m, vaddr, RV32EMU_ACC_LOAD, &paddr));
  assert(paddr == vaddr);

  m.cpu.csr[CSR_MSTATUS] = MSTATUS_MPRV | ((uint32_t)RV32EMU_PRIV_S << MSTATUS_MPP_SHIFT);

  assert(rv32emu_translate(&m, vaddr, RV32EMU_ACC_LOAD, &paddr));
  assert(paddr == target);

  assert(rv32emu_virt_write(&m, vaddr, 4, RV32EMU_ACC_STORE, 0xaabbccdd));
  assert(rv32emu_phys_read(&m, target, 4, &value));
  assert(value == 0xaabbccdd);

  rv32emu_platform_destroy(&m);
}

static void test_sv32_permission_fault(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  const uint32_t root = RV32EMU_DRAM_BASE + 0x5000u;
  const uint32_t l0 = RV32EMU_DRAM_BASE + 0x6000u;
  const uint32_t target = RV32EMU_DRAM_BASE + 0x7000u;
  const uint32_t vaddr = 0x40001000u;
  const uint32_t vpn1 = (vaddr >> 22) & 0x3ffu;
  const uint32_t vpn0 = (vaddr >> 12) & 0x3ffu;
  uint32_t value = 0;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.priv = RV32EMU_PRIV_U;

  assert(rv32emu_phys_write(&m, root + vpn1 * 4u, 4, pte_ptr(l0)));
  assert(rv32emu_phys_write(&m, l0 + vpn0 * 4u, 4, pte_leaf(target, PTE_R | PTE_A)));
  rv32emu_csr_write(&m, CSR_SATP, SATP_MODE_SV32 | ((root >> 12) & SATP_PPN_MASK));

  assert(!rv32emu_virt_read(&m, vaddr, 4, RV32EMU_ACC_LOAD, &value));
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_LOAD_PAGE_FAULT);
  assert(m.cpu.csr[CSR_MTVAL] == vaddr);
  assert(m.cpu.running == false);

  rv32emu_platform_destroy(&m);
}

static void test_sfence_vma(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE + 0x9000u;
  uint32_t prog[] = {
      0x12000073u, /* sfence.vma x0, x0 */
      enc_addi(14, 0, 3),
      0x00100073u, /* ebreak */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.priv = RV32EMU_PRIV_S;
  m.cpu.pc = base;
  write_prog(&m, base, prog, (uint32_t)(sizeof(prog) / sizeof(prog[0])));

  steps = rv32emu_run(&m, 8);
  assert(steps == 2);
  assert(m.cpu.x[14] == 3u);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.priv = RV32EMU_PRIV_U;
  m.cpu.pc = base;
  write_prog(&m, base, prog, 1);

  steps = rv32emu_run(&m, 8);
  assert(steps == 0);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_ILLEGAL_INST);

  rv32emu_platform_destroy(&m);
}

static void test_m_ext_and_amo(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE + 0xa000u;
  uint32_t mem_addr = RV32EMU_DRAM_BASE + 0x200u;
  uint32_t prog[] = {
      enc_addi(1, 0, 6),                  /* x1 = 6 */
      enc_addi(2, 0, 7),                  /* x2 = 7 */
      enc_op(0x01, 2, 1, 0x0, 3),         /* mul  x3, x1, x2 => 42 */
      enc_op(0x01, 1, 3, 0x4, 4),         /* div  x4, x3, x1 => 7 */
      enc_lui(6, RV32EMU_DRAM_BASE >> 12), /* x6 = 0x80000000 */
      enc_addi(6, 6, 0x200),              /* x6 = mem_addr */
      enc_amo(0x00, 2, 6, 5),             /* amoadd.w x5, x2, (x6) */
      enc_amo(0x02, 0, 6, 7),             /* lr.w    x7, (x6) */
      enc_amo(0x03, 1, 6, 8),             /* sc.w    x8, x1, (x6) */
      0x00100073u,                        /* ebreak */
  };
  uint32_t value = 0;
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.pc = base;
  assert(rv32emu_phys_write(&m, mem_addr, 4, 10u));
  write_prog(&m, base, prog, (uint32_t)(sizeof(prog) / sizeof(prog[0])));

  steps = rv32emu_run(&m, 128);
  assert(steps == 9);
  assert(m.cpu.x[3] == 42u);
  assert(m.cpu.x[4] == 7u);
  assert(m.cpu.x[5] == 10u);
  assert(m.cpu.x[7] == 17u);
  assert(m.cpu.x[8] == 0u); /* sc.w success */
  assert(rv32emu_phys_read(&m, mem_addr, 4, &value));
  assert(value == 6u);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_fp_load_store_and_moves(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE + 0xb000u;
  uint32_t mem = RV32EMU_DRAM_BASE + 0x400u;
  uint32_t prog[] = {
      enc_lui(1, RV32EMU_DRAM_BASE >> 12), /* x1 = 0x80000000 */
      enc_addi(1, 1, 0x400),               /* x1 = mem */
      enc_fp_load(0, 1, 0, 0x2),           /* flw f0, 0(x1) */
      enc_fp_store(0, 1, 4, 0x2),          /* fsw f0, 4(x1) */
      enc_fp_load(1, 1, 8, 0x3),           /* fld f1, 8(x1) */
      enc_fp_store(1, 1, 16, 0x3),         /* fsd f1, 16(x1) */
      enc_fp_op(0x70, 0, 0, 0x0, 3),       /* fmv.x.w x3, f0 */
      enc_fp_op(0x78, 0, 3, 0x0, 2),       /* fmv.w.x f2, x3 */
      enc_fp_store(2, 1, 24, 0x2),         /* fsw f2, 24(x1) */
      enc_fp_op(0x11, 1, 1, 0x0, 4),       /* fsgnj.d f4, f1, f1 */
      enc_fp_store(4, 1, 32, 0x3),         /* fsd f4, 32(x1) */
      0x00100073u,                         /* ebreak */
  };
  int steps;
  uint32_t w = 0;
  uint32_t lo = 0;
  uint32_t hi = 0;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.pc = base;

  assert(rv32emu_phys_write(&m, mem + 0u, 4, 0x3f800000u));
  assert(rv32emu_phys_write(&m, mem + 8u, 4, 0x55667788u));
  assert(rv32emu_phys_write(&m, mem + 12u, 4, 0x11223344u));
  write_prog(&m, base, prog, (uint32_t)(sizeof(prog) / sizeof(prog[0])));

  steps = rv32emu_run(&m, 256);
  assert(steps == 11);

  assert(rv32emu_phys_read(&m, mem + 4u, 4, &w));
  assert(w == 0x3f800000u);
  assert(rv32emu_phys_read(&m, mem + 16u, 4, &lo));
  assert(rv32emu_phys_read(&m, mem + 20u, 4, &hi));
  assert(lo == 0x55667788u);
  assert(hi == 0x11223344u);
  assert(rv32emu_phys_read(&m, mem + 24u, 4, &w));
  assert(w == 0x3f800000u);
  assert(rv32emu_phys_read(&m, mem + 32u, 4, &lo));
  assert(rv32emu_phys_read(&m, mem + 36u, 4, &hi));
  assert(lo == 0x55667788u);
  assert(hi == 0x11223344u);
  assert(m.cpu.x[3] == 0x3f800000u);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_compressed_fp_load_store(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE + 0xc000u;
  uint32_t a0_mem = RV32EMU_DRAM_BASE + 0x200u;
  uint32_t sp_mem = RV32EMU_DRAM_BASE + 0x600u;
  uint32_t prog32[] = {
      enc_lui(10, RV32EMU_DRAM_BASE >> 12), /* a0 = 0x80000000 */
      enc_addi(10, 10, 0x200),              /* a0 = 0x80000200 */
      enc_lui(2, RV32EMU_DRAM_BASE >> 12),  /* sp = 0x80000000 */
      enc_addi(2, 2, 0x600),                /* sp = 0x80000600 */
  };
  int steps;
  uint32_t lo = 0;
  uint32_t hi = 0;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.pc = base;

  write_prog(&m, base, prog32, (uint32_t)(sizeof(prog32) / sizeof(prog32[0])));
  write_prog16(&m, base + 16u, 0x2120u); /* c.fld   fs0,64(a0) */
  write_prog16(&m, base + 18u, 0xa0a2u); /* c.fsdsp fs0,64(sp) */
  write_prog16(&m, base + 20u, 0x2406u); /* c.fldsp fs0,64(sp) */
  write_prog16(&m, base + 22u, 0xa120u); /* c.fsd   fs0,64(a0) */
  assert(rv32emu_phys_write(&m, base + 24u, 4, 0x00100073u)); /* ebreak */

  assert(rv32emu_phys_write(&m, a0_mem + 64u, 4, 0x89abcdefu));
  assert(rv32emu_phys_write(&m, a0_mem + 68u, 4, 0x01234567u));
  assert(rv32emu_phys_write(&m, sp_mem + 64u, 4, 0u));
  assert(rv32emu_phys_write(&m, sp_mem + 68u, 4, 0u));

  steps = rv32emu_run(&m, 256);
  assert(steps == 8);

  assert(rv32emu_phys_read(&m, sp_mem + 64u, 4, &lo));
  assert(rv32emu_phys_read(&m, sp_mem + 68u, 4, &hi));
  assert(lo == 0x89abcdefu);
  assert(hi == 0x01234567u);

  assert(rv32emu_phys_read(&m, a0_mem + 64u, 4, &lo));
  assert(rv32emu_phys_read(&m, a0_mem + 68u, 4, &hi));
  assert(lo == 0x89abcdefu);
  assert(hi == 0x01234567u);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_csr_alias(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  rv32emu_csr_write(&m, CSR_MSTATUS, 0u);
  rv32emu_csr_write(&m, CSR_SSTATUS, MSTATUS_SIE | MSTATUS_SUM);
  assert((rv32emu_csr_read(&m, CSR_MSTATUS) & (MSTATUS_SIE | MSTATUS_SUM)) ==
         (MSTATUS_SIE | MSTATUS_SUM));
  assert((rv32emu_csr_read(&m, CSR_SSTATUS) & (MSTATUS_SIE | MSTATUS_SUM)) ==
         (MSTATUS_SIE | MSTATUS_SUM));

  rv32emu_csr_write(&m, CSR_MIE, 0u);
  rv32emu_csr_write(&m, CSR_SIE, MIP_SEIP | MIP_MTIP);
  assert((rv32emu_csr_read(&m, CSR_MIE) & (MIP_SEIP | MIP_STIP | MIP_SSIP)) == MIP_SEIP);
  assert(rv32emu_csr_read(&m, CSR_SIE) == MIP_SEIP);

  rv32emu_csr_write(&m, CSR_MIP, 0u);
  rv32emu_csr_write(&m, CSR_SIP, MIP_STIP | MIP_MEIP);
  assert((rv32emu_csr_read(&m, CSR_MIP) & (MIP_SEIP | MIP_STIP | MIP_SSIP)) == MIP_STIP);
  assert(rv32emu_csr_read(&m, CSR_SIP) == MIP_STIP);

  rv32emu_platform_destroy(&m);
}

static void test_csr_ops(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE;
  uint32_t prog[] = {
      enc_addi(1, 0, 5),
      enc_csr(0x1, 2, 1, CSR_MSTATUS), /* csrrw x2, mstatus, x1 */
      enc_csr(0x2, 3, 0, CSR_MSTATUS), /* csrrs x3, mstatus, x0 */
      enc_csr(0x6, 4, 2, CSR_MSTATUS), /* csrrsi x4, mstatus, 2 */
      enc_csr(0x7, 5, 1, CSR_MSTATUS), /* csrrci x5, mstatus, 1 */
      0x00100073u,                     /* ebreak */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.pc = base;
  write_prog(&m, base, prog, (uint32_t)(sizeof(prog) / sizeof(prog[0])));

  steps = rv32emu_run(&m, 5);
  assert(steps == 5);
  assert(m.cpu.x[2] == 0u);
  assert(m.cpu.x[3] == 5u);
  assert(m.cpu.x[4] == 5u);
  assert(m.cpu.x[5] == 7u);
  assert(m.cpu.csr[CSR_MSTATUS] == 6u);
  assert(m.cpu.running == true);

  steps = rv32emu_run(&m, 8);
  assert(steps == 0);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_mret(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE;
  uint32_t prog[] = {
      0x30200073u,      /* mret */
      enc_addi(10, 0, 42),
      0x00100073u, /* ebreak */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.pc = base;
  write_prog(&m, base, prog, (uint32_t)(sizeof(prog) / sizeof(prog[0])));

  m.cpu.priv = RV32EMU_PRIV_M;
  m.cpu.csr[CSR_MEPC] = base + 4u;
  m.cpu.csr[CSR_MSTATUS] = ((uint32_t)RV32EMU_PRIV_M << MSTATUS_MPP_SHIFT) | MSTATUS_MPIE;

  steps = rv32emu_run(&m, 2);
  assert(steps == 2);
  assert(m.cpu.x[10] == 42u);
  assert((m.cpu.csr[CSR_MSTATUS] & MSTATUS_MPP_MASK) == 0u);
  assert((m.cpu.csr[CSR_MSTATUS] & MSTATUS_MPIE) != 0u);
  assert((m.cpu.csr[CSR_MSTATUS] & MSTATUS_MIE) != 0u);
  assert(m.cpu.priv == RV32EMU_PRIV_M);
  assert(m.cpu.running == true);

  steps = rv32emu_run(&m, 8);
  assert(steps == 0);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_sret(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE;
  uint32_t prog[] = {
      0x10200073u,     /* sret */
      enc_addi(11, 0, 7),
      0x00100073u, /* ebreak */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));
  m.cpu.pc = base;
  write_prog(&m, base, prog, (uint32_t)(sizeof(prog) / sizeof(prog[0])));

  m.cpu.priv = RV32EMU_PRIV_S;
  m.cpu.csr[CSR_SEPC] = base + 4u;
  m.cpu.csr[CSR_MSTATUS] = MSTATUS_SPP | MSTATUS_SPIE;

  steps = rv32emu_run(&m, 2);
  assert(steps == 2);
  assert(m.cpu.x[11] == 7u);
  assert((m.cpu.csr[CSR_MSTATUS] & MSTATUS_SPP) == 0u);
  assert((m.cpu.csr[CSR_MSTATUS] & MSTATUS_SPIE) != 0u);
  assert((m.cpu.csr[CSR_MSTATUS] & MSTATUS_SIE) != 0u);
  assert(m.cpu.priv == RV32EMU_PRIV_S);
  assert(m.cpu.running == true);

  steps = rv32emu_run(&m, 8);
  assert(steps == 0);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_trap_vector_mret(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE;
  uint32_t trap = base + 0x100u;
  uint32_t main_prog[] = {
      0x00000073u,      /* ecall */
      enc_addi(12, 0, 99),
      enc_csr(0x5, 0, 0, CSR_MTVEC), /* csrrwi x0, mtvec, 0 */
      0x00100073u, /* ebreak */
  };
  uint32_t trap_prog[] = {
      enc_csr(0x2, 5, 0, CSR_MEPC), /* csrrs x5, mepc, x0 */
      enc_addi(5, 5, 4),
      enc_csr(0x1, 0, 5, CSR_MEPC), /* csrrw x0, mepc, x5 */
      0x30200073u,                  /* mret */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.pc = base;
  m.cpu.csr[CSR_MTVEC] = trap;

  write_prog(&m, base, main_prog, (uint32_t)(sizeof(main_prog) / sizeof(main_prog[0])));
  write_prog(&m, trap, trap_prog, (uint32_t)(sizeof(trap_prog) / sizeof(trap_prog[0])));

  steps = rv32emu_run(&m, 128);
  assert(steps == 6);
  assert(m.cpu.x[12] == 99u);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_interrupt_trap_mret(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE;
  uint32_t trap = base + 0x180u;
  uint32_t main_prog[] = {
      enc_addi(20, 0, 1),
      enc_addi(21, 0, 2),
      enc_csr(0x5, 0, 0, CSR_MTVEC), /* csrrwi x0, mtvec, 0 */
      0x00100073u,                    /* ebreak */
  };
  uint32_t trap_prog[] = {
      enc_csr(0x2, 22, 0, CSR_MCAUSE), /* csrrs x22, mcause, x0 */
      enc_csr(0x3, 0, 8, CSR_MIP),     /* csrrc x0, mip, x8 */
      0x30200073u,                     /* mret */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.pc = base;
  m.cpu.csr[CSR_MTVEC] = trap;
  m.cpu.csr[CSR_MSTATUS] = MSTATUS_MIE;
  rv32emu_csr_write(&m, CSR_MIE, MIP_MEIP);
  m.cpu.x[8] = MIP_MEIP;

  write_prog(&m, base, main_prog, (uint32_t)(sizeof(main_prog) / sizeof(main_prog[0])));
  write_prog(&m, trap, trap_prog, (uint32_t)(sizeof(trap_prog) / sizeof(trap_prog[0])));

  rv32emu_raise_interrupt(&m, RV32EMU_IRQ_MEIP);
  steps = rv32emu_run(&m, 128);

  assert(steps == 6);
  assert(m.cpu.x[20] == 1u);
  assert(m.cpu.x[21] == 2u);
  assert(m.cpu.x[22] == (0x80000000u | RV32EMU_IRQ_MEIP));
  assert((rv32emu_csr_read(&m, CSR_MIP) & MIP_MEIP) == 0u);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_mtvec_vectored_interrupt(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE;
  uint32_t trap_base = base + 0x300u;
  uint32_t trap_vec = trap_base + (RV32EMU_IRQ_MEIP * 4u);
  uint32_t main_prog[] = {
      enc_addi(20, 0, 1),
      enc_addi(21, 0, 2),
      enc_csr(0x5, 0, 0, CSR_MTVEC), /* csrrwi x0, mtvec, 0 */
      0x00100073u,                    /* ebreak */
  };
  uint32_t handler_prog[] = {
      enc_csr(0x2, 22, 0, CSR_MCAUSE), /* csrrs x22, mcause, x0 */
      enc_csr(0x3, 0, 8, CSR_MIP),     /* csrrc x0, mip, x8 */
      0x30200073u,                     /* mret */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.pc = base;
  m.cpu.csr[CSR_MTVEC] = trap_base | 1u; /* vectored mode */
  m.cpu.csr[CSR_MSTATUS] = MSTATUS_MIE;
  rv32emu_csr_write(&m, CSR_MIE, MIP_MEIP);
  m.cpu.x[8] = MIP_MEIP;

  write_prog(&m, base, main_prog, (uint32_t)(sizeof(main_prog) / sizeof(main_prog[0])));
  write_prog(&m, trap_vec, handler_prog, (uint32_t)(sizeof(handler_prog) / sizeof(handler_prog[0])));

  rv32emu_raise_interrupt(&m, RV32EMU_IRQ_MEIP);
  steps = rv32emu_run(&m, 128);

  assert(steps == 6);
  assert(m.cpu.x[20] == 1u);
  assert(m.cpu.x[21] == 2u);
  assert(m.cpu.x[22] == (0x80000000u | RV32EMU_IRQ_MEIP));
  assert((rv32emu_csr_read(&m, CSR_MIP) & MIP_MEIP) == 0u);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_mideleg_interrupt_to_s(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE;
  uint32_t trap = base + 0x380u;
  uint32_t main_prog[] = {
      enc_addi(25, 0, 7),
      enc_csr(0x5, 0, 0, CSR_STVEC), /* csrrwi x0, stvec, 0 */
      0x00100073u,                    /* ebreak */
  };
  uint32_t handler_prog[] = {
      enc_csr(0x2, 26, 0, CSR_SCAUSE), /* csrrs x26, scause, x0 */
      enc_csr(0x3, 0, 9, CSR_SIP),     /* csrrc x0, sip, x9 */
      0x10200073u,                     /* sret */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.pc = base;
  m.cpu.priv = RV32EMU_PRIV_S;
  m.cpu.csr[CSR_STVEC] = trap;
  m.cpu.csr[CSR_MSTATUS] = MSTATUS_SIE;
  m.cpu.csr[CSR_MIDELEG] = MIP_STIP;
  rv32emu_csr_write(&m, CSR_SIE, MIP_STIP);
  m.cpu.x[9] = MIP_STIP;

  write_prog(&m, base, main_prog, (uint32_t)(sizeof(main_prog) / sizeof(main_prog[0])));
  write_prog(&m, trap, handler_prog, (uint32_t)(sizeof(handler_prog) / sizeof(handler_prog[0])));

  rv32emu_raise_interrupt(&m, RV32EMU_IRQ_STIP);
  steps = rv32emu_run(&m, 128);

  assert(steps == 5);
  assert(m.cpu.x[25] == 7u);
  assert(m.cpu.x[26] == (0x80000000u | RV32EMU_IRQ_STIP));
  assert((rv32emu_csr_read(&m, CSR_SIP) & MIP_STIP) == 0u);
  assert(m.cpu.csr[CSR_MCAUSE] == RV32EMU_EXC_BREAKPOINT);

  rv32emu_platform_destroy(&m);
}

static void test_medeleg_exception_to_s(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t base = RV32EMU_DRAM_BASE;
  uint32_t trap = base + 0x400u;
  uint32_t main_prog[] = {
      0x00100073u, /* ebreak (delegated to S trap handler) */
      enc_addi(27, 0, 9),
      enc_csr(0x5, 0, 0, CSR_STVEC), /* csrrwi x0, stvec, 0 */
      0x00100073u,                    /* ebreak to stop */
  };
  uint32_t handler_prog[] = {
      enc_csr(0x2, 28, 0, CSR_SCAUSE), /* csrrs x28, scause, x0 */
      enc_csr(0x2, 29, 0, CSR_SEPC),   /* csrrs x29, sepc, x0 */
      enc_addi(29, 29, 4),
      enc_csr(0x1, 0, 29, CSR_SEPC), /* csrrw x0, sepc, x29 */
      0x10200073u,                   /* sret */
  };
  int steps;

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  m.cpu.pc = base;
  m.cpu.priv = RV32EMU_PRIV_S;
  m.cpu.csr[CSR_STVEC] = trap;
  m.cpu.csr[CSR_MEDELEG] = (1u << RV32EMU_EXC_BREAKPOINT);

  write_prog(&m, base, main_prog, (uint32_t)(sizeof(main_prog) / sizeof(main_prog[0])));
  write_prog(&m, trap, handler_prog, (uint32_t)(sizeof(handler_prog) / sizeof(handler_prog[0])));

  steps = rv32emu_run(&m, 128);

  assert(steps == 7);
  assert(m.cpu.x[27] == 9u);
  assert(m.cpu.x[28] == RV32EMU_EXC_BREAKPOINT);
  assert(m.cpu.csr[CSR_MCAUSE] == 0u);

  rv32emu_platform_destroy(&m);
}

int main(void) {
  test_sbi_shim_handle_and_ecall();
  test_sbi_hsm_start_status();
  test_sv32_translate_and_ad_bits();
  test_mprv_translate_for_m_mode_data_access();
  test_sv32_permission_fault();
  test_sfence_vma();
  test_m_ext_and_amo();
  test_fp_load_store_and_moves();
  test_compressed_fp_load_store();
  test_csr_alias();
  test_csr_ops();
  test_interrupt_trap_mret();
  test_mtvec_vectored_interrupt();
  test_mideleg_interrupt_to_s();
  test_medeleg_exception_to_s();
  test_mret();
  test_sret();
  test_trap_vector_mret();
  puts("[OK] rv32emu system test passed");
  return 0;
}
