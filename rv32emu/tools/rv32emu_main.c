#include "rv32emu.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define FW_DYNAMIC_INFO_MAGIC_VALUE 0x4942534fu
#define FW_DYNAMIC_INFO_VERSION_2 0x2u
#define FW_DYNAMIC_INFO_NEXT_MODE_S 0x1u
#define FW_DYNAMIC_INFO_NEXT_MODE_M 0x3u
#define RV32EMU_RUN_SLICE_INSTR 20000ull

typedef struct {
  const char *opensbi_path;
  const char *kernel_path;
  const char *dtb_path;
  const char *initrd_path;
  uint32_t opensbi_load_addr;
  uint32_t kernel_load_addr;
  uint32_t dtb_load_addr;
  uint32_t initrd_load_addr;
  uint32_t fw_dynamic_info_addr;
  uint32_t memory_mb;
  uint64_t max_instructions;
  bool has_fw_dynamic_info_addr;
  bool trace;
  bool boot_s_mode;
  bool use_fw_dynamic;
  bool interactive;
} cli_options_t;

static void usage(FILE *out, const char *prog) {
  fprintf(out,
          "Usage: %s [options]\n"
          "  --opensbi <path>            OpenSBI firmware image\n"
          "  --kernel <path>             Linux Image\n"
          "  --dtb <path>                Device tree blob\n"
          "  --initrd <path>             Initramfs image\n"
          "  --opensbi-load <hex>        OpenSBI load address (default 0x80000000)\n"
          "  --kernel-load <hex>         Kernel load address (default 0x80400000)\n"
          "  --dtb-load <hex>            DTB load address (default 0x87f00000)\n"
          "  --initrd-load <hex>         Initrd load address (default 0x88000000)\n"
          "  --fw-dynamic-info <hex>     FW_DYNAMIC info address (default kernel-load-0x1000)\n"
          "  --no-fw-dynamic             Boot OpenSBI without a2 fw_dynamic_info\n"
          "  --boot-mode <s|m>           OpenSBI next mode (default s)\n"
          "  --memory-mb <num>           RAM size in MiB (default 256)\n"
          "  --max-instr <num>           Max instructions (default 50000000)\n"
          "  --interactive               Enable stdin -> UART interactive mode\n"
          "  --trace                     Enable trace flag\n"
          "  -h, --help                  Show this help\n",
          prog);
}

static bool parse_u32(const char *s, uint32_t *out) {
  char *end = NULL;
  unsigned long v;

  if (s == NULL || out == NULL) {
    return false;
  }

  v = strtoul(s, &end, 0);
  if (*s == '\0' || (end != NULL && *end != '\0') || v > 0xfffffffful) {
    return false;
  }

  *out = (uint32_t)v;
  return true;
}

static bool parse_u64(const char *s, uint64_t *out) {
  char *end = NULL;
  unsigned long long v;

  if (s == NULL || out == NULL) {
    return false;
  }

  v = strtoull(s, &end, 0);
  if (*s == '\0' || (end != NULL && *end != '\0')) {
    return false;
  }

  *out = (uint64_t)v;
  return true;
}

static bool write_u32(rv32emu_machine_t *m, uint32_t addr, uint32_t value) {
  return rv32emu_phys_write(m, addr, 4, value);
}

static bool write_fw_dynamic_info(rv32emu_machine_t *m, uint32_t info_addr, uint32_t next_addr,
                                  bool next_s_mode) {
  uint32_t next_mode = next_s_mode ? FW_DYNAMIC_INFO_NEXT_MODE_S : FW_DYNAMIC_INFO_NEXT_MODE_M;

  return write_u32(m, info_addr + 0u, FW_DYNAMIC_INFO_MAGIC_VALUE) &&
         write_u32(m, info_addr + 4u, FW_DYNAMIC_INFO_VERSION_2) &&
         write_u32(m, info_addr + 8u, next_addr) && write_u32(m, info_addr + 12u, next_mode) &&
         write_u32(m, info_addr + 16u, 0u) && write_u32(m, info_addr + 20u, 0u);
}

static bool parse_args(int argc, char **argv, cli_options_t *cli) {
  int i;

  if (cli == NULL) {
    return false;
  }

  memset(cli, 0, sizeof(*cli));
  cli->opensbi_load_addr = RV32EMU_DRAM_BASE;
  cli->kernel_load_addr = RV32EMU_DEFAULT_KERNEL_LOAD;
  cli->dtb_load_addr = RV32EMU_DEFAULT_DTB_LOAD;
  cli->initrd_load_addr = RV32EMU_DEFAULT_INITRD_LOAD;
  cli->memory_mb = RV32EMU_DEFAULT_RAM_MB;
  cli->max_instructions = RV32EMU_DEFAULT_MAX_INSTR;
  cli->boot_s_mode = true;
  cli->use_fw_dynamic = true;

  for (i = 1; i < argc; i++) {
    const char *arg = argv[i];
    const char *val = NULL;

    if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      usage(stdout, argv[0]);
      exit(0);
    }

    if (arg[0] != '-') {
      fprintf(stderr, "[ERR] unexpected argument: %s\n", arg);
      return false;
    }

    if (!strcmp(arg, "--trace")) {
      cli->trace = true;
      continue;
    }
    if (!strcmp(arg, "--interactive")) {
      cli->interactive = true;
      continue;
    }
    if (!strcmp(arg, "--no-fw-dynamic")) {
      cli->use_fw_dynamic = false;
      continue;
    }

    if (i + 1 >= argc) {
      fprintf(stderr, "[ERR] missing value for %s\n", arg);
      return false;
    }
    val = argv[++i];

    if (!strcmp(arg, "--opensbi")) {
      cli->opensbi_path = val;
    } else if (!strcmp(arg, "--kernel")) {
      cli->kernel_path = val;
    } else if (!strcmp(arg, "--dtb")) {
      cli->dtb_path = val;
    } else if (!strcmp(arg, "--initrd")) {
      cli->initrd_path = val;
    } else if (!strcmp(arg, "--opensbi-load")) {
      if (!parse_u32(val, &cli->opensbi_load_addr)) {
        fprintf(stderr, "[ERR] invalid --opensbi-load: %s\n", val);
        return false;
      }
    } else if (!strcmp(arg, "--kernel-load")) {
      if (!parse_u32(val, &cli->kernel_load_addr)) {
        fprintf(stderr, "[ERR] invalid --kernel-load: %s\n", val);
        return false;
      }
    } else if (!strcmp(arg, "--dtb-load")) {
      if (!parse_u32(val, &cli->dtb_load_addr)) {
        fprintf(stderr, "[ERR] invalid --dtb-load: %s\n", val);
        return false;
      }
    } else if (!strcmp(arg, "--initrd-load")) {
      if (!parse_u32(val, &cli->initrd_load_addr)) {
        fprintf(stderr, "[ERR] invalid --initrd-load: %s\n", val);
        return false;
      }
    } else if (!strcmp(arg, "--fw-dynamic-info")) {
      if (!parse_u32(val, &cli->fw_dynamic_info_addr)) {
        fprintf(stderr, "[ERR] invalid --fw-dynamic-info: %s\n", val);
        return false;
      }
      cli->has_fw_dynamic_info_addr = true;
    } else if (!strcmp(arg, "--memory-mb")) {
      if (!parse_u32(val, &cli->memory_mb) || cli->memory_mb == 0u) {
        fprintf(stderr, "[ERR] invalid --memory-mb: %s\n", val);
        return false;
      }
    } else if (!strcmp(arg, "--max-instr")) {
      if (!parse_u64(val, &cli->max_instructions) || cli->max_instructions == 0u) {
        fprintf(stderr, "[ERR] invalid --max-instr: %s\n", val);
        return false;
      }
    } else if (!strcmp(arg, "--boot-mode")) {
      if (!strcmp(val, "s") || !strcmp(val, "S")) {
        cli->boot_s_mode = true;
      } else if (!strcmp(val, "m") || !strcmp(val, "M")) {
        cli->boot_s_mode = false;
      } else {
        fprintf(stderr, "[ERR] invalid --boot-mode: %s (use s|m)\n", val);
        return false;
      }
    } else {
      fprintf(stderr, "[ERR] unknown option: %s\n", arg);
      return false;
    }
  }

  if (cli->opensbi_path == NULL || cli->kernel_path == NULL || cli->dtb_path == NULL) {
    fprintf(stderr, "[ERR] --opensbi, --kernel and --dtb are required\n");
    return false;
  }

  if (!cli->has_fw_dynamic_info_addr) {
    if (cli->kernel_load_addr < RV32EMU_DRAM_BASE + 0x1000u) {
      fprintf(stderr, "[ERR] --kernel-load too low for default fw_dynamic_info address\n");
      return false;
    }
    cli->fw_dynamic_info_addr = cli->kernel_load_addr - 0x1000u;
  }

  return true;
}

typedef struct {
  bool enabled;
  bool has_saved_flags;
  bool has_saved_termios;
  int saved_flags;
  struct termios saved_termios;
} stdin_mode_t;

static bool rv32emu_setup_stdin_mode(stdin_mode_t *mode, bool interactive) {
  int flags;

  if (mode == NULL) {
    return false;
  }

  memset(mode, 0, sizeof(*mode));
  if (!interactive) {
    return true;
  }

  flags = fcntl(STDIN_FILENO, F_GETFL);
  if (flags < 0) {
    perror("[WARN] stdin F_GETFL failed");
    return false;
  }
  mode->saved_flags = flags;
  mode->has_saved_flags = true;
  if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
    perror("[WARN] stdin F_SETFL(O_NONBLOCK) failed");
    return false;
  }

  if (isatty(STDIN_FILENO)) {
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &mode->saved_termios) == 0) {
      raw = mode->saved_termios;
      mode->has_saved_termios = true;
      raw.c_iflag &= (tcflag_t) ~(ICRNL | IXON);
      raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        perror("[WARN] stdin tcsetattr(raw) failed");
      }
    } else {
      perror("[WARN] stdin tcgetattr failed");
    }
  }

  mode->enabled = true;
  return true;
}

static void rv32emu_restore_stdin_mode(const stdin_mode_t *mode) {
  if (mode == NULL || !mode->enabled) {
    return;
  }

  if (mode->has_saved_termios) {
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &mode->saved_termios);
  }
  if (mode->has_saved_flags) {
    (void)fcntl(STDIN_FILENO, F_SETFL, mode->saved_flags);
  }
}

static void rv32emu_pump_stdin_to_uart(rv32emu_machine_t *m) {
  ssize_t nread;
  uint8_t ch;

  if (m == NULL) {
    return;
  }

  for (;;) {
    if (m->plat.uart_rx_count >= RV32EMU_UART_RX_FIFO_SIZE) {
      return;
    }

    nread = read(STDIN_FILENO, &ch, 1);
    if (nread == 1) {
      (void)rv32emu_uart_push_rx(m, ch);
      continue;
    }
    if (nread == 0) {
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    return;
  }
}

static uint64_t rv32emu_run_interactive(rv32emu_machine_t *m, uint64_t max_instructions) {
  uint64_t executed = 0;

  while (m->cpu.running && executed < max_instructions) {
    uint64_t remaining = max_instructions - executed;
    uint64_t slice = remaining > RV32EMU_RUN_SLICE_INSTR ? RV32EMU_RUN_SLICE_INSTR : remaining;
    int delta;

    rv32emu_pump_stdin_to_uart(m);
    delta = rv32emu_run(m, slice);
    if (delta < 0) {
      break;
    }

    executed += (uint64_t)delta;
    rv32emu_pump_stdin_to_uart(m);
  }

  return executed;
}

int main(int argc, char **argv) {
  cli_options_t cli;
  rv32emu_machine_t m;
  rv32emu_options_t opts;
  uint32_t opensbi_entry = 0;
  uint32_t kernel_entry = 0;
  bool opensbi_entry_valid = false;
  bool kernel_entry_valid = false;
  uint32_t initrd_size = 0;
  uint64_t executed = 0;
  uint32_t mcause;
  uint32_t mtval;
  uint32_t mepc;
  stdin_mode_t stdin_mode;

  if (!parse_args(argc, argv, &cli)) {
    usage(stderr, argv[0]);
    return 2;
  }

  rv32emu_default_options(&opts);
  opts.ram_mb = cli.memory_mb;
  opts.trace = cli.trace;
  opts.enable_sbi_shim = false;
  opts.boot_s_mode = cli.boot_s_mode;
  opts.max_instructions = cli.max_instructions;
  opts.kernel_load_addr = cli.kernel_load_addr;
  opts.dtb_load_addr = cli.dtb_load_addr;
  opts.initrd_load_addr = cli.initrd_load_addr;

  if (!rv32emu_platform_init(&m, &opts)) {
    fprintf(stderr, "[ERR] platform init failed\n");
    return 1;
  }

  if (!rv32emu_load_image_auto(&m, cli.opensbi_path, cli.opensbi_load_addr, &opensbi_entry,
                               &opensbi_entry_valid)) {
    fprintf(stderr, "[ERR] failed to load OpenSBI: %s\n", cli.opensbi_path);
    rv32emu_platform_destroy(&m);
    return 1;
  }
  if (!opensbi_entry_valid) {
    opensbi_entry = cli.opensbi_load_addr;
  }

  if (!rv32emu_load_image_auto(&m, cli.kernel_path, cli.kernel_load_addr, &kernel_entry,
                               &kernel_entry_valid)) {
    fprintf(stderr, "[ERR] failed to load kernel: %s\n", cli.kernel_path);
    rv32emu_platform_destroy(&m);
    return 1;
  }
  if (!kernel_entry_valid) {
    kernel_entry = cli.kernel_load_addr;
  }

  if (!rv32emu_load_raw(&m, cli.dtb_path, cli.dtb_load_addr, NULL)) {
    fprintf(stderr, "[ERR] failed to load dtb: %s\n", cli.dtb_path);
    rv32emu_platform_destroy(&m);
    return 1;
  }

  if (cli.initrd_path != NULL &&
      !rv32emu_load_raw(&m, cli.initrd_path, cli.initrd_load_addr, &initrd_size)) {
    fprintf(stderr, "[ERR] failed to load initrd: %s\n", cli.initrd_path);
    rv32emu_platform_destroy(&m);
    return 1;
  }

  if (cli.use_fw_dynamic) {
    if (!write_fw_dynamic_info(&m, cli.fw_dynamic_info_addr, kernel_entry, cli.boot_s_mode)) {
      fprintf(stderr, "[ERR] failed to write fw_dynamic_info @ 0x%08x\n",
              cli.fw_dynamic_info_addr);
      rv32emu_platform_destroy(&m);
      return 1;
    }
  }

  m.cpu.pc = opensbi_entry;
  m.cpu.priv = RV32EMU_PRIV_M;
  m.cpu.x[10] = 0u;               /* a0 = hartid */
  m.cpu.x[11] = cli.dtb_load_addr; /* a1 = dtb */
  m.cpu.x[12] = cli.use_fw_dynamic ? cli.fw_dynamic_info_addr : 0u; /* a2 */

  fprintf(stderr,
          "[INFO] rv32emu boot start\n"
          "[INFO] opensbi=%s @0x%08x entry=0x%08x\n"
          "[INFO] kernel=%s @0x%08x entry=0x%08x\n"
          "[INFO] dtb=%s @0x%08x\n",
          cli.opensbi_path, cli.opensbi_load_addr, opensbi_entry, cli.kernel_path,
          cli.kernel_load_addr, kernel_entry, cli.dtb_path, cli.dtb_load_addr);
  if (cli.initrd_path != NULL) {
    fprintf(stderr, "[INFO] initrd=%s @0x%08x size=%u\n", cli.initrd_path, cli.initrd_load_addr,
            initrd_size);
  }
  if (cli.use_fw_dynamic) {
    fprintf(stderr, "[INFO] fw_dynamic_info @0x%08x next=0x%08x mode=%s\n",
            cli.fw_dynamic_info_addr, kernel_entry, cli.boot_s_mode ? "S" : "M");
  } else {
    fprintf(stderr, "[INFO] fw_dynamic_info disabled\n");
  }

  if (!rv32emu_setup_stdin_mode(&stdin_mode, cli.interactive)) {
    fprintf(stderr, "[ERR] failed to configure interactive stdin mode\n");
    rv32emu_platform_destroy(&m);
    return 1;
  }

  if (cli.interactive) {
    executed = rv32emu_run_interactive(&m, cli.max_instructions);
  } else {
    executed = (uint64_t)rv32emu_run(&m, cli.max_instructions);
  }

  rv32emu_restore_stdin_mode(&stdin_mode);
  mcause = rv32emu_csr_read(&m, CSR_MCAUSE);
  mtval = rv32emu_csr_read(&m, CSR_MTVAL);
  mepc = rv32emu_csr_read(&m, CSR_MEPC);

  fprintf(stderr,
          "[INFO] rv32emu stop: executed=%" PRIu64 " running=%d pc=0x%08x priv=%u mcause=0x%08x "
          "mepc=0x%08x mtval=0x%08x\n",
          executed, m.cpu.running ? 1 : 0, m.cpu.pc, (unsigned)m.cpu.priv, mcause, mepc,
          mtval);

  rv32emu_platform_destroy(&m);

  if (!m.cpu.running && mcause != RV32EMU_EXC_BREAKPOINT) {
    return 2;
  }

  return 0;
}
