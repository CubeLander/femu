#include "rv32emu_tb.h"
#include "../internal/tb_internal.h"

#if defined(__x86_64__)
#include "../internal/tb_jit_internal.h"

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
