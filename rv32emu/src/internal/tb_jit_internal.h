#ifndef RV32EMU_INTERNAL_TB_JIT_INTERNAL_H
#define RV32EMU_INTERNAL_TB_JIT_INTERNAL_H

#include "tb_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <stdint.h>

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

typedef struct {
  uint8_t jit_count;
  rv32emu_tb_jit_fn_t jit_fn;
  uint8_t jit_map_count;
  uint32_t jit_code_size;
  uint8_t pc_reloc_count;
  uint32_t base_start_pc;
  uint16_t pc_reloc_off[RV32EMU_JIT_MAX_PC_RELOCS];
  uint16_t jit_host_off[RV32EMU_TB_MAX_INSNS];
} rv32emu_jit_compiled_artifact_t;

typedef struct {
  bool valid;
  uint8_t jit_count;
  uint64_t prefix_sig;
  uint32_t pcs[RV32EMU_TB_MAX_INSNS];
  uint32_t raw[RV32EMU_TB_MAX_INSNS];
  uint8_t insn_len[RV32EMU_TB_MAX_INSNS];
  rv32emu_jit_compiled_artifact_t artifact;
} rv32emu_jit_template_line_t;

typedef struct {
  rv32emu_jit_template_line_t lines[RV32EMU_JIT_TEMPLATE_CACHE_LINES];
  pthread_mutex_t lock;
} rv32emu_jit_template_cache_t;

typedef struct {
  bool valid;
  uint8_t jit_count;
  uint64_t struct_sig;
  uint32_t raw[RV32EMU_TB_MAX_INSNS];
  uint8_t insn_len[RV32EMU_TB_MAX_INSNS];
  rv32emu_jit_compiled_artifact_t artifact;
} rv32emu_jit_struct_template_line_t;

typedef struct {
  rv32emu_jit_struct_template_line_t lines[RV32EMU_JIT_STRUCT_TEMPLATE_LINES];
  pthread_mutex_t lock;
} rv32emu_jit_struct_template_cache_t;

typedef struct {
  rv32emu_tb_line_t *line;
  uint32_t start_pc;
  uint32_t generation;
  bool portable;
  uint8_t count;
  uint8_t max_block_insns;
  uint8_t min_prefix_insns;
  uint32_t pcs[RV32EMU_TB_MAX_INSNS];
  rv32emu_decoded_insn_t decoded[RV32EMU_TB_MAX_INSNS];
} rv32emu_jit_async_job_t;

typedef struct {
  rv32emu_tb_line_t *line;
  uint32_t start_pc;
  uint32_t generation;
  uint64_t prefix_sig;
  bool portable;
  bool success;
  rv32emu_jit_compiled_artifact_t artifact;
} rv32emu_jit_async_done_t;

typedef struct {
  pthread_t workers[RV32EMU_JIT_MAX_ASYNC_WORKERS];
  rv32emu_jit_async_job_t *pending;
  rv32emu_jit_async_done_t *done;
  uint32_t worker_count;
  uint32_t queue_cap;
  uint32_t pending_head;
  uint32_t pending_tail;
  uint32_t pending_count;
  uint32_t done_head;
  uint32_t done_tail;
  uint32_t done_count;
  bool running;
  pthread_mutex_t lock;
  pthread_cond_t pending_cv;
  pthread_once_t once;
} rv32emu_jit_async_mgr_t;

typedef enum {
  RV32EMU_JIT_STAT_ASYNC_JOBS_ENQUEUED = 0,
  RV32EMU_JIT_STAT_ASYNC_JOBS_DROPPED = 1,
  RV32EMU_JIT_STAT_ASYNC_JOBS_COMPILED = 2,
  RV32EMU_JIT_STAT_ASYNC_RESULTS_APPLIED = 3,
  RV32EMU_JIT_STAT_ASYNC_APPLIED_DIRECT = 4,
  RV32EMU_JIT_STAT_ASYNC_APPLIED_RECYCLED = 5,
  RV32EMU_JIT_STAT_ASYNC_STALE_NONPORTABLE = 6,
  RV32EMU_JIT_STAT_ASYNC_STALE_NOT_SUCCESS = 7,
  RV32EMU_JIT_STAT_ASYNC_STALE_LOOKUP_MISS = 8,
  RV32EMU_JIT_STAT_ASYNC_STALE_STATE_MISMATCH = 9,
  RV32EMU_JIT_STAT_ASYNC_STALE_SIG_MISMATCH = 10,
  RV32EMU_JIT_STAT_ASYNC_RESULTS_STALE = 11,
  RV32EMU_JIT_STAT_ASYNC_SYNC_FALLBACKS = 12,
  RV32EMU_JIT_STAT_ASYNC_TEMPLATE_APPLIED = 13,
  RV32EMU_JIT_STAT_COMPILE_FAIL_ALLOC = 14,
  RV32EMU_JIT_STAT_COMPILE_FAIL_UNSUPPORTED_PREFIX = 15,
  RV32EMU_JIT_STAT_COMPILE_TEMPLATE_HIT = 16,
  RV32EMU_JIT_STAT_COMPILE_TEMPLATE_STORE = 17,
  RV32EMU_JIT_STAT_COMPILE_STRUCT_HIT = 18,
  RV32EMU_JIT_STAT_COMPILE_STRUCT_STORE = 19,
  RV32EMU_JIT_STAT_COMPILE_ATTEMPTS = 20,
  RV32EMU_JIT_STAT_COMPILE_FAIL_TOO_SHORT = 21,
  RV32EMU_JIT_STAT_COMPILE_FAIL_EMIT = 22,
  RV32EMU_JIT_STAT_COMPILE_SUCCESS = 23,
  RV32EMU_JIT_STAT_COMPILE_PREFIX_TRUNCATED = 24,
} rv32emu_jit_stat_event_t;

extern _Thread_local rv32emu_tb_cache_t *g_rv32emu_jit_tls_cache;
extern _Thread_local uint64_t g_rv32emu_jit_tls_budget;
extern _Thread_local uint64_t g_rv32emu_jit_tls_total;
extern _Thread_local bool g_rv32emu_jit_tls_handled;

int rv32emu_jit_block_commit(rv32emu_machine_t *m, rv32emu_cpu_t *cpu, uint32_t next_pc,
                             uint32_t retired);
void rv32emu_jit_retire_prefix(rv32emu_machine_t *m, rv32emu_cpu_t *cpu, uint32_t retired);
uint32_t rv32emu_jit_result_or_no_retire(void);
int rv32emu_jit_pre_dispatch(rv32emu_machine_t *m, rv32emu_cpu_t *cpu);
uint32_t rv32emu_jit_exec_mem(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                              const rv32emu_decoded_insn_t *d, uint32_t insn_pc,
                              uint32_t retired_prefix);
uint32_t rv32emu_jit_exec_cf(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                             const rv32emu_decoded_insn_t *d, uint32_t insn_pc,
                             uint32_t retired_prefix);
rv32emu_tb_jit_fn_t rv32emu_jit_chain_next(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                                           rv32emu_tb_line_t *from);
rv32emu_tb_jit_fn_t rv32emu_jit_chain_next_pc(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                                              uint32_t from_pc);

bool rv32emu_jit_pool_is_exhausted(void);
void *rv32emu_jit_alloc(size_t bytes);
bool rv32emu_jit_template_lookup(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                 uint8_t jit_count, uint64_t prefix_sig,
                                 rv32emu_jit_compiled_artifact_t *artifact_out);
void rv32emu_jit_template_store(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                uint8_t jit_count, uint64_t prefix_sig,
                                const rv32emu_jit_compiled_artifact_t *artifact);
bool rv32emu_jit_struct_template_lookup(const rv32emu_decoded_insn_t *decoded, uint8_t jit_count,
                                        uint32_t start_pc,
                                        rv32emu_jit_compiled_artifact_t *artifact_out);
void rv32emu_jit_struct_template_store(const rv32emu_decoded_insn_t *decoded, uint8_t jit_count,
                                       const rv32emu_jit_compiled_artifact_t *artifact);
void rv32emu_tb_line_clear_jit(rv32emu_tb_line_t *line, uint8_t state);
void rv32emu_tb_line_apply_jit(rv32emu_tb_line_t *line,
                               const rv32emu_jit_compiled_artifact_t *artifact);
uint64_t rv32emu_tb_prefix_signature(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                     uint8_t count);
bool rv32emu_tb_jit_template_key(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                 uint8_t count, uint8_t max_jit_insns,
                                 uint8_t min_prefix_insns, uint8_t *jit_count_out,
                                 uint64_t *prefix_sig_out);
bool rv32emu_tb_compile_jit_from_snapshot(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                          uint8_t count, rv32emu_tb_line_t *line_for_chain,
                                          uint32_t chain_from_pc, uint8_t max_jit_insns,
                                          uint8_t min_prefix_insns,
                                          rv32emu_jit_compiled_artifact_t *artifact_out);
bool rv32emu_tb_try_compile_jit(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line);
bool rv32emu_jit_insn_supported_query(const rv32emu_decoded_insn_t *d);
bool rv32emu_jit_emit_prologue(rv32emu_x86_emit_t *e);
bool rv32emu_jit_emit_epilogue(rv32emu_x86_emit_t *e, rv32emu_tb_line_t *line,
                               uint32_t chain_from_pc, uint32_t next_pc, uint32_t retired);
bool rv32emu_jit_emit_one_lowered(rv32emu_x86_emit_t *e, const rv32emu_decoded_insn_t *d,
                                  const rv32emu_decoded_insn_t *helper_d, uint32_t insn_pc,
                                  uint32_t retired_before, uint8_t *code_ptr,
                                  rv32emu_jit_compiled_artifact_t *artifact);
bool rv32emu_jit_record_pc_reloc_public(rv32emu_jit_compiled_artifact_t *artifact,
                                        uint8_t *code_ptr, uint8_t *imm_ptr);

void rv32emu_jit_stats_inc_event(rv32emu_jit_stat_event_t event);
void rv32emu_jit_stats_add_compile_prefix_insns(uint32_t value);
void rv32emu_jit_stats_inc_helper_mem_calls(void);
void rv32emu_jit_stats_inc_helper_cf_calls(void);

bool rv32emu_tb_async_env_enabled(void);
uint32_t rv32emu_tb_async_env_workers(void);
uint32_t rv32emu_tb_async_env_queue(void);
size_t rv32emu_tb_jit_pool_size_public(void);

uint32_t rv32emu_tb_next_jit_generation_public(void);
rv32emu_tb_line_t *rv32emu_tb_find_cached_line_public(rv32emu_tb_cache_t *cache, uint32_t pc);
bool rv32emu_jit_insn_supported_public(const rv32emu_decoded_insn_t *d);
bool rv32emu_jit_pool_is_exhausted_public(void);
uint64_t rv32emu_tb_prefix_signature_public(const rv32emu_decoded_insn_t *decoded,
                                            const uint32_t *pcs, uint8_t count);
bool rv32emu_tb_jit_template_key_public(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                        uint8_t count, uint8_t max_jit_insns,
                                        uint8_t min_prefix_insns, uint8_t *jit_count_out,
                                        uint64_t *prefix_sig_out);
bool rv32emu_jit_template_lookup_public(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                        uint8_t jit_count, uint64_t prefix_sig,
                                        rv32emu_jit_compiled_artifact_t *artifact_out);
void rv32emu_jit_template_store_public(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                       uint8_t jit_count, uint64_t prefix_sig,
                                       const rv32emu_jit_compiled_artifact_t *artifact);
bool rv32emu_jit_struct_template_lookup_public(const rv32emu_decoded_insn_t *decoded,
                                               uint8_t jit_count, uint32_t start_pc,
                                               rv32emu_jit_compiled_artifact_t *artifact_out);
void rv32emu_jit_struct_template_store_public(const rv32emu_decoded_insn_t *decoded,
                                              uint8_t jit_count,
                                              const rv32emu_jit_compiled_artifact_t *artifact);
bool rv32emu_tb_compile_jit_from_snapshot_public(const rv32emu_decoded_insn_t *decoded,
                                                 const uint32_t *pcs, uint8_t count,
                                                 rv32emu_tb_line_t *line_for_chain,
                                                 uint32_t chain_from_pc,
                                                 uint8_t max_jit_insns,
                                                 uint8_t min_prefix_insns,
                                                 rv32emu_jit_compiled_artifact_t *artifact_out);
void rv32emu_tb_line_apply_jit_public(rv32emu_tb_line_t *line,
                                      const rv32emu_jit_compiled_artifact_t *artifact);
void rv32emu_tb_line_clear_jit_public(rv32emu_tb_line_t *line, uint8_t state);
bool rv32emu_tb_try_compile_jit_public(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line);

void rv32emu_tb_async_force_sync_compile(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line);
bool rv32emu_tb_queue_jit_compile_async(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line,
                                        bool prefetch_hint);
void rv32emu_tb_jit_async_drain(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache);
bool rv32emu_jit_async_is_busy(uint8_t busy_pct);
bool rv32emu_tb_jit_async_supported(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache);
#endif

#endif
