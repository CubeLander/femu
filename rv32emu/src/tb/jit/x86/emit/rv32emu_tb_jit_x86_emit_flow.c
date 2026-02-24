#include "../../../../internal/tb_jit_internal.h"

#if defined(__x86_64__)
#include "rv32emu_tb_jit_x86_emit_primitives_base_impl.h"

rv32emu_tb_jit_fn_t rv32emu_jit_chain_next(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                                           rv32emu_tb_line_t *from);
rv32emu_tb_jit_fn_t rv32emu_jit_chain_next_pc(rv32emu_machine_t *m, rv32emu_cpu_t *cpu,
                                              uint32_t from_pc);

static bool rv32emu_emit_prologue(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_u8(e, 0x57u) && /* push rdi */
         rv32emu_emit_u8(e, 0x56u) && /* push rsi */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xecu) &&
         rv32emu_emit_u8(e, 0x08u) && /* sub rsp, 8 */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
         rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_pre_dispatch) && /* movabs rax, fn */
         rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) &&               /* call rax */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
         rv32emu_emit_u8(e, 0x08u) && /* add rsp, 8 */
         rv32emu_emit_mov_rsi_saved(e) && /* restore cpu ptr after helper call */
         rv32emu_emit_mov_rdi_saved(e) && /* restore machine ptr after helper call */
         rv32emu_emit_u8(e, 0x85u) && rv32emu_emit_u8(e, 0xc0u) && /* test eax, eax */
         rv32emu_emit_u8(e, 0x74u) && rv32emu_emit_u8(e, 0x03u) && /* jz +3 (continue) */
         rv32emu_emit_u8(e, 0x5eu) && rv32emu_emit_u8(e, 0x5fu) &&
         rv32emu_emit_u8(e, 0xc3u); /* pop rsi; pop rdi; ret */
}

static bool rv32emu_emit_epilogue(rv32emu_x86_emit_t *e, rv32emu_tb_line_t *line,
                                  uint32_t chain_from_pc, uint32_t next_pc, uint32_t retired) {
  if (line == NULL) {
    if (chain_from_pc != 0u) {
      return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xecu) &&
             rv32emu_emit_u8(e, 0x08u) && /* sub rsp, 8 */
             rv32emu_emit_u8(e, 0xbau) && rv32emu_emit_u32(e, next_pc) && /* mov edx, next_pc */
             rv32emu_emit_u8(e, 0xb9u) && rv32emu_emit_u32(e, retired) && /* mov ecx, retired */
             rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
             rv32emu_emit_u64(e,
                              (uint64_t)(uintptr_t)&rv32emu_jit_block_commit) && /* movabs rax, fn */
             rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) && /* call rax */
             rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
             rv32emu_emit_u8(e, 0x08u) && /* add rsp, 8 */
             rv32emu_emit_u8(e, 0x50u) && /* push rax (save cumulative retired) */
             rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x74u) &&
             rv32emu_emit_u8(e, 0x24u) && rv32emu_emit_u8(e, 0x08u) && /* mov rsi, [rsp + 8] */
             rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x7cu) &&
             rv32emu_emit_u8(e, 0x24u) && rv32emu_emit_u8(e, 0x10u) && /* mov rdi, [rsp + 16] */
             rv32emu_emit_u8(e, 0xbau) && rv32emu_emit_u32(e, chain_from_pc) && /* mov edx, pc */
             rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
             rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_chain_next_pc) &&
             rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) && /* call rax */
             rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x85u) &&
             rv32emu_emit_u8(e, 0xc0u) && /* test rax, rax */
             rv32emu_emit_u8(e, 0x75u) && rv32emu_emit_u8(e, 0x04u) && /* jne +4 */
             rv32emu_emit_u8(e, 0x58u) && /* pop rax */
             rv32emu_emit_u8(e, 0x5eu) && /* pop rsi */
             rv32emu_emit_u8(e, 0x5fu) && /* pop rdi */
             rv32emu_emit_u8(e, 0xc3u) && /* ret */
             rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
             rv32emu_emit_u8(e, 0x08u) && /* add rsp, 8 (drop saved retired) */
             rv32emu_emit_u8(e, 0x5eu) && /* pop rsi */
             rv32emu_emit_u8(e, 0x5fu) && /* pop rdi */
             rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xe0u); /* jmp rax */
    }

    return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xecu) &&
           rv32emu_emit_u8(e, 0x08u) && /* sub rsp, 8 */
           rv32emu_emit_u8(e, 0xbau) && rv32emu_emit_u32(e, next_pc) && /* mov edx, next_pc */
           rv32emu_emit_u8(e, 0xb9u) && rv32emu_emit_u32(e, retired) && /* mov ecx, retired */
           rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
           rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_block_commit) && /* movabs rax, fn */
           rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) && /* call rax */
           rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
           rv32emu_emit_u8(e, 0x08u) && /* add rsp, 8 */
           rv32emu_emit_u8(e, 0x5eu) && /* pop rsi */
           rv32emu_emit_u8(e, 0x5fu) && /* pop rdi */
           rv32emu_emit_u8(e, 0xc3u);  /* ret */
  }

  return rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xecu) &&
         rv32emu_emit_u8(e, 0x08u) && /* sub rsp, 8 */
         rv32emu_emit_u8(e, 0xbau) && rv32emu_emit_u32(e, next_pc) && /* mov edx, next_pc */
         rv32emu_emit_u8(e, 0xb9u) && rv32emu_emit_u32(e, retired) && /* mov ecx, retired */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
         rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_block_commit) && /* movabs rax, fn */
         rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) && /* call rax */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
         rv32emu_emit_u8(e, 0x08u) && /* add rsp, 8 */
         rv32emu_emit_u8(e, 0x50u) && /* push rax (save cumulative retired) */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x74u) &&
         rv32emu_emit_u8(e, 0x24u) && rv32emu_emit_u8(e, 0x08u) && /* mov rsi, [rsp + 8] */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x8bu) && rv32emu_emit_u8(e, 0x7cu) &&
         rv32emu_emit_u8(e, 0x24u) && rv32emu_emit_u8(e, 0x10u) && /* mov rdi, [rsp + 16] */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xbau) &&
         rv32emu_emit_u64(e, (uint64_t)(uintptr_t)line) && /* movabs rdx, line */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0xb8u) &&
         rv32emu_emit_u64(e, (uint64_t)(uintptr_t)&rv32emu_jit_chain_next) && /* movabs rax, fn */
         rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xd0u) && /* call rax */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x85u) &&
         rv32emu_emit_u8(e, 0xc0u) && /* test rax, rax */
         rv32emu_emit_u8(e, 0x75u) && rv32emu_emit_u8(e, 0x04u) && /* jne +4 */
         rv32emu_emit_u8(e, 0x58u) && /* pop rax */
         rv32emu_emit_u8(e, 0x5eu) && /* pop rsi */
         rv32emu_emit_u8(e, 0x5fu) && /* pop rdi */
         rv32emu_emit_u8(e, 0xc3u) && /* ret */
         rv32emu_emit_u8(e, 0x48u) && rv32emu_emit_u8(e, 0x83u) && rv32emu_emit_u8(e, 0xc4u) &&
         rv32emu_emit_u8(e, 0x08u) && /* add rsp, 8 (drop saved retired) */
         rv32emu_emit_u8(e, 0x5eu) && /* pop rsi */
         rv32emu_emit_u8(e, 0x5fu) && /* pop rdi */
         rv32emu_emit_u8(e, 0xffu) && rv32emu_emit_u8(e, 0xe0u); /* jmp rax */
}

bool rv32emu_jit_emit_prologue(rv32emu_x86_emit_t *e) {
  return rv32emu_emit_prologue(e);
}

bool rv32emu_jit_emit_epilogue(rv32emu_x86_emit_t *e, rv32emu_tb_line_t *line,
                               uint32_t chain_from_pc, uint32_t next_pc, uint32_t retired) {
  return rv32emu_emit_epilogue(e, line, chain_from_pc, next_pc, retired);
}
#endif
