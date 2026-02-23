#include "rv32emu.h"
#include "rv32emu_decode.h"

/* Generic bit-slice helper used by all field extractors. */
static inline uint32_t rv32emu_bits32(uint32_t value, int hi, int lo) {
  return (value >> lo) & ((1u << (hi - lo + 1)) - 1u);
}

/* I-type immediate: inst[31:20], sign-extended from 12 bits. */
static inline int32_t rv32emu_imm_i32(uint32_t insn) {
  return (int32_t)rv32emu_sign_extend(insn >> 20, 12);
}

/* S-type immediate: inst[31:25] | inst[11:7]. */
static inline int32_t rv32emu_imm_s32(uint32_t insn) {
  uint32_t imm = (rv32emu_bits32(insn, 31, 25) << 5) | rv32emu_bits32(insn, 11, 7);
  return (int32_t)rv32emu_sign_extend(imm, 12);
}

/* B-type branch immediate with implicit low bit 0. */
static inline int32_t rv32emu_imm_b32(uint32_t insn) {
  uint32_t imm = (rv32emu_bits32(insn, 31, 31) << 12) | (rv32emu_bits32(insn, 7, 7) << 11) |
                 (rv32emu_bits32(insn, 30, 25) << 5) | (rv32emu_bits32(insn, 11, 8) << 1);
  return (int32_t)rv32emu_sign_extend(imm, 13);
}

/* U-type upper immediate (low 12 bits are zero). */
static inline int32_t rv32emu_imm_u32(uint32_t insn) {
  return (int32_t)(insn & 0xfffff000u);
}

/* J-type jump immediate with implicit low bit 0. */
static inline int32_t rv32emu_imm_j32(uint32_t insn) {
  uint32_t imm = (rv32emu_bits32(insn, 31, 31) << 20) | (rv32emu_bits32(insn, 19, 12) << 12) |
                 (rv32emu_bits32(insn, 20, 20) << 11) | (rv32emu_bits32(insn, 30, 21) << 1);
  return (int32_t)rv32emu_sign_extend(imm, 21);
}

/*
 * Central decode stage for 32-bit instructions.
 *
 * This is intentionally explicit and repetitive: the goal is that students can
 * compare each ISA format in the spec with the exact extraction here.
 */
void rv32emu_decode32(uint32_t insn, rv32emu_decoded_insn_t *decoded) {
  if (decoded == NULL) {
    return;
  }

  decoded->raw = insn;
  decoded->opcode = rv32emu_bits32(insn, 6, 0);
  decoded->rd = rv32emu_bits32(insn, 11, 7);
  decoded->funct3 = rv32emu_bits32(insn, 14, 12);
  decoded->rs1 = rv32emu_bits32(insn, 19, 15);
  decoded->rs2 = rv32emu_bits32(insn, 24, 20);
  decoded->funct7 = rv32emu_bits32(insn, 31, 25);
  decoded->imm_i = rv32emu_imm_i32(insn);
  decoded->imm_s = rv32emu_imm_s32(insn);
  decoded->imm_b = rv32emu_imm_b32(insn);
  decoded->imm_u = rv32emu_imm_u32(insn);
  decoded->imm_j = rv32emu_imm_j32(insn);
  decoded->insn_class = RV32EMU_INSN_CLASS_UNKNOWN;

  switch (decoded->opcode) {
  case 0x37: /* lui */
  case 0x17: /* auipc */
    decoded->insn_class = RV32EMU_INSN_CLASS_U;
    break;
  case 0x6f: /* jal */
    decoded->insn_class = RV32EMU_INSN_CLASS_J;
    break;
  case 0x67: /* jalr */
  case 0x03: /* load */
  case 0x07: /* load-fp */
  case 0x13: /* op-imm */
    decoded->insn_class = RV32EMU_INSN_CLASS_I;
    break;
  case 0x63: /* branch */
    decoded->insn_class = RV32EMU_INSN_CLASS_B;
    break;
  case 0x23: /* store */
  case 0x27: /* store-fp */
    decoded->insn_class = RV32EMU_INSN_CLASS_S;
    break;
  case 0x33: /* op */
  case 0x53: /* op-fp */
  case 0x2f: /* amo */
    decoded->insn_class = RV32EMU_INSN_CLASS_R;
    break;
  case 0x73: /* system */
    decoded->insn_class = RV32EMU_INSN_CLASS_SYSTEM;
    break;
  default:
    break;
  }
}
