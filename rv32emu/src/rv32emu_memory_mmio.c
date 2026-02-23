#include "rv32emu.h"

/*
 * Internal MMIO helpers implemented in rv32emu_mmio_devices.c.
 * Callers hold mmio_lock around read/write helpers.
 */
bool rv32emu_mmio_read_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out);
bool rv32emu_mmio_write_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data);
void rv32emu_mmio_step_timer(rv32emu_machine_t *m);

static bool rv32emu_read_u32_le(const uint8_t *p, int len, uint32_t *out) {
  if (len == 1) {
    *out = p[0];
    return true;
  }
  if (len == 2) {
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    return true;
  }
  if (len == 4) {
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
    return true;
  }
  return false;
}

static bool rv32emu_write_u32_le(uint8_t *p, int len, uint32_t data) {
  if (len == 1) {
    p[0] = (uint8_t)data;
    return true;
  }
  if (len == 2) {
    p[0] = (uint8_t)data;
    p[1] = (uint8_t)(data >> 8);
    return true;
  }
  if (len == 4) {
    p[0] = (uint8_t)data;
    p[1] = (uint8_t)(data >> 8);
    p[2] = (uint8_t)(data >> 16);
    p[3] = (uint8_t)(data >> 24);
    return true;
  }
  return false;
}

/*
 * Threaded DRAM access path:
 * - aligned halfword/word use single relaxed atomic operation (fast path);
 * - unaligned accesses fall back to byte-granular relaxed atomics (slow path).
 *
 * This keeps host-side accesses race-free in experimental threaded mode while
 * preserving existing unaligned behavior (byte-assembled load/store).
 */
static bool rv32emu_read_u32_le_atomic(const uint8_t *p, uint32_t paddr, int len, uint32_t *out) {
  if (len == 1) {
    *out = (uint32_t)__atomic_load_n(&p[0], __ATOMIC_RELAXED);
    return true;
  }
  if (len == 2) {
    uint32_t b0;
    uint32_t b1;

    if ((paddr & 1u) == 0u) {
      const uint16_t *p16 = (const uint16_t *)(const void *)p;
      *out = (uint32_t)__atomic_load_n(p16, __ATOMIC_RELAXED);
      return true;
    }

    b0 = (uint32_t)__atomic_load_n(&p[0], __ATOMIC_RELAXED);
    b1 = (uint32_t)__atomic_load_n(&p[1], __ATOMIC_RELAXED);
    *out = b0 | (b1 << 8);
    return true;
  }
  if (len == 4) {
    uint32_t b0;
    uint32_t b1;
    uint32_t b2;
    uint32_t b3;

    if ((paddr & 3u) == 0u) {
      const uint32_t *p32 = (const uint32_t *)(const void *)p;
      *out = __atomic_load_n(p32, __ATOMIC_RELAXED);
      return true;
    }

    b0 = (uint32_t)__atomic_load_n(&p[0], __ATOMIC_RELAXED);
    b1 = (uint32_t)__atomic_load_n(&p[1], __ATOMIC_RELAXED);
    b2 = (uint32_t)__atomic_load_n(&p[2], __ATOMIC_RELAXED);
    b3 = (uint32_t)__atomic_load_n(&p[3], __ATOMIC_RELAXED);
    *out = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    return true;
  }
  return false;
}

static bool rv32emu_write_u32_le_atomic(uint8_t *p, uint32_t paddr, int len, uint32_t data) {
  if (len == 1) {
    __atomic_store_n(&p[0], (uint8_t)data, __ATOMIC_RELAXED);
    return true;
  }
  if (len == 2) {
    if ((paddr & 1u) == 0u) {
      uint16_t *p16 = (uint16_t *)(void *)p;
      __atomic_store_n(p16, (uint16_t)data, __ATOMIC_RELAXED);
      return true;
    }

    __atomic_store_n(&p[0], (uint8_t)data, __ATOMIC_RELAXED);
    __atomic_store_n(&p[1], (uint8_t)(data >> 8), __ATOMIC_RELAXED);
    return true;
  }
  if (len == 4) {
    if ((paddr & 3u) == 0u) {
      uint32_t *p32 = (uint32_t *)(void *)p;
      __atomic_store_n(p32, data, __ATOMIC_RELAXED);
      return true;
    }

    __atomic_store_n(&p[0], (uint8_t)data, __ATOMIC_RELAXED);
    __atomic_store_n(&p[1], (uint8_t)(data >> 8), __ATOMIC_RELAXED);
    __atomic_store_n(&p[2], (uint8_t)(data >> 16), __ATOMIC_RELAXED);
    __atomic_store_n(&p[3], (uint8_t)(data >> 24), __ATOMIC_RELAXED);
    return true;
  }
  return false;
}

static inline void rv32emu_dram_atomic_stat_inc(rv32emu_machine_t *m,
                                                atomic_uint_fast64_t *counter) {
  if (m == NULL || counter == NULL || !m->plat.dram_atomic_stats_enable) {
    return;
  }
  atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
}

void rv32emu_step_timer(rv32emu_machine_t *m) {
  rv32emu_mmio_step_timer(m);
}

void rv32emu_flush_timer(rv32emu_machine_t *m) {
  (void)m;
}

uint8_t *rv32emu_dram_ptr(rv32emu_machine_t *m, uint32_t paddr, size_t len) {
  uint32_t off;

  if (m == NULL || m->plat.dram == NULL || paddr < m->plat.dram_base) {
    return NULL;
  }

  off = paddr - m->plat.dram_base;
  if ((uint64_t)off + (uint64_t)len > (uint64_t)m->plat.dram_size) {
    return NULL;
  }

  return m->plat.dram + off;
}

bool rv32emu_phys_read(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out) {
  uint8_t *ptr;
  bool ok;

  if (m == NULL || out == NULL) {
    return false;
  }

  ptr = rv32emu_dram_ptr(m, paddr, (size_t)len);
  if (ptr != NULL) {
    if (m->threaded_exec_active) {
      if (len == 4 && (paddr & 3u) == 0u) {
        rv32emu_dram_atomic_stat_inc(m, &m->plat.dram_atomic_read_aligned32);
      } else if (len == 2 && (paddr & 1u) == 0u) {
        rv32emu_dram_atomic_stat_inc(m, &m->plat.dram_atomic_read_aligned16);
      } else {
        rv32emu_dram_atomic_stat_inc(m, &m->plat.dram_atomic_read_bytepath);
      }
      return rv32emu_read_u32_le_atomic(ptr, paddr, len, out);
    }
    if (pthread_mutex_lock(&m->plat.dram_lock) != 0) {
      return false;
    }
    ok = rv32emu_read_u32_le(ptr, len, out);
    (void)pthread_mutex_unlock(&m->plat.dram_lock);
    return ok;
  }

  if (pthread_mutex_lock(&m->plat.mmio_lock) != 0) {
    return false;
  }
  ok = rv32emu_mmio_read_locked(m, paddr, len, out);
  (void)pthread_mutex_unlock(&m->plat.mmio_lock);
  return ok;
}

bool rv32emu_phys_write(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data) {
  uint8_t *ptr;
  bool ok;

  if (m == NULL) {
    return false;
  }

  ptr = rv32emu_dram_ptr(m, paddr, (size_t)len);
  if (ptr != NULL) {
    if (m->threaded_exec_active) {
      if (len == 4 && (paddr & 3u) == 0u) {
        rv32emu_dram_atomic_stat_inc(m, &m->plat.dram_atomic_write_aligned32);
      } else if (len == 2 && (paddr & 1u) == 0u) {
        rv32emu_dram_atomic_stat_inc(m, &m->plat.dram_atomic_write_aligned16);
      } else {
        rv32emu_dram_atomic_stat_inc(m, &m->plat.dram_atomic_write_bytepath);
      }
      return rv32emu_write_u32_le_atomic(ptr, paddr, len, data);
    }
    if (pthread_mutex_lock(&m->plat.dram_lock) != 0) {
      return false;
    }
    ok = rv32emu_write_u32_le(ptr, len, data);
    (void)pthread_mutex_unlock(&m->plat.dram_lock);
    return ok;
  }

  if (pthread_mutex_lock(&m->plat.mmio_lock) != 0) {
    return false;
  }
  ok = rv32emu_mmio_write_locked(m, paddr, len, data);
  (void)pthread_mutex_unlock(&m->plat.mmio_lock);
  return ok;
}
