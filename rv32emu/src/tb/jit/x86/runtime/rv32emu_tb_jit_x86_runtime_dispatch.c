#include "../../../../internal/tb_jit_internal.h"

#if defined(__x86_64__)
/* JIT retire accounting and pre-dispatch guard helpers. */
int rv32emu_jit_block_commit(rv32emu_machine_t *m, rv32emu_cpu_t *cpu, uint32_t next_pc,
                             uint32_t retired) {
  if (m == NULL || cpu == NULL || retired == 0u) {
    return 0;
  }

  cpu->pc = next_pc;
  cpu->x[0] = 0u;
  cpu->cycle += retired;
  cpu->instret += retired;
  for (uint32_t i = 0u; i < retired; i++) {
    rv32emu_step_timer(m);
  }

  g_rv32emu_jit_tls_total += retired;
  if (g_rv32emu_jit_tls_budget > retired) {
    g_rv32emu_jit_tls_budget -= retired;
  } else {
    g_rv32emu_jit_tls_budget = 0u;
  }

  return (int)g_rv32emu_jit_tls_total;
}

void rv32emu_jit_retire_prefix(rv32emu_machine_t *m, rv32emu_cpu_t *cpu, uint32_t retired) {
  if (m == NULL || cpu == NULL || retired == 0u) {
    return;
  }

  cpu->x[0] = 0u;
  cpu->cycle += retired;
  cpu->instret += retired;
  for (uint32_t i = 0u; i < retired; i++) {
    rv32emu_step_timer(m);
  }

  g_rv32emu_jit_tls_total += retired;
  if (g_rv32emu_jit_tls_budget > retired) {
    g_rv32emu_jit_tls_budget -= retired;
  } else {
    g_rv32emu_jit_tls_budget = 0u;
  }
}

uint32_t rv32emu_jit_result_or_no_retire(void) {
  if (g_rv32emu_jit_tls_total == 0u) {
    return UINT32_MAX;
  }
  return (uint32_t)g_rv32emu_jit_tls_total;
}

/*
 * Entry pre-check keeps JIT blocks interrupt-safe even when a pending IRQ
 * appears between runner polling and native block entry.
 *
 * Return convention:
 * - 0   : continue executing block
 * - -1  : handled, return to dispatcher with no guest retire
 */
int rv32emu_jit_pre_dispatch(rv32emu_machine_t *m, rv32emu_cpu_t *cpu) {
  if (m == NULL || cpu == NULL) {
    g_rv32emu_jit_tls_handled = true;
    return -1;
  }

  if (!atomic_load_explicit(&cpu->running, memory_order_acquire)) {
    g_rv32emu_jit_tls_handled = true;
    return -1;
  }

  if (rv32emu_check_pending_interrupt(m)) {
    g_rv32emu_jit_tls_handled = true;
    return -1;
  }

  if (!atomic_load_explicit(&cpu->running, memory_order_acquire)) {
    g_rv32emu_jit_tls_handled = true;
    return -1;
  }

  return 0;
}
#endif
