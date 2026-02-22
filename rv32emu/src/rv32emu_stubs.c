#include "rv32emu.h"

#include <elf.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define RV32EMU_REG_A0 10u
#define RV32EMU_REG_A1 11u
#define RV32EMU_REG_A2 12u
#define RV32EMU_REG_A6 16u
#define RV32EMU_REG_A7 17u

#define SBI_EXT_LEGACY_SET_TIMER 0x00u
#define SBI_EXT_LEGACY_CONSOLE_PUTCHAR 0x01u
#define SBI_EXT_LEGACY_CONSOLE_GETCHAR 0x02u
#define SBI_EXT_LEGACY_CLEAR_IPI 0x03u
#define SBI_EXT_LEGACY_SEND_IPI 0x04u
#define SBI_EXT_LEGACY_REMOTE_FENCE_I 0x05u
#define SBI_EXT_LEGACY_REMOTE_SFENCE_VMA 0x06u
#define SBI_EXT_LEGACY_REMOTE_SFENCE_VMA_ASID 0x07u
#define SBI_EXT_LEGACY_SHUTDOWN 0x08u

#define SBI_EXT_BASE 0x10u
#define SBI_EXT_TIME 0x54494d45u
#define SBI_EXT_IPI 0x735049u
#define SBI_EXT_RFENCE 0x52464e43u
#define SBI_EXT_HSM 0x48534du
#define SBI_EXT_SRST 0x53525354u

#define SBI_ERR_SUCCESS 0
#define SBI_ERR_FAILED (-1)
#define SBI_ERR_NOT_SUPPORTED (-2)
#define SBI_ERR_INVALID_PARAM (-3)

#define SBI_IMPL_ID_RV32EMU 0x52563332u /* "RV32" */
#define SBI_IMPL_VERSION 0x00010000u
#define SBI_SPEC_VERSION_0_2 0x00000002u

static void rv32emu_sbi_set_legacy_ret(rv32emu_machine_t *m, int32_t value) {
  m->cpu.x[RV32EMU_REG_A0] = (uint32_t)value;
}

static void rv32emu_sbi_set_ret(rv32emu_machine_t *m, int32_t error, uint32_t value) {
  m->cpu.x[RV32EMU_REG_A0] = (uint32_t)error;
  m->cpu.x[RV32EMU_REG_A1] = value;
}

static uint64_t rv32emu_sbi_arg_u64(rv32emu_machine_t *m, uint32_t lo_reg) {
  uint64_t lo = m->cpu.x[lo_reg];
  uint64_t hi = m->cpu.x[lo_reg + 1u];
  return lo | (hi << 32);
}

static bool rv32emu_sbi_is_supported_extension(uint32_t eid) {
  switch (eid) {
  case SBI_EXT_BASE:
  case SBI_EXT_TIME:
  case SBI_EXT_IPI:
  case SBI_EXT_RFENCE:
  case SBI_EXT_HSM:
  case SBI_EXT_SRST:
    return true;
  default:
    return false;
  }
}

static void rv32emu_sbi_sync_timer_pending(rv32emu_machine_t *m) {
  bool expired = m->plat.mtime >= m->plat.mtimecmp;

  if (m->opts.enable_sbi_shim) {
    if (expired) {
      m->cpu.csr[CSR_MIP] |= MIP_STIP;
    } else {
      m->cpu.csr[CSR_MIP] &= ~MIP_STIP;
    }
    m->cpu.csr[CSR_MIP] &= ~MIP_MTIP;
    return;
  }

  if (expired) {
    m->cpu.csr[CSR_MIP] |= MIP_MTIP;
  } else {
    m->cpu.csr[CSR_MIP] &= ~MIP_MTIP;
  }
}

static void rv32emu_sbi_set_timer(rv32emu_machine_t *m, uint64_t stime_value) {
  m->plat.mtimecmp = stime_value;
  rv32emu_sbi_sync_timer_pending(m);
}

static bool rv32emu_sbi_handle_legacy(rv32emu_machine_t *m, uint32_t eid) {
  switch (eid) {
  case SBI_EXT_LEGACY_SET_TIMER:
    rv32emu_sbi_set_timer(m, rv32emu_sbi_arg_u64(m, RV32EMU_REG_A0));
    rv32emu_sbi_set_legacy_ret(m, 0);
    return true;
  case SBI_EXT_LEGACY_CONSOLE_PUTCHAR:
    (void)rv32emu_phys_write(m, RV32EMU_UART_BASE, 1, m->cpu.x[RV32EMU_REG_A0] & 0xffu);
    rv32emu_sbi_set_legacy_ret(m, 0);
    return true;
  case SBI_EXT_LEGACY_CONSOLE_GETCHAR:
    rv32emu_sbi_set_legacy_ret(m, -1);
    return true;
  case SBI_EXT_LEGACY_CLEAR_IPI:
    m->cpu.csr[CSR_MIP] &= ~MIP_MSIP;
    rv32emu_sbi_set_legacy_ret(m, 0);
    return true;
  case SBI_EXT_LEGACY_SEND_IPI:
    m->cpu.csr[CSR_MIP] |= MIP_MSIP;
    rv32emu_sbi_set_legacy_ret(m, 0);
    return true;
  case SBI_EXT_LEGACY_REMOTE_FENCE_I:
  case SBI_EXT_LEGACY_REMOTE_SFENCE_VMA:
  case SBI_EXT_LEGACY_REMOTE_SFENCE_VMA_ASID:
    rv32emu_sbi_set_legacy_ret(m, 0);
    return true;
  case SBI_EXT_LEGACY_SHUTDOWN:
    m->cpu.running = false;
    rv32emu_sbi_set_legacy_ret(m, 0);
    return true;
  default:
    return false;
  }
}

static bool rv32emu_sbi_handle_base(rv32emu_machine_t *m, uint32_t fid) {
  switch (fid) {
  case 0u: /* get_spec_version */
    rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, SBI_SPEC_VERSION_0_2);
    return true;
  case 1u: /* get_impl_id */
    rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, SBI_IMPL_ID_RV32EMU);
    return true;
  case 2u: /* get_impl_version */
    rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, SBI_IMPL_VERSION);
    return true;
  case 3u: /* probe_extension */
    rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS,
                        rv32emu_sbi_is_supported_extension(m->cpu.x[RV32EMU_REG_A0]) ? 1u
                                                                                       : 0u);
    return true;
  case 4u: /* get_mvendorid */
    rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, m->cpu.csr[CSR_MVENDORID]);
    return true;
  case 5u: /* get_marchid */
    rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, m->cpu.csr[CSR_MARCHID]);
    return true;
  case 6u: /* get_mimpid */
    rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, m->cpu.csr[CSR_MIMPID]);
    return true;
  default:
    rv32emu_sbi_set_ret(m, SBI_ERR_NOT_SUPPORTED, 0);
    return true;
  }
}

static bool rv32emu_sbi_handle_time(rv32emu_machine_t *m, uint32_t fid) {
  if (fid != 0u) {
    rv32emu_sbi_set_ret(m, SBI_ERR_NOT_SUPPORTED, 0);
    return true;
  }

  rv32emu_sbi_set_timer(m, rv32emu_sbi_arg_u64(m, RV32EMU_REG_A0));
  rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, 0);
  return true;
}

static bool rv32emu_sbi_handle_ipi(rv32emu_machine_t *m, uint32_t fid) {
  if (fid != 0u) {
    rv32emu_sbi_set_ret(m, SBI_ERR_NOT_SUPPORTED, 0);
    return true;
  }

  m->cpu.csr[CSR_MIP] |= MIP_SSIP;
  rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, 0);
  return true;
}

static bool rv32emu_sbi_handle_rfence(rv32emu_machine_t *m, uint32_t fid) {
  (void)fid;
  rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, 0);
  return true;
}

static bool rv32emu_sbi_handle_hsm(rv32emu_machine_t *m, uint32_t fid) {
  uint32_t hartid = m->cpu.x[RV32EMU_REG_A0];

  switch (fid) {
  case 0u: /* hart_start */
    rv32emu_sbi_set_ret(m, SBI_ERR_NOT_SUPPORTED, 0);
    return true;
  case 1u: /* hart_stop */
    m->cpu.running = false;
    rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, 0);
    return true;
  case 2u: /* hart_status */
    rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, (hartid == 0u) ? 0u : 1u);
    return true;
  case 3u: /* hart_suspend */
    rv32emu_sbi_set_ret(m, SBI_ERR_NOT_SUPPORTED, 0);
    return true;
  default:
    rv32emu_sbi_set_ret(m, SBI_ERR_NOT_SUPPORTED, 0);
    return true;
  }
}

static bool rv32emu_sbi_handle_srst(rv32emu_machine_t *m, uint32_t fid) {
  if (fid != 0u) {
    rv32emu_sbi_set_ret(m, SBI_ERR_NOT_SUPPORTED, 0);
    return true;
  }

  m->cpu.running = false;
  rv32emu_sbi_set_ret(m, SBI_ERR_SUCCESS, 0);
  return true;
}

bool rv32emu_handle_sbi_ecall(rv32emu_machine_t *m) {
  uint32_t eid;
  uint32_t fid;

  if (m == NULL || !m->opts.enable_sbi_shim || m->cpu.priv == RV32EMU_PRIV_U) {
    return false;
  }

  eid = m->cpu.x[RV32EMU_REG_A7];
  fid = m->cpu.x[RV32EMU_REG_A6];

  if (rv32emu_sbi_handle_legacy(m, eid)) {
    return true;
  }

  switch (eid) {
  case SBI_EXT_BASE:
    return rv32emu_sbi_handle_base(m, fid);
  case SBI_EXT_TIME:
    return rv32emu_sbi_handle_time(m, fid);
  case SBI_EXT_IPI:
    return rv32emu_sbi_handle_ipi(m, fid);
  case SBI_EXT_RFENCE:
    return rv32emu_sbi_handle_rfence(m, fid);
  case SBI_EXT_HSM:
    return rv32emu_sbi_handle_hsm(m, fid);
  case SBI_EXT_SRST:
    return rv32emu_sbi_handle_srst(m, fid);
  default:
    rv32emu_sbi_set_ret(m, SBI_ERR_NOT_SUPPORTED, 0);
    return true;
  }
}

static bool rv32emu_file_is_elf32(const char *path, bool *is_elf32_out) {
  FILE *fp;
  uint8_t ident[EI_NIDENT];
  size_t nr;

  if (path == NULL || is_elf32_out == NULL) {
    return false;
  }

  *is_elf32_out = false;
  fp = fopen(path, "rb");
  if (fp == NULL) {
    return false;
  }

  nr = fread(ident, 1, sizeof(ident), fp);
  fclose(fp);
  if (nr < 5u) {
    return true;
  }

  if (ident[EI_MAG0] == ELFMAG0 && ident[EI_MAG1] == ELFMAG1 &&
      ident[EI_MAG2] == ELFMAG2 && ident[EI_MAG3] == ELFMAG3 &&
      ident[EI_CLASS] == ELFCLASS32) {
    *is_elf32_out = true;
  }

  return true;
}

bool rv32emu_load_image_auto(rv32emu_machine_t *m, const char *path, uint32_t load_addr,
                             uint32_t *entry_out, bool *entry_valid) {
  bool is_elf32 = false;
  uint32_t ignored_entry = 0;

  if (m == NULL || path == NULL) {
    return false;
  }

  if (entry_out == NULL) {
    entry_out = &ignored_entry;
  }
  if (entry_valid != NULL) {
    *entry_valid = false;
  }

  if (!rv32emu_file_is_elf32(path, &is_elf32)) {
    return false;
  }

  if (is_elf32) {
    if (!rv32emu_load_elf32(m, path, entry_out)) {
      return false;
    }
    if (entry_valid != NULL) {
      *entry_valid = true;
    }
    return true;
  }

  return rv32emu_load_raw(m, path, load_addr, NULL);
}

bool rv32emu_load_raw(rv32emu_machine_t *m, const char *path, uint32_t load_addr,
                      uint32_t *size_out) {
  FILE *fp;
  long end;
  size_t file_size;
  uint8_t *dst;

  if (m == NULL || path == NULL) {
    return false;
  }

  fp = fopen(path, "rb");
  if (fp == NULL) {
    return false;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return false;
  }

  end = ftell(fp);
  if (end < 0 || (uint64_t)end > (uint64_t)UINT32_MAX) {
    fclose(fp);
    return false;
  }
  file_size = (size_t)end;

  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return false;
  }

  dst = rv32emu_dram_ptr(m, load_addr, file_size);
  if (dst == NULL) {
    fclose(fp);
    return false;
  }

  if (file_size > 0 && fread(dst, 1, file_size, fp) != file_size) {
    fclose(fp);
    return false;
  }

  fclose(fp);
  if (size_out != NULL) {
    *size_out = (uint32_t)file_size;
  }
  return true;
}

bool rv32emu_load_elf32(rv32emu_machine_t *m, const char *path, uint32_t *entry_out) {
  FILE *fp;
  Elf32_Ehdr ehdr;
  bool loaded = false;

  if (m == NULL || path == NULL) {
    return false;
  }

  fp = fopen(path, "rb");
  if (fp == NULL) {
    return false;
  }

  if (fread(&ehdr, 1, sizeof(ehdr), fp) != sizeof(ehdr)) {
    fclose(fp);
    return false;
  }

  if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
      ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3 ||
      ehdr.e_ident[EI_CLASS] != ELFCLASS32 || ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
      ehdr.e_machine != EM_RISCV ||
      (ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN) ||
      ehdr.e_phentsize < sizeof(Elf32_Phdr)) {
    fclose(fp);
    return false;
  }

  for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
    Elf32_Phdr phdr;
    uint64_t phoff = (uint64_t)ehdr.e_phoff + (uint64_t)i * (uint64_t)ehdr.e_phentsize;
    uint32_t seg_addr;
    uint8_t *dst;

    if (phoff > (uint64_t)LONG_MAX) {
      fclose(fp);
      return false;
    }
    if (fseek(fp, (long)phoff, SEEK_SET) != 0) {
      fclose(fp);
      return false;
    }
    if (fread(&phdr, 1, sizeof(phdr), fp) != sizeof(phdr)) {
      fclose(fp);
      return false;
    }

    if (phdr.p_type != PT_LOAD) {
      continue;
    }
    if (phdr.p_memsz < phdr.p_filesz) {
      fclose(fp);
      return false;
    }

    seg_addr = (phdr.p_paddr != 0u) ? phdr.p_paddr : phdr.p_vaddr;
    dst = rv32emu_dram_ptr(m, seg_addr, phdr.p_memsz);
    if (dst == NULL) {
      fclose(fp);
      return false;
    }

    memset(dst, 0, phdr.p_memsz);
    if (phdr.p_filesz > 0u) {
      if (fseek(fp, (long)phdr.p_offset, SEEK_SET) != 0 ||
          fread(dst, 1, phdr.p_filesz, fp) != phdr.p_filesz) {
        fclose(fp);
        return false;
      }
    }
    loaded = true;
  }

  fclose(fp);
  if (!loaded) {
    return false;
  }

  if (entry_out != NULL) {
    *entry_out = ehdr.e_entry;
  }
  return true;
}
