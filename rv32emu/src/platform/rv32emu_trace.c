#include "rv32emu.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
  FILE *fp;
  pthread_mutex_t lock;
  uint64_t seq;
  uint64_t max_events;
  uint32_t mask;
  bool flush_each;
  bool close_fp;
  bool limit_notified;
} rv32emu_trace_state_t;

static rv32emu_trace_state_t *rv32emu_trace_state(const rv32emu_machine_t *m) {
  if (m == NULL) {
    return NULL;
  }
  return (rv32emu_trace_state_t *)m->plat.trace_state;
}

static bool rv32emu_env_enabled(const char *name) {
  const char *v;

  if (name == NULL) {
    return false;
  }
  v = getenv(name);
  return v != NULL && v[0] == '1';
}

static uint64_t rv32emu_env_u64(const char *name) {
  const char *v;
  char *end = NULL;
  unsigned long long parsed;

  if (name == NULL) {
    return 0u;
  }
  v = getenv(name);
  if (v == NULL || v[0] == '\0') {
    return 0u;
  }

  errno = 0;
  parsed = strtoull(v, &end, 0);
  if (errno != 0 || end == NULL || *end != '\0') {
    return 0u;
  }
  return (uint64_t)parsed;
}

static char rv32emu_trace_priv_char(rv32emu_priv_t priv) {
  switch (priv) {
  case RV32EMU_PRIV_M:
    return 'M';
  case RV32EMU_PRIV_S:
    return 'S';
  case RV32EMU_PRIV_U:
    return 'U';
  default:
    return '?';
  }
}

static uint32_t rv32emu_trace_default_mask(const rv32emu_machine_t *m) {
  uint32_t mask = RV32EMU_TRACE_EVT_SYSCALL | RV32EMU_TRACE_EVT_TRAP;

  if (m != NULL && m->opts.trace_mask != 0u) {
    mask = m->opts.trace_mask;
  }
  if (rv32emu_env_enabled("RV32EMU_TRACE_MMIO")) {
    mask |= RV32EMU_TRACE_EVT_MMIO;
  }
  if (rv32emu_env_enabled("RV32EMU_TRACE_ALL")) {
    mask = RV32EMU_TRACE_EVT_ALL;
  }
  return mask;
}

static bool rv32emu_trace_ensure_parent_dirs(const char *path) {
  char buf[PATH_MAX];
  char *last_slash;
  char *p;
  size_t len;

  if (path == NULL || path[0] == '\0') {
    errno = EINVAL;
    return false;
  }

  len = strlen(path);
  if (len >= sizeof(buf)) {
    errno = ENAMETOOLONG;
    return false;
  }
  memcpy(buf, path, len + 1u);

  last_slash = strrchr(buf, '/');
  if (last_slash == NULL) {
    return true;
  }
  if (last_slash == buf) {
    return true;
  }
  *last_slash = '\0';

  for (p = buf; *p != '\0'; p++) {
    if (*p != '/') {
      continue;
    }
    if (p == buf) {
      continue;
    }
    *p = '\0';
    if (mkdir(buf, 0777) != 0 && errno != EEXIST) {
      *p = '/';
      return false;
    }
    *p = '/';
  }

  if (mkdir(buf, 0777) != 0 && errno != EEXIST) {
    return false;
  }
  return true;
}

static void rv32emu_trace_write(rv32emu_machine_t *m, uint32_t event_mask, const char *fmt, ...) {
  rv32emu_trace_state_t *state;
  rv32emu_cpu_t *cpu;
  uint32_t hartid = 0u;
  va_list ap;

  if (m == NULL || fmt == NULL || !m->opts.trace) {
    return;
  }

  state = rv32emu_trace_state(m);
  if (state == NULL || state->fp == NULL || (state->mask & event_mask) == 0u) {
    return;
  }

  cpu = RV32EMU_CPU(m);
  if (cpu != NULL) {
    hartid = cpu->csr[CSR_MHARTID];
  }

  if (pthread_mutex_lock(&state->lock) != 0) {
    return;
  }

  if (state->max_events != 0u && state->seq >= state->max_events) {
    if (!state->limit_notified) {
      fprintf(state->fp, "# trace_limit_reached max_events=%" PRIu64 "\n", state->max_events);
      if (state->flush_each) {
        fflush(state->fp);
      }
      state->limit_notified = true;
    }
    (void)pthread_mutex_unlock(&state->lock);
    return;
  }

  if (cpu != NULL) {
    fprintf(state->fp,
            "%" PRIu64 " cycle=%" PRIu64 " instret=%" PRIu64 " hart=%u pc=0x%08x priv=%c ",
            state->seq++, cpu->cycle, cpu->instret, hartid, cpu->pc,
            rv32emu_trace_priv_char(cpu->priv));
  } else {
    fprintf(state->fp, "%" PRIu64 " cycle=0 instret=0 hart=0 pc=0x00000000 priv=? ",
            state->seq++);
  }

  va_start(ap, fmt);
  vfprintf(state->fp, fmt, ap);
  va_end(ap);
  fputc('\n', state->fp);

  if (state->flush_each) {
    fflush(state->fp);
  }
  (void)pthread_mutex_unlock(&state->lock);
}

bool rv32emu_trace_init(rv32emu_machine_t *m) {
  rv32emu_trace_state_t *state;
  const char *path;
  uint32_t mask;

  if (m == NULL) {
    return false;
  }
  m->plat.trace_state = NULL;
  if (!m->opts.trace) {
    return true;
  }

  state = calloc(1u, sizeof(*state));
  if (state == NULL) {
    fprintf(stderr, "[WARN] trace init failed: no memory\n");
    return false;
  }

  mask = rv32emu_trace_default_mask(m);
  state->mask = mask;
  state->flush_each = rv32emu_env_enabled("RV32EMU_TRACE_FLUSH");
  state->max_events = rv32emu_env_u64("RV32EMU_TRACE_MAX_EVENTS");
  path = m->opts.trace_path;
  if (path == NULL || path[0] == '\0') {
    path = getenv("RV32EMU_TRACE_FILE");
  }
  if (path == NULL || path[0] == '\0') {
    path = "rv32emu-trace.log";
  }

  if (strcmp(path, "-") == 0) {
    state->fp = stderr;
    state->close_fp = false;
  } else {
    if (!rv32emu_trace_ensure_parent_dirs(path)) {
      fprintf(stderr, "[WARN] trace init failed: cannot prepare parent dir for %s (%s)\n", path,
              strerror(errno));
      free(state);
      return false;
    }
    state->fp = fopen(path, "w");
    if (state->fp == NULL) {
      fprintf(stderr, "[WARN] trace init failed: cannot open %s (%s)\n", path, strerror(errno));
      free(state);
      return false;
    }
    state->close_fp = true;
  }

  if (pthread_mutex_init(&state->lock, NULL) != 0) {
    fprintf(stderr, "[WARN] trace init failed: mutex init\n");
    if (state->close_fp) {
      (void)fclose(state->fp);
    }
    free(state);
    return false;
  }

  (void)setvbuf(state->fp, NULL, _IOLBF, 0);
  m->plat.trace_state = state;
  rv32emu_trace_write(m, RV32EMU_TRACE_EVT_TRAP, "event=trace_start mask=0x%x path=%s", mask,
                      path);
  return true;
}

void rv32emu_trace_close(rv32emu_machine_t *m) {
  rv32emu_trace_state_t *state;
  FILE *fp;
  bool close_fp;

  if (m == NULL) {
    return;
  }

  state = rv32emu_trace_state(m);
  if (state == NULL) {
    return;
  }
  fp = state->fp;
  close_fp = state->close_fp;

  if (pthread_mutex_lock(&state->lock) == 0) {
    if (fp != NULL) {
      fprintf(fp, "# trace_stop seq=%" PRIu64 "\n", state->seq);
      fflush(fp);
    }
    (void)pthread_mutex_unlock(&state->lock);
  }

  (void)pthread_mutex_destroy(&state->lock);
  if (close_fp && fp != NULL) {
    (void)fclose(fp);
  }
  free(state);
  m->plat.trace_state = NULL;
}

bool rv32emu_trace_event_enabled(const rv32emu_machine_t *m, uint32_t event_mask) {
  rv32emu_trace_state_t *state = rv32emu_trace_state(m);

  if (m == NULL || !m->opts.trace || state == NULL) {
    return false;
  }
  return (state->mask & event_mask) != 0u;
}

void rv32emu_trace_syscall(rv32emu_machine_t *m, uint32_t cause, uint32_t a7, uint32_t a6,
                           uint32_t a0_in, uint32_t a1_in, uint32_t a2_in, uint32_t a0_out,
                           uint32_t a1_out, bool handled) {
  rv32emu_trace_write(
      m, RV32EMU_TRACE_EVT_SYSCALL,
      "event=syscall cause=%u a7=0x%08x a6=0x%08x a0_in=0x%08x a1_in=0x%08x a2_in=0x%08x "
      "a0_out=0x%08x a1_out=0x%08x handled=%u",
      cause, a7, a6, a0_in, a1_in, a2_in, a0_out, a1_out, handled ? 1u : 0u);
}

void rv32emu_trace_trap(rv32emu_machine_t *m, uint32_t from_pc, rv32emu_priv_t from_priv,
                        rv32emu_priv_t to_priv, uint32_t cause, uint32_t tval, bool is_interrupt,
                        bool delegated_to_s, uint32_t target_pc) {
  rv32emu_trace_write(
      m, RV32EMU_TRACE_EVT_TRAP,
      "event=%s cause=%u tval=0x%08x from_pc=0x%08x from_priv=%c to_priv=%c delegated=%u "
      "target_pc=0x%08x",
      is_interrupt ? "interrupt" : "exception", cause, tval, from_pc,
      rv32emu_trace_priv_char(from_priv), rv32emu_trace_priv_char(to_priv),
      delegated_to_s ? 1u : 0u, target_pc);
}

void rv32emu_trace_mmio(rv32emu_machine_t *m, bool is_write, uint32_t paddr, int len,
                        uint32_t value, bool ok) {
  rv32emu_trace_write(m, RV32EMU_TRACE_EVT_MMIO, "event=mmio_%s addr=0x%08x len=%d value=0x%08x "
                                                 "ok=%u",
                      is_write ? "write" : "read", paddr, len, value, ok ? 1u : 0u);
}
