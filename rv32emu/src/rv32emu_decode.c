#include "rv32emu.h"
#include "rv32emu_decode.h"

/* Generic bit-slice helper used by all field extractors. */
static inline uint32_t rv32emu_bits32(uint32_t value, int hi, int lo) {
  return (value >> lo) & ((1u << (hi - lo + 1)) - 1u);
}

/* Generic bit-slice helper for compressed instructions. */
static inline uint32_t rv32emu_bits16(uint16_t value, int hi, int lo) {
  return ((uint32_t)value >> lo) & ((1u << (hi - lo + 1)) - 1u);
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

/* C.ADDI/C.LI/C.ANDI shared immediate format. */
static inline int32_t rv32emu_imm_c_addi(uint16_t insn) {
  uint32_t imm = (rv32emu_bits16(insn, 12, 12) << 5) | rv32emu_bits16(insn, 6, 2);
  return (int32_t)rv32emu_sign_extend(imm, 6);
}

/* C.J/C.JAL immediate format in bytes. */
static inline int32_t rv32emu_imm_c_j(uint16_t insn) {
  uint32_t imm = (rv32emu_bits16(insn, 12, 12) << 11) | (rv32emu_bits16(insn, 8, 8) << 10) |
                 (rv32emu_bits16(insn, 10, 9) << 8) | (rv32emu_bits16(insn, 6, 6) << 7) |
                 (rv32emu_bits16(insn, 7, 7) << 6) | (rv32emu_bits16(insn, 2, 2) << 5) |
                 (rv32emu_bits16(insn, 11, 11) << 4) | (rv32emu_bits16(insn, 5, 3) << 1);
  return (int32_t)rv32emu_sign_extend(imm, 12);
}

/* C.BEQZ/C.BNEZ immediate format in bytes. */
static inline int32_t rv32emu_imm_c_b(uint16_t insn) {
  uint32_t imm = (rv32emu_bits16(insn, 12, 12) << 8) | (rv32emu_bits16(insn, 6, 5) << 6) |
                 (rv32emu_bits16(insn, 2, 2) << 5) | (rv32emu_bits16(insn, 11, 10) << 3) |
                 (rv32emu_bits16(insn, 4, 3) << 1);
  return (int32_t)rv32emu_sign_extend(imm, 9);
}

/* Canonical opcode -> class mapping reused by 32-bit and C expansion paths. */
static rv32emu_insn_class_t rv32emu_classify_opcode(uint32_t opcode) {
  switch (opcode) {
  case 0x37: /* lui */
  case 0x17: /* auipc */
    return RV32EMU_INSN_CLASS_U;
  case 0x6f: /* jal */
    return RV32EMU_INSN_CLASS_J;
  case 0x67: /* jalr */
  case 0x03: /* load */
  case 0x07: /* load-fp */
  case 0x13: /* op-imm */
    return RV32EMU_INSN_CLASS_I;
  case 0x63: /* branch */
    return RV32EMU_INSN_CLASS_B;
  case 0x23: /* store */
  case 0x27: /* store-fp */
    return RV32EMU_INSN_CLASS_S;
  case 0x33: /* op */
  case 0x53: /* op-fp */
  case 0x2f: /* amo */
    return RV32EMU_INSN_CLASS_R;
  case 0x73: /* system */
    return RV32EMU_INSN_CLASS_SYSTEM;
  default:
    return RV32EMU_INSN_CLASS_UNKNOWN;
  }
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
  decoded->insn_len = 4u;
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
  decoded->insn_class = rv32emu_classify_opcode(decoded->opcode);
}

bool rv32emu_decode16(uint16_t insn, rv32emu_decoded_insn_t *decoded) {
  uint32_t quadrant;
  uint32_t funct3;
  uint32_t rd;
  uint32_t rs2;
  uint32_t imm;

  if (decoded == NULL) {
    return false;
  }
  if ((insn & 0x3u) == 0x3u) {
    return false;
  }

  decoded->raw = (uint32_t)insn;
  decoded->insn_len = 2u;
  decoded->opcode = 0u;
  decoded->rd = 0u;
  decoded->funct3 = 0u;
  decoded->rs1 = 0u;
  decoded->rs2 = 0u;
  decoded->funct7 = 0u;
  decoded->imm_i = 0;
  decoded->imm_s = 0;
  decoded->imm_b = 0;
  decoded->imm_u = 0;
  decoded->imm_j = 0;
  decoded->insn_class = RV32EMU_INSN_CLASS_UNKNOWN;

  quadrant = rv32emu_bits16(insn, 1, 0);
  funct3 = rv32emu_bits16(insn, 15, 13);

  switch (quadrant) {
  case 0x0:
    switch (funct3) {
    case 0x0: /* c.addi4spn -> addi rd', x2, nzuimm */
      rd = 8u + rv32emu_bits16(insn, 4, 2);
      imm = (rv32emu_bits16(insn, 6, 6) << 2) | (rv32emu_bits16(insn, 5, 5) << 3) |
            (rv32emu_bits16(insn, 12, 11) << 4) | (rv32emu_bits16(insn, 10, 7) << 6);
      if (imm == 0u) {
        return false;
      }
      decoded->opcode = 0x13u;
      decoded->rd = rd;
      decoded->funct3 = 0x0u;
      decoded->rs1 = 2u;
      decoded->imm_i = (int32_t)imm;
      break;
    case 0x1: /* c.fld -> fld rd', uimm(rs1') */
      decoded->opcode = 0x07u;
      decoded->rd = 8u + rv32emu_bits16(insn, 4, 2);
      decoded->funct3 = 0x3u;
      decoded->rs1 = 8u + rv32emu_bits16(insn, 9, 7);
      imm = (rv32emu_bits16(insn, 12, 10) << 3) | (rv32emu_bits16(insn, 6, 5) << 6);
      decoded->imm_i = (int32_t)imm;
      break;
    case 0x2: /* c.lw -> lw rd', uimm(rs1') */
      decoded->opcode = 0x03u;
      decoded->rd = 8u + rv32emu_bits16(insn, 4, 2);
      decoded->funct3 = 0x2u;
      decoded->rs1 = 8u + rv32emu_bits16(insn, 9, 7);
      imm = (rv32emu_bits16(insn, 6, 6) << 2) | (rv32emu_bits16(insn, 12, 10) << 3) |
            (rv32emu_bits16(insn, 5, 5) << 6);
      decoded->imm_i = (int32_t)imm;
      break;
    case 0x3: /* c.flw -> flw rd', uimm(rs1') */
      decoded->opcode = 0x07u;
      decoded->rd = 8u + rv32emu_bits16(insn, 4, 2);
      decoded->funct3 = 0x2u;
      decoded->rs1 = 8u + rv32emu_bits16(insn, 9, 7);
      imm = (rv32emu_bits16(insn, 6, 6) << 2) | (rv32emu_bits16(insn, 12, 10) << 3) |
            (rv32emu_bits16(insn, 5, 5) << 6);
      decoded->imm_i = (int32_t)imm;
      break;
    case 0x5: /* c.fsd -> fsd rs2', uimm(rs1') */
      decoded->opcode = 0x27u;
      decoded->funct3 = 0x3u;
      decoded->rs1 = 8u + rv32emu_bits16(insn, 9, 7);
      decoded->rs2 = 8u + rv32emu_bits16(insn, 4, 2);
      imm = (rv32emu_bits16(insn, 12, 10) << 3) | (rv32emu_bits16(insn, 6, 5) << 6);
      decoded->imm_s = (int32_t)imm;
      break;
    case 0x6: /* c.sw -> sw rs2', uimm(rs1') */
      decoded->opcode = 0x23u;
      decoded->funct3 = 0x2u;
      decoded->rs1 = 8u + rv32emu_bits16(insn, 9, 7);
      decoded->rs2 = 8u + rv32emu_bits16(insn, 4, 2);
      imm = (rv32emu_bits16(insn, 6, 6) << 2) | (rv32emu_bits16(insn, 12, 10) << 3) |
            (rv32emu_bits16(insn, 5, 5) << 6);
      decoded->imm_s = (int32_t)imm;
      break;
    case 0x7: /* c.fsw -> fsw rs2', uimm(rs1') */
      decoded->opcode = 0x27u;
      decoded->funct3 = 0x2u;
      decoded->rs1 = 8u + rv32emu_bits16(insn, 9, 7);
      decoded->rs2 = 8u + rv32emu_bits16(insn, 4, 2);
      imm = (rv32emu_bits16(insn, 6, 6) << 2) | (rv32emu_bits16(insn, 12, 10) << 3) |
            (rv32emu_bits16(insn, 5, 5) << 6);
      decoded->imm_s = (int32_t)imm;
      break;
    default:
      return false;
    }
    break;
  case 0x1:
    switch (funct3) {
    case 0x0: /* c.addi/c.nop -> addi rd, rd, imm */
      rd = rv32emu_bits16(insn, 11, 7);
      decoded->opcode = 0x13u;
      decoded->rd = rd;
      decoded->funct3 = 0x0u;
      decoded->rs1 = rd;
      decoded->imm_i = rv32emu_imm_c_addi(insn);
      break;
    case 0x1: /* c.jal -> jal x1, imm */
      decoded->opcode = 0x6fu;
      decoded->rd = 1u;
      decoded->imm_j = rv32emu_imm_c_j(insn);
      break;
    case 0x2: /* c.li -> addi rd, x0, imm */
      decoded->opcode = 0x13u;
      decoded->rd = rv32emu_bits16(insn, 11, 7);
      decoded->funct3 = 0x0u;
      decoded->rs1 = 0u;
      decoded->imm_i = rv32emu_imm_c_addi(insn);
      break;
    case 0x3: /* c.addi16sp/c.lui */
      rd = rv32emu_bits16(insn, 11, 7);
      if (rd == 2u) {
        imm = (rv32emu_bits16(insn, 6, 6) << 4) | (rv32emu_bits16(insn, 2, 2) << 5) |
              (rv32emu_bits16(insn, 5, 5) << 6) | (rv32emu_bits16(insn, 4, 3) << 7) |
              (rv32emu_bits16(insn, 12, 12) << 9);
        if (imm == 0u) {
          return false;
        }
        decoded->opcode = 0x13u;
        decoded->rd = 2u;
        decoded->funct3 = 0x0u;
        decoded->rs1 = 2u;
        decoded->imm_i = (int32_t)rv32emu_sign_extend(imm, 10);
      } else {
        imm = (rv32emu_bits16(insn, 12, 12) << 5) | rv32emu_bits16(insn, 6, 2);
        if (rd == 0u || imm == 0u) {
          return false;
        }
        decoded->opcode = 0x37u;
        decoded->rd = rd;
        decoded->imm_u = (int32_t)((uint32_t)rv32emu_sign_extend(imm, 6) << 12);
      }
      break;
    case 0x4: /* shifts/logical/subset */
      rd = 8u + rv32emu_bits16(insn, 9, 7);
      switch (rv32emu_bits16(insn, 11, 10)) {
      case 0x0: /* c.srli */
        if (rv32emu_bits16(insn, 12, 12) != 0u) {
          return false;
        }
        decoded->opcode = 0x13u;
        decoded->rd = rd;
        decoded->funct3 = 0x5u;
        decoded->rs1 = rd;
        decoded->rs2 = rv32emu_bits16(insn, 6, 2);
        decoded->funct7 = 0x00u;
        break;
      case 0x1: /* c.srai */
        if (rv32emu_bits16(insn, 12, 12) != 0u) {
          return false;
        }
        decoded->opcode = 0x13u;
        decoded->rd = rd;
        decoded->funct3 = 0x5u;
        decoded->rs1 = rd;
        decoded->rs2 = rv32emu_bits16(insn, 6, 2);
        decoded->funct7 = 0x20u;
        break;
      case 0x2: /* c.andi */
        decoded->opcode = 0x13u;
        decoded->rd = rd;
        decoded->funct3 = 0x7u;
        decoded->rs1 = rd;
        decoded->imm_i = rv32emu_imm_c_addi(insn);
        break;
      case 0x3:
        if (rv32emu_bits16(insn, 12, 12) != 0u) {
          return false;
        }
        rs2 = 8u + rv32emu_bits16(insn, 4, 2);
        decoded->opcode = 0x33u;
        decoded->rd = rd;
        decoded->rs1 = rd;
        decoded->rs2 = rs2;
        switch (rv32emu_bits16(insn, 6, 5)) {
        case 0x0: /* c.sub */
          decoded->funct3 = 0x0u;
          decoded->funct7 = 0x20u;
          break;
        case 0x1: /* c.xor */
          decoded->funct3 = 0x4u;
          decoded->funct7 = 0x00u;
          break;
        case 0x2: /* c.or */
          decoded->funct3 = 0x6u;
          decoded->funct7 = 0x00u;
          break;
        case 0x3: /* c.and */
          decoded->funct3 = 0x7u;
          decoded->funct7 = 0x00u;
          break;
        default:
          return false;
        }
        break;
      default:
        return false;
      }
      break;
    case 0x5: /* c.j -> jal x0, imm */
      decoded->opcode = 0x6fu;
      decoded->rd = 0u;
      decoded->imm_j = rv32emu_imm_c_j(insn);
      break;
    case 0x6: /* c.beqz -> beq rs1', x0, imm */
      decoded->opcode = 0x63u;
      decoded->funct3 = 0x0u;
      decoded->rs1 = 8u + rv32emu_bits16(insn, 9, 7);
      decoded->rs2 = 0u;
      decoded->imm_b = rv32emu_imm_c_b(insn);
      break;
    case 0x7: /* c.bnez -> bne rs1', x0, imm */
      decoded->opcode = 0x63u;
      decoded->funct3 = 0x1u;
      decoded->rs1 = 8u + rv32emu_bits16(insn, 9, 7);
      decoded->rs2 = 0u;
      decoded->imm_b = rv32emu_imm_c_b(insn);
      break;
    default:
      return false;
    }
    break;
  case 0x2:
    switch (funct3) {
    case 0x0: /* c.slli */
      rd = rv32emu_bits16(insn, 11, 7);
      if (rd == 0u || rv32emu_bits16(insn, 12, 12) != 0u) {
        return false;
      }
      decoded->opcode = 0x13u;
      decoded->rd = rd;
      decoded->funct3 = 0x1u;
      decoded->rs1 = rd;
      decoded->rs2 = rv32emu_bits16(insn, 6, 2);
      decoded->funct7 = 0x00u;
      break;
    case 0x1: /* c.fldsp -> fld rd, uimm(x2) */
      rd = rv32emu_bits16(insn, 11, 7);
      if (rd == 0u) {
        return false;
      }
      decoded->opcode = 0x07u;
      decoded->rd = rd;
      decoded->funct3 = 0x3u;
      decoded->rs1 = 2u;
      imm = (rv32emu_bits16(insn, 4, 2) << 6) | (rv32emu_bits16(insn, 12, 12) << 5) |
            (rv32emu_bits16(insn, 6, 5) << 3);
      decoded->imm_i = (int32_t)imm;
      break;
    case 0x2: /* c.lwsp */
      rd = rv32emu_bits16(insn, 11, 7);
      if (rd == 0u) {
        return false;
      }
      decoded->opcode = 0x03u;
      decoded->rd = rd;
      decoded->funct3 = 0x2u;
      decoded->rs1 = 2u;
      imm = (rv32emu_bits16(insn, 6, 4) << 2) | (rv32emu_bits16(insn, 12, 12) << 5) |
            (rv32emu_bits16(insn, 3, 2) << 6);
      decoded->imm_i = (int32_t)imm;
      break;
    case 0x3: /* c.flwsp -> flw rd, uimm(x2) */
      rd = rv32emu_bits16(insn, 11, 7);
      if (rd == 0u) {
        return false;
      }
      decoded->opcode = 0x07u;
      decoded->rd = rd;
      decoded->funct3 = 0x2u;
      decoded->rs1 = 2u;
      imm = (rv32emu_bits16(insn, 6, 4) << 2) | (rv32emu_bits16(insn, 12, 12) << 5) |
            (rv32emu_bits16(insn, 3, 2) << 6);
      decoded->imm_i = (int32_t)imm;
      break;
    case 0x4:
      rd = rv32emu_bits16(insn, 11, 7);
      rs2 = rv32emu_bits16(insn, 6, 2);
      if (rv32emu_bits16(insn, 12, 12) == 0u) {
        if (rs2 == 0u) { /* c.jr */
          if (rd == 0u) {
            return false;
          }
          decoded->opcode = 0x67u;
          decoded->rd = 0u;
          decoded->funct3 = 0x0u;
          decoded->rs1 = rd;
          decoded->imm_i = 0;
        } else { /* c.mv */
          if (rd == 0u) {
            return false;
          }
          decoded->opcode = 0x33u;
          decoded->rd = rd;
          decoded->funct3 = 0x0u;
          decoded->rs1 = 0u;
          decoded->rs2 = rs2;
          decoded->funct7 = 0x00u;
        }
      } else {
        if (rd == 0u && rs2 == 0u) { /* c.ebreak */
          decoded->opcode = 0x73u;
          decoded->raw = 0x00100073u;
          break;
        }
        if (rs2 == 0u) { /* c.jalr */
          if (rd == 0u) {
            return false;
          }
          decoded->opcode = 0x67u;
          decoded->rd = 1u;
          decoded->funct3 = 0x0u;
          decoded->rs1 = rd;
          decoded->imm_i = 0;
        } else { /* c.add */
          if (rd == 0u) {
            return false;
          }
          decoded->opcode = 0x33u;
          decoded->rd = rd;
          decoded->funct3 = 0x0u;
          decoded->rs1 = rd;
          decoded->rs2 = rs2;
          decoded->funct7 = 0x00u;
        }
      }
      break;
    case 0x5: /* c.fsdsp -> fsd rs2, uimm(x2) */
      decoded->opcode = 0x27u;
      decoded->funct3 = 0x3u;
      decoded->rs1 = 2u;
      decoded->rs2 = rv32emu_bits16(insn, 6, 2);
      imm = (rv32emu_bits16(insn, 12, 10) << 3) | (rv32emu_bits16(insn, 9, 7) << 6);
      decoded->imm_s = (int32_t)imm;
      break;
    case 0x6: /* c.swsp */
      decoded->opcode = 0x23u;
      decoded->funct3 = 0x2u;
      decoded->rs1 = 2u;
      decoded->rs2 = rv32emu_bits16(insn, 6, 2);
      imm = (rv32emu_bits16(insn, 12, 9) << 2) | (rv32emu_bits16(insn, 8, 7) << 6);
      decoded->imm_s = (int32_t)imm;
      break;
    case 0x7: /* c.fswsp -> fsw rs2, uimm(x2) */
      decoded->opcode = 0x27u;
      decoded->funct3 = 0x2u;
      decoded->rs1 = 2u;
      decoded->rs2 = rv32emu_bits16(insn, 6, 2);
      imm = (rv32emu_bits16(insn, 12, 9) << 2) | (rv32emu_bits16(insn, 8, 7) << 6);
      decoded->imm_s = (int32_t)imm;
      break;
    default:
      return false;
    }
    break;
  default:
    return false;
  }

  if (decoded->opcode == 0u) {
    return false;
  }
  decoded->insn_class = rv32emu_classify_opcode(decoded->opcode);
  return true;
}
