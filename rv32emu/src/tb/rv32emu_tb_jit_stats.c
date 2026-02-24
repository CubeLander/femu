#include "rv32emu_tb.h"
#include "../internal/tb_internal.h"
#include "../internal/tb_jit_internal.h"

#include <inttypes.h>
#include <stdio.h>

rv32emu_jit_stats_t g_rv32emu_jit_stats = {
    .dispatch_calls = ATOMIC_VAR_INIT(0u),
    .dispatch_no_ready = ATOMIC_VAR_INIT(0u),
    .dispatch_budget_clamped = ATOMIC_VAR_INIT(0u),
    .dispatch_retired_calls = ATOMIC_VAR_INIT(0u),
    .dispatch_retired_insns = ATOMIC_VAR_INIT(0u),
    .dispatch_handled_no_retire = ATOMIC_VAR_INIT(0u),
    .dispatch_noprogress = ATOMIC_VAR_INIT(0u),
    .compile_attempts = ATOMIC_VAR_INIT(0u),
    .compile_success = ATOMIC_VAR_INIT(0u),
    .compile_template_hits = ATOMIC_VAR_INIT(0u),
    .compile_template_stores = ATOMIC_VAR_INIT(0u),
    .compile_struct_hits = ATOMIC_VAR_INIT(0u),
    .compile_struct_stores = ATOMIC_VAR_INIT(0u),
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
    .async_jobs_enqueued = ATOMIC_VAR_INIT(0u),
    .async_jobs_dropped = ATOMIC_VAR_INIT(0u),
    .async_jobs_compiled = ATOMIC_VAR_INIT(0u),
    .async_results_applied = ATOMIC_VAR_INIT(0u),
    .async_results_stale = ATOMIC_VAR_INIT(0u),
    .async_template_applied = ATOMIC_VAR_INIT(0u),
    .async_applied_direct = ATOMIC_VAR_INIT(0u),
    .async_applied_recycled = ATOMIC_VAR_INIT(0u),
    .async_stale_nonportable = ATOMIC_VAR_INIT(0u),
    .async_stale_not_success = ATOMIC_VAR_INIT(0u),
    .async_stale_lookup_miss = ATOMIC_VAR_INIT(0u),
    .async_stale_state_mismatch = ATOMIC_VAR_INIT(0u),
    .async_stale_sig_mismatch = ATOMIC_VAR_INIT(0u),
    .async_evict_queued = ATOMIC_VAR_INIT(0u),
    .async_sync_fallbacks = ATOMIC_VAR_INIT(0u),
};

atomic_uint_fast32_t g_rv32emu_jit_stats_mode = ATOMIC_VAR_INIT(0u);

bool rv32emu_jit_stats_enabled(void) {
  uint_fast32_t mode = atomic_load_explicit(&g_rv32emu_jit_stats_mode, memory_order_acquire);

  if (mode == 0u) {
    uint_fast32_t configured =
        rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_STATS", false) ? 2u : 1u;
    atomic_store_explicit(&g_rv32emu_jit_stats_mode, configured, memory_order_release);
    mode = configured;
  }

  return mode == 2u;
}

void rv32emu_jit_stats_inc_event(rv32emu_jit_stat_event_t event) {
  switch (event) {
  case RV32EMU_JIT_STAT_ASYNC_JOBS_ENQUEUED:
    RV32EMU_JIT_STATS_INC(async_jobs_enqueued);
    break;
  case RV32EMU_JIT_STAT_ASYNC_JOBS_DROPPED:
    RV32EMU_JIT_STATS_INC(async_jobs_dropped);
    break;
  case RV32EMU_JIT_STAT_ASYNC_JOBS_COMPILED:
    RV32EMU_JIT_STATS_INC(async_jobs_compiled);
    break;
  case RV32EMU_JIT_STAT_ASYNC_RESULTS_APPLIED:
    RV32EMU_JIT_STATS_INC(async_results_applied);
    break;
  case RV32EMU_JIT_STAT_ASYNC_APPLIED_DIRECT:
    RV32EMU_JIT_STATS_INC(async_applied_direct);
    break;
  case RV32EMU_JIT_STAT_ASYNC_APPLIED_RECYCLED:
    RV32EMU_JIT_STATS_INC(async_applied_recycled);
    break;
  case RV32EMU_JIT_STAT_ASYNC_STALE_NONPORTABLE:
    RV32EMU_JIT_STATS_INC(async_stale_nonportable);
    break;
  case RV32EMU_JIT_STAT_ASYNC_STALE_NOT_SUCCESS:
    RV32EMU_JIT_STATS_INC(async_stale_not_success);
    break;
  case RV32EMU_JIT_STAT_ASYNC_STALE_LOOKUP_MISS:
    RV32EMU_JIT_STATS_INC(async_stale_lookup_miss);
    break;
  case RV32EMU_JIT_STAT_ASYNC_STALE_STATE_MISMATCH:
    RV32EMU_JIT_STATS_INC(async_stale_state_mismatch);
    break;
  case RV32EMU_JIT_STAT_ASYNC_STALE_SIG_MISMATCH:
    RV32EMU_JIT_STATS_INC(async_stale_sig_mismatch);
    break;
  case RV32EMU_JIT_STAT_ASYNC_RESULTS_STALE:
    RV32EMU_JIT_STATS_INC(async_results_stale);
    break;
  case RV32EMU_JIT_STAT_ASYNC_SYNC_FALLBACKS:
    RV32EMU_JIT_STATS_INC(async_sync_fallbacks);
    break;
  case RV32EMU_JIT_STAT_ASYNC_TEMPLATE_APPLIED:
    RV32EMU_JIT_STATS_INC(async_template_applied);
    break;
  case RV32EMU_JIT_STAT_COMPILE_FAIL_ALLOC:
    RV32EMU_JIT_STATS_INC(compile_fail_alloc);
    break;
  case RV32EMU_JIT_STAT_COMPILE_FAIL_UNSUPPORTED_PREFIX:
    RV32EMU_JIT_STATS_INC(compile_fail_unsupported_prefix);
    break;
  case RV32EMU_JIT_STAT_COMPILE_TEMPLATE_HIT:
    RV32EMU_JIT_STATS_INC(compile_template_hits);
    break;
  case RV32EMU_JIT_STAT_COMPILE_TEMPLATE_STORE:
    RV32EMU_JIT_STATS_INC(compile_template_stores);
    break;
  case RV32EMU_JIT_STAT_COMPILE_STRUCT_HIT:
    RV32EMU_JIT_STATS_INC(compile_struct_hits);
    break;
  case RV32EMU_JIT_STAT_COMPILE_STRUCT_STORE:
    RV32EMU_JIT_STATS_INC(compile_struct_stores);
    break;
  case RV32EMU_JIT_STAT_COMPILE_ATTEMPTS:
    RV32EMU_JIT_STATS_INC(compile_attempts);
    break;
  case RV32EMU_JIT_STAT_COMPILE_FAIL_TOO_SHORT:
    RV32EMU_JIT_STATS_INC(compile_fail_too_short);
    break;
  case RV32EMU_JIT_STAT_COMPILE_FAIL_EMIT:
    RV32EMU_JIT_STATS_INC(compile_fail_emit);
    break;
  case RV32EMU_JIT_STAT_COMPILE_SUCCESS:
    RV32EMU_JIT_STATS_INC(compile_success);
    break;
  case RV32EMU_JIT_STAT_COMPILE_PREFIX_TRUNCATED:
    RV32EMU_JIT_STATS_INC(compile_prefix_truncated);
    break;
  default:
    break;
  }
}

void rv32emu_jit_stats_add_compile_prefix_insns(uint32_t value) {
  RV32EMU_JIT_STATS_ADD(compile_prefix_insns, value);
}

void rv32emu_jit_stats_inc_helper_mem_calls(void) {
  RV32EMU_JIT_STATS_INC(helper_mem_calls);
}

void rv32emu_jit_stats_inc_helper_cf_calls(void) {
  RV32EMU_JIT_STATS_INC(helper_cf_calls);
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
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_template_hits, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_template_stores, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_struct_hits, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.compile_struct_stores, 0u, memory_order_relaxed);
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
  atomic_store_explicit(&g_rv32emu_jit_stats.async_jobs_enqueued, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_jobs_dropped, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_jobs_compiled, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_results_applied, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_results_stale, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_template_applied, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_applied_direct, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_applied_recycled, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_stale_nonportable, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_stale_not_success, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_stale_lookup_miss, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_stale_state_mismatch, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_stale_sig_mismatch, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_evict_queued, 0u, memory_order_relaxed);
  atomic_store_explicit(&g_rv32emu_jit_stats.async_sync_fallbacks, 0u, memory_order_relaxed);
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
  uint64_t compile_template_hits;
  uint64_t compile_template_stores;
  uint64_t compile_struct_hits;
  uint64_t compile_struct_stores;
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
  uint64_t async_jobs_enqueued;
  uint64_t async_jobs_dropped;
  uint64_t async_jobs_compiled;
  uint64_t async_results_applied;
  uint64_t async_results_stale;
  uint64_t async_template_applied;
  uint64_t async_applied_direct;
  uint64_t async_applied_recycled;
  uint64_t async_stale_nonportable;
  uint64_t async_stale_not_success;
  uint64_t async_stale_lookup_miss;
  uint64_t async_stale_state_mismatch;
  uint64_t async_stale_sig_mismatch;
  uint64_t async_evict_queued;
  uint64_t async_sync_fallbacks;
  uint32_t max_block_insns;
  uint32_t min_prefix_insns;
  uint32_t chain_max_insns;
  uint32_t async_foreground_sync;
  uint32_t async_prefetch;
  uint32_t async_recycle;
  uint32_t template_fast_apply;
  uint32_t async_sync_fallback_spins;
  uint32_t async_busy_pct;
  uint32_t async_hot_discount;
  uint32_t async_hot_bonus;
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
  compile_template_hits =
      atomic_load_explicit(&g_rv32emu_jit_stats.compile_template_hits, memory_order_relaxed);
  compile_template_stores =
      atomic_load_explicit(&g_rv32emu_jit_stats.compile_template_stores, memory_order_relaxed);
  compile_struct_hits =
      atomic_load_explicit(&g_rv32emu_jit_stats.compile_struct_hits, memory_order_relaxed);
  compile_struct_stores =
      atomic_load_explicit(&g_rv32emu_jit_stats.compile_struct_stores, memory_order_relaxed);
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
  async_jobs_enqueued =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_jobs_enqueued, memory_order_relaxed);
  async_jobs_dropped =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_jobs_dropped, memory_order_relaxed);
  async_jobs_compiled =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_jobs_compiled, memory_order_relaxed);
  async_results_applied =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_results_applied, memory_order_relaxed);
  async_results_stale =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_results_stale, memory_order_relaxed);
  async_template_applied =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_template_applied, memory_order_relaxed);
  async_applied_direct =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_applied_direct, memory_order_relaxed);
  async_applied_recycled =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_applied_recycled, memory_order_relaxed);
  async_stale_nonportable =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_stale_nonportable, memory_order_relaxed);
  async_stale_not_success =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_stale_not_success, memory_order_relaxed);
  async_stale_lookup_miss =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_stale_lookup_miss, memory_order_relaxed);
  async_stale_state_mismatch =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_stale_state_mismatch, memory_order_relaxed);
  async_stale_sig_mismatch =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_stale_sig_mismatch, memory_order_relaxed);
  async_evict_queued =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_evict_queued, memory_order_relaxed);
  async_sync_fallbacks =
      atomic_load_explicit(&g_rv32emu_jit_stats.async_sync_fallbacks, memory_order_relaxed);

  max_block_insns = rv32emu_tb_max_block_insns_from_env();
  min_prefix_insns = rv32emu_tb_min_prefix_insns_from_env();
  chain_max_insns = rv32emu_tb_chain_max_insns_from_env();
  async_foreground_sync = rv32emu_tb_jit_async_foreground_sync_from_env() ? 1u : 0u;
  async_prefetch = rv32emu_tb_jit_async_prefetch_enabled_from_env() ? 1u : 0u;
  async_recycle = rv32emu_tb_jit_async_recycle_from_env() ? 1u : 0u;
  template_fast_apply = rv32emu_tb_jit_template_fast_apply_from_env() ? 1u : 0u;
  async_sync_fallback_spins = rv32emu_tb_jit_async_sync_fallback_spins_from_env();
  async_busy_pct = rv32emu_tb_jit_async_busy_pct_from_env();
  async_hot_discount = rv32emu_tb_jit_async_hot_discount_from_env();
  async_hot_bonus = rv32emu_tb_jit_async_hot_bonus_from_env();
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
          " chain_max=%" PRIu32 " async_fg_sync=%" PRIu32 " async_prefetch=%" PRIu32
          " async_recycle=%" PRIu32 " template_fast_apply=%" PRIu32
          " async_sync_fallback=%" PRIu32 " async_busy_pct=%" PRIu32
          " async_hot_discount=%" PRIu32 " async_hot_bonus=%" PRIu32 " pool_mb=%" PRIu32
          " executed=%" PRIu64 "\n",
          hot_threshold, max_block_insns, min_prefix_insns, chain_max_insns, async_foreground_sync,
          async_prefetch, async_recycle, template_fast_apply, async_sync_fallback_spins,
          async_busy_pct, async_hot_discount, async_hot_bonus, pool_mb, executed);
  fprintf(stderr,
          "[jit] dispatch calls=%" PRIu64 " retired_calls=%" PRIu64 " retired_insns=%" PRIu64
          " retire_rate=%.2f%% avg_retired=%.2f no_ready=%" PRIu64 " handled_no_retire=%" PRIu64
          " noprogress=%" PRIu64 " budget_clamped=%" PRIu64 "\n",
          dispatch_calls, dispatch_retired_calls, dispatch_retired_insns, dispatch_retire_rate,
          avg_retired_per_call, dispatch_no_ready, dispatch_handled_no_retire, dispatch_noprogress,
          dispatch_budget_clamped);
  fprintf(stderr,
          "[jit] compile attempts=%" PRIu64 " success=%" PRIu64 " hit_rate=%.2f%%"
          " template_hits=%" PRIu64 " template_stores=%" PRIu64
          " struct_hits=%" PRIu64 " struct_stores=%" PRIu64
          " prefix_insns=%" PRIu64 " prefix_truncated=%" PRIu64
          " fail_too_short=%" PRIu64 " fail_unsupported_prefix=%" PRIu64
          " fail_alloc=%" PRIu64 " fail_emit=%" PRIu64 "\n",
          compile_attempts, compile_success, compile_hit_rate, compile_template_hits,
          compile_template_stores, compile_struct_hits, compile_struct_stores, compile_prefix_insns,
          compile_prefix_truncated,
          compile_fail_too_short, compile_fail_unsupported_prefix, compile_fail_alloc,
          compile_fail_emit);
  fprintf(stderr,
          "[jit] helpers mem=%" PRIu64 " cf=%" PRIu64 " chain_hits=%" PRIu64
          " chain_misses=%" PRIu64 "\n",
          helper_mem_calls, helper_cf_calls, chain_hits, chain_misses);
  fprintf(stderr,
          "[jit] async enqueued=%" PRIu64 " dropped=%" PRIu64 " compiled=%" PRIu64
          " applied=%" PRIu64 " stale=%" PRIu64 " template_applied=%" PRIu64
          " sync_fallbacks=%" PRIu64 "\n",
          async_jobs_enqueued, async_jobs_dropped, async_jobs_compiled, async_results_applied,
          async_results_stale, async_template_applied, async_sync_fallbacks);
  fprintf(stderr,
          "[jit] async detail applied_direct=%" PRIu64 " applied_recycled=%" PRIu64
          " stale_nonportable=%" PRIu64 " stale_not_success=%" PRIu64
          " stale_lookup_miss=%" PRIu64 " stale_state_mismatch=%" PRIu64
          " stale_sig_mismatch=%" PRIu64 " evict_queued=%" PRIu64 "\n",
          async_applied_direct, async_applied_recycled, async_stale_nonportable,
          async_stale_not_success, async_stale_lookup_miss, async_stale_state_mismatch,
          async_stale_sig_mismatch, async_evict_queued);
}
