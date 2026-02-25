#ifndef RV32EMU_INTERNAL_TB_INTERNAL_H
#define RV32EMU_INTERNAL_TB_INTERNAL_H

#include "rv32emu_tb.h"
#include "tb_stats_internal.h"

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
rv32emu_tb_line_t *rv32emu_tb_lookup_or_build(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache,
                                              uint32_t pc);

#endif
