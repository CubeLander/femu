#include "rv32emu.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rv32emu_default_misa_value(void) {
  return (1u << 30) | (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 8) |
         (1u << 12) | (1u << 18) | (1u << 20);
}

void rv32emu_default_options(rv32emu_options_t *opts) {
  if (opts == NULL) {
    return;
  }

  memset(opts, 0, sizeof(*opts));
  opts->ram_mb = RV32EMU_DEFAULT_RAM_MB;
  opts->kernel_load_addr = RV32EMU_DEFAULT_KERNEL_LOAD;
  opts->dtb_load_addr = RV32EMU_DEFAULT_DTB_LOAD;
  opts->initrd_load_addr = RV32EMU_DEFAULT_INITRD_LOAD;
  opts->hart_count = RV32EMU_DEFAULT_HART_COUNT;
  opts->max_instructions = RV32EMU_DEFAULT_MAX_INSTR;
  opts->boot_s_mode = true;
}

bool rv32emu_platform_init(rv32emu_machine_t *m, const rv32emu_options_t *opts) {
  rv32emu_options_t local_opts;
  uint32_t hart;
  uint32_t context;

  if (m == NULL) {
    return false;
  }

  memset(m, 0, sizeof(*m));

  if (opts == NULL) {
    rv32emu_default_options(&local_opts);
    opts = &local_opts;
  }

  m->opts = *opts;
  if (m->opts.ram_mb == 0) {
    m->opts.ram_mb = RV32EMU_DEFAULT_RAM_MB;
  }
  if (m->opts.hart_count == 0u) {
    m->opts.hart_count = RV32EMU_DEFAULT_HART_COUNT;
  }
  if (m->opts.hart_count > RV32EMU_MAX_HARTS) {
    return false;
  }
  m->hart_count = m->opts.hart_count;
  m->active_hart = 0u;

  m->plat.dram_base = RV32EMU_DRAM_BASE;
  m->plat.dram_size = m->opts.ram_mb * 1024u * 1024u;
  m->plat.dram = calloc(1u, m->plat.dram_size);
  if (m->plat.dram == NULL) {
    return false;
  }

  m->plat.mtime = 0;
  for (hart = 0u; hart < RV32EMU_MAX_HARTS; hart++) {
    m->plat.clint_mtimecmp[hart] = UINT64_MAX;
    m->plat.clint_msip[hart] = 0u;
  }
  for (context = 0u; context < RV32EMU_MAX_PLIC_CONTEXTS; context++) {
    m->plat.plic_enable[context] = 0u;
    m->plat.plic_claim[context] = 0u;
  }

  for (hart = 0u; hart < m->hart_count; hart++) {
    rv32emu_cpu_t *cpu = &m->harts[hart];

    cpu->priv = RV32EMU_PRIV_M;
    cpu->running = (hart == 0u);
    cpu->trace = m->opts.trace;
    cpu->csr[CSR_MHARTID] = hart;
    cpu->csr[CSR_MISA] = rv32emu_default_misa_value();
    cpu->csr[CSR_TIME] = 0;
  }

  return true;
}

void rv32emu_platform_destroy(rv32emu_machine_t *m) {
  if (m == NULL) {
    return;
  }

  free(m->plat.dram);
  m->plat.dram = NULL;
  m->plat.dram_size = 0;
}
