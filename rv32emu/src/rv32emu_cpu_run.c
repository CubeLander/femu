#include "rv32emu.h"
#include "rv32emu_tb.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>

#define RV32EMU_HART_SLICE_INSTR 64u
#define RV32EMU_WORKER_COMMIT_BATCH 256u
#define RV32EMU_JIT_NO_RETIRE_FALLBACK_THRESHOLD 64u
#define RV32EMU_INTERP_BURST_MAX 32u
#define RV32EMU_JIT_NOPROGRESS_COOLDOWN 1024u

bool rv32emu_exec_one(rv32emu_machine_t *m);

static bool rv32emu_env_bool(const char *name, bool default_value) {
  const char *value;

  if (name == NULL) {
    return default_value;
  }
  value = getenv(name);
  if (value == NULL || value[0] == '\0') {
    return default_value;
  }

  switch (value[0]) {
  case '1':
  case 'y':
  case 'Y':
  case 't':
  case 'T':
    return true;
  case '0':
  case 'n':
  case 'N':
  case 'f':
  case 'F':
    return false;
  default:
    return default_value;
  }
}

static uint32_t rv32emu_exec_interp_burst(rv32emu_machine_t *m, uint64_t budget) {
  uint32_t retired = 0u;
  uint32_t idle_spins = 0u;
  uint32_t max_steps;

  if (m == NULL || budget == 0u) {
    return 0u;
  }

  max_steps = (budget > RV32EMU_INTERP_BURST_MAX) ? RV32EMU_INTERP_BURST_MAX : (uint32_t)budget;
  while (retired < max_steps && atomic_load_explicit(&RV32EMU_CPU(m)->running, memory_order_acquire)) {
    uint32_t pc_before = RV32EMU_CPU(m)->pc;

    if (rv32emu_check_pending_interrupt(m)) {
      if (!atomic_load_explicit(&RV32EMU_CPU(m)->running, memory_order_acquire)) {
        break;
      }
      if (RV32EMU_CPU(m)->pc == pc_before) {
        idle_spins++;
        if (idle_spins >= 4u) {
          break;
        }
      } else {
        idle_spins = 0u;
      }
      continue;
    }

    if (!rv32emu_exec_one(m)) {
      break;
    }

    retired++;
    idle_spins = 0u;
  }

  return retired;
}

static int rv32emu_run_single_thread(rv32emu_machine_t *m, uint64_t max_instructions, bool use_tb,
                                     bool use_jit, bool jit_skip_mmode, bool jit_guard) {
  uint64_t executed = 0;
  uint32_t next_hart = 0;
  rv32emu_tb_cache_t tb_cache[RV32EMU_MAX_HARTS];
  uint32_t jit_no_retire_streak[RV32EMU_MAX_HARTS] = {0u};
  uint32_t jit_cooldown[RV32EMU_MAX_HARTS] = {0u};

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
        bool jit_allowed = false;

        if (rv32emu_check_pending_interrupt(m)) {
          if (!atomic_load_explicit(&RV32EMU_CPU(m)->running, memory_order_acquire)) {
            break;
          }
          slice += 1u;
          continue;
        }

        jit_allowed = use_jit &&
                      (!jit_skip_mmode || RV32EMU_CPU(m)->priv != RV32EMU_PRIV_M);
        if (jit_guard && jit_allowed && jit_cooldown[hart] != 0u) {
          jit_cooldown[hart]--;
        }
        if (jit_allowed && (!jit_guard || jit_cooldown[hart] == 0u)) {
          uint32_t jit_entry_pc = RV32EMU_CPU(m)->pc;
          rv32emu_tb_jit_result_t jit_result =
              rv32emu_exec_tb_jit(m, &tb_cache[hart], max_instructions - executed);
          if (jit_result.status == RV32EMU_TB_JIT_RETIRED && jit_result.retired > 0u) {
            steps = jit_result.retired;
            if (jit_guard) {
              jit_no_retire_streak[hart] = 0u;
              jit_cooldown[hart] = 0u;
            }
          } else if (jit_result.status == RV32EMU_TB_JIT_HANDLED_NO_RETIRE) {
            if (jit_guard) {
              if (RV32EMU_CPU(m)->pc == jit_entry_pc) {
                if (jit_no_retire_streak[hart] < UINT32_MAX) {
                  jit_no_retire_streak[hart]++;
                }
              } else {
                jit_no_retire_streak[hart] = 1u;
              }
              if (jit_no_retire_streak[hart] >= RV32EMU_JIT_NO_RETIRE_FALLBACK_THRESHOLD &&
                  atomic_load_explicit(&RV32EMU_CPU(m)->running, memory_order_acquire) &&
                  rv32emu_exec_one(m)) {
                steps = 1u;
                jit_no_retire_streak[hart] = 0u;
              } else {
                jit_handled = true;
              }
              if (jit_cooldown[hart] < RV32EMU_JIT_NOPROGRESS_COOLDOWN) {
                jit_cooldown[hart]++;
              }
            } else {
              jit_handled = true;
            }
          } else {
            if (jit_guard) {
              jit_no_retire_streak[hart] = 0u;
              jit_cooldown[hart] = RV32EMU_JIT_NOPROGRESS_COOLDOWN;
            }
          }
        }

        if (steps == 0u) {
          if (!jit_handled) {
            if (use_tb) {
              uint64_t tb_budget = max_instructions - executed;
              uint64_t slice_budget = RV32EMU_HART_SLICE_INSTR - slice;
              rv32emu_tb_block_result_t tb_result;

              if (tb_budget > slice_budget) {
                tb_budget = slice_budget;
              }
              tb_result = rv32emu_exec_tb_block(m, &tb_cache[hart], tb_budget);
              if (tb_result.status == RV32EMU_TB_BLOCK_RETIRED && tb_result.retired > 0u) {
                steps = tb_result.retired;
              } else if (tb_result.status == RV32EMU_TB_BLOCK_HANDLED_NO_RETIRE) {
                jit_handled = true;
              } else if (atomic_load_explicit(&RV32EMU_CPU(m)->running, memory_order_acquire)) {
                steps = rv32emu_exec_interp_burst(m, tb_budget);
              }
            } else {
              if (rv32emu_exec_one(m)) {
                steps = 1u;
              }
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
  bool jit_skip_mmode;
  bool jit_guard;
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
  uint32_t jit_no_retire_streak = 0u;
  uint32_t jit_cooldown = 0u;
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
      uint64_t budget = ctx->max_instructions - (global_executed + local_executed);
      bool jit_allowed = ctx->use_jit &&
                         (!ctx->jit_skip_mmode || cpu->priv != RV32EMU_PRIV_M);
      if (ctx->jit_guard && jit_allowed && jit_cooldown != 0u) {
        jit_cooldown--;
      }
      if (jit_allowed && (!ctx->jit_guard || jit_cooldown == 0u)) {
        uint32_t jit_entry_pc = cpu->pc;
        rv32emu_tb_jit_result_t jit_result = rv32emu_exec_tb_jit(ctx->m, &tb_cache, budget);
        if (jit_result.status == RV32EMU_TB_JIT_RETIRED && jit_result.retired > 0u) {
          steps = jit_result.retired;
          if (ctx->jit_guard) {
            jit_no_retire_streak = 0u;
            jit_cooldown = 0u;
          }
        } else if (jit_result.status == RV32EMU_TB_JIT_HANDLED_NO_RETIRE) {
          if (ctx->jit_guard) {
            if (cpu->pc == jit_entry_pc) {
              if (jit_no_retire_streak < UINT32_MAX) {
                jit_no_retire_streak++;
              }
            } else {
              jit_no_retire_streak = 1u;
            }
            if (jit_no_retire_streak >= RV32EMU_JIT_NO_RETIRE_FALLBACK_THRESHOLD &&
                atomic_load_explicit(&cpu->running, memory_order_acquire) &&
                rv32emu_exec_one(ctx->m)) {
              steps = 1u;
              jit_no_retire_streak = 0u;
            } else {
              jit_handled = true;
            }
            if (jit_cooldown < RV32EMU_JIT_NOPROGRESS_COOLDOWN) {
              jit_cooldown++;
            }
          } else {
            jit_handled = true;
          }
        } else {
          if (ctx->jit_guard) {
            jit_no_retire_streak = 0u;
            jit_cooldown = RV32EMU_JIT_NOPROGRESS_COOLDOWN;
          }
        }
      }

      if (steps == 0u) {
        if (!jit_handled) {
          if (ctx->use_tb) {
            rv32emu_tb_block_result_t tb_result = rv32emu_exec_tb_block(ctx->m, &tb_cache, budget);
            if (tb_result.status == RV32EMU_TB_BLOCK_RETIRED && tb_result.retired > 0u) {
              steps = tb_result.retired;
            } else if (tb_result.status == RV32EMU_TB_BLOCK_HANDLED_NO_RETIRE) {
              jit_handled = true;
            } else if (atomic_load_explicit(&cpu->running, memory_order_acquire)) {
              steps = rv32emu_exec_interp_burst(ctx->m, budget);
            }
          } else {
            if (rv32emu_exec_one(ctx->m)) {
              steps = 1u;
            }
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
                                bool use_jit, bool jit_skip_mmode, bool jit_guard) {
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
    workers[hart].jit_skip_mmode = jit_skip_mmode;
    workers[hart].jit_guard = jit_guard;

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
    int tail = rv32emu_run_single_thread(m, max_instructions - executed_total, use_tb, use_jit,
                                         jit_skip_mmode, jit_guard);
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
  bool use_tb;
  bool use_jit;
  bool jit_skip_mmode;
  bool jit_guard;

  if (m == NULL) {
    return -1;
  }

  if (m->hart_count == 0u || m->hart_count > RV32EMU_MAX_HARTS) {
    return -1;
  }

  if (max_instructions == 0) {
    max_instructions = RV32EMU_DEFAULT_MAX_INSTR;
  }
  use_tb = rv32emu_env_bool("RV32EMU_EXPERIMENTAL_TB", false);
  use_jit = rv32emu_env_bool("RV32EMU_EXPERIMENTAL_JIT", false);
  /*
   * JIT defaults are safety-first for Linux boot:
   * - skip M-mode by default (OpenSBI path stays interpreter/TB)
   * - enable no-progress guard by default
   *
   * Both can be overridden explicitly via env=0/1.
   */
  jit_skip_mmode = rv32emu_env_bool("RV32EMU_EXPERIMENTAL_JIT_SKIP_MMODE", use_jit);
  jit_guard = rv32emu_env_bool("RV32EMU_EXPERIMENTAL_JIT_GUARD", use_jit);

  if (m->hart_count == 1u) {
    return rv32emu_run_single_thread(m, max_instructions, use_tb, use_jit, jit_skip_mmode,
                                     jit_guard);
  }

  threaded_env = getenv("RV32EMU_EXPERIMENTAL_HART_THREADS");
  if (threaded_env == NULL || threaded_env[0] != '1') {
    return rv32emu_run_single_thread(m, max_instructions, use_tb, use_jit, jit_skip_mmode,
                                     jit_guard);
  }

  return rv32emu_run_threaded(m, max_instructions, use_tb, use_jit, jit_skip_mmode, jit_guard);
}
