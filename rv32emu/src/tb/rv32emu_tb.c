#include "rv32emu_tb.h"
#include "../internal/tb_internal.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__x86_64__)
#include <pthread.h>
#include "../internal/tb_jit_internal.h"
#include <sys/mman.h>
#endif

bool rv32emu_exec_decoded(rv32emu_machine_t *m, const rv32emu_decoded_insn_t *decoded);

_Static_assert((RV32EMU_JIT_TEMPLATE_CACHE_LINES &
                (RV32EMU_JIT_TEMPLATE_CACHE_LINES - 1u)) == 0u,
               "template cache lines must be a power of two");

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

static inline uint32_t rv32emu_tb_slot(uint32_t set_idx, uint32_t way) {
  return set_idx * RV32EMU_TB_WAYS + way;
}

static uint8_t rv32emu_tb_line_evict_priority(const rv32emu_tb_line_t *line) {
  if (line == NULL || !line->valid) {
    return 255u;
  }

  switch (line->jit_state) {
  case RV32EMU_JIT_STATE_FAILED:
    return 5u;
  case RV32EMU_JIT_STATE_NONE:
    return 4u;
  case RV32EMU_JIT_STATE_READY:
    return 3u;
  case RV32EMU_JIT_STATE_QUEUED:
    return 1u;
  default:
    return 2u;
  }
}

static rv32emu_tb_line_t *rv32emu_tb_find_cached_line(rv32emu_tb_cache_t *cache, uint32_t pc) {
  uint32_t set_idx;

  if (cache == NULL) {
    return NULL;
  }

  set_idx = rv32emu_tb_index(pc);
  for (uint32_t way = 0u; way < RV32EMU_TB_WAYS; way++) {
    rv32emu_tb_line_t *line = &cache->lines[rv32emu_tb_slot(set_idx, way)];
    if (line->valid && line->start_pc == pc) {
      return line;
    }
  }

  return NULL;
}

static rv32emu_tb_line_t *rv32emu_tb_pick_victim_line(rv32emu_tb_cache_t *cache, uint32_t set_idx) {
  rv32emu_tb_line_t *best_line = NULL;
  uint8_t best_prio = 0u;
  uint8_t best_hotness = UINT8_MAX;

  if (cache == NULL || set_idx >= RV32EMU_TB_LINES) {
    return NULL;
  }

  for (uint32_t way = 0u; way < RV32EMU_TB_WAYS; way++) {
    rv32emu_tb_line_t *line = &cache->lines[rv32emu_tb_slot(set_idx, way)];
    uint8_t prio;

    if (!line->valid) {
      return line;
    }
    prio = rv32emu_tb_line_evict_priority(line);
    if (best_line == NULL || prio > best_prio ||
        (prio == best_prio && line->jit_hotness <= best_hotness)) {
      best_line = line;
      best_prio = prio;
      best_hotness = line->jit_hotness;
    }
  }

  return best_line;
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

static bool rv32emu_tb_jit_async_enabled_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC", false);
}

static uint32_t rv32emu_tb_jit_async_workers_from_env(void) {
  return rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_WORKERS",
                                 RV32EMU_JIT_DEFAULT_ASYNC_WORKERS, 1u,
                                 RV32EMU_JIT_MAX_ASYNC_WORKERS);
}

static uint32_t rv32emu_tb_jit_async_queue_from_env(void) {
  return rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_QUEUE",
                                 RV32EMU_JIT_DEFAULT_ASYNC_QUEUE, 64u, RV32EMU_JIT_MAX_ASYNC_QUEUE);
}

static bool rv32emu_tb_jit_async_foreground_sync_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC_FOREGROUND_SYNC", true);
}

static bool rv32emu_tb_jit_async_prefetch_enabled_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC_PREFETCH", false);
}

static bool rv32emu_tb_jit_async_allow_helpers_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC_ALLOW_HELPERS", true);
}

static bool rv32emu_tb_jit_async_redecode_helpers_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC_REDECODE_HELPERS", false);
}

static bool rv32emu_tb_jit_async_recycle_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_ASYNC_RECYCLE", false);
}

static bool rv32emu_tb_jit_template_fast_apply_from_env(void) {
  return rv32emu_tb_env_bool("RV32EMU_EXPERIMENTAL_JIT_TEMPLATE_FAST_APPLY", false);
}

static uint8_t rv32emu_tb_jit_async_sync_fallback_spins_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_SYNC_FALLBACK_SPINS",
                                          8u, 0u, 255u);
}

static uint8_t rv32emu_tb_jit_async_busy_pct_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_BUSY_PCT",
                                          RV32EMU_JIT_DEFAULT_ASYNC_BUSY_PCT, 10u, 100u);
}

static uint8_t rv32emu_tb_jit_async_hot_discount_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_HOT_DISCOUNT",
                                          RV32EMU_JIT_DEFAULT_ASYNC_HOT_DISCOUNT, 0u, 254u);
}

static uint8_t rv32emu_tb_jit_async_hot_bonus_from_env(void) {
  return (uint8_t)rv32emu_tb_u32_from_env("RV32EMU_EXPERIMENTAL_JIT_ASYNC_HOT_BONUS",
                                          RV32EMU_JIT_DEFAULT_ASYNC_HOT_BONUS, 0u, 255u);
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

static atomic_uint_fast32_t g_rv32emu_jit_stats_mode = ATOMIC_VAR_INIT(0u);
static atomic_uint_fast32_t g_rv32emu_jit_generation_seed = ATOMIC_VAR_INIT(1u);

static uint32_t rv32emu_tb_next_jit_generation(void) {
  uint_fast32_t gen =
      atomic_fetch_add_explicit(&g_rv32emu_jit_generation_seed, 1u, memory_order_relaxed);
  if (gen == UINT32_MAX) {
    atomic_store_explicit(&g_rv32emu_jit_generation_seed, 1u, memory_order_relaxed);
    gen = atomic_fetch_add_explicit(&g_rv32emu_jit_generation_seed, 1u, memory_order_relaxed);
  }
  return (uint32_t)gen;
}

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

#if defined(__x86_64__)
bool rv32emu_tb_async_env_enabled(void) {
  return rv32emu_tb_jit_async_enabled_from_env();
}

uint32_t rv32emu_tb_async_env_workers(void) {
  return rv32emu_tb_jit_async_workers_from_env();
}

uint32_t rv32emu_tb_async_env_queue(void) {
  return rv32emu_tb_jit_async_queue_from_env();
}

size_t rv32emu_tb_jit_pool_size_public(void) {
  return rv32emu_tb_jit_pool_size_from_env();
}

uint32_t rv32emu_tb_next_jit_generation_public(void) {
  return rv32emu_tb_next_jit_generation();
}

rv32emu_tb_line_t *rv32emu_tb_find_cached_line_public(rv32emu_tb_cache_t *cache, uint32_t pc) {
  return rv32emu_tb_find_cached_line(cache, pc);
}

bool rv32emu_jit_insn_supported_public(const rv32emu_decoded_insn_t *d) {
  return rv32emu_jit_insn_supported_query(d);
}

bool rv32emu_jit_pool_is_exhausted_public(void) {
  return rv32emu_jit_pool_is_exhausted();
}

uint64_t rv32emu_tb_prefix_signature_public(const rv32emu_decoded_insn_t *decoded,
                                            const uint32_t *pcs, uint8_t count) {
  return rv32emu_tb_prefix_signature(decoded, pcs, count);
}

bool rv32emu_tb_jit_template_key_public(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                        uint8_t count, uint8_t max_jit_insns,
                                        uint8_t min_prefix_insns, uint8_t *jit_count_out,
                                        uint64_t *prefix_sig_out) {
  return rv32emu_tb_jit_template_key(decoded, pcs, count, max_jit_insns, min_prefix_insns,
                                     jit_count_out, prefix_sig_out);
}

bool rv32emu_jit_template_lookup_public(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                        uint8_t jit_count, uint64_t prefix_sig,
                                        rv32emu_jit_compiled_artifact_t *artifact_out) {
  return rv32emu_jit_template_lookup(decoded, pcs, jit_count, prefix_sig, artifact_out);
}

void rv32emu_jit_template_store_public(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                       uint8_t jit_count, uint64_t prefix_sig,
                                       const rv32emu_jit_compiled_artifact_t *artifact) {
  rv32emu_jit_template_store(decoded, pcs, jit_count, prefix_sig, artifact);
}

bool rv32emu_jit_struct_template_lookup_public(const rv32emu_decoded_insn_t *decoded,
                                               uint8_t jit_count, uint32_t start_pc,
                                               rv32emu_jit_compiled_artifact_t *artifact_out) {
  return rv32emu_jit_struct_template_lookup(decoded, jit_count, start_pc, artifact_out);
}

void rv32emu_jit_struct_template_store_public(const rv32emu_decoded_insn_t *decoded,
                                              uint8_t jit_count,
                                              const rv32emu_jit_compiled_artifact_t *artifact) {
  rv32emu_jit_struct_template_store(decoded, jit_count, artifact);
}

bool rv32emu_tb_compile_jit_from_snapshot_public(const rv32emu_decoded_insn_t *decoded,
                                                 const uint32_t *pcs, uint8_t count,
                                                 rv32emu_tb_line_t *line_for_chain,
                                                 uint32_t chain_from_pc,
                                                 uint8_t max_jit_insns,
                                                 uint8_t min_prefix_insns,
                                                 rv32emu_jit_compiled_artifact_t *artifact_out) {
  return rv32emu_tb_compile_jit_from_snapshot(decoded, pcs, count, line_for_chain, chain_from_pc,
                                              max_jit_insns, min_prefix_insns, artifact_out);
}

void rv32emu_tb_line_apply_jit_public(rv32emu_tb_line_t *line,
                                      const rv32emu_jit_compiled_artifact_t *artifact) {
  rv32emu_tb_line_apply_jit(line, artifact);
}

void rv32emu_tb_line_clear_jit_public(rv32emu_tb_line_t *line, uint8_t state) {
  rv32emu_tb_line_clear_jit(line, state);
}

bool rv32emu_tb_try_compile_jit_public(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line) {
  return rv32emu_tb_try_compile_jit(cache, line);
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
  cache->jit_async_enabled = rv32emu_tb_jit_async_enabled_from_env();
  cache->jit_async_foreground_sync = rv32emu_tb_jit_async_foreground_sync_from_env();
  cache->jit_async_prefetch_enabled = rv32emu_tb_jit_async_prefetch_enabled_from_env();
  cache->jit_async_allow_helpers = rv32emu_tb_jit_async_allow_helpers_from_env();
  cache->jit_async_redecode_helpers = rv32emu_tb_jit_async_redecode_helpers_from_env();
  cache->jit_async_recycle = rv32emu_tb_jit_async_recycle_from_env();
  cache->jit_template_fast_apply = rv32emu_tb_jit_template_fast_apply_from_env();
  cache->jit_async_sync_fallback_spins = rv32emu_tb_jit_async_sync_fallback_spins_from_env();
  cache->jit_async_busy_pct = rv32emu_tb_jit_async_busy_pct_from_env();
  cache->jit_async_hot_discount = rv32emu_tb_jit_async_hot_discount_from_env();
  cache->jit_async_hot_bonus = rv32emu_tb_jit_async_hot_bonus_from_env();
  cache->jit_async_drain_ticks = 0u;
  for (uint32_t i = 0u; i < RV32EMU_TB_LINES; i++) {
    cache->repl_next_way[i] = 0u;
  }
  for (uint32_t i = 0u; i < RV32EMU_TB_TOTAL_LINES; i++) {
    cache->lines[i].valid = false;
    cache->lines[i].start_pc = 0u;
    cache->lines[i].count = 0u;
    cache->lines[i].jit_hotness = 0u;
    cache->lines[i].jit_tried = false;
    cache->lines[i].jit_valid = false;
    cache->lines[i].jit_state = RV32EMU_JIT_STATE_NONE;
    cache->lines[i].jit_async_wait = 0u;
    cache->lines[i].jit_async_prefetched = false;
    cache->lines[i].jit_count = 0u;
    cache->lines[i].jit_generation = rv32emu_tb_next_jit_generation();
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
  line->jit_state = RV32EMU_JIT_STATE_NONE;
  line->jit_async_wait = 0u;
  line->jit_async_prefetched = false;
  line->jit_count = 0u;
  line->jit_generation = rv32emu_tb_next_jit_generation();
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
  uint32_t set_idx;
  rv32emu_tb_line_t *line;

  if (m == NULL || cache == NULL) {
    return NULL;
  }

  line = rv32emu_tb_find_cached_line(cache, pc);
  if (line != NULL) {
    return line;
  }

  set_idx = rv32emu_tb_index(pc);
  line = rv32emu_tb_pick_victim_line(cache, set_idx);
  if (line == NULL) {
    return NULL;
  }
  if (line->valid && line->jit_state == RV32EMU_JIT_STATE_QUEUED) {
    RV32EMU_JIT_STATS_INC(async_evict_queued);
  }
  if (!rv32emu_tb_build_line(m, line, pc)) {
    return NULL;
  }
  return line;
}

#if defined(__x86_64__)
static bool rv32emu_tb_line_jit_ready(const rv32emu_tb_line_t *line) {
  return line != NULL && line->jit_state == RV32EMU_JIT_STATE_READY && line->jit_valid &&
         line->jit_count != 0u && line->jit_fn != NULL;
}

static uint32_t rv32emu_tb_resolve_hot_threshold(const rv32emu_tb_cache_t *cache) {
  uint32_t hot_threshold = RV32EMU_JIT_DEFAULT_HOT_THRESHOLD;

  if (cache != NULL && cache->jit_hot_threshold != 0u) {
    hot_threshold = (uint32_t)cache->jit_hot_threshold;
  }

  return hot_threshold;
}

static uint32_t rv32emu_tb_jit_compile_threshold(const rv32emu_tb_cache_t *cache,
                                                 bool async_compile) {
  uint32_t threshold = rv32emu_tb_resolve_hot_threshold(cache);

  if (async_compile && cache != NULL) {
    if (cache->jit_async_hot_discount >= threshold) {
      threshold = 1u;
    } else {
      threshold -= (uint32_t)cache->jit_async_hot_discount;
      if (threshold == 0u) {
        threshold = 1u;
      }
    }
    if (cache->jit_async_hot_bonus != 0u) {
      if ((uint32_t)cache->jit_async_hot_bonus >= 255u - threshold) {
        threshold = 255u;
      } else {
        threshold += (uint32_t)cache->jit_async_hot_bonus;
      }
    }
  }

  return threshold;
}

static bool rv32emu_tb_jit_prefix_matches_guest(rv32emu_machine_t *m, const rv32emu_tb_line_t *line) {
  if (m == NULL || line == NULL || !line->jit_valid || line->jit_count == 0u ||
      line->jit_count > line->count) {
    return false;
  }

  for (uint8_t i = 0u; i < line->jit_count; i++) {
    uint8_t len = (line->decoded[i].insn_len == 2u) ? 2u : 4u;
    uint32_t raw = 0u;

    if (!rv32emu_virt_read(m, line->pcs[i], len, RV32EMU_ACC_FETCH, &raw)) {
      return false;
    }
    if (len == 2u) {
      if ((line->decoded[i].raw & 0xffffu) != (raw & 0xffffu)) {
        return false;
      }
    } else if (line->decoded[i].raw != raw) {
      return false;
    }
  }

  return true;
}

static uint8_t rv32emu_tb_collect_static_successors(const rv32emu_tb_line_t *line,
                                                    uint32_t succ_out[2]) {
  const rv32emu_decoded_insn_t *tail;
  uint32_t tail_pc;
  uint32_t tail_step;
  uint8_t count = 0u;

  if (line == NULL || succ_out == NULL || !line->valid || line->count == 0u) {
    return 0u;
  }

  tail = &line->decoded[line->count - 1u];
  tail_pc = line->pcs[line->count - 1u];
  tail_step = (tail->insn_len == 2u) ? 2u : 4u;

  switch (tail->opcode) {
  case 0x63: { /* branch */
    uint32_t fallthrough = tail_pc + tail_step;
    uint32_t target = tail_pc + (uint32_t)tail->imm_b;
    succ_out[count++] = fallthrough;
    if (target != fallthrough) {
      succ_out[count++] = target;
    }
    break;
  }
  case 0x6f: /* jal */
    succ_out[count++] = tail_pc + (uint32_t)tail->imm_j;
    break;
  case 0x67: /* jalr: dynamic target, skip speculative prefetch */
  case 0x73: /* system: trap/return side effects, skip */
    break;
  default:
    succ_out[count++] = tail_pc + tail_step;
    break;
  }

  return count;
}

static void rv32emu_tb_async_prefetch_successors(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache,
                                                 rv32emu_tb_line_t *line) {
  uint32_t succ[2] = {0u, 0u};
  uint8_t succ_count;
  uint32_t prefetch_threshold;

  if (m == NULL || cache == NULL || line == NULL || !cache->jit_async_prefetch_enabled) {
    return;
  }
  if (!rv32emu_tb_line_jit_ready(line)) {
    return;
  }

  /* Probe successors every other hit to keep prefetch overhead bounded. */
  if (line->jit_async_prefetched) {
    line->jit_async_prefetched = false;
    return;
  }
  line->jit_async_prefetched = true;
  succ_count = rv32emu_tb_collect_static_successors(line, succ);
  prefetch_threshold =
      rv32emu_tb_jit_compile_threshold(cache, !cache->jit_async_foreground_sync);

  for (uint8_t i = 0u; i < succ_count; i++) {
    rv32emu_tb_line_t *next_line;
    uint32_t target = succ[i];

    if ((target & 1u) != 0u) {
      continue;
    }
    next_line = rv32emu_tb_lookup_or_build(m, cache, target);
    if (next_line == NULL || !next_line->valid || next_line->start_pc != target ||
        rv32emu_tb_line_jit_ready(next_line) || next_line->jit_state != RV32EMU_JIT_STATE_NONE) {
      continue;
    }

    if (next_line->jit_hotness < 255u) {
      next_line->jit_hotness++;
    }
    if ((uint32_t)next_line->jit_hotness < prefetch_threshold) {
      continue;
    }

    (void)rv32emu_tb_queue_jit_compile_async(cache, next_line, true);
  }
}

static bool rv32emu_tb_get_ready_jit_line(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache,
                                          uint32_t pc, uint64_t budget,
                                          rv32emu_tb_line_t **line_out) {
  rv32emu_tb_line_t *line;
  bool async_runtime_ok;
  bool async_compile_ok;
  uint32_t compile_threshold;

  if (m == NULL || cache == NULL || line_out == NULL || budget == 0u) {
    return false;
  }

  async_runtime_ok = false;
  async_compile_ok = false;
  if (!cache->jit_async_foreground_sync && rv32emu_tb_jit_async_supported(m, cache)) {
    async_runtime_ok = true;
    async_compile_ok = true;
    if (cache->jit_async_drain_ticks + 1u >= RV32EMU_JIT_DEFAULT_ASYNC_DRAIN_INTERVAL) {
      cache->jit_async_drain_ticks = 0u;
      rv32emu_tb_jit_async_drain(m, cache);
    } else {
      cache->jit_async_drain_ticks++;
    }
  }

  line = rv32emu_tb_lookup_or_build(m, cache, pc);
  if (line == NULL || line->start_pc != pc) {
    return false;
  }

  if (!rv32emu_tb_line_jit_ready(line) && line->jit_state == RV32EMU_JIT_STATE_NONE) {
    if (line->jit_hotness < 255u) {
      line->jit_hotness++;
    }

    compile_threshold = rv32emu_tb_jit_compile_threshold(cache, async_compile_ok);
    if ((uint32_t)line->jit_hotness >= compile_threshold) {
      if (async_compile_ok) {
        if (!rv32emu_tb_queue_jit_compile_async(cache, line, false) &&
            line->jit_state == RV32EMU_JIT_STATE_NONE && !rv32emu_jit_pool_is_exhausted()) {
          rv32emu_tb_async_force_sync_compile(cache, line);
        }
      } else {
        (void)rv32emu_tb_try_compile_jit(cache, line);
      }
    }
  }

  if (async_runtime_ok && line->jit_state == RV32EMU_JIT_STATE_QUEUED) {
    cache->jit_async_drain_ticks = 0u;
    rv32emu_tb_jit_async_drain(m, cache);
    if (line->jit_state == RV32EMU_JIT_STATE_QUEUED && cache->jit_async_sync_fallback_spins != 0u) {
      if (line->jit_async_wait < 255u) {
        line->jit_async_wait++;
      }
      if ((cache->jit_async_busy_pct != 0u && line->jit_async_wait != 0u &&
           rv32emu_jit_async_is_busy(cache->jit_async_busy_pct)) ||
          line->jit_async_wait >= cache->jit_async_sync_fallback_spins) {
        rv32emu_tb_async_force_sync_compile(cache, line);
      }
    }
  }

  if (!rv32emu_tb_line_jit_ready(line)) {
    return false;
  }
  if (async_runtime_ok && !rv32emu_tb_jit_prefix_matches_guest(m, line)) {
    line->valid = false;
    line->jit_hotness = 0u;
    line->jit_tried = false;
    line->jit_generation = rv32emu_tb_next_jit_generation();
    rv32emu_tb_line_clear_jit(line, RV32EMU_JIT_STATE_NONE);
    RV32EMU_JIT_STATS_INC(async_results_stale);
    return false;
  }
  if (async_runtime_ok) {
    rv32emu_tb_async_prefetch_successors(m, cache, line);
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
  rv32emu_tb_line_t *line;

  if (cache == NULL || from == NULL || !from->jit_chain_valid || from->jit_chain_fn == NULL ||
      from->jit_chain_pc != next_pc || budget == 0u) {
    return NULL;
  }

  line = rv32emu_tb_find_cached_line(cache, next_pc);
  if (line == NULL || !rv32emu_tb_line_jit_ready(line) || line->jit_fn != from->jit_chain_fn ||
      (uint64_t)line->jit_count > budget) {
    from->jit_chain_valid = false;
    from->jit_chain_pc = 0u;
    from->jit_chain_fn = NULL;
    return NULL;
  }

  return line;
}

static void rv32emu_tb_cache_chain(rv32emu_tb_line_t *from, rv32emu_tb_line_t *to) {
  if (from == NULL || to == NULL || !rv32emu_tb_line_jit_ready(to)) {
    return;
  }
  from->jit_chain_valid = true;
  from->jit_chain_pc = to->start_pc;
  from->jit_chain_fn = to->jit_fn;
}

rv32emu_tb_jit_fn_t rv32emu_jit_chain_next(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
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

rv32emu_tb_jit_fn_t rv32emu_jit_chain_next_pc(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                                              uint32_t from_pc) {
  rv32emu_tb_cache_t *cache = g_rv32emu_jit_tls_cache;
  rv32emu_tb_line_t *from;

  if (m == NULL || cpu == NULL || cache == NULL || from_pc == 0u) {
    return NULL;
  }
  from = rv32emu_tb_lookup_or_build(m, cache, from_pc);
  if (from == NULL || from->start_pc != from_pc || !rv32emu_tb_line_jit_ready(from)) {
    return NULL;
  }

  return rv32emu_jit_chain_next(m, cpu, from);
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
