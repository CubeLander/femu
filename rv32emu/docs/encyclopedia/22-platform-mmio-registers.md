# Platform and MMIO Registers

This chapter documents the implemented platform MMIO behavior for UART, CLINT, PLIC, and the current VirtIO placeholder window.

## 1. MMIO Address Map and Dispatch

Base map (`include/rv32emu.h`):

1. UART: `0x10000000` .. `0x100000ff` (`RV32EMU_UART_SIZE = 0x100`).
2. CLINT: `0x02000000` .. `0x0200ffff` (`RV32EMU_CLINT_SIZE = 0x10000`).
3. PLIC: `0x0c000000` .. `0x0fffffff` (`RV32EMU_PLIC_SIZE = 0x04000000`).
4. VirtIO MMIO window (stub): `0x10001000` .. `0x10008fff` (8 slots, stride `0x1000`).

Dispatch flow:

1. `rv32emu_phys_read` / `rv32emu_phys_write` first tries DRAM range, then falls back to MMIO (`src/memory/rv32emu_memory_mmio.c`).
2. MMIO dispatch is centralized in `rv32emu_mmio_read_locked` / `rv32emu_mmio_write_locked` (`src/memory/rv32emu_mmio_devices.c`).
3. All MMIO handlers run under `m->plat.mmio_lock` from the physical access layer (`src/memory/rv32emu_memory_mmio.c`).

## 2. UART Register Behavior (NS16550-like subset)

Implementation: `src/memory/rv32emu_mmio_uart_plic.c`.

Access width:

1. UART accepts only `len == 1` or `len == 4`.
2. Register index uses `off & 0x7` (8-byte register bank mirrored across the `0x100` window).

Implemented offsets (relative to `RV32EMU_UART_BASE`):

1. `0x00` (`RBR` read): pop one byte from RX FIFO; returns `0` if FIFO empty.
2. `0x00` (`THR` write): emits one byte via `putchar` + `fflush(stdout)`.
3. `0x01` (`IER` read/write): interrupt enable bits are stored in `uart_regs[1]`.
4. `0x02` (`IIR` read): synthesized from pending state:
   `0x04` (RDI), else `0x02` (THRI), else `0x01` (no interrupt).
5. `0x02` (`FCR` write): stores byte; if bit `0x02` is set, RX FIFO is cleared.
6. `0x03` (`LCR` read/write): plain backing register.
7. `0x04` (`MCR` read/write): plain backing register.
8. `0x05` (`LSR` read): synthesized as `THRE|TEMT` plus `DR` when RX FIFO non-empty.
9. `0x06` (`MSR` read): always `0`.
10. `0x07` (`SCR` read/write): plain backing register.

Important bit definitions used by the model:

1. `IER[0]` (`RDI`) gates RX interrupt.
2. `IER[1]` (`THRI`) gates TX-empty interrupt.
3. `LSR[0]` (`DR`), `LSR[5]` (`THRE`), `LSR[6]` (`TEMT`).

FIFO and state:

1. RX FIFO is a software ring buffer, size `RV32EMU_UART_RX_FIFO_SIZE = 256` (`include/rv32emu.h`).
2. External input uses `rv32emu_uart_push_rx`, which enqueues a byte and re-evaluates IRQ state (`src/memory/rv32emu_mmio_uart_plic.c`).

## 3. CLINT Register Behavior

Implementation: `src/memory/rv32emu_mmio_clint_timer.c`.

Access width/alignment:

1. CLINT accepts only `len == 4`.
2. Misaligned 32-bit accesses to `msip`/`mtimecmp` subregions return `false`.

Offsets (relative to `RV32EMU_CLINT_BASE`):

1. `0x0000 + 4*hart` (`msip[hart]`): read/write LSB only (`data & 1`).
2. `0x4000 + 8*hart` (`mtimecmp[hart]` low 32).
3. `0x4004 + 8*hart` (`mtimecmp[hart]` high 32).
4. `0xbff8` (`mtime` low 32, atomic read/write).
5. `0xbffc` (`mtime` high 32, atomic read/write).

IRQ effects:

1. Writing `msip` sets/clears `MIP_MSIP` on target hart immediately.
2. If `msip` is set on a halted hart (`running == false`), CLINT write wakes it (`running = true`).
3. Timer compare uses `mtime >= mtimecmp[hart]`.
4. Without SBI shim: sets/clears `MIP_MTIP`.
5. With SBI shim (`opts.enable_sbi_shim`): sets/clears `MIP_STIP`, and always clears `MIP_MTIP`.

Timer stepping model:

1. `rv32emu_step_timer` calls `rv32emu_mmio_step_timer` (`src/memory/rv32emu_memory_mmio.c`).
2. `rv32emu_mmio_step_timer` increments `mtime` by 1 each call and refreshes IRQs only when `mtime` reaches cached `next_timer_deadline` (`src/memory/rv32emu_mmio_clint_timer.c`, `include/rv32emu.h`).

## 4. PLIC Register Behavior

Implementation: `src/memory/rv32emu_mmio_uart_plic.c`.

Context model:

1. `context_count = hart_count * 2` (M-context + S-context per hart).
2. Hard maximum contexts: `RV32EMU_MAX_PLIC_CONTEXTS = 8` (`RV32EMU_MAX_HARTS = 4`).

Access width:

1. PLIC accepts only `len == 4`.

Implemented offsets (relative to `RV32EMU_PLIC_BASE`):

1. `0x1000` (`pending`): read/write whole pending bitmap (`m->plat.plic_pending`).
2. `0x2000 + 0x80*context` (`enable[context]`): read/write enable bitmap.
3. `0x2000 + 0x80*context + nonzero offset`: reads `0`, writes ignored.
4. `0x200000 + 0x1000*context + 0x0` (`threshold`): read returns `0`; write accepted but has no effect.
5. `0x200000 + 0x1000*context + 0x4` (`claim/complete`):
   read claims one IRQ; write matching claimed ID completes it.

Claim policy:

1. Candidates are `pending & enable[context]`.
2. Claimed IRQ is the smallest set ID in range `1..31`.
3. On claim, bit is removed from `pending` and cached in `plic_claim[context]`.
4. If a claim is already outstanding for context, repeated reads return the same ID until completion write.

## 5. End-to-End IRQ Propagation Paths

UART to CPU external interrupt path:

1. RX path: `rv32emu_uart_push_rx` or `RBR`/`IER`/`FCR` write updates UART state (`src/memory/rv32emu_mmio_uart_plic.c`).
2. `rv32emu_uart_sync_irq` sets/clears `plic_pending` bit 10 (`UART_PLIC_IRQ = 10`).
3. `rv32emu_update_plic_irq_lines` computes `pending & enable` for each hart M-context/S-context.
4. CPU `mip` is updated: `MIP_MEIP` for M-context, `MIP_SEIP` for S-context.
5. Core interrupt take/trap entry then depends on CSR mask/delegation logic outside MMIO layer.

CLINT software/timer interrupt path:

1. `msip` write directly sets/clears `MIP_MSIP` on target hart.
2. `mtime` progression or `mtimecmp` writes call timer sync helpers.
3. Timer sync sets `MIP_MTIP` (or `MIP_STIP` under SBI shim) per hart.

## 6. VirtIO MMIO Placeholder and Other Gaps

Implementation: `src/memory/rv32emu_mmio_devices.c`.

Current behavior in `0x10001000..0x10008fff`:

1. Only 32-bit accesses are accepted.
2. `MAGIC_VALUE@0x000` returns `0x74726976`.
3. `VERSION@0x004` returns `2`.
4. `DEVICE_ID@0x008` returns `0` (no device attached).
5. `VENDOR_ID@0x00c` returns `0x554d4551`.
6. `STATUS@0x070` returns `0`.
7. Other reads return `0`; writes are accepted but ignored.

Known model limitations (code-truth):

1. No PLIC priority/threshold arbitration; threshold is effectively fixed at 0.
2. PLIC source space is effectively 32-bit bitmap (`irq 1..31` claimable).
3. UART register set is a small functional subset (no divisor latch model, no timing model).
4. VirtIO is a stub only; it advertises MMIO identity fields but no functional queue/device behavior.
