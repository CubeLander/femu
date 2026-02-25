#ifndef RV32EMU_INTERNAL_TB_STATS_INTERNAL_H
#define RV32EMU_INTERNAL_TB_STATS_INTERNAL_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

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

#endif
