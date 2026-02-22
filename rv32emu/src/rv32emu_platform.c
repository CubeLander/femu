#include "rv32emu.h"

#include <stdlib.h>
#include <string.h>

void rv32emu_default_options(rv32emu_options_t *opts) {
  if (opts == NULL) {
    return;
  }

  memset(opts, 0, sizeof(*opts));
  opts->ram_mb = RV32EMU_DEFAULT_RAM_MB;
  opts->kernel_load_addr = RV32EMU_DEFAULT_KERNEL_LOAD;
  opts->dtb_load_addr = RV32EMU_DEFAULT_DTB_LOAD;
  opts->initrd_load_addr = RV32EMU_DEFAULT_INITRD_LOAD;
  opts->max_instructions = RV32EMU_DEFAULT_MAX_INSTR;
  opts->boot_s_mode = true;
}

bool rv32emu_platform_init(rv32emu_machine_t *m, const rv32emu_options_t *opts) {
  rv32emu_options_t local_opts;

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

  m->plat.dram_base = RV32EMU_DRAM_BASE;
  m->plat.dram_size = m->opts.ram_mb * 1024u * 1024u;
  m->plat.dram = calloc(1u, m->plat.dram_size);
  if (m->plat.dram == NULL) {
    return false;
  }

  m->plat.mtime = 0;
  m->plat.mtimecmp = UINT64_MAX;
  m->plat.clint_msip = 0;
  m->plat.plic_claim = 0;

  m->cpu.priv = RV32EMU_PRIV_M;
  m->cpu.running = true;
  m->cpu.trace = m->opts.trace;

  m->cpu.csr[CSR_MHARTID] = 0;
  m->cpu.csr[CSR_MISA] = (1u << 30) | (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) |
                         (1u << 8) | (1u << 12) | (1u << 18) | (1u << 20);
  m->cpu.csr[CSR_TIME] = 0;

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
