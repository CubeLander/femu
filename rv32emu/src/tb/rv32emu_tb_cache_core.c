#include "rv32emu_tb.h"
#include "../internal/tb_internal.h"

_Static_assert((RV32EMU_JIT_TEMPLATE_CACHE_LINES &
                (RV32EMU_JIT_TEMPLATE_CACHE_LINES - 1u)) == 0u,
               "template cache lines must be a power of two");

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

rv32emu_tb_line_t *rv32emu_tb_find_cached_line(rv32emu_tb_cache_t *cache, uint32_t pc) {
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

rv32emu_tb_line_t *rv32emu_tb_lookup_or_build(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache,
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
