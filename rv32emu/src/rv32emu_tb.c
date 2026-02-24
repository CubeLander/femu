#include "rv32emu_tb.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__x86_64__)
#include <pthread.h>
#include <sys/mman.h>
#endif

bool rv32emu_exec_decoded(rv32emu_machine_t *m, const rv32emu_decoded_insn_t *decoded);

#define RV32EMU_JIT_DEFAULT_HOT_THRESHOLD 3u
#define RV32EMU_JIT_DEFAULT_MAX_INSNS_PER_BLOCK 8u
#define RV32EMU_JIT_DEFAULT_MIN_PREFIX_INSNS 4u
#define RV32EMU_JIT_DEFAULT_CHAIN_MAX_INSNS 64u
#define RV32EMU_JIT_MAX_CHAIN_LIMIT 4096u
#define RV32EMU_JIT_DEFAULT_POOL_MB 4u
#define RV32EMU_JIT_MAX_POOL_MB 1024u

static bool rv32emu_tb_env_bool(const char *name, bool default_value) {
  const char *value;

  if (name == NULL) {
    return default_value;
  }
  value = getenv(name);
  if (value == NULL || value[0] == '\0') {
    return default_value;
  }

  switch (value[0]) {
  case '1':
  case 'y':
  case 'Y':
  case 't':
  case 'T':
    return true;
  case '0':
  case 'n':
  case 'N':
  case 'f':
  case 'F':
    return false;
  default:
    return default_value;
  }
}

static uint32_t rv32emu_tb_u32_from_env(const char *name, uint32_t default_value, uint32_t min_value,
                                        uint32_t max_value) {
  const char *env;
  char *endp = NULL;
  unsigned long parsed;

  if (min_value > max_value) {
    return default_value;
  }

  if (default_value < min_value) {
    default_value = min_value;
  } else if (default_value > max_value) {
    default_value = max_value;
  }

  if (name == NULL) {
    return default_value;
  }

  env = getenv(name);
  if (env == NULL || env[0] == '\0') {
    return default_value;
  }

  parsed = strtoul(env, &endp, 10);
  if (endp == env || *endp != '\0' || parsed < (unsigned long)min_value ||
      parsed > (unsigned long)max_value) {
    return default_value;
  }

  return (uint32_t)parsed;
}

static inline uint32_t rv32emu_tb_index(uint32_t pc) {
  return (pc >> 2) & (RV32EMU_TB_LINES - 1u);
}

static bool rv32emu_tb_is_block_terminator(uint32_t opcode) {
  switch (opcode) {
  case 0x63: /* branch */
  case 0x67: /* jalr */
  case 0x6f: /* jal */
  case 0x73: /* system */
    return true;
  default:
    return false;
  }
}

static uint8_t rv32emu_tb_hot_threshold_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_HOT",
                                          RV32EMU_JIT_DEFAULT_HOT_THRESHOLD, 1u, 255u);
}

static uint8_t rv32emu_tb_max_block_insns_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_MAX_BLOCK_INSNS",
                                          RV32EMU_JIT_DEFAULT_MAX_INSNS_PER_BLOCK, 1u,
                                          RV32EMU_TB_MAX_INSNS);
}

static uint8_t rv32emu_tb_min_prefix_insns_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_MIN_PREFIX_INSNS",
                                          RV32EMU_JIT_DEFAULT_MIN_PREFIX_INSNS, 1u,
                                          RV32EMU_TB_MAX_INSNS);
}

static uint32_t rv32emu_tb_chain_max_insns_from_env(void) {
  return rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_CHAIN_MAX_INSNS",
                                 RV32EMU_JIT_DEFAULT_CHAIN_MAX_INSNS, 1u,
                                 RV32EMU_JIT_MAX_CHAIN_LIMIT);
}

static size_t rv32emu_tb_jit_pool_size_from_env(void) {
  uint32_t pool_mb = rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_POOL_MB",
                                             RV32EMU_JIT_DEFAULT_POOL_MB, 1u,
                                             RV32EMU_JIT_MAX_POOL_MB);
  return (size_t)pool_mb * 1024u * 1024u;
}

typedef struct {
  atomic_uint_fast64_t dispatch_calls;
  atomic_uint_fast64_t dispatch_no_ready;
  atomic_uint_fast64_t dispatch_budget_clamped;
  atomic_uint_fast64_t dispatch_retired_calls;
  atomic_uint_fast64_t dispatch_retired_insns;
  atomic_uint_fast64_t dispatch_handled_no_retire;
  atomic_uint_fast64_t dispatch_noprogress;
  atomic_uint_fast64_t compile_attempts;
  atomic_uint_fast64_t compile_success;
  atomic_uint_fast64_t compile_prefix_insns;
  atomic_uint_fast64_t compile_prefix_truncated;
  atomic_uint_fast64_t compile_fail_too_short;
  atomic_uint_fast64_t compile_fail_unsupported_prefix;
  atomic_uint_fast64_t compile_fail_alloc;
  atomic_uint_fast64_t compile_fail_emit;
  atomic_uint_fast64_t helper_mem_calls;
  atomic_uint_fast64_t helper_cf_calls;
  atomic_uint_fast64_t chain_hits;
  atomic_uint_fast64_t chain_misses;
} rv32emu_jit_stats_t;

static rv32emu_jit_stats_t g_rv32emu_jit_stats = {
    .dispatch_calls = ATOMIC_VAR_INIT(0u),
    .dispatch_no_ready = ATOMIC_VAR_INIT(0u),
    .dispatch_budget_clamped = ATOMIC_VAR_INIT(0u),
    .dispatch_retired_calls = ATOMIC_VAR_INIT(0u),
    .dispatch_retired_insns = ATOMIC_VAR_INIT(0u),
    .dispatch_handled_no_retire = ATOMIC_VAR_INIT(0u),
    .dispatch_noprogress = ATOMIC_VAR_INIT(0u),
    .compile_attempts = ATOMIC_VAR_INIT(0u),
    .compile_success = ATOMIC_VAR_INIT(0u),
    .compile_prefix_insns = ATOMIC_VAR_INIT(0u),
    .compile_prefix_truncated = ATOMIC_VAR_INIT(0u),
    .compile_fail_too_short = ATOMIC_VAR_INIT(0u),
    .compile_fail_unsupported_prefix = ATOMIC_VAR_INIT(0u),
    .compile_fail_alloc = ATOMIC_VAR_INIT(0u),
    .compile_fail_emit = ATOMIC_VAR_INIT(0u),
    .helper_mem_calls = ATOMIC_VAR_INIT(0u),
    .helper_cf_calls = ATOMIC_VAR_INIT(0u),
    .chain_hits = ATOMIC_VAR_INIT(0u),
    .chain_misses = ATOMIC_VAR_INIT(0u),
};

static atomic_uint_fast32_t g_rv32emu_jit_stats_mode = ATOMIC_VAR_INIT(0u);

static bool rv32emu_jit_stats_enabled(void) {
  uint_fast32_t mode = atomic_load_explicit(&g_rv32emu_jit_stats_mode, memory_order_acquire);

  if (mode == 0u) {
    uint_fast32_t configured =
        rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_STATS", false) ? 2u : 1u;
    atomic_store_explicit(&g_rv32emu_jit_stats_mode, configured, memory_order_release);
    mode = configured;
  }

  return mode == 2u;
}

#define RV32EMU_JIT_STATS_INC(field)                                                            \
  do {                                                                                           \
    if (rv32emu_jit_stats_enabled()) {                                                           \
      atomic_fetch_add_explicit(&g_rv32emu_jit_stats.field, 1u, memory_order_relaxed);         \
    }                                                                                            \
  } while (0)

#define RV32EMU_JIT_STATS_ADD(field, value)                                                      \
  do {                                                                                           \
    if (rv32emu_jit_stats_enabled()) {                                                           \
      atomic_fetch_add_explicit(&g_rv32emu_jit_stats.field, (uint_fast64_t)(value),             \
                                memory_order_relaxed);                                            \
    }                                                                                            \
  } while (0)

#if defined(__x86_64__)
#define RV32EMU_JIT_BYTES_PER_INSN 112u
#define RV32EMU_JIT_EPILOGUE_BYTES 128u

typedef struct {
  uint8_t *base;
  size_t cap;
  size_t used;
  pthread_mutex_t lock;
  pthread_once_t once;
} rv32emu_jit_pool_t;

typedef struct {
  uint8_t *p;
  uint8_t *end;
} rv32emu_x86_emit_t;

static rv32emu_jit_pool_t g_rv32emu_jit_pool = {
    .base = NULL,
    .cap = 0u,
    .used = 0u,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .once = PTHREAD_ONCE_INIT,
};

static _Thread_local rv32emu_tb_cache_t *g_rv32emu_jit_tls_cache = NULL;
static _Thread_local uint64_t g_rv32emu_jit_tls_budget = 0u;
static _Thread_local uint64_t g_rv32emu_jit_tls_total = 0u;
static _Thread_local bool g_rv32emu_jit_tls_handled = false;

static void rv32emu_jit_pool_init_once(void) {
  size_t pool_size = rv32emu_tb_jit_pool_size_from_env();
  void *mem = mmap(NULL, pool_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
  if (mem == MAP_FAILED) {
    g_rv32emu_jit_pool.base = NULL;
    g_rv32emu_jit_pool.cap = 0u;
    g_rv32emu_jit_pool.used = 0u;
    return;
  }

  g_rv32emu_jit_pool.base = (uint8_t *)mem;
  g_rv32emu_jit_pool.cap = pool_size;
  g_rv32emu_jit_pool.used = 0u;
}

static void *rv32emu_jit_alloc(size_t bytes) {
  void *out = NULL;

  if (bytes == 0u) {
    return NULL;
  }

  (void)pthread_once(&g_rv32emu_jit_pool.once, rv32emu_jit_pool_init_once);
  if (g_rv32emu_jit_pool.base == NULL || g_rv32emu_jit_pool.cap == 0u) {
    return NULL;
  }

  if (pthread_mutex_lock(&g_rv32emu_jit_pool.lock) != 0) {
    return NULL;
  }

  {
    size_t aligned_used = (g_rv32emu_jit_pool.used + 15u) & ~((size_t)15u);
    if (aligned_used + bytes <= g_rv32emu_jit_pool.cap) {
      out = g_rv32emu_jit_pool.base + aligned_used;
      g_rv32emu_jit_pool.used = aligned_used + bytes;
    }
  }

  (void)pthread_mutex_unlock(&g_rv32emu_jit_pool.lock);
  return out;
}

static int rv32emu_jit_block_commit(rv32emu_machine_t *m, rv32emu_cpu_t *cpu, uint32_t next_pc,
                                    uint32_t retired) {
  if (m == NULL || cpu == NULL || retired == 0u) {
    return 0;
  }

  cpu->pc = next_pc;
  cpu->x[0] = 0u;
  cpu->cycle += retired;
  cpu->instret += retired;
  for (uint32_t i = 0u; i < retired; i++) {
    rv32emu_step_timer(m);
  }

  g_rv32emu_jit_tls_total += retired;
  if (g_rv32emu_jit_tls_budget > retired) {
    g_rv32emu_jit_tls_budget -= retired;
  } else {
    g_rv32emu_jit_tls_budget = 0u;
  }

  return (int)g_rv32emu_jit_tls_total;
}

static void rv32emu_jit_retire_prefix(rv32emu_machine_t *m, rv32emu_cpu_t *cpu, uint32_t retired) {
  if (m == NULL || cpu == NULL || retired == 0u) {
    return;
  }

  cpu->x[0] = 0u;
  cpu->cycle += retired;
  cpu->instret += retired;
  for (uint32_t i = 0u; i < retired; i++) {
    rv32emu_step_timer(m);
  }

  g_rv32emu_jit_tls_total += retired;
  if (g_rv32emu_jit_tls_budget > retired) {
    g_rv32emu_jit_tls_budget -= retired;
  } else {
    g_rv32emu_jit_tls_budget = 0u;
  }
}

static uint32_t rv32emu_jit_result_or_no_retire(void) {
  if (g_rv32emu_jit_tls_total == 0u) {
    return UINT32_MAX;
  }
  return (uint32_t)g_rv32emu_jit_tls_total;
}

/*
 * Entry pre-check keeps JIT blocks interrupt-safe even when a pending IRQ
 * appears between runner polling and native block entry.
 *
 * Return convention:
 * - 0   : continue executing block
 * - -1  : handled, return to dispatcher with no guest retire
 */
static int rv32emu_jit_pre_dispatch(rv32emu_machine_t *m, rv32emu_cpu_t *cpu) {
  if (m == NULL || cpu == NULL) {
    g_rv32emu_jit_tls_handled = true;
    return -1;
  }

  if (!atomic_load_explicit(&cpu->running, memory_order_acquire)) {
    g_rv32emu_jit_tls_handled = true;
    return -1;
  }

  if (rv32emu_check_pending_interrupt(m)) {
    g_rv32emu_jit_tls_handled = true;
    return -1;
  }

  if (!atomic_load_explicit(&cpu->running, memory_order_acquire)) {
    g_rv32emu_jit_tls_handled = true;
    return -1;
  }

  return 0;
}

static inline void rv32emu_jit_write_rd(rv32emu_cpu_t *cpu, uint32_t rd, uint32_t value) {
  if (cpu != NULL && rd != 0u) {
    cpu->x[rd] = value;
  }
}

static bool rv32emu_jit_load_value(rv32emu_machine_t *m, uint32_t addr, uint32_t funct3,
                                   uint32_t *value_out) {
  uint32_t raw = 0u;
  uint32_t b0 = 0u;
  uint32_t b1 = 0u;
  uint32_t b2 = 0u;
  uint32_t b3 = 0u;

  if (m == NULL || value_out == NULL) {
    return false;
  }

  switch (funct3) {
  case 0x0: /* lb */
    if (!rv32emu_virt_read(m, addr, 1, RV32EMU_ACC_LOAD, &raw)) {
      return false;
    }
    *value_out = rv32emu_sign_extend(raw & 0xffu, 8);
    return true;
  case 0x1: /* lh */
    if ((addr & 1u) == 0u) {
      if (!rv32emu_virt_read(m, addr, 2, RV32EMU_ACC_LOAD, &raw)) {
        return false;
      }
    } else {
      if (!rv32emu_virt_read(m, addr, 1, RV32EMU_ACC_LOAD, &b0) ||
          !rv32emu_virt_read(m, addr + 1u, 1, RV32EMU_ACC_LOAD, &b1)) {
        return false;
      }
      raw = (b0 & 0xffu) | ((b1 & 0xffu) << 8);
    }
    *value_out = rv32emu_sign_extend(raw & 0xffffu, 16);
    return true;
  case 0x2: /* lw */
    if ((addr & 3u) == 0u) {
      return rv32emu_virt_read(m, addr, 4, RV32EMU_ACC_LOAD, value_out);
    }
    if (!rv32emu_virt_read(m, addr, 1, RV32EMU_ACC_LOAD, &b0) ||
        !rv32emu_virt_read(m, addr + 1u, 1, RV32EMU_ACC_LOAD, &b1) ||
        !rv32emu_virt_read(m, addr + 2u, 1, RV32EMU_ACC_LOAD, &b2) ||
        !rv32emu_virt_read(m, addr + 3u, 1, RV32EMU_ACC_LOAD, &b3)) {
      return false;
    }
    *value_out = (b0 & 0xffu) | ((b1 & 0xffu) << 8) | ((b2 & 0xffu) << 16) |
                 ((b3 & 0xffu) << 24);
    return true;
  case 0x4: /* lbu */
    if (!rv32emu_virt_read(m, addr, 1, RV32EMU_ACC_LOAD, &raw)) {
      return false;
    }
    *value_out = raw & 0xffu;
    return true;
  case 0x5: /* lhu */
    if ((addr & 1u) == 0u) {
      if (!rv32emu_virt_read(m, addr, 2, RV32EMU_ACC_LOAD, &raw)) {
        return false;
      }
    } else {
      if (!rv32emu_virt_read(m, addr, 1, RV32EMU_ACC_LOAD, &b0) ||
          !rv32emu_virt_read(m, addr + 1u, 1, RV32EMU_ACC_LOAD, &b1)) {
        return false;
      }
      raw = (b0 & 0xffu) | ((b1 & 0xffu) << 8);
    }
    *value_out = raw & 0xffffu;
    return true;
  default:
    return false;
  }
}

static bool rv32emu_jit_store_value(rv32emu_machine_t *m, uint32_t addr, uint32_t funct3,
                                    uint32_t value) {
  if (m == NULL) {
    return false;
  }

  switch (funct3) {
  case 0x0: /* sb */
    return rv32emu_virt_write(m, addr, 1, RV32EMU_ACC_STORE, value);
  case 0x1: /* sh */
    if ((addr & 1u) == 0u) {
      return rv32emu_virt_write(m, addr, 2, RV32EMU_ACC_STORE, value);
    }
    return rv32emu_virt_write(m, addr, 1, RV32EMU_ACC_STORE, value & 0xffu) &&
           rv32emu_virt_write(m, addr + 1u, 1, RV32EMU_ACC_STORE, (value >> 8) & 0xffu);
  case 0x2: /* sw */
    if ((addr & 3u) == 0u) {
      return rv32emu_virt_write(m, addr, 4, RV32EMU_ACC_STORE, value);
    }
    return rv32emu_virt_write(m, addr, 1, RV32EMU_ACC_STORE, value & 0xffu) &&
           rv32emu_virt_write(m, addr + 1u, 1, RV32EMU_ACC_STORE, (value >> 8) & 0xffu) &&
           rv32emu_virt_write(m, addr + 2u, 1, RV32EMU_ACC_STORE, (value >> 16) & 0xffu) &&
           rv32emu_virt_write(m, addr + 3u, 1, RV32EMU_ACC_STORE, (value >> 24) & 0xffu);
  default:
    return false;
  }
}

static uint32_t rv32emu_jit_exec_mem(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                                     const rv32emu_decoded_insn_t *d, uint32_t insn_pc,
                                     uint32_t retired_prefix) {
  uint32_t rs1v;
  uint32_t rs2v;
  uint32_t addr;
  uint32_t value = 0u;
  bool ok = false;

  RV32EMU_JIT_STATS_INC(helper_mem_calls);

  if (m == NULL || cpu == NULL || d == NULL) {
    g_rv32emu_jit_tls_handled = true;
    return rv32emu_jit_result_or_no_retire();
  }

  cpu->pc = insn_pc;
  rs1v = cpu->x[d->rs1];
  rs2v = cpu->x[d->rs2];

  switch (d->opcode) {
  case 0x03: /* load */
    addr = rs1v + (uint32_t)d->imm_i;
    if (!rv32emu_jit_load_value(m, addr, d->funct3, &value)) {
      if (d->funct3 != 0x0u && d->funct3 != 0x1u && d->funct3 != 0x2u && d->funct3 != 0x4u &&
          d->funct3 != 0x5u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, d->raw);
      }
      if (retired_prefix != 0u) {
        rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
      }
      g_rv32emu_jit_tls_handled = true;
      return rv32emu_jit_result_or_no_retire();
    }
    rv32emu_jit_write_rd(cpu, d->rd, value);
    return 0u;
  case 0x23: /* store */
    addr = rs1v + (uint32_t)d->imm_s;
    ok = rv32emu_jit_store_value(m, addr, d->funct3, rs2v);
    if (!ok) {
      if (d->funct3 != 0x0u && d->funct3 != 0x1u && d->funct3 != 0x2u) {
        rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, d->raw);
      }
      if (retired_prefix != 0u) {
        rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
      }
      g_rv32emu_jit_tls_handled = true;
      return rv32emu_jit_result_or_no_retire();
    }
    return 0u;
  default:
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, d->raw);
    if (retired_prefix != 0u) {
      rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
    }
    g_rv32emu_jit_tls_handled = true;
    return rv32emu_jit_result_or_no_retire();
  }
}

static uint32_t rv32emu_jit_exec_cf(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                                    const rv32emu_decoded_insn_t *d, uint32_t insn_pc,
                                    uint32_t retired_prefix) {
  uint32_t next_pc;
  uint32_t ret_pc;
  uint32_t rs1v;
  uint32_t rs2v;

  RV32EMU_JIT_STATS_INC(helper_cf_calls);

  if (m == NULL || cpu == NULL || d == NULL) {
    g_rv32emu_jit_tls_handled = true;
    return rv32emu_jit_result_or_no_retire();
  }

  cpu->pc = insn_pc;
  next_pc = insn_pc + ((d->insn_len == 2u) ? 2u : 4u);
  ret_pc = next_pc;
  rs1v = cpu->x[d->rs1];
  rs2v = cpu->x[d->rs2];

  switch (d->opcode) {
  case 0x6f: /* jal */
    rv32emu_jit_write_rd(cpu, d->rd, ret_pc);
    next_pc = insn_pc + (uint32_t)d->imm_j;
    break;
  case 0x67: /* jalr */
    if (d->funct3 != 0x0u) {
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, d->raw);
      if (retired_prefix != 0u) {
        rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
      }
      g_rv32emu_jit_tls_handled = true;
      return rv32emu_jit_result_or_no_retire();
    }
    next_pc = (rs1v + (uint32_t)d->imm_i) & ~1u;
    rv32emu_jit_write_rd(cpu, d->rd, ret_pc);
    break;
  case 0x63: /* branch */
    switch (d->funct3) {
    case 0x0: /* beq */
      if (rs1v == rs2v) {
        next_pc = insn_pc + (uint32_t)d->imm_b;
      }
      break;
    case 0x1: /* bne */
      if (rs1v != rs2v) {
        next_pc = insn_pc + (uint32_t)d->imm_b;
      }
      break;
    case 0x4: /* blt */
      if ((int32_t)rs1v < (int32_t)rs2v) {
        next_pc = insn_pc + (uint32_t)d->imm_b;
      }
      break;
    case 0x5: /* bge */
      if ((int32_t)rs1v >= (int32_t)rs2v) {
        next_pc = insn_pc + (uint32_t)d->imm_b;
      }
      break;
    case 0x6: /* bltu */
      if (rs1v < rs2v) {
        next_pc = insn_pc + (uint32_t)d->imm_b;
      }
      break;
    case 0x7: /* bgeu */
      if (rs1v >= rs2v) {
        next_pc = insn_pc + (uint32_t)d->imm_b;
      }
      break;
    default:
      rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, d->raw);
      if (retired_prefix != 0u) {
        rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
      }
      g_rv32emu_jit_tls_handled = true;
      return rv32emu_jit_result_or_no_retire();
    }
    break;
  default:
    rv32emu_raise_exception(m, RV32EMU_EXC_ILLEGAL_INST, d->raw);
    if (retired_prefix != 0u) {
      rv32emu_jit_retire_prefix(m, cpu, retired_prefix);
    }
    g_rv32emu_jit_tls_handled = true;
    return rv32emu_jit_result_or_no_retire();
  }

  return (uint32_t)rv32emu_jit_block_commit(m, cpu, next_pc, retired_prefix + 1u);
}

static bool rv32emu_emit_u8(rv32emu_x86_emit_t *e, uint8_t v) {
  if (e == NULL || e->p == NULL || e->p >= e->end) {
    return false;
  }
  *e->p++ = v;
  return true;
}

static bool rv32emu_emit_u32(rv32emu_x86_emit_t *e, uint32_t v) {
  if (!rv32emu_emit_u8(e, (uint8_t)(v & 0xffu))) {
    return false;
  }
  if (!rv32emu_emit_u8(e, (uint8_t)((v >> 8) & 0xffu))) {
    return false;
  }
  if (!rv32emu_emit_u8(e, (uint8_t)((v >> 16) & 0xffu))) {
    return false;
  }
  return rv32emu_emit_u8(e, (uint8_t)((v >> 24) & 0xffu));
}

static bool rv32emu_emit_u64(rv32emu_x86_emit_t *e, uint64_t v) {
  if (!rv32emu_emit_u32(e, (uint32_t)(v & 0xffffffffu))) {
    return false;
  }
  return rv32emu_emit_u32(e, (uint32_t)(v >> 32));
}

static bool rv32emu_emit_mov_eax_mem_rsi(rv32emu_x86_emit_t *e, uint32_t disp32) {
  return rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x86u) && rv32emu_emit_u32(e, disp32);
}

static bool rv32emu_emit_mov_ecx_mem_rsi(rv32emu_x86_emit_t *e, uint32_t disp32) {
  return rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x8eu) && rv32emu_emit_u32(e, disp32);
}

static bool rv32emu_emit_mov_mem_rsi_eax(rv32emu_x86_emit_t *e, uint32_t disp32) {
  return rv32emu_emit_u8(e, 0x89u) && rv32emu_emit_u8(e, 0x86u) && rv32emu_emit_u32(e, disp32);
}

static bool rv32emu_emit_add_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x05u) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_add_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x01u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_sub_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x29u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_xor_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x31u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_or_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x09u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_and_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x21u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_xor_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x35u) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_or_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x0du) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_and_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x25u) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_cmp_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x3du) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_cmp_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x39u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_setl_al(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x0fu) && rv32emu_emit_u8(e, 0x9cu) && rv32emu_emit_u8(e, 0xc0u);
}

static bool rv32emu_emit_setb_al(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x0fu) && rv32emu_emit_u8(e, 0x92u) && rv32emu_emit_u8(e, 0xc0u);
}

static bool rv32emu_emit_movzx_eax_al(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x0fu) && rv32emu_emit_u8(e, 0xb6u) && rv32emu_emit_u8(e, 0xc0u);
}

static bool rv32emu_emit_shl_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt) {
  return rv32emu_emit_u8(e, 0xc1u) && rv32emu_emit_u8(e, 0xe0u) && rv32emu_emit_u8(e, shamt);
}

static bool rv32emu_emit_shr_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt) {
  return rv32emu_emit_u8(e, 0xc1u) && rv32emu_emit_u8(e, 0xe8u) && rv32emu_emit_u8(e, shamt);
}

static bool rv32emu_emit_sar_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt) {
  return rv32emu_emit_u8(e, 0xc1u) && rv32emu_emit_u8(e, 0xf8u) && rv32emu_emit_u8(e, shamt);
}

static bool rv32emu_emit_shl_eax_cl(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0xd3u) && rv32emu_emit_u8(e, 0xe0u);
}

static bool rv32emu_emit_shr_eax_cl(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0xd3u) && rv32emu_emit_u8(e, 0xe8u);
}

static bool rv32emu_emit_sar_eax_cl(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0xd3u) && rv32emu_emit_u8(e, 0xf8u);
}

static bool rv32emu_emit_mov_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0xb8u) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_mov_rdx_imm64(rv32emu_x86_emit_t *e, uint64_t imm64) {
  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xbau) && rv32emu_emit_u64(e, imm64);
}

static bool rv32emu_emit_mov_r8d_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x41u) && rv32emu_emit_u8(e, 0xb8u) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_mov_rsi_saved(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x34u) &&
         rv32emu_emit_u8(e, 0x24u);
}

static bool rv32emu_emit_mov_rdi_saved(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x7cu) &&
         rv32emu_emit_u8(e, 0x24u) && rv32emu_emit_u8(e, 0x08u);
}

static bool rv32emu_emit_jit_mem_helper(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                        uint32_t insn_pc, uint32_t retired_prefix) {
  if (e == NULL || d == NULL) {
    return false;
  }

  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xecu) &&
         rv32emu_emit_u8(e, 0x08u) &&                                       /* sub rsp, 8 */
         rv32emu_emit_mov_rdx_imm64(e, (uint64_t)(uintptr_t)d) &&           /* arg2: decoded* */
         rv32emu_emit_u8(e, 0xb9u) && rv32emu_emit_u32(e, insn_pc) &&       /* arg3: insn_pc */
         rv32emu_emit_mov_r8d_imm32(e, retired_prefix) &&                    /* arg4: retired_prefix */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
         rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_exec_mem) && /* movabs rax, fn */
         rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) &&          /* call rax */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
         rv32emu_emit_u8(e, 0x08u) &&                                         /* add rsp, 8 */
         rv32emu_emit_u8(e, 0x85u) && rv32emu_emit_u8(e, 0xc0u) &&           /* test eax, eax */
         rv32emu_emit_u8(e, 0x74u) && rv32emu_emit_u8(e, 0x03u) &&           /* jz +3 (success) */
         rv32emu_emit_u8(e, 0x5eu) && rv32emu_emit_u8(e, 0x5fu) && rv32emu_emit_u8(e, 0xc3u) &&
         rv32emu_emit_mov_rsi_saved(e) &&                                     /* restore cpu ptr */
         rv32emu_emit_mov_rdi_saved(e);                                       /* restore machine ptr */
}

static bool rv32emu_emit_jit_cf_helper(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                       uint32_t insn_pc, uint32_t retired_prefix) {
  if (e == NULL || d == NULL) {
    return false;
  }

  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xecu) &&
         rv32emu_emit_u8(e, 0x08u) &&                                      /* sub rsp, 8 */
         rv32emu_emit_mov_rdx_imm64(e, (uint64_t)(uintptr_t)d) &&          /* arg2: decoded* */
         rv32emu_emit_u8(e, 0xb9u) && rv32emu_emit_u32(e, insn_pc) &&      /* arg3: insn_pc */
         rv32emu_emit_mov_r8d_imm32(e, retired_prefix) &&                   /* arg4: retired_prefix */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
         rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_exec_cf) && /* movabs rax, fn */
         rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) &&         /* call rax */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
         rv32emu_emit_u8(e, 0x08u) &&                                      /* add rsp, 8 */
         rv32emu_emit_u8(e, 0x5eu) && rv32emu_emit_u8(e, 0x5fu) &&
         rv32emu_emit_u8(e, 0xc3u); /* pop rsi; pop rdi; ret */
}

static rv32emu_tb_jit_fn_t rv32emu_jit_chain_next(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                                                   rv32emu_tb_line_t *from);

static bool rv32emu_emit_prologue(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x57u) && /* push rdi */
         rv32emu_emit_u8(e, 0x56u) && /* push rsi */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xecu) &&
         rv32emu_emit_u8(e, 0x08u) && /* sub rsp, 8 */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
         rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_pre_dispatch) && /* movabs rax, fn */
         rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) &&               /* call rax */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
         rv32emu_emit_u8(e, 0x08u) && /* add rsp, 8 */
         rv32emu_emit_mov_rsi_saved(e) && /* restore cpu ptr after helper call */
         rv32emu_emit_mov_rdi_saved(e) && /* restore machine ptr after helper call */
         rv32emu_emit_u8(e, 0x85u) && rv32emu_emit_u8(e, 0xc0u) && /* test eax, eax */
         rv32emu_emit_u8(e, 0x74u) && rv32emu_emit_u8(e, 0x03u) && /* jz +3 (continue) */
         rv32emu_emit_u8(e, 0x5eu) && rv32emu_emit_u8(e, 0x5fu) &&
         rv32emu_emit_u8(e, 0xc3u); /* pop rsi; pop rdi; ret */
}

static bool rv32emu_emit_epilogue(rv32emu_x86_emit_t *e, rv32emu_tb_line_t *line, uint32_t next_pc,
                                  uint32_t retired) {
  if (line == NULL) {
    return false;
  }

  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xecu) &&
         rv32emu_emit_u8(e, 0x08u) && /* sub rsp, 8 */
         rv32emu_emit_u8(e, 0xbau) && rv32emu_emit_u32(e, next_pc) && /* mov edx, next_pc */
         rv32emu_emit_u8(e, 0xb9u) && rv32emu_emit_u32(e, retired) && /* mov ecx, retired */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
         rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_block_commit) && /* movabs rax, fn */
         rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) && /* call rax */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
         rv32emu_emit_u8(e, 0x08u) && /* add rsp, 8 */
         rv32emu_emit_u8(e, 0x50u) && /* push rax (save cumulative retired) */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x74u) &&
         rv32emu_emit_u8(e, 0x24u) && rv32emu_emit_u8(e, 0x08u) && /* mov rsi, [rsp + 8] */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x7cu) &&
         rv32emu_emit_u8(e, 0x24u) && rv32emu_emit_u8(e, 0x10u) && /* mov rdi, [rsp + 16] */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xbau) &&
         rv32emu_emit_u64(e, (uint64_t)(uintptr_t)line) && /* movabs rdx, line */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
         rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_chain_next) && /* movabs rax, fn */
         rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) && /* call rax */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x85u) &&
         rv32emu_emit_u8(e, 0xc0u) && /* test rax, rax */
         rv32emu_emit_u8(e, 0x75u) && rv32emu_emit_u8(e, 0x04u) && /* jne +4 */
         rv32emu_emit_u8(e, 0x58u) && /* pop rax */
         rv32emu_emit_u8(e, 0x5eu) && /* pop rsi */
         rv32emu_emit_u8(e, 0x5fu) && /* pop rdi */
         rv32emu_emit_u8(e, 0xc3u) && /* ret */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
         rv32emu_emit_u8(e, 0x08u) && /* add rsp, 8 (drop saved retired) */
         rv32emu_emit_u8(e, 0x5eu) && /* pop rsi */
         rv32emu_emit_u8(e, 0x5fu) && /* pop rdi */
         rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xe0u); /* jmp rax */
}

static inline uint32_t rv32emu_cpu_x_off(uint32_t idx) {
  return (uint32_t)(offsetof(rv32emu_cpu_t, x) + idx * sizeof(uint32_t));
}

static bool rv32emu_jit_insn_supported(const rv32emu_decoded_insn_t *d) {
  const char *disable_alu_env;
  const char *disable_mem_env;
  const char *disable_cf_env;
  bool allow_alu;
  bool allow_mem;
  bool allow_cf;

  if (d == NULL) {
    return false;
  }

  disable_alu_env = getenv("RV32EMU_EXPERIMENTAL_JIT_DISABLE_ALU");
  disable_mem_env = getenv("RV32EMU_EXPERIMENTAL_JIT_DISABLE_MEM");
  disable_cf_env = getenv("RV32EMU_EXPERIMENTAL_JIT_DISABLE_CF");
  allow_alu = !(disable_alu_env != NULL && disable_alu_env[0] == '1');
  allow_mem = !(disable_mem_env != NULL && disable_mem_env[0] == '1');
  allow_cf = !(disable_cf_env != NULL && disable_cf_env[0] == '1');

  switch (d->opcode) {
  case 0x37: /* lui */
  case 0x17: /* auipc */
    if (!allow_alu) {
      return false;
    }
    return true;
  case 0x13: /* op-imm */
    if (!allow_alu) {
      return false;
    }
    switch (d->funct3) {
    case 0x0: /* addi */
    case 0x1: /* slli */
    case 0x2: /* slti */
    case 0x3: /* sltiu */
    case 0x4: /* xori */
    case 0x6: /* ori */
    case 0x7: /* andi */
      return true;
    case 0x5: /* srli/srai */
      return d->funct7 == 0x00u || d->funct7 == 0x20u;
    default:
      return false;
    }
  case 0x33: /* op */
    if (!allow_alu) {
      return false;
    }
    if (d->funct7 == 0x01u) {
      return false; /* M-extension currently falls back to interpreter. */
    }
    switch (d->funct3) {
    case 0x0:
      return d->funct7 == 0x00u || d->funct7 == 0x20u; /* add/sub */
    case 0x1:                                                 /* sll */
    case 0x2:                                                 /* slt */
    case 0x3:                                                 /* sltu */
    case 0x4:                                                 /* xor */
    case 0x6:                                                 /* or */
    case 0x7:                                                 /* and */
      return true;
    case 0x5:
      return d->funct7 == 0x00u || d->funct7 == 0x20u; /* srl/sra */
    default:
      return false;
    }
  case 0x03: /* load */
    if (!allow_mem) {
      return false;
    }
    return d->funct3 == 0x0u || d->funct3 == 0x1u || d->funct3 == 0x2u || d->funct3 == 0x4u ||
           d->funct3 == 0x5u;
  case 0x23: /* store */
    if (!allow_mem) {
      return false;
    }
    return d->funct3 == 0x0u || d->funct3 == 0x1u || d->funct3 == 0x2u;
  case 0x6f: /* jal */
    if (!allow_cf) {
      return false;
    }
    return true;
  case 0x67: /* jalr */
    if (!allow_cf) {
      return false;
    }
    return d->funct3 == 0x0u;
  case 0x63: /* branch */
    if (!allow_cf) {
      return false;
    }
    return d->funct3 == 0x0u || d->funct3 == 0x1u || d->funct3 == 0x4u || d->funct3 == 0x5u ||
           d->funct3 == 0x6u || d->funct3 == 0x7u;
  default:
    return false;
  }
}

static bool rv32emu_jit_emit_one(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                 uint32_t insn_pc, uint32_t retired_before) {
  uint32_t rd_off;
  uint32_t rs1_off;
  uint32_t rs2_off;

  if (e == NULL || d == NULL) {
    return false;
  }

  rd_off = rv32emu_cpu_x_off(d->rd);
  rs1_off = rv32emu_cpu_x_off(d->rs1);
  rs2_off = rv32emu_cpu_x_off(d->rs2);

  switch (d->opcode) {
  case 0x37: /* lui */
    if (!rv32emu_emit_mov_eax_imm32(e, (uint32_t)d->imm_u)) {
      return false;
    }
    if (d->rd != 0u && !rv32emu_emit_mov_mem_rsi_eax(e, rd_off)) {
      return false;
    }
    return true;
  case 0x17: /* auipc */
    if (!rv32emu_emit_mov_eax_imm32(e, insn_pc + (uint32_t)d->imm_u)) {
      return false;
    }
    if (d->rd != 0u && !rv32emu_emit_mov_mem_rsi_eax(e, rd_off)) {
      return false;
    }
    return true;
  case 0x13: /* op-imm */
    if (!rv32emu_emit_mov_eax_mem_rsi(e, rs1_off)) {
      return false;
    }
    switch (d->funct3) {
    case 0x0: /* addi */
      if (d->imm_i != 0 && !rv32emu_emit_add_eax_imm32(e, (uint32_t)d->imm_i)) {
        return false;
      }
      break;
    case 0x1: /* slli */
      if (!rv32emu_emit_shl_eax_imm8(e, (uint8_t)d->rs2)) {
        return false;
      }
      break;
    case 0x2: /* slti */
      if (!rv32emu_emit_cmp_eax_imm32(e, (uint32_t)d->imm_i) || !rv32emu_emit_setl_al(e) ||
          !rv32emu_emit_movzx_eax_al(e)) {
        return false;
      }
      break;
    case 0x3: /* sltiu */
      if (!rv32emu_emit_cmp_eax_imm32(e, (uint32_t)d->imm_i) || !rv32emu_emit_setb_al(e) ||
          !rv32emu_emit_movzx_eax_al(e)) {
        return false;
      }
      break;
    case 0x4: /* xori */
      if (!rv32emu_emit_xor_eax_imm32(e, (uint32_t)d->imm_i)) {
        return false;
      }
      break;
    case 0x5: /* srli/srai */
      if (d->funct7 == 0x00u) {
        if (!rv32emu_emit_shr_eax_imm8(e, (uint8_t)d->rs2)) {
          return false;
        }
        break;
      }
      if (d->funct7 == 0x20u) {
        if (!rv32emu_emit_sar_eax_imm8(e, (uint8_t)d->rs2)) {
          return false;
        }
        break;
      }
      return false;
    case 0x6: /* ori */
      if (!rv32emu_emit_or_eax_imm32(e, (uint32_t)d->imm_i)) {
        return false;
      }
      break;
    case 0x7: /* andi */
      if (!rv32emu_emit_and_eax_imm32(e, (uint32_t)d->imm_i)) {
        return false;
      }
      break;
    default:
      return false;
    }
    if (d->rd != 0u && !rv32emu_emit_mov_mem_rsi_eax(e, rd_off)) {
      return false;
    }
    return true;
  case 0x33: /* op */
    if (d->funct7 == 0x01u) {
      return false;
    }
    if (!rv32emu_emit_mov_eax_mem_rsi(e, rs1_off)) {
      return false;
    }
    switch (d->funct3) {
    case 0x0: /* add/sub */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off)) {
        return false;
      }
      if (d->funct7 == 0x00u) {
        if (!rv32emu_emit_add_eax_ecx(e)) {
          return false;
        }
        break;
      }
      if (d->funct7 == 0x20u) {
        if (!rv32emu_emit_sub_eax_ecx(e)) {
          return false;
        }
        break;
      }
      return false;
    case 0x1: /* sll */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_shl_eax_cl(e)) {
        return false;
      }
      break;
    case 0x2: /* slt */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_cmp_eax_ecx(e) ||
          !rv32emu_emit_setl_al(e) || !rv32emu_emit_movzx_eax_al(e)) {
        return false;
      }
      break;
    case 0x3: /* sltu */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_cmp_eax_ecx(e) ||
          !rv32emu_emit_setb_al(e) || !rv32emu_emit_movzx_eax_al(e)) {
        return false;
      }
      break;
    case 0x4: /* xor */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_xor_eax_ecx(e)) {
        return false;
      }
      break;
    case 0x5: /* srl/sra */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off)) {
        return false;
      }
      if (d->funct7 == 0x00u) {
        if (!rv32emu_emit_shr_eax_cl(e)) {
          return false;
        }
        break;
      }
      if (d->funct7 == 0x20u) {
        if (!rv32emu_emit_sar_eax_cl(e)) {
          return false;
        }
        break;
      }
      return false;
    case 0x6: /* or */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_or_eax_ecx(e)) {
        return false;
      }
      break;
    case 0x7: /* and */
      if (!rv32emu_emit_mov_ecx_mem_rsi(e, rs2_off) || !rv32emu_emit_and_eax_ecx(e)) {
        return false;
      }
      break;
    default:
      return false;
    }
    if (d->rd != 0u && !rv32emu_emit_mov_mem_rsi_eax(e, rd_off)) {
      return false;
    }
    return true;
  case 0x03: /* load */
  case 0x23: /* store */
    return rv32emu_emit_jit_mem_helper(e, d, insn_pc, retired_before);
  case 0x63: /* branch */
  case 0x67: /* jalr */
  case 0x6f: /* jal */
    return rv32emu_emit_jit_cf_helper(e, d, insn_pc, retired_before);
  default:
    return false;
  }
}

static uint32_t rv32emu_tb_line_next_pc_for_count(const rv32emu_tb_line_t *line, uint32_t count) {
  if (line == NULL || line->count == 0u || count == 0u) {
    return 0u;
  }
  if (count < line->count) {
    return line->pcs[count];
  }

  {
    uint32_t last_idx = count - 1u;
    uint32_t step = (line->decoded[last_idx].insn_len == 2u) ? 2u : 4u;
    return line->pcs[last_idx] + step;
  }
}

static bool rv32emu_tb_try_compile_jit(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line) {
  uint32_t jit_count = 0u;
  uint32_t max_jit_insns = RV32EMU_JIT_DEFAULT_MAX_INSNS_PER_BLOCK;
  uint32_t min_prefix_insns = RV32EMU_JIT_DEFAULT_MIN_PREFIX_INSNS;
  uint32_t epilogue_next_pc;
  size_t code_bytes;
  uint8_t *code_ptr;
  rv32emu_x86_emit_t emit;

  if (line == NULL || !line->valid || line->count == 0u) {
    return false;
  }
  if (cache != NULL && cache->jit_max_block_insns != 0u) {
    max_jit_insns = cache->jit_max_block_insns;
  }
  if (cache != NULL && cache->jit_min_prefix_insns != 0u) {
    min_prefix_insns = cache->jit_min_prefix_insns;
  }
  if (min_prefix_insns > max_jit_insns) {
    min_prefix_insns = max_jit_insns;
  }

  RV32EMU_JIT_STATS_INC(compile_attempts);

  line->jit_valid = false;
  line->jit_count = 0u;
  line->jit_fn = NULL;
  line->jit_map_count = 0u;
  line->jit_code_size = 0u;
  line->jit_chain_valid = false;
  line->jit_chain_pc = 0u;
  line->jit_chain_fn = NULL;
  line->jit_tried = true;

  while (jit_count < line->count && jit_count < max_jit_insns) {
    if (!rv32emu_jit_insn_supported(&line->decoded[jit_count])) {
      break;
    }
    jit_count++;
  }

  if (jit_count == 0u) {
    RV32EMU_JIT_STATS_INC(compile_fail_unsupported_prefix);
    return false;
  }
  if (jit_count < min_prefix_insns) {
    RV32EMU_JIT_STATS_INC(compile_fail_too_short);
    return false;
  }

  code_bytes = (size_t)jit_count * RV32EMU_JIT_BYTES_PER_INSN + RV32EMU_JIT_EPILOGUE_BYTES;
  code_ptr = (uint8_t *)rv32emu_jit_alloc(code_bytes);
  if (code_ptr == NULL) {
    RV32EMU_JIT_STATS_INC(compile_fail_alloc);
    return false;
  }

  emit.p = code_ptr;
  emit.end = code_ptr + code_bytes;

  if (!rv32emu_emit_prologue(&emit)) {
    line->jit_valid = false;
    line->jit_count = 0u;
    line->jit_fn = NULL;
    line->jit_map_count = 0u;
    line->jit_code_size = 0u;
    line->jit_chain_valid = false;
    line->jit_chain_pc = 0u;
    line->jit_chain_fn = NULL;
    RV32EMU_JIT_STATS_INC(compile_fail_emit);
    return false;
  }

  for (uint32_t i = 0u; i < jit_count; i++) {
    line->jit_host_off[i] = (uint16_t)(uintptr_t)(emit.p - code_ptr);
    if (!rv32emu_jit_emit_one(&emit, &line->decoded[i], line->pcs[i], i)) {
      line->jit_valid = false;
      line->jit_count = 0u;
      line->jit_fn = NULL;
      line->jit_map_count = 0u;
      line->jit_code_size = 0u;
      line->jit_chain_valid = false;
      line->jit_chain_pc = 0u;
      line->jit_chain_fn = NULL;
      RV32EMU_JIT_STATS_INC(compile_fail_emit);
      return false;
    }
  }

  epilogue_next_pc = rv32emu_tb_line_next_pc_for_count(line, jit_count);
  if (!rv32emu_emit_epilogue(&emit, line, epilogue_next_pc, jit_count)) {
    line->jit_valid = false;
    line->jit_count = 0u;
    line->jit_fn = NULL;
    line->jit_map_count = 0u;
    line->jit_code_size = 0u;
    line->jit_chain_valid = false;
    line->jit_chain_pc = 0u;
    line->jit_chain_fn = NULL;
    RV32EMU_JIT_STATS_INC(compile_fail_emit);
    return false;
  }

  line->jit_valid = true;
  line->jit_count = (uint8_t)jit_count;
  line->jit_fn = (rv32emu_tb_jit_fn_t)(void *)code_ptr;
  line->jit_map_count = (uint8_t)jit_count;
  line->jit_code_size = (uint32_t)(uintptr_t)(emit.p - code_ptr);
  line->jit_chain_valid = false;
  line->jit_chain_pc = 0u;
  line->jit_chain_fn = NULL;
  RV32EMU_JIT_STATS_INC(compile_success);
  RV32EMU_JIT_STATS_ADD(compile_prefix_insns, jit_count);
  if (jit_count < line->count && jit_count == max_jit_insns) {
    RV32EMU_JIT_STATS_INC(compile_prefix_truncated);
  }
  return true;
}
#else
static bool rv32emu_tb_try_compile_jit(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line) {
  (void)cache;
  if (line != NULL) {
    line->jit_tried = true;
    line->jit_valid = false;
    line->jit_count = 0u;
    line->jit_fn = NULL;
    line->jit_map_count = 0u;
    line->jit_code_size = 0u;
    line->jit_chain_valid = false;
    line->jit_chain_pc = 0u;
    line->jit_chain_fn = NULL;
  }
  return false;
}
#endif

void rv32emu_tb_cache_reset(rv32emu_tb_cache_t *cache) {
  if (cache == NULL) {
    return;
  }
  cache->active = false;
  cache->jit_hot_threshold = rv32emu_tb_hot_threshold_from_env();
  cache->jit_max_block_insns = rv32emu_tb_max_block_insns_from_env();
  cache->jit_min_prefix_insns = rv32emu_tb_min_prefix_insns_from_env();
  cache->jit_chain_max_insns = rv32emu_tb_chain_max_insns_from_env();
  for (uint32_t i = 0u; i < RV32EMU_TB_LINES; i++) {
    cache->lines[i].valid = false;
    cache->lines[i].start_pc = 0u;
    cache->lines[i].count = 0u;
    cache->lines[i].jit_hotness = 0u;
    cache->lines[i].jit_tried = false;
    cache->lines[i].jit_valid = false;
    cache->lines[i].jit_count = 0u;
    cache->lines[i].jit_fn = NULL;
    cache->lines[i].jit_map_count = 0u;
    cache->lines[i].jit_code_size = 0u;
    cache->lines[i].jit_chain_valid = false;
    cache->lines[i].jit_chain_pc = 0u;
    cache->lines[i].jit_chain_fn = NULL;
  }
}

static bool rv32emu_tb_build_line(rv32emu_machine_t *m, rv32emu_tb_line_t *line, uint32_t start_pc) {
  uint32_t pc = start_pc;

  if (m == NULL || line == NULL) {
    return false;
  }

  line->valid = false;
  line->count = 0u;
  line->start_pc = start_pc;
  line->jit_hotness = 0u;
  line->jit_tried = false;
  line->jit_valid = false;
  line->jit_count = 0u;
  line->jit_fn = NULL;
  line->jit_map_count = 0u;
  line->jit_code_size = 0u;
  line->jit_chain_valid = false;
  line->jit_chain_pc = 0u;
  line->jit_chain_fn = NULL;

  for (uint32_t i = 0u; i < RV32EMU_TB_MAX_INSNS; i++) {
    uint32_t insn16 = 0u;
    uint32_t insn32 = 0u;
    uint32_t step = 4u;

    if ((pc & 1u) != 0u) {
      break;
    }
    if (!rv32emu_virt_read(m, pc, 2, RV32EMU_ACC_FETCH, &insn16)) {
      break;
    }
    line->pcs[line->count] = pc;
    if ((insn16 & 0x3u) != 0x3u) {
      if (!rv32emu_decode16((uint16_t)insn16, &line->decoded[line->count])) {
        break;
      }
      step = 2u;
    } else {
      if (!rv32emu_virt_read(m, pc, 4, RV32EMU_ACC_FETCH, &insn32)) {
        break;
      }
      rv32emu_decode32(insn32, &line->decoded[line->count]);
      step = 4u;
    }
    line->count++;
    pc += step;

    if (rv32emu_tb_is_block_terminator(line->decoded[line->count - 1u].opcode)) {
      break;
    }
  }

  line->valid = true;
  return true;
}

static rv32emu_tb_line_t *rv32emu_tb_lookup_or_build(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache,
                                                      uint32_t pc) {
  uint32_t idx;
  rv32emu_tb_line_t *line;

  if (m == NULL || cache == NULL) {
    return NULL;
  }

  idx = rv32emu_tb_index(pc);
  line = &cache->lines[idx];
  if (line->valid && line->start_pc == pc) {
    return line;
  }

  if (!rv32emu_tb_build_line(m, line, pc)) {
    return NULL;
  }
  return line;
}

#if defined(__x86_64__)
static bool rv32emu_tb_get_ready_jit_line(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache,
                                          uint32_t pc, uint64_t budget,
                                          rv32emu_tb_line_t **line_out) {
  rv32emu_tb_line_t *line;
  uint32_t hot_threshold;

  if (m == NULL || cache == NULL || line_out == NULL || budget == 0u) {
    return false;
  }

  line = rv32emu_tb_lookup_or_build(m, cache, pc);
  if (line == NULL || line->start_pc != pc) {
    return false;
  }

  if (!line->jit_valid || line->jit_count == 0u || line->jit_fn == NULL) {
    if (!line->jit_tried) {
      if (line->jit_hotness < 255u) {
        line->jit_hotness++;
      }

      hot_threshold = (uint32_t)cache->jit_hot_threshold;
      if (hot_threshold == 0u) {
        hot_threshold = RV32EMU_JIT_DEFAULT_HOT_THRESHOLD;
      }
      if (line->jit_hotness >= hot_threshold) {
        (void)rv32emu_tb_try_compile_jit(cache, line);
      }
    }
  }

  if (!line->jit_valid || line->jit_count == 0u || line->jit_fn == NULL) {
    return false;
  }
  if ((uint64_t)line->jit_count > budget) {
    return false;
  }

  *line_out = line;
  return true;
}

static rv32emu_tb_line_t *rv32emu_tb_try_cached_chain(rv32emu_tb_cache_t *cache,
                                                       rv32emu_tb_line_t *from, uint32_t next_pc,
                                                       uint64_t budget) {
  uint32_t idx;
  rv32emu_tb_line_t *line;

  if (cache == NULL || from == NULL || !from->jit_chain_valid || from->jit_chain_fn == NULL ||
      from->jit_chain_pc != next_pc || budget == 0u) {
    return NULL;
  }

  idx = rv32emu_tb_index(next_pc);
  line = &cache->lines[idx];
  if (!line->valid || line->start_pc != next_pc || !line->jit_valid || line->jit_count == 0u ||
      line->jit_fn == NULL || line->jit_fn != from->jit_chain_fn ||
      (uint64_t)line->jit_count > budget) {
    from->jit_chain_valid = false;
    from->jit_chain_pc = 0u;
    from->jit_chain_fn = NULL;
    return NULL;
  }

  return line;
}

static void rv32emu_tb_cache_chain(rv32emu_tb_line_t *from, rv32emu_tb_line_t *to) {
  if (from == NULL || to == NULL || to->jit_fn == NULL || to->jit_count == 0u) {
    return;
  }
  from->jit_chain_valid = true;
  from->jit_chain_pc = to->start_pc;
  from->jit_chain_fn = to->jit_fn;
}

static rv32emu_tb_jit_fn_t rv32emu_jit_chain_next(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                                                   rv32emu_tb_line_t *from) {
  rv32emu_tb_cache_t *cache = g_rv32emu_jit_tls_cache;
  rv32emu_tb_line_t *next_line;
  uint32_t next_pc;
  uint64_t budget = g_rv32emu_jit_tls_budget;

  if (m == NULL || cpu == NULL || from == NULL || cache == NULL || budget == 0u) {
    return NULL;
  }
  if (!atomic_load_explicit(&cpu->running, memory_order_acquire)) {
    return NULL;
  }

  if (rv32emu_check_pending_interrupt(m)) {
    g_rv32emu_jit_tls_handled = true;
    return NULL;
  }
  if (!atomic_load_explicit(&cpu->running, memory_order_acquire)) {
    return NULL;
  }

  next_pc = cpu->pc;
  next_line = rv32emu_tb_try_cached_chain(cache, from, next_pc, budget);
  if (next_line == NULL) {
    RV32EMU_JIT_STATS_INC(chain_misses);
    if (!rv32emu_tb_get_ready_jit_line(m, cache, next_pc, budget, &next_line)) {
      return NULL;
    }
    rv32emu_tb_cache_chain(from, next_line);
  } else {
    RV32EMU_JIT_STATS_INC(chain_hits);
  }

  return next_line->jit_fn;
}
#endif

rv32emu_tb_jit_result_t rv32emu_exec_tb_jit(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache,
                                            uint64_t budget) {
#if defined(__x86_64__)
  rv32emu_tb_jit_result_t result = {RV32EMU_TB_JIT_NOPROGRESS, 0u};
  rv32emu_tb_line_t *line;
  uint32_t pc;
  uint32_t chain_cap;
  rv32emu_cpu_t *cpu;
  uint64_t local_budget;
  int retired;
  bool handled;
  bool running_now;
  bool pc_changed;

  if (m == NULL || cache == NULL || budget == 0u) {
    return result;
  }

  cpu = RV32EMU_CPU(m);
  if (cpu == NULL) {
    return result;
  }

  RV32EMU_JIT_STATS_INC(dispatch_calls);

  chain_cap = cache->jit_chain_max_insns;
  if (chain_cap == 0u) {
    chain_cap = RV32EMU_JIT_DEFAULT_CHAIN_MAX_INSNS;
  }

  local_budget = budget;
  if (local_budget > chain_cap) {
    local_budget = chain_cap;
    RV32EMU_JIT_STATS_INC(dispatch_budget_clamped);
  }

  pc = cpu->pc;
  if (!rv32emu_tb_get_ready_jit_line(m, cache, pc, local_budget, &line)) {
    RV32EMU_JIT_STATS_INC(dispatch_no_ready);
    return result;
  }

  g_rv32emu_jit_tls_cache = cache;
  g_rv32emu_jit_tls_budget = local_budget;
  g_rv32emu_jit_tls_total = 0u;
  g_rv32emu_jit_tls_handled = false;

  retired = line->jit_fn(m, cpu);
  handled = g_rv32emu_jit_tls_handled;
  running_now = atomic_load_explicit(&cpu->running, memory_order_acquire);
  pc_changed = (cpu->pc != pc);

  g_rv32emu_jit_tls_cache = NULL;
  g_rv32emu_jit_tls_budget = 0u;
  g_rv32emu_jit_tls_total = 0u;
  g_rv32emu_jit_tls_handled = false;

  cache->active = false;
  if (retired <= 0) {
    /*
     * Avoid spinning forever on handled-no-retire when PC did not move.
     * In that case force caller fallback to interpreter for forward progress.
     */
    if (pc_changed || !running_now) {
      result.status = RV32EMU_TB_JIT_HANDLED_NO_RETIRE;
      RV32EMU_JIT_STATS_INC(dispatch_handled_no_retire);
    } else if (handled) {
      result.status = RV32EMU_TB_JIT_NOPROGRESS;
      RV32EMU_JIT_STATS_INC(dispatch_noprogress);
    } else {
      RV32EMU_JIT_STATS_INC(dispatch_noprogress);
    }
    return result;
  }
  if ((uint64_t)retired > local_budget) {
    retired = (int)local_budget;
  }

  result.status = RV32EMU_TB_JIT_RETIRED;
  result.retired = (uint32_t)retired;
  RV32EMU_JIT_STATS_INC(dispatch_retired_calls);
  RV32EMU_JIT_STATS_ADD(dispatch_retired_insns, result.retired);
  return result;
#else
  rv32emu_tb_jit_result_t result = {RV32EMU_TB_JIT_NOPROGRESS, 0u};
  (void)m;
  (void)cache;
  (void)budget;
  return result;
#endif
}

void rv32emu_jit_stats_reset(void) {
  if (!rv32emu_jit_stats_enabled()) {
    return;
  }

  atomic_store_explicit(&g_rv32emu_jit_stats.dispatch_calls, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.dispatch_no_ready, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.dispatch_budget_clamped, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.dispatch_retired_calls, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.dispatch_retired_insns, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.dispatch_handled_no_retire, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.dispatch_noprogress, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_attempts, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_success, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_prefix_insns, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_prefix_truncated, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_fail_too_short, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_fail_unsupported_prefix, 0u,
                        memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_fail_alloc, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_fail_emit, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.helper_mem_calls, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.helper_cf_calls, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.chain_hits, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.chain_misses, 0u, memory_order_relaxed);
}

void rv32emu_jit_stats_dump(uint64_t executed) {
  uint64_t dispatch_calls;
  uint64_t dispatch_no_ready;
  uint64_t dispatch_budget_clamped;
  uint64_t dispatch_retired_calls;
  uint64_t dispatch_retired_insns;
  uint64_t dispatch_handled_no_retire;
  uint64_t dispatch_noprogress;
  uint64_t compile_attempts;
  uint64_t compile_success;
  uint64_t compile_prefix_insns;
  uint64_t compile_prefix_truncated;
  uint64_t compile_fail_too_short;
  uint64_t compile_fail_unsupported_prefix;
  uint64_t compile_fail_alloc;
  uint64_t compile_fail_emit;
  uint64_t helper_mem_calls;
  uint64_t helper_cf_calls;
  uint64_t chain_hits;
  uint64_t chain_misses;
  uint32_t max_block_insns;
  uint32_t min_prefix_insns;
  uint32_t chain_max_insns;
  uint32_t hot_threshold;
  uint32_t pool_mb;
  double compile_hit_rate = 0.0;
  double dispatch_retire_rate = 0.0;
  double avg_retired_per_call = 0.0;

  if (!rv32emu_jit_stats_enabled()) {
    return;
  }

  dispatch_calls = atomic_load_explicit(&g_rv32emu_jit_stats.dispatch_calls, memory_order_relaxed);
  dispatch_no_ready =
      atomic_load_explicit(&g_rv32emu_jit_stats.dispatch_no_ready, memory_order_relaxed);
  dispatch_budget_clamped =
      atomic_load_explicit(&g_rv32emu_jit_stats.dispatch_budget_clamped, memory_order_relaxed);
  dispatch_retired_calls =
      atomic_load_explicit(&g_rv32emu_jit_stats.dispatch_retired_calls, memory_order_relaxed);
  dispatch_retired_insns =
      atomic_load_explicit(&g_rv32emu_jit_stats.dispatch_retired_insns, memory_order_relaxed);
  dispatch_handled_no_retire =
      atomic_load_explicit(&g_rv32emu_jit_stats.dispatch_handled_no_retire, memory_order_relaxed);
  dispatch_noprogress =
      atomic_load_explicit(&g_rv32emu_jit_stats.dispatch_noprogress, memory_order_relaxed);
  compile_attempts =
      atomic_load_explicit(&g_rv32emu_jit_stats.compile_attempts, memory_order_relaxed);
  compile_success = atomic_load_explicit(&g_rv32emu_jit_stats.compile_success, memory_order_relaxed);
  compile_prefix_insns =
      atomic_load_explicit(&g_rv32emu_jit_stats.compile_prefix_insns, memory_order_relaxed);
  compile_prefix_truncated =
      atomic_load_explicit(&g_rv32emu_jit_stats.compile_prefix_truncated, memory_order_relaxed);
  compile_fail_too_short =
      atomic_load_explicit(&g_rv32emu_jit_stats.compile_fail_too_short, memory_order_relaxed);
  compile_fail_unsupported_prefix =
      atomic_load_explicit(&g_rv32emu_jit_stats.compile_fail_unsupported_prefix,
                           memory_order_relaxed);
  compile_fail_alloc =
      atomic_load_explicit(&g_rv32emu_jit_stats.compile_fail_alloc, memory_order_relaxed);
  compile_fail_emit = atomic_load_explicit(&g_rv32emu_jit_stats.compile_fail_emit, memory_order_relaxed);
  helper_mem_calls =
      atomic_load_explicit(&g_rv32emu_jit_stats.helper_mem_calls, memory_order_relaxed);
  helper_cf_calls = atomic_load_explicit(&g_rv32emu_jit_stats.helper_cf_calls, memory_order_relaxed);
  chain_hits = atomic_load_explicit(&g_rv32emu_jit_stats.chain_hits, memory_order_relaxed);
  chain_misses = atomic_load_explicit(&g_rv32emu_jit_stats.chain_misses, memory_order_relaxed);

  max_block_insns = rv32emu_tb_max_block_insns_from_env();
  min_prefix_insns = rv32emu_tb_min_prefix_insns_from_env();
  chain_max_insns = rv32emu_tb_chain_max_insns_from_env();
  hot_threshold = rv32emu_tb_hot_threshold_from_env();
  pool_mb = rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_POOL_MB",
                                     RV32EMU_JIT_DEFAULT_POOL_MB, 1u, RV32EMU_JIT_MAX_POOL_MB);

  if (compile_attempts != 0u) {
    compile_hit_rate = ((double)compile_success * 100.0) / (double)compile_attempts;
  }
  if (dispatch_calls != 0u) {
    dispatch_retire_rate = ((double)dispatch_retired_calls * 100.0) / (double)dispatch_calls;
  }
  if (dispatch_retired_calls != 0u) {
    avg_retired_per_call = (double)dispatch_retired_insns / (double)dispatch_retired_calls;
  }

  fprintf(stderr,
          "[jit] cfg hot=%" PRIu32 " block_max=%" PRIu32 " min_prefix=%" PRIu32
          " chain_max=%" PRIu32 " pool_mb=%" PRIu32
          " executed=%" PRIu64 "\n",
          hot_threshold, max_block_insns, min_prefix_insns, chain_max_insns, pool_mb, executed);
  fprintf(stderr,
          "[jit] dispatch calls=%" PRIu64 " retired_calls=%" PRIu64 " retired_insns=%" PRIu64
          " retire_rate=%.2f%% avg_retired=%.2f no_ready=%" PRIu64 " handled_no_retire=%" PRIu64
          " noprogress=%" PRIu64 " budget_clamped=%" PRIu64 "\n",
          dispatch_calls, dispatch_retired_calls, dispatch_retired_insns, dispatch_retire_rate,
          avg_retired_per_call, dispatch_no_ready, dispatch_handled_no_retire, dispatch_noprogress,
          dispatch_budget_clamped);
  fprintf(stderr,
          "[jit] compile attempts=%" PRIu64 " success=%" PRIu64 " hit_rate=%.2f%%"
          " prefix_insns=%" PRIu64 " prefix_truncated=%" PRIu64
          " fail_too_short=%" PRIu64 " fail_unsupported_prefix=%" PRIu64
          " fail_alloc=%" PRIu64 " fail_emit=%" PRIu64 "\n",
          compile_attempts, compile_success, compile_hit_rate, compile_prefix_insns,
          compile_prefix_truncated, compile_fail_too_short, compile_fail_unsupported_prefix,
          compile_fail_alloc, compile_fail_emit);
  fprintf(stderr,
          "[jit] helpers mem=%" PRIu64 " cf=%" PRIu64 " chain_hits=%" PRIu64
          " chain_misses=%" PRIu64 "\n",
          helper_mem_calls, helper_cf_calls, chain_hits, chain_misses);
}

rv32emu_tb_block_result_t rv32emu_exec_tb_block(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache,
                                                 uint64_t budget) {
  rv32emu_tb_block_result_t result = {RV32EMU_TB_BLOCK_NOPROGRESS, 0u};
  rv32emu_cpu_t *cpu;
  uint32_t first_pc;

  if (m == NULL || cache == NULL || budget == 0u) {
    return result;
  }

  cpu = RV32EMU_CPU(m);
  if (cpu == NULL) {
    return result;
  }

  first_pc = cpu->pc;

  while (result.retired < budget) {
    rv32emu_tb_line_t *line = NULL;
    uint32_t pc;
    uint8_t index = 0u;

    if (!atomic_load_explicit(&cpu->running, memory_order_acquire)) {
      if (result.retired == 0u) {
        result.status = RV32EMU_TB_BLOCK_HANDLED_NO_RETIRE;
      } else {
        result.status = RV32EMU_TB_BLOCK_RETIRED;
      }
      return result;
    }

    if (rv32emu_check_pending_interrupt(m)) {
      if (result.retired == 0u && (cpu->pc != first_pc ||
                                   !atomic_load_explicit(&cpu->running, memory_order_acquire))) {
        result.status = RV32EMU_TB_BLOCK_HANDLED_NO_RETIRE;
      } else if (result.retired != 0u) {
        result.status = RV32EMU_TB_BLOCK_RETIRED;
      }
      return result;
    }

    pc = cpu->pc;
    if (cache->active) {
      rv32emu_tb_line_t *active = rv32emu_tb_lookup_or_build(m, cache, cache->active_start_pc);
      if (active != NULL && cache->active_index < active->count &&
          active->pcs[cache->active_index] == pc) {
        line = active;
        index = cache->active_index;
      } else {
        cache->active = false;
      }
    }

    if (line == NULL) {
      line = rv32emu_tb_lookup_or_build(m, cache, pc);
      if (line == NULL || line->count == 0u || line->pcs[0] != pc) {
        cache->active = false;
        if (result.retired != 0u) {
          result.status = RV32EMU_TB_BLOCK_RETIRED;
        }
        return result;
      }
      cache->active = true;
      cache->active_start_pc = line->start_pc;
      cache->active_index = 0u;
      index = 0u;
    }

    while (index < line->count && result.retired < budget) {
      if (cpu->pc != line->pcs[index]) {
        cache->active = false;
        if (result.retired != 0u) {
          result.status = RV32EMU_TB_BLOCK_RETIRED;
        }
        return result;
      }

      if (!rv32emu_exec_decoded(m, &line->decoded[index])) {
        cache->active = false;
        if (result.retired != 0u) {
          result.status = RV32EMU_TB_BLOCK_RETIRED;
        } else if (cpu->pc != first_pc ||
                   !atomic_load_explicit(&cpu->running, memory_order_acquire)) {
          result.status = RV32EMU_TB_BLOCK_HANDLED_NO_RETIRE;
        }
        return result;
      }
      result.retired++;

      if (index + 1u < line->count && cpu->pc == line->pcs[index + 1u]) {
        cache->active = true;
        cache->active_start_pc = line->start_pc;
        cache->active_index = (uint8_t)(index + 1u);
        index++;
        continue;
      }

      cache->active = false;
      break;
    }
  }

  if (result.retired != 0u) {
    result.status = RV32EMU_TB_BLOCK_RETIRED;
  }
  return result;
}

bool rv32emu_exec_one_tb(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache) {
  rv32emu_tb_line_t *line;
  uint32_t pc;
  uint8_t index;

  if (m == NULL || cache == NULL) {
    return false;
  }

  pc = RV32EMU_CPU(m)->pc;
  line = NULL;
  index = 0u;

  if (cache->active) {
    rv32emu_tb_line_t *active = rv32emu_tb_lookup_or_build(m, cache, cache->active_start_pc);
    if (active != NULL && cache->active_index < active->count &&
        active->pcs[cache->active_index] == pc) {
      line = active;
      index = cache->active_index;
    } else {
      cache->active = false;
    }
  }

  if (line == NULL) {
    line = rv32emu_tb_lookup_or_build(m, cache, pc);
    if (line == NULL || line->count == 0u || line->pcs[0] != pc) {
      cache->active = false;
      return false;
    }
    cache->active = true;
    cache->active_start_pc = line->start_pc;
    cache->active_index = 0u;
    index = 0u;
  }

  if (!rv32emu_exec_decoded(m, &line->decoded[index])) {
    cache->active = false;
    return false;
  }

  if (index + 1u < line->count && RV32EMU_CPU(m)->pc == line->pcs[index + 1u]) {
    cache->active = true;
    cache->active_start_pc = line->start_pc;
    cache->active_index = (uint8_t)(index + 1u);
  } else {
    cache->active = false;
  }

  return true;
}
