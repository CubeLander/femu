#ifndef RV32EMU_TB_H
#define RV32EMU_TB_H

#include "rv32emu.h"
#include "rv32emu_decode.h"

#define RV32EMU_TB_LINES 256u
#define RV32EMU_TB_MAX_INSNS 32u

typedef int (*rv32emu_tb_jit_fn_t)(rv32emu_machine_t *m, rv32emu_cpu_t *cpu);

typedef enum {
  RV32EMU_TB_JIT_NOPROGRESS = 0,
  RV32EMU_TB_JIT_RETIRED = 1,
  RV32EMU_TB_JIT_HANDLED_NO_RETIRE = 2,
} rv32emu_tb_jit_status_t;

typedef struct {
  rv32emu_tb_jit_status_t status;
  uint32_t retired;
} rv32emu_tb_jit_result_t;

typedef enum {
  RV32EMU_TB_BLOCK_NOPROGRESS = 0,
  RV32EMU_TB_BLOCK_RETIRED = 1,
  RV32EMU_TB_BLOCK_HANDLED_NO_RETIRE = 2,
} rv32emu_tb_block_status_t;

typedef struct {
  rv32emu_tb_block_status_t status;
  uint32_t retired;
} rv32emu_tb_block_result_t;

typedef struct {
  bool valid;
  uint32_t start_pc;
  uint8_t count;
  uint8_t jit_hotness;
  bool jit_tried;
  bool jit_valid;
  uint8_t jit_count;
  rv32emu_tb_jit_fn_t jit_fn;
  uint8_t jit_map_count;
  uint32_t jit_code_size;
  uint16_t jit_host_off[RV32EMU_TB_MAX_INSNS];
  bool jit_chain_valid;
  uint32_t jit_chain_pc;
  rv32emu_tb_jit_fn_t jit_chain_fn;
  uint32_t pcs[RV32EMU_TB_MAX_INSNS];
  rv32emu_decoded_insn_t decoded[RV32EMU_TB_MAX_INSNS];
} rv32emu_tb_line_t;

typedef struct {
  rv32emu_tb_line_t lines[RV32EMU_TB_LINES];
  bool active;
  uint32_t active_start_pc;
  uint8_t active_index;
  uint8_t jit_hot_threshold;
} rv32emu_tb_cache_t;

void rv32emu_tb_cache_reset(rv32emu_tb_cache_t *cache);
bool rv32emu_exec_one_tb(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache);
rv32emu_tb_block_result_t rv32emu_exec_tb_block(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache,
                                                 uint64_t budget);
rv32emu_tb_jit_result_t rv32emu_exec_tb_jit(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache,
                                            uint64_t budget);

#endif
