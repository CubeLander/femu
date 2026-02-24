# Parallel Agent Work Packages (Draft)

This file defines independent documentation tasks that can be executed in parallel by different agents.

## Package A: CPU ISA Semantics Chapter

Scope:

1. Expand `docs/encyclopedia/10-cpu-execution.md` into per-group subsections.
2. Add per-opcode semantic tables for currently implemented RV32I/M/A/F/C instructions.

Primary source files:

1. `src/cpu/rv32emu_cpu_exec.c`
2. `src/cpu/rv32emu_cpu_exec_system.c`
3. `src/cpu/rv32emu_decode.c`

Deliverables:

1. `docs/encyclopedia/11-cpu-isa-semantics.md`
2. Cross-links back to `10-cpu-execution.md`

Acceptance criteria:

1. Every listed opcode maps to implemented function path.
2. Illegal-instruction behavior is documented.
3. Privilege-sensitive behavior is clearly separated.

## Package B: MMU/Trap Deep Dive

Scope:

1. Explain translation walk and permission checks in detail.
2. Document trap delegation and cause/tval update rules.

Primary source files:

1. `src/cpu/rv32emu_virt_trap.c`
2. `src/cpu/rv32emu_csr.c`
3. `include/rv32emu.h`

Deliverables:

1. `docs/encyclopedia/21-mmu-trap-deep-dive.md`

Acceptance criteria:

1. Includes control-flow diagrams (markdown text diagrams are acceptable).
2. Enumerates all exception/interrupt codes used by implementation.
3. Highlights any simplifications vs full privileged spec.

## Package C: Platform and MMIO Chapter

Scope:

1. Expand memory and device behavior with register-level details.
2. Clarify UART/CLINT/PLIC model assumptions and limitations.

Primary source files:

1. `src/memory/rv32emu_memory_mmio.c`
2. `src/memory/rv32emu_mmio_devices.c`
3. `src/memory/rv32emu_mmio_uart_plic.c`
4. `src/memory/rv32emu_mmio_clint_timer.c`

Deliverables:

1. `docs/encyclopedia/22-platform-mmio-registers.md`

Acceptance criteria:

1. Register offsets and behavior are listed.
2. IRQ line propagation path is explained end to end.
3. Known gaps (e.g., virtio stubs) are explicit.

## Package D: Test Matrix and Evidence

Scope:

1. Convert current tests into a feature coverage matrix.
2. Propose missing tests and priorities.

Primary source files:

1. `tests/test_platform.c`
2. `tests/test_run.c`
3. `tests/test_system.c`
4. `tests/test_loader.c`

Deliverables:

1. `docs/testing/31-coverage-matrix.md`
2. `docs/testing/32-gap-analysis.md`

Acceptance criteria:

1. Each test links to feature claims.
2. Missing coverage is categorized by risk.
3. Suggested additions include concrete test names.

## Package E: TB/JIT Architecture Roadmap

Scope:

1. Document current decoded-TB design.
2. Define staged path to machine-code JIT backend.

Primary source files:

1. `src/tb/rv32emu_tb.c`
2. `include/rv32emu_tb.h`
3. `src/cpu/rv32emu_cpu_run.c`

Deliverables:

1. `docs/plan/41-tb-jit-roadmap.md`

Acceptance criteria:

1. Defines correctness invariants and side-exit rules.
2. Lists unsupported instruction classes and fallback policy.
3. Includes measurable milestones for each stage.

## Merge Discipline for Parallel Work

1. Each agent only edits its assigned target docs.
2. Shared files (`00-overview.md`, `30-test-strategy.md`) are edited only in dedicated consolidation pass.
3. Every package submission must include a short "Assumptions" section.
