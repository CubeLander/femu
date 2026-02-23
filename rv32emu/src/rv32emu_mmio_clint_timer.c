#include "rv32emu.h"

#define CLINT_MSIP_BASE 0x0000u
#define CLINT_MTIMECMP_BASE 0x4000u
#define CLINT_MTIME 0xbff8u

static void rv32emu_sync_timer_irq_for_hart(rv32emu_machine_t *m, uint32_t hartid) {
  rv32emu_cpu_t *cpu;
  bool expired;
  uint64_t mtime;

  if (m == NULL || hartid >= m->hart_count) {
    return;
  }

  cpu = rv32emu_hart_cpu(m, hartid);
  if (cpu == NULL) {
    return;
  }

  mtime = atomic_load_explicit(&m->plat.mtime, memory_order_relaxed);
  expired = mtime >= m->plat.clint_mtimecmp[hartid];
  if (m->opts.enable_sbi_shim) {
    if (expired) {
      rv32emu_cpu_mip_set_bits(cpu, MIP_STIP);
    } else {
      rv32emu_cpu_mip_clear_bits(cpu, MIP_STIP);
    }
    rv32emu_cpu_mip_clear_bits(cpu, MIP_MTIP);
    return;
  }

  if (expired) {
    rv32emu_cpu_mip_set_bits(cpu, MIP_MTIP);
  } else {
    rv32emu_cpu_mip_clear_bits(cpu, MIP_MTIP);
  }
}

static void rv32emu_sync_all_timer_irqs(rv32emu_machine_t *m) {
  uint32_t hart;

  if (m == NULL) {
    return;
  }

  for (hart = 0u; hart < m->hart_count; hart++) {
    rv32emu_sync_timer_irq_for_hart(m, hart);
  }
  rv32emu_timer_refresh_deadline(m);
}

bool rv32emu_mmio_clint_read_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t *out) {
  uint32_t off = paddr - RV32EMU_CLINT_BASE;
  uint32_t rel;
  uint32_t hart;

  if (off >= RV32EMU_CLINT_SIZE) {
    return false;
  }

  if (len != 4) {
    return false;
  }

  if (off < CLINT_MSIP_BASE + m->hart_count * 4u) {
    if ((off & 0x3u) != 0u) {
      return false;
    }
    hart = (off - CLINT_MSIP_BASE) / 4u;
    *out = m->plat.clint_msip[hart];
    return true;
  }

  if (off >= CLINT_MTIMECMP_BASE && off < CLINT_MTIMECMP_BASE + m->hart_count * 8u) {
    rel = off - CLINT_MTIMECMP_BASE;
    if ((rel & 0x3u) != 0u) {
      return false;
    }
    hart = rel / 8u;
    if ((rel & 0x4u) == 0u) {
      *out = (uint32_t)(m->plat.clint_mtimecmp[hart] & 0xffffffffu);
    } else {
      *out = (uint32_t)(m->plat.clint_mtimecmp[hart] >> 32);
    }
    return true;
  }

  switch (off) {
  case CLINT_MTIME:
    *out = (uint32_t)(atomic_load_explicit(&m->plat.mtime, memory_order_relaxed) & 0xffffffffu);
    return true;
  case CLINT_MTIME + 4:
    *out = (uint32_t)(atomic_load_explicit(&m->plat.mtime, memory_order_relaxed) >> 32);
    return true;
  default:
    *out = 0;
    return true;
  }
}

bool rv32emu_mmio_clint_write_locked(rv32emu_machine_t *m, uint32_t paddr, int len, uint32_t data) {
  uint32_t off = paddr - RV32EMU_CLINT_BASE;
  uint32_t rel;
  uint32_t hart;

  if (off >= RV32EMU_CLINT_SIZE) {
    return false;
  }

  if (len != 4) {
    return false;
  }

  if (off < CLINT_MSIP_BASE + m->hart_count * 4u) {
    rv32emu_cpu_t *cpu;

    if ((off & 0x3u) != 0u) {
      return false;
    }

    hart = (off - CLINT_MSIP_BASE) / 4u;
    m->plat.clint_msip[hart] = data & 1u;
    cpu = rv32emu_hart_cpu(m, hart);
    if (cpu != NULL) {
      if (m->plat.clint_msip[hart] != 0u &&
          !atomic_load_explicit(&cpu->running, memory_order_acquire)) {
        atomic_store_explicit(&cpu->running, true, memory_order_release);
      }
      if (m->plat.clint_msip[hart] != 0u) {
        rv32emu_cpu_mip_set_bits(cpu, MIP_MSIP);
      } else {
        rv32emu_cpu_mip_clear_bits(cpu, MIP_MSIP);
      }
    }
    return true;
  }

  if (off >= CLINT_MTIMECMP_BASE && off < CLINT_MTIMECMP_BASE + m->hart_count * 8u) {
    rel = off - CLINT_MTIMECMP_BASE;
    if ((rel & 0x3u) != 0u) {
      return false;
    }

    hart = rel / 8u;
    if ((rel & 0x4u) == 0u) {
      m->plat.clint_mtimecmp[hart] =
          (m->plat.clint_mtimecmp[hart] & 0xffffffff00000000ull) | (uint64_t)data;
    } else {
      m->plat.clint_mtimecmp[hart] =
          (m->plat.clint_mtimecmp[hart] & 0x00000000ffffffffull) | ((uint64_t)data << 32);
    }
    rv32emu_sync_timer_irq_for_hart(m, hart);
    rv32emu_timer_refresh_deadline(m);
    return true;
  }

  switch (off) {
  case CLINT_MTIME: {
    uint64_t old_mtime;
    uint64_t new_mtime;
    do {
      old_mtime = atomic_load_explicit(&m->plat.mtime, memory_order_relaxed);
      new_mtime = (old_mtime & 0xffffffff00000000ull) | (uint64_t)data;
    } while (!atomic_compare_exchange_weak_explicit(
        &m->plat.mtime, &old_mtime, new_mtime, memory_order_relaxed, memory_order_relaxed));
    break;
  }
  case CLINT_MTIME + 4: {
    uint64_t old_mtime;
    uint64_t new_mtime;
    do {
      old_mtime = atomic_load_explicit(&m->plat.mtime, memory_order_relaxed);
      new_mtime = (old_mtime & 0x00000000ffffffffull) | ((uint64_t)data << 32);
    } while (!atomic_compare_exchange_weak_explicit(
        &m->plat.mtime, &old_mtime, new_mtime, memory_order_relaxed, memory_order_relaxed));
    break;
  }
  default:
    return true;
  }

  rv32emu_sync_all_timer_irqs(m);
  return true;
}

static void rv32emu_timer_sync_if_due(rv32emu_machine_t *m, uint64_t mtime) {
  uint64_t deadline;

  if (m == NULL) {
    return;
  }

  deadline = atomic_load_explicit(&m->plat.next_timer_deadline, memory_order_relaxed);
  if (mtime < deadline) {
    return;
  }

  if (pthread_mutex_lock(&m->plat.mmio_lock) != 0) {
    return;
  }

  if (mtime >= atomic_load_explicit(&m->plat.next_timer_deadline, memory_order_relaxed)) {
    rv32emu_sync_all_timer_irqs(m);
  }

  (void)pthread_mutex_unlock(&m->plat.mmio_lock);
}

void rv32emu_mmio_step_timer(rv32emu_machine_t *m) {
  uint64_t mtime;

  if (m == NULL) {
    return;
  }

  mtime = atomic_fetch_add_explicit(&m->plat.mtime, 1u, memory_order_relaxed) + 1u;
  rv32emu_timer_sync_if_due(m, mtime);
}
