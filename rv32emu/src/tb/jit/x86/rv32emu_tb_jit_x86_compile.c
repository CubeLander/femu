#include "../../../internal/tb_jit_internal.h"

#if defined(__x86_64__)
#include <string.h>

/* Foreground compile flow plus per-line JIT state transitions. */
void rv32emu_tb_line_clear_jit(rv32emu_tb_line_t *line, uint8_t state) {
  if (line == NULL) {
    return;
  }
  line->jit_valid = false;
  line->jit_state = state;
  line->jit_async_wait = 0u;
  line->jit_async_prefetched = false;
  line->jit_count = 0u;
  line->jit_fn = NULL;
  line->jit_map_count = 0u;
  line->jit_code_size = 0u;
  line->jit_chain_valid = false;
  line->jit_chain_pc = 0u;
  line->jit_chain_fn = NULL;
}

void rv32emu_tb_line_apply_jit(rv32emu_tb_line_t *line,
                               const rv32emu_jit_compiled_artifact_t *artifact) {
  if (line == NULL || artifact == NULL) {
    return;
  }

  line->jit_valid = true;
  line->jit_state = RV32EMU_JIT_STATE_READY;
  line->jit_async_wait = 0u;
  line->jit_async_prefetched = false;
  line->jit_count = artifact->jit_count;
  line->jit_fn = artifact->jit_fn;
  line->jit_map_count = artifact->jit_map_count;
  line->jit_code_size = artifact->jit_code_size;
  memcpy(line->jit_host_off, artifact->jit_host_off, sizeof(line->jit_host_off));
  line->jit_chain_valid = false;
  line->jit_chain_pc = 0u;
  line->jit_chain_fn = NULL;
}

static uint32_t rv32emu_tb_prefix_next_pc(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                          uint8_t total_count, uint8_t prefix_count) {
  uint32_t last_idx;
  uint32_t step;

  if (decoded == NULL || pcs == NULL || total_count == 0u || prefix_count == 0u) {
    return 0u;
  }
  if (prefix_count < total_count) {
    return pcs[prefix_count];
  }

  last_idx = (uint32_t)prefix_count - 1u;
  step = (decoded[last_idx].insn_len == 2u) ? 2u : 4u;
  return pcs[last_idx] + step;
}

uint64_t rv32emu_tb_prefix_signature(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                     uint8_t count) {
  uint64_t h = UINT64_C(1469598103934665603);

  if (decoded == NULL || pcs == NULL || count == 0u) {
    return 0u;
  }

  for (uint8_t i = 0u; i < count; i++) {
    h ^= (uint64_t)pcs[i];
    h *= UINT64_C(1099511628211);
    h ^= (uint64_t)decoded[i].raw;
    h *= UINT64_C(1099511628211);
    h ^= (uint64_t)decoded[i].insn_len;
    h *= UINT64_C(1099511628211);
  }

  return (h == 0u) ? 1u : h;
}

static uint8_t rv32emu_tb_jit_supported_prefix(const rv32emu_decoded_insn_t *decoded, uint8_t count,
                                               uint8_t max_jit_insns) {
  uint8_t jit_count = 0u;

  if (decoded == NULL || count == 0u || max_jit_insns == 0u) {
    return 0u;
  }

  while (jit_count < count && jit_count < max_jit_insns) {
    if (!rv32emu_jit_insn_supported_query(&decoded[jit_count])) {
      break;
    }
    jit_count++;
  }
  return jit_count;
}

bool rv32emu_tb_jit_template_key(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                 uint8_t count, uint8_t max_jit_insns,
                                 uint8_t min_prefix_insns, uint8_t *jit_count_out,
                                 uint64_t *prefix_sig_out) {
  uint8_t jit_count;
  uint64_t prefix_sig;

  if (decoded == NULL || pcs == NULL || count == 0u || jit_count_out == NULL || prefix_sig_out == NULL) {
    return false;
  }
  if (max_jit_insns == 0u) {
    max_jit_insns = RV32EMU_JIT_DEFAULT_MAX_INSNS_PER_BLOCK;
  }
  if (min_prefix_insns == 0u) {
    min_prefix_insns = RV32EMU_JIT_DEFAULT_MIN_PREFIX_INSNS;
  }
  if (min_prefix_insns > max_jit_insns) {
    min_prefix_insns = max_jit_insns;
  }

  jit_count = rv32emu_tb_jit_supported_prefix(decoded, count, max_jit_insns);
  if (jit_count < min_prefix_insns) {
    return false;
  }

  prefix_sig = rv32emu_tb_prefix_signature(decoded, pcs, jit_count);
  if (prefix_sig == 0u) {
    return false;
  }

  *jit_count_out = jit_count;
  *prefix_sig_out = prefix_sig;
  return true;
}

bool rv32emu_tb_compile_jit_from_snapshot(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                          uint8_t count, rv32emu_tb_line_t *line_for_chain,
                                          uint32_t chain_from_pc, uint8_t max_jit_insns,
                                          uint8_t min_prefix_insns,
                                          rv32emu_jit_compiled_artifact_t *artifact_out) {
  rv32emu_x86_emit_t emit;
  rv32emu_decoded_insn_t *helper_snapshot;
  const rv32emu_decoded_insn_t *helper_base = decoded;
  uint8_t *epilogue_start;
  uint32_t jit_count = 0u;
  uint32_t epilogue_next_pc;
  size_t code_bytes;
  uint8_t *code_ptr;

  if (decoded == NULL || pcs == NULL || count == 0u || artifact_out == NULL) {
    return false;
  }
  if (max_jit_insns == 0u) {
    max_jit_insns = RV32EMU_JIT_DEFAULT_MAX_INSNS_PER_BLOCK;
  }
  if (min_prefix_insns == 0u) {
    min_prefix_insns = RV32EMU_JIT_DEFAULT_MIN_PREFIX_INSNS;
  }
  if (min_prefix_insns > max_jit_insns) {
    min_prefix_insns = max_jit_insns;
  }

  rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_ATTEMPTS);
  artifact_out->pc_reloc_count = 0u;
  artifact_out->base_start_pc = pcs[0];

  while (jit_count < count && jit_count < max_jit_insns) {
    if (!rv32emu_jit_insn_supported_query(&decoded[jit_count])) {
      break;
    }
    jit_count++;
  }

  if (jit_count == 0u) {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_UNSUPPORTED_PREFIX);
    return false;
  }
  if (jit_count < min_prefix_insns) {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_TOO_SHORT);
    return false;
  }

  code_bytes = (size_t)jit_count * RV32EMU_JIT_BYTES_PER_INSN + RV32EMU_JIT_EPILOGUE_BYTES;
  code_ptr = (uint8_t *)rv32emu_jit_alloc(code_bytes);
  if (code_ptr == NULL) {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_ALLOC);
    return false;
  }

  helper_snapshot = NULL;
  if (line_for_chain != NULL) {
    helper_base = line_for_chain->decoded;
  } else {
    helper_snapshot =
        (rv32emu_decoded_insn_t *)rv32emu_jit_alloc((size_t)jit_count * sizeof(rv32emu_decoded_insn_t));
    if (helper_snapshot == NULL) {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_ALLOC);
      return false;
    }
    memcpy(helper_snapshot, decoded, (size_t)jit_count * sizeof(rv32emu_decoded_insn_t));
    helper_base = helper_snapshot;
  }

  emit.p = code_ptr;
  emit.end = code_ptr + code_bytes;
  if (!rv32emu_jit_emit_prologue(&emit)) {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_EMIT);
    return false;
  }

  for (uint32_t i = 0u; i < jit_count; i++) {
    artifact_out->jit_host_off[i] = (uint16_t)(uintptr_t)(emit.p - code_ptr);
    if (!rv32emu_jit_emit_one_lowered(&emit, &decoded[i], &helper_base[i], pcs[i], i, code_ptr,
                                      artifact_out)) {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_EMIT);
      return false;
    }
  }

  epilogue_next_pc = rv32emu_tb_prefix_next_pc(decoded, pcs, count, (uint8_t)jit_count);
  epilogue_start = emit.p;
  if (!rv32emu_jit_emit_epilogue(&emit, line_for_chain, chain_from_pc, epilogue_next_pc,
                                 jit_count)) {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_EMIT);
    return false;
  }
  if (line_for_chain == NULL && chain_from_pc != 0u && epilogue_start != NULL && code_ptr != NULL) {
    uint32_t ep_off = (uint32_t)(uintptr_t)(epilogue_start - code_ptr);
    if (ep_off + 46u <= (uint32_t)(uintptr_t)(emit.p - code_ptr) &&
        ep_off + 42u <= UINT16_MAX) {
      if (!rv32emu_jit_record_pc_reloc_public(artifact_out, code_ptr, code_ptr + ep_off + 5u) ||
          !rv32emu_jit_record_pc_reloc_public(artifact_out, code_ptr, code_ptr + ep_off + 42u)) {
        rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_EMIT);
        return false;
      }
    }
  }

  artifact_out->jit_count = (uint8_t)jit_count;
  artifact_out->jit_fn = (rv32emu_tb_jit_fn_t)(void *)code_ptr;
  artifact_out->jit_map_count = (uint8_t)jit_count;
  artifact_out->jit_code_size = (uint32_t)(uintptr_t)(emit.p - code_ptr);

  rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_SUCCESS);
  rv32emu_jit_stats_add_compile_prefix_insns(jit_count);
  if (jit_count < count && jit_count == max_jit_insns) {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_PREFIX_TRUNCATED);
  }
  return true;
}

bool rv32emu_tb_try_compile_jit(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line) {
  rv32emu_jit_compiled_artifact_t artifact;
  uint8_t max_jit_insns = RV32EMU_JIT_DEFAULT_MAX_INSNS_PER_BLOCK;
  uint8_t min_prefix_insns = RV32EMU_JIT_DEFAULT_MIN_PREFIX_INSNS;
  uint8_t template_jit_count = 0u;
  uint64_t template_sig = 0u;

  if (line == NULL || !line->valid || line->count == 0u) {
    return false;
  }
  if (cache != NULL && cache->jit_max_block_insns != 0u) {
    max_jit_insns = cache->jit_max_block_insns;
  }
  if (cache != NULL && cache->jit_min_prefix_insns != 0u) {
    min_prefix_insns = cache->jit_min_prefix_insns;
  }

  line->jit_tried = true;
  rv32emu_tb_line_clear_jit(line, RV32EMU_JIT_STATE_NONE);
  if (rv32emu_tb_jit_template_key(line->decoded, line->pcs, line->count, max_jit_insns,
                                  min_prefix_insns, &template_jit_count, &template_sig) &&
      rv32emu_jit_template_lookup(line->decoded, line->pcs, template_jit_count, template_sig,
                                  &artifact)) {
    rv32emu_tb_line_apply_jit(line, &artifact);
    return true;
  }
  if (rv32emu_jit_pool_is_exhausted()) {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_ALLOC);
    line->jit_state = RV32EMU_JIT_STATE_FAILED;
    return false;
  }
  if (!rv32emu_tb_compile_jit_from_snapshot(line->decoded, line->pcs, line->count, line,
                                            line->start_pc, max_jit_insns, min_prefix_insns,
                                            &artifact)) {
    line->jit_state = RV32EMU_JIT_STATE_FAILED;
    return false;
  }

  rv32emu_tb_line_apply_jit(line, &artifact);
  return true;
}
#endif
