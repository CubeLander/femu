#ifndef RV32EMU_DECODE_H
#define RV32EMU_DECODE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  RV32EMU_INSN_CLASS_UNKNOWN = 0,
  RV32EMU_INSN_CLASS_U,
  RV32EMU_INSN_CLASS_J,
  RV32EMU_INSN_CLASS_I,
  RV32EMU_INSN_CLASS_B,
  RV32EMU_INSN_CLASS_S,
  RV32EMU_INSN_CLASS_R,
  RV32EMU_INSN_CLASS_SYSTEM,
} rv32emu_insn_class_t;

/*
 * A teaching-oriented decoded view of one 32-bit RISC-V instruction.
 *
 * Why this struct exists:
 * - decode fields once, execute many branches with readable names
 * - make ISA lectures/labs map directly to code fields
 * - keep immediate-generation rules centralized
 */
typedef struct {
  /* Original 32-bit instruction word fetched from memory. */
  uint32_t raw;
  /* Common fixed fields (all extracted by bit slicing). */
  uint32_t opcode;
  uint32_t rd;
  uint32_t funct3;
  uint32_t rs1;
  uint32_t rs2;
  uint32_t funct7;
  /* Pre-decoded immediates for each canonical format. */
  int32_t imm_i;
  int32_t imm_s;
  int32_t imm_b;
  int32_t imm_u;
  int32_t imm_j;
  /* Coarse instruction family for pedagogy and tracing. */
  rv32emu_insn_class_t insn_class;
} rv32emu_decoded_insn_t;

/* Decode one 32-bit instruction into rv32emu_decoded_insn_t. */
void rv32emu_decode32(uint32_t insn, rv32emu_decoded_insn_t *decoded);

#endif
