#include "rv32emu.h"
#include "rv32emu_tb.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>

#define RV32EMU_HART_SLICE_INSTR 64u
#define RV32EMU_WORKER_COMMIT_BATCH 256u

bool rv32emu_exec_one(rv32emu_machine_t *m);

static int rv32emu_run_single_thread(rv32emu_machine_t *m, uint64_t max_instructions, bool use_tb,
                                     bool use_jit) {
  uint64_t executed = 0;
  uint32_t next_hart = 0;
  rv32emu_tb_cache_t tb_cache[RV32EMU_MAX_HARTS];

  for (uint32_t hart = 0u; hart < RV32EMU_MAX_HARTS; hart++) {
    rv32emu_tb_cache_reset(&tb_cache[hart]);
  }

  while (executed < max_instructions) {
    bool progressed = false;
    uint32_t checked;

    for (checked = 0u; checked < m->hart_count; checked++) {
      uint32_t hart = (next_hart + checked) % m->hart_count;

      if (!atomic_load_explicit(&m->harts[hart].running, memory_order_acquire)) {
        continue;
      }

      progressed = true;
      rv32emu_set_active_hart(m, hart);

      for (uint32_t slice = 0u;
           slice < RV32EMU_HART_SLICE_INSTR && executed < max_instructions &&
           atomic_load_explicit(&RV32EMU_CPU(m)->running, memory_order_acquire);) {
        uint32_t steps = 0u;
        bool jit_handled = false;

        if (rv32emu_check_pending_interrupt(m)) {
          if (!atomic_load_explicit(&RV32EMU_CPU(m)->running, memory_order_acquire)) {
            break;
          }
          slice += 1u;
          continue;
        }

        if (use_jit) {
          rv32emu_tb_jit_result_t jit_result =
              rv32emu_exec_tb_jit(m, &tb_cache[hart], max_instructions - executed);
          if (jit_result.status == RV32EMU_TB_JIT_RETIRED && jit_result.retired > 0u) {
            steps = jit_result.retired;
          } else if (jit_result.status == RV32EMU_TB_JIT_HANDLED_NO_RETIRE) {
            jit_handled = true;
          }
        }

        if (steps == 0u) {
          if (!jit_handled) {
            bool ok = false;
            if (use_tb) {
              ok = rv32emu_exec_one_tb(m, &tb_cache[hart]);
              if (!ok && atomic_load_explicit(&RV32EMU_CPU(m)->running, memory_order_acquire)) {
                ok = rv32emu_exec_one(m);
              }
            } else {
              ok = rv32emu_exec_one(m);
            }
            if (ok) {
              steps = 1u;
            }
          } else {
            slice += 1u;
            continue;
          }
        }

        if (steps != 0u) {
          executed += steps;
          slice += steps;
          if (slice > RV32EMU_HART_SLICE_INSTR) {
            slice = RV32EMU_HART_SLICE_INSTR;
          }
          continue;
        }

        if (!atomic_load_explicit(&RV32EMU_CPU(m)->running, memory_order_acquire)) {
          break;
        }
        slice += 1u;
      }

      next_hart = (hart + 1u) % m->hart_count;
      break;
    }

    if (!progressed) {
      break;
    }
  }

  rv32emu_set_active_hart(m, 0u);
  return (int)executed;
}

typedef struct {
  atomic_uint_fast64_t executed;
  atomic_bool stop;
} rv32emu_thread_state_t;

typedef struct {
  rv32emu_machine_t *m;
  rv32emu_thread_state_t *state;
  uint64_t max_instructions;
  uint32_t hartid;
  bool use_tb;
  bool use_jit;
} rv32emu_worker_ctx_t;

static bool rv32emu_worker_commit_executed(rv32emu_thread_state_t *state, uint64_t *local_executed,
                                           uint64_t max_instructions) {
  uint64_t executed_now;

  if (state == NULL || local_executed == NULL || *local_executed == 0u) {
    return false;
  }

  executed_now = atomic_fetch_add_explicit(&state->executed, *local_executed, memory_order_relaxed) +
                 *local_executed;
  *local_executed = 0u;
  if (executed_now >= max_instructions) {
    atomic_store_explicit(&state->stop, true, memory_order_release);
    return true;
  }
  return false;
}

static void *rv32emu_run_worker(void *opaque) {
  rv32emu_worker_ctx_t *ctx = (rv32emu_worker_ctx_t *)opaque;
  rv32emu_thread_state_t *state = ctx->state;
  rv32emu_cpu_t *cpu = rv32emu_hart_cpu(ctx->m, ctx->hartid);
  uint64_t local_executed = 0u;
  rv32emu_tb_cache_t tb_cache;

  if (cpu == NULL) {
    return NULL;
  }
  rv32emu_tb_cache_reset(&tb_cache);
  rv32emu_bind_thread_hart(ctx->m, ctx->hartid);

  for (;;) {
    if (atomic_load_explicit(&state->stop, memory_order_acquire)) {
      break;
    }
    uint64_t global_executed = atomic_load_explicit(&state->executed, memory_order_relaxed);
    if (global_executed + local_executed >= ctx->max_instructions) {
      atomic_store_explicit(&state->stop, true, memory_order_release);
      break;
    }
    if (!atomic_load_explicit(&cpu->running, memory_order_acquire)) {
      (void)rv32emu_worker_commit_executed(state, &local_executed, ctx->max_instructions);
      if (!rv32emu_any_hart_running(ctx->m)) {
        atomic_store_explicit(&state->stop, true, memory_order_release);
        break;
      } else {
        sched_yield();
        continue;
      }
    }
    if (rv32emu_check_pending_interrupt(ctx->m)) {
      if (!atomic_load_explicit(&cpu->running, memory_order_acquire) &&
          !rv32emu_any_hart_running(ctx->m)) {
        atomic_store_explicit(&state->stop, true, memory_order_release);
      }
      continue;
    }
    {
      uint32_t steps = 0u;
      bool jit_handled = false;
      if (ctx->use_jit) {
        uint64_t budget = ctx->max_instructions - (global_executed + local_executed);
        rv32emu_tb_jit_result_t jit_result = rv32emu_exec_tb_jit(ctx->m, &tb_cache, budget);
        if (jit_result.status == RV32EMU_TB_JIT_RETIRED && jit_result.retired > 0u) {
          steps = jit_result.retired;
        } else if (jit_result.status == RV32EMU_TB_JIT_HANDLED_NO_RETIRE) {
          jit_handled = true;
        }
      }

      if (steps == 0u) {
        if (!jit_handled) {
          bool ok = false;
          if (ctx->use_tb) {
            ok = rv32emu_exec_one_tb(ctx->m, &tb_cache);
            if (!ok && atomic_load_explicit(&cpu->running, memory_order_acquire)) {
              ok = rv32emu_exec_one(ctx->m);
            }
          } else {
            ok = rv32emu_exec_one(ctx->m);
          }
          if (ok) {
            steps = 1u;
          }
        } else {
          continue;
        }
      }

      if (steps != 0u) {
        local_executed += steps;
        if (local_executed >= RV32EMU_WORKER_COMMIT_BATCH) {
          (void)rv32emu_worker_commit_executed(state, &local_executed, ctx->max_instructions);
        }
        continue;
      }
    }

    if (!atomic_load_explicit(&cpu->running, memory_order_acquire) &&
        !rv32emu_any_hart_running(ctx->m)) {
      atomic_store_explicit(&state->stop, true, memory_order_release);
    }
  }

  (void)rv32emu_worker_commit_executed(state, &local_executed, ctx->max_instructions);
  rv32emu_flush_timer(ctx->m);
  rv32emu_unbind_thread_hart();
  return NULL;
}

static int rv32emu_run_threaded(rv32emu_machine_t *m, uint64_t max_instructions, bool use_tb,
                                bool use_jit) {
  pthread_t threads[RV32EMU_MAX_HARTS];
  rv32emu_worker_ctx_t workers[RV32EMU_MAX_HARTS];
  rv32emu_thread_state_t state;
  uint32_t hart;
  uint32_t started = 0;
  bool create_failed = false;
  uint64_t executed_total;

  atomic_store_explicit(&state.executed, 0u, memory_order_relaxed);
  atomic_store_explicit(&state.stop, false, memory_order_relaxed);
  m->threaded_exec_active = true;
  for (hart = 0u; hart < m->hart_count; hart++) {
    m->harts[hart].timer_batch_ticks = 0u;
  }

  for (hart = 0u; hart < m->hart_count; hart++) {
    workers[hart].m = m;
    workers[hart].state = &state;
    workers[hart].max_instructions = max_instructions;
    workers[hart].hartid = hart;
    workers[hart].use_tb = use_tb;
    workers[hart].use_jit = use_jit;

    if (pthread_create(&threads[hart], NULL, rv32emu_run_worker, &workers[hart]) != 0) {
      create_failed = true;
      break;
    }
    started++;
  }

  if (create_failed) {
    atomic_store_explicit(&state.stop, true, memory_order_release);
  }

  for (hart = 0u; hart < started; hart++) {
    (void)pthread_join(threads[hart], NULL);
  }
  m->threaded_exec_active = false;

  executed_total = atomic_load_explicit(&state.executed, memory_order_relaxed);

  if (create_failed && executed_total < max_instructions && rv32emu_any_hart_running(m)) {
    int tail = rv32emu_run_single_thread(m, max_instructions - executed_total, use_tb, use_jit);
    if (tail < 0) {
      return -1;
    }
    executed_total += (uint64_t)tail;
  }

  rv32emu_set_active_hart(m, 0u);
  return (int)executed_total;
}

int rv32emu_run(rv32emu_machine_t *m, uint64_t max_instructions) {
  const char *threaded_env;
  const char *tb_env;
  const char *jit_env;
  bool use_tb;
  bool use_jit;

  if (m == NULL) {
    return -1;
  }

  if (m->hart_count == 0u || m->hart_count > RV32EMU_MAX_HARTS) {
    return -1;
  }

  if (max_instructions == 0) {
    max_instructions = RV32EMU_DEFAULT_MAX_INSTR;
  }
  tb_env = getenv("RV32EMU_EXPERIMENTAL_TB");
  use_tb = (tb_env != NULL && tb_env[0] == '1');
  jit_env = getenv("RV32EMU_EXPERIMENTAL_JIT");
  use_jit = (jit_env != NULL && jit_env[0] == '1');

  if (m->hart_count == 1u) {
    return rv32emu_run_single_thread(m, max_instructions, use_tb, use_jit);
  }

  threaded_env = getenv("RV32EMU_EXPERIMENTAL_HART_THREADS");
  if (threaded_env == NULL || threaded_env[0] != '1') {
    return rv32emu_run_single_thread(m, max_instructions, use_tb, use_jit);
  }

  return rv32emu_run_threaded(m, max_instructions, use_tb, use_jit);
}
