#include "../../../../internal/tb_jit_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(__x86_64__)
static rv32emu_jit_async_mgr_t g_rv32emu_jit_async_mgr = {
    .workers = {0},
    .pending = NULL,
    .done = NULL,
    .worker_count = 0u,
    .queue_cap = 0u,
    .pending_head = 0u,
    .pending_tail = 0u,
    .pending_count = 0u,
    .done_head = 0u,
    .done_tail = 0u,
    .done_count = 0u,
    .running = false,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .pending_cv = PTHREAD_COND_INITIALIZER,
    .once = PTHREAD_ONCE_INIT,
};

/* Background JIT compile queue, worker, and apply/drain path. */
static void *rv32emu_jit_async_worker_main(void *opaque) {
  rv32emu_jit_async_mgr_t *mgr = (rv32emu_jit_async_mgr_t *)opaque;

  for (;;) {
    rv32emu_jit_async_job_t job;
    rv32emu_jit_async_done_t done;
    rv32emu_jit_compiled_artifact_t artifact;
    uint8_t template_jit_count;
    uint64_t template_sig;
    bool template_key_ready;
    bool ok;

    if (mgr == NULL) {
      return NULL;
    }

    if (pthread_mutex_lock(&mgr->lock) != 0) {
      return NULL;
    }
    while (mgr->running && mgr->pending_count == 0u) {
      (void)pthread_cond_wait(&mgr->pending_cv, &mgr->lock);
    }
    if (!mgr->running) {
      (void)pthread_mutex_unlock(&mgr->lock);
      return NULL;
    }

    job = mgr->pending[mgr->pending_head];
    mgr->pending_head = (mgr->pending_head + 1u) % mgr->queue_cap;
    mgr->pending_count--;
    (void)pthread_mutex_unlock(&mgr->lock);

    memset(&artifact, 0, sizeof(artifact));
    template_jit_count = 0u;
    template_sig = 0u;
    template_key_ready = false;
    if (job.portable) {
      template_key_ready = rv32emu_tb_jit_template_key_public(
          job.decoded, job.pcs, job.count, job.max_block_insns, job.min_prefix_insns,
          &template_jit_count, &template_sig);
      if (template_key_ready &&
          rv32emu_jit_template_lookup_public(job.decoded, job.pcs, template_jit_count, template_sig,
                                             &artifact)) {
        ok = true;
      } else if (template_key_ready &&
                 rv32emu_jit_struct_template_lookup_public(job.decoded, template_jit_count,
                                                           job.start_pc, &artifact)) {
        ok = true;
      } else {
        ok = rv32emu_tb_compile_jit_from_snapshot_public(
            job.decoded, job.pcs, job.count, NULL, job.start_pc, job.max_block_insns,
            job.min_prefix_insns, &artifact);
        if (ok && template_key_ready && artifact.jit_count == template_jit_count) {
          rv32emu_jit_template_store_public(job.decoded, job.pcs, template_jit_count, template_sig,
                                            &artifact);
          rv32emu_jit_struct_template_store_public(job.decoded, template_jit_count, &artifact);
        }
      }
    } else {
      ok = rv32emu_tb_compile_jit_from_snapshot_public(
          job.decoded, job.pcs, job.count, job.line, job.start_pc, job.max_block_insns,
          job.min_prefix_insns, &artifact);
    }
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_JOBS_COMPILED);

    done.line = job.line;
    done.start_pc = job.start_pc;
    done.generation = job.generation;
    done.prefix_sig = 0u;
    done.portable = job.portable;
    done.success = ok;
    if (ok) {
      done.artifact = artifact;
      if (job.portable) {
        if (template_key_ready && template_jit_count == artifact.jit_count) {
          done.prefix_sig = template_sig;
        } else {
          done.prefix_sig =
              rv32emu_tb_prefix_signature_public(job.decoded, job.pcs, artifact.jit_count);
        }
      }
    } else {
      memset(&done.artifact, 0, sizeof(done.artifact));
    }

    if (pthread_mutex_lock(&mgr->lock) != 0) {
      return NULL;
    }
    if (mgr->done_count < mgr->queue_cap) {
      mgr->done[mgr->done_tail] = done;
      mgr->done_tail = (mgr->done_tail + 1u) % mgr->queue_cap;
      mgr->done_count++;
    } else {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_JOBS_DROPPED);
    }
    (void)pthread_mutex_unlock(&mgr->lock);
  }
}

static void rv32emu_jit_async_init_once(void) {
  rv32emu_jit_async_mgr_t *mgr = &g_rv32emu_jit_async_mgr;
  uint32_t worker_count;
  uint32_t queue_cap;

  if (!rv32emu_tb_async_env_enabled()) {
    return;
  }

  worker_count = rv32emu_tb_async_env_workers();
  queue_cap = rv32emu_tb_async_env_queue();
  if (worker_count == 0u || queue_cap < 2u) {
    return;
  }

  mgr->pending = (rv32emu_jit_async_job_t *)calloc((size_t)queue_cap, sizeof(*mgr->pending));
  mgr->done = (rv32emu_jit_async_done_t *)calloc((size_t)queue_cap, sizeof(*mgr->done));
  if (mgr->pending == NULL || mgr->done == NULL) {
    free(mgr->pending);
    free(mgr->done);
    mgr->pending = NULL;
    mgr->done = NULL;
    return;
  }

  mgr->queue_cap = queue_cap;
  mgr->pending_head = 0u;
  mgr->pending_tail = 0u;
  mgr->pending_count = 0u;
  mgr->done_head = 0u;
  mgr->done_tail = 0u;
  mgr->done_count = 0u;
  mgr->worker_count = 0u;
  mgr->running = true;

  for (uint32_t i = 0u; i < worker_count; i++) {
    if (pthread_create(&mgr->workers[i], NULL, rv32emu_jit_async_worker_main, mgr) != 0) {
      break;
    }
    mgr->worker_count++;
  }

  if (mgr->worker_count == 0u) {
    mgr->running = false;
    free(mgr->pending);
    free(mgr->done);
    mgr->pending = NULL;
    mgr->done = NULL;
    mgr->queue_cap = 0u;
  }
}

bool rv32emu_jit_async_running(void) {
  rv32emu_jit_async_mgr_t *mgr = &g_rv32emu_jit_async_mgr;
  (void)pthread_once(&mgr->once, rv32emu_jit_async_init_once);
  return mgr->running && mgr->queue_cap != 0u && mgr->worker_count != 0u && mgr->pending != NULL &&
         mgr->done != NULL;
}

static bool rv32emu_jit_async_enqueue_job(const rv32emu_jit_async_job_t *job) {
  rv32emu_jit_async_mgr_t *mgr = &g_rv32emu_jit_async_mgr;
  bool ok = false;

  if (job == NULL || !rv32emu_jit_async_running()) {
    return false;
  }
  if (pthread_mutex_lock(&mgr->lock) != 0) {
    return false;
  }
  if (mgr->running && mgr->pending_count < mgr->queue_cap) {
    mgr->pending[mgr->pending_tail] = *job;
    mgr->pending_tail = (mgr->pending_tail + 1u) % mgr->queue_cap;
    mgr->pending_count++;
    ok = true;
    (void)pthread_cond_signal(&mgr->pending_cv);
  }
  (void)pthread_mutex_unlock(&mgr->lock);

  if (ok) {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_JOBS_ENQUEUED);
  } else {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_JOBS_DROPPED);
  }
  return ok;
}

static bool rv32emu_jit_async_pop_done(rv32emu_jit_async_done_t *done) {
  rv32emu_jit_async_mgr_t *mgr = &g_rv32emu_jit_async_mgr;

  if (done == NULL || !rv32emu_jit_async_running()) {
    return false;
  }
  if (pthread_mutex_lock(&mgr->lock) != 0) {
    return false;
  }
  if (!mgr->running || mgr->done_count == 0u) {
    (void)pthread_mutex_unlock(&mgr->lock);
    return false;
  }

  *done = mgr->done[mgr->done_head];
  mgr->done_head = (mgr->done_head + 1u) % mgr->queue_cap;
  mgr->done_count--;
  (void)pthread_mutex_unlock(&mgr->lock);
  return true;
}

bool rv32emu_jit_async_is_busy(uint8_t busy_pct) {
  rv32emu_jit_async_mgr_t *mgr = &g_rv32emu_jit_async_mgr;
  uint32_t cap = 0u;
  uint32_t pending = 0u;
  uint32_t done = 0u;
  uint32_t depth = 0u;

  if (busy_pct == 0u || busy_pct > 100u || !rv32emu_jit_async_running()) {
    return false;
  }
  if (pthread_mutex_lock(&mgr->lock) != 0) {
    return false;
  }
  if (mgr->running && mgr->queue_cap != 0u) {
    cap = mgr->queue_cap;
    pending = mgr->pending_count;
    done = mgr->done_count;
  }
  (void)pthread_mutex_unlock(&mgr->lock);

  if (cap == 0u) {
    return false;
  }

  depth = pending + done;
  return depth * 100u >= cap * (uint32_t)busy_pct;
}

bool rv32emu_tb_jit_async_supported(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache) {
  if (m == NULL || cache == NULL || !cache->jit_async_enabled) {
    return false;
  }
  if (m->hart_count != 1u || m->threaded_exec_active) {
    return false;
  }
  return rv32emu_jit_async_running();
}

/* Async result application and stale-path accounting. */
typedef enum {
  RV32EMU_ASYNC_APPLY_DIRECT = 0,
  RV32EMU_ASYNC_APPLY_RECYCLED = 1,
  RV32EMU_ASYNC_STALE_NONPORTABLE = 2,
  RV32EMU_ASYNC_STALE_NOT_SUCCESS = 3,
  RV32EMU_ASYNC_STALE_LOOKUP_MISS = 4,
  RV32EMU_ASYNC_STALE_STATE_MISMATCH = 5,
  RV32EMU_ASYNC_STALE_SIG_MISMATCH = 6,
} rv32emu_jit_async_apply_result_t;

static rv32emu_jit_async_apply_result_t
rv32emu_tb_try_apply_async_done(rv32emu_tb_cache_t *cache, const rv32emu_jit_async_done_t *done) {
  rv32emu_tb_line_t *line;

  if (cache == NULL || done == NULL) {
    return RV32EMU_ASYNC_STALE_LOOKUP_MISS;
  }

  line = done->line;
  if (line != NULL && line->jit_generation == done->generation &&
      line->jit_state == RV32EMU_JIT_STATE_QUEUED) {
    line->jit_tried = true;
    if (done->success) {
      rv32emu_tb_line_apply_jit_public(line, &done->artifact);
    } else {
      rv32emu_tb_line_clear_jit_public(line, RV32EMU_JIT_STATE_FAILED);
    }
    return RV32EMU_ASYNC_APPLY_DIRECT;
  }

  if (!done->portable) {
    return RV32EMU_ASYNC_STALE_NONPORTABLE;
  }
  if (!done->success || done->artifact.jit_count == 0u || done->prefix_sig == 0u) {
    return RV32EMU_ASYNC_STALE_NOT_SUCCESS;
  }

  line = rv32emu_tb_find_cached_line_public(cache, done->start_pc);
  if (line == NULL || !line->valid || line->start_pc != done->start_pc ||
      line->count < done->artifact.jit_count) {
    return RV32EMU_ASYNC_STALE_LOOKUP_MISS;
  }
  if (line->jit_state != RV32EMU_JIT_STATE_NONE && line->jit_state != RV32EMU_JIT_STATE_QUEUED) {
    return RV32EMU_ASYNC_STALE_STATE_MISMATCH;
  }
  if (rv32emu_tb_prefix_signature_public(line->decoded, line->pcs, done->artifact.jit_count) !=
      done->prefix_sig) {
    return RV32EMU_ASYNC_STALE_SIG_MISMATCH;
  }

  line->jit_tried = true;
  rv32emu_tb_line_apply_jit_public(line, &done->artifact);
  return RV32EMU_ASYNC_APPLY_RECYCLED;
}

void rv32emu_tb_jit_async_drain(rv32emu_machine_t *m, rv32emu_tb_cache_t *cache) {
  rv32emu_jit_async_apply_result_t apply_result;
  rv32emu_jit_async_done_t done;

  if (m == NULL || cache == NULL || !rv32emu_jit_async_running()) {
    return;
  }

  while (rv32emu_jit_async_pop_done(&done)) {
    apply_result = rv32emu_tb_try_apply_async_done(cache, &done);
    if (apply_result == RV32EMU_ASYNC_APPLY_DIRECT) {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_RESULTS_APPLIED);
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_APPLIED_DIRECT);
      continue;
    }
    if (apply_result == RV32EMU_ASYNC_APPLY_RECYCLED) {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_RESULTS_APPLIED);
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_APPLIED_RECYCLED);
      continue;
    }

    if (apply_result == RV32EMU_ASYNC_STALE_NONPORTABLE) {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_STALE_NONPORTABLE);
    } else if (apply_result == RV32EMU_ASYNC_STALE_NOT_SUCCESS) {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_STALE_NOT_SUCCESS);
    } else if (apply_result == RV32EMU_ASYNC_STALE_LOOKUP_MISS) {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_STALE_LOOKUP_MISS);
    } else if (apply_result == RV32EMU_ASYNC_STALE_STATE_MISMATCH) {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_STALE_STATE_MISMATCH);
    } else if (apply_result == RV32EMU_ASYNC_STALE_SIG_MISMATCH) {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_STALE_SIG_MISMATCH);
    }
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_RESULTS_STALE);
  }
}

static bool rv32emu_tb_jit_async_block_has_helpers(const rv32emu_tb_cache_t *cache,
                                                   const rv32emu_tb_line_t *line) {
  uint8_t max_jit_insns = RV32EMU_JIT_DEFAULT_MAX_INSNS_PER_BLOCK;

  if (cache == NULL || line == NULL || !line->valid || line->count == 0u) {
    return true;
  }
  if (cache->jit_max_block_insns != 0u) {
    max_jit_insns = cache->jit_max_block_insns;
  }

  for (uint8_t i = 0u; i < line->count && i < max_jit_insns; i++) {
    uint32_t opcode;

    if (!rv32emu_jit_insn_supported_public(&line->decoded[i])) {
      break;
    }
    opcode = line->decoded[i].opcode;
    if (opcode == 0x03u || opcode == 0x23u || opcode == 0x63u || opcode == 0x67u ||
        opcode == 0x6fu) {
      return true;
    }
  }

  return false;
}

/* Async scheduling entry points from the foreground TB path. */
void rv32emu_tb_async_force_sync_compile(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line) {
  if (cache == NULL || line == NULL || !line->valid || line->count == 0u) {
    return;
  }
  if (rv32emu_jit_pool_is_exhausted_public()) {
    line->jit_tried = true;
    rv32emu_tb_line_clear_jit_public(line, RV32EMU_JIT_STATE_FAILED);
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_SYNC_FALLBACKS);
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_ALLOC);
    return;
  }
  line->jit_generation = rv32emu_tb_next_jit_generation_public();
  line->jit_tried = false;
  rv32emu_tb_line_clear_jit_public(line, RV32EMU_JIT_STATE_NONE);
  rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_SYNC_FALLBACKS);
  (void)rv32emu_tb_try_compile_jit_public(cache, line);
}

bool rv32emu_tb_queue_jit_compile_async(rv32emu_tb_cache_t *cache, rv32emu_tb_line_t *line,
                                        bool prefetch_hint) {
  rv32emu_jit_async_job_t job;
  rv32emu_jit_compiled_artifact_t artifact;
  uint8_t template_jit_count = 0u;
  uint64_t template_sig = 0u;

  if (cache == NULL || line == NULL || !line->valid || line->count == 0u ||
      line->jit_state != RV32EMU_JIT_STATE_NONE) {
    return false;
  }
  if (cache->jit_async_recycle && cache->jit_template_fast_apply &&
      rv32emu_tb_jit_template_key_public(line->decoded, line->pcs, line->count,
                                         cache->jit_max_block_insns,
                                         cache->jit_min_prefix_insns, &template_jit_count,
                                         &template_sig) &&
      rv32emu_jit_template_lookup_public(line->decoded, line->pcs, template_jit_count,
                                         template_sig, &artifact)) {
    line->jit_tried = true;
    rv32emu_tb_line_apply_jit_public(line, &artifact);
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_TEMPLATE_APPLIED);
    return true;
  }
  if (rv32emu_jit_pool_is_exhausted_public()) {
    line->jit_tried = true;
    rv32emu_tb_line_clear_jit_public(line, RV32EMU_JIT_STATE_FAILED);
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_ALLOC);
    return false;
  }
  if (cache->jit_async_busy_pct != 0u && rv32emu_jit_async_is_busy(cache->jit_async_busy_pct)) {
    if (prefetch_hint) {
      rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_ASYNC_JOBS_DROPPED);
      return false;
    }
    if (!cache->jit_async_foreground_sync) {
      return false;
    }
  }
  if (!cache->jit_async_allow_helpers && rv32emu_tb_jit_async_block_has_helpers(cache, line)) {
    line->jit_tried = true;
    rv32emu_tb_line_clear_jit_public(line, RV32EMU_JIT_STATE_FAILED);
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_UNSUPPORTED_PREFIX);
    return false;
  }

  memset(&job, 0, sizeof(job));
  job.line = line;
  job.start_pc = line->start_pc;
  job.generation = line->jit_generation;
  job.portable = cache->jit_async_recycle;
  job.count = line->count;
  job.max_block_insns = cache->jit_max_block_insns;
  job.min_prefix_insns = cache->jit_min_prefix_insns;
  memcpy(job.pcs, line->pcs, sizeof(job.pcs));
  memcpy(job.decoded, line->decoded, sizeof(job.decoded));

  if (!rv32emu_jit_async_enqueue_job(&job)) {
    return false;
  }

  line->jit_tried = true;
  rv32emu_tb_line_clear_jit_public(line, RV32EMU_JIT_STATE_QUEUED);
  return true;
}
#endif
