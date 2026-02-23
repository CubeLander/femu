# Memory, MMU, Trap, and MMIO (Draft)

## 1. Address Space Model

Key mapped regions (`include/rv32emu.h`):

1. DRAM: base `0x80000000`.
2. UART: `0x10000000`.
3. CLINT: `0x02000000`.
4. PLIC: `0x0c000000`.

Main access APIs:

1. `rv32emu_phys_read` / `rv32emu_phys_write`.
2. `rv32emu_virt_read` / `rv32emu_virt_write`.
3. `rv32emu_translate`.

## 2. DRAM Path

`src/rv32emu_memory_mmio.c` provides DRAM fast path and helper conversion routines:

1. Native little-endian load/store helpers for 1/2/4-byte accesses.
2. Atomic byte/halfword/word path for threaded execution mode.
3. LR/SC invalidation is coordinated in virtual write flow (`src/rv32emu_virt_trap.c`).

## 3. MMIO Split

Current MMIO implementation is split into modules:

1. `rv32emu_mmio_devices.c`: top-level dispatch + virtio MMIO stubs.
2. `rv32emu_mmio_uart_plic.c`: UART FIFO/register behavior and PLIC delivery.
3. `rv32emu_mmio_clint_timer.c`: CLINT registers and timer interrupt scheduling.

## 4. Timer Model

Timer behavior:

1. `mtime` is monotonic (incremented per retired instruction in current model).
2. Next deadline cache avoids per-instruction full scans when possible.
3. CLINT writes to `mtimecmp` trigger per-hart timer IRQ refresh.

## 5. MMU and Trap Flow

`src/rv32emu_virt_trap.c` handles:

1. Sv32 translation (`satp` mode checks and page-table walk).
2. Access rights and fault classification.
3. Exception and interrupt trap entry.
4. Delegation behavior across M/S/U privilege paths.

## 6. CSR Interaction

CSR behavior lives in `src/rv32emu_csr.c` and is consumed across execute/trap paths.

Important interactions:

1. Trap entry updates cause/tval/epc-like fields.
2. Interrupt-pending bits (`mip`) are updated via platform + trap logic.
3. `mret`/`sret` privilege restoration occurs in dedicated system execute module.

## 7. Known Gaps and Future Work

1. Virtio MMIO is currently placeholder-level.
2. Memory model is functional for coursework, not a full microarchitectural simulator.
3. TB/JIT invalidation against self-modifying code is not yet implemented.
