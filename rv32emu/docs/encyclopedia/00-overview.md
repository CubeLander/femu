# RV32 Emulator Encyclopedia (Draft v0)

## 1. Project Positioning

`rv32emu` is an experimental RV32 emulator used for architecture coursework. It currently targets:

1. Readable architecture-first implementation.
2. Correctness over peak speed.
3. Incremental performance path via decode cache / translation-block (TB) mechanisms.

This draft is the first pass of a "living encyclopedia" for the codebase. It is intended to be expanded by multiple agents in parallel.

## 2. Codebase Map

Core headers:

1. `include/rv32emu.h`: machine model, constants, CSR IDs, public interfaces.
2. `include/rv32emu_decode.h`: 32-bit decode structure and decode API.
3. `include/rv32emu_tb.h`: translation-block cache structures and APIs.

Execution and CPU:

1. `src/rv32emu_cpu_exec.c`: instruction semantics and grouped execute paths.
2. `src/rv32emu_cpu_exec_system.c`: CSR/system-return behavior (`mret`/`sret`).
3. `src/rv32emu_cpu_run.c`: single-thread and threaded run loops.
4. `src/rv32emu_decode.c`: decode stage (`rv32emu_decode32`).
5. `src/rv32emu_tb.c`: TB build/lookup and decoded-block step execution.

Memory and platform:

1. `src/rv32emu_memory_mmio.c`: DRAM fast/atomic paths + MMIO dispatch entry.
2. `src/rv32emu_mmio_devices.c`: top MMIO dispatcher + virtio placeholders.
3. `src/rv32emu_mmio_uart_plic.c`: UART/PLIC behavior.
4. `src/rv32emu_mmio_clint_timer.c`: CLINT/timer behavior.
5. `src/rv32emu_platform.c`: machine init/destroy and defaults.

Trap/MMU/SBI/Loader:

1. `src/rv32emu_virt_trap.c`: address translation, trap routing, interrupt handling.
2. `src/rv32emu_csr.c`: CSR read/write semantics.
3. `src/rv32emu_stubs.c`: SBI handlers + image loading (`raw`, `ELF32`, `auto`).

Tests:

1. `tests/test_platform.c`
2. `tests/test_run.c`
3. `tests/test_system.c`
4. `tests/test_loader.c`

## 3. Current Execution Modes

The emulator currently supports three conceptual execution levels:

1. Pure interpretive step (`rv32emu_exec_one`).
2. Decode-structured interpretive execution (`rv32emu_exec_decoded`).
3. Experimental TB/JIT path:
   - decoded-TB stepping (`RV32EMU_EXPERIMENTAL_TB=1`)
   - x86_64 machine-code JIT prefix execution (`RV32EMU_EXPERIMENTAL_JIT=1`)
   - hotness threshold (`RV32EMU_EXPERIMENTAL_JIT_HOT`)
   - direct tail-jump JIT block chaining for compatible successor blocks

## 4. Design Principles for the Encyclopedia

All future documentation should follow these rules:

1. Separate ISA semantics from implementation shortcuts.
2. Explicitly annotate where behavior is architectural vs emulator-specific.
3. Track test evidence for each behavior claim.
4. Highlight known gaps and future work (e.g., machine-code JIT backend).

## 5. Next Documentation Deliverables

1. CPU pipeline and instruction-class chapters.
2. Memory hierarchy and MMIO model chapter.
3. MMU/trap/delegation chapter.
4. Testing strategy + coverage matrix + known blind spots.
5. TB/JIT roadmap and correctness contracts.
