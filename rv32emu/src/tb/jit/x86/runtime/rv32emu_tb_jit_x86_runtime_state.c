#include "../../../../internal/tb_jit_internal.h"

#if defined(__x86_64__)
#include <stdatomic.h>
#include <string.h>
#include <sys/mman.h>

static rv32emu_jit_pool_t g_rv32emu_jit_pool = {
    .base = NULL,
    .cap = 0u,
    .used = 0u,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .once = PTHREAD_ONCE_INIT,
};
static atomic_bool g_rv32emu_jit_pool_exhausted = ATOMIC_VAR_INIT(false);
static rv32emu_jit_template_cache_t g_rv32emu_jit_template_cache = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
};
static rv32emu_jit_struct_template_cache_t g_rv32emu_jit_struct_template_cache = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

_Thread_local rv32emu_tb_cache_t *g_rv32emu_jit_tls_cache = NULL;
_Thread_local uint64_t g_rv32emu_jit_tls_budget = 0u;
_Thread_local uint64_t g_rv32emu_jit_tls_total = 0u;
_Thread_local bool g_rv32emu_jit_tls_handled = false;

static void rv32emu_jit_pool_init_once(void) {
  size_t pool_size = rv32emu_tb_jit_pool_size_public();
  void *mem = mmap(NULL, pool_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
  if (mem == MAP_FAILED) {
    g_rv32emu_jit_pool.base = NULL;
    g_rv32emu_jit_pool.cap = 0u;
    g_rv32emu_jit_pool.used = 0u;
    atomic_store_explicit(&g_rv32emu_jit_pool_exhausted, true, memory_order_relaxed);
    return;
  }

  g_rv32emu_jit_pool.base = (uint8_t *)mem;
  g_rv32emu_jit_pool.cap = pool_size;
  g_rv32emu_jit_pool.used = 0u;
  atomic_store_explicit(&g_rv32emu_jit_pool_exhausted, false, memory_order_relaxed);
}

bool rv32emu_jit_pool_is_exhausted(void) {
  return atomic_load_explicit(&g_rv32emu_jit_pool_exhausted, memory_order_relaxed);
}

void *rv32emu_jit_alloc(size_t bytes) {
  void *out = NULL;

  if (bytes == 0u) {
    return NULL;
  }

  (void)pthread_once(&g_rv32emu_jit_pool.once, rv32emu_jit_pool_init_once);
  if (g_rv32emu_jit_pool.base == NULL || g_rv32emu_jit_pool.cap == 0u) {
    return NULL;
  }

  if (pthread_mutex_lock(&g_rv32emu_jit_pool.lock) != 0) {
    return NULL;
  }

  {
    size_t aligned_used = (g_rv32emu_jit_pool.used + 15u) & ~((size_t)15u);
    if (aligned_used + bytes <= g_rv32emu_jit_pool.cap) {
      out = g_rv32emu_jit_pool.base + aligned_used;
      g_rv32emu_jit_pool.used = aligned_used + bytes;
    } else {
      atomic_store_explicit(&g_rv32emu_jit_pool_exhausted, true, memory_order_relaxed);
    }
  }

  (void)pthread_mutex_unlock(&g_rv32emu_jit_pool.lock);
  return out;
}

static bool rv32emu_jit_template_match_locked(const rv32emu_jit_template_line_t *line,
                                              const rv32emu_decoded_insn_t *decoded,
                                              const uint32_t *pcs, uint8_t jit_count,
                                              uint64_t prefix_sig) {
  if (line == NULL || decoded == NULL || pcs == NULL || jit_count == 0u ||
      jit_count > RV32EMU_TB_MAX_INSNS || !line->valid || line->jit_count != jit_count ||
      line->prefix_sig != prefix_sig) {
    return false;
  }

  for (uint8_t i = 0u; i < jit_count; i++) {
    if (line->pcs[i] != pcs[i] || line->raw[i] != decoded[i].raw ||
        line->insn_len[i] != decoded[i].insn_len) {
      return false;
    }
  }

  return line->artifact.jit_count != 0u && line->artifact.jit_fn != NULL;
}

bool rv32emu_jit_template_lookup(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                 uint8_t jit_count, uint64_t prefix_sig,
                                 rv32emu_jit_compiled_artifact_t *artifact_out) {
  rv32emu_jit_template_cache_t *cache = &g_rv32emu_jit_template_cache;
  rv32emu_jit_template_line_t *line;
  uint32_t idx;
  bool found = false;

  if (decoded == NULL || pcs == NULL || jit_count == 0u || prefix_sig == 0u ||
      artifact_out == NULL) {
    return false;
  }

  idx = (uint32_t)prefix_sig & (RV32EMU_JIT_TEMPLATE_CACHE_LINES - 1u);
  if (pthread_mutex_lock(&cache->lock) != 0) {
    return false;
  }
  line = &cache->lines[idx];
  if (rv32emu_jit_template_match_locked(line, decoded, pcs, jit_count, prefix_sig)) {
    *artifact_out = line->artifact;
    found = true;
  }
  (void)pthread_mutex_unlock(&cache->lock);

  if (found) {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_TEMPLATE_HIT);
  }
  return found;
}

void rv32emu_jit_template_store(const rv32emu_decoded_insn_t *decoded, const uint32_t *pcs,
                                uint8_t jit_count, uint64_t prefix_sig,
                                const rv32emu_jit_compiled_artifact_t *artifact) {
  rv32emu_jit_template_cache_t *cache = &g_rv32emu_jit_template_cache;
  rv32emu_jit_template_line_t *line;
  uint32_t idx;

  if (decoded == NULL || pcs == NULL || jit_count == 0u || prefix_sig == 0u || artifact == NULL ||
      artifact->jit_count != jit_count || artifact->jit_fn == NULL) {
    return;
  }

  idx = (uint32_t)prefix_sig & (RV32EMU_JIT_TEMPLATE_CACHE_LINES - 1u);
  if (pthread_mutex_lock(&cache->lock) != 0) {
    return;
  }
  line = &cache->lines[idx];
  line->valid = true;
  line->jit_count = jit_count;
  line->prefix_sig = prefix_sig;
  for (uint8_t i = 0u; i < jit_count; i++) {
    line->pcs[i] = pcs[i];
    line->raw[i] = decoded[i].raw;
    line->insn_len[i] = decoded[i].insn_len;
  }
  line->artifact = *artifact;
  (void)pthread_mutex_unlock(&cache->lock);
  rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_TEMPLATE_STORE);
}

static uint64_t rv32emu_tb_structure_signature(const rv32emu_decoded_insn_t *decoded,
                                               uint8_t jit_count) {
  uint64_t h = UINT64_C(1469598103934665603);

  if (decoded == NULL || jit_count == 0u || jit_count > RV32EMU_TB_MAX_INSNS) {
    return 0u;
  }

  for (uint8_t i = 0u; i < jit_count; i++) {
    h ^= (uint64_t)decoded[i].raw;
    h *= UINT64_C(1099511628211);
    h ^= (uint64_t)decoded[i].insn_len;
    h *= UINT64_C(1099511628211);
  }

  h ^= (uint64_t)jit_count;
  h *= UINT64_C(1099511628211);
  return (h == 0u) ? 1u : h;
}

static bool rv32emu_jit_clone_artifact_with_delta(
    const rv32emu_jit_compiled_artifact_t *template_artifact, uint32_t start_pc,
    rv32emu_jit_compiled_artifact_t *artifact_out) {
  uint8_t *dst;
  uint8_t *src;
  uint32_t delta;

  if (template_artifact == NULL || artifact_out == NULL || template_artifact->jit_fn == NULL ||
      template_artifact->jit_code_size == 0u) {
    return false;
  }

  dst = (uint8_t *)rv32emu_jit_alloc((size_t)template_artifact->jit_code_size);
  if (dst == NULL) {
    rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_FAIL_ALLOC);
    return false;
  }
  src = (uint8_t *)(void *)template_artifact->jit_fn;
  memcpy(dst, src, (size_t)template_artifact->jit_code_size);

  *artifact_out = *template_artifact;
  artifact_out->jit_fn = (rv32emu_tb_jit_fn_t)(void *)dst;
  delta = start_pc - template_artifact->base_start_pc;
  if (delta != 0u) {
    for (uint8_t i = 0u; i < template_artifact->pc_reloc_count; i++) {
      uint16_t off = template_artifact->pc_reloc_off[i];
      uint32_t value;

      if ((uint32_t)off + 4u > template_artifact->jit_code_size) {
        return false;
      }
      memcpy(&value, dst + off, sizeof(value));
      value += delta;
      memcpy(dst + off, &value, sizeof(value));
    }
  }
  artifact_out->base_start_pc = start_pc;
  return true;
}

bool rv32emu_jit_struct_template_lookup(const rv32emu_decoded_insn_t *decoded, uint8_t jit_count,
                                        uint32_t start_pc,
                                        rv32emu_jit_compiled_artifact_t *artifact_out) {
  rv32emu_jit_struct_template_cache_t *cache = &g_rv32emu_jit_struct_template_cache;
  rv32emu_jit_compiled_artifact_t template_artifact;
  uint64_t sig;
  uint32_t idx;
  bool matched = false;

  if (decoded == NULL || artifact_out == NULL || jit_count == 0u || jit_count > RV32EMU_TB_MAX_INSNS) {
    return false;
  }

  sig = rv32emu_tb_structure_signature(decoded, jit_count);
  if (sig == 0u) {
    return false;
  }

  idx = (uint32_t)sig & (RV32EMU_JIT_STRUCT_TEMPLATE_LINES - 1u);
  if (pthread_mutex_lock(&cache->lock) != 0) {
    return false;
  }
  {
    rv32emu_jit_struct_template_line_t *line = &cache->lines[idx];
    if (line->valid && line->jit_count == jit_count && line->struct_sig == sig &&
        line->artifact.jit_count == jit_count && line->artifact.jit_fn != NULL &&
        line->artifact.pc_reloc_count != 0u) {
      matched = true;
      for (uint8_t i = 0u; i < jit_count; i++) {
        if (line->raw[i] != decoded[i].raw || line->insn_len[i] != decoded[i].insn_len) {
          matched = false;
          break;
        }
      }
      if (matched) {
        template_artifact = line->artifact;
      }
    }
  }
  (void)pthread_mutex_unlock(&cache->lock);
  if (!matched) {
    return false;
  }

  if (!rv32emu_jit_clone_artifact_with_delta(&template_artifact, start_pc, artifact_out)) {
    return false;
  }
  rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_STRUCT_HIT);
  rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_TEMPLATE_HIT);
  return true;
}

void rv32emu_jit_struct_template_store(const rv32emu_decoded_insn_t *decoded, uint8_t jit_count,
                                       const rv32emu_jit_compiled_artifact_t *artifact) {
  rv32emu_jit_struct_template_cache_t *cache = &g_rv32emu_jit_struct_template_cache;
  uint64_t sig;
  uint32_t idx;

  if (decoded == NULL || artifact == NULL || jit_count == 0u || jit_count > RV32EMU_TB_MAX_INSNS ||
      artifact->jit_fn == NULL || artifact->jit_count != jit_count || artifact->pc_reloc_count == 0u) {
    return;
  }

  sig = rv32emu_tb_structure_signature(decoded, jit_count);
  if (sig == 0u) {
    return;
  }

  idx = (uint32_t)sig & (RV32EMU_JIT_STRUCT_TEMPLATE_LINES - 1u);
  if (pthread_mutex_lock(&cache->lock) != 0) {
    return;
  }
  {
    rv32emu_jit_struct_template_line_t *line = &cache->lines[idx];
    line->valid = true;
    line->jit_count = jit_count;
    line->struct_sig = sig;
    for (uint8_t i = 0u; i < jit_count; i++) {
      line->raw[i] = decoded[i].raw;
      line->insn_len[i] = decoded[i].insn_len;
    }
    line->artifact = *artifact;
  }
  (void)pthread_mutex_unlock(&cache->lock);
  rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_STRUCT_STORE);
  rv32emu_jit_stats_inc_event(RV32EMU_JIT_STAT_COMPILE_TEMPLATE_STORE);
}
#endif
