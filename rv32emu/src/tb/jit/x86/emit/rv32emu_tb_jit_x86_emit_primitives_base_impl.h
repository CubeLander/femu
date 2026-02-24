/* Base byte emitters and shared frame-register helpers. */
static bool rv32emu_emit_u8(rv32emu_x86_emit_t *e, uint8_t v) {
  if (e == NULL || e->p == NULL || e->p >= e->end) {
    return false;
  }
  *e->p++ = v;
  return true;
}

static bool rv32emu_emit_u32(rv32emu_x86_emit_t *e, uint32_t v) {
  if (!rv32emu_emit_u8(e, (uint8_t)(v & 0xffu))) {
    return false;
  }
  if (!rv32emu_emit_u8(e, (uint8_t)((v >> 8) & 0xffu))) {
    return false;
  }
  if (!rv32emu_emit_u8(e, (uint8_t)((v >> 16) & 0xffu))) {
    return false;
  }
  return rv32emu_emit_u8(e, (uint8_t)((v >> 24) & 0xffu));
}

static bool rv32emu_emit_u64(rv32emu_x86_emit_t *e, uint64_t v) {
  if (!rv32emu_emit_u32(e, (uint32_t)(v & 0xffffffffu))) {
    return false;
  }
  return rv32emu_emit_u32(e, (uint32_t)(v >> 32));
}

static bool rv32emu_emit_mov_rsi_saved(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x34u) &&
         rv32emu_emit_u8(e, 0x24u);
}

static bool rv32emu_emit_mov_rdi_saved(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x7cu) &&
         rv32emu_emit_u8(e, 0x24u) && rv32emu_emit_u8(e, 0x08u);
}
