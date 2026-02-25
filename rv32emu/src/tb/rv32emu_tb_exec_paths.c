#include "rv32emu_tb.h"
#include "../internal/tb_internal.h"

#include <stddef.h>

#if defined(__x86_64__)
#include <pthread.h>
#include "../internal/tb_jit_internal.h"
#include <sys/mman.h>
#endif

bool rv32emu_exec_decoded(rv32emu_machine_t *m, const rv32emu_decoded_insn_t *decoded);

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
