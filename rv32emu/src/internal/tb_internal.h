#ifndef RV32EMU_INTERNAL_TB_INTERNAL_H
#define RV32EMU_INTERNAL_TB_INTERNAL_H

#include "rv32emu_tb.h"

#include <stdatomic.h>

#define RV32EMU_JIT_DEFAULT_HOT_THRESHOLD 3u
#define RV32EMU_JIT_DEFAULT_MAX_INSNS_PER_BLOCK 8u
#define RV32EMU_JIT_DEFAULT_MIN_PREFIX_INSNS 4u
#define RV32EMU_JIT_DEFAULT_CHAIN_MAX_INSNS 64u
#define RV32EMU_JIT_MAX_CHAIN_LIMIT 4096u
#define RV32EMU_JIT_DEFAULT_POOL_MB 4u
#define RV32EMU_JIT_MAX_POOL_MB 1024u
#define RV32EMU_JIT_DEFAULT_ASYNC_QUEUE 1024u
#define RV32EMU_JIT_MAX_ASYNC_QUEUE 16384u
#define RV32EMU_JIT_DEFAULT_ASYNC_WORKERS 2u
#define RV32EMU_JIT_MAX_ASYNC_WORKERS 8u
#define RV32EMU_JIT_DEFAULT_ASYNC_BUSY_PCT 75u
#define RV32EMU_JIT_DEFAULT_ASYNC_HOT_DISCOUNT 1u
#define RV32EMU_JIT_DEFAULT_ASYNC_HOT_BONUS 0u
#define RV32EMU_JIT_DEFAULT_ASYNC_DRAIN_INTERVAL 8u
#define RV32EMU_JIT_TEMPLATE_CACHE_LINES 1024u
#define RV32EMU_JIT_STRUCT_TEMPLATE_LINES 1024u
#define RV32EMU_JIT_MAX_PC_RELOCS (RV32EMU_TB_MAX_INSNS + 8u)

#define RV32EMU_JIT_STATE_NONE 0u
#define RV32EMU_JIT_STATE_QUEUED 1u
#define RV32EMU_JIT_STATE_READY 2u
#define RV32EMU_JIT_STATE_FAILED 3u

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
  atomic_uint_fast64_t compile_template_hits;
  atomic_uint_fast64_t compile_template_stores;
  atomic_uint_fast64_t compile_struct_hits;
  atomic_uint_fast64_t compile_struct_stores;
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
  atomic_uint_fast64_t async_jobs_enqueued;
  atomic_uint_fast64_t async_jobs_dropped;
  atomic_uint_fast64_t async_jobs_compiled;
  atomic_uint_fast64_t async_results_applied;
  atomic_uint_fast64_t async_results_stale;
  atomic_uint_fast64_t async_template_applied;
  atomic_uint_fast64_t async_applied_direct;
  atomic_uint_fast64_t async_applied_recycled;
  atomic_uint_fast64_t async_stale_nonportable;
  atomic_uint_fast64_t async_stale_not_success;
  atomic_uint_fast64_t async_stale_lookup_miss;
  atomic_uint_fast64_t async_stale_state_mismatch;
  atomic_uint_fast64_t async_stale_sig_mismatch;
  atomic_uint_fast64_t async_evict_queued;
  atomic_uint_fast64_t async_sync_fallbacks;
} rv32emu_jit_stats_t;

extern rv32emu_jit_stats_t g_rv32emu_jit_stats;
extern atomic_uint_fast32_t g_rv32emu_jit_stats_mode;
bool rv32emu_jit_stats_enabled(void);

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

bool rv32emu_tb_env_bool(const char *name, bool default_value);
uint32_t rv32emu_tb_u32_from_env(const char *name, uint32_t default_value, uint32_t min_value,
                                 uint32_t max_value);

uint8_t rv32emu_tb_hot_threshold_from_env(void);
uint8_t rv32emu_tb_max_block_insns_from_env(void);
uint8_t rv32emu_tb_min_prefix_insns_from_env(void);
uint32_t rv32emu_tb_chain_max_insns_from_env(void);
size_t rv32emu_tb_jit_pool_size_from_env(void);

bool rv32emu_tb_jit_async_enabled_from_env(void);
uint32_t rv32emu_tb_jit_async_workers_from_env(void);
uint32_t rv32emu_tb_jit_async_queue_from_env(void);
bool rv32emu_tb_jit_async_foreground_sync_from_env(void);
bool rv32emu_tb_jit_async_prefetch_enabled_from_env(void);
bool rv32emu_tb_jit_async_allow_helpers_from_env(void);
bool rv32emu_tb_jit_async_redecode_helpers_from_env(void);
bool rv32emu_tb_jit_async_recycle_from_env(void);
bool rv32emu_tb_jit_template_fast_apply_from_env(void);
uint8_t rv32emu_tb_jit_async_sync_fallback_spins_from_env(void);
uint8_t rv32emu_tb_jit_async_busy_pct_from_env(void);
uint8_t rv32emu_tb_jit_async_hot_discount_from_env(void);
uint8_t rv32emu_tb_jit_async_hot_bonus_from_env(void);

uint32_t rv32emu_tb_next_jit_generation(void);
rv32emu_tb_line_t *rv32emu_tb_find_cached_line(rv32emu_tb_cache_t *cache, uint32_t pc);

#endif
