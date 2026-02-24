#include "rv32emu_tb.h"
#include "../internal/tb_internal.h"

#include <stdlib.h>
#include <stdatomic.h>

bool rv32emu_tb_env_bool(const char *name, bool default_value) {
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

uint32_t rv32emu_tb_u32_from_env(const char *name, uint32_t default_value, uint32_t min_value,
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

uint8_t rv32emu_tb_hot_threshold_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_HOT",
                                          RV32EMU_JIT_DEFAULT_HOT_THRESHOLD, 1u, 255u);
}

uint8_t rv32emu_tb_max_block_insns_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_MAX_BLOCK_INSNS",
                                          RV32EMU_JIT_DEFAULT_MAX_INSNS_PER_BLOCK, 1u,
                                          RV32EMU_TB_MAX_INSNS);
}

uint8_t rv32emu_tb_min_prefix_insns_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_MIN_PREFIX_INSNS",
                                          RV32EMU_JIT_DEFAULT_MIN_PREFIX_INSNS, 1u,
                                          RV32EMU_TB_MAX_INSNS);
}

uint32_t rv32emu_tb_chain_max_insns_from_env(void) {
  return rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_CHAIN_MAX_INSNS",
                                 RV32EMU_JIT_DEFAULT_CHAIN_MAX_INSNS, 1u,
                                 RV32EMU_JIT_MAX_CHAIN_LIMIT);
}

size_t rv32emu_tb_jit_pool_size_from_env(void) {
  uint32_t pool_mb = rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_POOL_MB",
                                             RV32EMU_JIT_DEFAULT_POOL_MB, 1u,
                                             RV32EMU_JIT_MAX_POOL_MB);
  return (size_t)pool_mb * 1024u * 1024u;
}

bool rv32emu_tb_jit_async_enabled_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC", false);
}

uint32_t rv32emu_tb_jit_async_workers_from_env(void) {
  return rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_WORKERS",
                                 RV32EMU_JIT_DEFAULT_ASYNC_WORKERS, 1u,
                                 RV32EMU_JIT_MAX_ASYNC_WORKERS);
}

uint32_t rv32emu_tb_jit_async_queue_from_env(void) {
  return rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_QUEUE",
                                 RV32EMU_JIT_DEFAULT_ASYNC_QUEUE, 64u, RV32EMU_JIT_MAX_ASYNC_QUEUE);
}

bool rv32emu_tb_jit_async_foreground_sync_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC_FOREGROUND_SYNC", true);
}

bool rv32emu_tb_jit_async_prefetch_enabled_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC_PREFETCH", false);
}

bool rv32emu_tb_jit_async_allow_helpers_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC_ALLOW_HELPERS", true);
}

bool rv32emu_tb_jit_async_redecode_helpers_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC_REDECODE_HELPERS", false);
}

bool rv32emu_tb_jit_async_recycle_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC_RECYCLE", false);
}

bool rv32emu_tb_jit_template_fast_apply_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_TEMPLATE_FAST_APPLY", false);
}

uint8_t rv32emu_tb_jit_async_sync_fallback_spins_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_SYNC_FALLBACK_SPINS",
                                          8u, 0u, 255u);
}

uint8_t rv32emu_tb_jit_async_busy_pct_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_BUSY_PCT",
                                          RV32EMU_JIT_DEFAULT_ASYNC_BUSY_PCT, 10u, 100u);
}

uint8_t rv32emu_tb_jit_async_hot_discount_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_HOT_DISCOUNT",
                                          RV32EMU_JIT_DEFAULT_ASYNC_HOT_DISCOUNT, 0u, 254u);
}

uint8_t rv32emu_tb_jit_async_hot_bonus_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_HOT_BONUS",
                                          RV32EMU_JIT_DEFAULT_ASYNC_HOT_BONUS, 0u, 255u);
}

static atomic_uint_fast32_t g_rv32emu_jit_generation_seed = ATOMIC_VAR_INIT(1u);

uint32_t rv32emu_tb_next_jit_generation(void) {
  uint_fast32_t gen =
      atomic_fetch_add_explicit(&g_rv32emu_jit_generation_seed, 1u, memory_order_relaxed);
  if (gen == UINT32_MAX) {
    atomic_store_explicit(&g_rv32emu_jit_generation_seed, 1u, memory_order_relaxed);
    gen = atomic_fetch_add_explicit(&g_rv32emu_jit_generation_seed, 1u, memory_order_relaxed);
  }
  return (uint32_t)gen;
}
