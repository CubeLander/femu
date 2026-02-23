#ifndef RV32EMU_H
#define RV32EMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RV32EMU_DRAM_BASE 0x80000000u
#define RV32EMU_DEFAULT_RAM_MB 256u

#define RV32EMU_UART_BASE 0x10000000u
#define RV32EMU_UART_SIZE 0x00000100u
#define RV32EMU_CLINT_BASE 0x02000000u
#define RV32EMU_CLINT_SIZE 0x00010000u
#define RV32EMU_PLIC_BASE 0x0c000000u
#define RV32EMU_PLIC_SIZE 0x04000000u

#define RV32EMU_DEFAULT_KERNEL_LOAD 0x80400000u
#define RV32EMU_DEFAULT_DTB_LOAD 0x87f00000u
#define RV32EMU_DEFAULT_INITRD_LOAD 0x88000000u
#define RV32EMU_DEFAULT_MAX_INSTR (50000000ull)
#define RV32EMU_UART_RX_FIFO_SIZE 256u
#define RV32EMU_DEFAULT_HART_COUNT 1u
#define RV32EMU_MAX_HARTS 4u
#define RV32EMU_MAX_PLIC_CONTEXTS (RV32EMU_MAX_HARTS * 2u)

typedef enum {
  RV32EMU_PRIV_U = 0,
  RV32EMU_PRIV_S = 1,
  RV32EMU_PRIV_M = 3,
} rv32emu_priv_t;

typedef enum {
  RV32EMU_ACC_FETCH = 0,
  RV32EMU_ACC_LOAD = 1,
  RV32EMU_ACC_STORE = 2,
} rv32emu_access_t;

enum {
  RV32EMU_EXC_INST_MISALIGNED = 0,
  RV32EMU_EXC_INST_ACCESS_FAULT = 1,
  RV32EMU_EXC_ILLEGAL_INST = 2,
  RV32EMU_EXC_BREAKPOINT = 3,
  RV32EMU_EXC_LOAD_MISALIGNED = 4,
  RV32EMU_EXC_LOAD_ACCESS_FAULT = 5,
  RV32EMU_EXC_STORE_MISALIGNED = 6,
  RV32EMU_EXC_STORE_ACCESS_FAULT = 7,
  RV32EMU_EXC_ECALL_U = 8,
  RV32EMU_EXC_ECALL_S = 9,
  RV32EMU_EXC_ECALL_M = 11,
  RV32EMU_EXC_INST_PAGE_FAULT = 12,
  RV32EMU_EXC_LOAD_PAGE_FAULT = 13,
  RV32EMU_EXC_STORE_PAGE_FAULT = 15,
};

enum {
  RV32EMU_IRQ_SSIP = 1,
  RV32EMU_IRQ_MSIP = 3,
  RV32EMU_IRQ_STIP = 5,
  RV32EMU_IRQ_MTIP = 7,
  RV32EMU_IRQ_SEIP = 9,
  RV32EMU_IRQ_MEIP = 11,
};

/* CSR numbers */
enum {
  CSR_FFLAGS = 0x001,
  CSR_FRM = 0x002,
  CSR_FCSR = 0x003,

  CSR_SSTATUS = 0x100,
  CSR_SCOUNTEREN = 0x106,
  CSR_SIE = 0x104,
  CSR_STVEC = 0x105,
  CSR_SSCRATCH = 0x140,
  CSR_SEPC = 0x141,
  CSR_SCAUSE = 0x142,
  CSR_STVAL = 0x143,
  CSR_SIP = 0x144,
  CSR_SATP = 0x180,

  CSR_MSTATUS = 0x300,
  CSR_MISA = 0x301,
  CSR_MCOUNTEREN = 0x306,
  CSR_MEDELEG = 0x302,
  CSR_MIDELEG = 0x303,
  CSR_MIE = 0x304,
  CSR_MTVEC = 0x305,
  CSR_MSCRATCH = 0x340,
  CSR_MEPC = 0x341,
  CSR_MCAUSE = 0x342,
  CSR_MTVAL = 0x343,
  CSR_MIP = 0x344,

  CSR_CYCLE = 0xc00,
  CSR_TIME = 0xc01,
  CSR_INSTRET = 0xc02,
  CSR_CYCLEH = 0xc80,
  CSR_TIMEH = 0xc81,
  CSR_INSTRETH = 0xc82,

  CSR_MVENDORID = 0xf11,
  CSR_MARCHID = 0xf12,
  CSR_MIMPID = 0xf13,
  CSR_MHARTID = 0xf14,
};

enum {
  MSTATUS_SIE = (1u << 1),
  MSTATUS_MIE = (1u << 3),
  MSTATUS_SPIE = (1u << 5),
  MSTATUS_MPIE = (1u << 7),
  MSTATUS_SPP = (1u << 8),
  MSTATUS_FS_SHIFT = 13,
  MSTATUS_FS_MASK = (3u << MSTATUS_FS_SHIFT),
  MSTATUS_MPRV = (1u << 17),
  MSTATUS_MPP_SHIFT = 11,
  MSTATUS_MPP_MASK = (3u << MSTATUS_MPP_SHIFT),
  MSTATUS_SUM = (1u << 18),
  MSTATUS_MXR = (1u << 19),
};

enum {
  MIP_SSIP = (1u << RV32EMU_IRQ_SSIP),
  MIP_MSIP = (1u << RV32EMU_IRQ_MSIP),
  MIP_STIP = (1u << RV32EMU_IRQ_STIP),
  MIP_MTIP = (1u << RV32EMU_IRQ_MTIP),
  MIP_SEIP = (1u << RV32EMU_IRQ_SEIP),
  MIP_MEIP = (1u << RV32EMU_IRQ_MEIP),
};

enum {
  PTE_V = 1u << 0,
  PTE_R = 1u << 1,
  PTE_W = 1u << 2,
  PTE_X = 1u << 3,
  PTE_U = 1u << 4,
  PTE_G = 1u << 5,
  PTE_A = 1u << 6,
  PTE_D = 1u << 7,
};

typedef struct {
  const char *kernel_path;
  const char *dtb_path;
  const char *initrd_path;
  uint32_t ram_mb;
  uint32_t kernel_load_addr;
  uint32_t dtb_load_addr;
  uint32_t initrd_load_addr;
  uint32_t entry_override;
  bool has_entry_override;
  bool boot_s_mode;
  bool enable_sbi_shim;
  bool trace;
  uint32_t hart_count;
  uint64_t max_instructions;
} rv32emu_options_t;

typedef struct {
  uint8_t *dram;
  uint32_t dram_base;
  uint32_t dram_size;

  uint64_t mtime;
  uint64_t clint_mtimecmp[RV32EMU_MAX_HARTS];
  uint32_t clint_msip[RV32EMU_MAX_HARTS];
  uint64_t next_timer_deadline;

  uint32_t plic_pending;
  uint32_t plic_enable[RV32EMU_MAX_PLIC_CONTEXTS];
  uint32_t plic_claim[RV32EMU_MAX_PLIC_CONTEXTS];

  uint8_t uart_regs[8];
  uint8_t uart_rx_fifo[RV32EMU_UART_RX_FIFO_SIZE];
  uint16_t uart_rx_head;
  uint16_t uart_rx_tail;
  uint16_t uart_rx_count;
  bool uart_tx_irq_pending;
} rv32emu_platform_t;

typedef struct {
  uint32_t x[32];
  uint64_t f[32];
  uint32_t pc;
  uint64_t cycle;
  uint64_t instret;

  rv32emu_priv_t priv;
  bool running;
  bool trace;

  uint32_t lr_addr;
  bool lr_valid;

  uint32_t csr[4096];
} rv32emu_cpu_t;

typedef struct {
  rv32emu_options_t opts;
  rv32emu_platform_t plat;
  union {
    rv32emu_cpu_t cpu;
    rv32emu_cpu_t harts[RV32EMU_MAX_HARTS];
  };
  uint32_t hart_count;
  uint32_t active_hart;
  rv32emu_cpu_t *cpu_cur;
} rv32emu_machine_t;

static inline uint32_t rv32emu_hart_slot(const rv32emu_machine_t *m, uint32_t hartid) {
  if (m == NULL || hartid >= m->hart_count || hartid >= RV32EMU_MAX_HARTS) {
    return RV32EMU_MAX_HARTS;
  }
  return hartid;
}

static inline rv32emu_cpu_t *rv32emu_hart_cpu(rv32emu_machine_t *m, uint32_t hartid) {
  uint32_t slot = rv32emu_hart_slot(m, hartid);
  if (slot >= RV32EMU_MAX_HARTS) {
    return NULL;
  }
  return &m->harts[slot];
}

static inline const rv32emu_cpu_t *rv32emu_hart_cpu_const(const rv32emu_machine_t *m,
                                                           uint32_t hartid) {
  uint32_t slot = rv32emu_hart_slot(m, hartid);
  if (slot >= RV32EMU_MAX_HARTS) {
    return NULL;
  }
  return &m->harts[slot];
}

static inline rv32emu_cpu_t *rv32emu_current_cpu(rv32emu_machine_t *m) {
  if (m == NULL || m->hart_count == 0u) {
    return NULL;
  }
  if (m->cpu_cur == NULL) {
    return &m->harts[0];
  }
  return m->cpu_cur;
}

static inline const rv32emu_cpu_t *rv32emu_current_cpu_const(const rv32emu_machine_t *m) {
  if (m == NULL || m->hart_count == 0u) {
    return NULL;
  }
  if (m->cpu_cur == NULL) {
    return &m->harts[0];
  }
  return m->cpu_cur;
}

static inline rv32emu_cpu_t *rv32emu_current_cpu_fast(rv32emu_machine_t *m) {
  return m->cpu_cur;
}

static inline const rv32emu_cpu_t *rv32emu_current_cpu_const_fast(const rv32emu_machine_t *m) {
  return m->cpu_cur;
}

static inline void rv32emu_set_active_hart(rv32emu_machine_t *m, uint32_t hartid) {
  if (m == NULL || m->hart_count == 0u || hartid >= m->hart_count) {
    if (m != NULL) {
      m->active_hart = 0u;
      m->cpu_cur = (m->hart_count == 0u) ? NULL : &m->harts[0];
    }
    return;
  }

  m->active_hart = hartid;
  m->cpu_cur = &m->harts[hartid];
}

#define RV32EMU_CPU(m) rv32emu_current_cpu_fast(m)
#define RV32EMU_CPU_CONST(m) rv32emu_current_cpu_const_fast(m)

static inline uint64_t rv32emu_timer_next_deadline(const rv32emu_machine_t *m) {
  uint64_t next = UINT64_MAX;
  uint32_t hart;

  if (m == NULL) {
    return UINT64_MAX;
  }

  for (hart = 0u; hart < m->hart_count; hart++) {
    uint64_t cmp = m->plat.clint_mtimecmp[hart];

    /*
     * Only future comparators are candidates for the next event.
     * Expired comparators (cmp <= mtime) already have pending IRQ state, and
     * including them would force a pointless full-hart scan every instruction.
     */
    if (cmp > m->plat.mtime && cmp < next) {
      next = cmp;
    }
  }

  return next;
}

static inline void rv32emu_timer_refresh_deadline(rv32emu_machine_t *m) {
  if (m == NULL) {
    return;
  }
  m->plat.next_timer_deadline = rv32emu_timer_next_deadline(m);
}

static inline bool rv32emu_any_hart_running(const rv32emu_machine_t *m) {
  uint32_t i;

  if (m == NULL) {
    return false;
  }

  for (i = 0; i < m->hart_count; i++) {
    const rv32emu_cpu_t *cpu = rv32emu_hart_cpu_const(m, i);
    if (cpu != NULL && cpu->running) {
      return true;
    }
  }
  return false;
}

void rv32emu_default_options(rv32emu_options_t *opts);

bool rv32emu_platform_init(rv32emu_machine_t *m, const rv32emu_options_t *opts);
void rv32emu_platform_destroy(rv32emu_machine_t *m);

uint8_t *rv32emu_dram_ptr(rv32emu_machine_t *m, uint32_t paddr, size_t len);

bool rv32emu_phys_read(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out);
bool rv32emu_phys_write(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data);
bool rv32emu_uart_push_rx(rv32emu_machine_t *m, uint8_t data);

void rv32emu_step_timer(rv32emu_machine_t *m);

bool rv32emu_translate(rv32emu_machine_t *m, uint32_t vaddr, rv32emu_access_t access,
                       uint32_t *paddr_out);

bool rv32emu_virt_read(rv32emu_machine_t *m, uint32_t vaddr, int len,
                       rv32emu_access_t access, uint32_t *out);
bool rv32emu_virt_write(rv32emu_machine_t *m, uint32_t vaddr, int len,
                        rv32emu_access_t access, uint32_t data);

uint32_t rv32emu_csr_read(rv32emu_machine_t *m, uint16_t csr_num);
void rv32emu_csr_write(rv32emu_machine_t *m, uint16_t csr_num, uint32_t value);

void rv32emu_raise_exception(rv32emu_machine_t *m, uint32_t cause, uint32_t tval);
void rv32emu_raise_interrupt(rv32emu_machine_t *m, uint32_t cause_num);
bool rv32emu_check_pending_interrupt(rv32emu_machine_t *m);

bool rv32emu_handle_sbi_ecall(rv32emu_machine_t *m);

bool rv32emu_load_image_auto(rv32emu_machine_t *m, const char *path, uint32_t load_addr,
                             uint32_t *entry_out, bool *entry_valid);
bool rv32emu_load_raw(rv32emu_machine_t *m, const char *path, uint32_t load_addr,
                      uint32_t *size_out);
bool rv32emu_load_elf32(rv32emu_machine_t *m, const char *path, uint32_t *entry_out);

int rv32emu_run(rv32emu_machine_t *m, uint64_t max_instructions);

static inline uint32_t rv32emu_sign_extend(uint32_t value, int bits) {
  const uint32_t shift = 32u - (uint32_t)bits;
  return (uint32_t)((int32_t)(value << shift) >> shift);
}

#endif
