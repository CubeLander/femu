#define _POSIX_C_SOURCE 200809L

#include "rv32emu.h"

#include <assert.h>
#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_all_or_die(int fd, const void *buf, size_t len) {
  const uint8_t *p = (const uint8_t *)buf;
  size_t off = 0;

  while (off < len) {
    ssize_t n = write(fd, p + off, len - off);
    assert(n > 0);
    off += (size_t)n;
  }
}

static void write_pad_zero_or_die(int fd, size_t len) {
  uint8_t zero[64] = {0};
  size_t off = 0;

  while (off < len) {
    size_t chunk = len - off;
    if (chunk > sizeof(zero)) {
      chunk = sizeof(zero);
    }
    write_all_or_die(fd, zero, chunk);
    off += chunk;
  }
}

static void test_load_raw_and_auto(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  char path[] = "/tmp/rv32emu-raw-XXXXXX";
  uint8_t raw[] = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u};
  uint32_t size = 0;
  uint32_t value = 0;
  uint32_t entry = 0;
  bool entry_valid = true;
  int fd;

  fd = mkstemp(path);
  assert(fd >= 0);
  write_all_or_die(fd, raw, sizeof(raw));
  close(fd);

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  assert(rv32emu_load_raw(&m, path, RV32EMU_DRAM_BASE + 0x200u, &size));
  assert(size == sizeof(raw));
  assert(rv32emu_phys_read(&m, RV32EMU_DRAM_BASE + 0x200u, 4, &value));
  assert(value == 0x44332211u);
  assert(rv32emu_phys_read(&m, RV32EMU_DRAM_BASE + 0x204u, 1, &value));
  assert(value == 0x55u);

  assert(rv32emu_load_image_auto(&m, path, RV32EMU_DRAM_BASE + 0x300u, &entry, &entry_valid));
  assert(entry_valid == false);

  rv32emu_platform_destroy(&m);
  unlink(path);
}

static void test_load_elf32_and_auto(void) {
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  char path[] = "/tmp/rv32emu-elf-XXXXXX";
  uint32_t load_addr = RV32EMU_DRAM_BASE + 0x1000u;
  uint8_t payload[] = {0xaau, 0xbbu, 0xccu, 0xddu};
  Elf32_Ehdr ehdr;
  Elf32_Phdr phdr;
  uint32_t entry = 0;
  uint32_t value = 0;
  bool entry_valid = false;
  int fd;

  memset(&ehdr, 0, sizeof(ehdr));
  memset(&phdr, 0, sizeof(phdr));

  ehdr.e_ident[EI_MAG0] = ELFMAG0;
  ehdr.e_ident[EI_MAG1] = ELFMAG1;
  ehdr.e_ident[EI_MAG2] = ELFMAG2;
  ehdr.e_ident[EI_MAG3] = ELFMAG3;
  ehdr.e_ident[EI_CLASS] = ELFCLASS32;
  ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
  ehdr.e_ident[EI_VERSION] = EV_CURRENT;
  ehdr.e_ident[EI_OSABI] = ELFOSABI_NONE;
  ehdr.e_type = ET_EXEC;
  ehdr.e_machine = EM_RISCV;
  ehdr.e_version = EV_CURRENT;
  ehdr.e_entry = load_addr + 0x10u;
  ehdr.e_phoff = sizeof(Elf32_Ehdr);
  ehdr.e_ehsize = sizeof(Elf32_Ehdr);
  ehdr.e_phentsize = sizeof(Elf32_Phdr);
  ehdr.e_phnum = 1;

  phdr.p_type = PT_LOAD;
  phdr.p_offset = 0x100u;
  phdr.p_vaddr = load_addr;
  phdr.p_paddr = load_addr;
  phdr.p_filesz = sizeof(payload);
  phdr.p_memsz = 8u;
  phdr.p_flags = PF_R | PF_X;
  phdr.p_align = 4u;

  fd = mkstemp(path);
  assert(fd >= 0);
  write_all_or_die(fd, &ehdr, sizeof(ehdr));
  write_all_or_die(fd, &phdr, sizeof(phdr));
  write_pad_zero_or_die(fd, 0x100u - sizeof(ehdr) - sizeof(phdr));
  write_all_or_die(fd, payload, sizeof(payload));
  close(fd);

  rv32emu_default_options(&opts);
  assert(rv32emu_platform_init(&m, &opts));

  assert(rv32emu_load_elf32(&m, path, &entry));
  assert(entry == load_addr + 0x10u);
  assert(rv32emu_phys_read(&m, load_addr, 4, &value));
  assert(value == 0xddccbbaau);
  assert(rv32emu_phys_read(&m, load_addr + 4u, 4, &value));
  assert(value == 0u);

  assert(rv32emu_load_image_auto(&m, path, RV32EMU_DRAM_BASE + 0x2000u, &entry, &entry_valid));
  assert(entry_valid == true);
  assert(entry == load_addr + 0x10u);

  rv32emu_platform_destroy(&m);
  unlink(path);
}

int main(void) {
  test_load_raw_and_auto();
  test_load_elf32_and_auto();
  puts("[OK] rv32emu loader test passed");
  return 0;
}
