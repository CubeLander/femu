#include "../../../internal/tb_internal.h"

#if !defined(__x86_64__)
/* Non-x86 fallback keeps JIT entry points in a consistent failed state. */
bool rv32emu_tb_try_compile_jit(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line) {
  (void)cache;
  if (line != NULL) {
    line->jit_tried = true;
    line->jit_state = RV32EMU_JIT_STATE_FAILED;
    line->jit_async_wait = 0u;
    line->jit_async_prefetched = false;
    line->jit_valid = false;
    line->jit_count = 0u;
    line->jit_fn = NULL;
    line->jit_map_count = 0u;
    line->jit_code_size = 0u;
    line->jit_chain_valid = false;
    line->jit_chain_pc = 0u;
    line->jit_chain_fn = NULL;
  }
  return false;
}
#endif
