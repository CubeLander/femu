/* Register/memory ALU primitive encoders used by lowering paths. */
static bool rv32emu_emit_mov_eax_mem_rsi(rv32emu_x86_emit_t *e, uint32_t disp32) {
  return rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x86u) && rv32emu_emit_u32(e, disp32);
}

static bool rv32emu_emit_mov_ecx_mem_rsi(rv32emu_x86_emit_t *e, uint32_t disp32) {
  return rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x8eu) && rv32emu_emit_u32(e, disp32);
}

static bool rv32emu_emit_mov_mem_rsi_eax(rv32emu_x86_emit_t *e, uint32_t disp32) {
  return rv32emu_emit_u8(e, 0x89u) && rv32emu_emit_u8(e, 0x86u) && rv32emu_emit_u32(e, disp32);
}

static bool rv32emu_emit_add_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x05u) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_add_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x01u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_sub_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x29u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_xor_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x31u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_or_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x09u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_and_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x21u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_xor_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x35u) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_or_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x0du) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_and_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x25u) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_cmp_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0x3du) && rv32emu_emit_u32(e, imm32);
}

static bool rv32emu_emit_cmp_eax_ecx(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x39u) && rv32emu_emit_u8(e, 0xc8u);
}

static bool rv32emu_emit_setl_al(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x0fu) && rv32emu_emit_u8(e, 0x9cu) && rv32emu_emit_u8(e, 0xc0u);
}

static bool rv32emu_emit_setb_al(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x0fu) && rv32emu_emit_u8(e, 0x92u) && rv32emu_emit_u8(e, 0xc0u);
}

static bool rv32emu_emit_movzx_eax_al(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x0fu) && rv32emu_emit_u8(e, 0xb6u) && rv32emu_emit_u8(e, 0xc0u);
}

static bool rv32emu_emit_shl_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt) {
  return rv32emu_emit_u8(e, 0xc1u) && rv32emu_emit_u8(e, 0xe0u) && rv32emu_emit_u8(e, shamt);
}

static bool rv32emu_emit_shr_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt) {
  return rv32emu_emit_u8(e, 0xc1u) && rv32emu_emit_u8(e, 0xe8u) && rv32emu_emit_u8(e, shamt);
}

static bool rv32emu_emit_sar_eax_imm8(rv32emu_x86_emit_t *e, uint8_t shamt) {
  return rv32emu_emit_u8(e, 0xc1u) && rv32emu_emit_u8(e, 0xf8u) && rv32emu_emit_u8(e, shamt);
}

static bool rv32emu_emit_shl_eax_cl(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0xd3u) && rv32emu_emit_u8(e, 0xe0u);
}

static bool rv32emu_emit_shr_eax_cl(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0xd3u) && rv32emu_emit_u8(e, 0xe8u);
}

static bool rv32emu_emit_sar_eax_cl(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0xd3u) && rv32emu_emit_u8(e, 0xf8u);
}

static bool rv32emu_emit_mov_eax_imm32(rv32emu_x86_emit_t *e, uint32_t imm32) {
  return rv32emu_emit_u8(e, 0xb8u) && rv32emu_emit_u32(e, imm32);
}
